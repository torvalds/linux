/*
 * Copyright © 2006 Keith Packard
 * Copyright © 2007-2008 Dave Airlie
 * Copyright © 2007-2008 Intel Corporation
 *   Jesse Barnes <jesse.barnes@intel.com>
 * Copyright © 2011-2013 Intel Corporation
 * Copyright © 2015 Intel Corporation
 *   Daniel Vetter <daniel.vetter@ffwll.ch>
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

#ifndef __DRM_MODESET_HELPER_VTABLES_H__
#define __DRM_MODESET_HELPER_VTABLES_H__

#include <drm/drm_crtc.h>

/**
 * DOC: overview
 *
 * The DRM mode setting helper functions are common code for drivers to use if
 * they wish.  Drivers are not forced to use this code in their
 * implementations but it would be useful if the code they do use at least
 * provides a consistent interface and operation to userspace. Therefore it is
 * highly recommended to use the provided helpers as much as possible.
 *
 * Because there is only one pointer per modeset object to hold a vfunc table
 * for helper libraries they are by necessity shared among the different
 * helpers.
 *
 * To make this clear all the helper vtables are pulled together in this location here.
 */

enum mode_set_atomic;

/**
 * struct drm_crtc_helper_funcs - helper operations for CRTCs
 * @dpms: set power state
 * @prepare: prepare the CRTC, called before @mode_set
 * @commit: commit changes to CRTC, called after @mode_set
 * @mode_fixup: try to fixup proposed mode for this CRTC
 * @mode_set: set this mode
 * @mode_set_nofb: set mode only (no scanout buffer attached)
 * @mode_set_base: update the scanout buffer
 * @mode_set_base_atomic: non-blocking mode set (used for kgdb support)
 * @load_lut: load color palette
 * @disable: disable CRTC when no longer in use
 * @enable: enable CRTC
 * @atomic_check: check for validity of an atomic state
 * @atomic_begin: begin atomic update
 * @atomic_flush: flush atomic update
 *
 * The helper operations are called by the mid-layer CRTC helper.
 *
 * Note that with atomic helpers @dpms, @prepare and @commit hooks are
 * deprecated. Used @enable and @disable instead exclusively.
 *
 * With legacy crtc helpers there's a big semantic difference between @disable
 * and the other hooks: @disable also needs to release any resources acquired in
 * @mode_set (like shared PLLs).
 */
struct drm_crtc_helper_funcs {
	/*
	 * Control power levels on the CRTC.  If the mode passed in is
	 * unsupported, the provider must use the next lowest power level.
	 */
	void (*dpms)(struct drm_crtc *crtc, int mode);
	void (*prepare)(struct drm_crtc *crtc);
	void (*commit)(struct drm_crtc *crtc);

	/* Provider can fixup or change mode timings before modeset occurs */
	bool (*mode_fixup)(struct drm_crtc *crtc,
			   const struct drm_display_mode *mode,
			   struct drm_display_mode *adjusted_mode);
	/* Actually set the mode */
	int (*mode_set)(struct drm_crtc *crtc, struct drm_display_mode *mode,
			struct drm_display_mode *adjusted_mode, int x, int y,
			struct drm_framebuffer *old_fb);
	/* Actually set the mode for atomic helpers, optional */
	void (*mode_set_nofb)(struct drm_crtc *crtc);

	/* Move the crtc on the current fb to the given position *optional* */
	int (*mode_set_base)(struct drm_crtc *crtc, int x, int y,
			     struct drm_framebuffer *old_fb);
	int (*mode_set_base_atomic)(struct drm_crtc *crtc,
				    struct drm_framebuffer *fb, int x, int y,
				    enum mode_set_atomic);

	/* reload the current crtc LUT */
	void (*load_lut)(struct drm_crtc *crtc);

	void (*disable)(struct drm_crtc *crtc);
	void (*enable)(struct drm_crtc *crtc);

	/* atomic helpers */
	int (*atomic_check)(struct drm_crtc *crtc,
			    struct drm_crtc_state *state);
	void (*atomic_begin)(struct drm_crtc *crtc,
			     struct drm_crtc_state *old_crtc_state);
	void (*atomic_flush)(struct drm_crtc *crtc,
			     struct drm_crtc_state *old_crtc_state);
};

/**
 * drm_crtc_helper_add - sets the helper vtable for a crtc
 * @crtc: DRM CRTC
 * @funcs: helper vtable to set for @crtc
 */
static inline void drm_crtc_helper_add(struct drm_crtc *crtc,
				       const struct drm_crtc_helper_funcs *funcs)
{
	crtc->helper_private = funcs;
}

/**
 * struct drm_encoder_helper_funcs - helper operations for encoders
 * @dpms: set power state
 * @save: save connector state
 * @restore: restore connector state
 * @mode_fixup: try to fixup proposed mode for this connector
 * @prepare: part of the disable sequence, called before the CRTC modeset
 * @commit: called after the CRTC modeset
 * @mode_set: set this mode, optional for atomic helpers
 * @get_crtc: return CRTC that the encoder is currently attached to
 * @detect: connection status detection
 * @disable: disable encoder when not in use (overrides DPMS off)
 * @enable: enable encoder
 * @atomic_check: check for validity of an atomic update
 *
 * The helper operations are called by the mid-layer CRTC helper.
 *
 * Note that with atomic helpers @dpms, @prepare and @commit hooks are
 * deprecated. Used @enable and @disable instead exclusively.
 *
 * With legacy crtc helpers there's a big semantic difference between @disable
 * and the other hooks: @disable also needs to release any resources acquired in
 * @mode_set (like shared PLLs).
 */
struct drm_encoder_helper_funcs {
	void (*dpms)(struct drm_encoder *encoder, int mode);
	void (*save)(struct drm_encoder *encoder);
	void (*restore)(struct drm_encoder *encoder);

	bool (*mode_fixup)(struct drm_encoder *encoder,
			   const struct drm_display_mode *mode,
			   struct drm_display_mode *adjusted_mode);
	void (*prepare)(struct drm_encoder *encoder);
	void (*commit)(struct drm_encoder *encoder);
	void (*mode_set)(struct drm_encoder *encoder,
			 struct drm_display_mode *mode,
			 struct drm_display_mode *adjusted_mode);
	struct drm_crtc *(*get_crtc)(struct drm_encoder *encoder);
	/* detect for DAC style encoders */
	enum drm_connector_status (*detect)(struct drm_encoder *encoder,
					    struct drm_connector *connector);
	void (*disable)(struct drm_encoder *encoder);

	void (*enable)(struct drm_encoder *encoder);

	/* atomic helpers */
	int (*atomic_check)(struct drm_encoder *encoder,
			    struct drm_crtc_state *crtc_state,
			    struct drm_connector_state *conn_state);
};

/**
 * drm_encoder_helper_add - sets the helper vtable for a encoder
 * @encoder: DRM encoder
 * @funcs: helper vtable to set for @encoder
 */
static inline void drm_encoder_helper_add(struct drm_encoder *encoder,
					  const struct drm_encoder_helper_funcs *funcs)
{
	encoder->helper_private = funcs;
}

/**
 * struct drm_connector_helper_funcs - helper operations for connectors
 * @get_modes: get mode list for this connector
 * @mode_valid: is this mode valid on the given connector? (optional)
 * @best_encoder: return the preferred encoder for this connector
 * @atomic_best_encoder: atomic version of @best_encoder
 *
 * The helper operations are called by the mid-layer CRTC helper.
 */
struct drm_connector_helper_funcs {
	int (*get_modes)(struct drm_connector *connector);
	enum drm_mode_status (*mode_valid)(struct drm_connector *connector,
					   struct drm_display_mode *mode);
	struct drm_encoder *(*best_encoder)(struct drm_connector *connector);
	struct drm_encoder *(*atomic_best_encoder)(struct drm_connector *connector,
						   struct drm_connector_state *connector_state);
};

/**
 * drm_connector_helper_add - sets the helper vtable for a connector
 * @connector: DRM connector
 * @funcs: helper vtable to set for @connector
 */
static inline void drm_connector_helper_add(struct drm_connector *connector,
					    const struct drm_connector_helper_funcs *funcs)
{
	connector->helper_private = funcs;
}

/**
 * struct drm_plane_helper_funcs - helper operations for CRTCs
 * @prepare_fb: prepare a framebuffer for use by the plane
 * @cleanup_fb: cleanup a framebuffer when it's no longer used by the plane
 * @atomic_check: check that a given atomic state is valid and can be applied
 * @atomic_update: apply an atomic state to the plane (mandatory)
 * @atomic_disable: disable the plane
 *
 * The helper operations are called by the mid-layer CRTC helper.
 */
struct drm_plane_helper_funcs {
	int (*prepare_fb)(struct drm_plane *plane,
			  const struct drm_plane_state *new_state);
	void (*cleanup_fb)(struct drm_plane *plane,
			   const struct drm_plane_state *old_state);

	int (*atomic_check)(struct drm_plane *plane,
			    struct drm_plane_state *state);
	void (*atomic_update)(struct drm_plane *plane,
			      struct drm_plane_state *old_state);
	void (*atomic_disable)(struct drm_plane *plane,
			       struct drm_plane_state *old_state);
};

/**
 * drm_plane_helper_add - sets the helper vtable for a plane
 * @plane: DRM plane
 * @funcs: helper vtable to set for @plane
 */
static inline void drm_plane_helper_add(struct drm_plane *plane,
					const struct drm_plane_helper_funcs *funcs)
{
	plane->helper_private = funcs;
}

#endif
