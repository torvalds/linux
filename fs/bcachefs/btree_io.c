// SPDX-License-Identifier: GPL-2.0

#include "bcachefs.h"
#include "bkey_methods.h"
#include "bkey_sort.h"
#include "btree_cache.h"
#include "btree_io.h"
#include "btree_iter.h"
#include "btree_locking.h"
#include "btree_update.h"
#include "btree_update_interior.h"
#include "buckets.h"
#include "checksum.h"
#include "debug.h"
#include "error.h"
#include "extents.h"
#include "io.h"
#include "journal_reclaim.h"
#include "journal_seq_blacklist.h"
#include "super-io.h"
#include "trace.h"

static void verify_no_dups(struct btree *b,
			   struct bkey_packed *start,
			   struct bkey_packed *end)
{
#ifdef CONFIG_BCACHEFS_DEBUG
	struct bkey_packed *k;

	for (k = start; k != end && bkey_next(k) != end; k = bkey_next(k)) {
		struct bkey l = bkey_unpack_key(b, k);
		struct bkey r = bkey_unpack_key(b, bkey_next(k));

		BUG_ON(btree_node_is_extents(b)
		       ? bkey_cmp(l.p, bkey_start_pos(&r)) > 0
		       : bkey_cmp(l.p, bkey_start_pos(&r)) >= 0);
		//BUG_ON(bkey_cmp_packed(&b->format, k, bkey_next(k)) >= 0);
	}
#endif
}

static void clear_needs_whiteout(struct bset *i)
{
	struct bkey_packed *k;

	for (k = i->start; k != vstruct_last(i); k = bkey_next(k))
		k->needs_whiteout = false;
}

static void set_needs_whiteout(struct bset *i)
{
	struct bkey_packed *k;

	for (k = i->start; k != vstruct_last(i); k = bkey_next(k))
		k->needs_whiteout = true;
}

static void btree_bounce_free(struct bch_fs *c, unsigned order,
			      bool used_mempool, void *p)
{
	if (used_mempool)
		mempool_free(p, &c->btree_bounce_pool);
	else
		vpfree(p, PAGE_SIZE << order);
}

static void *btree_bounce_alloc(struct bch_fs *c, unsigned order,
				bool *used_mempool)
{
	void *p;

	BUG_ON(order > btree_page_order(c));

	*used_mempool = false;
	p = (void *) __get_free_pages(__GFP_NOWARN|GFP_NOWAIT, order);
	if (p)
		return p;

	*used_mempool = true;
	return mempool_alloc(&c->btree_bounce_pool, GFP_NOIO);
}

static unsigned should_compact_bset(struct btree *b, struct bset_tree *t,
				    bool compacting,
				    enum compact_mode mode)
{
	unsigned bset_u64s = le16_to_cpu(bset(b, t)->u64s);
	unsigned dead_u64s = bset_u64s - b->nr.bset_u64s[t - b->set];

	if (mode == COMPACT_LAZY) {
		if (should_compact_bset_lazy(b, t) ||
		    (compacting && !bset_written(b, bset(b, t))))
			return dead_u64s;
	} else {
		if (bset_written(b, bset(b, t)))
			return dead_u64s;
	}

	return 0;
}

bool __bch2_compact_whiteouts(struct bch_fs *c, struct btree *b,
			     enum compact_mode mode)
{
	const struct bkey_format *f = &b->format;
	struct bset_tree *t;
	struct bkey_packed *whiteouts = NULL;
	struct bkey_packed *u_start, *u_pos;
	struct sort_iter sort_iter;
	unsigned order, whiteout_u64s = 0, u64s;
	bool used_mempool, compacting = false;

	for_each_bset(b, t)
		whiteout_u64s += should_compact_bset(b, t,
					whiteout_u64s != 0, mode);

	if (!whiteout_u64s)
		return false;

	sort_iter_init(&sort_iter, b);

	whiteout_u64s += b->whiteout_u64s;
	order = get_order(whiteout_u64s * sizeof(u64));

	whiteouts = btree_bounce_alloc(c, order, &used_mempool);
	u_start = u_pos = whiteouts;

	memcpy_u64s(u_pos, unwritten_whiteouts_start(c, b),
		    b->whiteout_u64s);
	u_pos = (void *) u_pos + b->whiteout_u64s * sizeof(u64);

	sort_iter_add(&sort_iter, u_start, u_pos);

	for_each_bset(b, t) {
		struct bset *i = bset(b, t);
		struct bkey_packed *k, *n, *out, *start, *end;
		struct btree_node_entry *src = NULL, *dst = NULL;

		if (t != b->set && !bset_written(b, i)) {
			src = container_of(i, struct btree_node_entry, keys);
			dst = max(write_block(b),
				  (void *) btree_bkey_last(b, t -1));
		}

		if (!should_compact_bset(b, t, compacting, mode)) {
			if (src != dst) {
				memmove(dst, src, sizeof(*src) +
					le16_to_cpu(src->keys.u64s) *
					sizeof(u64));
				i = &dst->keys;
				set_btree_bset(b, t, i);
			}
			continue;
		}

		compacting = true;
		u_start = u_pos;
		start = i->start;
		end = vstruct_last(i);

		if (src != dst) {
			memmove(dst, src, sizeof(*src));
			i = &dst->keys;
			set_btree_bset(b, t, i);
		}

		out = i->start;

		for (k = start; k != end; k = n) {
			n = bkey_next(k);

			if (bkey_deleted(k) && btree_node_is_extents(b))
				continue;

			if (bkey_whiteout(k) && !k->needs_whiteout)
				continue;

			if (bkey_whiteout(k)) {
				unreserve_whiteout(b, k);
				memcpy_u64s(u_pos, k, bkeyp_key_u64s(f, k));
				set_bkeyp_val_u64s(f, u_pos, 0);
				u_pos = bkey_next(u_pos);
			} else if (mode != COMPACT_WRITTEN_NO_WRITE_LOCK) {
				bkey_copy(out, k);
				out = bkey_next(out);
			}
		}

		sort_iter_add(&sort_iter, u_start, u_pos);

		if (mode != COMPACT_WRITTEN_NO_WRITE_LOCK) {
			i->u64s = cpu_to_le16((u64 *) out - i->_data);
			set_btree_bset_end(b, t);
			bch2_bset_set_no_aux_tree(b, t);
		}
	}

	b->whiteout_u64s = (u64 *) u_pos - (u64 *) whiteouts;

	BUG_ON((void *) unwritten_whiteouts_start(c, b) <
	       (void *) btree_bkey_last(b, bset_tree_last(b)));

	u64s = (btree_node_is_extents(b)
		? bch2_sort_extent_whiteouts
		: bch2_sort_key_whiteouts)(unwritten_whiteouts_start(c, b),
					   &sort_iter);

	BUG_ON(u64s > b->whiteout_u64s);
	BUG_ON(u64s != b->whiteout_u64s && !btree_node_is_extents(b));
	BUG_ON(u_pos != whiteouts && !u64s);

	if (u64s != b->whiteout_u64s) {
		void *src = unwritten_whiteouts_start(c, b);

		b->whiteout_u64s = u64s;
		memmove_u64s_up(unwritten_whiteouts_start(c, b), src, u64s);
	}

	verify_no_dups(b,
		       unwritten_whiteouts_start(c, b),
		       unwritten_whiteouts_end(c, b));

	btree_bounce_free(c, order, used_mempool, whiteouts);

	if (mode != COMPACT_WRITTEN_NO_WRITE_LOCK)
		bch2_btree_build_aux_trees(b);

	bch_btree_keys_u64s_remaining(c, b);
	bch2_verify_btree_nr_keys(b);

	return true;
}

static bool bch2_drop_whiteouts(struct btree *b)
{
	struct bset_tree *t;
	bool ret = false;

	for_each_bset(b, t) {
		struct bset *i = bset(b, t);
		struct bkey_packed *k, *n, *out, *start, *end;

		if (!should_compact_bset(b, t, true, COMPACT_WRITTEN))
			continue;

		start	= btree_bkey_first(b, t);
		end	= btree_bkey_last(b, t);

		if (!bset_written(b, i) &&
		    t != b->set) {
			struct bset *dst =
			       max_t(struct bset *, write_block(b),
				     (void *) btree_bkey_last(b, t -1));

			memmove(dst, i, sizeof(struct bset));
			i = dst;
			set_btree_bset(b, t, i);
		}

		out = i->start;

		for (k = start; k != end; k = n) {
			n = bkey_next(k);

			if (!bkey_whiteout(k)) {
				bkey_copy(out, k);
				out = bkey_next(out);
			}
		}

		i->u64s = cpu_to_le16((u64 *) out - i->_data);
		bch2_bset_set_no_aux_tree(b, t);
		ret = true;
	}

	bch2_verify_btree_nr_keys(b);

	return ret;
}

static void btree_node_sort(struct bch_fs *c, struct btree *b,
			    struct btree_iter *iter,
			    unsigned start_idx,
			    unsigned end_idx,
			    bool filter_whiteouts)
{
	struct btree_node *out;
	struct sort_iter sort_iter;
	struct bset_tree *t;
	struct bset *start_bset = bset(b, &b->set[start_idx]);
	bool used_mempool = false;
	u64 start_time, seq = 0;
	unsigned i, u64s = 0, order, shift = end_idx - start_idx - 1;
	bool sorting_entire_node = start_idx == 0 &&
		end_idx == b->nsets;

	sort_iter_init(&sort_iter, b);

	for (t = b->set + start_idx;
	     t < b->set + end_idx;
	     t++) {
		u64s += le16_to_cpu(bset(b, t)->u64s);
		sort_iter_add(&sort_iter,
			      btree_bkey_first(b, t),
			      btree_bkey_last(b, t));
	}

	order = sorting_entire_node
		? btree_page_order(c)
		: get_order(__vstruct_bytes(struct btree_node, u64s));

	out = btree_bounce_alloc(c, order, &used_mempool);

	start_time = local_clock();

	if (btree_node_is_extents(b))
		filter_whiteouts = bset_written(b, start_bset);

	u64s = (btree_node_is_extents(b)
		? bch2_sort_extents
		: bch2_sort_keys)(out->keys.start,
				  &sort_iter,
				  filter_whiteouts);

	out->keys.u64s = cpu_to_le16(u64s);

	BUG_ON(vstruct_end(&out->keys) > (void *) out + (PAGE_SIZE << order));

	if (sorting_entire_node)
		bch2_time_stats_update(&c->times[BCH_TIME_btree_node_sort],
				       start_time);

	/* Make sure we preserve bset journal_seq: */
	for (t = b->set + start_idx; t < b->set + end_idx; t++)
		seq = max(seq, le64_to_cpu(bset(b, t)->journal_seq));
	start_bset->journal_seq = cpu_to_le64(seq);

	if (sorting_entire_node) {
		unsigned u64s = le16_to_cpu(out->keys.u64s);

		BUG_ON(order != btree_page_order(c));

		/*
		 * Our temporary buffer is the same size as the btree node's
		 * buffer, we can just swap buffers instead of doing a big
		 * memcpy()
		 */
		*out = *b->data;
		out->keys.u64s = cpu_to_le16(u64s);
		swap(out, b->data);
		set_btree_bset(b, b->set, &b->data->keys);
	} else {
		start_bset->u64s = out->keys.u64s;
		memcpy_u64s(start_bset->start,
			    out->keys.start,
			    le16_to_cpu(out->keys.u64s));
	}

	for (i = start_idx + 1; i < end_idx; i++)
		b->nr.bset_u64s[start_idx] +=
			b->nr.bset_u64s[i];

	b->nsets -= shift;

	for (i = start_idx + 1; i < b->nsets; i++) {
		b->nr.bset_u64s[i]	= b->nr.bset_u64s[i + shift];
		b->set[i]		= b->set[i + shift];
	}

	for (i = b->nsets; i < MAX_BSETS; i++)
		b->nr.bset_u64s[i] = 0;

	set_btree_bset_end(b, &b->set[start_idx]);
	bch2_bset_set_no_aux_tree(b, &b->set[start_idx]);

	btree_bounce_free(c, order, used_mempool, out);

	bch2_verify_btree_nr_keys(b);
}

void bch2_btree_sort_into(struct bch_fs *c,
			 struct btree *dst,
			 struct btree *src)
{
	struct btree_nr_keys nr;
	struct btree_node_iter src_iter;
	u64 start_time = local_clock();

	BUG_ON(dst->nsets != 1);

	bch2_bset_set_no_aux_tree(dst, dst->set);

	bch2_btree_node_iter_init_from_start(&src_iter, src);

	if (btree_node_is_extents(src))
		nr = bch2_sort_repack_merge(c, btree_bset_first(dst),
				src, &src_iter,
				&dst->format,
				true);
	else
		nr = bch2_sort_repack(btree_bset_first(dst),
				src, &src_iter,
				&dst->format,
				true);

	bch2_time_stats_update(&c->times[BCH_TIME_btree_node_sort],
			       start_time);

	set_btree_bset_end(dst, dst->set);

	dst->nr.live_u64s	+= nr.live_u64s;
	dst->nr.bset_u64s[0]	+= nr.bset_u64s[0];
	dst->nr.packed_keys	+= nr.packed_keys;
	dst->nr.unpacked_keys	+= nr.unpacked_keys;

	bch2_verify_btree_nr_keys(dst);
}

#define SORT_CRIT	(4096 / sizeof(u64))

/*
 * We're about to add another bset to the btree node, so if there's currently
 * too many bsets - sort some of them together:
 */
static bool btree_node_compact(struct bch_fs *c, struct btree *b,
			       struct btree_iter *iter)
{
	unsigned unwritten_idx;
	bool ret = false;

	for (unwritten_idx = 0;
	     unwritten_idx < b->nsets;
	     unwritten_idx++)
		if (!bset_written(b, bset(b, &b->set[unwritten_idx])))
			break;

	if (b->nsets - unwritten_idx > 1) {
		btree_node_sort(c, b, iter, unwritten_idx,
				b->nsets, false);
		ret = true;
	}

	if (unwritten_idx > 1) {
		btree_node_sort(c, b, iter, 0, unwritten_idx, false);
		ret = true;
	}

	return ret;
}

void bch2_btree_build_aux_trees(struct btree *b)
{
	struct bset_tree *t;

	for_each_bset(b, t)
		bch2_bset_build_aux_tree(b, t,
				!bset_written(b, bset(b, t)) &&
				t == bset_tree_last(b));
}

/*
 * @bch_btree_init_next - initialize a new (unwritten) bset that can then be
 * inserted into
 *
 * Safe to call if there already is an unwritten bset - will only add a new bset
 * if @b doesn't already have one.
 *
 * Returns true if we sorted (i.e. invalidated iterators
 */
void bch2_btree_init_next(struct bch_fs *c, struct btree *b,
			  struct btree_iter *iter)
{
	struct btree_node_entry *bne;
	bool did_sort;

	EBUG_ON(!(b->lock.state.seq & 1));
	EBUG_ON(iter && iter->l[b->level].b != b);

	did_sort = btree_node_compact(c, b, iter);

	bne = want_new_bset(c, b);
	if (bne)
		bch2_bset_init_next(c, b, bne);

	bch2_btree_build_aux_trees(b);

	if (iter && did_sort)
		bch2_btree_iter_reinit_node(iter, b);
}

static struct nonce btree_nonce(struct bset *i, unsigned offset)
{
	return (struct nonce) {{
		[0] = cpu_to_le32(offset),
		[1] = ((__le32 *) &i->seq)[0],
		[2] = ((__le32 *) &i->seq)[1],
		[3] = ((__le32 *) &i->journal_seq)[0]^BCH_NONCE_BTREE,
	}};
}

static void bset_encrypt(struct bch_fs *c, struct bset *i, unsigned offset)
{
	struct nonce nonce = btree_nonce(i, offset);

	if (!offset) {
		struct btree_node *bn = container_of(i, struct btree_node, keys);
		unsigned bytes = (void *) &bn->keys - (void *) &bn->flags;

		bch2_encrypt(c, BSET_CSUM_TYPE(i), nonce, &bn->flags,
			     bytes);

		nonce = nonce_add(nonce, round_up(bytes, CHACHA_BLOCK_SIZE));
	}

	bch2_encrypt(c, BSET_CSUM_TYPE(i), nonce, i->_data,
		     vstruct_end(i) - (void *) i->_data);
}

static void btree_err_msg(struct printbuf *out, struct bch_fs *c,
			  struct btree *b, struct bset *i,
			  unsigned offset, int write)
{
	pr_buf(out, "error validating btree node %s"
	       "at btree %u level %u/%u\n"
	       "pos %llu:%llu node offset %u",
	       write ? "before write " : "",
	       b->btree_id, b->level,
	       c->btree_roots[b->btree_id].level,
	       b->key.k.p.inode, b->key.k.p.offset,
	       b->written);
	if (i)
		pr_buf(out, " bset u64s %u", le16_to_cpu(i->u64s));
}

enum btree_err_type {
	BTREE_ERR_FIXABLE,
	BTREE_ERR_WANT_RETRY,
	BTREE_ERR_MUST_RETRY,
	BTREE_ERR_FATAL,
};

enum btree_validate_ret {
	BTREE_RETRY_READ = 64,
};

#define btree_err(type, c, b, i, msg, ...)				\
({									\
	__label__ out;							\
	char _buf[300];							\
	struct printbuf out = PBUF(_buf);				\
									\
	btree_err_msg(&out, c, b, i, b->written, write);		\
	pr_buf(&out, ": " msg, ##__VA_ARGS__);				\
									\
	if (type == BTREE_ERR_FIXABLE &&				\
	    write == READ &&						\
	    !test_bit(BCH_FS_INITIAL_GC_DONE, &c->flags)) {		\
		mustfix_fsck_err(c, "%s", _buf);			\
		goto out;						\
	}								\
									\
	switch (write) {						\
	case READ:							\
		bch_err(c, "%s", _buf);					\
									\
		switch (type) {						\
		case BTREE_ERR_FIXABLE:					\
			ret = BCH_FSCK_ERRORS_NOT_FIXED;		\
			goto fsck_err;					\
		case BTREE_ERR_WANT_RETRY:				\
			if (have_retry) {				\
				ret = BTREE_RETRY_READ;			\
				goto fsck_err;				\
			}						\
			break;						\
		case BTREE_ERR_MUST_RETRY:				\
			ret = BTREE_RETRY_READ;				\
			goto fsck_err;					\
		case BTREE_ERR_FATAL:					\
			ret = BCH_FSCK_ERRORS_NOT_FIXED;		\
			goto fsck_err;					\
		}							\
		break;							\
	case WRITE:							\
		bch_err(c, "corrupt metadata before write: %s", _buf);	\
									\
		if (bch2_fs_inconsistent(c)) {				\
			ret = BCH_FSCK_ERRORS_NOT_FIXED;		\
			goto fsck_err;					\
		}							\
		break;							\
	}								\
out:									\
	true;								\
})

#define btree_err_on(cond, ...)	((cond) ? btree_err(__VA_ARGS__) : false)

static int validate_bset(struct bch_fs *c, struct btree *b,
			 struct bset *i, unsigned sectors,
			 unsigned *whiteout_u64s, int write,
			 bool have_retry)
{
	struct bkey_packed *k, *prev = NULL;
	struct bpos prev_pos = POS_MIN;
	bool seen_non_whiteout = false;
	unsigned version;
	const char *err;
	int ret = 0;

	if (i == &b->data->keys) {
		/* These indicate that we read the wrong btree node: */
		btree_err_on(BTREE_NODE_ID(b->data) != b->btree_id,
			     BTREE_ERR_MUST_RETRY, c, b, i,
			     "incorrect btree id");

		btree_err_on(BTREE_NODE_LEVEL(b->data) != b->level,
			     BTREE_ERR_MUST_RETRY, c, b, i,
			     "incorrect level");

		if (BSET_BIG_ENDIAN(i) != CPU_BIG_ENDIAN) {
			u64 *p = (u64 *) &b->data->ptr;

			*p = swab64(*p);
			bch2_bpos_swab(&b->data->min_key);
			bch2_bpos_swab(&b->data->max_key);
		}

		btree_err_on(bkey_cmp(b->data->max_key, b->key.k.p),
			     BTREE_ERR_MUST_RETRY, c, b, i,
			     "incorrect max key");

		/* XXX: ideally we would be validating min_key too */
#if 0
		/*
		 * not correct anymore, due to btree node write error
		 * handling
		 *
		 * need to add b->data->seq to btree keys and verify
		 * against that
		 */
		btree_err_on(!extent_contains_ptr(bkey_i_to_s_c_extent(&b->key),
						  b->data->ptr),
			     BTREE_ERR_FATAL, c, b, i,
			     "incorrect backpointer");
#endif
		err = bch2_bkey_format_validate(&b->data->format);
		btree_err_on(err,
			     BTREE_ERR_FATAL, c, b, i,
			     "invalid bkey format: %s", err);
	}

	version = le16_to_cpu(i->version);
	btree_err_on((version != BCH_BSET_VERSION_OLD &&
		      version < bcachefs_metadata_version_min) ||
		     version >= bcachefs_metadata_version_max,
		     BTREE_ERR_FATAL, c, b, i,
		     "unsupported bset version");

	if (btree_err_on(b->written + sectors > c->opts.btree_node_size,
			 BTREE_ERR_FIXABLE, c, b, i,
			 "bset past end of btree node")) {
		i->u64s = 0;
		return 0;
	}

	btree_err_on(b->written && !i->u64s,
		     BTREE_ERR_FIXABLE, c, b, i,
		     "empty bset");

	if (!BSET_SEPARATE_WHITEOUTS(i)) {
		seen_non_whiteout = true;
		*whiteout_u64s = 0;
	}

	for (k = i->start;
	     k != vstruct_last(i);) {
		struct bkey_s_c u;
		struct bkey tmp;
		const char *invalid;

		if (btree_err_on(!k->u64s,
				 BTREE_ERR_FIXABLE, c, b, i,
				 "KEY_U64s 0: %zu bytes of metadata lost",
				 vstruct_end(i) - (void *) k)) {
			i->u64s = cpu_to_le16((u64 *) k - i->_data);
			break;
		}

		if (btree_err_on(bkey_next(k) > vstruct_last(i),
				 BTREE_ERR_FIXABLE, c, b, i,
				 "key extends past end of bset")) {
			i->u64s = cpu_to_le16((u64 *) k - i->_data);
			break;
		}

		if (btree_err_on(k->format > KEY_FORMAT_CURRENT,
				 BTREE_ERR_FIXABLE, c, b, i,
				 "invalid bkey format %u", k->format)) {
			i->u64s = cpu_to_le16(le16_to_cpu(i->u64s) - k->u64s);
			memmove_u64s_down(k, bkey_next(k),
					  (u64 *) vstruct_end(i) - (u64 *) k);
			continue;
		}

		if (BSET_BIG_ENDIAN(i) != CPU_BIG_ENDIAN)
			bch2_bkey_swab(&b->format, k);

		if (!write &&
		    version < bcachefs_metadata_version_bkey_renumber)
			bch2_bkey_renumber(btree_node_type(b), k, write);

		u = bkey_disassemble(b, k, &tmp);

		invalid = __bch2_bkey_invalid(c, u, btree_node_type(b)) ?:
			bch2_bkey_in_btree_node(b, u) ?:
			(write ? bch2_bkey_val_invalid(c, u) : NULL);
		if (invalid) {
			char buf[160];

			bch2_bkey_val_to_text(&PBUF(buf), c, u);
			btree_err(BTREE_ERR_FIXABLE, c, b, i,
				  "invalid bkey:\n%s\n%s", invalid, buf);

			i->u64s = cpu_to_le16(le16_to_cpu(i->u64s) - k->u64s);
			memmove_u64s_down(k, bkey_next(k),
					  (u64 *) vstruct_end(i) - (u64 *) k);
			continue;
		}

		if (write &&
		    version < bcachefs_metadata_version_bkey_renumber)
			bch2_bkey_renumber(btree_node_type(b), k, write);

		/*
		 * with the separate whiteouts thing (used for extents), the
		 * second set of keys actually can have whiteouts too, so we
		 * can't solely go off bkey_whiteout()...
		 */

		if (!seen_non_whiteout &&
		    (!bkey_whiteout(k) ||
		     (bkey_cmp(prev_pos, bkey_start_pos(u.k)) > 0))) {
			*whiteout_u64s = k->_data - i->_data;
			seen_non_whiteout = true;
		} else if (bkey_cmp(prev_pos, bkey_start_pos(u.k)) > 0) {
			btree_err(BTREE_ERR_FATAL, c, b, i,
				  "keys out of order: %llu:%llu > %llu:%llu",
				  prev_pos.inode,
				  prev_pos.offset,
				  u.k->p.inode,
				  bkey_start_offset(u.k));
			/* XXX: repair this */
		}

		prev_pos = u.k->p;
		prev = k;
		k = bkey_next(k);
	}

	SET_BSET_BIG_ENDIAN(i, CPU_BIG_ENDIAN);
fsck_err:
	return ret;
}

int bch2_btree_node_read_done(struct bch_fs *c, struct btree *b, bool have_retry)
{
	struct btree_node_entry *bne;
	struct btree_node_iter_large *iter;
	struct btree_node *sorted;
	struct bkey_packed *k;
	struct bset *i;
	bool used_mempool;
	unsigned u64s;
	int ret, retry_read = 0, write = READ;

	iter = mempool_alloc(&c->fill_iter, GFP_NOIO);
	iter->used = 0;

	if (bch2_meta_read_fault("btree"))
		btree_err(BTREE_ERR_MUST_RETRY, c, b, NULL,
			  "dynamic fault");

	btree_err_on(le64_to_cpu(b->data->magic) != bset_magic(c),
		     BTREE_ERR_MUST_RETRY, c, b, NULL,
		     "bad magic");

	btree_err_on(!b->data->keys.seq,
		     BTREE_ERR_MUST_RETRY, c, b, NULL,
		     "bad btree header");

	while (b->written < c->opts.btree_node_size) {
		unsigned sectors, whiteout_u64s = 0;
		struct nonce nonce;
		struct bch_csum csum;
		bool first = !b->written;

		if (!b->written) {
			i = &b->data->keys;

			btree_err_on(!bch2_checksum_type_valid(c, BSET_CSUM_TYPE(i)),
				     BTREE_ERR_WANT_RETRY, c, b, i,
				     "unknown checksum type");

			nonce = btree_nonce(i, b->written << 9);
			csum = csum_vstruct(c, BSET_CSUM_TYPE(i), nonce, b->data);

			btree_err_on(bch2_crc_cmp(csum, b->data->csum),
				     BTREE_ERR_WANT_RETRY, c, b, i,
				     "invalid checksum");

			bset_encrypt(c, i, b->written << 9);

			sectors = vstruct_sectors(b->data, c->block_bits);

			btree_node_set_format(b, b->data->format);
		} else {
			bne = write_block(b);
			i = &bne->keys;

			if (i->seq != b->data->keys.seq)
				break;

			btree_err_on(!bch2_checksum_type_valid(c, BSET_CSUM_TYPE(i)),
				     BTREE_ERR_WANT_RETRY, c, b, i,
				     "unknown checksum type");

			nonce = btree_nonce(i, b->written << 9);
			csum = csum_vstruct(c, BSET_CSUM_TYPE(i), nonce, bne);

			btree_err_on(bch2_crc_cmp(csum, bne->csum),
				     BTREE_ERR_WANT_RETRY, c, b, i,
				     "invalid checksum");

			bset_encrypt(c, i, b->written << 9);

			sectors = vstruct_sectors(bne, c->block_bits);
		}

		ret = validate_bset(c, b, i, sectors, &whiteout_u64s,
				    READ, have_retry);
		if (ret)
			goto fsck_err;

		b->written += sectors;

		ret = bch2_journal_seq_should_ignore(c, le64_to_cpu(i->journal_seq), b);
		if (ret < 0) {
			btree_err(BTREE_ERR_FATAL, c, b, i,
				  "insufficient memory");
			goto err;
		}

		if (ret) {
			btree_err_on(first,
				     BTREE_ERR_FIXABLE, c, b, i,
				     "first btree node bset has blacklisted journal seq");
			if (!first)
				continue;
		}

		bch2_btree_node_iter_large_push(iter, b,
					   i->start,
					   vstruct_idx(i, whiteout_u64s));

		bch2_btree_node_iter_large_push(iter, b,
					   vstruct_idx(i, whiteout_u64s),
					   vstruct_last(i));
	}

	for (bne = write_block(b);
	     bset_byte_offset(b, bne) < btree_bytes(c);
	     bne = (void *) bne + block_bytes(c))
		btree_err_on(bne->keys.seq == b->data->keys.seq,
			     BTREE_ERR_WANT_RETRY, c, b, NULL,
			     "found bset signature after last bset");

	sorted = btree_bounce_alloc(c, btree_page_order(c), &used_mempool);
	sorted->keys.u64s = 0;

	set_btree_bset(b, b->set, &b->data->keys);

	b->nr = btree_node_is_extents(b)
		? bch2_extent_sort_fix_overlapping(c, &sorted->keys, b, iter)
		: bch2_key_sort_fix_overlapping(&sorted->keys, b, iter);

	u64s = le16_to_cpu(sorted->keys.u64s);
	*sorted = *b->data;
	sorted->keys.u64s = cpu_to_le16(u64s);
	swap(sorted, b->data);
	set_btree_bset(b, b->set, &b->data->keys);
	b->nsets = 1;

	BUG_ON(b->nr.live_u64s != u64s);

	btree_bounce_free(c, btree_page_order(c), used_mempool, sorted);

	i = &b->data->keys;
	for (k = i->start; k != vstruct_last(i);) {
		struct bkey tmp;
		struct bkey_s_c u = bkey_disassemble(b, k, &tmp);
		const char *invalid = bch2_bkey_val_invalid(c, u);

		if (invalid ||
		    (inject_invalid_keys(c) &&
		     !bversion_cmp(u.k->version, MAX_VERSION))) {
			char buf[160];

			bch2_bkey_val_to_text(&PBUF(buf), c, u);
			btree_err(BTREE_ERR_FIXABLE, c, b, i,
				  "invalid bkey %s: %s", buf, invalid);

			btree_keys_account_key_drop(&b->nr, 0, k);

			i->u64s = cpu_to_le16(le16_to_cpu(i->u64s) - k->u64s);
			memmove_u64s_down(k, bkey_next(k),
					  (u64 *) vstruct_end(i) - (u64 *) k);
			set_btree_bset_end(b, b->set);
			continue;
		}

		k = bkey_next(k);
	}

	bch2_bset_build_aux_tree(b, b->set, false);

	set_needs_whiteout(btree_bset_first(b));

	btree_node_reset_sib_u64s(b);
out:
	mempool_free(iter, &c->fill_iter);
	return retry_read;
err:
fsck_err:
	if (ret == BTREE_RETRY_READ) {
		retry_read = 1;
	} else {
		bch2_inconsistent_error(c);
		set_btree_node_read_error(b);
	}
	goto out;
}

static void btree_node_read_work(struct work_struct *work)
{
	struct btree_read_bio *rb =
		container_of(work, struct btree_read_bio, work);
	struct bch_fs *c	= rb->c;
	struct bch_dev *ca	= bch_dev_bkey_exists(c, rb->pick.ptr.dev);
	struct btree *b		= rb->bio.bi_private;
	struct bio *bio		= &rb->bio;
	struct bch_io_failures failed = { .nr = 0 };
	bool can_retry;

	goto start;
	while (1) {
		bch_info(c, "retrying read");
		ca = bch_dev_bkey_exists(c, rb->pick.ptr.dev);
		rb->have_ioref		= bch2_dev_get_ioref(ca, READ);
		bio_reset(bio, NULL, REQ_OP_READ|REQ_SYNC|REQ_META);
		bio->bi_iter.bi_sector	= rb->pick.ptr.offset;
		bio->bi_iter.bi_size	= btree_bytes(c);

		if (rb->have_ioref) {
			bio_set_dev(bio, ca->disk_sb.bdev);
			submit_bio_wait(bio);
		} else {
			bio->bi_status = BLK_STS_REMOVED;
		}
start:
		bch2_dev_io_err_on(bio->bi_status, ca, "btree read");
		if (rb->have_ioref)
			percpu_ref_put(&ca->io_ref);
		rb->have_ioref = false;

		bch2_mark_io_failure(&failed, &rb->pick);

		can_retry = bch2_bkey_pick_read_device(c,
				bkey_i_to_s_c(&b->key),
				&failed, &rb->pick) > 0;

		if (!bio->bi_status &&
		    !bch2_btree_node_read_done(c, b, can_retry))
			break;

		if (!can_retry) {
			set_btree_node_read_error(b);
			break;
		}
	}

	bch2_time_stats_update(&c->times[BCH_TIME_btree_node_read],
			       rb->start_time);
	bio_put(&rb->bio);
	clear_btree_node_read_in_flight(b);
	wake_up_bit(&b->flags, BTREE_NODE_read_in_flight);
}

static void btree_node_read_endio(struct bio *bio)
{
	struct btree_read_bio *rb =
		container_of(bio, struct btree_read_bio, bio);
	struct bch_fs *c	= rb->c;

	if (rb->have_ioref) {
		struct bch_dev *ca = bch_dev_bkey_exists(c, rb->pick.ptr.dev);
		bch2_latency_acct(ca, rb->start_time, READ);
	}

	queue_work(system_unbound_wq, &rb->work);
}

void bch2_btree_node_read(struct bch_fs *c, struct btree *b,
			  bool sync)
{
	struct extent_ptr_decoded pick;
	struct btree_read_bio *rb;
	struct bch_dev *ca;
	struct bio *bio;
	int ret;

	trace_btree_read(c, b);

	ret = bch2_bkey_pick_read_device(c, bkey_i_to_s_c(&b->key),
					 NULL, &pick);
	if (bch2_fs_fatal_err_on(ret <= 0, c,
			"btree node read error: no device to read from")) {
		set_btree_node_read_error(b);
		return;
	}

	ca = bch_dev_bkey_exists(c, pick.ptr.dev);

	bio = bio_alloc_bioset(NULL,
			       buf_pages(b->data, btree_bytes(c)),
			       REQ_OP_READ|REQ_SYNC|REQ_META,
			       GFP_NOIO,
			       &c->btree_bio);
	rb = container_of(bio, struct btree_read_bio, bio);
	rb->c			= c;
	rb->start_time		= local_clock();
	rb->have_ioref		= bch2_dev_get_ioref(ca, READ);
	rb->pick		= pick;
	INIT_WORK(&rb->work, btree_node_read_work);
	bio->bi_iter.bi_sector	= pick.ptr.offset;
	bio->bi_iter.bi_size	= btree_bytes(c);
	bio->bi_end_io		= btree_node_read_endio;
	bio->bi_private		= b;
	bch2_bio_map(bio, b->data);

	set_btree_node_read_in_flight(b);

	if (rb->have_ioref) {
		this_cpu_add(ca->io_done->sectors[READ][BCH_DATA_BTREE],
			     bio_sectors(bio));
		bio_set_dev(bio, ca->disk_sb.bdev);

		if (sync) {
			submit_bio_wait(bio);

			bio->bi_private	= b;
			btree_node_read_work(&rb->work);
		} else {
			submit_bio(bio);
		}
	} else {
		bio->bi_status = BLK_STS_REMOVED;

		if (sync)
			btree_node_read_work(&rb->work);
		else
			queue_work(system_unbound_wq, &rb->work);

	}
}

int bch2_btree_root_read(struct bch_fs *c, enum btree_id id,
			const struct bkey_i *k, unsigned level)
{
	struct closure cl;
	struct btree *b;
	int ret;

	closure_init_stack(&cl);

	do {
		ret = bch2_btree_cache_cannibalize_lock(c, &cl);
		closure_sync(&cl);
	} while (ret);

	b = bch2_btree_node_mem_alloc(c);
	bch2_btree_cache_cannibalize_unlock(c);

	BUG_ON(IS_ERR(b));

	bkey_copy(&b->key, k);
	BUG_ON(bch2_btree_node_hash_insert(&c->btree_cache, b, level, id));

	bch2_btree_node_read(c, b, true);

	if (btree_node_read_error(b)) {
		bch2_btree_node_hash_remove(&c->btree_cache, b);

		mutex_lock(&c->btree_cache.lock);
		list_move(&b->list, &c->btree_cache.freeable);
		mutex_unlock(&c->btree_cache.lock);

		ret = -EIO;
		goto err;
	}

	bch2_btree_set_root_for_read(c, b);
err:
	six_unlock_write(&b->lock);
	six_unlock_intent(&b->lock);

	return ret;
}

void bch2_btree_complete_write(struct bch_fs *c, struct btree *b,
			      struct btree_write *w)
{
	unsigned long old, new, v = READ_ONCE(b->will_make_reachable);

	do {
		old = new = v;
		if (!(old & 1))
			break;

		new &= ~1UL;
	} while ((v = cmpxchg(&b->will_make_reachable, old, new)) != old);

	if (old & 1)
		closure_put(&((struct btree_update *) new)->cl);

	bch2_journal_pin_drop(&c->journal, &w->journal);
	closure_wake_up(&w->wait);
}

static void btree_node_write_done(struct bch_fs *c, struct btree *b)
{
	struct btree_write *w = btree_prev_write(b);

	bch2_btree_complete_write(c, b, w);
	btree_node_io_unlock(b);
}

static void bch2_btree_node_write_error(struct bch_fs *c,
					struct btree_write_bio *wbio)
{
	struct btree *b		= wbio->wbio.bio.bi_private;
	__BKEY_PADDED(k, BKEY_BTREE_PTR_VAL_U64s_MAX) tmp;
	struct bkey_i_btree_ptr *new_key;
	struct bkey_s_btree_ptr bp;
	struct bch_extent_ptr *ptr;
	struct btree_trans trans;
	struct btree_iter *iter;
	int ret;

	bch2_trans_init(&trans, c);

	iter = bch2_trans_get_node_iter(&trans, b->btree_id, b->key.k.p,
					BTREE_MAX_DEPTH, b->level, 0);
retry:
	ret = bch2_btree_iter_traverse(iter);
	if (ret)
		goto err;

	/* has node been freed? */
	if (iter->l[b->level].b != b) {
		/* node has been freed: */
		BUG_ON(!btree_node_dying(b));
		goto out;
	}

	BUG_ON(!btree_node_hashed(b));

	bkey_copy(&tmp.k, &b->key);

	new_key = bkey_i_to_btree_ptr(&tmp.k);
	bp = btree_ptr_i_to_s(new_key);

	bch2_bkey_drop_ptrs(bkey_i_to_s(&tmp.k), ptr,
		bch2_dev_list_has_dev(wbio->wbio.failed, ptr->dev));

	if (!bch2_bkey_nr_ptrs(bp.s_c))
		goto err;

	ret = bch2_btree_node_update_key(c, iter, b, new_key);
	if (ret == -EINTR)
		goto retry;
	if (ret)
		goto err;
out:
	bch2_trans_exit(&trans);
	bio_put(&wbio->wbio.bio);
	btree_node_write_done(c, b);
	return;
err:
	set_btree_node_noevict(b);
	bch2_fs_fatal_error(c, "fatal error writing btree node");
	goto out;
}

void bch2_btree_write_error_work(struct work_struct *work)
{
	struct bch_fs *c = container_of(work, struct bch_fs,
					btree_write_error_work);
	struct bio *bio;

	while (1) {
		spin_lock_irq(&c->btree_write_error_lock);
		bio = bio_list_pop(&c->btree_write_error_list);
		spin_unlock_irq(&c->btree_write_error_lock);

		if (!bio)
			break;

		bch2_btree_node_write_error(c,
			container_of(bio, struct btree_write_bio, wbio.bio));
	}
}

static void btree_node_write_work(struct work_struct *work)
{
	struct btree_write_bio *wbio =
		container_of(work, struct btree_write_bio, work);
	struct bch_fs *c	= wbio->wbio.c;
	struct btree *b		= wbio->wbio.bio.bi_private;

	btree_bounce_free(c,
		wbio->wbio.order,
		wbio->wbio.used_mempool,
		wbio->data);

	if (wbio->wbio.failed.nr) {
		unsigned long flags;

		spin_lock_irqsave(&c->btree_write_error_lock, flags);
		bio_list_add(&c->btree_write_error_list, &wbio->wbio.bio);
		spin_unlock_irqrestore(&c->btree_write_error_lock, flags);

		queue_work(c->wq, &c->btree_write_error_work);
		return;
	}

	bio_put(&wbio->wbio.bio);
	btree_node_write_done(c, b);
}

static void btree_node_write_endio(struct bio *bio)
{
	struct bch_write_bio *wbio	= to_wbio(bio);
	struct bch_write_bio *parent	= wbio->split ? wbio->parent : NULL;
	struct bch_write_bio *orig	= parent ?: wbio;
	struct bch_fs *c		= wbio->c;
	struct bch_dev *ca		= bch_dev_bkey_exists(c, wbio->dev);
	unsigned long flags;

	if (wbio->have_ioref)
		bch2_latency_acct(ca, wbio->submit_time, WRITE);

	if (bio->bi_status == BLK_STS_REMOVED ||
	    bch2_dev_io_err_on(bio->bi_status, ca, "btree write") ||
	    bch2_meta_write_fault("btree")) {
		spin_lock_irqsave(&c->btree_write_error_lock, flags);
		bch2_dev_list_add_dev(&orig->failed, wbio->dev);
		spin_unlock_irqrestore(&c->btree_write_error_lock, flags);
	}

	if (wbio->have_ioref)
		percpu_ref_put(&ca->io_ref);

	if (parent) {
		bio_put(bio);
		bio_endio(&parent->bio);
	} else {
		struct btree_write_bio *wb =
			container_of(orig, struct btree_write_bio, wbio);

		INIT_WORK(&wb->work, btree_node_write_work);
		queue_work(system_unbound_wq, &wb->work);
	}
}

static int validate_bset_for_write(struct bch_fs *c, struct btree *b,
				   struct bset *i, unsigned sectors)
{
	unsigned whiteout_u64s = 0;
	int ret;

	if (bch2_bkey_invalid(c, bkey_i_to_s_c(&b->key), BKEY_TYPE_BTREE))
		return -1;

	ret = validate_bset(c, b, i, sectors, &whiteout_u64s, WRITE, false);
	if (ret)
		bch2_inconsistent_error(c);

	return ret;
}

void __bch2_btree_node_write(struct bch_fs *c, struct btree *b,
			    enum six_lock_type lock_type_held)
{
	struct btree_write_bio *wbio;
	struct bset_tree *t;
	struct bset *i;
	struct btree_node *bn = NULL;
	struct btree_node_entry *bne = NULL;
	BKEY_PADDED(key) k;
	struct bch_extent_ptr *ptr;
	struct sort_iter sort_iter;
	struct nonce nonce;
	unsigned bytes_to_write, sectors_to_write, order, bytes, u64s;
	u64 seq = 0;
	bool used_mempool;
	unsigned long old, new;
	bool validate_before_checksum = false;
	void *data;

	if (test_bit(BCH_FS_HOLD_BTREE_WRITES, &c->flags))
		return;

	/*
	 * We may only have a read lock on the btree node - the dirty bit is our
	 * "lock" against racing with other threads that may be trying to start
	 * a write, we do a write iff we clear the dirty bit. Since setting the
	 * dirty bit requires a write lock, we can't race with other threads
	 * redirtying it:
	 */
	do {
		old = new = READ_ONCE(b->flags);

		if (!(old & (1 << BTREE_NODE_dirty)))
			return;

		if (!btree_node_may_write(b))
			return;

		if (old & (1 << BTREE_NODE_write_in_flight)) {
			btree_node_wait_on_io(b);
			continue;
		}

		new &= ~(1 << BTREE_NODE_dirty);
		new &= ~(1 << BTREE_NODE_need_write);
		new |=  (1 << BTREE_NODE_write_in_flight);
		new |=  (1 << BTREE_NODE_just_written);
		new ^=  (1 << BTREE_NODE_write_idx);
	} while (cmpxchg_acquire(&b->flags, old, new) != old);

	BUG_ON(btree_node_fake(b));
	BUG_ON((b->will_make_reachable != 0) != !b->written);

	BUG_ON(b->written >= c->opts.btree_node_size);
	BUG_ON(b->written & (c->opts.block_size - 1));
	BUG_ON(bset_written(b, btree_bset_last(b)));
	BUG_ON(le64_to_cpu(b->data->magic) != bset_magic(c));
	BUG_ON(memcmp(&b->data->format, &b->format, sizeof(b->format)));

	/*
	 * We can't block on six_lock_write() here; another thread might be
	 * trying to get a journal reservation with read locks held, and getting
	 * a journal reservation might be blocked on flushing the journal and
	 * doing btree writes:
	 */
	if (lock_type_held == SIX_LOCK_intent &&
	    six_trylock_write(&b->lock)) {
		__bch2_compact_whiteouts(c, b, COMPACT_WRITTEN);
		six_unlock_write(&b->lock);
	} else {
		__bch2_compact_whiteouts(c, b, COMPACT_WRITTEN_NO_WRITE_LOCK);
	}

	BUG_ON(b->uncompacted_whiteout_u64s);

	sort_iter_init(&sort_iter, b);

	bytes = !b->written
		? sizeof(struct btree_node)
		: sizeof(struct btree_node_entry);

	bytes += b->whiteout_u64s * sizeof(u64);

	for_each_bset(b, t) {
		i = bset(b, t);

		if (bset_written(b, i))
			continue;

		bytes += le16_to_cpu(i->u64s) * sizeof(u64);
		sort_iter_add(&sort_iter,
			      btree_bkey_first(b, t),
			      btree_bkey_last(b, t));
		seq = max(seq, le64_to_cpu(i->journal_seq));
	}

	order = get_order(bytes);
	data = btree_bounce_alloc(c, order, &used_mempool);

	if (!b->written) {
		bn = data;
		*bn = *b->data;
		i = &bn->keys;
	} else {
		bne = data;
		bne->keys = b->data->keys;
		i = &bne->keys;
	}

	i->journal_seq	= cpu_to_le64(seq);
	i->u64s		= 0;

	if (!btree_node_is_extents(b)) {
		sort_iter_add(&sort_iter,
			      unwritten_whiteouts_start(c, b),
			      unwritten_whiteouts_end(c, b));
		SET_BSET_SEPARATE_WHITEOUTS(i, false);
	} else {
		memcpy_u64s(i->start,
			    unwritten_whiteouts_start(c, b),
			    b->whiteout_u64s);
		i->u64s = cpu_to_le16(b->whiteout_u64s);
		SET_BSET_SEPARATE_WHITEOUTS(i, true);
	}

	b->whiteout_u64s = 0;

	u64s = btree_node_is_extents(b)
		? bch2_sort_extents(vstruct_last(i), &sort_iter, false)
		: bch2_sort_keys(i->start, &sort_iter, false);
	le16_add_cpu(&i->u64s, u64s);

	clear_needs_whiteout(i);

	/* do we have data to write? */
	if (b->written && !i->u64s)
		goto nowrite;

	bytes_to_write = vstruct_end(i) - data;
	sectors_to_write = round_up(bytes_to_write, block_bytes(c)) >> 9;

	memset(data + bytes_to_write, 0,
	       (sectors_to_write << 9) - bytes_to_write);

	BUG_ON(b->written + sectors_to_write > c->opts.btree_node_size);
	BUG_ON(BSET_BIG_ENDIAN(i) != CPU_BIG_ENDIAN);
	BUG_ON(i->seq != b->data->keys.seq);

	i->version = c->sb.version < bcachefs_metadata_version_new_versioning
		? cpu_to_le16(BCH_BSET_VERSION_OLD)
		: cpu_to_le16(c->sb.version);
	SET_BSET_CSUM_TYPE(i, bch2_meta_checksum_type(c));

	if (bch2_csum_type_is_encryption(BSET_CSUM_TYPE(i)))
		validate_before_checksum = true;

	/* validate_bset will be modifying: */
	if (le16_to_cpu(i->version) <
	    bcachefs_metadata_version_bkey_renumber)
		validate_before_checksum = true;

	/* if we're going to be encrypting, check metadata validity first: */
	if (validate_before_checksum &&
	    validate_bset_for_write(c, b, i, sectors_to_write))
		goto err;

	bset_encrypt(c, i, b->written << 9);

	nonce = btree_nonce(i, b->written << 9);

	if (bn)
		bn->csum = csum_vstruct(c, BSET_CSUM_TYPE(i), nonce, bn);
	else
		bne->csum = csum_vstruct(c, BSET_CSUM_TYPE(i), nonce, bne);

	/* if we're not encrypting, check metadata after checksumming: */
	if (!validate_before_checksum &&
	    validate_bset_for_write(c, b, i, sectors_to_write))
		goto err;

	/*
	 * We handle btree write errors by immediately halting the journal -
	 * after we've done that, we can't issue any subsequent btree writes
	 * because they might have pointers to new nodes that failed to write.
	 *
	 * Furthermore, there's no point in doing any more btree writes because
	 * with the journal stopped, we're never going to update the journal to
	 * reflect that those writes were done and the data flushed from the
	 * journal:
	 *
	 * Make sure to update b->written so bch2_btree_init_next() doesn't
	 * break:
	 */
	if (bch2_journal_error(&c->journal) ||
	    c->opts.nochanges)
		goto err;

	trace_btree_write(b, bytes_to_write, sectors_to_write);

	wbio = container_of(bio_alloc_bioset(NULL,
				buf_pages(data, sectors_to_write << 9),
				REQ_OP_WRITE|REQ_META|REQ_FUA,
				GFP_NOIO,
				&c->btree_bio),
			    struct btree_write_bio, wbio.bio);
	wbio_init(&wbio->wbio.bio);
	wbio->data			= data;
	wbio->wbio.order		= order;
	wbio->wbio.used_mempool		= used_mempool;
	wbio->wbio.bio.bi_iter.bi_size	= sectors_to_write << 9;
	wbio->wbio.bio.bi_end_io	= btree_node_write_endio;
	wbio->wbio.bio.bi_private	= b;

	bch2_bio_map(&wbio->wbio.bio, data);

	/*
	 * If we're appending to a leaf node, we don't technically need FUA -
	 * this write just needs to be persisted before the next journal write,
	 * which will be marked FLUSH|FUA.
	 *
	 * Similarly if we're writing a new btree root - the pointer is going to
	 * be in the next journal entry.
	 *
	 * But if we're writing a new btree node (that isn't a root) or
	 * appending to a non leaf btree node, we need either FUA or a flush
	 * when we write the parent with the new pointer. FUA is cheaper than a
	 * flush, and writes appending to leaf nodes aren't blocking anything so
	 * just make all btree node writes FUA to keep things sane.
	 */

	bkey_copy(&k.key, &b->key);

	bkey_for_each_ptr(bch2_bkey_ptrs(bkey_i_to_s(&k.key)), ptr)
		ptr->offset += b->written;

	b->written += sectors_to_write;

	bch2_submit_wbio_replicas(&wbio->wbio, c, BCH_DATA_BTREE, &k.key);
	return;
err:
	set_btree_node_noevict(b);
	b->written += sectors_to_write;
nowrite:
	btree_bounce_free(c, order, used_mempool, data);
	btree_node_write_done(c, b);
}

/*
 * Work that must be done with write lock held:
 */
bool bch2_btree_post_write_cleanup(struct bch_fs *c, struct btree *b)
{
	bool invalidated_iter = false;
	struct btree_node_entry *bne;
	struct bset_tree *t;

	if (!btree_node_just_written(b))
		return false;

	BUG_ON(b->whiteout_u64s);
	BUG_ON(b->uncompacted_whiteout_u64s);

	clear_btree_node_just_written(b);

	/*
	 * Note: immediately after write, bset_written() doesn't work - the
	 * amount of data we had to write after compaction might have been
	 * smaller than the offset of the last bset.
	 *
	 * However, we know that all bsets have been written here, as long as
	 * we're still holding the write lock:
	 */

	/*
	 * XXX: decide if we really want to unconditionally sort down to a
	 * single bset:
	 */
	if (b->nsets > 1) {
		btree_node_sort(c, b, NULL, 0, b->nsets, true);
		invalidated_iter = true;
	} else {
		invalidated_iter = bch2_drop_whiteouts(b);
	}

	for_each_bset(b, t)
		set_needs_whiteout(bset(b, t));

	bch2_btree_verify(c, b);

	/*
	 * If later we don't unconditionally sort down to a single bset, we have
	 * to ensure this is still true:
	 */
	BUG_ON((void *) btree_bkey_last(b, bset_tree_last(b)) > write_block(b));

	bne = want_new_bset(c, b);
	if (bne)
		bch2_bset_init_next(c, b, bne);

	bch2_btree_build_aux_trees(b);

	return invalidated_iter;
}

/*
 * Use this one if the node is intent locked:
 */
void bch2_btree_node_write(struct bch_fs *c, struct btree *b,
			  enum six_lock_type lock_type_held)
{
	BUG_ON(lock_type_held == SIX_LOCK_write);

	if (lock_type_held == SIX_LOCK_intent ||
	    six_lock_tryupgrade(&b->lock)) {
		__bch2_btree_node_write(c, b, SIX_LOCK_intent);

		/* don't cycle lock unnecessarily: */
		if (btree_node_just_written(b) &&
		    six_trylock_write(&b->lock)) {
			bch2_btree_post_write_cleanup(c, b);
			six_unlock_write(&b->lock);
		}

		if (lock_type_held == SIX_LOCK_read)
			six_lock_downgrade(&b->lock);
	} else {
		__bch2_btree_node_write(c, b, SIX_LOCK_read);
	}
}

static void __bch2_btree_flush_all(struct bch_fs *c, unsigned flag)
{
	struct bucket_table *tbl;
	struct rhash_head *pos;
	struct btree *b;
	unsigned i;
restart:
	rcu_read_lock();
	for_each_cached_btree(b, c, tbl, i, pos)
		if (test_bit(flag, &b->flags)) {
			rcu_read_unlock();
			wait_on_bit_io(&b->flags, flag, TASK_UNINTERRUPTIBLE);
			goto restart;

		}
	rcu_read_unlock();
}

void bch2_btree_flush_all_reads(struct bch_fs *c)
{
	__bch2_btree_flush_all(c, BTREE_NODE_read_in_flight);
}

void bch2_btree_flush_all_writes(struct bch_fs *c)
{
	__bch2_btree_flush_all(c, BTREE_NODE_write_in_flight);
}

void bch2_btree_verify_flushed(struct bch_fs *c)
{
	struct bucket_table *tbl;
	struct rhash_head *pos;
	struct btree *b;
	unsigned i;

	rcu_read_lock();
	for_each_cached_btree(b, c, tbl, i, pos) {
		unsigned long flags = READ_ONCE(b->flags);

		BUG_ON((flags & (1 << BTREE_NODE_dirty)) ||
		       (flags & (1 << BTREE_NODE_write_in_flight)));
	}
	rcu_read_unlock();
}

ssize_t bch2_dirty_btree_nodes_print(struct bch_fs *c, char *buf)
{
	struct printbuf out = _PBUF(buf, PAGE_SIZE);
	struct bucket_table *tbl;
	struct rhash_head *pos;
	struct btree *b;
	unsigned i;

	rcu_read_lock();
	for_each_cached_btree(b, c, tbl, i, pos) {
		unsigned long flags = READ_ONCE(b->flags);
		unsigned idx = (flags & (1 << BTREE_NODE_write_idx)) != 0;

		if (!(flags & (1 << BTREE_NODE_dirty)))
			continue;

		pr_buf(&out, "%p d %u n %u l %u w %u b %u r %u:%lu c %u p %u\n",
		       b,
		       (flags & (1 << BTREE_NODE_dirty)) != 0,
		       (flags & (1 << BTREE_NODE_need_write)) != 0,
		       b->level,
		       b->written,
		       !list_empty_careful(&b->write_blocked),
		       b->will_make_reachable != 0,
		       b->will_make_reachable & 1,
		       b->writes[ idx].wait.list.first != NULL,
		       b->writes[!idx].wait.list.first != NULL);
	}
	rcu_read_unlock();

	return out.pos - buf;
}
