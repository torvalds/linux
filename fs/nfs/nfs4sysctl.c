// SPDX-License-Identifier: GPL-2.0
/*
 * linux/fs/nfs/nfs4sysctl.c
 *
 * Sysctl interface to NFS v4 parameters
 *
 * Copyright (c) 2006 Trond Myklebust <Trond.Myklebust@netapp.com>
 */
#include <linux/sysctl.h>
#include <linux/nfs_fs.h>

#include "nfs4_fs.h"
#include "nfs4idmap.h"
#include "callback.h"

static const int nfs_set_port_min;
static const int nfs_set_port_max = 65535;
static struct ctl_table_header *nfs4_callback_sysctl_table;

static struct ctl_table nfs4_cb_sysctls[] = {
	{
		.procname = "nfs_callback_tcpport",
		.data = &nfs_callback_set_tcpport,
		.maxlen = sizeof(int),
		.mode = 0644,
		.proc_handler = proc_dointvec_minmax,
		.extra1 = (int *)&nfs_set_port_min,
		.extra2 = (int *)&nfs_set_port_max,
	},
	{
		.procname = "idmap_cache_timeout",
		.data = &nfs_idmap_cache_timeout,
		.maxlen = sizeof(int),
		.mode = 0644,
		.proc_handler = proc_dointvec_jiffies,
	},
	{ }
};

static struct ctl_table nfs4_cb_sysctl_dir[] = {
	{
		.procname = "nfs",
		.mode = 0555,
		.child = nfs4_cb_sysctls,
	},
	{ }
};

static struct ctl_table nfs4_cb_sysctl_root[] = {
	{
		.procname = "fs",
		.mode = 0555,
		.child = nfs4_cb_sysctl_dir,
	},
	{ }
};

int nfs4_register_sysctl(void)
{
	nfs4_callback_sysctl_table = register_sysctl_table(nfs4_cb_sysctl_root);
	if (nfs4_callback_sysctl_table == NULL)
		return -ENOMEM;
	return 0;
}

void nfs4_unregister_sysctl(void)
{
	unregister_sysctl_table(nfs4_callback_sysctl_table);
	nfs4_callback_sysctl_table = NULL;
}
