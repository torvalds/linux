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
#include <linux/mutex.h>

struct backlight_device;
struct dentry;
struct device_node;
struct drm_connector;
struct drm_device;
struct drm_panel_follower;
struct drm_panel;
struct display_timing;

enum drm_panel_orientation;

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
 *
 * Backlight can be handled automatically if configured using
 * drm_panel_of_backlight() or drm_panel_dp_aux_backlight(). Then the driver
 * does not need to implement the functionality to enable/disable backlight.
 */
struct drm_panel_funcs {
	/**
	 * @prepare:
	 *
	 * Turn on panel and perform set up.
	 *
	 * This function is optional.
	 */
	int (*prepare)(struct drm_panel *panel);

	/**
	 * @enable:
	 *
	 * Enable panel (turn on back light, etc.).
	 *
	 * This function is optional.
	 */
	int (*enable)(struct drm_panel *panel);

	/**
	 * @disable:
	 *
	 * Disable panel (turn off back light, etc.).
	 *
	 * This function is optional.
	 */
	int (*disable)(struct drm_panel *panel);

	/**
	 * @unprepare:
	 *
	 * Turn off panel.
	 *
	 * This function is optional.
	 */
	int (*unprepare)(struct drm_panel *panel);

	/**
	 * @get_modes:
	 *
	 * Add modes to the connector that the panel is attached to
	 * and returns the number of modes added.
	 *
	 * This function is mandatory.
	 */
	int (*get_modes)(struct drm_panel *panel,
			 struct drm_connector *connector);

	/**
	 * @get_orientation:
	 *
	 * Return the panel orientation set by device tree or EDID.
	 *
	 * This function is optional.
	 */
	enum drm_panel_orientation (*get_orientation)(struct drm_panel *panel);

	/**
	 * @get_timings:
	 *
	 * Copy display timings into the provided array and return
	 * the number of display timings available.
	 *
	 * This function is optional.
	 */
	int (*get_timings)(struct drm_panel *panel, unsigned int num_timings,
			   struct display_timing *timings);

	/**
	 * @debugfs_init:
	 *
	 * Allows panels to create panels-specific debugfs files.
	 */
	void (*debugfs_init)(struct drm_panel *panel, struct dentry *root);
};

struct drm_panel_follower_funcs {
	/**
	 * @panel_prepared:
	 *
	 * Called after the panel has been powered on.
	 */
	int (*panel_prepared)(struct drm_panel_follower *follower);

	/**
	 * @panel_unpreparing:
	 *
	 * Called before the panel is powered off.
	 */
	int (*panel_unpreparing)(struct drm_panel_follower *follower);
};

struct drm_panel_follower {
	/**
	 * @funcs:
	 *
	 * Dependent device callbacks; should be initted by the caller.
	 */
	const struct drm_panel_follower_funcs *funcs;

	/**
	 * @list
	 *
	 * Used for linking into panel's list; set by drm_panel_add_follower().
	 */
	struct list_head list;

	/**
	 * @panel
	 *
	 * The panel we're dependent on; set by drm_panel_add_follower().
	 */
	struct drm_panel *panel;
};

/**
 * struct drm_panel - DRM panel object
 */
struct drm_panel {
	/**
	 * @dev:
	 *
	 * Parent device of the panel.
	 */
	struct device *dev;

	/**
	 * @backlight:
	 *
	 * Backlight device, used to turn on backlight after the call
	 * to enable(), and to turn off backlight before the call to
	 * disable().
	 * backlight is set by drm_panel_of_backlight() or
	 * drm_panel_dp_aux_backlight() and drivers shall not assign it.
	 */
	struct backlight_device *backlight;

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

	/**
	 * @followers:
	 *
	 * A list of struct drm_panel_follower dependent on this panel.
	 */
	struct list_head followers;

	/**
	 * @follower_lock:
	 *
	 * Lock for followers list.
	 */
	struct mutex follower_lock;

	/**
	 * @prepare_prev_first:
	 *
	 * The previous controller should be prepared first, before the prepare
	 * for the panel is called. This is largely required for DSI panels
	 * where the DSI host controller should be initialised to LP-11 before
	 * the panel is powered up.
	 */
	bool prepare_prev_first;

	/**
	 * @prepared:
	 *
	 * If true then the panel has been prepared.
	 */
	bool prepared;

	/**
	 * @enabled:
	 *
	 * If true then the panel has been enabled.
	 */
	bool enabled;
};

void drm_panel_init(struct drm_panel *panel, struct device *dev,
		    const struct drm_panel_funcs *funcs,
		    int connector_type);

void drm_panel_add(struct drm_panel *panel);
void drm_panel_remove(struct drm_panel *panel);

int drm_panel_prepare(struct drm_panel *panel);
int drm_panel_unprepare(struct drm_panel *panel);

int drm_panel_enable(struct drm_panel *panel);
int drm_panel_disable(struct drm_panel *panel);

int drm_panel_get_modes(struct drm_panel *panel, struct drm_connector *connector);

#if defined(CONFIG_OF) && defined(CONFIG_DRM_PANEL)
struct drm_panel *of_drm_find_panel(const struct device_node *np);
int of_drm_get_panel_orientation(const struct device_node *np,
				 enum drm_panel_orientation *orientation);
#else
static inline struct drm_panel *of_drm_find_panel(const struct device_node *np)
{
	return ERR_PTR(-ENODEV);
}

static inline int of_drm_get_panel_orientation(const struct device_node *np,
					       enum drm_panel_orientation *orientation)
{
	return -ENODEV;
}
#endif

#if defined(CONFIG_DRM_PANEL)
bool drm_is_panel_follower(struct device *dev);
int drm_panel_add_follower(struct device *follower_dev,
			   struct drm_panel_follower *follower);
void drm_panel_remove_follower(struct drm_panel_follower *follower);
int devm_drm_panel_add_follower(struct device *follower_dev,
				struct drm_panel_follower *follower);
#else
static inline bool drm_is_panel_follower(struct device *dev)
{
	return false;
}

static inline int drm_panel_add_follower(struct device *follower_dev,
					 struct drm_panel_follower *follower)
{
	return -ENODEV;
}

static inline void drm_panel_remove_follower(struct drm_panel_follower *follower) { }
static inline int devm_drm_panel_add_follower(struct device *follower_dev,
					      struct drm_panel_follower *follower)
{
	return -ENODEV;
}
#endif

#if IS_ENABLED(CONFIG_DRM_PANEL) && (IS_BUILTIN(CONFIG_BACKLIGHT_CLASS_DEVICE) || \
	(IS_MODULE(CONFIG_DRM) && IS_MODULE(CONFIG_BACKLIGHT_CLASS_DEVICE)))
int drm_panel_of_backlight(struct drm_panel *panel);
#else
static inline int drm_panel_of_backlight(struct drm_panel *panel)
{
	return 0;
}
#endif

#endif
