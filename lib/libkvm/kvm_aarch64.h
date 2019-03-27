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

#ifndef __KVM_AARCH64_H__
#define	__KVM_AARCH64_H__

#ifdef __aarch64__
#include <machine/pte.h>
#endif

typedef uint64_t	aarch64_physaddr_t;
typedef uint64_t	aarch64_pte_t;

#define	AARCH64_PAGE_SHIFT	12
#define	AARCH64_PAGE_SIZE	(1 << AARCH64_PAGE_SHIFT)
#define	AARCH64_PAGE_MASK	(AARCH64_PAGE_SIZE - 1)

/* Source: arm64/include/pte.h */
#define	AARCH64_ATTR_MASK	0xfff0000000000fff
#define	AARCH64_ATTR_UXN	(1ULL << 54)
#define	AARCH64_ATTR_PXN	(1ULL << 53)
#define	AARCH64_ATTR_XN		(AARCH64_ATTR_PXN | AARCH64_ATTR_UXN)
#define	AARCH64_ATTR_AP(x)	((x) << 6)
#define	AARCH64_ATTR_AP_RO	(1 << 1)

#define	AARCH64_ATTR_DESCR_MASK	3

#define	AARCH64_L3_SHIFT	12
#define	AARCH64_L3_PAGE		0x3

#ifdef __aarch64__
_Static_assert(PAGE_SHIFT == AARCH64_PAGE_SHIFT, "PAGE_SHIFT mismatch");
_Static_assert(PAGE_SIZE == AARCH64_PAGE_SIZE, "PAGE_SIZE mismatch");
_Static_assert(PAGE_MASK == AARCH64_PAGE_MASK, "PAGE_MASK mismatch");
_Static_assert(ATTR_MASK == AARCH64_ATTR_MASK, "ATTR_MASK mismatch");
_Static_assert(ATTR_DESCR_MASK == AARCH64_ATTR_DESCR_MASK,
    "ATTR_DESCR_MASK mismatch");
_Static_assert(L3_SHIFT == AARCH64_L3_SHIFT, "L3_SHIFT mismatch");
_Static_assert(L3_PAGE == AARCH64_L3_PAGE, "L3_PAGE mismatch");
#endif

#endif /* !__KVM_AARCH64_H__ */
