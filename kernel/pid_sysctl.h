/* SPDX-License-Identifier: GPL-2.0 */
#ifndef LINUX_PID_SYSCTL_H
#define LINUX_PID_SYSCTL_H

#include <linux/pid_namespace.h>

#if defined(CONFIG_SYSCTL) && defined(CONFIG_MEMFD_CREATE)
static inline void initialize_memfd_noexec_scope(struct pid_namespace *ns)
{
	ns->memfd_noexec_scope =
		task_active_pid_ns(current)->memfd_noexec_scope;
}

static int pid_mfd_noexec_dointvec_minmax(struct ctl_table *table,
	int write, void *buf, size_t *lenp, loff_t *ppos)
{
	struct pid_namespace *ns = task_active_pid_ns(current);
	struct ctl_table table_copy;

	if (write && !ns_capable(ns->user_ns, CAP_SYS_ADMIN))
		return -EPERM;

	table_copy = *table;
	if (ns != &init_pid_ns)
		table_copy.data = &ns->memfd_noexec_scope;

	/*
	 * set minimum to current value, the effect is only bigger
	 * value is accepted.
	 */
	if (*(int *)table_copy.data > *(int *)table_copy.extra1)
		table_copy.extra1 = table_copy.data;

	return proc_dointvec_minmax(&table_copy, write, buf, lenp, ppos);
}

static struct ctl_table pid_ns_ctl_table_vm[] = {
	{
		.procname	= "memfd_noexec",
		.data		= &init_pid_ns.memfd_noexec_scope,
		.maxlen		= sizeof(init_pid_ns.memfd_noexec_scope),
		.mode		= 0644,
		.proc_handler	= pid_mfd_noexec_dointvec_minmax,
		.extra1		= SYSCTL_ZERO,
		.extra2		= SYSCTL_TWO,
	},
	{ }
};
static inline void register_pid_ns_sysctl_table_vm(void)
{
	register_sysctl("vm", pid_ns_ctl_table_vm);
}
#else
static inline void initialize_memfd_noexec_scope(struct pid_namespace *ns) {}
static inline void set_memfd_noexec_scope(struct pid_namespace *ns) {}
static inline void register_pid_ns_sysctl_table_vm(void) {}
#endif

#endif /* LINUX_PID_SYSCTL_H */
