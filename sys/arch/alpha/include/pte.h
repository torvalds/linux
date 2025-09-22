/* $OpenBSD: pte.h,v 1.12 2014/01/26 17:40:11 miod Exp $ */
/* $NetBSD: pte.h,v 1.26 1999/04/09 00:38:11 thorpej Exp $ */

/*-
 * Copyright (c) 1998 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Jason R. Thorpe of the Numerical Aerospace Simulation Facility,
 * NASA Ames Research Center.
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
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * Copyright (c) 1994, 1995, 1996 Carnegie-Mellon University.
 * All rights reserved.
 *
 * Author: Chris G. Demetriou
 * 
 * Permission to use, copy, modify and distribute this software and
 * its documentation is hereby granted, provided that both the copyright
 * notice and this permission notice appear in all copies of the
 * software, derivative works or modified versions, and any portions
 * thereof, and that both notices appear in supporting documentation.
 * 
 * CARNEGIE MELLON ALLOWS FREE USE OF THIS SOFTWARE IN ITS "AS IS" 
 * CONDITION.  CARNEGIE MELLON DISCLAIMS ANY LIABILITY OF ANY KIND 
 * FOR ANY DAMAGES WHATSOEVER RESULTING FROM THE USE OF THIS SOFTWARE.
 * 
 * Carnegie Mellon requests users of this software to return to
 *
 *  Software Distribution Coordinator  or  Software.Distribution@CS.CMU.EDU
 *  School of Computer Science
 *  Carnegie Mellon University
 *  Pittsburgh PA 15213-3890
 *
 * any improvements or extensions that they make and grant Carnegie the
 * rights to redistribute these changes.
 */

#ifndef _MACHINE_PTE_H_
#define	_MACHINE_PTE_H_

/*
 * Alpha page table entry.
 * Things which are in the VMS PALcode but not in the OSF PALcode
 * are marked with "(VMS)".
 *
 * This information derived from pp. (II) 3-3 - (II) 3-6 and
 * (III) 3-3 - (III) 3-5 of the "Alpha Architecture Reference Manual" by
 * Richard L. Sites.
 */

/*
 * Alpha Page Table Entry
 */

#include <machine/alpha_cpu.h>

typedef	alpha_pt_entry_t	pt_entry_t;

#define	PT_ENTRY_NULL	((pt_entry_t *) 0)
#define	PTESHIFT	3			/* pte size == 1 << PTESHIFT */

#define	PG_V		ALPHA_PTE_VALID
#define	PG_NV		0
#define	PG_FOR		ALPHA_PTE_FAULT_ON_READ
#define	PG_FOW		ALPHA_PTE_FAULT_ON_WRITE
#define	PG_FOE		ALPHA_PTE_FAULT_ON_EXECUTE
#define	PG_ASM		ALPHA_PTE_ASM
#define	PG_GH		ALPHA_PTE_GRANULARITY
#define	PG_KRE		ALPHA_PTE_KR
#define	PG_URE		ALPHA_PTE_UR
#define	PG_KWE		ALPHA_PTE_KW
#define	PG_UWE		ALPHA_PTE_UW
#define	PG_PROT		(ALPHA_PTE_PROT | PG_EXEC | PG_FOE)
#define	PG_RSVD		0x000000000000cc80	/* Reserved for hardware */
#define	PG_WIRED	0x0000000000010000	/* Wired. [SOFTWARE] */
#define	PG_PVLIST	0x0000000000020000	/* on pv list [SOFTWARE] */
#define	PG_EXEC		0x0000000000040000	/* execute perms [SOFTWARE] */
#define	PG_FRAME	ALPHA_PTE_PFN
#define	PG_SHIFT	32
#define	PG_PFNUM(x)	ALPHA_PTE_TO_PFN(x)

/*
 * These are the PALcode PTE bits that we care about when checking to see
 * if a PTE has changed in such a way as to require a TBI.
 */
#define	PG_PALCODE(x)	((x) & ALPHA_PTE_PALCODE)

#if defined(_KERNEL) || defined(__KVM_ALPHA_PRIVATE)
#define	NPTEPG_SHIFT	(PAGE_SHIFT - PTESHIFT)
#define	NPTEPG		(1L << NPTEPG_SHIFT)

#define	PTEMASK		(NPTEPG - 1)

#define	l3pte_index(va)	\
	(((vaddr_t)(va) >> PAGE_SHIFT) & PTEMASK)

#define	l2pte_index(va)	\
	(((vaddr_t)(va) >> (PAGE_SHIFT + NPTEPG_SHIFT)) & PTEMASK)

#define	l1pte_index(va) \
	(((vaddr_t)(va) >> (PAGE_SHIFT + 2 * NPTEPG_SHIFT)) & PTEMASK)

#define	VPT_INDEX(va)	\
	(((vaddr_t)(va) >> PAGE_SHIFT) & ((1 << 3 * NPTEPG_SHIFT) - 1))

/* Space mapped by one level 1 PTE */
#define	ALPHA_L1SEG_SIZE	(1L << ((2 * NPTEPG_SHIFT) + PAGE_SHIFT))

/* Space mapped by one level 2 PTE */
#define	ALPHA_L2SEG_SIZE	(1L << (NPTEPG_SHIFT + PAGE_SHIFT))

#define	alpha_trunc_l1seg(x)	(((u_long)(x)) & ~(ALPHA_L1SEG_SIZE-1))
#define	alpha_trunc_l2seg(x)	(((u_long)(x)) & ~(ALPHA_L2SEG_SIZE-1))
#endif /* _KERNEL || __KVM_ALPHA_PRIVATE */

#ifdef _KERNEL
extern	pt_entry_t *kernel_lev1map;	/* kernel level 1 page table */
#endif /* _KERNEL */

#endif /* ! _MACHINE_PTE_H_ */
