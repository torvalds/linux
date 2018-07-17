/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _BCACHEFS_FS_H
#define _BCACHEFS_FS_H

#include "inode.h"
#include "opts.h"
#include "str_hash.h"
#include "quota_types.h"

#include <linux/seqlock.h>
#include <linux/stat.h>

/*
 * Two-state lock - can be taken for add or block - both states are shared,
 * like read side of rwsem, but conflict with other state:
 */
struct pagecache_lock {
	atomic_long_t		v;
	wait_queue_head_t	wait;
};

static inline void pagecache_lock_init(struct pagecache_lock *lock)
{
	atomic_long_set(&lock->v, 0);
	init_waitqueue_head(&lock->wait);
}

void bch2_pagecache_add_put(struct pagecache_lock *);
void bch2_pagecache_add_get(struct pagecache_lock *);
void bch2_pagecache_block_put(struct pagecache_lock *);
void bch2_pagecache_block_get(struct pagecache_lock *);

struct bch_inode_info {
	struct inode		v;

	struct mutex		ei_update_lock;
	u64			ei_journal_seq;
	u64			ei_quota_reserved;
	unsigned long		ei_last_dirtied;
	struct pagecache_lock	ei_pagecache_lock;

	struct mutex		ei_quota_lock;
	struct bch_qid		ei_qid;

	struct bch_hash_info	ei_str_hash;

	/* copy of inode in btree: */
	struct bch_inode_unpacked ei_inode;
};

#define to_bch_ei(_inode)					\
	container_of_or_null(_inode, struct bch_inode_info, v)

static inline struct bch_inode_info *file_bch_inode(struct file *file)
{
	return to_bch_ei(file_inode(file));
}

static inline u8 mode_to_type(umode_t mode)
{
	return (mode >> 12) & 15;
}

static inline unsigned nlink_bias(umode_t mode)
{
	return S_ISDIR(mode) ? 2 : 1;
}

struct bch_inode_unpacked;

#ifndef NO_BCACHEFS_FS

/* returns 0 if we want to do the update, or error is passed up */
typedef int (*inode_set_fn)(struct bch_inode_info *,
			    struct bch_inode_unpacked *, void *);

void bch2_inode_update_after_write(struct bch_fs *,
				   struct bch_inode_info *,
				   struct bch_inode_unpacked *,
				   unsigned);
int __must_check bch2_write_inode_trans(struct btree_trans *,
				struct bch_inode_info *,
				struct bch_inode_unpacked *,
				inode_set_fn, void *);
int __must_check bch2_write_inode(struct bch_fs *, struct bch_inode_info *,
				  inode_set_fn, void *, unsigned);

void bch2_vfs_exit(void);
int bch2_vfs_init(void);

#else

static inline void bch2_vfs_exit(void) {}
static inline int bch2_vfs_init(void) { return 0; }

#endif /* NO_BCACHEFS_FS */

#endif /* _BCACHEFS_FS_H */
