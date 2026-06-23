// SPDX-FileCopyrightText: 2002-2026 PCSX2 Dev Team
// SPDX-License-Identifier: GPL-3.0+

#pragma once

// Direct evdev absolute-pointer source for lightguns (e.g. Sinden) on Linux.
//
// Why this exists: under gamescope (Steam Deck Game Mode) the compositor's
// pointer pipeline is RELATIVE-ONLY -- libinput flattens an absolute USB
// "mouse" (the Sinden, ABS_X/ABS_Y 0..32767) to relative deltas before
// gamescope ever sees it, so the gun's absolute screen position never reaches
// the Qt window. The lightgun aim (DEV9/ACJV) and the ImGui crosshair both ride
// that single Qt cursor, so both break in Game Mode.
//
// This source reads the gun's ABS_X/ABS_Y straight off /dev/input and feeds
// InputManager::UpdatePointerAbsolutePosition(0, ...) directly, bypassing the
// compositor entirely -- the same approach RetroArch's udev mouse driver uses
// to make lightguns work in Game Mode on this hardware. It works identically in
// X11 and gamescope.
//
// Enabled only when the env var PCSX2_EVDEV_LIGHTGUN is set (the launcher sets
// it for lightgun games):
//   "1" / "auto"        -> auto-discover by device name (DEFAULT_DEVICE_NAME)
//   "/dev/input/eventN" -> use that node directly
//   "<name substring>"  -> match a device whose EVIOCGNAME contains the string
//
// Linux-only: compiled into pcsx2LinuxSources; callers must guard with
// #if defined(__linux__).

namespace EvdevLightgun
{
	// Polled once per frame from InputManager::PollSources(). Cheap no-op when
	// disabled. Drains pending ABS events and, on change, updates pointer 0's
	// absolute position in window-pixel space.
	void Poll();

	// True once a gun device is open and feeding pointer 0. Used to suppress the
	// Qt MouseMove absolute update so there is a single authoritative writer.
	bool IsActive();

	// Close the device (safe to call when never opened).
	void Shutdown();
} // namespace EvdevLightgun
