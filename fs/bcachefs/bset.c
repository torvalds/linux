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

void bch2_dump_bset(struct btree *b, struct bset *i, unsigned set)
{
	struct bkey_packed *_k, *_n;
	struct bkey k, n;
	char buf[120];

	if (!i->u64s)
		return;

	for (_k = i->start, k = bkey_unpack_key(b, _k);
	     _k < vstruct_last(i);
	     _k = _n, k = n) {
		_n = bkey_next(_k);

		bch2_bkey_to_text(&PBUF(buf), &k);
		printk(KERN_ERR "block %u key %5u: %s\n", set,
		       __btree_node_key_to_offset(b, _k), buf);

		if (_n == vstruct_last(i))
			continue;

		n = bkey_unpack_key(b, _n);

		if (bkey_cmp(bkey_start_pos(&n), k.p) < 0) {
			printk(KERN_ERR "Key skipped backwards\n");
			continue;
		}

		/*
		 * Weird check for duplicate non extent keys: extents are
		 * deleted iff they have 0 size, so if it has zero size and it's
		 * not deleted these aren't extents:
		 */
		if (((!k.size && !bkey_deleted(&k)) ||
		     (!n.size && !bkey_deleted(&n))) &&
		    !bkey_deleted(&k) &&
		    !bkey_cmp(n.p, k.p))
			printk(KERN_ERR "Duplicate keys\n");
	}
}

void bch2_dump_btree_node(struct btree *b)
{
	struct bset_tree *t;

	console_lock();
	for_each_bset(b, t)
		bch2_dump_bset(b, bset(b, t), t - b->set);
	console_unlock();
}

void bch2_dump_btree_node_iter(struct btree *b,
			      struct btree_node_iter *iter)
{
	struct btree_node_iter_set *set;

	printk(KERN_ERR "btree node iter with %u/%u sets:\n",
	       __btree_node_iter_used(iter), b->nsets);

	btree_node_iter_for_each(iter, set) {
		struct bkey_packed *k = __btree_node_offset_to_key(b, set->k);
		struct bset_tree *t = bch2_bkey_to_bset(b, k);
		struct bkey uk = bkey_unpack_key(b, k);
		char buf[100];

		bch2_bkey_to_text(&PBUF(buf), &uk);
		printk(KERN_ERR "set %zu key %u: %s\n",
		       t - b->set, set->k, buf);
	}
}

#ifdef CONFIG_BCACHEFS_DEBUG

void __bch2_verify_btree_nr_keys(struct btree *b)
{
	struct bset_tree *t;
	struct bkey_packed *k;
	struct btree_nr_keys nr = { 0 };

	for_each_bset(b, t)
		for (k = btree_bkey_first(b, t);
		     k != btree_bkey_last(b, t);
		     k = bkey_next(k))
			if (!bkey_whiteout(k))
				btree_keys_account_key_add(&nr, t - b->set, k);

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
		char buf1[80], buf2[80];

		bch2_dump_btree_node(b);
		bch2_bkey_to_text(&PBUF(buf1), &ku);
		bch2_bkey_to_text(&PBUF(buf2), &nu);
		printk(KERN_ERR "out of order/overlapping:\n%s\n%s\n",
		       buf1, buf2);
		printk(KERN_ERR "iter was:");

		btree_node_iter_for_each(_iter, set) {
			struct bkey_packed *k = __btree_node_offset_to_key(b, set->k);
			struct bset_tree *t = bch2_bkey_to_bset(b, k);
			printk(" [%zi %zi]", t - b->set,
			       k->_data - bset(b, t)->_data);
		}
		panic("\n");
	}
}

void bch2_btree_node_iter_verify(struct btree_node_iter *iter,
				 struct btree *b)
{
	struct btree_node_iter_set *set, *s2;
	struct bkey_packed *k, *p;
	struct bset_tree *t;

	if (bch2_btree_node_iter_end(iter))
		return;

	/* Verify no duplicates: */
	btree_node_iter_for_each(iter, set)
		btree_node_iter_for_each(iter, s2)
			BUG_ON(set != s2 && set->end == s2->end);

	/* Verify that set->end is correct: */
	btree_node_iter_for_each(iter, set) {
		for_each_bset(b, t)
			if (set->end == t->end_offset)
				goto found;
		BUG();
found:
		BUG_ON(set->k < btree_bkey_first_offset(t) ||
		       set->k >= t->end_offset);
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
	struct bkey_packed *next = (void *) (where->_data + clobber_u64s);
#if 0
	BUG_ON(prev &&
	       bkey_iter_cmp(b, prev, insert) > 0);
#else
	if (prev &&
	    bkey_iter_cmp(b, prev, insert) > 0) {
		struct bkey k1 = bkey_unpack_key(b, prev);
		struct bkey k2 = bkey_unpack_key(b, insert);
		char buf1[100];
		char buf2[100];

		bch2_dump_btree_node(b);
		bch2_bkey_to_text(&PBUF(buf1), &k1);
		bch2_bkey_to_text(&PBUF(buf2), &k2);

		panic("prev > insert:\n"
		      "prev    key %5u %s\n"
		      "insert  key %5u %s\n",
		       __btree_node_key_to_offset(b, prev), buf1,
		       __btree_node_key_to_offset(b, insert), buf2);
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
		char buf1[100];
		char buf2[100];

		bch2_dump_btree_node(b);
		bch2_bkey_to_text(&PBUF(buf1), &k1);
		bch2_bkey_to_text(&PBUF(buf2), &k2);

		panic("insert > next:\n"
		      "insert  key %5u %s\n"
		      "next    key %5u %s\n",
		       __btree_node_key_to_offset(b, insert), buf1,
		       __btree_node_key_to_offset(b, next), buf2);
	}
#endif
}

#else

static inline void bch2_btree_node_iter_next_check(struct btree_node_iter *iter,
						   struct btree *b) {}

#endif

/* Auxiliary search trees */

#define BFLOAT_FAILED_UNPACKED	(U8_MAX - 0)
#define BFLOAT_FAILED_PREV	(U8_MAX - 1)
#define BFLOAT_FAILED_OVERFLOW	(U8_MAX - 2)
#define BFLOAT_FAILED		(U8_MAX - 2)

#define KEY_WORDS		BITS_TO_LONGS(1 << BKEY_EXPONENT_BITS)

struct bkey_float {
	u8		exponent;
	u8		key_offset;
	union {
		u32	mantissa32;
	struct {
		u16	mantissa16;
		u16	_pad;
	};
	};
} __packed;

#define BFLOAT_32BIT_NR		32U

static unsigned bkey_float_byte_offset(unsigned idx)
{
	int d = (idx - BFLOAT_32BIT_NR) << 1;

	d &= ~(d >> 31);

	return idx * 6 - d;
}

struct ro_aux_tree {
	struct bkey_float	_d[0];
};

struct rw_aux_tree {
	u16		offset;
	struct bpos	k;
};

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

#define BSET_CACHELINE		128

/* Space required for the btree node keys */
static inline size_t btree_keys_bytes(struct btree *b)
{
	return PAGE_SIZE << b->page_order;
}

static inline size_t btree_keys_cachelines(struct btree *b)
{
	return btree_keys_bytes(b) / BSET_CACHELINE;
}

static inline size_t btree_aux_data_bytes(struct btree *b)
{
	return btree_keys_cachelines(b) * 8;
}

static inline size_t btree_aux_data_u64s(struct btree *b)
{
	return btree_aux_data_bytes(b) / sizeof(u64);
}

static unsigned bset_aux_tree_buf_end(const struct bset_tree *t)
{
	BUG_ON(t->aux_data_offset == U16_MAX);

	switch (bset_aux_tree_type(t)) {
	case BSET_NO_AUX_TREE:
		return t->aux_data_offset;
	case BSET_RO_AUX_TREE:
		return t->aux_data_offset +
			DIV_ROUND_UP(bkey_float_byte_offset(t->size) +
				     sizeof(u8) * t->size, 8);
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

static u8 *ro_aux_tree_prev(const struct btree *b,
			    const struct bset_tree *t)
{
	EBUG_ON(bset_aux_tree_type(t) != BSET_RO_AUX_TREE);

	return __aux_tree_base(b, t) + bkey_float_byte_offset(t->size);
}

static struct bkey_float *bkey_float_get(struct ro_aux_tree *b,
					 unsigned idx)
{
	return (void *) b + bkey_float_byte_offset(idx);
}

static struct bkey_float *bkey_float(const struct btree *b,
				     const struct bset_tree *t,
				     unsigned idx)
{
	return bkey_float_get(ro_aux_tree_base(b, t), idx);
}

static void bset_aux_tree_verify(struct btree *b)
{
#ifdef CONFIG_BCACHEFS_DEBUG
	struct bset_tree *t;

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

/* Memory allocation */

void bch2_btree_keys_free(struct btree *b)
{
	kvfree(b->aux_data);
	b->aux_data = NULL;
}

int bch2_btree_keys_alloc(struct btree *b, unsigned page_order, gfp_t gfp)
{
	b->page_order	= page_order;
	b->aux_data	= kvmalloc(btree_aux_data_bytes(b), gfp);
	if (!b->aux_data)
		return -ENOMEM;

	return 0;
}

void bch2_btree_keys_init(struct btree *b, bool *expensive_debug_checks)
{
	unsigned i;

	b->nsets		= 0;
	memset(&b->nr, 0, sizeof(b->nr));
#ifdef CONFIG_BCACHEFS_DEBUG
	b->expensive_debug_checks = expensive_debug_checks;
#endif
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
			__eytzinger1_to_inorder(j, t->size, t->extra),
			bkey_float(b, t, j)->key_offset);
}

static struct bkey_packed *tree_to_prev_bkey(const struct btree *b,
					     const struct bset_tree *t,
					     unsigned j)
{
	unsigned prev_u64s = ro_aux_tree_prev(b, t)[j];

	return (void *) (tree_to_bkey(b, t, j)->_data - prev_u64s);
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

	if (!btree_keys_expensive_checks(b))
		return;

	BUG_ON(bset_has_ro_aux_tree(t));

	if (!bset_has_rw_aux_tree(t))
		return;

	BUG_ON(t->size < 1);
	BUG_ON(rw_aux_to_bkey(b, t, j) != k);

	goto start;
	while (1) {
		if (rw_aux_to_bkey(b, t, j) == k) {
			BUG_ON(bkey_cmp(rw_aux_tree(b, t)[j].k,
					bkey_unpack_pos(b, k)));
start:
			if (++j == t->size)
				break;

			BUG_ON(rw_aux_tree(b, t)[j].offset <=
			       rw_aux_tree(b, t)[j - 1].offset);
		}

		k = bkey_next(k);
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

static inline unsigned bfloat_mantissa(const struct bkey_float *f,
				       unsigned idx)
{
	return idx < BFLOAT_32BIT_NR ? f->mantissa32 : f->mantissa16;
}

static inline void bfloat_mantissa_set(struct bkey_float *f,
				       unsigned idx, unsigned mantissa)
{
	if (idx < BFLOAT_32BIT_NR)
		f->mantissa32 = mantissa;
	else
		f->mantissa16 = mantissa;
}

static inline unsigned bkey_mantissa(const struct bkey_packed *k,
				     const struct bkey_float *f,
				     unsigned idx)
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
	v >>= 64 - (f->exponent & 7) - (idx < BFLOAT_32BIT_NR ? 32 : 16);
#endif
	return idx < BFLOAT_32BIT_NR ? (u32) v : (u16) v;
}

static void make_bfloat(struct btree *b, struct bset_tree *t,
			unsigned j,
			struct bkey_packed *min_key,
			struct bkey_packed *max_key)
{
	struct bkey_float *f = bkey_float(b, t, j);
	struct bkey_packed *m = tree_to_bkey(b, t, j);
	struct bkey_packed *p = tree_to_prev_bkey(b, t, j);
	struct bkey_packed *l, *r;
	unsigned bits = j < BFLOAT_32BIT_NR ? 32 : 16;
	unsigned mantissa;
	int shift, exponent, high_bit;

	EBUG_ON(bkey_next(p) != m);

	if (is_power_of_2(j)) {
		l = min_key;

		if (!l->u64s) {
			if (!bkey_pack_pos(l, b->data->min_key, b)) {
				struct bkey_i tmp;

				bkey_init(&tmp.k);
				tmp.k.p = b->data->min_key;
				bkey_copy(l, &tmp);
			}
		}
	} else {
		l = tree_to_prev_bkey(b, t, j >> ffs(j));

		EBUG_ON(m < l);
	}

	if (is_power_of_2(j + 1)) {
		r = max_key;

		if (!r->u64s) {
			if (!bkey_pack_pos(r, t->max_key, b)) {
				struct bkey_i tmp;

				bkey_init(&tmp.k);
				tmp.k.p = t->max_key;
				bkey_copy(r, &tmp);
			}
		}
	} else {
		r = tree_to_bkey(b, t, j >> (ffz(j) + 1));

		EBUG_ON(m > r);
	}

	/*
	 * for failed bfloats, the lookup code falls back to comparing against
	 * the original key.
	 */

	if (!bkey_packed(l) || !bkey_packed(r) ||
	    !bkey_packed(p) || !bkey_packed(m) ||
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
		       min_t(unsigned, bits, b->nr_key_bits) - 1);
	exponent = high_bit - (bits - 1);

	/*
	 * Then we calculate the actual shift value, from the start of the key
	 * (k->_data), to get the key bits starting at exponent:
	 */
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
	shift = (int) (b->format.key_u64s * 64 - b->nr_key_bits) + exponent;

	EBUG_ON(shift + bits > b->format.key_u64s * 64);
#else
	shift = high_bit_offset +
		b->nr_key_bits -
		exponent -
		bits;

	EBUG_ON(shift < KEY_PACKED_BITS_START);
#endif
	EBUG_ON(shift < 0 || shift >= BFLOAT_FAILED);

	f->exponent = shift;
	mantissa = bkey_mantissa(m, f, j);

	/*
	 * If we've got garbage bits, set them to all 1s - it's legal for the
	 * bfloat to compare larger than the original key, but not smaller:
	 */
	if (exponent < 0)
		mantissa |= ~(~0U << -exponent);

	bfloat_mantissa_set(f, j, mantissa);

	/*
	 * The bfloat must be able to tell its key apart from the previous key -
	 * if its key and the previous key don't differ in the required bits,
	 * flag as failed - unless the keys are actually equal, in which case
	 * we aren't required to return a specific one:
	 */
	if (exponent > 0 &&
	    bfloat_mantissa(f, j) == bkey_mantissa(p, f, j) &&
	    bkey_cmp_packed(b, p, m)) {
		f->exponent = BFLOAT_FAILED_PREV;
		return;
	}

	/*
	 * f->mantissa must compare >= the original key - for transitivity with
	 * the comparison in bset_search_tree. If we're dropping set bits,
	 * increment it:
	 */
	if (exponent > (int) bch2_bkey_ffs(b, m)) {
		if (j < BFLOAT_32BIT_NR
		    ? f->mantissa32 == U32_MAX
		    : f->mantissa16 == U16_MAX)
			f->exponent = BFLOAT_FAILED_OVERFLOW;

		if (j < BFLOAT_32BIT_NR)
			f->mantissa32++;
		else
			f->mantissa16++;
	}
}

/* bytes remaining - only valid for last bset: */
static unsigned __bset_tree_capacity(struct btree *b, struct bset_tree *t)
{
	bset_aux_tree_verify(b);

	return btree_aux_data_bytes(b) - t->aux_data_offset * sizeof(u64);
}

static unsigned bset_ro_tree_capacity(struct btree *b, struct bset_tree *t)
{
	unsigned bytes = __bset_tree_capacity(b, t);

	if (bytes < 7 * BFLOAT_32BIT_NR)
		return bytes / 7;

	bytes -= 7 * BFLOAT_32BIT_NR;

	return BFLOAT_32BIT_NR + bytes / 5;
}

static unsigned bset_rw_tree_capacity(struct btree *b, struct bset_tree *t)
{
	return __bset_tree_capacity(b, t) / sizeof(struct rw_aux_tree);
}

static void __build_rw_aux_tree(struct btree *b, struct bset_tree *t)
{
	struct bkey_packed *k;

	t->size = 1;
	t->extra = BSET_RW_AUX_TREE_VAL;
	rw_aux_tree(b, t)[0].offset =
		__btree_node_key_to_offset(b, btree_bkey_first(b, t));

	for (k = btree_bkey_first(b, t);
	     k != btree_bkey_last(b, t);
	     k = bkey_next(k)) {
		if (t->size == bset_rw_tree_capacity(b, t))
			break;

		if ((void *) k - (void *) rw_aux_to_bkey(b, t, t->size - 1) >
		    L1_CACHE_BYTES)
			rw_aux_tree_set(b, t, t->size++, k);
	}
}

static void __build_ro_aux_tree(struct btree *b, struct bset_tree *t)
{
	struct bkey_packed *prev = NULL, *k = btree_bkey_first(b, t);
	struct bkey_packed min_key, max_key;
	unsigned j, cacheline = 1;

	/* signal to make_bfloat() that they're uninitialized: */
	min_key.u64s = max_key.u64s = 0;

	t->size = min(bkey_to_cacheline(b, t, btree_bkey_last(b, t)),
		      bset_ro_tree_capacity(b, t));
retry:
	if (t->size < 2) {
		t->size = 0;
		t->extra = BSET_NO_AUX_TREE_VAL;
		return;
	}

	t->extra = (t->size - rounddown_pow_of_two(t->size - 1)) << 1;

	/* First we figure out where the first key in each cacheline is */
	eytzinger1_for_each(j, t->size) {
		while (bkey_to_cacheline(b, t, k) < cacheline)
			prev = k, k = bkey_next(k);

		if (k >= btree_bkey_last(b, t)) {
			/* XXX: this path sucks */
			t->size--;
			goto retry;
		}

		ro_aux_tree_prev(b, t)[j] = prev->u64s;
		bkey_float(b, t, j)->key_offset =
			bkey_to_cacheline_offset(b, t, cacheline++, k);

		EBUG_ON(tree_to_prev_bkey(b, t, j) != prev);
		EBUG_ON(tree_to_bkey(b, t, j) != k);
	}

	while (bkey_next(k) != btree_bkey_last(b, t))
		k = bkey_next(k);

	t->max_key = bkey_unpack_pos(b, k);

	/* Then we build the tree */
	eytzinger1_for_each(j, t->size)
		make_bfloat(b, t, j, &min_key, &max_key);
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

void bch2_bset_init_next(struct bch_fs *c, struct btree *b,
			 struct btree_node_entry *bne)
{
	struct bset *i = &bne->keys;
	struct bset_tree *t;

	BUG_ON(bset_byte_offset(b, bne) >= btree_bytes(c));
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
							t->size, t->extra))
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
		for (i = p; i != k; i = bkey_next(i))
			if (i->type >= min_key_type)
				ret = i;

		k = p;
	}

	if (btree_keys_expensive_checks(b)) {
		BUG_ON(ret >= orig_k);

		for (i = ret ? bkey_next(ret) : btree_bkey_first(b, t);
		     i != orig_k;
		     i = bkey_next(i))
			BUG_ON(i->type >= min_key_type);
	}

	return ret;
}

/* Insert */

static void rw_aux_tree_fix_invalidated_key(struct btree *b,
					    struct bset_tree *t,
					    struct bkey_packed *k)
{
	unsigned offset = __btree_node_key_to_offset(b, k);
	unsigned j = rw_aux_tree_bsearch(b, t, offset);

	if (j < t->size &&
	    rw_aux_tree(b, t)[j].offset == offset)
		rw_aux_tree_set(b, t, j, k);

	bch2_bset_verify_rw_aux_tree(b, t);
}

static void ro_aux_tree_fix_invalidated_key(struct btree *b,
					    struct bset_tree *t,
					    struct bkey_packed *k)
{
	struct bkey_packed min_key, max_key;
	unsigned inorder, j;

	EBUG_ON(bset_aux_tree_type(t) != BSET_RO_AUX_TREE);

	/* signal to make_bfloat() that they're uninitialized: */
	min_key.u64s = max_key.u64s = 0;

	if (bkey_next(k) == btree_bkey_last(b, t)) {
		t->max_key = bkey_unpack_pos(b, k);

		for (j = 1; j < t->size; j = j * 2 + 1)
			make_bfloat(b, t, j, &min_key, &max_key);
	}

	inorder = bkey_to_cacheline(b, t, k);

	if (inorder &&
	    inorder < t->size) {
		j = __inorder_to_eytzinger1(inorder, t->size, t->extra);

		if (k == tree_to_bkey(b, t, j)) {
			/* Fix the node this key corresponds to */
			make_bfloat(b, t, j, &min_key, &max_key);

			/* Children for which this key is the right boundary */
			for (j = eytzinger1_left_child(j);
			     j < t->size;
			     j = eytzinger1_right_child(j))
				make_bfloat(b, t, j, &min_key, &max_key);
		}
	}

	if (inorder + 1 < t->size) {
		j = __inorder_to_eytzinger1(inorder + 1, t->size, t->extra);

		if (k == tree_to_prev_bkey(b, t, j)) {
			make_bfloat(b, t, j, &min_key, &max_key);

			/* Children for which this key is the left boundary */
			for (j = eytzinger1_right_child(j);
			     j < t->size;
			     j = eytzinger1_left_child(j))
				make_bfloat(b, t, j, &min_key, &max_key);
		}
	}
}

/**
 * bch2_bset_fix_invalidated_key() - given an existing  key @k that has been
 * modified, fix any auxiliary search tree by remaking all the nodes in the
 * auxiliary search tree that @k corresponds to
 */
void bch2_bset_fix_invalidated_key(struct btree *b, struct bkey_packed *k)
{
	struct bset_tree *t = bch2_bkey_to_bset_inlined(b, k);

	switch (bset_aux_tree_type(t)) {
	case BSET_NO_AUX_TREE:
		break;
	case BSET_RO_AUX_TREE:
		ro_aux_tree_fix_invalidated_key(b, t, k);
		break;
	case BSET_RW_AUX_TREE:
		rw_aux_tree_fix_invalidated_key(b, t, k);
		break;
	}
}

static void bch2_bset_fix_lookup_table(struct btree *b,
				       struct bset_tree *t,
				       struct bkey_packed *_where,
				       unsigned clobber_u64s,
				       unsigned new_u64s)
{
	int shift = new_u64s - clobber_u64s;
	unsigned l, j, where = __btree_node_key_to_offset(b, _where);

	EBUG_ON(bset_has_ro_aux_tree(t));

	if (!bset_has_rw_aux_tree(t))
		return;

	/* returns first entry >= where */
	l = rw_aux_tree_bsearch(b, t, where);

	if (!l) /* never delete first entry */
		l++;
	else if (l < t->size &&
		 where < t->end_offset &&
		 rw_aux_tree(b, t)[l].offset == where)
		rw_aux_tree_set(b, t, l++, _where);

	/* l now > where */

	for (j = l;
	     j < t->size &&
	     rw_aux_tree(b, t)[j].offset < where + clobber_u64s;
	     j++)
		;

	if (j < t->size &&
	    rw_aux_tree(b, t)[j].offset + shift ==
	    rw_aux_tree(b, t)[l - 1].offset)
		j++;

	memmove(&rw_aux_tree(b, t)[l],
		&rw_aux_tree(b, t)[j],
		(void *) &rw_aux_tree(b, t)[t->size] -
		(void *) &rw_aux_tree(b, t)[j]);
	t->size -= j - l;

	for (j = l; j < t->size; j++)
	       rw_aux_tree(b, t)[j].offset += shift;

	EBUG_ON(l < t->size &&
		rw_aux_tree(b, t)[l].offset ==
		rw_aux_tree(b, t)[l - 1].offset);

	if (t->size < bset_rw_tree_capacity(b, t) &&
	    (l < t->size
	     ? rw_aux_tree(b, t)[l].offset
	     : t->end_offset) -
	    rw_aux_tree(b, t)[l - 1].offset >
	    L1_CACHE_BYTES / sizeof(u64)) {
		struct bkey_packed *start = rw_aux_to_bkey(b, t, l - 1);
		struct bkey_packed *end = l < t->size
			? rw_aux_to_bkey(b, t, l)
			: btree_bkey_last(b, t);
		struct bkey_packed *k = start;

		while (1) {
			k = bkey_next(k);
			if (k == end)
				break;

			if ((void *) k - (void *) start >= L1_CACHE_BYTES) {
				memmove(&rw_aux_tree(b, t)[l + 1],
					&rw_aux_tree(b, t)[l],
					(void *) &rw_aux_tree(b, t)[t->size] -
					(void *) &rw_aux_tree(b, t)[l]);
				t->size++;
				rw_aux_tree_set(b, t, l, k);
				break;
			}
		}
	}

	bch2_bset_verify_rw_aux_tree(b, t);
	bset_aux_tree_verify(b);
}

void bch2_bset_insert(struct btree *b,
		      struct btree_node_iter *iter,
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

	if (!bkey_whiteout(&insert->k))
		btree_keys_account_key_add(&b->nr, t - b->set, src);

	if (src->u64s != clobber_u64s) {
		u64 *src_p = where->_data + clobber_u64s;
		u64 *dst_p = where->_data + src->u64s;

		EBUG_ON((int) le16_to_cpu(bset(b, t)->u64s) <
			(int) clobber_u64s - src->u64s);

		memmove_u64s(dst_p, src_p, btree_bkey_last(b, t)->_data - src_p);
		le16_add_cpu(&bset(b, t)->u64s, src->u64s - clobber_u64s);
		set_btree_bset_end(b, t);
	}

	memcpy_u64s(where, src,
		    bkeyp_key_u64s(f, src));
	memcpy_u64s(bkeyp_val(f, where), &insert->v,
		    bkeyp_val_u64s(f, src));

	bch2_bset_fix_lookup_table(b, t, where, clobber_u64s, src->u64s);

	bch2_verify_btree_nr_keys(b);
}

void bch2_bset_delete(struct btree *b,
		      struct bkey_packed *where,
		      unsigned clobber_u64s)
{
	struct bset_tree *t = bset_tree_last(b);
	u64 *src_p = where->_data + clobber_u64s;
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
				struct bpos *search,
				const struct bkey_packed *packed_search)
{
	unsigned l = 0, r = t->size;

	while (l + 1 != r) {
		unsigned m = (l + r) >> 1;

		if (bkey_cmp(rw_aux_tree(b, t)[m].k, *search) < 0)
			l = m;
		else
			r = m;
	}

	return rw_aux_to_bkey(b, t, l);
}

noinline
static int bset_search_tree_slowpath(const struct btree *b,
				struct bset_tree *t, struct bpos *search,
				const struct bkey_packed *packed_search,
				unsigned n)
{
	return bkey_cmp_p_or_unp(b, tree_to_bkey(b, t, n),
				 packed_search, search) < 0;
}

__flatten
static struct bkey_packed *bset_search_tree(const struct btree *b,
				struct bset_tree *t,
				struct bpos *search,
				const struct bkey_packed *packed_search)
{
	struct ro_aux_tree *base = ro_aux_tree_base(b, t);
	struct bkey_float *f = bkey_float_get(base, 1);
	void *p;
	unsigned inorder, n = 1;

	while (1) {
		if (likely(n << 4 < t->size)) {
			p = bkey_float_get(base, n << 4);
			prefetch(p);
		} else if (n << 3 < t->size) {
			inorder = __eytzinger1_to_inorder(n, t->size, t->extra);
			p = bset_cacheline(b, t, inorder);
#ifdef CONFIG_X86_64
			asm(".intel_syntax noprefix;"
			    "prefetcht0 [%0 - 127 + 64 * 0];"
			    "prefetcht0 [%0 - 127 + 64 * 1];"
			    "prefetcht0 [%0 - 127 + 64 * 2];"
			    "prefetcht0 [%0 - 127 + 64 * 3];"
			    ".att_syntax prefix;"
			    :
			    : "r" (p + 127));
#else
			prefetch(p + L1_CACHE_BYTES * 0);
			prefetch(p + L1_CACHE_BYTES * 1);
			prefetch(p + L1_CACHE_BYTES * 2);
			prefetch(p + L1_CACHE_BYTES * 3);
#endif
		} else if (n >= t->size)
			break;

		f = bkey_float_get(base, n);

		if (packed_search &&
		    likely(f->exponent < BFLOAT_FAILED))
			n = n * 2 + (bfloat_mantissa(f, n) <
				     bkey_mantissa(packed_search, f, n));
		else
			n = n * 2 + bset_search_tree_slowpath(b, t,
						search, packed_search, n);
	} while (n < t->size);

	inorder = __eytzinger1_to_inorder(n >> 1, t->size, t->extra);

	/*
	 * n would have been the node we recursed to - the low bit tells us if
	 * we recursed left or recursed right.
	 */
	if (n & 1) {
		return cacheline_to_bkey(b, t, inorder, f->key_offset);
	} else {
		if (--inorder) {
			n = eytzinger1_prev(n >> 1, t->size);
			f = bkey_float_get(base, n);
			return cacheline_to_bkey(b, t, inorder, f->key_offset);
		} else
			return btree_bkey_first(b, t);
	}
}

/*
 * Returns the first key greater than or equal to @search
 */
__always_inline __flatten
static struct bkey_packed *bch2_bset_search(struct btree *b,
				struct bset_tree *t,
				struct bpos *search,
				struct bkey_packed *packed_search,
				const struct bkey_packed *lossy_packed_search)
{
	struct bkey_packed *m;

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
		m = btree_bkey_first(b, t);
		break;
	case BSET_RW_AUX_TREE:
		m = bset_search_write_set(b, t, search, lossy_packed_search);
		break;
	case BSET_RO_AUX_TREE:
		/*
		 * Each node in the auxiliary search tree covers a certain range
		 * of bits, and keys above and below the set it covers might
		 * differ outside those bits - so we have to special case the
		 * start and end - handle that here:
		 */

		if (bkey_cmp(*search, t->max_key) > 0)
			return btree_bkey_last(b, t);

		m = bset_search_tree(b, t, search, lossy_packed_search);
		break;
	}

	if (lossy_packed_search)
		while (m != btree_bkey_last(b, t) &&
		       bkey_iter_cmp_p_or_unp(b, search, lossy_packed_search,
					      m) > 0)
			m = bkey_next(m);

	if (!packed_search)
		while (m != btree_bkey_last(b, t) &&
		       bkey_iter_pos_cmp(b, search, m) > 0)
			m = bkey_next(m);

	if (btree_keys_expensive_checks(b)) {
		struct bkey_packed *prev = bch2_bkey_prev_all(b, t, m);

		BUG_ON(prev &&
		       bkey_iter_cmp_p_or_unp(b, search, packed_search,
					      prev) <= 0);
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

noinline __flatten __attribute__((cold))
static void btree_node_iter_init_pack_failed(struct btree_node_iter *iter,
			      struct btree *b, struct bpos *search)
{
	struct bset_tree *t;

	trace_bkey_pack_pos_fail(search);

	for_each_bset(b, t)
		__bch2_btree_node_iter_push(iter, b,
			bch2_bset_search(b, t, search, NULL, NULL),
			btree_bkey_last(b, t));

	bch2_btree_node_iter_sort(iter, b);
}

/**
 * bch_btree_node_iter_init - initialize a btree node iterator, starting from a
 * given position
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
 *    bkey_successor(start_of_range) to get the first extent that overlaps with
 *    the range we want - if we're unlucky and there's an extent that ends
 *    exactly where we searched, then there could be a deleted key at the same
 *    position and we'd get that when we search instead of the preceding extent
 *    we needed.
 *
 *    So we've got to search for start_of_range, then after the lookup iterate
 *    past any extents that compare equal to the position we searched for.
 */
void bch2_btree_node_iter_init(struct btree_node_iter *iter,
			       struct btree *b, struct bpos *search)
{
	struct bset_tree *t;
	struct bkey_packed p, *packed_search = NULL;

	EBUG_ON(bkey_cmp(*search, b->data->min_key) < 0);
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

	for_each_bset(b, t)
		__bch2_btree_node_iter_push(iter, b,
					   bch2_bset_search(b, t, search,
							    packed_search, &p),
					   btree_bkey_last(b, t));

	bch2_btree_node_iter_sort(iter, b);
}

void bch2_btree_node_iter_init_from_start(struct btree_node_iter *iter,
					  struct btree *b)
{
	struct bset_tree *t;

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
		bch2_btree_node_iter_set_drop(iter, iter->data);
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
	if (btree_keys_expensive_checks(b)) {
		bch2_btree_node_iter_verify(iter, b);
		bch2_btree_node_iter_next_check(iter, b);
	}

	__bch2_btree_node_iter_advance(iter, b);
}

/*
 * Expensive:
 */
struct bkey_packed *bch2_btree_node_iter_prev_filter(struct btree_node_iter *iter,
						     struct btree *b,
						     unsigned min_key_type)
{
	struct bkey_packed *k, *prev = NULL;
	struct bkey_packed *orig_pos = bch2_btree_node_iter_peek_all(iter, b);
	struct btree_node_iter_set *set;
	struct bset_tree *t;
	unsigned end = 0;

	bch2_btree_node_iter_verify(iter, b);

	for_each_bset(b, t) {
		k = bch2_bkey_prev_filter(b, t,
			bch2_btree_node_iter_bset_pos(iter, b, t),
			min_key_type);
		if (k &&
		    (!prev || bkey_iter_cmp(b, k, prev) > 0)) {
			prev = k;
			end = t->end_offset;
		}
	}

	if (!prev)
		goto out;

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
out:
	if (btree_keys_expensive_checks(b)) {
		struct btree_node_iter iter2 = *iter;

		if (prev)
			__bch2_btree_node_iter_advance(&iter2, b);

		while ((k = bch2_btree_node_iter_peek_all(&iter2, b)) != orig_pos) {
			BUG_ON(k->type >= min_key_type);
			__bch2_btree_node_iter_advance(&iter2, b);
		}
	}

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

void bch2_btree_keys_stats(struct btree *b, struct bset_stats *stats)
{
	struct bset_tree *t;

	for_each_bset(b, t) {
		enum bset_aux_tree_type type = bset_aux_tree_type(t);
		size_t j;

		stats->sets[type].nr++;
		stats->sets[type].bytes += le16_to_cpu(bset(b, t)->u64s) *
			sizeof(u64);

		if (bset_has_ro_aux_tree(t)) {
			stats->floats += t->size - 1;

			for (j = 1; j < t->size; j++)
				switch (bkey_float(b, t, j)->exponent) {
				case BFLOAT_FAILED_UNPACKED:
					stats->failed_unpacked++;
					break;
				case BFLOAT_FAILED_PREV:
					stats->failed_prev++;
					break;
				case BFLOAT_FAILED_OVERFLOW:
					stats->failed_overflow++;
					break;
				}
		}
	}
}

void bch2_bfloat_to_text(struct printbuf *out, struct btree *b,
			 struct bkey_packed *k)
{
	struct bset_tree *t = bch2_bkey_to_bset(b, k);
	struct bkey_packed *l, *r, *p;
	struct bkey uk, up;
	char buf1[200], buf2[200];
	unsigned j, inorder;

	if (out->pos != out->end)
		*out->pos = '\0';

	if (!bset_has_ro_aux_tree(t))
		return;

	inorder = bkey_to_cacheline(b, t, k);
	if (!inorder || inorder >= t->size)
		return;

	j = __inorder_to_eytzinger1(inorder, t->size, t->extra);
	if (k != tree_to_bkey(b, t, j))
		return;

	switch (bkey_float(b, t, j)->exponent) {
	case BFLOAT_FAILED_UNPACKED:
		uk = bkey_unpack_key(b, k);
		pr_buf(out,
		       "    failed unpacked at depth %u\n"
		       "\t%llu:%llu\n",
		       ilog2(j),
		       uk.p.inode, uk.p.offset);
		break;
	case BFLOAT_FAILED_PREV:
		p = tree_to_prev_bkey(b, t, j);
		l = is_power_of_2(j)
			? btree_bkey_first(b, t)
			: tree_to_prev_bkey(b, t, j >> ffs(j));
		r = is_power_of_2(j + 1)
			? bch2_bkey_prev_all(b, t, btree_bkey_last(b, t))
			: tree_to_bkey(b, t, j >> (ffz(j) + 1));

		up = bkey_unpack_key(b, p);
		uk = bkey_unpack_key(b, k);
		bch2_to_binary(buf1, high_word(&b->format, p), b->nr_key_bits);
		bch2_to_binary(buf2, high_word(&b->format, k), b->nr_key_bits);

		pr_buf(out,
		       "    failed prev at depth %u\n"
		       "\tkey starts at bit %u but first differing bit at %u\n"
		       "\t%llu:%llu\n"
		       "\t%llu:%llu\n"
		       "\t%s\n"
		       "\t%s\n",
		       ilog2(j),
		       bch2_bkey_greatest_differing_bit(b, l, r),
		       bch2_bkey_greatest_differing_bit(b, p, k),
		       uk.p.inode, uk.p.offset,
		       up.p.inode, up.p.offset,
		       buf1, buf2);
		break;
	case BFLOAT_FAILED_OVERFLOW:
		uk = bkey_unpack_key(b, k);
		pr_buf(out,
		       "    failed overflow at depth %u\n"
		       "\t%llu:%llu\n",
		       ilog2(j),
		       uk.p.inode, uk.p.offset);
		break;
	}
}
