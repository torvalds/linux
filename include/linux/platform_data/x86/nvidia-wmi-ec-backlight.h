/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2020, NVIDIA CORPORATION.  All rights reserved.
 */

#ifndef __PLATFORM_DATA_X86_NVIDIA_WMI_EC_BACKLIGHT_H
#define __PLATFORM_DATA_X86_NVIDIA_WMI_EC_BACKLIGHT_H

#define WMI_BRIGHTNESS_GUID "603E9613-EF25-4338-A3D0-C46177516DB7"

/**
 * enum wmi_brightness_method - WMI method IDs
 * @WMI_BRIGHTNESS_METHOD_LEVEL:  Get/Set EC brightness level status
 * @WMI_BRIGHTNESS_METHOD_SOURCE: Get/Set EC Brightness Source
 */
enum wmi_brightness_method {
	WMI_BRIGHTNESS_METHOD_LEVEL = 1,
	WMI_BRIGHTNESS_METHOD_SOURCE = 2,
	WMI_BRIGHTNESS_METHOD_MAX
};

/**
 * enum wmi_brightness_mode - Operation mode for WMI-wrapped method
 * @WMI_BRIGHTNESS_MODE_GET:            Get the current brightness level/source.
 * @WMI_BRIGHTNESS_MODE_SET:            Set the brightness level.
 * @WMI_BRIGHTNESS_MODE_GET_MAX_LEVEL:  Get the maximum brightness level. This
 *                                      is only valid when the WMI method is
 *                                      %WMI_BRIGHTNESS_METHOD_LEVEL.
 */
enum wmi_brightness_mode {
	WMI_BRIGHTNESS_MODE_GET = 0,
	WMI_BRIGHTNESS_MODE_SET = 1,
	WMI_BRIGHTNESS_MODE_GET_MAX_LEVEL = 2,
	WMI_BRIGHTNESS_MODE_MAX
};

/**
 * enum wmi_brightness_source - Backlight brightness control source selection
 * @WMI_BRIGHTNESS_SOURCE_GPU: Backlight brightness is controlled by the GPU.
 * @WMI_BRIGHTNESS_SOURCE_EC:  Backlight brightness is controlled by the
 *                             system's Embedded Controller (EC).
 * @WMI_BRIGHTNESS_SOURCE_AUX: Backlight brightness is controlled over the
 *                             DisplayPort AUX channel.
 */
enum wmi_brightness_source {
	WMI_BRIGHTNESS_SOURCE_GPU = 1,
	WMI_BRIGHTNESS_SOURCE_EC = 2,
	WMI_BRIGHTNESS_SOURCE_AUX = 3,
	WMI_BRIGHTNESS_SOURCE_MAX
};

/**
 * struct wmi_brightness_args - arguments for the WMI-wrapped ACPI method
 * @mode:    Pass in an &enum wmi_brightness_mode value to select between
 *           getting or setting a value.
 * @val:     In parameter for value to set when using %WMI_BRIGHTNESS_MODE_SET
 *           mode. Not used in conjunction with %WMI_BRIGHTNESS_MODE_GET or
 *           %WMI_BRIGHTNESS_MODE_GET_MAX_LEVEL mode.
 * @ret:     Out parameter returning retrieved value when operating in
 *           %WMI_BRIGHTNESS_MODE_GET or %WMI_BRIGHTNESS_MODE_GET_MAX_LEVEL
 *           mode. Not used in %WMI_BRIGHTNESS_MODE_SET mode.
 * @ignored: Padding; not used. The ACPI method expects a 24 byte params struct.
 *
 * This is the parameters structure for the WmiBrightnessNotify ACPI method as
 * wrapped by WMI. The value passed in to @val or returned by @ret will be a
 * brightness value when the WMI method ID is %WMI_BRIGHTNESS_METHOD_LEVEL, or
 * an &enum wmi_brightness_source value with %WMI_BRIGHTNESS_METHOD_SOURCE.
 */
struct wmi_brightness_args {
	u32 mode;
	u32 val;
	u32 ret;
	u32 ignored[3];
};

#endif
