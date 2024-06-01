// SPDX-License-Identifier: GPL-2.0-only
/*
 * sysctl.c: General linux system control interface
 */

#include <linux/sysctl.h>
#include <linux/printk.h>
#include <linux/capability.h>
#include <linux/ratelimit.h>
#include "internal.h"

static const int ten_thousand = 10000;

static int proc_dointvec_minmax_sysadmin(struct ctl_table *table, int write,
				void *buffer, size_t *lenp, loff_t *ppos)
{
	if (write && !capable(CAP_SYS_ADMIN))
		return -EPERM;

	return proc_dointvec_minmax(table, write, buffer, lenp, ppos);
}

static struct ctl_table printk_sysctls[] = {
	{
		.procname	= "printk",
		.data		= &console_loglevel,
		.maxlen		= 4*sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec,
	},
	{
		.procname	= "printk_ratelimit",
		.data		= &printk_ratelimit_state.interval,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec_jiffies,
	},
	{
		.procname	= "printk_ratelimit_burst",
		.data		= &printk_ratelimit_state.burst,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec,
	},
	{
		.procname	= "printk_delay",
		.data		= &printk_delay_msec,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec_minmax,
		.extra1		= SYSCTL_ZERO,
		.extra2		= (void *)&ten_thousand,
	},
	{
		.procname	= "printk_devkmsg",
		.data		= devkmsg_log_str,
		.maxlen		= DEVKMSG_STR_MAX_SIZE,
		.mode		= 0644,
		.proc_handler	= devkmsg_sysctl_set_loglvl,
	},
	{
		.procname	= "dmesg_restrict",
		.data		= &dmesg_restrict,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec_minmax_sysadmin,
		.extra1		= SYSCTL_ZERO,
		.extra2		= SYSCTL_ONE,
	},
	{
		.procname	= "kptr_restrict",
		.data		= &kptr_restrict,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec_minmax_sysadmin,
		.extra1		= SYSCTL_ZERO,
		.extra2		= SYSCTL_TWO,
	},
};

void __init printk_sysctl_init(void)
{
	register_sysctl_init("kernel", printk_sysctls);
}
