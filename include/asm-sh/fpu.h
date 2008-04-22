#ifndef __ASM_SH_FPU_H
#define __ASM_SH_FPU_H

#ifndef __ASSEMBLY__
#include <linux/preempt.h>
#include <asm/ptrace.h>

#ifdef CONFIG_SH_FPU
static inline void release_fpu(struct pt_regs *regs)
{
	regs->sr |= SR_FD;
}

static inline void grab_fpu(struct pt_regs *regs)
{
	regs->sr &= ~SR_FD;
}

struct task_struct;

extern void save_fpu(struct task_struct *__tsk, struct pt_regs *regs);
#else

#define release_fpu(regs)	do { } while (0)
#define grab_fpu(regs)		do { } while (0)

static inline void save_fpu(struct task_struct *tsk, struct pt_regs *regs)
{
	clear_tsk_thread_flag(tsk, TIF_USEDFPU);
}
#endif

extern int do_fpu_inst(unsigned short, struct pt_regs *);

static inline void unlazy_fpu(struct task_struct *tsk, struct pt_regs *regs)
{
	preempt_disable();
	if (test_tsk_thread_flag(tsk, TIF_USEDFPU))
		save_fpu(tsk, regs);
	preempt_enable();
}

static inline void clear_fpu(struct task_struct *tsk, struct pt_regs *regs)
{
	preempt_disable();
	if (test_tsk_thread_flag(tsk, TIF_USEDFPU)) {
		clear_tsk_thread_flag(tsk, TIF_USEDFPU);
		release_fpu(regs);
	}
	preempt_enable();
}

#endif /* __ASSEMBLY__ */

#endif /* __ASM_SH_FPU_H */
