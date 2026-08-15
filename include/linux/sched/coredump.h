/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_SCHED_COREDUMP_H
#define _LINUX_SCHED_COREDUMP_H

/*
 * Task dumpability mode.  Gates core dump production and ptrace_attach()
 * authorization.  The numeric values are stable ABI (suid_dumpable
 * sysctl, prctl(PR_SET_DUMPABLE)); do not renumber.
 */
enum task_dumpable {
	TASK_DUMPABLE_OFF	= 0,	/* no dump; ptrace needs CAP_SYS_PTRACE */
	TASK_DUMPABLE_OWNER	= 1,	/* default; dump and ptrace by uid match */
	TASK_DUMPABLE_ROOT	= 2,	/* dump as root; ptrace needs CAP_SYS_PTRACE */
};

void task_exec_state_set_dumpable(enum task_dumpable value);
enum task_dumpable task_exec_state_get_dumpable(struct task_struct *task);

#endif /* _LINUX_SCHED_COREDUMP_H */
