/* SPDX-License-Identifier: MIT */
/*
 * Function prototypes for panel-related quirks.
 *
 * Copyright (C) 2017 Hans de Goede <hdegoede@redhat.com>
 */

#ifndef __DRM_PANEL_QUIRKS_H__
#define __DRM_PANEL_QUIRKS_H__

#include <linux/types.h>

struct drm_edid;

int drm_get_panel_orientation_quirk(int width, int height);

struct drm_panel_backlight_quirk {
	u16 min_brightness;
	u32 brightness_mask;
	bool force_pwm;
};

const struct drm_panel_backlight_quirk *
drm_get_panel_backlight_quirk(const struct drm_edid *edid);

#endif
