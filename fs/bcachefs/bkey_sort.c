// SPDX-License-Identifier: GPL-2.0
#include "bcachefs.h"
#include "bkey_buf.h"
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

static inline void sort_iter_sift(struct sort_iter *iter, unsigned from,
				  sort_cmp_fn cmp)
{
	unsigned i;

	for (i = from;
	     i + 1 < iter->used &&
	     cmp(iter->b, iter->data[i].k, iter->data[i + 1].k) > 0;
	     i++)
		swap(iter->data[i], iter->data[i + 1]);
}

static inline void sort_iter_sort(struct sort_iter *iter, sort_cmp_fn cmp)
{
	unsigned i = iter->used;

	while (i--)
		sort_iter_sift(iter, i, cmp);
}

static inline struct bkey_packed *sort_iter_peek(struct sort_iter *iter)
{
	return !sort_iter_end(iter) ? iter->data->k : NULL;
}

static inline void sort_iter_advance(struct sort_iter *iter, sort_cmp_fn cmp)
{
	struct sort_iter_set *i = iter->data;

	BUG_ON(!iter->used);

	i->k = bkey_next(i->k);

	BUG_ON(i->k > i->end);

	if (i->k == i->end)
		array_remove_item(iter->data, iter->used, 0);
	else
		sort_iter_sift(iter, 0, cmp);
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
	return bch2_bkey_cmp_packed(b, l, r) ?:
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
		!bch2_bkey_cmp_packed(iter->b,
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
		if (!bkey_deleted(k) &&
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
	if (!bkey_deleted(k.k)) {
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
		if (filter_whiteouts && bkey_deleted(in))
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
	struct bkey_buf k;
	struct btree_nr_keys nr;

	memset(&nr, 0, sizeof(nr));
	bch2_bkey_buf_init(&k);

	while ((k_packed = bch2_btree_node_iter_next_all(iter, src))) {
		if (filter_whiteouts && bkey_deleted(k_packed))
			continue;

		/*
		 * NOTE:
		 * bch2_bkey_normalize may modify the key we pass it (dropping
		 * stale pointers) and we don't have a write lock on the src
		 * node; we have to make a copy of the entire key before calling
		 * normalize
		 */
		bch2_bkey_buf_realloc(&k, c, k_packed->u64s + BKEY_U64s);
		bch2_bkey_unpack(src, k.k, k_packed);

		if (filter_whiteouts &&
		    bch2_bkey_normalize(c, bkey_i_to_s(k.k)))
			continue;

		extent_sort_append(c, out_f, &nr, &out, bkey_i_to_s(k.k));
	}

	dst->u64s = cpu_to_le16((u64 *) out - dst->_data);
	bch2_bkey_buf_exit(&k, c);
	return nr;
}

static inline int sort_keys_cmp(struct btree *b,
				struct bkey_packed *l,
				struct bkey_packed *r)
{
	return bch2_bkey_cmp_packed(b, l, r) ?:
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

		if (bkey_deleted(in) &&
		    (filter_whiteouts || !in->needs_whiteout))
			continue;

		while ((next = sort_iter_peek(iter)) &&
		       !bch2_bkey_cmp_packed(iter->b, in, next)) {
			BUG_ON(in->needs_whiteout &&
			       next->needs_whiteout);
			needs_whiteout |= in->needs_whiteout;
			in = sort_iter_next(iter, sort_keys_cmp);
		}

		if (bkey_deleted(in)) {
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
