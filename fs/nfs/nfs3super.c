// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2012 Netapp, Inc. All rights reserved.
 */
#include <linux/module.h>
#include <linux/nfs_fs.h>
#include "internal.h"
#include "nfs3_fs.h"
#include "nfs.h"

struct nfs_subversion nfs_v3 = {
	.owner = THIS_MODULE,
	.nfs_fs   = &nfs_fs_type,
	.rpc_vers = &nfs_version3,
	.rpc_ops  = &nfs_v3_clientops,
	.sops     = &nfs_sops,
};

static int __init init_nfs_v3(void)
{
	register_nfs_version(&nfs_v3);
	return 0;
}

static void __exit exit_nfs_v3(void)
{
	unregister_nfs_version(&nfs_v3);
}

MODULE_LICENSE("GPL");

module_init(init_nfs_v3);
module_exit(exit_nfs_v3);
