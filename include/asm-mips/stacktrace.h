#ifndef _ASM_STACKTRACE_H
#define _ASM_STACKTRACE_H

#include <asm/ptrace.h>

#ifdef CONFIG_KALLSYMS
extern int raw_show_trace;
extern unsigned long unwind_stack(struct task_struct *task, unsigned long *sp,
				  unsigned long pc, unsigned long *ra);
#else
#define raw_show_trace 1
#define unwind_stack(task, sp, pc, ra)	0
#endif

static __always_inline void prepare_frametrace(struct pt_regs *regs)
{
#ifndef CONFIG_KALLSYMS
	/*
	 * Remove any garbage that may be in regs (specially func
	 * addresses) to avoid show_raw_backtrace() to report them
	 */
	memset(regs, 0, sizeof(*regs));
#endif
	__asm__ __volatile__(
		".set push\n\t"
		".set noat\n\t"
#ifdef CONFIG_64BIT
		"1: dla $1, 1b\n\t"
		"sd $1, %0\n\t"
		"sd $29, %1\n\t"
		"sd $31, %2\n\t"
#else
		"1: la $1, 1b\n\t"
		"sw $1, %0\n\t"
		"sw $29, %1\n\t"
		"sw $31, %2\n\t"
#endif
		".set pop\n\t"
		: "=m" (regs->cp0_epc),
		"=m" (regs->regs[29]), "=m" (regs->regs[31])
		: : "memory");
}

#endif /* _ASM_STACKTRACE_H */
