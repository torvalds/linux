// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2020 Collabora Ltd.
 */
#include <linux/sched.h>
#include <linux/prctl.h>
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

int set_syscall_user_dispatch(unsigned long mode, unsigned long offset,
			      unsigned long len, char __user *selector)
{
	switch (mode) {
	case PR_SYS_DISPATCH_OFF:
		if (offset || len || selector)
			return -EINVAL;
		break;
	case PR_SYS_DISPATCH_ON:
		/*
		 * Validate the direct dispatcher region just for basic
		 * sanity against overflow and a 0-sized dispatcher
		 * region.  If the user is able to submit a syscall from
		 * an address, that address is obviously valid.
		 */
		if (offset && offset + len <= offset)
			return -EINVAL;

		if (selector && !access_ok(selector, sizeof(*selector)))
			return -EFAULT;

		break;
	default:
		return -EINVAL;
	}

	current->syscall_dispatch.selector = selector;
	current->syscall_dispatch.offset = offset;
	current->syscall_dispatch.len = len;
	current->syscall_dispatch.on_dispatch = false;

	if (mode == PR_SYS_DISPATCH_ON)
		set_syscall_work(SYSCALL_USER_DISPATCH);
	else
		clear_syscall_work(SYSCALL_USER_DISPATCH);

	return 0;
}
