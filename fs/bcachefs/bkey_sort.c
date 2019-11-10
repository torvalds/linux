// SPDX-License-Identifier: GPL-2.0
#include "bcachefs.h"
#include "bkey_on_stack.h"
#include "bkey_sort.h"
#include "bset.h"
#include "extents.h"

/* too many iterators, need to clean this up */

/* btree_node_iter_large: */

#define btree_node_iter_cmp_heap(h, _l, _r) btree_node_iter_cmp(b, _l, _r)

static inline bool
bch2_btree_node_iter_large_end(struct btree_node_iter_large *iter)
{
	return !iter->used;
}

static inline struct bkey_packed *
bch2_btree_node_iter_large_peek_all(struct btree_node_iter_large *iter,
				    struct btree *b)
{
	return bch2_btree_node_iter_large_end(iter)
		? NULL
		: __btree_node_offset_to_key(b, iter->data->k);
}

static void
bch2_btree_node_iter_large_advance(struct btree_node_iter_large *iter,
				   struct btree *b)
{
	iter->data->k += __btree_node_offset_to_key(b, iter->data->k)->u64s;

	EBUG_ON(!iter->used);
	EBUG_ON(iter->data->k > iter->data->end);

	if (iter->data->k == iter->data->end)
		heap_del(iter, 0, btree_node_iter_cmp_heap, NULL);
	else
		heap_sift_down(iter, 0, btree_node_iter_cmp_heap, NULL);
}

static inline struct bkey_packed *
bch2_btree_node_iter_large_next_all(struct btree_node_iter_large *iter,
				    struct btree *b)
{
	struct bkey_packed *ret = bch2_btree_node_iter_large_peek_all(iter, b);

	if (ret)
		bch2_btree_node_iter_large_advance(iter, b);

	return ret;
}

void bch2_btree_node_iter_large_push(struct btree_node_iter_large *iter,
				     struct btree *b,
				     const struct bkey_packed *k,
				     const struct bkey_packed *end)
{
	if (k != end) {
		struct btree_node_iter_set n =
			((struct btree_node_iter_set) {
				 __btree_node_key_to_offset(b, k),
				 __btree_node_key_to_offset(b, end)
			 });

		__heap_add(iter, n, btree_node_iter_cmp_heap, NULL);
	}
}

static void sort_key_next(struct btree_node_iter_large *iter,
			  struct btree *b,
			  struct btree_node_iter_set *i)
{
	i->k += __btree_node_offset_to_key(b, i->k)->u64s;

	while (i->k != i->end &&
	       !__btree_node_offset_to_key(b, i->k)->u64s)
		i->k++;

	if (i->k == i->end)
		*i = iter->data[--iter->used];
}

/* regular sort_iters */

typedef int (*sort_cmp_fn)(struct btree *,
			   struct bkey_packed *,
			   struct bkey_packed *);

static inline void __sort_iter_sift(struct sort_iter *iter,
				    unsigned from,
				    sort_cmp_fn cmp)
{
	unsigned i;

	for (i = from;
	     i + 1 < iter->used &&
	     cmp(iter->b, iter->data[i].k, iter->data[i + 1].k) > 0;
	     i++)
		swap(iter->data[i], iter->data[i + 1]);
}

static inline void sort_iter_sift(struct sort_iter *iter, sort_cmp_fn cmp)
{

	__sort_iter_sift(iter, 0, cmp);
}

static inline void sort_iter_sort(struct sort_iter *iter, sort_cmp_fn cmp)
{
	unsigned i = iter->used;

	while (i--)
		__sort_iter_sift(iter, i, cmp);
}

static inline struct bkey_packed *sort_iter_peek(struct sort_iter *iter)
{
	return iter->used ? iter->data->k : NULL;
}

static inline void sort_iter_advance(struct sort_iter *iter, sort_cmp_fn cmp)
{
	iter->data->k = bkey_next_skip_noops(iter->data->k, iter->data->end);

	BUG_ON(iter->data->k > iter->data->end);

	if (iter->data->k == iter->data->end)
		array_remove_item(iter->data, iter->used, 0);
	else
		sort_iter_sift(iter, cmp);
}

static inline struct bkey_packed *sort_iter_next(struct sort_iter *iter,
						 sort_cmp_fn cmp)
{
	struct bkey_packed *ret = sort_iter_peek(iter);

	if (ret)
		sort_iter_advance(iter, cmp);

	return ret;
}

/*
 * Returns true if l > r - unless l == r, in which case returns true if l is
 * older than r.
 *
 * Necessary for btree_sort_fixup() - if there are multiple keys that compare
 * equal in different sets, we have to process them newest to oldest.
 */
#define key_sort_cmp(h, l, r)						\
({									\
	bkey_cmp_packed(b,						\
			__btree_node_offset_to_key(b, (l).k),		\
			__btree_node_offset_to_key(b, (r).k))		\
									\
	?: (l).k - (r).k;						\
})

static inline bool should_drop_next_key(struct btree_node_iter_large *iter,
					struct btree *b)
{
	struct btree_node_iter_set *l = iter->data, *r = iter->data + 1;
	struct bkey_packed *k = __btree_node_offset_to_key(b, l->k);

	if (bkey_whiteout(k))
		return true;

	if (iter->used < 2)
		return false;

	if (iter->used > 2 &&
	    key_sort_cmp(iter, r[0], r[1]) >= 0)
		r++;

	/*
	 * key_sort_cmp() ensures that when keys compare equal the older key
	 * comes first; so if l->k compares equal to r->k then l->k is older and
	 * should be dropped.
	 */
	return !bkey_cmp_packed(b,
				__btree_node_offset_to_key(b, l->k),
				__btree_node_offset_to_key(b, r->k));
}

struct btree_nr_keys bch2_key_sort_fix_overlapping(struct bset *dst,
					struct btree *b,
					struct btree_node_iter_large *iter)
{
	struct bkey_packed *out = dst->start;
	struct btree_nr_keys nr;

	memset(&nr, 0, sizeof(nr));

	heap_resort(iter, key_sort_cmp, NULL);

	while (!bch2_btree_node_iter_large_end(iter)) {
		if (!should_drop_next_key(iter, b)) {
			struct bkey_packed *k =
				__btree_node_offset_to_key(b, iter->data->k);

			bkey_copy(out, k);
			btree_keys_account_key_add(&nr, 0, out);
			out = bkey_next(out);
		}

		sort_key_next(iter, b, iter->data);
		heap_sift_down(iter, 0, key_sort_cmp, NULL);
	}

	dst->u64s = cpu_to_le16((u64 *) out - dst->_data);
	return nr;
}

/*
 * If keys compare equal, compare by pointer order:
 *
 * Necessary for sort_fix_overlapping() - if there are multiple keys that
 * compare equal in different sets, we have to process them newest to oldest.
 */
#define extent_sort_cmp(h, l, r)					\
({									\
	struct bkey _ul = bkey_unpack_key(b,				\
				__btree_node_offset_to_key(b, (l).k));	\
	struct bkey _ur = bkey_unpack_key(b,				\
				__btree_node_offset_to_key(b, (r).k));	\
									\
	bkey_cmp(bkey_start_pos(&_ul),					\
		 bkey_start_pos(&_ur)) ?: (r).k - (l).k;		\
})

static inline void extent_sort_sift(struct btree_node_iter_large *iter,
				    struct btree *b, size_t i)
{
	heap_sift_down(iter, i, extent_sort_cmp, NULL);
}

static inline void extent_sort_next(struct btree_node_iter_large *iter,
				    struct btree *b,
				    struct btree_node_iter_set *i)
{
	sort_key_next(iter, b, i);
	heap_sift_down(iter, i - iter->data, extent_sort_cmp, NULL);
}

static void extent_sort_advance_prev(struct bkey_format *f,
				     struct btree_nr_keys *nr,
				     struct bkey_packed *start,
				     struct bkey_packed **prev)
{
	if (*prev) {
		bch2_bkey_pack(*prev, (void *) *prev, f);

		btree_keys_account_key_add(nr, 0, *prev);
		*prev = bkey_next(*prev);
	} else {
		*prev = start;
	}
}

static void extent_sort_append(struct bch_fs *c,
			       struct bkey_format *f,
			       struct btree_nr_keys *nr,
			       struct bkey_packed *start,
			       struct bkey_packed **prev,
			       struct bkey_s k)
{
	if (bkey_whiteout(k.k))
		return;

	/*
	 * prev is always unpacked, for key merging - until right before we
	 * advance it:
	 */

	if (*prev &&
	    bch2_bkey_merge(c, bkey_i_to_s((void *) *prev), k) ==
	    BCH_MERGE_MERGE)
		return;

	extent_sort_advance_prev(f, nr, start, prev);

	bkey_reassemble((void *) *prev, k.s_c);
}

struct btree_nr_keys bch2_extent_sort_fix_overlapping(struct bch_fs *c,
					struct bset *dst,
					struct btree *b,
					struct btree_node_iter_large *iter)
{
	struct bkey_format *f = &b->format;
	struct btree_node_iter_set *_l = iter->data, *_r;
	struct bkey_packed *prev = NULL, *lk, *rk;
	struct bkey l_unpacked, r_unpacked;
	struct bkey_s l, r;
	struct btree_nr_keys nr;
	struct bkey_on_stack split;

	memset(&nr, 0, sizeof(nr));
	bkey_on_stack_init(&split);

	heap_resort(iter, extent_sort_cmp, NULL);

	while (!bch2_btree_node_iter_large_end(iter)) {
		lk = __btree_node_offset_to_key(b, _l->k);
		l = __bkey_disassemble(b, lk, &l_unpacked);

		if (iter->used == 1) {
			extent_sort_append(c, f, &nr, dst->start, &prev, l);
			extent_sort_next(iter, b, _l);
			continue;
		}

		_r = iter->data + 1;
		if (iter->used > 2 &&
		    extent_sort_cmp(iter, _r[0], _r[1]) >= 0)
			_r++;

		rk = __btree_node_offset_to_key(b, _r->k);
		r = __bkey_disassemble(b, rk, &r_unpacked);

		/* If current key and next key don't overlap, just append */
		if (bkey_cmp(l.k->p, bkey_start_pos(r.k)) <= 0) {
			extent_sort_append(c, f, &nr, dst->start, &prev, l);
			extent_sort_next(iter, b, _l);
			continue;
		}

		/* Skip 0 size keys */
		if (!r.k->size) {
			extent_sort_next(iter, b, _r);
			continue;
		}

		/*
		 * overlap: keep the newer key and trim the older key so they
		 * don't overlap. comparing pointers tells us which one is
		 * newer, since the bsets are appended one after the other.
		 */

		/* can't happen because of comparison func */
		BUG_ON(_l->k < _r->k &&
		       !bkey_cmp(bkey_start_pos(l.k), bkey_start_pos(r.k)));

		if (_l->k > _r->k) {
			/* l wins, trim r */
			if (bkey_cmp(l.k->p, r.k->p) >= 0) {
				sort_key_next(iter, b, _r);
			} else {
				__bch2_cut_front(l.k->p, r);
				extent_save(b, rk, r.k);
			}

			extent_sort_sift(iter, b, _r - iter->data);
		} else if (bkey_cmp(l.k->p, r.k->p) > 0) {
			bkey_on_stack_realloc(&split, c, l.k->u64s);

			/*
			 * r wins, but it overlaps in the middle of l - split l:
			 */
			bkey_reassemble(split.k, l.s_c);
			bch2_cut_back(bkey_start_pos(r.k), &split.k->k);

			__bch2_cut_front(r.k->p, l);
			extent_save(b, lk, l.k);

			extent_sort_sift(iter, b, 0);

			extent_sort_append(c, f, &nr, dst->start,
					   &prev, bkey_i_to_s(split.k));
		} else {
			bch2_cut_back(bkey_start_pos(r.k), l.k);
			extent_save(b, lk, l.k);
		}
	}

	extent_sort_advance_prev(f, &nr, dst->start, &prev);

	dst->u64s = cpu_to_le16((u64 *) prev - dst->_data);

	bkey_on_stack_exit(&split, c);
	return nr;
}

/* Sort + repack in a new format: */
struct btree_nr_keys
bch2_sort_repack(struct bset *dst, struct btree *src,
		 struct btree_node_iter *src_iter,
		 struct bkey_format *out_f,
		 bool filter_whiteouts)
{
	struct bkey_format *in_f = &src->format;
	struct bkey_packed *in, *out = vstruct_last(dst);
	struct btree_nr_keys nr;

	memset(&nr, 0, sizeof(nr));

	while ((in = bch2_btree_node_iter_next_all(src_iter, src))) {
		if (filter_whiteouts && bkey_whiteout(in))
			continue;

		if (bch2_bkey_transform(out_f, out, bkey_packed(in)
				       ? in_f : &bch2_bkey_format_current, in))
			out->format = KEY_FORMAT_LOCAL_BTREE;
		else
			bch2_bkey_unpack(src, (void *) out, in);

		btree_keys_account_key_add(&nr, 0, out);
		out = bkey_next(out);
	}

	dst->u64s = cpu_to_le16((u64 *) out - dst->_data);
	return nr;
}

/* Sort, repack, and merge: */
struct btree_nr_keys
bch2_sort_repack_merge(struct bch_fs *c,
		       struct bset *dst, struct btree *src,
		       struct btree_node_iter *iter,
		       struct bkey_format *out_f,
		       bool filter_whiteouts)
{
	struct bkey_packed *prev = NULL, *k_packed;
	struct bkey_s k;
	struct btree_nr_keys nr;
	struct bkey unpacked;

	memset(&nr, 0, sizeof(nr));

	while ((k_packed = bch2_btree_node_iter_next_all(iter, src))) {
		if (filter_whiteouts && bkey_whiteout(k_packed))
			continue;

		k = __bkey_disassemble(src, k_packed, &unpacked);

		if (filter_whiteouts &&
		    bch2_bkey_normalize(c, k))
			continue;

		extent_sort_append(c, out_f, &nr, vstruct_last(dst), &prev, k);
	}

	extent_sort_advance_prev(out_f, &nr, vstruct_last(dst), &prev);

	dst->u64s = cpu_to_le16((u64 *) prev - dst->_data);
	return nr;
}

static inline int sort_keys_cmp(struct btree *b,
				struct bkey_packed *l,
				struct bkey_packed *r)
{
	return bkey_cmp_packed(b, l, r) ?:
		(int) bkey_whiteout(r) - (int) bkey_whiteout(l) ?:
		(int) l->needs_whiteout - (int) r->needs_whiteout;
}

unsigned bch2_sort_keys(struct bkey_packed *dst,
			struct sort_iter *iter,
			bool filter_whiteouts)
{
	const struct bkey_format *f = &iter->b->format;
	struct bkey_packed *in, *next, *out = dst;

	sort_iter_sort(iter, sort_keys_cmp);

	while ((in = sort_iter_next(iter, sort_keys_cmp))) {
		if (bkey_whiteout(in) &&
		    (filter_whiteouts || !in->needs_whiteout))
			continue;

		if (bkey_whiteout(in) &&
		    (next = sort_iter_peek(iter)) &&
		    !bkey_cmp_packed(iter->b, in, next)) {
			BUG_ON(in->needs_whiteout &&
			       next->needs_whiteout);
			/*
			 * XXX racy, called with read lock from write path
			 *
			 * leads to spurious BUG_ON() in bkey_unpack_key() in
			 * debug mode
			 */
			next->needs_whiteout |= in->needs_whiteout;
			continue;
		}

		if (bkey_whiteout(in)) {
			memcpy_u64s(out, in, bkeyp_key_u64s(f, in));
			set_bkeyp_val_u64s(f, out, 0);
		} else {
			bkey_copy(out, in);
		}
		out = bkey_next(out);
	}

	return (u64 *) out - (u64 *) dst;
}

static inline int sort_extents_cmp(struct btree *b,
				   struct bkey_packed *l,
				   struct bkey_packed *r)
{
	return bkey_cmp_packed(b, l, r) ?:
		(int) bkey_deleted(l) - (int) bkey_deleted(r);
}

unsigned bch2_sort_extents(struct bkey_packed *dst,
			   struct sort_iter *iter,
			   bool filter_whiteouts)
{
	struct bkey_packed *in, *out = dst;

	sort_iter_sort(iter, sort_extents_cmp);

	while ((in = sort_iter_next(iter, sort_extents_cmp))) {
		if (bkey_deleted(in))
			continue;

		if (bkey_whiteout(in) &&
		    (filter_whiteouts || !in->needs_whiteout))
			continue;

		bkey_copy(out, in);
		out = bkey_next(out);
	}

	return (u64 *) out - (u64 *) dst;
}

static inline int sort_key_whiteouts_cmp(struct btree *b,
					 struct bkey_packed *l,
					 struct bkey_packed *r)
{
	return bkey_cmp_packed(b, l, r);
}

unsigned bch2_sort_key_whiteouts(struct bkey_packed *dst,
				 struct sort_iter *iter)
{
	struct bkey_packed *in, *out = dst;

	sort_iter_sort(iter, sort_key_whiteouts_cmp);

	while ((in = sort_iter_next(iter, sort_key_whiteouts_cmp))) {
		bkey_copy(out, in);
		out = bkey_next(out);
	}

	return (u64 *) out - (u64 *) dst;
}

static inline int sort_extent_whiteouts_cmp(struct btree *b,
					    struct bkey_packed *l,
					    struct bkey_packed *r)
{
	struct bkey ul = bkey_unpack_key(b, l);
	struct bkey ur = bkey_unpack_key(b, r);

	return bkey_cmp(bkey_start_pos(&ul), bkey_start_pos(&ur));
}

unsigned bch2_sort_extent_whiteouts(struct bkey_packed *dst,
				    struct sort_iter *iter)
{
	const struct bkey_format *f = &iter->b->format;
	struct bkey_packed *in, *out = dst;
	struct bkey_i l, r;
	bool prev = false, l_packed = false;
	u64 max_packed_size	= bkey_field_max(f, BKEY_FIELD_SIZE);
	u64 max_packed_offset	= bkey_field_max(f, BKEY_FIELD_OFFSET);
	u64 new_size;

	max_packed_size = min_t(u64, max_packed_size, KEY_SIZE_MAX);

	sort_iter_sort(iter, sort_extent_whiteouts_cmp);

	while ((in = sort_iter_next(iter, sort_extent_whiteouts_cmp))) {
		if (bkey_deleted(in))
			continue;

		EBUG_ON(bkeyp_val_u64s(f, in));
		EBUG_ON(in->type != KEY_TYPE_discard);

		r.k = bkey_unpack_key(iter->b, in);

		if (prev &&
		    bkey_cmp(l.k.p, bkey_start_pos(&r.k)) >= 0) {
			if (bkey_cmp(l.k.p, r.k.p) >= 0)
				continue;

			new_size = l_packed
				? min(max_packed_size, max_packed_offset -
				      bkey_start_offset(&l.k))
				: KEY_SIZE_MAX;

			new_size = min(new_size, r.k.p.offset -
				       bkey_start_offset(&l.k));

			BUG_ON(new_size < l.k.size);

			bch2_key_resize(&l.k, new_size);

			if (bkey_cmp(l.k.p, r.k.p) >= 0)
				continue;

			bch2_cut_front(l.k.p, &r);
		}

		if (prev) {
			if (!bch2_bkey_pack(out, &l, f)) {
				BUG_ON(l_packed);
				bkey_copy(out, &l);
			}
			out = bkey_next(out);
		}

		l = r;
		prev = true;
		l_packed = bkey_packed(in);
	}

	if (prev) {
		if (!bch2_bkey_pack(out, &l, f)) {
			BUG_ON(l_packed);
			bkey_copy(out, &l);
		}
		out = bkey_next(out);
	}

	return (u64 *) out - (u64 *) dst;
}
