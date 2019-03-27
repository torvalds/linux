/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright 2003-2011 Netlogic Microsystems (Netlogic). All rights
 * reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY Netlogic Microsystems ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL NETLOGIC OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGE.
 *
 * NETLOGIC_BSD
 * $FreeBSD$
 */

#ifndef __XLP_MMU_H__
#define	__XLP_MMU_H__

#include <mips/nlm/hal/mips-extns.h>

static __inline__ uint32_t
nlm_read_c0_config6(void)
{
	uint32_t rv;

	__asm__ __volatile__ (
		".set	push\n"
		".set	mips64\n"
		"mfc0	%0, $16, 6\n"
		".set	pop\n"
		: "=r" (rv));

        return rv;
}

static __inline__ void
nlm_write_c0_config6(uint32_t value)
{
	__asm__ __volatile__ (
		".set	push\n"
		".set	mips64\n"
		"mtc0	%0, $16, 6\n"
		".set	pop\n"
		: : "r" (value));
}

static __inline__ uint32_t
nlm_read_c0_config7(void)
{
	uint32_t rv;

	__asm__ __volatile__ (
		".set	push\n"
		".set	mips64\n"
		"mfc0	%0, $16, 7\n"
		".set	pop\n"
		: "=r" (rv));

        return rv;
}

static __inline__ void
nlm_write_c0_config7(uint32_t value)
{
	__asm__ __volatile__ (
		".set	push\n"
		".set	mips64\n"
		"mtc0	%0, $16, 7\n"
		".set	pop\n"
		: : "r" (value));
}
/**
 * On power on reset, XLP comes up with 64 TLBs.
 * Large-variable-tlb's (ELVT) and extended TLB is disabled.
 * Enabling large-variable-tlb's sets up the standard
 * TLB size from 64 to 128 TLBs.
 * Enabling fixed TLB (EFT) sets up an additional 2048 tlbs.
 * ELVT + EFT = 128 + 2048 = 2176 TLB entries.
 * threads  64-entry-standard-tlb    128-entry-standard-tlb
 * per      std-tlb-only| std+EFT  | std-tlb-only| std+EFT
 * core                 |          |             |
 * --------------------------------------------------------
 * 1         64           64+2048     128          128+2048
 * 2         64           64+1024      64           64+1024
 * 4         32           32+512       32           32+512
 *
 * 1(G)      64           64+2048     128          128+2048
 * 2(G)      128         128+2048     128          128+2048
 * 4(G)      128         128+2048     128          128+2048
 * (G) = Global mode
 */


/* en = 1 to enable
 * en = 0 to disable
 */
static __inline__ void nlm_large_variable_tlb_en (int en)
{
	unsigned int val;

	val = nlm_read_c0_config6();
	val |= (en << 5);
	nlm_write_c0_config6(val);
	return;
}

/* en = 1 to enable
 * en = 0 to disable
 */
static __inline__ void nlm_pagewalker_en(int en)
{
	unsigned int val;

	val = nlm_read_c0_config6();
	val |= (en << 3);
	nlm_write_c0_config6(val);
	return;
}

/* en = 1 to enable
 * en = 0 to disable
 */
static __inline__ void nlm_extended_tlb_en(int en)
{
	unsigned int val;

	val = nlm_read_c0_config6();
	val |= (en << 2);
	nlm_write_c0_config6(val);
	return;
}

static __inline__ int nlm_get_num_combined_tlbs(void)
{
	return (((nlm_read_c0_config6() >> 16) & 0xffff) + 1);
}

/* get number of variable TLB entries */
static __inline__ int nlm_get_num_vtlbs(void)
{
	return (((nlm_read_c0_config6() >> 6) & 0x3ff) + 1);
}

static __inline__ void nlm_setup_extended_pagemask(int mask)
{
	nlm_write_c0_config7(mask);
}

#endif
