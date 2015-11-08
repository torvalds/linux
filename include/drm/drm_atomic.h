/*
 * Copyright (C) 2014 Red Hat
 * Copyright (C) 2014 Intel Corp.
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
 *
 * Authors:
 * Rob Clark <robdclark@gmail.com>
 * Daniel Vetter <daniel.vetter@ffwll.ch>
 */

#ifndef DRM_ATOMIC_H_
#define DRM_ATOMIC_H_

#include <drm/drm_crtc.h>

struct drm_atomic_state * __must_check
drm_atomic_state_alloc(struct drm_device *dev);
void drm_atomic_state_clear(struct drm_atomic_state *state);
void drm_atomic_state_free(struct drm_atomic_state *state);

int  __must_check
drm_atomic_state_init(struct drm_device *dev, struct drm_atomic_state *state);
void drm_atomic_state_default_clear(struct drm_atomic_state *state);
void drm_atomic_state_default_release(struct drm_atomic_state *state);

struct drm_crtc_state * __must_check
drm_atomic_get_crtc_state(struct drm_atomic_state *state,
			  struct drm_crtc *crtc);
int drm_atomic_crtc_set_property(struct drm_crtc *crtc,
		struct drm_crtc_state *state, struct drm_property *property,
		uint64_t val);
struct drm_plane_state * __must_check
drm_atomic_get_plane_state(struct drm_atomic_state *state,
			   struct drm_plane *plane);
int drm_atomic_plane_set_property(struct drm_plane *plane,
		struct drm_plane_state *state, struct drm_property *property,
		uint64_t val);
struct drm_connector_state * __must_check
drm_atomic_get_connector_state(struct drm_atomic_state *state,
			       struct drm_connector *connector);
int drm_atomic_connector_set_property(struct drm_connector *connector,
		struct drm_connector_state *state, struct drm_property *property,
		uint64_t val);

/**
 * drm_atomic_get_existing_crtc_state - get crtc state, if it exists
 * @state: global atomic state object
 * @crtc: crtc to grab
 *
 * This function returns the crtc state for the given crtc, or NULL
 * if the crtc is not part of the global atomic state.
 */
static inline struct drm_crtc_state *
drm_atomic_get_existing_crtc_state(struct drm_atomic_state *state,
				   struct drm_crtc *crtc)
{
	return state->crtc_states[drm_crtc_index(crtc)];
}

/**
 * drm_atomic_get_existing_plane_state - get plane state, if it exists
 * @state: global atomic state object
 * @plane: plane to grab
 *
 * This function returns the plane state for the given plane, or NULL
 * if the plane is not part of the global atomic state.
 */
static inline struct drm_plane_state *
drm_atomic_get_existing_plane_state(struct drm_atomic_state *state,
				    struct drm_plane *plane)
{
	return state->plane_states[drm_plane_index(plane)];
}

/**
 * drm_atomic_get_existing_connector_state - get connector state, if it exists
 * @state: global atomic state object
 * @connector: connector to grab
 *
 * This function returns the connector state for the given connector,
 * or NULL if the connector is not part of the global atomic state.
 */
static inline struct drm_connector_state *
drm_atomic_get_existing_connector_state(struct drm_atomic_state *state,
					struct drm_connector *connector)
{
	int index = drm_connector_index(connector);

	if (index >= state->num_connector)
		return NULL;

	return state->connector_states[index];
}

int __must_check
drm_atomic_set_mode_for_crtc(struct drm_crtc_state *state,
			     struct drm_display_mode *mode);
int __must_check
drm_atomic_set_mode_prop_for_crtc(struct drm_crtc_state *state,
				  struct drm_property_blob *blob);
int __must_check
drm_atomic_set_crtc_for_plane(struct drm_plane_state *plane_state,
			      struct drm_crtc *crtc);
void drm_atomic_set_fb_for_plane(struct drm_plane_state *plane_state,
				 struct drm_framebuffer *fb);
int __must_check
drm_atomic_set_crtc_for_connector(struct drm_connector_state *conn_state,
				  struct drm_crtc *crtc);
int __must_check
drm_atomic_add_affected_connectors(struct drm_atomic_state *state,
				   struct drm_crtc *crtc);
int __must_check
drm_atomic_add_affected_planes(struct drm_atomic_state *state,
			       struct drm_crtc *crtc);

int
drm_atomic_connectors_for_crtc(struct drm_atomic_state *state,
			       struct drm_crtc *crtc);

void drm_atomic_legacy_backoff(struct drm_atomic_state *state);

int __must_check drm_atomic_check_only(struct drm_atomic_state *state);
int __must_check drm_atomic_commit(struct drm_atomic_state *state);
int __must_check drm_atomic_async_commit(struct drm_atomic_state *state);

#define for_each_connector_in_state(state, connector, connector_state, __i) \
	for ((__i) = 0;							\
	     (__i) < (state)->num_connector &&				\
	     ((connector) = (state)->connectors[__i],			\
	     (connector_state) = (state)->connector_states[__i], 1); 	\
	     (__i)++)							\
		if (connector)

#define for_each_crtc_in_state(state, crtc, crtc_state, __i)	\
	for ((__i) = 0;						\
	     (__i) < (state)->dev->mode_config.num_crtc &&	\
	     ((crtc) = (state)->crtcs[__i],			\
	     (crtc_state) = (state)->crtc_states[__i], 1);	\
	     (__i)++)						\
		if (crtc_state)

#define for_each_plane_in_state(state, plane, plane_state, __i)		\
	for ((__i) = 0;							\
	     (__i) < (state)->dev->mode_config.num_total_plane &&	\
	     ((plane) = (state)->planes[__i],				\
	     (plane_state) = (state)->plane_states[__i], 1);		\
	     (__i)++)							\
		if (plane_state)
static inline bool
drm_atomic_crtc_needs_modeset(struct drm_crtc_state *state)
{
	return state->mode_changed || state->active_changed ||
	       state->connectors_changed;
}


#endif /* DRM_ATOMIC_H_ */
