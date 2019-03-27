/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1997 Berkeley Software Design, Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Berkeley Software Design Inc's name may not be used to endorse or
 *    promote products derived from this software without specific prior
 *    written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY BERKELEY SOFTWARE DESIGN INC ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL BERKELEY SOFTWARE DESIGN INC BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *	from: BSDI: pmap.v9.h,v 1.10.2.6 1999/08/23 22:18:44 cp Exp
 * $FreeBSD$
 */

#ifndef	_MACHINE_TTE_H_
#define	_MACHINE_TTE_H_

#define	TTE_SHIFT	(5)

#define	TD_SIZE_SHIFT	(61)
#define	TD_SOFT2_SHIFT	(50)
#define	TD_RSVD2_SHIFT	(49)
#define	TD_SIZE2_SHIFT	(48)
#define	TD_DIAG_SF_SHIFT	(41)
#define	TD_RSVD_CH_SHIFT	(43)
#define	TD_RSVD_OC_SHIFT	(47)
#define	TD_RSVD_PT_SHIFT	TD_RSVD_CH_SHIFT
#define	TD_RSVD_VE_SHIFT	(41)
#define	TD_PA_SHIFT	(13)
#define	TD_SOFT_SHIFT	(7)

#define	TD_SIZE_BITS	(2)
#define	TD_SOFT2_BITS	(9)
#define	TD_RSVD2_BITS	(1)	/* US-IV+, SPARC64 VI, VII, VIIIfx */
#define	TD_SIZE2_BITS	(1)	/* US-IV+, SPARC64 VI, VII, VIIIfx */
#define	TD_DIAG_SF_BITS	(9)	/* US-I, II{,e,i} */
#define	TD_RSVD_CH_BITS	(7)	/* US-III{,i,+}, US-IV, SPARC64 V */
#define	TD_RSVD_OC_BITS	(1)	/* SPARC64 VI, VII */
#define	TD_RSVD_PT_BITS	(5)	/* US-IV+, SPARC64 VI, VII */
#define	TD_RSVD_VE_BITS	(7)	/* SPARC64 VIIIfx */
#define	TD_PA_CH_BITS	(30)	/* US-III{,i,+}, US-IV{,+}, SPARC64 V */
#define	TD_PA_OC_BITS	(34)	/* SPARC64 VI, VII */
#define	TD_PA_SF_BITS	(28)	/* US-I, II{,e,i}, SPARC64 VIIIfx */
#define	TD_PA_BITS	TD_PA_CH_BITS
#define	TD_SOFT_BITS	(6)

#define	TD_SIZE_MASK	((1UL << TD_SIZE_BITS) - 1)
#define	TD_SOFT2_MASK	((1UL << TD_SOFT2_BITS) - 1)
#define	TD_RSVD2_MASK	((1UL << TD_RSVD2_BITS) - 1)
#define	TD_SIZE2_MASK	((1UL << TD_SIZE2_BITS) - 1)
#define	TD_DIAG_SF_MASK	((1UL << TD_DIAG_SF_BITS) - 1)
#define	TD_RSVD_CH_MASK	((1UL << TD_RSVD_CH_BITS) - 1)
#define	TD_RSVD_OC_MASK	((1UL << TD_RSVD_OC_BITS) - 1)
#define	TD_RSVD_PT_MASK	((1UL << TD_RSVD_PT_BITS) - 1)
#define	TD_RSVD_VE_MASK	((1UL << TD_RSVD_VE_BITS) - 1)
#define	TD_PA_CH_MASK	((1UL << TD_PA_CH_BITS) - 1)
#define	TD_PA_OC_MASK	((1UL << TD_PA_OC_BITS) - 1)
#define	TD_PA_SF_MASK	((1UL << TD_PA_SF_BITS) - 1)
#define	TD_PA_MASK	((1UL << TD_PA_BITS) - 1)
#define	TD_SOFT_MASK	((1UL << TD_SOFT_BITS) - 1)

#define	TS_8K		(0UL)
#define	TS_64K		(1UL)
#define	TS_512K		(2UL)
#define	TS_4M		(3UL)
#define	TS_32M		(4UL)	/* US-IV+, SPARC64 VI, VII only */
#define	TS_256M		(5UL)	/* US-IV+, SPARC64 VI, VII only */
#define	TS_2G		(6UL)	/* SPARC64 VIIIfx only */

#define	TS_MIN		TS_8K
#define	TS_MAX		TS_4M

#define	TD_V		(1UL << 63)
#define	TD_8K		(TS_8K << TD_SIZE_SHIFT)
#define	TD_64K		(TS_64K << TD_SIZE_SHIFT)
#define	TD_512K		(TS_512K << TD_SIZE_SHIFT)
#define	TD_4M		(TS_4M << TD_SIZE_SHIFT)
#define	TD_32M								\
	(((TS_32M & TD_SIZE_MASK) << TD_SIZE_SHIFT) |			\
	(TD_SIZE2_MASK << TD_SIZE2_SHIFT))
#define	TD_256M								\
	(((TS_256M & TD_SIZE_MASK) << TD_SIZE_SHIFT) |			\
	(TD_SIZE2_MASK << TD_SIZE2_SHIFT))
#define	TD_2G								\
	(((TS_2G & TD_SIZE_MASK) << TD_SIZE_SHIFT) |			\
	(TD_SIZE2_MASK << TD_SIZE2_SHIFT))
#define	TD_NFO		(1UL << 60)
#define	TD_IE		(1UL << 59)
#define	TD_PA(pa)	((pa) & (TD_PA_MASK << TD_PA_SHIFT))
/* NOTE: bit 6 of TD_SOFT will be sign-extended if used as an immediate. */
#define	TD_FAKE		((1UL << 5) << TD_SOFT_SHIFT)
#define	TD_EXEC		((1UL << 4) << TD_SOFT_SHIFT)
#define	TD_REF		((1UL << 3) << TD_SOFT_SHIFT)
#define	TD_PV		((1UL << 2) << TD_SOFT_SHIFT)
#define	TD_SW		((1UL << 1) << TD_SOFT_SHIFT)
#define	TD_WIRED	((1UL << 0) << TD_SOFT_SHIFT)
#define	TD_L		(1UL << 6)
#define	TD_CP		(1UL << 5)
#define	TD_CV		(1UL << 4)
#define	TD_E		(1UL << 3)
#define	TD_P		(1UL << 2)
#define	TD_W		(1UL << 1)
#define	TD_G		(1UL << 0)

#define	TV_SIZE_BITS	(TD_SIZE_BITS)
#define	TV_VPN(va, sz)	((((va) >> TTE_PAGE_SHIFT(sz)) << TV_SIZE_BITS) | sz)

#define	TTE_SIZE_SPREAD	(3)
#define	TTE_PAGE_SHIFT(sz)						\
	(PAGE_SHIFT + ((sz) * TTE_SIZE_SPREAD))

#define	TTE_GET_SIZE(tp)						\
	(((tp)->tte_data >> TD_SIZE_SHIFT) & TD_SIZE_MASK)
#define	TTE_GET_PAGE_SHIFT(tp)						\
	TTE_PAGE_SHIFT(TTE_GET_SIZE(tp))
#define	TTE_GET_PAGE_SIZE(tp)						\
	(1 << TTE_GET_PAGE_SHIFT(tp))
#define	TTE_GET_PAGE_MASK(tp)						\
	(TTE_GET_PAGE_SIZE(tp) - 1)

#define	TTE_GET_PA(tp)							\
	((tp)->tte_data & (TD_PA_MASK << TD_PA_SHIFT))
#define	TTE_GET_VPN(tp)							\
	((tp)->tte_vpn >> TV_SIZE_BITS)
#define	TTE_GET_VA(tp)							\
	(TTE_GET_VPN(tp) << TTE_GET_PAGE_SHIFT(tp))
#define	TTE_GET_PMAP(tp)						\
	(((tp)->tte_data & TD_P) != 0 ?	(kernel_pmap) :			\
	(PHYS_TO_VM_PAGE(pmap_kextract((vm_offset_t)(tp)))->md.pmap))
#define	TTE_ZERO(tp)							\
	memset(tp, 0, sizeof(*tp))

struct pmap;

struct tte {
	u_long	tte_vpn;
	u_long	tte_data;
	TAILQ_ENTRY(tte) tte_link;
};

static __inline int
tte_match(struct tte *tp, vm_offset_t va)
{

	return (((tp->tte_data & TD_V) != 0) &&
	    (tp->tte_vpn == TV_VPN(va, TTE_GET_SIZE(tp))));
}

#endif /* !_MACHINE_TTE_H_ */
