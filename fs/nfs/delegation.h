/*
 * linux/fs/nfs/delegation.h
 *
 * Copyright (c) Trond Myklebust
 *
 * Definitions pertaining to NFS delegated files
 */
#ifndef FS_NFS_DELEGATION_H
#define FS_NFS_DELEGATION_H

#if defined(CONFIG_NFS_V4)
/*
 * NFSv4 delegation
 */
struct nfs_delegation {
	struct list_head super_list;
	struct rpc_cred *cred;
	struct inode *inode;
	nfs4_stateid stateid;
	int type;
#define NFS_DELEGATION_NEED_RECLAIM 1
	long flags;
	loff_t maxsize;
};

int nfs_inode_set_delegation(struct inode *inode, struct rpc_cred *cred, struct nfs_openres *res);
void nfs_inode_reclaim_delegation(struct inode *inode, struct rpc_cred *cred, struct nfs_openres *res);
int __nfs_inode_return_delegation(struct inode *inode);
int nfs_async_inode_return_delegation(struct inode *inode, const nfs4_stateid *stateid);

struct inode *nfs_delegation_find_inode(struct nfs4_client *clp, const struct nfs_fh *fhandle);
void nfs_return_all_delegations(struct super_block *sb);
void nfs_handle_cb_pathdown(struct nfs4_client *clp);

void nfs_delegation_mark_reclaim(struct nfs4_client *clp);
void nfs_delegation_reap_unclaimed(struct nfs4_client *clp);

/* NFSv4 delegation-related procedures */
int nfs4_proc_delegreturn(struct inode *inode, struct rpc_cred *cred, const nfs4_stateid *stateid);
int nfs4_open_delegation_recall(struct dentry *dentry, struct nfs4_state *state);

static inline int nfs_have_delegation(struct inode *inode, int flags)
{
	flags &= FMODE_READ|FMODE_WRITE;
	smp_rmb();
	if ((NFS_I(inode)->delegation_state & flags) == flags)
		return 1;
	return 0;
}

static inline int nfs_inode_return_delegation(struct inode *inode)
{
	int err = 0;

	if (NFS_I(inode)->delegation != NULL)
		err = __nfs_inode_return_delegation(inode);
	return err;
}
#else
static inline int nfs_have_delegation(struct inode *inode, int flags)
{
	return 0;
}

static inline int nfs_inode_return_delegation(struct inode *inode)
{
	return 0;
}
#endif

#endif
