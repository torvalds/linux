/*
 * Copyright 2016 Intel Corp.
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

#ifndef _DRM_VBLANK_H_
#define _DRM_VBLANK_H_

#include <linux/seqlock.h>
#include <linux/idr.h>
#include <linux/poll.h>

#include <drm/drm_file.h>
#include <drm/drm_modes.h>
#include <uapi/drm/drm.h>

struct drm_device;
struct drm_crtc;

/**
 * struct drm_pending_vblank_event - pending vblank event tracking
 */
struct drm_pending_vblank_event {
	/**
	 * @base: Base structure for tracking pending DRM events.
	 */
	struct drm_pending_event base;
	/**
	 * @pipe: drm_crtc_index() of the &drm_crtc this event is for.
	 */
	unsigned int pipe;
	/**
	 * @sequence: frame event should be triggered at
	 */
	u64 sequence;
	/**
	 * @event: Actual event which will be sent to userspace.
	 */
	union {
		struct drm_event base;
		struct drm_event_vblank vbl;
		struct drm_event_crtc_sequence seq;
	} event;
};

/**
 * struct drm_vblank_crtc - vblank tracking for a CRTC
 *
 * This structure tracks the vblank state for one CRTC.
 *
 * Note that for historical reasons - the vblank handling code is still shared
 * with legacy/non-kms drivers - this is a free-standing structure not directly
 * connected to &struct drm_crtc. But all public interface functions are taking
 * a &struct drm_crtc to hide this implementation detail.
 */
struct drm_vblank_crtc {
	/**
	 * @dev: Pointer to the &drm_device.
	 */
	struct drm_device *dev;
	/**
	 * @queue: Wait queue for vblank waiters.
	 */
	wait_queue_head_t queue;	/**< VBLANK wait queue */
	/**
	 * @disable_timer: Disable timer for the delayed vblank disabling
	 * hysteresis logic. Vblank disabling is controlled through the
	 * drm_vblank_offdelay module option and the setting of the
	 * &drm_device.max_vblank_count value.
	 */
	struct timer_list disable_timer;

	/**
	 * @seqlock: Protect vblank count and time.
	 */
	seqlock_t seqlock;		/* protects vblank count and time */

	/**
	 * @count: Current software vblank counter.
	 */
	u64 count;
	/**
	 * @time: Vblank timestamp corresponding to @count.
	 */
	ktime_t time;

	/**
	 * @refcount: Number of users/waiters of the vblank interrupt. Only when
	 * this refcount reaches 0 can the hardware interrupt be disabled using
	 * @disable_timer.
	 */
	atomic_t refcount;		/* number of users of vblank interruptsper crtc */
	/**
	 * @last: Protected by &drm_device.vbl_lock, used for wraparound handling.
	 */
	u32 last;
	/**
	 * @inmodeset: Tracks whether the vblank is disabled due to a modeset.
	 * For legacy driver bit 2 additionally tracks whether an additional
	 * temporary vblank reference has been acquired to paper over the
	 * hardware counter resetting/jumping. KMS drivers should instead just
	 * call drm_crtc_vblank_off() and drm_crtc_vblank_on(), which explicitly
	 * save and restore the vblank count.
	 */
	unsigned int inmodeset;		/* Display driver is setting mode */
	/**
	 * @pipe: drm_crtc_index() of the &drm_crtc corresponding to this
	 * structure.
	 */
	unsigned int pipe;
	/**
	 * @framedur_ns: Frame/Field duration in ns, used by
	 * drm_calc_vbltimestamp_from_scanoutpos() and computed by
	 * drm_calc_timestamping_constants().
	 */
	int framedur_ns;
	/**
	 * @linedur_ns: Line duration in ns, used by
	 * drm_calc_vbltimestamp_from_scanoutpos() and computed by
	 * drm_calc_timestamping_constants().
	 */
	int linedur_ns;

	/**
	 * @hwmode:
	 *
	 * Cache of the current hardware display mode. Only valid when @enabled
	 * is set. This is used by helpers like
	 * drm_calc_vbltimestamp_from_scanoutpos(). We can't just access the
	 * hardware mode by e.g. looking at &drm_crtc_state.adjusted_mode,
	 * because that one is really hard to get from interrupt context.
	 */
	struct drm_display_mode hwmode;

	/**
	 * @enabled: Tracks the enabling state of the corresponding &drm_crtc to
	 * avoid double-disabling and hence corrupting saved state. Needed by
	 * drivers not using atomic KMS, since those might go through their CRTC
	 * disabling functions multiple times.
	 */
	bool enabled;
};

int drm_vblank_init(struct drm_device *dev, unsigned int num_crtcs);
u64 drm_crtc_vblank_count(struct drm_crtc *crtc);
u64 drm_crtc_vblank_count_and_time(struct drm_crtc *crtc,
				   ktime_t *vblanktime);
void drm_crtc_send_vblank_event(struct drm_crtc *crtc,
			       struct drm_pending_vblank_event *e);
void drm_crtc_arm_vblank_event(struct drm_crtc *crtc,
			      struct drm_pending_vblank_event *e);
void drm_vblank_set_event(struct drm_pending_vblank_event *e,
			  u64 *seq,
			  ktime_t *now);
bool drm_handle_vblank(struct drm_device *dev, unsigned int pipe);
bool drm_crtc_handle_vblank(struct drm_crtc *crtc);
int drm_crtc_vblank_get(struct drm_crtc *crtc);
void drm_crtc_vblank_put(struct drm_crtc *crtc);
void drm_wait_one_vblank(struct drm_device *dev, unsigned int pipe);
void drm_crtc_wait_one_vblank(struct drm_crtc *crtc);
void drm_crtc_vblank_off(struct drm_crtc *crtc);
void drm_crtc_vblank_reset(struct drm_crtc *crtc);
void drm_crtc_vblank_on(struct drm_crtc *crtc);
u32 drm_crtc_accurate_vblank_count(struct drm_crtc *crtc);

bool drm_calc_vbltimestamp_from_scanoutpos(struct drm_device *dev,
					   unsigned int pipe, int *max_error,
					   ktime_t *vblank_time,
					   bool in_vblank_irq);
void drm_calc_timestamping_constants(struct drm_crtc *crtc,
				     const struct drm_display_mode *mode);
wait_queue_head_t *drm_crtc_vblank_waitqueue(struct drm_crtc *crtc);
#endif
