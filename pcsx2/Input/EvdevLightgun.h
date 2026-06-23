// SPDX-FileCopyrightText: 2002-2026 PCSX2 Dev Team
// SPDX-License-Identifier: GPL-3.0+

#pragma once

#include "common/Pcsx2Defs.h" // u32

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
	// Polled once per frame from InputManager::PollSources() on the VM/CPU thread. Cheap
	// no-op when disabled. Discovers up to two smoothed Sinden guns (P1->pointer 0,
	// P2->pointer 1) and feeds each one's absolute position -- and, when two guns are
	// present, its buttons -- directly, bypassing the compositor.
	void Poll();

	// True once at least one gun device is open. Suppresses the Qt MouseMove absolute
	// update so the evdev source is the single authoritative position writer.
	bool IsActive();

	// True once two gun devices are open (2-player). Gates per-device button emission and
	// suppression of the Qt (compositor) button, so single-player keeps its validated path.
	bool IsMultiGun();

	// True if slot idx (0=P1, 1=P2) currently has an open device. Queried by ACJV to avoid
	// a phantom second player when only one gun is connected.
	bool SlotActive(u32 idx);

	// Close all devices (safe to call when never opened).
	void Shutdown();
} // namespace EvdevLightgun
