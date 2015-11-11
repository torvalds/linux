#include <linux/ptrace.h>
#include <linux/sched.h>
#include <linux/export.h>
#include <asm/syscall.h>

static int collect_syscall(struct task_struct *target, long *callno,
			   unsigned long args[6], unsigned int maxargs,
			   unsigned long *sp, unsigned long *pc)
{
	struct pt_regs *regs = task_pt_regs(target);
	if (unlikely(!regs))
		return -EAGAIN;

	*sp = user_stack_pointer(regs);
	*pc = instruction_pointer(regs);

	*callno = syscall_get_nr(target, regs);
	if (*callno != -1L && maxargs > 0)
		syscall_get_arguments(target, regs, 0, maxargs, args);

	return 0;
}

/**
 * task_current_syscall - Discover what a blocked task is doing.
 * @target:		thread to examine
 * @callno:		filled with system call number or -1
 * @args:		filled with @maxargs system call arguments
 * @maxargs:		number of elements in @args to fill
 * @sp:			filled with user stack pointer
 * @pc:			filled with user PC
 *
 * If @target is blocked in a system call, returns zero with *@callno
 * set to the the call's number and @args filled in with its arguments.
 * Registers not used for system call arguments may not be available and
 * it is not kosher to use &struct user_regset calls while the system
 * call is still in progress.  Note we may get this result if @target
 * has finished its system call but not yet returned to user mode, such
 * as when it's stopped for signal handling or syscall exit tracing.
 *
 * If @target is blocked in the kernel during a fault or exception,
 * returns zero with *@callno set to -1 and does not fill in @args.
 * If so, it's now safe to examine @target using &struct user_regset
 * get() calls as long as we're sure @target won't return to user mode.
 *
 * Returns -%EAGAIN if @target does not remain blocked.
 *
 * Returns -%EINVAL if @maxargs is too large (maximum is six).
 */
int task_current_syscall(struct task_struct *target, long *callno,
			 unsigned long args[6], unsigned int maxargs,
			 unsigned long *sp, unsigned long *pc)
{
	long state;
	unsigned long ncsw;

	if (unlikely(maxargs > 6))
		return -EINVAL;

	if (target == current)
		return collect_syscall(target, callno, args, maxargs, sp, pc);

	state = target->state;
	if (unlikely(!state))
		return -EAGAIN;

	ncsw = wait_task_inactive(target, state);
	if (unlikely(!ncsw) ||
	    unlikely(collect_syscall(target, callno, args, maxargs, sp, pc)) ||
	    unlikely(wait_task_inactive(target, state) != ncsw))
		return -EAGAIN;

	return 0;
}
