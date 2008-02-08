/* MN10300 FPU definitions
 *
 * Copyright (C) 2007 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 * Derived from include/asm-i386/i387.h: Copyright (C) 1994 Linus Torvalds
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public Licence
 * as published by the Free Software Foundation; either version
 * 2 of the Licence, or (at your option) any later version.
 */
#ifndef _ASM_FPU_H
#define _ASM_FPU_H

#include <asm/processor.h>
#include <asm/sigcontext.h>
#include <asm/user.h>

#ifdef __KERNEL__

/* the task that owns the FPU state */
extern struct task_struct *fpu_state_owner;

#define set_using_fpu(tsk)				\
do {							\
	(tsk)->thread.fpu_flags |= THREAD_USING_FPU;	\
} while (0)

#define clear_using_fpu(tsk)				\
do {							\
	(tsk)->thread.fpu_flags &= ~THREAD_USING_FPU;	\
} while (0)

#define is_using_fpu(tsk) ((tsk)->thread.fpu_flags & THREAD_USING_FPU)

#define unlazy_fpu(tsk)					\
do {							\
	preempt_disable();				\
	if (fpu_state_owner == (tsk))			\
		fpu_save(&tsk->thread.fpu_state);	\
	preempt_enable();				\
} while (0)

#define exit_fpu()				\
do {						\
	struct task_struct *__tsk = current;	\
	preempt_disable();			\
	if (fpu_state_owner == __tsk)		\
		fpu_state_owner = NULL;		\
	preempt_enable();			\
} while (0)

#define flush_fpu()					\
do {							\
	struct task_struct *__tsk = current;		\
	preempt_disable();				\
	if (fpu_state_owner == __tsk) {			\
		fpu_state_owner = NULL;			\
		__tsk->thread.uregs->epsw &= ~EPSW_FE;	\
	}						\
	preempt_enable();				\
	clear_using_fpu(__tsk);				\
} while (0)

extern asmlinkage void fpu_init_state(void);
extern asmlinkage void fpu_kill_state(struct task_struct *);
extern asmlinkage void fpu_disabled(struct pt_regs *, enum exception_code);
extern asmlinkage void fpu_exception(struct pt_regs *, enum exception_code);

#ifdef CONFIG_FPU
extern asmlinkage void fpu_save(struct fpu_state_struct *);
extern asmlinkage void fpu_restore(struct fpu_state_struct *);
#else
#define fpu_save(a)
#define fpu_restore(a)
#endif /* CONFIG_FPU  */

/*
 * signal frame handlers
 */
extern int fpu_setup_sigcontext(struct fpucontext *buf);
extern int fpu_restore_sigcontext(struct fpucontext *buf);

#endif /* __KERNEL__ */
#endif /* _ASM_FPU_H */
