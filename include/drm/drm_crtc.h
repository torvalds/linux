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

struct drm_device;
struct drm_mode_set;
struct drm_framebuffer;


#define DRM_MODE_OBJECT_CRTC 0xcccccccc
#define DRM_MODE_OBJECT_CONNECTOR 0xc0c0c0c0
#define DRM_MODE_OBJECT_ENCODER 0xe0e0e0e0
#define DRM_MODE_OBJECT_MODE 0xdededede
#define DRM_MODE_OBJECT_PROPERTY 0xb0b0b0b0
#define DRM_MODE_OBJECT_FB 0xfbfbfbfb
#define DRM_MODE_OBJECT_BLOB 0xbbbbbbbb

struct drm_mode_object {
	uint32_t id;
	uint32_t type;
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
    MODE_NOMODE,	/* no mode with a maching name */
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
	.vscan = (vs), .flags = (f), .vrefresh = 0

#define CRTC_INTERLACE_HALVE_V 0x1 /* halve V values for interlacing */

struct drm_display_mode {
	/* Header */
	struct list_head head;
	struct drm_mode_object base;

	char name[DRM_DISPLAY_MODE_LEN];

	int connector_count;
	enum drm_mode_status status;
	int type;

	/* Proposed mode values */
	int clock;
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
	int crtc_hadjusted;
	int crtc_vadjusted;

	/* Driver private mode info */
	int private_size;
	int *private;
	int private_flags;

	int vrefresh;
	float hsync;
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


/*
 * Describes a given display (e.g. CRT or flat panel) and its limitations.
 */
struct drm_display_info {
	char name[DRM_DISPLAY_INFO_LEN];
	/* Input info */
	bool serration_vsync;
	bool sync_on_green;
	bool composite_sync;
	bool separate_syncs;
	bool blank_to_black;
	unsigned char video_level;
	bool digital;
	/* Physical size */
        unsigned int width_mm;
	unsigned int height_mm;

	/* Display parameters */
	unsigned char gamma; /* FIXME: storage format */
	bool gtf_supported;
	bool standard_color;
	enum {
		monochrome = 0,
		rgb,
		other,
		unknown,
	} display_type;
	bool active_off_supported;
	bool suspend_supported;
	bool standby_supported;

	/* Color info FIXME: storage format */
	unsigned short redx, redy;
	unsigned short greenx, greeny;
	unsigned short bluex, bluey;
	unsigned short whitex, whitey;

	/* Clock limits FIXME: storage format */
	unsigned int min_vfreq, max_vfreq;
	unsigned int min_hfreq, max_hfreq;
	unsigned int pixel_clock;

	/* White point indices FIXME: storage format */
	unsigned int wpx1, wpy1;
	unsigned int wpgamma1;
	unsigned int wpx2, wpy2;
	unsigned int wpgamma2;

	enum subpixel_order subpixel_order;

	char *raw_edid; /* if any */
};

struct drm_framebuffer_funcs {
	void (*destroy)(struct drm_framebuffer *framebuffer);
	int (*create_handle)(struct drm_framebuffer *fb,
			     struct drm_file *file_priv,
			     unsigned int *handle);
};

struct drm_framebuffer {
	struct drm_device *dev;
	struct list_head head;
	struct drm_mode_object base;
	const struct drm_framebuffer_funcs *funcs;
	unsigned int pitch;
	unsigned int width;
	unsigned int height;
	/* depth can be 15 or 16 */
	unsigned int depth;
	int bits_per_pixel;
	int flags;
	void *fbdev;
	u32 pseudo_palette[17];
	struct list_head filp_head;
};

struct drm_property_blob {
	struct drm_mode_object base;
	struct list_head head;
	unsigned int length;
	void *data;
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

/**
 * drm_crtc_funcs - control CRTCs for a given device
 * @dpms: control display power levels
 * @save: save CRTC state
 * @resore: restore CRTC state
 * @lock: lock the CRTC
 * @unlock: unlock the CRTC
 * @shadow_allocate: allocate shadow pixmap
 * @shadow_create: create shadow pixmap for rotation support
 * @shadow_destroy: free shadow pixmap
 * @mode_fixup: fixup proposed mode
 * @mode_set: set the desired mode on the CRTC
 * @gamma_set: specify color ramp for CRTC
 * @destroy: deinit and free object.
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

	/* cursor controls */
	int (*cursor_set)(struct drm_crtc *crtc, struct drm_file *file_priv,
			  uint32_t handle, uint32_t width, uint32_t height);
	int (*cursor_move)(struct drm_crtc *crtc, int x, int y);

	/* Set gamma on the CRTC */
	void (*gamma_set)(struct drm_crtc *crtc, u16 *r, u16 *g, u16 *b,
			  uint32_t size);
	/* Object destroy routine */
	void (*destroy)(struct drm_crtc *crtc);

	int (*set_config)(struct drm_mode_set *set);
};

/**
 * drm_crtc - central CRTC control structure
 * @enabled: is this CRTC enabled?
 * @x: x position on screen
 * @y: y position on screen
 * @desired_mode: new desired mode
 * @desired_x: desired x for desired_mode
 * @desired_y: desired y for desired_mode
 * @funcs: CRTC control functions
 *
 * Each CRTC may have one or more connectors associated with it.  This structure
 * allows the CRTC to be controlled.
 */
struct drm_crtc {
	struct drm_device *dev;
	struct list_head head;

	struct drm_mode_object base;

	/* framebuffer the connector is currently bound to */
	struct drm_framebuffer *fb;

	bool enabled;

	struct drm_display_mode mode;

	int x, y;
	struct drm_display_mode *desired_mode;
	int desired_x, desired_y;
	const struct drm_crtc_funcs *funcs;

	/* CRTC gamma size for reporting to userspace */
	uint32_t gamma_size;
	uint16_t *gamma_store;

	/* if you are using the helper */
	void *helper_private;
};


/**
 * drm_connector_funcs - control connectors on a given device
 * @dpms: set power state (see drm_crtc_funcs above)
 * @save: save connector state
 * @restore: restore connector state
 * @mode_valid: is this mode valid on the given connector?
 * @mode_fixup: try to fixup proposed mode for this connector
 * @mode_set: set this mode
 * @detect: is this connector active?
 * @get_modes: get mode list for this connector
 * @set_property: property for this connector may need update
 * @destroy: make object go away
 *
 * Each CRTC may have one or more connectors attached to it.  The functions
 * below allow the core DRM code to control connectors, enumerate available modes,
 * etc.
 */
struct drm_connector_funcs {
	void (*dpms)(struct drm_connector *connector, int mode);
	void (*save)(struct drm_connector *connector);
	void (*restore)(struct drm_connector *connector);
	enum drm_connector_status (*detect)(struct drm_connector *connector);
	int (*fill_modes)(struct drm_connector *connector, uint32_t max_width, uint32_t max_height);
	int (*set_property)(struct drm_connector *connector, struct drm_property *property,
			     uint64_t val);
	void (*destroy)(struct drm_connector *connector);
};

struct drm_encoder_funcs {
	void (*destroy)(struct drm_encoder *encoder);
};

#define DRM_CONNECTOR_MAX_UMODES 16
#define DRM_CONNECTOR_MAX_PROPERTY 16
#define DRM_CONNECTOR_LEN 32
#define DRM_CONNECTOR_MAX_ENCODER 2

/**
 * drm_encoder - central DRM encoder structure
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

/**
 * drm_connector - central DRM connector control structure
 * @crtc: CRTC this connector is currently connected to, NULL if none
 * @interlace_allowed: can this connector handle interlaced modes?
 * @doublescan_allowed: can this connector handle doublescan?
 * @available_modes: modes available on this connector (from get_modes() + user)
 * @initial_x: initial x position for this connector
 * @initial_y: initial y position for this connector
 * @status: connector connected?
 * @funcs: connector control functions
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

	int initial_x, initial_y;
	enum drm_connector_status status;

	/* these are modes added by probing with DDC or the BIOS */
	struct list_head probed_modes;

	struct drm_display_info display_info;
	const struct drm_connector_funcs *funcs;

	struct list_head user_modes;
	struct drm_property_blob *edid_blob_ptr;
	u32 property_ids[DRM_CONNECTOR_MAX_PROPERTY];
	uint64_t property_values[DRM_CONNECTOR_MAX_PROPERTY];

	/* requested DPMS state */
	int dpms;

	void *helper_private;

	uint32_t encoder_ids[DRM_CONNECTOR_MAX_ENCODER];
	uint32_t force_encoder_id;
	struct drm_encoder *encoder; /* currently active encoder */
};

/**
 * struct drm_mode_set
 *
 * Represents a single crtc the connectors that it drives with what mode
 * and from which framebuffer it scans out from.
 *
 * This is used to set modes.
 */
struct drm_mode_set {
	struct list_head head;

	struct drm_framebuffer *fb;
	struct drm_crtc *crtc;
	struct drm_display_mode *mode;

	uint32_t x;
	uint32_t y;

	struct drm_connector **connectors;
	size_t num_connectors;
};

/**
 * struct drm_mode_config_funcs - configure CRTCs for a given screen layout
 * @resize: adjust CRTCs as necessary for the proposed layout
 *
 * Currently only a resize hook is available.  DRM will call back into the
 * driver with a new screen width and height.  If the driver can't support
 * the proposed size, it can return false.  Otherwise it should adjust
 * the CRTC<->connector mappings as needed and update its view of the screen.
 */
struct drm_mode_config_funcs {
	struct drm_framebuffer *(*fb_create)(struct drm_device *dev, struct drm_file *file_priv, struct drm_mode_fb_cmd *mode_cmd);
	int (*fb_changed)(struct drm_device *dev);
};

struct drm_mode_group {
	uint32_t num_crtcs;
	uint32_t num_encoders;
	uint32_t num_connectors;

	/* list of object IDs for this group */
	uint32_t *id_list;
};

/**
 * drm_mode_config - Mode configuration control structure
 *
 */
struct drm_mode_config {
	struct mutex mutex; /* protects configuration (mode lists etc.) */
	struct mutex idr_mutex; /* for IDR management */
	struct idr crtc_idr; /* use this idr for all IDs, fb, crtc, connector, modes - just makes life easier */
	/* this is limited to one for now */
	int num_fb;
	struct list_head fb_list;
	int num_connector;
	struct list_head connector_list;
	int num_encoder;
	struct list_head encoder_list;

	int num_crtc;
	struct list_head crtc_list;

	struct list_head property_list;

	/* in-kernel framebuffers - hung of filp_head in drm_framebuffer */
	struct list_head fb_kernel_list;

	int min_width, min_height;
	int max_width, max_height;
	struct drm_mode_config_funcs *funcs;
	resource_size_t fb_base;

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
};

#define obj_to_crtc(x) container_of(x, struct drm_crtc, base)
#define obj_to_connector(x) container_of(x, struct drm_connector, base)
#define obj_to_encoder(x) container_of(x, struct drm_encoder, base)
#define obj_to_mode(x) container_of(x, struct drm_display_mode, base)
#define obj_to_fb(x) container_of(x, struct drm_framebuffer, base)
#define obj_to_property(x) container_of(x, struct drm_property, base)
#define obj_to_blob(x) container_of(x, struct drm_property_blob, base)


extern void drm_crtc_init(struct drm_device *dev,
			  struct drm_crtc *crtc,
			  const struct drm_crtc_funcs *funcs);
extern void drm_crtc_cleanup(struct drm_crtc *crtc);

extern void drm_connector_init(struct drm_device *dev,
			    struct drm_connector *connector,
			    const struct drm_connector_funcs *funcs,
			    int connector_type);

extern void drm_connector_cleanup(struct drm_connector *connector);

extern void drm_encoder_init(struct drm_device *dev,
			     struct drm_encoder *encoder,
			     const struct drm_encoder_funcs *funcs,
			     int encoder_type);

extern void drm_encoder_cleanup(struct drm_encoder *encoder);

extern char *drm_get_connector_name(struct drm_connector *connector);
extern char *drm_get_dpms_name(int val);
extern char *drm_get_dvi_i_subconnector_name(int val);
extern char *drm_get_dvi_i_select_name(int val);
extern char *drm_get_tv_subconnector_name(int val);
extern char *drm_get_tv_select_name(int val);
extern void drm_fb_release(struct drm_file *file_priv);
extern int drm_mode_group_init_legacy_group(struct drm_device *dev, struct drm_mode_group *group);
extern struct edid *drm_get_edid(struct drm_connector *connector,
				 struct i2c_adapter *adapter);
extern int drm_do_probe_ddc_edid(struct i2c_adapter *adapter,
				 unsigned char *buf, int len);
extern int drm_add_edid_modes(struct drm_connector *connector, struct edid *edid);
extern void drm_mode_probed_add(struct drm_connector *connector, struct drm_display_mode *mode);
extern void drm_mode_remove(struct drm_connector *connector, struct drm_display_mode *mode);
extern struct drm_display_mode *drm_mode_duplicate(struct drm_device *dev,
						   struct drm_display_mode *mode);
extern void drm_mode_debug_printmodeline(struct drm_display_mode *mode);
extern void drm_mode_config_init(struct drm_device *dev);
extern void drm_mode_config_cleanup(struct drm_device *dev);
extern void drm_mode_set_name(struct drm_display_mode *mode);
extern bool drm_mode_equal(struct drm_display_mode *mode1, struct drm_display_mode *mode2);
extern int drm_mode_width(struct drm_display_mode *mode);
extern int drm_mode_height(struct drm_display_mode *mode);

/* for us by fb module */
extern int drm_mode_attachmode_crtc(struct drm_device *dev,
				    struct drm_crtc *crtc,
				    struct drm_display_mode *mode);
extern int drm_mode_detachmode_crtc(struct drm_device *dev, struct drm_display_mode *mode);

extern struct drm_display_mode *drm_mode_create(struct drm_device *dev);
extern void drm_mode_destroy(struct drm_device *dev, struct drm_display_mode *mode);
extern void drm_mode_list_concat(struct list_head *head,
				 struct list_head *new);
extern void drm_mode_validate_size(struct drm_device *dev,
				   struct list_head *mode_list,
				   int maxX, int maxY, int maxPitch);
extern void drm_mode_prune_invalid(struct drm_device *dev,
				   struct list_head *mode_list, bool verbose);
extern void drm_mode_sort(struct list_head *mode_list);
extern int drm_mode_vrefresh(struct drm_display_mode *mode);
extern void drm_mode_set_crtcinfo(struct drm_display_mode *p,
				  int adjust_flags);
extern void drm_mode_connector_list_update(struct drm_connector *connector);
extern int drm_mode_connector_update_edid_property(struct drm_connector *connector,
						struct edid *edid);
extern int drm_connector_property_set_value(struct drm_connector *connector,
					 struct drm_property *property,
					 uint64_t value);
extern int drm_connector_property_get_value(struct drm_connector *connector,
					 struct drm_property *property,
					 uint64_t *value);
extern struct drm_display_mode *drm_crtc_mode_create(struct drm_device *dev);
extern void drm_framebuffer_set_object(struct drm_device *dev,
				       unsigned long handle);
extern int drm_framebuffer_init(struct drm_device *dev,
				struct drm_framebuffer *fb,
				const struct drm_framebuffer_funcs *funcs);
extern void drm_framebuffer_cleanup(struct drm_framebuffer *fb);
extern int drmfb_probe(struct drm_device *dev, struct drm_crtc *crtc);
extern int drmfb_remove(struct drm_device *dev, struct drm_framebuffer *fb);
extern void drm_crtc_probe_connector_modes(struct drm_device *dev, int maxX, int maxY);
extern bool drm_crtc_in_use(struct drm_crtc *crtc);

extern int drm_connector_attach_property(struct drm_connector *connector,
				      struct drm_property *property, uint64_t init_val);
extern struct drm_property *drm_property_create(struct drm_device *dev, int flags,
						const char *name, int num_values);
extern void drm_property_destroy(struct drm_device *dev, struct drm_property *property);
extern int drm_property_add_enum(struct drm_property *property, int index,
				 uint64_t value, const char *name);
extern int drm_mode_create_dvi_i_properties(struct drm_device *dev);
extern int drm_mode_create_tv_properties(struct drm_device *dev, int num_formats,
				     char *formats[]);
extern int drm_mode_create_scaling_mode_property(struct drm_device *dev);
extern int drm_mode_create_dithering_property(struct drm_device *dev);
extern char *drm_get_encoder_name(struct drm_encoder *encoder);

extern int drm_mode_connector_attach_encoder(struct drm_connector *connector,
					     struct drm_encoder *encoder);
extern void drm_mode_connector_detach_encoder(struct drm_connector *connector,
					   struct drm_encoder *encoder);
extern bool drm_mode_crtc_set_gamma_size(struct drm_crtc *crtc,
					 int gamma_size);
extern void *drm_mode_object_find(struct drm_device *dev, uint32_t id, uint32_t type);
/* IOCTLs */
extern int drm_mode_getresources(struct drm_device *dev,
				 void *data, struct drm_file *file_priv);

extern int drm_mode_getcrtc(struct drm_device *dev,
			    void *data, struct drm_file *file_priv);
extern int drm_mode_getconnector(struct drm_device *dev,
			      void *data, struct drm_file *file_priv);
extern int drm_mode_setcrtc(struct drm_device *dev,
			    void *data, struct drm_file *file_priv);
extern int drm_mode_cursor_ioctl(struct drm_device *dev,
				void *data, struct drm_file *file_priv);
extern int drm_mode_addfb(struct drm_device *dev,
			  void *data, struct drm_file *file_priv);
extern int drm_mode_rmfb(struct drm_device *dev,
			 void *data, struct drm_file *file_priv);
extern int drm_mode_getfb(struct drm_device *dev,
			  void *data, struct drm_file *file_priv);
extern int drm_mode_addmode_ioctl(struct drm_device *dev,
				  void *data, struct drm_file *file_priv);
extern int drm_mode_rmmode_ioctl(struct drm_device *dev,
				 void *data, struct drm_file *file_priv);
extern int drm_mode_attachmode_ioctl(struct drm_device *dev,
				     void *data, struct drm_file *file_priv);
extern int drm_mode_detachmode_ioctl(struct drm_device *dev,
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
extern bool drm_detect_hdmi_monitor(struct edid *edid);
extern struct drm_display_mode *drm_cvt_mode(struct drm_device *dev,
				int hdisplay, int vdisplay, int vrefresh,
				bool reduced, bool interlaced);
extern struct drm_display_mode *drm_gtf_mode(struct drm_device *dev,
				int hdisplay, int vdisplay, int vrefresh,
				bool interlaced, int margins);
#endif /* __DRM_CRTC_H__ */
