/*
 * linux/fs/nfs/sysctl.c
 *
 * Sysctl interface to NFS parameters
 */
#include <linux/config.h>
#include <linux/types.h>
#include <linux/linkage.h>
#include <linux/ctype.h>
#include <linux/fs.h>
#include <linux/sysctl.h>
#include <linux/module.h>
#include <linux/nfs4.h>
#include <linux/nfs_idmap.h>

#include "callback.h"

static const int nfs_set_port_min = 0;
static const int nfs_set_port_max = 65535;
static struct ctl_table_header *nfs_callback_sysctl_table;
/*
 * Something that isn't CTL_ANY, CTL_NONE or a value that may clash.
 * Use the same values as fs/lockd/svc.c
 */
#define CTL_UNNUMBERED -2

static ctl_table nfs_cb_sysctls[] = {
#ifdef CONFIG_NFS_V4
	{
		.ctl_name = CTL_UNNUMBERED,
		.procname = "nfs_callback_tcpport",
		.data = &nfs_callback_set_tcpport,
		.maxlen = sizeof(int),
		.mode = 0644,
		.proc_handler = &proc_dointvec_minmax,
		.extra1 = (int *)&nfs_set_port_min,
		.extra2 = (int *)&nfs_set_port_max,
	},
	{
		.ctl_name = CTL_UNNUMBERED,
		.procname = "idmap_cache_timeout",
		.data = &nfs_idmap_cache_timeout,
		.maxlen = sizeof(int),
		.mode = 0644,
		.proc_handler = &proc_dointvec_jiffies,
		.strategy = &sysctl_jiffies,
	},
#endif
	{ .ctl_name = 0 }
};

static ctl_table nfs_cb_sysctl_dir[] = {
	{
		.ctl_name = CTL_UNNUMBERED,
		.procname = "nfs",
		.mode = 0555,
		.child = nfs_cb_sysctls,
	},
	{ .ctl_name = 0 }
};

static ctl_table nfs_cb_sysctl_root[] = {
	{
		.ctl_name = CTL_FS,
		.procname = "fs",
		.mode = 0555,
		.child = nfs_cb_sysctl_dir,
	},
	{ .ctl_name = 0 }
};

int nfs_register_sysctl(void)
{
	nfs_callback_sysctl_table = register_sysctl_table(nfs_cb_sysctl_root, 0);
	if (nfs_callback_sysctl_table == NULL)
		return -ENOMEM;
	return 0;
}

void nfs_unregister_sysctl(void)
{
	unregister_sysctl_table(nfs_callback_sysctl_table);
	nfs_callback_sysctl_table = NULL;
}
