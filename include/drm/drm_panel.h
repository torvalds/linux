/*
 * Copyright (C) 2013, NVIDIA Corporation.  All rights reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sub license,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial portions
 * of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

#ifndef __DRM_PANEL_H__
#define __DRM_PANEL_H__

#include <linux/list.h>

struct drm_connector;
struct drm_device;
struct drm_panel;
struct display_timing;

/**
 * struct drm_panel_funcs - perform operations on a given panel
 * @loader_protect: protect loader logo panel's power
 * @disable: disable panel (turn off back light, etc.)
 * @unprepare: turn off panel
 * @prepare: turn on panel and perform set up
 * @enable: enable panel (turn on back light, etc.)
 * @get_modes: add modes to the connector that the panel is attached to and
 * return the number of modes added
 * @get_timings: copy display timings into the provided array and return
 * the number of display timings available
 *
 * The .prepare() function is typically called before the display controller
 * starts to transmit video data. Panel drivers can use this to turn the panel
 * on and wait for it to become ready. If additional configuration is required
 * (via a control bus such as I2C, SPI or DSI for example) this is a good time
 * to do that.
 *
 * After the display controller has started transmitting video data, it's safe
 * to call the .enable() function. This will typically enable the backlight to
 * make the image on screen visible. Some panels require a certain amount of
 * time or frames before the image is displayed. This function is responsible
 * for taking this into account before enabling the backlight to avoid visual
 * glitches.
 *
 * Before stopping video transmission from the display controller it can be
 * necessary to turn off the panel to avoid visual glitches. This is done in
 * the .disable() function. Analogously to .enable() this typically involves
 * turning off the backlight and waiting for some time to make sure no image
 * is visible on the panel. It is then safe for the display controller to
 * cease transmission of video data.
 *
 * To save power when no video data is transmitted, a driver can power down
 * the panel. This is the job of the .unprepare() function.
 */
struct drm_panel_funcs {
	int (*loader_protect)(struct drm_panel *panel, bool on);
	int (*disable)(struct drm_panel *panel);
	int (*unprepare)(struct drm_panel *panel);
	int (*prepare)(struct drm_panel *panel);
	int (*enable)(struct drm_panel *panel);
	int (*get_modes)(struct drm_panel *panel);
	int (*get_timings)(struct drm_panel *panel, unsigned int num_timings,
			   struct display_timing *timings);
};

struct drm_panel {
	struct drm_device *drm;
	struct drm_connector *connector;
	struct device *dev;

	const struct drm_panel_funcs *funcs;

	struct list_head list;
};

static inline int drm_panel_unprepare(struct drm_panel *panel)
{
	if (panel && panel->funcs && panel->funcs->unprepare)
		return panel->funcs->unprepare(panel);

	return panel ? -ENOSYS : -EINVAL;
}

static inline int drm_panel_disable(struct drm_panel *panel)
{
	if (panel && panel->funcs && panel->funcs->disable)
		return panel->funcs->disable(panel);

	return panel ? -ENOSYS : -EINVAL;
}

static inline int drm_panel_loader_protect(struct drm_panel *panel, bool on)
{
	if (panel && panel->funcs && panel->funcs->loader_protect)
		return panel->funcs->loader_protect(panel, on);

	return panel ? -ENOSYS : -EINVAL;
}

static inline int drm_panel_prepare(struct drm_panel *panel)
{
	if (panel && panel->funcs && panel->funcs->prepare)
		return panel->funcs->prepare(panel);

	return panel ? -ENOSYS : -EINVAL;
}

static inline int drm_panel_enable(struct drm_panel *panel)
{
	if (panel && panel->funcs && panel->funcs->enable)
		return panel->funcs->enable(panel);

	return panel ? -ENOSYS : -EINVAL;
}

static inline int drm_panel_get_modes(struct drm_panel *panel)
{
	if (panel && panel->funcs && panel->funcs->get_modes)
		return panel->funcs->get_modes(panel);

	return panel ? -ENOSYS : -EINVAL;
}

void drm_panel_init(struct drm_panel *panel);

int drm_panel_add(struct drm_panel *panel);
void drm_panel_remove(struct drm_panel *panel);

int drm_panel_attach(struct drm_panel *panel, struct drm_connector *connector);
int drm_panel_detach(struct drm_panel *panel);

int rockchip_drm_crtc_send_mcu_cmd(struct drm_device *drm_dev,
				   struct device_node *np_crtc,
				   u32 type, u32 value);
struct drm_panel *drm_find_panel_by_connector(struct drm_connector *connector);
#ifdef CONFIG_OF
struct drm_panel *of_drm_find_panel(struct device_node *np);
#else
static inline struct drm_panel *of_drm_find_panel(struct device_node *np)
{
	return NULL;
}
#endif

#endif
