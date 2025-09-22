/*	$OpenBSD: db_trace.c,v 1.16 2021/03/25 04:12:00 jsg Exp $	*/
/*	$NetBSD: db_trace.c,v 1.8 2003/01/17 22:28:48 thorpej Exp $	*/

/*
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
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/proc.h>
#include <sys/user.h>
#include <machine/db_machdep.h>

#include <ddb/db_interface.h>
#include <ddb/db_sym.h>
#include <ddb/db_output.h>

db_regs_t ddb_regs;

#define INKERNEL(va)	(((vaddr_t)(va)) >= VM_MIN_KERNEL_ADDRESS)

/*
 *          return link value       [fp, #+4]
 *          return fp value         [fp]        <- fp points to here
 */
#define FR_RFP	(0)
#define FR_RLV	(+1)

void
db_stack_trace_print(db_expr_t addr, int have_addr, db_expr_t count,
    char *modif, int (*pr)(const char *, ...))
{
	u_int32_t	*frame, *lastframe;
	char		c, *cp = modif;
	int		kernel_only = 1;
	int		trace_thread = 0;
	vaddr_t		scp;

	while ((c = *cp++) != 0) {
		if (c == 'u')
			kernel_only = 0;
		if (c == 't')
			trace_thread = 1;
	}

	if (!have_addr) {
		frame = (u_int32_t *)(ddb_regs.tf_r11);
		scp = ddb_regs.tf_pc;
	} else {
		if (trace_thread) {
			struct proc *p;
			struct user *u;
			(*pr) ("trace: pid %d ", (int)addr);
			p = tfind(addr);
			if (p == NULL) {
				(*pr)("not found\n");
				return;
			}
			u = p->p_addr;
			frame = (u_int32_t *)(u->u_pcb.pcb_un.un_32.pcb32_r11);
			(*pr)("at %p\n", frame);
			scp = u->u_pcb.pcb_un.un_32.pcb32_pc;
		} else
			frame = (u_int32_t *)(addr);
	}
	lastframe = NULL;

	while (count-- && frame != NULL) {
		db_printsym(scp, DB_STGY_PROC, pr);
		(*pr)("\n\trlv=0x%08x rfp=0x%08x\n", frame[FR_RLV], frame[FR_RFP]);
		scp = frame[FR_RLV];

		/*
		 * Switch to next frame up
		 */
		if (frame[FR_RFP] == 0)
			break; /* Top of stack */

		lastframe = frame;
		frame = (u_int32_t *)(frame[FR_RFP]);

		if (INKERNEL((int)frame)) {
			/* staying in kernel */
			if (frame <= lastframe) {
				(*pr)("Bad frame pointer: %p\n", frame);
				break;
			}
		} else if (INKERNEL((int)lastframe)) {
			/* switch from user to kernel */
			if (kernel_only)
				break;	/* kernel stack only */
		} else {
			/* in user */
			if (frame <= lastframe) {
				(*pr)("Bad user frame pointer: %p\n",
					  frame);
				break;
			}
		}
	}
}
