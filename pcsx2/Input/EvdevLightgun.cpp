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
#include <cctype>
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
		// One slot per player: slot N feeds pointer index N. Names are the virtual mice
		// sinden-smoother.py creates ("SindenLightgun Mouse (Smoothed P1/P2)").
		constexpr u32 NUM_SLOTS = 2; // == InputManager::MAX_POINTER_DEVICES
		constexpr const char* SLOT_NAME[NUM_SLOTS] = {"Smoothed P1", "Smoothed P2"};
		// Frames between open retries while enabled but not all open (the smoother may
		// create the virtual devices slightly after the emulator launches).
		constexpr unsigned REDISCOVER_INTERVAL = 300;

		struct Slot
		{
			int fd = -1;
			int min_x = 0, max_x = 32767;
			int min_y = 0, max_y = 32767;
			int last_x = -1, last_y = -1;
			unsigned last_buttons = 0; // bit0=Left, bit1=Right, bit2=Middle
			bool from_fallback = false; // opened by the generic-Sinden fallback, not a SLOT_NAME match
		};

		bool s_env_checked = false;
		bool s_enabled = false;
		bool s_path_mode = false; // explicit /dev/input path → slot 0 only
		std::string s_path;

		Slot s_slots[NUM_SLOTS];
		// Read on the Qt UI thread via IsActive()/IsMultiGun(); mutated on the VM/CPU thread.
		// Atomic to avoid a formal cross-thread data race.
		std::atomic<int> s_open_count{0};
		unsigned s_retry_counter = 0;
		// Bounded discovery: rescan /dev/input only during a grace window after launch (or
		// after a disconnect, which resets s_retry_counter) so a slightly-late 2nd gun is
		// still caught, but a permanent single-gun session doesn't rescan forever.
		constexpr unsigned DISCOVERY_GRACE_FRAMES = REDISCOVER_INTERVAL * 4;

		// evdev BTN_* → pointer button index (matches s_pointer_button_setting_names
		// {LeftButton, RightButton, MiddleButton}). -1 = not a button we forward.
		int buttonIndex(int code)
		{
			switch (code)
			{
				case BTN_LEFT:
					return 0;
				case BTN_RIGHT:
					return 1;
				case BTN_MIDDLE:
					return 2;
				default:
					return -1;
			}
		}

		// True if `name` contains "sinden" (case-insensitive) -- a stock/raw Sinden lightgun.
		// The smoother's virtual mice also contain it, but those are claimed by the SLOT_NAME
		// pass first; this is the single-gun fallback used only when no smoother is present.
		bool nameHasSinden(std::string_view nv)
		{
			constexpr std::string_view needle = "sinden";
			if (nv.size() < needle.size())
				return false;
			for (size_t i = 0; i + needle.size() <= nv.size(); i++)
			{
				size_t j = 0;
				for (; j < needle.size(); j++)
				{
					if (std::tolower(static_cast<unsigned char>(nv[i + j])) != needle[j])
						break;
				}
				if (j == needle.size())
					return true;
			}
			return false;
		}

		void closeSlot(Slot& s)
		{
			if (s.fd >= 0)
			{
				close(s.fd);
				s_open_count.fetch_sub(1, std::memory_order_relaxed);
			}
			s.fd = -1;
			s.last_x = -1;
			s.last_y = -1;
			s.last_buttons = 0;
			s.from_fallback = false;
		}

		// Read an ABS axis range; false if the device lacks the axis or it is degenerate.
		bool readAbs(int fd, int axis, int& mn, int& mx, int& cur)
		{
			input_absinfo ai = {};
			if (ioctl(fd, EVIOCGABS(axis), &ai) < 0)
				return false;
			mn = ai.minimum;
			mx = ai.maximum;
			cur = ai.value;
			return mx > mn;
		}

		// Open `path` into slot `idx`; false if it can't be opened or lacks ABS_X/Y.
		bool openInto(u32 idx, const char* path)
		{
			const int fd = open(path, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
			if (fd < 0)
				return false;

			int mnx, mxx, mny, mxy, curx = -1, cury = -1;
			if (!readAbs(fd, ABS_X, mnx, mxx, curx) || !readAbs(fd, ABS_Y, mny, mxy, cury))
			{
				// Not an absolute pointing device -- ignore.
				close(fd);
				return false;
			}

			Slot& s = s_slots[idx];
			s.fd = fd;
			s.min_x = mnx;
			s.max_x = mxx;
			s.min_y = mny;
			s.max_y = mxy;
			// Prime last_x/last_y from the device's CURRENT absolute position (EVIOCGABS value)
			// so the pointer is fed on the first event for EITHER axis, instead of being stranded
			// at (0,0) until BOTH axes have each emitted an event. A lightgun's Y can be late/sparse
			// (player sweeps horizontally first), which otherwise leaves that pointer (e.g. P2) dead
			// with no crosshair. Clamp into range in case the driver reports a stale value.
			s.last_x = (curx >= mnx && curx <= mxx) ? curx : mnx;
			s.last_y = (cury >= mny && cury <= mxy) ? cury : mny;
			s.last_buttons = 0;
			s_open_count.fetch_add(1, std::memory_order_relaxed);
			Console.WriteLn("EvdevLightgun: slot %u opened %s (ABS_X %d..%d, ABS_Y %d..%d)", idx, path, mnx, mxx, mny, mxy);
			return true;
		}

		// True if `path` is an absolute pointing device (ABS_X + ABS_Y), checked WITHOUT taking a slot.
		// Lets an upgrade confirm a "Smoothed PN" candidate is valid before dropping a working fallback.
		bool probeAbs(const char* path)
		{
			const int fd = open(path, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
			if (fd < 0)
				return false;
			int mn, mx, cur;
			const bool ok = readAbs(fd, ABS_X, mn, mx, cur) && readAbs(fd, ABS_Y, mn, mx, cur);
			close(fd);
			return ok;
		}

		// Open any not-yet-open slots. Path mode → slot 0 = s_path; name mode → scan
		// /dev/input/event*: the smoother's "Smoothed P1/P2" mice fill the slots, else a stock
		// Sinden ("sinden" in the device name) fills slot 0 as a single-gun fallback. A late
		// "Smoothed PN" device upgrades a fallback-filled slot, so the smoother always wins.
		void discover()
		{
			if (s_path_mode)
			{
				if (s_slots[0].fd < 0)
					openInto(0, s_path.c_str());
				return;
			}

			DIR* dir = opendir("/dev/input");
			if (!dir)
				return;

			// Fallback candidate: a stock/raw Sinden ("sinden" in its name) for when the smoother's
			// "Smoothed P1/P2" virtual mice aren't present (a new user with no smoother). Stashed,
			// not opened, so the smoother's named devices -- if they appear anywhere in this scan --
			// always win the slots. Single gun only (slot 0).
			char fallback_path[320] = {};

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
				if (!got_name)
					continue;

				const std::string_view nv(name);
				bool matched_named = false;
				for (u32 idx = 0; idx < NUM_SLOTS; idx++)
				{
					if (nv.find(SLOT_NAME[idx]) == std::string_view::npos)
						continue;
					matched_named = true; // a smoother device -> never stashed as a fallback
					if (s_slots[idx].fd >= 0 && !s_slots[idx].from_fallback)
						continue; // slot already held by a smoother device
					if (s_slots[idx].fd >= 0)
					{
						if (!probeAbs(path))
							continue; // don't drop a working fallback for an invalid candidate
						closeSlot(s_slots[idx]); // upgrade: drop the fallback, take the smoother device
					}
					if (openInto(idx, path))
						s_slots[idx].from_fallback = false;
				}

				// Remember the first stock Sinden for the single-gun fallback below. Skip devices
				// we just claimed via SLOT_NAME (the smoother's own mice also contain "sinden").
				if (!matched_named && fallback_path[0] == '\0' && nameHasSinden(nv))
					std::snprintf(fallback_path, sizeof(fallback_path), "%s", path);
			}

			closedir(dir);

			// No smoother device claimed slot 0 -> open the stock Sinden as a single gun. openInto()
			// rejects anything without ABS_X/Y, so a touchscreen/keyboard that slipped the name
			// filter still cannot be used as a gun. Flag it so a late "Smoothed P1" can upgrade it.
			if (s_slots[0].fd < 0 && fallback_path[0] != '\0' && openInto(0, fallback_path))
			{
				s_slots[0].from_fallback = true;
				// Restart the rediscovery window on this no-fallback -> fallback transition, so a late
				// "Smoothed P1" still gets a full grace window of discover() ticks to upgrade the slot.
				// Without this the upgrade reuses the launch-time budget and can land in its dead tail
				// (or after it expires), leaving P1 stranded on the raw fallback. Bounded: re-armed only
				// when the fallback is (re)grabbed into an empty slot 0, never every frame.
				s_retry_counter = 0;
			}
		}

		void initFromEnv()
		{
			s_env_checked = true;
			const char* env = std::getenv("PCSX2_EVDEV_LIGHTGUN");
			if (!env || env[0] == '\0')
				return; // disabled

			s_enabled = true;
			const std::string_view v(env);
			if (v.rfind("/dev/", 0) == 0)
			{
				s_path_mode = true;
				s_path.assign(v);
			}
			Console.WriteLn("EvdevLightgun: enabled (%s)", s_path_mode ? s_path.c_str() : "auto, by smoother name");
		}

		// Drain one slot's pending events; emit button changes + the latest position for pointer `idx`.
		void pollSlot(u32 idx)
		{
			Slot& s = s_slots[idx];
			input_event ev[32];
			bool pos_changed = false;
			for (;;)
			{
				const ssize_t n = read(s.fd, ev, sizeof(ev));
				if (n <= 0)
				{
					if (n < 0 && (errno == ENODEV || errno == EBADF))
					{
						Console.WriteLn("EvdevLightgun: slot %u disconnected", idx);
						closeSlot(s);
						s_retry_counter = 0; // re-arm immediate rediscovery
						return;
					}
					break; // EAGAIN / drained
				}

				const size_t count = static_cast<size_t>(n) / sizeof(input_event);
				for (size_t i = 0; i < count; i++)
				{
					if (ev[i].type == EV_ABS)
					{
						if (ev[i].code == ABS_X)
						{
							s.last_x = ev[i].value;
							pos_changed = true;
						}
						else if (ev[i].code == ABS_Y)
						{
							s.last_y = ev[i].value;
							pos_changed = true;
						}
					}
					else if (ev[i].type == EV_KEY)
					{
						const int bi = buttonIndex(ev[i].code);
						if (bi < 0)
							continue;
						const unsigned mask = 1u << bi;
						const bool pressed = (ev[i].value != 0);
						if (pressed != ((s.last_buttons & mask) != 0))
						{
							if (pressed)
								s.last_buttons |= mask;
							else
								s.last_buttons &= ~mask;
							// Only emit per-device buttons when a 2nd gun is present. With one gun the
							// validated single-player path keeps delivering the trigger via the Qt
							// (compositor) Pointer-0 click, so don't change that behaviour.
							if (s_open_count.load(std::memory_order_acquire) >= 2)
							{
								// Emit this gun's button as its own Pointer-N (e.g. P2 trigger = Pointer-1/LeftButton).
								InputManager::InvokeEvents(
									InputManager::MakePointerButtonKey(idx, static_cast<u32>(bi)), pressed ? 1.0f : 0.0f);
							}
						}
					}
				}
			}

			if (!pos_changed || s.fd < 0 || s.last_x < 0 || s.last_y < 0)
				return;

			// Map absolute device coords -> window pixels (GSTranslateWindowToDisplayCoordinates
			// handles draw_rect/letterbox downstream; in fullscreen the window spans the screen).
			GSDevice* const dev = g_gs_device.get();
			if (!dev)
				return;
			const int ww = dev->GetWindowWidth();
			const int wh = dev->GetWindowHeight();
			if (ww <= 0 || wh <= 0)
				return;

			const float fx = std::clamp(
				static_cast<float>(s.last_x - s.min_x) / static_cast<float>(s.max_x - s.min_x), 0.0f, 1.0f);
			const float fy = std::clamp(
				static_cast<float>(s.last_y - s.min_y) / static_cast<float>(s.max_y - s.min_y), 0.0f, 1.0f);
			InputManager::UpdatePointerAbsolutePosition(idx, fx * static_cast<float>(ww), fy * static_cast<float>(wh));
		}
	} // namespace

	void Poll()
	{
		if (!s_env_checked)
			initFromEnv();
		if (!s_enabled)
			return;

		// (Re)discover missing slots on the first frame then once per REDISCOVER_INTERVAL, but only
		// within the bounded grace window (avoids a permanent single-gun rescan-storm). Also keep
		// rediscovering while a slot still holds the generic-Sinden FALLBACK, so a late "Smoothed PN"
		// smoother device can upgrade it even once s_open_count has already reached `want`.
		const int want = s_path_mode ? 1 : static_cast<int>(NUM_SLOTS);
		bool want_rediscover = s_open_count.load(std::memory_order_relaxed) < want;
		if (!want_rediscover && !s_path_mode)
		{
			for (u32 i = 0; i < NUM_SLOTS; i++)
			{
				if (s_slots[i].fd >= 0 && s_slots[i].from_fallback)
				{
					want_rediscover = true;
					break;
				}
			}
		}
		if (want_rediscover && s_retry_counter < DISCOVERY_GRACE_FRAMES)
		{
			if ((s_retry_counter++ % REDISCOVER_INTERVAL) == 0)
				discover();
		}

		for (u32 idx = 0; idx < NUM_SLOTS; idx++)
		{
			if (s_slots[idx].fd >= 0)
				pollSlot(idx);
		}
	}

	bool IsActive()
	{
		return s_open_count.load(std::memory_order_acquire) > 0;
	}

	bool IsMultiGun()
	{
		return s_open_count.load(std::memory_order_acquire) >= 2;
	}

	bool SlotActive(u32 idx)
	{
		// Queried from ACJV on the same EE/CPU thread that runs Poll(), so a plain fd read is fine.
		return idx < NUM_SLOTS && s_slots[idx].fd >= 0;
	}

	void Shutdown()
	{
		for (u32 idx = 0; idx < NUM_SLOTS; idx++)
			closeSlot(s_slots[idx]);
		s_env_checked = false;
		s_enabled = false;
		s_path_mode = false;
		s_retry_counter = 0;
	}
} // namespace EvdevLightgun
