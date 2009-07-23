/*
 * Definitions for diskquota-operations. When diskquota is configured these
 * macros expand to the right source-code.
 *
 * Author:  Marco van Wieringen <mvw@planets.elm.net>
 */
#ifndef _LINUX_QUOTAOPS_
#define _LINUX_QUOTAOPS_

#include <linux/fs.h>

static inline struct quota_info *sb_dqopt(struct super_block *sb)
{
	return &sb->s_dquot;
}

#if defined(CONFIG_QUOTA)

/*
 * declaration of quota_function calls in kernel.
 */
void sync_quota_sb(struct super_block *sb, int type);
static inline void writeout_quota_sb(struct super_block *sb, int type)
{
	if (sb->s_qcop->quota_sync)
		sb->s_qcop->quota_sync(sb, type);
}

int dquot_initialize(struct inode *inode, int type);
int dquot_drop(struct inode *inode);
struct dquot *dqget(struct super_block *sb, unsigned int id, int type);
void dqput(struct dquot *dquot);
int dquot_scan_active(struct super_block *sb,
		      int (*fn)(struct dquot *dquot, unsigned long priv),
		      unsigned long priv);
struct dquot *dquot_alloc(struct super_block *sb, int type);
void dquot_destroy(struct dquot *dquot);

int dquot_alloc_space(struct inode *inode, qsize_t number, int prealloc);
int dquot_alloc_inode(const struct inode *inode, qsize_t number);

int dquot_reserve_space(struct inode *inode, qsize_t number, int prealloc);
int dquot_claim_space(struct inode *inode, qsize_t number);
void dquot_release_reserved_space(struct inode *inode, qsize_t number);
qsize_t dquot_get_reserved_space(struct inode *inode);

int dquot_free_space(struct inode *inode, qsize_t number);
int dquot_free_inode(const struct inode *inode, qsize_t number);

int dquot_transfer(struct inode *inode, struct iattr *iattr);
int dquot_commit(struct dquot *dquot);
int dquot_acquire(struct dquot *dquot);
int dquot_release(struct dquot *dquot);
int dquot_commit_info(struct super_block *sb, int type);
int dquot_mark_dquot_dirty(struct dquot *dquot);

int vfs_quota_on(struct super_block *sb, int type, int format_id,
 	char *path, int remount);
int vfs_quota_enable(struct inode *inode, int type, int format_id,
	unsigned int flags);
int vfs_quota_on_path(struct super_block *sb, int type, int format_id,
 	struct path *path);
int vfs_quota_on_mount(struct super_block *sb, char *qf_name,
 	int format_id, int type);
int vfs_quota_off(struct super_block *sb, int type, int remount);
int vfs_quota_disable(struct super_block *sb, int type, unsigned int flags);
int vfs_quota_sync(struct super_block *sb, int type);
int vfs_get_dqinfo(struct super_block *sb, int type, struct if_dqinfo *ii);
int vfs_set_dqinfo(struct super_block *sb, int type, struct if_dqinfo *ii);
int vfs_get_dqblk(struct super_block *sb, int type, qid_t id, struct if_dqblk *di);
int vfs_set_dqblk(struct super_block *sb, int type, qid_t id, struct if_dqblk *di);

void vfs_dq_drop(struct inode *inode);
int vfs_dq_transfer(struct inode *inode, struct iattr *iattr);
int vfs_dq_quota_on_remount(struct super_block *sb);

static inline struct mem_dqinfo *sb_dqinfo(struct super_block *sb, int type)
{
	return sb_dqopt(sb)->info + type;
}

/*
 * Functions for checking status of quota
 */

static inline int sb_has_quota_usage_enabled(struct super_block *sb, int type)
{
	return sb_dqopt(sb)->flags &
				dquot_state_flag(DQUOT_USAGE_ENABLED, type);
}

static inline int sb_has_quota_limits_enabled(struct super_block *sb, int type)
{
	return sb_dqopt(sb)->flags &
				dquot_state_flag(DQUOT_LIMITS_ENABLED, type);
}

static inline int sb_has_quota_suspended(struct super_block *sb, int type)
{
	return sb_dqopt(sb)->flags &
				dquot_state_flag(DQUOT_SUSPENDED, type);
}

static inline int sb_any_quota_suspended(struct super_block *sb)
{
	return sb_has_quota_suspended(sb, USRQUOTA) ||
		sb_has_quota_suspended(sb, GRPQUOTA);
}

/* Does kernel know about any quota information for given sb + type? */
static inline int sb_has_quota_loaded(struct super_block *sb, int type)
{
	/* Currently if anything is on, then quota usage is on as well */
	return sb_has_quota_usage_enabled(sb, type);
}

static inline int sb_any_quota_loaded(struct super_block *sb)
{
	return sb_has_quota_loaded(sb, USRQUOTA) ||
		sb_has_quota_loaded(sb, GRPQUOTA);
}

static inline int sb_has_quota_active(struct super_block *sb, int type)
{
	return sb_has_quota_loaded(sb, type) &&
	       !sb_has_quota_suspended(sb, type);
}

static inline int sb_any_quota_active(struct super_block *sb)
{
	return sb_has_quota_active(sb, USRQUOTA) ||
	       sb_has_quota_active(sb, GRPQUOTA);
}

/*
 * Operations supported for diskquotas.
 */
extern struct dquot_operations dquot_operations;
extern struct quotactl_ops vfs_quotactl_ops;

#define sb_dquot_ops (&dquot_operations)
#define sb_quotactl_ops (&vfs_quotactl_ops)

/* It is better to call this function outside of any transaction as it might
 * need a lot of space in journal for dquot structure allocation. */
static inline void vfs_dq_init(struct inode *inode)
{
	BUG_ON(!inode->i_sb);
	if (sb_any_quota_active(inode->i_sb) && !IS_NOQUOTA(inode))
		inode->i_sb->dq_op->initialize(inode, -1);
}

/* The following allocation/freeing/transfer functions *must* be called inside
 * a transaction (deadlocks possible otherwise) */
static inline int vfs_dq_prealloc_space_nodirty(struct inode *inode, qsize_t nr)
{
	if (sb_any_quota_active(inode->i_sb)) {
		/* Used space is updated in alloc_space() */
		if (inode->i_sb->dq_op->alloc_space(inode, nr, 1) == NO_QUOTA)
			return 1;
	}
	else
		inode_add_bytes(inode, nr);
	return 0;
}

static inline int vfs_dq_prealloc_space(struct inode *inode, qsize_t nr)
{
	int ret;
        if (!(ret =  vfs_dq_prealloc_space_nodirty(inode, nr)))
		mark_inode_dirty(inode);
	return ret;
}

static inline int vfs_dq_alloc_space_nodirty(struct inode *inode, qsize_t nr)
{
	if (sb_any_quota_active(inode->i_sb)) {
		/* Used space is updated in alloc_space() */
		if (inode->i_sb->dq_op->alloc_space(inode, nr, 0) == NO_QUOTA)
			return 1;
	}
	else
		inode_add_bytes(inode, nr);
	return 0;
}

static inline int vfs_dq_alloc_space(struct inode *inode, qsize_t nr)
{
	int ret;
	if (!(ret = vfs_dq_alloc_space_nodirty(inode, nr)))
		mark_inode_dirty(inode);
	return ret;
}

static inline int vfs_dq_reserve_space(struct inode *inode, qsize_t nr)
{
	if (sb_any_quota_active(inode->i_sb)) {
		/* Used space is updated in alloc_space() */
		if (inode->i_sb->dq_op->reserve_space(inode, nr, 0) == NO_QUOTA)
			return 1;
	}
	return 0;
}

static inline int vfs_dq_alloc_inode(struct inode *inode)
{
	if (sb_any_quota_active(inode->i_sb)) {
		vfs_dq_init(inode);
		if (inode->i_sb->dq_op->alloc_inode(inode, 1) == NO_QUOTA)
			return 1;
	}
	return 0;
}

/*
 * Convert in-memory reserved quotas to real consumed quotas
 */
static inline int vfs_dq_claim_space(struct inode *inode, qsize_t nr)
{
	if (sb_any_quota_active(inode->i_sb)) {
		if (inode->i_sb->dq_op->claim_space(inode, nr) == NO_QUOTA)
			return 1;
	} else
		inode_add_bytes(inode, nr);

	mark_inode_dirty(inode);
	return 0;
}

/*
 * Release reserved (in-memory) quotas
 */
static inline
void vfs_dq_release_reservation_space(struct inode *inode, qsize_t nr)
{
	if (sb_any_quota_active(inode->i_sb))
		inode->i_sb->dq_op->release_rsv(inode, nr);
}

static inline void vfs_dq_free_space_nodirty(struct inode *inode, qsize_t nr)
{
	if (sb_any_quota_active(inode->i_sb))
		inode->i_sb->dq_op->free_space(inode, nr);
	else
		inode_sub_bytes(inode, nr);
}

static inline void vfs_dq_free_space(struct inode *inode, qsize_t nr)
{
	vfs_dq_free_space_nodirty(inode, nr);
	mark_inode_dirty(inode);
}

static inline void vfs_dq_free_inode(struct inode *inode)
{
	if (sb_any_quota_active(inode->i_sb))
		inode->i_sb->dq_op->free_inode(inode, 1);
}

/* Cannot be called inside a transaction */
static inline int vfs_dq_off(struct super_block *sb, int remount)
{
	int ret = -ENOSYS;

	if (sb->s_qcop && sb->s_qcop->quota_off)
		ret = sb->s_qcop->quota_off(sb, -1, remount);
	return ret;
}

#else

static inline int sb_has_quota_usage_enabled(struct super_block *sb, int type)
{
	return 0;
}

static inline int sb_has_quota_limits_enabled(struct super_block *sb, int type)
{
	return 0;
}

static inline int sb_has_quota_suspended(struct super_block *sb, int type)
{
	return 0;
}

static inline int sb_any_quota_suspended(struct super_block *sb)
{
	return 0;
}

/* Does kernel know about any quota information for given sb + type? */
static inline int sb_has_quota_loaded(struct super_block *sb, int type)
{
	return 0;
}

static inline int sb_any_quota_loaded(struct super_block *sb)
{
	return 0;
}

static inline int sb_has_quota_active(struct super_block *sb, int type)
{
	return 0;
}

static inline int sb_any_quota_active(struct super_block *sb)
{
	return 0;
}

/*
 * NO-OP when quota not configured.
 */
#define sb_dquot_ops				(NULL)
#define sb_quotactl_ops				(NULL)

static inline void vfs_dq_init(struct inode *inode)
{
}

static inline void vfs_dq_drop(struct inode *inode)
{
}

static inline int vfs_dq_alloc_inode(struct inode *inode)
{
	return 0;
}

static inline void vfs_dq_free_inode(struct inode *inode)
{
}

static inline void sync_quota_sb(struct super_block *sb, int type)
{
}

static inline void writeout_quota_sb(struct super_block *sb, int type)
{
}

static inline int vfs_dq_off(struct super_block *sb, int remount)
{
	return 0;
}

static inline int vfs_dq_quota_on_remount(struct super_block *sb)
{
	return 0;
}

static inline int vfs_dq_transfer(struct inode *inode, struct iattr *iattr)
{
	return 0;
}

static inline int vfs_dq_prealloc_space_nodirty(struct inode *inode, qsize_t nr)
{
	inode_add_bytes(inode, nr);
	return 0;
}

static inline int vfs_dq_prealloc_space(struct inode *inode, qsize_t nr)
{
	vfs_dq_prealloc_space_nodirty(inode, nr);
	mark_inode_dirty(inode);
	return 0;
}

static inline int vfs_dq_alloc_space_nodirty(struct inode *inode, qsize_t nr)
{
	inode_add_bytes(inode, nr);
	return 0;
}

static inline int vfs_dq_alloc_space(struct inode *inode, qsize_t nr)
{
	vfs_dq_alloc_space_nodirty(inode, nr);
	mark_inode_dirty(inode);
	return 0;
}

static inline int vfs_dq_reserve_space(struct inode *inode, qsize_t nr)
{
	return 0;
}

static inline int vfs_dq_claim_space(struct inode *inode, qsize_t nr)
{
	return vfs_dq_alloc_space(inode, nr);
}

static inline
int vfs_dq_release_reservation_space(struct inode *inode, qsize_t nr)
{
	return 0;
}

static inline void vfs_dq_free_space_nodirty(struct inode *inode, qsize_t nr)
{
	inode_sub_bytes(inode, nr);
}

static inline void vfs_dq_free_space(struct inode *inode, qsize_t nr)
{
	vfs_dq_free_space_nodirty(inode, nr);
	mark_inode_dirty(inode);
}	

#endif /* CONFIG_QUOTA */

static inline int vfs_dq_prealloc_block_nodirty(struct inode *inode, qsize_t nr)
{
	return vfs_dq_prealloc_space_nodirty(inode, nr << inode->i_blkbits);
}

static inline int vfs_dq_prealloc_block(struct inode *inode, qsize_t nr)
{
	return vfs_dq_prealloc_space(inode, nr << inode->i_blkbits);
}

static inline int vfs_dq_alloc_block_nodirty(struct inode *inode, qsize_t nr)
{
	return vfs_dq_alloc_space_nodirty(inode, nr << inode->i_blkbits);
}

static inline int vfs_dq_alloc_block(struct inode *inode, qsize_t nr)
{
	return vfs_dq_alloc_space(inode, nr << inode->i_blkbits);
}

static inline int vfs_dq_reserve_block(struct inode *inode, qsize_t nr)
{
	return vfs_dq_reserve_space(inode, nr << inode->i_blkbits);
}

static inline int vfs_dq_claim_block(struct inode *inode, qsize_t nr)
{
	return vfs_dq_claim_space(inode, nr << inode->i_blkbits);
}

static inline
void vfs_dq_release_reservation_block(struct inode *inode, qsize_t nr)
{
	vfs_dq_release_reservation_space(inode, nr << inode->i_blkbits);
}

static inline void vfs_dq_free_block_nodirty(struct inode *inode, qsize_t nr)
{
	vfs_dq_free_space_nodirty(inode, nr << inode->i_blkbits);
}

static inline void vfs_dq_free_block(struct inode *inode, qsize_t nr)
{
	vfs_dq_free_space(inode, nr << inode->i_blkbits);
}

#endif /* _LINUX_QUOTAOPS_ */
