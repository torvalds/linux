/*-
 * Copyright (c) 2015 John H. Baldwin <jhb@FreeBSD.org>
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

#ifndef __KVM_SPARC64_H__
#define	__KVM_SPARC64_H__

#ifdef __sparc64__
#include <sys/queue.h>
#include <machine/tlb.h>
#include <machine/tte.h>
#include <vm/vm.h>
#include <vm/vm_param.h>
#endif

#define	SPARC64_PAGE_SHIFT	13
#define	SPARC64_PAGE_SIZE	(1 << SPARC64_PAGE_SHIFT)
#define	SPARC64_PAGE_MASK	(SPARC64_PAGE_SIZE - 1)

#define	SPARC64_MIN_DIRECT_ADDRESS	(0xfffff80000000000)

#define	SPARC64_DIRECT_ADDRESS_BITS	(43)
#define	SPARC64_DIRECT_ADDRESS_MASK					\
	(((uint64_t)1 << SPARC64_DIRECT_ADDRESS_BITS) - 1)

#define	SPARC64_DIRECT_TO_PHYS(va)	((va) & SPARC64_DIRECT_ADDRESS_MASK)

#define	SPARC64_TTE_SHIFT	(5)

#define	SPARC64_TD_SIZE_SHIFT	(61)
#define	SPARC64_TD_PA_SHIFT	(13)

#define	SPARC64_TD_SIZE_BITS	(2)
#define	SPARC64_TD_PA_CH_BITS	(30)	/* US-III{,i,+}, US-IV{,+}, SPARC64 V */
#define	SPARC64_TD_PA_BITS	SPARC64_TD_PA_CH_BITS

#define	SPARC64_TD_SIZE_MASK	(((uint64_t)1 << SPARC64_TD_SIZE_BITS) - 1)
#define	SPARC64_TD_PA_MASK	(((uint64_t)1 << SPARC64_TD_PA_BITS) - 1)

#define	SPARC64_TD_V		((uint64_t)1 << 63)

#define	SPARC64_TV_SIZE_BITS	(SPARC64_TD_SIZE_BITS)
#define	SPARC64_TV_VPN(va, sz)						\
	((((va) >> SPARC64_TTE_PAGE_SHIFT(sz)) << SPARC64_TV_SIZE_BITS) | sz)

#define	SPARC64_TTE_SIZE_SPREAD	(3)
#define	SPARC64_TTE_PAGE_SHIFT(sz)					\
	(SPARC64_PAGE_SHIFT + ((sz) * SPARC64_TTE_SIZE_SPREAD))

#define	SPARC64_TTE_GET_SIZE(tp)					\
	(((tp)->tte_data >> SPARC64_TD_SIZE_SHIFT) & SPARC64_TD_SIZE_MASK)

#define	SPARC64_TTE_GET_PA(tp)						\
	((tp)->tte_data & (SPARC64_TD_PA_MASK << SPARC64_TD_PA_SHIFT))

struct sparc64_tte {
	uint64_t tte_vpn;
	uint64_t tte_data;
};

static __inline int
sparc64_tte_match(struct sparc64_tte *tp, kvaddr_t va)
{

	return (((tp->tte_data & SPARC64_TD_V) != 0) &&
	    (tp->tte_vpn == SPARC64_TV_VPN(va, SPARC64_TTE_GET_SIZE(tp))));
}

#ifdef __sparc64__
_Static_assert(PAGE_SHIFT == SPARC64_PAGE_SHIFT, "PAGE_SHIFT mismatch");
_Static_assert(PAGE_SIZE == SPARC64_PAGE_SIZE, "PAGE_SIZE mismatch");
_Static_assert(PAGE_MASK == SPARC64_PAGE_MASK, "PAGE_MASK mismatch");
_Static_assert(VM_MIN_DIRECT_ADDRESS == SPARC64_MIN_DIRECT_ADDRESS,
    "VM_MIN_DIRECT_ADDRESS mismatch");
_Static_assert(TLB_DIRECT_ADDRESS_BITS == SPARC64_DIRECT_ADDRESS_BITS,
    "TLB_DIRECT_ADDRESS_BITS mismatch");
_Static_assert(TLB_DIRECT_ADDRESS_MASK == SPARC64_DIRECT_ADDRESS_MASK,
    "TLB_DIRECT_ADDRESS_MASK mismatch");
_Static_assert(TTE_SHIFT == SPARC64_TTE_SHIFT, "TTE_SHIFT mismatch");
_Static_assert(TD_SIZE_SHIFT == SPARC64_TD_SIZE_SHIFT,
    "TD_SIZE_SHIFT mismatch");
_Static_assert(TD_PA_SHIFT == SPARC64_TD_PA_SHIFT,
    "TD_PA_SHIFT mismatch");
_Static_assert(TD_SIZE_BITS == SPARC64_TD_SIZE_BITS, "TD_SIZE_BITS mismatch");
_Static_assert(TD_PA_BITS == SPARC64_TD_PA_BITS, "TD_PA_BITS mismatch");
_Static_assert(TD_SIZE_MASK == SPARC64_TD_SIZE_MASK, "TD_SIZE_MASK mismatch");
_Static_assert(TD_PA_MASK == SPARC64_TD_PA_MASK, "TD_PA_MASK mismatch");
_Static_assert(TD_V == SPARC64_TD_V, "TD_V mismatch");
_Static_assert(TV_SIZE_BITS == SPARC64_TV_SIZE_BITS, "TV_SIZE_BITS mismatch");
_Static_assert(TTE_SIZE_SPREAD == SPARC64_TTE_SIZE_SPREAD,
    "TTE_SIZE_SPREAD mismatch");
#endif

#endif /* !__KVM_SPARC64_H__ */
