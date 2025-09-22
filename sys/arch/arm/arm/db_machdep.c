/*	$OpenBSD: db_machdep.c,v 1.6 2021/03/25 04:12:00 jsg Exp $	*/
/*	$NetBSD: db_machdep.c,v 1.8 2003/07/15 00:24:41 lukem Exp $	*/

/* 
 * Copyright (c) 1996 Mark Brinicombe
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
#include <sys/proc.h>
#include <sys/vnode.h>
#include <sys/systm.h>

#include <arm/db_machdep.h>

#include <ddb/db_output.h>

void
db_show_frame_cmd(db_expr_t addr, int have_addr, db_expr_t count, char *modif)
{
	struct trapframe *frame;

	if (!have_addr) {
		db_printf("frame address must be specified\n");
		return;
	}

	frame = (struct trapframe *)addr;

	db_printf("frame address = %08x  ", (u_int)frame);
	db_printf("spsr=%08lx\n", frame->tf_spsr);
	db_printf("r0 =%08lx r1 =%08lx r2 =%08lx r3 =%08lx\n",
	    frame->tf_r0, frame->tf_r1, frame->tf_r2, frame->tf_r3);
	db_printf("r4 =%08lx r5 =%08lx r6 =%08lx r7 =%08lx\n",
	    frame->tf_r4, frame->tf_r5, frame->tf_r6, frame->tf_r7);
	db_printf("r8 =%08lx r9 =%08lx r10=%08lx r11=%08lx\n",
	    frame->tf_r8, frame->tf_r9, frame->tf_r10, frame->tf_r11);
	db_printf("r12=%08lx r13=%08lx r14=%08lx r15=%08lx\n",
	    frame->tf_r12, frame->tf_usr_sp, frame->tf_usr_lr, frame->tf_pc);
	db_printf("slr=%08lx\n", frame->tf_svc_lr);
}
