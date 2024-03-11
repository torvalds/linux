/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _BCACHEFS_BSET_H
#define _BCACHEFS_BSET_H

#include <linux/kernel.h>
#include <linux/types.h>

#include "bcachefs.h"
#include "bkey.h"
#include "bkey_methods.h"
#include "btree_types.h"
#include "util.h" /* for time_stats */
#include "vstructs.h"

/*
 * BKEYS:
 *
 * A bkey contains a key, a size field, a variable number of pointers, and some
 * ancillary flag bits.
 *
 * We use two different functions for validating bkeys, bkey_invalid and
 * bkey_deleted().
 *
 * The one exception to the rule that ptr_invalid() filters out invalid keys is
 * that it also filters out keys of size 0 - these are keys that have been
 * completely overwritten. It'd be safe to delete these in memory while leaving
 * them on disk, just unnecessary work - so we filter them out when resorting
 * instead.
 *
 * We can't filter out stale keys when we're resorting, because garbage
 * collection needs to find them to ensure bucket gens don't wrap around -
 * unless we're rewriting the btree node those stale keys still exist on disk.
 *
 * We also implement functions here for removing some number of sectors from the
 * front or the back of a bkey - this is mainly used for fixing overlapping
 * extents, by removing the overlapping sectors from the older key.
 *
 * BSETS:
 *
 * A bset is an array of bkeys laid out contiguously in memory in sorted order,
 * along with a header. A btree node is made up of a number of these, written at
 * different times.
 *
 * There could be many of them on disk, but we never allow there to be more than
 * 4 in memory - we lazily resort as needed.
 *
 * We implement code here for creating and maintaining auxiliary search trees
 * (described below) for searching an individial bset, and on top of that we
 * implement a btree iterator.
 *
 * BTREE ITERATOR:
 *
 * Most of the code in bcache doesn't care about an individual bset - it needs
 * to search entire btree nodes and iterate over them in sorted order.
 *
 * The btree iterator code serves both functions; it iterates through the keys
 * in a btree node in sorted order, starting from either keys after a specific
 * point (if you pass it a search key) or the start of the btree node.
 *
 * AUXILIARY SEARCH TREES:
 *
 * Since keys are variable length, we can't use a binary search on a bset - we
 * wouldn't be able to find the start of the next key. But binary searches are
 * slow anyways, due to terrible cache behaviour; bcache originally used binary
 * searches and that code topped out at under 50k lookups/second.
 *
 * So we need to construct some sort of lookup table. Since we only insert keys
 * into the last (unwritten) set, most of the keys within a given btree node are
 * usually in sets that are mostly constant. We use two different types of
 * lookup tables to take advantage of this.
 *
 * Both lookup tables share in common that they don't index every key in the
 * set; they index one key every BSET_CACHELINE bytes, and then a linear search
 * is used for the rest.
 *
 * For sets that have been written to disk and are no longer being inserted
 * into, we construct a binary search tree in an array - traversing a binary
 * search tree in an array gives excellent locality of reference and is very
 * fast, since both children of any node are adjacent to each other in memory
 * (and their grandchildren, and great grandchildren...) - this means
 * prefetching can be used to great effect.
 *
 * It's quite useful performance wise to keep these nodes small - not just
 * because they're more likely to be in L2, but also because we can prefetch
 * more nodes on a single cacheline and thus prefetch more iterations in advance
 * when traversing this tree.
 *
 * Nodes in the auxiliary search tree must contain both a key to compare against
 * (we don't want to fetch the key from the set, that would defeat the purpose),
 * and a pointer to the key. We use a few tricks to compress both of these.
 *
 * To compress the pointer, we take advantage of the fact that one node in the
 * search tree corresponds to precisely BSET_CACHELINE bytes in the set. We have
 * a function (to_inorder()) that takes the index of a node in a binary tree and
 * returns what its index would be in an inorder traversal, so we only have to
 * store the low bits of the offset.
 *
 * The key is 84 bits (KEY_DEV + key->key, the offset on the device). To
 * compress that,  we take advantage of the fact that when we're traversing the
 * search tree at every iteration we know that both our search key and the key
 * we're looking for lie within some range - bounded by our previous
 * comparisons. (We special case the start of a search so that this is true even
 * at the root of the tree).
 *
 * So we know the key we're looking for is between a and b, and a and b don't
 * differ higher than bit 50, we don't need to check anything higher than bit
 * 50.
 *
 * We don't usually need the rest of the bits, either; we only need enough bits
 * to partition the key range we're currently checking.  Consider key n - the
 * key our auxiliary search tree node corresponds to, and key p, the key
 * immediately preceding n.  The lowest bit we need to store in the auxiliary
 * search tree is the highest bit that differs between n and p.
 *
 * Note that this could be bit 0 - we might sometimes need all 80 bits to do the
 * comparison. But we'd really like our nodes in the auxiliary search tree to be
 * of fixed size.
 *
 * The solution is to make them fixed size, and when we're constructing a node
 * check if p and n differed in the bits we needed them to. If they don't we
 * flag that node, and when doing lookups we fallback to comparing against the
 * real key. As long as this doesn't happen to often (and it seems to reliably
 * happen a bit less than 1% of the time), we win - even on failures, that key
 * is then more likely to be in cache than if we were doing binary searches all
 * the way, since we're touching so much less memory.
 *
 * The keys in the auxiliary search tree are stored in (software) floating
 * point, with an exponent and a mantissa. The exponent needs to be big enough
 * to address all the bits in the original key, but the number of bits in the
 * mantissa is somewhat arbitrary; more bits just gets us fewer failures.
 *
 * We need 7 bits for the exponent and 3 bits for the key's offset (since keys
 * are 8 byte aligned); using 22 bits for the mantissa means a node is 4 bytes.
 * We need one node per 128 bytes in the btree node, which means the auxiliary
 * search trees take up 3% as much memory as the btree itself.
 *
 * Constructing these auxiliary search trees is moderately expensive, and we
 * don't want to be constantly rebuilding the search tree for the last set
 * whenever we insert another key into it. For the unwritten set, we use a much
 * simpler lookup table - it's just a flat array, so index i in the lookup table
 * corresponds to the i range of BSET_CACHELINE bytes in the set. Indexing
 * within each byte range works the same as with the auxiliary search trees.
 *
 * These are much easier to keep up to date when we insert a key - we do it
 * somewhat lazily; when we shift a key up we usually just increment the pointer
 * to it, only when it would overflow do we go to the trouble of finding the
 * first key in that range of bytes again.
 */

enum bset_aux_tree_type {
	BSET_NO_AUX_TREE,
	BSET_RO_AUX_TREE,
	BSET_RW_AUX_TREE,
};

#define BSET_TREE_NR_TYPES	3

#define BSET_NO_AUX_TREE_VAL	(U16_MAX)
#define BSET_RW_AUX_TREE_VAL	(U16_MAX - 1)

static inline enum bset_aux_tree_type bset_aux_tree_type(const struct bset_tree *t)
{
	switch (t->extra) {
	case BSET_NO_AUX_TREE_VAL:
		EBUG_ON(t->size);
		return BSET_NO_AUX_TREE;
	case BSET_RW_AUX_TREE_VAL:
		EBUG_ON(!t->size);
		return BSET_RW_AUX_TREE;
	default:
		EBUG_ON(!t->size);
		return BSET_RO_AUX_TREE;
	}
}

/*
 * BSET_CACHELINE was originally intended to match the hardware cacheline size -
 * it used to be 64, but I realized the lookup code would touch slightly less
 * memory if it was 128.
 *
 * It definites the number of bytes (in struct bset) per struct bkey_float in
 * the auxiliar search tree - when we're done searching the bset_float tree we
 * have this many bytes left that we do a linear search over.
 *
 * Since (after level 5) every level of the bset_tree is on a new cacheline,
 * we're touching one fewer cacheline in the bset tree in exchange for one more
 * cacheline in the linear search - but the linear search might stop before it
 * gets to the second cacheline.
 */

#define BSET_CACHELINE		256

static inline size_t btree_keys_cachelines(const struct btree *b)
{
	return (1U << b->byte_order) / BSET_CACHELINE;
}

static inline size_t btree_aux_data_bytes(const struct btree *b)
{
	return btree_keys_cachelines(b) * 8;
}

static inline size_t btree_aux_data_u64s(const struct btree *b)
{
	return btree_aux_data_bytes(b) / sizeof(u64);
}

#define for_each_bset(_b, _t)						\
	for (_t = (_b)->set; _t < (_b)->set + (_b)->nsets; _t++)

#define bset_tree_for_each_key(_b, _t, _k)				\
	for (_k = btree_bkey_first(_b, _t);				\
	     _k != btree_bkey_last(_b, _t);				\
	     _k = bkey_p_next(_k))

static inline bool bset_has_ro_aux_tree(const struct bset_tree *t)
{
	return bset_aux_tree_type(t) == BSET_RO_AUX_TREE;
}

static inline bool bset_has_rw_aux_tree(struct bset_tree *t)
{
	return bset_aux_tree_type(t) == BSET_RW_AUX_TREE;
}

static inline void bch2_bset_set_no_aux_tree(struct btree *b,
					    struct bset_tree *t)
{
	BUG_ON(t < b->set);

	for (; t < b->set + ARRAY_SIZE(b->set); t++) {
		t->size = 0;
		t->extra = BSET_NO_AUX_TREE_VAL;
		t->aux_data_offset = U16_MAX;
	}
}

static inline void btree_node_set_format(struct btree *b,
					 struct bkey_format f)
{
	int len;

	b->format	= f;
	b->nr_key_bits	= bkey_format_key_bits(&f);

	len = bch2_compile_bkey_format(&b->format, b->aux_data);
	BUG_ON(len < 0 || len > U8_MAX);

	b->unpack_fn_len = len;

	bch2_bset_set_no_aux_tree(b, b->set);
}

static inline struct bset *bset_next_set(struct btree *b,
					 unsigned block_bytes)
{
	struct bset *i = btree_bset_last(b);

	EBUG_ON(!is_power_of_2(block_bytes));

	return ((void *) i) + round_up(vstruct_bytes(i), block_bytes);
}

void bch2_btree_keys_init(struct btree *);

void bch2_bset_init_first(struct btree *, struct bset *);
void bch2_bset_init_next(struct btree *, struct btree_node_entry *);
void bch2_bset_build_aux_tree(struct btree *, struct bset_tree *, bool);

void bch2_bset_insert(struct btree *, struct btree_node_iter *,
		     struct bkey_packed *, struct bkey_i *, unsigned);
void bch2_bset_delete(struct btree *, struct bkey_packed *, unsigned);

/* Bkey utility code */

/* packed or unpacked */
static inline int bkey_cmp_p_or_unp(const struct btree *b,
				    const struct bkey_packed *l,
				    const struct bkey_packed *r_packed,
				    const struct bpos *r)
{
	EBUG_ON(r_packed && !bkey_packed(r_packed));

	if (unlikely(!bkey_packed(l)))
		return bpos_cmp(packed_to_bkey_c(l)->p, *r);

	if (likely(r_packed))
		return __bch2_bkey_cmp_packed_format_checked(l, r_packed, b);

	return __bch2_bkey_cmp_left_packed_format_checked(b, l, r);
}

static inline struct bset_tree *
bch2_bkey_to_bset_inlined(struct btree *b, struct bkey_packed *k)
{
	unsigned offset = __btree_node_key_to_offset(b, k);
	struct bset_tree *t;

	for_each_bset(b, t)
		if (offset <= t->end_offset) {
			EBUG_ON(offset < btree_bkey_first_offset(t));
			return t;
		}

	BUG();
}

struct bset_tree *bch2_bkey_to_bset(struct btree *, struct bkey_packed *);

struct bkey_packed *bch2_bkey_prev_filter(struct btree *, struct bset_tree *,
					  struct bkey_packed *, unsigned);

static inline struct bkey_packed *
bch2_bkey_prev_all(struct btree *b, struct bset_tree *t, struct bkey_packed *k)
{
	return bch2_bkey_prev_filter(b, t, k, 0);
}

static inline struct bkey_packed *
bch2_bkey_prev(struct btree *b, struct bset_tree *t, struct bkey_packed *k)
{
	return bch2_bkey_prev_filter(b, t, k, 1);
}

/* Btree key iteration */

void bch2_btree_node_iter_push(struct btree_node_iter *, struct btree *,
			      const struct bkey_packed *,
			      const struct bkey_packed *);
void bch2_btree_node_iter_init(struct btree_node_iter *, struct btree *,
			       struct bpos *);
void bch2_btree_node_iter_init_from_start(struct btree_node_iter *,
					  struct btree *);
struct bkey_packed *bch2_btree_node_iter_bset_pos(struct btree_node_iter *,
						 struct btree *,
						 struct bset_tree *);

void bch2_btree_node_iter_sort(struct btree_node_iter *, struct btree *);
void bch2_btree_node_iter_set_drop(struct btree_node_iter *,
				   struct btree_node_iter_set *);
void bch2_btree_node_iter_advance(struct btree_node_iter *, struct btree *);

#define btree_node_iter_for_each(_iter, _set)				\
	for (_set = (_iter)->data;					\
	     _set < (_iter)->data + ARRAY_SIZE((_iter)->data) &&	\
	     (_set)->k != (_set)->end;					\
	     _set++)

static inline bool __btree_node_iter_set_end(struct btree_node_iter *iter,
					     unsigned i)
{
	return iter->data[i].k == iter->data[i].end;
}

static inline bool bch2_btree_node_iter_end(struct btree_node_iter *iter)
{
	return __btree_node_iter_set_end(iter, 0);
}

/*
 * When keys compare equal, deleted keys compare first:
 *
 * XXX: only need to compare pointers for keys that are both within a
 * btree_node_iterator - we need to break ties for prev() to work correctly
 */
static inline int bkey_iter_cmp(const struct btree *b,
				const struct bkey_packed *l,
				const struct bkey_packed *r)
{
	return bch2_bkey_cmp_packed(b, l, r)
		?: (int) bkey_deleted(r) - (int) bkey_deleted(l)
		?: cmp_int(l, r);
}

static inline int btree_node_iter_cmp(const struct btree *b,
				      struct btree_node_iter_set l,
				      struct btree_node_iter_set r)
{
	return bkey_iter_cmp(b,
			__btree_node_offset_to_key(b, l.k),
			__btree_node_offset_to_key(b, r.k));
}

/* These assume r (the search key) is not a deleted key: */
static inline int bkey_iter_pos_cmp(const struct btree *b,
			const struct bkey_packed *l,
			const struct bpos *r)
{
	return bkey_cmp_left_packed(b, l, r)
		?: -((int) bkey_deleted(l));
}

static inline int bkey_iter_cmp_p_or_unp(const struct btree *b,
				    const struct bkey_packed *l,
				    const struct bkey_packed *r_packed,
				    const struct bpos *r)
{
	return bkey_cmp_p_or_unp(b, l, r_packed, r)
		?: -((int) bkey_deleted(l));
}

static inline struct bkey_packed *
__bch2_btree_node_iter_peek_all(struct btree_node_iter *iter,
				struct btree *b)
{
	return __btree_node_offset_to_key(b, iter->data->k);
}

static inline struct bkey_packed *
bch2_btree_node_iter_peek_all(struct btree_node_iter *iter, struct btree *b)
{
	return !bch2_btree_node_iter_end(iter)
		? __btree_node_offset_to_key(b, iter->data->k)
		: NULL;
}

static inline struct bkey_packed *
bch2_btree_node_iter_peek(struct btree_node_iter *iter, struct btree *b)
{
	struct bkey_packed *k;

	while ((k = bch2_btree_node_iter_peek_all(iter, b)) &&
	       bkey_deleted(k))
		bch2_btree_node_iter_advance(iter, b);

	return k;
}

static inline struct bkey_packed *
bch2_btree_node_iter_next_all(struct btree_node_iter *iter, struct btree *b)
{
	struct bkey_packed *ret = bch2_btree_node_iter_peek_all(iter, b);

	if (ret)
		bch2_btree_node_iter_advance(iter, b);

	return ret;
}

struct bkey_packed *bch2_btree_node_iter_prev_all(struct btree_node_iter *,
						  struct btree *);
struct bkey_packed *bch2_btree_node_iter_prev(struct btree_node_iter *,
					      struct btree *);

struct bkey_s_c bch2_btree_node_iter_peek_unpack(struct btree_node_iter *,
						struct btree *,
						struct bkey *);

#define for_each_btree_node_key(b, k, iter)				\
	for (bch2_btree_node_iter_init_from_start((iter), (b));		\
	     (k = bch2_btree_node_iter_peek((iter), (b)));		\
	     bch2_btree_node_iter_advance(iter, b))

#define for_each_btree_node_key_unpack(b, k, iter, unpacked)		\
	for (bch2_btree_node_iter_init_from_start((iter), (b));		\
	     (k = bch2_btree_node_iter_peek_unpack((iter), (b), (unpacked))).k;\
	     bch2_btree_node_iter_advance(iter, b))

/* Accounting: */

static inline void btree_keys_account_key(struct btree_nr_keys *n,
					  unsigned bset,
					  struct bkey_packed *k,
					  int sign)
{
	n->live_u64s		+= k->u64s * sign;
	n->bset_u64s[bset]	+= k->u64s * sign;

	if (bkey_packed(k))
		n->packed_keys	+= sign;
	else
		n->unpacked_keys += sign;
}

static inline void btree_keys_account_val_delta(struct btree *b,
						struct bkey_packed *k,
						int delta)
{
	struct bset_tree *t = bch2_bkey_to_bset(b, k);

	b->nr.live_u64s			+= delta;
	b->nr.bset_u64s[t - b->set]	+= delta;
}

#define btree_keys_account_key_add(_nr, _bset_idx, _k)		\
	btree_keys_account_key(_nr, _bset_idx, _k, 1)
#define btree_keys_account_key_drop(_nr, _bset_idx, _k)	\
	btree_keys_account_key(_nr, _bset_idx, _k, -1)

#define btree_account_key_add(_b, _k)				\
	btree_keys_account_key(&(_b)->nr,			\
		bch2_bkey_to_bset(_b, _k) - (_b)->set, _k, 1)
#define btree_account_key_drop(_b, _k)				\
	btree_keys_account_key(&(_b)->nr,			\
		bch2_bkey_to_bset(_b, _k) - (_b)->set, _k, -1)

struct bset_stats {
	struct {
		size_t nr, bytes;
	} sets[BSET_TREE_NR_TYPES];

	size_t floats;
	size_t failed;
};

void bch2_btree_keys_stats(const struct btree *, struct bset_stats *);
void bch2_bfloat_to_text(struct printbuf *, struct btree *,
			 struct bkey_packed *);

/* Debug stuff */

void bch2_dump_bset(struct bch_fs *, struct btree *, struct bset *, unsigned);
void bch2_dump_btree_node(struct bch_fs *, struct btree *);
void bch2_dump_btree_node_iter(struct btree *, struct btree_node_iter *);

#ifdef CONFIG_BCACHEFS_DEBUG

void __bch2_verify_btree_nr_keys(struct btree *);
void bch2_btree_node_iter_verify(struct btree_node_iter *, struct btree *);
void bch2_verify_insert_pos(struct btree *, struct bkey_packed *,
			    struct bkey_packed *, unsigned);

#else

static inline void __bch2_verify_btree_nr_keys(struct btree *b) {}
static inline void bch2_btree_node_iter_verify(struct btree_node_iter *iter,
					      struct btree *b) {}
static inline void bch2_verify_insert_pos(struct btree *b,
					  struct bkey_packed *where,
					  struct bkey_packed *insert,
					  unsigned clobber_u64s) {}
#endif

static inline void bch2_verify_btree_nr_keys(struct btree *b)
{
	if (bch2_debug_check_btree_accounting)
		__bch2_verify_btree_nr_keys(b);
}

#endif /* _BCACHEFS_BSET_H */
