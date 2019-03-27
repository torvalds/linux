/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1991 Regents of the University of California.
 * All rights reserved.
 *
 * Copyright (c) 2018 The FreeBSD Foundation
 * All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * the Systems Programming Group of the University of Utah Computer
 * Science Department and William Jolitz of UUNET Technologies Inc.
 *
 * Portions of this software were developed by
 * Konstantin Belousov <kib@FreeBSD.org> under sponsorship from
 * the FreeBSD Foundation.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * Derived from hp300 version by Mike Hibler, this version by William
 * Jolitz uses a recursive map [a pde points to the page directory] to
 * map the page tables using the pagetables themselves. This is done to
 * reduce the impact on kernel virtual memory for lots of sparse address
 * space, and to reduce the cost of memory to each process.
 *
 *	from: hp300: @(#)pmap.h	7.2 (Berkeley) 12/16/90
 *	from: @(#)pmap.h	7.4 (Berkeley) 5/12/91
 * $FreeBSD$
 */

#ifndef _MACHINE_PMAP_NOPAE_H
#define	_MACHINE_PMAP_NOPAE_H

#define	NTRPPTD		1
#define	LOWPTDI		1
#define	KERNPTDI	2

#define NPGPTD		1
#define NPGPTD_SHIFT	10
#undef	PDRSHIFT
#define	PDRSHIFT	PDRSHIFT_NOPAE
#undef	NBPDR
#define NBPDR		(1 << PDRSHIFT_NOPAE)	/* bytes/page dir */

#define	PG_FRAME	PG_FRAME_NOPAE
#define	PG_PS_FRAME	PG_PS_FRAME_NOPAE

#define KVA_PAGES	(256*4)

#ifndef NKPT
#define	NKPT		30
#endif

typedef uint32_t pd_entry_t;
typedef uint32_t pt_entry_t;
typedef	uint32_t pdpt_entry_t;	/* Only to keep struct pmap layout. */

#define	PTESHIFT	(2)
#define	PDESHIFT	(2)

#define	pde_cmpset(pdep, old, new)	atomic_cmpset_int(pdep, old, new)
#define	pte_load_store(ptep, pte)	atomic_swap_int(ptep, pte)
#define	pte_load_clear(ptep)		atomic_swap_int(ptep, 0)
#define	pte_store(ptep, pte) do { \
	*(u_int *)(ptep) = (u_int)(pte); \
} while (0)
#define	pte_store_zero(ptep, pte)	pte_store(ptep, pte)
#define	pte_load(ptep)			atomic_load_int(ptep)

extern pt_entry_t PTmap[];
extern pd_entry_t PTD[];
extern pd_entry_t PTDpde[];
extern pd_entry_t *IdlePTD_nopae;
extern pt_entry_t *KPTmap_nopae;

struct pmap;
pt_entry_t *__CONCAT(PMTYPE, pmap_pte)(struct pmap *, vm_offset_t) __pure2;

#endif
