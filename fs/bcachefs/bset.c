// SPDX-License-Identifier: GPL-2.0
/*
 * Code for working with individual keys, and sorted sets of keys with in a
 * btree node
 *
 * Copyright 2012 Google, Inc.
 */

#include "bcachefs.h"
#include "btree_cache.h"
#include "bset.h"
#include "eytzinger.h"
#include "trace.h"
#include "util.h"

#include <asm/unaligned.h>
#include <linux/console.h>
#include <linux/random.h>
#include <linux/prefetch.h>

static inline void __bch2_btree_node_iter_advance(struct btree_node_iter *,
						  struct btree *);

static inline unsigned __btree_node_iter_used(struct btree_node_iter *iter)
{
	unsigned n = ARRAY_SIZE(iter->data);

	while (n && __btree_node_iter_set_end(iter, n - 1))
		--n;

	return n;
}

struct bset_tree *bch2_bkey_to_bset(struct btree *b, struct bkey_packed *k)
{
	return bch2_bkey_to_bset_inlined(b, k);
}

/*
 * There are never duplicate live keys in the btree - but including keys that
 * have been flagged as deleted (and will be cleaned up later) we _will_ see
 * duplicates.
 *
 * Thus the sort order is: usual key comparison first, but for keys that compare
 * equal the deleted key(s) come first, and the (at most one) live version comes
 * last.
 *
 * The main reason for this is insertion: to handle overwrites, we first iterate
 * over keys that compare equal to our insert key, and then insert immediately
 * prior to the first key greater than the key we're inserting - our insert
 * position will be after all keys that compare equal to our insert key, which
 * by the time we actually do the insert will all be deleted.
 */

void bch2_dump_bset(struct bch_fs *c, struct btree *b,
		    struct bset *i, unsigned set)
{
	struct bkey_packed *_k, *_n;
	struct bkey uk, n;
	struct bkey_s_c k;
	struct printbuf buf = PRINTBUF;

	if (!i->u64s)
		return;

	for (_k = i->start;
	     _k < vstruct_last(i);
	     _k = _n) {
		_n = bkey_p_next(_k);

		if (!_k->u64s) {
			printk(KERN_ERR "block %u key %5zu - u64s 0? aieee!\n", set,
			       _k->_data - i->_data);
			break;
		}

		k = bkey_disassemble(b, _k, &uk);

		printbuf_reset(&buf);
		if (c)
			bch2_bkey_val_to_text(&buf, c, k);
		else
			bch2_bkey_to_text(&buf, k.k);
		printk(KERN_ERR "block %u key %5zu: %s\n", set,
		       _k->_data - i->_data, buf.buf);

		if (_n == vstruct_last(i))
			continue;

		n = bkey_unpack_key(b, _n);

		if (bpos_lt(n.p, k.k->p)) {
			printk(KERN_ERR "Key skipped backwards\n");
			continue;
		}

		if (!bkey_deleted(k.k) && bpos_eq(n.p, k.k->p))
			printk(KERN_ERR "Duplicate keys\n");
	}

	printbuf_exit(&buf);
}

void bch2_dump_btree_node(struct bch_fs *c, struct btree *b)
{
	console_lock();
	for_each_bset(b, t)
		bch2_dump_bset(c, b, bset(b, t), t - b->set);
	console_unlock();
}

void bch2_dump_btree_node_iter(struct btree *b,
			      struct btree_node_iter *iter)
{
	struct btree_node_iter_set *set;
	struct printbuf buf = PRINTBUF;

	printk(KERN_ERR "btree node iter with %u/%u sets:\n",
	       __btree_node_iter_used(iter), b->nsets);

	btree_node_iter_for_each(iter, set) {
		struct bkey_packed *k = __btree_node_offset_to_key(b, set->k);
		struct bset_tree *t = bch2_bkey_to_bset(b, k);
		struct bkey uk = bkey_unpack_key(b, k);

		printbuf_reset(&buf);
		bch2_bkey_to_text(&buf, &uk);
		printk(KERN_ERR "set %zu key %u: %s\n",
		       t - b->set, set->k, buf.buf);
	}

	printbuf_exit(&buf);
}

struct btree_nr_keys bch2_btree_node_count_keys(struct btree *b)
{
	struct bkey_packed *k;
	struct btree_nr_keys nr = {};

	for_each_bset(b, t)
		bset_tree_for_each_key(b, t, k)
			if (!bkey_deleted(k))
				btree_keys_account_key_add(&nr, t - b->set, k);
	return nr;
}

#ifdef CONFIG_BCACHEFS_DEBUG

void __bch2_verify_btree_nr_keys(struct btree *b)
{
	struct btree_nr_keys nr = bch2_btree_node_count_keys(b);

	BUG_ON(memcmp(&nr, &b->nr, sizeof(nr)));
}

static void bch2_btree_node_iter_next_check(struct btree_node_iter *_iter,
					    struct btree *b)
{
	struct btree_node_iter iter = *_iter;
	const struct bkey_packed *k, *n;

	k = bch2_btree_node_iter_peek_all(&iter, b);
	__bch2_btree_node_iter_advance(&iter, b);
	n = bch2_btree_node_iter_peek_all(&iter, b);

	bkey_unpack_key(b, k);

	if (n &&
	    bkey_iter_cmp(b, k, n) > 0) {
		struct btree_node_iter_set *set;
		struct bkey ku = bkey_unpack_key(b, k);
		struct bkey nu = bkey_unpack_key(b, n);
		struct printbuf buf1 = PRINTBUF;
		struct printbuf buf2 = PRINTBUF;

		bch2_dump_btree_node(NULL, b);
		bch2_bkey_to_text(&buf1, &ku);
		bch2_bkey_to_text(&buf2, &nu);
		printk(KERN_ERR "out of order/overlapping:\n%s\n%s\n",
		       buf1.buf, buf2.buf);
		printk(KERN_ERR "iter was:");

		btree_node_iter_for_each(_iter, set) {
			struct bkey_packed *k2 = __btree_node_offset_to_key(b, set->k);
			struct bset_tree *t = bch2_bkey_to_bset(b, k2);
			printk(" [%zi %zi]", t - b->set,
			       k2->_data - bset(b, t)->_data);
		}
		panic("\n");
	}
}

void bch2_btree_node_iter_verify(struct btree_node_iter *iter,
				 struct btree *b)
{
	struct btree_node_iter_set *set, *s2;
	struct bkey_packed *k, *p;

	if (bch2_btree_node_iter_end(iter))
		return;

	/* Verify no duplicates: */
	btree_node_iter_for_each(iter, set) {
		BUG_ON(set->k > set->end);
		btree_node_iter_for_each(iter, s2)
			BUG_ON(set != s2 && set->end == s2->end);
	}

	/* Verify that set->end is correct: */
	btree_node_iter_for_each(iter, set) {
		for_each_bset(b, t)
			if (set->end == t->end_offset) {
				BUG_ON(set->k < btree_bkey_first_offset(t) ||
				       set->k >= t->end_offset);
				goto found;
			}
		BUG();
found:
		do {} while (0);
	}

	/* Verify iterator is sorted: */
	btree_node_iter_for_each(iter, set)
		BUG_ON(set != iter->data &&
		       btree_node_iter_cmp(b, set[-1], set[0]) > 0);

	k = bch2_btree_node_iter_peek_all(iter, b);

	for_each_bset(b, t) {
		if (iter->data[0].end == t->end_offset)
			continue;

		p = bch2_bkey_prev_all(b, t,
			bch2_btree_node_iter_bset_pos(iter, b, t));

		BUG_ON(p && bkey_iter_cmp(b, k, p) < 0);
	}
}

void bch2_verify_insert_pos(struct btree *b, struct bkey_packed *where,
			    struct bkey_packed *insert, unsigned clobber_u64s)
{
	struct bset_tree *t = bch2_bkey_to_bset(b, where);
	struct bkey_packed *prev = bch2_bkey_prev_all(b, t, where);
	struct bkey_packed *next = (void *) ((u64 *) where->_data + clobber_u64s);
	struct printbuf buf1 = PRINTBUF;
	struct printbuf buf2 = PRINTBUF;
#if 0
	BUG_ON(prev &&
	       bkey_iter_cmp(b, prev, insert) > 0);
#else
	if (prev &&
	    bkey_iter_cmp(b, prev, insert) > 0) {
		struct bkey k1 = bkey_unpack_key(b, prev);
		struct bkey k2 = bkey_unpack_key(b, insert);

		bch2_dump_btree_node(NULL, b);
		bch2_bkey_to_text(&buf1, &k1);
		bch2_bkey_to_text(&buf2, &k2);

		panic("prev > insert:\n"
		      "prev    key %s\n"
		      "insert  key %s\n",
		      buf1.buf, buf2.buf);
	}
#endif
#if 0
	BUG_ON(next != btree_bkey_last(b, t) &&
	       bkey_iter_cmp(b, insert, next) > 0);
#else
	if (next != btree_bkey_last(b, t) &&
	    bkey_iter_cmp(b, insert, next) > 0) {
		struct bkey k1 = bkey_unpack_key(b, insert);
		struct bkey k2 = bkey_unpack_key(b, next);

		bch2_dump_btree_node(NULL, b);
		bch2_bkey_to_text(&buf1, &k1);
		bch2_bkey_to_text(&buf2, &k2);

		panic("insert > next:\n"
		      "insert  key %s\n"
		      "next    key %s\n",
		      buf1.buf, buf2.buf);
	}
#endif
}

#else

static inline void bch2_btree_node_iter_next_check(struct btree_node_iter *iter,
						   struct btree *b) {}

#endif

/* Auxiliary search trees */

#define BFLOAT_FAILED_UNPACKED	U8_MAX
#define BFLOAT_FAILED		U8_MAX

struct bkey_float {
	u8		exponent;
	u8		key_offset;
	u16		mantissa;
};
#define BKEY_MANTISSA_BITS	16

struct ro_aux_tree {
	u8			nothing[0];
	struct bkey_float	f[];
};

struct rw_aux_tree {
	u16		offset;
	struct bpos	k;
};

static unsigned bset_aux_tree_buf_end(const struct bset_tree *t)
{
	BUG_ON(t->aux_data_offset == U16_MAX);

	switch (bset_aux_tree_type(t)) {
	case BSET_NO_AUX_TREE:
		return t->aux_data_offset;
	case BSET_RO_AUX_TREE:
		return t->aux_data_offset +
			DIV_ROUND_UP(t->size * sizeof(struct bkey_float), 8);
	case BSET_RW_AUX_TREE:
		return t->aux_data_offset +
			DIV_ROUND_UP(sizeof(struct rw_aux_tree) * t->size, 8);
	default:
		BUG();
	}
}

static unsigned bset_aux_tree_buf_start(const struct btree *b,
					const struct bset_tree *t)
{
	return t == b->set
		? DIV_ROUND_UP(b->unpack_fn_len, 8)
		: bset_aux_tree_buf_end(t - 1);
}

static void *__aux_tree_base(const struct btree *b,
			     const struct bset_tree *t)
{
	return b->aux_data + t->aux_data_offset * 8;
}

static struct ro_aux_tree *ro_aux_tree_base(const struct btree *b,
					    const struct bset_tree *t)
{
	EBUG_ON(bset_aux_tree_type(t) != BSET_RO_AUX_TREE);

	return __aux_tree_base(b, t);
}

static struct bkey_float *bkey_float(const struct btree *b,
				     const struct bset_tree *t,
				     unsigned idx)
{
	return ro_aux_tree_base(b, t)->f + idx;
}

static void bset_aux_tree_verify(struct btree *b)
{
#ifdef CONFIG_BCACHEFS_DEBUG
	for_each_bset(b, t) {
		if (t->aux_data_offset == U16_MAX)
			continue;

		BUG_ON(t != b->set &&
		       t[-1].aux_data_offset == U16_MAX);

		BUG_ON(t->aux_data_offset < bset_aux_tree_buf_start(b, t));
		BUG_ON(t->aux_data_offset > btree_aux_data_u64s(b));
		BUG_ON(bset_aux_tree_buf_end(t) > btree_aux_data_u64s(b));
	}
#endif
}

void bch2_btree_keys_init(struct btree *b)
{
	unsigned i;

	b->nsets		= 0;
	memset(&b->nr, 0, sizeof(b->nr));

	for (i = 0; i < MAX_BSETS; i++)
		b->set[i].data_offset = U16_MAX;

	bch2_bset_set_no_aux_tree(b, b->set);
}

/* Binary tree stuff for auxiliary search trees */

/*
 * Cacheline/offset <-> bkey pointer arithmetic:
 *
 * t->tree is a binary search tree in an array; each node corresponds to a key
 * in one cacheline in t->set (BSET_CACHELINE bytes).
 *
 * This means we don't have to store the full index of the key that a node in
 * the binary tree points to; eytzinger1_to_inorder() gives us the cacheline, and
 * then bkey_float->m gives us the offset within that cacheline, in units of 8
 * bytes.
 *
 * cacheline_to_bkey() and friends abstract out all the pointer arithmetic to
 * make this work.
 *
 * To construct the bfloat for an arbitrary key we need to know what the key
 * immediately preceding it is: we have to check if the two keys differ in the
 * bits we're going to store in bkey_float->mantissa. t->prev[j] stores the size
 * of the previous key so we can walk backwards to it from t->tree[j]'s key.
 */

static inline void *bset_cacheline(const struct btree *b,
				   const struct bset_tree *t,
				   unsigned cacheline)
{
	return (void *) round_down((unsigned long) btree_bkey_first(b, t),
				   L1_CACHE_BYTES) +
		cacheline * BSET_CACHELINE;
}

static struct bkey_packed *cacheline_to_bkey(const struct btree *b,
					     const struct bset_tree *t,
					     unsigned cacheline,
					     unsigned offset)
{
	return bset_cacheline(b, t, cacheline) + offset * 8;
}

static unsigned bkey_to_cacheline(const struct btree *b,
				  const struct bset_tree *t,
				  const struct bkey_packed *k)
{
	return ((void *) k - bset_cacheline(b, t, 0)) / BSET_CACHELINE;
}

static ssize_t __bkey_to_cacheline_offset(const struct btree *b,
					  const struct bset_tree *t,
					  unsigned cacheline,
					  const struct bkey_packed *k)
{
	return (u64 *) k - (u64 *) bset_cacheline(b, t, cacheline);
}

static unsigned bkey_to_cacheline_offset(const struct btree *b,
					 const struct bset_tree *t,
					 unsigned cacheline,
					 const struct bkey_packed *k)
{
	size_t m = __bkey_to_cacheline_offset(b, t, cacheline, k);

	EBUG_ON(m > U8_MAX);
	return m;
}

static inline struct bkey_packed *tree_to_bkey(const struct btree *b,
					       const struct bset_tree *t,
					       unsigned j)
{
	return cacheline_to_bkey(b, t,
			__eytzinger1_to_inorder(j, t->size - 1, t->extra),
			bkey_float(b, t, j)->key_offset);
}

static struct rw_aux_tree *rw_aux_tree(const struct btree *b,
				       const struct bset_tree *t)
{
	EBUG_ON(bset_aux_tree_type(t) != BSET_RW_AUX_TREE);

	return __aux_tree_base(b, t);
}

/*
 * For the write set - the one we're currently inserting keys into - we don't
 * maintain a full search tree, we just keep a simple lookup table in t->prev.
 */
static struct bkey_packed *rw_aux_to_bkey(const struct btree *b,
					  struct bset_tree *t,
					  unsigned j)
{
	return __btree_node_offset_to_key(b, rw_aux_tree(b, t)[j].offset);
}

static void rw_aux_tree_set(const struct btree *b, struct bset_tree *t,
			    unsigned j, struct bkey_packed *k)
{
	EBUG_ON(k >= btree_bkey_last(b, t));

	rw_aux_tree(b, t)[j] = (struct rw_aux_tree) {
		.offset	= __btree_node_key_to_offset(b, k),
		.k	= bkey_unpack_pos(b, k),
	};
}

static void bch2_bset_verify_rw_aux_tree(struct btree *b,
					struct bset_tree *t)
{
	struct bkey_packed *k = btree_bkey_first(b, t);
	unsigned j = 0;

	if (!bch2_expensive_debug_checks)
		return;

	BUG_ON(bset_has_ro_aux_tree(t));

	if (!bset_has_rw_aux_tree(t))
		return;

	BUG_ON(t->size < 1);
	BUG_ON(rw_aux_to_bkey(b, t, j) != k);

	goto start;
	while (1) {
		if (rw_aux_to_bkey(b, t, j) == k) {
			BUG_ON(!bpos_eq(rw_aux_tree(b, t)[j].k,
					bkey_unpack_pos(b, k)));
start:
			if (++j == t->size)
				break;

			BUG_ON(rw_aux_tree(b, t)[j].offset <=
			       rw_aux_tree(b, t)[j - 1].offset);
		}

		k = bkey_p_next(k);
		BUG_ON(k >= btree_bkey_last(b, t));
	}
}

/* returns idx of first entry >= offset: */
static unsigned rw_aux_tree_bsearch(struct btree *b,
				    struct bset_tree *t,
				    unsigned offset)
{
	unsigned bset_offs = offset - btree_bkey_first_offset(t);
	unsigned bset_u64s = t->end_offset - btree_bkey_first_offset(t);
	unsigned idx = bset_u64s ? bset_offs * t->size / bset_u64s : 0;

	EBUG_ON(bset_aux_tree_type(t) != BSET_RW_AUX_TREE);
	EBUG_ON(!t->size);
	EBUG_ON(idx > t->size);

	while (idx < t->size &&
	       rw_aux_tree(b, t)[idx].offset < offset)
		idx++;

	while (idx &&
	       rw_aux_tree(b, t)[idx - 1].offset >= offset)
		idx--;

	EBUG_ON(idx < t->size &&
		rw_aux_tree(b, t)[idx].offset < offset);
	EBUG_ON(idx && rw_aux_tree(b, t)[idx - 1].offset >= offset);
	EBUG_ON(idx + 1 < t->size &&
		rw_aux_tree(b, t)[idx].offset ==
		rw_aux_tree(b, t)[idx + 1].offset);

	return idx;
}

static inline unsigned bkey_mantissa(const struct bkey_packed *k,
				     const struct bkey_float *f)
{
	u64 v;

	EBUG_ON(!bkey_packed(k));

	v = get_unaligned((u64 *) (((u8 *) k->_data) + (f->exponent >> 3)));

	/*
	 * In little endian, we're shifting off low bits (and then the bits we
	 * want are at the low end), in big endian we're shifting off high bits
	 * (and then the bits we want are at the high end, so we shift them
	 * back down):
	 */
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
	v >>= f->exponent & 7;
#else
	v >>= 64 - (f->exponent & 7) - BKEY_MANTISSA_BITS;
#endif
	return (u16) v;
}

static __always_inline void make_bfloat(struct btree *b, struct bset_tree *t,
					unsigned j,
					struct bkey_packed *min_key,
					struct bkey_packed *max_key)
{
	struct bkey_float *f = bkey_float(b, t, j);
	struct bkey_packed *m = tree_to_bkey(b, t, j);
	struct bkey_packed *l = is_power_of_2(j)
		? min_key
		: tree_to_bkey(b, t, j >> ffs(j));
	struct bkey_packed *r = is_power_of_2(j + 1)
		? max_key
		: tree_to_bkey(b, t, j >> (ffz(j) + 1));
	unsigned mantissa;
	int shift, exponent, high_bit;

	/*
	 * for failed bfloats, the lookup code falls back to comparing against
	 * the original key.
	 */

	if (!bkey_packed(l) || !bkey_packed(r) || !bkey_packed(m) ||
	    !b->nr_key_bits) {
		f->exponent = BFLOAT_FAILED_UNPACKED;
		return;
	}

	/*
	 * The greatest differing bit of l and r is the first bit we must
	 * include in the bfloat mantissa we're creating in order to do
	 * comparisons - that bit always becomes the high bit of
	 * bfloat->mantissa, and thus the exponent we're calculating here is
	 * the position of what will become the low bit in bfloat->mantissa:
	 *
	 * Note that this may be negative - we may be running off the low end
	 * of the key: we handle this later:
	 */
	high_bit = max(bch2_bkey_greatest_differing_bit(b, l, r),
		       min_t(unsigned, BKEY_MANTISSA_BITS, b->nr_key_bits) - 1);
	exponent = high_bit - (BKEY_MANTISSA_BITS - 1);

	/*
	 * Then we calculate the actual shift value, from the start of the key
	 * (k->_data), to get the key bits starting at exponent:
	 */
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
	shift = (int) (b->format.key_u64s * 64 - b->nr_key_bits) + exponent;

	EBUG_ON(shift + BKEY_MANTISSA_BITS > b->format.key_u64s * 64);
#else
	shift = high_bit_offset +
		b->nr_key_bits -
		exponent -
		BKEY_MANTISSA_BITS;

	EBUG_ON(shift < KEY_PACKED_BITS_START);
#endif
	EBUG_ON(shift < 0 || shift >= BFLOAT_FAILED);

	f->exponent = shift;
	mantissa = bkey_mantissa(m, f);

	/*
	 * If we've got garbage bits, set them to all 1s - it's legal for the
	 * bfloat to compare larger than the original key, but not smaller:
	 */
	if (exponent < 0)
		mantissa |= ~(~0U << -exponent);

	f->mantissa = mantissa;
}

/* bytes remaining - only valid for last bset: */
static unsigned __bset_tree_capacity(struct btree *b, const struct bset_tree *t)
{
	bset_aux_tree_verify(b);

	return btree_aux_data_bytes(b) - t->aux_data_offset * sizeof(u64);
}

static unsigned bset_ro_tree_capacity(struct btree *b, const struct bset_tree *t)
{
	return __bset_tree_capacity(b, t) / sizeof(struct bkey_float);
}

static unsigned bset_rw_tree_capacity(struct btree *b, const struct bset_tree *t)
{
	return __bset_tree_capacity(b, t) / sizeof(struct rw_aux_tree);
}

static noinline void __build_rw_aux_tree(struct btree *b, struct bset_tree *t)
{
	struct bkey_packed *k;

	t->size = 1;
	t->extra = BSET_RW_AUX_TREE_VAL;
	rw_aux_tree(b, t)[0].offset =
		__btree_node_key_to_offset(b, btree_bkey_first(b, t));

	bset_tree_for_each_key(b, t, k) {
		if (t->size == bset_rw_tree_capacity(b, t))
			break;

		if ((void *) k - (void *) rw_aux_to_bkey(b, t, t->size - 1) >
		    L1_CACHE_BYTES)
			rw_aux_tree_set(b, t, t->size++, k);
	}
}

static noinline void __build_ro_aux_tree(struct btree *b, struct bset_tree *t)
{
	struct bkey_packed *k = btree_bkey_first(b, t);
	struct bkey_i min_key, max_key;
	unsigned cacheline = 1;

	t->size = min(bkey_to_cacheline(b, t, btree_bkey_last(b, t)),
		      bset_ro_tree_capacity(b, t));
retry:
	if (t->size < 2) {
		t->size = 0;
		t->extra = BSET_NO_AUX_TREE_VAL;
		return;
	}

	t->extra = eytzinger1_extra(t->size - 1);

	/* First we figure out where the first key in each cacheline is */
	eytzinger1_for_each(j, t->size - 1) {
		while (bkey_to_cacheline(b, t, k) < cacheline)
			k = bkey_p_next(k);

		if (k >= btree_bkey_last(b, t)) {
			/* XXX: this path sucks */
			t->size--;
			goto retry;
		}

		bkey_float(b, t, j)->key_offset =
			bkey_to_cacheline_offset(b, t, cacheline++, k);

		EBUG_ON(tree_to_bkey(b, t, j) != k);
	}

	if (!bkey_pack_pos(bkey_to_packed(&min_key), b->data->min_key, b)) {
		bkey_init(&min_key.k);
		min_key.k.p = b->data->min_key;
	}

	if (!bkey_pack_pos(bkey_to_packed(&max_key), b->data->max_key, b)) {
		bkey_init(&max_key.k);
		max_key.k.p = b->data->max_key;
	}

	/* Then we build the tree */
	eytzinger1_for_each(j, t->size - 1)
		make_bfloat(b, t, j,
			    bkey_to_packed(&min_key),
			    bkey_to_packed(&max_key));
}

static void bset_alloc_tree(struct btree *b, struct bset_tree *t)
{
	struct bset_tree *i;

	for (i = b->set; i != t; i++)
		BUG_ON(bset_has_rw_aux_tree(i));

	bch2_bset_set_no_aux_tree(b, t);

	/* round up to next cacheline: */
	t->aux_data_offset = round_up(bset_aux_tree_buf_start(b, t),
				      SMP_CACHE_BYTES / sizeof(u64));

	bset_aux_tree_verify(b);
}

void bch2_bset_build_aux_tree(struct btree *b, struct bset_tree *t,
			     bool writeable)
{
	if (writeable
	    ? bset_has_rw_aux_tree(t)
	    : bset_has_ro_aux_tree(t))
		return;

	bset_alloc_tree(b, t);

	if (!__bset_tree_capacity(b, t))
		return;

	if (writeable)
		__build_rw_aux_tree(b, t);
	else
		__build_ro_aux_tree(b, t);

	bset_aux_tree_verify(b);
}

void bch2_bset_init_first(struct btree *b, struct bset *i)
{
	struct bset_tree *t;

	BUG_ON(b->nsets);

	memset(i, 0, sizeof(*i));
	get_random_bytes(&i->seq, sizeof(i->seq));
	SET_BSET_BIG_ENDIAN(i, CPU_BIG_ENDIAN);

	t = &b->set[b->nsets++];
	set_btree_bset(b, t, i);
}

void bch2_bset_init_next(struct btree *b, struct btree_node_entry *bne)
{
	struct bset *i = &bne->keys;
	struct bset_tree *t;

	BUG_ON(bset_byte_offset(b, bne) >= btree_buf_bytes(b));
	BUG_ON((void *) bne < (void *) btree_bkey_last(b, bset_tree_last(b)));
	BUG_ON(b->nsets >= MAX_BSETS);

	memset(i, 0, sizeof(*i));
	i->seq = btree_bset_first(b)->seq;
	SET_BSET_BIG_ENDIAN(i, CPU_BIG_ENDIAN);

	t = &b->set[b->nsets++];
	set_btree_bset(b, t, i);
}

/*
 * find _some_ key in the same bset as @k that precedes @k - not necessarily the
 * immediate predecessor:
 */
static struct bkey_packed *__bkey_prev(struct btree *b, struct bset_tree *t,
				       struct bkey_packed *k)
{
	struct bkey_packed *p;
	unsigned offset;
	int j;

	EBUG_ON(k < btree_bkey_first(b, t) ||
		k > btree_bkey_last(b, t));

	if (k == btree_bkey_first(b, t))
		return NULL;

	switch (bset_aux_tree_type(t)) {
	case BSET_NO_AUX_TREE:
		p = btree_bkey_first(b, t);
		break;
	case BSET_RO_AUX_TREE:
		j = min_t(unsigned, t->size - 1, bkey_to_cacheline(b, t, k));

		do {
			p = j ? tree_to_bkey(b, t,
					__inorder_to_eytzinger1(j--,
							t->size - 1, t->extra))
			      : btree_bkey_first(b, t);
		} while (p >= k);
		break;
	case BSET_RW_AUX_TREE:
		offset = __btree_node_key_to_offset(b, k);
		j = rw_aux_tree_bsearch(b, t, offset);
		p = j ? rw_aux_to_bkey(b, t, j - 1)
		      : btree_bkey_first(b, t);
		break;
	}

	return p;
}

struct bkey_packed *bch2_bkey_prev_filter(struct btree *b,
					  struct bset_tree *t,
					  struct bkey_packed *k,
					  unsigned min_key_type)
{
	struct bkey_packed *p, *i, *ret = NULL, *orig_k = k;

	while ((p = __bkey_prev(b, t, k)) && !ret) {
		for (i = p; i != k; i = bkey_p_next(i))
			if (i->type >= min_key_type)
				ret = i;

		k = p;
	}

	if (bch2_expensive_debug_checks) {
		BUG_ON(ret >= orig_k);

		for (i = ret
			? bkey_p_next(ret)
			: btree_bkey_first(b, t);
		     i != orig_k;
		     i = bkey_p_next(i))
			BUG_ON(i->type >= min_key_type);
	}

	return ret;
}

/* Insert */

static void rw_aux_tree_insert_entry(struct btree *b,
				     struct bset_tree *t,
				     unsigned idx)
{
	EBUG_ON(!idx || idx > t->size);
	struct bkey_packed *start = rw_aux_to_bkey(b, t, idx - 1);
	struct bkey_packed *end = idx < t->size
				  ? rw_aux_to_bkey(b, t, idx)
				  : btree_bkey_last(b, t);

	if (t->size < bset_rw_tree_capacity(b, t) &&
	    (void *) end - (void *) start > L1_CACHE_BYTES) {
		struct bkey_packed *k = start;

		while (1) {
			k = bkey_p_next(k);
			if (k == end)
				break;

			if ((void *) k - (void *) start >= L1_CACHE_BYTES) {
				memmove(&rw_aux_tree(b, t)[idx + 1],
					&rw_aux_tree(b, t)[idx],
					(void *) &rw_aux_tree(b, t)[t->size] -
					(void *) &rw_aux_tree(b, t)[idx]);
				t->size++;
				rw_aux_tree_set(b, t, idx, k);
				break;
			}
		}
	}
}

static void bch2_bset_fix_lookup_table(struct btree *b,
				       struct bset_tree *t,
				       struct bkey_packed *_where,
				       unsigned clobber_u64s,
				       unsigned new_u64s)
{
	int shift = new_u64s - clobber_u64s;
	unsigned idx, j, where = __btree_node_key_to_offset(b, _where);

	EBUG_ON(bset_has_ro_aux_tree(t));

	if (!bset_has_rw_aux_tree(t))
		return;

	if (where > rw_aux_tree(b, t)[t->size - 1].offset) {
		rw_aux_tree_insert_entry(b, t, t->size);
		goto verify;
	}

	/* returns first entry >= where */
	idx = rw_aux_tree_bsearch(b, t, where);

	if (rw_aux_tree(b, t)[idx].offset == where) {
		if (!idx) { /* never delete first entry */
			idx++;
		} else if (where < t->end_offset) {
			rw_aux_tree_set(b, t, idx++, _where);
		} else {
			EBUG_ON(where != t->end_offset);
			rw_aux_tree_insert_entry(b, t, --t->size);
			goto verify;
		}
	}

	EBUG_ON(idx < t->size && rw_aux_tree(b, t)[idx].offset <= where);
	if (idx < t->size &&
	    rw_aux_tree(b, t)[idx].offset + shift ==
	    rw_aux_tree(b, t)[idx - 1].offset) {
		memmove(&rw_aux_tree(b, t)[idx],
			&rw_aux_tree(b, t)[idx + 1],
			(void *) &rw_aux_tree(b, t)[t->size] -
			(void *) &rw_aux_tree(b, t)[idx + 1]);
		t->size -= 1;
	}

	for (j = idx; j < t->size; j++)
		rw_aux_tree(b, t)[j].offset += shift;

	EBUG_ON(idx < t->size &&
		rw_aux_tree(b, t)[idx].offset ==
		rw_aux_tree(b, t)[idx - 1].offset);

	rw_aux_tree_insert_entry(b, t, idx);

verify:
	bch2_bset_verify_rw_aux_tree(b, t);
	bset_aux_tree_verify(b);
}

void bch2_bset_insert(struct btree *b,
		      struct bkey_packed *where,
		      struct bkey_i *insert,
		      unsigned clobber_u64s)
{
	struct bkey_format *f = &b->format;
	struct bset_tree *t = bset_tree_last(b);
	struct bkey_packed packed, *src = bkey_to_packed(insert);

	bch2_bset_verify_rw_aux_tree(b, t);
	bch2_verify_insert_pos(b, where, bkey_to_packed(insert), clobber_u64s);

	if (bch2_bkey_pack_key(&packed, &insert->k, f))
		src = &packed;

	if (!bkey_deleted(&insert->k))
		btree_keys_account_key_add(&b->nr, t - b->set, src);

	if (src->u64s != clobber_u64s) {
		u64 *src_p = (u64 *) where->_data + clobber_u64s;
		u64 *dst_p = (u64 *) where->_data + src->u64s;

		EBUG_ON((int) le16_to_cpu(bset(b, t)->u64s) <
			(int) clobber_u64s - src->u64s);

		memmove_u64s(dst_p, src_p, btree_bkey_last(b, t)->_data - src_p);
		le16_add_cpu(&bset(b, t)->u64s, src->u64s - clobber_u64s);
		set_btree_bset_end(b, t);
	}

	memcpy_u64s_small(where, src,
		    bkeyp_key_u64s(f, src));
	memcpy_u64s(bkeyp_val(f, where), &insert->v,
		    bkeyp_val_u64s(f, src));

	if (src->u64s != clobber_u64s)
		bch2_bset_fix_lookup_table(b, t, where, clobber_u64s, src->u64s);

	bch2_verify_btree_nr_keys(b);
}

void bch2_bset_delete(struct btree *b,
		      struct bkey_packed *where,
		      unsigned clobber_u64s)
{
	struct bset_tree *t = bset_tree_last(b);
	u64 *src_p = (u64 *) where->_data + clobber_u64s;
	u64 *dst_p = where->_data;

	bch2_bset_verify_rw_aux_tree(b, t);

	EBUG_ON(le16_to_cpu(bset(b, t)->u64s) < clobber_u64s);

	memmove_u64s_down(dst_p, src_p, btree_bkey_last(b, t)->_data - src_p);
	le16_add_cpu(&bset(b, t)->u64s, -clobber_u64s);
	set_btree_bset_end(b, t);

	bch2_bset_fix_lookup_table(b, t, where, clobber_u64s, 0);
}

/* Lookup */

__flatten
static struct bkey_packed *bset_search_write_set(const struct btree *b,
				struct bset_tree *t,
				struct bpos *search)
{
	unsigned l = 0, r = t->size;

	while (l + 1 != r) {
		unsigned m = (l + r) >> 1;

		if (bpos_lt(rw_aux_tree(b, t)[m].k, *search))
			l = m;
		else
			r = m;
	}

	return rw_aux_to_bkey(b, t, l);
}

static inline void prefetch_four_cachelines(void *p)
{
#ifdef CONFIG_X86_64
	asm("prefetcht0 (-127 + 64 * 0)(%0);"
	    "prefetcht0 (-127 + 64 * 1)(%0);"
	    "prefetcht0 (-127 + 64 * 2)(%0);"
	    "prefetcht0 (-127 + 64 * 3)(%0);"
	    :
	    : "r" (p + 127));
#else
	prefetch(p + L1_CACHE_BYTES * 0);
	prefetch(p + L1_CACHE_BYTES * 1);
	prefetch(p + L1_CACHE_BYTES * 2);
	prefetch(p + L1_CACHE_BYTES * 3);
#endif
}

static inline bool bkey_mantissa_bits_dropped(const struct btree *b,
					      const struct bkey_float *f)
{
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
	unsigned key_bits_start = b->format.key_u64s * 64 - b->nr_key_bits;

	return f->exponent > key_bits_start;
#else
	unsigned key_bits_end = high_bit_offset + b->nr_key_bits;

	return f->exponent + BKEY_MANTISSA_BITS < key_bits_end;
#endif
}

__flatten
static struct bkey_packed *bset_search_tree(const struct btree *b,
				const struct bset_tree *t,
				const struct bpos *search,
				const struct bkey_packed *packed_search)
{
	struct ro_aux_tree *base = ro_aux_tree_base(b, t);
	struct bkey_float *f;
	struct bkey_packed *k;
	unsigned inorder, n = 1, l, r;
	int cmp;

	do {
		if (likely(n << 4 < t->size))
			prefetch(&base->f[n << 4]);

		f = &base->f[n];
		if (unlikely(f->exponent >= BFLOAT_FAILED))
			goto slowpath;

		l = f->mantissa;
		r = bkey_mantissa(packed_search, f);

		if (unlikely(l == r) && bkey_mantissa_bits_dropped(b, f))
			goto slowpath;

		n = n * 2 + (l < r);
		continue;
slowpath:
		k = tree_to_bkey(b, t, n);
		cmp = bkey_cmp_p_or_unp(b, k, packed_search, search);
		if (!cmp)
			return k;

		n = n * 2 + (cmp < 0);
	} while (n < t->size);

	inorder = __eytzinger1_to_inorder(n >> 1, t->size - 1, t->extra);

	/*
	 * n would have been the node we recursed to - the low bit tells us if
	 * we recursed left or recursed right.
	 */
	if (likely(!(n & 1))) {
		--inorder;
		if (unlikely(!inorder))
			return btree_bkey_first(b, t);

		f = &base->f[eytzinger1_prev(n >> 1, t->size - 1)];
	}

	return cacheline_to_bkey(b, t, inorder, f->key_offset);
}

static __always_inline __flatten
struct bkey_packed *__bch2_bset_search(struct btree *b,
				struct bset_tree *t,
				struct bpos *search,
				const struct bkey_packed *lossy_packed_search)
{

	/*
	 * First, we search for a cacheline, then lastly we do a linear search
	 * within that cacheline.
	 *
	 * To search for the cacheline, there's three different possibilities:
	 *  * The set is too small to have a search tree, so we just do a linear
	 *    search over the whole set.
	 *  * The set is the one we're currently inserting into; keeping a full
	 *    auxiliary search tree up to date would be too expensive, so we
	 *    use a much simpler lookup table to do a binary search -
	 *    bset_search_write_set().
	 *  * Or we use the auxiliary search tree we constructed earlier -
	 *    bset_search_tree()
	 */

	switch (bset_aux_tree_type(t)) {
	case BSET_NO_AUX_TREE:
		return btree_bkey_first(b, t);
	case BSET_RW_AUX_TREE:
		return bset_search_write_set(b, t, search);
	case BSET_RO_AUX_TREE:
		return bset_search_tree(b, t, search, lossy_packed_search);
	default:
		BUG();
	}
}

static __always_inline __flatten
struct bkey_packed *bch2_bset_search_linear(struct btree *b,
				struct bset_tree *t,
				struct bpos *search,
				struct bkey_packed *packed_search,
				const struct bkey_packed *lossy_packed_search,
				struct bkey_packed *m)
{
	if (lossy_packed_search)
		while (m != btree_bkey_last(b, t) &&
		       bkey_iter_cmp_p_or_unp(b, m,
					lossy_packed_search, search) < 0)
			m = bkey_p_next(m);

	if (!packed_search)
		while (m != btree_bkey_last(b, t) &&
		       bkey_iter_pos_cmp(b, m, search) < 0)
			m = bkey_p_next(m);

	if (bch2_expensive_debug_checks) {
		struct bkey_packed *prev = bch2_bkey_prev_all(b, t, m);

		BUG_ON(prev &&
		       bkey_iter_cmp_p_or_unp(b, prev,
					packed_search, search) >= 0);
	}

	return m;
}

/* Btree node iterator */

static inline void __bch2_btree_node_iter_push(struct btree_node_iter *iter,
			      struct btree *b,
			      const struct bkey_packed *k,
			      const struct bkey_packed *end)
{
	if (k != end) {
		struct btree_node_iter_set *pos;

		btree_node_iter_for_each(iter, pos)
			;

		BUG_ON(pos >= iter->data + ARRAY_SIZE(iter->data));
		*pos = (struct btree_node_iter_set) {
			__btree_node_key_to_offset(b, k),
			__btree_node_key_to_offset(b, end)
		};
	}
}

void bch2_btree_node_iter_push(struct btree_node_iter *iter,
			       struct btree *b,
			       const struct bkey_packed *k,
			       const struct bkey_packed *end)
{
	__bch2_btree_node_iter_push(iter, b, k, end);
	bch2_btree_node_iter_sort(iter, b);
}

noinline __flatten __cold
static void btree_node_iter_init_pack_failed(struct btree_node_iter *iter,
			      struct btree *b, struct bpos *search)
{
	struct bkey_packed *k;

	trace_bkey_pack_pos_fail(search);

	bch2_btree_node_iter_init_from_start(iter, b);

	while ((k = bch2_btree_node_iter_peek(iter, b)) &&
	       bkey_iter_pos_cmp(b, k, search) < 0)
		bch2_btree_node_iter_advance(iter, b);
}

/**
 * bch2_btree_node_iter_init - initialize a btree node iterator, starting from a
 * given position
 *
 * @iter:	iterator to initialize
 * @b:		btree node to search
 * @search:	search key
 *
 * Main entry point to the lookup code for individual btree nodes:
 *
 * NOTE:
 *
 * When you don't filter out deleted keys, btree nodes _do_ contain duplicate
 * keys. This doesn't matter for most code, but it does matter for lookups.
 *
 * Some adjacent keys with a string of equal keys:
 *	i j k k k k l m
 *
 * If you search for k, the lookup code isn't guaranteed to return you any
 * specific k. The lookup code is conceptually doing a binary search and
 * iterating backwards is very expensive so if the pivot happens to land at the
 * last k that's what you'll get.
 *
 * This works out ok, but it's something to be aware of:
 *
 *  - For non extents, we guarantee that the live key comes last - see
 *    btree_node_iter_cmp(), keys_out_of_order(). So the duplicates you don't
 *    see will only be deleted keys you don't care about.
 *
 *  - For extents, deleted keys sort last (see the comment at the top of this
 *    file). But when you're searching for extents, you actually want the first
 *    key strictly greater than your search key - an extent that compares equal
 *    to the search key is going to have 0 sectors after the search key.
 *
 *    But this does mean that we can't just search for
 *    bpos_successor(start_of_range) to get the first extent that overlaps with
 *    the range we want - if we're unlucky and there's an extent that ends
 *    exactly where we searched, then there could be a deleted key at the same
 *    position and we'd get that when we search instead of the preceding extent
 *    we needed.
 *
 *    So we've got to search for start_of_range, then after the lookup iterate
 *    past any extents that compare equal to the position we searched for.
 */
__flatten
void bch2_btree_node_iter_init(struct btree_node_iter *iter,
			       struct btree *b, struct bpos *search)
{
	struct bkey_packed p, *packed_search = NULL;
	struct btree_node_iter_set *pos = iter->data;
	struct bkey_packed *k[MAX_BSETS];
	unsigned i;

	EBUG_ON(bpos_lt(*search, b->data->min_key));
	EBUG_ON(bpos_gt(*search, b->data->max_key));
	bset_aux_tree_verify(b);

	memset(iter, 0, sizeof(*iter));

	switch (bch2_bkey_pack_pos_lossy(&p, *search, b)) {
	case BKEY_PACK_POS_EXACT:
		packed_search = &p;
		break;
	case BKEY_PACK_POS_SMALLER:
		packed_search = NULL;
		break;
	case BKEY_PACK_POS_FAIL:
		btree_node_iter_init_pack_failed(iter, b, search);
		return;
	}

	for (i = 0; i < b->nsets; i++) {
		k[i] = __bch2_bset_search(b, b->set + i, search, &p);
		prefetch_four_cachelines(k[i]);
	}

	for (i = 0; i < b->nsets; i++) {
		struct bset_tree *t = b->set + i;
		struct bkey_packed *end = btree_bkey_last(b, t);

		k[i] = bch2_bset_search_linear(b, t, search,
					       packed_search, &p, k[i]);
		if (k[i] != end)
			*pos++ = (struct btree_node_iter_set) {
				__btree_node_key_to_offset(b, k[i]),
				__btree_node_key_to_offset(b, end)
			};
	}

	bch2_btree_node_iter_sort(iter, b);
}

void bch2_btree_node_iter_init_from_start(struct btree_node_iter *iter,
					  struct btree *b)
{
	memset(iter, 0, sizeof(*iter));

	for_each_bset(b, t)
		__bch2_btree_node_iter_push(iter, b,
					   btree_bkey_first(b, t),
					   btree_bkey_last(b, t));
	bch2_btree_node_iter_sort(iter, b);
}

struct bkey_packed *bch2_btree_node_iter_bset_pos(struct btree_node_iter *iter,
						  struct btree *b,
						  struct bset_tree *t)
{
	struct btree_node_iter_set *set;

	btree_node_iter_for_each(iter, set)
		if (set->end == t->end_offset)
			return __btree_node_offset_to_key(b, set->k);

	return btree_bkey_last(b, t);
}

static inline bool btree_node_iter_sort_two(struct btree_node_iter *iter,
					    struct btree *b,
					    unsigned first)
{
	bool ret;

	if ((ret = (btree_node_iter_cmp(b,
					iter->data[first],
					iter->data[first + 1]) > 0)))
		swap(iter->data[first], iter->data[first + 1]);
	return ret;
}

void bch2_btree_node_iter_sort(struct btree_node_iter *iter,
			       struct btree *b)
{
	/* unrolled bubble sort: */

	if (!__btree_node_iter_set_end(iter, 2)) {
		btree_node_iter_sort_two(iter, b, 0);
		btree_node_iter_sort_two(iter, b, 1);
	}

	if (!__btree_node_iter_set_end(iter, 1))
		btree_node_iter_sort_two(iter, b, 0);
}

void bch2_btree_node_iter_set_drop(struct btree_node_iter *iter,
				   struct btree_node_iter_set *set)
{
	struct btree_node_iter_set *last =
		iter->data + ARRAY_SIZE(iter->data) - 1;

	memmove(&set[0], &set[1], (void *) last - (void *) set);
	*last = (struct btree_node_iter_set) { 0, 0 };
}

static inline void __bch2_btree_node_iter_advance(struct btree_node_iter *iter,
						  struct btree *b)
{
	iter->data->k += __bch2_btree_node_iter_peek_all(iter, b)->u64s;

	EBUG_ON(iter->data->k > iter->data->end);

	if (unlikely(__btree_node_iter_set_end(iter, 0))) {
		/* avoid an expensive memmove call: */
		iter->data[0] = iter->data[1];
		iter->data[1] = iter->data[2];
		iter->data[2] = (struct btree_node_iter_set) { 0, 0 };
		return;
	}

	if (__btree_node_iter_set_end(iter, 1))
		return;

	if (!btree_node_iter_sort_two(iter, b, 0))
		return;

	if (__btree_node_iter_set_end(iter, 2))
		return;

	btree_node_iter_sort_two(iter, b, 1);
}

void bch2_btree_node_iter_advance(struct btree_node_iter *iter,
				  struct btree *b)
{
	if (bch2_expensive_debug_checks) {
		bch2_btree_node_iter_verify(iter, b);
		bch2_btree_node_iter_next_check(iter, b);
	}

	__bch2_btree_node_iter_advance(iter, b);
}

/*
 * Expensive:
 */
struct bkey_packed *bch2_btree_node_iter_prev_all(struct btree_node_iter *iter,
						  struct btree *b)
{
	struct bkey_packed *k, *prev = NULL;
	struct btree_node_iter_set *set;
	unsigned end = 0;

	if (bch2_expensive_debug_checks)
		bch2_btree_node_iter_verify(iter, b);

	for_each_bset(b, t) {
		k = bch2_bkey_prev_all(b, t,
			bch2_btree_node_iter_bset_pos(iter, b, t));
		if (k &&
		    (!prev || bkey_iter_cmp(b, k, prev) > 0)) {
			prev = k;
			end = t->end_offset;
		}
	}

	if (!prev)
		return NULL;

	/*
	 * We're manually memmoving instead of just calling sort() to ensure the
	 * prev we picked ends up in slot 0 - sort won't necessarily put it
	 * there because of duplicate deleted keys:
	 */
	btree_node_iter_for_each(iter, set)
		if (set->end == end)
			goto found;

	BUG_ON(set != &iter->data[__btree_node_iter_used(iter)]);
found:
	BUG_ON(set >= iter->data + ARRAY_SIZE(iter->data));

	memmove(&iter->data[1],
		&iter->data[0],
		(void *) set - (void *) &iter->data[0]);

	iter->data[0].k = __btree_node_key_to_offset(b, prev);
	iter->data[0].end = end;

	if (bch2_expensive_debug_checks)
		bch2_btree_node_iter_verify(iter, b);
	return prev;
}

struct bkey_packed *bch2_btree_node_iter_prev(struct btree_node_iter *iter,
					      struct btree *b)
{
	struct bkey_packed *prev;

	do {
		prev = bch2_btree_node_iter_prev_all(iter, b);
	} while (prev && bkey_deleted(prev));

	return prev;
}

struct bkey_s_c bch2_btree_node_iter_peek_unpack(struct btree_node_iter *iter,
						 struct btree *b,
						 struct bkey *u)
{
	struct bkey_packed *k = bch2_btree_node_iter_peek(iter, b);

	return k ? bkey_disassemble(b, k, u) : bkey_s_c_null;
}

/* Mergesort */

void bch2_btree_keys_stats(const struct btree *b, struct bset_stats *stats)
{
	for_each_bset_c(b, t) {
		enum bset_aux_tree_type type = bset_aux_tree_type(t);
		size_t j;

		stats->sets[type].nr++;
		stats->sets[type].bytes += le16_to_cpu(bset(b, t)->u64s) *
			sizeof(u64);

		if (bset_has_ro_aux_tree(t)) {
			stats->floats += t->size - 1;

			for (j = 1; j < t->size; j++)
				stats->failed +=
					bkey_float(b, t, j)->exponent ==
					BFLOAT_FAILED;
		}
	}
}

void bch2_bfloat_to_text(struct printbuf *out, struct btree *b,
			 struct bkey_packed *k)
{
	struct bset_tree *t = bch2_bkey_to_bset(b, k);
	struct bkey uk;
	unsigned j, inorder;

	if (!bset_has_ro_aux_tree(t))
		return;

	inorder = bkey_to_cacheline(b, t, k);
	if (!inorder || inorder >= t->size)
		return;

	j = __inorder_to_eytzinger1(inorder, t->size - 1, t->extra);
	if (k != tree_to_bkey(b, t, j))
		return;

	switch (bkey_float(b, t, j)->exponent) {
	case BFLOAT_FAILED:
		uk = bkey_unpack_key(b, k);
		prt_printf(out,
		       "    failed unpacked at depth %u\n"
		       "\t",
		       ilog2(j));
		bch2_bpos_to_text(out, uk.p);
		prt_printf(out, "\n");
		break;
	}
}
