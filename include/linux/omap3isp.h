/*
 * omap3isp.h
 *
 * TI OMAP3 ISP - User-space API
 *
 * Copyright (C) 2010 Nokia Corporation
 * Copyright (C) 2009 Texas Instruments, Inc.
 *
 * Contacts: Laurent Pinchart <laurent.pinchart@ideasonboard.com>
 *	     Sakari Ailus <sakari.ailus@iki.fi>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA
 * 02110-1301 USA
 */

#ifndef OMAP3_ISP_USER_H
#define OMAP3_ISP_USER_H

#include <linux/types.h>
#include <linux/videodev2.h>

/*
 * Private IOCTLs
 *
 * VIDIOC_OMAP3ISP_CCDC_CFG: Set CCDC configuration
 * VIDIOC_OMAP3ISP_PRV_CFG: Set preview engine configuration
 * VIDIOC_OMAP3ISP_AEWB_CFG: Set AEWB module configuration
 * VIDIOC_OMAP3ISP_HIST_CFG: Set histogram module configuration
 * VIDIOC_OMAP3ISP_AF_CFG: Set auto-focus module configuration
 * VIDIOC_OMAP3ISP_STAT_REQ: Read statistics (AEWB/AF/histogram) data
 * VIDIOC_OMAP3ISP_STAT_EN: Enable/disable a statistics module
 */

#define VIDIOC_OMAP3ISP_CCDC_CFG \
	_IOWR('V', BASE_VIDIOC_PRIVATE + 1, struct omap3isp_ccdc_update_config)
#define VIDIOC_OMAP3ISP_PRV_CFG \
	_IOWR('V', BASE_VIDIOC_PRIVATE + 2, struct omap3isp_prev_update_config)
#define VIDIOC_OMAP3ISP_AEWB_CFG \
	_IOWR('V', BASE_VIDIOC_PRIVATE + 3, struct omap3isp_h3a_aewb_config)
#define VIDIOC_OMAP3ISP_HIST_CFG \
	_IOWR('V', BASE_VIDIOC_PRIVATE + 4, struct omap3isp_hist_config)
#define VIDIOC_OMAP3ISP_AF_CFG \
	_IOWR('V', BASE_VIDIOC_PRIVATE + 5, struct omap3isp_h3a_af_config)
#define VIDIOC_OMAP3ISP_STAT_REQ \
	_IOWR('V', BASE_VIDIOC_PRIVATE + 6, struct omap3isp_stat_data)
#define VIDIOC_OMAP3ISP_STAT_EN \
	_IOWR('V', BASE_VIDIOC_PRIVATE + 7, unsigned long)

/*
 * Events
 *
 * V4L2_EVENT_OMAP3ISP_AEWB: AEWB statistics data ready
 * V4L2_EVENT_OMAP3ISP_AF: AF statistics data ready
 * V4L2_EVENT_OMAP3ISP_HIST: Histogram statistics data ready
 */

#define V4L2_EVENT_OMAP3ISP_CLASS	(V4L2_EVENT_PRIVATE_START | 0x100)
#define V4L2_EVENT_OMAP3ISP_AEWB	(V4L2_EVENT_OMAP3ISP_CLASS | 0x1)
#define V4L2_EVENT_OMAP3ISP_AF		(V4L2_EVENT_OMAP3ISP_CLASS | 0x2)
#define V4L2_EVENT_OMAP3ISP_HIST	(V4L2_EVENT_OMAP3ISP_CLASS | 0x3)

struct omap3isp_stat_event_status {
	__u32 frame_number;
	__u16 config_counter;
	__u8 buf_err;
};

/* AE/AWB related structures and flags*/

/* H3A Range Constants */
#define OMAP3ISP_AEWB_MAX_SATURATION_LIM	1023
#define OMAP3ISP_AEWB_MIN_WIN_H			2
#define OMAP3ISP_AEWB_MAX_WIN_H			256
#define OMAP3ISP_AEWB_MIN_WIN_W			6
#define OMAP3ISP_AEWB_MAX_WIN_W			256
#define OMAP3ISP_AEWB_MIN_WINVC			1
#define OMAP3ISP_AEWB_MIN_WINHC			1
#define OMAP3ISP_AEWB_MAX_WINVC			128
#define OMAP3ISP_AEWB_MAX_WINHC			36
#define OMAP3ISP_AEWB_MAX_WINSTART		4095
#define OMAP3ISP_AEWB_MIN_SUB_INC		2
#define OMAP3ISP_AEWB_MAX_SUB_INC		32
#define OMAP3ISP_AEWB_MAX_BUF_SIZE		83600

#define OMAP3ISP_AF_IIRSH_MIN			0
#define OMAP3ISP_AF_IIRSH_MAX			4095
#define OMAP3ISP_AF_PAXEL_HORIZONTAL_COUNT_MIN	1
#define OMAP3ISP_AF_PAXEL_HORIZONTAL_COUNT_MAX	36
#define OMAP3ISP_AF_PAXEL_VERTICAL_COUNT_MIN	1
#define OMAP3ISP_AF_PAXEL_VERTICAL_COUNT_MAX	128
#define OMAP3ISP_AF_PAXEL_INCREMENT_MIN		2
#define OMAP3ISP_AF_PAXEL_INCREMENT_MAX		32
#define OMAP3ISP_AF_PAXEL_HEIGHT_MIN		2
#define OMAP3ISP_AF_PAXEL_HEIGHT_MAX		256
#define OMAP3ISP_AF_PAXEL_WIDTH_MIN		16
#define OMAP3ISP_AF_PAXEL_WIDTH_MAX		256
#define OMAP3ISP_AF_PAXEL_HZSTART_MIN		1
#define OMAP3ISP_AF_PAXEL_HZSTART_MAX		4095
#define OMAP3ISP_AF_PAXEL_VTSTART_MIN		0
#define OMAP3ISP_AF_PAXEL_VTSTART_MAX		4095
#define OMAP3ISP_AF_THRESHOLD_MAX		255
#define OMAP3ISP_AF_COEF_MAX			4095
#define OMAP3ISP_AF_PAXEL_SIZE			48
#define OMAP3ISP_AF_MAX_BUF_SIZE		221184

/**
 * struct omap3isp_h3a_aewb_config - AE AWB configuration reset values
 * saturation_limit: Saturation limit.
 * @win_height: Window Height. Range 2 - 256, even values only.
 * @win_width: Window Width. Range 6 - 256, even values only.
 * @ver_win_count: Vertical Window Count. Range 1 - 128.
 * @hor_win_count: Horizontal Window Count. Range 1 - 36.
 * @ver_win_start: Vertical Window Start. Range 0 - 4095.
 * @hor_win_start: Horizontal Window Start. Range 0 - 4095.
 * @blk_ver_win_start: Black Vertical Windows Start. Range 0 - 4095.
 * @blk_win_height: Black Window Height. Range 2 - 256, even values only.
 * @subsample_ver_inc: Subsample Vertical points increment Range 2 - 32, even
 *                     values only.
 * @subsample_hor_inc: Subsample Horizontal points increment Range 2 - 32, even
 *                     values only.
 * @alaw_enable: AEW ALAW EN flag.
 */
struct omap3isp_h3a_aewb_config {
	/*
	 * Common fields.
	 * They should be the first ones and must be in the same order as in
	 * ispstat_generic_config struct.
	 */
	__u32 buf_size;
	__u16 config_counter;

	/* Private fields */
	__u16 saturation_limit;
	__u16 win_height;
	__u16 win_width;
	__u16 ver_win_count;
	__u16 hor_win_count;
	__u16 ver_win_start;
	__u16 hor_win_start;
	__u16 blk_ver_win_start;
	__u16 blk_win_height;
	__u16 subsample_ver_inc;
	__u16 subsample_hor_inc;
	__u8 alaw_enable;
};

/**
 * struct omap3isp_stat_data - Statistic data sent to or received from user
 * @ts: Timestamp of returned framestats.
 * @buf: Pointer to pass to user.
 * @frame_number: Frame number of requested stats.
 * @cur_frame: Current frame number being processed.
 * @config_counter: Number of the configuration associated with the data.
 */
struct omap3isp_stat_data {
	struct timeval ts;
	void __user *buf;
	__u32 buf_size;
	__u16 frame_number;
	__u16 cur_frame;
	__u16 config_counter;
};


/* Histogram related structs */

/* Flags for number of bins */
#define OMAP3ISP_HIST_BINS_32		0
#define OMAP3ISP_HIST_BINS_64		1
#define OMAP3ISP_HIST_BINS_128		2
#define OMAP3ISP_HIST_BINS_256		3

/* Number of bins * 4 colors * 4-bytes word */
#define OMAP3ISP_HIST_MEM_SIZE_BINS(n)	((1 << ((n)+5))*4*4)

#define OMAP3ISP_HIST_MEM_SIZE		1024
#define OMAP3ISP_HIST_MIN_REGIONS	1
#define OMAP3ISP_HIST_MAX_REGIONS	4
#define OMAP3ISP_HIST_MAX_WB_GAIN	255
#define OMAP3ISP_HIST_MIN_WB_GAIN	0
#define OMAP3ISP_HIST_MAX_BIT_WIDTH	14
#define OMAP3ISP_HIST_MIN_BIT_WIDTH	8
#define OMAP3ISP_HIST_MAX_WG		4
#define OMAP3ISP_HIST_MAX_BUF_SIZE	4096

/* Source */
#define OMAP3ISP_HIST_SOURCE_CCDC	0
#define OMAP3ISP_HIST_SOURCE_MEM	1

/* CFA pattern */
#define OMAP3ISP_HIST_CFA_BAYER		0
#define OMAP3ISP_HIST_CFA_FOVEONX3	1

struct omap3isp_hist_region {
	__u16 h_start;
	__u16 h_end;
	__u16 v_start;
	__u16 v_end;
};

struct omap3isp_hist_config {
	/*
	 * Common fields.
	 * They should be the first ones and must be in the same order as in
	 * ispstat_generic_config struct.
	 */
	__u32 buf_size;
	__u16 config_counter;

	__u8 num_acc_frames;	/* Num of image frames to be processed and
				   accumulated for each histogram frame */
	__u16 hist_bins;	/* number of bins: 32, 64, 128, or 256 */
	__u8 cfa;		/* BAYER or FOVEON X3 */
	__u8 wg[OMAP3ISP_HIST_MAX_WG];	/* White Balance Gain */
	__u8 num_regions;	/* number of regions to be configured */
	struct omap3isp_hist_region region[OMAP3ISP_HIST_MAX_REGIONS];
};

/* Auto Focus related structs */

#define OMAP3ISP_AF_NUM_COEF		11

enum omap3isp_h3a_af_fvmode {
	OMAP3ISP_AF_MODE_SUMMED = 0,
	OMAP3ISP_AF_MODE_PEAK = 1
};

/* Red, Green, and blue pixel location in the AF windows */
enum omap3isp_h3a_af_rgbpos {
	OMAP3ISP_AF_GR_GB_BAYER = 0,	/* GR and GB as Bayer pattern */
	OMAP3ISP_AF_RG_GB_BAYER = 1,	/* RG and GB as Bayer pattern */
	OMAP3ISP_AF_GR_BG_BAYER = 2,	/* GR and BG as Bayer pattern */
	OMAP3ISP_AF_RG_BG_BAYER = 3,	/* RG and BG as Bayer pattern */
	OMAP3ISP_AF_GG_RB_CUSTOM = 4,	/* GG and RB as custom pattern */
	OMAP3ISP_AF_RB_GG_CUSTOM = 5	/* RB and GG as custom pattern */
};

/* Contains the information regarding the Horizontal Median Filter */
struct omap3isp_h3a_af_hmf {
	__u8 enable;	/* Status of Horizontal Median Filter */
	__u8 threshold;	/* Threshold Value for Horizontal Median Filter */
};

/* Contains the information regarding the IIR Filters */
struct omap3isp_h3a_af_iir {
	__u16 h_start;			/* IIR horizontal start */
	__u16 coeff_set0[OMAP3ISP_AF_NUM_COEF];	/* Filter coefficient, set 0 */
	__u16 coeff_set1[OMAP3ISP_AF_NUM_COEF];	/* Filter coefficient, set 1 */
};

/* Contains the information regarding the Paxels Structure in AF Engine */
struct omap3isp_h3a_af_paxel {
	__u16 h_start;	/* Horizontal Start Position */
	__u16 v_start;	/* Vertical Start Position */
	__u8 width;	/* Width of the Paxel */
	__u8 height;	/* Height of the Paxel */
	__u8 h_cnt;	/* Horizontal Count */
	__u8 v_cnt;	/* vertical Count */
	__u8 line_inc;	/* Line Increment */
};

/* Contains the parameters required for hardware set up of AF Engine */
struct omap3isp_h3a_af_config {
	/*
	 * Common fields.
	 * They should be the first ones and must be in the same order as in
	 * ispstat_generic_config struct.
	 */
	__u32 buf_size;
	__u16 config_counter;

	struct omap3isp_h3a_af_hmf hmf;		/* HMF configurations */
	struct omap3isp_h3a_af_iir iir;		/* IIR filter configurations */
	struct omap3isp_h3a_af_paxel paxel;	/* Paxel parameters */
	enum omap3isp_h3a_af_rgbpos rgb_pos;	/* RGB Positions */
	enum omap3isp_h3a_af_fvmode fvmode;	/* Accumulator mode */
	__u8 alaw_enable;			/* AF ALAW status */
};

/* ISP CCDC structs */

/* Abstraction layer CCDC configurations */
#define OMAP3ISP_CCDC_ALAW		(1 << 0)
#define OMAP3ISP_CCDC_LPF		(1 << 1)
#define OMAP3ISP_CCDC_BLCLAMP		(1 << 2)
#define OMAP3ISP_CCDC_BCOMP		(1 << 3)
#define OMAP3ISP_CCDC_FPC		(1 << 4)
#define OMAP3ISP_CCDC_CULL		(1 << 5)
#define OMAP3ISP_CCDC_CONFIG_LSC	(1 << 7)
#define OMAP3ISP_CCDC_TBL_LSC		(1 << 8)

#define OMAP3ISP_RGB_MAX		3

/* Enumeration constants for Alaw input width */
enum omap3isp_alaw_ipwidth {
	OMAP3ISP_ALAW_BIT12_3 = 0x3,
	OMAP3ISP_ALAW_BIT11_2 = 0x4,
	OMAP3ISP_ALAW_BIT10_1 = 0x5,
	OMAP3ISP_ALAW_BIT9_0 = 0x6
};

/**
 * struct omap3isp_ccdc_lsc_config - LSC configuration
 * @offset: Table Offset of the gain table.
 * @gain_mode_n: Vertical dimension of a paxel in LSC configuration.
 * @gain_mode_m: Horizontal dimension of a paxel in LSC configuration.
 * @gain_format: Gain table format.
 * @fmtsph: Start pixel horizontal from start of the HS sync pulse.
 * @fmtlnh: Number of pixels in horizontal direction to use for the data
 *          reformatter.
 * @fmtslv: Start line from start of VS sync pulse for the data reformatter.
 * @fmtlnv: Number of lines in vertical direction for the data reformatter.
 * @initial_x: X position, in pixels, of the first active pixel in reference
 *             to the first active paxel. Must be an even number.
 * @initial_y: Y position, in pixels, of the first active pixel in reference
 *             to the first active paxel. Must be an even number.
 * @size: Size of LSC gain table. Filled when loaded from userspace.
 */
struct omap3isp_ccdc_lsc_config {
	__u16 offset;
	__u8 gain_mode_n;
	__u8 gain_mode_m;
	__u8 gain_format;
	__u16 fmtsph;
	__u16 fmtlnh;
	__u16 fmtslv;
	__u16 fmtlnv;
	__u8 initial_x;
	__u8 initial_y;
	__u32 size;
};

/**
 * struct omap3isp_ccdc_bclamp - Optical & Digital black clamp subtract
 * @obgain: Optical black average gain.
 * @obstpixel: Start Pixel w.r.t. HS pulse in Optical black sample.
 * @oblines: Optical Black Sample lines.
 * @oblen: Optical Black Sample Length.
 * @dcsubval: Digital Black Clamp subtract value.
 */
struct omap3isp_ccdc_bclamp {
	__u8 obgain;
	__u8 obstpixel;
	__u8 oblines;
	__u8 oblen;
	__u16 dcsubval;
};

/**
 * struct omap3isp_ccdc_fpc - Faulty Pixels Correction
 * @fpnum: Number of faulty pixels to be corrected in the frame.
 * @fpcaddr: Memory address of the FPC Table
 */
struct omap3isp_ccdc_fpc {
	__u16 fpnum;
	__u32 fpcaddr;
};

/**
 * struct omap3isp_ccdc_blcomp - Black Level Compensation parameters
 * @b_mg: B/Mg pixels. 2's complement. -128 to +127.
 * @gb_g: Gb/G pixels. 2's complement. -128 to +127.
 * @gr_cy: Gr/Cy pixels. 2's complement. -128 to +127.
 * @r_ye: R/Ye pixels. 2's complement. -128 to +127.
 */
struct omap3isp_ccdc_blcomp {
	__u8 b_mg;
	__u8 gb_g;
	__u8 gr_cy;
	__u8 r_ye;
};

/**
 * omap3isp_ccdc_culling - Culling parameters
 * @v_pattern: Vertical culling pattern.
 * @h_odd: Horizontal Culling pattern for odd lines.
 * @h_even: Horizontal Culling pattern for even lines.
 */
struct omap3isp_ccdc_culling {
	__u8 v_pattern;
	__u16 h_odd;
	__u16 h_even;
};

/**
 * omap3isp_ccdc_update_config - CCDC configuration
 * @update: Specifies which CCDC registers should be updated.
 * @flag: Specifies which CCDC functions should be enabled.
 * @alawip: Enable/Disable A-Law compression.
 * @bclamp: Black clamp control register.
 * @blcomp: Black level compensation value for RGrGbB Pixels. 2's complement.
 * @fpc: Number of faulty pixels corrected in the frame, address of FPC table.
 * @cull: Cull control register.
 * @lsc: Pointer to LSC gain table.
 */
struct omap3isp_ccdc_update_config {
	__u16 update;
	__u16 flag;
	enum omap3isp_alaw_ipwidth alawip;
	struct omap3isp_ccdc_bclamp __user *bclamp;
	struct omap3isp_ccdc_blcomp __user *blcomp;
	struct omap3isp_ccdc_fpc __user *fpc;
	struct omap3isp_ccdc_lsc_config __user *lsc_cfg;
	struct omap3isp_ccdc_culling __user *cull;
	__u8 __user *lsc;
};

/* Preview configurations */
#define OMAP3ISP_PREV_LUMAENH		(1 << 0)
#define OMAP3ISP_PREV_INVALAW		(1 << 1)
#define OMAP3ISP_PREV_HRZ_MED		(1 << 2)
#define OMAP3ISP_PREV_CFA		(1 << 3)
#define OMAP3ISP_PREV_CHROMA_SUPP	(1 << 4)
#define OMAP3ISP_PREV_WB		(1 << 5)
#define OMAP3ISP_PREV_BLKADJ		(1 << 6)
#define OMAP3ISP_PREV_RGB2RGB		(1 << 7)
#define OMAP3ISP_PREV_COLOR_CONV	(1 << 8)
#define OMAP3ISP_PREV_YC_LIMIT		(1 << 9)
#define OMAP3ISP_PREV_DEFECT_COR	(1 << 10)
/* Bit 11 was OMAP3ISP_PREV_GAMMABYPASS, now merged with OMAP3ISP_PREV_GAMMA */
#define OMAP3ISP_PREV_DRK_FRM_CAPTURE	(1 << 12)
#define OMAP3ISP_PREV_DRK_FRM_SUBTRACT	(1 << 13)
#define OMAP3ISP_PREV_LENS_SHADING	(1 << 14)
#define OMAP3ISP_PREV_NF		(1 << 15)
#define OMAP3ISP_PREV_GAMMA		(1 << 16)

#define OMAP3ISP_PREV_NF_TBL_SIZE	64
#define OMAP3ISP_PREV_CFA_TBL_SIZE	576
#define OMAP3ISP_PREV_CFA_BLK_SIZE	(OMAP3ISP_PREV_CFA_TBL_SIZE / 4)
#define OMAP3ISP_PREV_GAMMA_TBL_SIZE	1024
#define OMAP3ISP_PREV_YENH_TBL_SIZE	128

#define OMAP3ISP_PREV_DETECT_CORRECT_CHANNELS	4

/**
 * struct omap3isp_prev_hmed - Horizontal Median Filter
 * @odddist: Distance between consecutive pixels of same color in the odd line.
 * @evendist: Distance between consecutive pixels of same color in the even
 *            line.
 * @thres: Horizontal median filter threshold.
 */
struct omap3isp_prev_hmed {
	__u8 odddist;
	__u8 evendist;
	__u8 thres;
};

/*
 * Enumeration for CFA Formats supported by preview
 */
enum omap3isp_cfa_fmt {
	OMAP3ISP_CFAFMT_BAYER,
	OMAP3ISP_CFAFMT_SONYVGA,
	OMAP3ISP_CFAFMT_RGBFOVEON,
	OMAP3ISP_CFAFMT_DNSPL,
	OMAP3ISP_CFAFMT_HONEYCOMB,
	OMAP3ISP_CFAFMT_RRGGBBFOVEON
};

/**
 * struct omap3isp_prev_cfa - CFA Interpolation
 * @format: CFA Format Enum value supported by preview.
 * @gradthrs_vert: CFA Gradient Threshold - Vertical.
 * @gradthrs_horz: CFA Gradient Threshold - Horizontal.
 * @table: Pointer to the CFA table.
 */
struct omap3isp_prev_cfa {
	enum omap3isp_cfa_fmt format;
	__u8 gradthrs_vert;
	__u8 gradthrs_horz;
	__u32 table[4][OMAP3ISP_PREV_CFA_BLK_SIZE];
};

/**
 * struct omap3isp_prev_csup - Chrominance Suppression
 * @gain: Gain.
 * @thres: Threshold.
 * @hypf_en: Flag to enable/disable the High Pass Filter.
 */
struct omap3isp_prev_csup {
	__u8 gain;
	__u8 thres;
	__u8 hypf_en;
};

/**
 * struct omap3isp_prev_wbal - White Balance
 * @dgain: Digital gain (U10Q8).
 * @coef3: White balance gain - COEF 3 (U8Q5).
 * @coef2: White balance gain - COEF 2 (U8Q5).
 * @coef1: White balance gain - COEF 1 (U8Q5).
 * @coef0: White balance gain - COEF 0 (U8Q5).
 */
struct omap3isp_prev_wbal {
	__u16 dgain;
	__u8 coef3;
	__u8 coef2;
	__u8 coef1;
	__u8 coef0;
};

/**
 * struct omap3isp_prev_blkadj - Black Level Adjustment
 * @red: Black level offset adjustment for Red in 2's complement format
 * @green: Black level offset adjustment for Green in 2's complement format
 * @blue: Black level offset adjustment for Blue in 2's complement format
 */
struct omap3isp_prev_blkadj {
	/*Black level offset adjustment for Red in 2's complement format */
	__u8 red;
	/*Black level offset adjustment for Green in 2's complement format */
	__u8 green;
	/* Black level offset adjustment for Blue in 2's complement format */
	__u8 blue;
};

/**
 * struct omap3isp_prev_rgbtorgb - RGB to RGB Blending
 * @matrix: Blending values(S12Q8 format)
 *              [RR] [GR] [BR]
 *              [RG] [GG] [BG]
 *              [RB] [GB] [BB]
 * @offset: Blending offset value for R,G,B in 2's complement integer format.
 */
struct omap3isp_prev_rgbtorgb {
	__u16 matrix[OMAP3ISP_RGB_MAX][OMAP3ISP_RGB_MAX];
	__u16 offset[OMAP3ISP_RGB_MAX];
};

/**
 * struct omap3isp_prev_csc - Color Space Conversion from RGB-YCbYCr
 * @matrix: Color space conversion coefficients(S10Q8)
 *              [CSCRY]  [CSCGY]  [CSCBY]
 *              [CSCRCB] [CSCGCB] [CSCBCB]
 *              [CSCRCR] [CSCGCR] [CSCBCR]
 * @offset: CSC offset values for Y offset, CB offset and CR offset respectively
 */
struct omap3isp_prev_csc {
	__u16 matrix[OMAP3ISP_RGB_MAX][OMAP3ISP_RGB_MAX];
	__s16 offset[OMAP3ISP_RGB_MAX];
};

/**
 * struct omap3isp_prev_yclimit - Y, C Value Limit
 * @minC: Minimum C value
 * @maxC: Maximum C value
 * @minY: Minimum Y value
 * @maxY: Maximum Y value
 */
struct omap3isp_prev_yclimit {
	__u8 minC;
	__u8 maxC;
	__u8 minY;
	__u8 maxY;
};

/**
 * struct omap3isp_prev_dcor - Defect correction
 * @couplet_mode_en: Flag to enable or disable the couplet dc Correction in NF
 * @detect_correct: Thresholds for correction bit 0:10 detect 16:25 correct
 */
struct omap3isp_prev_dcor {
	__u8 couplet_mode_en;
	__u32 detect_correct[OMAP3ISP_PREV_DETECT_CORRECT_CHANNELS];
};

/**
 * struct omap3isp_prev_nf - Noise Filter
 * @spread: Spread value to be used in Noise Filter
 * @table: Pointer to the Noise Filter table
 */
struct omap3isp_prev_nf {
	__u8 spread;
	__u32 table[OMAP3ISP_PREV_NF_TBL_SIZE];
};

/**
 * struct omap3isp_prev_gtables - Gamma correction tables
 * @red: Array for red gamma table.
 * @green: Array for green gamma table.
 * @blue: Array for blue gamma table.
 */
struct omap3isp_prev_gtables {
	__u32 red[OMAP3ISP_PREV_GAMMA_TBL_SIZE];
	__u32 green[OMAP3ISP_PREV_GAMMA_TBL_SIZE];
	__u32 blue[OMAP3ISP_PREV_GAMMA_TBL_SIZE];
};

/**
 * struct omap3isp_prev_luma - Luma enhancement
 * @table: Array for luma enhancement table.
 */
struct omap3isp_prev_luma {
	__u32 table[OMAP3ISP_PREV_YENH_TBL_SIZE];
};

/**
 * struct omap3isp_prev_update_config - Preview engine configuration (user)
 * @update: Specifies which ISP Preview registers should be updated.
 * @flag: Specifies which ISP Preview functions should be enabled.
 * @shading_shift: 3bit value of shift used in shading compensation.
 * @luma: Pointer to luma enhancement structure.
 * @hmed: Pointer to structure containing the odd and even distance.
 *        between the pixels in the image along with the filter threshold.
 * @cfa: Pointer to structure containing the CFA interpolation table, CFA.
 *       format in the image, vertical and horizontal gradient threshold.
 * @csup: Pointer to Structure for Chrominance Suppression coefficients.
 * @wbal: Pointer to structure for White Balance.
 * @blkadj: Pointer to structure for Black Adjustment.
 * @rgb2rgb: Pointer to structure for RGB to RGB Blending.
 * @csc: Pointer to structure for Color Space Conversion from RGB-YCbYCr.
 * @yclimit: Pointer to structure for Y, C Value Limit.
 * @dcor: Pointer to structure for defect correction.
 * @nf: Pointer to structure for Noise Filter
 * @gamma: Pointer to gamma structure.
 */
struct omap3isp_prev_update_config {
	__u32 update;
	__u32 flag;
	__u32 shading_shift;
	struct omap3isp_prev_luma __user *luma;
	struct omap3isp_prev_hmed __user *hmed;
	struct omap3isp_prev_cfa __user *cfa;
	struct omap3isp_prev_csup __user *csup;
	struct omap3isp_prev_wbal __user *wbal;
	struct omap3isp_prev_blkadj __user *blkadj;
	struct omap3isp_prev_rgbtorgb __user *rgb2rgb;
	struct omap3isp_prev_csc __user *csc;
	struct omap3isp_prev_yclimit __user *yclimit;
	struct omap3isp_prev_dcor __user *dcor;
	struct omap3isp_prev_nf __user *nf;
	struct omap3isp_prev_gtables __user *gamma;
};

#endif	/* OMAP3_ISP_USER_H */
