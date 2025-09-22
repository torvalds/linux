/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2023 Intel Corporation
 */

#ifndef __INTEL_DISPLAY_RPS_H__
#define __INTEL_DISPLAY_RPS_H__

#include <linux/types.h>

struct dma_fence;
struct drm_crtc;
struct drm_i915_private;
struct intel_atomic_state;

void intel_display_rps_boost_after_vblank(struct drm_crtc *crtc,
					  struct dma_fence *fence);
void intel_display_rps_mark_interactive(struct drm_i915_private *i915,
					struct intel_atomic_state *state,
					bool interactive);

#endif /* __INTEL_DISPLAY_RPS_H__ */
