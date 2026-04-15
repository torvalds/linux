/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Copyright (C) 2016 Noralf Trønnes
 */

/*
 * Simple KMS helpers are deprected in favor of regular atomic helpers. Do not
 * use the min new code.
 */

#ifndef __LINUX_DRM_SIMPLE_KMS_HELPER_H
#define __LINUX_DRM_SIMPLE_KMS_HELPER_H

#include <drm/drm_crtc.h>
#include <drm/drm_encoder.h>
#include <drm/drm_plane.h>

struct drm_simple_display_pipe;

struct drm_simple_display_pipe_funcs {
	enum drm_mode_status (*mode_valid)(struct drm_simple_display_pipe *pipe,
					   const struct drm_display_mode *mode);
	void (*enable)(struct drm_simple_display_pipe *pipe,
		       struct drm_crtc_state *crtc_state,
		       struct drm_plane_state *plane_state);
	void (*disable)(struct drm_simple_display_pipe *pipe);
	int (*check)(struct drm_simple_display_pipe *pipe,
		     struct drm_plane_state *plane_state,
		     struct drm_crtc_state *crtc_state);
	void (*update)(struct drm_simple_display_pipe *pipe,
		       struct drm_plane_state *old_plane_state);
	int (*prepare_fb)(struct drm_simple_display_pipe *pipe,
			  struct drm_plane_state *plane_state);
	void (*cleanup_fb)(struct drm_simple_display_pipe *pipe,
			   struct drm_plane_state *plane_state);
	int (*begin_fb_access)(struct drm_simple_display_pipe *pipe,
			       struct drm_plane_state *new_plane_state);
	void (*end_fb_access)(struct drm_simple_display_pipe *pipe,
			      struct drm_plane_state *plane_state);
	int (*enable_vblank)(struct drm_simple_display_pipe *pipe);
	void (*disable_vblank)(struct drm_simple_display_pipe *pipe);
	void (*reset_crtc)(struct drm_simple_display_pipe *pipe);
	struct drm_crtc_state * (*duplicate_crtc_state)(struct drm_simple_display_pipe *pipe);
	void (*destroy_crtc_state)(struct drm_simple_display_pipe *pipe,
				   struct drm_crtc_state *crtc_state);
	void (*reset_plane)(struct drm_simple_display_pipe *pipe);
	struct drm_plane_state * (*duplicate_plane_state)(struct drm_simple_display_pipe *pipe);
	void (*destroy_plane_state)(struct drm_simple_display_pipe *pipe,
				    struct drm_plane_state *plane_state);
};

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

void *__drmm_simple_encoder_alloc(struct drm_device *dev, size_t size,
				  size_t offset, int encoder_type);

#define drmm_simple_encoder_alloc(dev, type, member, encoder_type) \
	((type *)__drmm_simple_encoder_alloc(dev, sizeof(type), \
					     offsetof(type, member), \
					     encoder_type))

#endif /* __LINUX_DRM_SIMPLE_KMS_HELPER_H */
