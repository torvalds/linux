/*
 * Copyright (c) 2016 Intel Corporation
 *
 * Permission to use, copy, modify, distribute, and sell this software and its
 * documentation for any purpose is hereby granted without fee, provided that
 * the above copyright notice appear in all copies and that both that copyright
 * notice and this permission notice appear in supporting documentation, and
 * that the name of the copyright holders not be used in advertising or
 * publicity pertaining to distribution of the software without specific,
 * written prior permission.  The copyright holders make no representations
 * about the suitability of this software for any purpose.  It is provided "as
 * is" without express or implied warranty.
 *
 * THE COPYRIGHT HOLDERS DISCLAIM ALL WARRANTIES WITH REGARD TO THIS SOFTWARE,
 * INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS, IN NO
 * EVENT SHALL THE COPYRIGHT HOLDERS BE LIABLE FOR ANY SPECIAL, INDIRECT OR
 * CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE,
 * DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER
 * TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE
 * OF THIS SOFTWARE.
 */

#ifndef __DRM_BRIDGE_H__
#define __DRM_BRIDGE_H__

#include <linux/ctype.h>
#include <linux/list.h>
#include <linux/mutex.h>

#include <drm/drm_atomic.h>
#include <drm/drm_encoder.h>
#include <drm/drm_mode_object.h>
#include <drm/drm_modes.h>

struct device_node;

struct drm_bridge;
struct drm_bridge_timings;
struct drm_connector;
struct drm_display_info;
struct drm_minor;
struct drm_panel;
struct edid;
struct i2c_adapter;

/**
 * enum drm_bridge_attach_flags - Flags for &drm_bridge_funcs.attach
 */
enum drm_bridge_attach_flags {
	/**
	 * @DRM_BRIDGE_ATTACH_NO_CONNECTOR: When this flag is set the bridge
	 * shall not create a drm_connector.
	 */
	DRM_BRIDGE_ATTACH_NO_CONNECTOR = BIT(0),
};

/**
 * struct drm_bridge_funcs - drm_bridge control functions
 */
struct drm_bridge_funcs {
	/**
	 * @attach:
	 *
	 * This callback is invoked whenever our bridge is being attached to a
	 * &drm_encoder. The flags argument tunes the behaviour of the attach
	 * operation (see DRM_BRIDGE_ATTACH_*).
	 *
	 * The @attach callback is optional.
	 *
	 * RETURNS:
	 *
	 * Zero on success, error code on failure.
	 */
	int (*attach)(struct drm_bridge *bridge,
		      enum drm_bridge_attach_flags flags);

	/**
	 * @detach:
	 *
	 * This callback is invoked whenever our bridge is being detached from a
	 * &drm_encoder.
	 *
	 * The @detach callback is optional.
	 */
	void (*detach)(struct drm_bridge *bridge);

	/**
	 * @mode_valid:
	 *
	 * This callback is used to check if a specific mode is valid in this
	 * bridge. This should be implemented if the bridge has some sort of
	 * restriction in the modes it can display. For example, a given bridge
	 * may be responsible to set a clock value. If the clock can not
	 * produce all the values for the available modes then this callback
	 * can be used to restrict the number of modes to only the ones that
	 * can be displayed.
	 *
	 * This hook is used by the probe helpers to filter the mode list in
	 * drm_helper_probe_single_connector_modes(), and it is used by the
	 * atomic helpers to validate modes supplied by userspace in
	 * drm_atomic_helper_check_modeset().
	 *
	 * The @mode_valid callback is optional.
	 *
	 * NOTE:
	 *
	 * Since this function is both called from the check phase of an atomic
	 * commit, and the mode validation in the probe paths it is not allowed
	 * to look at anything else but the passed-in mode, and validate it
	 * against configuration-invariant hardware constraints. Any further
	 * limits which depend upon the configuration can only be checked in
	 * @mode_fixup.
	 *
	 * RETURNS:
	 *
	 * drm_mode_status Enum
	 */
	enum drm_mode_status (*mode_valid)(struct drm_bridge *bridge,
					   const struct drm_display_info *info,
					   const struct drm_display_mode *mode);

	/**
	 * @mode_fixup:
	 *
	 * This callback is used to validate and adjust a mode. The parameter
	 * mode is the display mode that should be fed to the next element in
	 * the display chain, either the final &drm_connector or the next
	 * &drm_bridge. The parameter adjusted_mode is the input mode the bridge
	 * requires. It can be modified by this callback and does not need to
	 * match mode. See also &drm_crtc_state.adjusted_mode for more details.
	 *
	 * This is the only hook that allows a bridge to reject a modeset. If
	 * this function passes all other callbacks must succeed for this
	 * configuration.
	 *
	 * The mode_fixup callback is optional. &drm_bridge_funcs.mode_fixup()
	 * is not called when &drm_bridge_funcs.atomic_check() is implemented,
	 * so only one of them should be provided.
	 *
	 * NOTE:
	 *
	 * This function is called in the check phase of atomic modesets, which
	 * can be aborted for any reason (including on userspace's request to
	 * just check whether a configuration would be possible). Drivers MUST
	 * NOT touch any persistent state (hardware or software) or data
	 * structures except the passed in @state parameter.
	 *
	 * Also beware that userspace can request its own custom modes, neither
	 * core nor helpers filter modes to the list of probe modes reported by
	 * the GETCONNECTOR IOCTL and stored in &drm_connector.modes. To ensure
	 * that modes are filtered consistently put any bridge constraints and
	 * limits checks into @mode_valid.
	 *
	 * RETURNS:
	 *
	 * True if an acceptable configuration is possible, false if the modeset
	 * operation should be rejected.
	 */
	bool (*mode_fixup)(struct drm_bridge *bridge,
			   const struct drm_display_mode *mode,
			   struct drm_display_mode *adjusted_mode);
	/**
	 * @disable:
	 *
	 * This callback should disable the bridge. It is called right before
	 * the preceding element in the display pipe is disabled. If the
	 * preceding element is a bridge this means it's called before that
	 * bridge's @disable vfunc. If the preceding element is a &drm_encoder
	 * it's called right before the &drm_encoder_helper_funcs.disable,
	 * &drm_encoder_helper_funcs.prepare or &drm_encoder_helper_funcs.dpms
	 * hook.
	 *
	 * The bridge can assume that the display pipe (i.e. clocks and timing
	 * signals) feeding it is still running when this callback is called.
	 *
	 * The @disable callback is optional.
	 *
	 * NOTE:
	 *
	 * This is deprecated, do not use!
	 * New drivers shall use &drm_bridge_funcs.atomic_disable.
	 */
	void (*disable)(struct drm_bridge *bridge);

	/**
	 * @post_disable:
	 *
	 * This callback should disable the bridge. It is called right after the
	 * preceding element in the display pipe is disabled. If the preceding
	 * element is a bridge this means it's called after that bridge's
	 * @post_disable function. If the preceding element is a &drm_encoder
	 * it's called right after the encoder's
	 * &drm_encoder_helper_funcs.disable, &drm_encoder_helper_funcs.prepare
	 * or &drm_encoder_helper_funcs.dpms hook.
	 *
	 * The bridge must assume that the display pipe (i.e. clocks and timing
	 * signals) feeding it is no longer running when this callback is
	 * called.
	 *
	 * The @post_disable callback is optional.
	 *
	 * NOTE:
	 *
	 * This is deprecated, do not use!
	 * New drivers shall use &drm_bridge_funcs.atomic_post_disable.
	 */
	void (*post_disable)(struct drm_bridge *bridge);

	/**
	 * @mode_set:
	 *
	 * This callback should set the given mode on the bridge. It is called
	 * after the @mode_set callback for the preceding element in the display
	 * pipeline has been called already. If the bridge is the first element
	 * then this would be &drm_encoder_helper_funcs.mode_set. The display
	 * pipe (i.e.  clocks and timing signals) is off when this function is
	 * called.
	 *
	 * The adjusted_mode parameter is the mode output by the CRTC for the
	 * first bridge in the chain. It can be different from the mode
	 * parameter that contains the desired mode for the connector at the end
	 * of the bridges chain, for instance when the first bridge in the chain
	 * performs scaling. The adjusted mode is mostly useful for the first
	 * bridge in the chain and is likely irrelevant for the other bridges.
	 *
	 * For atomic drivers the adjusted_mode is the mode stored in
	 * &drm_crtc_state.adjusted_mode.
	 *
	 * NOTE:
	 *
	 * This is deprecated, do not use!
	 * New drivers shall set their mode in the
	 * &drm_bridge_funcs.atomic_enable operation.
	 */
	void (*mode_set)(struct drm_bridge *bridge,
			 const struct drm_display_mode *mode,
			 const struct drm_display_mode *adjusted_mode);
	/**
	 * @pre_enable:
	 *
	 * This callback should enable the bridge. It is called right before
	 * the preceding element in the display pipe is enabled. If the
	 * preceding element is a bridge this means it's called before that
	 * bridge's @pre_enable function. If the preceding element is a
	 * &drm_encoder it's called right before the encoder's
	 * &drm_encoder_helper_funcs.enable, &drm_encoder_helper_funcs.commit or
	 * &drm_encoder_helper_funcs.dpms hook.
	 *
	 * The display pipe (i.e. clocks and timing signals) feeding this bridge
	 * will not yet be running when this callback is called. The bridge must
	 * not enable the display link feeding the next bridge in the chain (if
	 * there is one) when this callback is called.
	 *
	 * The @pre_enable callback is optional.
	 *
	 * NOTE:
	 *
	 * This is deprecated, do not use!
	 * New drivers shall use &drm_bridge_funcs.atomic_pre_enable.
	 */
	void (*pre_enable)(struct drm_bridge *bridge);

	/**
	 * @enable:
	 *
	 * This callback should enable the bridge. It is called right after
	 * the preceding element in the display pipe is enabled. If the
	 * preceding element is a bridge this means it's called after that
	 * bridge's @enable function. If the preceding element is a
	 * &drm_encoder it's called right after the encoder's
	 * &drm_encoder_helper_funcs.enable, &drm_encoder_helper_funcs.commit or
	 * &drm_encoder_helper_funcs.dpms hook.
	 *
	 * The bridge can assume that the display pipe (i.e. clocks and timing
	 * signals) feeding it is running when this callback is called. This
	 * callback must enable the display link feeding the next bridge in the
	 * chain if there is one.
	 *
	 * The @enable callback is optional.
	 *
	 * NOTE:
	 *
	 * This is deprecated, do not use!
	 * New drivers shall use &drm_bridge_funcs.atomic_enable.
	 */
	void (*enable)(struct drm_bridge *bridge);

	/**
	 * @atomic_pre_enable:
	 *
	 * This callback should enable the bridge. It is called right before
	 * the preceding element in the display pipe is enabled. If the
	 * preceding element is a bridge this means it's called before that
	 * bridge's @atomic_pre_enable or @pre_enable function. If the preceding
	 * element is a &drm_encoder it's called right before the encoder's
	 * &drm_encoder_helper_funcs.atomic_enable hook.
	 *
	 * The display pipe (i.e. clocks and timing signals) feeding this bridge
	 * will not yet be running when this callback is called. The bridge must
	 * not enable the display link feeding the next bridge in the chain (if
	 * there is one) when this callback is called.
	 *
	 * The @atomic_pre_enable callback is optional.
	 */
	void (*atomic_pre_enable)(struct drm_bridge *bridge,
				  struct drm_bridge_state *old_bridge_state);

	/**
	 * @atomic_enable:
	 *
	 * This callback should enable the bridge. It is called right after
	 * the preceding element in the display pipe is enabled. If the
	 * preceding element is a bridge this means it's called after that
	 * bridge's @atomic_enable or @enable function. If the preceding element
	 * is a &drm_encoder it's called right after the encoder's
	 * &drm_encoder_helper_funcs.atomic_enable hook.
	 *
	 * The bridge can assume that the display pipe (i.e. clocks and timing
	 * signals) feeding it is running when this callback is called. This
	 * callback must enable the display link feeding the next bridge in the
	 * chain if there is one.
	 *
	 * The @atomic_enable callback is optional.
	 */
	void (*atomic_enable)(struct drm_bridge *bridge,
			      struct drm_bridge_state *old_bridge_state);
	/**
	 * @atomic_disable:
	 *
	 * This callback should disable the bridge. It is called right before
	 * the preceding element in the display pipe is disabled. If the
	 * preceding element is a bridge this means it's called before that
	 * bridge's @atomic_disable or @disable vfunc. If the preceding element
	 * is a &drm_encoder it's called right before the
	 * &drm_encoder_helper_funcs.atomic_disable hook.
	 *
	 * The bridge can assume that the display pipe (i.e. clocks and timing
	 * signals) feeding it is still running when this callback is called.
	 *
	 * The @atomic_disable callback is optional.
	 */
	void (*atomic_disable)(struct drm_bridge *bridge,
			       struct drm_bridge_state *old_bridge_state);

	/**
	 * @atomic_post_disable:
	 *
	 * This callback should disable the bridge. It is called right after the
	 * preceding element in the display pipe is disabled. If the preceding
	 * element is a bridge this means it's called after that bridge's
	 * @atomic_post_disable or @post_disable function. If the preceding
	 * element is a &drm_encoder it's called right after the encoder's
	 * &drm_encoder_helper_funcs.atomic_disable hook.
	 *
	 * The bridge must assume that the display pipe (i.e. clocks and timing
	 * signals) feeding it is no longer running when this callback is
	 * called.
	 *
	 * The @atomic_post_disable callback is optional.
	 */
	void (*atomic_post_disable)(struct drm_bridge *bridge,
				    struct drm_bridge_state *old_bridge_state);

	/**
	 * @atomic_duplicate_state:
	 *
	 * Duplicate the current bridge state object (which is guaranteed to be
	 * non-NULL).
	 *
	 * The atomic_duplicate_state hook is mandatory if the bridge
	 * implements any of the atomic hooks, and should be left unassigned
	 * otherwise. For bridges that don't subclass &drm_bridge_state, the
	 * drm_atomic_helper_bridge_duplicate_state() helper function shall be
	 * used to implement this hook.
	 *
	 * RETURNS:
	 * A valid drm_bridge_state object or NULL if the allocation fails.
	 */
	struct drm_bridge_state *(*atomic_duplicate_state)(struct drm_bridge *bridge);

	/**
	 * @atomic_destroy_state:
	 *
	 * Destroy a bridge state object previously allocated by
	 * &drm_bridge_funcs.atomic_duplicate_state().
	 *
	 * The atomic_destroy_state hook is mandatory if the bridge implements
	 * any of the atomic hooks, and should be left unassigned otherwise.
	 * For bridges that don't subclass &drm_bridge_state, the
	 * drm_atomic_helper_bridge_destroy_state() helper function shall be
	 * used to implement this hook.
	 */
	void (*atomic_destroy_state)(struct drm_bridge *bridge,
				     struct drm_bridge_state *state);

	/**
	 * @atomic_get_output_bus_fmts:
	 *
	 * Return the supported bus formats on the output end of a bridge.
	 * The returned array must be allocated with kmalloc() and will be
	 * freed by the caller. If the allocation fails, NULL should be
	 * returned. num_output_fmts must be set to the returned array size.
	 * Formats listed in the returned array should be listed in decreasing
	 * preference order (the core will try all formats until it finds one
	 * that works).
	 *
	 * This method is only called on the last element of the bridge chain
	 * as part of the bus format negotiation process that happens in
	 * &drm_atomic_bridge_chain_select_bus_fmts().
	 * This method is optional. When not implemented, the core will
	 * fall back to &drm_connector.display_info.bus_formats[0] if
	 * &drm_connector.display_info.num_bus_formats > 0,
	 * or to MEDIA_BUS_FMT_FIXED otherwise.
	 */
	u32 *(*atomic_get_output_bus_fmts)(struct drm_bridge *bridge,
					   struct drm_bridge_state *bridge_state,
					   struct drm_crtc_state *crtc_state,
					   struct drm_connector_state *conn_state,
					   unsigned int *num_output_fmts);

	/**
	 * @atomic_get_input_bus_fmts:
	 *
	 * Return the supported bus formats on the input end of a bridge for
	 * a specific output bus format.
	 *
	 * The returned array must be allocated with kmalloc() and will be
	 * freed by the caller. If the allocation fails, NULL should be
	 * returned. num_input_fmts must be set to the returned array size.
	 * Formats listed in the returned array should be listed in decreasing
	 * preference order (the core will try all formats until it finds one
	 * that works). When the format is not supported NULL should be
	 * returned and num_input_fmts should be set to 0.
	 *
	 * This method is called on all elements of the bridge chain as part of
	 * the bus format negotiation process that happens in
	 * drm_atomic_bridge_chain_select_bus_fmts().
	 * This method is optional. When not implemented, the core will bypass
	 * bus format negotiation on this element of the bridge without
	 * failing, and the previous element in the chain will be passed
	 * MEDIA_BUS_FMT_FIXED as its output bus format.
	 *
	 * Bridge drivers that need to support being linked to bridges that are
	 * not supporting bus format negotiation should handle the
	 * output_fmt == MEDIA_BUS_FMT_FIXED case appropriately, by selecting a
	 * sensible default value or extracting this information from somewhere
	 * else (FW property, &drm_display_mode, &drm_display_info, ...)
	 *
	 * Note: Even if input format selection on the first bridge has no
	 * impact on the negotiation process (bus format negotiation stops once
	 * we reach the first element of the chain), drivers are expected to
	 * return accurate input formats as the input format may be used to
	 * configure the CRTC output appropriately.
	 */
	u32 *(*atomic_get_input_bus_fmts)(struct drm_bridge *bridge,
					  struct drm_bridge_state *bridge_state,
					  struct drm_crtc_state *crtc_state,
					  struct drm_connector_state *conn_state,
					  u32 output_fmt,
					  unsigned int *num_input_fmts);

	/**
	 * @atomic_check:
	 *
	 * This method is responsible for checking bridge state correctness.
	 * It can also check the state of the surrounding components in chain
	 * to make sure the whole pipeline can work properly.
	 *
	 * &drm_bridge_funcs.atomic_check() hooks are called in reverse
	 * order (from the last to the first bridge).
	 *
	 * This method is optional. &drm_bridge_funcs.mode_fixup() is not
	 * called when &drm_bridge_funcs.atomic_check() is implemented, so only
	 * one of them should be provided.
	 *
	 * If drivers need to tweak &drm_bridge_state.input_bus_cfg.flags or
	 * &drm_bridge_state.output_bus_cfg.flags it should happen in
	 * this function. By default the &drm_bridge_state.output_bus_cfg.flags
	 * field is set to the next bridge
	 * &drm_bridge_state.input_bus_cfg.flags value or
	 * &drm_connector.display_info.bus_flags if the bridge is the last
	 * element in the chain.
	 *
	 * RETURNS:
	 * zero if the check passed, a negative error code otherwise.
	 */
	int (*atomic_check)(struct drm_bridge *bridge,
			    struct drm_bridge_state *bridge_state,
			    struct drm_crtc_state *crtc_state,
			    struct drm_connector_state *conn_state);

	/**
	 * @atomic_reset:
	 *
	 * Reset the bridge to a predefined state (or retrieve its current
	 * state) and return a &drm_bridge_state object matching this state.
	 * This function is called at attach time.
	 *
	 * The atomic_reset hook is mandatory if the bridge implements any of
	 * the atomic hooks, and should be left unassigned otherwise. For
	 * bridges that don't subclass &drm_bridge_state, the
	 * drm_atomic_helper_bridge_reset() helper function shall be used to
	 * implement this hook.
	 *
	 * Note that the atomic_reset() semantics is not exactly matching the
	 * reset() semantics found on other components (connector, plane, ...).
	 *
	 * 1. The reset operation happens when the bridge is attached, not when
	 *    drm_mode_config_reset() is called
	 * 2. It's meant to be used exclusively on bridges that have been
	 *    converted to the ATOMIC API
	 *
	 * RETURNS:
	 * A valid drm_bridge_state object in case of success, an ERR_PTR()
	 * giving the reason of the failure otherwise.
	 */
	struct drm_bridge_state *(*atomic_reset)(struct drm_bridge *bridge);

	/**
	 * @detect:
	 *
	 * Check if anything is attached to the bridge output.
	 *
	 * This callback is optional, if not implemented the bridge will be
	 * considered as always having a component attached to its output.
	 * Bridges that implement this callback shall set the
	 * DRM_BRIDGE_OP_DETECT flag in their &drm_bridge->ops.
	 *
	 * RETURNS:
	 *
	 * drm_connector_status indicating the bridge output status.
	 */
	enum drm_connector_status (*detect)(struct drm_bridge *bridge);

	/**
	 * @get_modes:
	 *
	 * Fill all modes currently valid for the sink into the &drm_connector
	 * with drm_mode_probed_add().
	 *
	 * The @get_modes callback is mostly intended to support non-probeable
	 * displays such as many fixed panels. Bridges that support reading
	 * EDID shall leave @get_modes unimplemented and implement the
	 * &drm_bridge_funcs->edid_read callback instead.
	 *
	 * This callback is optional. Bridges that implement it shall set the
	 * DRM_BRIDGE_OP_MODES flag in their &drm_bridge->ops.
	 *
	 * The connector parameter shall be used for the sole purpose of
	 * filling modes, and shall not be stored internally by bridge drivers
	 * for future usage.
	 *
	 * RETURNS:
	 *
	 * The number of modes added by calling drm_mode_probed_add().
	 */
	int (*get_modes)(struct drm_bridge *bridge,
			 struct drm_connector *connector);

	/**
	 * @edid_read:
	 *
	 * Read the EDID data of the connected display.
	 *
	 * The @edid_read callback is the preferred way of reporting mode
	 * information for a display connected to the bridge output. Bridges
	 * that support reading EDID shall implement this callback and leave
	 * the @get_modes callback unimplemented.
	 *
	 * The caller of this operation shall first verify the output
	 * connection status and refrain from reading EDID from a disconnected
	 * output.
	 *
	 * This callback is optional. Bridges that implement it shall set the
	 * DRM_BRIDGE_OP_EDID flag in their &drm_bridge->ops.
	 *
	 * The connector parameter shall be used for the sole purpose of EDID
	 * retrieval, and shall not be stored internally by bridge drivers for
	 * future usage.
	 *
	 * RETURNS:
	 *
	 * An edid structure newly allocated with drm_edid_alloc() or returned
	 * from drm_edid_read() family of functions on success, or NULL
	 * otherwise. The caller is responsible for freeing the returned edid
	 * structure with drm_edid_free().
	 */
	const struct drm_edid *(*edid_read)(struct drm_bridge *bridge,
					    struct drm_connector *connector);

	/**
	 * @hpd_notify:
	 *
	 * Notify the bridge of hot plug detection.
	 *
	 * This callback is optional, it may be implemented by bridges that
	 * need to be notified of display connection or disconnection for
	 * internal reasons. One use case is to reset the internal state of CEC
	 * controllers for HDMI bridges.
	 */
	void (*hpd_notify)(struct drm_bridge *bridge,
			   enum drm_connector_status status);

	/**
	 * @hpd_enable:
	 *
	 * Enable hot plug detection. From now on the bridge shall call
	 * drm_bridge_hpd_notify() each time a change is detected in the output
	 * connection status, until hot plug detection gets disabled with
	 * @hpd_disable.
	 *
	 * This callback is optional and shall only be implemented by bridges
	 * that support hot-plug notification without polling. Bridges that
	 * implement it shall also implement the @hpd_disable callback and set
	 * the DRM_BRIDGE_OP_HPD flag in their &drm_bridge->ops.
	 */
	void (*hpd_enable)(struct drm_bridge *bridge);

	/**
	 * @hpd_disable:
	 *
	 * Disable hot plug detection. Once this function returns the bridge
	 * shall not call drm_bridge_hpd_notify() when a change in the output
	 * connection status occurs.
	 *
	 * This callback is optional and shall only be implemented by bridges
	 * that support hot-plug notification without polling. Bridges that
	 * implement it shall also implement the @hpd_enable callback and set
	 * the DRM_BRIDGE_OP_HPD flag in their &drm_bridge->ops.
	 */
	void (*hpd_disable)(struct drm_bridge *bridge);

	/**
	 * @hdmi_tmds_char_rate_valid:
	 *
	 * Check whether a particular TMDS character rate is supported by the
	 * driver.
	 *
	 * This callback is optional and should only be implemented by the
	 * bridges that take part in the HDMI connector implementation. Bridges
	 * that implement it shall set the DRM_BRIDGE_OP_HDMI flag in their
	 * &drm_bridge->ops.
	 *
	 * Returns:
	 *
	 * Either &drm_mode_status.MODE_OK or one of the failure reasons
	 * in &enum drm_mode_status.
	 */
	enum drm_mode_status
	(*hdmi_tmds_char_rate_valid)(const struct drm_bridge *bridge,
				     const struct drm_display_mode *mode,
				     unsigned long long tmds_rate);

	/**
	 * @hdmi_clear_infoframe:
	 *
	 * This callback clears the infoframes in the hardware during commit.
	 * It will be called multiple times, once for every disabled infoframe
	 * type.
	 *
	 * This callback is optional but it must be implemented by bridges that
	 * set the DRM_BRIDGE_OP_HDMI flag in their &drm_bridge->ops.
	 */
	int (*hdmi_clear_infoframe)(struct drm_bridge *bridge,
				    enum hdmi_infoframe_type type);
	/**
	 * @hdmi_write_infoframe:
	 *
	 * Program the infoframe into the hardware. It will be called multiple
	 * times, once for every updated infoframe type.
	 *
	 * This callback is optional but it must be implemented by bridges that
	 * set the DRM_BRIDGE_OP_HDMI flag in their &drm_bridge->ops.
	 */
	int (*hdmi_write_infoframe)(struct drm_bridge *bridge,
				    enum hdmi_infoframe_type type,
				    const u8 *buffer, size_t len);

	/**
	 * @debugfs_init:
	 *
	 * Allows bridges to create bridge-specific debugfs files.
	 */
	void (*debugfs_init)(struct drm_bridge *bridge, struct dentry *root);
};

/**
 * struct drm_bridge_timings - timing information for the bridge
 */
struct drm_bridge_timings {
	/**
	 * @input_bus_flags:
	 *
	 * Tells what additional settings for the pixel data on the bus
	 * this bridge requires (like pixel signal polarity). See also
	 * &drm_display_info->bus_flags.
	 */
	u32 input_bus_flags;
	/**
	 * @setup_time_ps:
	 *
	 * Defines the time in picoseconds the input data lines must be
	 * stable before the clock edge.
	 */
	u32 setup_time_ps;
	/**
	 * @hold_time_ps:
	 *
	 * Defines the time in picoseconds taken for the bridge to sample the
	 * input signal after the clock edge.
	 */
	u32 hold_time_ps;
	/**
	 * @dual_link:
	 *
	 * True if the bus operates in dual-link mode. The exact meaning is
	 * dependent on the bus type. For LVDS buses, this indicates that even-
	 * and odd-numbered pixels are received on separate links.
	 */
	bool dual_link;
};

/**
 * enum drm_bridge_ops - Bitmask of operations supported by the bridge
 */
enum drm_bridge_ops {
	/**
	 * @DRM_BRIDGE_OP_DETECT: The bridge can detect displays connected to
	 * its output. Bridges that set this flag shall implement the
	 * &drm_bridge_funcs->detect callback.
	 */
	DRM_BRIDGE_OP_DETECT = BIT(0),
	/**
	 * @DRM_BRIDGE_OP_EDID: The bridge can retrieve the EDID of the display
	 * connected to its output. Bridges that set this flag shall implement
	 * the &drm_bridge_funcs->edid_read callback.
	 */
	DRM_BRIDGE_OP_EDID = BIT(1),
	/**
	 * @DRM_BRIDGE_OP_HPD: The bridge can detect hot-plug and hot-unplug
	 * without requiring polling. Bridges that set this flag shall
	 * implement the &drm_bridge_funcs->hpd_enable and
	 * &drm_bridge_funcs->hpd_disable callbacks if they support enabling
	 * and disabling hot-plug detection dynamically.
	 */
	DRM_BRIDGE_OP_HPD = BIT(2),
	/**
	 * @DRM_BRIDGE_OP_MODES: The bridge can retrieve the modes supported
	 * by the display at its output. This does not include reading EDID
	 * which is separately covered by @DRM_BRIDGE_OP_EDID. Bridges that set
	 * this flag shall implement the &drm_bridge_funcs->get_modes callback.
	 */
	DRM_BRIDGE_OP_MODES = BIT(3),
	/**
	 * @DRM_BRIDGE_OP_HDMI: The bridge provides HDMI connector operations,
	 * including infoframes support. Bridges that set this flag must
	 * implement the &drm_bridge_funcs->write_infoframe callback.
	 *
	 * Note: currently there can be at most one bridge in a chain that sets
	 * this bit. This is to simplify corresponding glue code in connector
	 * drivers.
	 */
	DRM_BRIDGE_OP_HDMI = BIT(4),
};

/**
 * struct drm_bridge - central DRM bridge control structure
 */
struct drm_bridge {
	/** @base: inherit from &drm_private_object */
	struct drm_private_obj base;
	/** @dev: DRM device this bridge belongs to */
	struct drm_device *dev;
	/** @encoder: encoder to which this bridge is connected */
	struct drm_encoder *encoder;
	/** @chain_node: used to form a bridge chain */
	struct list_head chain_node;
	/** @of_node: device node pointer to the bridge */
	struct device_node *of_node;
	/** @list: to keep track of all added bridges */
	struct list_head list;
	/**
	 * @timings:
	 *
	 * the timing specification for the bridge, if any (may be NULL)
	 */
	const struct drm_bridge_timings *timings;
	/** @funcs: control functions */
	const struct drm_bridge_funcs *funcs;
	/** @driver_private: pointer to the bridge driver's internal context */
	void *driver_private;
	/** @ops: bitmask of operations supported by the bridge */
	enum drm_bridge_ops ops;
	/**
	 * @type: Type of the connection at the bridge output
	 * (DRM_MODE_CONNECTOR_*). For bridges at the end of this chain this
	 * identifies the type of connected display.
	 */
	int type;
	/**
	 * @interlace_allowed: Indicate that the bridge can handle interlaced
	 * modes.
	 */
	bool interlace_allowed;
	/**
	 * @ycbcr_420_allowed: Indicate that the bridge can handle YCbCr 420
	 * output.
	 */
	bool ycbcr_420_allowed;
	/**
	 * @pre_enable_prev_first: The bridge requires that the prev
	 * bridge @pre_enable function is called before its @pre_enable,
	 * and conversely for post_disable. This is most frequently a
	 * requirement for DSI devices which need the host to be initialised
	 * before the peripheral.
	 */
	bool pre_enable_prev_first;
	/**
	 * @ddc: Associated I2C adapter for DDC access, if any.
	 */
	struct i2c_adapter *ddc;
	/** private: */
	/**
	 * @hpd_mutex: Protects the @hpd_cb and @hpd_data fields.
	 */
	struct mutex hpd_mutex;
	/**
	 * @hpd_cb: Hot plug detection callback, registered with
	 * drm_bridge_hpd_enable().
	 */
	void (*hpd_cb)(void *data, enum drm_connector_status status);
	/**
	 * @hpd_data: Private data passed to the Hot plug detection callback
	 * @hpd_cb.
	 */
	void *hpd_data;

	/**
	 * @vendor: Vendor of the product to be used for the SPD InfoFrame
	 * generation. This is required if @DRM_BRIDGE_OP_HDMI is set.
	 */
	const char *vendor;

	/**
	 * @product: Name of the product to be used for the SPD InfoFrame
	 * generation. This is required if @DRM_BRIDGE_OP_HDMI is set.
	 */
	const char *product;

	/**
	 * @supported_formats: Bitmask of @hdmi_colorspace listing supported
	 * output formats. This is only relevant if @DRM_BRIDGE_OP_HDMI is set.
	 */
	unsigned int supported_formats;

	/**
	 * @max_bpc: Maximum bits per char the HDMI bridge supports. Allowed
	 * values are 8, 10 and 12. This is only relevant if
	 * @DRM_BRIDGE_OP_HDMI is set.
	 */
	unsigned int max_bpc;
};

static inline struct drm_bridge *
drm_priv_to_bridge(struct drm_private_obj *priv)
{
	return container_of(priv, struct drm_bridge, base);
}

void drm_bridge_add(struct drm_bridge *bridge);
int devm_drm_bridge_add(struct device *dev, struct drm_bridge *bridge);
void drm_bridge_remove(struct drm_bridge *bridge);
int drm_bridge_attach(struct drm_encoder *encoder, struct drm_bridge *bridge,
		      struct drm_bridge *previous,
		      enum drm_bridge_attach_flags flags);

#ifdef CONFIG_OF
struct drm_bridge *of_drm_find_bridge(struct device_node *np);
#else
static inline struct drm_bridge *of_drm_find_bridge(struct device_node *np)
{
	return NULL;
}
#endif

/**
 * drm_bridge_get_next_bridge() - Get the next bridge in the chain
 * @bridge: bridge object
 *
 * RETURNS:
 * the next bridge in the chain after @bridge, or NULL if @bridge is the last.
 */
static inline struct drm_bridge *
drm_bridge_get_next_bridge(struct drm_bridge *bridge)
{
	if (list_is_last(&bridge->chain_node, &bridge->encoder->bridge_chain))
		return NULL;

	return list_next_entry(bridge, chain_node);
}

/**
 * drm_bridge_get_prev_bridge() - Get the previous bridge in the chain
 * @bridge: bridge object
 *
 * RETURNS:
 * the previous bridge in the chain, or NULL if @bridge is the first.
 */
static inline struct drm_bridge *
drm_bridge_get_prev_bridge(struct drm_bridge *bridge)
{
	if (list_is_first(&bridge->chain_node, &bridge->encoder->bridge_chain))
		return NULL;

	return list_prev_entry(bridge, chain_node);
}

/**
 * drm_bridge_chain_get_first_bridge() - Get the first bridge in the chain
 * @encoder: encoder object
 *
 * RETURNS:
 * the first bridge in the chain, or NULL if @encoder has no bridge attached
 * to it.
 */
static inline struct drm_bridge *
drm_bridge_chain_get_first_bridge(struct drm_encoder *encoder)
{
	return list_first_entry_or_null(&encoder->bridge_chain,
					struct drm_bridge, chain_node);
}

/**
 * drm_for_each_bridge_in_chain() - Iterate over all bridges present in a chain
 * @encoder: the encoder to iterate bridges on
 * @bridge: a bridge pointer updated to point to the current bridge at each
 *	    iteration
 *
 * Iterate over all bridges present in the bridge chain attached to @encoder.
 */
#define drm_for_each_bridge_in_chain(encoder, bridge)			\
	list_for_each_entry(bridge, &(encoder)->bridge_chain, chain_node)

enum drm_mode_status
drm_bridge_chain_mode_valid(struct drm_bridge *bridge,
			    const struct drm_display_info *info,
			    const struct drm_display_mode *mode);
void drm_bridge_chain_mode_set(struct drm_bridge *bridge,
			       const struct drm_display_mode *mode,
			       const struct drm_display_mode *adjusted_mode);

int drm_atomic_bridge_chain_check(struct drm_bridge *bridge,
				  struct drm_crtc_state *crtc_state,
				  struct drm_connector_state *conn_state);
void drm_atomic_bridge_chain_disable(struct drm_bridge *bridge,
				     struct drm_atomic_state *state);
void drm_atomic_bridge_chain_post_disable(struct drm_bridge *bridge,
					  struct drm_atomic_state *state);
void drm_atomic_bridge_chain_pre_enable(struct drm_bridge *bridge,
					struct drm_atomic_state *state);
void drm_atomic_bridge_chain_enable(struct drm_bridge *bridge,
				    struct drm_atomic_state *state);

u32 *
drm_atomic_helper_bridge_propagate_bus_fmt(struct drm_bridge *bridge,
					struct drm_bridge_state *bridge_state,
					struct drm_crtc_state *crtc_state,
					struct drm_connector_state *conn_state,
					u32 output_fmt,
					unsigned int *num_input_fmts);

enum drm_connector_status drm_bridge_detect(struct drm_bridge *bridge);
int drm_bridge_get_modes(struct drm_bridge *bridge,
			 struct drm_connector *connector);
const struct drm_edid *drm_bridge_edid_read(struct drm_bridge *bridge,
					    struct drm_connector *connector);
void drm_bridge_hpd_enable(struct drm_bridge *bridge,
			   void (*cb)(void *data,
				      enum drm_connector_status status),
			   void *data);
void drm_bridge_hpd_disable(struct drm_bridge *bridge);
void drm_bridge_hpd_notify(struct drm_bridge *bridge,
			   enum drm_connector_status status);

#ifdef CONFIG_DRM_PANEL_BRIDGE
bool drm_bridge_is_panel(const struct drm_bridge *bridge);
struct drm_bridge *drm_panel_bridge_add(struct drm_panel *panel);
struct drm_bridge *drm_panel_bridge_add_typed(struct drm_panel *panel,
					      u32 connector_type);
void drm_panel_bridge_remove(struct drm_bridge *bridge);
int drm_panel_bridge_set_orientation(struct drm_connector *connector,
				     struct drm_bridge *bridge);
struct drm_bridge *devm_drm_panel_bridge_add(struct device *dev,
					     struct drm_panel *panel);
struct drm_bridge *devm_drm_panel_bridge_add_typed(struct device *dev,
						   struct drm_panel *panel,
						   u32 connector_type);
struct drm_bridge *drmm_panel_bridge_add(struct drm_device *drm,
					     struct drm_panel *panel);
struct drm_connector *drm_panel_bridge_connector(struct drm_bridge *bridge);
#else
static inline bool drm_bridge_is_panel(const struct drm_bridge *bridge)
{
	return false;
}

static inline int drm_panel_bridge_set_orientation(struct drm_connector *connector,
						   struct drm_bridge *bridge)
{
	return -EINVAL;
}
#endif

#if defined(CONFIG_OF) && defined(CONFIG_DRM_PANEL_BRIDGE)
struct drm_bridge *devm_drm_of_get_bridge(struct device *dev, struct device_node *node,
					  u32 port, u32 endpoint);
struct drm_bridge *drmm_of_get_bridge(struct drm_device *drm, struct device_node *node,
					  u32 port, u32 endpoint);
#else
static inline struct drm_bridge *devm_drm_of_get_bridge(struct device *dev,
							struct device_node *node,
							u32 port,
							u32 endpoint)
{
	return ERR_PTR(-ENODEV);
}

static inline struct drm_bridge *drmm_of_get_bridge(struct drm_device *drm,
						     struct device_node *node,
						     u32 port,
						     u32 endpoint)
{
	return ERR_PTR(-ENODEV);
}
#endif

#endif
