/*
 * Copyright (C) 2014 Red Hat
 * Author: Rob Clark <robdclark@gmail.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 */

#ifndef DRM_MODESET_LOCK_H_
#define DRM_MODESET_LOCK_H_

#include <linux/ww_mutex.h>

struct drm_modeset_lock;

/**
 * drm_modeset_acquire_ctx - locking context (see ww_acquire_ctx)
 * @ww_ctx: base acquire ctx
 * @contended: used internally for -EDEADLK handling
 * @locked: list of held locks
 *
 * Each thread competing for a set of locks must use one acquire
 * ctx.  And if any lock fxn returns -EDEADLK, it must backoff and
 * retry.
 */
struct drm_modeset_acquire_ctx {

	struct ww_acquire_ctx ww_ctx;

	/**
	 * Contended lock: if a lock is contended you should only call
	 * drm_modeset_backoff() which drops locks and slow-locks the
	 * contended lock.
	 */
	struct drm_modeset_lock *contended;

	/**
	 * list of held locks (drm_modeset_lock)
	 */
	struct list_head locked;
};

/**
 * drm_modeset_lock - used for locking modeset resources.
 * @mutex: resource locking
 * @head: used to hold it's place on state->locked list when
 *    part of an atomic update
 *
 * Used for locking CRTCs and other modeset resources.
 */
struct drm_modeset_lock {
	/**
	 * modeset lock
	 */
	struct ww_mutex mutex;

	/**
	 * Resources that are locked as part of an atomic update are added
	 * to a list (so we know what to unlock at the end).
	 */
	struct list_head head;
};

extern struct ww_class crtc_ww_class;

void drm_modeset_acquire_init(struct drm_modeset_acquire_ctx *ctx,
		uint32_t flags);
void drm_modeset_acquire_fini(struct drm_modeset_acquire_ctx *ctx);
void drm_modeset_drop_locks(struct drm_modeset_acquire_ctx *ctx);
void drm_modeset_backoff(struct drm_modeset_acquire_ctx *ctx);
int drm_modeset_backoff_interruptible(struct drm_modeset_acquire_ctx *ctx);

/**
 * drm_modeset_lock_init - initialize lock
 * @lock: lock to init
 */
static inline void drm_modeset_lock_init(struct drm_modeset_lock *lock)
{
	ww_mutex_init(&lock->mutex, &crtc_ww_class);
	INIT_LIST_HEAD(&lock->head);
}

/**
 * drm_modeset_lock_fini - cleanup lock
 * @lock: lock to cleanup
 */
static inline void drm_modeset_lock_fini(struct drm_modeset_lock *lock)
{
	WARN_ON(!list_empty(&lock->head));
}

/**
 * drm_modeset_is_locked - equivalent to mutex_is_locked()
 * @lock: lock to check
 */
static inline bool drm_modeset_is_locked(struct drm_modeset_lock *lock)
{
	return ww_mutex_is_locked(&lock->mutex);
}

int drm_modeset_lock(struct drm_modeset_lock *lock,
		struct drm_modeset_acquire_ctx *ctx);
int drm_modeset_lock_interruptible(struct drm_modeset_lock *lock,
		struct drm_modeset_acquire_ctx *ctx);
void drm_modeset_unlock(struct drm_modeset_lock *lock);

struct drm_device;

void drm_modeset_lock_all(struct drm_device *dev);
void drm_modeset_unlock_all(struct drm_device *dev);
void drm_warn_on_modeset_not_all_locked(struct drm_device *dev);

int drm_modeset_lock_all_crtcs(struct drm_device *dev,
		struct drm_modeset_acquire_ctx *ctx);

#endif /* DRM_MODESET_LOCK_H_ */
