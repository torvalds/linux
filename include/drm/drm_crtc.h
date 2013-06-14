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
#include <drm/drm_mode.h>

#include <drm/drm_fourcc.h>

struct drm_device;
struct drm_mode_set;
struct drm_framebuffer;
struct drm_object_properties;
struct drm_file;
struct drm_clip_rect;

#define DRM_MODE_OBJECT_CRTC 0xcccccccc
#define DRM_MODE_OBJECT_CONNECTOR 0xc0c0c0c0
#define DRM_MODE_OBJECT_ENCODER 0xe0e0e0e0
#define DRM_MODE_OBJECT_MODE 0xdededede
#define DRM_MODE_OBJECT_PROPERTY 0xb0b0b0b0
#define DRM_MODE_OBJECT_FB 0xfbfbfbfb
#define DRM_MODE_OBJECT_BLOB 0xbbbbbbbb
#define DRM_MODE_OBJECT_PLANE 0xeeeeeeee

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

/*
 * Note on terminology:  here, for brevity and convenience, we refer to connector
 * control chips as 'CRTCs'.  They can control any type of connector, VGA, LVDS,
 * DVI, etc.  And 'screen' refers to the whole of the visible display, which
 * may span multiple monitors (and therefore multiple CRTC and connector
 * structures).
 */

enum drm_mode_status {
    MODE_OK	= 0,	/* Mode OK */
    MODE_HSYNC,		/* hsync out of range */
    MODE_VSYNC,		/* vsync out of range */
    MODE_H_ILLEGAL,	/* mode has illegal horizontal timings */
    MODE_V_ILLEGAL,	/* mode has illegal horizontal timings */
    MODE_BAD_WIDTH,	/* requires an unsupported linepitch */
    MODE_NOMODE,	/* no mode with a matching name */
    MODE_NO_INTERLACE,	/* interlaced mode not supported */
    MODE_NO_DBLESCAN,	/* doublescan mode not supported */
    MODE_NO_VSCAN,	/* multiscan mode not supported */
    MODE_MEM,		/* insufficient video memory */
    MODE_VIRTUAL_X,	/* mode width too large for specified virtual size */
    MODE_VIRTUAL_Y,	/* mode height too large for specified virtual size */
    MODE_MEM_VIRT,	/* insufficient video memory given virtual size */
    MODE_NOCLOCK,	/* no fixed clock available */
    MODE_CLOCK_HIGH,	/* clock required is too high */
    MODE_CLOCK_LOW,	/* clock required is too low */
    MODE_CLOCK_RANGE,	/* clock/mode isn't in a ClockRange */
    MODE_BAD_HVALUE,	/* horizontal timing was out of range */
    MODE_BAD_VVALUE,	/* vertical timing was out of range */
    MODE_BAD_VSCAN,	/* VScan value out of range */
    MODE_HSYNC_NARROW,	/* horizontal sync too narrow */
    MODE_HSYNC_WIDE,	/* horizontal sync too wide */
    MODE_HBLANK_NARROW,	/* horizontal blanking too narrow */
    MODE_HBLANK_WIDE,	/* horizontal blanking too wide */
    MODE_VSYNC_NARROW,	/* vertical sync too narrow */
    MODE_VSYNC_WIDE,	/* vertical sync too wide */
    MODE_VBLANK_NARROW,	/* vertical blanking too narrow */
    MODE_VBLANK_WIDE,	/* vertical blanking too wide */
    MODE_PANEL,         /* exceeds panel dimensions */
    MODE_INTERLACE_WIDTH, /* width too large for interlaced mode */
    MODE_ONE_WIDTH,     /* only one width is supported */
    MODE_ONE_HEIGHT,    /* only one height is supported */
    MODE_ONE_SIZE,      /* only one resolution is supported */
    MODE_NO_REDUCED,    /* monitor doesn't accept reduced blanking */
    MODE_UNVERIFIED = -3, /* mode needs to reverified */
    MODE_BAD = -2,	/* unspecified reason */
    MODE_ERROR	= -1	/* error condition */
};

#define DRM_MODE_TYPE_CLOCK_CRTC_C (DRM_MODE_TYPE_CLOCK_C | \
				    DRM_MODE_TYPE_CRTC_C)

#define DRM_MODE(nm, t, c, hd, hss, hse, ht, hsk, vd, vss, vse, vt, vs, f) \
	.name = nm, .status = 0, .type = (t), .clock = (c), \
	.hdisplay = (hd), .hsync_start = (hss), .hsync_end = (hse), \
	.htotal = (ht), .hskew = (hsk), .vdisplay = (vd), \
	.vsync_start = (vss), .vsync_end = (vse), .vtotal = (vt), \
	.vscan = (vs), .flags = (f), \
	.base.type = DRM_MODE_OBJECT_MODE

#define CRTC_INTERLACE_HALVE_V 0x1 /* halve V values for interlacing */

struct drm_display_mode {
	/* Header */
	struct list_head head;
	struct drm_mode_object base;

	char name[DRM_DISPLAY_MODE_LEN];

	enum drm_mode_status status;
	unsigned int type;

	/* Proposed mode values */
	int clock;		/* in kHz */
	int hdisplay;
	int hsync_start;
	int hsync_end;
	int htotal;
	int hskew;
	int vdisplay;
	int vsync_start;
	int vsync_end;
	int vtotal;
	int vscan;
	unsigned int flags;

	/* Addressable image size (may be 0 for projectors, etc.) */
	int width_mm;
	int height_mm;

	/* Actual mode we give to hw */
	int clock_index;
	int synth_clock;
	int crtc_hdisplay;
	int crtc_hblank_start;
	int crtc_hblank_end;
	int crtc_hsync_start;
	int crtc_hsync_end;
	int crtc_htotal;
	int crtc_hskew;
	int crtc_vdisplay;
	int crtc_vblank_start;
	int crtc_vblank_end;
	int crtc_vsync_start;
	int crtc_vsync_end;
	int crtc_vtotal;

	/* Driver private mode info */
	int private_size;
	int *private;
	int private_flags;

	int vrefresh;		/* in Hz */
	int hsync;		/* in kHz */
};

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

	u8 cea_rev;
};

struct drm_framebuffer_funcs {
	/* note: use drm_framebuffer_remove() */
	void (*destroy)(struct drm_framebuffer *framebuffer);
	int (*create_handle)(struct drm_framebuffer *fb,
			     struct drm_file *file_priv,
			     unsigned int *handle);
	/**
	 * Optinal callback for the dirty fb ioctl.
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
	unsigned int length;
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

	struct list_head enum_blob_list;
};

struct drm_crtc;
struct drm_connector;
struct drm_encoder;
struct drm_pending_vblank_event;
struct drm_plane;

/**
 * drm_crtc_funcs - control CRTCs for a given device
 * @save: save CRTC state
 * @restore: restore CRTC state
 * @reset: reset CRTC after state has been invalidated (e.g. resume)
 * @cursor_set: setup the cursor
 * @cursor_move: move the cursor
 * @gamma_set: specify color ramp for CRTC
 * @destroy: deinit and free object
 * @set_property: called when a property is changed
 * @set_config: apply a new CRTC configuration
 * @page_flip: initiate a page flip
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
			 struct drm_pending_vblank_event *event);

	int (*set_property)(struct drm_crtc *crtc,
			    struct drm_property *property, uint64_t val);
};

/**
 * drm_crtc - central CRTC control structure
 * @dev: parent DRM device
 * @head: list management
 * @base: base KMS object for ID tracking etc.
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
 * @framedur_ns: precise line timing
 * @pixeldur_ns: precise pixel timing
 * @helper_private: mid-layer private data
 * @properties: property tracking for this CRTC
 *
 * Each CRTC may have one or more connectors associated with it.  This structure
 * allows the CRTC to be controlled.
 */
struct drm_crtc {
	struct drm_device *dev;
	struct list_head head;

	/**
	 * crtc mutex
	 *
	 * This provides a read lock for the overall crtc state (mode, dpms
	 * state, ...) and a write lock for everything which can be update
	 * without a full modeset (fb, cursor data, ...)
	 */
	struct mutex mutex;

	struct drm_mode_object base;

	/* framebuffer the connector is currently bound to */
	struct drm_framebuffer *fb;

	/* Temporary tracking of the old fb while a modeset is ongoing. Used
	 * by drm_mode_set_config_internal to implement correct refcounting. */
	struct drm_framebuffer *old_fb;

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
	s64 framedur_ns, linedur_ns, pixeldur_ns;

	/* if you are using the helper */
	void *helper_private;

	struct drm_object_properties properties;
};


/**
 * drm_connector_funcs - control connectors on a given device
 * @dpms: set power state (see drm_crtc_funcs above)
 * @save: save connector state
 * @restore: restore connector state
 * @reset: reset connector after state has been invalidated (e.g. resume)
 * @detect: is this connector active?
 * @fill_modes: fill mode list for this connector
 * @set_property: property for this connector may need an update
 * @destroy: make object go away
 * @force: notify the driver that the connector is forced on
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
};

/**
 * drm_encoder_funcs - encoder controls
 * @reset: reset state (e.g. at init or resume time)
 * @destroy: cleanup and free associated data
 *
 * Encoders sit between CRTCs and connectors.
 */
struct drm_encoder_funcs {
	void (*reset)(struct drm_encoder *encoder);
	void (*destroy)(struct drm_encoder *encoder);
};

#define DRM_CONNECTOR_MAX_UMODES 16
#define DRM_CONNECTOR_LEN 32
#define DRM_CONNECTOR_MAX_ENCODER 3

/**
 * drm_encoder - central DRM encoder structure
 * @dev: parent DRM device
 * @head: list management
 * @base: base KMS object
 * @encoder_type: one of the %DRM_MODE_ENCODER_<foo> types in drm_mode.h
 * @possible_crtcs: bitmask of potential CRTC bindings
 * @possible_clones: bitmask of potential sibling encoders for cloning
 * @crtc: currently bound CRTC
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
	int encoder_type;
	uint32_t possible_crtcs;
	uint32_t possible_clones;

	struct drm_crtc *crtc;
	const struct drm_encoder_funcs *funcs;
	void *helper_private;
};

enum drm_connector_force {
	DRM_FORCE_UNSPECIFIED,
	DRM_FORCE_OFF,
	DRM_FORCE_ON,         /* force on analog part normally */
	DRM_FORCE_ON_DIGITAL, /* for DVI-I use digital connector */
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
 * drm_connector - central DRM connector control structure
 * @dev: parent DRM device
 * @kdev: kernel device for sysfs attributes
 * @attr: sysfs attributes
 * @head: list management
 * @base: base KMS object
 * @connector_type: one of the %DRM_MODE_CONNECTOR_<foo> types from drm_mode.h
 * @connector_type_id: index into connector type enum
 * @interlace_allowed: can this connector handle interlaced modes?
 * @doublescan_allowed: can this connector handle doublescan?
 * @modes: modes available on this connector (from fill_modes() + user)
 * @status: one of the drm_connector_status enums (connected, not, or unknown)
 * @probed_modes: list of modes derived directly from the display
 * @display_info: information about attached display (e.g. from EDID)
 * @funcs: connector control functions
 * @edid_blob_ptr: DRM property containing EDID if present
 * @properties: property tracking for this connector
 * @polled: a %DRM_CONNECTOR_POLL_<foo> value for core driven polling
 * @dpms: current dpms state
 * @helper_private: mid-layer private data
 * @force: a %DRM_FORCE_<foo> state for forced mode sets
 * @encoder_ids: valid encoders for this connector
 * @encoder: encoder driving this connector, if any
 * @eld: EDID-like data, if present
 * @dvi_dual: dual link DVI, if found
 * @max_tmds_clock: max clock rate, if found
 * @latency_present: AV delay info from ELD, if found
 * @video_latency: video latency info from ELD, if found
 * @audio_latency: audio latency info from ELD, if found
 * @null_edid_counter: track sinks that give us all zeros for the EDID
 *
 * Each connector may be connected to one or more CRTCs, or may be clonable by
 * another connector if they can share a CRTC.  Each connector also has a specific
 * position in the broader display (referred to as a 'screen' though it could
 * span multiple monitors).
 */
struct drm_connector {
	struct drm_device *dev;
	struct device kdev;
	struct device_attribute *attr;
	struct list_head head;

	struct drm_mode_object base;

	int connector_type;
	int connector_type_id;
	bool interlace_allowed;
	bool doublescan_allowed;
	struct list_head modes; /* list of modes on this connector */

	enum drm_connector_status status;

	/* these are modes added by probing with DDC or the BIOS */
	struct list_head probed_modes;

	struct drm_display_info display_info;
	const struct drm_connector_funcs *funcs;

	struct drm_property_blob *edid_blob_ptr;
	struct drm_object_properties properties;

	uint8_t polled; /* DRM_CONNECTOR_POLL_* */

	/* requested DPMS state */
	int dpms;

	void *helper_private;

	/* forced on connector */
	enum drm_connector_force force;
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
};

/**
 * drm_plane_funcs - driver plane control functions
 * @update_plane: update the plane configuration
 * @disable_plane: shut down the plane
 * @destroy: clean up plane resources
 * @set_property: called when a property is changed
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

	int (*set_property)(struct drm_plane *plane,
			    struct drm_property *property, uint64_t val);
};

/**
 * drm_plane - central DRM plane control structure
 * @dev: DRM device this plane belongs to
 * @head: for list management
 * @base: base mode object
 * @possible_crtcs: pipes this plane can be bound to
 * @format_types: array of formats supported by this plane
 * @format_count: number of formats supported
 * @crtc: currently bound CRTC
 * @fb: currently bound fb
 * @funcs: helper functions
 * @properties: property tracking for this plane
 */
struct drm_plane {
	struct drm_device *dev;
	struct list_head head;

	struct drm_mode_object base;

	uint32_t possible_crtcs;
	uint32_t *format_types;
	uint32_t format_count;

	struct drm_crtc *crtc;
	struct drm_framebuffer *fb;

	const struct drm_plane_funcs *funcs;

	struct drm_object_properties properties;
};

/**
 * drm_mode_set - new values for a CRTC config change
 * @head: list management
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
 *
 * Some global (i.e. not per-CRTC, connector, etc) mode setting functions that
 * involve drivers.
 */
struct drm_mode_config_funcs {
	struct drm_framebuffer *(*fb_create)(struct drm_device *dev,
					     struct drm_file *file_priv,
					     struct drm_mode_fb_cmd2 *mode_cmd);
	void (*output_poll_changed)(struct drm_device *dev);
};

/**
 * drm_mode_group - group of mode setting resources for potential sub-grouping
 * @num_crtcs: CRTC count
 * @num_encoders: encoder count
 * @num_connectors: connector count
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

	/* list of object IDs for this group */
	uint32_t *id_list;
};

/**
 * drm_mode_config - Mode configuration control structure
 * @mutex: mutex protecting KMS related lists and structures
 * @idr_mutex: mutex for KMS ID allocation and management
 * @crtc_idr: main KMS ID tracking object
 * @num_fb: number of fbs available
 * @fb_list: list of framebuffers available
 * @num_connector: number of connectors on this device
 * @connector_list: list of connector objects
 * @num_encoder: number of encoders on this device
 * @encoder_list: list of encoder objects
 * @num_crtc: number of CRTCs on this device
 * @crtc_list: list of CRTC objects
 * @min_width: minimum pixel width on this device
 * @min_height: minimum pixel height on this device
 * @max_width: maximum pixel width on this device
 * @max_height: maximum pixel height on this device
 * @funcs: core driver provided mode setting functions
 * @fb_base: base address of the framebuffer
 * @poll_enabled: track polling status for this device
 * @output_poll_work: delayed work for polling in process context
 * @*_property: core property tracking
 *
 * Core mode resource tracking structure.  All CRTC, encoders, and connectors
 * enumerated by the driver are added here, as are global properties.  Some
 * global restrictions are also here, e.g. dimension restrictions.
 */
struct drm_mode_config {
	struct mutex mutex; /* protects configuration (mode lists etc.) */
	struct mutex idr_mutex; /* for IDR management */
	struct idr crtc_idr; /* use this idr for all IDs, fb, crtc, connector, modes - just makes life easier */
	/* this is limited to one for now */


	/**
	 * fb_lock - mutex to protect fb state
	 *
	 * Besides the global fb list his also protects the fbs list in the
	 * file_priv
	 */
	struct mutex fb_lock;
	int num_fb;
	struct list_head fb_list;

	int num_connector;
	struct list_head connector_list;
	int num_encoder;
	struct list_head encoder_list;
	int num_plane;
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
	struct drm_property *dithering_mode_property;
	struct drm_property *dirty_info_property;

	/* dumb ioctl parameters */
	uint32_t preferred_depth, prefer_shadow;
};

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

extern void drm_modeset_lock_all(struct drm_device *dev);
extern void drm_modeset_unlock_all(struct drm_device *dev);
extern void drm_warn_on_modeset_not_all_locked(struct drm_device *dev);

extern int drm_crtc_init(struct drm_device *dev,
			 struct drm_crtc *crtc,
			 const struct drm_crtc_funcs *funcs);
extern void drm_crtc_cleanup(struct drm_crtc *crtc);

extern int drm_connector_init(struct drm_device *dev,
			      struct drm_connector *connector,
			      const struct drm_connector_funcs *funcs,
			      int connector_type);

extern void drm_connector_cleanup(struct drm_connector *connector);
/* helper to unplug all connectors from sysfs for device */
extern void drm_connector_unplug_all(struct drm_device *dev);

extern int drm_encoder_init(struct drm_device *dev,
			    struct drm_encoder *encoder,
			    const struct drm_encoder_funcs *funcs,
			    int encoder_type);

extern int drm_plane_init(struct drm_device *dev,
			  struct drm_plane *plane,
			  unsigned long possible_crtcs,
			  const struct drm_plane_funcs *funcs,
			  const uint32_t *formats, uint32_t format_count,
			  bool priv);
extern void drm_plane_cleanup(struct drm_plane *plane);
extern void drm_plane_force_disable(struct drm_plane *plane);

extern void drm_encoder_cleanup(struct drm_encoder *encoder);

extern const char *drm_get_connector_name(const struct drm_connector *connector);
extern const char *drm_get_connector_status_name(enum drm_connector_status status);
extern const char *drm_get_dpms_name(int val);
extern const char *drm_get_dvi_i_subconnector_name(int val);
extern const char *drm_get_dvi_i_select_name(int val);
extern const char *drm_get_tv_subconnector_name(int val);
extern const char *drm_get_tv_select_name(int val);
extern void drm_fb_release(struct drm_file *file_priv);
extern int drm_mode_group_init_legacy_group(struct drm_device *dev, struct drm_mode_group *group);
extern bool drm_probe_ddc(struct i2c_adapter *adapter);
extern struct edid *drm_get_edid(struct drm_connector *connector,
				 struct i2c_adapter *adapter);
extern int drm_add_edid_modes(struct drm_connector *connector, struct edid *edid);
extern void drm_mode_probed_add(struct drm_connector *connector, struct drm_display_mode *mode);
extern void drm_mode_remove(struct drm_connector *connector, struct drm_display_mode *mode);
extern void drm_mode_copy(struct drm_display_mode *dst, const struct drm_display_mode *src);
extern struct drm_display_mode *drm_mode_duplicate(struct drm_device *dev,
						   const struct drm_display_mode *mode);
extern void drm_mode_debug_printmodeline(const struct drm_display_mode *mode);
extern void drm_mode_config_init(struct drm_device *dev);
extern void drm_mode_config_reset(struct drm_device *dev);
extern void drm_mode_config_cleanup(struct drm_device *dev);
extern void drm_mode_set_name(struct drm_display_mode *mode);
extern bool drm_mode_equal(const struct drm_display_mode *mode1, const struct drm_display_mode *mode2);
extern bool drm_mode_equal_no_clocks(const struct drm_display_mode *mode1, const struct drm_display_mode *mode2);
extern int drm_mode_width(const struct drm_display_mode *mode);
extern int drm_mode_height(const struct drm_display_mode *mode);

/* for us by fb module */
extern struct drm_display_mode *drm_mode_create(struct drm_device *dev);
extern void drm_mode_destroy(struct drm_device *dev, struct drm_display_mode *mode);
extern void drm_mode_list_concat(struct list_head *head,
				 struct list_head *new);
extern void drm_mode_validate_size(struct drm_device *dev,
				   struct list_head *mode_list,
				   int maxX, int maxY, int maxPitch);
extern void drm_mode_validate_clocks(struct drm_device *dev,
				     struct list_head *mode_list,
				     int *min, int *max, int n_ranges);
extern void drm_mode_prune_invalid(struct drm_device *dev,
				   struct list_head *mode_list, bool verbose);
extern void drm_mode_sort(struct list_head *mode_list);
extern int drm_mode_hsync(const struct drm_display_mode *mode);
extern int drm_mode_vrefresh(const struct drm_display_mode *mode);
extern void drm_mode_set_crtcinfo(struct drm_display_mode *p,
				  int adjust_flags);
extern void drm_mode_connector_list_update(struct drm_connector *connector);
extern int drm_mode_connector_update_edid_property(struct drm_connector *connector,
						struct edid *edid);
extern int drm_object_property_set_value(struct drm_mode_object *obj,
					 struct drm_property *property,
					 uint64_t val);
extern int drm_object_property_get_value(struct drm_mode_object *obj,
					 struct drm_property *property,
					 uint64_t *value);
extern struct drm_display_mode *drm_crtc_mode_create(struct drm_device *dev);
extern void drm_framebuffer_set_object(struct drm_device *dev,
				       unsigned long handle);
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
extern int drmfb_probe(struct drm_device *dev, struct drm_crtc *crtc);
extern int drmfb_remove(struct drm_device *dev, struct drm_framebuffer *fb);
extern void drm_crtc_probe_connector_modes(struct drm_device *dev, int maxX, int maxY);
extern bool drm_crtc_in_use(struct drm_crtc *crtc);

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
					 int num_values);
struct drm_property *drm_property_create_range(struct drm_device *dev, int flags,
					 const char *name,
					 uint64_t min, uint64_t max);
extern void drm_property_destroy(struct drm_device *dev, struct drm_property *property);
extern int drm_property_add_enum(struct drm_property *property, int index,
				 uint64_t value, const char *name);
extern int drm_mode_create_dvi_i_properties(struct drm_device *dev);
extern int drm_mode_create_tv_properties(struct drm_device *dev, int num_formats,
				     char *formats[]);
extern int drm_mode_create_scaling_mode_property(struct drm_device *dev);
extern int drm_mode_create_dithering_property(struct drm_device *dev);
extern int drm_mode_create_dirty_info_property(struct drm_device *dev);
extern const char *drm_get_encoder_name(const struct drm_encoder *encoder);

extern int drm_mode_connector_attach_encoder(struct drm_connector *connector,
					     struct drm_encoder *encoder);
extern void drm_mode_connector_detach_encoder(struct drm_connector *connector,
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
extern int drm_mode_hotplug_ioctl(struct drm_device *dev,
				  void *data, struct drm_file *file_priv);
extern int drm_mode_replacefb(struct drm_device *dev,
			      void *data, struct drm_file *file_priv);
extern int drm_mode_getencoder(struct drm_device *dev,
			       void *data, struct drm_file *file_priv);
extern int drm_mode_gamma_get_ioctl(struct drm_device *dev,
				    void *data, struct drm_file *file_priv);
extern int drm_mode_gamma_set_ioctl(struct drm_device *dev,
				    void *data, struct drm_file *file_priv);
extern u8 *drm_find_cea_extension(struct edid *edid);
extern u8 drm_match_cea_mode(const struct drm_display_mode *to_match);
extern bool drm_detect_hdmi_monitor(struct edid *edid);
extern bool drm_detect_monitor_audio(struct edid *edid);
extern bool drm_rgb_quant_range_selectable(struct edid *edid);
extern int drm_mode_page_flip_ioctl(struct drm_device *dev,
				    void *data, struct drm_file *file_priv);
extern struct drm_display_mode *drm_cvt_mode(struct drm_device *dev,
				int hdisplay, int vdisplay, int vrefresh,
				bool reduced, bool interlaced, bool margins);
extern struct drm_display_mode *drm_gtf_mode(struct drm_device *dev,
				int hdisplay, int vdisplay, int vrefresh,
				bool interlaced, int margins);
extern struct drm_display_mode *drm_gtf_mode_complex(struct drm_device *dev,
				int hdisplay, int vdisplay, int vrefresh,
				bool interlaced, int margins, int GTF_M,
				int GTF_2C, int GTF_K, int GTF_2J);
extern int drm_add_modes_noedid(struct drm_connector *connector,
				int hdisplay, int vdisplay);

extern int drm_edid_header_is_valid(const u8 *raw_edid);
extern bool drm_edid_block_valid(u8 *raw_edid, int block, bool print_bad_edid);
extern bool drm_edid_is_valid(struct edid *edid);
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

extern void drm_fb_get_bpp_depth(uint32_t format, unsigned int *depth,
				 int *bpp);
extern int drm_format_num_planes(uint32_t format);
extern int drm_format_plane_cpp(uint32_t format, int plane);
extern int drm_format_horz_chroma_subsampling(uint32_t format);
extern int drm_format_vert_chroma_subsampling(uint32_t format);
extern const char *drm_get_format_name(uint32_t format);

#endif /* __DRM_CRTC_H__ */
