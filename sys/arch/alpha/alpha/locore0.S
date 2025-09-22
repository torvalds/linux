/* $OpenBSD: locore0.S,v 1.3 2024/10/10 19:33:05 miod Exp $ */
/* $NetBSD: locore.s,v 1.94 2001/04/26 03:10:44 ross Exp $ */

/*-
 * Copyright (c) 1999, 2000 The NetBSD Foundation, Inc.
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

.file	1 __FILE__

#include <machine/asm.h>

#include "assym.h"

#if defined(MULTIPROCESSOR)

#define	GET_CPUINFO		call_pal PAL_OSF1_rdval

#else	/* if not MULTIPROCESSOR... */

IMPORT(cpu_info_primary, CPU_INFO_SIZEOF)

#define	GET_CPUINFO		lda v0, cpu_info_primary

#endif

/*
 * Perform actions necessary to switch to a new context.  The
 * hwpcb should be in a0.  Clobbers v0, t0, t8..t11, a0.
 */
#define	SWITCH_CONTEXT							\
	/* Make a note of the context we're running on. */		\
	GET_CPUINFO						;	\
	stq	a0, CPU_INFO_CURPCB(v0)				;	\
									\
	/* Swap in the new context. */					\
	call_pal PAL_OSF1_swpctx


	/* don't reorder instructions; paranoia. */
	.set noreorder
	.text

/*
 * This is for kvm_mkdb, and should be the address of the beginning
 * of the kernel text segment (not necessarily the same as kernbase).
 */
	EXPORT(kernel_text)
.loc	1 __LINE__
kernel_text:

/*
 * bootstack: a temporary stack, for booting.
 *
 * Extends from 'start' down.
 */
bootstack:

/*
 * __start: Kernel start.
 *
 * Arguments:
 *	a0 is the first free page frame number (PFN) (no longer used)
 *	a1 is the page table base register (PTBR)
 *	a2 is the bootinfo magic number
 *	a3 is the pointer to the bootinfo structure
 *
 * All arguments are passed to alpha_init().
 */
NESTED_NOPROFILE(__start,1,0,ra,0,0)
	br	pv,Lstart1
Lstart1: LDGP(pv)

	/* Switch to the boot stack. */
	lda	sp,bootstack

	/* Load KGP with current GP. */
	or	gp,zero,a0
	call_pal PAL_OSF1_wrkgp		/* clobbers a0, t0, t8-t11 */

	/*
	 * Call alpha_init() to do pre-main initialization.
	 * alpha_init() gets the arguments we were called with,
	 * which are already in a0 (destroyed), a1, a2, a3 and a4.
	 */
	CALL(alpha_init)

	/* Set up the virtual page table pointer. */
	ldiq	a0, VPTBASE
	call_pal PAL_OSF1_wrvptptr	/* clobbers a0, t0, t8-t11 */

	/*
	 * Switch to proc0's PCB.
	 */
	lda	a0, proc0
	ldq	a0, P_MD_PCBPADDR(a0)		/* phys addr of PCB */
	SWITCH_CONTEXT

	/*
	 * We've switched to a new page table base, so invalidate the TLB
	 * and I-stream.  This happens automatically everywhere but here.
	 */
	ldiq	a0, -2				/* TBIA */
	call_pal PAL_OSF1_tbi
	call_pal PAL_imb

	/*
	 * All ready to go!  Call main()!
	 */
	CALL(main)

	/* This should never happen. */
	PANIC("main() returned",Lmain_returned_pmsg)
	END(__start)
