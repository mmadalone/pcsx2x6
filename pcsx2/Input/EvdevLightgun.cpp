// SPDX-FileCopyrightText: 2002-2026 PCSX2 Dev Team
// SPDX-License-Identifier: GPL-3.0+

#include "Input/EvdevLightgun.h"
#include "Input/InputManager.h"
#include "GS/Renderers/Common/GSDevice.h"

#include "common/Console.h"

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <string_view>

#include <cerrno>
#include <dirent.h>
#include <fcntl.h>
#include <linux/input.h>
#include <sys/ioctl.h>
#include <unistd.h>

namespace EvdevLightgun
{
	namespace
	{
		// Default smoothed P1 device created by sinden-smoother.py on this rig.
		constexpr const char* DEFAULT_DEVICE_NAME = "SindenLightgun Mouse (Smoothed P1)";
		// Frames between open retries while enabled but not yet open (the smoother
		// may start the virtual device slightly after the emulator).
		constexpr unsigned REDISCOVER_INTERVAL = 300;

		bool s_env_checked = false;
		bool s_enabled = false;
		bool s_match_is_path = false;
		std::string s_match;

		// Read on the Qt UI thread via IsActive(); written on the VM/CPU thread in
		// Poll()/closeDevice(). Atomic to avoid a formal cross-thread data race.
		std::atomic<int> s_fd{-1};
		int s_min_x = 0, s_max_x = 32767;
		int s_min_y = 0, s_max_y = 32767;
		int s_last_x = -1, s_last_y = -1;
		unsigned s_retry_counter = 0;

		void closeDevice()
		{
			if (s_fd >= 0)
				close(s_fd);
			s_fd = -1;
			s_last_x = -1;
			s_last_y = -1;
			// Re-arm immediate rediscovery so a hot re-plug recovers next frame
			// (matches cold-start) instead of waiting out the retry interval.
			s_retry_counter = 0;
		}

		// Read an ABS axis range; false if the device lacks the axis or it is degenerate.
		bool readAbs(int fd, int axis, int& mn, int& mx)
		{
			input_absinfo ai = {};
			if (ioctl(fd, EVIOCGABS(axis), &ai) < 0)
				return false;
			mn = ai.minimum;
			mx = ai.maximum;
			return mx > mn;
		}

		bool openPath(const char* path)
		{
			const int fd = open(path, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
			if (fd < 0)
				return false;

			int mnx, mxx, mny, mxy;
			if (!readAbs(fd, ABS_X, mnx, mxx) || !readAbs(fd, ABS_Y, mny, mxy))
			{
				// Not an absolute pointing device (e.g. a relative mouse) -- ignore.
				close(fd);
				return false;
			}

			s_fd = fd;
			s_min_x = mnx;
			s_max_x = mxx;
			s_min_y = mny;
			s_max_y = mxy;
			s_last_x = -1;
			s_last_y = -1;
			Console.WriteLn("EvdevLightgun: opened %s (ABS_X %d..%d, ABS_Y %d..%d)", path, mnx, mxx, mny, mxy);
			return true;
		}

		// Scan /dev/input/event* for the first device whose name contains s_match.
		bool discoverByName()
		{
			DIR* dir = opendir("/dev/input");
			if (!dir)
				return false;

			bool opened = false;
			for (dirent* de = readdir(dir); de != nullptr; de = readdir(dir))
			{
				if (std::strncmp(de->d_name, "event", 5) != 0)
					continue;

				char path[320];
				std::snprintf(path, sizeof(path), "/dev/input/%s", de->d_name);

				const int fd = open(path, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
				if (fd < 0)
					continue;

				char name[256] = {};
				const bool got_name = ioctl(fd, EVIOCGNAME(sizeof(name) - 1), name) >= 0;
				close(fd);

				if (got_name && std::string_view(name).find(s_match) != std::string_view::npos)
				{
					if (openPath(path))
					{
						opened = true;
						break;
					}
				}
			}

			closedir(dir);
			return opened;
		}

		void initFromEnv()
		{
			s_env_checked = true;
			const char* env = std::getenv("PCSX2_EVDEV_LIGHTGUN");
			if (!env || env[0] == '\0')
				return; // disabled

			s_enabled = true;
			const std::string_view v(env);
			if (v == "1" || v == "auto")
			{
				s_match = DEFAULT_DEVICE_NAME;
				s_match_is_path = false;
			}
			else if (v.rfind("/dev/", 0) == 0)
			{
				s_match.assign(v);
				s_match_is_path = true;
			}
			else
			{
				s_match.assign(v);
				s_match_is_path = false;
			}
			Console.WriteLn("EvdevLightgun: enabled, match=\"%s\"%s", s_match.c_str(),
				s_match_is_path ? " (path)" : " (name)");
		}

		bool tryOpen()
		{
			return s_match_is_path ? openPath(s_match.c_str()) : discoverByName();
		}
	} // namespace

	void Poll()
	{
		if (!s_env_checked)
			initFromEnv();
		if (!s_enabled)
			return;

		if (s_fd < 0)
		{
			// Retry discovery on the first frame and then once every REDISCOVER_INTERVAL.
			if ((s_retry_counter++ % REDISCOVER_INTERVAL) != 0)
				return;
			if (!tryOpen())
				return;
		}

		// Drain everything pending; keep only the most recent ABS_X/ABS_Y.
		input_event ev[32];
		bool updated = false;
		for (;;)
		{
			const ssize_t n = read(s_fd, ev, sizeof(ev));
			if (n <= 0)
			{
				if (n < 0 && (errno == ENODEV || errno == EBADF))
				{
					Console.WriteLn("EvdevLightgun: device disconnected");
					closeDevice();
				}
				break; // EAGAIN / no more data
			}

			const size_t count = static_cast<size_t>(n) / sizeof(input_event);
			for (size_t i = 0; i < count; i++)
			{
				if (ev[i].type != EV_ABS)
					continue;
				if (ev[i].code == ABS_X)
				{
					s_last_x = ev[i].value;
					updated = true;
				}
				else if (ev[i].code == ABS_Y)
				{
					s_last_y = ev[i].value;
					updated = true;
				}
			}
		}

		if (!updated || s_fd < 0 || s_last_x < 0 || s_last_y < 0)
			return;

		// Map absolute device coords -> window pixels. GSTranslateWindowToDisplay-
		// Coordinates() handles the draw_rect/letterbox downstream, so we feed raw
		// window-pixel coordinates (in fullscreen the window spans the screen, which
		// is exactly the space the gun is calibrated to).
		GSDevice* const dev = g_gs_device.get();
		if (!dev)
			return;
		const int ww = dev->GetWindowWidth();
		const int wh = dev->GetWindowHeight();
		if (ww <= 0 || wh <= 0)
			return;

		const float nx = std::clamp(
			static_cast<float>(s_last_x - s_min_x) / static_cast<float>(s_max_x - s_min_x), 0.0f, 1.0f);
		const float ny = std::clamp(
			static_cast<float>(s_last_y - s_min_y) / static_cast<float>(s_max_y - s_min_y), 0.0f, 1.0f);

		InputManager::UpdatePointerAbsolutePosition(0, nx * static_cast<float>(ww), ny * static_cast<float>(wh));
	}

	bool IsActive()
	{
		return s_fd >= 0;
	}

	void Shutdown()
	{
		closeDevice();
		s_env_checked = false;
		s_enabled = false;
		s_retry_counter = 0;
	}
} // namespace EvdevLightgun
