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
	__u64 change_attr;
	struct rcu_head rcu;
};

int nfs_inode_set_delegation(struct inode *inode, struct rpc_cred *cred, struct nfs_openres *res);
void nfs_inode_reclaim_delegation(struct inode *inode, struct rpc_cred *cred, struct nfs_openres *res);
int nfs_inode_return_delegation(struct inode *inode);
int nfs_async_inode_return_delegation(struct inode *inode, const nfs4_stateid *stateid);
void nfs_inode_return_delegation_noreclaim(struct inode *inode);

struct inode *nfs_delegation_find_inode(struct nfs_client *clp, const struct nfs_fh *fhandle);
void nfs_return_all_delegations(struct super_block *sb);
void nfs_expire_all_delegations(struct nfs_client *clp);
void nfs_handle_cb_pathdown(struct nfs_client *clp);

void nfs_delegation_mark_reclaim(struct nfs_client *clp);
void nfs_delegation_reap_unclaimed(struct nfs_client *clp);

/* NFSv4 delegation-related procedures */
int nfs4_proc_delegreturn(struct inode *inode, struct rpc_cred *cred, const nfs4_stateid *stateid, int issync);
int nfs4_open_delegation_recall(struct nfs_open_context *ctx, struct nfs4_state *state, const nfs4_stateid *stateid);
int nfs4_lock_delegation_recall(struct nfs4_state *state, struct file_lock *fl);
int nfs4_copy_delegation_stateid(nfs4_stateid *dst, struct inode *inode);

static inline int nfs_have_delegation(struct inode *inode, int flags)
{
	struct nfs_delegation *delegation;
	int ret = 0;

	flags &= FMODE_READ|FMODE_WRITE;
	rcu_read_lock();
	delegation = rcu_dereference(NFS_I(inode)->delegation);
	if (delegation != NULL && (delegation->type & flags) == flags)
		ret = 1;
	rcu_read_unlock();
	return ret;
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
