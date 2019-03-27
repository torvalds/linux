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

#ifndef __KVM_ARM_H__
#define	__KVM_ARM_H__

typedef uint32_t	arm_physaddr_t;
typedef uint32_t	arm_pd_entry_t;
typedef uint32_t	arm_pt_entry_t;

#define	ARM_PAGE_SHIFT	12
#define	ARM_PAGE_SIZE	(1 << ARM_PAGE_SHIFT)	/* Page size */
#define	ARM_PAGE_MASK	(ARM_PAGE_SIZE - 1)

#define	ARM_L1_TABLE_SIZE	0x4000		/* 16K */

#define	ARM_L1_S_SIZE	0x00100000	/* 1M */
#define	ARM_L1_S_OFFSET	(ARM_L1_S_SIZE - 1)
#define	ARM_L1_S_FRAME	(~ARM_L1_S_OFFSET)
#define	ARM_L1_S_SHIFT	20

#define	ARM_L2_L_SIZE	0x00010000	/* 64K */
#define	ARM_L2_L_OFFSET	(ARM_L2_L_SIZE - 1)
#define	ARM_L2_L_FRAME	(~ARM_L2_L_OFFSET)
#define	ARM_L2_L_SHIFT	16

#define	ARM_L2_S_SIZE	0x00001000	/* 4K */
#define	ARM_L2_S_OFFSET	(ARM_L2_S_SIZE - 1)
#define	ARM_L2_S_FRAME	(~ARM_L2_S_OFFSET)
#define	ARM_L2_S_SHIFT	12
#define	ARM_L2_TEX1	0x00000080
#define	ARM_PTE2_RO	ARM_L2_TEX1
#define	ARM_L2_NX	0x00000001
#define	ARM_PTE2_NX	ARM_L2_NX

/*
 * Note: L2_S_PROT_W differs depending on whether the system is generic or
 *       xscale.  This isn't easily accessible in this context, so use an
 *       approximation of 'xscale' which is a subset of 'generic'.
 */
#define	ARM_L2_AP0(x)	((x) << 4)
#define	ARM_AP_W	0x01
#define	ARM_L2_S_PROT_W	(ARM_L2_AP0(ARM_AP_W))

#define	ARM_L1_TYPE_INV	0x00		/* Invalid (fault) */
#define	ARM_L1_TYPE_C	0x01		/* Coarse L2 */
#define	ARM_L1_TYPE_S	0x02		/* Section */
#define	ARM_L1_TYPE_MASK	0x03		/* Mask	of type	bits */

#define	ARM_L1_S_ADDR_MASK	0xfff00000	/* phys	address	of section */
#define	ARM_L1_C_ADDR_MASK	0xfffffc00	/* phys	address	of L2 Table */

#define	ARM_L2_TYPE_INV	0x00		/* Invalid (fault) */
#define	ARM_L2_TYPE_L	0x01		/* Large Page - 64k */
#define	ARM_L2_TYPE_S	0x02		/* Small Page -  4k */
#define	ARM_L2_TYPE_T	0x03		/* Tiny Page  -  1k - not used */
#define	ARM_L2_TYPE_MASK	0x03

#ifdef __arm__
#include <machine/acle-compat.h>

#if __ARM_ARCH >= 6
#include <machine/pte-v6.h>
#else
#include <machine/pte-v4.h>
#endif

_Static_assert(PAGE_SHIFT == ARM_PAGE_SHIFT, "PAGE_SHIFT mismatch");
_Static_assert(PAGE_SIZE == ARM_PAGE_SIZE, "PAGE_SIZE mismatch");
_Static_assert(PAGE_MASK == ARM_PAGE_MASK, "PAGE_MASK mismatch");
_Static_assert(L1_TABLE_SIZE == ARM_L1_TABLE_SIZE, "L1_TABLE_SIZE mismatch");
_Static_assert(L1_S_SIZE == ARM_L1_S_SIZE, "L1_S_SIZE mismatch");
_Static_assert(L1_S_OFFSET == ARM_L1_S_OFFSET, "L1_S_OFFSET mismatch");
_Static_assert(L1_S_FRAME == ARM_L1_S_FRAME, "L1_S_FRAME mismatch");
_Static_assert(L1_S_SHIFT == ARM_L1_S_SHIFT, "L1_S_SHIFT mismatch");
_Static_assert(L2_L_SIZE == ARM_L2_L_SIZE, "L2_L_SIZE mismatch");
_Static_assert(L2_L_OFFSET == ARM_L2_L_OFFSET, "L2_L_OFFSET mismatch");
_Static_assert(L2_L_FRAME == ARM_L2_L_FRAME, "L2_L_FRAME mismatch");
_Static_assert(L2_L_SHIFT == ARM_L2_L_SHIFT, "L2_L_SHIFT mismatch");
_Static_assert(L2_S_SIZE == ARM_L2_S_SIZE, "L2_S_SIZE mismatch");
_Static_assert(L2_S_OFFSET == ARM_L2_S_OFFSET, "L2_S_OFFSET mismatch");
_Static_assert(L2_S_FRAME == ARM_L2_S_FRAME, "L2_S_FRAME mismatch");
_Static_assert(L2_S_SHIFT == ARM_L2_S_SHIFT, "L2_S_SHIFT mismatch");
_Static_assert(L1_TYPE_INV == ARM_L1_TYPE_INV, "L1_TYPE_INV mismatch");
_Static_assert(L1_TYPE_C == ARM_L1_TYPE_C, "L1_TYPE_C mismatch");
_Static_assert(L1_TYPE_S == ARM_L1_TYPE_S, "L1_TYPE_S mismatch");
_Static_assert(L1_TYPE_MASK == ARM_L1_TYPE_MASK, "L1_TYPE_MASK mismatch");
_Static_assert(L1_S_ADDR_MASK == ARM_L1_S_ADDR_MASK, "L1_S_ADDR_MASK mismatch");
_Static_assert(L1_C_ADDR_MASK == ARM_L1_C_ADDR_MASK, "L1_C_ADDR_MASK mismatch");
_Static_assert(L2_TYPE_INV == ARM_L2_TYPE_INV, "L2_TYPE_INV mismatch");
_Static_assert(L2_TYPE_L == ARM_L2_TYPE_L, "L2_TYPE_L mismatch");
_Static_assert(L2_TYPE_S == ARM_L2_TYPE_S, "L2_TYPE_S mismatch");
#if __ARM_ARCH < 6
_Static_assert(L2_TYPE_T == ARM_L2_TYPE_T, "L2_TYPE_T mismatch");
#endif
_Static_assert(L2_TYPE_MASK == ARM_L2_TYPE_MASK, "L2_TYPE_MASK mismatch");
#endif

int	_arm_native(kvm_t *);

#endif /* !__KVM_ARM_H__ */
