/*	$OpenBSD: mmu_sh4.h,v 1.3 2025/07/16 07:15:42 jsg Exp $	*/
/*	$NetBSD: mmu_sh4.h,v 1.6 2006/03/04 01:55:03 uwe Exp $	*/

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

#ifndef _SH_MMU_SH4_H_
#define	_SH_MMU_SH4_H_
#include <sh/devreg.h>

/* ITLB 4-entry full-associative UTLB 64-entry full-associative */
#define	SH4_PTEH			0xff000000
#define	  SH4_PTEH_VPN_MASK		  0xfffffc00
#define	  SH4_PTEH_ASID_MASK		  0x000000ff
#define	SH4_PTEL			0xff000004
#define	  SH4_PTEL_WT			  0x00000001
#define	  SH4_PTEL_SH			  0x00000002
#define	  SH4_PTEL_D			  0x00000004
#define	  SH4_PTEL_C			  0x00000008
#define	  SH4_PTEL_PR_SHIFT		  5
#define	  SH4_PTEL_PR_MASK		  0x00000060	/* [5:6] */
#define	  SH4_PTEL_SZ_MASK		  0x00000090	/* [4][7] */
#define	    SH4_PTEL_SZ_1K		  0x00000000
#define	    SH4_PTEL_SZ_4K		  0x00000010
#define	    SH4_PTEL_SZ_64K		  0x00000080
#define	    SH4_PTEL_SZ_1M		  0x00000090
#define	  SH4_PTEL_V			  0x00000100
#define	  SH4_PTEL_HWBITS		  0x1ffff1ff /* [28:12]PFN [8:0]attr. */

#define	SH4_PTEA			0xff000034
#define	  SH4_PTEA_SA_MASK		  0x00000007
#define	  SH4_PTEA_SA_TC		  0x00000008
#define	SH4_TTB				0xff000008
#define	SH4_TEA				0xff00000c
#define	SH4_MMUCR			0xff000010
#define	  SH4_MMUCR_AT			  0x00000001
#define	  SH4_MMUCR_TI			  0x00000004
#define	  SH4_MMUCR_SV			  0x00000100
#define	  SH4_MMUCR_SQMD		  0x00000200
#define	  SH4_MMUCR_URC_SHIFT		  10
#define	  SH4_MMUCR_URC_MASK		  0x0000fc00	/* [10:15] */
#define	  SH4_MMUCR_URB_SHIFT		  18
#define	  SH4_MMUCR_URB_MASK		  0x00fc0000	/* [18:23] */
#define	  SH4_MMUCR_LRUI_SHIFT		  26
#define	  SH4_MMUCR_LRUT_MASK		  0xfc000000	/* [26:31] */

#define	  SH4_MMUCR_MASK	(SH4_MMUCR_LRUT_MASK | SH4_MMUCR_URB_MASK | \
    SH4_MMUCR_URC_MASK | SH4_MMUCR_SQMD | SH4_MMUCR_SV | SH4_MMUCR_AT)
/*
 * memory-mapped TLB
 *	must be access from P2-area program.
 *	branch to the other area must be made at least 8 instruction
 *	after access.
 */
#define	SH4_ITLB_ENTRY		4
#define	SH4_UTLB_ENTRY		64

/* ITLB */
#define	SH4_ITLB_AA			0xf2000000
/* address specification (common for address and data array(0,1)) */
#define	  SH4_ITLB_E_SHIFT		  8
#define	  SH4_ITLB_E_MASK		  0x00000300	/* [9:8] */
/* data specification */
/* address-array */
#define	  SH4_ITLB_AA_ASID_MASK		  0x000000ff	/* [7:0] */
#define	  SH4_ITLB_AA_V			  0x00000100
#define	  SH4_ITLB_AA_VPN_SHIFT		  10
#define	  SH4_ITLB_AA_VPN_MASK		  0xfffffc00	/* [31:10] */
/* data-array 1 */
#define	SH4_ITLB_DA1			0xf3000000
#define	  SH4_ITLB_DA1_SH		  0x00000002
#define	  SH4_ITLB_DA1_C		  0x00000008
#define	  SH4_ITLB_DA1_SZ_MASK		  0x00000090	/* [7][4] */
#define	    SH4_ITLB_DA1_SZ_1K		  0x00000000
#define	    SH4_ITLB_DA1_SZ_4K		  0x00000010
#define	    SH4_ITLB_DA1_SZ_64K		  0x00000080
#define	    SH4_ITLB_DA1_SZ_1M		  0x00000090
#define	  SH4_ITLB_DA1_PR		  0x00000040
#define	  SH4_ITLB_DA1_V		  0x00000100
#define	  SH4_ITLB_DA1_PPN_SHIFT	  11
#define	  SH4_ITLB_DA1_PPN_MASK		  0x1ffffc00	/* [28:10] */
/* data-array 2 */
#define	SH4_ITLB_DA2			0xf3800000
#define	  SH4_ITLB_DA2_SA_MASK		  0x00000003
#define	  SH4_ITLB_DA2_TC		  0x00000004

/* UTLB */
#define	SH4_UTLB_AA			0xf6000000
/* address specification (common for address and data array(0,1)) */
#define	  SH4_UTLB_E_SHIFT		  8
#define	  SH4_UTLB_E_MASK		  0x00003f00
#define	  SH4_UTLB_A			  0x00000080
/* data specification */
/* address-array */
#define	  SH4_UTLB_AA_VPN_MASK		  0xfffffc00	/* [31:10] */
#define	  SH4_UTLB_AA_D			  0x00000200
#define	  SH4_UTLB_AA_V			  0x00000100
#define	  SH4_UTLB_AA_ASID_MASK		  0x000000ff	/* [7:0] */
/* data-array 1 */
#define	SH4_UTLB_DA1			0xf7000000
#define	  SH4_UTLB_DA1_WT		  0x00000001
#define	  SH4_UTLB_DA1_SH		  0x00000002
#define	  SH4_UTLB_DA1_D		  0x00000004
#define	  SH4_UTLB_DA1_C		  0x00000008
#define	  SH4_UTLB_DA1_SZ_MASK		  0x00000090	/* [7][4] */
#define	    SH4_UTLB_DA1_SZ_1K		  0x00000000
#define	    SH4_UTLB_DA1_SZ_4K		  0x00000010
#define	    SH4_UTLB_DA1_SZ_64K		  0x00000080
#define	    SH4_UTLB_DA1_SZ_1M		  0x00000090
#define	  SH4_UTLB_DA1_PR_SHIFT		  5
#define	  SH4_UTLB_DA1_PR_MASK		  0x00000060
#define	  SH4_UTLB_DA1_V		  0x00000100
#define	  SH4_UTLB_DA1_PPN_SHIFT	  11
#define	  SH4_UTLB_DA1_PPN_MASK		  0x1ffffc00	/* [28:10] */
/* data-array 2 */
#define	SH4_UTLB_DA2			0xf7800000
#define	  SH4_UTLB_DA2_SA_MASK		  0x00000003
#define	  SH4_UTLB_DA2_TC		  0x00000004

#define	SH4_TLB_DISABLE	*(volatile uint32_t *)SH4_MMUCR = SH4_MMUCR_TI
#endif /* !_SH_MMU_SH4_H_ */
