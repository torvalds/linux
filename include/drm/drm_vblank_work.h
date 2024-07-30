/* SPDX-License-Identifier: MIT */

#ifndef _DRM_VBLANK_WORK_H_
#define _DRM_VBLANK_WORK_H_

#include <linux/kthread.h>

struct drm_crtc;

/**
 * struct drm_vblank_work - A delayed work item which delays until a target
 * vblank passes, and then executes at realtime priority outside of IRQ
 * context.
 *
 * See also:
 * drm_vblank_work_schedule()
 * drm_vblank_work_init()
 * drm_vblank_work_cancel_sync()
 * drm_vblank_work_flush()
 * drm_vblank_work_flush_all()
 */
struct drm_vblank_work {
	/**
	 * @base: The base &kthread_work item which will be executed by
	 * &drm_vblank_crtc.worker. Drivers should not interact with this
	 * directly, and instead rely on drm_vblank_work_init() to initialize
	 * this.
	 */
	struct kthread_work base;

	/**
	 * @vblank: A pointer to &drm_vblank_crtc this work item belongs to.
	 */
	struct drm_vblank_crtc *vblank;

	/**
	 * @count: The target vblank this work will execute on. Drivers should
	 * not modify this value directly, and instead use
	 * drm_vblank_work_schedule()
	 */
	u64 count;

	/**
	 * @cancelling: The number of drm_vblank_work_cancel_sync() calls that
	 * are currently running. A work item cannot be rescheduled until all
	 * calls have finished.
	 */
	int cancelling;

	/**
	 * @node: The position of this work item in
	 * &drm_vblank_crtc.pending_work.
	 */
	struct list_head node;
};

/**
 * to_drm_vblank_work - Retrieve the respective &drm_vblank_work item from a
 * &kthread_work
 * @_work: The &kthread_work embedded inside a &drm_vblank_work
 */
#define to_drm_vblank_work(_work) \
	container_of((_work), struct drm_vblank_work, base)

int drm_vblank_work_schedule(struct drm_vblank_work *work,
			     u64 count, bool nextonmiss);
void drm_vblank_work_init(struct drm_vblank_work *work, struct drm_crtc *crtc,
			  void (*func)(struct kthread_work *work));
bool drm_vblank_work_cancel_sync(struct drm_vblank_work *work);
void drm_vblank_work_flush(struct drm_vblank_work *work);
void drm_vblank_work_flush_all(struct drm_crtc *crtc);

#endif /* !_DRM_VBLANK_WORK_H_ */
