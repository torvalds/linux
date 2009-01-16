/*
 * Copyright (c) 2007 Dave Airlie <airlied@linux.ie>
 * Copyright (c) 2007 Jakob Bornecrantz <wallbraker@gmail.com>
 * Copyright (c) 2008 Red Hat Inc.
 * Copyright (c) 2007-2008 Tungsten Graphics, Inc., Cedar Park, TX., USA
 * Copyright (c) 2007-2008 Intel Corporation
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
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

#ifndef _DRM_MODE_H
#define _DRM_MODE_H

#if !defined(__KERNEL__) && !defined(_KERNEL)
#include <stdint.h>
#else
#include <linux/kernel.h>
#endif

#define DRM_DISPLAY_INFO_LEN	32
#define DRM_CONNECTOR_NAME_LEN	32
#define DRM_DISPLAY_MODE_LEN	32
#define DRM_PROP_NAME_LEN	32

#define DRM_MODE_TYPE_BUILTIN	(1<<0)
#define DRM_MODE_TYPE_CLOCK_C	((1<<1) | DRM_MODE_TYPE_BUILTIN)
#define DRM_MODE_TYPE_CRTC_C	((1<<2) | DRM_MODE_TYPE_BUILTIN)
#define DRM_MODE_TYPE_PREFERRED	(1<<3)
#define DRM_MODE_TYPE_DEFAULT	(1<<4)
#define DRM_MODE_TYPE_USERDEF	(1<<5)
#define DRM_MODE_TYPE_DRIVER	(1<<6)

/* Video mode flags */
/* bit compatible with the xorg definitions. */
#define DRM_MODE_FLAG_PHSYNC	(1<<0)
#define DRM_MODE_FLAG_NHSYNC	(1<<1)
#define DRM_MODE_FLAG_PVSYNC	(1<<2)
#define DRM_MODE_FLAG_NVSYNC	(1<<3)
#define DRM_MODE_FLAG_INTERLACE	(1<<4)
#define DRM_MODE_FLAG_DBLSCAN	(1<<5)
#define DRM_MODE_FLAG_CSYNC	(1<<6)
#define DRM_MODE_FLAG_PCSYNC	(1<<7)
#define DRM_MODE_FLAG_NCSYNC	(1<<8)
#define DRM_MODE_FLAG_HSKEW	(1<<9) /* hskew provided */
#define DRM_MODE_FLAG_BCAST	(1<<10)
#define DRM_MODE_FLAG_PIXMUX	(1<<11)
#define DRM_MODE_FLAG_DBLCLK	(1<<12)
#define DRM_MODE_FLAG_CLKDIV2	(1<<13)

/* DPMS flags */
/* bit compatible with the xorg definitions. */
#define DRM_MODE_DPMS_ON	0
#define DRM_MODE_DPMS_STANDBY	1
#define DRM_MODE_DPMS_SUSPEND	2
#define DRM_MODE_DPMS_OFF	3

/* Scaling mode options */
#define DRM_MODE_SCALE_NON_GPU		0
#define DRM_MODE_SCALE_FULLSCREEN	1
#define DRM_MODE_SCALE_NO_SCALE		2
#define DRM_MODE_SCALE_ASPECT		3

/* Dithering mode options */
#define DRM_MODE_DITHERING_OFF	0
#define DRM_MODE_DITHERING_ON	1

struct drm_mode_modeinfo {
	uint32_t clock;
	uint16_t hdisplay, hsync_start, hsync_end, htotal, hskew;
	uint16_t vdisplay, vsync_start, vsync_end, vtotal, vscan;

	uint32_t vrefresh; /* vertical refresh * 1000 */

	uint32_t flags;
	uint32_t type;
	char name[DRM_DISPLAY_MODE_LEN];
};

struct drm_mode_card_res {
	uint64_t fb_id_ptr;
	uint64_t crtc_id_ptr;
	uint64_t connector_id_ptr;
	uint64_t encoder_id_ptr;
	uint32_t count_fbs;
	uint32_t count_crtcs;
	uint32_t count_connectors;
	uint32_t count_encoders;
	uint32_t min_width, max_width;
	uint32_t min_height, max_height;
};

struct drm_mode_crtc {
	uint64_t set_connectors_ptr;
	uint32_t count_connectors;

	uint32_t crtc_id; /**< Id */
	uint32_t fb_id; /**< Id of framebuffer */

	uint32_t x, y; /**< Position on the frameuffer */

	uint32_t gamma_size;
	uint32_t mode_valid;
	struct drm_mode_modeinfo mode;
};

#define DRM_MODE_ENCODER_NONE	0
#define DRM_MODE_ENCODER_DAC	1
#define DRM_MODE_ENCODER_TMDS	2
#define DRM_MODE_ENCODER_LVDS	3
#define DRM_MODE_ENCODER_TVDAC	4

struct drm_mode_get_encoder {
	uint32_t encoder_id;
	uint32_t encoder_type;

	uint32_t crtc_id; /**< Id of crtc */

	uint32_t possible_crtcs;
	uint32_t possible_clones;
};

/* This is for connectors with multiple signal types. */
/* Try to match DRM_MODE_CONNECTOR_X as closely as possible. */
#define DRM_MODE_SUBCONNECTOR_Automatic	0
#define DRM_MODE_SUBCONNECTOR_Unknown	0
#define DRM_MODE_SUBCONNECTOR_DVID	3
#define DRM_MODE_SUBCONNECTOR_DVIA	4
#define DRM_MODE_SUBCONNECTOR_Composite	5
#define DRM_MODE_SUBCONNECTOR_SVIDEO	6
#define DRM_MODE_SUBCONNECTOR_Component	8

#define DRM_MODE_CONNECTOR_Unknown	0
#define DRM_MODE_CONNECTOR_VGA		1
#define DRM_MODE_CONNECTOR_DVII		2
#define DRM_MODE_CONNECTOR_DVID		3
#define DRM_MODE_CONNECTOR_DVIA		4
#define DRM_MODE_CONNECTOR_Composite	5
#define DRM_MODE_CONNECTOR_SVIDEO	6
#define DRM_MODE_CONNECTOR_LVDS		7
#define DRM_MODE_CONNECTOR_Component	8
#define DRM_MODE_CONNECTOR_9PinDIN	9
#define DRM_MODE_CONNECTOR_DisplayPort	10
#define DRM_MODE_CONNECTOR_HDMIA	11
#define DRM_MODE_CONNECTOR_HDMIB	12

struct drm_mode_get_connector {

	uint64_t encoders_ptr;
	uint64_t modes_ptr;
	uint64_t props_ptr;
	uint64_t prop_values_ptr;

	uint32_t count_modes;
	uint32_t count_props;
	uint32_t count_encoders;

	uint32_t encoder_id; /**< Current Encoder */
	uint32_t connector_id; /**< Id */
	uint32_t connector_type;
	uint32_t connector_type_id;

	uint32_t connection;
	uint32_t mm_width, mm_height; /**< HxW in millimeters */
	uint32_t subpixel;
};

#define DRM_MODE_PROP_PENDING	(1<<0)
#define DRM_MODE_PROP_RANGE	(1<<1)
#define DRM_MODE_PROP_IMMUTABLE	(1<<2)
#define DRM_MODE_PROP_ENUM	(1<<3) /* enumerated type with text strings */
#define DRM_MODE_PROP_BLOB	(1<<4)

struct drm_mode_property_enum {
	uint64_t value;
	char name[DRM_PROP_NAME_LEN];
};

struct drm_mode_get_property {
	uint64_t values_ptr; /* values and blob lengths */
	uint64_t enum_blob_ptr; /* enum and blob id ptrs */

	uint32_t prop_id;
	uint32_t flags;
	char name[DRM_PROP_NAME_LEN];

	uint32_t count_values;
	uint32_t count_enum_blobs;
};

struct drm_mode_connector_set_property {
	uint64_t value;
	uint32_t prop_id;
	uint32_t connector_id;
};

struct drm_mode_get_blob {
	uint32_t blob_id;
	uint32_t length;
	uint64_t data;
};

struct drm_mode_fb_cmd {
	uint32_t fb_id;
	uint32_t width, height;
	uint32_t pitch;
	uint32_t bpp;
	uint32_t depth;
	/* driver specific handle */
	uint32_t handle;
};

struct drm_mode_mode_cmd {
	uint32_t connector_id;
	struct drm_mode_modeinfo mode;
};

#define DRM_MODE_CURSOR_BO	(1<<0)
#define DRM_MODE_CURSOR_MOVE	(1<<1)

/*
 * depending on the value in flags diffrent members are used.
 *
 * CURSOR_BO uses
 *    crtc
 *    width
 *    height
 *    handle - if 0 turns the cursor of
 *
 * CURSOR_MOVE uses
 *    crtc
 *    x
 *    y
 */
struct drm_mode_cursor {
	uint32_t flags;
	uint32_t crtc_id;
	int32_t x;
	int32_t y;
	uint32_t width;
	uint32_t height;
	/* driver specific handle */
	uint32_t handle;
};

struct drm_mode_crtc_lut {
	uint32_t crtc_id;
	uint32_t gamma_size;

	/* pointers to arrays */
	uint64_t red;
	uint64_t green;
	uint64_t blue;
};

#endif
