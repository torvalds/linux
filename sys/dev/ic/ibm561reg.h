/* $NetBSD: ibm561reg.h,v 1.1 2001/12/12 07:46:48 elric Exp $ */
/* $OpenBSD: ibm561reg.h,v 1.2 2008/06/26 05:42:15 ray Exp $ */

/*-
 * Copyright (c) 2001 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Roland C. Dowdeswell of Ponte, Inc.
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
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#define IBM561_ADDR_LOW			0x00
#define IBM561_ADDR_HIGH		0x01
#define IBM561_CMD			0x02
#define IBM561_CMD_FB_WAT		0x03
#define IBM561_CMD_CMAP			0x03
#define IBM561_CMD_GAMMA		0x03

#define IBM561_CONFIG_REG1		0x0001
#define IBM561_CONFIG_REG2		0x0002
#define IBM561_CONFIG_REG3		0x0003
#define IBM561_CONFIG_REG4		0x0004

#define IBM561_SYNC_CNTL		0x0020
#define IBM561_PLL_VCO_DIV		0x0021
#define IBM561_PLL_REF_REG		0x0022
#define IBM561_CURS_CNTL_REG		0x0030
#define IBM561_HOTSPOT_REG		0x0034
#define IBM561_VRAM_MASK_REG		0x0050
#define IBM561_DIV_DOTCLCK		0x0082
#define IBM561_FB_WINTYPE		0x1000
#define IBM561_AUXFB_WINTYPE		0x0e00
#define IBM561_OL_WINTYPE		0x1400
#define IBM561_AUXOL_WINTYPE		0x0f00
#define IBM561_CMAP_TABLE		0x4000
#define IBM561_RED_GAMMA_TABLE		0x3000
#define IBM561_GREEN_GAMMA_TABLE	0x3400
#define IBM561_BLUE_GAMMA_TABLE		0x3800

#define IBM561_CHROMAKEY0		0x0010
#define IBM561_CHROMAKEY1		0x0011
#define IBM561_CHROMAKEYMASK0		0x0012
#define IBM561_CHROMAKEYMASK1		0x0013

#define IBM561_WAT_SEG_REG		0x0006

#define IBM561_NCMAP_ENTRIES		1024
#define IBM561_NGAMMA_ENTRIES		256

/* we actually have 1024 of them, but I am just
 * going define a few, so this is good.
 */
#define IBM561_NWTYPES			16
