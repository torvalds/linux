/* SPDX-License-Identifier: GPL-2.0 */
#ifndef LINUX_PID_SYSCTL_H
#define LINUX_PID_SYSCTL_H

#include <linux/pid_namespace.h>

#if defined(CONFIG_SYSCTL) && defined(CONFIG_MEMFD_CREATE)
static int pid_mfd_noexec_dointvec_minmax(const struct ctl_table *table,
	int write, void *buf, size_t *lenp, loff_t *ppos)
{
	struct pid_namespace *ns = task_active_pid_ns(current);
	struct ctl_table table_copy;
	int err, scope, parent_scope;

	if (write && !ns_capable(ns->user_ns, CAP_SYS_ADMIN))
		return -EPERM;

	table_copy = *table;

	/* You cannot set a lower enforcement value than your parent. */
	parent_scope = pidns_memfd_noexec_scope(ns->parent);
	/* Equivalent to pidns_memfd_noexec_scope(ns). */
	scope = max(READ_ONCE(ns->memfd_noexec_scope), parent_scope);

	table_copy.data = &scope;
	table_copy.extra1 = &parent_scope;

	err = proc_dointvec_minmax(&table_copy, write, buf, lenp, ppos);
	if (!err && write)
		WRITE_ONCE(ns->memfd_noexec_scope, scope);
	return err;
}

static const struct ctl_table pid_ns_ctl_table_vm[] = {
	{
		.procname	= "memfd_noexec",
		.data		= &init_pid_ns.memfd_noexec_scope,
		.maxlen		= sizeof(init_pid_ns.memfd_noexec_scope),
		.mode		= 0644,
		.proc_handler	= pid_mfd_noexec_dointvec_minmax,
		.extra1		= SYSCTL_ZERO,
		.extra2		= SYSCTL_TWO,
	},
};
static inline void register_pid_ns_sysctl_table_vm(void)
{
	register_sysctl("vm", pid_ns_ctl_table_vm);
}
#else
static inline void register_pid_ns_sysctl_table_vm(void) {}
#endif

#endif /* LINUX_PID_SYSCTL_H */
