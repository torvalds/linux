/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Definitions for diskquota-operations. When diskquota is configured these
 * macros expand to the right source-code.
 *
 * Author:  Marco van Wieringen <mvw@planets.elm.net>
 */
#ifndef _LINUX_QUOTAOPS_
#define _LINUX_QUOTAOPS_

#include <linux/fs.h>

#define DQUOT_SPACE_WARN	0x1
#define DQUOT_SPACE_RESERVE	0x2
#define DQUOT_SPACE_NOFAIL	0x4

static inline struct quota_info *sb_dqopt(struct super_block *sb)
{
	return &sb->s_dquot;
}

/* i_mutex must being held */
static inline bool is_quota_modification(struct inode *inode, struct iattr *ia)
{
	return (ia->ia_valid & ATTR_SIZE) ||
		(ia->ia_valid & ATTR_UID && !uid_eq(ia->ia_uid, inode->i_uid)) ||
		(ia->ia_valid & ATTR_GID && !gid_eq(ia->ia_gid, inode->i_gid));
}

#if defined(CONFIG_QUOTA)

#define quota_error(sb, fmt, args...) \
	__quota_error((sb), __func__, fmt , ## args)

extern __printf(3, 4)
void __quota_error(struct super_block *sb, const char *func,
		   const char *fmt, ...);

/*
 * declaration of quota_function calls in kernel.
 */
int dquot_initialize(struct inode *inode);
bool dquot_initialize_needed(struct inode *inode);
void dquot_drop(struct inode *inode);
struct dquot *dqget(struct super_block *sb, struct kqid qid);
static inline struct dquot *dqgrab(struct dquot *dquot)
{
	/* Make sure someone else has active reference to dquot */
	WARN_ON_ONCE(!atomic_read(&dquot->dq_count));
	WARN_ON_ONCE(!test_bit(DQ_ACTIVE_B, &dquot->dq_flags));
	atomic_inc(&dquot->dq_count);
	return dquot;
}

static inline bool dquot_is_busy(struct dquot *dquot)
{
	if (test_bit(DQ_MOD_B, &dquot->dq_flags))
		return true;
	if (atomic_read(&dquot->dq_count) > 1)
		return true;
	return false;
}

void dqput(struct dquot *dquot);
int dquot_scan_active(struct super_block *sb,
		      int (*fn)(struct dquot *dquot, unsigned long priv),
		      unsigned long priv);
struct dquot *dquot_alloc(struct super_block *sb, int type);
void dquot_destroy(struct dquot *dquot);

int __dquot_alloc_space(struct inode *inode, qsize_t number, int flags);
void __dquot_free_space(struct inode *inode, qsize_t number, int flags);

int dquot_alloc_inode(struct inode *inode);

int dquot_claim_space_nodirty(struct inode *inode, qsize_t number);
void dquot_free_inode(struct inode *inode);
void dquot_reclaim_space_nodirty(struct inode *inode, qsize_t number);

int dquot_disable(struct super_block *sb, int type, unsigned int flags);
/* Suspend quotas on remount RO */
static inline int dquot_suspend(struct super_block *sb, int type)
{
	return dquot_disable(sb, type, DQUOT_SUSPENDED);
}
int dquot_resume(struct super_block *sb, int type);

int dquot_commit(struct dquot *dquot);
int dquot_acquire(struct dquot *dquot);
int dquot_release(struct dquot *dquot);
int dquot_commit_info(struct super_block *sb, int type);
int dquot_get_next_id(struct super_block *sb, struct kqid *qid);
int dquot_mark_dquot_dirty(struct dquot *dquot);

int dquot_file_open(struct inode *inode, struct file *file);

int dquot_load_quota_sb(struct super_block *sb, int type, int format_id,
	unsigned int flags);
int dquot_load_quota_inode(struct inode *inode, int type, int format_id,
	unsigned int flags);
int dquot_quota_on(struct super_block *sb, int type, int format_id,
	const struct path *path);
int dquot_quota_on_mount(struct super_block *sb, char *qf_name,
 	int format_id, int type);
int dquot_quota_off(struct super_block *sb, int type);
int dquot_writeback_dquots(struct super_block *sb, int type);
int dquot_quota_sync(struct super_block *sb, int type);
int dquot_get_state(struct super_block *sb, struct qc_state *state);
int dquot_set_dqinfo(struct super_block *sb, int type, struct qc_info *ii);
int dquot_get_dqblk(struct super_block *sb, struct kqid id,
		struct qc_dqblk *di);
int dquot_get_next_dqblk(struct super_block *sb, struct kqid *id,
		struct qc_dqblk *di);
int dquot_set_dqblk(struct super_block *sb, struct kqid id,
		struct qc_dqblk *di);

int __dquot_transfer(struct inode *inode, struct dquot **transfer_to);
int dquot_transfer(struct inode *inode, struct iattr *iattr);

static inline struct mem_dqinfo *sb_dqinfo(struct super_block *sb, int type)
{
	return sb_dqopt(sb)->info + type;
}

/*
 * Functions for checking status of quota
 */

static inline bool sb_has_quota_usage_enabled(struct super_block *sb, int type)
{
	return sb_dqopt(sb)->flags &
				dquot_state_flag(DQUOT_USAGE_ENABLED, type);
}

static inline bool sb_has_quota_limits_enabled(struct super_block *sb, int type)
{
	return sb_dqopt(sb)->flags &
				dquot_state_flag(DQUOT_LIMITS_ENABLED, type);
}

static inline bool sb_has_quota_suspended(struct super_block *sb, int type)
{
	return sb_dqopt(sb)->flags &
				dquot_state_flag(DQUOT_SUSPENDED, type);
}

static inline unsigned sb_any_quota_suspended(struct super_block *sb)
{
	return dquot_state_types(sb_dqopt(sb)->flags, DQUOT_SUSPENDED);
}

/* Does kernel know about any quota information for given sb + type? */
static inline bool sb_has_quota_loaded(struct super_block *sb, int type)
{
	/* Currently if anything is on, then quota usage is on as well */
	return sb_has_quota_usage_enabled(sb, type);
}

static inline unsigned sb_any_quota_loaded(struct super_block *sb)
{
	return dquot_state_types(sb_dqopt(sb)->flags, DQUOT_USAGE_ENABLED);
}

static inline bool sb_has_quota_active(struct super_block *sb, int type)
{
	return sb_has_quota_loaded(sb, type) &&
	       !sb_has_quota_suspended(sb, type);
}

/*
 * Operations supported for diskquotas.
 */
extern const struct dquot_operations dquot_operations;
extern const struct quotactl_ops dquot_quotactl_sysfile_ops;

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

static inline int dquot_initialize(struct inode *inode)
{
	return 0;
}

static inline bool dquot_initialize_needed(struct inode *inode)
{
	return false;
}

static inline void dquot_drop(struct inode *inode)
{
}

static inline int dquot_alloc_inode(struct inode *inode)
{
	return 0;
}

static inline void dquot_free_inode(struct inode *inode)
{
}

static inline int dquot_transfer(struct inode *inode, struct iattr *iattr)
{
	return 0;
}

static inline int __dquot_alloc_space(struct inode *inode, qsize_t number,
		int flags)
{
	if (!(flags & DQUOT_SPACE_RESERVE))
		inode_add_bytes(inode, number);
	return 0;
}

static inline void __dquot_free_space(struct inode *inode, qsize_t number,
		int flags)
{
	if (!(flags & DQUOT_SPACE_RESERVE))
		inode_sub_bytes(inode, number);
}

static inline int dquot_claim_space_nodirty(struct inode *inode, qsize_t number)
{
	inode_add_bytes(inode, number);
	return 0;
}

static inline int dquot_reclaim_space_nodirty(struct inode *inode,
					      qsize_t number)
{
	inode_sub_bytes(inode, number);
	return 0;
}

static inline int dquot_disable(struct super_block *sb, int type,
		unsigned int flags)
{
	return 0;
}

static inline int dquot_suspend(struct super_block *sb, int type)
{
	return 0;
}

static inline int dquot_resume(struct super_block *sb, int type)
{
	return 0;
}

#define dquot_file_open		generic_file_open

static inline int dquot_writeback_dquots(struct super_block *sb, int type)
{
	return 0;
}

#endif /* CONFIG_QUOTA */

static inline int dquot_alloc_space_nodirty(struct inode *inode, qsize_t nr)
{
	return __dquot_alloc_space(inode, nr, DQUOT_SPACE_WARN);
}

static inline void dquot_alloc_space_nofail(struct inode *inode, qsize_t nr)
{
	__dquot_alloc_space(inode, nr, DQUOT_SPACE_WARN|DQUOT_SPACE_NOFAIL);
	mark_inode_dirty_sync(inode);
}

static inline int dquot_alloc_space(struct inode *inode, qsize_t nr)
{
	int ret;

	ret = dquot_alloc_space_nodirty(inode, nr);
	if (!ret) {
		/*
		 * Mark inode fully dirty. Since we are allocating blocks, inode
		 * would become fully dirty soon anyway and it reportedly
		 * reduces lock contention.
		 */
		mark_inode_dirty(inode);
	}
	return ret;
}

static inline int dquot_alloc_block_nodirty(struct inode *inode, qsize_t nr)
{
	return dquot_alloc_space_nodirty(inode, nr << inode->i_blkbits);
}

static inline void dquot_alloc_block_nofail(struct inode *inode, qsize_t nr)
{
	dquot_alloc_space_nofail(inode, nr << inode->i_blkbits);
}

static inline int dquot_alloc_block(struct inode *inode, qsize_t nr)
{
	return dquot_alloc_space(inode, nr << inode->i_blkbits);
}

static inline int dquot_prealloc_block_nodirty(struct inode *inode, qsize_t nr)
{
	return __dquot_alloc_space(inode, nr << inode->i_blkbits, 0);
}

static inline int dquot_prealloc_block(struct inode *inode, qsize_t nr)
{
	int ret;

	ret = dquot_prealloc_block_nodirty(inode, nr);
	if (!ret)
		mark_inode_dirty_sync(inode);
	return ret;
}

static inline int dquot_reserve_block(struct inode *inode, qsize_t nr)
{
	return __dquot_alloc_space(inode, nr << inode->i_blkbits,
				DQUOT_SPACE_WARN|DQUOT_SPACE_RESERVE);
}

static inline int dquot_claim_block(struct inode *inode, qsize_t nr)
{
	int ret;

	ret = dquot_claim_space_nodirty(inode, nr << inode->i_blkbits);
	if (!ret)
		mark_inode_dirty_sync(inode);
	return ret;
}

static inline void dquot_reclaim_block(struct inode *inode, qsize_t nr)
{
	dquot_reclaim_space_nodirty(inode, nr << inode->i_blkbits);
	mark_inode_dirty_sync(inode);
}

static inline void dquot_free_space_nodirty(struct inode *inode, qsize_t nr)
{
	__dquot_free_space(inode, nr, 0);
}

static inline void dquot_free_space(struct inode *inode, qsize_t nr)
{
	dquot_free_space_nodirty(inode, nr);
	mark_inode_dirty_sync(inode);
}

static inline void dquot_free_block_nodirty(struct inode *inode, qsize_t nr)
{
	dquot_free_space_nodirty(inode, nr << inode->i_blkbits);
}

static inline void dquot_free_block(struct inode *inode, qsize_t nr)
{
	dquot_free_space(inode, nr << inode->i_blkbits);
}

static inline void dquot_release_reservation_block(struct inode *inode,
		qsize_t nr)
{
	__dquot_free_space(inode, nr << inode->i_blkbits, DQUOT_SPACE_RESERVE);
}

unsigned int qtype_enforce_flag(int type);

#endif /* _LINUX_QUOTAOPS_ */
