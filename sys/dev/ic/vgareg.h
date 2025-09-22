/* $OpenBSD: vgareg.h,v 1.5 2009/02/01 14:37:22 miod Exp $ */
/* $NetBSD: vgareg.h,v 1.2 1998/05/28 16:48:41 drochner Exp $ */

/*
 * Copyright (c) 1998
 *	Matthias Drochner.  All rights reserved.
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
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */

struct reg_vgaattr { /* indexed via port 0x3c0 */
	u_int8_t palette[16];
	u_int8_t mode, overscan, colplen, horpixpan;
	u_int8_t colreset, misc;
} __packed;
#define VGA_ATC_INDEX 0
#define VGA_ATC_DATAW 0
#define VGA_ATC_DATAR 1

struct reg_vgats { /* indexed via port 0x3c4 */
	u_int8_t syncreset, mode, wrplmask, fontsel, memmode;
} __packed;
#define VGA_TS_INDEX 4
#define VGA_TS_DATA 5

struct reg_vgagdc { /* indexed via port 0x3ce */
	u_int8_t setres, ensetres, colorcomp, rotfunc;
	u_int8_t rdplanesel, mode, misc, colorcare;
	u_int8_t bitmask;
} __packed;
#define VGA_GDC_INDEX 0xe
#define VGA_GDC_DATA 0xf

#define	VGA_DAC_MASK	0x06	/* pixel write mask */
#define	VGA_DAC_READ	0x07	/* palette read address */
#define	VGA_DAC_WRITE	0x08	/* palette write address */
#define	VGA_DAC_DATA	0x09	/* palette data register */
