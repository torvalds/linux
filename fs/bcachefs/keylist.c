// SPDX-License-Identifier: GPL-2.0

#include "bcachefs.h"
#include "bkey.h"
#include "keylist.h"

int bch2_keylist_realloc(struct keylist *l, u64 *inline_u64s,
			size_t nr_inline_u64s, size_t new_u64s)
{
	size_t oldsize = bch2_keylist_u64s(l);
	size_t newsize = oldsize + new_u64s;
	u64 *old_buf = l->keys_p == inline_u64s ? NULL : l->keys_p;
	u64 *new_keys;

	newsize = roundup_pow_of_two(newsize);

	if (newsize <= nr_inline_u64s ||
	    (old_buf && roundup_pow_of_two(oldsize) == newsize))
		return 0;

	new_keys = krealloc(old_buf, sizeof(u64) * newsize, GFP_NOFS);
	if (!new_keys)
		return -ENOMEM;

	if (!old_buf)
		memcpy_u64s(new_keys, inline_u64s, oldsize);

	l->keys_p = new_keys;
	l->top_p = new_keys + oldsize;

	return 0;
}

void bch2_keylist_pop_front(struct keylist *l)
{
	l->top_p -= bch2_keylist_front(l)->k.u64s;

	memmove_u64s_down(l->keys,
			  bkey_next(l->keys),
			  bch2_keylist_u64s(l));
}

#ifdef CONFIG_BCACHEFS_DEBUG
void bch2_verify_keylist_sorted(struct keylist *l)
{
	for_each_keylist_key(l, k)
		BUG_ON(bkey_next(k) != l->top &&
		       bpos_ge(k->k.p, bkey_next(k)->k.p));
}
#endif
