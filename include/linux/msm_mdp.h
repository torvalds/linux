/* include/linux/msm_mdp.h
 *
 * Copyright (C) 2007 Google Incorporated
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */
#ifndef _MSM_MDP_H_
#define _MSM_MDP_H_

#include <linux/types.h>

#define MSMFB_IOCTL_MAGIC 'm'
#define MSMFB_GRP_DISP          _IOW(MSMFB_IOCTL_MAGIC, 1, unsigned int)
#define MSMFB_BLIT              _IOW(MSMFB_IOCTL_MAGIC, 2, unsigned int)

enum {
	MDP_RGB_565,		/* RGB 565 planar */
	MDP_XRGB_8888,		/* RGB 888 padded */
	MDP_Y_CBCR_H2V2,	/* Y and CbCr, pseudo planar w/ Cb is in MSB */
	MDP_ARGB_8888,		/* ARGB 888 */
	MDP_RGB_888,		/* RGB 888 planar */
	MDP_Y_CRCB_H2V2,	/* Y and CrCb, pseudo planar w/ Cr is in MSB */
	MDP_YCRYCB_H2V1,	/* YCrYCb interleave */
	MDP_Y_CRCB_H2V1,	/* Y and CrCb, pseduo planar w/ Cr is in MSB */
	MDP_Y_CBCR_H2V1,	/* Y and CrCb, pseduo planar w/ Cr is in MSB */
	MDP_RGBA_8888,		/* ARGB 888 */
	MDP_BGRA_8888,		/* ABGR 888 */
	MDP_IMGTYPE_LIMIT	/* Non valid image type after this enum */
};

enum {
	PMEM_IMG,
	FB_IMG,
};

/* flag values */
#define MDP_ROT_NOP	0
#define MDP_FLIP_LR	0x1
#define MDP_FLIP_UD	0x2
#define MDP_ROT_90	0x4
#define MDP_ROT_180	(MDP_FLIP_UD|MDP_FLIP_LR)
#define MDP_ROT_270	(MDP_ROT_90|MDP_FLIP_UD|MDP_FLIP_LR)
#define MDP_DITHER	0x8
#define MDP_BLUR	0x10

#define MDP_TRANSP_NOP	0xffffffff
#define MDP_ALPHA_NOP	0xff

struct mdp_rect {
	u32 x, y, w, h;
};

struct mdp_img {
	u32 width, height, format, offset;
	int memory_id;		/* the file descriptor */
};

struct mdp_blit_req {
	struct mdp_img src;
	struct mdp_img dst;
	struct mdp_rect src_rect;
	struct mdp_rect dst_rect;
	u32 alpha, transp_mask, flags;
};

struct mdp_blit_req_list {
	u32 count;
	struct mdp_blit_req req[];
};

#endif /* _MSM_MDP_H_ */
