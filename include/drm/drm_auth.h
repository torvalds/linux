#ifndef _DRM_AUTH_H_
#define _DRM_AUTH_H_

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

#include <linux/idr.h>
#include <linux/kref.h>
#include <linux/wait.h>

struct drm_file;
struct drm_hw_lock;

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

	/**
	 * @lessor:
	 *
	 * Lease grantor, only set if this &struct drm_master represents a
	 * lessee holding a lease of objects from @lessor. Full owners of the
	 * device have this set to NULL.
	 *
	 * The lessor does not change once it's set in drm_lease_create(), and
	 * each lessee holds a reference to its lessor that it releases upon
	 * being destroyed in drm_lease_destroy().
	 *
	 * See also the :ref:`section on display resource leasing
	 * <drm_leasing>`.
	 */
	struct drm_master *lessor;

	/**
	 * @lessee_id:
	 *
	 * ID for lessees. Owners (i.e. @lessor is NULL) always have ID 0.
	 * Protected by &drm_device.mode_config's &drm_mode_config.idr_mutex.
	 */
	int	lessee_id;

	/**
	 * @lessee_list:
	 *
	 * List entry of lessees of @lessor, where they are linked to @lessees.
	 * Not used for owners. Protected by &drm_device.mode_config's
	 * &drm_mode_config.idr_mutex.
	 */
	struct list_head lessee_list;

	/**
	 * @lessees:
	 *
	 * List of drm_masters leasing from this one. Protected by
	 * &drm_device.mode_config's &drm_mode_config.idr_mutex.
	 *
	 * This list is empty if no leases have been granted, or if all lessees
	 * have been destroyed. Since lessors are referenced by all their
	 * lessees, this master cannot be destroyed unless the list is empty.
	 */
	struct list_head lessees;

	/**
	 * @leases:
	 *
	 * Objects leased to this drm_master. Protected by
	 * &drm_device.mode_config's &drm_mode_config.idr_mutex.
	 *
	 * Objects are leased all together in drm_lease_create(), and are
	 * removed all together when the lease is revoked.
	 */
	struct idr leases;

	/**
	 * @lessee_idr:
	 *
	 * All lessees under this owner (only used where @lessor is NULL).
	 * Protected by &drm_device.mode_config's &drm_mode_config.idr_mutex.
	 */
	struct idr lessee_idr;
	/* private: */
#if IS_ENABLED(CONFIG_DRM_LEGACY)
	struct drm_lock_data lock;
#endif
};

struct drm_master *drm_master_get(struct drm_master *master);
struct drm_master *drm_file_get_master(struct drm_file *file_priv);
void drm_master_put(struct drm_master **master);
bool drm_is_current_master(struct drm_file *fpriv);

struct drm_master *drm_master_create(struct drm_device *dev);

#endif
