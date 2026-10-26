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
#include <linux/nfsd_ssc.h>

struct nfs_ssc_client_ops_tbl {
	const struct nfs4_ssc_client_ops __rcu *ssc_nfs4_ops;
};

static struct nfs_ssc_client_ops_tbl nfs_ssc_client_tbl __read_mostly;

/**
 * nfsd42_ssc_open - Open a file to be used for server-to-server copy
 * @ss_mnt: active mount point on which the source file resides
 * @src_fh: file handle of the source file to be copied
 * @stateid: stateid to use for COPY operation
 *
 * Caller must close the returned file using nfsd42_ssc_close().
 *
 * Return: an open file, or an ERR_PTR on error
 */
struct file *nfsd42_ssc_open(struct vfsmount *ss_mnt, struct nfs_fh *src_fh,
			     nfs4_stateid *stateid)
{
	/*
	 * Built under CONFIG_NFS_V4_2_SSC_HELPER, which the NFS client
	 * enables on its own. The dispatch below is live only when the
	 * server also sets CONFIG_NFSD_V4_2_INTER_SSC; without it the
	 * source file cannot be opened, so callers get -EIO.
	 */
#if IS_ENABLED(CONFIG_NFSD_V4_2_INTER_SSC)
	const struct nfs4_ssc_client_ops *ops;
	struct file *res;

	/*
	 * sco_open() sleeps and must not run inside an RCU read-side
	 * section. Pin the provider module so the open runs with the
	 * module held; try_module_get() fails once unregister begins,
	 * and the copy then gets -EIO.
	 */
	rcu_read_lock();
	ops = rcu_dereference(nfs_ssc_client_tbl.ssc_nfs4_ops);
	if (ops && try_module_get(ops->owner)) {
		rcu_read_unlock();
		res = ops->sco_open(ss_mnt, src_fh, stateid);
		module_put(ops->owner);
		return res;
	}
	rcu_read_unlock();
#endif

	return ERR_PTR(-EIO);
}
EXPORT_SYMBOL_GPL(nfsd42_ssc_open);

/**
 * nfsd42_ssc_close - Close a file opened with nfsd42_ssc_open()
 * @filp: struct file to be closed
 *
 * The real cleanup happens unconditionally in nfsd4_cleanup_inter_ssc().
 * The client ops table is read under RCU; nfs42_ssc_unregister() calls
 * synchronize_rcu() so unregistration cannot complete while a close is
 * in flight.
 */
void nfsd42_ssc_close(struct file *filp)
{
	/* Live only under CONFIG_NFSD_V4_2_INTER_SSC; see nfsd42_ssc_open(). */
#if IS_ENABLED(CONFIG_NFSD_V4_2_INTER_SSC)
	const struct nfs4_ssc_client_ops *ops;

	rcu_read_lock();
	ops = rcu_dereference(nfs_ssc_client_tbl.ssc_nfs4_ops);
	if (ops)
		ops->sco_close(filp);
	rcu_read_unlock();
#endif
}
EXPORT_SYMBOL_GPL(nfsd42_ssc_close);

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
	rcu_assign_pointer(nfs_ssc_client_tbl.ssc_nfs4_ops, ops);
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
	if (rcu_dereference_protected(nfs_ssc_client_tbl.ssc_nfs4_ops,
				      true) != ops)
		return;

	rcu_assign_pointer(nfs_ssc_client_tbl.ssc_nfs4_ops, NULL);
	synchronize_rcu();
}
EXPORT_SYMBOL_GPL(nfs42_ssc_unregister);
#endif /* CONFIG_NFS_V4_2 */
