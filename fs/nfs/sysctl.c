/*
 * linux/fs/nfs/sysctl.c
 *
 * Sysctl interface to NFS parameters
 */
#include <linux/types.h>
#include <linux/linkage.h>
#include <linux/ctype.h>
#include <linux/fs.h>
#include <linux/sysctl.h>
#include <linux/module.h>
#include <linux/nfs_fs.h>

static struct ctl_table_header *nfs_callback_sysctl_table;

static struct ctl_table nfs_cb_sysctls[] = {
	{
		.procname	= "nfs_mountpoint_timeout",
		.data		= &nfs_mountpoint_expiry_timeout,
		.maxlen		= sizeof(nfs_mountpoint_expiry_timeout),
		.mode		= 0644,
		.proc_handler	= proc_dointvec_jiffies,
	},
	{
		.procname	= "nfs_congestion_kb",
		.data		= &nfs_congestion_kb,
		.maxlen		= sizeof(nfs_congestion_kb),
		.mode		= 0644,
		.proc_handler	= proc_dointvec,
	},
	{ }
};

static struct ctl_table nfs_cb_sysctl_dir[] = {
	{
		.procname = "nfs",
		.mode = 0555,
		.child = nfs_cb_sysctls,
	},
	{ }
};

static struct ctl_table nfs_cb_sysctl_root[] = {
	{
		.procname = "fs",
		.mode = 0555,
		.child = nfs_cb_sysctl_dir,
	},
	{ }
};

int nfs_register_sysctl(void)
{
	nfs_callback_sysctl_table = register_sysctl_table(nfs_cb_sysctl_root);
	if (nfs_callback_sysctl_table == NULL)
		return -ENOMEM;
	return 0;
}

void nfs_unregister_sysctl(void)
{
	unregister_sysctl_table(nfs_callback_sysctl_table);
	nfs_callback_sysctl_table = NULL;
}
