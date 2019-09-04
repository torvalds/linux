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

#include <linux/err.h>
#include <linux/errno.h>
#include <linux/list.h>

struct device_node;
struct drm_connector;
struct drm_device;
struct drm_panel;
struct display_timing;

/**
 * struct drm_panel_funcs - perform operations on a given panel
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
	/**
	 * @prepare:
	 *
	 * Turn on panel and perform set up.
	 */
	int (*prepare)(struct drm_panel *panel);

	/**
	 * @enable:
	 *
	 * Enable panel (turn on back light, etc.).
	 */
	int (*enable)(struct drm_panel *panel);

	/**
	 * @disable:
	 *
	 * Disable panel (turn off back light, etc.).
	 */
	int (*disable)(struct drm_panel *panel);

	/**
	 * @unprepare:
	 *
	 * Turn off panel.
	 */
	int (*unprepare)(struct drm_panel *panel);

	/**
	 * @get_modes:
	 *
	 * Add modes to the connector that the panel is attached to and
	 * return the number of modes added.
	 */
	int (*get_modes)(struct drm_panel *panel);

	/**
	 * @get_timings:
	 *
	 * Copy display timings into the provided array and return
	 * the number of display timings available.
	 */
	int (*get_timings)(struct drm_panel *panel, unsigned int num_timings,
			   struct display_timing *timings);
};

/**
 * struct drm_panel - DRM panel object
 */
struct drm_panel {
	/**
	 * @drm:
	 *
	 * DRM device owning the panel.
	 */
	struct drm_device *drm;

	/**
	 * @connector:
	 *
	 * DRM connector that the panel is attached to.
	 */
	struct drm_connector *connector;

	/**
	 * @dev:
	 *
	 * Parent device of the panel.
	 */
	struct device *dev;

	/**
	 * @funcs:
	 *
	 * Operations that can be performed on the panel.
	 */
	const struct drm_panel_funcs *funcs;

	/**
	 * @connector_type:
	 *
	 * Type of the panel as a DRM_MODE_CONNECTOR_* value. This is used to
	 * initialise the drm_connector corresponding to the panel with the
	 * correct connector type.
	 */
	int connector_type;

	/**
	 * @list:
	 *
	 * Panel entry in registry.
	 */
	struct list_head list;
};

void drm_panel_init(struct drm_panel *panel, struct device *dev,
		    const struct drm_panel_funcs *funcs,
		    int connector_type);

int drm_panel_add(struct drm_panel *panel);
void drm_panel_remove(struct drm_panel *panel);

int drm_panel_attach(struct drm_panel *panel, struct drm_connector *connector);
void drm_panel_detach(struct drm_panel *panel);

int drm_panel_prepare(struct drm_panel *panel);
int drm_panel_unprepare(struct drm_panel *panel);

int drm_panel_enable(struct drm_panel *panel);
int drm_panel_disable(struct drm_panel *panel);

int drm_panel_get_modes(struct drm_panel *panel);

#if defined(CONFIG_OF) && defined(CONFIG_DRM_PANEL)
struct drm_panel *of_drm_find_panel(const struct device_node *np);
#else
static inline struct drm_panel *of_drm_find_panel(const struct device_node *np)
{
	return ERR_PTR(-ENODEV);
}
#endif

#endif
