/*	$OpenBSD: atxxreg.h,v 1.1 2008/04/15 20:23:54 miod Exp $	*/

/*
 * Copyright (c) 2008 Miodrag Vallat.
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

/*
 * Alliance Promotion AP6422, AT24 and AT3D extended register set definitions.
 *
 * This has been reconstructed from XFree86 ``apm'' driver, whose authors
 * apparently do not believe in meaningful constants for numbers. See
 * apm_regs.h for more madness.
 */

/*
 * Dual coordinates encoding
 */

#define	ATR_DUAL(y,x)			(((y) << 16) | (x))

/*
 * Clipping Control
 */

#define	ATR_CLIP_CONTROL		0x0030	/* byte access */
#define	ATR_CLIP_LEFT			0x0038
#define	ATR_CLIP_TOP			0x003a
#define	ATR_CLIP_LEFTTOP		0x0038
#define	ATR_CLIP_RIGHT			0x003c
#define	ATR_CLIP_BOTTOM			0x003e
#define	ATR_CLIP_RIGHTBOTTOM		0x003c

/*
 * Drawing Engine
 */

#define	ATR_DEC				0x0040
#define	ATR_ROP				0x0046
#define	ATR_BYTEMASK			0x0047
#define	ATR_PATTERN1			0x0048
#define	ATR_PATTERN2			0x004c
#define	ATR_SRC_X			0x0050
#define	ATR_SRC_Y			0x0052
#define	ATR_SRC_XY			0x0050
#define	ATR_DST_X			0x0054
#define	ATR_DST_Y			0x0056
#define	ATR_DST_XY			0x0054
#define	ATR_W				0x0058
#define	ATR_H				0x005a
#define	ATR_WH				0x0058
#define	ATR_OFFSET			0x005c
#define	ATR_SRC_OFFSET			0x005e
#define	ATR_FG				0x0060
#define	ATR_BG				0x0064

/* DEC layout */
#define	DEC_COMMAND_MASK		0x0000003f
#define	DEC_COMMAND_SHIFT		0
#define	DEC_DIR_X_REVERSE		0x00000040
#define	DEC_DIR_Y_REVERSE		0x00000080
#define	DEC_DIR_Y_MAJOR			0x00000100
#define	DEC_SRC_LINEAR			0x00000200
#define	DEC_SRC_CONTIGUOUS		0x00000800
#define	DEC_MONOCHROME			0x00001000
#define	DEC_SRC_TRANSPARENT		0x00002000
#define	DEC_DEPTH_MASK			0x0001c000
#define	DEC_DEPTH_SHIFT			14
#define	DEC_DST_LINEAR			0x00040000
#define	DEC_DST_CONTIGUOUS		0x00080000
#define	DEC_DST_TRANSPARENT		0x00100000
#define	DEC_DST_TRANSPARENT_POLARITY	0x00200000
#define	DEC_PATTERN_MASK		0x00c00000
#define	DEC_PATTERN_SHIFT		22
#define	DEC_WIDTH_MASK			0x07000000
#define	DEC_WIDTH_SHIFT			24
#define	DEC_UPDATE_MASK			0x18000000
#define	DEC_UPDATE_SHIFT		27
#define	DEC_START_MASK			0x60000000
#define	DEC_START_SHIFT			29
#define	DEC_START			0x80000000

/* DEC commands */
#define	DEC_COMMAND_NOP			0x00
#define	DEC_COMMAND_BLT			0x01	/* screen to screen blt */
#define	DEC_COMMAND_RECT		0x02	/* rectangle fill */
#define	DEC_COMMAND_BLT_STRETCH		0x03	/* blt and stretch */
#define	DEC_COMMAND_STRIP		0x04	/* strip pattern */
#define	DEC_COMMAND_HOST_BLT		0x08	/* host to screen blt */
#define	DEC_COMMAND_SCREEN_BLT		0x09	/* screen to host blt */
#define	DEC_COMMAND_VECT_ENDP		0x0c	/* vector with end point */
#define	DEC_COMMAND_VECT_NO_ENDP	0x0d	/* vector without end point */

/* depth */
#define	DEC_DEPTH_8			0x01
#define	DEC_DEPTH_16			0x02
#define	DEC_DEPTH_32			0x03
#define	DEC_DEPTH_24			0x04

/* width */
#define	DEC_WIDTH_LINEAR		0x00
#define	DEC_WIDTH_640			0x01
#define	DEC_WIDTH_800			0x02
#define	DEC_WIDTH_1024			0x04
#define	DEC_WIDTH_1152			0x05
#define	DEC_WIDTH_1280			0x06
#define	DEC_WIDTH_1600			0x07

/* update mode */
#define	DEC_UPDATE_NONE			0x00
#define	DEC_UPDATE_TOP_RIGHT		0x01
#define	DEC_UPDATE_BOTTOM_LEFT		0x02
#define	DEC_UPDATE_LASTPIX		0x03

/* quickstart mode - operation starts as soon as given register is written to */
#define	DEC_START_DIMX			0x01
#define	DEC_START_SRC			0x02
#define	DEC_START_DST			0x03

/* ROP */
#define	ROP_DST				0x66
#define	ROP_SRC				0xcc
#define	ROP_PATTERN			0xf0

/*
 * Configuration Registers
 */

#define	ATR_PIXEL			0x0080	/* byte access */
#define PIXEL_DEPTH_MASK		0x0f
#define	PIXEL_DEPTH_SHIFT		0

/* pixel depth */
#define	PIXEL_4				0x01
#define	PIXEL_8				0x02
#define	PIXEL_15			0x0c
#define	PIXEL_16			0x0d
#define	PIXEL_24			0x0e
#define	PIXEL_32			0x0f

#define	ATR_APERTURE			0x00c0	/* short access */

/*
 * DPMS Control
 */

#define	ATR_DPMS			0x00d0	/* byte access */

#define	DPMS_HSYNC_DISABLE		0x01
#define	DPMS_VSYNC_DISABLE		0x02

/*
 * RAMDAC
 */

#define	ATR_COLOR_CORRECTION		0x00e0
#define	ATR_MCLK			0x00e8
#define	ATR_PCLK			0x00ec

/*
 * Hardware Cursor
 *
 * The position can not become negative; the offset register, encoded as
 * (signed y delta << 8) | signed x delta, allow the cursor image to
 * cross the upper-left corner.
 */

#define	ATR_CURSOR_ENABLE		0x0140
#define	ATR_CURSOR_FG			0x0141	/* 3:3:2 */
#define	ATR_CURSOR_BG			0x0142	/* 3:3:2 */
#define	ATR_CURSOR_ADDRESS		0x0144	/* in KB from vram */
#define	ATR_CURSOR_POSITION		0x0148
#define	ATR_CURSOR_OFFSET		0x014c	/* short access */

/*
 * Identification Register
 */

#define	ATR_ID				0x0182

#define	ID_AP6422			0x6422
#define	ID_AT24				0x6424
#define	ID_AT3D				0x643d

/*
 * Status Registers
 */

#define	ATR_FIFO_STATUS			0x01fc
#define	ATR_BLT_STATUS			0x01fd

#define	FIFO_MASK			0x0f
#define	FIFO_SHIFT			0
#define	FIFO_AP6422		4
#define	FIFO_AT24		8

#define	BLT_HOST_BUSY			0x01
#define	BLT_ENGINE_BUSY			0x04
