// SPDX-License-Identifier: GPL-2.0-only
/*
 * Helper for knfsd's SSC to access ops in NFS client modules
 *
 * Author: Dai Ngo <dai.ngo@oracle.com>
 *
 * Copyright (c) 2020, Oracle and/or its affiliates.
 */

#include <linux/module.h>
#include <linux/fs.h>
#include <linux/nfs_ssc.h>
#include "../nfs/nfs4_fs.h"

MODULE_LICENSE("GPL");

struct nfs_ssc_client_ops_tbl nfs_ssc_client_tbl;
EXPORT_SYMBOL_GPL(nfs_ssc_client_tbl);

#ifdef CONFIG_NFS_V4_2
/**
 * nfs42_ssc_register - install the NFS_V4 client ops in the nfs_ssc_client_tbl
 * @ops: NFS_V4 ops to be installed
 *
 * Return values:
 *   None
 */
void nfs42_ssc_register(const struct nfs4_ssc_client_ops *ops)
{
	nfs_ssc_client_tbl.ssc_nfs4_ops = ops;
}
EXPORT_SYMBOL_GPL(nfs42_ssc_register);

/**
 * nfs42_ssc_unregister - uninstall the NFS_V4 client ops from
 *				the nfs_ssc_client_tbl
 * @ops: ops to be uninstalled
 *
 * Return values:
 *   None
 */
void nfs42_ssc_unregister(const struct nfs4_ssc_client_ops *ops)
{
	if (nfs_ssc_client_tbl.ssc_nfs4_ops != ops)
		return;

	nfs_ssc_client_tbl.ssc_nfs4_ops = NULL;
}
EXPORT_SYMBOL_GPL(nfs42_ssc_unregister);
#endif /* CONFIG_NFS_V4_2 */

#ifdef CONFIG_NFS_V4_2
/**
 * nfs_ssc_register - install the NFS_FS client ops in the nfs_ssc_client_tbl
 * @ops: NFS_FS ops to be installed
 *
 * Return values:
 *   None
 */
void nfs_ssc_register(const struct nfs_ssc_client_ops *ops)
{
	nfs_ssc_client_tbl.ssc_nfs_ops = ops;
}
EXPORT_SYMBOL_GPL(nfs_ssc_register);

/**
 * nfs_ssc_unregister - uninstall the NFS_FS client ops from
 *				the nfs_ssc_client_tbl
 * @ops: ops to be uninstalled
 *
 * Return values:
 *   None
 */
void nfs_ssc_unregister(const struct nfs_ssc_client_ops *ops)
{
	if (nfs_ssc_client_tbl.ssc_nfs_ops != ops)
		return;
	nfs_ssc_client_tbl.ssc_nfs_ops = NULL;
}
EXPORT_SYMBOL_GPL(nfs_ssc_unregister);

#else
void nfs_ssc_register(const struct nfs_ssc_client_ops *ops)
{
}
EXPORT_SYMBOL_GPL(nfs_ssc_register);

void nfs_ssc_unregister(const struct nfs_ssc_client_ops *ops)
{
}
EXPORT_SYMBOL_GPL(nfs_ssc_unregister);
#endif /* CONFIG_NFS_V4_2 */
