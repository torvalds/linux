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
#include <linux/uts.h>
#include <linux/utsname.h>
#include <linux/version.h>
#include <linux/sysctl.h>

static void *get_uts(ctl_table *table, int write)
{
	char *which = table->data;
	struct uts_namespace *uts_ns;

	uts_ns = current->nsproxy->uts_ns;
	which = (which - (char *)&init_uts_ns) + (char *)uts_ns;

	if (!write)
		down_read(&uts_sem);
	else
		down_write(&uts_sem);
	return which;
}

static void put_uts(ctl_table *table, int write, void *which)
{
	if (!write)
		up_read(&uts_sem);
	else
		up_write(&uts_sem);
}

#ifdef CONFIG_PROC_FS
/*
 *	Special case of dostring for the UTS structure. This has locks
 *	to observe. Should this be in kernel/sys.c ????
 */
static int proc_do_uts_string(ctl_table *table, int write, struct file *filp,
		  void __user *buffer, size_t *lenp, loff_t *ppos)
{
	struct ctl_table uts_table;
	int r;
	memcpy(&uts_table, table, sizeof(uts_table));
	uts_table.data = get_uts(table, write);
	r = proc_dostring(&uts_table,write,filp,buffer,lenp, ppos);
	put_uts(table, write, uts_table.data);
	return r;
}
#else
#define proc_do_uts_string NULL
#endif


#ifdef CONFIG_SYSCTL_SYSCALL
/* The generic string strategy routine: */
static int sysctl_uts_string(ctl_table *table, int __user *name, int nlen,
		  void __user *oldval, size_t __user *oldlenp,
		  void __user *newval, size_t newlen)
{
	struct ctl_table uts_table;
	int r, write;
	write = newval && newlen;
	memcpy(&uts_table, table, sizeof(uts_table));
	uts_table.data = get_uts(table, write);
	r = sysctl_string(&uts_table, name, nlen,
		oldval, oldlenp, newval, newlen);
	put_uts(table, write, uts_table.data);
	return r;
}
#else
#define sysctl_uts_string NULL
#endif

static struct ctl_table uts_kern_table[] = {
	{
		.ctl_name	= KERN_OSTYPE,
		.procname	= "ostype",
		.data		= init_uts_ns.name.sysname,
		.maxlen		= sizeof(init_uts_ns.name.sysname),
		.mode		= 0444,
		.proc_handler	= proc_do_uts_string,
		.strategy	= sysctl_uts_string,
	},
	{
		.ctl_name	= KERN_OSRELEASE,
		.procname	= "osrelease",
		.data		= init_uts_ns.name.release,
		.maxlen		= sizeof(init_uts_ns.name.release),
		.mode		= 0444,
		.proc_handler	= proc_do_uts_string,
		.strategy	= sysctl_uts_string,
	},
	{
		.ctl_name	= KERN_VERSION,
		.procname	= "version",
		.data		= init_uts_ns.name.version,
		.maxlen		= sizeof(init_uts_ns.name.version),
		.mode		= 0444,
		.proc_handler	= proc_do_uts_string,
		.strategy	= sysctl_uts_string,
	},
	{
		.ctl_name	= KERN_NODENAME,
		.procname	= "hostname",
		.data		= init_uts_ns.name.nodename,
		.maxlen		= sizeof(init_uts_ns.name.nodename),
		.mode		= 0644,
		.proc_handler	= proc_do_uts_string,
		.strategy	= sysctl_uts_string,
	},
	{
		.ctl_name	= KERN_DOMAINNAME,
		.procname	= "domainname",
		.data		= init_uts_ns.name.domainname,
		.maxlen		= sizeof(init_uts_ns.name.domainname),
		.mode		= 0644,
		.proc_handler	= proc_do_uts_string,
		.strategy	= sysctl_uts_string,
	},
	{}
};

static struct ctl_table uts_root_table[] = {
	{
		.ctl_name	= CTL_KERN,
		.procname	= "kernel",
		.mode		= 0555,
		.child		= uts_kern_table,
	},
	{}
};

static int __init utsname_sysctl_init(void)
{
	register_sysctl_table(uts_root_table);
	return 0;
}

__initcall(utsname_sysctl_init);
