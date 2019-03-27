/*-
 * Copyright (C) 2000 David S. Miller (davem@redhat.com)
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * DAVID MILLER BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
 * IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 *	from: XFree86: ffb_dac.h,v 1.1 2000/05/23 04:47:44 dawes Exp
 */
/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2003 Jake Burkholder.
 * All rights reserved.
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#ifndef _DEV_FB_CREATORREG_H_
#define	_DEV_FB_CREATORREG_H_

#define	FFB_NREG		24

#define	FFB_PROM		0
#define	FFB_DAC			1
#define	FFB_FBC			2
#define	FFB_DFB8R		3
#define	FFB_DFB8G		4
#define	FFB_DFB8B		5
#define	FFB_DFB8X		6
#define	FFB_DFB24		7
#define	FFB_DFB32		8
#define	FFB_SFB8R		9
#define	FFB_SFB8G		10
#define	FFB_SFB8B		11
#define	FFB_SFB8X		12
#define	FFB_SFB32		13
#define	FFB_SFB64		14
#define	FFB_DFB422A		15

#define	FFB_DAC_TYPE		0x0
#define	FFB_DAC_VALUE		0x4
#define	FFB_DAC_TYPE2		0x8
#define	FFB_DAC_VALUE2		0xc

/* FFB_DAC_TYPE configuration and palette register addresses */
#define	FFB_DAC_CFG_UCTRL	0x1001		/* User Control */
#define	FFB_DAC_CFG_TGEN	0x6000		/* Timing Generator Control */
#define	FFB_DAC_CFG_DID		0x8000		/* Device Identification */

/* FFB_DAC_CFG_UCTRL register */
#define	FFB_DAC_UCTRL_IPDISAB	0x0001		/* Input Pullup Resistor Dis. */
#define	FFB_DAC_UCTRL_ABLANK	0x0002		/* Asynchronous Blank */
#define	FFB_DAC_UCTRL_DBENAB	0x0004		/* Double-Buffer Enable */
#define	FFB_DAC_UCTRL_OVENAB	0x0008		/* Overlay Enable */
#define	FFB_DAC_UCTRL_WMODE	0x0030		/* Window Mode */
#define	FFB_DAC_UCTRL_WM_COMB	0x0000		/* Window Mode Combined */
#define	FFB_DAC_UCTRL_WM_S4	0x0010		/* Window Mode Separate 4 */
#define	FFB_DAC_UCTRL_WM_S8	0x0020		/* Window Mode Separate 8 */
#define	FFB_DAC_UCTRL_WM_RESV	0x0030		/* Window Mode Reserved */
#define	FFB_DAC_UCTRL_MANREV	0x0f00		/* Manufacturing Revision */

/* FFB_DAC_CFG_TGEN register */
#define	FFB_DAC_CFG_TGEN_VIDE	0x01		/* Video Enable */
#define	FFB_DAC_CFG_TGEN_TGE	0x02		/* Timing Generator Enable */
#define	FFB_DAC_CFG_TGEN_HSD	0x04		/* HSYNC* Disable */
#define	FFB_DAC_CFG_TGEN_VSD	0x08		/* VSYNC* Disable */
#define	FFB_DAC_CFG_TGEN_EQD	0x10		/* Equalization Disable */
#define	FFB_DAC_CFG_TGEN_MM	0x20		/* 0 = Slave, 1 = Master */
#define	FFB_DAC_CFG_TGEN_IM	0x40		/* 1 = Interlaced Mode */

/* FFB_DAC_CFG_DID register */
#define	FFB_DAC_CFG_DID_ONE	0x00000001	/* Always Set */
#define	FFB_DAC_CFG_DID_MANUF	0x00000ffe	/* DAC Manufacturer ID */
#define	FFB_DAC_CFG_DID_PNUM	0x0ffff000	/* DAC Part Number */
#define	FFB_DAC_CFG_DID_REV	0xf0000000	/* DAC Revision */

/* FFB_DAC_TYPE2 cursor register addresses */
#define	FFB_DAC_CUR_BITMAP_P0	0x0		/* Plane 0 Cursor Bitmap */
#define	FFB_DAC_CUR_BITMAP_P1	0x80		/* Plane 1 Cursor Bitmap */
#define	FFB_DAC_CUR_CTRL	0x100		/* Cursor Control */
#define	FFB_DAC_CUR_COLOR0	0x101		/* Cursor Color 0 */
#define	FFB_DAC_CUR_COLOR1	0x102		/* Cursor Color 1 (bg) */
#define	FFB_DAC_CUR_COLOR2	0x103		/* Cursor Color 2 (fg) */
#define	FFB_DAC_CUR_POS		0x104		/* Active Cursor Position */

/* FFB_DAC_CUR_CTRL register (might be inverted on PAC1 DACs) */
#define	FFB_DAC_CUR_CTRL_P0	0x1		/* Plane0 Display Disable */
#define	FFB_DAC_CUR_CTRL_P1	0x2		/* Plane1 Display Disable */

#define	FFB_FBC_BY		0x60
#define	FFB_FBC_BX		0x64
#define	FFB_FBC_DY		0x68
#define	FFB_FBC_DX		0x6c
#define	FFB_FBC_BH		0x70
#define	FFB_FBC_BW		0x74
#define	FFB_FBC_PPC		0x200		/* Pixel Processor Control */
#define	FFB_FBC_FG		0x208		/* Foreground */
#define	FFB_FBC_BG		0x20c		/* Background */
#define	FFB_FBC_FBC		0x254		/* Frame Buffer Control */
#define	FFB_FBC_ROP		0x258		/* Raster Operation */
#define	FFB_FBC_PMASK		0x290		/* Pixel Mask */
#define	FFB_FBC_DRAWOP		0x300		/* Draw Operation */
#define	FFB_FBC_FONTXY		0x314		/* Font X/Y */
#define	FFB_FBC_FONTW		0x318		/* Font Width */
#define	FFB_FBC_FONTINC		0x31c		/* Font Increment */
#define	FFB_FBC_FONT		0x320		/* Font Data */
#define	FFB_FBC_UCSR		0x900		/* User Control & Status */

#define	FBC_PPC_VCE_DIS		0x00001000
#define	FBC_PPC_APE_DIS		0x00000800
#define	FBC_PPC_TBE_OPAQUE	0x00000200
#define	FBC_PPC_CS_CONST	0x00000003

#define	FFB_FBC_WB_A		0x20000000
#define	FFB_FBC_RB_A		0x00004000
#define	FFB_FBC_SB_BOTH		0x00003000
#define	FFB_FBC_XE_OFF		0x00000040
#define	FFB_FBC_RGBE_MASK	0x0000003f

#define	FBC_ROP_NEW		0x83

#define	FBC_DRAWOP_RECTANGLE	0x08

#define	FBC_UCSR_FIFO_OVFL	0x80000000
#define	FBC_UCSR_READ_ERR	0x40000000
#define	FBC_UCSR_RP_BUSY	0x02000000
#define	FBC_UCSR_FB_BUSY	0x01000000
#define	FBC_UCSR_FIFO_MASK	0x00000fff

#define	FFB_VIRT_SFB8R		0x00000000
#define	FFB_VIRT_SFB8G		0x00400000
#define	FFB_VIRT_SFB8B		0x00800000
#define	FFB_VIRT_SFB8X		0x00c00000
#define	FFB_VIRT_SFB32		0x01000000
#define	FFB_VIRT_SFB64		0x02000000
#define	FFB_VIRT_FBC		0x04000000
#define	FFB_VIRT_FBC_BM		0x04002000
#define	FFB_VIRT_DFB8R		0x04004000
#define	FFB_VIRT_DFB8G		0x04404000
#define	FFB_VIRT_DFB8B		0x04804000
#define	FFB_VIRT_DFB8X		0x04c04000
#define	FFB_VIRT_DFB24		0x05004000
#define	FFB_VIRT_DFB32		0x06004000
#define	FFB_VIRT_DFB422A	0x07004000
#define	FFB_VIRT_DFB422AD	0x07804000
#define	FFB_VIRT_DFB24B		0x08004000
#define	FFB_VIRT_DFB422B	0x09004000
#define	FFB_VIRT_DFB422BD	0x09804000
#define	FFB_VIRT_SFB16Z		0x0a004000
#define	FFB_VIRT_SFB8Z		0x0a404000
#define	FFB_VIRT_SFB422		0x0ac04000
#define	FFB_VIRT_SFB422D	0x0b404000
#define	FFB_VIRT_FBC_KREG	0x0bc04000
#define	FFB_VIRT_DAC		0x0bc06000
#define	FFB_VIRT_PROM		0x0bc08000
#define	FFB_VIRT_EXP		0x0bc18000

#define	FFB_PHYS_SFB8R		0x04000000
#define	FFB_PHYS_SFB8G		0x04400000
#define	FFB_PHYS_SFB8B		0x04800000
#define	FFB_PHYS_SFB8X		0x04c00000
#define	FFB_PHYS_SFB32		0x05000000
#define	FFB_PHYS_SFB64		0x06000000
#define	FFB_PHYS_FBC		0x00600000
#define	FFB_PHYS_FBC_BM		0x00600000
#define	FFB_PHYS_DFB8R		0x01000000
#define	FFB_PHYS_DFB8G		0x01400000
#define	FFB_PHYS_DFB8B		0x01800000
#define	FFB_PHYS_DFB8X		0x01c00000
#define	FFB_PHYS_DFB24		0x02000000
#define	FFB_PHYS_DFB32		0x03000000
#define	FFB_PHYS_DFB422A	0x09000000
#define	FFB_PHYS_DFB422AD	0x09800000
#define	FFB_PHYS_DFB24B		0x0a000000
#define	FFB_PHYS_DFB422B	0x0b000000
#define	FFB_PHYS_DFB422BD	0x0b800000
#define	FFB_PHYS_SFB16Z		0x0c800000
#define	FFB_PHYS_SFB8Z		0x0c000000
#define	FFB_PHYS_SFB422		0x0d000000
#define	FFB_PHYS_SFB422D	0x0d800000
#define	FFB_PHYS_FBC_KREG	0x00610000
#define	FFB_PHYS_DAC		0x00400000
#define	FFB_PHYS_PROM		0x00000000
#define	FFB_PHYS_EXP		0x00200000

#define	FFB_SIZE_SFB8R		0x00400000
#define	FFB_SIZE_SFB8G		0x00400000
#define	FFB_SIZE_SFB8B		0x00400000
#define	FFB_SIZE_SFB8X		0x00400000
#define	FFB_SIZE_SFB32		0x01000000
#define	FFB_SIZE_SFB64		0x02000000
#define	FFB_SIZE_FBC		0x00002000
#define	FFB_SIZE_FBC_BM		0x00002000
#define	FFB_SIZE_DFB8R		0x00400000
#define	FFB_SIZE_DFB8G		0x00400000
#define	FFB_SIZE_DFB8B		0x00400000
#define	FFB_SIZE_DFB8X		0x00400000
#define	FFB_SIZE_DFB24		0x01000000
#define	FFB_SIZE_DFB32		0x01000000
#define	FFB_SIZE_DFB422A	0x00800000
#define	FFB_SIZE_DFB422AD	0x00800000
#define	FFB_SIZE_DFB24B		0x01000000
#define	FFB_SIZE_DFB422B	0x00800000
#define	FFB_SIZE_DFB422BD	0x00800000
#define	FFB_SIZE_SFB16Z		0x00800000
#define	FFB_SIZE_SFB8Z		0x00800000
#define	FFB_SIZE_SFB422		0x00800000
#define	FFB_SIZE_SFB422D	0x00800000
#define	FFB_SIZE_FBC_KREG	0x00002000
#define	FFB_SIZE_DAC		0x00002000
#define	FFB_SIZE_PROM		0x00010000
#define	FFB_SIZE_EXP		0x00002000

#endif /* !_DEV_FB_CREATORREG_H_ */
