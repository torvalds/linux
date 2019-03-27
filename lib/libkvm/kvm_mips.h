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

#ifndef __KVM_MIPS_H__
#define	__KVM_MIPS_H__

#ifdef __mips__
#include <machine/pte.h>
#endif

typedef uint64_t	mips_physaddr_t;

typedef uint32_t	mips32_pte_t;
typedef uint64_t	mips64_pte_t;

#define	MIPS_PAGE_SHIFT		12
#define	MIPS_PAGE_SIZE		(1 << MIPS_PAGE_SHIFT)
#define	MIPS_PAGE_MASK		(MIPS_PAGE_SIZE - 1)

#define	MIPS32_KSEG0_START	0x80000000
#define	MIPS32_KSEG0_END	0x9fffffff
#define	MIPS32_KSEG1_START	0xa0000000
#define	MIPS32_KSEG1_END	0xbfffffff
#define	MIPS64_KSEG0_START	0xffffffff80000000
#define	MIPS64_KSEG0_END	0xffffffff9fffffff
#define	MIPS64_KSEG1_START	0xffffffffa0000000
#define	MIPS64_KSEG1_END	0xffffffffbfffffff

#define	MIPS32_PFN_MASK		(0x1FFFFFC0)
#define	MIPS64_PFN_MASK		0x3FFFFFFC0
#define	MIPS_PFN_SHIFT		(6)

#define	MIPS_PFN_TO_PA(pfn)	(((pfn) >> MIPS_PFN_SHIFT) << MIPS_PAGE_SHIFT)
#define	MIPS32_PTE_TO_PFN(pte)	((pte) & MIPS32_PFN_MASK)
#define	MIPS32_PTE_TO_PA(pte)	(MIPS_PFN_TO_PA(MIPS32_PTE_TO_PFN((pte))))
#define	MIPS64_PTE_TO_PFN(pte)	((pte) & MIPS64_PFN_MASK)
#define	MIPS64_PTE_TO_PA(pte)	(MIPS_PFN_TO_PA(MIPS64_PTE_TO_PFN((pte))))

#define	MIPS32_SWBITS_SHIFT	29
#define	MIPS64_SWBITS_SHIFT	55
#define	MIPS_PTE_V		0x02
#define	MIPS32_PTE_RO		((mips32_pte_t)0x01 << MIPS32_SWBITS_SHIFT)
#define	MIPS64_PTE_RO		((mips64_pte_t)0x01 << MIPS64_SWBITS_SHIFT)

static inline mips32_pte_t
_mips32_pte_get(kvm_t *kd, u_long pteindex)
{
	mips32_pte_t *pte = _kvm_pmap_get(kd, pteindex, sizeof(*pte));

	return _kvm32toh(kd, *pte);
}

static inline mips64_pte_t
_mips64_pte_get(kvm_t *kd, u_long pteindex)
{
	mips64_pte_t *pte = _kvm_pmap_get(kd, pteindex, sizeof(*pte));

	return _kvm64toh(kd, *pte);
}

#ifdef __mips__
_Static_assert(PAGE_SHIFT == MIPS_PAGE_SHIFT, "PAGE_SHIFT mismatch");
_Static_assert(PAGE_SIZE == MIPS_PAGE_SIZE, "PAGE_SIZE mismatch");
_Static_assert(PAGE_MASK == MIPS_PAGE_MASK, "PAGE_MASK mismatch");
#ifdef __mips_n64
_Static_assert((uint64_t)MIPS_KSEG0_START == MIPS64_KSEG0_START,
    "MIPS_KSEG0_START mismatch");
_Static_assert((uint64_t)MIPS_KSEG0_END == MIPS64_KSEG0_END,
    "MIPS_KSEG0_END mismatch");
_Static_assert((uint64_t)MIPS_KSEG1_START == MIPS64_KSEG1_START,
    "MIPS_KSEG1_START mismatch");
_Static_assert((uint64_t)MIPS_KSEG1_END == MIPS64_KSEG1_END,
    "MIPS_KSEG1_END mismatch");
#else
_Static_assert((uint32_t)MIPS_KSEG0_START == MIPS32_KSEG0_START,
    "MIPS_KSEG0_START mismatch");
_Static_assert((uint32_t)MIPS_KSEG0_END == MIPS32_KSEG0_END,
    "MIPS_KSEG0_END mismatch");
_Static_assert((uint32_t)MIPS_KSEG1_START == MIPS32_KSEG1_START,
    "MIPS_KSEG1_START mismatch");
_Static_assert((uint32_t)MIPS_KSEG1_END == MIPS32_KSEG1_END,
    "MIPS_KSEG1_END mismatch");
#endif
#if defined(__mips_n64) || defined(__mips_n32)
_Static_assert(TLBLO_PFN_MASK == MIPS64_PFN_MASK, "TLBLO_PFN_MASK mismatch");
#else
_Static_assert(TLBLO_PFN_MASK == MIPS32_PFN_MASK, "TLBLO_PFN_MASK mismatch");
#endif
_Static_assert(TLBLO_PFN_SHIFT == MIPS_PFN_SHIFT, "TLBLO_PFN_SHIFT mismatch");
_Static_assert(TLB_PAGE_SHIFT == MIPS_PAGE_SHIFT, "TLB_PAGE_SHIFT mismatch");
#endif

#endif /* !__KVM_MIPS_H__ */
