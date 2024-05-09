/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _BCACHEFS_KEYLIST_H
#define _BCACHEFS_KEYLIST_H

#include "keylist_types.h"

int bch2_keylist_realloc(struct keylist *, u64 *, size_t, size_t);
void bch2_keylist_pop_front(struct keylist *);

static inline void bch2_keylist_init(struct keylist *l, u64 *inline_keys)
{
	l->top_p = l->keys_p = inline_keys;
}

static inline void bch2_keylist_free(struct keylist *l, u64 *inline_keys)
{
	if (l->keys_p != inline_keys)
		kfree(l->keys_p);
}

static inline void bch2_keylist_push(struct keylist *l)
{
	l->top = bkey_next(l->top);
}

static inline void bch2_keylist_add(struct keylist *l, const struct bkey_i *k)
{
	bkey_copy(l->top, k);
	bch2_keylist_push(l);
}

static inline bool bch2_keylist_empty(struct keylist *l)
{
	return l->top == l->keys;
}

static inline size_t bch2_keylist_u64s(struct keylist *l)
{
	return l->top_p - l->keys_p;
}

static inline size_t bch2_keylist_bytes(struct keylist *l)
{
	return bch2_keylist_u64s(l) * sizeof(u64);
}

static inline struct bkey_i *bch2_keylist_front(struct keylist *l)
{
	return l->keys;
}

#define for_each_keylist_key(_keylist, _k)			\
	for (_k = (_keylist)->keys;				\
	     _k != (_keylist)->top;				\
	     _k = bkey_next(_k))

static inline u64 keylist_sectors(struct keylist *keys)
{
	struct bkey_i *k;
	u64 ret = 0;

	for_each_keylist_key(keys, k)
		ret += k->k.size;

	return ret;
}

#ifdef CONFIG_BCACHEFS_DEBUG
void bch2_verify_keylist_sorted(struct keylist *);
#else
static inline void bch2_verify_keylist_sorted(struct keylist *l) {}
#endif

#endif /* _BCACHEFS_KEYLIST_H */
