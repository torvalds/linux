/* SPDX-License-Identifier: GPL-2.0+ */

#ifndef _DRM_VBLANK_HELPER_H_
#define _DRM_VBLANK_HELPER_H_

#include <linux/hrtimer_types.h>
#include <linux/types.h>

struct drm_crtc;

/*
 * VBLANK timer
 */

int drm_crtc_vblank_helper_enable_vblank_timer(struct drm_crtc *crtc);
void drm_crtc_vblank_helper_disable_vblank_timer(struct drm_crtc *crtc);
bool drm_crtc_vblank_helper_get_vblank_timestamp_from_timer(struct drm_crtc *crtc,
							    int *max_error,
							    ktime_t *vblank_time,
							    bool in_vblank_irq);

/**
 * DRM_CRTC_VBLANK_TIMER_FUNCS - Default implementation for VBLANK timers
 *
 * This macro initializes struct &drm_crtc_funcs to default helpers for
 * VBLANK timers.
 */
#define DRM_CRTC_VBLANK_TIMER_FUNCS \
	.enable_vblank = drm_crtc_vblank_helper_enable_vblank_timer, \
	.disable_vblank = drm_crtc_vblank_helper_disable_vblank_timer, \
	.get_vblank_timestamp = drm_crtc_vblank_helper_get_vblank_timestamp_from_timer

#endif
