/*
 *  Copyright (C) 2007 IBM Corporation
 *
 *  Author: Cedric Le Goater <clg@fr.ibm.com>
 *
 *  This program is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU General Public License as
 *  published by the Free Software Foundation, version 2 of the
 *  License.
 */

#include <linux/nsproxy.h>
#include <linux/ipc_namespace.h>
#include <linux/sysctl.h>

/*
 * Define the ranges various user-specified maximum values can
 * be set to.
 */
#define MIN_MSGMAX	1		/* min value for msg_max */
#define MAX_MSGMAX	HARD_MSGMAX	/* max value for msg_max */
#define MIN_MSGSIZEMAX	128		/* min value for msgsize_max */
#define MAX_MSGSIZEMAX	(8192*128)	/* max value for msgsize_max */

static void *get_mq(ctl_table *table)
{
	char *which = table->data;
	struct ipc_namespace *ipc_ns = current->nsproxy->ipc_ns;
	which = (which - (char *)&init_ipc_ns) + (char *)ipc_ns;
	return which;
}

#ifdef CONFIG_PROC_SYSCTL
static int proc_mq_dointvec(ctl_table *table, int write, struct file *filp,
	void __user *buffer, size_t *lenp, loff_t *ppos)
{
	struct ctl_table mq_table;
	memcpy(&mq_table, table, sizeof(mq_table));
	mq_table.data = get_mq(table);

	return proc_dointvec(&mq_table, write, filp, buffer, lenp, ppos);
}

static int proc_mq_dointvec_minmax(ctl_table *table, int write,
	struct file *filp, void __user *buffer, size_t *lenp, loff_t *ppos)
{
	struct ctl_table mq_table;
	memcpy(&mq_table, table, sizeof(mq_table));
	mq_table.data = get_mq(table);

	return proc_dointvec_minmax(&mq_table, write, filp, buffer,
					lenp, ppos);
}
#else
#define proc_mq_dointvec NULL
#define proc_mq_dointvec_minmax NULL
#endif

static int msg_max_limit_min = MIN_MSGMAX;
static int msg_max_limit_max = MAX_MSGMAX;

static int msg_maxsize_limit_min = MIN_MSGSIZEMAX;
static int msg_maxsize_limit_max = MAX_MSGSIZEMAX;

static ctl_table mq_sysctls[] = {
	{
		.procname	= "queues_max",
		.data		= &init_ipc_ns.mq_queues_max,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_mq_dointvec,
	},
	{
		.procname	= "msg_max",
		.data		= &init_ipc_ns.mq_msg_max,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_mq_dointvec_minmax,
		.extra1		= &msg_max_limit_min,
		.extra2		= &msg_max_limit_max,
	},
	{
		.procname	= "msgsize_max",
		.data		= &init_ipc_ns.mq_msgsize_max,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_mq_dointvec_minmax,
		.extra1		= &msg_maxsize_limit_min,
		.extra2		= &msg_maxsize_limit_max,
	},
	{ .ctl_name = 0 }
};

static ctl_table mq_sysctl_dir[] = {
	{
		.procname	= "mqueue",
		.mode		= 0555,
		.child		= mq_sysctls,
	},
	{ .ctl_name = 0 }
};

static ctl_table mq_sysctl_root[] = {
	{
		.ctl_name	= CTL_FS,
		.procname	= "fs",
		.mode		= 0555,
		.child		= mq_sysctl_dir,
	},
	{ .ctl_name = 0 }
};

struct ctl_table_header *mq_register_sysctl_table(void)
{
	return register_sysctl_table(mq_sysctl_root);
}
