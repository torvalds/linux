/*	$NetBSD: edidreg.h,v 1.3 2011/03/30 18:49:56 jdc Exp $	*/
/*	$FreeBSD$	*/

/*-
 * Copyright (c) 2006 Itronix Inc.
 * All rights reserved.
 *
 * Written by Garrett D'Amore for Itronix Inc.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The name of Itronix Inc. may not be used to endorse
 *    or promote products derived from this software without specific
 *    prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY ITRONIX INC. ``AS IS'' AND ANY EXPRESS
 * OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL ITRONIX INC. BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE
 * GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */ 

#ifndef _DEV_VIDEOMODE_EDIDREG_H
#define _DEV_VIDEOMODE_EDIDREG_H

#define	EDID_OFFSET_SIGNATURE		0x00
#define	EDID_OFFSET_MANUFACTURER_ID	0x08
#define	EDID_OFFSET_PRODUCT_ID		0x0a
#define	EDID_OFFSET_SERIAL_NUMBER	0x0c
#define	EDID_OFFSET_MANUFACTURE_WEEK	0x10
#define	EDID_OFFSET_MANUFACTURE_YEAR	0x11
#define	EDID_OFFSET_VERSION		0x12
#define	EDID_OFFSET_REVISION		0x13
#define	EDID_OFFSET_VIDEO_INPUT		0x14
#define	EDID_OFFSET_MAX_HSIZE		0x15	/* in cm */
#define	EDID_OFFSET_MAX_VSIZE		0x16
#define	EDID_OFFSET_GAMMA		0x17
#define	EDID_OFFSET_FEATURE		0x18
#define	EDID_OFFSET_CHROMA		0x19
#define	EDID_OFFSET_EST_TIMING_1	0x23
#define	EDID_OFFSET_EST_TIMING_2	0x24
#define EDID_OFFSET_MFG_TIMING		0x25
#define	EDID_OFFSET_STD_TIMING		0x26
#define	EDID_OFFSET_DESC_BLOCK		0x36

#define	EDID_SIGNATURE		{ 0, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0 }

/* assume x is 16-bit value */
#define	EDID_VENDOR_ID(ptr)		((((ptr)[8]) << 8) + ptr[9])
#define	EDID_MANFID_0(x)		((((x) >> 10) & 0x1f) + '@')
#define	EDID_MANFID_1(x)		((((x) >> 5) & 0x1f) + '@')
#define	EDID_MANFID_2(x)		((((x) >> 0) & 0x1f) + '@')

/* relative to edid block */
#define	EDID_PRODUCT_ID(ptr)		(((ptr)[10]) | (((ptr)[11]) << 8))
#define	EDID_SERIAL_NUMBER(ptr)		(((ptr)[12] << 24) + \
					((ptr)[13] << 16) + \
					((ptr)[14] << 8) + \
					(ptr)[15])

/* relative to edid block */
#define	EDID_WEEK(ptr)			((ptr)[16])
#define	EDID_YEAR(ptr)			(((ptr)[17]) + 1990)

#define	EDID_VERSION(ptr)		((ptr)[18])
#define	EDID_REVISION(ptr)		((ptr)[19])

#define	EDID_VIDEO_INPUT(ptr)		((ptr)[20])
#define	EDID_VIDEO_INPUT_DIGITAL	0x80
/* if INPUT_BIT_DIGITAL set */
#define	EDID_VIDEO_INPUT_DFP1_COMPAT	0x01
/* if INPUT_BIT_DIGITAL not set */
#define	EDID_VIDEO_INPUT_BLANK_TO_BLACK	0x10
#define	EDID_VIDEO_INPUT_SEPARATE_SYNCS	0x08
#define	EDID_VIDEO_INPUT_COMPOSITE_SYNC	0x04
#define	EDID_VIDEO_INPUT_SYNC_ON_GRN	0x02
#define	EDID_VIDEO_INPUT_SERRATION	0x01
#define	EDID_VIDEO_INPUT_LEVEL(x)	(((x) & 0x60) >> 5)
/* meanings of level bits are as follows, I don't know names */
/* 0 = 0.7,0.3,  1 = 0.714,0.286, 2 = 1.0,0.4, 3 = 0.7,0.0 */

/* relative to edid block */
#define	EDID_MAX_HSIZE(ptr)		((ptr)[21])	/* cm */
#define	EDID_MAX_VSIZE(ptr)		((ptr)[22])	/* cm */
/* gamma is scaled by 100 (avoid fp), e.g. 213 == 2.13 */
#define	_GAMMA(x)			((x) == 0xff ? 100 : ((x) + 100))
#define	EDID_GAMMA(ptr)			_GAMMA(ptr[23])

#define	EDID_FEATURES(ptr)		((ptr)[24])
#define	EDID_FEATURES_STANDBY			0x80
#define	EDID_FEATURES_SUSPEND			0x40
#define	EDID_FEATURES_ACTIVE_OFF		0x20
#define	EDID_FEATURES_DISP_TYPE(x)		(((x) & 0x18) >> 3)
#define	EDID_FEATURES_DISP_TYPE_MONO		0
#define	EDID_FEATURES_DISP_TYPE_RGB		1
#define	EDID_FEATURES_DISP_TYPE_NON_RGB		2
#define	EDID_FEATURES_DISP_TYPE_UNDEFINED	3
#define	EDID_FEATURES_STD_COLOR			0x04
#define	EDID_FEATURES_PREFERRED_TIMING		0x02
#define	EDID_FEATURES_DEFAULT_GTF		0x01

/* chroma values 0.0 - 0.999 scaled as 0-999 */
#define	_CHLO(byt, shft)	(((byt) >> (shft)) & 0x3)
#define	_CHHI(byt)		((byt) << 2)
#define	_CHHILO(ptr, l, s, h)	(_CHLO((ptr)[l], s) | _CHHI((ptr)[h]))
#define	_CHROMA(ptr, l, s, h)	((_CHHILO(ptr, l, s, h) * 1000) / 1024)

#define	EDID_CHROMA_REDX(ptr)	(_CHROMA(ptr, 25, 6, 27))
#define	EDID_CHROMA_REDY(ptr)	(_CHROMA(ptr, 25, 4, 28))
#define	EDID_CHROMA_GREENX(ptr)	(_CHROMA(ptr, 25, 2, 29))
#define	EDID_CHROMA_GREENY(ptr)	(_CHROMA(ptr, 25, 0, 30))
#define	EDID_CHROMA_BLUEX(ptr)	(_CHROMA(ptr, 26, 6, 31))
#define	EDID_CHROMA_BLUEY(ptr)	(_CHROMA(ptr, 26, 4, 32))
#define	EDID_CHROMA_WHITEX(ptr)	(_CHROMA(ptr, 26, 2, 33))
#define	EDID_CHROMA_WHITEY(ptr)	(_CHROMA(ptr, 26, 0, 34))

/* relative to edid block */
#define	EDID_EST_TIMING(ptr)		(((ptr)[35] << 8) | (ptr)[36])
#define	EDID_EST_TIMING_720_400_70	0x8000	/* 720x400 @ 70Hz */
#define	EDID_EST_TIMING_720_400_88	0x4000	/* 720x400 @ 88Hz */
#define	EDID_EST_TIMING_640_480_60	0x2000	/* 640x480 @ 60Hz */
#define	EDID_EST_TIMING_640_480_67	0x1000	/* 640x480 @ 67Hz */
#define	EDID_EST_TIMING_640_480_72	0x0800	/* 640x480 @ 72Hz */
#define	EDID_EST_TIMING_640_480_75	0x0400	/* 640x480 @ 75Hz */
#define	EDID_EST_TIMING_800_600_56	0x0200	/* 800x600 @ 56Hz */
#define	EDID_EST_TIMING_800_600_60	0x0100	/* 800x600 @ 60Hz */
#define	EDID_EST_TIMING_800_600_72	0x0080	/* 800x600 @ 72Hz */
#define	EDID_EST_TIMING_800_600_75	0x0040	/* 800x600 @ 75Hz */
#define	EDID_EST_TIMING_832_624_75	0x0020	/* 832x624 @ 75Hz */
#define	EDID_EST_TIMING_1024_768_87I	0x0010	/* 1024x768i @ 87Hz */
#define	EDID_EST_TIMING_1024_768_60	0x0008	/* 1024x768 @ 60Hz */
#define	EDID_EST_TIMING_1024_768_70	0x0004	/* 1024x768 @ 70Hz */
#define	EDID_EST_TIMING_1024_768_75	0x0002	/* 1024x768 @ 75Hz */
#define	EDID_EST_TIMING_1280_1024_75	0x0001	/* 1280x1024 @ 75Hz */

/*
 * N.B.: ptr is relative to standard timing block - used for standard timing
 * descriptors as well as standard timings section of edid!
 */
#define	EDID_STD_TIMING_HRES(ptr)	((((ptr)[0]) * 8) + 248)
#define	EDID_STD_TIMING_VFREQ(ptr)	((((ptr)[1]) & 0x3f) + 60)
#define	EDID_STD_TIMING_RATIO(ptr)	((ptr)[1] & 0xc0)
#define	EDID_STD_TIMING_RATIO_16_10	0x00
#define	EDID_STD_TIMING_RATIO_4_3	0x40
#define	EDID_STD_TIMING_RATIO_5_4	0x80
#define	EDID_STD_TIMING_RATIO_16_9	0xc0

#define	EDID_STD_TIMING_SIZE		16
#define	EDID_STD_TIMING_COUNT		8

/*
 * N.B.: ptr is relative to descriptor block start
 */
#define	EDID_BLOCK_SIZE			18
#define	EDID_BLOCK_COUNT		4

/* detailed timing block.... what a mess */
#define	EDID_BLOCK_IS_DET_TIMING(ptr)		((ptr)[0] | (ptr)[1])

#define	EDID_DET_TIMING_DOT_CLOCK(ptr)	(((ptr)[0] | ((ptr)[1] << 8)) * 10000)
#define	_HACT_LO(ptr)			((ptr)[2])
#define	_HBLK_LO(ptr)			((ptr)[3])
#define	_HACT_HI(ptr)			(((ptr)[4] & 0xf0) << 4)
#define	_HBLK_HI(ptr)			(((ptr)[4] & 0x0f) << 8)
#define	EDID_DET_TIMING_HACTIVE(ptr)		(_HACT_LO(ptr) | _HACT_HI(ptr))
#define	EDID_DET_TIMING_HBLANK(ptr)		(_HBLK_LO(ptr) | _HBLK_HI(ptr))
#define	_VACT_LO(ptr)			((ptr)[5])
#define	_VBLK_LO(ptr)			((ptr)[6])
#define	_VACT_HI(ptr)			(((ptr)[7] & 0xf0) << 4)
#define	_VBLK_HI(ptr)			(((ptr)[7] & 0x0f) << 8)
#define	EDID_DET_TIMING_VACTIVE(ptr)		(_VACT_LO(ptr) | _VACT_HI(ptr))
#define	EDID_DET_TIMING_VBLANK(ptr)		(_VBLK_LO(ptr) | _VBLK_HI(ptr))
#define	_HOFF_LO(ptr)			((ptr)[8])
#define	_HWID_LO(ptr)			((ptr)[9])
#define	_VOFF_LO(ptr)			((ptr)[10] >> 4)
#define	_VWID_LO(ptr)			((ptr)[10] & 0xf)
#define	_HOFF_HI(ptr)			(((ptr)[11] & 0xc0) << 2)
#define	_HWID_HI(ptr)			(((ptr)[11] & 0x30) << 4)
#define	_VOFF_HI(ptr)			(((ptr)[11] & 0x0c) << 2)
#define	_VWID_HI(ptr)			(((ptr)[11] & 0x03) << 4)
#define	EDID_DET_TIMING_HSYNC_OFFSET(ptr)	(_HOFF_LO(ptr) | _HOFF_HI(ptr))
#define	EDID_DET_TIMING_HSYNC_WIDTH(ptr)	(_HWID_LO(ptr) | _HWID_HI(ptr))
#define	EDID_DET_TIMING_VSYNC_OFFSET(ptr)	(_VOFF_LO(ptr) | _VOFF_HI(ptr))
#define	EDID_DET_TIMING_VSYNC_WIDTH(ptr)	(_VWID_LO(ptr) | _VWID_HI(ptr))
#define	_HSZ_LO(ptr)			((ptr)[12])
#define	_VSZ_LO(ptr)			((ptr)[13])
#define	_HSZ_HI(ptr)			(((ptr)[14] & 0xf0) << 4)
#define	_VSZ_HI(ptr)			(((ptr)[14] & 0x0f) << 8)
#define	EDID_DET_TIMING_HSIZE(ptr)		(_HSZ_LO(ptr) | _HSZ_HI(ptr))
#define	EDID_DET_TIMING_VSIZE(ptr)		(_VSZ_LO(ptr) | _VSZ_HI(ptr))
#define	EDID_DET_TIMING_HBORDER(ptr)	((ptr)[15])
#define	EDID_DET_TIMING_VBORDER(ptr)	((ptr)[16])
#define	EDID_DET_TIMING_FLAGS(ptr)	((ptr)[17])
#define	EDID_DET_TIMING_FLAG_INTERLACE		0x80
#define	EDID_DET_TIMING_FLAG_STEREO		0x60	/* stereo or not */
#define	EDID_DET_TIMING_FLAG_SYNC_SEPARATE	0x18
#define	EDID_DET_TIMING_FLAG_VSYNC_POSITIVE	0x04
#define	EDID_DET_TIMING_FLAG_HSYNC_POSITIVE	0x02
#define	EDID_DET_TIMING_FLAG_STEREO_MODE	0x01	/* stereo mode */


/* N.B.: these tests assume that we already checked for detailed timing! */
#define	EDID_BLOCK_TYPE(ptr)			((ptr)[3])

#define	EDID_DESC_BLOCK_SIZE			18
#define	EDID_DESC_BLOCK_TYPE_SERIAL		0xFF
#define	EDID_DESC_BLOCK_TYPE_ASCII		0xFE
#define	EDID_DESC_BLOCK_TYPE_RANGE		0xFD
#define	EDID_DESC_BLOCK_TYPE_NAME		0xFC
#define	EDID_DESC_BLOCK_TYPE_COLOR_POINT	0xFB
#define	EDID_DESC_BLOCK_TYPE_STD_TIMING		0xFA

/* used for descriptors 0xFF, 0xFE, and 0xFC */
#define	EDID_DESC_ASCII_DATA_OFFSET		5
#define	EDID_DESC_ASCII_DATA_LEN		13

#define	EDID_DESC_RANGE_MIN_VFREQ(ptr)		((ptr)[5])	/* Hz */
#define	EDID_DESC_RANGE_MAX_VFREQ(ptr)		((ptr)[6])	/* Hz */
#define	EDID_DESC_RANGE_MIN_HFREQ(ptr)		((ptr)[7])	/* kHz */
#define	EDID_DESC_RANGE_MAX_HFREQ(ptr)		((ptr)[8])	/* kHz */
#define	EDID_DESC_RANGE_MAX_CLOCK(ptr)		(((ptr)[9]) * 10) /* MHz */
#define	EDID_DESC_RANGE_HAVE_GTF2(ptr)		(((ptr)[10]) == 0x02)
#define	EDID_DESC_RANGE_GTF2_HFREQ(ptr)		(((ptr)[12]) * 2)
#define	EDID_DESC_RANGE_GTF2_C(ptr)		(((ptr)[13]) / 2)
#define	EDID_DESC_RANGE_GTF2_M(ptr)		((ptr)[14] + ((ptr)[15] << 8))
#define	EDID_DESC_RANGE_GTF2_K(ptr)		((ptr)[16])
#define	EDID_DESC_RANGE_GTF2_J(ptr)		((ptr)[17] / 2)

#define	EDID_DESC_COLOR_WHITEX(ptr)
#define	EDID_DESC_COLOR_WHITE_INDEX_1(ptr)	((ptr)[5])
#define	EDID_DESC_COLOR_WHITEX_1(ptr)		_CHROMA(ptr, 6, 2, 7)
#define	EDID_DESC_COLOR_WHITEY_1(ptr)		_CHROMA(ptr, 6, 0, 8)
#define	EDID_DESC_COLOR_GAMMA_1(ptr)		_GAMMA(ptr[9])
#define	EDID_DESC_COLOR_WHITE_INDEX_2(ptr)	((ptr)[10])
#define	EDID_DESC_COLOR_WHITEX_2(ptr)		_CHROMA(ptr, 11, 2, 12)
#define	EDID_DESC_COLOR_WHITEY_2(ptr)		_CHROMA(ptr, 11, 0, 13)
#define	EDID_DESC_COLOR_GAMMA_2(ptr)		_GAMMA(ptr[14])

#define	EDID_DESC_STD_TIMING_START		5
#define	EDID_DESC_STD_TIMING_COUNT		6

#define	EDID_EXT_BLOCK_COUNT(ptr)		((ptr)[126])

#endif /* _DEV_VIDEOMODE_EDIDREG_H */
