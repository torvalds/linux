// SPDX-License-Identifier: GPL-2.0
#include "bcachefs.h"
#include "bkey_on_stack.h"
#include "bkey_sort.h"
#include "bset.h"
#include "extents.h"

typedef int (*sort_cmp_fn)(struct btree *,
			   struct bkey_packed *,
			   struct bkey_packed *);

static inline bool sort_iter_end(struct sort_iter *iter)
{
	return !iter->used;
}

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
	return !sort_iter_end(iter) ? iter->data->k : NULL;
}

static inline void __sort_iter_advance(struct sort_iter *iter,
				       unsigned idx, sort_cmp_fn cmp)
{
	struct sort_iter_set *i = iter->data + idx;

	BUG_ON(idx >= iter->used);

	i->k = bkey_next_skip_noops(i->k, i->end);

	BUG_ON(i->k > i->end);

	if (i->k == i->end)
		array_remove_item(iter->data, iter->used, idx);
	else
		__sort_iter_sift(iter, idx, cmp);
}

static inline void sort_iter_advance(struct sort_iter *iter, sort_cmp_fn cmp)
{
	__sort_iter_advance(iter, 0, cmp);
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
 * If keys compare equal, compare by pointer order:
 */
static inline int key_sort_fix_overlapping_cmp(struct btree *b,
					       struct bkey_packed *l,
					       struct bkey_packed *r)
{
	return bkey_cmp_packed(b, l, r) ?:
		cmp_int((unsigned long) l, (unsigned long) r);
}

static inline bool should_drop_next_key(struct sort_iter *iter)
{
	/*
	 * key_sort_cmp() ensures that when keys compare equal the older key
	 * comes first; so if l->k compares equal to r->k then l->k is older
	 * and should be dropped.
	 */
	return iter->used >= 2 &&
		!bkey_cmp_packed(iter->b,
				 iter->data[0].k,
				 iter->data[1].k);
}

struct btree_nr_keys
bch2_key_sort_fix_overlapping(struct bch_fs *c, struct bset *dst,
			      struct sort_iter *iter)
{
	struct bkey_packed *out = dst->start;
	struct bkey_packed *k;
	struct btree_nr_keys nr;

	memset(&nr, 0, sizeof(nr));

	sort_iter_sort(iter, key_sort_fix_overlapping_cmp);

	while ((k = sort_iter_peek(iter))) {
		if (!bkey_whiteout(k) &&
		    !should_drop_next_key(iter)) {
			bkey_copy(out, k);
			btree_keys_account_key_add(&nr, 0, out);
			out = bkey_next(out);
		}

		sort_iter_advance(iter, key_sort_fix_overlapping_cmp);
	}

	dst->u64s = cpu_to_le16((u64 *) out - dst->_data);
	return nr;
}

static void extent_sort_append(struct bch_fs *c,
			       struct bkey_format *f,
			       struct btree_nr_keys *nr,
			       struct bkey_packed **out,
			       struct bkey_s k)
{
	if (!bkey_whiteout(k.k)) {
		if (!bch2_bkey_pack_key(*out, k.k, f))
			memcpy_u64s_small(*out, k.k, BKEY_U64s);

		memcpy_u64s_small(bkeyp_val(f, *out), k.v, bkey_val_u64s(k.k));

		btree_keys_account_key_add(nr, 0, *out);
		*out = bkey_next(*out);
	}
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

/* Sort, repack, and call bch2_bkey_normalize() to drop stale pointers: */
struct btree_nr_keys
bch2_sort_repack_merge(struct bch_fs *c,
		       struct bset *dst, struct btree *src,
		       struct btree_node_iter *iter,
		       struct bkey_format *out_f,
		       bool filter_whiteouts)
{
	struct bkey_packed *out = vstruct_last(dst), *k_packed;
	struct bkey_on_stack k;
	struct btree_nr_keys nr;

	memset(&nr, 0, sizeof(nr));
	bkey_on_stack_init(&k);

	while ((k_packed = bch2_btree_node_iter_next_all(iter, src))) {
		if (filter_whiteouts && bkey_whiteout(k_packed))
			continue;

		/*
		 * NOTE:
		 * bch2_bkey_normalize may modify the key we pass it (dropping
		 * stale pointers) and we don't have a write lock on the src
		 * node; we have to make a copy of the entire key before calling
		 * normalize
		 */
		bkey_on_stack_realloc(&k, c, k_packed->u64s + BKEY_U64s);
		bch2_bkey_unpack(src, k.k, k_packed);

		if (filter_whiteouts &&
		    bch2_bkey_normalize(c, bkey_i_to_s(k.k)))
			continue;

		extent_sort_append(c, out_f, &nr, &out, bkey_i_to_s(k.k));
	}

	dst->u64s = cpu_to_le16((u64 *) out - dst->_data);
	bkey_on_stack_exit(&k, c);
	return nr;
}

static inline int sort_keys_cmp(struct btree *b,
				struct bkey_packed *l,
				struct bkey_packed *r)
{
	return bkey_cmp_packed(b, l, r) ?:
		(int) bkey_deleted(r) - (int) bkey_deleted(l) ?:
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
		bool needs_whiteout = false;

		if (bkey_whiteout(in) &&
		    (filter_whiteouts || !in->needs_whiteout))
			continue;

		while ((next = sort_iter_peek(iter)) &&
		       !bkey_cmp_packed(iter->b, in, next)) {
			BUG_ON(in->needs_whiteout &&
			       next->needs_whiteout);
			needs_whiteout |= in->needs_whiteout;
			in = sort_iter_next(iter, sort_keys_cmp);
		}

		if (bkey_whiteout(in)) {
			memcpy_u64s(out, in, bkeyp_key_u64s(f, in));
			set_bkeyp_val_u64s(f, out, 0);
		} else {
			bkey_copy(out, in);
		}
		out->needs_whiteout |= needs_whiteout;
		out = bkey_next(out);
	}

	return (u64 *) out - (u64 *) dst;
}

/* Compat code for btree_node_old_extent_overwrite: */

/*
 * If keys compare equal, compare by pointer order:
 *
 * Necessary for sort_fix_overlapping() - if there are multiple keys that
 * compare equal in different sets, we have to process them newest to oldest.
 */
static inline int extent_sort_fix_overlapping_cmp(struct btree *b,
						  struct bkey_packed *l,
						  struct bkey_packed *r)
{
	struct bkey ul = bkey_unpack_key(b, l);
	struct bkey ur = bkey_unpack_key(b, r);

	return bkey_cmp(bkey_start_pos(&ul),
			bkey_start_pos(&ur)) ?:
		cmp_int((unsigned long) r, (unsigned long) l);
}

/*
 * The algorithm in extent_sort_fix_overlapping() relies on keys in the same
 * bset being ordered by start offset - but 0 size whiteouts (which are always
 * KEY_TYPE_deleted) break this ordering, so we need to skip over them:
 */
static void extent_iter_advance(struct sort_iter *iter, unsigned idx)
{
	struct sort_iter_set *i = iter->data + idx;

	do {
		i->k = bkey_next_skip_noops(i->k, i->end);
	} while (i->k != i->end && bkey_deleted(i->k));

	if (i->k == i->end)
		array_remove_item(iter->data, iter->used, idx);
	else
		__sort_iter_sift(iter, idx, extent_sort_fix_overlapping_cmp);
}

struct btree_nr_keys
bch2_extent_sort_fix_overlapping(struct bch_fs *c, struct bset *dst,
				 struct sort_iter *iter)
{
	struct btree *b = iter->b;
	struct bkey_format *f = &b->format;
	struct sort_iter_set *_l = iter->data, *_r = iter->data + 1;
	struct bkey_packed *out = dst->start;
	struct bkey l_unpacked, r_unpacked;
	struct bkey_s l, r;
	struct btree_nr_keys nr;
	struct bkey_on_stack split;
	unsigned i;

	memset(&nr, 0, sizeof(nr));
	bkey_on_stack_init(&split);

	sort_iter_sort(iter, extent_sort_fix_overlapping_cmp);
	for (i = 0; i < iter->used;) {
		if (bkey_deleted(iter->data[i].k))
			__sort_iter_advance(iter, i,
					    extent_sort_fix_overlapping_cmp);
		else
			i++;
	}

	while (!sort_iter_end(iter)) {
		l = __bkey_disassemble(b, _l->k, &l_unpacked);

		if (iter->used == 1) {
			extent_sort_append(c, f, &nr, &out, l);
			extent_iter_advance(iter, 0);
			continue;
		}

		r = __bkey_disassemble(b, _r->k, &r_unpacked);

		/* If current key and next key don't overlap, just append */
		if (bkey_cmp(l.k->p, bkey_start_pos(r.k)) <= 0) {
			extent_sort_append(c, f, &nr, &out, l);
			extent_iter_advance(iter, 0);
			continue;
		}

		/* Skip 0 size keys */
		if (!r.k->size) {
			extent_iter_advance(iter, 1);
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
				extent_iter_advance(iter, 1);
			} else {
				bch2_cut_front_s(l.k->p, r);
				extent_save(b, _r->k, r.k);
				__sort_iter_sift(iter, 1,
					 extent_sort_fix_overlapping_cmp);
			}
		} else if (bkey_cmp(l.k->p, r.k->p) > 0) {

			/*
			 * r wins, but it overlaps in the middle of l - split l:
			 */
			bkey_on_stack_reassemble(&split, c, l.s_c);
			bch2_cut_back(bkey_start_pos(r.k), split.k);

			bch2_cut_front_s(r.k->p, l);
			extent_save(b, _l->k, l.k);

			__sort_iter_sift(iter, 0,
					 extent_sort_fix_overlapping_cmp);

			extent_sort_append(c, f, &nr, &out,
					   bkey_i_to_s(split.k));
		} else {
			bch2_cut_back_s(bkey_start_pos(r.k), l);
			extent_save(b, _l->k, l.k);
		}
	}

	dst->u64s = cpu_to_le16((u64 *) out - dst->_data);

	bkey_on_stack_exit(&split, c);
	return nr;
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
