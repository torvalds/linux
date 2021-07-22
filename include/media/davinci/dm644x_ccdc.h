/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Copyright (C) 2006-2009 Texas Instruments Inc
 */
#ifndef _DM644X_CCDC_H
#define _DM644X_CCDC_H
#include <media/davinci/ccdc_types.h>
#include <media/davinci/vpfe_types.h>

/* enum for No of pixel per line to be avg. in Black Clamping*/
enum ccdc_sample_length {
	CCDC_SAMPLE_1PIXELS,
	CCDC_SAMPLE_2PIXELS,
	CCDC_SAMPLE_4PIXELS,
	CCDC_SAMPLE_8PIXELS,
	CCDC_SAMPLE_16PIXELS
};

/* enum for No of lines in Black Clamping */
enum ccdc_sample_line {
	CCDC_SAMPLE_1LINES,
	CCDC_SAMPLE_2LINES,
	CCDC_SAMPLE_4LINES,
	CCDC_SAMPLE_8LINES,
	CCDC_SAMPLE_16LINES
};

/* enum for Alaw gamma width */
enum ccdc_gamma_width {
	CCDC_GAMMA_BITS_15_6,	/* use bits 15-6 for gamma */
	CCDC_GAMMA_BITS_14_5,
	CCDC_GAMMA_BITS_13_4,
	CCDC_GAMMA_BITS_12_3,
	CCDC_GAMMA_BITS_11_2,
	CCDC_GAMMA_BITS_10_1,
	CCDC_GAMMA_BITS_09_0	/* use bits 9-0 for gamma */
};

/* returns the highest bit used for the gamma */
static inline u8 ccdc_gamma_width_max_bit(enum ccdc_gamma_width width)
{
	return 15 - width;
}

enum ccdc_data_size {
	CCDC_DATA_16BITS,
	CCDC_DATA_15BITS,
	CCDC_DATA_14BITS,
	CCDC_DATA_13BITS,
	CCDC_DATA_12BITS,
	CCDC_DATA_11BITS,
	CCDC_DATA_10BITS,
	CCDC_DATA_8BITS
};

/* returns the highest bit used for this data size */
static inline u8 ccdc_data_size_max_bit(enum ccdc_data_size sz)
{
	return sz == CCDC_DATA_8BITS ? 7 : 15 - sz;
}

/* structure for ALaw */
struct ccdc_a_law {
	/* Enable/disable A-Law */
	unsigned char enable;
	/* Gamma Width Input */
	enum ccdc_gamma_width gamma_wd;
};

/* structure for Black Clamping */
struct ccdc_black_clamp {
	unsigned char enable;
	/* only if bClampEnable is TRUE */
	enum ccdc_sample_length sample_pixel;
	/* only if bClampEnable is TRUE */
	enum ccdc_sample_line sample_ln;
	/* only if bClampEnable is TRUE */
	unsigned short start_pixel;
	/* only if bClampEnable is TRUE */
	unsigned short sgain;
	/* only if bClampEnable is FALSE */
	unsigned short dc_sub;
};

/* structure for Black Level Compensation */
struct ccdc_black_compensation {
	/* Constant value to subtract from Red component */
	char r;
	/* Constant value to subtract from Gr component */
	char gr;
	/* Constant value to subtract from Blue component */
	char b;
	/* Constant value to subtract from Gb component */
	char gb;
};

/* Structure for CCDC configuration parameters for raw capture mode passed
 * by application
 */
struct ccdc_config_params_raw {
	/* data size value from 8 to 16 bits */
	enum ccdc_data_size data_sz;
	/* Structure for Optional A-Law */
	struct ccdc_a_law alaw;
	/* Structure for Optical Black Clamp */
	struct ccdc_black_clamp blk_clamp;
	/* Structure for Black Compensation */
	struct ccdc_black_compensation blk_comp;
};


#ifdef __KERNEL__
#include <linux/io.h>
/* Define to enable/disable video port */
#define FP_NUM_BYTES		4
/* Define for extra pixel/line and extra lines/frame */
#define NUM_EXTRAPIXELS		8
#define NUM_EXTRALINES		8

/* settings for commonly used video formats */
#define CCDC_WIN_PAL     {0, 0, 720, 576}
/* ntsc square pixel */
#define CCDC_WIN_VGA	{0, 0, (640 + NUM_EXTRAPIXELS), (480 + NUM_EXTRALINES)}

/* Structure for CCDC configuration parameters for raw capture mode */
struct ccdc_params_raw {
	/* pixel format */
	enum ccdc_pixfmt pix_fmt;
	/* progressive or interlaced frame */
	enum ccdc_frmfmt frm_fmt;
	/* video window */
	struct v4l2_rect win;
	/* field id polarity */
	enum vpfe_pin_pol fid_pol;
	/* vertical sync polarity */
	enum vpfe_pin_pol vd_pol;
	/* horizontal sync polarity */
	enum vpfe_pin_pol hd_pol;
	/* interleaved or separated fields */
	enum ccdc_buftype buf_type;
	/*
	 * enable to store the image in inverse
	 * order in memory(bottom to top)
	 */
	unsigned char image_invert_enable;
	/* configurable parameters */
	struct ccdc_config_params_raw config_params;
};

struct ccdc_params_ycbcr {
	/* pixel format */
	enum ccdc_pixfmt pix_fmt;
	/* progressive or interlaced frame */
	enum ccdc_frmfmt frm_fmt;
	/* video window */
	struct v4l2_rect win;
	/* field id polarity */
	enum vpfe_pin_pol fid_pol;
	/* vertical sync polarity */
	enum vpfe_pin_pol vd_pol;
	/* horizontal sync polarity */
	enum vpfe_pin_pol hd_pol;
	/* enable BT.656 embedded sync mode */
	int bt656_enable;
	/* cb:y:cr:y or y:cb:y:cr in memory */
	enum ccdc_pixorder pix_order;
	/* interleaved or separated fields  */
	enum ccdc_buftype buf_type;
};
#endif
#endif				/* _DM644X_CCDC_H */
