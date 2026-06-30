// SPDX-FileCopyrightText: 2002-2026 PCSX2 Dev Team
// SPDX-License-Identifier: GPL-3.0+

#pragma once
#include "USB/deviceproxy.h"

namespace usb_lightgun
{
	class GunCon2Device : public DeviceProxy
	{
	public:
		USBDevice* CreateDevice(SettingsInterface& si, u32 port, u32 subtype) const override;
		const char* Name() const override;
		const char* TypeName() const override;
		const char* IconName() const override;
		bool Freeze(USBDevice* dev, StateWrapper& sw) const override;
		void UpdateSettings(USBDevice* dev, SettingsInterface& si) const override;
		float GetBindingValue(const USBDevice* dev, u32 bind_index) const override;
		void SetBindingValue(USBDevice* dev, u32 bind_index, float value) const override;
		std::span<const InputBindingInfo> Bindings(u32 subtype) const override;
		std::span<const SettingInfo> Settings(u32 subtype) const override;
	};

	// Retail GunCon2: shares the arcade "Light Gun" emulated device + runtime. It inherits
	// CreateDevice / UpdateSettings / Get+SetBindingValue / Settings / Freeze, all of which read config
	// via the virtual TypeName(), so overriding TypeName() is enough to give it its own ini namespace.
	// It exposes the FULL upstream GunCon2 binding set (Shoot Offscreen, Calibration Shot, B, C, ...) so
	// retail discs can pass their gun calibration screen. Registered as a separate "GunCon 2" entry in
	// the USB port device picker; the arcade GunCon2Device stays byte-for-byte unchanged.
	class GunCon2RetailDevice final : public GunCon2Device
	{
	public:
		const char* Name() const override;
		const char* TypeName() const override;
		std::span<const InputBindingInfo> Bindings(u32 subtype) const override;
	};
} // namespace usb_lightgun
