/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Copyright (C) 2016 Noralf Trønnes
 */

#ifndef __LINUX_DRM_SIMPLE_KMS_HELPER_H
#define __LINUX_DRM_SIMPLE_KMS_HELPER_H

#include <drm/drm_crtc.h>
#include <drm/drm_encoder.h>
#include <drm/drm_plane.h>

struct drm_simple_display_pipe;

/**
 * struct drm_simple_display_pipe_funcs - helper operations for a simple
 *                                        display pipeline
 */
struct drm_simple_display_pipe_funcs {
	/**
	 * @mode_valid:
	 *
	 * This callback is used to check if a specific mode is valid in the
	 * crtc used in this simple display pipe. This should be implemented
	 * if the display pipe has some sort of restriction in the modes
	 * it can display. For example, a given display pipe may be responsible
	 * to set a clock value. If the clock can not produce all the values
	 * for the available modes then this callback can be used to restrict
	 * the number of modes to only the ones that can be displayed. Another
	 * reason can be bandwidth mitigation: the memory port on the display
	 * controller can have bandwidth limitations not allowing pixel data
	 * to be fetched at any rate.
	 *
	 * This hook is used by the probe helpers to filter the mode list in
	 * drm_helper_probe_single_connector_modes(), and it is used by the
	 * atomic helpers to validate modes supplied by userspace in
	 * drm_atomic_helper_check_modeset().
	 *
	 * This function is optional.
	 *
	 * NOTE:
	 *
	 * Since this function is both called from the check phase of an atomic
	 * commit, and the mode validation in the probe paths it is not allowed
	 * to look at anything else but the passed-in mode, and validate it
	 * against configuration-invariant hardware constraints.
	 *
	 * RETURNS:
	 *
	 * drm_mode_status Enum
	 */
	enum drm_mode_status (*mode_valid)(struct drm_simple_display_pipe *pipe,
					   const struct drm_display_mode *mode);

	/**
	 * @enable:
	 *
	 * This function should be used to enable the pipeline.
	 * It is called when the underlying crtc is enabled.
	 * This hook is optional.
	 */
	void (*enable)(struct drm_simple_display_pipe *pipe,
		       struct drm_crtc_state *crtc_state,
		       struct drm_plane_state *plane_state);
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
	 * interrupt handling, or drm_crtc_send_vblank_event() for more
	 * complex case. In case the hardware lacks vblank support entirely,
	 * drivers can set &struct drm_crtc_state.no_vblank in
	 * &struct drm_simple_display_pipe_funcs.check and let DRM's
	 * atomic helper fake a vblank event.
	 */
	void (*update)(struct drm_simple_display_pipe *pipe,
		       struct drm_plane_state *old_plane_state);

	/**
	 * @prepare_fb:
	 *
	 * Optional, called by &drm_plane_helper_funcs.prepare_fb.  Please read
	 * the documentation for the &drm_plane_helper_funcs.prepare_fb hook for
	 * more details.
	 *
	 * Drivers which always have their buffers pinned should use
	 * drm_gem_fb_simple_display_pipe_prepare_fb() for this hook.
	 */
	int (*prepare_fb)(struct drm_simple_display_pipe *pipe,
			  struct drm_plane_state *plane_state);

	/**
	 * @cleanup_fb:
	 *
	 * Optional, called by &drm_plane_helper_funcs.cleanup_fb.  Please read
	 * the documentation for the &drm_plane_helper_funcs.cleanup_fb hook for
	 * more details.
	 */
	void (*cleanup_fb)(struct drm_simple_display_pipe *pipe,
			   struct drm_plane_state *plane_state);

	/**
	 * @enable_vblank:
	 *
	 * Optional, called by &drm_crtc_funcs.enable_vblank. Please read
	 * the documentation for the &drm_crtc_funcs.enable_vblank hook for
	 * more details.
	 */
	int (*enable_vblank)(struct drm_simple_display_pipe *pipe);

	/**
	 * @disable_vblank:
	 *
	 * Optional, called by &drm_crtc_funcs.disable_vblank. Please read
	 * the documentation for the &drm_crtc_funcs.disable_vblank hook for
	 * more details.
	 */
	void (*disable_vblank)(struct drm_simple_display_pipe *pipe);
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

int drm_simple_display_pipe_init(struct drm_device *dev,
			struct drm_simple_display_pipe *pipe,
			const struct drm_simple_display_pipe_funcs *funcs,
			const uint32_t *formats, unsigned int format_count,
			const uint64_t *format_modifiers,
			struct drm_connector *connector);

int drm_simple_encoder_init(struct drm_device *dev,
			    struct drm_encoder *encoder,
			    int encoder_type);

#endif /* __LINUX_DRM_SIMPLE_KMS_HELPER_H */
