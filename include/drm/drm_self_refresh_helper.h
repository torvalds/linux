// SPDX-License-Identifier: MIT
/*
 * Copyright (C) 2019 Google, Inc.
 *
 * Authors:
 * Sean Paul <seanpaul@chromium.org>
 */
#ifndef DRM_SELF_REFRESH_HELPER_H_
#define DRM_SELF_REFRESH_HELPER_H_

struct drm_atomic_state;
struct drm_crtc;

void drm_self_refresh_helper_alter_state(struct drm_atomic_state *state);
void drm_self_refresh_helper_update_avg_times(struct drm_atomic_state *state,
					unsigned int commit_time_ms,
					unsigned int new_self_refresh_mask);

int drm_self_refresh_helper_init(struct drm_crtc *crtc);
void drm_self_refresh_helper_cleanup(struct drm_crtc *crtc);
#endif
