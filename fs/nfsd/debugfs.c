// SPDX-License-Identifier: GPL-2.0

#include <linux/debugfs.h>

#include "nfsd.h"

static struct dentry *nfsd_top_dir __read_mostly;

/*
 * /sys/kernel/debug/nfsd/disable-splice-read
 *
 * Contents:
 *   %0: NFS READ is allowed to use page splicing
 *   %1: NFS READ uses only iov iter read
 *
 * The default value of this setting is zero (page splicing is
 * allowed). This setting takes immediate effect for all NFS
 * versions, all exports, and in all NFSD net namespaces.
 */

static int nfsd_dsr_get(void *data, u64 *val)
{
	*val = nfsd_disable_splice_read ? 1 : 0;
	return 0;
}

static int nfsd_dsr_set(void *data, u64 val)
{
	nfsd_disable_splice_read = (val > 0) ? true : false;
	return 0;
}

DEFINE_DEBUGFS_ATTRIBUTE(nfsd_dsr_fops, nfsd_dsr_get, nfsd_dsr_set, "%llu\n");

void nfsd_debugfs_exit(void)
{
	debugfs_remove_recursive(nfsd_top_dir);
	nfsd_top_dir = NULL;
}

void nfsd_debugfs_init(void)
{
	nfsd_top_dir = debugfs_create_dir("nfsd", NULL);

	debugfs_create_file("disable-splice-read", S_IWUSR | S_IRUGO,
			    nfsd_top_dir, NULL, &nfsd_dsr_fops);
}
