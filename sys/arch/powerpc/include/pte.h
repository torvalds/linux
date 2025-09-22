/*	$OpenBSD: pte.h,v 1.11 2023/01/31 01:27:58 gkoehler Exp $	*/
/*	$NetBSD: pte.h,v 1.1 1996/09/30 16:34:32 ws Exp $	*/

/*-
 * Copyright (C) 1995, 1996 Wolfgang Solfrank.
 * Copyright (C) 1995, 1996 TooLs GmbH.
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
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by TooLs GmbH.
 * 4. The name of TooLs GmbH may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY TOOLS GMBH ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL TOOLS GMBH BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef	_POWERPC_PTE_H_
#define	_POWERPC_PTE_H_

/*
 * Page Table Entries
 */
#ifndef	_LOCORE
struct pte_32 {
	u_int32_t pte_hi;
	u_int32_t pte_lo;
};
struct pte_64 {
	u_int64_t pte_hi;
	u_int64_t pte_lo;
};
#endif	/* _LOCORE */

/* 32 bit */
/* High word: */
#define	PTE_VALID_32	0x80000000
#define	PTE_VSID_SHIFT_32	7
#define	PTE_HID_32	0x00000040
#define	PTE_API_32	0x0000003f
 /* Low word: */
#define	PTE_RPGN_32	0xfffff000
#define	PTE_REF_32	0x00000100
#define	PTE_CHG_32	0x00000080
#define	PTE_WIM_32	0x00000078
#define	PTE_W_32	0x00000040
#define	PTE_EXE_32	0x00000040 /* only used in pmap_attr, same as PTE_W */
#define	PTE_I_32	0x00000020
#define	PTE_M_32	0x00000010
#define	PTE_G_32	0x00000008
#define	PTE_PP_32	0x00000003
#define	PTE_RO_32	0x00000003
#define	PTE_RW_32	0x00000002


/* 64 bit */
/* High doubleword: */
#define	PTE_VALID_64		0x0000000000000001ULL
#define	PTE_AVPN_SHIFT_64	7
#define PTE_AVPN_64		0xffffffffffffff80ULL
#define PTE_API_SHIFT_64	7
#define PTE_API_64		0x0000000000000f80ULL
#define PTE_VSID_SHIFT_64  12
#define PTE_VSID_64		0xfffffffffffff000ULL
#define	PTE_HID_64		0x0000000000000002ULL
/* Low word: */
#define	PTE_RPGN_64		0x3ffffffffffff000ULL
#define	PTE_AC_64		0x0000000000000200ULL
#define	PTE_REF_64		0x0000000000000100ULL
#define	PTE_CHG_64		0x0000000000000080ULL
#define	PTE_WIMG_64		0x0000000000000078ULL
#define	PTE_W_64		0x0000000000000040ULL
#define PTE_EXE_64		PTE_W
#define	PTE_I_64		0x0000000000000020ULL
#define	PTE_M_64		0x0000000000000010ULL
#define	PTE_G_64		0x0000000000000008ULL
#define PTE_N_64		0x0000000000000004ULL
#define	PTE_PP_64		0x0000000000000003ULL
#define	PTE_RO_64		0x0000000000000003ULL
#define	PTE_RW_64		0x0000000000000002ULL

/*
 * Extract bits from address
 */
#define	ADDR_SR_SHIFT		28
#define	ADDR_PIDX		0x0ffff000
#define	ADDR_PIDX_SHIFT		12
#define	ADDR_API_SHIFT_32	22
#define	ADDR_API_SHIFT_64	16
#define	ADDR_POFF		0x00000fff

/*
 * Bits in DSISR:
 */
#define	DSISR_DIRECT	0x80000000
#define	DSISR_NOTFOUND	0x40000000
#define	DSISR_PROTECT	0x08000000
#define	DSISR_INVRX	0x04000000
#define	DSISR_STORE	0x02000000
#define	DSISR_DABR	0x00400000
#define	DSISR_SEGMENT	0x00200000
#define	DSISR_EAR	0x00100000

/*
 * Bits in SRR1 on ISI:
 */
#define	ISSRR1_NOTFOUND	0x40000000
#define	ISSRR1_DIRECT	0x10000000
#define	ISSRR1_PROTECT	0x08000000
#define	ISSRR1_SEGMENT	0x00200000

#endif	/* _POWERPC_PTE_H_ */
