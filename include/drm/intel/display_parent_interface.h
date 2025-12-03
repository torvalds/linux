/* SPDX-License-Identifier: MIT */
/* Copyright © 2025 Intel Corporation x*/

#ifndef __DISPLAY_PARENT_INTERFACE_H__
#define __DISPLAY_PARENT_INTERFACE_H__

#include <linux/types.h>

struct drm_device;
struct ref_tracker;

struct intel_display_rpm_interface {
	struct ref_tracker *(*get)(const struct drm_device *drm);
	struct ref_tracker *(*get_raw)(const struct drm_device *drm);
	struct ref_tracker *(*get_if_in_use)(const struct drm_device *drm);
	struct ref_tracker *(*get_noresume)(const struct drm_device *drm);

	void (*put)(const struct drm_device *drm, struct ref_tracker *wakeref);
	void (*put_raw)(const struct drm_device *drm, struct ref_tracker *wakeref);
	void (*put_unchecked)(const struct drm_device *drm);

	bool (*suspended)(const struct drm_device *drm);
	void (*assert_held)(const struct drm_device *drm);
	void (*assert_block)(const struct drm_device *drm);
	void (*assert_unblock)(const struct drm_device *drm);
};

/**
 * struct intel_display_parent_interface - services parent driver provides to display
 *
 * The parent, or core, driver provides a pointer to this structure to display
 * driver when calling intel_display_device_probe(). The display driver uses it
 * to access services provided by the parent driver. The structure may contain
 * sub-struct pointers to group function pointers by functionality.
 *
 * All function and sub-struct pointers must be initialized and callable unless
 * explicitly marked as "optional" below. The display driver will only NULL
 * check the optional pointers.
 */
struct intel_display_parent_interface {
	/** @rpm: Runtime PM functions */
	const struct intel_display_rpm_interface *rpm;
};

#endif
