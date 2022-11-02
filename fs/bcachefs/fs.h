/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _BCACHEFS_FS_H
#define _BCACHEFS_FS_H

#include "inode.h"
#include "opts.h"
#include "str_hash.h"
#include "quota_types.h"
#include "two_state_shared_lock.h"

#include <linux/seqlock.h>
#include <linux/stat.h>

struct bch_inode_info {
	struct inode		v;
	unsigned long		ei_flags;

	struct mutex		ei_update_lock;
	u64			ei_quota_reserved;
	unsigned long		ei_last_dirtied;
	two_state_lock_t	ei_pagecache_lock;

	struct mutex		ei_quota_lock;
	struct bch_qid		ei_qid;

	u32			ei_subvol;

	/*
	 * When we've been doing nocow writes we'll need to issue flushes to the
	 * underlying block devices
	 *
	 * XXX: a device may have had a flush issued by some other codepath. It
	 * would be better to keep for each device a sequence number that's
	 * incremented when we isusue a cache flush, and track here the sequence
	 * number that needs flushing.
	 */
	struct bch_devs_mask	ei_devs_need_flush;

	/* copy of inode in btree: */
	struct bch_inode_unpacked ei_inode;
};

#define bch2_pagecache_add_put(i)	bch2_two_state_unlock(&i->ei_pagecache_lock, 0)
#define bch2_pagecache_add_tryget(i)	bch2_two_state_trylock(&i->ei_pagecache_lock, 0)
#define bch2_pagecache_add_get(i)	bch2_two_state_lock(&i->ei_pagecache_lock, 0)

#define bch2_pagecache_block_put(i)	bch2_two_state_unlock(&i->ei_pagecache_lock, 1)
#define bch2_pagecache_block_get(i)	bch2_two_state_lock(&i->ei_pagecache_lock, 1)

static inline subvol_inum inode_inum(struct bch_inode_info *inode)
{
	return (subvol_inum) {
		.subvol	= inode->ei_subvol,
		.inum	= inode->ei_inode.bi_inum,
	};
}

/*
 * Set if we've gotten a btree error for this inode, and thus the vfs inode and
 * btree inode may be inconsistent:
 */
#define EI_INODE_ERROR			0

/*
 * Set in the inode is in a snapshot subvolume - we don't do quota accounting in
 * those:
 */
#define EI_INODE_SNAPSHOT		1

#define to_bch_ei(_inode)					\
	container_of_or_null(_inode, struct bch_inode_info, v)

static inline int ptrcmp(void *l, void *r)
{
	return cmp_int(l, r);
}

enum bch_inode_lock_op {
	INODE_LOCK		= (1U << 0),
	INODE_PAGECACHE_BLOCK	= (1U << 1),
	INODE_UPDATE_LOCK	= (1U << 2),
};

#define bch2_lock_inodes(_locks, ...)					\
do {									\
	struct bch_inode_info *a[] = { NULL, __VA_ARGS__ };		\
	unsigned i;							\
									\
	bubble_sort(&a[1], ARRAY_SIZE(a) - 1, ptrcmp);			\
									\
	for (i = 1; i < ARRAY_SIZE(a); i++)				\
		if (a[i] != a[i - 1]) {					\
			if ((_locks) & INODE_LOCK)			\
				down_write_nested(&a[i]->v.i_rwsem, i);	\
			if ((_locks) & INODE_PAGECACHE_BLOCK)		\
				bch2_pagecache_block_get(a[i]);\
			if ((_locks) & INODE_UPDATE_LOCK)			\
				mutex_lock_nested(&a[i]->ei_update_lock, i);\
		}							\
} while (0)

#define bch2_unlock_inodes(_locks, ...)					\
do {									\
	struct bch_inode_info *a[] = { NULL, __VA_ARGS__ };		\
	unsigned i;							\
									\
	bubble_sort(&a[1], ARRAY_SIZE(a) - 1, ptrcmp);			\
									\
	for (i = 1; i < ARRAY_SIZE(a); i++)				\
		if (a[i] != a[i - 1]) {					\
			if ((_locks) & INODE_LOCK)			\
				up_write(&a[i]->v.i_rwsem);		\
			if ((_locks) & INODE_PAGECACHE_BLOCK)		\
				bch2_pagecache_block_put(a[i]);\
			if ((_locks) & INODE_UPDATE_LOCK)			\
				mutex_unlock(&a[i]->ei_update_lock);	\
		}							\
} while (0)

static inline struct bch_inode_info *file_bch_inode(struct file *file)
{
	return to_bch_ei(file_inode(file));
}

static inline bool inode_attr_changing(struct bch_inode_info *dir,
				struct bch_inode_info *inode,
				enum inode_opt_id id)
{
	return !(inode->ei_inode.bi_fields_set & (1 << id)) &&
		bch2_inode_opt_get(&dir->ei_inode, id) !=
		bch2_inode_opt_get(&inode->ei_inode, id);
}

static inline bool inode_attrs_changing(struct bch_inode_info *dir,
				 struct bch_inode_info *inode)
{
	unsigned id;

	for (id = 0; id < Inode_opt_nr; id++)
		if (inode_attr_changing(dir, inode, id))
			return true;

	return false;
}

struct bch_inode_unpacked;

#ifndef NO_BCACHEFS_FS

struct bch_inode_info *
__bch2_create(struct mnt_idmap *, struct bch_inode_info *,
	      struct dentry *, umode_t, dev_t, subvol_inum, unsigned);

int bch2_fs_quota_transfer(struct bch_fs *,
			   struct bch_inode_info *,
			   struct bch_qid,
			   unsigned,
			   enum quota_acct_mode);

static inline int bch2_set_projid(struct bch_fs *c,
				  struct bch_inode_info *inode,
				  u32 projid)
{
	struct bch_qid qid = inode->ei_qid;

	qid.q[QTYP_PRJ] = projid;

	return bch2_fs_quota_transfer(c, inode, qid,
				      1 << QTYP_PRJ,
				      KEY_TYPE_QUOTA_PREALLOC);
}

struct inode *bch2_vfs_inode_get(struct bch_fs *, subvol_inum);

/* returns 0 if we want to do the update, or error is passed up */
typedef int (*inode_set_fn)(struct bch_inode_info *,
			    struct bch_inode_unpacked *, void *);

void bch2_inode_update_after_write(struct btree_trans *,
				   struct bch_inode_info *,
				   struct bch_inode_unpacked *,
				   unsigned);
int __must_check bch2_write_inode(struct bch_fs *, struct bch_inode_info *,
				  inode_set_fn, void *, unsigned);

int bch2_setattr_nonsize(struct mnt_idmap *,
			 struct bch_inode_info *,
			 struct iattr *);
int __bch2_unlink(struct inode *, struct dentry *, bool);

void bch2_evict_subvolume_inodes(struct bch_fs *, snapshot_id_list *);

void bch2_vfs_exit(void);
int bch2_vfs_init(void);

#else

static inline void bch2_evict_subvolume_inodes(struct bch_fs *c,
					       snapshot_id_list *s) {}
static inline void bch2_vfs_exit(void) {}
static inline int bch2_vfs_init(void) { return 0; }

#endif /* NO_BCACHEFS_FS */

#endif /* _BCACHEFS_FS_H */
