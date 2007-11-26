#ifndef __ASM_SH_FPU_H
#define __ASM_SH_FPU_H

#define SR_FD    0x00008000

#ifndef __ASSEMBLY__
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
#define save_fpu(tsk, regs)	do { } while (0)
#endif

extern int do_fpu_inst(unsigned short, struct pt_regs *);

#define unlazy_fpu(tsk, regs) do {			\
	if (test_tsk_thread_flag(tsk, TIF_USEDFPU)) {	\
		save_fpu(tsk, regs);			\
	}						\
} while (0)

#define clear_fpu(tsk, regs) do {				\
	if (test_tsk_thread_flag(tsk, TIF_USEDFPU)) {		\
		clear_tsk_thread_flag(tsk, TIF_USEDFPU);	\
		release_fpu(regs);				\
	}							\
} while (0)

#endif /* __ASSEMBLY__ */

#endif /* __ASM_SH_FPU_H */
