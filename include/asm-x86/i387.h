/*
 * Copyright (C) 1994 Linus Torvalds
 *
 * Pentium III FXSR, SSE support
 * General FPU state handling cleanups
 *	Gareth Hughes <gareth@valinux.com>, May 2000
 * x86-64 work by Andi Kleen 2002
 */

#ifndef _ASM_X86_I387_H
#define _ASM_X86_I387_H

#include <linux/sched.h>
#include <linux/kernel_stat.h>
#include <linux/regset.h>
#include <asm/processor.h>
#include <asm/sigcontext.h>
#include <asm/user.h>
#include <asm/uaccess.h>

extern void fpu_init(void);
extern unsigned int mxcsr_feature_mask;
extern void mxcsr_feature_mask_init(void);
extern void init_fpu(struct task_struct *child);
extern asmlinkage void math_state_restore(void);

extern user_regset_active_fn fpregs_active, xfpregs_active;
extern user_regset_get_fn fpregs_get, xfpregs_get, fpregs_soft_get;
extern user_regset_set_fn fpregs_set, xfpregs_set, fpregs_soft_set;

#ifdef CONFIG_IA32_EMULATION
struct _fpstate_ia32;
extern int save_i387_ia32(struct _fpstate_ia32 __user *buf);
extern int restore_i387_ia32(struct _fpstate_ia32 __user *buf);
#endif

#ifdef CONFIG_X86_64

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
#if 0 /* See comment in __save_init_fpu() below. */
		     : [fx] "r" (fx), "m" (*fx), "0" (0));
#else
		     : [fx] "cdaSDb" (fx), "m" (*fx), "0" (0));
#endif
	if (unlikely(err))
		init_fpu(current);
	return err;
}

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

static inline void __save_init_fpu(struct task_struct *tsk)
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
	task_thread_info(tsk)->status &= ~TS_USEDFPU;
}

/*
 * Signal frame handlers.
 */

static inline int save_i387(struct _fpstate __user *buf)
{
	struct task_struct *tsk = current;
	int err = 0;

	BUILD_BUG_ON(sizeof(struct user_i387_struct) !=
			sizeof(tsk->thread.i387.fxsave));

	if ((unsigned long)buf % 16)
		printk("save_i387: bad fpstate %p\n", buf);

	if (!used_math())
		return 0;
	clear_used_math(); /* trigger finit */
	if (task_thread_info(tsk)->status & TS_USEDFPU) {
		err = save_i387_checking((struct i387_fxsave_struct __user *)buf);
		if (err) return err;
		task_thread_info(tsk)->status &= ~TS_USEDFPU;
		stts();
	} else {
		if (__copy_to_user(buf, &tsk->thread.i387.fxsave,
				   sizeof(struct i387_fxsave_struct)))
			return -1;
	}
	return 1;
}

/*
 * This restores directly out of user space. Exceptions are handled.
 */
static inline int restore_i387(struct _fpstate __user *buf)
{
	set_used_math();
	if (!(task_thread_info(current)->status & TS_USEDFPU)) {
		clts();
		task_thread_info(current)->status |= TS_USEDFPU;
	}
	return restore_fpu_checking((__force struct i387_fxsave_struct *)buf);
}

#else  /* CONFIG_X86_32 */

static inline void tolerant_fwait(void)
{
	asm volatile("fnclex ; fwait");
}

static inline void restore_fpu(struct task_struct *tsk)
{
	/*
	 * The "nop" is needed to make the instructions the same
	 * length.
	 */
	alternative_input(
		"nop ; frstor %1",
		"fxrstor %1",
		X86_FEATURE_FXSR,
		"m" ((tsk)->thread.i387.fxsave));
}

/* We need a safe address that is cheap to find and that is already
   in L1 during context switch. The best choices are unfortunately
   different for UP and SMP */
#ifdef CONFIG_SMP
#define safe_address (__per_cpu_offset[0])
#else
#define safe_address (kstat_cpu(0).cpustat.user)
#endif

/*
 * These must be called with preempt disabled
 */
static inline void __save_init_fpu(struct task_struct *tsk)
{
	/* Use more nops than strictly needed in case the compiler
	   varies code */
	alternative_input(
		"fnsave %[fx] ;fwait;" GENERIC_NOP8 GENERIC_NOP4,
		"fxsave %[fx]\n"
		"bt $7,%[fsw] ; jnc 1f ; fnclex\n1:",
		X86_FEATURE_FXSR,
		[fx] "m" (tsk->thread.i387.fxsave),
		[fsw] "m" (tsk->thread.i387.fxsave.swd) : "memory");
	/* AMD K7/K8 CPUs don't save/restore FDP/FIP/FOP unless an exception
	   is pending.  Clear the x87 state here by setting it to fixed
	   values. safe_address is a random variable that should be in L1 */
	alternative_input(
		GENERIC_NOP8 GENERIC_NOP2,
		"emms\n\t"	  	/* clear stack tags */
		"fildl %[addr]", 	/* set F?P to defined value */
		X86_FEATURE_FXSAVE_LEAK,
		[addr] "m" (safe_address));
	task_thread_info(tsk)->status &= ~TS_USEDFPU;
}

/*
 * Signal frame handlers...
 */
extern int save_i387(struct _fpstate __user *buf);
extern int restore_i387(struct _fpstate __user *buf);

#endif	/* CONFIG_X86_64 */

static inline void __unlazy_fpu(struct task_struct *tsk)
{
	if (task_thread_info(tsk)->status & TS_USEDFPU) {
		__save_init_fpu(tsk);
		stts();
	} else
		tsk->fpu_counter = 0;
}

static inline void __clear_fpu(struct task_struct *tsk)
{
	if (task_thread_info(tsk)->status & TS_USEDFPU) {
		tolerant_fwait();
		task_thread_info(tsk)->status &= ~TS_USEDFPU;
		stts();
	}
}

static inline void kernel_fpu_begin(void)
{
	struct thread_info *me = current_thread_info();
	preempt_disable();
	if (me->status & TS_USEDFPU)
		__save_init_fpu(me->task);
	else
		clts();
}

static inline void kernel_fpu_end(void)
{
	stts();
	preempt_enable();
}

#ifdef CONFIG_X86_64

static inline void save_init_fpu(struct task_struct *tsk)
{
	__save_init_fpu(tsk);
	stts();
}

#define unlazy_fpu	__unlazy_fpu
#define clear_fpu	__clear_fpu

#else  /* CONFIG_X86_32 */

/*
 * These disable preemption on their own and are safe
 */
static inline void save_init_fpu(struct task_struct *tsk)
{
	preempt_disable();
	__save_init_fpu(tsk);
	stts();
	preempt_enable();
}

static inline void unlazy_fpu(struct task_struct *tsk)
{
	preempt_disable();
	__unlazy_fpu(tsk);
	preempt_enable();
}

static inline void clear_fpu(struct task_struct *tsk)
{
	preempt_disable();
	__clear_fpu(tsk);
	preempt_enable();
}

#endif	/* CONFIG_X86_64 */

/*
 * i387 state interaction
 */
static inline unsigned short get_fpu_cwd(struct task_struct *tsk)
{
	if (cpu_has_fxsr) {
		return tsk->thread.i387.fxsave.cwd;
	} else {
		return (unsigned short)tsk->thread.i387.fsave.cwd;
	}
}

static inline unsigned short get_fpu_swd(struct task_struct *tsk)
{
	if (cpu_has_fxsr) {
		return tsk->thread.i387.fxsave.swd;
	} else {
		return (unsigned short)tsk->thread.i387.fsave.swd;
	}
}

static inline unsigned short get_fpu_mxcsr(struct task_struct *tsk)
{
	if (cpu_has_xmm) {
		return tsk->thread.i387.fxsave.mxcsr;
	} else {
		return MXCSR_DEFAULT;
	}
}

#endif	/* _ASM_X86_I387_H */
