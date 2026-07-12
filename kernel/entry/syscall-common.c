// SPDX-License-Identifier: GPL-2.0

#include <linux/audit.h>
#include <linux/entry-common.h>

#define CREATE_TRACE_POINTS
#include <trace/events/syscalls.h>

/* Out of line to prevent tracepoint code duplication */

void trace_syscall_enter(struct pt_regs *regs)
{
	trace_sys_enter(regs, syscall_get_nr(current, regs));
}

void trace_syscall_exit(struct pt_regs *regs, long ret)
{
	trace_sys_exit(regs, ret);
}

#ifdef CONFIG_AUDITSYSCALL
void syscall_enter_audit(struct pt_regs *regs)
{
	long syscall = syscall_get_nr(current, regs);
	unsigned long args[6];

	syscall_get_arguments(current, regs, args);
	__audit_syscall_entry(syscall, args[0], args[1], args[2], args[3]);
}
#endif
