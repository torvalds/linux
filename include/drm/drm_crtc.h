/*
 * Copyright © 2006 Keith Packard
 * Copyright © 2007-2008 Dave Airlie
 * Copyright © 2007-2008 Intel Corporation
 *   Jesse Barnes <jesse.barnes@intel.com>
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
#ifndef __DRM_CRTC_H__
#define __DRM_CRTC_H__

#include <linux/i2c.h>
#include <linux/spinlock.h>
#include <linux/types.h>
#include <linux/fb.h>
#include <linux/hdmi.h>
#include <linux/media-bus-format.h>
#include <uapi/drm/drm_mode.h>
#include <uapi/drm/drm_fourcc.h>
#include <drm/drm_modeset_lock.h>
#include <drm/drm_rect.h>
#include <drm/drm_mode_object.h>
#include <drm/drm_framebuffer.h>
#include <drm/drm_modes.h>
#include <drm/drm_connector.h>
#include <drm/drm_property.h>
#include <drm/drm_bridge.h>
#include <drm/drm_edid.h>
#include <drm/drm_plane.h>
#include <drm/drm_blend.h>
#include <drm/drm_color_mgmt.h>
#include <drm/drm_debugfs_crc.h>
#include <drm/drm_mode_config.h>

struct drm_device;
struct drm_mode_set;
struct drm_file;
struct drm_clip_rect;
struct drm_printer;
struct device_node;
struct dma_fence;
struct edid;

static inline int64_t U642I64(uint64_t val)
{
	return (int64_t)*((int64_t *)&val);
}
static inline uint64_t I642U64(int64_t val)
{
	return (uint64_t)*((uint64_t *)&val);
}

struct drm_crtc;
struct drm_pending_vblank_event;
struct drm_plane;
struct drm_bridge;
struct drm_atomic_state;

struct drm_crtc_helper_funcs;
struct drm_plane_helper_funcs;

/**
 * struct drm_crtc_state - mutable CRTC state
 * @crtc: backpointer to the CRTC
 * @enable: whether the CRTC should be enabled, gates all other state
 * @active: whether the CRTC is actively displaying (used for DPMS)
 * @planes_changed: planes on this crtc are updated
 * @mode_changed: @mode or @enable has been changed
 * @active_changed: @active has been toggled.
 * @connectors_changed: connectors to this crtc have been updated
 * @zpos_changed: zpos values of planes on this crtc have been updated
 * @color_mgmt_changed: color management properties have changed (degamma or
 *	gamma LUT or CSC matrix)
 * @plane_mask: bitmask of (1 << drm_plane_index(plane)) of attached planes
 * @connector_mask: bitmask of (1 << drm_connector_index(connector)) of attached connectors
 * @encoder_mask: bitmask of (1 << drm_encoder_index(encoder)) of attached encoders
 * @adjusted_mode: for use by helpers and drivers to compute adjusted mode timings
 * @mode: current mode timings
 * @mode_blob: &drm_property_blob for @mode
 * @degamma_lut: Lookup table for converting framebuffer pixel data
 *	before apply the conversion matrix
 * @ctm: Transformation matrix
 * @gamma_lut: Lookup table for converting pixel data after the
 *	conversion matrix
 * @state: backpointer to global drm_atomic_state
 *
 * Note that the distinction between @enable and @active is rather subtile:
 * Flipping @active while @enable is set without changing anything else may
 * never return in a failure from the &drm_mode_config_funcs.atomic_check
 * callback. Userspace assumes that a DPMS On will always succeed. In other
 * words: @enable controls resource assignment, @active controls the actual
 * hardware state.
 *
 * The three booleans active_changed, connectors_changed and mode_changed are
 * intended to indicate whether a full modeset is needed, rather than strictly
 * describing what has changed in a commit.
 * See also: drm_atomic_crtc_needs_modeset()
 */
struct drm_crtc_state {
	struct drm_crtc *crtc;

	bool enable;
	bool active;

	/* computed state bits used by helpers and drivers */
	bool planes_changed : 1;
	bool mode_changed : 1;
	bool active_changed : 1;
	bool connectors_changed : 1;
	bool zpos_changed : 1;
	bool color_mgmt_changed : 1;

	/* attached planes bitmask:
	 * WARNING: transitional helpers do not maintain plane_mask so
	 * drivers not converted over to atomic helpers should not rely
	 * on plane_mask being accurate!
	 */
	u32 plane_mask;

	u32 connector_mask;
	u32 encoder_mask;

	/* adjusted_mode: for use by helpers and drivers */
	struct drm_display_mode adjusted_mode;

	struct drm_display_mode mode;

	/* blob property to expose current mode to atomic userspace */
	struct drm_property_blob *mode_blob;

	/* blob property to expose color management to userspace */
	struct drm_property_blob *degamma_lut;
	struct drm_property_blob *ctm;
	struct drm_property_blob *gamma_lut;

	/**
	 * @target_vblank:
	 *
	 * Target vertical blank period when a page flip
	 * should take effect.
	 */
	u32 target_vblank;

	/**
	 * @pageflip_flags:
	 *
	 * DRM_MODE_PAGE_FLIP_* flags, as passed to the page flip ioctl.
	 * Zero in any other case.
	 */
	u32 pageflip_flags;

	/**
	 * @event:
	 *
	 * Optional pointer to a DRM event to signal upon completion of the
	 * state update. The driver must send out the event when the atomic
	 * commit operation completes. There are two cases:
	 *
	 *  - The event is for a CRTC which is being disabled through this
	 *    atomic commit. In that case the event can be send out any time
	 *    after the hardware has stopped scanning out the current
	 *    framebuffers. It should contain the timestamp and counter for the
	 *    last vblank before the display pipeline was shut off.
	 *
	 *  - For a CRTC which is enabled at the end of the commit (even when it
	 *    undergoes an full modeset) the vblank timestamp and counter must
	 *    be for the vblank right before the first frame that scans out the
	 *    new set of buffers. Again the event can only be sent out after the
	 *    hardware has stopped scanning out the old buffers.
	 *
	 *  - Events for disabled CRTCs are not allowed, and drivers can ignore
	 *    that case.
	 *
	 * This can be handled by the drm_crtc_send_vblank_event() function,
	 * which the driver should call on the provided event upon completion of
	 * the atomic commit. Note that if the driver supports vblank signalling
	 * and timestamping the vblank counters and timestamps must agree with
	 * the ones returned from page flip events. With the current vblank
	 * helper infrastructure this can be achieved by holding a vblank
	 * reference while the page flip is pending, acquired through
	 * drm_crtc_vblank_get() and released with drm_crtc_vblank_put().
	 * Drivers are free to implement their own vblank counter and timestamp
	 * tracking though, e.g. if they have accurate timestamp registers in
	 * hardware.
	 *
	 * For hardware which supports some means to synchronize vblank
	 * interrupt delivery with committing display state there's also
	 * drm_crtc_arm_vblank_event(). See the documentation of that function
	 * for a detailed discussion of the constraints it needs to be used
	 * safely.
	 */
	struct drm_pending_vblank_event *event;

	struct drm_atomic_state *state;
};

/**
 * struct drm_crtc_funcs - control CRTCs for a given device
 *
 * The drm_crtc_funcs structure is the central CRTC management structure
 * in the DRM.  Each CRTC controls one or more connectors (note that the name
 * CRTC is simply historical, a CRTC may control LVDS, VGA, DVI, TV out, etc.
 * connectors, not just CRTs).
 *
 * Each driver is responsible for filling out this structure at startup time,
 * in addition to providing other modesetting features, like i2c and DDC
 * bus accessors.
 */
struct drm_crtc_funcs {
	/**
	 * @reset:
	 *
	 * Reset CRTC hardware and software state to off. This function isn't
	 * called by the core directly, only through drm_mode_config_reset().
	 * It's not a helper hook only for historical reasons.
	 *
	 * Atomic drivers can use drm_atomic_helper_crtc_reset() to reset
	 * atomic state using this hook.
	 */
	void (*reset)(struct drm_crtc *crtc);

	/**
	 * @cursor_set:
	 *
	 * Update the cursor image. The cursor position is relative to the CRTC
	 * and can be partially or fully outside of the visible area.
	 *
	 * Note that contrary to all other KMS functions the legacy cursor entry
	 * points don't take a framebuffer object, but instead take directly a
	 * raw buffer object id from the driver's buffer manager (which is
	 * either GEM or TTM for current drivers).
	 *
	 * This entry point is deprecated, drivers should instead implement
	 * universal plane support and register a proper cursor plane using
	 * drm_crtc_init_with_planes().
	 *
	 * This callback is optional
	 *
	 * RETURNS:
	 *
	 * 0 on success or a negative error code on failure.
	 */
	int (*cursor_set)(struct drm_crtc *crtc, struct drm_file *file_priv,
			  uint32_t handle, uint32_t width, uint32_t height);

	/**
	 * @cursor_set2:
	 *
	 * Update the cursor image, including hotspot information. The hotspot
	 * must not affect the cursor position in CRTC coordinates, but is only
	 * meant as a hint for virtualized display hardware to coordinate the
	 * guests and hosts cursor position. The cursor hotspot is relative to
	 * the cursor image. Otherwise this works exactly like @cursor_set.
	 *
	 * This entry point is deprecated, drivers should instead implement
	 * universal plane support and register a proper cursor plane using
	 * drm_crtc_init_with_planes().
	 *
	 * This callback is optional.
	 *
	 * RETURNS:
	 *
	 * 0 on success or a negative error code on failure.
	 */
	int (*cursor_set2)(struct drm_crtc *crtc, struct drm_file *file_priv,
			   uint32_t handle, uint32_t width, uint32_t height,
			   int32_t hot_x, int32_t hot_y);

	/**
	 * @cursor_move:
	 *
	 * Update the cursor position. The cursor does not need to be visible
	 * when this hook is called.
	 *
	 * This entry point is deprecated, drivers should instead implement
	 * universal plane support and register a proper cursor plane using
	 * drm_crtc_init_with_planes().
	 *
	 * This callback is optional.
	 *
	 * RETURNS:
	 *
	 * 0 on success or a negative error code on failure.
	 */
	int (*cursor_move)(struct drm_crtc *crtc, int x, int y);

	/**
	 * @gamma_set:
	 *
	 * Set gamma on the CRTC.
	 *
	 * This callback is optional.
	 *
	 * NOTE:
	 *
	 * Drivers that support gamma tables and also fbdev emulation through
	 * the provided helper library need to take care to fill out the gamma
	 * hooks for both. Currently there's a bit an unfortunate duplication
	 * going on, which should eventually be unified to just one set of
	 * hooks.
	 */
	int (*gamma_set)(struct drm_crtc *crtc, u16 *r, u16 *g, u16 *b,
			 uint32_t size);

	/**
	 * @destroy:
	 *
	 * Clean up plane resources. This is only called at driver unload time
	 * through drm_mode_config_cleanup() since a CRTC cannot be hotplugged
	 * in DRM.
	 */
	void (*destroy)(struct drm_crtc *crtc);

	/**
	 * @set_config:
	 *
	 * This is the main legacy entry point to change the modeset state on a
	 * CRTC. All the details of the desired configuration are passed in a
	 * &struct drm_mode_set - see there for details.
	 *
	 * Drivers implementing atomic modeset should use
	 * drm_atomic_helper_set_config() to implement this hook.
	 *
	 * RETURNS:
	 *
	 * 0 on success or a negative error code on failure.
	 */
	int (*set_config)(struct drm_mode_set *set);

	/**
	 * @page_flip:
	 *
	 * Legacy entry point to schedule a flip to the given framebuffer.
	 *
	 * Page flipping is a synchronization mechanism that replaces the frame
	 * buffer being scanned out by the CRTC with a new frame buffer during
	 * vertical blanking, avoiding tearing (except when requested otherwise
	 * through the DRM_MODE_PAGE_FLIP_ASYNC flag). When an application
	 * requests a page flip the DRM core verifies that the new frame buffer
	 * is large enough to be scanned out by the CRTC in the currently
	 * configured mode and then calls this hook with a pointer to the new
	 * frame buffer.
	 *
	 * The driver must wait for any pending rendering to the new framebuffer
	 * to complete before executing the flip. It should also wait for any
	 * pending rendering from other drivers if the underlying buffer is a
	 * shared dma-buf.
	 *
	 * An application can request to be notified when the page flip has
	 * completed. The drm core will supply a &struct drm_event in the event
	 * parameter in this case. This can be handled by the
	 * drm_crtc_send_vblank_event() function, which the driver should call on
	 * the provided event upon completion of the flip. Note that if
	 * the driver supports vblank signalling and timestamping the vblank
	 * counters and timestamps must agree with the ones returned from page
	 * flip events. With the current vblank helper infrastructure this can
	 * be achieved by holding a vblank reference while the page flip is
	 * pending, acquired through drm_crtc_vblank_get() and released with
	 * drm_crtc_vblank_put(). Drivers are free to implement their own vblank
	 * counter and timestamp tracking though, e.g. if they have accurate
	 * timestamp registers in hardware.
	 *
	 * This callback is optional.
	 *
	 * NOTE:
	 *
	 * Very early versions of the KMS ABI mandated that the driver must
	 * block (but not reject) any rendering to the old framebuffer until the
	 * flip operation has completed and the old framebuffer is no longer
	 * visible. This requirement has been lifted, and userspace is instead
	 * expected to request delivery of an event and wait with recycling old
	 * buffers until such has been received.
	 *
	 * RETURNS:
	 *
	 * 0 on success or a negative error code on failure. Note that if a
	 * page flip operation is already pending the callback should return
	 * -EBUSY. Pageflips on a disabled CRTC (either by setting a NULL mode
	 * or just runtime disabled through DPMS respectively the new atomic
	 * "ACTIVE" state) should result in an -EINVAL error code. Note that
	 * drm_atomic_helper_page_flip() checks this already for atomic drivers.
	 */
	int (*page_flip)(struct drm_crtc *crtc,
			 struct drm_framebuffer *fb,
			 struct drm_pending_vblank_event *event,
			 uint32_t flags);

	/**
	 * @page_flip_target:
	 *
	 * Same as @page_flip but with an additional parameter specifying the
	 * absolute target vertical blank period (as reported by
	 * drm_crtc_vblank_count()) when the flip should take effect.
	 *
	 * Note that the core code calls drm_crtc_vblank_get before this entry
	 * point, and will call drm_crtc_vblank_put if this entry point returns
	 * any non-0 error code. It's the driver's responsibility to call
	 * drm_crtc_vblank_put after this entry point returns 0, typically when
	 * the flip completes.
	 */
	int (*page_flip_target)(struct drm_crtc *crtc,
				struct drm_framebuffer *fb,
				struct drm_pending_vblank_event *event,
				uint32_t flags, uint32_t target);

	/**
	 * @set_property:
	 *
	 * This is the legacy entry point to update a property attached to the
	 * CRTC.
	 *
	 * Drivers implementing atomic modeset should use
	 * drm_atomic_helper_crtc_set_property() to implement this hook.
	 *
	 * This callback is optional if the driver does not support any legacy
	 * driver-private properties.
	 *
	 * RETURNS:
	 *
	 * 0 on success or a negative error code on failure.
	 */
	int (*set_property)(struct drm_crtc *crtc,
			    struct drm_property *property, uint64_t val);

	/**
	 * @atomic_duplicate_state:
	 *
	 * Duplicate the current atomic state for this CRTC and return it.
	 * The core and helpers guarantee that any atomic state duplicated with
	 * this hook and still owned by the caller (i.e. not transferred to the
	 * driver by calling &drm_mode_config_funcs.atomic_commit) will be
	 * cleaned up by calling the @atomic_destroy_state hook in this
	 * structure.
	 *
	 * Atomic drivers which don't subclass &struct drm_crtc_state should use
	 * drm_atomic_helper_crtc_duplicate_state(). Drivers that subclass the
	 * state structure to extend it with driver-private state should use
	 * __drm_atomic_helper_crtc_duplicate_state() to make sure shared state is
	 * duplicated in a consistent fashion across drivers.
	 *
	 * It is an error to call this hook before &drm_crtc.state has been
	 * initialized correctly.
	 *
	 * NOTE:
	 *
	 * If the duplicate state references refcounted resources this hook must
	 * acquire a reference for each of them. The driver must release these
	 * references again in @atomic_destroy_state.
	 *
	 * RETURNS:
	 *
	 * Duplicated atomic state or NULL when the allocation failed.
	 */
	struct drm_crtc_state *(*atomic_duplicate_state)(struct drm_crtc *crtc);

	/**
	 * @atomic_destroy_state:
	 *
	 * Destroy a state duplicated with @atomic_duplicate_state and release
	 * or unreference all resources it references
	 */
	void (*atomic_destroy_state)(struct drm_crtc *crtc,
				     struct drm_crtc_state *state);

	/**
	 * @atomic_set_property:
	 *
	 * Decode a driver-private property value and store the decoded value
	 * into the passed-in state structure. Since the atomic core decodes all
	 * standardized properties (even for extensions beyond the core set of
	 * properties which might not be implemented by all drivers) this
	 * requires drivers to subclass the state structure.
	 *
	 * Such driver-private properties should really only be implemented for
	 * truly hardware/vendor specific state. Instead it is preferred to
	 * standardize atomic extension and decode the properties used to expose
	 * such an extension in the core.
	 *
	 * Do not call this function directly, use
	 * drm_atomic_crtc_set_property() instead.
	 *
	 * This callback is optional if the driver does not support any
	 * driver-private atomic properties.
	 *
	 * NOTE:
	 *
	 * This function is called in the state assembly phase of atomic
	 * modesets, which can be aborted for any reason (including on
	 * userspace's request to just check whether a configuration would be
	 * possible). Drivers MUST NOT touch any persistent state (hardware or
	 * software) or data structures except the passed in @state parameter.
	 *
	 * Also since userspace controls in which order properties are set this
	 * function must not do any input validation (since the state update is
	 * incomplete and hence likely inconsistent). Instead any such input
	 * validation must be done in the various atomic_check callbacks.
	 *
	 * RETURNS:
	 *
	 * 0 if the property has been found, -EINVAL if the property isn't
	 * implemented by the driver (which should never happen, the core only
	 * asks for properties attached to this CRTC). No other validation is
	 * allowed by the driver. The core already checks that the property
	 * value is within the range (integer, valid enum value, ...) the driver
	 * set when registering the property.
	 */
	int (*atomic_set_property)(struct drm_crtc *crtc,
				   struct drm_crtc_state *state,
				   struct drm_property *property,
				   uint64_t val);
	/**
	 * @atomic_get_property:
	 *
	 * Reads out the decoded driver-private property. This is used to
	 * implement the GETCRTC IOCTL.
	 *
	 * Do not call this function directly, use
	 * drm_atomic_crtc_get_property() instead.
	 *
	 * This callback is optional if the driver does not support any
	 * driver-private atomic properties.
	 *
	 * RETURNS:
	 *
	 * 0 on success, -EINVAL if the property isn't implemented by the
	 * driver (which should never happen, the core only asks for
	 * properties attached to this CRTC).
	 */
	int (*atomic_get_property)(struct drm_crtc *crtc,
				   const struct drm_crtc_state *state,
				   struct drm_property *property,
				   uint64_t *val);

	/**
	 * @late_register:
	 *
	 * This optional hook can be used to register additional userspace
	 * interfaces attached to the crtc like debugfs interfaces.
	 * It is called late in the driver load sequence from drm_dev_register().
	 * Everything added from this callback should be unregistered in
	 * the early_unregister callback.
	 *
	 * Returns:
	 *
	 * 0 on success, or a negative error code on failure.
	 */
	int (*late_register)(struct drm_crtc *crtc);

	/**
	 * @early_unregister:
	 *
	 * This optional hook should be used to unregister the additional
	 * userspace interfaces attached to the crtc from
	 * @late_register. It is called from drm_dev_unregister(),
	 * early in the driver unload sequence to disable userspace access
	 * before data structures are torndown.
	 */
	void (*early_unregister)(struct drm_crtc *crtc);

	/**
	 * @set_crc_source:
	 *
	 * Changes the source of CRC checksums of frames at the request of
	 * userspace, typically for testing purposes. The sources available are
	 * specific of each driver and a %NULL value indicates that CRC
	 * generation is to be switched off.
	 *
	 * When CRC generation is enabled, the driver should call
	 * drm_crtc_add_crc_entry() at each frame, providing any information
	 * that characterizes the frame contents in the crcN arguments, as
	 * provided from the configured source. Drivers must accept a "auto"
	 * source name that will select a default source for this CRTC.
	 *
	 * This callback is optional if the driver does not support any CRC
	 * generation functionality.
	 *
	 * RETURNS:
	 *
	 * 0 on success or a negative error code on failure.
	 */
	int (*set_crc_source)(struct drm_crtc *crtc, const char *source,
			      size_t *values_cnt);

	/**
	 * @atomic_print_state:
	 *
	 * If driver subclasses &struct drm_crtc_state, it should implement
	 * this optional hook for printing additional driver specific state.
	 *
	 * Do not call this directly, use drm_atomic_crtc_print_state()
	 * instead.
	 */
	void (*atomic_print_state)(struct drm_printer *p,
				   const struct drm_crtc_state *state);

	/**
	 * @get_vblank_counter:
	 *
	 * Driver callback for fetching a raw hardware vblank counter for the
	 * CRTC. It's meant to be used by new drivers as the replacement of
	 * &drm_driver.get_vblank_counter hook.
	 *
	 * This callback is optional. If a device doesn't have a hardware
	 * counter, the driver can simply leave the hook as NULL. The DRM core
	 * will account for missed vblank events while interrupts where disabled
	 * based on system timestamps.
	 *
	 * Wraparound handling and loss of events due to modesetting is dealt
	 * with in the DRM core code, as long as drivers call
	 * drm_crtc_vblank_off() and drm_crtc_vblank_on() when disabling or
	 * enabling a CRTC.
	 *
	 * Returns:
	 *
	 * Raw vblank counter value.
	 */
	u32 (*get_vblank_counter)(struct drm_crtc *crtc);

	/**
	 * @enable_vblank:
	 *
	 * Enable vblank interrupts for the CRTC. It's meant to be used by
	 * new drivers as the replacement of &drm_driver.enable_vblank hook.
	 *
	 * Returns:
	 *
	 * Zero on success, appropriate errno if the vblank interrupt cannot
	 * be enabled.
	 */
	int (*enable_vblank)(struct drm_crtc *crtc);

	/**
	 * @disable_vblank:
	 *
	 * Disable vblank interrupts for the CRTC. It's meant to be used by
	 * new drivers as the replacement of &drm_driver.disable_vblank hook.
	 */
	void (*disable_vblank)(struct drm_crtc *crtc);
};

/**
 * struct drm_crtc - central CRTC control structure
 * @dev: parent DRM device
 * @port: OF node used by drm_of_find_possible_crtcs()
 * @head: list management
 * @name: human readable name, can be overwritten by the driver
 * @mutex: per-CRTC locking
 * @base: base KMS object for ID tracking etc.
 * @primary: primary plane for this CRTC
 * @cursor: cursor plane for this CRTC
 * @cursor_x: current x position of the cursor, used for universal cursor planes
 * @cursor_y: current y position of the cursor, used for universal cursor planes
 * @enabled: is this CRTC enabled?
 * @mode: current mode timings
 * @hwmode: mode timings as programmed to hw regs
 * @x: x position on screen
 * @y: y position on screen
 * @funcs: CRTC control functions
 * @gamma_size: size of gamma ramp
 * @gamma_store: gamma ramp values
 * @helper_private: mid-layer private data
 * @properties: property tracking for this CRTC
 *
 * Each CRTC may have one or more connectors associated with it.  This structure
 * allows the CRTC to be controlled.
 */
struct drm_crtc {
	struct drm_device *dev;
	struct device_node *port;
	struct list_head head;

	char *name;

	/**
	 * @mutex:
	 *
	 * This provides a read lock for the overall crtc state (mode, dpms
	 * state, ...) and a write lock for everything which can be update
	 * without a full modeset (fb, cursor data, crtc properties ...). A full
	 * modeset also need to grab &drm_mode_config.connection_mutex.
	 */
	struct drm_modeset_lock mutex;

	struct drm_mode_object base;

	/* primary and cursor planes for CRTC */
	struct drm_plane *primary;
	struct drm_plane *cursor;

	/**
	 * @index: Position inside the mode_config.list, can be used as an array
	 * index. It is invariant over the lifetime of the CRTC.
	 */
	unsigned index;

	/* position of cursor plane on crtc */
	int cursor_x;
	int cursor_y;

	bool enabled;

	/* Requested mode from modesetting. */
	struct drm_display_mode mode;

	/* Programmed mode in hw, after adjustments for encoders,
	 * crtc, panel scaling etc. Needed for timestamping etc.
	 */
	struct drm_display_mode hwmode;

	int x, y;
	const struct drm_crtc_funcs *funcs;

	/* Legacy FB CRTC gamma size for reporting to userspace */
	uint32_t gamma_size;
	uint16_t *gamma_store;

	/* if you are using the helper */
	const struct drm_crtc_helper_funcs *helper_private;

	struct drm_object_properties properties;

	/**
	 * @state:
	 *
	 * Current atomic state for this CRTC.
	 */
	struct drm_crtc_state *state;

	/**
	 * @commit_list:
	 *
	 * List of &drm_crtc_commit structures tracking pending commits.
	 * Protected by @commit_lock. This list doesn't hold its own full
	 * reference, but burrows it from the ongoing commit. Commit entries
	 * must be removed from this list once the commit is fully completed,
	 * but before it's correspoding &drm_atomic_state gets destroyed.
	 */
	struct list_head commit_list;

	/**
	 * @commit_lock:
	 *
	 * Spinlock to protect @commit_list.
	 */
	spinlock_t commit_lock;

	/**
	 * @acquire_ctx:
	 *
	 * Per-CRTC implicit acquire context used by atomic drivers for legacy
	 * IOCTLs, so that atomic drivers can get at the locking acquire
	 * context.
	 */
	struct drm_modeset_acquire_ctx *acquire_ctx;

#ifdef CONFIG_DEBUG_FS
	/**
	 * @debugfs_entry:
	 *
	 * Debugfs directory for this CRTC.
	 */
	struct dentry *debugfs_entry;

	/**
	 * @crc:
	 *
	 * Configuration settings of CRC capture.
	 */
	struct drm_crtc_crc crc;
#endif

	/**
	 * @fence_context:
	 *
	 * timeline context used for fence operations.
	 */
	unsigned int fence_context;

	/**
	 * @fence_lock:
	 *
	 * spinlock to protect the fences in the fence_context.
	 */

	spinlock_t fence_lock;
	/**
	 * @fence_seqno:
	 *
	 * Seqno variable used as monotonic counter for the fences
	 * created on the CRTC's timeline.
	 */
	unsigned long fence_seqno;

	/**
	 * @timeline_name:
	 *
	 * The name of the CRTC's fence timeline.
	 */
	char timeline_name[32];
};

/**
 * struct drm_mode_set - new values for a CRTC config change
 * @fb: framebuffer to use for new config
 * @crtc: CRTC whose configuration we're about to change
 * @mode: mode timings to use
 * @x: position of this CRTC relative to @fb
 * @y: position of this CRTC relative to @fb
 * @connectors: array of connectors to drive with this CRTC if possible
 * @num_connectors: size of @connectors array
 *
 * This represents a modeset configuration for the legacy SETCRTC ioctl and is
 * also used internally. Atomic drivers instead use &drm_atomic_state.
 */
struct drm_mode_set {
	struct drm_framebuffer *fb;
	struct drm_crtc *crtc;
	struct drm_display_mode *mode;

	uint32_t x;
	uint32_t y;

	struct drm_connector **connectors;
	size_t num_connectors;
};

#define obj_to_crtc(x) container_of(x, struct drm_crtc, base)

__printf(6, 7)
int drm_crtc_init_with_planes(struct drm_device *dev,
			      struct drm_crtc *crtc,
			      struct drm_plane *primary,
			      struct drm_plane *cursor,
			      const struct drm_crtc_funcs *funcs,
			      const char *name, ...);
void drm_crtc_cleanup(struct drm_crtc *crtc);

/**
 * drm_crtc_index - find the index of a registered CRTC
 * @crtc: CRTC to find index for
 *
 * Given a registered CRTC, return the index of that CRTC within a DRM
 * device's list of CRTCs.
 */
static inline unsigned int drm_crtc_index(const struct drm_crtc *crtc)
{
	return crtc->index;
}

/**
 * drm_crtc_mask - find the mask of a registered CRTC
 * @crtc: CRTC to find mask for
 *
 * Given a registered CRTC, return the mask bit of that CRTC for an
 * encoder's possible_crtcs field.
 */
static inline uint32_t drm_crtc_mask(const struct drm_crtc *crtc)
{
	return 1 << drm_crtc_index(crtc);
}

int drm_crtc_force_disable(struct drm_crtc *crtc);
int drm_crtc_force_disable_all(struct drm_device *dev);

int drm_mode_set_config_internal(struct drm_mode_set *set);
struct drm_crtc *drm_crtc_from_index(struct drm_device *dev, int idx);

/**
 * drm_crtc_find - look up a CRTC object from its ID
 * @dev: DRM device
 * @id: &drm_mode_object ID
 *
 * This can be used to look up a CRTC from its userspace ID. Only used by
 * drivers for legacy IOCTLs and interface, nowadays extensions to the KMS
 * userspace interface should be done using &drm_property.
 */
static inline struct drm_crtc *drm_crtc_find(struct drm_device *dev,
	uint32_t id)
{
	struct drm_mode_object *mo;
	mo = drm_mode_object_find(dev, id, DRM_MODE_OBJECT_CRTC);
	return mo ? obj_to_crtc(mo) : NULL;
}

/**
 * drm_for_each_crtc - iterate over all CRTCs
 * @crtc: a &struct drm_crtc as the loop cursor
 * @dev: the &struct drm_device
 *
 * Iterate over all CRTCs of @dev.
 */
#define drm_for_each_crtc(crtc, dev) \
	list_for_each_entry(crtc, &(dev)->mode_config.crtc_list, head)

#endif /* __DRM_CRTC_H__ */
