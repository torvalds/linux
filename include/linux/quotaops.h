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
void inode_add_rsv_space(struct inode *inode, qsize_t number);
void inode_claim_rsv_space(struct inode *inode, qsize_t number);
void inode_sub_rsv_space(struct inode *inode, qsize_t number);

void dquot_initialize(struct inode *inode);
void dquot_drop(struct inode *inode);
struct dquot *dqget(struct super_block *sb, unsigned int id, int type);
void dqput(struct dquot *dquot);
int dquot_scan_active(struct super_block *sb,
		      int (*fn)(struct dquot *dquot, unsigned long priv),
		      unsigned long priv);
struct dquot *dquot_alloc(struct super_block *sb, int type);
void dquot_destroy(struct dquot *dquot);

int __dquot_alloc_space(struct inode *inode, qsize_t number,
		int warn, int reserve);
void __dquot_free_space(struct inode *inode, qsize_t number, int reserve);

int dquot_alloc_inode(const struct inode *inode);

int dquot_claim_space_nodirty(struct inode *inode, qsize_t number);
void dquot_free_inode(const struct inode *inode);

int dquot_commit(struct dquot *dquot);
int dquot_acquire(struct dquot *dquot);
int dquot_release(struct dquot *dquot);
int dquot_commit_info(struct super_block *sb, int type);
int dquot_mark_dquot_dirty(struct dquot *dquot);

int dquot_file_open(struct inode *inode, struct file *file);

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
int vfs_quota_sync(struct super_block *sb, int type, int wait);
int vfs_get_dqinfo(struct super_block *sb, int type, struct if_dqinfo *ii);
int vfs_set_dqinfo(struct super_block *sb, int type, struct if_dqinfo *ii);
int vfs_get_dqblk(struct super_block *sb, int type, qid_t id, struct if_dqblk *di);
int vfs_set_dqblk(struct super_block *sb, int type, qid_t id, struct if_dqblk *di);

int dquot_transfer(struct inode *inode, struct iattr *iattr);
int vfs_dq_quota_on_remount(struct super_block *sb);

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
	unsigned type, tmsk = 0;
	for (type = 0; type < MAXQUOTAS; type++)
		tmsk |= sb_has_quota_suspended(sb, type) << type;
	return tmsk;
}

/* Does kernel know about any quota information for given sb + type? */
static inline bool sb_has_quota_loaded(struct super_block *sb, int type)
{
	/* Currently if anything is on, then quota usage is on as well */
	return sb_has_quota_usage_enabled(sb, type);
}

static inline unsigned sb_any_quota_loaded(struct super_block *sb)
{
	unsigned type, tmsk = 0;
	for (type = 0; type < MAXQUOTAS; type++)
		tmsk |= sb_has_quota_loaded(sb, type) << type;
	return	tmsk;
}

static inline bool sb_has_quota_active(struct super_block *sb, int type)
{
	return sb_has_quota_loaded(sb, type) &&
	       !sb_has_quota_suspended(sb, type);
}

static inline unsigned sb_any_quota_active(struct super_block *sb)
{
	return sb_any_quota_loaded(sb) & ~sb_any_quota_suspended(sb);
}

/*
 * Operations supported for diskquotas.
 */
extern const struct dquot_operations dquot_operations;
extern const struct quotactl_ops vfs_quotactl_ops;

#define sb_dquot_ops (&dquot_operations)
#define sb_quotactl_ops (&vfs_quotactl_ops)

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

static inline void dquot_initialize(struct inode *inode)
{
}

static inline void dquot_drop(struct inode *inode)
{
}

static inline int dquot_alloc_inode(const struct inode *inode)
{
	return 0;
}

static inline void dquot_free_inode(const struct inode *inode)
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

static inline int dquot_transfer(struct inode *inode, struct iattr *iattr)
{
	return 0;
}

static inline int __dquot_alloc_space(struct inode *inode, qsize_t number,
		int warn, int reserve)
{
	if (!reserve)
		inode_add_bytes(inode, number);
	return 0;
}

static inline void __dquot_free_space(struct inode *inode, qsize_t number,
		int reserve)
{
	if (!reserve)
		inode_sub_bytes(inode, number);
}

static inline int dquot_claim_space_nodirty(struct inode *inode, qsize_t number)
{
	inode_add_bytes(inode, number);
	return 0;
}

#define dquot_file_open		generic_file_open

#endif /* CONFIG_QUOTA */

static inline int dquot_alloc_space_nodirty(struct inode *inode, qsize_t nr)
{
	return __dquot_alloc_space(inode, nr, 1, 0);
}

static inline int dquot_alloc_space(struct inode *inode, qsize_t nr)
{
	int ret;

	ret = dquot_alloc_space_nodirty(inode, nr);
	if (!ret)
		mark_inode_dirty(inode);
	return ret;
}

static inline int dquot_alloc_block_nodirty(struct inode *inode, qsize_t nr)
{
	return dquot_alloc_space_nodirty(inode, nr << inode->i_blkbits);
}

static inline int dquot_alloc_block(struct inode *inode, qsize_t nr)
{
	return dquot_alloc_space(inode, nr << inode->i_blkbits);
}

static inline int dquot_prealloc_block_nodirty(struct inode *inode, qsize_t nr)
{
	return __dquot_alloc_space(inode, nr << inode->i_blkbits, 0, 0);
}

static inline int dquot_prealloc_block(struct inode *inode, qsize_t nr)
{
	int ret;

	ret = dquot_prealloc_block_nodirty(inode, nr);
	if (!ret)
		mark_inode_dirty(inode);
	return ret;
}

static inline int dquot_reserve_block(struct inode *inode, qsize_t nr)
{
	return __dquot_alloc_space(inode, nr << inode->i_blkbits, 1, 1);
}

static inline int dquot_claim_block(struct inode *inode, qsize_t nr)
{
	int ret;

	ret = dquot_claim_space_nodirty(inode, nr << inode->i_blkbits);
	if (!ret)
		mark_inode_dirty(inode);
	return ret;
}

static inline void dquot_free_space_nodirty(struct inode *inode, qsize_t nr)
{
	__dquot_free_space(inode, nr, 0);
}

static inline void dquot_free_space(struct inode *inode, qsize_t nr)
{
	dquot_free_space_nodirty(inode, nr);
	mark_inode_dirty(inode);
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
	__dquot_free_space(inode, nr << inode->i_blkbits, 1);
}

#endif /* _LINUX_QUOTAOPS_ */
