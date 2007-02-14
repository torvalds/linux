/*
 *  Copyright (C) 2007
 *
 *  Author: Eric Biederman <ebiederm@xmision.com>
 *
 *  This program is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU General Public License as
 *  published by the Free Software Foundation, version 2 of the
 *  License.
 */

#include <linux/module.h>
#include <linux/ipc.h>
#include <linux/nsproxy.h>
#include <linux/sysctl.h>
#include <linux/uaccess.h>

#ifdef CONFIG_IPC_NS
static void *get_ipc(ctl_table *table)
{
	char *which = table->data;
	struct ipc_namespace *ipc_ns = current->nsproxy->ipc_ns;
	which = (which - (char *)&init_ipc_ns) + (char *)ipc_ns;
	return which;
}
#else
#define get_ipc(T) ((T)->data)
#endif

#ifdef CONFIG_PROC_FS
static int proc_ipc_dointvec(ctl_table *table, int write, struct file *filp,
	void __user *buffer, size_t *lenp, loff_t *ppos)
{
	struct ctl_table ipc_table;
	memcpy(&ipc_table, table, sizeof(ipc_table));
	ipc_table.data = get_ipc(table);

	return proc_dointvec(&ipc_table, write, filp, buffer, lenp, ppos);
}

static int proc_ipc_doulongvec_minmax(ctl_table *table, int write,
	struct file *filp, void __user *buffer, size_t *lenp, loff_t *ppos)
{
	struct ctl_table ipc_table;
	memcpy(&ipc_table, table, sizeof(ipc_table));
	ipc_table.data = get_ipc(table);

	return proc_doulongvec_minmax(&ipc_table, write, filp, buffer,
					lenp, ppos);
}

#else
#define proc_ipc_doulongvec_minmax NULL
#define proc_ipc_dointvec	   NULL
#endif

#ifdef CONFIG_SYSCTL_SYSCALL
/* The generic sysctl ipc data routine. */
static int sysctl_ipc_data(ctl_table *table, int __user *name, int nlen,
		void __user *oldval, size_t __user *oldlenp,
		void __user *newval, size_t newlen)
{
	size_t len;
	void *data;

	/* Get out of I don't have a variable */
	if (!table->data || !table->maxlen)
		return -ENOTDIR;

	data = get_ipc(table);
	if (!data)
		return -ENOTDIR;

	if (oldval && oldlenp) {
		if (get_user(len, oldlenp))
			return -EFAULT;
		if (len) {
			if (len > table->maxlen)
				len = table->maxlen;
			if (copy_to_user(oldval, data, len))
				return -EFAULT;
			if (put_user(len, oldlenp))
				return -EFAULT;
		}
	}

	if (newval && newlen) {
		if (newlen > table->maxlen)
			newlen = table->maxlen;

		if (copy_from_user(data, newval, newlen))
			return -EFAULT;
	}
	return 1;
}
#else
#define sysctl_ipc_data NULL
#endif

static struct ctl_table ipc_kern_table[] = {
	{
		.ctl_name	= KERN_SHMMAX,
		.procname	= "shmmax",
		.data		= &init_ipc_ns.shm_ctlmax,
		.maxlen		= sizeof (init_ipc_ns.shm_ctlmax),
		.mode		= 0644,
		.proc_handler	= proc_ipc_doulongvec_minmax,
		.strategy	= sysctl_ipc_data,
	},
	{
		.ctl_name	= KERN_SHMALL,
		.procname	= "shmall",
		.data		= &init_ipc_ns.shm_ctlall,
		.maxlen		= sizeof (init_ipc_ns.shm_ctlall),
		.mode		= 0644,
		.proc_handler	= proc_ipc_doulongvec_minmax,
		.strategy	= sysctl_ipc_data,
	},
	{
		.ctl_name	= KERN_SHMMNI,
		.procname	= "shmmni",
		.data		= &init_ipc_ns.shm_ctlmni,
		.maxlen		= sizeof (init_ipc_ns.shm_ctlmni),
		.mode		= 0644,
		.proc_handler	= proc_ipc_dointvec,
		.strategy	= sysctl_ipc_data,
	},
	{
		.ctl_name	= KERN_MSGMAX,
		.procname	= "msgmax",
		.data		= &init_ipc_ns.msg_ctlmax,
		.maxlen		= sizeof (init_ipc_ns.msg_ctlmax),
		.mode		= 0644,
		.proc_handler	= proc_ipc_dointvec,
		.strategy	= sysctl_ipc_data,
	},
	{
		.ctl_name	= KERN_MSGMNI,
		.procname	= "msgmni",
		.data		= &init_ipc_ns.msg_ctlmni,
		.maxlen		= sizeof (init_ipc_ns.msg_ctlmni),
		.mode		= 0644,
		.proc_handler	= proc_ipc_dointvec,
		.strategy	= sysctl_ipc_data,
	},
	{
		.ctl_name	= KERN_MSGMNB,
		.procname	=  "msgmnb",
		.data		= &init_ipc_ns.msg_ctlmnb,
		.maxlen		= sizeof (init_ipc_ns.msg_ctlmnb),
		.mode		= 0644,
		.proc_handler	= proc_ipc_dointvec,
		.strategy	= sysctl_ipc_data,
	},
	{
		.ctl_name	= KERN_SEM,
		.procname	= "sem",
		.data		= &init_ipc_ns.sem_ctls,
		.maxlen		= 4*sizeof (int),
		.mode		= 0644,
		.proc_handler	= proc_ipc_dointvec,
		.strategy	= sysctl_ipc_data,
	},
	{}
};

static struct ctl_table ipc_root_table[] = {
	{
		.ctl_name	= CTL_KERN,
		.procname	= "kernel",
		.mode		= 0555,
		.child		= ipc_kern_table,
	},
	{}
};

static int __init ipc_sysctl_init(void)
{
	register_sysctl_table(ipc_root_table);
	return 0;
}

__initcall(ipc_sysctl_init);
