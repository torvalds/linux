/*	$OpenBSD: pcb.h,v 1.9 2024/10/22 21:50:02 jsg Exp $ */
/*
 * Copyright (c) 1996 Nivas Madhur
 * Mach Operating System
 * Copyright (c) 1993-1992 Carnegie Mellon University
 * All Rights Reserved.
 *
 * Permission to use, copy, modify and distribute this software and its
 * documentation is hereby granted, provided that both the copyright
 * notice and this permission notice appear in all copies of the
 * software, derivative works or modified versions, and any portions
 * thereof, and that both notices appear in supporting documentation.
 *
 * CARNEGIE MELLON AND OMRON ALLOW FREE USE OF THIS SOFTWARE IN ITS "AS IS"
 * CONDITION.  CARNEGIE MELLON AND OMRON DISCLAIM ANY LIABILITY OF ANY KIND
 * FOR ANY DAMAGES WHATSOEVER RESULTING FROM THE USE OF THIS SOFTWARE.
 *
 * Carnegie Mellon requests users of this software to return to
 *
 *  Software Distribution Coordinator  or  Software.Distribution@CS.CMU.EDU
 *  School of Computer Science
 *  Carnegie Mellon University
 *  Pittsburgh PA 15213-3890
 *
 * any improvements or extensions that they make and grant Carnegie Mellon
 * the rights to redistribute these changes.
 */
/*
 * Motorola 88100 pcb definitions
 *
 */
/*
 */
#ifndef _M88K_PCB_H_
#define _M88K_PCB_H_

#include <machine/frame.h>

/*
 * Our PCB is the regular PCB+Save area for kernel frame.
 * Upon entering kernel mode from userland, save the user context
 * in the saved_state area - this is passed as the exception frame.
 * On a context switch, only registers that need to be saved by the
 * C calling convention and few other regs (pc, psr etc) are saved
 * in the kernel_state part of the PCB. Typically, trap frames are
 * saved on the stack (by low level handlers or by hardware) but,
 * we just decided to do it in the PCB.
 */

struct m88100_pcb {
	unsigned long pcb_pc;	/* address to return */
	unsigned long :32;
	unsigned long pcb_r14;
	unsigned long pcb_r15;
	unsigned long pcb_r16;
	unsigned long pcb_r17;
	unsigned long pcb_r18;
	unsigned long pcb_r19;
	unsigned long pcb_r20;
	unsigned long pcb_r21;
	unsigned long pcb_r22;
	unsigned long pcb_r23;
	unsigned long pcb_r24;
	unsigned long pcb_r25;
	unsigned long pcb_r26;
	unsigned long pcb_r27;
	unsigned long pcb_r28;
	unsigned long pcb_r29;
	unsigned long pcb_r30;
	unsigned long pcb_sp; 	/* kernel stack pointer */
	/* floating-point state */
	unsigned long pcb_fcr62;
	unsigned long pcb_fcr63;
};

struct pcb {
	struct m88100_pcb	kernel_state;
	struct trapframe	user_state;
	int			pcb_onfault;
};

/*
 *	Location of saved user registers for the proc.
 */
#define	USER_REGS(p) \
	(((struct reg *)(&((p)->p_addr->u_pcb.user_state))))

#endif /* _M88K_PCB_H_ */
