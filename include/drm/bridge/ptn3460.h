/*
 * Copyright (C) 2013 Google, Inc.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#ifndef _DRM_BRIDGE_PTN3460_H_
#define _DRM_BRIDGE_PTN3460_H_

struct drm_device;
struct drm_encoder;
struct i2c_client;
struct device_node;

#if defined(CONFIG_DRM_PTN3460) || defined(CONFIG_DRM_PTN3460_MODULE)

int ptn3460_init(struct drm_device *dev, struct drm_encoder *encoder,
		struct i2c_client *client, struct device_node *node);
#else

static inline int ptn3460_init(struct drm_device *dev,
		struct drm_encoder *encoder, struct i2c_client *client,
		struct device_node *node)
{
	return 0;
}

#endif

#endif
