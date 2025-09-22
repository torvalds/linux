/*	$OpenBSD: mmu_sh3.h,v 1.2 2008/06/26 05:42:12 ray Exp $	*/
/*	$NetBSD: mmu_sh3.h,v 1.6 2006/03/04 01:55:03 uwe Exp $	*/

/*-
 * Copyright (c) 2002 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by UCHIYAMA Yasushi.
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

#ifndef _SH_MMU_SH3_H_
#define	_SH_MMU_SH3_H_
#include <sh/devreg.h>

/* 128-entry 4-way set-associative */
#define	SH3_MMU_WAY			4
#define	SH3_MMU_ENTRY			32

#define	SH3_PTEH			0xfffffff0
#define	  SH3_PTEH_ASID_MASK		  0x000000ff
#define	  SH3_PTEH_VPN_MASK		  0xfffffc00
#define	SH3_PTEL			0xfffffff4
#define	  SH3_PTEL_HWBITS		  0x1ffff17e /* [28:12][8][6:1] */
#define	SH3_TTB				0xfffffff8
#define	SH3_TEA				0xfffffffc
#define	SH3_MMUCR			0xffffffe0
#define	  SH3_MMUCR_AT			  0x00000001
#define	  SH3_MMUCR_IX			  0x00000002
#define	  SH3_MMUCR_TF			  0x00000004
#define	  SH3_MMUCR_RC			  0x00000030
#define	  SH3_MMUCR_SV			  0x00000100

/*
 * memory-mapped TLB
 */
/* Address array */
#define	SH3_MMUAA			0xf2000000
/* address specification */
#define	  SH3_MMU_VPN_SHIFT		  12
#define	  SH3_MMU_VPN_MASK		  0x0001f000	/* [16:12] */
#define	  SH3_MMU_WAY_SHIFT		  8
#define	  SH3_MMU_WAY_MASK		  0x00000300	/* [9:8] */
/* data specification */
#define	  SH3_MMU_D_VALID		  0x00000100
#define	  SH3_MMUAA_D_VPN_MASK_1K	  0xfffe0c00	/* [31:17][11:10] */
#define	  SH3_MMUAA_D_VPN_MASK_4K	  0xfffe0000	/* [31:17] */
#define	  SH3_MMUAA_D_ASID_MASK		  0x000000ff

/* Data array */
#define	SH3_MMUDA			0xf3000000
#define	  SH3_MMUDA_D_PPN_MASK		  0xfffffc00
#define	  SH3_MMUDA_D_V			  0x00000100
#define	  SH3_MMUDA_D_PR_SHIFT		  5
#define	  SH3_MMUDA_D_PR_MASK		  0x00000060	/* [6:5] */
#define	  SH3_MMUDA_D_SZ		  0x00000010
#define	  SH3_MMUDA_D_C			  0x00000008
#define	  SH3_MMUDA_D_D			  0x00000004
#define	  SH3_MMUDA_D_SH		  0x00000002

#define	SH3_TLB_DISABLE	*(volatile uint32_t *)SH3_MMUCR = SH3_MMUCR_TF
#endif /* !_SH_MMU_SH3_H_ */
