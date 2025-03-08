// SPDX-License-Identifier: GPL-2.0

#include <linux/debugfs.h>

#include "nfsd.h"

static struct dentry *nfsd_top_dir __read_mostly;

void nfsd_debugfs_exit(void)
{
	debugfs_remove_recursive(nfsd_top_dir);
	nfsd_top_dir = NULL;
}

void nfsd_debugfs_init(void)
{
	nfsd_top_dir = debugfs_create_dir("nfsd", NULL);
}
