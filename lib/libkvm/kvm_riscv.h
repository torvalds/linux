/*-
 * Copyright (c) 2015 John H. Baldwin <jhb@FreeBSD.org>
 * Copyright (c) 2019 Mitchell Horne
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

#ifndef __KVM_RISCV_H__
#define	__KVM_RISCV_H__

#ifdef __riscv
#include <machine/pte.h>
#endif

typedef uint64_t	riscv_physaddr_t;
typedef uint64_t	riscv_pt_entry_t;

#define	RISCV_PAGE_SHIFT	12
#define	RISCV_PAGE_SIZE		(1 << RISCV_PAGE_SHIFT)
#define	RISCV_PAGE_MASK		(RISCV_PAGE_SIZE - 1)

/* Source: sys/riscv/include/pte.h */
#define	RISCV_L3_SHIFT		12
#define	RISCV_L3_SIZE		(1 << L3_SHIFT)
#define	RISCV_L3_OFFSET 	(L3_SIZE - 1)

#define	RISCV_PTE_SW_MANAGED	(1 << 9)
#define	RISCV_PTE_SW_WIRED	(1 << 8)
#define	RISCV_PTE_D		(1 << 7) /* Dirty */
#define	RISCV_PTE_A		(1 << 6) /* Accessed */
#define	RISCV_PTE_G		(1 << 5) /* Global */
#define	RISCV_PTE_U		(1 << 4) /* User */
#define	RISCV_PTE_X		(1 << 3) /* Execute */
#define	RISCV_PTE_W		(1 << 2) /* Write */
#define	RISCV_PTE_R		(1 << 1) /* Read */
#define	RISCV_PTE_V		(1 << 0) /* Valid */
#define	RISCV_PTE_RWX		(RISCV_PTE_R | RISCV_PTE_W | RISCV_PTE_X)

#define	RISCV_PTE_PPN0_S	10

#ifdef __riscv
_Static_assert(sizeof(pt_entry_t) == sizeof(riscv_pt_entry_t),
    "pt_entry_t size mismatch");

_Static_assert(PAGE_SHIFT == RISCV_PAGE_SHIFT, "PAGE_SHIFT mismatch");
_Static_assert(PAGE_SIZE == RISCV_PAGE_SIZE, "PAGE_SIZE mismatch");
_Static_assert(PAGE_MASK == RISCV_PAGE_MASK, "PAGE_MASK mismatch");

_Static_assert(L3_SHIFT == RISCV_L3_SHIFT, "L3_SHIFT mismatch");
_Static_assert(L3_SIZE == RISCV_L3_SIZE, "L3_SIZE mismatch");
_Static_assert(L3_OFFSET == RISCV_L3_OFFSET, "L3_OFFSET mismatch");
_Static_assert(PTE_PPN0_S == RISCV_PTE_PPN0_S, "PTE_PPN0_S mismatch");

_Static_assert(PTE_SW_MANAGED == RISCV_PTE_SW_MANAGED,
    "PTE_SW_MANAGED mismatch");
_Static_assert(PTE_SW_WIRED == RISCV_PTE_SW_WIRED, "PTE_SW_WIRED mismatch");
_Static_assert(PTE_D == RISCV_PTE_D, "PTE_D mismatch");
_Static_assert(PTE_A == RISCV_PTE_A, "PTE_A mismatch");
_Static_assert(PTE_G == RISCV_PTE_G, "PTE_G mismatch");
_Static_assert(PTE_U == RISCV_PTE_U, "PTE_U mismatch");
_Static_assert(PTE_X == RISCV_PTE_X, "PTE_X mismatch");
_Static_assert(PTE_W == RISCV_PTE_W, "PTE_W mismatch");
_Static_assert(PTE_R == RISCV_PTE_R, "PTE_R mismatch");
_Static_assert(PTE_V == RISCV_PTE_V, "PTE_V mismatch");
_Static_assert(PTE_RWX == RISCV_PTE_RWX, "PTE_RWX mismatch");
#endif

#endif /* !__KVM_RISCV_H__ */
