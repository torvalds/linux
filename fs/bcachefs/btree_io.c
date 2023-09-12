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
#include "io_write.h"
#include "journal_reclaim.h"
#include "journal_seq_blacklist.h"
#include "recovery.h"
#include "super-io.h"
#include "trace.h"

#include <linux/sched/mm.h>

void bch2_btree_node_io_unlock(struct btree *b)
{
	EBUG_ON(!btree_node_write_in_flight(b));

	clear_btree_node_write_in_flight_inner(b);
	clear_btree_node_write_in_flight(b);
	wake_up_bit(&b->flags, BTREE_NODE_write_in_flight);
}

void bch2_btree_node_io_lock(struct btree *b)
{
	bch2_assert_btree_nodes_not_locked();

	wait_on_bit_lock_io(&b->flags, BTREE_NODE_write_in_flight,
			    TASK_UNINTERRUPTIBLE);
}

void __bch2_btree_node_wait_on_read(struct btree *b)
{
	wait_on_bit_io(&b->flags, BTREE_NODE_read_in_flight,
		       TASK_UNINTERRUPTIBLE);
}

void __bch2_btree_node_wait_on_write(struct btree *b)
{
	wait_on_bit_io(&b->flags, BTREE_NODE_write_in_flight,
		       TASK_UNINTERRUPTIBLE);
}

void bch2_btree_node_wait_on_read(struct btree *b)
{
	bch2_assert_btree_nodes_not_locked();

	wait_on_bit_io(&b->flags, BTREE_NODE_read_in_flight,
		       TASK_UNINTERRUPTIBLE);
}

void bch2_btree_node_wait_on_write(struct btree *b)
{
	bch2_assert_btree_nodes_not_locked();

	wait_on_bit_io(&b->flags, BTREE_NODE_write_in_flight,
		       TASK_UNINTERRUPTIBLE);
}

static void verify_no_dups(struct btree *b,
			   struct bkey_packed *start,
			   struct bkey_packed *end)
{
#ifdef CONFIG_BCACHEFS_DEBUG
	struct bkey_packed *k, *p;

	if (start == end)
		return;

	for (p = start, k = bkey_p_next(start);
	     k != end;
	     p = k, k = bkey_p_next(k)) {
		struct bkey l = bkey_unpack_key(b, p);
		struct bkey r = bkey_unpack_key(b, k);

		BUG_ON(bpos_ge(l.p, bkey_start_pos(&r)));
	}
#endif
}

static void set_needs_whiteout(struct bset *i, int v)
{
	struct bkey_packed *k;

	for (k = i->start; k != vstruct_last(i); k = bkey_p_next(k))
		k->needs_whiteout = v;
}

static void btree_bounce_free(struct bch_fs *c, size_t size,
			      bool used_mempool, void *p)
{
	if (used_mempool)
		mempool_free(p, &c->btree_bounce_pool);
	else
		vpfree(p, size);
}

static void *btree_bounce_alloc(struct bch_fs *c, size_t size,
				bool *used_mempool)
{
	unsigned flags = memalloc_nofs_save();
	void *p;

	BUG_ON(size > btree_bytes(c));

	*used_mempool = false;
	p = vpmalloc(size, __GFP_NOWARN|GFP_NOWAIT);
	if (!p) {
		*used_mempool = true;
		p = mempool_alloc(&c->btree_bounce_pool, GFP_NOFS);
	}
	memalloc_nofs_restore(flags);
	return p;
}

static void sort_bkey_ptrs(const struct btree *bt,
			   struct bkey_packed **ptrs, unsigned nr)
{
	unsigned n = nr, a = nr / 2, b, c, d;

	if (!a)
		return;

	/* Heap sort: see lib/sort.c: */
	while (1) {
		if (a)
			a--;
		else if (--n)
			swap(ptrs[0], ptrs[n]);
		else
			break;

		for (b = a; c = 2 * b + 1, (d = c + 1) < n;)
			b = bch2_bkey_cmp_packed(bt,
					    ptrs[c],
					    ptrs[d]) >= 0 ? c : d;
		if (d == n)
			b = c;

		while (b != a &&
		       bch2_bkey_cmp_packed(bt,
				       ptrs[a],
				       ptrs[b]) >= 0)
			b = (b - 1) / 2;
		c = b;
		while (b != a) {
			b = (b - 1) / 2;
			swap(ptrs[b], ptrs[c]);
		}
	}
}

static void bch2_sort_whiteouts(struct bch_fs *c, struct btree *b)
{
	struct bkey_packed *new_whiteouts, **ptrs, **ptrs_end, *k;
	bool used_mempool = false;
	size_t bytes = b->whiteout_u64s * sizeof(u64);

	if (!b->whiteout_u64s)
		return;

	new_whiteouts = btree_bounce_alloc(c, bytes, &used_mempool);

	ptrs = ptrs_end = ((void *) new_whiteouts + bytes);

	for (k = unwritten_whiteouts_start(c, b);
	     k != unwritten_whiteouts_end(c, b);
	     k = bkey_p_next(k))
		*--ptrs = k;

	sort_bkey_ptrs(b, ptrs, ptrs_end - ptrs);

	k = new_whiteouts;

	while (ptrs != ptrs_end) {
		bkey_copy(k, *ptrs);
		k = bkey_p_next(k);
		ptrs++;
	}

	verify_no_dups(b, new_whiteouts,
		       (void *) ((u64 *) new_whiteouts + b->whiteout_u64s));

	memcpy_u64s(unwritten_whiteouts_start(c, b),
		    new_whiteouts, b->whiteout_u64s);

	btree_bounce_free(c, bytes, used_mempool, new_whiteouts);
}

static bool should_compact_bset(struct btree *b, struct bset_tree *t,
				bool compacting, enum compact_mode mode)
{
	if (!bset_dead_u64s(b, t))
		return false;

	switch (mode) {
	case COMPACT_LAZY:
		return should_compact_bset_lazy(b, t) ||
			(compacting && !bset_written(b, bset(b, t)));
	case COMPACT_ALL:
		return true;
	default:
		BUG();
	}
}

static bool bch2_drop_whiteouts(struct btree *b, enum compact_mode mode)
{
	struct bset_tree *t;
	bool ret = false;

	for_each_bset(b, t) {
		struct bset *i = bset(b, t);
		struct bkey_packed *k, *n, *out, *start, *end;
		struct btree_node_entry *src = NULL, *dst = NULL;

		if (t != b->set && !bset_written(b, i)) {
			src = container_of(i, struct btree_node_entry, keys);
			dst = max(write_block(b),
				  (void *) btree_bkey_last(b, t - 1));
		}

		if (src != dst)
			ret = true;

		if (!should_compact_bset(b, t, ret, mode)) {
			if (src != dst) {
				memmove(dst, src, sizeof(*src) +
					le16_to_cpu(src->keys.u64s) *
					sizeof(u64));
				i = &dst->keys;
				set_btree_bset(b, t, i);
			}
			continue;
		}

		start	= btree_bkey_first(b, t);
		end	= btree_bkey_last(b, t);

		if (src != dst) {
			memmove(dst, src, sizeof(*src));
			i = &dst->keys;
			set_btree_bset(b, t, i);
		}

		out = i->start;

		for (k = start; k != end; k = n) {
			n = bkey_p_next(k);

			if (!bkey_deleted(k)) {
				bkey_copy(out, k);
				out = bkey_p_next(out);
			} else {
				BUG_ON(k->needs_whiteout);
			}
		}

		i->u64s = cpu_to_le16((u64 *) out - i->_data);
		set_btree_bset_end(b, t);
		bch2_bset_set_no_aux_tree(b, t);
		ret = true;
	}

	bch2_verify_btree_nr_keys(b);

	bch2_btree_build_aux_trees(b);

	return ret;
}

bool bch2_compact_whiteouts(struct bch_fs *c, struct btree *b,
			    enum compact_mode mode)
{
	return bch2_drop_whiteouts(b, mode);
}

static void btree_node_sort(struct bch_fs *c, struct btree *b,
			    unsigned start_idx,
			    unsigned end_idx,
			    bool filter_whiteouts)
{
	struct btree_node *out;
	struct sort_iter_stack sort_iter;
	struct bset_tree *t;
	struct bset *start_bset = bset(b, &b->set[start_idx]);
	bool used_mempool = false;
	u64 start_time, seq = 0;
	unsigned i, u64s = 0, bytes, shift = end_idx - start_idx - 1;
	bool sorting_entire_node = start_idx == 0 &&
		end_idx == b->nsets;

	sort_iter_stack_init(&sort_iter, b);

	for (t = b->set + start_idx;
	     t < b->set + end_idx;
	     t++) {
		u64s += le16_to_cpu(bset(b, t)->u64s);
		sort_iter_add(&sort_iter.iter,
			      btree_bkey_first(b, t),
			      btree_bkey_last(b, t));
	}

	bytes = sorting_entire_node
		? btree_bytes(c)
		: __vstruct_bytes(struct btree_node, u64s);

	out = btree_bounce_alloc(c, bytes, &used_mempool);

	start_time = local_clock();

	u64s = bch2_sort_keys(out->keys.start, &sort_iter.iter, filter_whiteouts);

	out->keys.u64s = cpu_to_le16(u64s);

	BUG_ON(vstruct_end(&out->keys) > (void *) out + bytes);

	if (sorting_entire_node)
		bch2_time_stats_update(&c->times[BCH_TIME_btree_node_sort],
				       start_time);

	/* Make sure we preserve bset journal_seq: */
	for (t = b->set + start_idx; t < b->set + end_idx; t++)
		seq = max(seq, le64_to_cpu(bset(b, t)->journal_seq));
	start_bset->journal_seq = cpu_to_le64(seq);

	if (sorting_entire_node) {
		u64s = le16_to_cpu(out->keys.u64s);

		BUG_ON(bytes != btree_bytes(c));

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

	btree_bounce_free(c, bytes, used_mempool, out);

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

/*
 * We're about to add another bset to the btree node, so if there's currently
 * too many bsets - sort some of them together:
 */
static bool btree_node_compact(struct bch_fs *c, struct btree *b)
{
	unsigned unwritten_idx;
	bool ret = false;

	for (unwritten_idx = 0;
	     unwritten_idx < b->nsets;
	     unwritten_idx++)
		if (!bset_written(b, bset(b, &b->set[unwritten_idx])))
			break;

	if (b->nsets - unwritten_idx > 1) {
		btree_node_sort(c, b, unwritten_idx,
				b->nsets, false);
		ret = true;
	}

	if (unwritten_idx > 1) {
		btree_node_sort(c, b, 0, unwritten_idx, false);
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
 * If we have MAX_BSETS (3) bsets, should we sort them all down to just one?
 *
 * The first bset is going to be of similar order to the size of the node, the
 * last bset is bounded by btree_write_set_buffer(), which is set to keep the
 * memmove on insert from being too expensive: the middle bset should, ideally,
 * be the geometric mean of the first and the last.
 *
 * Returns true if the middle bset is greater than that geometric mean:
 */
static inline bool should_compact_all(struct bch_fs *c, struct btree *b)
{
	unsigned mid_u64s_bits =
		(ilog2(btree_max_u64s(c)) + BTREE_WRITE_SET_U64s_BITS) / 2;

	return bset_u64s(&b->set[1]) > 1U << mid_u64s_bits;
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
void bch2_btree_init_next(struct btree_trans *trans, struct btree *b)
{
	struct bch_fs *c = trans->c;
	struct btree_node_entry *bne;
	bool reinit_iter = false;

	EBUG_ON(!six_lock_counts(&b->c.lock).n[SIX_LOCK_write]);
	BUG_ON(bset_written(b, bset(b, &b->set[1])));
	BUG_ON(btree_node_just_written(b));

	if (b->nsets == MAX_BSETS &&
	    !btree_node_write_in_flight(b) &&
	    should_compact_all(c, b)) {
		bch2_btree_node_write(c, b, SIX_LOCK_write,
				      BTREE_WRITE_init_next_bset);
		reinit_iter = true;
	}

	if (b->nsets == MAX_BSETS &&
	    btree_node_compact(c, b))
		reinit_iter = true;

	BUG_ON(b->nsets >= MAX_BSETS);

	bne = want_new_bset(c, b);
	if (bne)
		bch2_bset_init_next(c, b, bne);

	bch2_btree_build_aux_trees(b);

	if (reinit_iter)
		bch2_trans_node_reinit_iter(trans, b);
}

static void btree_pos_to_text(struct printbuf *out, struct bch_fs *c,
			  struct btree *b)
{
	prt_printf(out, "%s level %u/%u\n  ",
	       bch2_btree_ids[b->c.btree_id],
	       b->c.level,
	       bch2_btree_id_root(c, b->c.btree_id)->level);
	bch2_bkey_val_to_text(out, c, bkey_i_to_s_c(&b->key));
}

static void btree_err_msg(struct printbuf *out, struct bch_fs *c,
			  struct bch_dev *ca,
			  struct btree *b, struct bset *i,
			  unsigned offset, int write)
{
	prt_printf(out, bch2_log_msg(c, "%s"),
		   write == READ
		   ? "error validating btree node "
		   : "corrupt btree node before write ");
	if (ca)
		prt_printf(out, "on %s ", ca->name);
	prt_printf(out, "at btree ");
	btree_pos_to_text(out, c, b);

	prt_printf(out, "\n  node offset %u", b->written);
	if (i)
		prt_printf(out, " bset u64s %u", le16_to_cpu(i->u64s));
	prt_str(out, ": ");
}

__printf(8, 9)
static int __btree_err(int ret,
		       struct bch_fs *c,
		       struct bch_dev *ca,
		       struct btree *b,
		       struct bset *i,
		       int write,
		       bool have_retry,
		       const char *fmt, ...)
{
	struct printbuf out = PRINTBUF;
	va_list args;

	btree_err_msg(&out, c, ca, b, i, b->written, write);

	va_start(args, fmt);
	prt_vprintf(&out, fmt, args);
	va_end(args);

	if (write == WRITE) {
		bch2_print_string_as_lines(KERN_ERR, out.buf);
		ret = c->opts.errors == BCH_ON_ERROR_continue
			? 0
			: -BCH_ERR_fsck_errors_not_fixed;
		goto out;
	}

	if (!have_retry && ret == -BCH_ERR_btree_node_read_err_want_retry)
		ret = -BCH_ERR_btree_node_read_err_fixable;
	if (!have_retry && ret == -BCH_ERR_btree_node_read_err_must_retry)
		ret = -BCH_ERR_btree_node_read_err_bad_node;

	switch (ret) {
	case -BCH_ERR_btree_node_read_err_fixable:
		mustfix_fsck_err(c, "%s", out.buf);
		ret = -BCH_ERR_fsck_fix;
		break;
	case -BCH_ERR_btree_node_read_err_want_retry:
	case -BCH_ERR_btree_node_read_err_must_retry:
		bch2_print_string_as_lines(KERN_ERR, out.buf);
		break;
	case -BCH_ERR_btree_node_read_err_bad_node:
		bch2_print_string_as_lines(KERN_ERR, out.buf);
		bch2_topology_error(c);
		ret = bch2_run_explicit_recovery_pass(c, BCH_RECOVERY_PASS_check_topology) ?: -EIO;
		break;
	case -BCH_ERR_btree_node_read_err_incompatible:
		bch2_print_string_as_lines(KERN_ERR, out.buf);
		ret = -BCH_ERR_fsck_errors_not_fixed;
		break;
	default:
		BUG();
	}
out:
fsck_err:
	printbuf_exit(&out);
	return ret;
}

#define btree_err(type, c, ca, b, i, msg, ...)				\
({									\
	int _ret = __btree_err(type, c, ca, b, i, write, have_retry, msg, ##__VA_ARGS__);\
									\
	if (_ret != -BCH_ERR_fsck_fix) {				\
		ret = _ret;						\
		goto fsck_err;						\
	}								\
									\
	*saw_error = true;						\
})

#define btree_err_on(cond, ...)	((cond) ? btree_err(__VA_ARGS__) : false)

/*
 * When btree topology repair changes the start or end of a node, that might
 * mean we have to drop keys that are no longer inside the node:
 */
__cold
void bch2_btree_node_drop_keys_outside_node(struct btree *b)
{
	struct bset_tree *t;

	for_each_bset(b, t) {
		struct bset *i = bset(b, t);
		struct bkey_packed *k;

		for (k = i->start; k != vstruct_last(i); k = bkey_p_next(k))
			if (bkey_cmp_left_packed(b, k, &b->data->min_key) >= 0)
				break;

		if (k != i->start) {
			unsigned shift = (u64 *) k - (u64 *) i->start;

			memmove_u64s_down(i->start, k,
					  (u64 *) vstruct_end(i) - (u64 *) k);
			i->u64s = cpu_to_le16(le16_to_cpu(i->u64s) - shift);
			set_btree_bset_end(b, t);
		}

		for (k = i->start; k != vstruct_last(i); k = bkey_p_next(k))
			if (bkey_cmp_left_packed(b, k, &b->data->max_key) > 0)
				break;

		if (k != vstruct_last(i)) {
			i->u64s = cpu_to_le16((u64 *) k - (u64 *) i->start);
			set_btree_bset_end(b, t);
		}
	}

	/*
	 * Always rebuild search trees: eytzinger search tree nodes directly
	 * depend on the values of min/max key:
	 */
	bch2_bset_set_no_aux_tree(b, b->set);
	bch2_btree_build_aux_trees(b);

	struct bkey_s_c k;
	struct bkey unpacked;
	struct btree_node_iter iter;
	for_each_btree_node_key_unpack(b, k, &iter, &unpacked) {
		BUG_ON(bpos_lt(k.k->p, b->data->min_key));
		BUG_ON(bpos_gt(k.k->p, b->data->max_key));
	}
}

static int validate_bset(struct bch_fs *c, struct bch_dev *ca,
			 struct btree *b, struct bset *i,
			 unsigned offset, unsigned sectors,
			 int write, bool have_retry, bool *saw_error)
{
	unsigned version = le16_to_cpu(i->version);
	struct printbuf buf1 = PRINTBUF;
	struct printbuf buf2 = PRINTBUF;
	int ret = 0;

	btree_err_on(!bch2_version_compatible(version),
		     -BCH_ERR_btree_node_read_err_incompatible, c, ca, b, i,
		     "unsupported bset version %u.%u",
		     BCH_VERSION_MAJOR(version),
		     BCH_VERSION_MINOR(version));

	if (btree_err_on(version < c->sb.version_min,
			 -BCH_ERR_btree_node_read_err_fixable, c, NULL, b, i,
			 "bset version %u older than superblock version_min %u",
			 version, c->sb.version_min)) {
		mutex_lock(&c->sb_lock);
		c->disk_sb.sb->version_min = cpu_to_le16(version);
		bch2_write_super(c);
		mutex_unlock(&c->sb_lock);
	}

	if (btree_err_on(BCH_VERSION_MAJOR(version) >
			 BCH_VERSION_MAJOR(c->sb.version),
			 -BCH_ERR_btree_node_read_err_fixable, c, NULL, b, i,
			 "bset version %u newer than superblock version %u",
			 version, c->sb.version)) {
		mutex_lock(&c->sb_lock);
		c->disk_sb.sb->version = cpu_to_le16(version);
		bch2_write_super(c);
		mutex_unlock(&c->sb_lock);
	}

	btree_err_on(BSET_SEPARATE_WHITEOUTS(i),
		     -BCH_ERR_btree_node_read_err_incompatible, c, ca, b, i,
		     "BSET_SEPARATE_WHITEOUTS no longer supported");

	if (btree_err_on(offset + sectors > btree_sectors(c),
			 -BCH_ERR_btree_node_read_err_fixable, c, ca, b, i,
			 "bset past end of btree node")) {
		i->u64s = 0;
		ret = 0;
		goto out;
	}

	btree_err_on(offset && !i->u64s,
		     -BCH_ERR_btree_node_read_err_fixable, c, ca, b, i,
		     "empty bset");

	btree_err_on(BSET_OFFSET(i) &&
		     BSET_OFFSET(i) != offset,
		     -BCH_ERR_btree_node_read_err_want_retry, c, ca, b, i,
		     "bset at wrong sector offset");

	if (!offset) {
		struct btree_node *bn =
			container_of(i, struct btree_node, keys);
		/* These indicate that we read the wrong btree node: */

		if (b->key.k.type == KEY_TYPE_btree_ptr_v2) {
			struct bch_btree_ptr_v2 *bp =
				&bkey_i_to_btree_ptr_v2(&b->key)->v;

			/* XXX endianness */
			btree_err_on(bp->seq != bn->keys.seq,
				     -BCH_ERR_btree_node_read_err_must_retry, c, ca, b, NULL,
				     "incorrect sequence number (wrong btree node)");
		}

		btree_err_on(BTREE_NODE_ID(bn) != b->c.btree_id,
			     -BCH_ERR_btree_node_read_err_must_retry, c, ca, b, i,
			     "incorrect btree id");

		btree_err_on(BTREE_NODE_LEVEL(bn) != b->c.level,
			     -BCH_ERR_btree_node_read_err_must_retry, c, ca, b, i,
			     "incorrect level");

		if (!write)
			compat_btree_node(b->c.level, b->c.btree_id, version,
					  BSET_BIG_ENDIAN(i), write, bn);

		if (b->key.k.type == KEY_TYPE_btree_ptr_v2) {
			struct bch_btree_ptr_v2 *bp =
				&bkey_i_to_btree_ptr_v2(&b->key)->v;

			if (BTREE_PTR_RANGE_UPDATED(bp)) {
				b->data->min_key = bp->min_key;
				b->data->max_key = b->key.k.p;
			}

			btree_err_on(!bpos_eq(b->data->min_key, bp->min_key),
				     -BCH_ERR_btree_node_read_err_must_retry, c, ca, b, NULL,
				     "incorrect min_key: got %s should be %s",
				     (printbuf_reset(&buf1),
				      bch2_bpos_to_text(&buf1, bn->min_key), buf1.buf),
				     (printbuf_reset(&buf2),
				      bch2_bpos_to_text(&buf2, bp->min_key), buf2.buf));
		}

		btree_err_on(!bpos_eq(bn->max_key, b->key.k.p),
			     -BCH_ERR_btree_node_read_err_must_retry, c, ca, b, i,
			     "incorrect max key %s",
			     (printbuf_reset(&buf1),
			      bch2_bpos_to_text(&buf1, bn->max_key), buf1.buf));

		if (write)
			compat_btree_node(b->c.level, b->c.btree_id, version,
					  BSET_BIG_ENDIAN(i), write, bn);

		btree_err_on(bch2_bkey_format_invalid(c, &bn->format, write, &buf1),
			     -BCH_ERR_btree_node_read_err_bad_node, c, ca, b, i,
			     "invalid bkey format: %s\n  %s", buf1.buf,
			     (printbuf_reset(&buf2),
			      bch2_bkey_format_to_text(&buf2, &bn->format), buf2.buf));
		printbuf_reset(&buf1);

		compat_bformat(b->c.level, b->c.btree_id, version,
			       BSET_BIG_ENDIAN(i), write,
			       &bn->format);
	}
out:
fsck_err:
	printbuf_exit(&buf2);
	printbuf_exit(&buf1);
	return ret;
}

static int bset_key_invalid(struct bch_fs *c, struct btree *b,
			    struct bkey_s_c k,
			    bool updated_range, int rw,
			    struct printbuf *err)
{
	return __bch2_bkey_invalid(c, k, btree_node_type(b), READ, err) ?:
		(!updated_range ? bch2_bkey_in_btree_node(b, k, err) : 0) ?:
		(rw == WRITE ? bch2_bkey_val_invalid(c, k, READ, err) : 0);
}

static int validate_bset_keys(struct bch_fs *c, struct btree *b,
			 struct bset *i, int write,
			 bool have_retry, bool *saw_error)
{
	unsigned version = le16_to_cpu(i->version);
	struct bkey_packed *k, *prev = NULL;
	struct printbuf buf = PRINTBUF;
	bool updated_range = b->key.k.type == KEY_TYPE_btree_ptr_v2 &&
		BTREE_PTR_RANGE_UPDATED(&bkey_i_to_btree_ptr_v2(&b->key)->v);
	int ret = 0;

	for (k = i->start;
	     k != vstruct_last(i);) {
		struct bkey_s u;
		struct bkey tmp;

		if (btree_err_on(bkey_p_next(k) > vstruct_last(i),
				 -BCH_ERR_btree_node_read_err_fixable, c, NULL, b, i,
				 "key extends past end of bset")) {
			i->u64s = cpu_to_le16((u64 *) k - i->_data);
			break;
		}

		if (btree_err_on(k->format > KEY_FORMAT_CURRENT,
				 -BCH_ERR_btree_node_read_err_fixable, c, NULL, b, i,
				 "invalid bkey format %u", k->format)) {
			i->u64s = cpu_to_le16(le16_to_cpu(i->u64s) - k->u64s);
			memmove_u64s_down(k, bkey_p_next(k),
					  (u64 *) vstruct_end(i) - (u64 *) k);
			continue;
		}

		/* XXX: validate k->u64s */
		if (!write)
			bch2_bkey_compat(b->c.level, b->c.btree_id, version,
				    BSET_BIG_ENDIAN(i), write,
				    &b->format, k);

		u = __bkey_disassemble(b, k, &tmp);

		printbuf_reset(&buf);
		if (bset_key_invalid(c, b, u.s_c, updated_range, write, &buf)) {
			printbuf_reset(&buf);
			prt_printf(&buf, "invalid bkey:  ");
			bset_key_invalid(c, b, u.s_c, updated_range, write, &buf);
			prt_printf(&buf, "\n  ");
			bch2_bkey_val_to_text(&buf, c, u.s_c);

			btree_err(-BCH_ERR_btree_node_read_err_fixable, c, NULL, b, i, "%s", buf.buf);

			i->u64s = cpu_to_le16(le16_to_cpu(i->u64s) - k->u64s);
			memmove_u64s_down(k, bkey_p_next(k),
					  (u64 *) vstruct_end(i) - (u64 *) k);
			continue;
		}

		if (write)
			bch2_bkey_compat(b->c.level, b->c.btree_id, version,
				    BSET_BIG_ENDIAN(i), write,
				    &b->format, k);

		if (prev && bkey_iter_cmp(b, prev, k) > 0) {
			struct bkey up = bkey_unpack_key(b, prev);

			printbuf_reset(&buf);
			prt_printf(&buf, "keys out of order: ");
			bch2_bkey_to_text(&buf, &up);
			prt_printf(&buf, " > ");
			bch2_bkey_to_text(&buf, u.k);

			bch2_dump_bset(c, b, i, 0);

			if (btree_err(-BCH_ERR_btree_node_read_err_fixable, c, NULL, b, i, "%s", buf.buf)) {
				i->u64s = cpu_to_le16(le16_to_cpu(i->u64s) - k->u64s);
				memmove_u64s_down(k, bkey_p_next(k),
						  (u64 *) vstruct_end(i) - (u64 *) k);
				continue;
			}
		}

		prev = k;
		k = bkey_p_next(k);
	}
fsck_err:
	printbuf_exit(&buf);
	return ret;
}

int bch2_btree_node_read_done(struct bch_fs *c, struct bch_dev *ca,
			      struct btree *b, bool have_retry, bool *saw_error)
{
	struct btree_node_entry *bne;
	struct sort_iter *iter;
	struct btree_node *sorted;
	struct bkey_packed *k;
	struct bch_extent_ptr *ptr;
	struct bset *i;
	bool used_mempool, blacklisted;
	bool updated_range = b->key.k.type == KEY_TYPE_btree_ptr_v2 &&
		BTREE_PTR_RANGE_UPDATED(&bkey_i_to_btree_ptr_v2(&b->key)->v);
	unsigned u64s;
	unsigned ptr_written = btree_ptr_sectors_written(&b->key);
	struct printbuf buf = PRINTBUF;
	int ret = 0, retry_read = 0, write = READ;

	b->version_ondisk = U16_MAX;
	/* We might get called multiple times on read retry: */
	b->written = 0;

	iter = mempool_alloc(&c->fill_iter, GFP_NOFS);
	sort_iter_init(iter, b, (btree_blocks(c) + 1) * 2);

	if (bch2_meta_read_fault("btree"))
		btree_err(-BCH_ERR_btree_node_read_err_must_retry, c, ca, b, NULL,
			  "dynamic fault");

	btree_err_on(le64_to_cpu(b->data->magic) != bset_magic(c),
		     -BCH_ERR_btree_node_read_err_must_retry, c, ca, b, NULL,
		     "bad magic: want %llx, got %llx",
		     bset_magic(c), le64_to_cpu(b->data->magic));

	btree_err_on(!b->data->keys.seq,
		     -BCH_ERR_btree_node_read_err_must_retry, c, ca, b, NULL,
		     "bad btree header: seq 0");

	if (b->key.k.type == KEY_TYPE_btree_ptr_v2) {
		struct bch_btree_ptr_v2 *bp =
			&bkey_i_to_btree_ptr_v2(&b->key)->v;

		btree_err_on(b->data->keys.seq != bp->seq,
			     -BCH_ERR_btree_node_read_err_must_retry, c, ca, b, NULL,
			     "got wrong btree node (seq %llx want %llx)",
			     b->data->keys.seq, bp->seq);
	}

	while (b->written < (ptr_written ?: btree_sectors(c))) {
		unsigned sectors;
		struct nonce nonce;
		struct bch_csum csum;
		bool first = !b->written;

		if (!b->written) {
			i = &b->data->keys;

			btree_err_on(!bch2_checksum_type_valid(c, BSET_CSUM_TYPE(i)),
				     -BCH_ERR_btree_node_read_err_want_retry, c, ca, b, i,
				     "unknown checksum type %llu",
				     BSET_CSUM_TYPE(i));

			nonce = btree_nonce(i, b->written << 9);
			csum = csum_vstruct(c, BSET_CSUM_TYPE(i), nonce, b->data);

			btree_err_on(bch2_crc_cmp(csum, b->data->csum),
				     -BCH_ERR_btree_node_read_err_want_retry, c, ca, b, i,
				     "invalid checksum");

			ret = bset_encrypt(c, i, b->written << 9);
			if (bch2_fs_fatal_err_on(ret, c,
					"error decrypting btree node: %i", ret))
				goto fsck_err;

			btree_err_on(btree_node_type_is_extents(btree_node_type(b)) &&
				     !BTREE_NODE_NEW_EXTENT_OVERWRITE(b->data),
				     -BCH_ERR_btree_node_read_err_incompatible, c, NULL, b, NULL,
				     "btree node does not have NEW_EXTENT_OVERWRITE set");

			sectors = vstruct_sectors(b->data, c->block_bits);
		} else {
			bne = write_block(b);
			i = &bne->keys;

			if (i->seq != b->data->keys.seq)
				break;

			btree_err_on(!bch2_checksum_type_valid(c, BSET_CSUM_TYPE(i)),
				     -BCH_ERR_btree_node_read_err_want_retry, c, ca, b, i,
				     "unknown checksum type %llu",
				     BSET_CSUM_TYPE(i));

			nonce = btree_nonce(i, b->written << 9);
			csum = csum_vstruct(c, BSET_CSUM_TYPE(i), nonce, bne);

			btree_err_on(bch2_crc_cmp(csum, bne->csum),
				     -BCH_ERR_btree_node_read_err_want_retry, c, ca, b, i,
				     "invalid checksum");

			ret = bset_encrypt(c, i, b->written << 9);
			if (bch2_fs_fatal_err_on(ret, c,
					"error decrypting btree node: %i\n", ret))
				goto fsck_err;

			sectors = vstruct_sectors(bne, c->block_bits);
		}

		b->version_ondisk = min(b->version_ondisk,
					le16_to_cpu(i->version));

		ret = validate_bset(c, ca, b, i, b->written, sectors,
				    READ, have_retry, saw_error);
		if (ret)
			goto fsck_err;

		if (!b->written)
			btree_node_set_format(b, b->data->format);

		ret = validate_bset_keys(c, b, i, READ, have_retry, saw_error);
		if (ret)
			goto fsck_err;

		SET_BSET_BIG_ENDIAN(i, CPU_BIG_ENDIAN);

		blacklisted = bch2_journal_seq_is_blacklisted(c,
					le64_to_cpu(i->journal_seq),
					true);

		btree_err_on(blacklisted && first,
			     -BCH_ERR_btree_node_read_err_fixable, c, ca, b, i,
			     "first btree node bset has blacklisted journal seq (%llu)",
			     le64_to_cpu(i->journal_seq));

		btree_err_on(blacklisted && ptr_written,
			     -BCH_ERR_btree_node_read_err_fixable, c, ca, b, i,
			     "found blacklisted bset (journal seq %llu) in btree node at offset %u-%u/%u",
			     le64_to_cpu(i->journal_seq),
			     b->written, b->written + sectors, ptr_written);

		b->written += sectors;

		if (blacklisted && !first)
			continue;

		sort_iter_add(iter,
			      vstruct_idx(i, 0),
			      vstruct_last(i));
	}

	if (ptr_written) {
		btree_err_on(b->written < ptr_written,
			     -BCH_ERR_btree_node_read_err_want_retry, c, ca, b, NULL,
			     "btree node data missing: expected %u sectors, found %u",
			     ptr_written, b->written);
	} else {
		for (bne = write_block(b);
		     bset_byte_offset(b, bne) < btree_bytes(c);
		     bne = (void *) bne + block_bytes(c))
			btree_err_on(bne->keys.seq == b->data->keys.seq &&
				     !bch2_journal_seq_is_blacklisted(c,
								      le64_to_cpu(bne->keys.journal_seq),
								      true),
				     -BCH_ERR_btree_node_read_err_want_retry, c, ca, b, NULL,
				     "found bset signature after last bset");
	}

	sorted = btree_bounce_alloc(c, btree_bytes(c), &used_mempool);
	sorted->keys.u64s = 0;

	set_btree_bset(b, b->set, &b->data->keys);

	b->nr = bch2_key_sort_fix_overlapping(c, &sorted->keys, iter);

	u64s = le16_to_cpu(sorted->keys.u64s);
	*sorted = *b->data;
	sorted->keys.u64s = cpu_to_le16(u64s);
	swap(sorted, b->data);
	set_btree_bset(b, b->set, &b->data->keys);
	b->nsets = 1;

	BUG_ON(b->nr.live_u64s != u64s);

	btree_bounce_free(c, btree_bytes(c), used_mempool, sorted);

	if (updated_range)
		bch2_btree_node_drop_keys_outside_node(b);

	i = &b->data->keys;
	for (k = i->start; k != vstruct_last(i);) {
		struct bkey tmp;
		struct bkey_s u = __bkey_disassemble(b, k, &tmp);

		printbuf_reset(&buf);

		if (bch2_bkey_val_invalid(c, u.s_c, READ, &buf) ||
		    (bch2_inject_invalid_keys &&
		     !bversion_cmp(u.k->version, MAX_VERSION))) {
			printbuf_reset(&buf);

			prt_printf(&buf, "invalid bkey: ");
			bch2_bkey_val_invalid(c, u.s_c, READ, &buf);
			prt_printf(&buf, "\n  ");
			bch2_bkey_val_to_text(&buf, c, u.s_c);

			btree_err(-BCH_ERR_btree_node_read_err_fixable, c, NULL, b, i, "%s", buf.buf);

			btree_keys_account_key_drop(&b->nr, 0, k);

			i->u64s = cpu_to_le16(le16_to_cpu(i->u64s) - k->u64s);
			memmove_u64s_down(k, bkey_p_next(k),
					  (u64 *) vstruct_end(i) - (u64 *) k);
			set_btree_bset_end(b, b->set);
			continue;
		}

		if (u.k->type == KEY_TYPE_btree_ptr_v2) {
			struct bkey_s_btree_ptr_v2 bp = bkey_s_to_btree_ptr_v2(u);

			bp.v->mem_ptr = 0;
		}

		k = bkey_p_next(k);
	}

	bch2_bset_build_aux_tree(b, b->set, false);

	set_needs_whiteout(btree_bset_first(b), true);

	btree_node_reset_sib_u64s(b);

	bkey_for_each_ptr(bch2_bkey_ptrs(bkey_i_to_s(&b->key)), ptr) {
		struct bch_dev *ca2 = bch_dev_bkey_exists(c, ptr->dev);

		if (ca2->mi.state != BCH_MEMBER_STATE_rw)
			set_btree_node_need_rewrite(b);
	}

	if (!ptr_written)
		set_btree_node_need_rewrite(b);
out:
	mempool_free(iter, &c->fill_iter);
	printbuf_exit(&buf);
	return retry_read;
fsck_err:
	if (ret == -BCH_ERR_btree_node_read_err_want_retry ||
	    ret == -BCH_ERR_btree_node_read_err_must_retry)
		retry_read = 1;
	else
		set_btree_node_read_error(b);
	goto out;
}

static void btree_node_read_work(struct work_struct *work)
{
	struct btree_read_bio *rb =
		container_of(work, struct btree_read_bio, work);
	struct bch_fs *c	= rb->c;
	struct btree *b		= rb->b;
	struct bch_dev *ca	= bch_dev_bkey_exists(c, rb->pick.ptr.dev);
	struct bio *bio		= &rb->bio;
	struct bch_io_failures failed = { .nr = 0 };
	struct printbuf buf = PRINTBUF;
	bool saw_error = false;
	bool retry = false;
	bool can_retry;

	goto start;
	while (1) {
		retry = true;
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
		printbuf_reset(&buf);
		btree_pos_to_text(&buf, c, b);
		bch2_dev_io_err_on(bio->bi_status, ca, "btree read error %s for %s",
				   bch2_blk_status_to_str(bio->bi_status), buf.buf);
		if (rb->have_ioref)
			percpu_ref_put(&ca->io_ref);
		rb->have_ioref = false;

		bch2_mark_io_failure(&failed, &rb->pick);

		can_retry = bch2_bkey_pick_read_device(c,
				bkey_i_to_s_c(&b->key),
				&failed, &rb->pick) > 0;

		if (!bio->bi_status &&
		    !bch2_btree_node_read_done(c, ca, b, can_retry, &saw_error)) {
			if (retry)
				bch_info(c, "retry success");
			break;
		}

		saw_error = true;

		if (!can_retry) {
			set_btree_node_read_error(b);
			break;
		}
	}

	bch2_time_stats_update(&c->times[BCH_TIME_btree_node_read],
			       rb->start_time);
	bio_put(&rb->bio);

	if (saw_error && !btree_node_read_error(b)) {
		printbuf_reset(&buf);
		bch2_bpos_to_text(&buf, b->key.k.p);
		bch_info(c, "%s: rewriting btree node at btree=%s level=%u %s due to error",
			 __func__, bch2_btree_ids[b->c.btree_id], b->c.level, buf.buf);

		bch2_btree_node_rewrite_async(c, b);
	}

	printbuf_exit(&buf);
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

	queue_work(c->io_complete_wq, &rb->work);
}

struct btree_node_read_all {
	struct closure		cl;
	struct bch_fs		*c;
	struct btree		*b;
	unsigned		nr;
	void			*buf[BCH_REPLICAS_MAX];
	struct bio		*bio[BCH_REPLICAS_MAX];
	blk_status_t		err[BCH_REPLICAS_MAX];
};

static unsigned btree_node_sectors_written(struct bch_fs *c, void *data)
{
	struct btree_node *bn = data;
	struct btree_node_entry *bne;
	unsigned offset = 0;

	if (le64_to_cpu(bn->magic) !=  bset_magic(c))
		return 0;

	while (offset < btree_sectors(c)) {
		if (!offset) {
			offset += vstruct_sectors(bn, c->block_bits);
		} else {
			bne = data + (offset << 9);
			if (bne->keys.seq != bn->keys.seq)
				break;
			offset += vstruct_sectors(bne, c->block_bits);
		}
	}

	return offset;
}

static bool btree_node_has_extra_bsets(struct bch_fs *c, unsigned offset, void *data)
{
	struct btree_node *bn = data;
	struct btree_node_entry *bne;

	if (!offset)
		return false;

	while (offset < btree_sectors(c)) {
		bne = data + (offset << 9);
		if (bne->keys.seq == bn->keys.seq)
			return true;
		offset++;
	}

	return false;
	return offset;
}

static void btree_node_read_all_replicas_done(struct closure *cl)
{
	struct btree_node_read_all *ra =
		container_of(cl, struct btree_node_read_all, cl);
	struct bch_fs *c = ra->c;
	struct btree *b = ra->b;
	struct printbuf buf = PRINTBUF;
	bool dump_bset_maps = false;
	bool have_retry = false;
	int ret = 0, best = -1, write = READ;
	unsigned i, written = 0, written2 = 0;
	__le64 seq = b->key.k.type == KEY_TYPE_btree_ptr_v2
		? bkey_i_to_btree_ptr_v2(&b->key)->v.seq : 0;
	bool _saw_error = false, *saw_error = &_saw_error;

	for (i = 0; i < ra->nr; i++) {
		struct btree_node *bn = ra->buf[i];

		if (ra->err[i])
			continue;

		if (le64_to_cpu(bn->magic) != bset_magic(c) ||
		    (seq && seq != bn->keys.seq))
			continue;

		if (best < 0) {
			best = i;
			written = btree_node_sectors_written(c, bn);
			continue;
		}

		written2 = btree_node_sectors_written(c, ra->buf[i]);
		if (btree_err_on(written2 != written, -BCH_ERR_btree_node_read_err_fixable, c, NULL, b, NULL,
				 "btree node sectors written mismatch: %u != %u",
				 written, written2) ||
		    btree_err_on(btree_node_has_extra_bsets(c, written2, ra->buf[i]),
				 -BCH_ERR_btree_node_read_err_fixable, c, NULL, b, NULL,
				 "found bset signature after last bset") ||
		    btree_err_on(memcmp(ra->buf[best], ra->buf[i], written << 9),
				 -BCH_ERR_btree_node_read_err_fixable, c, NULL, b, NULL,
				 "btree node replicas content mismatch"))
			dump_bset_maps = true;

		if (written2 > written) {
			written = written2;
			best = i;
		}
	}
fsck_err:
	if (dump_bset_maps) {
		for (i = 0; i < ra->nr; i++) {
			struct btree_node *bn = ra->buf[i];
			struct btree_node_entry *bne = NULL;
			unsigned offset = 0, sectors;
			bool gap = false;

			if (ra->err[i])
				continue;

			printbuf_reset(&buf);

			while (offset < btree_sectors(c)) {
				if (!offset) {
					sectors = vstruct_sectors(bn, c->block_bits);
				} else {
					bne = ra->buf[i] + (offset << 9);
					if (bne->keys.seq != bn->keys.seq)
						break;
					sectors = vstruct_sectors(bne, c->block_bits);
				}

				prt_printf(&buf, " %u-%u", offset, offset + sectors);
				if (bne && bch2_journal_seq_is_blacklisted(c,
							le64_to_cpu(bne->keys.journal_seq), false))
					prt_printf(&buf, "*");
				offset += sectors;
			}

			while (offset < btree_sectors(c)) {
				bne = ra->buf[i] + (offset << 9);
				if (bne->keys.seq == bn->keys.seq) {
					if (!gap)
						prt_printf(&buf, " GAP");
					gap = true;

					sectors = vstruct_sectors(bne, c->block_bits);
					prt_printf(&buf, " %u-%u", offset, offset + sectors);
					if (bch2_journal_seq_is_blacklisted(c,
							le64_to_cpu(bne->keys.journal_seq), false))
						prt_printf(&buf, "*");
				}
				offset++;
			}

			bch_err(c, "replica %u:%s", i, buf.buf);
		}
	}

	if (best >= 0) {
		memcpy(b->data, ra->buf[best], btree_bytes(c));
		ret = bch2_btree_node_read_done(c, NULL, b, false, saw_error);
	} else {
		ret = -1;
	}

	if (ret)
		set_btree_node_read_error(b);
	else if (*saw_error)
		bch2_btree_node_rewrite_async(c, b);

	for (i = 0; i < ra->nr; i++) {
		mempool_free(ra->buf[i], &c->btree_bounce_pool);
		bio_put(ra->bio[i]);
	}

	closure_debug_destroy(&ra->cl);
	kfree(ra);
	printbuf_exit(&buf);

	clear_btree_node_read_in_flight(b);
	wake_up_bit(&b->flags, BTREE_NODE_read_in_flight);
}

static void btree_node_read_all_replicas_endio(struct bio *bio)
{
	struct btree_read_bio *rb =
		container_of(bio, struct btree_read_bio, bio);
	struct bch_fs *c	= rb->c;
	struct btree_node_read_all *ra = rb->ra;

	if (rb->have_ioref) {
		struct bch_dev *ca = bch_dev_bkey_exists(c, rb->pick.ptr.dev);

		bch2_latency_acct(ca, rb->start_time, READ);
	}

	ra->err[rb->idx] = bio->bi_status;
	closure_put(&ra->cl);
}

/*
 * XXX This allocates multiple times from the same mempools, and can deadlock
 * under sufficient memory pressure (but is only a debug path)
 */
static int btree_node_read_all_replicas(struct bch_fs *c, struct btree *b, bool sync)
{
	struct bkey_s_c k = bkey_i_to_s_c(&b->key);
	struct bkey_ptrs_c ptrs = bch2_bkey_ptrs_c(k);
	const union bch_extent_entry *entry;
	struct extent_ptr_decoded pick;
	struct btree_node_read_all *ra;
	unsigned i;

	ra = kzalloc(sizeof(*ra), GFP_NOFS);
	if (!ra)
		return -BCH_ERR_ENOMEM_btree_node_read_all_replicas;

	closure_init(&ra->cl, NULL);
	ra->c	= c;
	ra->b	= b;
	ra->nr	= bch2_bkey_nr_ptrs(k);

	for (i = 0; i < ra->nr; i++) {
		ra->buf[i] = mempool_alloc(&c->btree_bounce_pool, GFP_NOFS);
		ra->bio[i] = bio_alloc_bioset(NULL,
					      buf_pages(ra->buf[i], btree_bytes(c)),
					      REQ_OP_READ|REQ_SYNC|REQ_META,
					      GFP_NOFS,
					      &c->btree_bio);
	}

	i = 0;
	bkey_for_each_ptr_decode(k.k, ptrs, pick, entry) {
		struct bch_dev *ca = bch_dev_bkey_exists(c, pick.ptr.dev);
		struct btree_read_bio *rb =
			container_of(ra->bio[i], struct btree_read_bio, bio);
		rb->c			= c;
		rb->b			= b;
		rb->ra			= ra;
		rb->start_time		= local_clock();
		rb->have_ioref		= bch2_dev_get_ioref(ca, READ);
		rb->idx			= i;
		rb->pick		= pick;
		rb->bio.bi_iter.bi_sector = pick.ptr.offset;
		rb->bio.bi_end_io	= btree_node_read_all_replicas_endio;
		bch2_bio_map(&rb->bio, ra->buf[i], btree_bytes(c));

		if (rb->have_ioref) {
			this_cpu_add(ca->io_done->sectors[READ][BCH_DATA_btree],
				     bio_sectors(&rb->bio));
			bio_set_dev(&rb->bio, ca->disk_sb.bdev);

			closure_get(&ra->cl);
			submit_bio(&rb->bio);
		} else {
			ra->err[i] = BLK_STS_REMOVED;
		}

		i++;
	}

	if (sync) {
		closure_sync(&ra->cl);
		btree_node_read_all_replicas_done(&ra->cl);
	} else {
		continue_at(&ra->cl, btree_node_read_all_replicas_done,
			    c->io_complete_wq);
	}

	return 0;
}

void bch2_btree_node_read(struct bch_fs *c, struct btree *b,
			  bool sync)
{
	struct extent_ptr_decoded pick;
	struct btree_read_bio *rb;
	struct bch_dev *ca;
	struct bio *bio;
	int ret;

	trace_and_count(c, btree_node_read, c, b);

	if (bch2_verify_all_btree_replicas &&
	    !btree_node_read_all_replicas(c, b, sync))
		return;

	ret = bch2_bkey_pick_read_device(c, bkey_i_to_s_c(&b->key),
					 NULL, &pick);

	if (ret <= 0) {
		struct printbuf buf = PRINTBUF;

		prt_str(&buf, "btree node read error: no device to read from\n at ");
		btree_pos_to_text(&buf, c, b);
		bch_err(c, "%s", buf.buf);

		if (c->recovery_passes_explicit & BIT_ULL(BCH_RECOVERY_PASS_check_topology) &&
		    c->curr_recovery_pass > BCH_RECOVERY_PASS_check_topology)
			bch2_fatal_error(c);

		set_btree_node_read_error(b);
		clear_btree_node_read_in_flight(b);
		wake_up_bit(&b->flags, BTREE_NODE_read_in_flight);
		printbuf_exit(&buf);
		return;
	}

	ca = bch_dev_bkey_exists(c, pick.ptr.dev);

	bio = bio_alloc_bioset(NULL,
			       buf_pages(b->data, btree_bytes(c)),
			       REQ_OP_READ|REQ_SYNC|REQ_META,
			       GFP_NOFS,
			       &c->btree_bio);
	rb = container_of(bio, struct btree_read_bio, bio);
	rb->c			= c;
	rb->b			= b;
	rb->ra			= NULL;
	rb->start_time		= local_clock();
	rb->have_ioref		= bch2_dev_get_ioref(ca, READ);
	rb->pick		= pick;
	INIT_WORK(&rb->work, btree_node_read_work);
	bio->bi_iter.bi_sector	= pick.ptr.offset;
	bio->bi_end_io		= btree_node_read_endio;
	bch2_bio_map(bio, b->data, btree_bytes(c));

	if (rb->have_ioref) {
		this_cpu_add(ca->io_done->sectors[READ][BCH_DATA_btree],
			     bio_sectors(bio));
		bio_set_dev(bio, ca->disk_sb.bdev);

		if (sync) {
			submit_bio_wait(bio);

			btree_node_read_work(&rb->work);
		} else {
			submit_bio(bio);
		}
	} else {
		bio->bi_status = BLK_STS_REMOVED;

		if (sync)
			btree_node_read_work(&rb->work);
		else
			queue_work(c->io_complete_wq, &rb->work);
	}
}

static int __bch2_btree_root_read(struct btree_trans *trans, enum btree_id id,
				  const struct bkey_i *k, unsigned level)
{
	struct bch_fs *c = trans->c;
	struct closure cl;
	struct btree *b;
	int ret;

	closure_init_stack(&cl);

	do {
		ret = bch2_btree_cache_cannibalize_lock(c, &cl);
		closure_sync(&cl);
	} while (ret);

	b = bch2_btree_node_mem_alloc(trans, level != 0);
	bch2_btree_cache_cannibalize_unlock(c);

	BUG_ON(IS_ERR(b));

	bkey_copy(&b->key, k);
	BUG_ON(bch2_btree_node_hash_insert(&c->btree_cache, b, level, id));

	set_btree_node_read_in_flight(b);

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
	six_unlock_write(&b->c.lock);
	six_unlock_intent(&b->c.lock);

	return ret;
}

int bch2_btree_root_read(struct bch_fs *c, enum btree_id id,
			const struct bkey_i *k, unsigned level)
{
	return bch2_trans_run(c, __bch2_btree_root_read(trans, id, k, level));
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
}

static void __btree_node_write_done(struct bch_fs *c, struct btree *b)
{
	struct btree_write *w = btree_prev_write(b);
	unsigned long old, new, v;
	unsigned type = 0;

	bch2_btree_complete_write(c, b, w);

	v = READ_ONCE(b->flags);
	do {
		old = new = v;

		if ((old & (1U << BTREE_NODE_dirty)) &&
		    (old & (1U << BTREE_NODE_need_write)) &&
		    !(old & (1U << BTREE_NODE_never_write)) &&
		    !(old & (1U << BTREE_NODE_write_blocked)) &&
		    !(old & (1U << BTREE_NODE_will_make_reachable))) {
			new &= ~(1U << BTREE_NODE_dirty);
			new &= ~(1U << BTREE_NODE_need_write);
			new |=  (1U << BTREE_NODE_write_in_flight);
			new |=  (1U << BTREE_NODE_write_in_flight_inner);
			new |=  (1U << BTREE_NODE_just_written);
			new ^=  (1U << BTREE_NODE_write_idx);

			type = new & BTREE_WRITE_TYPE_MASK;
			new &= ~BTREE_WRITE_TYPE_MASK;
		} else {
			new &= ~(1U << BTREE_NODE_write_in_flight);
			new &= ~(1U << BTREE_NODE_write_in_flight_inner);
		}
	} while ((v = cmpxchg(&b->flags, old, new)) != old);

	if (new & (1U << BTREE_NODE_write_in_flight))
		__bch2_btree_node_write(c, b, BTREE_WRITE_ALREADY_STARTED|type);
	else
		wake_up_bit(&b->flags, BTREE_NODE_write_in_flight);
}

static void btree_node_write_done(struct bch_fs *c, struct btree *b)
{
	struct btree_trans *trans = bch2_trans_get(c);

	btree_node_lock_nopath_nofail(trans, &b->c, SIX_LOCK_read);
	__btree_node_write_done(c, b);
	six_unlock_read(&b->c.lock);

	bch2_trans_put(trans);
}

static void btree_node_write_work(struct work_struct *work)
{
	struct btree_write_bio *wbio =
		container_of(work, struct btree_write_bio, work);
	struct bch_fs *c	= wbio->wbio.c;
	struct btree *b		= wbio->wbio.bio.bi_private;
	struct bch_extent_ptr *ptr;
	int ret = 0;

	btree_bounce_free(c,
		wbio->data_bytes,
		wbio->wbio.used_mempool,
		wbio->data);

	bch2_bkey_drop_ptrs(bkey_i_to_s(&wbio->key), ptr,
		bch2_dev_list_has_dev(wbio->wbio.failed, ptr->dev));

	if (!bch2_bkey_nr_ptrs(bkey_i_to_s_c(&wbio->key)))
		goto err;

	if (wbio->wbio.first_btree_write) {
		if (wbio->wbio.failed.nr) {

		}
	} else {
		ret = bch2_trans_do(c, NULL, NULL, 0,
			bch2_btree_node_update_key_get_iter(trans, b, &wbio->key,
					BCH_WATERMARK_reclaim|
					BTREE_INSERT_JOURNAL_RECLAIM|
					BTREE_INSERT_NOFAIL|
					BTREE_INSERT_NOCHECK_RW,
					!wbio->wbio.failed.nr));
		if (ret)
			goto err;
	}
out:
	bio_put(&wbio->wbio.bio);
	btree_node_write_done(c, b);
	return;
err:
	set_btree_node_noevict(b);
	if (!bch2_err_matches(ret, EROFS))
		bch2_fs_fatal_error(c, "fatal error writing btree node: %s", bch2_err_str(ret));
	goto out;
}

static void btree_node_write_endio(struct bio *bio)
{
	struct bch_write_bio *wbio	= to_wbio(bio);
	struct bch_write_bio *parent	= wbio->split ? wbio->parent : NULL;
	struct bch_write_bio *orig	= parent ?: wbio;
	struct btree_write_bio *wb	= container_of(orig, struct btree_write_bio, wbio);
	struct bch_fs *c		= wbio->c;
	struct btree *b			= wbio->bio.bi_private;
	struct bch_dev *ca		= bch_dev_bkey_exists(c, wbio->dev);
	unsigned long flags;

	if (wbio->have_ioref)
		bch2_latency_acct(ca, wbio->submit_time, WRITE);

	if (bch2_dev_io_err_on(bio->bi_status, ca, "btree write error: %s",
			       bch2_blk_status_to_str(bio->bi_status)) ||
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
		return;
	}

	clear_btree_node_write_in_flight_inner(b);
	wake_up_bit(&b->flags, BTREE_NODE_write_in_flight_inner);
	INIT_WORK(&wb->work, btree_node_write_work);
	queue_work(c->btree_io_complete_wq, &wb->work);
}

static int validate_bset_for_write(struct bch_fs *c, struct btree *b,
				   struct bset *i, unsigned sectors)
{
	struct printbuf buf = PRINTBUF;
	bool saw_error;
	int ret;

	ret = bch2_bkey_invalid(c, bkey_i_to_s_c(&b->key),
				BKEY_TYPE_btree, WRITE, &buf);

	if (ret)
		bch2_fs_inconsistent(c, "invalid btree node key before write: %s", buf.buf);
	printbuf_exit(&buf);
	if (ret)
		return ret;

	ret = validate_bset_keys(c, b, i, WRITE, false, &saw_error) ?:
		validate_bset(c, NULL, b, i, b->written, sectors, WRITE, false, &saw_error);
	if (ret) {
		bch2_inconsistent_error(c);
		dump_stack();
	}

	return ret;
}

static void btree_write_submit(struct work_struct *work)
{
	struct btree_write_bio *wbio = container_of(work, struct btree_write_bio, work);
	struct bch_extent_ptr *ptr;
	BKEY_PADDED_ONSTACK(k, BKEY_BTREE_PTR_VAL_U64s_MAX) tmp;

	bkey_copy(&tmp.k, &wbio->key);

	bkey_for_each_ptr(bch2_bkey_ptrs(bkey_i_to_s(&tmp.k)), ptr)
		ptr->offset += wbio->sector_offset;

	bch2_submit_wbio_replicas(&wbio->wbio, wbio->wbio.c, BCH_DATA_btree,
				  &tmp.k, false);
}

void __bch2_btree_node_write(struct bch_fs *c, struct btree *b, unsigned flags)
{
	struct btree_write_bio *wbio;
	struct bset_tree *t;
	struct bset *i;
	struct btree_node *bn = NULL;
	struct btree_node_entry *bne = NULL;
	struct sort_iter_stack sort_iter;
	struct nonce nonce;
	unsigned bytes_to_write, sectors_to_write, bytes, u64s;
	u64 seq = 0;
	bool used_mempool;
	unsigned long old, new;
	bool validate_before_checksum = false;
	enum btree_write_type type = flags & BTREE_WRITE_TYPE_MASK;
	void *data;
	int ret;

	if (flags & BTREE_WRITE_ALREADY_STARTED)
		goto do_write;

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

		if ((flags & BTREE_WRITE_ONLY_IF_NEED) &&
		    !(old & (1 << BTREE_NODE_need_write)))
			return;

		if (old &
		    ((1 << BTREE_NODE_never_write)|
		     (1 << BTREE_NODE_write_blocked)))
			return;

		if (b->written &&
		    (old & (1 << BTREE_NODE_will_make_reachable)))
			return;

		if (old & (1 << BTREE_NODE_write_in_flight))
			return;

		if (flags & BTREE_WRITE_ONLY_IF_NEED)
			type = new & BTREE_WRITE_TYPE_MASK;
		new &= ~BTREE_WRITE_TYPE_MASK;

		new &= ~(1 << BTREE_NODE_dirty);
		new &= ~(1 << BTREE_NODE_need_write);
		new |=  (1 << BTREE_NODE_write_in_flight);
		new |=  (1 << BTREE_NODE_write_in_flight_inner);
		new |=  (1 << BTREE_NODE_just_written);
		new ^=  (1 << BTREE_NODE_write_idx);
	} while (cmpxchg_acquire(&b->flags, old, new) != old);

	if (new & (1U << BTREE_NODE_need_write))
		return;
do_write:
	BUG_ON((type == BTREE_WRITE_initial) != (b->written == 0));

	atomic_dec(&c->btree_cache.dirty);

	BUG_ON(btree_node_fake(b));
	BUG_ON((b->will_make_reachable != 0) != !b->written);

	BUG_ON(b->written >= btree_sectors(c));
	BUG_ON(b->written & (block_sectors(c) - 1));
	BUG_ON(bset_written(b, btree_bset_last(b)));
	BUG_ON(le64_to_cpu(b->data->magic) != bset_magic(c));
	BUG_ON(memcmp(&b->data->format, &b->format, sizeof(b->format)));

	bch2_sort_whiteouts(c, b);

	sort_iter_stack_init(&sort_iter, b);

	bytes = !b->written
		? sizeof(struct btree_node)
		: sizeof(struct btree_node_entry);

	bytes += b->whiteout_u64s * sizeof(u64);

	for_each_bset(b, t) {
		i = bset(b, t);

		if (bset_written(b, i))
			continue;

		bytes += le16_to_cpu(i->u64s) * sizeof(u64);
		sort_iter_add(&sort_iter.iter,
			      btree_bkey_first(b, t),
			      btree_bkey_last(b, t));
		seq = max(seq, le64_to_cpu(i->journal_seq));
	}

	BUG_ON(b->written && !seq);

	/* bch2_varint_decode may read up to 7 bytes past the end of the buffer: */
	bytes += 8;

	/* buffer must be a multiple of the block size */
	bytes = round_up(bytes, block_bytes(c));

	data = btree_bounce_alloc(c, bytes, &used_mempool);

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

	sort_iter_add(&sort_iter.iter,
		      unwritten_whiteouts_start(c, b),
		      unwritten_whiteouts_end(c, b));
	SET_BSET_SEPARATE_WHITEOUTS(i, false);

	b->whiteout_u64s = 0;

	u64s = bch2_sort_keys(i->start, &sort_iter.iter, false);
	le16_add_cpu(&i->u64s, u64s);

	BUG_ON(!b->written && i->u64s != b->data->keys.u64s);

	set_needs_whiteout(i, false);

	/* do we have data to write? */
	if (b->written && !i->u64s)
		goto nowrite;

	bytes_to_write = vstruct_end(i) - data;
	sectors_to_write = round_up(bytes_to_write, block_bytes(c)) >> 9;

	if (!b->written &&
	    b->key.k.type == KEY_TYPE_btree_ptr_v2)
		BUG_ON(btree_ptr_sectors_written(&b->key) != sectors_to_write);

	memset(data + bytes_to_write, 0,
	       (sectors_to_write << 9) - bytes_to_write);

	BUG_ON(b->written + sectors_to_write > btree_sectors(c));
	BUG_ON(BSET_BIG_ENDIAN(i) != CPU_BIG_ENDIAN);
	BUG_ON(i->seq != b->data->keys.seq);

	i->version = cpu_to_le16(c->sb.version);
	SET_BSET_OFFSET(i, b->written);
	SET_BSET_CSUM_TYPE(i, bch2_meta_checksum_type(c));

	if (bch2_csum_type_is_encryption(BSET_CSUM_TYPE(i)))
		validate_before_checksum = true;

	/* validate_bset will be modifying: */
	if (le16_to_cpu(i->version) < bcachefs_metadata_version_current)
		validate_before_checksum = true;

	/* if we're going to be encrypting, check metadata validity first: */
	if (validate_before_checksum &&
	    validate_bset_for_write(c, b, i, sectors_to_write))
		goto err;

	ret = bset_encrypt(c, i, b->written << 9);
	if (bch2_fs_fatal_err_on(ret, c,
			"error encrypting btree node: %i\n", ret))
		goto err;

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
	 * Also on journal error, the pending write may have updates that were
	 * never journalled (interior nodes, see btree_update_nodes_written()) -
	 * it's critical that we don't do the write in that case otherwise we
	 * will have updates visible that weren't in the journal:
	 *
	 * Make sure to update b->written so bch2_btree_init_next() doesn't
	 * break:
	 */
	if (bch2_journal_error(&c->journal) ||
	    c->opts.nochanges)
		goto err;

	trace_and_count(c, btree_node_write, b, bytes_to_write, sectors_to_write);

	wbio = container_of(bio_alloc_bioset(NULL,
				buf_pages(data, sectors_to_write << 9),
				REQ_OP_WRITE|REQ_META,
				GFP_NOFS,
				&c->btree_bio),
			    struct btree_write_bio, wbio.bio);
	wbio_init(&wbio->wbio.bio);
	wbio->data			= data;
	wbio->data_bytes		= bytes;
	wbio->sector_offset		= b->written;
	wbio->wbio.c			= c;
	wbio->wbio.used_mempool		= used_mempool;
	wbio->wbio.first_btree_write	= !b->written;
	wbio->wbio.bio.bi_end_io	= btree_node_write_endio;
	wbio->wbio.bio.bi_private	= b;

	bch2_bio_map(&wbio->wbio.bio, data, sectors_to_write << 9);

	bkey_copy(&wbio->key, &b->key);

	b->written += sectors_to_write;

	if (wbio->key.k.type == KEY_TYPE_btree_ptr_v2)
		bkey_i_to_btree_ptr_v2(&wbio->key)->v.sectors_written =
			cpu_to_le16(b->written);

	atomic64_inc(&c->btree_write_stats[type].nr);
	atomic64_add(bytes_to_write, &c->btree_write_stats[type].bytes);

	INIT_WORK(&wbio->work, btree_write_submit);
	queue_work(c->io_complete_wq, &wbio->work);
	return;
err:
	set_btree_node_noevict(b);
	b->written += sectors_to_write;
nowrite:
	btree_bounce_free(c, bytes, used_mempool, data);
	__btree_node_write_done(c, b);
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
		btree_node_sort(c, b, 0, b->nsets, true);
		invalidated_iter = true;
	} else {
		invalidated_iter = bch2_drop_whiteouts(b, COMPACT_ALL);
	}

	for_each_bset(b, t)
		set_needs_whiteout(bset(b, t), true);

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
			   enum six_lock_type lock_type_held,
			   unsigned flags)
{
	if (lock_type_held == SIX_LOCK_intent ||
	    (lock_type_held == SIX_LOCK_read &&
	     six_lock_tryupgrade(&b->c.lock))) {
		__bch2_btree_node_write(c, b, flags);

		/* don't cycle lock unnecessarily: */
		if (btree_node_just_written(b) &&
		    six_trylock_write(&b->c.lock)) {
			bch2_btree_post_write_cleanup(c, b);
			six_unlock_write(&b->c.lock);
		}

		if (lock_type_held == SIX_LOCK_read)
			six_lock_downgrade(&b->c.lock);
	} else {
		__bch2_btree_node_write(c, b, flags);
		if (lock_type_held == SIX_LOCK_write &&
		    btree_node_just_written(b))
			bch2_btree_post_write_cleanup(c, b);
	}
}

static bool __bch2_btree_flush_all(struct bch_fs *c, unsigned flag)
{
	struct bucket_table *tbl;
	struct rhash_head *pos;
	struct btree *b;
	unsigned i;
	bool ret = false;
restart:
	rcu_read_lock();
	for_each_cached_btree(b, c, tbl, i, pos)
		if (test_bit(flag, &b->flags)) {
			rcu_read_unlock();
			wait_on_bit_io(&b->flags, flag, TASK_UNINTERRUPTIBLE);
			ret = true;
			goto restart;
		}
	rcu_read_unlock();

	return ret;
}

bool bch2_btree_flush_all_reads(struct bch_fs *c)
{
	return __bch2_btree_flush_all(c, BTREE_NODE_read_in_flight);
}

bool bch2_btree_flush_all_writes(struct bch_fs *c)
{
	return __bch2_btree_flush_all(c, BTREE_NODE_write_in_flight);
}

static const char * const bch2_btree_write_types[] = {
#define x(t, n) [n] = #t,
	BCH_BTREE_WRITE_TYPES()
	NULL
};

void bch2_btree_write_stats_to_text(struct printbuf *out, struct bch_fs *c)
{
	printbuf_tabstop_push(out, 20);
	printbuf_tabstop_push(out, 10);

	prt_tab(out);
	prt_str(out, "nr");
	prt_tab(out);
	prt_str(out, "size");
	prt_newline(out);

	for (unsigned i = 0; i < BTREE_WRITE_TYPE_NR; i++) {
		u64 nr		= atomic64_read(&c->btree_write_stats[i].nr);
		u64 bytes	= atomic64_read(&c->btree_write_stats[i].bytes);

		prt_printf(out, "%s:", bch2_btree_write_types[i]);
		prt_tab(out);
		prt_u64(out, nr);
		prt_tab(out);
		prt_human_readable_u64(out, nr ? div64_u64(bytes, nr) : 0);
		prt_newline(out);
	}
}
