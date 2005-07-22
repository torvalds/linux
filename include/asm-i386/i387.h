/*
 * include/asm-i386/i387.h
 *
 * Copyright (C) 1994 Linus Torvalds
 *
 * Pentium III FXSR, SSE support
 * General FPU state handling cleanups
 *	Gareth Hughes <gareth@valinux.com>, May 2000
 */

#ifndef __ASM_I386_I387_H
#define __ASM_I386_I387_H

#include <linux/sched.h>
#include <linux/init.h>
#include <asm/processor.h>
#include <asm/sigcontext.h>
#include <asm/user.h>

extern void mxcsr_feature_mask_init(void);
extern void init_fpu(struct task_struct *);

/*
 * FPU lazy state save handling...
 */

/*
 * The "nop" is needed to make the instructions the same
 * length.
 */
#define restore_fpu(tsk)			\
	alternative_input(			\
		"nop ; frstor %1",		\
		"fxrstor %1",			\
		X86_FEATURE_FXSR,		\
		"m" ((tsk)->thread.i387.fxsave))

extern void kernel_fpu_begin(void);
#define kernel_fpu_end() do { stts(); preempt_enable(); } while(0)

/*
 * These must be called with preempt disabled
 */
static inline void __save_init_fpu( struct task_struct *tsk )
{
	alternative_input(
		"fnsave %1 ; fwait ;" GENERIC_NOP2,
		"fxsave %1 ; fnclex",
		X86_FEATURE_FXSR,
		"m" (tsk->thread.i387.fxsave)
		:"memory");
	tsk->thread_info->status &= ~TS_USEDFPU;
}

#define __unlazy_fpu( tsk ) do { \
	if ((tsk)->thread_info->status & TS_USEDFPU) \
		save_init_fpu( tsk ); \
} while (0)

#define __clear_fpu( tsk )					\
do {								\
	if ((tsk)->thread_info->status & TS_USEDFPU) {		\
		asm volatile("fnclex ; fwait");				\
		(tsk)->thread_info->status &= ~TS_USEDFPU;	\
		stts();						\
	}							\
} while (0)


/*
 * These disable preemption on their own and are safe
 */
static inline void save_init_fpu( struct task_struct *tsk )
{
	preempt_disable();
	__save_init_fpu(tsk);
	stts();
	preempt_enable();
}

#define unlazy_fpu( tsk ) do {	\
	preempt_disable();	\
	__unlazy_fpu(tsk);	\
	preempt_enable();	\
} while (0)

#define clear_fpu( tsk ) do {	\
	preempt_disable();	\
	__clear_fpu( tsk );	\
	preempt_enable();	\
} while (0)
					\
/*
 * FPU state interaction...
 */
extern unsigned short get_fpu_cwd( struct task_struct *tsk );
extern unsigned short get_fpu_swd( struct task_struct *tsk );
extern unsigned short get_fpu_mxcsr( struct task_struct *tsk );

/*
 * Signal frame handlers...
 */
extern int save_i387( struct _fpstate __user *buf );
extern int restore_i387( struct _fpstate __user *buf );

/*
 * ptrace request handers...
 */
extern int get_fpregs( struct user_i387_struct __user *buf,
		       struct task_struct *tsk );
extern int set_fpregs( struct task_struct *tsk,
		       struct user_i387_struct __user *buf );

extern int get_fpxregs( struct user_fxsr_struct __user *buf,
			struct task_struct *tsk );
extern int set_fpxregs( struct task_struct *tsk,
			struct user_fxsr_struct __user *buf );

/*
 * FPU state for core dumps...
 */
extern int dump_fpu( struct pt_regs *regs,
		     struct user_i387_struct *fpu );

#endif /* __ASM_I386_I387_H */
