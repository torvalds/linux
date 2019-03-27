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

#ifndef __KVM_I386_H__
#define	__KVM_I386_H__

#ifdef __i386__
#include <vm/vm.h>
#include <vm/pmap.h>
#endif

typedef uint32_t	i386_physaddr_t;
typedef uint32_t	i386_pte_t;
typedef uint32_t	i386_pde_t;
typedef uint64_t	i386_physaddr_pae_t;
typedef	uint64_t	i386_pte_pae_t;
typedef	uint64_t	i386_pde_pae_t;

#define	I386_PAGE_SHIFT		12
#define	I386_PAGE_SIZE		(1 << I386_PAGE_SHIFT)
#define	I386_PAGE_MASK		(I386_PAGE_SIZE - 1)
#define	I386_NPTEPG		(I386_PAGE_SIZE / sizeof(i386_pte_t))
#define	I386_PDRSHIFT		22
#define	I386_NBPDR		(1 << I386_PDRSHIFT)
#define	I386_PAGE_PS_MASK	(I386_NBPDR - 1)
#define	I386_NPTEPG_PAE		(I386_PAGE_SIZE / sizeof(i386_pte_pae_t))
#define	I386_PDRSHIFT_PAE	21
#define	I386_NBPDR_PAE		(1 << I386_PDRSHIFT_PAE)
#define	I386_PAGE_PS_MASK_PAE	(I386_NBPDR_PAE - 1)

/* Source: i386/include/pmap.h */
#define	I386_PG_V		0x001
#define	I386_PG_RW		0x002
#define	I386_PG_PS		0x080
#define	I386_PG_NX		(1ULL << 63)
#define	I386_PG_FRAME_PAE	(0x000ffffffffff000ull)
#define	I386_PG_PS_FRAME_PAE	(0x000fffffffe00000ull)
#define	I386_PG_FRAME		(0xfffff000)
#define	I386_PG_PS_FRAME	(0xffc00000)

#ifdef __i386__
_Static_assert(PAGE_SHIFT == I386_PAGE_SHIFT, "PAGE_SHIFT mismatch");
_Static_assert(PAGE_SIZE == I386_PAGE_SIZE, "PAGE_SIZE mismatch");
_Static_assert(PAGE_MASK == I386_PAGE_MASK, "PAGE_MASK mismatch");
#if 0
_Static_assert(NPTEPG == I386_NPTEPG, "NPTEPG mismatch");
_Static_assert(NBPDR == I386_NBPDR, "NBPDR mismatch");
#endif
_Static_assert(PDRSHIFT_NOPAE == I386_PDRSHIFT, "PDRSHIFT mismatch");

_Static_assert(PG_V == I386_PG_V, "PG_V mismatch");
_Static_assert(PG_PS == I386_PG_PS, "PG_PS mismatch");
_Static_assert((u_int)PG_FRAME_NOPAE == I386_PG_FRAME, "PG_FRAME mismatch");
_Static_assert(PG_PS_FRAME_NOPAE == I386_PG_PS_FRAME, "PG_PS_FRAME mismatch");
#endif

int	_i386_native(kvm_t *);

#endif /* !__KVM_I386_H__ */
