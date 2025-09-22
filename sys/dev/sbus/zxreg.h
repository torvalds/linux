/*	$OpenBSD: zxreg.h,v 1.2 2008/06/26 05:42:18 ray Exp $	*/
/*	$NetBSD: zxreg.h,v 1.1 2002/09/13 14:03:53 ad Exp $	*/

/*
 *  Copyright (c) 2002 The NetBSD Foundation, Inc.
 *  All rights reserved.
 *
 *  This code is derived from software contributed to The NetBSD Foundation
 *  by Andrew Doran.
 *
 *  Redistribution and use in source and binary forms, with or without
 *  modification, are permitted provided that the following conditions
 *  are met:
 *  1. Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *  2. Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 *
 *  THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 *  ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 *  TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 *  PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 *  BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 *  CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 *  SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 *  INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 *  CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 *  ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 *  POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * Copyright (C) 1999, 2000 Jakub Jelinek (jakub@redhat.com)
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
 * JAKUB JELINEK BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
 * IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 */
 
#ifndef _DEV_SBUS_ZXREG_H_
#define _DEV_SBUS_ZXREG_H_

/* Hardware offsets. */
#define ZX_OFF_UNK2		0x00000000
#define ZX_OFF_LC_SS0_KRN	0x00200000
#define ZX_OFF_LC_SS0_USR	0x00201000
#define ZX_OFF_LD_SS0		0x00400000
#define ZX_OFF_LD_GBL		0x00401000
#define ZX_OFF_LX_CROSS		0x00600000
#define ZX_OFF_LX_CURSOR	0x00601000
#define ZX_OFF_UNK		0x00602000
#define ZX_OFF_SS0		0x00800000
#define ZX_OFF_LC_SS1_KRN	0x01200000
#define ZX_OFF_LC_SS1_USR	0x01201000
#define ZX_OFF_LD_SS1		0x01400000
#define ZX_OFF_SS1		0x01800000

/* ROP register */
#define ZX_ATTR_PICK_DISABLE	0x00000000
#define ZX_ATTR_PICK_2D		0x80000000
#define ZX_ATTR_PICK_3D		0xa0000000
#define ZX_ATTR_PICK_2D_REND	0xc0000000
#define ZX_ATTR_PICK_3D_REND	0xe0000000

#define ZX_ATTR_DCE_DISABLE	0x00000000
#define ZX_ATTR_DCE_ENABLE	0x10000000

#define ZX_ATTR_APE_DISABLE	0x00000000
#define ZX_ATTR_APE_ENABLE	0x08000000

#define ZX_ATTR_COLOR_VAR	0x00000000
#define ZX_ATTR_COLOR_CONST	0x04000000

#define ZX_ATTR_AA_DISABLE	0x02000000
#define ZX_ATTR_AA_ENABLE	0x01000000

#define ZX_ATTR_ABE_BG		0x00000000	/* dst + alpha * (src - bg) */
#define ZX_ATTR_ABE_FB		0x00800000	/* dst + alpha * (src - dst) */

#define ZX_ATTR_ABE_DISABLE	0x00000000
#define ZX_ATTR_ABE_ENABLE	0x00400000

#define ZX_ATTR_BLTSRC_A	0x00000000
#define ZX_ATTR_BLTSRC_B	0x00200000

#define ZX_ROP_ZERO		(0x0 << 18)
#define ZX_ROP_NEW_AND_OLD	(0x8 << 18)
#define ZX_ROP_NEW_AND_NOLD	(0x4 << 18)
#define ZX_ROP_NEW		(0xc << 18)
#define ZX_ROP_NNEW_AND_OLD	(0x2 << 18)
#define ZX_ROP_OLD		(0xa << 18)
#define ZX_ROP_NEW_XOR_OLD	(0x6 << 18)
#define ZX_ROP_NEW_OR_OLD	(0xe << 18)
#define ZX_ROP_NNEW_AND_NOLD	(0x1 << 18)
#define ZX_ROP_NNEW_XOR_NOLD	(0x9 << 18)
#define ZX_ROP_NOLD		(0x5 << 18)
#define ZX_ROP_NEW_OR_NOLD	(0xd << 18)
#define ZX_ROP_NNEW		(0x3 << 18)
#define ZX_ROP_NNEW_OR_OLD	(0xb << 18)
#define ZX_ROP_NNEW_OR_NOLD	(0x7 << 18)
#define ZX_ROP_ONES		(0xf << 18)

#define ZX_ATTR_HSR_DISABLE	0x00000000
#define ZX_ATTR_HSR_ENABLE	0x00020000

#define ZX_ATTR_WRITEZ_DISABLE	0x00000000
#define ZX_ATTR_WRITEZ_ENABLE	0x00010000

#define ZX_ATTR_Z_VAR		0x00000000
#define ZX_ATTR_Z_CONST		0x00008000

#define ZX_ATTR_WCLIP_DISABLE	0x00000000
#define ZX_ATTR_WCLIP_ENABLE	0x00004000

#define ZX_ATTR_MONO		0x00000000
#define ZX_ATTR_STEREO_LEFT	0x00001000
#define ZX_ATTR_STEREO_RIGHT	0x00003000

#define ZX_ATTR_WE_DISABLE	0x00000000
#define ZX_ATTR_WE_ENABLE	0x00000800

#define ZX_ATTR_FCE_DISABLE	0x00000000
#define ZX_ATTR_FCE_ENABLE	0x00000400

#define ZX_ATTR_RE_DISABLE	0x00000000
#define ZX_ATTR_RE_ENABLE	0x00000200

#define ZX_ATTR_GE_DISABLE	0x00000000
#define ZX_ATTR_GE_ENABLE	0x00000100

#define ZX_ATTR_BE_DISABLE	0x00000000
#define ZX_ATTR_BE_ENABLE	0x00000080

#define ZX_ATTR_RGBE_DISABLE	0x00000000
#define ZX_ATTR_RGBE_ENABLE	0x00000380

#define ZX_ATTR_OE_DISABLE	0x00000000
#define ZX_ATTR_OE_ENABLE	0x00000040

#define ZX_ATTR_ZE_DISABLE	0x00000000
#define ZX_ATTR_ZE_ENABLE	0x00000020

#define ZX_ATTR_FORCE_WID	0x00000010

#define ZX_ATTR_FC_PLANE_MASK	0x0000000e

#define ZX_ATTR_BUFFER_A	0x00000000
#define ZX_ATTR_BUFFER_B	0x00000001

/* CSR */
#define ZX_CSR_BLT_BUSY		0x20000000

struct zx_draw {
	u_int32_t	zd_pad0[896];
	u_int32_t	zd_csr;
	u_int32_t	zd_wid;
	u_int32_t	zd_wmask;
	u_int32_t	zd_widclip;
	u_int32_t	zd_vclipmin;
	u_int32_t	zd_vclipmax;
	u_int32_t	zd_pickmin;	/* SS1 only */
	u_int32_t	zd_pickmax;	/* SS1 only */
	u_int32_t	zd_fg;
	u_int32_t	zd_bg;
	u_int32_t	zd_src;		/* Copy/Scroll (SS0 only) */
	u_int32_t	zd_dst;		/* Copy/Scroll/Fill (SS0 only) */
	u_int32_t	zd_extent;	/* Copy/Scroll/Fill size (SS0 only) */
	u_int32_t	zd_pad1[3];
	u_int32_t	zd_setsem;	/* SS1 only */
	u_int32_t	zd_clrsem;	/* SS1 only */
	u_int32_t	zd_clrpick;	/* SS1 only */
	u_int32_t	zd_clrdat;	/* SS1 only */
	u_int32_t	zd_alpha;	/* SS1 only */
	u_int32_t	zd_pad2[11];
	u_int32_t	zd_winbg;
	u_int32_t	zd_planemask;
	u_int32_t	zd_rop;
	u_int32_t	zd_z;
	u_int32_t	zd_dczf;	/* SS1 only */
	u_int32_t	zd_dczb;	/* SS1 only */
	u_int32_t	zd_dcs;		/* SS1 only */
	u_int32_t	zd_dczs;	/* SS1 only */
	u_int32_t	zd_pickfb;	/* SS1 only */
	u_int32_t	zd_pickbb;	/* SS1 only */
	u_int32_t	zd_dcfc;	/* SS1 only */
	u_int32_t	zd_forcecol;	/* SS1 only */
	u_int32_t	zd_door[8];	/* SS1 only */
	u_int32_t	zd_pick[5];	/* SS1 only */
};

/* EXTENT */
#define	ZX_EXTENT_DIR_FORWARDS	0x00000000
#define	ZX_EXTENT_DIR_BACKWARDS	0x80000000

struct zx_draw_ss1 {
	u_int32_t	zd_pad0[957];
	u_int32_t	zd_misc;
};
#define	ZX_SS1_MISC_ENABLE	0x00000001
#define	ZX_SS1_MISC_STEREO	0x00000002

#define ZX_ADDRSPC_OBGR		0x00
#define ZX_ADDRSPC_Z		0x01
#define ZX_ADDRSPC_W		0x02
#define ZX_ADDRSPC_FONT_OBGR	0x04
#define ZX_ADDRSPC_FONT_Z	0x05
#define ZX_ADDRSPC_FONT_W	0x06
#define ZX_ADDRSPC_O		0x08
#define ZX_ADDRSPC_B		0x09
#define ZX_ADDRSPC_G		0x0a
#define ZX_ADDRSPC_R		0x0b

struct zx_command {
	u_int32_t	zc_csr;
	u_int32_t	zc_addrspace;
	u_int32_t 	zc_fontmsk;
	u_int32_t	zc_fontt;
	u_int32_t	zc_extent;
	u_int32_t	zc_src;
	u_int32_t	zc_dst;
	u_int32_t	zc_copy;
	u_int32_t	zc_fill;
};

#define ZX_CROSS_TYPE_CLUT0	0x00001000
#define ZX_CROSS_TYPE_CLUT1	0x00001001
#define ZX_CROSS_TYPE_CLUT2	0x00001002
#define ZX_CROSS_TYPE_WID	0x00001003
#define ZX_CROSS_TYPE_UNK	0x00001006
#define ZX_CROSS_TYPE_VIDEO	0x00002003
#define ZX_CROSS_TYPE_CLUTDATA	0x00004000

#define ZX_CROSS_CSR_ENABLE	0x00000008
#define ZX_CROSS_CSR_PROGRESS	0x00000004
#define ZX_CROSS_CSR_UNK	0x00000002
#define ZX_CROSS_CSR_UNK2	0x00000001

struct zx_cross {
	u_int32_t	zx_type;
	u_int32_t	zx_csr;
	u_int32_t	zx_value;
};

struct zx_cursor {
	u_int32_t	zcu_pad0[4];
	u_int32_t	zcu_type;
	u_int32_t	zcu_misc;
	u_int32_t	zcu_sxy;
	u_int32_t	zcu_data;
};

#endif	/* !_DEV_SBUS_ZXREG_H_ */
