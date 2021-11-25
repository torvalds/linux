/* SPDX-License-Identifier: MIT */
/*
 * Copyright (C) 2020 Red Hat, Inc.
 *
 * Authors:
 * Hans de Goede <hdegoede@redhat.com>
 */

#ifndef __DRM_PRIVACY_SCREEN_DRIVER_H__
#define __DRM_PRIVACY_SCREEN_DRIVER_H__

#include <linux/device.h>
#include <linux/list.h>
#include <linux/mutex.h>
#include <drm/drm_connector.h>

struct drm_privacy_screen;

/**
 * struct drm_privacy_screen_ops - drm_privacy_screen operations
 *
 * Defines the operations which the privacy-screen class code may call.
 * These functions should be implemented by the privacy-screen driver.
 */
struct drm_privacy_screen_ops {
	/**
	 * @set_sw_state: Called to request a change of the privacy-screen
	 * state. The privacy-screen class code contains a check to avoid this
	 * getting called when the hw_state reports the state is locked.
	 * It is the driver's responsibility to update sw_state and hw_state.
	 * This is always called with the drm_privacy_screen's lock held.
	 */
	int (*set_sw_state)(struct drm_privacy_screen *priv,
			    enum drm_privacy_screen_status sw_state);
	/**
	 * @get_hw_state: Called to request that the driver gets the current
	 * privacy-screen state from the hardware and then updates sw_state and
	 * hw_state accordingly. This will be called by the core just before
	 * the privacy-screen is registered in sysfs.
	 */
	void (*get_hw_state)(struct drm_privacy_screen *priv);
};

/**
 * struct drm_privacy_screen - central privacy-screen structure
 *
 * Central privacy-screen structure, this contains the struct device used
 * to register the screen in sysfs, the screen's state, ops, etc.
 */
struct drm_privacy_screen {
	/** @dev: device used to register the privacy-screen in sysfs. */
	struct device dev;
	/** @lock: mutex protection all fields in this struct. */
	struct mutex lock;
	/** @list: privacy-screen devices list list-entry. */
	struct list_head list;
	/** @notifier_head: privacy-screen notifier head. */
	struct blocking_notifier_head notifier_head;
	/**
	 * @ops: &struct drm_privacy_screen_ops for this privacy-screen.
	 * This is NULL if the driver has unregistered the privacy-screen.
	 */
	const struct drm_privacy_screen_ops *ops;
	/**
	 * @sw_state: The privacy-screen's software state, see
	 * :ref:`Standard Connector Properties<standard_connector_properties>`
	 * for more info.
	 */
	enum drm_privacy_screen_status sw_state;
	/**
	 * @hw_state: The privacy-screen's hardware state, see
	 * :ref:`Standard Connector Properties<standard_connector_properties>`
	 * for more info.
	 */
	enum drm_privacy_screen_status hw_state;
};

struct drm_privacy_screen *drm_privacy_screen_register(
	struct device *parent, const struct drm_privacy_screen_ops *ops);
void drm_privacy_screen_unregister(struct drm_privacy_screen *priv);

void drm_privacy_screen_call_notifier_chain(struct drm_privacy_screen *priv);

#endif
