// SPDX-License-Identifier: MIT
/*
 * Copyright © 2023 Intel Corporation
 */

#include "i915_drv.h"
#include "intel_crtc.h"
#include "intel_display_types.h"
#include "intel_sprite_uapi.h"

static bool has_dst_key_in_primary_plane(struct drm_i915_private *dev_priv)
{
	return DISPLAY_VER(dev_priv) >= 9;
}

static void intel_plane_set_ckey(struct intel_plane_state *plane_state,
				 const struct drm_intel_sprite_colorkey *set)
{
	struct intel_plane *plane = to_intel_plane(plane_state->uapi.plane);
	struct drm_i915_private *dev_priv = to_i915(plane->base.dev);
	struct drm_intel_sprite_colorkey *key = &plane_state->ckey;

	*key = *set;

	/*
	 * We want src key enabled on the
	 * sprite and not on the primary.
	 */
	if (plane->id == PLANE_PRIMARY &&
	    set->flags & I915_SET_COLORKEY_SOURCE)
		key->flags = 0;

	/*
	 * On SKL+ we want dst key enabled on
	 * the primary and not on the sprite.
	 */
	if (DISPLAY_VER(dev_priv) >= 9 && plane->id != PLANE_PRIMARY &&
	    set->flags & I915_SET_COLORKEY_DESTINATION)
		key->flags = 0;
}

int intel_sprite_set_colorkey_ioctl(struct drm_device *dev, void *data,
				    struct drm_file *file_priv)
{
	struct drm_i915_private *dev_priv = to_i915(dev);
	struct drm_intel_sprite_colorkey *set = data;
	struct drm_plane *plane;
	struct drm_plane_state *plane_state;
	struct drm_atomic_state *state;
	struct drm_modeset_acquire_ctx ctx;
	int ret = 0;

	/* ignore the pointless "none" flag */
	set->flags &= ~I915_SET_COLORKEY_NONE;

	if (set->flags & ~(I915_SET_COLORKEY_DESTINATION | I915_SET_COLORKEY_SOURCE))
		return -EINVAL;

	/* Make sure we don't try to enable both src & dest simultaneously */
	if ((set->flags & (I915_SET_COLORKEY_DESTINATION | I915_SET_COLORKEY_SOURCE)) == (I915_SET_COLORKEY_DESTINATION | I915_SET_COLORKEY_SOURCE))
		return -EINVAL;

	if ((IS_VALLEYVIEW(dev_priv) || IS_CHERRYVIEW(dev_priv)) &&
	    set->flags & I915_SET_COLORKEY_DESTINATION)
		return -EINVAL;

	plane = drm_plane_find(dev, file_priv, set->plane_id);
	if (!plane || plane->type != DRM_PLANE_TYPE_OVERLAY)
		return -ENOENT;

	/*
	 * SKL+ only plane 2 can do destination keying against plane 1.
	 * Also multiple planes can't do destination keying on the same
	 * pipe simultaneously.
	 */
	if (DISPLAY_VER(dev_priv) >= 9 &&
	    to_intel_plane(plane)->id >= PLANE_3 &&
	    set->flags & I915_SET_COLORKEY_DESTINATION)
		return -EINVAL;

	drm_modeset_acquire_init(&ctx, 0);

	state = drm_atomic_state_alloc(plane->dev);
	if (!state) {
		ret = -ENOMEM;
		goto out;
	}
	state->acquire_ctx = &ctx;
	to_intel_atomic_state(state)->internal = true;

	while (1) {
		plane_state = drm_atomic_get_plane_state(state, plane);
		ret = PTR_ERR_OR_ZERO(plane_state);
		if (!ret)
			intel_plane_set_ckey(to_intel_plane_state(plane_state), set);

		/*
		 * On some platforms we have to configure
		 * the dst colorkey on the primary plane.
		 */
		if (!ret && has_dst_key_in_primary_plane(dev_priv)) {
			struct intel_crtc *crtc =
				intel_crtc_for_pipe(dev_priv,
						    to_intel_plane(plane)->pipe);

			plane_state = drm_atomic_get_plane_state(state,
								 crtc->base.primary);
			ret = PTR_ERR_OR_ZERO(plane_state);
			if (!ret)
				intel_plane_set_ckey(to_intel_plane_state(plane_state), set);
		}

		if (!ret)
			ret = drm_atomic_commit(state);

		if (ret != -EDEADLK)
			break;

		drm_atomic_state_clear(state);
		drm_modeset_backoff(&ctx);
	}

	drm_atomic_state_put(state);
out:
	drm_modeset_drop_locks(&ctx);
	drm_modeset_acquire_fini(&ctx);
	return ret;
}
