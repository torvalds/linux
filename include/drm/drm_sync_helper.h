/*
 * drm_sync_helper.h: software fence and helper functions for fences and
 * reservations used for dma buffer access synchronization between drivers.
 *
 * Copyright 2014 Google, Inc.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#ifndef _DRM_SYNC_HELPER_H_
#define _DRM_SYNC_HELPER_H_

#include <linux/fence.h>
#include <linux/reservation.h>
#include <linux/atomic.h>
#include <linux/workqueue.h>

/**
 * Create software fence
 * @context: execution context
 * @seqno: the sequence number of this fence inside the execution context
 */
struct fence *drm_sw_fence_new(unsigned int context,
			unsigned seqno);

/**
 * Signal and decrease reference count for a fence if it exists
 * @fence: fence to signal
 *
 * Utility function called when owner access to object associated with fence is
 * finished (e.g. GPU done with rendering).
 */
static inline void drm_fence_signal_and_put(struct fence **fence)
{
	if (*fence) {
		fence_signal(*fence);
		fence_put(*fence);
		*fence = NULL;
	}
}

struct drm_reservation_cb;

struct drm_reservation_fence_cb {
	struct fence_cb base;
	struct drm_reservation_cb *parent;
	struct fence *fence;
};

/**
 * Callback executed when all fences in reservation callback are signaled
 * @rcb: reservation callback structure
 * @context: context provided by user at init time
 */
typedef void (*drm_reservation_cb_func_t)(struct drm_reservation_cb *rcb,
					  void *context);

/**
 * Reservation callback structure
 * @work: work context in which func is executed
 * @fence_cbs: fence callbacks array
 * @num_fence_cbs: number of fence callbacks
 * @count: count of signaled fences, when it drops to 0 func is called
 * @func: callback to execute when all fences are signaled
 * @context: context provided by user during initialization
 *
 * It is safe and expected that func will destroy this structure before
 * returning.
 */
struct drm_reservation_cb {
	struct work_struct work;
	struct drm_reservation_fence_cb **fence_cbs;
	unsigned num_fence_cbs;
	atomic_t count;
	void *context;
	drm_reservation_cb_func_t func;
};

/**
 * Initialize reservation callback
 * @rcb: reservation callback structure to initialize
 * @func: function to call when all fences are signaled
 * @context: parameter to call func with
 */
void drm_reservation_cb_init(struct drm_reservation_cb *rcb,
			     drm_reservation_cb_func_t func,
			     void *context);

/**
 * Add fences from reservation object to callback
 * @rcb: reservation callback structure
 * @resv: reservation object
 * @exclusive: (for exclusive wait) when true add all fences, otherwise only
 *    exclusive fence
 */
int drm_reservation_cb_add(struct drm_reservation_cb *rcb,
			   struct reservation_object *resv,
			   bool exclusive);

/**
 * Finish adding fences
 * @rcb: reservation callback structure
 *
 * It will trigger callback worker if all fences were signaled before.
 */
void drm_reservation_cb_done(struct drm_reservation_cb *rcb);

/**
 * Cleanup reservation callback structure
 * @rcb: reservation callback structure
 *
 * Can be called to cancel primed reservation callback.
 */
void drm_reservation_cb_fini(struct drm_reservation_cb *rcb);

/**
 * Add reservation to array of reservations
 * @resv: reservation to add
 * @resvs: array of reservations
 * @excl_resvs_bitmap: bitmap for exclusive reservations
 * @num_resvs: number of reservations in array
 * @exclusive: bool to store in excl_resvs_bitmap
 */
void
drm_add_reservation(struct reservation_object *resv,
			struct reservation_object **resvs,
			unsigned long *excl_resvs_bitmap,
			unsigned int *num_resvs, bool exclusive);

/**
 * Acquire ww_mutex lock on all reservations in the array
 * @resvs: array of reservations
 * @num_resvs: number of reservations in the array
 * @ctx: ww mutex context
 */
int drm_lock_reservations(struct reservation_object **resvs,
			unsigned int num_resvs, struct ww_acquire_ctx *ctx);

/**
 * Release ww_mutex lock on all reservations in the array
 * @resvs: array of reservations
 * @num_resvs: number of reservations in the array
 * @ctx: ww mutex context
 */
void drm_unlock_reservations(struct reservation_object **resvs,
				unsigned int num_resvs,
				struct ww_acquire_ctx *ctx);

#endif
