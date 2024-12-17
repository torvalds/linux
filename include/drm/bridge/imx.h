// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (C) 2012 Sascha Hauer, Pengutronix
 */

#ifndef DRM_IMX_BRIDGE_H
#define DRM_IMX_BRIDGE_H

struct device;
struct device_node;
struct drm_bridge;

struct drm_bridge *devm_imx_drm_legacy_bridge(struct device *dev,
					      struct device_node *np,
					      int type);

#endif
