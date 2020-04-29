// SPDX-License-Identifier: GPL-2.0
#include <linux/ptrace.h>
#include <linux/sched.h>
#include <linux/sched/task_stack.h>
#include <linux/export.h>
#include <asm/syscall.h>

static int collect_syscall(struct task_struct *target, struct syscall_info *info)
{
	struct pt_regs *regs;

	if (!try_get_task_stack(target)) {
		/* Task has no stack, so the task isn't in a syscall. */
		memset(info, 0, sizeof(*info));
		info->data.nr = -1;
		return 0;
	}

	regs = task_pt_regs(target);
	if (unlikely(!regs)) {
		put_task_stack(target);
		return -EAGAIN;
	}

	info->sp = user_stack_pointer(regs);
	info->data.instruction_pointer = instruction_pointer(regs);

	info->data.nr = syscall_get_nr(target, regs);
	if (info->data.nr != -1L)
		syscall_get_arguments(target, regs,
				      (unsigned long *)&info->data.args[0]);

	put_task_stack(target);
	return 0;
}

/**
 * task_current_syscall - Discover what a blocked task is doing.
 * @target:		thread to examine
 * @info:		structure with the following fields:
 *			 .sp        - filled with user stack pointer
 *			 .data.nr   - filled with system call number or -1
 *			 .data.args - filled with @maxargs system call arguments
 *			 .data.instruction_pointer - filled with user PC
 *
 * If @target is blocked in a system call, returns zero with @info.data.nr
 * set to the the call's number and @info.data.args filled in with its
 * arguments. Registers not used for system call arguments may not be available
 * and it is not kosher to use &struct user_regset calls while the system
 * call is still in progress.  Note we may get this result if @target
 * has finished its system call but not yet returned to user mode, such
 * as when it's stopped for signal handling or syscall exit tracing.
 *
 * If @target is blocked in the kernel during a fault or exception,
 * returns zero with *@info.data.nr set to -1 and does not fill in
 * @info.data.args. If so, it's now safe to examine @target using
 * &struct user_regset get() calls as long as we're sure @target won't return
 * to user mode.
 *
 * Returns -%EAGAIN if @target does not remain blocked.
 */
int task_current_syscall(struct task_struct *target, struct syscall_info *info)
{
	long state;
	unsigned long ncsw;

	if (target == current)
		return collect_syscall(target, info);

	state = target->state;
	if (unlikely(!state))
		return -EAGAIN;

	ncsw = wait_task_inactive(target, state);
	if (unlikely(!ncsw) ||
	    unlikely(collect_syscall(target, info)) ||
	    unlikely(wait_task_inactive(target, state) != ncsw))
		return -EAGAIN;

	return 0;
}
