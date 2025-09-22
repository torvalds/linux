/*	$OpenBSD: proc.h,v 1.14 2025/06/29 15:55:21 miod Exp $	*/
/*	$NetBSD: proc.h,v 1.2 1995/03/24 15:01:36 cgd Exp $	*/

/*
 * Copyright (c) 1994, 1995 Carnegie-Mellon University.
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

#include <machine/cpu.h>
/*
 * Machine-dependent part of the proc struct for the Alpha.
 */

struct mdbpt {
	vaddr_t	addr;
	u_int32_t contents;
};

struct mdproc {
	u_int md_flags;
	volatile u_int md_astpending;	/* AST pending for this process */
	struct trapframe *md_tf;	/* trap/syscall registers */
	struct pcb *md_pcbpaddr;	/* phys addr of the pcb */
	struct mdbpt md_sstep[2];	/* two breakpoints for sstep */
};

/*
 * md_flags usage
 * --------------
 * MDP_FPUSED
 *      A largely unused bit indicating the presence of FPU history.
 *      Cleared on exec. Set but not used by the fpu context switcher
 *      itself.
 *
 * MDP_FP_C
 *      The architected FP Control word. It should forever begin at bit 1,
 *      as the bits are AARM specified and this way it doesn't need to be
 *      shifted.
 *
 *      Until C99 there was never an IEEE 754 API, making most of the
 *      standard useless.  Because of overlapping AARM, OSF/1, NetBSD, and
 *      C99 API's, the use of the MDP_FP_C bits is defined variously in
 *      ieeefp.h and fpu.h.
 */
#define	MDP_FPUSED	0x00000001		/* Process used the FPU */
#ifndef NO_IEEE
#define	MDP_FP_C	0x007ffffe	/* Extended FP_C Quadword bits */
#endif
#define MDP_STEP1	0x00800000	/* Single step normal */
#define MDP_STEP2	0x01800000	/* Single step branch */
