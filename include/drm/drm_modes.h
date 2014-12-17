/*
 * Copyright © 2006 Keith Packard
 * Copyright © 2007-2008 Dave Airlie
 * Copyright © 2007-2008 Intel Corporation
 *   Jesse Barnes <jesse.barnes@intel.com>
 * Copyright © 2014 Intel Corporation
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
#ifndef __DRM_MODES_H__
#define __DRM_MODES_H__

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
    MODE_NO_STEREO,	/* stereo modes not supported */
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

#define CRTC_INTERLACE_HALVE_V	(1 << 0) /* halve V values for interlacing */
#define CRTC_STEREO_DOUBLE	(1 << 1) /* adjust timings for stereo modes */

#define DRM_MODE_FLAG_3D_MAX	DRM_MODE_FLAG_3D_SIDE_BY_SIDE_HALF

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
	int crtc_clock;		/* in KHz */
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
	int *private;
	int private_flags;

	int vrefresh;		/* in Hz */
	int hsync;		/* in kHz */
	enum hdmi_picture_aspect picture_aspect_ratio;
};

/* mode specified on the command line */
struct drm_cmdline_mode {
	bool specified;
	bool refresh_specified;
	bool bpp_specified;
	int xres, yres;
	int bpp;
	int refresh;
	bool rb;
	bool interlace;
	bool cvt;
	bool margins;
	enum drm_connector_force force;
};

/**
 * drm_mode_is_stereo - check for stereo mode flags
 * @mode: drm_display_mode to check
 *
 * Returns:
 * True if the mode is one of the stereo modes (like side-by-side), false if
 * not.
 */
static inline bool drm_mode_is_stereo(const struct drm_display_mode *mode)
{
	return mode->flags & DRM_MODE_FLAG_3D_MASK;
}

struct drm_connector;
struct drm_cmdline_mode;

struct drm_display_mode *drm_mode_create(struct drm_device *dev);
void drm_mode_destroy(struct drm_device *dev, struct drm_display_mode *mode);
void drm_mode_probed_add(struct drm_connector *connector, struct drm_display_mode *mode);
void drm_mode_debug_printmodeline(const struct drm_display_mode *mode);

struct drm_display_mode *drm_cvt_mode(struct drm_device *dev,
				      int hdisplay, int vdisplay, int vrefresh,
				      bool reduced, bool interlaced,
				      bool margins);
struct drm_display_mode *drm_gtf_mode(struct drm_device *dev,
				      int hdisplay, int vdisplay, int vrefresh,
				      bool interlaced, int margins);
struct drm_display_mode *drm_gtf_mode_complex(struct drm_device *dev,
					      int hdisplay, int vdisplay,
					      int vrefresh, bool interlaced,
					      int margins,
					      int GTF_M, int GTF_2C,
					      int GTF_K, int GTF_2J);
void drm_display_mode_from_videomode(const struct videomode *vm,
				     struct drm_display_mode *dmode);
int of_get_drm_display_mode(struct device_node *np,
			    struct drm_display_mode *dmode,
			    int index);

void drm_mode_set_name(struct drm_display_mode *mode);
int drm_mode_hsync(const struct drm_display_mode *mode);
int drm_mode_vrefresh(const struct drm_display_mode *mode);

void drm_mode_set_crtcinfo(struct drm_display_mode *p,
			   int adjust_flags);
void drm_mode_copy(struct drm_display_mode *dst,
		   const struct drm_display_mode *src);
struct drm_display_mode *drm_mode_duplicate(struct drm_device *dev,
					    const struct drm_display_mode *mode);
bool drm_mode_equal(const struct drm_display_mode *mode1,
		    const struct drm_display_mode *mode2);
bool drm_mode_equal_no_clocks_no_stereo(const struct drm_display_mode *mode1,
					const struct drm_display_mode *mode2);

/* for use by the crtc helper probe functions */
enum drm_mode_status drm_mode_validate_size(const struct drm_display_mode *mode,
					    int maxX, int maxY);
void drm_mode_prune_invalid(struct drm_device *dev,
			    struct list_head *mode_list, bool verbose);
void drm_mode_sort(struct list_head *mode_list);
void drm_mode_connector_list_update(struct drm_connector *connector, bool merge_type_bits);

/* parsing cmdline modes */
bool
drm_mode_parse_command_line_for_connector(const char *mode_option,
					  struct drm_connector *connector,
					  struct drm_cmdline_mode *mode);
struct drm_display_mode *
drm_mode_create_from_cmdline_mode(struct drm_device *dev,
				  struct drm_cmdline_mode *cmd);

#endif /* __DRM_MODES_H__ */
