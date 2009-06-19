/*
 * Copyright (C) 2006-2009 Texas Instruments Inc
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
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

/* enum for Alaw gama width */
enum ccdc_gama_width {
	CCDC_GAMMA_BITS_15_6,
	CCDC_GAMMA_BITS_14_5,
	CCDC_GAMMA_BITS_13_4,
	CCDC_GAMMA_BITS_12_3,
	CCDC_GAMMA_BITS_11_2,
	CCDC_GAMMA_BITS_10_1,
	CCDC_GAMMA_BITS_09_0
};

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

/* structure for ALaw */
struct ccdc_a_law {
	/* Enable/disable A-Law */
	unsigned char enable;
	/* Gama Width Input */
	enum ccdc_gama_width gama_wd;
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

/* structure for fault pixel correction */
struct ccdc_fault_pixel {
	/* Enable or Disable fault pixel correction */
	unsigned char enable;
	/* Number of fault pixel */
	unsigned short fp_num;
	/* Address of fault pixel table */
	unsigned int fpc_table_addr;
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
	/* Structure for Fault Pixel Module Configuration */
	struct ccdc_fault_pixel fault_pxl;
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
	/* configurable paramaters */
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
