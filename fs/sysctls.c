// SPDX-License-Identifier: GPL-2.0
/*
 * /proc/sys/fs shared sysctls
 *
 * These sysctls are shared between different filesystems.
 */
#include <linux/init.h>
#include <linux/sysctl.h>

static struct ctl_table fs_shared_sysctls[] = {
	{
		.procname	= "overflowuid",
		.data		= &fs_overflowuid,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec_minmax,
		.extra1		= SYSCTL_ZERO,
		.extra2		= SYSCTL_MAXOLDUID,
	},
	{
		.procname	= "overflowgid",
		.data		= &fs_overflowgid,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec_minmax,
		.extra1		= SYSCTL_ZERO,
		.extra2		= SYSCTL_MAXOLDUID,
	},
	{ }
};

DECLARE_SYSCTL_BASE(fs, fs_shared_sysctls);

static int __init init_fs_sysctls(void)
{
	return register_sysctl_base(fs);
}

early_initcall(init_fs_sysctls);
