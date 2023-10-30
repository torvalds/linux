/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _BCACHEFS_BKEY_BUF_H
#define _BCACHEFS_BKEY_BUF_H

#include "bcachefs.h"
#include "bkey.h"

struct bkey_buf {
	struct bkey_i	*k;
	u64		onstack[12];
};

static inline void bch2_bkey_buf_realloc(struct bkey_buf *s,
					 struct bch_fs *c, unsigned u64s)
{
	if (s->k == (void *) s->onstack &&
	    u64s > ARRAY_SIZE(s->onstack)) {
		s->k = mempool_alloc(&c->large_bkey_pool, GFP_NOFS);
		memcpy(s->k, s->onstack, sizeof(s->onstack));
	}
}

static inline void bch2_bkey_buf_reassemble(struct bkey_buf *s,
					    struct bch_fs *c,
					    struct bkey_s_c k)
{
	bch2_bkey_buf_realloc(s, c, k.k->u64s);
	bkey_reassemble(s->k, k);
}

static inline void bch2_bkey_buf_copy(struct bkey_buf *s,
				      struct bch_fs *c,
				      struct bkey_i *src)
{
	bch2_bkey_buf_realloc(s, c, src->k.u64s);
	bkey_copy(s->k, src);
}

static inline void bch2_bkey_buf_unpack(struct bkey_buf *s,
					struct bch_fs *c,
					struct btree *b,
					struct bkey_packed *src)
{
	bch2_bkey_buf_realloc(s, c, BKEY_U64s +
			      bkeyp_val_u64s(&b->format, src));
	bch2_bkey_unpack(b, s->k, src);
}

static inline void bch2_bkey_buf_init(struct bkey_buf *s)
{
	s->k = (void *) s->onstack;
}

static inline void bch2_bkey_buf_exit(struct bkey_buf *s, struct bch_fs *c)
{
	if (s->k != (void *) s->onstack)
		mempool_free(s->k, &c->large_bkey_pool);
	s->k = NULL;
}

#endif /* _BCACHEFS_BKEY_BUF_H */
