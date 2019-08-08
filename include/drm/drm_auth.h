/*
 * Internal Header for the Direct Rendering Manager
 *
 * Copyright 2016 Intel Corporation
 *
 * Author: Daniel Vetter <daniel.vetter@ffwll.ch>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * VA LINUX SYSTEMS AND/OR ITS SUPPLIERS BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 */

#ifndef _DRM_AUTH_H_
#define _DRM_AUTH_H_

/*
 * Legacy DRI1 locking data structure. Only here instead of in drm_legacy.h for
 * include ordering reasons.
 *
 * DO NOT USE.
 */
struct drm_lock_data {
	struct drm_hw_lock *hw_lock;
	struct drm_file *file_priv;
	wait_queue_head_t lock_queue;
	unsigned long lock_time;
	spinlock_t spinlock;
	uint32_t kernel_waiters;
	uint32_t user_waiters;
	int idle_has_lock;
};

/**
 * struct drm_master - drm master structure
 *
 * @refcount: Refcount for this master object.
 * @dev: Link back to the DRM device
 * @driver_priv: Pointer to driver-private information.
 * @lessor: Lease holder
 * @lessee_id: id for lessees. Owners always have id 0
 * @lessee_list: other lessees of the same master
 * @lessees: drm_masters leasing from this one
 * @leases: Objects leased to this drm_master.
 * @lessee_idr: All lessees under this owner (only used where lessor == NULL)
 *
 * Note that master structures are only relevant for the legacy/primary device
 * nodes, hence there can only be one per device, not one per drm_minor.
 */
struct drm_master {
	struct kref refcount;
	struct drm_device *dev;
	/**
	 * @unique: Unique identifier: e.g. busid. Protected by
	 * &drm_device.master_mutex.
	 */
	char *unique;
	/**
	 * @unique_len: Length of unique field. Protected by
	 * &drm_device.master_mutex.
	 */
	int unique_len;
	/**
	 * @magic_map: Map of used authentication tokens. Protected by
	 * &drm_device.master_mutex.
	 */
	struct idr magic_map;
	void *driver_priv;

	/* Tree of display resource leases, each of which is a drm_master struct
	 * All of these get activated simultaneously, so drm_device master points
	 * at the top of the tree (for which lessor is NULL). Protected by
	 * &drm_device.mode_config.idr_mutex.
	 */

	struct drm_master *lessor;
	int	lessee_id;
	struct list_head lessee_list;
	struct list_head lessees;
	struct idr leases;
	struct idr lessee_idr;
	/* private: */
#if IS_ENABLED(CONFIG_DRM_LEGACY)
	struct drm_lock_data lock;
#endif
};

struct drm_master *drm_master_get(struct drm_master *master);
void drm_master_put(struct drm_master **master);
bool drm_is_current_master(struct drm_file *fpriv);

struct drm_master *drm_master_create(struct drm_device *dev);

#endif
