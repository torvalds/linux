/*
 * Copyright (C) 2013 - 2014 Texas Instruments, Inc.
 *
 * Benoit Parrot <bparrot@ti.com>
 * Lad, Prabhakar <prabhakar.csengg@gmail.com>
 *
 * This program is free software; you may redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#ifndef AM437X_VPFE_USER_H
#define AM437X_VPFE_USER_H

#include <linux/videodev2.h>

enum vpfe_ccdc_data_size {
	VPFE_CCDC_DATA_16BITS = 0,
	VPFE_CCDC_DATA_15BITS,
	VPFE_CCDC_DATA_14BITS,
	VPFE_CCDC_DATA_13BITS,
	VPFE_CCDC_DATA_12BITS,
	VPFE_CCDC_DATA_11BITS,
	VPFE_CCDC_DATA_10BITS,
	VPFE_CCDC_DATA_8BITS,
};

/* enum for No of pixel per line to be avg. in Black Clamping*/
enum vpfe_ccdc_sample_length {
	VPFE_CCDC_SAMPLE_1PIXELS = 0,
	VPFE_CCDC_SAMPLE_2PIXELS,
	VPFE_CCDC_SAMPLE_4PIXELS,
	VPFE_CCDC_SAMPLE_8PIXELS,
	VPFE_CCDC_SAMPLE_16PIXELS,
};

/* enum for No of lines in Black Clamping */
enum vpfe_ccdc_sample_line {
	VPFE_CCDC_SAMPLE_1LINES = 0,
	VPFE_CCDC_SAMPLE_2LINES,
	VPFE_CCDC_SAMPLE_4LINES,
	VPFE_CCDC_SAMPLE_8LINES,
	VPFE_CCDC_SAMPLE_16LINES,
};

/* enum for Alaw gamma width */
enum vpfe_ccdc_gamma_width {
	VPFE_CCDC_GAMMA_BITS_15_6 = 0,	/* use bits 15-6 for gamma */
	VPFE_CCDC_GAMMA_BITS_14_5,
	VPFE_CCDC_GAMMA_BITS_13_4,
	VPFE_CCDC_GAMMA_BITS_12_3,
	VPFE_CCDC_GAMMA_BITS_11_2,
	VPFE_CCDC_GAMMA_BITS_10_1,
	VPFE_CCDC_GAMMA_BITS_09_0,	/* use bits 9-0 for gamma */
};

/* structure for ALaw */
struct vpfe_ccdc_a_law {
	/* Enable/disable A-Law */
	unsigned char enable;
	/* Gamma Width Input */
	enum vpfe_ccdc_gamma_width gamma_wd;
};

/* structure for Black Clamping */
struct vpfe_ccdc_black_clamp {
	unsigned char enable;
	/* only if bClampEnable is TRUE */
	enum vpfe_ccdc_sample_length sample_pixel;
	/* only if bClampEnable is TRUE */
	enum vpfe_ccdc_sample_line sample_ln;
	/* only if bClampEnable is TRUE */
	unsigned short start_pixel;
	/* only if bClampEnable is TRUE */
	unsigned short sgain;
	/* only if bClampEnable is FALSE */
	unsigned short dc_sub;
};

/* structure for Black Level Compensation */
struct vpfe_ccdc_black_compensation {
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
struct vpfe_ccdc_config_params_raw {
	/* data size value from 8 to 16 bits */
	enum vpfe_ccdc_data_size data_sz;
	/* Structure for Optional A-Law */
	struct vpfe_ccdc_a_law alaw;
	/* Structure for Optical Black Clamp */
	struct vpfe_ccdc_black_clamp blk_clamp;
	/* Structure for Black Compensation */
	struct vpfe_ccdc_black_compensation blk_comp;
};

/*
 *  Private IOCTL
 * VIDIOC_AM437X_CCDC_CFG - Set CCDC configuration for raw capture
 * This is an experimental ioctl that will change in future kernels. So use
 * this ioctl with care !
 **/
#define VIDIOC_AM437X_CCDC_CFG \
	_IOW('V', BASE_VIDIOC_PRIVATE + 1, void *)

#endif		/* AM437X_VPFE_USER_H */
