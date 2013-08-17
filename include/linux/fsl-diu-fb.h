/*
 * Copyright 2008 Freescale Semiconductor, Inc. All Rights Reserved.
 *
 *  Freescale DIU Frame Buffer device driver
 *
 *  Authors: Hongjun Chen <hong-jun.chen@freescale.com>
 *           Paul Widmer <paul.widmer@freescale.com>
 *           Srikanth Srinivasan <srikanth.srinivasan@freescale.com>
 *           York Sun <yorksun@freescale.com>
 *
 *   Based on imxfb.c Copyright (C) 2004 S.Hauer, Pengutronix
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 *
 */

#ifndef __FSL_DIU_FB_H__
#define __FSL_DIU_FB_H__

#include <linux/types.h>

struct mfb_chroma_key {
	int enable;
	__u8  red_max;
	__u8  green_max;
	__u8  blue_max;
	__u8  red_min;
	__u8  green_min;
	__u8  blue_min;
};

struct aoi_display_offset {
	__s32 x_aoi_d;
	__s32 y_aoi_d;
};

#define MFB_SET_CHROMA_KEY	_IOW('M', 1, struct mfb_chroma_key)
#define MFB_SET_BRIGHTNESS	_IOW('M', 3, __u8)
#define MFB_SET_ALPHA		_IOW('M', 0, __u8)
#define MFB_GET_ALPHA		_IOR('M', 0, __u8)
#define MFB_SET_AOID		_IOW('M', 4, struct aoi_display_offset)
#define MFB_GET_AOID		_IOR('M', 4, struct aoi_display_offset)
#define MFB_SET_PIXFMT		_IOW('M', 8, __u32)
#define MFB_GET_PIXFMT		_IOR('M', 8, __u32)

/*
 * The original definitions of MFB_SET_PIXFMT and MFB_GET_PIXFMT used the
 * wrong value for 'size' field of the ioctl.  The current macros above use the
 * right size, but we still need to provide backwards compatibility, at least
 * for a while.
*/
#define MFB_SET_PIXFMT_OLD	0x80014d08
#define MFB_GET_PIXFMT_OLD	0x40014d08

#ifdef __KERNEL__

/*
 * These are the fields of area descriptor(in DDR memory) for every plane
 */
struct diu_ad {
	/* Word 0(32-bit) in DDR memory */
/* 	__u16 comp; */
/* 	__u16 pixel_s:2; */
/* 	__u16 pallete:1; */
/* 	__u16 red_c:2; */
/* 	__u16 green_c:2; */
/* 	__u16 blue_c:2; */
/* 	__u16 alpha_c:3; */
/* 	__u16 byte_f:1; */
/* 	__u16 res0:3; */

	__be32 pix_fmt; /* hard coding pixel format */

	/* Word 1(32-bit) in DDR memory */
	__le32 addr;

	/* Word 2(32-bit) in DDR memory */
/* 	__u32 delta_xs:11; */
/* 	__u32 res1:1; */
/* 	__u32 delta_ys:11; */
/* 	__u32 res2:1; */
/* 	__u32 g_alpha:8; */
	__le32 src_size_g_alpha;

	/* Word 3(32-bit) in DDR memory */
/* 	__u32 delta_xi:11; */
/* 	__u32 res3:5; */
/* 	__u32 delta_yi:11; */
/* 	__u32 res4:3; */
/* 	__u32 flip:2; */
	__le32 aoi_size;

	/* Word 4(32-bit) in DDR memory */
	/*__u32 offset_xi:11;
	__u32 res5:5;
	__u32 offset_yi:11;
	__u32 res6:5;
	*/
	__le32 offset_xyi;

	/* Word 5(32-bit) in DDR memory */
	/*__u32 offset_xd:11;
	__u32 res7:5;
	__u32 offset_yd:11;
	__u32 res8:5; */
	__le32 offset_xyd;


	/* Word 6(32-bit) in DDR memory */
	__u8 ckmax_r;
	__u8 ckmax_g;
	__u8 ckmax_b;
	__u8 res9;

	/* Word 7(32-bit) in DDR memory */
	__u8 ckmin_r;
	__u8 ckmin_g;
	__u8 ckmin_b;
	__u8 res10;
/* 	__u32 res10:8; */

	/* Word 8(32-bit) in DDR memory */
	__le32 next_ad;

	/* Word 9(32-bit) in DDR memory, just for 64-bit aligned */
	__u32 paddr;
} __attribute__ ((packed));

/* DIU register map */
struct diu {
	__be32 desc[3];
	__be32 gamma;
	__be32 pallete;
	__be32 cursor;
	__be32 curs_pos;
	__be32 diu_mode;
	__be32 bgnd;
	__be32 bgnd_wb;
	__be32 disp_size;
	__be32 wb_size;
	__be32 wb_mem_addr;
	__be32 hsyn_para;
	__be32 vsyn_para;
	__be32 syn_pol;
	__be32 thresholds;
	__be32 int_status;
	__be32 int_mask;
	__be32 colorbar[8];
	__be32 filling;
	__be32 plut;
} __attribute__ ((packed));

/*
 * Modes of operation of DIU.  The DIU supports five different modes, but
 * the driver only supports modes 0 and 1.
 */
#define MFB_MODE0	0	/* DIU off */
#define MFB_MODE1	1	/* All three planes output to display */

#endif /* __KERNEL__ */
#endif /* __FSL_DIU_FB_H__ */
