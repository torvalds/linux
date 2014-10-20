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
#define DRM_MODE_OBJECT_BRIDGE 0xbdbdbdbd
#define DRM_MODE_OBJECT_ANY 0

struct drm_mode_object {
	uint32_t id;
	uint32_t type;
	struct drm_object_properties *properties;
};

#define DRM_OBJECT_MAX_PROPERTY 24
struct drm_object_properties {
	int count;
	uint32_t ids[DRM_OBJECT_MAX_PROPERTY];
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
#define DRM_ROTATE_0	0
#define DRM_ROTATE_90	1
#define DRM_ROTATE_180	2
#define DRM_ROTATE_270	3
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
	unsigned int width;
	unsigned int height;
	/* depth can be 15 or 16 */
	unsigned int depth;
	int bits_per_pixel;
	int flags;
	uint32_t pixel_format; /* fourcc format */
	struct list_head filp_head;
	/* if you are using the helper */
	void *helper_private;
};

struct drm_property_blob {
	struct drm_mode_object base;
	struct list_head head;
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
 * @enable: whether the CRTC should be enabled, gates all other state
 * @mode_changed: for use by helpers and drivers when computing state updates
 * @plane_mask: bitmask of (1 << drm_plane_index(plane)) of attached planes
 * @last_vblank_count: for helpers and drivers to capture the vblank of the
 * 	update to ensure framebuffer cleanup isn't done too early
 * @planes_changed: for use by helpers and drivers when computing state updates
 * @adjusted_mode: for use by helpers and drivers to compute adjusted mode timings
 * @mode: current mode timings
 * @event: optional pointer to a DRM event to signal upon completion of the
 * 	state update
 * @state: backpointer to global drm_atomic_state
 */
struct drm_crtc_state {
	bool enable;

	/* computed state bits used by helpers and drivers */
	bool planes_changed : 1;
	bool mode_changed : 1;

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

	struct drm_pending_vblank_event *event;

	struct drm_atomic_state *state;
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
 * @invert_dimensions: for purposes of error checking crtc vs fb sizes,
 *    invert the width/height of the crtc.  This is used if the driver
 *    is performing 90 or 270 degree rotated scanout
 * @x: x position on screen
 * @y: y position on screen
 * @funcs: CRTC control functions
 * @gamma_size: size of gamma ramp
 * @gamma_store: gamma ramp values
 * @framedur_ns: precise frame timing
 * @linedur_ns: precise line timing
 * @pixeldur_ns: precise pixel timing
 * @helper_private: mid-layer private data
 * @properties: property tracking for this CRTC
 * @state: current atomic state for this CRTC
 * @acquire_ctx: per-CRTC implicit acquire context used by atomic drivers for
 * 	legacy ioctls
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

	bool invert_dimensions;

	int x, y;
	const struct drm_crtc_funcs *funcs;

	/* CRTC gamma size for reporting to userspace */
	uint32_t gamma_size;
	uint16_t *gamma_store;

	/* Constants needed for precise vblank and swap timestamping. */
	int framedur_ns, linedur_ns, pixeldur_ns;

	/* if you are using the helper */
	void *helper_private;

	struct drm_object_properties properties;

	struct drm_crtc_state *state;

	/*
	 * For legacy crtc ioctls so that atomic drivers can get at the locking
	 * acquire context.
	 */
	struct drm_modeset_acquire_ctx *acquire_ctx;
};

/**
 * struct drm_connector_state - mutable connector state
 * @crtc: CRTC to connect connector to, NULL if disabled
 * @best_encoder: can be used by helpers and drivers to select the encoder
 * @state: backpointer to global drm_atomic_state
 */
struct drm_connector_state {
	struct drm_crtc *crtc;  /* do not write directly, use drm_atomic_set_crtc_for_connector() */

	struct drm_encoder *best_encoder;

	struct drm_atomic_state *state;
};

/**
 * struct drm_connector_funcs - control connectors on a given device
 * @dpms: set power state (see drm_crtc_funcs above)
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
 *
 * Each CRTC may have one or more connectors attached to it.  The functions
 * below allow the core DRM code to control connectors, enumerate available modes,
 * etc.
 */
struct drm_connector_funcs {
	void (*dpms)(struct drm_connector *connector, int mode);
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
	void *helper_private;
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

	uint8_t polled; /* DRM_CONNECTOR_POLL_* */

	/* requested DPMS state */
	int dpms;

	void *helper_private;

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
	struct drm_crtc *crtc;   /* do not write directly, use drm_atomic_set_crtc_for_plane() */
	struct drm_framebuffer *fb;  /* do not write directly, use drm_atomic_set_fb_for_plane() */
	struct fence *fence;

	/* Signed dest location allows it to be partially off screen */
	int32_t crtc_x, crtc_y;
	uint32_t crtc_w, crtc_h;

	/* Source values are 16.16 fixed point */
	uint32_t src_x, src_y;
	uint32_t src_h, src_w;

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

	struct drm_modeset_lock mutex;

	struct drm_mode_object base;

	uint32_t possible_crtcs;
	uint32_t *format_types;
	uint32_t format_count;

	struct drm_crtc *crtc;
	struct drm_framebuffer *fb;

	struct drm_framebuffer *old_fb;

	const struct drm_plane_funcs *funcs;

	struct drm_object_properties properties;

	enum drm_plane_type type;

	void *helper_private;

	struct drm_plane_state *state;
};

/**
 * struct drm_bridge_funcs - drm_bridge control functions
 * @mode_fixup: Try to fixup (or reject entirely) proposed mode for this bridge
 * @disable: Called right before encoder prepare, disables the bridge
 * @post_disable: Called right after encoder prepare, for lockstepped disable
 * @mode_set: Set this mode to the bridge
 * @pre_enable: Called right before encoder commit, for lockstepped commit
 * @enable: Called right after encoder commit, enables the bridge
 * @destroy: make object go away
 */
struct drm_bridge_funcs {
	bool (*mode_fixup)(struct drm_bridge *bridge,
			   const struct drm_display_mode *mode,
			   struct drm_display_mode *adjusted_mode);
	void (*disable)(struct drm_bridge *bridge);
	void (*post_disable)(struct drm_bridge *bridge);
	void (*mode_set)(struct drm_bridge *bridge,
			 struct drm_display_mode *mode,
			 struct drm_display_mode *adjusted_mode);
	void (*pre_enable)(struct drm_bridge *bridge);
	void (*enable)(struct drm_bridge *bridge);
	void (*destroy)(struct drm_bridge *bridge);
};

/**
 * struct drm_bridge - central DRM bridge control structure
 * @dev: DRM device this bridge belongs to
 * @head: list management
 * @base: base mode object
 * @funcs: control functions
 * @driver_private: pointer to the bridge driver's internal context
 */
struct drm_bridge {
	struct drm_device *dev;
	struct list_head head;

	struct drm_mode_object base;

	const struct drm_bridge_funcs *funcs;
	void *driver_private;
};

/**
 * struct struct drm_atomic_state - the global state object for atomic updates
 * @dev: parent DRM device
 * @flags: state flags like async update
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
	uint32_t flags;
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
 * @fb_create: create a new framebuffer object
 * @output_poll_changed: function to handle output configuration changes
 * @atomic_check: check whether a give atomic state update is possible
 * @atomic_commit: commit an atomic state update previously verified with
 * 	atomic_check()
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
};

/**
 * struct drm_mode_group - group of mode setting resources for potential sub-grouping
 * @num_crtcs: CRTC count
 * @num_encoders: encoder count
 * @num_connectors: connector count
 * @num_bridges: bridge count
 * @id_list: list of KMS object IDs in this group
 *
 * Currently this simply tracks the global mode setting state.  But in the
 * future it could allow groups of objects to be set aside into independent
 * control groups for use by different user level processes (e.g. two X servers
 * running simultaneously on different heads, each with their own mode
 * configuration and freedom of mode setting).
 */
struct drm_mode_group {
	uint32_t num_crtcs;
	uint32_t num_encoders;
	uint32_t num_connectors;
	uint32_t num_bridges;

	/* list of object IDs for this group */
	uint32_t *id_list;
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
 * @num_bridge: number of bridges on this device
 * @bridge_list: list of bridge objects
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
 * @*_property: core property tracking
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
	int num_bridge;
	struct list_head bridge_list;
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
	struct delayed_work output_poll_work;

	/* pointers to standard properties */
	struct list_head property_blob_list;
	struct drm_property *edid_property;
	struct drm_property *dpms_property;
	struct drm_property *path_property;
	struct drm_property *plane_type_property;
	struct drm_property *rotation_property;

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

	/* properties for virtual machine layout */
	struct drm_property *suggested_x_property;
	struct drm_property *suggested_y_property;

	/* dumb ioctl parameters */
	uint32_t preferred_depth, prefer_shadow;

	/* whether async page flip is supported or not */
	bool async_page_flip;

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

extern int drm_crtc_init_with_planes(struct drm_device *dev,
				     struct drm_crtc *crtc,
				     struct drm_plane *primary,
				     struct drm_plane *cursor,
				     const struct drm_crtc_funcs *funcs);
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
/* helper to unplug all connectors from sysfs for device */
extern void drm_connector_unplug_all(struct drm_device *dev);

extern int drm_bridge_init(struct drm_device *dev, struct drm_bridge *bridge,
			   const struct drm_bridge_funcs *funcs);
extern void drm_bridge_cleanup(struct drm_bridge *bridge);

extern int drm_encoder_init(struct drm_device *dev,
			    struct drm_encoder *encoder,
			    const struct drm_encoder_funcs *funcs,
			    int encoder_type);

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

extern int drm_universal_plane_init(struct drm_device *dev,
				    struct drm_plane *plane,
				    unsigned long possible_crtcs,
				    const struct drm_plane_funcs *funcs,
				    const uint32_t *formats,
				    uint32_t format_count,
				    enum drm_plane_type type);
extern int drm_plane_init(struct drm_device *dev,
			  struct drm_plane *plane,
			  unsigned long possible_crtcs,
			  const struct drm_plane_funcs *funcs,
			  const uint32_t *formats, uint32_t format_count,
			  bool is_primary);
extern void drm_plane_cleanup(struct drm_plane *plane);
extern unsigned int drm_plane_index(struct drm_plane *plane);
extern void drm_plane_force_disable(struct drm_plane *plane);
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
extern int drm_mode_group_init_legacy_group(struct drm_device *dev, struct drm_mode_group *group);
extern void drm_mode_group_destroy(struct drm_mode_group *group);
extern void drm_reinit_primary_mode_group(struct drm_device *dev);
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
extern int drm_mode_connector_update_edid_property(struct drm_connector *connector,
						   const struct edid *edid);

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
extern void drm_property_destroy(struct drm_device *dev, struct drm_property *property);
extern int drm_property_add_enum(struct drm_property *property, int index,
				 uint64_t value, const char *name);
extern int drm_mode_create_dvi_i_properties(struct drm_device *dev);
extern int drm_mode_create_tv_properties(struct drm_device *dev,
					 unsigned int num_modes,
					 char *modes[]);
extern int drm_mode_create_scaling_mode_property(struct drm_device *dev);
extern int drm_mode_create_aspect_ratio_property(struct drm_device *dev);
extern int drm_mode_create_dirty_info_property(struct drm_device *dev);
extern int drm_mode_create_suggested_offset_properties(struct drm_device *dev);

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
extern bool drm_edid_block_valid(u8 *raw_edid, int block, bool print_bad_edid);
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

extern void drm_fb_get_bpp_depth(uint32_t format, unsigned int *depth,
				 int *bpp);
extern int drm_format_num_planes(uint32_t format);
extern int drm_format_plane_cpp(uint32_t format, int plane);
extern int drm_format_horz_chroma_subsampling(uint32_t format);
extern int drm_format_vert_chroma_subsampling(uint32_t format);
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

static inline struct drm_property_blob *
drm_property_blob_find(struct drm_device *dev, uint32_t id)
{
	struct drm_mode_object *mo;
	mo = drm_mode_object_find(dev, id, DRM_MODE_OBJECT_BLOB);
	return mo ? obj_to_blob(mo) : NULL;
}

/* Plane list iterator for legacy (overlay only) planes. */
#define drm_for_each_legacy_plane(plane, planelist) \
	list_for_each_entry(plane, planelist, head) \
		if (plane->type == DRM_PLANE_TYPE_OVERLAY)

#endif /* __DRM_CRTC_H__ */
