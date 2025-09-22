/*	$OpenBSD: p9000.h,v 1.4 2007/05/22 04:14:03 jsg Exp $	*/
/*
 * Copyright (c) 2003, Miodrag Vallat.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 * ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * Weitek Power9000 and Power9100 definitions.
 *
 * Although the datasheet is not available anymore, a good source of
 * documentation is several code examples in XFree86 3.x (vga256/p9x00) and the
 * {Net,Open}BSD source trees.
 */

/*
 * Frame buffer control registers
 *
 * Offsets below are relative to the following locations:
 * P9000 at 0x00100000, P9100 at 0x000000
 */

/*
 * System control registers
 */

/* System configuration register */
#define	P9000_SYSTEM_CONFIG				0x00000004

#define	SCR_PIXEL_MASK					0x1c000000
#define	SCR_PIXEL_8BPP					0x08000000
#define	SCR_PIXEL_16BPP					0x0c000000
#define	SCR_PIXEL_24BPP					0x1c000000
#define	SCR_PIXEL_32BPP					0x14000000
#define	SCR_SWAP_WORDS					0x00002000
#define	SCR_SWAP_BYTES					0x00001000
#define	SCR_SWAP_BITS					0x00000800
#define	SCR_READ_BUFFER_MASK				0x00000400
#define	SCR_WRITE_BUFFER_MASK				0x00000200
#define	SCR_ID_MASK					0x00000007
#define	SCR_SC(sc0, sc1, sc2, sc3) \
    (((sc0) << 14) | ((sc1) << 17) | ((sc2) << 20) | ((sc3) << 29))

/* Interrupt status register */
#define	P9000_INTERRUPT					0x00000008

/* Interrupt enable register */
#define	P9000_INTERRUPT_ENABLE				0x0000000c

#define	IER_MASTER_ENABLE				0x00000080
#define	IER_MASTER_INTERRUPT				0x00000040
#define	IER_VBLANK_ENABLE				0x00000020
#define	IER_VBLANK_INTERRUPT				0x00000010
#define	IER_PICK_ENABLE					0x00000008
#define	IER_PICK_INTERRUPT				0x00000004
#define	IER_IDLE_ENABLE					0x00000002
#define	IER_IDLE_INTERRUPT				0x00000001

/* Alternate read bank register (bits 16-22) */
#define	P9000_ALTBANK_READ				0x00000010

/* Alternate write bank register (bits 16-22) */
#define	P9000_ALTBANK_WRITE				0x00000014

/*
 * Video control registers
 */

/* Horizontal counter */
#define	P9000_HCR					0x00000104
/* Horizontal total */
#define	P9000_HTR					0x00000108
/* Horizontal sync rising edge */
#define	P9000_HSRE					0x0000010c
/* Horizontal blank rising edge */
#define	P9000_HBRE					0x00000110
/* Horizontal blank falling edge */
#define	P9000_HBFE					0x00000114
/* Horizontal counter preload */
#define	P9000_HCP					0x00000118

/* Vertical counter */
#define	P9000_VCR					0x0000011c
/* Vertical length */
#define	P9000_VL					0x00000120
/* Vertical sync rising edge */
#define	P9000_VSRE					0x00000124
/* Vertical blank rising edge */
#define	P9000_VBRE					0x00000128
/* Vertical blank falling edge */
#define	P9000_VBFE					0x0000012c
/* Vertical counter preload */
#define	P9000_VCP					0x00000130

/* Screen repaint address */
#define	P9000_SRA					0x00000134
/* Screen repaint timing control #1 */
#define	P9000_SRTC1					0x00000138

#define	SRTC1_VSYNC_INTERNAL				0x00000100
#define	SRTC1_HSYNC_INTERNAL				0x00000080
#define	SRTC1_VIDEN					0x00000020
#define	SRTC1_RESTRICTED				0x00000010
#define	SRTC1_BUFFER1					0x00000008

/* QSF counter. Film at 11 */
#define	P9000_QSF					0x0000013c
/* Screen repaint timing control #2 */
#define	P9000_SRTC2					0x00000140

/*
 * VRAM control registers
 */

/* Memory configuration */
#define	P9000_MCR					0x00000184
/* Refresh period */
#define	P9000_REFRESH_PERIOD				0x00000188
/* Refresh count */
#define	P9000_REFRESH_COUNT				0x0000018c
/* RAS low maximum */
#define	P9000_RASLOW_MAXIMUM				0x00000190
/* RAS low current */
#define	P9000_RASLOW_CURRENT				0x00000194
/* RAMDAC free FIFO (P9100 only, bits 12-15) and power-up configuration */
#define	P9000_POWERUP_CONFIG				0x00000198
#define	P9100_FREE_FIFO					0x00000198

/*
 * RAMDAC registers (P9100 only)
 */

#define	P9100_RAMDAC_REGISTER(index)		(0x00000200 + ((index) << 2))


/*
 * Accelerated features
 *
 * Offsets below are relative to the following locations:
 * P9000 at 0x00180000, P9100 at 0x002000
 */

/*
 * Parameter engine
 */ 

/* Status register */
#define	P9000_PE_STATUS					0x00000000
#define	STATUS_QUAD_BUSY				0x80000000
#define	STATUS_BLIT_BUSY				0x40000000
#define	STATUS_PICK_DETECTED				0x00000080
#define	STATUS_PIXEL_ERROR				0x00000040
#define	STATUS_BLIT_ERROR				0x00000020
#define	STATUS_QUAD_ERROR				0x00000010
#define	STATUS_QUAD_CONCAVE				0x00000008
#define	STATUS_QUAD_OUTSIDE				0x00000004
#define	STATUS_QUAD_INSIDE				0x00000002
#define	STATUS_QUAD_STRADDLE				0x00000001

/* Engine arguments / operation triggers */
#define	P9000_PE_BLIT					0x00000004
#define	P9000_PE_QUAD					0x00000008
#define	P9000_PE_PIXEL8					0x0000000c
#define	P9000_PE_NEXTPIXELS				0x00000014
#define	P9000_PE_PIXEL1(index)			(0x00000080 + ((index) << 2))

/* Control and conditions registers */

/* Out of range */
#define	P9000_PE_OOR					0x00000184
/* Index register (0-3, for meta coordinates) */
#define	P9000_PE_INDEX					0x0000018c
/* Window offset (16x16)*/
#define	P9000_PE_WINOFFSET				0x00000190
/* Clipping window */
#define	P9000_PE_WINMIN					0x00000194
#define	P9000_PE_WINMAX					0x00000198
/* X Clip register */
#define	P9000_X_CLIPPING				0x000001a0
/* Y Clip register */
#define	P9000_Y_CLIPPING				0x000001a4
/* X Edge Less Than register */
#define	P9000_X_EDGE_LESS				0x000001a8
/* X Edge Greater Than register */
#define	P9000_X_EDGE_GREATER				0x000001ac
/* Y Edge Less Than register */
#define	P9000_Y_EDGE_LESS				0x000001b0
/* Y Edge Greater Than register */
#define	P9000_Y_EDGE_GREATER				0x000001b4

/*
 * Drawing engine
 */

/* Colors - 8 bit for P9000, 32 bit for P9100 */
#define	P9000_DE_FG_COLOR				0x00000200
#define	P9000_DE_BG_COLOR				0x00000204
#define	P9100_DE_COLOR0					0x00000200
#define	P9100_DE_COLOR1					0x00000204
#define	P9100_DE_COLOR2					0x00000238
#define	P9100_DE_COLOR3					0x0000023c

/* How to encode a colors in 8 and 16 bit mode, for the P9100 */
#define	P9100_COLOR8(c)		((c) | ((c) << 8) | ((c) << 16) | ((c) << 24))
#define	P9100_COLOR16(c)	((c) | ((c) << 16))

/* Plane mask (8 bits on P9000, 32 bits on P9100) */
#define	P9000_DE_PLANEMASK				0x00000208

/* Drawing mode */
#define	P9000_DE_DRAWMODE				0x0000020c
#define	DM_PICK_CONTROL					0x00000008
#define	DM_PICK_ENABLE					0x00000004
#define	DM_BUFFER_CONTROL				0x00000002
#define	DM_BUFFER_ENABLE0				0x00000000
#define	DM_BUFFER_ENABLE1				0x00000001

/* Pattern Origin (4 bit x 4 bit offset) */
#define	P9000_DE_PATTERN_ORIGIN_X			0x00000210
#define	P9000_DE_PATTERN_ORIGIN_Y			0x00000214

/* Raster operation */
#define	P9000_DE_RASTER					0x00000218
#define	P9100_RASTER_NO_SOLID				0x00002000
#define	P9100_RASTER_PATTERN_4COLOR			0x00004000
#define	P9100_RASTER_PIXEL1_TRANSPARENT			0x00008000
#define	P9000_RASTER_QUAD_OVERSIZE			0x00010000
#define	P9000_RASTER_QUAD_PATTERN			0x00020000

/* Raster minterms */
#define	P9000_RASTER_SRC				0xcccc
#define	P9000_RASTER_DST				0xaaaa
#define	P9000_RASTER_PATTERN				0xff00
#define	P9000_RASTER_MASK				0xffff
#define	P9100_RASTER_SRC				0x00cc
#define	P9100_RASTER_DST				0x00aa
#define	P9100_RASTER_PATTERN				0x00f0
#define	P9100_RASTER_MASK				0x00ff

/* Pixel8 excess storage */
#define	P9000_DE_PIXEL8					0x0000021c

/* Clipping window - same as in PE */
#define	P9000_DE_WINMIN					0x00000220
#define	P9000_DE_WINMAX					0x00000224

/* Quad pattern - up to 4 items on P9000, 8 on P9100 */
#define	P9000_DE_PATTERN(index)			(0x00000280 + ((index) << 2))

/* User pattern - up to 4 items */
#define	P9000_DE_USER(index)			(0x00000290 + ((index) << 2))

/* Byte clipping window */
#define	P9100_DE_B_WINMIN				0x000002a0
#define	P9100_DE_B_WINMAX				0x000002a4

/*
 * Coordinates
 */

/* 32 bit X value */
#define	P9000_COORD_X					0x00000008
/* 32 bit Y value */
#define	P9000_COORD_Y					0x00000010
/* 16 bit X, 16 bit Y values packed */
#define	P9000_COORD_XY					0x00000018

/* Absolute (screen) coordinates */
#define	P9000_COORD_ABS					0x00000000
/* Relative (in-window) coordinates */
#define	P9000_COORD_REL					0x00000020

/* How to pack a x16y16 value - note that they are in fact 12 bit values */
#define	P9000_COORDS(x,y)	((((x) & 0x0fff) << 16) | ((y) & 0x0fff))

/* Device coordinates - 4 edges */
#define	P9000_DC_COORD(index)			(0x00001000 + ((index) * 0x40))

/* Load coordinates */
#define	P9000_LC_POINT					0x00001200
#define	P9000_LC_LINE					0x00001240
#define	P9000_LC_TRI					0x00001280
#define	P9000_LC_QUAD					0x000012c0
#define	P9000_LC_RECT					0x00001300
