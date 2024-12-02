/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2007-2009 Texas Instruments Inc
 * Copyright (C) 2007 MontaVista Software, Inc.
 *
 * Andy Lowe (alowe@mvista.com), MontaVista Software
 * - Initial version
 * Murali Karicheri (mkaricheri@gmail.com), Texas Instruments Ltd.
 * - ported to sub device interface
 */
#ifndef _OSD_H
#define _OSD_H

#include <media/davinci/vpbe_types.h>

#define DM644X_VPBE_OSD_SUBDEV_NAME	"dm644x,vpbe-osd"
#define DM365_VPBE_OSD_SUBDEV_NAME	"dm365,vpbe-osd"
#define DM355_VPBE_OSD_SUBDEV_NAME	"dm355,vpbe-osd"

/**
 * enum osd_layer
 * @WIN_OSD0: On-Screen Display Window 0
 * @WIN_VID0: Video Window 0
 * @WIN_OSD1: On-Screen Display Window 1
 * @WIN_VID1: Video Window 1
 *
 * Description:
 * An enumeration of the osd display layers.
 */
enum osd_layer {
	WIN_OSD0,
	WIN_VID0,
	WIN_OSD1,
	WIN_VID1,
};

/**
 * enum osd_win_layer
 * @OSDWIN_OSD0: On-Screen Display Window 0
 * @OSDWIN_OSD1: On-Screen Display Window 1
 *
 * Description:
 * An enumeration of the OSD Window layers.
 */
enum osd_win_layer {
	OSDWIN_OSD0,
	OSDWIN_OSD1,
};

/**
 * enum osd_pix_format
 * @PIXFMT_1BPP: 1-bit-per-pixel bitmap
 * @PIXFMT_2BPP: 2-bits-per-pixel bitmap
 * @PIXFMT_4BPP: 4-bits-per-pixel bitmap
 * @PIXFMT_8BPP: 8-bits-per-pixel bitmap
 * @PIXFMT_RGB565: 16-bits-per-pixel RGB565
 * @PIXFMT_YCBCRI: YUV 4:2:2
 * @PIXFMT_RGB888: 24-bits-per-pixel RGB888
 * @PIXFMT_YCRCBI: YUV 4:2:2 with chroma swap
 * @PIXFMT_NV12: YUV 4:2:0 planar
 * @PIXFMT_OSD_ATTR: OSD Attribute Window pixel format (4bpp)
 *
 * Description:
 * An enumeration of the DaVinci pixel formats.
 */
enum osd_pix_format {
	PIXFMT_1BPP = 0,
	PIXFMT_2BPP,
	PIXFMT_4BPP,
	PIXFMT_8BPP,
	PIXFMT_RGB565,
	PIXFMT_YCBCRI,
	PIXFMT_RGB888,
	PIXFMT_YCRCBI,
	PIXFMT_NV12,
	PIXFMT_OSD_ATTR,
};

/**
 * enum osd_h_exp_ratio
 * @H_EXP_OFF: no expansion (1/1)
 * @H_EXP_9_OVER_8: 9/8 expansion ratio
 * @H_EXP_3_OVER_2: 3/2 expansion ratio
 *
 * Description:
 * An enumeration of the available horizontal expansion ratios.
 */
enum osd_h_exp_ratio {
	H_EXP_OFF,
	H_EXP_9_OVER_8,
	H_EXP_3_OVER_2,
};

/**
 * enum osd_v_exp_ratio
 * @V_EXP_OFF: no expansion (1/1)
 * @V_EXP_6_OVER_5: 6/5 expansion ratio
 *
 * Description:
 * An enumeration of the available vertical expansion ratios.
 */
enum osd_v_exp_ratio {
	V_EXP_OFF,
	V_EXP_6_OVER_5,
};

/**
 * enum osd_zoom_factor
 * @ZOOM_X1: no zoom (x1)
 * @ZOOM_X2: x2 zoom
 * @ZOOM_X4: x4 zoom
 *
 * Description:
 * An enumeration of the available zoom factors.
 */
enum osd_zoom_factor {
	ZOOM_X1,
	ZOOM_X2,
	ZOOM_X4,
};

/**
 * enum osd_clut
 * @ROM_CLUT: ROM CLUT
 * @RAM_CLUT: RAM CLUT
 *
 * Description:
 * An enumeration of the available Color Lookup Tables (CLUTs).
 */
enum osd_clut {
	ROM_CLUT,
	RAM_CLUT,
};

/**
 * enum osd_rom_clut
 * @ROM_CLUT0: Macintosh CLUT
 * @ROM_CLUT1: CLUT from DM270 and prior devices
 *
 * Description:
 * An enumeration of the ROM Color Lookup Table (CLUT) options.
 */
enum osd_rom_clut {
	ROM_CLUT0,
	ROM_CLUT1,
};

/**
 * enum osd_blending_factor
 * @OSD_0_VID_8: OSD pixels are fully transparent
 * @OSD_1_VID_7: OSD pixels contribute 1/8, video pixels contribute 7/8
 * @OSD_2_VID_6: OSD pixels contribute 2/8, video pixels contribute 6/8
 * @OSD_3_VID_5: OSD pixels contribute 3/8, video pixels contribute 5/8
 * @OSD_4_VID_4: OSD pixels contribute 4/8, video pixels contribute 4/8
 * @OSD_5_VID_3: OSD pixels contribute 5/8, video pixels contribute 3/8
 * @OSD_6_VID_2: OSD pixels contribute 6/8, video pixels contribute 2/8
 * @OSD_8_VID_0: OSD pixels are fully opaque
 *
 * Description:
 * An enumeration of the DaVinci pixel blending factor options.
 */
enum osd_blending_factor {
	OSD_0_VID_8,
	OSD_1_VID_7,
	OSD_2_VID_6,
	OSD_3_VID_5,
	OSD_4_VID_4,
	OSD_5_VID_3,
	OSD_6_VID_2,
	OSD_8_VID_0,
};

/**
 * enum osd_blink_interval
 * @BLINK_X1: blink interval is 1 vertical refresh cycle
 * @BLINK_X2: blink interval is 2 vertical refresh cycles
 * @BLINK_X3: blink interval is 3 vertical refresh cycles
 * @BLINK_X4: blink interval is 4 vertical refresh cycles
 *
 * Description:
 * An enumeration of the DaVinci pixel blinking interval options.
 */
enum osd_blink_interval {
	BLINK_X1,
	BLINK_X2,
	BLINK_X3,
	BLINK_X4,
};

/**
 * enum osd_cursor_h_width
 * @H_WIDTH_1: horizontal line width is 1 pixel
 * @H_WIDTH_4: horizontal line width is 4 pixels
 * @H_WIDTH_8: horizontal line width is 8 pixels
 * @H_WIDTH_12: horizontal line width is 12 pixels
 * @H_WIDTH_16: horizontal line width is 16 pixels
 * @H_WIDTH_20: horizontal line width is 20 pixels
 * @H_WIDTH_24: horizontal line width is 24 pixels
 * @H_WIDTH_28: horizontal line width is 28 pixels
 */
enum osd_cursor_h_width {
	H_WIDTH_1,
	H_WIDTH_4,
	H_WIDTH_8,
	H_WIDTH_12,
	H_WIDTH_16,
	H_WIDTH_20,
	H_WIDTH_24,
	H_WIDTH_28,
};

/**
 * enum osd_cursor_v_width
 * @V_WIDTH_1: vertical line width is 1 line
 * @V_WIDTH_2: vertical line width is 2 lines
 * @V_WIDTH_4: vertical line width is 4 lines
 * @V_WIDTH_6: vertical line width is 6 lines
 * @V_WIDTH_8: vertical line width is 8 lines
 * @V_WIDTH_10: vertical line width is 10 lines
 * @V_WIDTH_12: vertical line width is 12 lines
 * @V_WIDTH_14: vertical line width is 14 lines
 */
enum osd_cursor_v_width {
	V_WIDTH_1,
	V_WIDTH_2,
	V_WIDTH_4,
	V_WIDTH_6,
	V_WIDTH_8,
	V_WIDTH_10,
	V_WIDTH_12,
	V_WIDTH_14,
};

/**
 * struct osd_cursor_config
 * @xsize: horizontal size in pixels
 * @ysize: vertical size in lines
 * @xpos: horizontal offset in pixels from the left edge of the display
 * @ypos: vertical offset in lines from the top of the display
 * @interlaced: Non-zero if the display is interlaced, or zero otherwise
 * @h_width: horizontal line width
 * @v_width: vertical line width
 * @clut: the CLUT selector (ROM or RAM) for the cursor color
 * @clut_index: an index into the CLUT for the cursor color
 *
 * Description:
 * A structure describing the configuration parameters of the hardware
 * rectangular cursor.
 */
struct osd_cursor_config {
	unsigned xsize;
	unsigned ysize;
	unsigned xpos;
	unsigned ypos;
	int interlaced;
	enum osd_cursor_h_width h_width;
	enum osd_cursor_v_width v_width;
	enum osd_clut clut;
	unsigned char clut_index;
};

/**
 * struct osd_layer_config
 * @pixfmt: pixel format
 * @line_length: offset in bytes between start of each line in memory
 * @xsize: number of horizontal pixels displayed per line
 * @ysize: number of lines displayed
 * @xpos: horizontal offset in pixels from the left edge of the display
 * @ypos: vertical offset in lines from the top of the display
 * @interlaced: Non-zero if the display is interlaced, or zero otherwise
 *
 * Description:
 * A structure describing the configuration parameters of an On-Screen Display
 * (OSD) or video layer related to how the image is stored in memory.
 * @line_length must be a multiple of the cache line size (32 bytes).
 */
struct osd_layer_config {
	enum osd_pix_format pixfmt;
	unsigned line_length;
	unsigned xsize;
	unsigned ysize;
	unsigned xpos;
	unsigned ypos;
	int interlaced;
};

/* parameters that apply on a per-window (OSD or video) basis */
struct osd_window_state {
	int is_allocated;
	int is_enabled;
	unsigned long fb_base_phys;
	enum osd_zoom_factor h_zoom;
	enum osd_zoom_factor v_zoom;
	struct osd_layer_config lconfig;
};

/* parameters that apply on a per-OSD-window basis */
struct osd_osdwin_state {
	enum osd_clut clut;
	enum osd_blending_factor blend;
	int colorkey_blending;
	unsigned colorkey;
	int rec601_attenuation;
	/* index is pixel value */
	unsigned char palette_map[16];
};

/* hardware rectangular cursor parameters */
struct osd_cursor_state {
	int is_enabled;
	struct osd_cursor_config config;
};

struct osd_state;

struct vpbe_osd_ops {
	int (*initialize)(struct osd_state *sd);
	int (*request_layer)(struct osd_state *sd, enum osd_layer layer);
	void (*release_layer)(struct osd_state *sd, enum osd_layer layer);
	int (*enable_layer)(struct osd_state *sd, enum osd_layer layer,
			    int otherwin);
	void (*disable_layer)(struct osd_state *sd, enum osd_layer layer);
	int (*set_layer_config)(struct osd_state *sd, enum osd_layer layer,
				struct osd_layer_config *lconfig);
	void (*get_layer_config)(struct osd_state *sd, enum osd_layer layer,
				 struct osd_layer_config *lconfig);
	void (*start_layer)(struct osd_state *sd, enum osd_layer layer,
			    unsigned long fb_base_phys,
			    unsigned long cbcr_ofst);
	void (*set_left_margin)(struct osd_state *sd, u32 val);
	void (*set_top_margin)(struct osd_state *sd, u32 val);
	void (*set_interpolation_filter)(struct osd_state *sd, int filter);
	int (*set_vid_expansion)(struct osd_state *sd,
					enum osd_h_exp_ratio h_exp,
					enum osd_v_exp_ratio v_exp);
	void (*get_vid_expansion)(struct osd_state *sd,
					enum osd_h_exp_ratio *h_exp,
					enum osd_v_exp_ratio *v_exp);
	void (*set_zoom)(struct osd_state *sd, enum osd_layer layer,
				enum osd_zoom_factor h_zoom,
				enum osd_zoom_factor v_zoom);
};

struct osd_state {
	enum vpbe_version vpbe_type;
	spinlock_t lock;
	struct device *dev;
	dma_addr_t osd_base_phys;
	void __iomem *osd_base;
	unsigned long osd_size;
	/* 1-->the isr will toggle the VID0 ping-pong buffer */
	int pingpong;
	int interpolation_filter;
	int field_inversion;
	enum osd_h_exp_ratio osd_h_exp;
	enum osd_v_exp_ratio osd_v_exp;
	enum osd_h_exp_ratio vid_h_exp;
	enum osd_v_exp_ratio vid_v_exp;
	enum osd_clut backg_clut;
	unsigned backg_clut_index;
	enum osd_rom_clut rom_clut;
	int is_blinking;
	/* attribute window blinking enabled */
	enum osd_blink_interval blink;
	/* YCbCrI or YCrCbI */
	enum osd_pix_format yc_pixfmt;
	/* columns are Y, Cb, Cr */
	unsigned char clut_ram[256][3];
	struct osd_cursor_state cursor;
	/* OSD0, VID0, OSD1, VID1 */
	struct osd_window_state win[4];
	/* OSD0, OSD1 */
	struct osd_osdwin_state osdwin[2];
	/* OSD device Operations */
	struct vpbe_osd_ops ops;
};

struct osd_platform_data {
	int  field_inv_wa_enable;
};

#endif
