// SPDX-License-Identifier: GPL-2.0-only
/*
 *  Copyright (C) 2007
 *
 *  Author: Eric Biederman <ebiederm@xmision.com>
 */

#include <linux/export.h>
#include <linux/uts.h>
#include <linux/utsname.h>
#include <linux/random.h>
#include <linux/sysctl.h>
#include <linux/wait.h>
#include <linux/rwsem.h>

#ifdef CONFIG_PROC_SYSCTL

static void *get_uts(const struct ctl_table *table)
{
	char *which = table->data;
	struct uts_namespace *uts_ns;

	uts_ns = current->nsproxy->uts_ns;
	which = (which - (char *)&init_uts_ns) + (char *)uts_ns;

	return which;
}

/*
 *	Special case of dostring for the UTS structure. This has locks
 *	to observe. Should this be in kernel/sys.c ????
 */
static int proc_do_uts_string(const struct ctl_table *table, int write,
		  void *buffer, size_t *lenp, loff_t *ppos)
{
	struct ctl_table uts_table;
	int r;
	char tmp_data[__NEW_UTS_LEN + 1];

	memcpy(&uts_table, table, sizeof(uts_table));
	uts_table.data = tmp_data;

	/*
	 * Buffer the value in tmp_data so that proc_dostring() can be called
	 * without holding any locks.
	 * We also need to read the original value in the write==1 case to
	 * support partial writes.
	 */
	down_read(&uts_sem);
	memcpy(tmp_data, get_uts(table), sizeof(tmp_data));
	up_read(&uts_sem);
	r = proc_dostring(&uts_table, write, buffer, lenp, ppos);

	if (write) {
		/*
		 * Write back the new value.
		 * Note that, since we dropped uts_sem, the result can
		 * theoretically be incorrect if there are two parallel writes
		 * at non-zero offsets to the same sysctl.
		 */
		add_device_randomness(tmp_data, sizeof(tmp_data));
		down_write(&uts_sem);
		memcpy(get_uts(table), tmp_data, sizeof(tmp_data));
		up_write(&uts_sem);
		proc_sys_poll_notify(table->poll);
	}

	return r;
}
#else
#define proc_do_uts_string NULL
#endif

static DEFINE_CTL_TABLE_POLL(hostname_poll);
static DEFINE_CTL_TABLE_POLL(domainname_poll);

// Note: update 'enum uts_proc' to match any changes to this table
static struct ctl_table uts_kern_table[] = {
	{
		.procname	= "arch",
		.data		= init_uts_ns.name.machine,
		.maxlen		= sizeof(init_uts_ns.name.machine),
		.mode		= 0444,
		.proc_handler	= proc_do_uts_string,
	},
	{
		.procname	= "ostype",
		.data		= init_uts_ns.name.sysname,
		.maxlen		= sizeof(init_uts_ns.name.sysname),
		.mode		= 0444,
		.proc_handler	= proc_do_uts_string,
	},
	{
		.procname	= "osrelease",
		.data		= init_uts_ns.name.release,
		.maxlen		= sizeof(init_uts_ns.name.release),
		.mode		= 0444,
		.proc_handler	= proc_do_uts_string,
	},
	{
		.procname	= "version",
		.data		= init_uts_ns.name.version,
		.maxlen		= sizeof(init_uts_ns.name.version),
		.mode		= 0444,
		.proc_handler	= proc_do_uts_string,
	},
	{
		.procname	= "hostname",
		.data		= init_uts_ns.name.nodename,
		.maxlen		= sizeof(init_uts_ns.name.nodename),
		.mode		= 0644,
		.proc_handler	= proc_do_uts_string,
		.poll		= &hostname_poll,
	},
	{
		.procname	= "domainname",
		.data		= init_uts_ns.name.domainname,
		.maxlen		= sizeof(init_uts_ns.name.domainname),
		.mode		= 0644,
		.proc_handler	= proc_do_uts_string,
		.poll		= &domainname_poll,
	},
};

#ifdef CONFIG_PROC_SYSCTL
/*
 * Notify userspace about a change in a certain entry of uts_kern_table,
 * identified by the parameter proc.
 */
void uts_proc_notify(enum uts_proc proc)
{
	struct ctl_table *table = &uts_kern_table[proc];

	proc_sys_poll_notify(table->poll);
}
#endif

static int __init utsname_sysctl_init(void)
{
	register_sysctl("kernel", uts_kern_table);
	return 0;
}

device_initcall(utsname_sysctl_init);
