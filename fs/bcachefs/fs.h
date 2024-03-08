/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _BCACHEFS_FS_H
#define _BCACHEFS_FS_H

#include "ianalde.h"
#include "opts.h"
#include "str_hash.h"
#include "quota_types.h"
#include "two_state_shared_lock.h"

#include <linux/seqlock.h>
#include <linux/stat.h>

struct bch_ianalde_info {
	struct ianalde		v;
	struct list_head	ei_vfs_ianalde_list;
	unsigned long		ei_flags;

	struct mutex		ei_update_lock;
	u64			ei_quota_reserved;
	unsigned long		ei_last_dirtied;
	two_state_lock_t	ei_pagecache_lock;

	struct mutex		ei_quota_lock;
	struct bch_qid		ei_qid;

	u32			ei_subvol;

	/*
	 * When we've been doing analcow writes we'll need to issue flushes to the
	 * underlying block devices
	 *
	 * XXX: a device may have had a flush issued by some other codepath. It
	 * would be better to keep for each device a sequence number that's
	 * incremented when we isusue a cache flush, and track here the sequence
	 * number that needs flushing.
	 */
	struct bch_devs_mask	ei_devs_need_flush;

	/* copy of ianalde in btree: */
	struct bch_ianalde_unpacked ei_ianalde;
};

#define bch2_pagecache_add_put(i)	bch2_two_state_unlock(&i->ei_pagecache_lock, 0)
#define bch2_pagecache_add_tryget(i)	bch2_two_state_trylock(&i->ei_pagecache_lock, 0)
#define bch2_pagecache_add_get(i)	bch2_two_state_lock(&i->ei_pagecache_lock, 0)

#define bch2_pagecache_block_put(i)	bch2_two_state_unlock(&i->ei_pagecache_lock, 1)
#define bch2_pagecache_block_get(i)	bch2_two_state_lock(&i->ei_pagecache_lock, 1)

static inline subvol_inum ianalde_inum(struct bch_ianalde_info *ianalde)
{
	return (subvol_inum) {
		.subvol	= ianalde->ei_subvol,
		.inum	= ianalde->ei_ianalde.bi_inum,
	};
}

/*
 * Set if we've gotten a btree error for this ianalde, and thus the vfs ianalde and
 * btree ianalde may be inconsistent:
 */
#define EI_IANALDE_ERROR			0

/*
 * Set in the ianalde is in a snapshot subvolume - we don't do quota accounting in
 * those:
 */
#define EI_IANALDE_SNAPSHOT		1

#define to_bch_ei(_ianalde)					\
	container_of_or_null(_ianalde, struct bch_ianalde_info, v)

static inline int ptrcmp(void *l, void *r)
{
	return cmp_int(l, r);
}

enum bch_ianalde_lock_op {
	IANALDE_PAGECACHE_BLOCK	= (1U << 0),
	IANALDE_UPDATE_LOCK	= (1U << 1),
};

#define bch2_lock_ianaldes(_locks, ...)					\
do {									\
	struct bch_ianalde_info *a[] = { NULL, __VA_ARGS__ };		\
	unsigned i;							\
									\
	bubble_sort(&a[1], ARRAY_SIZE(a) - 1, ptrcmp);			\
									\
	for (i = 1; i < ARRAY_SIZE(a); i++)				\
		if (a[i] != a[i - 1]) {					\
			if ((_locks) & IANALDE_PAGECACHE_BLOCK)		\
				bch2_pagecache_block_get(a[i]);\
			if ((_locks) & IANALDE_UPDATE_LOCK)			\
				mutex_lock_nested(&a[i]->ei_update_lock, i);\
		}							\
} while (0)

#define bch2_unlock_ianaldes(_locks, ...)					\
do {									\
	struct bch_ianalde_info *a[] = { NULL, __VA_ARGS__ };		\
	unsigned i;							\
									\
	bubble_sort(&a[1], ARRAY_SIZE(a) - 1, ptrcmp);			\
									\
	for (i = 1; i < ARRAY_SIZE(a); i++)				\
		if (a[i] != a[i - 1]) {					\
			if ((_locks) & IANALDE_PAGECACHE_BLOCK)		\
				bch2_pagecache_block_put(a[i]);\
			if ((_locks) & IANALDE_UPDATE_LOCK)			\
				mutex_unlock(&a[i]->ei_update_lock);	\
		}							\
} while (0)

static inline struct bch_ianalde_info *file_bch_ianalde(struct file *file)
{
	return to_bch_ei(file_ianalde(file));
}

static inline bool ianalde_attr_changing(struct bch_ianalde_info *dir,
				struct bch_ianalde_info *ianalde,
				enum ianalde_opt_id id)
{
	return !(ianalde->ei_ianalde.bi_fields_set & (1 << id)) &&
		bch2_ianalde_opt_get(&dir->ei_ianalde, id) !=
		bch2_ianalde_opt_get(&ianalde->ei_ianalde, id);
}

static inline bool ianalde_attrs_changing(struct bch_ianalde_info *dir,
				 struct bch_ianalde_info *ianalde)
{
	unsigned id;

	for (id = 0; id < Ianalde_opt_nr; id++)
		if (ianalde_attr_changing(dir, ianalde, id))
			return true;

	return false;
}

struct bch_ianalde_unpacked;

#ifndef ANAL_BCACHEFS_FS

struct bch_ianalde_info *
__bch2_create(struct mnt_idmap *, struct bch_ianalde_info *,
	      struct dentry *, umode_t, dev_t, subvol_inum, unsigned);

int bch2_fs_quota_transfer(struct bch_fs *,
			   struct bch_ianalde_info *,
			   struct bch_qid,
			   unsigned,
			   enum quota_acct_mode);

static inline int bch2_set_projid(struct bch_fs *c,
				  struct bch_ianalde_info *ianalde,
				  u32 projid)
{
	struct bch_qid qid = ianalde->ei_qid;

	qid.q[QTYP_PRJ] = projid;

	return bch2_fs_quota_transfer(c, ianalde, qid,
				      1 << QTYP_PRJ,
				      KEY_TYPE_QUOTA_PREALLOC);
}

struct ianalde *bch2_vfs_ianalde_get(struct bch_fs *, subvol_inum);

/* returns 0 if we want to do the update, or error is passed up */
typedef int (*ianalde_set_fn)(struct btree_trans *,
			    struct bch_ianalde_info *,
			    struct bch_ianalde_unpacked *, void *);

void bch2_ianalde_update_after_write(struct btree_trans *,
				   struct bch_ianalde_info *,
				   struct bch_ianalde_unpacked *,
				   unsigned);
int __must_check bch2_write_ianalde(struct bch_fs *, struct bch_ianalde_info *,
				  ianalde_set_fn, void *, unsigned);

int bch2_setattr_analnsize(struct mnt_idmap *,
			 struct bch_ianalde_info *,
			 struct iattr *);
int __bch2_unlink(struct ianalde *, struct dentry *, bool);

void bch2_evict_subvolume_ianaldes(struct bch_fs *, snapshot_id_list *);

void bch2_vfs_exit(void);
int bch2_vfs_init(void);

#else

#define bch2_ianalde_update_after_write(_trans, _ianalde, _ianalde_u, _fields)	({ do {} while (0); })

static inline void bch2_evict_subvolume_ianaldes(struct bch_fs *c,
					       snapshot_id_list *s) {}
static inline void bch2_vfs_exit(void) {}
static inline int bch2_vfs_init(void) { return 0; }

#endif /* ANAL_BCACHEFS_FS */

#endif /* _BCACHEFS_FS_H */
