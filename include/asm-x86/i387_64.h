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
extern asmlinkage void math_state_restore(void);

/*
 * FPU lazy state save handling...
 */

#define unlazy_fpu(tsk) do { \
	if (task_thread_info(tsk)->status & TS_USEDFPU) \
		save_init_fpu(tsk); 			\
	else						\
		tsk->fpu_counter = 0;			\
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
	if (task_thread_info(tsk)->status & TS_USEDFPU) {	\
		tolerant_fwait();				\
		task_thread_info(tsk)->status &= ~TS_USEDFPU;	\
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

#define X87_FSW_ES (1 << 7)	/* Exception Summary */

/* AMD CPUs don't save/restore FDP/FIP/FOP unless an exception
   is pending. Clear the x87 state here by setting it to fixed
   values. The kernel data segment can be sometimes 0 and sometimes
   new user value. Both should be ok.
   Use the PDA as safe address because it should be already in L1. */
static inline void clear_fpu_state(struct i387_fxsave_struct *fx)
{
	if (unlikely(fx->swd & X87_FSW_ES))
		 asm volatile("fnclex");
	alternative_input(ASM_NOP8 ASM_NOP2,
	     	     "    emms\n"		/* clear stack tags */
	     	     "    fildl %%gs:0",	/* load to clear state */
		     X86_FEATURE_FXSAVE_LEAK);
}

static inline int restore_fpu_checking(struct i387_fxsave_struct *fx) 
{ 
	int err;

	asm volatile("1:  rex64/fxrstor (%[fx])\n\t"
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
#if 0 /* See comment in __fxsave_clear() below. */
		     : [fx] "r" (fx), "m" (*fx), "0" (0));
#else
		     : [fx] "cdaSDb" (fx), "m" (*fx), "0" (0));
#endif
	if (unlikely(err))
		init_fpu(current);
	return err;
} 

static inline int save_i387_checking(struct i387_fxsave_struct __user *fx) 
{ 
	int err;

	asm volatile("1:  rex64/fxsave (%[fx])\n\t"
		     "2:\n"
		     ".section .fixup,\"ax\"\n"
		     "3:  movl $-1,%[err]\n"
		     "    jmp  2b\n"
		     ".previous\n"
		     ".section __ex_table,\"a\"\n"
		     "   .align 8\n"
		     "   .quad  1b,3b\n"
		     ".previous"
		     : [err] "=r" (err), "=m" (*fx)
#if 0 /* See comment in __fxsave_clear() below. */
		     : [fx] "r" (fx), "0" (0));
#else
		     : [fx] "cdaSDb" (fx), "0" (0));
#endif
	if (unlikely(err) && __clear_user(fx, sizeof(struct i387_fxsave_struct)))
		err = -EFAULT;
	/* No need to clear here because the caller clears USED_MATH */
	return err;
} 

static inline void __fxsave_clear(struct task_struct *tsk)
{
	/* Using "rex64; fxsave %0" is broken because, if the memory operand
	   uses any extended registers for addressing, a second REX prefix
	   will be generated (to the assembler, rex64 followed by semicolon
	   is a separate instruction), and hence the 64-bitness is lost. */
#if 0
	/* Using "fxsaveq %0" would be the ideal choice, but is only supported
	   starting with gas 2.16. */
	__asm__ __volatile__("fxsaveq %0"
			     : "=m" (tsk->thread.i387.fxsave));
#elif 0
	/* Using, as a workaround, the properly prefixed form below isn't
	   accepted by any binutils version so far released, complaining that
	   the same type of prefix is used twice if an extended register is
	   needed for addressing (fix submitted to mainline 2005-11-21). */
	__asm__ __volatile__("rex64/fxsave %0"
			     : "=m" (tsk->thread.i387.fxsave));
#else
	/* This, however, we can work around by forcing the compiler to select
	   an addressing mode that doesn't require extended registers. */
	__asm__ __volatile__("rex64/fxsave %P2(%1)"
			     : "=m" (tsk->thread.i387.fxsave)
			     : "cdaSDb" (tsk),
				"i" (offsetof(__typeof__(*tsk),
					      thread.i387.fxsave)));
#endif
	clear_fpu_state(&tsk->thread.i387.fxsave);
}

static inline void kernel_fpu_begin(void)
{
	struct thread_info *me = current_thread_info();
	preempt_disable();
	if (me->status & TS_USEDFPU) {
		__fxsave_clear(me->task);
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

static inline void save_init_fpu(struct task_struct *tsk)
{
 	__fxsave_clear(tsk);
	task_thread_info(tsk)->status &= ~TS_USEDFPU;
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
