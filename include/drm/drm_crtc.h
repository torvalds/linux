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
#include <linux/idr.h>
#include <linux/fb.h>
#include <linux/hdmi.h>
#include <linux/media-bus-format.h>
#include <uapi/drm/drm_mode.h>
#include <uapi/drm/drm_fourcc.h>
#include <drm/drm_modeset_lock.h>

struct drm_device;
struct drm_mode_set;
struct drm_framebuffer;
struct drm_object_properties;
struct drm_file;
struct drm_clip_rect;
struct device_node;
struct fence;

#define DRM_MODE_OBJECT_CRTC 0xcccccccc
#define DRM_MODE_OBJECT_CONNECTOR 0xc0c0c0c0
#define DRM_MODE_OBJECT_ENCODER 0xe0e0e0e0
#define DRM_MODE_OBJECT_MODE 0xdededede
#define DRM_MODE_OBJECT_PROPERTY 0xb0b0b0b0
#define DRM_MODE_OBJECT_FB 0xfbfbfbfb
#define DRM_MODE_OBJECT_BLOB 0xbbbbbbbb
#define DRM_MODE_OBJECT_PLANE 0xeeeeeeee
#define DRM_MODE_OBJECT_ANY 0

struct drm_mode_object {
	uint32_t id;
	uint32_t type;
	struct drm_object_properties *properties;
};

#define DRM_OBJECT_MAX_PROPERTY 24
struct drm_object_properties {
	int count, atomic_count;
	/* NOTE: if we ever start dynamically destroying properties (ie.
	 * not at drm_mode_config_cleanup() time), then we'd have to do
	 * a better job of detaching property from mode objects to avoid
	 * dangling property pointers:
	 */
	struct drm_property *properties[DRM_OBJECT_MAX_PROPERTY];
	/* do not read/write values directly, but use drm_object_property_get_value()
	 * and drm_object_property_set_value():
	 */
	uint64_t values[DRM_OBJECT_MAX_PROPERTY];
};

static inline int64_t U642I64(uint64_t val)
{
	return (int64_t)*((int64_t *)&val);
}
static inline uint64_t I642U64(int64_t val)
{
	return (uint64_t)*((uint64_t *)&val);
}

/*
 * Rotation property bits. DRM_ROTATE_<degrees> rotates the image by the
 * specified amount in degrees in counter clockwise direction. DRM_REFLECT_X and
 * DRM_REFLECT_Y reflects the image along the specified axis prior to rotation
 */
#define DRM_ROTATE_MASK 0x0f
#define DRM_ROTATE_0	0
#define DRM_ROTATE_90	1
#define DRM_ROTATE_180	2
#define DRM_ROTATE_270	3
#define DRM_REFLECT_MASK (~DRM_ROTATE_MASK)
#define DRM_REFLECT_X	4
#define DRM_REFLECT_Y	5

enum drm_connector_force {
	DRM_FORCE_UNSPECIFIED,
	DRM_FORCE_OFF,
	DRM_FORCE_ON,         /* force on analog part normally */
	DRM_FORCE_ON_DIGITAL, /* for DVI-I use digital connector */
};

#include <drm/drm_modes.h>

enum drm_connector_status {
	connector_status_connected = 1,
	connector_status_disconnected = 2,
	connector_status_unknown = 3,
};

enum subpixel_order {
	SubPixelUnknown = 0,
	SubPixelHorizontalRGB,
	SubPixelHorizontalBGR,
	SubPixelVerticalRGB,
	SubPixelVerticalBGR,
	SubPixelNone,
};

#define DRM_COLOR_FORMAT_RGB444		(1<<0)
#define DRM_COLOR_FORMAT_YCRCB444	(1<<1)
#define DRM_COLOR_FORMAT_YCRCB422	(1<<2)
/*
 * Describes a given display (e.g. CRT or flat panel) and its limitations.
 */
struct drm_display_info {
	char name[DRM_DISPLAY_INFO_LEN];

	/* Physical size */
        unsigned int width_mm;
	unsigned int height_mm;

	/* Clock limits FIXME: storage format */
	unsigned int min_vfreq, max_vfreq;
	unsigned int min_hfreq, max_hfreq;
	unsigned int pixel_clock;
	unsigned int bpc;

	enum subpixel_order subpixel_order;
	u32 color_formats;

	const u32 *bus_formats;
	unsigned int num_bus_formats;

	/* Mask of supported hdmi deep color modes */
	u8 edid_hdmi_dc_modes;

	u8 cea_rev;
};

/* data corresponds to displayid vend/prod/serial */
struct drm_tile_group {
	struct kref refcount;
	struct drm_device *dev;
	int id;
	u8 group_data[8];
};

/**
 * struct drm_framebuffer_funcs - framebuffer hooks
 */
struct drm_framebuffer_funcs {
	/**
	 * @destroy:
	 *
	 * Clean up framebuffer resources, specifically also unreference the
	 * backing storage. The core guarantees to call this function for every
	 * framebuffer successfully created by ->fb_create() in
	 * &drm_mode_config_funcs. Drivers must also call
	 * drm_framebuffer_cleanup() to release DRM core resources for this
	 * framebuffer.
	 */
	void (*destroy)(struct drm_framebuffer *framebuffer);

	/**
	 * @create_handle:
	 *
	 * Create a buffer handle in the driver-specific buffer manager (either
	 * GEM or TTM) valid for the passed-in struct &drm_file. This is used by
	 * the core to implement the GETFB IOCTL, which returns (for
	 * sufficiently priviledged user) also a native buffer handle. This can
	 * be used for seamless transitions between modesetting clients by
	 * copying the current screen contents to a private buffer and blending
	 * between that and the new contents.
	 *
	 * GEM based drivers should call drm_gem_handle_create() to create the
	 * handle.
	 *
	 * RETURNS:
	 *
	 * 0 on success or a negative error code on failure.
	 */
	int (*create_handle)(struct drm_framebuffer *fb,
			     struct drm_file *file_priv,
			     unsigned int *handle);
	/**
	 * @dirty:
	 *
	 * Optional callback for the dirty fb IOCTL.
	 *
	 * Userspace can notify the driver via this callback that an area of the
	 * framebuffer has changed and should be flushed to the display
	 * hardware. This can also be used internally, e.g. by the fbdev
	 * emulation, though that's not the case currently.
	 *
	 * See documentation in drm_mode.h for the struct drm_mode_fb_dirty_cmd
	 * for more information as all the semantics and arguments have a one to
	 * one mapping on this function.
	 *
	 * RETURNS:
	 *
	 * 0 on success or a negative error code on failure.
	 */
	int (*dirty)(struct drm_framebuffer *framebuffer,
		     struct drm_file *file_priv, unsigned flags,
		     unsigned color, struct drm_clip_rect *clips,
		     unsigned num_clips);
};

struct drm_framebuffer {
	struct drm_device *dev;
	/*
	 * Note that the fb is refcounted for the benefit of driver internals,
	 * for example some hw, disabling a CRTC/plane is asynchronous, and
	 * scanout does not actually complete until the next vblank.  So some
	 * cleanup (like releasing the reference(s) on the backing GEM bo(s))
	 * should be deferred.  In cases like this, the driver would like to
	 * hold a ref to the fb even though it has already been removed from
	 * userspace perspective.
	 */
	struct kref refcount;
	/*
	 * Place on the dev->mode_config.fb_list, access protected by
	 * dev->mode_config.fb_lock.
	 */
	struct list_head head;
	struct drm_mode_object base;
	const struct drm_framebuffer_funcs *funcs;
	unsigned int pitches[4];
	unsigned int offsets[4];
	uint64_t modifier[4];
	unsigned int width;
	unsigned int height;
	/* depth can be 15 or 16 */
	unsigned int depth;
	int bits_per_pixel;
	int flags;
	uint32_t pixel_format; /* fourcc format */
	struct list_head filp_head;
};

struct drm_property_blob {
	struct drm_mode_object base;
	struct drm_device *dev;
	struct kref refcount;
	struct list_head head_global;
	struct list_head head_file;
	size_t length;
	unsigned char data[];
};

struct drm_property_enum {
	uint64_t value;
	struct list_head head;
	char name[DRM_PROP_NAME_LEN];
};

struct drm_property {
	struct list_head head;
	struct drm_mode_object base;
	uint32_t flags;
	char name[DRM_PROP_NAME_LEN];
	uint32_t num_values;
	uint64_t *values;
	struct drm_device *dev;

	struct list_head enum_list;
};

struct drm_crtc;
struct drm_connector;
struct drm_encoder;
struct drm_pending_vblank_event;
struct drm_plane;
struct drm_bridge;
struct drm_atomic_state;

struct drm_crtc_helper_funcs;
struct drm_encoder_helper_funcs;
struct drm_connector_helper_funcs;
struct drm_plane_helper_funcs;

/**
 * struct drm_crtc_state - mutable CRTC state
 * @crtc: backpointer to the CRTC
 * @enable: whether the CRTC should be enabled, gates all other state
 * @active: whether the CRTC is actively displaying (used for DPMS)
 * @planes_changed: planes on this crtc are updated
 * @mode_changed: crtc_state->mode or crtc_state->enable has been changed
 * @active_changed: crtc_state->active has been toggled.
 * @connectors_changed: connectors to this crtc have been updated
 * @color_mgmt_changed: color management properties have changed (degamma or
 *	gamma LUT or CSC matrix)
 * @plane_mask: bitmask of (1 << drm_plane_index(plane)) of attached planes
 * @connector_mask: bitmask of (1 << drm_connector_index(connector)) of attached connectors
 * @encoder_mask: bitmask of (1 << drm_encoder_index(encoder)) of attached encoders
 * @last_vblank_count: for helpers and drivers to capture the vblank of the
 * 	update to ensure framebuffer cleanup isn't done too early
 * @adjusted_mode: for use by helpers and drivers to compute adjusted mode timings
 * @mode: current mode timings
 * @degamma_lut: Lookup table for converting framebuffer pixel data
 *	before apply the conversion matrix
 * @ctm: Transformation matrix
 * @gamma_lut: Lookup table for converting pixel data after the
 *	conversion matrix
 * @event: optional pointer to a DRM event to signal upon completion of the
 * 	state update
 * @state: backpointer to global drm_atomic_state
 *
 * Note that the distinction between @enable and @active is rather subtile:
 * Flipping @active while @enable is set without changing anything else may
 * never return in a failure from the ->atomic_check callback. Userspace assumes
 * that a DPMS On will always succeed. In other words: @enable controls resource
 * assignment, @active controls the actual hardware state.
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
	bool color_mgmt_changed : 1;

	/* attached planes bitmask:
	 * WARNING: transitional helpers do not maintain plane_mask so
	 * drivers not converted over to atomic helpers should not rely
	 * on plane_mask being accurate!
	 */
	u32 plane_mask;

	u32 connector_mask;
	u32 encoder_mask;

	/* last_vblank_count: for vblank waits before cleanup */
	u32 last_vblank_count;

	/* adjusted_mode: for use by helpers and drivers */
	struct drm_display_mode adjusted_mode;

	struct drm_display_mode mode;

	/* blob property to expose current mode to atomic userspace */
	struct drm_property_blob *mode_blob;

	/* blob property to expose color management to userspace */
	struct drm_property_blob *degamma_lut;
	struct drm_property_blob *ctm;
	struct drm_property_blob *gamma_lut;

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
	void (*gamma_set)(struct drm_crtc *crtc, u16 *r, u16 *g, u16 *b,
			  uint32_t start, uint32_t size);

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
	 * struct &drm_mode_set - see there for details.
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
	 * configured mode and then calls the CRTC ->page_flip() operation with a
	 * pointer to the new frame buffer.
	 *
	 * The driver must wait for any pending rendering to the new framebuffer
	 * to complete before executing the flip. It should also wait for any
	 * pending rendering from other drivers if the underlying buffer is a
	 * shared dma-buf.
	 *
	 * An application can request to be notified when the page flip has
	 * completed. The drm core will supply a struct &drm_event in the event
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
	 * FIXME:
	 *
	 * Up to that point drivers need to manage events themselves and can use
	 * even->base.list freely for that. Specifically they need to ensure
	 * that they don't send out page flip (or vblank) events for which the
	 * corresponding drm file has been closed already. The drm core
	 * unfortunately does not (yet) take care of that. Therefore drivers
	 * currently must clean up and release pending events in their
	 * ->preclose driver function.
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
	 * ->page_flip() operation is already pending the callback should return
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
	 * The core and helpers gurantee that any atomic state duplicated with
	 * this hook and still owned by the caller (i.e. not transferred to the
	 * driver by calling ->atomic_commit() from struct
	 * &drm_mode_config_funcs) will be cleaned up by calling the
	 * @atomic_destroy_state hook in this structure.
	 *
	 * Atomic drivers which don't subclass struct &drm_crtc should use
	 * drm_atomic_helper_crtc_duplicate_state(). Drivers that subclass the
	 * state structure to extend it with driver-private state should use
	 * __drm_atomic_helper_crtc_duplicate_state() to make sure shared state is
	 * duplicated in a consistent fashion across drivers.
	 *
	 * It is an error to call this hook before crtc->state has been
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
};

/**
 * struct drm_crtc - central CRTC control structure
 * @dev: parent DRM device
 * @port: OF node used by drm_of_find_possible_crtcs()
 * @head: list management
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
 * @state: current atomic state for this CRTC
 * @acquire_ctx: per-CRTC implicit acquire context used by atomic drivers for
 * 	legacy IOCTLs
 *
 * Each CRTC may have one or more connectors associated with it.  This structure
 * allows the CRTC to be controlled.
 */
struct drm_crtc {
	struct drm_device *dev;
	struct device_node *port;
	struct list_head head;

	char *name;

	/*
	 * crtc mutex
	 *
	 * This provides a read lock for the overall crtc state (mode, dpms
	 * state, ...) and a write lock for everything which can be update
	 * without a full modeset (fb, cursor data, ...)
	 */
	struct drm_modeset_lock mutex;

	struct drm_mode_object base;

	/* primary and cursor planes for CRTC */
	struct drm_plane *primary;
	struct drm_plane *cursor;

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

	struct drm_crtc_state *state;

	/*
	 * For legacy crtc IOCTLs so that atomic drivers can get at the locking
	 * acquire context.
	 */
	struct drm_modeset_acquire_ctx *acquire_ctx;
};

/**
 * struct drm_connector_state - mutable connector state
 * @connector: backpointer to the connector
 * @crtc: CRTC to connect connector to, NULL if disabled
 * @best_encoder: can be used by helpers and drivers to select the encoder
 * @state: backpointer to global drm_atomic_state
 */
struct drm_connector_state {
	struct drm_connector *connector;

	struct drm_crtc *crtc;  /* do not write directly, use drm_atomic_set_crtc_for_connector() */

	struct drm_encoder *best_encoder;

	struct drm_atomic_state *state;
};

/**
 * struct drm_connector_funcs - control connectors on a given device
 *
 * Each CRTC may have one or more connectors attached to it.  The functions
 * below allow the core DRM code to control connectors, enumerate available modes,
 * etc.
 */
struct drm_connector_funcs {
	/**
	 * @dpms:
	 *
	 * Legacy entry point to set the per-connector DPMS state. Legacy DPMS
	 * is exposed as a standard property on the connector, but diverted to
	 * this callback in the drm core. Note that atomic drivers don't
	 * implement the 4 level DPMS support on the connector any more, but
	 * instead only have an on/off "ACTIVE" property on the CRTC object.
	 *
	 * Drivers implementing atomic modeset should use
	 * drm_atomic_helper_connector_dpms() to implement this hook.
	 *
	 * RETURNS:
	 *
	 * 0 on success or a negative error code on failure.
	 */
	int (*dpms)(struct drm_connector *connector, int mode);

	/**
	 * @reset:
	 *
	 * Reset connector hardware and software state to off. This function isn't
	 * called by the core directly, only through drm_mode_config_reset().
	 * It's not a helper hook only for historical reasons.
	 *
	 * Atomic drivers can use drm_atomic_helper_connector_reset() to reset
	 * atomic state using this hook.
	 */
	void (*reset)(struct drm_connector *connector);

	/**
	 * @detect:
	 *
	 * Check to see if anything is attached to the connector. The parameter
	 * force is set to false whilst polling, true when checking the
	 * connector due to a user request. force can be used by the driver to
	 * avoid expensive, destructive operations during automated probing.
	 *
	 * FIXME:
	 *
	 * Note that this hook is only called by the probe helper. It's not in
	 * the helper library vtable purely for historical reasons. The only DRM
	 * core	entry point to probe connector state is @fill_modes.
	 *
	 * RETURNS:
	 *
	 * drm_connector_status indicating the connector's status.
	 */
	enum drm_connector_status (*detect)(struct drm_connector *connector,
					    bool force);

	/**
	 * @force:
	 *
	 * This function is called to update internal encoder state when the
	 * connector is forced to a certain state by userspace, either through
	 * the sysfs interfaces or on the kernel cmdline. In that case the
	 * @detect callback isn't called.
	 *
	 * FIXME:
	 *
	 * Note that this hook is only called by the probe helper. It's not in
	 * the helper library vtable purely for historical reasons. The only DRM
	 * core	entry point to probe connector state is @fill_modes.
	 */
	void (*force)(struct drm_connector *connector);

	/**
	 * @fill_modes:
	 *
	 * Entry point for output detection and basic mode validation. The
	 * driver should reprobe the output if needed (e.g. when hotplug
	 * handling is unreliable), add all detected modes to connector->modes
	 * and filter out any the device can't support in any configuration. It
	 * also needs to filter out any modes wider or higher than the
	 * parameters max_width and max_height indicate.
	 *
	 * The drivers must also prune any modes no longer valid from
	 * connector->modes. Furthermore it must update connector->status and
	 * connector->edid.  If no EDID has been received for this output
	 * connector->edid must be NULL.
	 *
	 * Drivers using the probe helpers should use
	 * drm_helper_probe_single_connector_modes() or
	 * drm_helper_probe_single_connector_modes_nomerge() to implement this
	 * function.
	 *
	 * RETURNS:
	 *
	 * The number of modes detected and filled into connector->modes.
	 */
	int (*fill_modes)(struct drm_connector *connector, uint32_t max_width, uint32_t max_height);

	/**
	 * @set_property:
	 *
	 * This is the legacy entry point to update a property attached to the
	 * connector.
	 *
	 * Drivers implementing atomic modeset should use
	 * drm_atomic_helper_connector_set_property() to implement this hook.
	 *
	 * This callback is optional if the driver does not support any legacy
	 * driver-private properties.
	 *
	 * RETURNS:
	 *
	 * 0 on success or a negative error code on failure.
	 */
	int (*set_property)(struct drm_connector *connector, struct drm_property *property,
			     uint64_t val);

	/**
	 * @destroy:
	 *
	 * Clean up connector resources. This is called at driver unload time
	 * through drm_mode_config_cleanup(). It can also be called at runtime
	 * when a connector is being hot-unplugged for drivers that support
	 * connector hotplugging (e.g. DisplayPort MST).
	 */
	void (*destroy)(struct drm_connector *connector);

	/**
	 * @atomic_duplicate_state:
	 *
	 * Duplicate the current atomic state for this connector and return it.
	 * The core and helpers gurantee that any atomic state duplicated with
	 * this hook and still owned by the caller (i.e. not transferred to the
	 * driver by calling ->atomic_commit() from struct
	 * &drm_mode_config_funcs) will be cleaned up by calling the
	 * @atomic_destroy_state hook in this structure.
	 *
	 * Atomic drivers which don't subclass struct &drm_connector_state should use
	 * drm_atomic_helper_connector_duplicate_state(). Drivers that subclass the
	 * state structure to extend it with driver-private state should use
	 * __drm_atomic_helper_connector_duplicate_state() to make sure shared state is
	 * duplicated in a consistent fashion across drivers.
	 *
	 * It is an error to call this hook before connector->state has been
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
	struct drm_connector_state *(*atomic_duplicate_state)(struct drm_connector *connector);

	/**
	 * @atomic_destroy_state:
	 *
	 * Destroy a state duplicated with @atomic_duplicate_state and release
	 * or unreference all resources it references
	 */
	void (*atomic_destroy_state)(struct drm_connector *connector,
				     struct drm_connector_state *state);

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
	 * drm_atomic_connector_set_property() instead.
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
	 * implemented by the driver (which shouldn't ever happen, the core only
	 * asks for properties attached to this connector). No other validation
	 * is allowed by the driver. The core already checks that the property
	 * value is within the range (integer, valid enum value, ...) the driver
	 * set when registering the property.
	 */
	int (*atomic_set_property)(struct drm_connector *connector,
				   struct drm_connector_state *state,
				   struct drm_property *property,
				   uint64_t val);

	/**
	 * @atomic_get_property:
	 *
	 * Reads out the decoded driver-private property. This is used to
	 * implement the GETCONNECTOR IOCTL.
	 *
	 * Do not call this function directly, use
	 * drm_atomic_connector_get_property() instead.
	 *
	 * This callback is optional if the driver does not support any
	 * driver-private atomic properties.
	 *
	 * RETURNS:
	 *
	 * 0 on success, -EINVAL if the property isn't implemented by the
	 * driver (which shouldn't ever happen, the core only asks for
	 * properties attached to this connector).
	 */
	int (*atomic_get_property)(struct drm_connector *connector,
				   const struct drm_connector_state *state,
				   struct drm_property *property,
				   uint64_t *val);
};

/**
 * struct drm_encoder_funcs - encoder controls
 *
 * Encoders sit between CRTCs and connectors.
 */
struct drm_encoder_funcs {
	/**
	 * @reset:
	 *
	 * Reset encoder hardware and software state to off. This function isn't
	 * called by the core directly, only through drm_mode_config_reset().
	 * It's not a helper hook only for historical reasons.
	 */
	void (*reset)(struct drm_encoder *encoder);

	/**
	 * @destroy:
	 *
	 * Clean up encoder resources. This is only called at driver unload time
	 * through drm_mode_config_cleanup() since an encoder cannot be
	 * hotplugged in DRM.
	 */
	void (*destroy)(struct drm_encoder *encoder);
};

#define DRM_CONNECTOR_MAX_ENCODER 3

/**
 * struct drm_encoder - central DRM encoder structure
 * @dev: parent DRM device
 * @head: list management
 * @base: base KMS object
 * @name: encoder name
 * @encoder_type: one of the %DRM_MODE_ENCODER_<foo> types in drm_mode.h
 * @possible_crtcs: bitmask of potential CRTC bindings
 * @possible_clones: bitmask of potential sibling encoders for cloning
 * @crtc: currently bound CRTC
 * @bridge: bridge associated to the encoder
 * @funcs: control functions
 * @helper_private: mid-layer private data
 *
 * CRTCs drive pixels to encoders, which convert them into signals
 * appropriate for a given connector or set of connectors.
 */
struct drm_encoder {
	struct drm_device *dev;
	struct list_head head;

	struct drm_mode_object base;
	char *name;
	int encoder_type;
	uint32_t possible_crtcs;
	uint32_t possible_clones;

	struct drm_crtc *crtc;
	struct drm_bridge *bridge;
	const struct drm_encoder_funcs *funcs;
	const struct drm_encoder_helper_funcs *helper_private;
};

/* should we poll this connector for connects and disconnects */
/* hot plug detectable */
#define DRM_CONNECTOR_POLL_HPD (1 << 0)
/* poll for connections */
#define DRM_CONNECTOR_POLL_CONNECT (1 << 1)
/* can cleanly poll for disconnections without flickering the screen */
/* DACs should rarely do this without a lot of testing */
#define DRM_CONNECTOR_POLL_DISCONNECT (1 << 2)

#define MAX_ELD_BYTES	128

/**
 * struct drm_connector - central DRM connector control structure
 * @dev: parent DRM device
 * @kdev: kernel device for sysfs attributes
 * @attr: sysfs attributes
 * @head: list management
 * @base: base KMS object
 * @name: connector name
 * @connector_type: one of the %DRM_MODE_CONNECTOR_<foo> types from drm_mode.h
 * @connector_type_id: index into connector type enum
 * @interlace_allowed: can this connector handle interlaced modes?
 * @doublescan_allowed: can this connector handle doublescan?
 * @stereo_allowed: can this connector handle stereo modes?
 * @modes: modes available on this connector (from fill_modes() + user)
 * @status: one of the drm_connector_status enums (connected, not, or unknown)
 * @probed_modes: list of modes derived directly from the display
 * @display_info: information about attached display (e.g. from EDID)
 * @funcs: connector control functions
 * @edid_blob_ptr: DRM property containing EDID if present
 * @properties: property tracking for this connector
 * @path_blob_ptr: DRM blob property data for the DP MST path property
 * @polled: a %DRM_CONNECTOR_POLL_<foo> value for core driven polling
 * @dpms: current dpms state
 * @helper_private: mid-layer private data
 * @cmdline_mode: mode line parsed from the kernel cmdline for this connector
 * @force: a %DRM_FORCE_<foo> state for forced mode sets
 * @override_edid: has the EDID been overwritten through debugfs for testing?
 * @encoder_ids: valid encoders for this connector
 * @encoder: encoder driving this connector, if any
 * @eld: EDID-like data, if present
 * @dvi_dual: dual link DVI, if found
 * @max_tmds_clock: max clock rate, if found
 * @latency_present: AV delay info from ELD, if found
 * @video_latency: video latency info from ELD, if found
 * @audio_latency: audio latency info from ELD, if found
 * @null_edid_counter: track sinks that give us all zeros for the EDID
 * @bad_edid_counter: track sinks that give us an EDID with invalid checksum
 * @edid_corrupt: indicates whether the last read EDID was corrupt
 * @debugfs_entry: debugfs directory for this connector
 * @state: current atomic state for this connector
 * @has_tile: is this connector connected to a tiled monitor
 * @tile_group: tile group for the connected monitor
 * @tile_is_single_monitor: whether the tile is one monitor housing
 * @num_h_tile: number of horizontal tiles in the tile group
 * @num_v_tile: number of vertical tiles in the tile group
 * @tile_h_loc: horizontal location of this tile
 * @tile_v_loc: vertical location of this tile
 * @tile_h_size: horizontal size of this tile.
 * @tile_v_size: vertical size of this tile.
 *
 * Each connector may be connected to one or more CRTCs, or may be clonable by
 * another connector if they can share a CRTC.  Each connector also has a specific
 * position in the broader display (referred to as a 'screen' though it could
 * span multiple monitors).
 */
struct drm_connector {
	struct drm_device *dev;
	struct device *kdev;
	struct device_attribute *attr;
	struct list_head head;

	struct drm_mode_object base;

	char *name;
	int connector_id;
	int connector_type;
	int connector_type_id;
	bool interlace_allowed;
	bool doublescan_allowed;
	bool stereo_allowed;
	struct list_head modes; /* list of modes on this connector */

	enum drm_connector_status status;

	/* these are modes added by probing with DDC or the BIOS */
	struct list_head probed_modes;

	struct drm_display_info display_info;
	const struct drm_connector_funcs *funcs;

	struct drm_property_blob *edid_blob_ptr;
	struct drm_object_properties properties;

	struct drm_property_blob *path_blob_ptr;

	struct drm_property_blob *tile_blob_ptr;

	uint8_t polled; /* DRM_CONNECTOR_POLL_* */

	/* requested DPMS state */
	int dpms;

	const struct drm_connector_helper_funcs *helper_private;

	/* forced on connector */
	struct drm_cmdline_mode cmdline_mode;
	enum drm_connector_force force;
	bool override_edid;
	uint32_t encoder_ids[DRM_CONNECTOR_MAX_ENCODER];
	struct drm_encoder *encoder; /* currently active encoder */

	/* EDID bits */
	uint8_t eld[MAX_ELD_BYTES];
	bool dvi_dual;
	int max_tmds_clock;	/* in MHz */
	bool latency_present[2];
	int video_latency[2];	/* [0]: progressive, [1]: interlaced */
	int audio_latency[2];
	int null_edid_counter; /* needed to workaround some HW bugs where we get all 0s */
	unsigned bad_edid_counter;

	/* Flag for raw EDID header corruption - used in Displayport
	 * compliance testing - * Displayport Link CTS Core 1.2 rev1.1 4.2.2.6
	 */
	bool edid_corrupt;

	struct dentry *debugfs_entry;

	struct drm_connector_state *state;

	/* DisplayID bits */
	bool has_tile;
	struct drm_tile_group *tile_group;
	bool tile_is_single_monitor;

	uint8_t num_h_tile, num_v_tile;
	uint8_t tile_h_loc, tile_v_loc;
	uint16_t tile_h_size, tile_v_size;
};

/**
 * struct drm_plane_state - mutable plane state
 * @plane: backpointer to the plane
 * @crtc: currently bound CRTC, NULL if disabled
 * @fb: currently bound framebuffer
 * @fence: optional fence to wait for before scanning out @fb
 * @crtc_x: left position of visible portion of plane on crtc
 * @crtc_y: upper position of visible portion of plane on crtc
 * @crtc_w: width of visible portion of plane on crtc
 * @crtc_h: height of visible portion of plane on crtc
 * @src_x: left position of visible portion of plane within
 *	plane (in 16.16)
 * @src_y: upper position of visible portion of plane within
 *	plane (in 16.16)
 * @src_w: width of visible portion of plane (in 16.16)
 * @src_h: height of visible portion of plane (in 16.16)
 * @state: backpointer to global drm_atomic_state
 */
struct drm_plane_state {
	struct drm_plane *plane;

	struct drm_crtc *crtc;   /* do not write directly, use drm_atomic_set_crtc_for_plane() */
	struct drm_framebuffer *fb;  /* do not write directly, use drm_atomic_set_fb_for_plane() */
	struct fence *fence;

	/* Signed dest location allows it to be partially off screen */
	int32_t crtc_x, crtc_y;
	uint32_t crtc_w, crtc_h;

	/* Source values are 16.16 fixed point */
	uint32_t src_x, src_y;
	uint32_t src_h, src_w;

	/* Plane rotation */
	unsigned int rotation;

	struct drm_atomic_state *state;
};


/**
 * struct drm_plane_funcs - driver plane control functions
 */
struct drm_plane_funcs {
	/**
	 * @update_plane:
	 *
	 * This is the legacy entry point to enable and configure the plane for
	 * the given CRTC and framebuffer. It is never called to disable the
	 * plane, i.e. the passed-in crtc and fb paramters are never NULL.
	 *
	 * The source rectangle in frame buffer memory coordinates is given by
	 * the src_x, src_y, src_w and src_h parameters (as 16.16 fixed point
	 * values). Devices that don't support subpixel plane coordinates can
	 * ignore the fractional part.
	 *
	 * The destination rectangle in CRTC coordinates is given by the
	 * crtc_x, crtc_y, crtc_w and crtc_h parameters (as integer values).
	 * Devices scale the source rectangle to the destination rectangle. If
	 * scaling is not supported, and the source rectangle size doesn't match
	 * the destination rectangle size, the driver must return a
	 * -<errorname>EINVAL</errorname> error.
	 *
	 * Drivers implementing atomic modeset should use
	 * drm_atomic_helper_update_plane() to implement this hook.
	 *
	 * RETURNS:
	 *
	 * 0 on success or a negative error code on failure.
	 */
	int (*update_plane)(struct drm_plane *plane,
			    struct drm_crtc *crtc, struct drm_framebuffer *fb,
			    int crtc_x, int crtc_y,
			    unsigned int crtc_w, unsigned int crtc_h,
			    uint32_t src_x, uint32_t src_y,
			    uint32_t src_w, uint32_t src_h);

	/**
	 * @disable_plane:
	 *
	 * This is the legacy entry point to disable the plane. The DRM core
	 * calls this method in response to a DRM_IOCTL_MODE_SETPLANE IOCTL call
	 * with the frame buffer ID set to 0.  Disabled planes must not be
	 * processed by the CRTC.
	 *
	 * Drivers implementing atomic modeset should use
	 * drm_atomic_helper_disable_plane() to implement this hook.
	 *
	 * RETURNS:
	 *
	 * 0 on success or a negative error code on failure.
	 */
	int (*disable_plane)(struct drm_plane *plane);

	/**
	 * @destroy:
	 *
	 * Clean up plane resources. This is only called at driver unload time
	 * through drm_mode_config_cleanup() since a plane cannot be hotplugged
	 * in DRM.
	 */
	void (*destroy)(struct drm_plane *plane);

	/**
	 * @reset:
	 *
	 * Reset plane hardware and software state to off. This function isn't
	 * called by the core directly, only through drm_mode_config_reset().
	 * It's not a helper hook only for historical reasons.
	 *
	 * Atomic drivers can use drm_atomic_helper_plane_reset() to reset
	 * atomic state using this hook.
	 */
	void (*reset)(struct drm_plane *plane);

	/**
	 * @set_property:
	 *
	 * This is the legacy entry point to update a property attached to the
	 * plane.
	 *
	 * Drivers implementing atomic modeset should use
	 * drm_atomic_helper_plane_set_property() to implement this hook.
	 *
	 * This callback is optional if the driver does not support any legacy
	 * driver-private properties.
	 *
	 * RETURNS:
	 *
	 * 0 on success or a negative error code on failure.
	 */
	int (*set_property)(struct drm_plane *plane,
			    struct drm_property *property, uint64_t val);

	/**
	 * @atomic_duplicate_state:
	 *
	 * Duplicate the current atomic state for this plane and return it.
	 * The core and helpers gurantee that any atomic state duplicated with
	 * this hook and still owned by the caller (i.e. not transferred to the
	 * driver by calling ->atomic_commit() from struct
	 * &drm_mode_config_funcs) will be cleaned up by calling the
	 * @atomic_destroy_state hook in this structure.
	 *
	 * Atomic drivers which don't subclass struct &drm_plane_state should use
	 * drm_atomic_helper_plane_duplicate_state(). Drivers that subclass the
	 * state structure to extend it with driver-private state should use
	 * __drm_atomic_helper_plane_duplicate_state() to make sure shared state is
	 * duplicated in a consistent fashion across drivers.
	 *
	 * It is an error to call this hook before plane->state has been
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
	struct drm_plane_state *(*atomic_duplicate_state)(struct drm_plane *plane);

	/**
	 * @atomic_destroy_state:
	 *
	 * Destroy a state duplicated with @atomic_duplicate_state and release
	 * or unreference all resources it references
	 */
	void (*atomic_destroy_state)(struct drm_plane *plane,
				     struct drm_plane_state *state);

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
	 * drm_atomic_plane_set_property() instead.
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
	 * implemented by the driver (which shouldn't ever happen, the core only
	 * asks for properties attached to this plane). No other validation is
	 * allowed by the driver. The core already checks that the property
	 * value is within the range (integer, valid enum value, ...) the driver
	 * set when registering the property.
	 */
	int (*atomic_set_property)(struct drm_plane *plane,
				   struct drm_plane_state *state,
				   struct drm_property *property,
				   uint64_t val);

	/**
	 * @atomic_get_property:
	 *
	 * Reads out the decoded driver-private property. This is used to
	 * implement the GETPLANE IOCTL.
	 *
	 * Do not call this function directly, use
	 * drm_atomic_plane_get_property() instead.
	 *
	 * This callback is optional if the driver does not support any
	 * driver-private atomic properties.
	 *
	 * RETURNS:
	 *
	 * 0 on success, -EINVAL if the property isn't implemented by the
	 * driver (which should never happen, the core only asks for
	 * properties attached to this plane).
	 */
	int (*atomic_get_property)(struct drm_plane *plane,
				   const struct drm_plane_state *state,
				   struct drm_property *property,
				   uint64_t *val);
};

enum drm_plane_type {
	DRM_PLANE_TYPE_OVERLAY,
	DRM_PLANE_TYPE_PRIMARY,
	DRM_PLANE_TYPE_CURSOR,
};


/**
 * struct drm_plane - central DRM plane control structure
 * @dev: DRM device this plane belongs to
 * @head: for list management
 * @base: base mode object
 * @possible_crtcs: pipes this plane can be bound to
 * @format_types: array of formats supported by this plane
 * @format_count: number of formats supported
 * @format_default: driver hasn't supplied supported formats for the plane
 * @crtc: currently bound CRTC
 * @fb: currently bound fb
 * @old_fb: Temporary tracking of the old fb while a modeset is ongoing. Used by
 * 	drm_mode_set_config_internal() to implement correct refcounting.
 * @funcs: helper functions
 * @properties: property tracking for this plane
 * @type: type of plane (overlay, primary, cursor)
 * @state: current atomic state for this plane
 */
struct drm_plane {
	struct drm_device *dev;
	struct list_head head;

	char *name;

	struct drm_modeset_lock mutex;

	struct drm_mode_object base;

	uint32_t possible_crtcs;
	uint32_t *format_types;
	unsigned int format_count;
	bool format_default;

	struct drm_crtc *crtc;
	struct drm_framebuffer *fb;

	struct drm_framebuffer *old_fb;

	const struct drm_plane_funcs *funcs;

	struct drm_object_properties properties;

	enum drm_plane_type type;

	const struct drm_plane_helper_funcs *helper_private;

	struct drm_plane_state *state;
};

/**
 * struct drm_bridge_funcs - drm_bridge control functions
 * @attach: Called during drm_bridge_attach
 */
struct drm_bridge_funcs {
	int (*attach)(struct drm_bridge *bridge);

	/**
	 * @mode_fixup:
	 *
	 * This callback is used to validate and adjust a mode. The paramater
	 * mode is the display mode that should be fed to the next element in
	 * the display chain, either the final &drm_connector or the next
	 * &drm_bridge. The parameter adjusted_mode is the input mode the bridge
	 * requires. It can be modified by this callback and does not need to
	 * match mode.
	 *
	 * This is the only hook that allows a bridge to reject a modeset. If
	 * this function passes all other callbacks must succeed for this
	 * configuration.
	 *
	 * NOTE:
	 *
	 * This function is called in the check phase of atomic modesets, which
	 * can be aborted for any reason (including on userspace's request to
	 * just check whether a configuration would be possible). Drivers MUST
	 * NOT touch any persistent state (hardware or software) or data
	 * structures except the passed in @state parameter.
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
	 * bridge's ->disable() function. If the preceding element is a
	 * &drm_encoder it's called right before the encoder's ->disable(),
	 * ->prepare() or ->dpms() hook from struct &drm_encoder_helper_funcs.
	 *
	 * The bridge can assume that the display pipe (i.e. clocks and timing
	 * signals) feeding it is still running when this callback is called.
	 */
	void (*disable)(struct drm_bridge *bridge);

	/**
	 * @post_disable:
	 *
	 * This callback should disable the bridge. It is called right after
	 * the preceding element in the display pipe is disabled. If the
	 * preceding element is a bridge this means it's called after that
	 * bridge's ->post_disable() function. If the preceding element is a
	 * &drm_encoder it's called right after the encoder's ->disable(),
	 * ->prepare() or ->dpms() hook from struct &drm_encoder_helper_funcs.
	 *
	 * The bridge must assume that the display pipe (i.e. clocks and timing
	 * singals) feeding it is no longer running when this callback is
	 * called.
	 */
	void (*post_disable)(struct drm_bridge *bridge);

	/**
	 * @mode_set:
	 *
	 * This callback should set the given mode on the bridge. It is called
	 * after the ->mode_set() callback for the preceding element in the
	 * display pipeline has been called already. The display pipe (i.e.
	 * clocks and timing signals) is off when this function is called.
	 */
	void (*mode_set)(struct drm_bridge *bridge,
			 struct drm_display_mode *mode,
			 struct drm_display_mode *adjusted_mode);
	/**
	 * @pre_enable:
	 *
	 * This callback should enable the bridge. It is called right before
	 * the preceding element in the display pipe is enabled. If the
	 * preceding element is a bridge this means it's called before that
	 * bridge's ->pre_enable() function. If the preceding element is a
	 * &drm_encoder it's called right before the encoder's ->enable(),
	 * ->commit() or ->dpms() hook from struct &drm_encoder_helper_funcs.
	 *
	 * The display pipe (i.e. clocks and timing signals) feeding this bridge
	 * will not yet be running when this callback is called. The bridge must
	 * not enable the display link feeding the next bridge in the chain (if
	 * there is one) when this callback is called.
	 */
	void (*pre_enable)(struct drm_bridge *bridge);

	/**
	 * @enable:
	 *
	 * This callback should enable the bridge. It is called right after
	 * the preceding element in the display pipe is enabled. If the
	 * preceding element is a bridge this means it's called after that
	 * bridge's ->enable() function. If the preceding element is a
	 * &drm_encoder it's called right after the encoder's ->enable(),
	 * ->commit() or ->dpms() hook from struct &drm_encoder_helper_funcs.
	 *
	 * The bridge can assume that the display pipe (i.e. clocks and timing
	 * signals) feeding it is running when this callback is called. This
	 * callback must enable the display link feeding the next bridge in the
	 * chain if there is one.
	 */
	void (*enable)(struct drm_bridge *bridge);
};

/**
 * struct drm_bridge - central DRM bridge control structure
 * @dev: DRM device this bridge belongs to
 * @encoder: encoder to which this bridge is connected
 * @next: the next bridge in the encoder chain
 * @of_node: device node pointer to the bridge
 * @list: to keep track of all added bridges
 * @funcs: control functions
 * @driver_private: pointer to the bridge driver's internal context
 */
struct drm_bridge {
	struct drm_device *dev;
	struct drm_encoder *encoder;
	struct drm_bridge *next;
#ifdef CONFIG_OF
	struct device_node *of_node;
#endif
	struct list_head list;

	const struct drm_bridge_funcs *funcs;
	void *driver_private;
};

/**
 * struct drm_atomic_state - the global state object for atomic updates
 * @dev: parent DRM device
 * @allow_modeset: allow full modeset
 * @legacy_cursor_update: hint to enforce legacy cursor IOCTL semantics
 * @legacy_set_config: Disable conflicting encoders instead of failing with -EINVAL.
 * @planes: pointer to array of plane pointers
 * @plane_states: pointer to array of plane states pointers
 * @crtcs: pointer to array of CRTC pointers
 * @crtc_states: pointer to array of CRTC states pointers
 * @num_connector: size of the @connectors and @connector_states arrays
 * @connectors: pointer to array of connector pointers
 * @connector_states: pointer to array of connector states pointers
 * @acquire_ctx: acquire context for this atomic modeset state update
 */
struct drm_atomic_state {
	struct drm_device *dev;
	bool allow_modeset : 1;
	bool legacy_cursor_update : 1;
	bool legacy_set_config : 1;
	struct drm_plane **planes;
	struct drm_plane_state **plane_states;
	struct drm_crtc **crtcs;
	struct drm_crtc_state **crtc_states;
	int num_connector;
	struct drm_connector **connectors;
	struct drm_connector_state **connector_states;

	struct drm_modeset_acquire_ctx *acquire_ctx;
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
 * Represents a single crtc the connectors that it drives with what mode
 * and from which framebuffer it scans out from.
 *
 * This is used to set modes.
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

/**
 * struct drm_mode_config_funcs - basic driver provided mode setting functions
 *
 * Some global (i.e. not per-CRTC, connector, etc) mode setting functions that
 * involve drivers.
 */
struct drm_mode_config_funcs {
	/**
	 * @fb_create:
	 *
	 * Create a new framebuffer object. The core does basic checks on the
	 * requested metadata, but most of that is left to the driver. See
	 * struct &drm_mode_fb_cmd2 for details.
	 *
	 * If the parameters are deemed valid and the backing storage objects in
	 * the underlying memory manager all exist, then the driver allocates
	 * a new &drm_framebuffer structure, subclassed to contain
	 * driver-specific information (like the internal native buffer object
	 * references). It also needs to fill out all relevant metadata, which
	 * should be done by calling drm_helper_mode_fill_fb_struct().
	 *
	 * The initialization is finalized by calling drm_framebuffer_init(),
	 * which registers the framebuffer and makes it accessible to other
	 * threads.
	 *
	 * RETURNS:
	 *
	 * A new framebuffer with an initial reference count of 1 or a negative
	 * error code encoded with ERR_PTR().
	 */
	struct drm_framebuffer *(*fb_create)(struct drm_device *dev,
					     struct drm_file *file_priv,
					     const struct drm_mode_fb_cmd2 *mode_cmd);

	/**
	 * @output_poll_changed:
	 *
	 * Callback used by helpers to inform the driver of output configuration
	 * changes.
	 *
	 * Drivers implementing fbdev emulation with the helpers can call
	 * drm_fb_helper_hotplug_changed from this hook to inform the fbdev
	 * helper of output changes.
	 *
	 * FIXME:
	 *
	 * Except that there's no vtable for device-level helper callbacks
	 * there's no reason this is a core function.
	 */
	void (*output_poll_changed)(struct drm_device *dev);

	/**
	 * @atomic_check:
	 *
	 * This is the only hook to validate an atomic modeset update. This
	 * function must reject any modeset and state changes which the hardware
	 * or driver doesn't support. This includes but is of course not limited
	 * to:
	 *
	 *  - Checking that the modes, framebuffers, scaling and placement
	 *    requirements and so on are within the limits of the hardware.
	 *
	 *  - Checking that any hidden shared resources are not oversubscribed.
	 *    This can be shared PLLs, shared lanes, overall memory bandwidth,
	 *    display fifo space (where shared between planes or maybe even
	 *    CRTCs).
	 *
	 *  - Checking that virtualized resources exported to userspace are not
	 *    oversubscribed. For various reasons it can make sense to expose
	 *    more planes, crtcs or encoders than which are physically there. One
	 *    example is dual-pipe operations (which generally should be hidden
	 *    from userspace if when lockstepped in hardware, exposed otherwise),
	 *    where a plane might need 1 hardware plane (if it's just on one
	 *    pipe), 2 hardware planes (when it spans both pipes) or maybe even
	 *    shared a hardware plane with a 2nd plane (if there's a compatible
	 *    plane requested on the area handled by the other pipe).
	 *
	 *  - Check that any transitional state is possible and that if
	 *    requested, the update can indeed be done in the vblank period
	 *    without temporarily disabling some functions.
	 *
	 *  - Check any other constraints the driver or hardware might have.
	 *
	 *  - This callback also needs to correctly fill out the &drm_crtc_state
	 *    in this update to make sure that drm_atomic_crtc_needs_modeset()
	 *    reflects the nature of the possible update and returns true if and
	 *    only if the update cannot be applied without tearing within one
	 *    vblank on that CRTC. The core uses that information to reject
	 *    updates which require a full modeset (i.e. blanking the screen, or
	 *    at least pausing updates for a substantial amount of time) if
	 *    userspace has disallowed that in its request.
	 *
	 *  - The driver also does not need to repeat basic input validation
	 *    like done for the corresponding legacy entry points. The core does
	 *    that before calling this hook.
	 *
	 * See the documentation of @atomic_commit for an exhaustive list of
	 * error conditions which don't have to be checked at the
	 * ->atomic_check() stage?
	 *
	 * See the documentation for struct &drm_atomic_state for how exactly
	 * an atomic modeset update is described.
	 *
	 * Drivers using the atomic helpers can implement this hook using
	 * drm_atomic_helper_check(), or one of the exported sub-functions of
	 * it.
	 *
	 * RETURNS:
	 *
	 * 0 on success or one of the below negative error codes:
	 *
	 *  - -EINVAL, if any of the above constraints are violated.
	 *
	 *  - -EDEADLK, when returned from an attempt to acquire an additional
	 *    &drm_modeset_lock through drm_modeset_lock().
	 *
	 *  - -ENOMEM, if allocating additional state sub-structures failed due
	 *    to lack of memory.
	 *
	 *  - -EINTR, -EAGAIN or -ERESTARTSYS, if the IOCTL should be restarted.
	 *    This can either be due to a pending signal, or because the driver
	 *    needs to completely bail out to recover from an exceptional
	 *    situation like a GPU hang. From a userspace point all errors are
	 *    treated equally.
	 */
	int (*atomic_check)(struct drm_device *dev,
			    struct drm_atomic_state *state);

	/**
	 * @atomic_commit:
	 *
	 * This is the only hook to commit an atomic modeset update. The core
	 * guarantees that @atomic_check has been called successfully before
	 * calling this function, and that nothing has been changed in the
	 * interim.
	 *
	 * See the documentation for struct &drm_atomic_state for how exactly
	 * an atomic modeset update is described.
	 *
	 * Drivers using the atomic helpers can implement this hook using
	 * drm_atomic_helper_commit(), or one of the exported sub-functions of
	 * it.
	 *
	 * Asynchronous commits (as indicated with the async parameter) must
	 * do any preparatory work which might result in an unsuccessful commit
	 * in the context of this callback. The only exceptions are hardware
	 * errors resulting in -EIO. But even in that case the driver must
	 * ensure that the display pipe is at least running, to avoid
	 * compositors crashing when pageflips don't work. Anything else,
	 * specifically committing the update to the hardware, should be done
	 * without blocking the caller. For updates which do not require a
	 * modeset this must be guaranteed.
	 *
	 * The driver must wait for any pending rendering to the new
	 * framebuffers to complete before executing the flip. It should also
	 * wait for any pending rendering from other drivers if the underlying
	 * buffer is a shared dma-buf. Asynchronous commits must not wait for
	 * rendering in the context of this callback.
	 *
	 * An application can request to be notified when the atomic commit has
	 * completed. These events are per-CRTC and can be distinguished by the
	 * CRTC index supplied in &drm_event to userspace.
	 *
	 * The drm core will supply a struct &drm_event in the event
	 * member of each CRTC's &drm_crtc_state structure. This can be handled by the
	 * drm_crtc_send_vblank_event() function, which the driver should call on
	 * the provided event upon completion of the atomic commit. Note that if
	 * the driver supports vblank signalling and timestamping the vblank
	 * counters and timestamps must agree with the ones returned from page
	 * flip events. With the current vblank helper infrastructure this can
	 * be achieved by holding a vblank reference while the page flip is
	 * pending, acquired through drm_crtc_vblank_get() and released with
	 * drm_crtc_vblank_put(). Drivers are free to implement their own vblank
	 * counter and timestamp tracking though, e.g. if they have accurate
	 * timestamp registers in hardware.
	 *
	 * NOTE:
	 *
	 * Drivers are not allowed to shut down any display pipe successfully
	 * enabled through an atomic commit on their own. Doing so can result in
	 * compositors crashing if a page flip is suddenly rejected because the
	 * pipe is off.
	 *
	 * RETURNS:
	 *
	 * 0 on success or one of the below negative error codes:
	 *
	 *  - -EBUSY, if an asynchronous updated is requested and there is
	 *    an earlier updated pending. Drivers are allowed to support a queue
	 *    of outstanding updates, but currently no driver supports that.
	 *    Note that drivers must wait for preceding updates to complete if a
	 *    synchronous update is requested, they are not allowed to fail the
	 *    commit in that case.
	 *
	 *  - -ENOMEM, if the driver failed to allocate memory. Specifically
	 *    this can happen when trying to pin framebuffers, which must only
	 *    be done when committing the state.
	 *
	 *  - -ENOSPC, as a refinement of the more generic -ENOMEM to indicate
	 *    that the driver has run out of vram, iommu space or similar GPU
	 *    address space needed for framebuffer.
	 *
	 *  - -EIO, if the hardware completely died.
	 *
	 *  - -EINTR, -EAGAIN or -ERESTARTSYS, if the IOCTL should be restarted.
	 *    This can either be due to a pending signal, or because the driver
	 *    needs to completely bail out to recover from an exceptional
	 *    situation like a GPU hang. From a userspace point of view all errors are
	 *    treated equally.
	 *
	 * This list is exhaustive. Specifically this hook is not allowed to
	 * return -EINVAL (any invalid requests should be caught in
	 * @atomic_check) or -EDEADLK (this function must not acquire
	 * additional modeset locks).
	 */
	int (*atomic_commit)(struct drm_device *dev,
			     struct drm_atomic_state *state,
			     bool async);

	/**
	 * @atomic_state_alloc:
	 *
	 * This optional hook can be used by drivers that want to subclass struct
	 * &drm_atomic_state to be able to track their own driver-private global
	 * state easily. If this hook is implemented, drivers must also
	 * implement @atomic_state_clear and @atomic_state_free.
	 *
	 * RETURNS:
	 *
	 * A new &drm_atomic_state on success or NULL on failure.
	 */
	struct drm_atomic_state *(*atomic_state_alloc)(struct drm_device *dev);

	/**
	 * @atomic_state_clear:
	 *
	 * This hook must clear any driver private state duplicated into the
	 * passed-in &drm_atomic_state. This hook is called when the caller
	 * encountered a &drm_modeset_lock deadlock and needs to drop all
	 * already acquired locks as part of the deadlock avoidance dance
	 * implemented in drm_modeset_lock_backoff().
	 *
	 * Any duplicated state must be invalidated since a concurrent atomic
	 * update might change it, and the drm atomic interfaces always apply
	 * updates as relative changes to the current state.
	 *
	 * Drivers that implement this must call drm_atomic_state_default_clear()
	 * to clear common state.
	 */
	void (*atomic_state_clear)(struct drm_atomic_state *state);

	/**
	 * @atomic_state_free:
	 *
	 * This hook needs driver private resources and the &drm_atomic_state
	 * itself. Note that the core first calls drm_atomic_state_clear() to
	 * avoid code duplicate between the clear and free hooks.
	 *
	 * Drivers that implement this must call drm_atomic_state_default_free()
	 * to release common resources.
	 */
	void (*atomic_state_free)(struct drm_atomic_state *state);
};

/**
 * struct drm_mode_config - Mode configuration control structure
 * @mutex: mutex protecting KMS related lists and structures
 * @connection_mutex: ww mutex protecting connector state and routing
 * @acquire_ctx: global implicit acquire context used by atomic drivers for
 * 	legacy IOCTLs
 * @idr_mutex: mutex for KMS ID allocation and management
 * @crtc_idr: main KMS ID tracking object
 * @fb_lock: mutex to protect fb state and lists
 * @num_fb: number of fbs available
 * @fb_list: list of framebuffers available
 * @num_connector: number of connectors on this device
 * @connector_list: list of connector objects
 * @num_encoder: number of encoders on this device
 * @encoder_list: list of encoder objects
 * @num_overlay_plane: number of overlay planes on this device
 * @num_total_plane: number of universal (i.e. with primary/curso) planes on this device
 * @plane_list: list of plane objects
 * @num_crtc: number of CRTCs on this device
 * @crtc_list: list of CRTC objects
 * @property_list: list of property objects
 * @min_width: minimum pixel width on this device
 * @min_height: minimum pixel height on this device
 * @max_width: maximum pixel width on this device
 * @max_height: maximum pixel height on this device
 * @funcs: core driver provided mode setting functions
 * @fb_base: base address of the framebuffer
 * @poll_enabled: track polling support for this device
 * @poll_running: track polling status for this device
 * @output_poll_work: delayed work for polling in process context
 * @property_blob_list: list of all the blob property objects
 * @blob_lock: mutex for blob property allocation and management
 * @*_property: core property tracking
 * @degamma_lut_property: LUT used to convert the framebuffer's colors to linear
 *	gamma
 * @degamma_lut_size_property: size of the degamma LUT as supported by the
 *	driver (read-only)
 * @ctm_property: Matrix used to convert colors after the lookup in the
 *	degamma LUT
 * @gamma_lut_property: LUT used to convert the colors, after the CSC matrix, to
 *	the gamma space of the connected screen (read-only)
 * @gamma_lut_size_property: size of the gamma LUT as supported by the driver
 * @preferred_depth: preferred RBG pixel depth, used by fb helpers
 * @prefer_shadow: hint to userspace to prefer shadow-fb rendering
 * @async_page_flip: does this device support async flips on the primary plane?
 * @cursor_width: hint to userspace for max cursor width
 * @cursor_height: hint to userspace for max cursor height
 *
 * Core mode resource tracking structure.  All CRTC, encoders, and connectors
 * enumerated by the driver are added here, as are global properties.  Some
 * global restrictions are also here, e.g. dimension restrictions.
 */
struct drm_mode_config {
	struct mutex mutex; /* protects configuration (mode lists etc.) */
	struct drm_modeset_lock connection_mutex; /* protects connector->encoder and encoder->crtc links */
	struct drm_modeset_acquire_ctx *acquire_ctx; /* for legacy _lock_all() / _unlock_all() */
	struct mutex idr_mutex; /* for IDR management */
	struct idr crtc_idr; /* use this idr for all IDs, fb, crtc, connector, modes - just makes life easier */
	struct idr tile_idr; /* use this idr for all IDs, fb, crtc, connector, modes - just makes life easier */
	/* this is limited to one for now */

	struct mutex fb_lock; /* proctects global and per-file fb lists */
	int num_fb;
	struct list_head fb_list;

	int num_connector;
	struct ida connector_ida;
	struct list_head connector_list;
	int num_encoder;
	struct list_head encoder_list;

	/*
	 * Track # of overlay planes separately from # of total planes.  By
	 * default we only advertise overlay planes to userspace; if userspace
	 * sets the "universal plane" capability bit, we'll go ahead and
	 * expose all planes.
	 */
	int num_overlay_plane;
	int num_total_plane;
	struct list_head plane_list;

	int num_crtc;
	struct list_head crtc_list;

	struct list_head property_list;

	int min_width, min_height;
	int max_width, max_height;
	const struct drm_mode_config_funcs *funcs;
	resource_size_t fb_base;

	/* output poll support */
	bool poll_enabled;
	bool poll_running;
	bool delayed_event;
	struct delayed_work output_poll_work;

	struct mutex blob_lock;

	/* pointers to standard properties */
	struct list_head property_blob_list;
	struct drm_property *edid_property;
	struct drm_property *dpms_property;
	struct drm_property *path_property;
	struct drm_property *tile_property;
	struct drm_property *plane_type_property;
	struct drm_property *rotation_property;
	struct drm_property *prop_src_x;
	struct drm_property *prop_src_y;
	struct drm_property *prop_src_w;
	struct drm_property *prop_src_h;
	struct drm_property *prop_crtc_x;
	struct drm_property *prop_crtc_y;
	struct drm_property *prop_crtc_w;
	struct drm_property *prop_crtc_h;
	struct drm_property *prop_fb_id;
	struct drm_property *prop_crtc_id;
	struct drm_property *prop_active;
	struct drm_property *prop_mode_id;

	/* DVI-I properties */
	struct drm_property *dvi_i_subconnector_property;
	struct drm_property *dvi_i_select_subconnector_property;

	/* TV properties */
	struct drm_property *tv_subconnector_property;
	struct drm_property *tv_select_subconnector_property;
	struct drm_property *tv_mode_property;
	struct drm_property *tv_left_margin_property;
	struct drm_property *tv_right_margin_property;
	struct drm_property *tv_top_margin_property;
	struct drm_property *tv_bottom_margin_property;
	struct drm_property *tv_brightness_property;
	struct drm_property *tv_contrast_property;
	struct drm_property *tv_flicker_reduction_property;
	struct drm_property *tv_overscan_property;
	struct drm_property *tv_saturation_property;
	struct drm_property *tv_hue_property;

	/* Optional properties */
	struct drm_property *scaling_mode_property;
	struct drm_property *aspect_ratio_property;
	struct drm_property *dirty_info_property;

	/* Optional color correction properties */
	struct drm_property *degamma_lut_property;
	struct drm_property *degamma_lut_size_property;
	struct drm_property *ctm_property;
	struct drm_property *gamma_lut_property;
	struct drm_property *gamma_lut_size_property;

	/* properties for virtual machine layout */
	struct drm_property *suggested_x_property;
	struct drm_property *suggested_y_property;

	/* dumb ioctl parameters */
	uint32_t preferred_depth, prefer_shadow;

	/* whether async page flip is supported or not */
	bool async_page_flip;

	/* whether the driver supports fb modifiers */
	bool allow_fb_modifiers;

	/* cursor size */
	uint32_t cursor_width, cursor_height;
};

/**
 * drm_for_each_plane_mask - iterate over planes specified by bitmask
 * @plane: the loop cursor
 * @dev: the DRM device
 * @plane_mask: bitmask of plane indices
 *
 * Iterate over all planes specified by bitmask.
 */
#define drm_for_each_plane_mask(plane, dev, plane_mask) \
	list_for_each_entry((plane), &(dev)->mode_config.plane_list, head) \
		for_each_if ((plane_mask) & (1 << drm_plane_index(plane)))

/**
 * drm_for_each_encoder_mask - iterate over encoders specified by bitmask
 * @encoder: the loop cursor
 * @dev: the DRM device
 * @encoder_mask: bitmask of encoder indices
 *
 * Iterate over all encoders specified by bitmask.
 */
#define drm_for_each_encoder_mask(encoder, dev, encoder_mask) \
	list_for_each_entry((encoder), &(dev)->mode_config.encoder_list, head) \
		for_each_if ((encoder_mask) & (1 << drm_encoder_index(encoder)))

#define obj_to_crtc(x) container_of(x, struct drm_crtc, base)
#define obj_to_connector(x) container_of(x, struct drm_connector, base)
#define obj_to_encoder(x) container_of(x, struct drm_encoder, base)
#define obj_to_mode(x) container_of(x, struct drm_display_mode, base)
#define obj_to_fb(x) container_of(x, struct drm_framebuffer, base)
#define obj_to_property(x) container_of(x, struct drm_property, base)
#define obj_to_blob(x) container_of(x, struct drm_property_blob, base)
#define obj_to_plane(x) container_of(x, struct drm_plane, base)

struct drm_prop_enum_list {
	int type;
	char *name;
};

extern __printf(6, 7)
int drm_crtc_init_with_planes(struct drm_device *dev,
			      struct drm_crtc *crtc,
			      struct drm_plane *primary,
			      struct drm_plane *cursor,
			      const struct drm_crtc_funcs *funcs,
			      const char *name, ...);
extern void drm_crtc_cleanup(struct drm_crtc *crtc);
extern unsigned int drm_crtc_index(struct drm_crtc *crtc);

/**
 * drm_crtc_mask - find the mask of a registered CRTC
 * @crtc: CRTC to find mask for
 *
 * Given a registered CRTC, return the mask bit of that CRTC for an
 * encoder's possible_crtcs field.
 */
static inline uint32_t drm_crtc_mask(struct drm_crtc *crtc)
{
	return 1 << drm_crtc_index(crtc);
}

extern void drm_connector_ida_init(void);
extern void drm_connector_ida_destroy(void);
extern int drm_connector_init(struct drm_device *dev,
			      struct drm_connector *connector,
			      const struct drm_connector_funcs *funcs,
			      int connector_type);
int drm_connector_register(struct drm_connector *connector);
void drm_connector_unregister(struct drm_connector *connector);

extern void drm_connector_cleanup(struct drm_connector *connector);
static inline unsigned drm_connector_index(struct drm_connector *connector)
{
	return connector->connector_id;
}

/* helper to unplug all connectors from sysfs for device */
extern void drm_connector_unplug_all(struct drm_device *dev);

extern int drm_bridge_add(struct drm_bridge *bridge);
extern void drm_bridge_remove(struct drm_bridge *bridge);
extern struct drm_bridge *of_drm_find_bridge(struct device_node *np);
extern int drm_bridge_attach(struct drm_device *dev, struct drm_bridge *bridge);

bool drm_bridge_mode_fixup(struct drm_bridge *bridge,
			const struct drm_display_mode *mode,
			struct drm_display_mode *adjusted_mode);
void drm_bridge_disable(struct drm_bridge *bridge);
void drm_bridge_post_disable(struct drm_bridge *bridge);
void drm_bridge_mode_set(struct drm_bridge *bridge,
			struct drm_display_mode *mode,
			struct drm_display_mode *adjusted_mode);
void drm_bridge_pre_enable(struct drm_bridge *bridge);
void drm_bridge_enable(struct drm_bridge *bridge);

extern __printf(5, 6)
int drm_encoder_init(struct drm_device *dev,
		     struct drm_encoder *encoder,
		     const struct drm_encoder_funcs *funcs,
		     int encoder_type, const char *name, ...);
extern unsigned int drm_encoder_index(struct drm_encoder *encoder);

/**
 * drm_encoder_crtc_ok - can a given crtc drive a given encoder?
 * @encoder: encoder to test
 * @crtc: crtc to test
 *
 * Return false if @encoder can't be driven by @crtc, true otherwise.
 */
static inline bool drm_encoder_crtc_ok(struct drm_encoder *encoder,
				       struct drm_crtc *crtc)
{
	return !!(encoder->possible_crtcs & drm_crtc_mask(crtc));
}

extern __printf(8, 9)
int drm_universal_plane_init(struct drm_device *dev,
			     struct drm_plane *plane,
			     unsigned long possible_crtcs,
			     const struct drm_plane_funcs *funcs,
			     const uint32_t *formats,
			     unsigned int format_count,
			     enum drm_plane_type type,
			     const char *name, ...);
extern int drm_plane_init(struct drm_device *dev,
			  struct drm_plane *plane,
			  unsigned long possible_crtcs,
			  const struct drm_plane_funcs *funcs,
			  const uint32_t *formats, unsigned int format_count,
			  bool is_primary);
extern void drm_plane_cleanup(struct drm_plane *plane);
extern unsigned int drm_plane_index(struct drm_plane *plane);
extern struct drm_plane * drm_plane_from_index(struct drm_device *dev, int idx);
extern void drm_plane_force_disable(struct drm_plane *plane);
extern int drm_plane_check_pixel_format(const struct drm_plane *plane,
					u32 format);
extern void drm_crtc_get_hv_timing(const struct drm_display_mode *mode,
				   int *hdisplay, int *vdisplay);
extern int drm_crtc_check_viewport(const struct drm_crtc *crtc,
				   int x, int y,
				   const struct drm_display_mode *mode,
				   const struct drm_framebuffer *fb);

extern void drm_encoder_cleanup(struct drm_encoder *encoder);

extern const char *drm_get_connector_status_name(enum drm_connector_status status);
extern const char *drm_get_subpixel_order_name(enum subpixel_order order);
extern const char *drm_get_dpms_name(int val);
extern const char *drm_get_dvi_i_subconnector_name(int val);
extern const char *drm_get_dvi_i_select_name(int val);
extern const char *drm_get_tv_subconnector_name(int val);
extern const char *drm_get_tv_select_name(int val);
extern void drm_fb_release(struct drm_file *file_priv);
extern void drm_property_destroy_user_blobs(struct drm_device *dev,
                                            struct drm_file *file_priv);
extern bool drm_probe_ddc(struct i2c_adapter *adapter);
extern struct edid *drm_get_edid(struct drm_connector *connector,
				 struct i2c_adapter *adapter);
extern struct edid *drm_get_edid_switcheroo(struct drm_connector *connector,
					    struct i2c_adapter *adapter);
extern struct edid *drm_edid_duplicate(const struct edid *edid);
extern int drm_add_edid_modes(struct drm_connector *connector, struct edid *edid);
extern void drm_mode_config_init(struct drm_device *dev);
extern void drm_mode_config_reset(struct drm_device *dev);
extern void drm_mode_config_cleanup(struct drm_device *dev);

extern int drm_mode_connector_set_path_property(struct drm_connector *connector,
						const char *path);
int drm_mode_connector_set_tile_property(struct drm_connector *connector);
extern int drm_mode_connector_update_edid_property(struct drm_connector *connector,
						   const struct edid *edid);

extern int drm_display_info_set_bus_formats(struct drm_display_info *info,
					    const u32 *formats,
					    unsigned int num_formats);

static inline bool drm_property_type_is(struct drm_property *property,
		uint32_t type)
{
	/* instanceof for props.. handles extended type vs original types: */
	if (property->flags & DRM_MODE_PROP_EXTENDED_TYPE)
		return (property->flags & DRM_MODE_PROP_EXTENDED_TYPE) == type;
	return property->flags & type;
}

static inline bool drm_property_type_valid(struct drm_property *property)
{
	if (property->flags & DRM_MODE_PROP_EXTENDED_TYPE)
		return !(property->flags & DRM_MODE_PROP_LEGACY_TYPE);
	return !!(property->flags & DRM_MODE_PROP_LEGACY_TYPE);
}

extern int drm_object_property_set_value(struct drm_mode_object *obj,
					 struct drm_property *property,
					 uint64_t val);
extern int drm_object_property_get_value(struct drm_mode_object *obj,
					 struct drm_property *property,
					 uint64_t *value);
extern int drm_framebuffer_init(struct drm_device *dev,
				struct drm_framebuffer *fb,
				const struct drm_framebuffer_funcs *funcs);
extern struct drm_framebuffer *drm_framebuffer_lookup(struct drm_device *dev,
						      uint32_t id);
extern void drm_framebuffer_unreference(struct drm_framebuffer *fb);
extern void drm_framebuffer_reference(struct drm_framebuffer *fb);
extern void drm_framebuffer_remove(struct drm_framebuffer *fb);
extern void drm_framebuffer_cleanup(struct drm_framebuffer *fb);
extern void drm_framebuffer_unregister_private(struct drm_framebuffer *fb);

extern void drm_object_attach_property(struct drm_mode_object *obj,
				       struct drm_property *property,
				       uint64_t init_val);
extern struct drm_property *drm_property_create(struct drm_device *dev, int flags,
						const char *name, int num_values);
extern struct drm_property *drm_property_create_enum(struct drm_device *dev, int flags,
					 const char *name,
					 const struct drm_prop_enum_list *props,
					 int num_values);
struct drm_property *drm_property_create_bitmask(struct drm_device *dev,
					 int flags, const char *name,
					 const struct drm_prop_enum_list *props,
					 int num_props,
					 uint64_t supported_bits);
struct drm_property *drm_property_create_range(struct drm_device *dev, int flags,
					 const char *name,
					 uint64_t min, uint64_t max);
struct drm_property *drm_property_create_signed_range(struct drm_device *dev,
					 int flags, const char *name,
					 int64_t min, int64_t max);
struct drm_property *drm_property_create_object(struct drm_device *dev,
					 int flags, const char *name, uint32_t type);
struct drm_property *drm_property_create_bool(struct drm_device *dev, int flags,
					 const char *name);
struct drm_property_blob *drm_property_create_blob(struct drm_device *dev,
                                                   size_t length,
                                                   const void *data);
struct drm_property_blob *drm_property_lookup_blob(struct drm_device *dev,
                                                   uint32_t id);
struct drm_property_blob *drm_property_reference_blob(struct drm_property_blob *blob);
void drm_property_unreference_blob(struct drm_property_blob *blob);
extern void drm_property_destroy(struct drm_device *dev, struct drm_property *property);
extern int drm_property_add_enum(struct drm_property *property, int index,
				 uint64_t value, const char *name);
extern int drm_mode_create_dvi_i_properties(struct drm_device *dev);
extern int drm_mode_create_tv_properties(struct drm_device *dev,
					 unsigned int num_modes,
					 const char * const modes[]);
extern int drm_mode_create_scaling_mode_property(struct drm_device *dev);
extern int drm_mode_create_aspect_ratio_property(struct drm_device *dev);
extern int drm_mode_create_dirty_info_property(struct drm_device *dev);
extern int drm_mode_create_suggested_offset_properties(struct drm_device *dev);
extern bool drm_property_change_valid_get(struct drm_property *property,
					 uint64_t value, struct drm_mode_object **ref);
extern void drm_property_change_valid_put(struct drm_property *property,
		struct drm_mode_object *ref);

extern int drm_mode_connector_attach_encoder(struct drm_connector *connector,
					     struct drm_encoder *encoder);
extern int drm_mode_crtc_set_gamma_size(struct drm_crtc *crtc,
					 int gamma_size);
extern struct drm_mode_object *drm_mode_object_find(struct drm_device *dev,
		uint32_t id, uint32_t type);

/* IOCTLs */
extern int drm_mode_getresources(struct drm_device *dev,
				 void *data, struct drm_file *file_priv);
extern int drm_mode_getplane_res(struct drm_device *dev, void *data,
				   struct drm_file *file_priv);
extern int drm_mode_getcrtc(struct drm_device *dev,
			    void *data, struct drm_file *file_priv);
extern int drm_mode_getconnector(struct drm_device *dev,
			      void *data, struct drm_file *file_priv);
extern int drm_mode_set_config_internal(struct drm_mode_set *set);
extern int drm_mode_setcrtc(struct drm_device *dev,
			    void *data, struct drm_file *file_priv);
extern int drm_mode_getplane(struct drm_device *dev,
			       void *data, struct drm_file *file_priv);
extern int drm_mode_setplane(struct drm_device *dev,
			       void *data, struct drm_file *file_priv);
extern int drm_mode_cursor_ioctl(struct drm_device *dev,
				void *data, struct drm_file *file_priv);
extern int drm_mode_cursor2_ioctl(struct drm_device *dev,
				void *data, struct drm_file *file_priv);
extern int drm_mode_addfb(struct drm_device *dev,
			  void *data, struct drm_file *file_priv);
extern int drm_mode_addfb2(struct drm_device *dev,
			   void *data, struct drm_file *file_priv);
extern uint32_t drm_mode_legacy_fb_format(uint32_t bpp, uint32_t depth);
extern int drm_mode_rmfb(struct drm_device *dev,
			 void *data, struct drm_file *file_priv);
extern int drm_mode_getfb(struct drm_device *dev,
			  void *data, struct drm_file *file_priv);
extern int drm_mode_dirtyfb_ioctl(struct drm_device *dev,
				  void *data, struct drm_file *file_priv);

extern int drm_mode_getproperty_ioctl(struct drm_device *dev,
				      void *data, struct drm_file *file_priv);
extern int drm_mode_getblob_ioctl(struct drm_device *dev,
				  void *data, struct drm_file *file_priv);
extern int drm_mode_createblob_ioctl(struct drm_device *dev,
				     void *data, struct drm_file *file_priv);
extern int drm_mode_destroyblob_ioctl(struct drm_device *dev,
				      void *data, struct drm_file *file_priv);
extern int drm_mode_connector_property_set_ioctl(struct drm_device *dev,
					      void *data, struct drm_file *file_priv);
extern int drm_mode_getencoder(struct drm_device *dev,
			       void *data, struct drm_file *file_priv);
extern int drm_mode_gamma_get_ioctl(struct drm_device *dev,
				    void *data, struct drm_file *file_priv);
extern int drm_mode_gamma_set_ioctl(struct drm_device *dev,
				    void *data, struct drm_file *file_priv);
extern u8 drm_match_cea_mode(const struct drm_display_mode *to_match);
extern enum hdmi_picture_aspect drm_get_cea_aspect_ratio(const u8 video_code);
extern bool drm_detect_hdmi_monitor(struct edid *edid);
extern bool drm_detect_monitor_audio(struct edid *edid);
extern bool drm_rgb_quant_range_selectable(struct edid *edid);
extern int drm_mode_page_flip_ioctl(struct drm_device *dev,
				    void *data, struct drm_file *file_priv);
extern int drm_add_modes_noedid(struct drm_connector *connector,
				int hdisplay, int vdisplay);
extern void drm_set_preferred_mode(struct drm_connector *connector,
				   int hpref, int vpref);

extern int drm_edid_header_is_valid(const u8 *raw_edid);
extern bool drm_edid_block_valid(u8 *raw_edid, int block, bool print_bad_edid,
				 bool *edid_corrupt);
extern bool drm_edid_is_valid(struct edid *edid);

extern struct drm_tile_group *drm_mode_create_tile_group(struct drm_device *dev,
							 char topology[8]);
extern struct drm_tile_group *drm_mode_get_tile_group(struct drm_device *dev,
					       char topology[8]);
extern void drm_mode_put_tile_group(struct drm_device *dev,
				   struct drm_tile_group *tg);
struct drm_display_mode *drm_mode_find_dmt(struct drm_device *dev,
					   int hsize, int vsize, int fresh,
					   bool rb);

extern int drm_mode_create_dumb_ioctl(struct drm_device *dev,
				      void *data, struct drm_file *file_priv);
extern int drm_mode_mmap_dumb_ioctl(struct drm_device *dev,
				    void *data, struct drm_file *file_priv);
extern int drm_mode_destroy_dumb_ioctl(struct drm_device *dev,
				      void *data, struct drm_file *file_priv);
extern int drm_mode_obj_get_properties_ioctl(struct drm_device *dev, void *data,
					     struct drm_file *file_priv);
extern int drm_mode_obj_set_property_ioctl(struct drm_device *dev, void *data,
					   struct drm_file *file_priv);
extern int drm_mode_plane_set_obj_prop(struct drm_plane *plane,
				       struct drm_property *property,
				       uint64_t value);
extern int drm_mode_atomic_ioctl(struct drm_device *dev,
				 void *data, struct drm_file *file_priv);

extern void drm_fb_get_bpp_depth(uint32_t format, unsigned int *depth,
				 int *bpp);
extern int drm_format_num_planes(uint32_t format);
extern int drm_format_plane_cpp(uint32_t format, int plane);
extern int drm_format_horz_chroma_subsampling(uint32_t format);
extern int drm_format_vert_chroma_subsampling(uint32_t format);
extern int drm_format_plane_width(int width, uint32_t format, int plane);
extern int drm_format_plane_height(int height, uint32_t format, int plane);
extern const char *drm_get_format_name(uint32_t format);
extern struct drm_property *drm_mode_create_rotation_property(struct drm_device *dev,
							      unsigned int supported_rotations);
extern unsigned int drm_rotation_simplify(unsigned int rotation,
					  unsigned int supported_rotations);

/* Helpers */

static inline struct drm_plane *drm_plane_find(struct drm_device *dev,
		uint32_t id)
{
	struct drm_mode_object *mo;
	mo = drm_mode_object_find(dev, id, DRM_MODE_OBJECT_PLANE);
	return mo ? obj_to_plane(mo) : NULL;
}

static inline struct drm_crtc *drm_crtc_find(struct drm_device *dev,
	uint32_t id)
{
	struct drm_mode_object *mo;
	mo = drm_mode_object_find(dev, id, DRM_MODE_OBJECT_CRTC);
	return mo ? obj_to_crtc(mo) : NULL;
}

static inline struct drm_encoder *drm_encoder_find(struct drm_device *dev,
	uint32_t id)
{
	struct drm_mode_object *mo;
	mo = drm_mode_object_find(dev, id, DRM_MODE_OBJECT_ENCODER);
	return mo ? obj_to_encoder(mo) : NULL;
}

static inline struct drm_connector *drm_connector_find(struct drm_device *dev,
		uint32_t id)
{
	struct drm_mode_object *mo;
	mo = drm_mode_object_find(dev, id, DRM_MODE_OBJECT_CONNECTOR);
	return mo ? obj_to_connector(mo) : NULL;
}

static inline struct drm_property *drm_property_find(struct drm_device *dev,
		uint32_t id)
{
	struct drm_mode_object *mo;
	mo = drm_mode_object_find(dev, id, DRM_MODE_OBJECT_PROPERTY);
	return mo ? obj_to_property(mo) : NULL;
}

/*
 * Extract a degamma/gamma LUT value provided by user and round it to the
 * precision supported by the hardware.
 */
static inline uint32_t drm_color_lut_extract(uint32_t user_input,
					     uint32_t bit_precision)
{
	uint32_t val = user_input + (1 << (16 - bit_precision - 1));
	uint32_t max = 0xffff >> (16 - bit_precision);

	val >>= 16 - bit_precision;

	return clamp_val(val, 0, max);
}

/* Plane list iterator for legacy (overlay only) planes. */
#define drm_for_each_legacy_plane(plane, dev) \
	list_for_each_entry(plane, &(dev)->mode_config.plane_list, head) \
		for_each_if (plane->type == DRM_PLANE_TYPE_OVERLAY)

#define drm_for_each_plane(plane, dev) \
	list_for_each_entry(plane, &(dev)->mode_config.plane_list, head)

#define drm_for_each_crtc(crtc, dev) \
	list_for_each_entry(crtc, &(dev)->mode_config.crtc_list, head)

static inline void
assert_drm_connector_list_read_locked(struct drm_mode_config *mode_config)
{
	/*
	 * The connector hotadd/remove code currently grabs both locks when
	 * updating lists. Hence readers need only hold either of them to be
	 * safe and the check amounts to
	 *
	 * WARN_ON(not_holding(A) && not_holding(B)).
	 */
	WARN_ON(!mutex_is_locked(&mode_config->mutex) &&
		!drm_modeset_is_locked(&mode_config->connection_mutex));
}

#define drm_for_each_connector(connector, dev) \
	for (assert_drm_connector_list_read_locked(&(dev)->mode_config),	\
	     connector = list_first_entry(&(dev)->mode_config.connector_list,	\
					  struct drm_connector, head);		\
	     &connector->head != (&(dev)->mode_config.connector_list);		\
	     connector = list_next_entry(connector, head))

#define drm_for_each_encoder(encoder, dev) \
	list_for_each_entry(encoder, &(dev)->mode_config.encoder_list, head)

#define drm_for_each_fb(fb, dev) \
	for (WARN_ON(!mutex_is_locked(&(dev)->mode_config.fb_lock)),		\
	     fb = list_first_entry(&(dev)->mode_config.fb_list,	\
					  struct drm_framebuffer, head);	\
	     &fb->head != (&(dev)->mode_config.fb_list);			\
	     fb = list_next_entry(fb, head))

#endif /* __DRM_CRTC_H__ */
