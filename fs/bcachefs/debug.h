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
	if (bch2_verify_btree_ondisk)
		__bch2_btree_verify(c, b);
}

#ifdef CONFIG_DEBUG_FS
void bch2_fs_debug_exit(struct bch_fs *);
void bch2_fs_debug_init(struct bch_fs *);
#else
static inline void bch2_fs_debug_exit(struct bch_fs *c) {}
static inline void bch2_fs_debug_init(struct bch_fs *c) {}
#endif

void bch2_debug_exit(void);
int bch2_debug_init(void);

#endif /* _BCACHEFS_DEBUG_H */
