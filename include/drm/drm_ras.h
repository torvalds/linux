/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2026 Intel Corporation
 */

#ifndef __DRM_RAS_H__
#define __DRM_RAS_H__

#include <uapi/drm/drm_ras.h>

/**
 * struct drm_ras_node - A DRM RAS Node
 */
struct drm_ras_node {
	/** @id: Unique identifier for the node. Dynamically assigned. */
	u32 id;
	/**
	 * @device_name: Human-readable name of the device. Given by the driver.
	 */
	const char *device_name;
	/** @node_name: Human-readable name of the node. Given by the driver. */
	const char *node_name;
	/** @type: Type of the node (enum drm_ras_node_type). */
	enum drm_ras_node_type type;

	/* Error-Counter Related Callback and Variables */

	/** @error_counter_range: Range of valid Error IDs for this node. */
	struct {
		/** @first: First valid Error ID. */
		u32 first;
		/** @last: Last valid Error ID. Mandatory entry. */
		u32 last;
	} error_counter_range;

	/**
	 * @query_error_counter:
	 *
	 * This callback is used by drm-ras to query a specific error counter.
	 * Used for input check and to iterate all error counters in a node.
	 *
	 * Driver should expect query_error_counter() to be called with
	 * error_id from `error_counter_range.first` to
	 * `error_counter_range.last`.
	 *
	 * The @query_error_counter is a mandatory callback for
	 * error_counter_node.
	 *
	 * Returns: 0 on success,
	 *          -ENOENT when error_id is not supported as an indication that
	 *                  drm_ras should silently skip this entry. Used for
	 *                  supporting non-contiguous error ranges.
	 *                  Driver is responsible for maintaining the list of
	 *                  supported error IDs in the range of first to last.
	 *          Other negative values on errors that should terminate the
	 *          netlink query.
	 */
	int (*query_error_counter)(struct drm_ras_node *node, u32 error_id,
				   const char **name, u32 *val);

	/** @priv: Driver private data */
	void *priv;
};

struct drm_device;

#if IS_ENABLED(CONFIG_DRM_RAS)
int drm_ras_node_register(struct drm_ras_node *node);
void drm_ras_node_unregister(struct drm_ras_node *node);
#else
static inline int drm_ras_node_register(struct drm_ras_node *node) { return 0; }
static inline void drm_ras_node_unregister(struct drm_ras_node *node) { }
#endif

#endif
