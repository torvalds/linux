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
 *
 * These hooks are used by the legacy CRTC helpers, the transitional plane
 * helpers and the new atomic modesetting helpers.
 */
struct drm_crtc_helper_funcs {
	/**
	 * @dpms:
	 *
	 * Callback to control power levels on the CRTC.  If the mode passed in
	 * is unsupported, the provider must use the next lowest power level.
	 * This is used by the legacy CRTC helpers to implement DPMS
	 * functionality in drm_helper_connector_dpms().
	 *
	 * This callback is also used to disable a CRTC by calling it with
	 * DRM_MODE_DPMS_OFF if the @disable hook isn't used.
	 *
	 * This callback is used by the legacy CRTC helpers.  Atomic helpers
	 * also support using this hook for enabling and disabling a CRTC to
	 * facilitate transitions to atomic, but it is deprecated. Instead
	 * @enable and @disable should be used.
	 */
	void (*dpms)(struct drm_crtc *crtc, int mode);

	/**
	 * @prepare:
	 *
	 * This callback should prepare the CRTC for a subsequent modeset, which
	 * in practice means the driver should disable the CRTC if it is
	 * running. Most drivers ended up implementing this by calling their
	 * @dpms hook with DRM_MODE_DPMS_OFF.
	 *
	 * This callback is used by the legacy CRTC helpers.  Atomic helpers
	 * also support using this hook for disabling a CRTC to facilitate
	 * transitions to atomic, but it is deprecated. Instead @disable should
	 * be used.
	 */
	void (*prepare)(struct drm_crtc *crtc);

	/**
	 * @commit:
	 *
	 * This callback should commit the new mode on the CRTC after a modeset,
	 * which in practice means the driver should enable the CRTC.  Most
	 * drivers ended up implementing this by calling their @dpms hook with
	 * DRM_MODE_DPMS_ON.
	 *
	 * This callback is used by the legacy CRTC helpers.  Atomic helpers
	 * also support using this hook for enabling a CRTC to facilitate
	 * transitions to atomic, but it is deprecated. Instead @enable should
	 * be used.
	 */
	void (*commit)(struct drm_crtc *crtc);

	/**
	 * @mode_fixup:
	 *
	 * This callback is used to validate a mode. The parameter mode is the
	 * display mode that userspace requested, adjusted_mode is the mode the
	 * encoders need to be fed with. Note that this is the inverse semantics
	 * of the meaning for the &drm_encoder and &drm_bridge
	 * ->mode_fixup() functions. If the CRTC cannot support the requested
	 * conversion from mode to adjusted_mode it should reject the modeset.
	 *
	 * This function is used by both legacy CRTC helpers and atomic helpers.
	 * With atomic helpers it is optional.
	 *
	 * NOTE:
	 *
	 * This function is called in the check phase of atomic modesets, which
	 * can be aborted for any reason (including on userspace's request to
	 * just check whether a configuration would be possible). Atomic drivers
	 * MUST NOT touch any persistent state (hardware or software) or data
	 * structures except the passed in adjusted_mode parameter.
	 *
	 * This is in contrast to the legacy CRTC helpers where this was
	 * allowed.
	 *
	 * Atomic drivers which need to inspect and adjust more state should
	 * instead use the @atomic_check callback.
	 *
	 * Also beware that neither core nor helpers filter modes before
	 * passing them to the driver: While the list of modes that is
	 * advertised to userspace is filtered using the connector's
	 * ->mode_valid() callback, neither the core nor the helpers do any
	 * filtering on modes passed in from userspace when setting a mode. It
	 * is therefore possible for userspace to pass in a mode that was
	 * previously filtered out using ->mode_valid() or add a custom mode
	 * that wasn't probed from EDID or similar to begin with.  Even though
	 * this is an advanced feature and rarely used nowadays, some users rely
	 * on being able to specify modes manually so drivers must be prepared
	 * to deal with it. Specifically this means that all drivers need not
	 * only validate modes in ->mode_valid() but also in ->mode_fixup() to
	 * make sure invalid modes passed in from userspace are rejected.
	 *
	 * RETURNS:
	 *
	 * True if an acceptable configuration is possible, false if the modeset
	 * operation should be rejected.
	 */
	bool (*mode_fixup)(struct drm_crtc *crtc,
			   const struct drm_display_mode *mode,
			   struct drm_display_mode *adjusted_mode);

	/**
	 * @mode_set:
	 *
	 * This callback is used by the legacy CRTC helpers to set a new mode,
	 * position and framebuffer. Since it ties the primary plane to every
	 * mode change it is incompatible with universal plane support. And
	 * since it can't update other planes it's incompatible with atomic
	 * modeset support.
	 *
	 * This callback is only used by CRTC helpers and deprecated.
	 *
	 * RETURNS:
	 *
	 * 0 on success or a negative error code on failure.
	 */
	int (*mode_set)(struct drm_crtc *crtc, struct drm_display_mode *mode,
			struct drm_display_mode *adjusted_mode, int x, int y,
			struct drm_framebuffer *old_fb);

	/**
	 * @mode_set_nofb:
	 *
	 * This callback is used to update the display mode of a CRTC without
	 * changing anything of the primary plane configuration. This fits the
	 * requirement of atomic and hence is used by the atomic helpers. It is
	 * also used by the transitional plane helpers to implement a
	 * @mode_set hook in drm_helper_crtc_mode_set().
	 *
	 * Note that the display pipe is completely off when this function is
	 * called. Atomic drivers which need hardware to be running before they
	 * program the new display mode (e.g. because they implement runtime PM)
	 * should not use this hook. This is because the helper library calls
	 * this hook only once per mode change and not every time the display
	 * pipeline is suspended using either DPMS or the new "ACTIVE" property.
	 * Which means register values set in this callback might get reset when
	 * the CRTC is suspended, but not restored.  Such drivers should instead
	 * move all their CRTC setup into the @enable callback.
	 *
	 * This callback is optional.
	 */
	void (*mode_set_nofb)(struct drm_crtc *crtc);

	/**
	 * @mode_set_base:
	 *
	 * This callback is used by the legacy CRTC helpers to set a new
	 * framebuffer and scanout position. It is optional and used as an
	 * optimized fast-path instead of a full mode set operation with all the
	 * resulting flickering. If it is not present
	 * drm_crtc_helper_set_config() will fall back to a full modeset, using
	 * the ->mode_set() callback. Since it can't update other planes it's
	 * incompatible with atomic modeset support.
	 *
	 * This callback is only used by the CRTC helpers and deprecated.
	 *
	 * RETURNS:
	 *
	 * 0 on success or a negative error code on failure.
	 */
	int (*mode_set_base)(struct drm_crtc *crtc, int x, int y,
			     struct drm_framebuffer *old_fb);

	/**
	 * @mode_set_base_atomic:
	 *
	 * This callback is used by the fbdev helpers to set a new framebuffer
	 * and scanout without sleeping, i.e. from an atomic calling context. It
	 * is only used to implement kgdb support.
	 *
	 * This callback is optional and only needed for kgdb support in the fbdev
	 * helpers.
	 *
	 * RETURNS:
	 *
	 * 0 on success or a negative error code on failure.
	 */
	int (*mode_set_base_atomic)(struct drm_crtc *crtc,
				    struct drm_framebuffer *fb, int x, int y,
				    enum mode_set_atomic);

	/**
	 * @load_lut:
	 *
	 * Load a LUT prepared with the @gamma_set functions from
	 * &drm_fb_helper_funcs.
	 *
	 * This callback is optional and is only used by the fbdev emulation
	 * helpers.
	 *
	 * FIXME:
	 *
	 * This callback is functionally redundant with the core gamma table
	 * support and simply exists because the fbdev hasn't yet been
	 * refactored to use the core gamma table interfaces.
	 */
	void (*load_lut)(struct drm_crtc *crtc);

	/**
	 * @disable:
	 *
	 * This callback should be used to disable the CRTC. With the atomic
	 * drivers it is called after all encoders connected to this CRTC have
	 * been shut off already using their own ->disable hook. If that
	 * sequence is too simple drivers can just add their own hooks and call
	 * it from this CRTC callback here by looping over all encoders
	 * connected to it using for_each_encoder_on_crtc().
	 *
	 * This hook is used both by legacy CRTC helpers and atomic helpers.
	 * Atomic drivers don't need to implement it if there's no need to
	 * disable anything at the CRTC level. To ensure that runtime PM
	 * handling (using either DPMS or the new "ACTIVE" property) works
	 * @disable must be the inverse of @enable for atomic drivers.
	 *
	 * NOTE:
	 *
	 * With legacy CRTC helpers there's a big semantic difference between
	 * @disable and other hooks (like @prepare or @dpms) used to shut down a
	 * CRTC: @disable is only called when also logically disabling the
	 * display pipeline and needs to release any resources acquired in
	 * @mode_set (like shared PLLs, or again release pinned framebuffers).
	 *
	 * Therefore @disable must be the inverse of @mode_set plus @commit for
	 * drivers still using legacy CRTC helpers, which is different from the
	 * rules under atomic.
	 */
	void (*disable)(struct drm_crtc *crtc);

	/**
	 * @enable:
	 *
	 * This callback should be used to enable the CRTC. With the atomic
	 * drivers it is called before all encoders connected to this CRTC are
	 * enabled through the encoder's own ->enable hook.  If that sequence is
	 * too simple drivers can just add their own hooks and call it from this
	 * CRTC callback here by looping over all encoders connected to it using
	 * for_each_encoder_on_crtc().
	 *
	 * This hook is used only by atomic helpers, for symmetry with @disable.
	 * Atomic drivers don't need to implement it if there's no need to
	 * enable anything at the CRTC level. To ensure that runtime PM handling
	 * (using either DPMS or the new "ACTIVE" property) works
	 * @enable must be the inverse of @disable for atomic drivers.
	 */
	void (*enable)(struct drm_crtc *crtc);

	/**
	 * @atomic_check:
	 *
	 * Drivers should check plane-update related CRTC constraints in this
	 * hook. They can also check mode related limitations but need to be
	 * aware of the calling order, since this hook is used by
	 * drm_atomic_helper_check_planes() whereas the preparations needed to
	 * check output routing and the display mode is done in
	 * drm_atomic_helper_check_modeset(). Therefore drivers that want to
	 * check output routing and display mode constraints in this callback
	 * must ensure that drm_atomic_helper_check_modeset() has been called
	 * beforehand. This is calling order used by the default helper
	 * implementation in drm_atomic_helper_check().
	 *
	 * When using drm_atomic_helper_check_planes() CRTCs' ->atomic_check()
	 * hooks are called after the ones for planes, which allows drivers to
	 * assign shared resources requested by planes in the CRTC callback
	 * here. For more complicated dependencies the driver can call the provided
	 * check helpers multiple times until the computed state has a final
	 * configuration and everything has been checked.
	 *
	 * This function is also allowed to inspect any other object's state and
	 * can add more state objects to the atomic commit if needed. Care must
	 * be taken though to ensure that state check&compute functions for
	 * these added states are all called, and derived state in other objects
	 * all updated. Again the recommendation is to just call check helpers
	 * until a maximal configuration is reached.
	 *
	 * This callback is used by the atomic modeset helpers and by the
	 * transitional plane helpers, but it is optional.
	 *
	 * NOTE:
	 *
	 * This function is called in the check phase of an atomic update. The
	 * driver is not allowed to change anything outside of the free-standing
	 * state objects passed-in or assembled in the overall &drm_atomic_state
	 * update tracking structure.
	 *
	 * RETURNS:
	 *
	 * 0 on success, -EINVAL if the state or the transition can't be
	 * supported, -ENOMEM on memory allocation failure and -EDEADLK if an
	 * attempt to obtain another state object ran into a &drm_modeset_lock
	 * deadlock.
	 */
	int (*atomic_check)(struct drm_crtc *crtc,
			    struct drm_crtc_state *state);

	/**
	 * @atomic_begin:
	 *
	 * Drivers should prepare for an atomic update of multiple planes on
	 * a CRTC in this hook. Depending upon hardware this might be vblank
	 * evasion, blocking updates by setting bits or doing preparatory work
	 * for e.g. manual update display.
	 *
	 * This hook is called before any plane commit functions are called.
	 *
	 * Note that the power state of the display pipe when this function is
	 * called depends upon the exact helpers and calling sequence the driver
	 * has picked. See drm_atomic_commit_planes() for a discussion of the
	 * tradeoffs and variants of plane commit helpers.
	 *
	 * This callback is used by the atomic modeset helpers and by the
	 * transitional plane helpers, but it is optional.
	 */
	void (*atomic_begin)(struct drm_crtc *crtc,
			     struct drm_crtc_state *old_crtc_state);
	/**
	 * @atomic_flush:
	 *
	 * Drivers should finalize an atomic update of multiple planes on
	 * a CRTC in this hook. Depending upon hardware this might include
	 * checking that vblank evasion was successful, unblocking updates by
	 * setting bits or setting the GO bit to flush out all updates.
	 *
	 * Simple hardware or hardware with special requirements can commit and
	 * flush out all updates for all planes from this hook and forgo all the
	 * other commit hooks for plane updates.
	 *
	 * This hook is called after any plane commit functions are called.
	 *
	 * Note that the power state of the display pipe when this function is
	 * called depends upon the exact helpers and calling sequence the driver
	 * has picked. See drm_atomic_commit_planes() for a discussion of the
	 * tradeoffs and variants of plane commit helpers.
	 *
	 * This callback is used by the atomic modeset helpers and by the
	 * transitional plane helpers, but it is optional.
	 */
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
 *
 * These hooks are used by the legacy CRTC helpers, the transitional plane
 * helpers and the new atomic modesetting helpers.
 */
struct drm_encoder_helper_funcs {
	/**
	 * @dpms:
	 *
	 * Callback to control power levels on the encoder.  If the mode passed in
	 * is unsupported, the provider must use the next lowest power level.
	 * This is used by the legacy encoder helpers to implement DPMS
	 * functionality in drm_helper_connector_dpms().
	 *
	 * This callback is also used to disable an encoder by calling it with
	 * DRM_MODE_DPMS_OFF if the @disable hook isn't used.
	 *
	 * This callback is used by the legacy CRTC helpers.  Atomic helpers
	 * also support using this hook for enabling and disabling an encoder to
	 * facilitate transitions to atomic, but it is deprecated. Instead
	 * @enable and @disable should be used.
	 */
	void (*dpms)(struct drm_encoder *encoder, int mode);

	/**
	 * @mode_fixup:
	 *
	 * This callback is used to validate and adjust a mode. The parameter
	 * mode is the display mode that should be fed to the next element in
	 * the display chain, either the final &drm_connector or a &drm_bridge.
	 * The parameter adjusted_mode is the input mode the encoder requires. It
	 * can be modified by this callback and does not need to match mode.
	 *
	 * This function is used by both legacy CRTC helpers and atomic helpers.
	 * With atomic helpers it is optional.
	 *
	 * NOTE:
	 *
	 * This function is called in the check phase of atomic modesets, which
	 * can be aborted for any reason (including on userspace's request to
	 * just check whether a configuration would be possible). Atomic drivers
	 * MUST NOT touch any persistent state (hardware or software) or data
	 * structures except the passed in adjusted_mode parameter.
	 *
	 * This is in contrast to the legacy CRTC helpers where this was
	 * allowed.
	 *
	 * Atomic drivers which need to inspect and adjust more state should
	 * instead use the @atomic_check callback.
	 *
	 * Also beware that neither core nor helpers filter modes before
	 * passing them to the driver: While the list of modes that is
	 * advertised to userspace is filtered using the connector's
	 * ->mode_valid() callback, neither the core nor the helpers do any
	 * filtering on modes passed in from userspace when setting a mode. It
	 * is therefore possible for userspace to pass in a mode that was
	 * previously filtered out using ->mode_valid() or add a custom mode
	 * that wasn't probed from EDID or similar to begin with.  Even though
	 * this is an advanced feature and rarely used nowadays, some users rely
	 * on being able to specify modes manually so drivers must be prepared
	 * to deal with it. Specifically this means that all drivers need not
	 * only validate modes in ->mode_valid() but also in ->mode_fixup() to
	 * make sure invalid modes passed in from userspace are rejected.
	 *
	 * RETURNS:
	 *
	 * True if an acceptable configuration is possible, false if the modeset
	 * operation should be rejected.
	 */
	bool (*mode_fixup)(struct drm_encoder *encoder,
			   const struct drm_display_mode *mode,
			   struct drm_display_mode *adjusted_mode);

	/**
	 * @prepare:
	 *
	 * This callback should prepare the encoder for a subsequent modeset,
	 * which in practice means the driver should disable the encoder if it
	 * is running. Most drivers ended up implementing this by calling their
	 * @dpms hook with DRM_MODE_DPMS_OFF.
	 *
	 * This callback is used by the legacy CRTC helpers.  Atomic helpers
	 * also support using this hook for disabling an encoder to facilitate
	 * transitions to atomic, but it is deprecated. Instead @disable should
	 * be used.
	 */
	void (*prepare)(struct drm_encoder *encoder);

	/**
	 * @commit:
	 *
	 * This callback should commit the new mode on the encoder after a modeset,
	 * which in practice means the driver should enable the encoder.  Most
	 * drivers ended up implementing this by calling their @dpms hook with
	 * DRM_MODE_DPMS_ON.
	 *
	 * This callback is used by the legacy CRTC helpers.  Atomic helpers
	 * also support using this hook for enabling an encoder to facilitate
	 * transitions to atomic, but it is deprecated. Instead @enable should
	 * be used.
	 */
	void (*commit)(struct drm_encoder *encoder);

	/**
	 * @mode_set:
	 *
	 * This callback is used to update the display mode of an encoder.
	 *
	 * Note that the display pipe is completely off when this function is
	 * called. Drivers which need hardware to be running before they program
	 * the new display mode (because they implement runtime PM) should not
	 * use this hook, because the helper library calls it only once and not
	 * every time the display pipeline is suspend using either DPMS or the
	 * new "ACTIVE" property. Such drivers should instead move all their
	 * encoder setup into the ->enable() callback.
	 *
	 * This callback is used both by the legacy CRTC helpers and the atomic
	 * modeset helpers. It is optional in the atomic helpers.
	 */
	void (*mode_set)(struct drm_encoder *encoder,
			 struct drm_display_mode *mode,
			 struct drm_display_mode *adjusted_mode);

	/**
	 * @get_crtc:
	 *
	 * This callback is used by the legacy CRTC helpers to work around
	 * deficiencies in its own book-keeping.
	 *
	 * Do not use, use atomic helpers instead, which get the book keeping
	 * right.
	 *
	 * FIXME:
	 *
	 * Currently only nouveau is using this, and as soon as nouveau is
	 * atomic we can ditch this hook.
	 */
	struct drm_crtc *(*get_crtc)(struct drm_encoder *encoder);

	/**
	 * @detect:
	 *
	 * This callback can be used by drivers who want to do detection on the
	 * encoder object instead of in connector functions.
	 *
	 * It is not used by any helper and therefore has purely driver-specific
	 * semantics. New drivers shouldn't use this and instead just implement
	 * their own private callbacks.
	 *
	 * FIXME:
	 *
	 * This should just be converted into a pile of driver vfuncs.
	 * Currently radeon, amdgpu and nouveau are using it.
	 */
	enum drm_connector_status (*detect)(struct drm_encoder *encoder,
					    struct drm_connector *connector);

	/**
	 * @disable:
	 *
	 * This callback should be used to disable the encoder. With the atomic
	 * drivers it is called before this encoder's CRTC has been shut off
	 * using the CRTC's own ->disable hook.  If that sequence is too simple
	 * drivers can just add their own driver private encoder hooks and call
	 * them from CRTC's callback by looping over all encoders connected to
	 * it using for_each_encoder_on_crtc().
	 *
	 * This hook is used both by legacy CRTC helpers and atomic helpers.
	 * Atomic drivers don't need to implement it if there's no need to
	 * disable anything at the encoder level. To ensure that runtime PM
	 * handling (using either DPMS or the new "ACTIVE" property) works
	 * @disable must be the inverse of @enable for atomic drivers.
	 *
	 * NOTE:
	 *
	 * With legacy CRTC helpers there's a big semantic difference between
	 * @disable and other hooks (like @prepare or @dpms) used to shut down a
	 * encoder: @disable is only called when also logically disabling the
	 * display pipeline and needs to release any resources acquired in
	 * @mode_set (like shared PLLs, or again release pinned framebuffers).
	 *
	 * Therefore @disable must be the inverse of @mode_set plus @commit for
	 * drivers still using legacy CRTC helpers, which is different from the
	 * rules under atomic.
	 */
	void (*disable)(struct drm_encoder *encoder);

	/**
	 * @enable:
	 *
	 * This callback should be used to enable the encoder. With the atomic
	 * drivers it is called after this encoder's CRTC has been enabled using
	 * the CRTC's own ->enable hook.  If that sequence is too simple drivers
	 * can just add their own driver private encoder hooks and call them
	 * from CRTC's callback by looping over all encoders connected to it
	 * using for_each_encoder_on_crtc().
	 *
	 * This hook is used only by atomic helpers, for symmetry with @disable.
	 * Atomic drivers don't need to implement it if there's no need to
	 * enable anything at the encoder level. To ensure that runtime PM handling
	 * (using either DPMS or the new "ACTIVE" property) works
	 * @enable must be the inverse of @disable for atomic drivers.
	 */
	void (*enable)(struct drm_encoder *encoder);

	/**
	 * @atomic_check:
	 *
	 * This callback is used to validate encoder state for atomic drivers.
	 * Since the encoder is the object connecting the CRTC and connector it
	 * gets passed both states, to be able to validate interactions and
	 * update the CRTC to match what the encoder needs for the requested
	 * connector.
	 *
	 * This function is used by the atomic helpers, but it is optional.
	 *
	 * NOTE:
	 *
	 * This function is called in the check phase of an atomic update. The
	 * driver is not allowed to change anything outside of the free-standing
	 * state objects passed-in or assembled in the overall &drm_atomic_state
	 * update tracking structure.
	 *
	 * RETURNS:
	 *
	 * 0 on success, -EINVAL if the state or the transition can't be
	 * supported, -ENOMEM on memory allocation failure and -EDEADLK if an
	 * attempt to obtain another state object ran into a &drm_modeset_lock
	 * deadlock.
	 */
	int (*atomic_check)(struct drm_encoder *encoder,
			    struct drm_crtc_state *crtc_state,
			    struct drm_connector_state *conn_state);
};

/**
 * drm_encoder_helper_add - sets the helper vtable for an encoder
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
 *
 * These functions are used by the atomic and legacy modeset helpers and by the
 * probe helpers.
 */
struct drm_connector_helper_funcs {
	/**
	 * @get_modes:
	 *
	 * This function should fill in all modes currently valid for the sink
	 * into the connector->probed_modes list. It should also update the
	 * EDID property by calling drm_mode_connector_update_edid_property().
	 *
	 * The usual way to implement this is to cache the EDID retrieved in the
	 * probe callback somewhere in the driver-private connector structure.
	 * In this function drivers then parse the modes in the EDID and add
	 * them by calling drm_add_edid_modes(). But connectors that driver a
	 * fixed panel can also manually add specific modes using
	 * drm_mode_probed_add(). Drivers which manually add modes should also
	 * make sure that the @display_info, @width_mm and @height_mm fields of the
	 * struct #drm_connector are filled in.
	 *
	 * Virtual drivers that just want some standard VESA mode with a given
	 * resolution can call drm_add_modes_noedid(), and mark the preferred
	 * one using drm_set_preferred_mode().
	 *
	 * Finally drivers that support audio probably want to update the ELD
	 * data, too, using drm_edid_to_eld().
	 *
	 * This function is only called after the ->detect() hook has indicated
	 * that a sink is connected and when the EDID isn't overridden through
	 * sysfs or the kernel commandline.
	 *
	 * This callback is used by the probe helpers in e.g.
	 * drm_helper_probe_single_connector_modes().
	 *
	 * RETURNS:
	 *
	 * The number of modes added by calling drm_mode_probed_add().
	 */
	int (*get_modes)(struct drm_connector *connector);

	/**
	 * @mode_valid:
	 *
	 * Callback to validate a mode for a connector, irrespective of the
	 * specific display configuration.
	 *
	 * This callback is used by the probe helpers to filter the mode list
	 * (which is usually derived from the EDID data block from the sink).
	 * See e.g. drm_helper_probe_single_connector_modes().
	 *
	 * NOTE:
	 *
	 * This only filters the mode list supplied to userspace in the
	 * GETCONNECOTR IOCTL. Userspace is free to create modes of its own and
	 * ask the kernel to use them. It this case the atomic helpers or legacy
	 * CRTC helpers will not call this function. Drivers therefore must
	 * still fully validate any mode passed in in a modeset request.
	 *
	 * RETURNS:
	 *
	 * Either MODE_OK or one of the failure reasons in enum
	 * &drm_mode_status.
	 */
	enum drm_mode_status (*mode_valid)(struct drm_connector *connector,
					   struct drm_display_mode *mode);
	/**
	 * @best_encoder:
	 *
	 * This function should select the best encoder for the given connector.
	 *
	 * This function is used by both the atomic helpers (in the
	 * drm_atomic_helper_check_modeset() function) and in the legacy CRTC
	 * helpers.
	 *
	 * NOTE:
	 *
	 * In atomic drivers this function is called in the check phase of an
	 * atomic update. The driver is not allowed to change or inspect
	 * anything outside of arguments passed-in. Atomic drivers which need to
	 * inspect dynamic configuration state should instead use
	 * @atomic_best_encoder.
	 *
	 * RETURNS:
	 *
	 * Encoder that should be used for the given connector and connector
	 * state, or NULL if no suitable encoder exists. Note that the helpers
	 * will ensure that encoders aren't used twice, drivers should not check
	 * for this.
	 */
	struct drm_encoder *(*best_encoder)(struct drm_connector *connector);

	/**
	 * @atomic_best_encoder:
	 *
	 * This is the atomic version of @best_encoder for atomic drivers which
	 * need to select the best encoder depending upon the desired
	 * configuration and can't select it statically.
	 *
	 * This function is used by drm_atomic_helper_check_modeset() and either
	 * this or @best_encoder is required.
	 *
	 * NOTE:
	 *
	 * This function is called in the check phase of an atomic update. The
	 * driver is not allowed to change anything outside of the free-standing
	 * state objects passed-in or assembled in the overall &drm_atomic_state
	 * update tracking structure.
	 *
	 * RETURNS:
	 *
	 * Encoder that should be used for the given connector and connector
	 * state, or NULL if no suitable encoder exists. Note that the helpers
	 * will ensure that encoders aren't used twice, drivers should not check
	 * for this.
	 */
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
 * struct drm_plane_helper_funcs - helper operations for planes
 *
 * These functions are used by the atomic helpers and by the transitional plane
 * helpers.
 */
struct drm_plane_helper_funcs {
	/**
	 * @prepare_fb:
	 *
	 * This hook is to prepare a framebuffer for scanout by e.g. pinning
	 * it's backing storage or relocating it into a contiguous block of
	 * VRAM. Other possible preparatory work includes flushing caches.
	 *
	 * This function must not block for outstanding rendering, since it is
	 * called in the context of the atomic IOCTL even for async commits to
	 * be able to return any errors to userspace. Instead the recommended
	 * way is to fill out the fence member of the passed-in
	 * &drm_plane_state. If the driver doesn't support native fences then
	 * equivalent functionality should be implemented through private
	 * members in the plane structure.
	 *
	 * The helpers will call @cleanup_fb with matching arguments for every
	 * successful call to this hook.
	 *
	 * This callback is used by the atomic modeset helpers and by the
	 * transitional plane helpers, but it is optional.
	 *
	 * RETURNS:
	 *
	 * 0 on success or one of the following negative error codes allowed by
	 * the atomic_commit hook in &drm_mode_config_funcs. When using helpers
	 * this callback is the only one which can fail an atomic commit,
	 * everything else must complete successfully.
	 */
	int (*prepare_fb)(struct drm_plane *plane,
			  const struct drm_plane_state *new_state);
	/**
	 * @cleanup_fb:
	 *
	 * This hook is called to clean up any resources allocated for the given
	 * framebuffer and plane configuration in @prepare_fb.
	 *
	 * This callback is used by the atomic modeset helpers and by the
	 * transitional plane helpers, but it is optional.
	 */
	void (*cleanup_fb)(struct drm_plane *plane,
			   const struct drm_plane_state *old_state);

	/**
	 * @atomic_check:
	 *
	 * Drivers should check plane specific constraints in this hook.
	 *
	 * When using drm_atomic_helper_check_planes() plane's ->atomic_check()
	 * hooks are called before the ones for CRTCs, which allows drivers to
	 * request shared resources that the CRTC controls here. For more
	 * complicated dependencies the driver can call the provided check helpers
	 * multiple times until the computed state has a final configuration and
	 * everything has been checked.
	 *
	 * This function is also allowed to inspect any other object's state and
	 * can add more state objects to the atomic commit if needed. Care must
	 * be taken though to ensure that state check&compute functions for
	 * these added states are all called, and derived state in other objects
	 * all updated. Again the recommendation is to just call check helpers
	 * until a maximal configuration is reached.
	 *
	 * This callback is used by the atomic modeset helpers and by the
	 * transitional plane helpers, but it is optional.
	 *
	 * NOTE:
	 *
	 * This function is called in the check phase of an atomic update. The
	 * driver is not allowed to change anything outside of the free-standing
	 * state objects passed-in or assembled in the overall &drm_atomic_state
	 * update tracking structure.
	 *
	 * RETURNS:
	 *
	 * 0 on success, -EINVAL if the state or the transition can't be
	 * supported, -ENOMEM on memory allocation failure and -EDEADLK if an
	 * attempt to obtain another state object ran into a &drm_modeset_lock
	 * deadlock.
	 */
	int (*atomic_check)(struct drm_plane *plane,
			    struct drm_plane_state *state);

	/**
	 * @atomic_update:
	 *
	 * Drivers should use this function to update the plane state.  This
	 * hook is called in-between the ->atomic_begin() and
	 * ->atomic_flush() of &drm_crtc_helper_funcs.
	 *
	 * Note that the power state of the display pipe when this function is
	 * called depends upon the exact helpers and calling sequence the driver
	 * has picked. See drm_atomic_commit_planes() for a discussion of the
	 * tradeoffs and variants of plane commit helpers.
	 *
	 * This callback is used by the atomic modeset helpers and by the
	 * transitional plane helpers, but it is optional.
	 */
	void (*atomic_update)(struct drm_plane *plane,
			      struct drm_plane_state *old_state);
	/**
	 * @atomic_disable:
	 *
	 * Drivers should use this function to unconditionally disable a plane.
	 * This hook is called in-between the ->atomic_begin() and
	 * ->atomic_flush() of &drm_crtc_helper_funcs. It is an alternative to
	 * @atomic_update, which will be called for disabling planes, too, if
	 * the @atomic_disable hook isn't implemented.
	 *
	 * This hook is also useful to disable planes in preparation of a modeset,
	 * by calling drm_atomic_helper_disable_planes_on_crtc() from the
	 * ->disable() hook in &drm_crtc_helper_funcs.
	 *
	 * Note that the power state of the display pipe when this function is
	 * called depends upon the exact helpers and calling sequence the driver
	 * has picked. See drm_atomic_commit_planes() for a discussion of the
	 * tradeoffs and variants of plane commit helpers.
	 *
	 * This callback is used by the atomic modeset helpers and by the
	 * transitional plane helpers, but it is optional.
	 */
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
