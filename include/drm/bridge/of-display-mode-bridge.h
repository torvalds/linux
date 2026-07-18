/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Copyright (C) 2012 Sascha Hauer, Pengutronix
 */

#ifndef DRM_OF_DISPLAY_MODE_BRIDGE_H
#define DRM_OF_DISPLAY_MODE_BRIDGE_H

struct device;
struct device_node;
struct drm_bridge;

struct drm_bridge *devm_drm_of_display_mode_bridge(struct device *dev,
						   struct device_node *np,
						   int type);

#endif
