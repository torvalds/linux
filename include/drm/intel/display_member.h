/* SPDX-License-Identifier: MIT */
/* Copyright © 2025 Intel Corporation */

#ifndef __DRM_INTEL_DISPLAY_H__
#define __DRM_INTEL_DISPLAY_H__

#include <linux/build_bug.h>
#include <linux/stddef.h>
#include <linux/stringify.h>

#include <drm/drm_device.h>

struct intel_display;

/*
 * A dummy device struct to define the relative offsets of drm and display
 * members. With the members identically placed in struct drm_i915_private and
 * struct xe_device, this allows figuring out the struct intel_display pointer
 * without the definition of either driver specific structure.
 */
struct __intel_generic_device {
	struct drm_device drm;
	struct intel_display *display;
};

/**
 * INTEL_DISPLAY_MEMBER_STATIC_ASSERT() - ensure correct placing of drm and display members
 * @type: The struct to check
 * @drm_member: Name of the struct drm_device member
 * @display_member: Name of the struct intel_display * member.
 *
 * Use this static assert macro to ensure the struct drm_i915_private and struct
 * xe_device struct drm_device and struct intel_display * members are at the
 * same relative offsets.
 */
#define INTEL_DISPLAY_MEMBER_STATIC_ASSERT(type, drm_member, display_member) \
	static_assert( \
		offsetof(struct __intel_generic_device, display) - offsetof(struct __intel_generic_device, drm) == \
		offsetof(type, display_member) - offsetof(type, drm_member), \
		__stringify(type) " " __stringify(drm_member) " and " __stringify(display_member) " members at invalid offsets")

#endif
