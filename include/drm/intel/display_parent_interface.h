/* SPDX-License-Identifier: MIT */
/* Copyright © 2025 Intel Corporation x*/

#ifndef __DISPLAY_PARENT_INTERFACE_H__
#define __DISPLAY_PARENT_INTERFACE_H__

#include <linux/types.h>

struct drm_device;

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
};

#endif
