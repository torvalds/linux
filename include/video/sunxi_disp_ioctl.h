/*
 * Copyright (C) 2007-2012 Allwinner Technology Co., Ltd.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.	 See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston,
 * MA 02111-1307 USA
 */

#ifndef __SUNXI_DISP_IOCTL_H__
#define __SUNXI_DISP_IOCTL_H__

#define __bool signed char

/* for tracking the ioctls API/ABI */
#define SUNXI_DISP_VERSION_MAJOR 1
#define SUNXI_DISP_VERSION_MINOR 0

#define SUNXI_DISP_VERSION ((SUNXI_DISP_VERSION_MAJOR << 16) | SUNXI_DISP_VERSION_MINOR)
#define SUNXI_DISP_VERSION_MAJOR_GET(x) (((x) >> 16) & 0x7FFF)
#define SUNXI_DISP_VERSION_MINOR_GET(x) ((x) & 0xFFFF)

typedef struct {
	__u8 alpha;
	__u8 red;
	__u8 green;
	__u8 blue;
} __disp_color_t;
typedef struct {
	__s32 x;
	__s32 y;
	__u32 width;
	__u32 height;
} __disp_rect_t;
typedef struct {
	__u32 width;
	__u32 height;
} __disp_rectsz_t;
typedef struct {
	__s32 x;
	__s32 y;
} __disp_pos_t;

typedef enum {
	DISP_FORMAT_1BPP = 0x0,
	DISP_FORMAT_2BPP = 0x1,
	DISP_FORMAT_4BPP = 0x2,
	DISP_FORMAT_8BPP = 0x3,
	DISP_FORMAT_RGB655 = 0x4,
	DISP_FORMAT_RGB565 = 0x5,
	DISP_FORMAT_RGB556 = 0x6,
	DISP_FORMAT_ARGB1555 = 0x7,
	DISP_FORMAT_RGBA5551 = 0x8,
	DISP_FORMAT_ARGB888 = 0x9, /* alpha padding to 0xff */
	DISP_FORMAT_ARGB8888 = 0xa,
	DISP_FORMAT_RGB888 = 0xb,
	DISP_FORMAT_ARGB4444 = 0xc,

	DISP_FORMAT_YUV444 = 0x10,
	DISP_FORMAT_YUV422 = 0x11,
	DISP_FORMAT_YUV420 = 0x12,
	DISP_FORMAT_YUV411 = 0x13,
	DISP_FORMAT_CSIRGB = 0x14,
} __disp_pixel_fmt_t;

typedef enum {
	/* interleaved,1 address */
	DISP_MOD_INTERLEAVED = 0x1,
	/*
	 * No macroblock plane mode, 3 address, RGB/YUV each channel were stored
	 */
	DISP_MOD_NON_MB_PLANAR = 0x0,
	/* No macroblock UV packaged mode, 2 address, Y and UV were stored */
	DISP_MOD_NON_MB_UV_COMBINED = 0x2,
	/* Macroblock plane mode, 3 address,RGB/YUV each channel were stored */
	DISP_MOD_MB_PLANAR = 0x4,
	/* Macroblock UV packaged mode, 2 address, Y and UV were stored */
	DISP_MOD_MB_UV_COMBINED = 0x6,
} __disp_pixel_mod_t;

typedef enum {
	/* for interleave argb8888 */
	DISP_SEQ_ARGB = 0x0,	/* A at a high level */
	DISP_SEQ_BGRA = 0x2,

	/* for interleaved yuv422 */
	DISP_SEQ_UYVY = 0x3,
	DISP_SEQ_YUYV = 0x4,
	DISP_SEQ_VYUY = 0x5,
	DISP_SEQ_YVYU = 0x6,

	/* for interleaved yuv444 */
	DISP_SEQ_AYUV = 0x7,
	DISP_SEQ_VUYA = 0x8,

	/* for uv_combined yuv420 */
	DISP_SEQ_UVUV = 0x9,
	DISP_SEQ_VUVU = 0xa,

	/* for 16bpp rgb */
	DISP_SEQ_P10 = 0xd,	/* p1 high */
	DISP_SEQ_P01 = 0xe,	/* p0 high */

	/* for planar format or 8bpp rgb */
	DISP_SEQ_P3210 = 0xf,	/* p3 high */
	DISP_SEQ_P0123 = 0x10,	/* p0 high */

	/* for 4bpp rgb */
	DISP_SEQ_P76543210 = 0x11,
	DISP_SEQ_P67452301 = 0x12,
	DISP_SEQ_P10325476 = 0x13,
	DISP_SEQ_P01234567 = 0x14,

	/* for 2bpp rgb */
	/* 15,14,13,12,11,10,9,8,7,6,5,4,3,2,1,0 */
	DISP_SEQ_2BPP_BIG_BIG = 0x15,
	/* 12,13,14,15,8,9,10,11,4,5,6,7,0,1,2,3 */
	DISP_SEQ_2BPP_BIG_LITTER = 0x16,
	/* 3,2,1,0,7,6,5,4,11,10,9,8,15,14,13,12 */
	DISP_SEQ_2BPP_LITTER_BIG = 0x17,
	/* 0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15 */
	DISP_SEQ_2BPP_LITTER_LITTER = 0x18,

	/* for 1bpp rgb */
	/*
	 * 31,30,29,28,27,26,25,24,23,22,21,20,19,18,17,16,
	 * 15,14,13,12,11,10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0
	 */
	DISP_SEQ_1BPP_BIG_BIG = 0x19,
	/*
	 * 24,25,26,27,28,29,30,31,16,17,18,19,20,21,22,23,
	 *  8, 9,10,11,12,13,14,15, 0, 1, 2, 3, 4, 5, 6, 7
	 */
	DISP_SEQ_1BPP_BIG_LITTER = 0x1a,
	/*
	 *  7, 6, 5, 4, 3, 2, 1, 0,15,14,13,12,11,10, 9, 8,
	 * 23,22,21,20,19,18,17,16,31,30,29,28,27,26,25,24
	 */
	DISP_SEQ_1BPP_LITTER_BIG = 0x1b,
	/*
	 *  0, 1, 2, 3, 4, 5, 6, 7, 8, 9,10,11,12,13,14,15,
	 * 16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31
	 */
	DISP_SEQ_1BPP_LITTER_LITTER = 0x1c,
} __disp_pixel_seq_t;

typedef enum {
	DISP_3D_SRC_MODE_TB = 0x0, /* top bottom */
	DISP_3D_SRC_MODE_FP = 0x1, /* frame packing */
	DISP_3D_SRC_MODE_SSF = 0x2, /* side by side full */
	DISP_3D_SRC_MODE_SSH = 0x3, /* side by side half */
	DISP_3D_SRC_MODE_LI = 0x4, /* line interleaved */
} __disp_3d_src_mode_t;

typedef enum {
	DISP_3D_OUT_MODE_CI_1 = 0x5, /* column interlaved 1 */
	DISP_3D_OUT_MODE_CI_2 = 0x6, /* column interlaved 2 */
	DISP_3D_OUT_MODE_CI_3 = 0x7, /* column interlaved 3 */
	DISP_3D_OUT_MODE_CI_4 = 0x8, /* column interlaved 4 */
	DISP_3D_OUT_MODE_LIRGB = 0x9, /* line interleaved rgb */

	DISP_3D_OUT_MODE_TB = 0x0, /* top bottom */
	DISP_3D_OUT_MODE_FP = 0x1, /* frame packing */
	DISP_3D_OUT_MODE_SSF = 0x2, /* side by side full */
	DISP_3D_OUT_MODE_SSH = 0x3, /* side by side half */
	DISP_3D_OUT_MODE_LI = 0x4, /* line interleaved */
	DISP_3D_OUT_MODE_FA = 0xa, /* field alternative */
} __disp_3d_out_mode_t;

typedef enum {
	DISP_BT601 = 0,
	DISP_BT709 = 1,
	DISP_YCC = 2,
	DISP_VXYCC = 3,
} __disp_cs_mode_t;

typedef enum {
	DISP_COLOR_RANGE_16_255 = 0,
	DISP_COLOR_RANGE_0_255 = 1,
	DISP_COLOR_RANGE_16_235 = 2,
} __disp_color_range_t;

typedef enum {
	DISP_OUTPUT_TYPE_NONE = 0,
	DISP_OUTPUT_TYPE_LCD = 1,
	DISP_OUTPUT_TYPE_TV = 2,
	DISP_OUTPUT_TYPE_HDMI = 4,
	DISP_OUTPUT_TYPE_VGA = 8,
} __disp_output_type_t;

typedef enum {
	DISP_TV_NONE = 0,
	DISP_TV_CVBS = 1,
	DISP_TV_YPBPR = 2,
	DISP_TV_SVIDEO = 4,
} __disp_tv_output_t;

typedef enum {
	DISP_TV_MOD_480I = 0,
	DISP_TV_MOD_576I = 1,
	DISP_TV_MOD_480P = 2,
	DISP_TV_MOD_576P = 3,
	DISP_TV_MOD_720P_50HZ = 4,
	DISP_TV_MOD_720P_60HZ = 5,
	DISP_TV_MOD_1080I_50HZ = 6,
	DISP_TV_MOD_1080I_60HZ = 7,
	DISP_TV_MOD_1080P_24HZ = 8,
	DISP_TV_MOD_1080P_50HZ = 9,
	DISP_TV_MOD_1080P_60HZ = 0xa,
	DISP_TV_MOD_1080P_24HZ_3D_FP = 0x17,
	DISP_TV_MOD_720P_50HZ_3D_FP = 0x18,
	DISP_TV_MOD_720P_60HZ_3D_FP = 0x19,
	DISP_TV_MOD_PAL = 0xb,
	DISP_TV_MOD_PAL_SVIDEO = 0xc,
	DISP_TV_MOD_NTSC = 0xe,
	DISP_TV_MOD_NTSC_SVIDEO = 0xf,
	DISP_TV_MOD_PAL_M = 0x11,
	DISP_TV_MOD_PAL_M_SVIDEO = 0x12,
	DISP_TV_MOD_PAL_NC = 0x14,
	DISP_TV_MOD_PAL_NC_SVIDEO = 0x15,

	DISP_TV_MOD_H1360_V768_60HZ = 0x1a,
	DISP_TV_MOD_H1280_V1024_60HZ = 0x1b,

	DISP_TV_MODE_NUM = 0x1c,

	/* Reserved, do not use in fex files */
	DISP_TV_MODE_EDID = 0xff
} __disp_tv_mode_t;

typedef enum {
	DISP_TV_DAC_SRC_COMPOSITE = 0,
	DISP_TV_DAC_SRC_LUMA = 1,
	DISP_TV_DAC_SRC_CHROMA = 2,
	DISP_TV_DAC_SRC_Y = 4,
	DISP_TV_DAC_SRC_PB = 5,
	DISP_TV_DAC_SRC_PR = 6,
	DISP_TV_DAC_SRC_NONE = 7,
} __disp_tv_dac_source;

typedef enum {
	DISP_VGA_H1680_V1050 = 0,
	DISP_VGA_H1440_V900 = 1,
	DISP_VGA_H1360_V768 = 2,
	DISP_VGA_H1280_V1024 = 3,
	DISP_VGA_H1024_V768 = 4,
	DISP_VGA_H800_V600 = 5,
	DISP_VGA_H640_V480 = 6,
	DISP_VGA_H1440_V900_RB = 7, /* not support yet */
	DISP_VGA_H1680_V1050_RB = 8, /* not support yet */
	DISP_VGA_H1920_V1080_RB = 9,
	DISP_VGA_H1920_V1080 = 0xa,
	DISP_VGA_H1280_V720 = 0xb,
	DISP_VGA_MODE_NUM = 0xc,
} __disp_vga_mode_t;

typedef enum {
	DISP_LCDC_SRC_DE_CH1 = 0,
	DISP_LCDC_SRC_DE_CH2 = 1,
	DISP_LCDC_SRC_DMA = 2,
	DISP_LCDC_SRC_WHITE = 3,
	DISP_LCDC_SRC_BLACK = 4,
	DISP_LCDC_SRC_BLUT = 5,
} __disp_lcdc_src_t;

typedef enum {
	DISP_LAYER_WORK_MODE_NORMAL = 0, /* normal work mode */
	DISP_LAYER_WORK_MODE_PALETTE = 1, /* palette work mode */
	/* internal frame buffer work mode */
	DISP_LAYER_WORK_MODE_INTER_BUF = 2,
	DISP_LAYER_WORK_MODE_GAMMA = 3, /* gamma correction work mode */
	DISP_LAYER_WORK_MODE_SCALER = 4, /* scaler work mode */
} __disp_layer_work_mode_t;

typedef enum {
	DISP_VIDEO_NATUAL = 0,
	DISP_VIDEO_SOFT = 1,
	DISP_VIDEO_VERYSOFT = 2,
	DISP_VIDEO_SHARP = 3,
	DISP_VIDEO_VERYSHARP = 4
} __disp_video_smooth_t;

typedef enum {
	DISP_HWC_MOD_H32_V32_8BPP = 0,
	DISP_HWC_MOD_H64_V64_2BPP = 1,
	DISP_HWC_MOD_H64_V32_4BPP = 2,
	DISP_HWC_MOD_H32_V64_4BPP = 3,
} __disp_hwc_mode_t;

typedef enum {
	DISP_EXIT_MODE_CLEAN_ALL = 0,
	DISP_EXIT_MODE_CLEAN_PARTLY = 1, /* only clean interrupt temply */
} __disp_exit_mode_t;

typedef enum { /* only for debug!!! */
	DISP_REG_SCALER0 = 0,
	DISP_REG_SCALER1 = 1,
	DISP_REG_IMAGE0 = 2,
	DISP_REG_IMAGE1 = 3,
	DISP_REG_LCDC0 = 4,
	DISP_REG_LCDC1 = 5,
	DISP_REG_TVEC0 = 6,
	DISP_REG_TVEC1 = 7,
	DISP_REG_CCMU = 8,
	DISP_REG_PIOC = 9,
	DISP_REG_PWM = 10,
} __disp_reg_index_t;

typedef struct {
	/*
	 * The way these are treated today, these are physical addresses. Are
	 * there any actual userspace applications out there that use this?
	 * -- libv.
	 */
	/*
	 * the contents of the frame buffer address for rgb type only addr[0]
	 * valid
	 */
	__u32 addr[3];
	__disp_rectsz_t size; /* unit is pixel */
	__disp_pixel_fmt_t format;
	__disp_pixel_seq_t seq;
	__disp_pixel_mod_t mode;
	/*
	 * blue red color swap flag, FALSE:RGB; TRUE:BGR,only used in rgb format
	 */
	__bool br_swap;
	__disp_cs_mode_t cs_mode; /* color space */
	__bool b_trd_src; /* if 3d source, used for scaler mode layer */
	/* source 3d mode, used for scaler mode layer */
	__disp_3d_src_mode_t trd_mode;
	__u32 trd_right_addr[3]; /* used when in frame packing 3d mode */
} __disp_fb_t;

typedef struct {
	__disp_layer_work_mode_t mode; /* layer work mode */
	__bool b_from_screen;
	 /*
	  * layer pipe,0/1,if in scaler mode, scaler0 must be pipe0,
	  * scaler1 must be pipe1
	  */
	__u8 pipe;
	/*
	 * layer priority,can get layer prio,but never set layer prio.
	 * From bottom to top, priority from low to high
	 */
	__u8 prio;
	__bool alpha_en; /* layer global alpha enable */
	__u16 alpha_val; /* layer global alpha value */
	__bool ck_enable; /* layer color key enable */
	/*  framebuffer source window,only care x,y if is not scaler mode */
	__disp_rect_t src_win;
	__disp_rect_t scn_win; /* screen window */
	__disp_fb_t fb; /* framebuffer */
	__bool b_trd_out; /* if output 3d mode, used for scaler mode layer */
	/* output 3d mode, used for scaler mode layer */
	__disp_3d_out_mode_t out_trd_mode;
} __disp_layer_info_t;

typedef struct {
	__disp_color_t ck_max;
	__disp_color_t ck_min;
	/*
	 * 0/1:always match;
	 * 2:match if min<=color<=max;
	 * 3:match if color>max or color<min
	 */
	__u32 red_match_rule;
	__u32 green_match_rule;
	__u32 blue_match_rule;
} __disp_colorkey_t;

typedef struct {
	__s32 id;
	__u32 addr[3];
	__u32 addr_right[3]; /* used when in frame packing 3d mode */
	__bool interlace;
	__bool top_field_first;
	__u32 frame_rate; /*  *FRAME_RATE_BASE(now scheduled for 1000) */
	__u32 flag_addr; /* dit maf flag address */
	__u32 flag_stride; /* dit maf flag line stride */
	__bool maf_valid;
	__bool pre_frame_valid;
} __disp_video_fb_t;

typedef struct {
	__bool maf_enable;
	__bool pre_frame_enable;
} __disp_dit_info_t;

typedef struct {
	__disp_hwc_mode_t pat_mode;
	__u32 addr;
} __disp_hwc_pattern_t;

typedef struct {
	__disp_fb_t input_fb;
	__disp_rect_t source_regn;
	__disp_fb_t output_fb;
	// __disp_rect_t   out_regn;
} __disp_scaler_para_t;

typedef struct {
	__disp_fb_t fb;
	/* source region,only care x,y because of not scaler */
	__disp_rect_t src_win;
	__disp_rect_t scn_win; /*  sceen region */
} __disp_sprite_block_para_t;

typedef struct {
	/*
	 * used when the screen is not displaying on any output device
	 * (lcd/hdmi/vga/tv)
	 */
	__disp_rectsz_t screen_size;
	__disp_fb_t output_fb;
} __disp_capture_screen_para_t;

struct __disp_video_timing {
	__s32 VIC;
	__s32 PCLK;
	__s32 AVI_PR;

	__s32 INPUTX;
	__s32 INPUTY;
	__s32 HT;
	__s32 HBP;
	__s32 HFP;
	__s32 HPSW;
	__s32 VT;
	__s32 VBP;
	__s32 VFP;
	__s32 VPSW;

	__s32 I;	/* 0: Progressive 1: Interlaced */
	__s32 HSYNC;	/* 0: Negative 1: Positive */
	__s32 VSYNC;	/* 0: Negative 1: Positive */
};

typedef struct {
	__s32(*hdmi_wait_edid) (void);
	__s32(*Hdmi_open) (void);
	__s32(*Hdmi_close) (void);
	__s32(*hdmi_set_mode) (__disp_tv_mode_t mode);
	__s32(*hdmi_set_videomode) (const struct __disp_video_timing *mode);
	__s32(*hdmi_mode_support) (__disp_tv_mode_t mode);
	__s32(*hdmi_get_video_timing) (__disp_tv_mode_t mode,
				struct __disp_video_timing *video_timing);
	__s32(*hdmi_get_HPD_status) (void);
	__s32(*hdmi_set_pll) (__u32 pll, __u32 clk);
} __disp_hdmi_func;

typedef struct {
	__u32 lcd_x;
	__u32 lcd_y;
	__u32 lcd_dclk_freq;
	__u32 lcd_pwm_not_used;
	__u32 lcd_pwm_ch;
	__u32 lcd_pwm_freq;
	__u32 lcd_pwm_pol;
	__u32 lcd_srgb;
	__u32 lcd_swap;

	__u32 lcd_if; /* 0:hv(sync+de); 1:8080; 2:ttl; 3:lvds */

	__u32 lcd_uf;
	__u32 lcd_vt;
	__u32 lcd_ht;
	__u32 lcd_vbp;
	__u32 lcd_hbp;

	__u32 lcd_hv_if;
	__u32 lcd_hv_smode;
	__u32 lcd_hv_s888_if;
	__u32 lcd_hv_syuv_if;
	__u32 lcd_hv_vspw;
	__u32 lcd_hv_hspw;
	__u32 lcd_hv_lde_used;
	__u32 lcd_hv_lde_iovalue;

	__u32 lcd_ttl_stvh;
	__u32 lcd_ttl_stvdl;
	__u32 lcd_ttl_stvdp;
	__u32 lcd_ttl_ckvt;
	__u32 lcd_ttl_ckvh;
	__u32 lcd_ttl_ckvd;
	__u32 lcd_ttl_oevt;
	__u32 lcd_ttl_oevh;
	__u32 lcd_ttl_oevd;
	__u32 lcd_ttl_sthh;
	__u32 lcd_ttl_sthd;
	__u32 lcd_ttl_oehh;
	__u32 lcd_ttl_oehd;
	__u32 lcd_ttl_revd;
	__u32 lcd_ttl_datarate;
	__u32 lcd_ttl_revsel;
	__u32 lcd_ttl_datainv_en;
	__u32 lcd_ttl_datainv_sel;

	__u32 lcd_lvds_ch; /*  0: single channel; 1:dual channel */
	__u32 lcd_lvds_mode; /*  0:NS mode; 1:JEIDA mode */
	__u32 lcd_lvds_bitwidth; /*  0:24bit; 1:18bit */
	__u32 lcd_lvds_io_cross; /*  0:normal; 1:pn cross */

	/*
	 * 0:18bit;
	 * 1:16bit mode0;
	 * 2:16bit mode1;
	 * 3:16bit mode2;
	 * 4:16bit mode3;
	 * 5:9bit;
	 * 6:8bit 256K;
	 * 7:8bit 65K
	 */
	__u32 lcd_cpu_if;
	__u32 lcd_cpu_da;
	__u32 lcd_frm;

	__u32 lcd_io_cfg0;
	__u32 lcd_io_cfg1;
	__u32 lcd_io_strength;

	__u32 lcd_gamma_correction_en;
	__u32 lcd_gamma_tbl[256];

	__u32 lcd_hv_srgb_seq0;
	__u32 lcd_hv_srgb_seq1;
	__u32 lcd_hv_syuv_seq;
	__u32 lcd_hv_syuv_fdly;

	__u32 port_index;
	__u32 start_delay; /* not need to config for user */
	__u32 tcon_index; /* not need to config for user */
} __panel_para_t;

typedef struct {
	__u32 base_lcdc0;
	__u32 base_lcdc1;
	__u32 base_pioc;
	__u32 base_ccmu;
	__u32 base_pwm;
} __reg_bases_t;

typedef void (*LCD_FUNC) (__u32 sel);
typedef struct lcd_function {
	LCD_FUNC func;
	__u32 delay; /* ms */
} __lcd_function_t;

typedef struct lcd_flow {
	__lcd_function_t func[5];
	__u32 func_num;
} __lcd_flow_t;

typedef struct {
	void (*cfg_panel_info) (__panel_para_t *info);
	 __s32(*cfg_open_flow) (__u32 sel);
	 __s32(*cfg_close_flow) (__u32 sel);
	 __s32(*lcd_user_defined_func) (__u32 sel, __u32 para1, __u32 para2,
					__u32 para3);
} __lcd_panel_fun_t;

typedef struct {
	__bool enable;
	__u32 active_state;
	__u32 duty_ns;
	__u32 period_ns;
} __pwm_info_t;

typedef enum {
	FB_MODE_SCREEN0 = 0,
	FB_MODE_SCREEN1 = 1,
	/* two screen, top buffer for screen0, bottom buffer for screen1 */
	FB_MODE_DUAL_SAME_SCREEN_TB = 2,
	/* two screen, they have same contents; */
	FB_MODE_DUAL_DIFF_SCREEN_SAME_CONTENTS = 3,
} __fb_mode_t;

typedef struct {
	__fb_mode_t fb_mode;
	__disp_layer_work_mode_t mode;
	__u32 buffer_num;
	__u32 width;
	__u32 height;

	__u32 output_width; /* used when scaler mode */
	__u32 output_height; /* used when scaler mode */

	/* used when FB_MODE_DUAL_DIFF_SCREEN_SAME_CONTENTS */
	__u32 primary_screen_id;
	__u32 aux_output_width;
	__u32 aux_output_height;

	/* maybe not used anymore */
	__u32 line_length; /* in byte unit */
	__u32 smem_len;
	__u32 ch1_offset; /* use when PLANAR or UV_COMBINED mode */
	__u32 ch2_offset; /* use when PLANAR mode */
} __disp_fb_create_para_t;

typedef enum {
	DISP_INIT_MODE_SCREEN0 = 0, /* fb0 for screen0 */
	DISP_INIT_MODE_SCREEN1 = 1, /* fb0 for screen1 */
	/* fb0 for screen0 and fb1 for screen1 */
	DISP_INIT_MODE_TWO_DIFF_SCREEN = 2,
	/* fb0(up buffer for screen0, down buffer for screen1) */
	DISP_INIT_MODE_TWO_SAME_SCREEN = 3,
	/*
	 * fb0 for two different screen(screen0 layer is normal layer,
	 * screen1 layer is scaler layer);
	 */
	DISP_INIT_MODE_TWO_DIFF_SCREEN_SAME_CONTENTS = 4,
} __disp_init_mode_t;

typedef struct {
	__bool b_init;
	/*
	 * 0:single screen0(fb0);
	 * 1:single screen1(fb0);
	 * 2:dual diff screen(fb0, fb1);
	 * 3:dual same screen(fb0 up and down);
	 * 4:dual diff screen same contents(fb0)
	 */
	__disp_init_mode_t disp_mode;

	/* for screen0 and screen1 */
	__disp_output_type_t output_type[2];
	__disp_tv_mode_t tv_mode[2];
	__disp_vga_mode_t vga_mode[2];

	/* for fb0 and fb1 */
	__u32 buffer_num[2];
	__bool scaler_mode[2];
	__disp_pixel_fmt_t format[2];
	__disp_pixel_seq_t seq[2];
	__bool br_swap[2];
} __disp_init_t;

typedef enum tag_DISP_CMD {
	/* ----disp global---- */
	DISP_CMD_VERSION = 0x00,
	DISP_CMD_RESERVE1 = 0x01,
	/* fail when the value is 0x02 in linux,why??? */
	DISP_CMD_SET_BKCOLOR = 0x3f,
	DISP_CMD_GET_BKCOLOR = 0x03,
	DISP_CMD_SET_COLORKEY = 0x04,
	DISP_CMD_GET_COLORKEY = 0x05,
	DISP_CMD_SET_PALETTE_TBL = 0x06,
	DISP_CMD_GET_PALETTE_TBL = 0x07,
	DISP_CMD_SCN_GET_WIDTH = 0x08,
	DISP_CMD_SCN_GET_HEIGHT = 0x09,
	DISP_CMD_GET_OUTPUT_TYPE = 0x0a,
	DISP_CMD_SET_EXIT_MODE = 0x0c,
	DISP_CMD_SET_GAMMA_TABLE = 0x0d,
	DISP_CMD_GAMMA_CORRECTION_ON = 0x0e,
	DISP_CMD_GAMMA_CORRECTION_OFF = 0x0f,
	DISP_CMD_START_CMD_CACHE = 0x10,
	DISP_CMD_EXECUTE_CMD_AND_STOP_CACHE = 0x11,
	DISP_CMD_SET_BRIGHT = 0x12,
	DISP_CMD_SET_CONTRAST = 0x13,
	DISP_CMD_SET_SATURATION = 0x14,
	DISP_CMD_GET_BRIGHT = 0x16,
	DISP_CMD_GET_CONTRAST = 0x17,
	DISP_CMD_GET_SATURATION = 0x18,
	DISP_CMD_ENHANCE_ON = 0x1a,
	DISP_CMD_ENHANCE_OFF = 0x1b,
	DISP_CMD_GET_ENHANCE_EN = 0x1c,
	DISP_CMD_CLK_ON = 0x1d,
	DISP_CMD_CLK_OFF = 0x1e,
	/*
	 * when the screen is not used to display(lcd/tv/vga/hdmi) directly,
	 * maybe capture the screen and scaler to dram, or as a layer of
	 * another screen
	 */
	DISP_CMD_SET_SCREEN_SIZE = 0x1f,
	DISP_CMD_CAPTURE_SCREEN = 0x20,	/* caputre screen and scaler to dram */
	DISP_CMD_DE_FLICKER_ON = 0x21,
	DISP_CMD_DE_FLICKER_OFF = 0x22,
	DISP_CMD_SET_HUE = 0x23,
	DISP_CMD_GET_HUE = 0x24,
	DISP_CMD_DRC_OFF = 0x25,
	DISP_CMD_GET_DRC_EN = 0x26,
	DISP_CMD_DE_FLICKER_SET_WINDOW = 0x27,
	DISP_CMD_DRC_SET_WINDOW = 0x28,
	DISP_CMD_DRC_ON = 0x29,
	DISP_CMD_GET_DE_FLICKER_EN = 0x2a,

	/* ----layer---- */
	DISP_CMD_LAYER_REQUEST = 0x40,
	DISP_CMD_LAYER_RELEASE = 0x41,
	DISP_CMD_LAYER_OPEN = 0x42,
	DISP_CMD_LAYER_CLOSE = 0x43,
	DISP_CMD_LAYER_SET_FB = 0x44,
	DISP_CMD_LAYER_GET_FB = 0x45,
	DISP_CMD_LAYER_SET_SRC_WINDOW = 0x46,
	DISP_CMD_LAYER_GET_SRC_WINDOW = 0x47,
	DISP_CMD_LAYER_SET_SCN_WINDOW = 0x48,
	DISP_CMD_LAYER_GET_SCN_WINDOW = 0x49,
	DISP_CMD_LAYER_SET_PARA = 0x4a,
	DISP_CMD_LAYER_GET_PARA = 0x4b,
	DISP_CMD_LAYER_ALPHA_ON = 0x4c,
	DISP_CMD_LAYER_ALPHA_OFF = 0x4d,
	DISP_CMD_LAYER_GET_ALPHA_EN = 0x4e,
	DISP_CMD_LAYER_SET_ALPHA_VALUE = 0x4f,
	DISP_CMD_LAYER_GET_ALPHA_VALUE = 0x50,
	DISP_CMD_LAYER_CK_ON = 0x51,
	DISP_CMD_LAYER_CK_OFF = 0x52,
	DISP_CMD_LAYER_GET_CK_EN = 0x53,
	DISP_CMD_LAYER_SET_PIPE = 0x54,
	DISP_CMD_LAYER_GET_PIPE = 0x55,
	DISP_CMD_LAYER_TOP = 0x56,
	DISP_CMD_LAYER_BOTTOM = 0x57,
	DISP_CMD_LAYER_GET_PRIO = 0x58,
	DISP_CMD_LAYER_SET_SMOOTH = 0x59,
	DISP_CMD_LAYER_GET_SMOOTH = 0x5a,
	DISP_CMD_LAYER_SET_BRIGHT = 0x5b, /* brightness */
	DISP_CMD_LAYER_SET_CONTRAST = 0x5c, /* contrast */
	DISP_CMD_LAYER_SET_SATURATION = 0x5d, /* saturation */
	DISP_CMD_LAYER_SET_HUE = 0x5e, /* hue, chroma */
	DISP_CMD_LAYER_GET_BRIGHT = 0x5f,
	DISP_CMD_LAYER_GET_CONTRAST = 0x60,
	DISP_CMD_LAYER_GET_SATURATION = 0x61,
	DISP_CMD_LAYER_GET_HUE = 0x62,
	DISP_CMD_LAYER_ENHANCE_ON = 0x63,
	DISP_CMD_LAYER_ENHANCE_OFF = 0x64,
	DISP_CMD_LAYER_GET_ENHANCE_EN = 0x65,
	DISP_CMD_LAYER_VPP_ON = 0x67,
	DISP_CMD_LAYER_VPP_OFF = 0x68,
	DISP_CMD_LAYER_GET_VPP_EN = 0x69,
	DISP_CMD_LAYER_SET_LUMA_SHARP_LEVEL = 0x6a,
	DISP_CMD_LAYER_GET_LUMA_SHARP_LEVEL = 0x6b,
	DISP_CMD_LAYER_SET_CHROMA_SHARP_LEVEL = 0x6c,
	DISP_CMD_LAYER_GET_CHROMA_SHARP_LEVEL = 0x6d,
	DISP_CMD_LAYER_SET_WHITE_EXTEN_LEVEL = 0x6e,
	DISP_CMD_LAYER_GET_WHITE_EXTEN_LEVEL = 0x6f,
	DISP_CMD_LAYER_SET_BLACK_EXTEN_LEVEL = 0x70,
	DISP_CMD_LAYER_GET_BLACK_EXTEN_LEVEL = 0x71,

	/* ----scaler---- */
	DISP_CMD_SCALER_REQUEST = 0x80,
	DISP_CMD_SCALER_RELEASE = 0x81,
	DISP_CMD_SCALER_EXECUTE = 0x82,

	/* ----hwc---- */
	DISP_CMD_HWC_OPEN = 0xc0,
	DISP_CMD_HWC_CLOSE = 0xc1,
	DISP_CMD_HWC_SET_POS = 0xc2,
	DISP_CMD_HWC_GET_POS = 0xc3,
	DISP_CMD_HWC_SET_FB = 0xc4,
	DISP_CMD_HWC_SET_PALETTE_TABLE = 0xc5,

	/* ----video---- */
	DISP_CMD_VIDEO_START = 0x100,
	DISP_CMD_VIDEO_STOP = 0x101,
	DISP_CMD_VIDEO_SET_FB = 0x102,
	DISP_CMD_VIDEO_GET_FRAME_ID = 0x103,
	DISP_CMD_VIDEO_GET_DIT_INFO = 0x104,

	/* ----lcd---- */
	DISP_CMD_LCD_ON = 0x140,
	DISP_CMD_LCD_OFF = 0x141,
	DISP_CMD_LCD_SET_BRIGHTNESS = 0x142,
	DISP_CMD_LCD_GET_BRIGHTNESS = 0x143,
	DISP_CMD_LCD_CPUIF_XY_SWITCH = 0x146,
	DISP_CMD_LCD_CHECK_OPEN_FINISH = 0x14a,
	DISP_CMD_LCD_CHECK_CLOSE_FINISH = 0x14b,
	DISP_CMD_LCD_SET_SRC = 0x14c,
	DISP_CMD_LCD_USER_DEFINED_FUNC = 0x14d,

	/* ----tv---- */
	DISP_CMD_TV_ON = 0x180,
	DISP_CMD_TV_OFF = 0x181,
	DISP_CMD_TV_SET_MODE = 0x182,
	DISP_CMD_TV_GET_MODE = 0x183,
	DISP_CMD_TV_AUTOCHECK_ON = 0x184,
	DISP_CMD_TV_AUTOCHECK_OFF = 0x185,
	DISP_CMD_TV_GET_INTERFACE = 0x186,
	DISP_CMD_TV_SET_SRC = 0x187,
	DISP_CMD_TV_GET_DAC_STATUS = 0x188,
	DISP_CMD_TV_SET_DAC_SOURCE = 0x189,
	DISP_CMD_TV_GET_DAC_SOURCE = 0x18a,

	/* ----hdmi---- */
	DISP_CMD_HDMI_ON = 0x1c0,
	DISP_CMD_HDMI_OFF = 0x1c1,
	DISP_CMD_HDMI_SET_MODE = 0x1c2,
	DISP_CMD_HDMI_GET_MODE = 0x1c3,
	DISP_CMD_HDMI_SUPPORT_MODE = 0x1c4,
	DISP_CMD_HDMI_GET_HPD_STATUS = 0x1c5,
	DISP_CMD_HDMI_SET_SRC = 0x1c6,

	/* ----vga---- */
	DISP_CMD_VGA_ON = 0x200,
	DISP_CMD_VGA_OFF = 0x201,
	DISP_CMD_VGA_SET_MODE = 0x202,
	DISP_CMD_VGA_GET_MODE = 0x203,
	DISP_CMD_VGA_SET_SRC = 0x204,

	/* ----sprite---- */
	DISP_CMD_SPRITE_OPEN = 0x240,
	DISP_CMD_SPRITE_CLOSE = 0x241,
	DISP_CMD_SPRITE_SET_FORMAT = 0x242,
	DISP_CMD_SPRITE_GLOBAL_ALPHA_ENABLE = 0x243,
	DISP_CMD_SPRITE_GLOBAL_ALPHA_DISABLE = 0x244,
	DISP_CMD_SPRITE_GET_GLOBAL_ALPHA_ENABLE = 0x252,
	DISP_CMD_SPRITE_SET_GLOBAL_ALPHA_VALUE = 0x245,
	DISP_CMD_SPRITE_GET_GLOBAL_ALPHA_VALUE = 0x253,
	DISP_CMD_SPRITE_SET_ORDER = 0x246,
	DISP_CMD_SPRITE_GET_TOP_BLOCK = 0x250,
	DISP_CMD_SPRITE_GET_BOTTOM_BLOCK = 0x251,
	DISP_CMD_SPRITE_SET_PALETTE_TBL = 0x247,
	DISP_CMD_SPRITE_GET_BLOCK_NUM = 0x259,
	DISP_CMD_SPRITE_BLOCK_REQUEST = 0x248,
	DISP_CMD_SPRITE_BLOCK_RELEASE = 0x249,
	DISP_CMD_SPRITE_BLOCK_OPEN = 0x257,
	DISP_CMD_SPRITE_BLOCK_CLOSE = 0x258,
	DISP_CMD_SPRITE_BLOCK_SET_SOURCE_WINDOW = 0x25a,
	DISP_CMD_SPRITE_BLOCK_GET_SOURCE_WINDOW = 0x25b,
	DISP_CMD_SPRITE_BLOCK_SET_SCREEN_WINDOW = 0x24a,
	DISP_CMD_SPRITE_BLOCK_GET_SCREEN_WINDOW = 0x24c,
	DISP_CMD_SPRITE_BLOCK_SET_FB = 0x24b,
	DISP_CMD_SPRITE_BLOCK_GET_FB = 0x24d,
	DISP_CMD_SPRITE_BLOCK_SET_PARA = 0x25c,
	DISP_CMD_SPRITE_BLOCK_GET_PARA = 0x25d,
	DISP_CMD_SPRITE_BLOCK_SET_TOP = 0x24e,
	DISP_CMD_SPRITE_BLOCK_SET_BOTTOM = 0x24f,
	DISP_CMD_SPRITE_BLOCK_GET_PREV_BLOCK = 0x254,
	DISP_CMD_SPRITE_BLOCK_GET_NEXT_BLOCK = 0x255,
	DISP_CMD_SPRITE_BLOCK_GET_PRIO = 0x256,

	/* ----framebuffer---- */
	DISP_CMD_FB_REQUEST = 0x280,
	DISP_CMD_FB_RELEASE = 0x281,
	DISP_CMD_FB_GET_PARA = 0x282,
	DISP_CMD_GET_DISP_INIT_PARA = 0x283,

	/* ---for Displayer Test -------- */
	DISP_CMD_MEM_REQUEST = 0x2c0,
	DISP_CMD_MEM_RELASE = 0x2c1,
	DISP_CMD_MEM_GETADR = 0x2c2,
	DISP_CMD_MEM_SELIDX = 0x2c3,

	DISP_CMD_SUSPEND = 0x2d0,
	DISP_CMD_RESUME = 0x2d1,

	DISP_CMD_PRINT_REG = 0x2e0,

	/* ---pwm -------- */
	DISP_CMD_PWM_SET_PARA = 0x300,
	DISP_CMD_PWM_GET_PARA = 0x301,
} __disp_cmd_t;

#define GET_UMP_SECURE_ID_BUF1 _IOWR('m', 310, unsigned int)
#define GET_UMP_SECURE_ID_BUF2 _IOWR('m', 311, unsigned int)

#define GET_UMP_SECURE_ID_SUNXI_FB _IOWR('s', 100, unsigned int)

#define FBIOGET_LAYER_HDL_0 0x4700
#define FBIOGET_LAYER_HDL_1 0x4701

#define FBIO_CLOSE 0x4710
#define FBIO_OPEN 0x4711
#define FBIO_ALPHA_ON 0x4712
#define FBIO_ALPHA_OFF 0x4713
#define FBIOPUT_ALPHA_VALUE 0x4714

#define FBIO_DISPLAY_SCREEN0_ONLY 0x4720
#define FBIO_DISPLAY_SCREEN1_ONLY 0x4721
#define FBIO_DISPLAY_TWO_SAME_SCREEN_TB 0x4722
#define FBIO_DISPLAY_TWO_DIFF_SCREEN_SAME_CONTENTS 0x4723

#endif /* __SUNXI_DISP_IOCTL_H__ */
