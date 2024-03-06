// SPDX-License-Identifier: GPL-2.0-only
/*
 *  Copyright (C) 2007 IBM Corporation
 *
 *  Author: Cedric Le Goater <clg@fr.ibm.com>
 */

#include <linux/nsproxy.h>
#include <linux/ipc_namespace.h>
#include <linux/sysctl.h>

#include <linux/stat.h>
#include <linux/capability.h>
#include <linux/slab.h>

static int msg_max_limit_min = MIN_MSGMAX;
static int msg_max_limit_max = HARD_MSGMAX;

static int msg_maxsize_limit_min = MIN_MSGSIZEMAX;
static int msg_maxsize_limit_max = HARD_MSGSIZEMAX;

static struct ctl_table mq_sysctls[] = {
	{
		.procname	= "queues_max",
		.data		= &init_ipc_ns.mq_queues_max,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec,
	},
	{
		.procname	= "msg_max",
		.data		= &init_ipc_ns.mq_msg_max,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec_minmax,
		.extra1		= &msg_max_limit_min,
		.extra2		= &msg_max_limit_max,
	},
	{
		.procname	= "msgsize_max",
		.data		= &init_ipc_ns.mq_msgsize_max,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec_minmax,
		.extra1		= &msg_maxsize_limit_min,
		.extra2		= &msg_maxsize_limit_max,
	},
	{
		.procname	= "msg_default",
		.data		= &init_ipc_ns.mq_msg_default,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec_minmax,
		.extra1		= &msg_max_limit_min,
		.extra2		= &msg_max_limit_max,
	},
	{
		.procname	= "msgsize_default",
		.data		= &init_ipc_ns.mq_msgsize_default,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec_minmax,
		.extra1		= &msg_maxsize_limit_min,
		.extra2		= &msg_maxsize_limit_max,
	},
	{}
};

static struct ctl_table_set *set_lookup(struct ctl_table_root *root)
{
	return &current->nsproxy->ipc_ns->mq_set;
}

static int set_is_seen(struct ctl_table_set *set)
{
	return &current->nsproxy->ipc_ns->mq_set == set;
}

static struct ctl_table_root set_root = {
	.lookup = set_lookup,
};

bool setup_mq_sysctls(struct ipc_namespace *ns)
{
	struct ctl_table *tbl;

	setup_sysctl_set(&ns->mq_set, &set_root, set_is_seen);

	tbl = kmemdup(mq_sysctls, sizeof(mq_sysctls), GFP_KERNEL);
	if (tbl) {
		int i;

		for (i = 0; i < ARRAY_SIZE(mq_sysctls); i++) {
			if (tbl[i].data == &init_ipc_ns.mq_queues_max)
				tbl[i].data = &ns->mq_queues_max;

			else if (tbl[i].data == &init_ipc_ns.mq_msg_max)
				tbl[i].data = &ns->mq_msg_max;

			else if (tbl[i].data == &init_ipc_ns.mq_msgsize_max)
				tbl[i].data = &ns->mq_msgsize_max;

			else if (tbl[i].data == &init_ipc_ns.mq_msg_default)
				tbl[i].data = &ns->mq_msg_default;

			else if (tbl[i].data == &init_ipc_ns.mq_msgsize_default)
				tbl[i].data = &ns->mq_msgsize_default;
			else
				tbl[i].data = NULL;
		}

		ns->mq_sysctls = __register_sysctl_table(&ns->mq_set,
							 "fs/mqueue", tbl,
							 ARRAY_SIZE(mq_sysctls));
	}
	if (!ns->mq_sysctls) {
		kfree(tbl);
		retire_sysctl_set(&ns->mq_set);
		return false;
	}

	return true;
}

void retire_mq_sysctls(struct ipc_namespace *ns)
{
	struct ctl_table *tbl;

	tbl = ns->mq_sysctls->ctl_table_arg;
	unregister_sysctl_table(ns->mq_sysctls);
	retire_sysctl_set(&ns->mq_set);
	kfree(tbl);
}
