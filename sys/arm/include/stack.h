/*-
 * Copyright (c) 2000, 2001 Ben Harris
 * Copyright (c) 1996 Scott K. Stevens
 *
 * Mach Operating System
 * Copyright (c) 1991,1990 Carnegie Mellon University
 * All Rights Reserved.
 *
 * Permission to use, copy, modify and distribute this software and its
 * documentation is hereby granted, provided that both the copyright
 * notice and this permission notice appear in all copies of the
 * software, derivative works or modified versions, and any portions
 * thereof, and that both notices appear in supporting documentation.
 *
 * CARNEGIE MELLON ALLOWS FREE USE OF THIS SOFTWARE IN ITS "AS IS"
 * CONDITION.  CARNEGIE MELLON DISCLAIMS ANY LIABILITY OF ANY KIND FOR
 * ANY DAMAGES WHATSOEVER RESULTING FROM THE USE OF THIS SOFTWARE.
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
 *
 * $FreeBSD$
 */

#ifndef _MACHINE_STACK_H_
#define	_MACHINE_STACK_H_

#define INKERNEL(va)	(((vm_offset_t)(va)) >= VM_MIN_KERNEL_ADDRESS)

#define FR_SCP	(0)
#define FR_RLV	(-1)
#define FR_RSP	(-2)
#define FR_RFP	(-3)

/* The state of the unwind process */
struct unwind_state {
	uint32_t registers[16];
	uint32_t start_pc;
	uint32_t *insn;
	u_int entries;
	u_int byte;
	uint16_t update_mask;
};

/* The register names */
#define	FP	11
#define	SP	13
#define	LR	14
#define	PC	15

int unwind_stack_one(struct unwind_state *, int);

#endif /* !_MACHINE_STACK_H_ */
