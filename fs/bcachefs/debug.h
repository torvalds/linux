/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _BCACHEFS_DEBUG_H
#define _BCACHEFS_DEBUG_H

#include "bcachefs.h"

struct bio;
struct btree;
struct bch_fs;

#define BCH_DEBUG_PARAM(name, description) extern bool bch2_##name;
BCH_DEBUG_PARAMS()
#undef BCH_DEBUG_PARAM

#define BCH_DEBUG_PARAM(name, description)				\
	static inline bool name(struct bch_fs *c)			\
	{ return bch2_##name || c->name;	}
BCH_DEBUG_PARAMS_ALWAYS()
#undef BCH_DEBUG_PARAM

#ifdef CONFIG_BCACHEFS_DEBUG

#define BCH_DEBUG_PARAM(name, description)				\
	static inline bool name(struct bch_fs *c)			\
	{ return bch2_##name || c->name;	}
BCH_DEBUG_PARAMS_DEBUG()
#undef BCH_DEBUG_PARAM

void __bch2_btree_verify(struct bch_fs *, struct btree *);

#define bypass_torture_test(d)		((d)->bypass_torture_test)

#else /* DEBUG */

#define BCH_DEBUG_PARAM(name, description)				\
	static inline bool name(struct bch_fs *c) { return false; }
BCH_DEBUG_PARAMS_DEBUG()
#undef BCH_DEBUG_PARAM

static inline void __bch2_btree_verify(struct bch_fs *c, struct btree *b) {}

#define bypass_torture_test(d)		0

#endif

static inline void bch2_btree_verify(struct bch_fs *c, struct btree *b)
{
	if (verify_btree_ondisk(c))
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
