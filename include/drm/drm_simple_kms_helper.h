/*
 * Copyright (C) 2016 Noralf Trønnes
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#ifndef __LINUX_DRM_SIMPLE_KMS_HELPER_H
#define __LINUX_DRM_SIMPLE_KMS_HELPER_H

struct drm_simple_display_pipe;

/**
 * struct drm_simple_display_pipe_funcs - helper operations for a simple
 *                                        display pipeline
 */
struct drm_simple_display_pipe_funcs {
	/**
	 * @enable:
	 *
	 * This function should be used to enable the pipeline.
	 * It is called when the underlying crtc is enabled.
	 * This hook is optional.
	 */
	void (*enable)(struct drm_simple_display_pipe *pipe,
		       struct drm_crtc_state *crtc_state);
	/**
	 * @disable:
	 *
	 * This function should be used to disable the pipeline.
	 * It is called when the underlying crtc is disabled.
	 * This hook is optional.
	 */
	void (*disable)(struct drm_simple_display_pipe *pipe);

	/**
	 * @check:
	 *
	 * This function is called in the check phase of an atomic update,
	 * specifically when the underlying plane is checked.
	 * The simple display pipeline helpers already check that the plane is
	 * not scaled, fills the entire visible area and is always enabled
	 * when the crtc is also enabled.
	 * This hook is optional.
	 *
	 * RETURNS:
	 *
	 * 0 on success, -EINVAL if the state or the transition can't be
	 * supported, -ENOMEM on memory allocation failure and -EDEADLK if an
	 * attempt to obtain another state object ran into a &drm_modeset_lock
	 * deadlock.
	 */
	int (*check)(struct drm_simple_display_pipe *pipe,
		     struct drm_plane_state *plane_state,
		     struct drm_crtc_state *crtc_state);
	/**
	 * @update:
	 *
	 * This function is called when the underlying plane state is updated.
	 * This hook is optional.
	 *
	 * This is the function drivers should submit the
	 * &drm_pending_vblank_event from. Using either
	 * drm_crtc_arm_vblank_event(), when the driver supports vblank
	 * interrupt handling, or drm_crtc_send_vblank_event() directly in case
	 * the hardware lacks vblank support entirely.
	 */
	void (*update)(struct drm_simple_display_pipe *pipe,
		       struct drm_plane_state *plane_state);

	/**
	 * @prepare_fb:
	 *
	 * Optional, called by struct &drm_plane_helper_funcs ->prepare_fb .
	 * Please read the documentation for the ->prepare_fb hook in
	 * struct &drm_plane_helper_funcs for more details.
	 */
	int (*prepare_fb)(struct drm_simple_display_pipe *pipe,
			  struct drm_plane_state *plane_state);

	/**
	 * @cleanup_fb:
	 *
	 * Optional, called by struct &drm_plane_helper_funcs ->cleanup_fb .
	 * Please read the documentation for the ->cleanup_fb hook in
	 * struct &drm_plane_helper_funcs for more details.
	 */
	void (*cleanup_fb)(struct drm_simple_display_pipe *pipe,
			   struct drm_plane_state *plane_state);
};

/**
 * struct drm_simple_display_pipe - simple display pipeline
 * @crtc: CRTC control structure
 * @plane: Plane control structure
 * @encoder: Encoder control structure
 * @connector: Connector control structure
 * @funcs: Pipeline control functions (optional)
 *
 * Simple display pipeline with plane, crtc and encoder collapsed into one
 * entity. It should be initialized by calling drm_simple_display_pipe_init().
 */
struct drm_simple_display_pipe {
	struct drm_crtc crtc;
	struct drm_plane plane;
	struct drm_encoder encoder;
	struct drm_connector *connector;

	const struct drm_simple_display_pipe_funcs *funcs;
};

int drm_simple_display_pipe_attach_bridge(struct drm_simple_display_pipe *pipe,
					  struct drm_bridge *bridge);

void drm_simple_display_pipe_detach_bridge(struct drm_simple_display_pipe *pipe);

int drm_simple_display_pipe_init(struct drm_device *dev,
			struct drm_simple_display_pipe *pipe,
			const struct drm_simple_display_pipe_funcs *funcs,
			const uint32_t *formats, unsigned int format_count,
			struct drm_connector *connector);

#endif /* __LINUX_DRM_SIMPLE_KMS_HELPER_H */
