/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _BCACHEFS_DEBUG_H
#define _BCACHEFS_DEBUG_H

#include "bcachefs.h"

struct bio;
struct btree;
struct bch_fs;

void __bch2_btree_verify(struct bch_fs *, struct btree *);
void bch2_btree_node_ondisk_to_text(struct printbuf *, struct bch_fs *,
				    const struct btree *);

static inline void bch2_btree_verify(struct bch_fs *c, struct btree *b)
{
	if (static_branch_unlikely(&bch2_verify_btree_ondisk))
		__bch2_btree_verify(c, b);
}

#ifdef CONFIG_DEBUG_FS
struct dump_iter {
	struct bch_fs		*c;
	struct async_obj_list	*list;
	enum btree_id		id;
	struct bpos		from;
	struct bpos		prev_node;
	u64			iter;

	struct printbuf		buf;

	char __user		*ubuf;	/* destination user buffer */
	size_t			size;	/* size of requested read */
	ssize_t			ret;	/* bytes read so far */
};

ssize_t bch2_debugfs_flush_buf(struct dump_iter *);
int bch2_dump_release(struct inode *, struct file *);

void bch2_fs_debug_exit(struct bch_fs *);
void bch2_fs_debug_init(struct bch_fs *);
#else
static inline void bch2_fs_debug_exit(struct bch_fs *c) {}
static inline void bch2_fs_debug_init(struct bch_fs *c) {}
#endif

void bch2_debug_exit(void);
int bch2_debug_init(void);

#endif /* _BCACHEFS_DEBUG_H */
