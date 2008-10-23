/* a.out coredump register dumper
 *
 * Copyright (C) 2007 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public Licence
 * as published by the Free Software Foundation; either version
 * 2 of the Licence, or (at your option) any later version.
 */

#ifndef ASM_X86__A_OUT_CORE_H
#define ASM_X86__A_OUT_CORE_H

#ifdef __KERNEL__
#ifdef CONFIG_X86_32

#include <linux/user.h>
#include <linux/elfcore.h>

/*
 * fill in the user structure for an a.out core dump
 */
static inline void aout_dump_thread(struct pt_regs *regs, struct user *dump)
{
	u16 gs;

/* changed the size calculations - should hopefully work better. lbt */
	dump->magic = CMAGIC;
	dump->start_code = 0;
	dump->start_stack = regs->sp & ~(PAGE_SIZE - 1);
	dump->u_tsize = ((unsigned long)current->mm->end_code) >> PAGE_SHIFT;
	dump->u_dsize = ((unsigned long)(current->mm->brk + (PAGE_SIZE - 1)))
			>> PAGE_SHIFT;
	dump->u_dsize -= dump->u_tsize;
	dump->u_ssize = 0;
	dump->u_debugreg[0] = current->thread.debugreg0;
	dump->u_debugreg[1] = current->thread.debugreg1;
	dump->u_debugreg[2] = current->thread.debugreg2;
	dump->u_debugreg[3] = current->thread.debugreg3;
	dump->u_debugreg[4] = 0;
	dump->u_debugreg[5] = 0;
	dump->u_debugreg[6] = current->thread.debugreg6;
	dump->u_debugreg[7] = current->thread.debugreg7;

	if (dump->start_stack < TASK_SIZE)
		dump->u_ssize = ((unsigned long)(TASK_SIZE - dump->start_stack))
				>> PAGE_SHIFT;

	dump->regs.bx = regs->bx;
	dump->regs.cx = regs->cx;
	dump->regs.dx = regs->dx;
	dump->regs.si = regs->si;
	dump->regs.di = regs->di;
	dump->regs.bp = regs->bp;
	dump->regs.ax = regs->ax;
	dump->regs.ds = (u16)regs->ds;
	dump->regs.es = (u16)regs->es;
	dump->regs.fs = (u16)regs->fs;
	savesegment(gs, gs);
	dump->regs.orig_ax = regs->orig_ax;
	dump->regs.ip = regs->ip;
	dump->regs.cs = (u16)regs->cs;
	dump->regs.flags = regs->flags;
	dump->regs.sp = regs->sp;
	dump->regs.ss = (u16)regs->ss;

	dump->u_fpvalid = dump_fpu(regs, &dump->i387);
}

#endif /* CONFIG_X86_32 */
#endif /* __KERNEL__ */
#endif /* ASM_X86__A_OUT_CORE_H */
