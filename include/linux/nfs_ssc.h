/* SPDX-License-Identifier: GPL-2.0 */
/*
 * include/linux/nfs_ssc.h
 *
 * Author: Dai Ngo <dai.ngo@oracle.com>
 *
 * Copyright (c) 2020, Oracle and/or its affiliates.
 */

#include <linux/nfs_fs.h>
#include <linux/sunrpc/svc.h>

extern struct nfs_ssc_client_ops_tbl nfs_ssc_client_tbl;

/*
 * NFS_V4
 */
struct nfs4_ssc_client_ops {
	struct file *(*sco_open)(struct vfsmount *ss_mnt,
		struct nfs_fh *src_fh, nfs4_stateid *stateid);
	void (*sco_close)(struct file *filep);
};

/*
 * NFS_FS
 */
struct nfs_ssc_client_ops {
	void (*sco_sb_deactive)(struct super_block *sb);
};

struct nfs_ssc_client_ops_tbl {
	const struct nfs4_ssc_client_ops *ssc_nfs4_ops;
	const struct nfs_ssc_client_ops *ssc_nfs_ops;
};

extern void nfs42_ssc_register_ops(void);
extern void nfs42_ssc_unregister_ops(void);

extern void nfs42_ssc_register(const struct nfs4_ssc_client_ops *ops);
extern void nfs42_ssc_unregister(const struct nfs4_ssc_client_ops *ops);

#ifdef CONFIG_NFSD_V4_2_INTER_SSC
static inline struct file *nfs42_ssc_open(struct vfsmount *ss_mnt,
		struct nfs_fh *src_fh, nfs4_stateid *stateid)
{
	if (nfs_ssc_client_tbl.ssc_nfs4_ops)
		return (*nfs_ssc_client_tbl.ssc_nfs4_ops->sco_open)(ss_mnt, src_fh, stateid);
	return ERR_PTR(-EIO);
}

static inline void nfs42_ssc_close(struct file *filep)
{
	if (nfs_ssc_client_tbl.ssc_nfs4_ops)
		(*nfs_ssc_client_tbl.ssc_nfs4_ops->sco_close)(filep);
}
#endif

struct nfsd4_ssc_umount_item {
	struct list_head nsui_list;
	bool nsui_busy;
	/*
	 * nsui_refcnt inited to 2, 1 on list and 1 for consumer. Entry
	 * is removed when refcnt drops to 1 and nsui_expire expires.
	 */
	refcount_t nsui_refcnt;
	unsigned long nsui_expire;
	struct vfsmount *nsui_vfsmount;
	char nsui_ipaddr[RPC_MAX_ADDRBUFLEN + 1];
};

/*
 * NFS_FS
 */
extern void nfs_ssc_register(const struct nfs_ssc_client_ops *ops);
extern void nfs_ssc_unregister(const struct nfs_ssc_client_ops *ops);

static inline void nfs_do_sb_deactive(struct super_block *sb)
{
	if (nfs_ssc_client_tbl.ssc_nfs_ops)
		(*nfs_ssc_client_tbl.ssc_nfs_ops->sco_sb_deactive)(sb);
}
