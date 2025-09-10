// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2020 Collabora Ltd.
 */
#include <linux/sched.h>
#include <linux/prctl.h>
#include <linux/ptrace.h>
#include <linux/syscall_user_dispatch.h>
#include <linux/uaccess.h>
#include <linux/signal.h>
#include <linux/elf.h>

#include <linux/sched/signal.h>
#include <linux/sched/task_stack.h>

#include <asm/syscall.h>

#include "common.h"

static void trigger_sigsys(struct pt_regs *regs)
{
	struct kernel_siginfo info;

	clear_siginfo(&info);
	info.si_signo = SIGSYS;
	info.si_code = SYS_USER_DISPATCH;
	info.si_call_addr = (void __user *)KSTK_EIP(current);
	info.si_errno = 0;
	info.si_arch = syscall_get_arch(current);
	info.si_syscall = syscall_get_nr(current, regs);

	force_sig_info(&info);
}

bool syscall_user_dispatch(struct pt_regs *regs)
{
	struct syscall_user_dispatch *sd = &current->syscall_dispatch;
	char state;

	if (likely(instruction_pointer(regs) - sd->offset < sd->len))
		return false;

	if (unlikely(arch_syscall_is_vdso_sigreturn(regs)))
		return false;

	if (likely(sd->selector)) {
		/*
		 * access_ok() is performed once, at prctl time, when
		 * the selector is loaded by userspace.
		 */
		if (unlikely(__get_user(state, sd->selector))) {
			force_exit_sig(SIGSEGV);
			return true;
		}

		if (likely(state == SYSCALL_DISPATCH_FILTER_ALLOW))
			return false;

		if (state != SYSCALL_DISPATCH_FILTER_BLOCK) {
			force_exit_sig(SIGSYS);
			return true;
		}
	}

	sd->on_dispatch = true;
	syscall_rollback(current, regs);
	trigger_sigsys(regs);

	return true;
}

static int task_set_syscall_user_dispatch(struct task_struct *task, unsigned long mode,
					  unsigned long offset, unsigned long len,
					  char __user *selector)
{
	switch (mode) {
	case PR_SYS_DISPATCH_OFF:
		if (offset || len || selector)
			return -EINVAL;
		break;
	case PR_SYS_DISPATCH_EXCLUSIVE_ON:
		/*
		 * Validate the direct dispatcher region just for basic
		 * sanity against overflow and a 0-sized dispatcher
		 * region.  If the user is able to submit a syscall from
		 * an address, that address is obviously valid.
		 */
		if (offset && offset + len <= offset)
			return -EINVAL;
		break;
	case PR_SYS_DISPATCH_INCLUSIVE_ON:
		if (len == 0 || offset + len <= offset)
			return -EINVAL;
		/*
		 * Invert the range, the check in syscall_user_dispatch()
		 * supports wrap-around.
		 */
		offset = offset + len;
		len = -len;
		break;
	default:
		return -EINVAL;
	}

	/*
	 * access_ok() will clear memory tags for tagged addresses
	 * if current has memory tagging enabled.
	 *
	 * To enable a tracer to set a tracees selector the
	 * selector address must be untagged for access_ok(),
	 * otherwise an untagged tracer will always fail to set a
	 * tagged tracees selector.
	 */
	if (mode != PR_SYS_DISPATCH_OFF && selector &&
		!access_ok(untagged_addr(selector), sizeof(*selector)))
		return -EFAULT;

	task->syscall_dispatch.selector = selector;
	task->syscall_dispatch.offset = offset;
	task->syscall_dispatch.len = len;
	task->syscall_dispatch.on_dispatch = false;

	if (mode != PR_SYS_DISPATCH_OFF)
		set_task_syscall_work(task, SYSCALL_USER_DISPATCH);
	else
		clear_task_syscall_work(task, SYSCALL_USER_DISPATCH);

	return 0;
}

int set_syscall_user_dispatch(unsigned long mode, unsigned long offset,
			      unsigned long len, char __user *selector)
{
	return task_set_syscall_user_dispatch(current, mode, offset, len, selector);
}

int syscall_user_dispatch_get_config(struct task_struct *task, unsigned long size,
				     void __user *data)
{
	struct syscall_user_dispatch *sd = &task->syscall_dispatch;
	struct ptrace_sud_config cfg;

	if (size != sizeof(cfg))
		return -EINVAL;

	if (test_task_syscall_work(task, SYSCALL_USER_DISPATCH))
		cfg.mode = PR_SYS_DISPATCH_ON;
	else
		cfg.mode = PR_SYS_DISPATCH_OFF;

	cfg.offset = sd->offset;
	cfg.len = sd->len;
	cfg.selector = (__u64)(uintptr_t)sd->selector;

	if (copy_to_user(data, &cfg, sizeof(cfg)))
		return -EFAULT;

	return 0;
}

int syscall_user_dispatch_set_config(struct task_struct *task, unsigned long size,
				     void __user *data)
{
	struct ptrace_sud_config cfg;

	if (size != sizeof(cfg))
		return -EINVAL;

	if (copy_from_user(&cfg, data, sizeof(cfg)))
		return -EFAULT;

	return task_set_syscall_user_dispatch(task, cfg.mode, cfg.offset, cfg.len,
					      (char __user *)(uintptr_t)cfg.selector);
}
