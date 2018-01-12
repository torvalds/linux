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

/* rotation property bits */
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

/**
 * struct drm_scrambling: sink's scrambling support.
 */
struct drm_scrambling {
	/**
	 * @supported: scrambling supported for rates > 340 Mhz.
	 */
	bool supported;
	/**
	 * @low_rates: scrambling supported for rates <= 340 Mhz.
	 */
	bool low_rates;
};

/*
 * struct drm_scdc - Information about scdc capabilities of a HDMI 2.0 sink
 *
 * Provides SCDC register support and capabilities related information on a
 * HDMI 2.0 sink. In case of a HDMI 1.4 sink, all parameter must be 0.
 */
struct drm_scdc {
	/**
	 * @supported: status control & data channel present.
	 */
	bool supported;
	/**
	 * @read_request: sink is capable of generating scdc read request.
	 */
	bool read_request;
	/**
	 * @scrambling: sink's scrambling capabilities
	 */
	struct drm_scrambling scrambling;
};


/**
 * struct drm_hdmi_info - runtime information about the connected HDMI sink
 *
 * Describes if a given display supports advanced HDMI 2.0 features.
 * This information is available in CEA-861-F extension blocks (like HF-VSDB).
 */
struct drm_hdmi_info {
	struct drm_scdc scdc;

	/**
	 * @y420_vdb_modes: bitmap of modes which can support ycbcr420
	 * output only (not normal RGB/YCBCR444/422 outputs). There are total
	 * 107 VICs defined by CEA-861-F spec, so the size is 128 bits to map
	 * upto 128 VICs;
	 */
	unsigned long y420_vdb_modes[BITS_TO_LONGS(128)];

	/**
	 * @y420_cmdb_modes: bitmap of modes which can support ycbcr420
	 * output also, along with normal HDMI outputs. There are total 107
	 * VICs defined by CEA-861-F spec, so the size is 128 bits to map upto
	 * 128 VICs;
	 */
	unsigned long y420_cmdb_modes[BITS_TO_LONGS(128)];

	/** @y420_cmdb_map: bitmap of SVD index, to extraxt vcb modes */
	u64 y420_cmdb_map;

	/** @y420_dc_modes: bitmap of deep color support index */
	u8 y420_dc_modes;

	/* Colorimerty info from EDID */
	u32 colorimetry;

	/* HDR metdata */
	struct hdr_static_metadata hdr_panel_metadata;
};

#define DRM_COLOR_FORMAT_RGB444		(1<<0)
#define DRM_COLOR_FORMAT_YCRCB444	(1<<1)
#define DRM_COLOR_FORMAT_YCRCB422	(1<<2)
#define DRM_COLOR_FORMAT_YCRCB420	(1<<3)
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

	/**
	 * @max_tmds_clock: Maximum TMDS clock rate supported by the
	 * sink in kHz. 0 means undefined.
	 */
	int max_tmds_clock;

	/**
	 * @dvi_dual: Dual-link DVI sink?
	 */
	bool dvi_dual;

	/* Mask of supported hdmi deep color modes */
	u8 edid_hdmi_dc_modes;

	u8 cea_rev;

	/**
	 * @hdmi: advance features of a HDMI sink.
	 */
	struct drm_hdmi_info hdmi;
};

/* data corresponds to displayid vend/prod/serial */
struct drm_tile_group {
	struct kref refcount;
	struct drm_device *dev;
	int id;
	u8 group_data[8];
};

struct drm_framebuffer_funcs {
	/* note: use drm_framebuffer_remove() */
	void (*destroy)(struct drm_framebuffer *framebuffer);
	int (*create_handle)(struct drm_framebuffer *fb,
			     struct drm_file *file_priv,
			     unsigned int *handle);
	/*
	 * Optional callback for the dirty fb ioctl.
	 *
	 * Userspace can notify the driver via this callback
	 * that a area of the framebuffer has changed and should
	 * be flushed to the display hardware.
	 *
	 * See documentation in drm_mode.h for the struct
	 * drm_mode_fb_dirty_cmd for more information as all
	 * the semantics and arguments have a one to one mapping
	 * on this function.
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
	struct drm_crtc_commit *commit;
};

/**
 * struct drm_crtc_funcs - control CRTCs for a given device
 * @save: save CRTC state
 * @restore: restore CRTC state
 * @reset: reset CRTC after state has been invalidated (e.g. resume)
 * @cursor_set: setup the cursor
 * @cursor_set2: setup the cursor with hotspot, superseeds @cursor_set if set
 * @cursor_move: move the cursor
 * @gamma_set: specify color ramp for CRTC
 * @destroy: deinit and free object
 * @set_property: called when a property is changed
 * @set_config: apply a new CRTC configuration
 * @page_flip: initiate a page flip
 * @atomic_duplicate_state: duplicate the atomic state for this CRTC
 * @atomic_destroy_state: destroy an atomic state for this CRTC
 * @atomic_set_property: set a property on an atomic state for this CRTC
 *    (do not call directly, use drm_atomic_crtc_set_property())
 * @atomic_get_property: get a property on an atomic state for this CRTC
 *    (do not call directly, use drm_atomic_crtc_get_property())
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
	/* Save CRTC state */
	void (*save)(struct drm_crtc *crtc); /* suspend? */
	/* Restore CRTC state */
	void (*restore)(struct drm_crtc *crtc); /* resume? */
	/* Reset CRTC state */
	void (*reset)(struct drm_crtc *crtc);

	/* cursor controls */
	int (*cursor_set)(struct drm_crtc *crtc, struct drm_file *file_priv,
			  uint32_t handle, uint32_t width, uint32_t height);
	int (*cursor_set2)(struct drm_crtc *crtc, struct drm_file *file_priv,
			   uint32_t handle, uint32_t width, uint32_t height,
			   int32_t hot_x, int32_t hot_y);
	int (*cursor_move)(struct drm_crtc *crtc, int x, int y);

	/* Set gamma on the CRTC */
	void (*gamma_set)(struct drm_crtc *crtc, u16 *r, u16 *g, u16 *b,
			  uint32_t start, uint32_t size);
	/* Object destroy routine */
	void (*destroy)(struct drm_crtc *crtc);

	int (*set_config)(struct drm_mode_set *set);

	/*
	 * Flip to the given framebuffer.  This implements the page
	 * flip ioctl described in drm_mode.h, specifically, the
	 * implementation must return immediately and block all
	 * rendering to the current fb until the flip has completed.
	 * If userspace set the event flag in the ioctl, the event
	 * argument will point to an event to send back when the flip
	 * completes, otherwise it will be NULL.
	 */
	int (*page_flip)(struct drm_crtc *crtc,
			 struct drm_framebuffer *fb,
			 struct drm_pending_vblank_event *event,
			 uint32_t flags);

	int (*set_property)(struct drm_crtc *crtc,
			    struct drm_property *property, uint64_t val);

	/* atomic update handling */
	struct drm_crtc_state *(*atomic_duplicate_state)(struct drm_crtc *crtc);
	void (*atomic_destroy_state)(struct drm_crtc *crtc,
				     struct drm_crtc_state *state);
	int (*atomic_set_property)(struct drm_crtc *crtc,
				   struct drm_crtc_state *state,
				   struct drm_property *property,
				   uint64_t val);
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
	 * late_unregister(). It is called from drm_dev_unregister(),
	 * early in the driver unload sequence to disable userspace access
	 * before data structures are torndown.
	 */
	void (*early_unregister)(struct drm_crtc *crtc);
};

#if defined(CONFIG_ROCKCHIP_DRM_DEBUG)
struct vop_dump_info {
	int win_id;
	int area_id;
	unsigned int pitches;
	unsigned int height;
	u32 pixel_format;
	bool AFBC_flag;
	bool yuv_format;
	unsigned long offset;
	unsigned long num_pages;
	struct page **pages;
};

struct vop_dump_list {
	struct list_head entry;
	struct vop_dump_info dump_info;
};

enum vop_dump_status {
	DUMP_DISABLE = 0,
	DUMP_KEEP
};
#endif

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
 *
 * Each CRTC may have one or more connectors associated with it.  This structure
 * allows the CRTC to be controlled.
 */
struct drm_crtc {
	struct drm_device *dev;
	struct device_node *port;
	struct list_head head;

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
	const void *helper_private;

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

#if defined(CONFIG_ROCKCHIP_DRM_DEBUG)
	/**
	 * @vop_dump_status the status of vop dump control
	 * @vop_dump_list_head the list head of vop dump list
	 * @vop_dump_list_init_flag init once
	 * @vop_dump_times control the dump times
	 * @frme_count the frame of dump buf
	 */
	enum vop_dump_status vop_dump_status;
	struct list_head vop_dump_list_head;
	bool vop_dump_list_init_flag;
	int vop_dump_times;
	int frame_count;
#endif
};

/**
 * struct drm_tv_connector_state - TV connector related states
 * @subconnector: selected subconnector
 * @margins: left/right/top/bottom margins
 * @mode: TV mode
 * @brightness: brightness in percent
 * @contrast: contrast in percent
 * @flicker_reduction: flicker reduction in percent
 * @overscan: overscan in percent
 * @saturation: saturation in percent
 * @hue: hue in percent
 */
struct drm_tv_connector_state {
	enum drm_mode_subconnector subconnector;
	struct {
		unsigned int left;
		unsigned int right;
		unsigned int top;
		unsigned int bottom;
	} margins;
	unsigned int mode;
	unsigned int brightness;
	unsigned int contrast;
	unsigned int flicker_reduction;
	unsigned int overscan;
	unsigned int saturation;
	unsigned int hue;
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

	/**
	 * @content_protection: Connector property to request content
	 * protection. This is most commonly used for HDCP.
	 */
	unsigned int content_protection;

	struct drm_tv_connector_state tv;

	/**
	 * @metadata_blob_ptr:
	 * DRM blob property for HDR metadata
	 */
	struct drm_property_blob *hdr_source_metadata_blob_ptr;
	bool hdr_metadata_changed : 1;
	uint64_t blob_id;
};

/**
 * struct drm_connector_funcs - control connectors on a given device
 * @dpms: set power state
 * @save: save connector state
 * @restore: restore connector state
 * @reset: reset connector after state has been invalidated (e.g. resume)
 * @detect: is this connector active?
 * @fill_modes: fill mode list for this connector
 * @set_property: property for this connector may need an update
 * @destroy: make object go away
 * @force: notify the driver that the connector is forced on
 * @atomic_duplicate_state: duplicate the atomic state for this connector
 * @atomic_destroy_state: destroy an atomic state for this connector
 * @atomic_set_property: set a property on an atomic state for this connector
 *    (do not call directly, use drm_atomic_connector_set_property())
 * @atomic_get_property: get a property on an atomic state for this connector
 *    (do not call directly, use drm_atomic_connector_get_property())
 *
 * Each CRTC may have one or more connectors attached to it.  The functions
 * below allow the core DRM code to control connectors, enumerate available modes,
 * etc.
 */
struct drm_connector_funcs {
	int (*dpms)(struct drm_connector *connector, int mode);
	void (*save)(struct drm_connector *connector);
	void (*restore)(struct drm_connector *connector);
	void (*reset)(struct drm_connector *connector);

	/* Check to see if anything is attached to the connector.
	 * @force is set to false whilst polling, true when checking the
	 * connector due to user request. @force can be used by the driver
	 * to avoid expensive, destructive operations during automated
	 * probing.
	 */
	enum drm_connector_status (*detect)(struct drm_connector *connector,
					    bool force);
	int (*fill_modes)(struct drm_connector *connector, uint32_t max_width, uint32_t max_height);
	int (*set_property)(struct drm_connector *connector, struct drm_property *property,
			     uint64_t val);

	/**
	 * @late_register:
	 *
	 * This optional hook can be used to register additional userspace
	 * interfaces attached to the connector, light backlight control, i2c,
	 * DP aux or similar interfaces. It is called late in the driver load
	 * sequence from drm_connector_register() when registering all the
	 * core drm connector interfaces. Everything added from this callback
	 * should be unregistered in the early_unregister callback.
	 *
	 * Returns:
	 *
	 * 0 on success, or a negative error code on failure.
	 */
	int (*late_register)(struct drm_connector *connector);

	/**
	 * @early_unregister:
	 *
	 * This optional hook should be used to unregister the additional
	 * userspace interfaces attached to the connector from
	 * late_unregister(). It is called from drm_connector_unregister(),
	 * early in the driver unload sequence to disable userspace access
	 * before data structures are torndown.
	 */
	void (*early_unregister)(struct drm_connector *connector);

	void (*destroy)(struct drm_connector *connector);
	void (*force)(struct drm_connector *connector);

	/* atomic update handling */
	struct drm_connector_state *(*atomic_duplicate_state)(struct drm_connector *connector);
	void (*atomic_destroy_state)(struct drm_connector *connector,
				     struct drm_connector_state *state);
	int (*atomic_set_property)(struct drm_connector *connector,
				   struct drm_connector_state *state,
				   struct drm_property *property,
				   uint64_t val);
	int (*atomic_get_property)(struct drm_connector *connector,
				   const struct drm_connector_state *state,
				   struct drm_property *property,
				   uint64_t *val);
};

/**
 * struct drm_encoder_funcs - encoder controls
 * @reset: reset state (e.g. at init or resume time)
 * @destroy: cleanup and free associated data
 *
 * Encoders sit between CRTCs and connectors.
 */
struct drm_encoder_funcs {
	void (*reset)(struct drm_encoder *encoder);
	void (*destroy)(struct drm_encoder *encoder);

	/**
	 * @late_register:
	 *
	 * This optional hook can be used to register additional userspace
	 * interfaces attached to the encoder like debugfs interfaces.
	 * It is called late in the driver load sequence from drm_dev_register().
	 * Everything added from this callback should be unregistered in
	 * the early_unregister callback.
	 *
	 * Returns:
	 *
	 * 0 on success, or a negative error code on failure.
	 */
	int (*late_register)(struct drm_encoder *encoder);

	/**
	 * @early_unregister:
	 *
	 * This optional hook should be used to unregister the additional
	 * userspace interfaces attached to the encoder from
	 * late_unregister(). It is called from drm_dev_unregister(),
	 * early in the driver unload sequence to disable userspace access
	 * before data structures are torndown.
	 */
	void (*early_unregister)(struct drm_encoder *encoder);
};

#define DRM_CONNECTOR_MAX_ENCODER 3

/**
 * struct drm_encoder - central DRM encoder structure
 * @dev: parent DRM device
 * @port: OF node used by find encoder by node
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
	struct device_node *port;
	struct list_head head;

	struct drm_mode_object base;
	char *name;
	int encoder_type;
	uint32_t possible_crtcs;
	uint32_t possible_clones;
	bool loader_protect;

	struct drm_crtc *crtc;
	struct drm_bridge *bridge;
	const struct drm_encoder_funcs *funcs;
	const void *helper_private;
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
 * @port: OF node used by find connector by node.
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
 * @content_protection_property: Optional property to control content protection
 *
 * Each connector may be connected to one or more CRTCs, or may be clonable by
 * another connector if they can share a CRTC.  Each connector also has a specific
 * position in the broader display (referred to as a 'screen' though it could
 * span multiple monitors).
 */
struct drm_connector {
	struct drm_device *dev;
	struct device_node *port;
	struct device *kdev;
	struct device_attribute *attr;
	struct list_head head;

	struct drm_mode_object base;

	char *name;
	int connector_type;
	int connector_type_id;
	bool interlace_allowed;
	bool doublescan_allowed;
	bool stereo_allowed;

	/**
	 * @ycbcr_420_allowed : This bool indicates if this connector is
	 * capable of handling YCBCR 420 output. While parsing the EDID
	 * blocks, its very helpful to know, if the source is capable of
	 * handling YCBCR 420 outputs.
	 */
	bool ycbcr_420_allowed;

	struct list_head modes; /* list of modes on this connector */

	enum drm_connector_status status;

	/* these are modes added by probing with DDC or the BIOS */
	struct list_head probed_modes;

	struct drm_display_info display_info;
	const struct drm_connector_funcs *funcs;

	struct drm_property_blob *edid_blob_ptr;
	struct drm_object_properties properties;

	/**
	 * @content_protection_property: DRM ENUM property for content
	 * protection
	 */
	struct drm_property *content_protection_property;

	struct drm_property_blob *path_blob_ptr;

	struct drm_property_blob *tile_blob_ptr;

	struct drm_property_blob *hdr_panel_blob_ptr;

	uint8_t polled; /* DRM_CONNECTOR_POLL_* */

	/* requested DPMS state */
	int dpms;

	const void *helper_private;

	/* forced on connector */
	struct drm_cmdline_mode cmdline_mode;
	enum drm_connector_force force;
	bool override_edid;
	uint32_t encoder_ids[DRM_CONNECTOR_MAX_ENCODER];
	struct drm_encoder *encoder; /* currently active encoder */
	bool loader_protect;

	/* EDID bits */
	uint8_t eld[MAX_ELD_BYTES];
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
 * @update_plane: update the plane configuration
 * @disable_plane: shut down the plane
 * @destroy: clean up plane resources
 * @reset: reset plane after state has been invalidated (e.g. resume)
 * @set_property: called when a property is changed
 * @atomic_duplicate_state: duplicate the atomic state for this plane
 * @atomic_destroy_state: destroy an atomic state for this plane
 * @atomic_set_property: set a property on an atomic state for this plane
 *    (do not call directly, use drm_atomic_plane_set_property())
 * @atomic_get_property: get a property on an atomic state for this plane
 *    (do not call directly, use drm_atomic_plane_get_property())
 */
struct drm_plane_funcs {
	int (*update_plane)(struct drm_plane *plane,
			    struct drm_crtc *crtc, struct drm_framebuffer *fb,
			    int crtc_x, int crtc_y,
			    unsigned int crtc_w, unsigned int crtc_h,
			    uint32_t src_x, uint32_t src_y,
			    uint32_t src_w, uint32_t src_h);
	int (*disable_plane)(struct drm_plane *plane);
	void (*destroy)(struct drm_plane *plane);
	void (*reset)(struct drm_plane *plane);

	int (*set_property)(struct drm_plane *plane,
			    struct drm_property *property, uint64_t val);

	/* atomic update handling */
	struct drm_plane_state *(*atomic_duplicate_state)(struct drm_plane *plane);
	void (*atomic_destroy_state)(struct drm_plane *plane,
				     struct drm_plane_state *state);
	int (*atomic_set_property)(struct drm_plane *plane,
				   struct drm_plane_state *state,
				   struct drm_property *property,
				   uint64_t val);
	int (*atomic_get_property)(struct drm_plane *plane,
				   const struct drm_plane_state *state,
				   struct drm_property *property,
				   uint64_t *val);
	/**
	 * @late_register:
	 *
	 * This optional hook can be used to register additional userspace
	 * interfaces attached to the plane like debugfs interfaces.
	 * It is called late in the driver load sequence from drm_dev_register().
	 * Everything added from this callback should be unregistered in
	 * the early_unregister callback.
	 *
	 * Returns:
	 *
	 * 0 on success, or a negative error code on failure.
	 */
	int (*late_register)(struct drm_plane *plane);

	/**
	 * @early_unregister:
	 *
	 * This optional hook should be used to unregister the additional
	 * userspace interfaces attached to the plane from
	 * late_unregister(). It is called from drm_dev_unregister(),
	 * early in the driver unload sequence to disable userspace access
	 * before data structures are torndown.
	 */
	void (*early_unregister)(struct drm_plane *plane);
};

enum drm_plane_type {
	DRM_PLANE_TYPE_OVERLAY,
	DRM_PLANE_TYPE_PRIMARY,
	DRM_PLANE_TYPE_CURSOR,
};

/**
 * struct drm_plane - central DRM plane control structure
 * @dev: DRM device this plane belongs to
 * @parent: this plane share some resources with parent plane.
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
	struct drm_plane *parent;
	struct list_head head;

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

	const void *helper_private;

	struct drm_plane_state *state;
};

/**
 * struct drm_bridge_funcs - drm_bridge control functions
 * @attach: Called during drm_bridge_attach
 * @mode_fixup: Try to fixup (or reject entirely) proposed mode for this bridge
 * @disable: Called right before encoder prepare, disables the bridge
 * @post_disable: Called right after encoder prepare, for lockstepped disable
 * @mode_set: Set this mode to the bridge
 * @pre_enable: Called right before encoder commit, for lockstepped commit
 * @enable: Called right after encoder commit, enables the bridge
 */
struct drm_bridge_funcs {
	int (*attach)(struct drm_bridge *bridge);
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
	 *
	 * The disable callback is optional.
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
	 *
	 * The post_disable callback is optional.
	 */
	void (*post_disable)(struct drm_bridge *bridge);
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
	 *
	 * The pre_enable callback is optional.
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
	 *
	 * The enable callback is optional.
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
 * struct drm_crtc_commit - track modeset commits on a CRTC
 *
 * This structure is used to track pending modeset changes and atomic commit on
 * a per-CRTC basis. Since updating the list should never block this structure
 * is reference counted to allow waiters to safely wait on an event to complete,
 * without holding any locks.
 *
 * It has 3 different events in total to allow a fine-grained synchronization
 * between outstanding updates::
 *
 *	atomic commit thread			hardware
 *
 * 	write new state into hardware	---->	...
 * 	signal hw_done
 * 						switch to new state on next
 * 	...					v/hblank
 *
 *	wait for buffers to show up		...
 *
 *	...					send completion irq
 *						irq handler signals flip_done
 *	cleanup old buffers
 *
 * 	signal cleanup_done
 *
 * 	wait for flip_done		<----
 * 	clean up atomic state
 *
 * The important bit to know is that cleanup_done is the terminal event, but the
 * ordering between flip_done and hw_done is entirely up to the specific driver
 * and modeset state change.
 *
 * For an implementation of how to use this look at
 * drm_atomic_helper_setup_commit() from the atomic helper library.
 */
struct drm_crtc_commit {
	/**
	 * @crtc:
	 *
	 * DRM CRTC for this commit.
	 */
	struct drm_crtc *crtc;

	/**
	 * @ref:
	 *
	 * Reference count for this structure. Needed to allow blocking on
	 * completions without the risk of the completion disappearing
	 * meanwhile.
	 */
	struct kref ref;

	/**
	 * @flip_done:
	 *
	 * Will be signaled when the hardware has flipped to the new set of
	 * buffers. Signals at the same time as when the drm event for this
	 * commit is sent to userspace, or when an out-fence is singalled. Note
	 * that for most hardware, in most cases this happens after @hw_done is
	 * signalled.
	 */
	struct completion flip_done;

	/**
	 * @hw_done:
	 *
	 * Will be signalled when all hw register changes for this commit have
	 * been written out. Especially when disabling a pipe this can be much
	 * later than than @flip_done, since that can signal already when the
	 * screen goes black, whereas to fully shut down a pipe more register
	 * I/O is required.
	 *
	 * Note that this does not need to include separately reference-counted
	 * resources like backing storage buffer pinning, or runtime pm
	 * management.
	 */
	struct completion hw_done;

	/**
	 * @cleanup_done:
	 *
	 * Will be signalled after old buffers have been cleaned up by calling
	 * drm_atomic_helper_cleanup_planes(). Since this can only happen after
	 * a vblank wait completed it might be a bit later. This completion is
	 * useful to throttle updates and avoid hardware updates getting ahead
	 * of the buffer cleanup too much.
	 */
	struct completion cleanup_done;

	/**
	 * @commit_entry:
	 *
	 * Entry on the per-CRTC commit_list. Protected by crtc->commit_lock.
	 */
	struct list_head commit_entry;

	/**
	 * @event:
	 *
	 * &drm_pending_vblank_event pointer to clean up private events.
	 */
	struct drm_pending_vblank_event *event;
};

/**
 * struct drm_atomic_state - the global state object for atomic updates
 * @dev: parent DRM device
 * @allow_modeset: allow full modeset
 * @legacy_cursor_update: hint to enforce legacy cursor ioctl semantics
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
	struct drm_plane **planes;
	struct drm_plane_state **plane_states;
	struct drm_crtc **crtcs;
	struct drm_crtc_commit **crtc_commits;
	struct drm_crtc_state **crtc_states;
	int num_connector;
	struct drm_connector **connectors;
	struct drm_connector_state **connector_states;

	struct drm_modeset_acquire_ctx *acquire_ctx;

	/**
	 * @commit_work:
	 *
	 * Work item which can be used by the driver or helpers to execute the
	 * commit without blocking.
	 */
	struct work_struct commit_work;
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
 * @fb_create: create a new framebuffer object
 * @output_poll_changed: function to handle output configuration changes
 * @atomic_check: check whether a given atomic state update is possible
 * @atomic_commit: commit an atomic state update previously verified with
 * 	atomic_check()
 * @atomic_state_alloc: allocate a new atomic state
 * @atomic_state_clear: clear the atomic state
 * @atomic_state_free: free the atomic state
 *
 * Some global (i.e. not per-CRTC, connector, etc) mode setting functions that
 * involve drivers.
 */
struct drm_mode_config_funcs {
	struct drm_framebuffer *(*fb_create)(struct drm_device *dev,
					     struct drm_file *file_priv,
					     struct drm_mode_fb_cmd2 *mode_cmd);
	void (*output_poll_changed)(struct drm_device *dev);

	int (*atomic_check)(struct drm_device *dev,
			    struct drm_atomic_state *a);
	int (*atomic_commit)(struct drm_device *dev,
			     struct drm_atomic_state *a,
			     bool async);
	struct drm_atomic_state *(*atomic_state_alloc)(struct drm_device *dev);
	void (*atomic_state_clear)(struct drm_atomic_state *state);
	void (*atomic_state_free)(struct drm_atomic_state *state);
};

/**
 * struct drm_mode_config - Mode configuration control structure
 * @mutex: mutex protecting KMS related lists and structures
 * @connection_mutex: ww mutex protecting connector state and routing
 * @acquire_ctx: global implicit acquire context used by atomic drivers for
 * 	legacy ioctls
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
	int num_share_plane;
	int num_share_overlay_plane;
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

	/* pointers to share properties */
	struct drm_property *prop_share_id;
	struct drm_property *prop_share_flags;

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
	struct drm_property *content_protection_property;

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

	/**
	 * hdr_metadata_property: Connector property containing hdr metatda
	 * This will be provided by userspace compositors based on HDR content
	 */
	struct drm_property *hdr_source_metadata_property;
	struct drm_property *hdr_panel_metadata_property;
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
		if ((plane_mask) & (1 << drm_plane_index(plane)))


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
extern unsigned int drm_connector_index(struct drm_connector *connector);

/* helpers to {un}register all connectors from sysfs for device */
extern int drm_connector_register_all(struct drm_device *dev);
extern void drm_connector_unregister_all(struct drm_device *dev);

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
extern int drm_share_plane_init(struct drm_device *dev, struct drm_plane *plane,
				struct drm_plane *parent,
				unsigned long possible_crtcs,
				const struct drm_plane_funcs *funcs,
				const uint32_t *formats,
				unsigned int format_count,
				enum drm_plane_type type);
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
extern const char *drm_get_content_protection_name(int val);
extern const char *drm_get_dvi_i_subconnector_name(int val);
extern const char *drm_get_dvi_i_select_name(int val);
extern const char *drm_get_tv_subconnector_name(int val);
extern const char *drm_get_tv_select_name(int val);
extern const char *drm_get_content_protection_name(int val);
extern const char *drm_get_connector_name(int val);
extern void drm_fb_release(struct drm_file *file_priv);
extern void drm_property_destroy_user_blobs(struct drm_device *dev,
                                            struct drm_file *file_priv);
extern bool drm_probe_ddc(struct i2c_adapter *adapter);
extern struct edid *drm_get_edid(struct drm_connector *connector,
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
int drm_mode_connector_update_hdr_property(struct drm_connector *connector,
					   const struct hdr_static_metadata *data);

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
extern int drm_connector_attach_content_protection_property(
			struct drm_connector *connector);
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
extern int drm_format_plane_bpp(uint32_t format, int plane);
extern int drm_format_plane_cpp(uint32_t format, int plane);
extern int drm_format_horz_chroma_subsampling(uint32_t format);
extern int drm_format_vert_chroma_subsampling(uint32_t format);
extern const char *drm_get_format_name(uint32_t format);
extern struct drm_property *drm_mode_create_rotation_property(struct drm_device *dev,
							      unsigned int supported_rotations);
extern unsigned int drm_rotation_simplify(unsigned int rotation,
					  unsigned int supported_rotations);
struct drm_display_mode *
drm_display_mode_from_vic_index(struct drm_connector *connector,
				const u8 *video_db, u8 video_len,
				u8 video_index);

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
		if (plane->type == DRM_PLANE_TYPE_OVERLAY)

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
