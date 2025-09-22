/*	$OpenBSD: frame.h,v 1.4 2011/03/23 16:54:34 pirofti Exp $	*/
/*	$NetBSD: frame.h,v 1.3 1996/07/11 05:31:32 cgd Exp $	*/

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

#ifndef _MACHINE_FRAME_H_
#define	_MACHINE_FRAME_H_

#include <machine/alpha_cpu.h>

/*
 * Software trap, exception, and syscall frame.
 *
 * Includes "hardware" (PALcode) frame.
 *
 * PALcode puts ALPHA_HWFRAME_* fields on stack.  We have to add
 * all of the general-purpose registers except for zero, for sp
 * (which is automatically saved in the PCB's USP field for entries
 * from user mode, and which is implicitly saved and restored by the
 * calling conventions for entries from kernel mode), and (on traps
 * and exceptions) for a0, a1, and a2 (which are saved by PALcode).
 */

/* Quadword offsets of the registers to be saved. */
#define	FRAME_V0	0
#define	FRAME_T0	1
#define	FRAME_T1	2
#define	FRAME_T2	3
#define	FRAME_T3	4
#define	FRAME_T4	5
#define	FRAME_T5	6
#define	FRAME_T6	7
#define	FRAME_T7	8
#define	FRAME_S0	9
#define	FRAME_S1	10
#define	FRAME_S2	11
#define	FRAME_S3	12
#define	FRAME_S4	13
#define	FRAME_S5	14
#define	FRAME_S6	15
#define	FRAME_A3	16
#define	FRAME_A4	17
#define	FRAME_A5	18
#define	FRAME_T8	19
#define	FRAME_T9	20
#define	FRAME_T10	21
#define	FRAME_T11	22
#define	FRAME_RA	23
#define	FRAME_T12	24
#define	FRAME_AT	25
#define	FRAME_SP	26

#define	FRAME_SW_SIZE	(FRAME_SP + 1)
#define	FRAME_HW_OFFSET	FRAME_SW_SIZE

#define	FRAME_PS	(FRAME_HW_OFFSET + ALPHA_HWFRAME_PS)
#define	FRAME_PC	(FRAME_HW_OFFSET + ALPHA_HWFRAME_PC)
#define	FRAME_GP	(FRAME_HW_OFFSET + ALPHA_HWFRAME_GP)
#define	FRAME_A0	(FRAME_HW_OFFSET + ALPHA_HWFRAME_A0)
#define	FRAME_A1	(FRAME_HW_OFFSET + ALPHA_HWFRAME_A1)
#define	FRAME_A2	(FRAME_HW_OFFSET + ALPHA_HWFRAME_A2)

#define	FRAME_HW_SIZE	ALPHA_HWFRAME_SIZE
#define	FRAME_SIZE	(FRAME_HW_OFFSET + FRAME_HW_SIZE)

struct trapframe {
	unsigned long	tf_regs[FRAME_SIZE];	/* See above */
};

#endif /* _MACHINE_FRAME_H_ */
