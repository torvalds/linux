/* SPDX-License-Identifier: GPL-2.0 */
/*
 * linux/fs/nfs/delegation.h
 *
 * Copyright (c) Trond Myklebust
 *
 * Definitions pertaining to NFS delegated files
 */
#ifndef FS_NFS_DELEGATION_H
#define FS_NFS_DELEGATION_H

#if IS_ENABLED(CONFIG_NFS_V4)
/*
 * NFSv4 delegation
 */
struct nfs_delegation {
	struct list_head super_list;
	const struct cred *cred;
	struct inode *inode;
	nfs4_stateid stateid;
	fmode_t type;
	unsigned long pagemod_limit;
	__u64 change_attr;
	unsigned long flags;
	spinlock_t lock;
	struct rcu_head rcu;
};

enum {
	NFS_DELEGATION_NEED_RECLAIM = 0,
	NFS_DELEGATION_RETURN,
	NFS_DELEGATION_RETURN_IF_CLOSED,
	NFS_DELEGATION_REFERENCED,
	NFS_DELEGATION_RETURNING,
	NFS_DELEGATION_REVOKED,
	NFS_DELEGATION_TEST_EXPIRED,
	NFS_DELEGATION_INODE_FREEING,
};

int nfs_inode_set_delegation(struct inode *inode, const struct cred *cred,
		fmode_t type, const nfs4_stateid *stateid, unsigned long pagemod_limit);
void nfs_inode_reclaim_delegation(struct inode *inode, const struct cred *cred,
		fmode_t type, const nfs4_stateid *stateid, unsigned long pagemod_limit);
int nfs4_inode_return_delegation(struct inode *inode);
int nfs_async_inode_return_delegation(struct inode *inode, const nfs4_stateid *stateid);
void nfs_inode_return_delegation_noreclaim(struct inode *inode);

struct inode *nfs_delegation_find_inode(struct nfs_client *clp, const struct nfs_fh *fhandle);
void nfs_server_return_all_delegations(struct nfs_server *);
void nfs_expire_all_delegations(struct nfs_client *clp);
void nfs_expire_unused_delegation_types(struct nfs_client *clp, fmode_t flags);
void nfs_expire_unreferenced_delegations(struct nfs_client *clp);
int nfs_client_return_marked_delegations(struct nfs_client *clp);
int nfs_delegations_present(struct nfs_client *clp);
void nfs_remove_bad_delegation(struct inode *inode, const nfs4_stateid *stateid);

void nfs_delegation_mark_reclaim(struct nfs_client *clp);
void nfs_delegation_reap_unclaimed(struct nfs_client *clp);

void nfs_mark_test_expired_all_delegations(struct nfs_client *clp);
void nfs_reap_expired_delegations(struct nfs_client *clp);

/* NFSv4 delegation-related procedures */
int nfs4_proc_delegreturn(struct inode *inode, const struct cred *cred, const nfs4_stateid *stateid, int issync);
int nfs4_open_delegation_recall(struct nfs_open_context *ctx, struct nfs4_state *state, const nfs4_stateid *stateid, fmode_t type);
int nfs4_lock_delegation_recall(struct file_lock *fl, struct nfs4_state *state, const nfs4_stateid *stateid);
bool nfs4_copy_delegation_stateid(struct inode *inode, fmode_t flags, nfs4_stateid *dst, const struct cred **cred);
bool nfs4_refresh_delegation_stateid(nfs4_stateid *dst, struct inode *inode);

void nfs_mark_delegation_referenced(struct nfs_delegation *delegation);
int nfs4_have_delegation(struct inode *inode, fmode_t flags);
int nfs4_check_delegation(struct inode *inode, fmode_t flags);
bool nfs4_delegation_flush_on_close(const struct inode *inode);
void nfs_inode_find_delegation_state_and_recover(struct inode *inode,
		const nfs4_stateid *stateid);
int nfs4_inode_make_writeable(struct inode *inode);

#endif

static inline int nfs_have_delegated_attributes(struct inode *inode)
{
	return NFS_PROTO(inode)->have_delegation(inode, FMODE_READ) &&
		!(NFS_I(inode)->cache_validity & NFS_INO_REVAL_FORCED);
}

#endif
