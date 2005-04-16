/*
 *  linux/arch/ppc64/kernel/ptrace-common.h
 *
 *    Copyright (c) 2002 Stephen Rothwell, IBM Coproration
 *    Extracted from ptrace.c and ptrace32.c
 *
 * This file is subject to the terms and conditions of the GNU General
 * Public License.  See the file README.legal in the main directory of
 * this archive for more details.
 */

#ifndef _PPC64_PTRACE_COMMON_H
#define _PPC64_PTRACE_COMMON_H
/*
 * Set of msr bits that gdb can change on behalf of a process.
 */
#define MSR_DEBUGCHANGE	(MSR_FE0 | MSR_SE | MSR_BE | MSR_FE1)

/*
 * Get contents of register REGNO in task TASK.
 */
static inline unsigned long get_reg(struct task_struct *task, int regno)
{
	unsigned long tmp = 0;

	/*
	 * Put the correct FP bits in, they might be wrong as a result
	 * of our lazy FP restore.
	 */
	if (regno == PT_MSR) {
		tmp = ((unsigned long *)task->thread.regs)[PT_MSR];
		tmp |= task->thread.fpexc_mode;
	} else if (regno < (sizeof(struct pt_regs) / sizeof(unsigned long))) {
		tmp = ((unsigned long *)task->thread.regs)[regno];
	}

	return tmp;
}

/*
 * Write contents of register REGNO in task TASK.
 */
static inline int put_reg(struct task_struct *task, int regno,
			  unsigned long data)
{
	if (regno < PT_SOFTE) {
		if (regno == PT_MSR)
			data = (data & MSR_DEBUGCHANGE)
				| (task->thread.regs->msr & ~MSR_DEBUGCHANGE);
		((unsigned long *)task->thread.regs)[regno] = data;
		return 0;
	}
	return -EIO;
}

static inline void set_single_step(struct task_struct *task)
{
	struct pt_regs *regs = task->thread.regs;
	if (regs != NULL)
		regs->msr |= MSR_SE;
	set_ti_thread_flag(task->thread_info, TIF_SINGLESTEP);
}

static inline void clear_single_step(struct task_struct *task)
{
	struct pt_regs *regs = task->thread.regs;
	if (regs != NULL)
		regs->msr &= ~MSR_SE;
	clear_ti_thread_flag(task->thread_info, TIF_SINGLESTEP);
}

#endif /* _PPC64_PTRACE_COMMON_H */
