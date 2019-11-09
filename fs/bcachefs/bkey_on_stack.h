/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _BCACHEFS_BKEY_ON_STACK_H
#define _BCACHEFS_BKEY_ON_STACK_H

#include "bcachefs.h"

struct bkey_on_stack {
	struct bkey_i	*k;
	u64		onstack[12];
};

static inline void bkey_on_stack_realloc(struct bkey_on_stack *s,
					 struct bch_fs *c, unsigned u64s)
{
	if (s->k == (void *) s->onstack &&
	    u64s > ARRAY_SIZE(s->onstack)) {
		s->k = mempool_alloc(&c->large_bkey_pool, GFP_NOFS);
		memcpy(s->k, s->onstack, sizeof(s->onstack));
	}
}

static inline void bkey_on_stack_init(struct bkey_on_stack *s)
{
	s->k = (void *) s->onstack;
}

static inline void bkey_on_stack_exit(struct bkey_on_stack *s,
				      struct bch_fs *c)
{
	if (s->k != (void *) s->onstack)
		mempool_free(s->k, &c->large_bkey_pool);
	s->k = NULL;
}

#endif /* _BCACHEFS_BKEY_ON_STACK_H */
