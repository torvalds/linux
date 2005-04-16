/*
 * include/asm-x86_64/i387.h
 *
 * Copyright (C) 1994 Linus Torvalds
 *
 * Pentium III FXSR, SSE support
 * General FPU state handling cleanups
 *	Gareth Hughes <gareth@valinux.com>, May 2000
 * x86-64 work by Andi Kleen 2002
 */

#ifndef __ASM_X86_64_I387_H
#define __ASM_X86_64_I387_H

#include <linux/sched.h>
#include <asm/processor.h>
#include <asm/sigcontext.h>
#include <asm/user.h>
#include <asm/thread_info.h>
#include <asm/uaccess.h>

extern void fpu_init(void);
extern unsigned int mxcsr_feature_mask;
extern void mxcsr_feature_mask_init(void);
extern void init_fpu(struct task_struct *child);
extern int save_i387(struct _fpstate __user *buf);

/*
 * FPU lazy state save handling...
 */

#define unlazy_fpu(tsk) do { \
	if ((tsk)->thread_info->status & TS_USEDFPU) \
		save_init_fpu(tsk); \
} while (0)

/* Ignore delayed exceptions from user space */
static inline void tolerant_fwait(void)
{
	asm volatile("1: fwait\n"
		     "2:\n"
		     "   .section __ex_table,\"a\"\n"
		     "	.align 8\n"
		     "	.quad 1b,2b\n"
		     "	.previous\n");
}

#define clear_fpu(tsk) do { \
	if ((tsk)->thread_info->status & TS_USEDFPU) {		\
		tolerant_fwait();				\
		(tsk)->thread_info->status &= ~TS_USEDFPU;	\
		stts();						\
	}							\
} while (0)

/*
 * ptrace request handers...
 */
extern int get_fpregs(struct user_i387_struct __user *buf,
		      struct task_struct *tsk);
extern int set_fpregs(struct task_struct *tsk,
		      struct user_i387_struct __user *buf);

/*
 * i387 state interaction
 */
#define get_fpu_mxcsr(t) ((t)->thread.i387.fxsave.mxcsr)
#define get_fpu_cwd(t) ((t)->thread.i387.fxsave.cwd)
#define get_fpu_fxsr_twd(t) ((t)->thread.i387.fxsave.twd)
#define get_fpu_swd(t) ((t)->thread.i387.fxsave.swd)
#define set_fpu_cwd(t,val) ((t)->thread.i387.fxsave.cwd = (val))
#define set_fpu_swd(t,val) ((t)->thread.i387.fxsave.swd = (val))
#define set_fpu_fxsr_twd(t,val) ((t)->thread.i387.fxsave.twd = (val))

static inline int restore_fpu_checking(struct i387_fxsave_struct *fx) 
{ 
	int err;
	asm volatile("1:  rex64 ; fxrstor (%[fx])\n\t"
		     "2:\n"
		     ".section .fixup,\"ax\"\n"
		     "3:  movl $-1,%[err]\n"
		     "    jmp  2b\n"
		     ".previous\n"
		     ".section __ex_table,\"a\"\n"
		     "   .align 8\n"
		     "   .quad  1b,3b\n"
		     ".previous"
		     : [err] "=r" (err)
		     : [fx] "r" (fx), "0" (0)); 
	if (unlikely(err))
		init_fpu(current);
	return err;
} 

static inline int save_i387_checking(struct i387_fxsave_struct __user *fx) 
{ 
	int err;
	asm volatile("1:  rex64 ; fxsave (%[fx])\n\t"
		     "2:\n"
		     ".section .fixup,\"ax\"\n"
		     "3:  movl $-1,%[err]\n"
		     "    jmp  2b\n"
		     ".previous\n"
		     ".section __ex_table,\"a\"\n"
		     "   .align 8\n"
		     "   .quad  1b,3b\n"
		     ".previous"
		     : [err] "=r" (err)
		     : [fx] "r" (fx), "0" (0)); 
	if (unlikely(err))
		__clear_user(fx, sizeof(struct i387_fxsave_struct));
	return err;
} 

static inline void kernel_fpu_begin(void)
{
	struct thread_info *me = current_thread_info();
	preempt_disable();
	if (me->status & TS_USEDFPU) { 
		asm volatile("rex64 ; fxsave %0 ; fnclex"
			      : "=m" (me->task->thread.i387.fxsave));
		me->status &= ~TS_USEDFPU;
		return;
	}
	clts();
}

static inline void kernel_fpu_end(void)
{
	stts();
	preempt_enable();
}

static inline void save_init_fpu( struct task_struct *tsk )
{
	asm volatile( "rex64 ; fxsave %0 ; fnclex"
		      : "=m" (tsk->thread.i387.fxsave));
	tsk->thread_info->status &= ~TS_USEDFPU;
	stts();
}

/* 
 * This restores directly out of user space. Exceptions are handled.
 */
static inline int restore_i387(struct _fpstate __user *buf)
{
	return restore_fpu_checking((__force struct i387_fxsave_struct *)buf);
}

#endif /* __ASM_X86_64_I387_H */
