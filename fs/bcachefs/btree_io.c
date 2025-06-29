// SPDX-License-Identifier: GPL-2.0

#include "bcachefs.h"
#include "async_objs.h"
#include "bkey_buf.h"
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
#include "enumerated_ref.h"
#include "error.h"
#include "extents.h"
#include "io_write.h"
#include "journal_reclaim.h"
#include "journal_seq_blacklist.h"
#include "recovery.h"
#include "super-io.h"
#include "trace.h"

#include <linux/sched/mm.h>

static void bch2_btree_node_header_to_text(struct printbuf *out, struct btree_node *bn)
{
	bch2_btree_id_level_to_text(out, BTREE_NODE_ID(bn), BTREE_NODE_LEVEL(bn));
	prt_printf(out, " seq %llx %llu\n", bn->keys.seq, BTREE_NODE_SEQ(bn));
	prt_str(out, "min: ");
	bch2_bpos_to_text(out, bn->min_key);
	prt_newline(out);
	prt_str(out, "max: ");
	bch2_bpos_to_text(out, bn->max_key);
}

void bch2_btree_node_io_unlock(struct btree *b)
{
	EBUG_ON(!btree_node_write_in_flight(b));

	clear_btree_node_write_in_flight_inner(b);
	clear_btree_node_write_in_flight(b);
	smp_mb__after_atomic();
	wake_up_bit(&b->flags, BTREE_NODE_write_in_flight);
}

void bch2_btree_node_io_lock(struct btree *b)
{
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
	wait_on_bit_io(&b->flags, BTREE_NODE_read_in_flight,
		       TASK_UNINTERRUPTIBLE);
}

void bch2_btree_node_wait_on_write(struct btree *b)
{
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
		kvfree(p);
}

static void *btree_bounce_alloc(struct bch_fs *c, size_t size,
				bool *used_mempool)
{
	unsigned flags = memalloc_nofs_save();
	void *p;

	BUG_ON(size > c->opts.btree_node_size);

	*used_mempool = false;
	p = kvmalloc(size, __GFP_NOWARN|GFP_NOWAIT);
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

	for (k = unwritten_whiteouts_start(b);
	     k != unwritten_whiteouts_end(b);
	     k = bkey_p_next(k))
		*--ptrs = k;

	sort_bkey_ptrs(b, ptrs, ptrs_end - ptrs);

	k = new_whiteouts;

	while (ptrs != ptrs_end) {
		bkey_p_copy(k, *ptrs);
		k = bkey_p_next(k);
		ptrs++;
	}

	verify_no_dups(b, new_whiteouts,
		       (void *) ((u64 *) new_whiteouts + b->whiteout_u64s));

	memcpy_u64s(unwritten_whiteouts_start(b),
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
				bkey_p_copy(out, k);
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
			    unsigned end_idx)
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
		? btree_buf_bytes(b)
		: __vstruct_bytes(struct btree_node, u64s);

	out = btree_bounce_alloc(c, bytes, &used_mempool);

	start_time = local_clock();

	u64s = bch2_sort_keys(out->keys.start, &sort_iter.iter);

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

		BUG_ON(bytes != btree_buf_bytes(b));

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
		btree_node_sort(c, b, unwritten_idx, b->nsets);
		ret = true;
	}

	if (unwritten_idx > 1) {
		btree_node_sort(c, b, 0, unwritten_idx);
		ret = true;
	}

	return ret;
}

void bch2_btree_build_aux_trees(struct btree *b)
{
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
		bch2_btree_node_write_trans(trans, b, SIX_LOCK_write,
					    BTREE_WRITE_init_next_bset);
		reinit_iter = true;
	}

	if (b->nsets == MAX_BSETS &&
	    btree_node_compact(c, b))
		reinit_iter = true;

	BUG_ON(b->nsets >= MAX_BSETS);

	bne = want_new_bset(c, b);
	if (bne)
		bch2_bset_init_next(b, bne);

	bch2_btree_build_aux_trees(b);

	if (reinit_iter)
		bch2_trans_node_reinit_iter(trans, b);
}

static void btree_err_msg(struct printbuf *out, struct bch_fs *c,
			  struct bch_dev *ca,
			  bool print_pos,
			  struct btree *b, struct bset *i, struct bkey_packed *k,
			  unsigned offset, int rw)
{
	if (print_pos) {
		prt_str(out, rw == READ
			? "error validating btree node "
			: "corrupt btree node before write ");
		prt_printf(out, "at btree ");
		bch2_btree_pos_to_text(out, c, b);
		prt_newline(out);
	}

	if (ca)
		prt_printf(out, "%s ", ca->name);

	prt_printf(out, "node offset %u/%u",
		   b->written, btree_ptr_sectors_written(bkey_i_to_s_c(&b->key)));
	if (i)
		prt_printf(out, " bset u64s %u", le16_to_cpu(i->u64s));
	if (k)
		prt_printf(out, " bset byte offset %lu",
			   (unsigned long)(void *)k -
			   ((unsigned long)(void *)i & ~511UL));
	prt_str(out, ": ");
}

__printf(11, 12)
static int __btree_err(int ret,
		       struct bch_fs *c,
		       struct bch_dev *ca,
		       struct btree *b,
		       struct bset *i,
		       struct bkey_packed *k,
		       int rw,
		       enum bch_sb_error_id err_type,
		       struct bch_io_failures *failed,
		       struct printbuf *err_msg,
		       const char *fmt, ...)
{
	if (c->recovery.curr_pass == BCH_RECOVERY_PASS_scan_for_btree_nodes)
		return bch_err_throw(c, fsck_fix);

	bool have_retry = false;
	int ret2;

	if (ca) {
		bch2_mark_btree_validate_failure(failed, ca->dev_idx);

		struct extent_ptr_decoded pick;
		have_retry = !bch2_bkey_pick_read_device(c,
					bkey_i_to_s_c(&b->key),
					failed, &pick, -1);
	}

	if (!have_retry && ret == -BCH_ERR_btree_node_read_err_want_retry)
		ret = bch_err_throw(c, btree_node_read_err_fixable);
	if (!have_retry && ret == -BCH_ERR_btree_node_read_err_must_retry)
		ret = bch_err_throw(c, btree_node_read_err_bad_node);

	bch2_sb_error_count(c, err_type);

	bool print_deferred = err_msg &&
		rw == READ &&
		!(test_bit(BCH_FS_in_fsck, &c->flags) &&
		  c->opts.fix_errors == FSCK_FIX_ask);

	struct printbuf out = PRINTBUF;
	bch2_log_msg_start(c, &out);

	if (!print_deferred)
		err_msg = &out;

	btree_err_msg(err_msg, c, ca, !print_deferred, b, i, k, b->written, rw);

	va_list args;
	va_start(args, fmt);
	prt_vprintf(err_msg, fmt, args);
	va_end(args);

	if (print_deferred) {
		prt_newline(err_msg);

		switch (ret) {
		case -BCH_ERR_btree_node_read_err_fixable:
			ret2 = bch2_fsck_err_opt(c, FSCK_CAN_FIX, err_type);
			if (!bch2_err_matches(ret2, BCH_ERR_fsck_fix) &&
			    !bch2_err_matches(ret2, BCH_ERR_fsck_ignore)) {
				ret = ret2;
				goto fsck_err;
			}

			if (!have_retry)
				ret = bch_err_throw(c, fsck_fix);
			goto out;
		case -BCH_ERR_btree_node_read_err_bad_node:
			prt_str(&out, ", ");
			ret = __bch2_topology_error(c, &out);
			break;
		}

		goto out;
	}

	if (rw == WRITE) {
		prt_str(&out, ", ");
		ret = __bch2_inconsistent_error(c, &out)
			? -BCH_ERR_fsck_errors_not_fixed
			: 0;
		goto print;
	}

	switch (ret) {
	case -BCH_ERR_btree_node_read_err_fixable:
		ret2 = __bch2_fsck_err(c, NULL, FSCK_CAN_FIX, err_type, "%s", out.buf);
		if (!bch2_err_matches(ret2, BCH_ERR_fsck_fix) &&
		    !bch2_err_matches(ret2, BCH_ERR_fsck_ignore)) {
			ret = ret2;
			goto fsck_err;
		}

		if (!have_retry)
			ret = bch_err_throw(c, fsck_fix);
		goto out;
	case -BCH_ERR_btree_node_read_err_bad_node:
		prt_str(&out, ", ");
		ret = __bch2_topology_error(c, &out);
		break;
	}
print:
	bch2_print_str(c, KERN_ERR, out.buf);
out:
fsck_err:
	printbuf_exit(&out);
	return ret;
}

#define btree_err(type, c, ca, b, i, k, _err_type, msg, ...)		\
({									\
	int _ret = __btree_err(type, c, ca, b, i, k, write,		\
			       BCH_FSCK_ERR_##_err_type,		\
			       failed, err_msg,				\
			       msg, ##__VA_ARGS__);			\
									\
	if (!bch2_err_matches(_ret, BCH_ERR_fsck_fix)) {		\
		ret = _ret;						\
		goto fsck_err;						\
	}								\
									\
	true;								\
})

#define btree_err_on(cond, ...)	((cond) ? btree_err(__VA_ARGS__) : false)

/*
 * When btree topology repair changes the start or end of a node, that might
 * mean we have to drop keys that are no longer inside the node:
 */
__cold
void bch2_btree_node_drop_keys_outside_node(struct btree *b)
{
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
	b->nr = bch2_btree_node_count_keys(b);

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
			 unsigned offset, unsigned sectors, int write,
			 struct bch_io_failures *failed,
			 struct printbuf *err_msg)
{
	unsigned version = le16_to_cpu(i->version);
	unsigned ptr_written = btree_ptr_sectors_written(bkey_i_to_s_c(&b->key));
	struct printbuf buf1 = PRINTBUF;
	struct printbuf buf2 = PRINTBUF;
	int ret = 0;

	btree_err_on(!bch2_version_compatible(version),
		     -BCH_ERR_btree_node_read_err_incompatible,
		     c, ca, b, i, NULL,
		     btree_node_unsupported_version,
		     "unsupported bset version %u.%u",
		     BCH_VERSION_MAJOR(version),
		     BCH_VERSION_MINOR(version));

	if (c->recovery.curr_pass != BCH_RECOVERY_PASS_scan_for_btree_nodes &&
	    btree_err_on(version < c->sb.version_min,
			 -BCH_ERR_btree_node_read_err_fixable,
			 c, NULL, b, i, NULL,
			 btree_node_bset_older_than_sb_min,
			 "bset version %u older than superblock version_min %u",
			 version, c->sb.version_min)) {
		if (bch2_version_compatible(version)) {
			mutex_lock(&c->sb_lock);
			c->disk_sb.sb->version_min = cpu_to_le16(version);
			bch2_write_super(c);
			mutex_unlock(&c->sb_lock);
		} else {
			/* We have no idea what's going on: */
			i->version = cpu_to_le16(c->sb.version);
		}
	}

	if (btree_err_on(BCH_VERSION_MAJOR(version) >
			 BCH_VERSION_MAJOR(c->sb.version),
			 -BCH_ERR_btree_node_read_err_fixable,
			 c, NULL, b, i, NULL,
			 btree_node_bset_newer_than_sb,
			 "bset version %u newer than superblock version %u",
			 version, c->sb.version)) {
		mutex_lock(&c->sb_lock);
		c->disk_sb.sb->version = cpu_to_le16(version);
		bch2_write_super(c);
		mutex_unlock(&c->sb_lock);
	}

	btree_err_on(BSET_SEPARATE_WHITEOUTS(i),
		     -BCH_ERR_btree_node_read_err_incompatible,
		     c, ca, b, i, NULL,
		     btree_node_unsupported_version,
		     "BSET_SEPARATE_WHITEOUTS no longer supported");

	if (!write &&
	    btree_err_on(offset + sectors > (ptr_written ?: btree_sectors(c)),
			 -BCH_ERR_btree_node_read_err_fixable,
			 c, ca, b, i, NULL,
			 bset_past_end_of_btree_node,
			 "bset past end of btree node (offset %u len %u but written %zu)",
			 offset, sectors, ptr_written ?: btree_sectors(c)))
		i->u64s = 0;

	btree_err_on(offset && !i->u64s,
		     -BCH_ERR_btree_node_read_err_fixable,
		     c, ca, b, i, NULL,
		     bset_empty,
		     "empty bset");

	btree_err_on(BSET_OFFSET(i) && BSET_OFFSET(i) != offset,
		     -BCH_ERR_btree_node_read_err_want_retry,
		     c, ca, b, i, NULL,
		     bset_wrong_sector_offset,
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
				     -BCH_ERR_btree_node_read_err_must_retry,
				     c, ca, b, NULL, NULL,
				     bset_bad_seq,
				     "incorrect sequence number (wrong btree node)");
		}

		btree_err_on(BTREE_NODE_ID(bn) != b->c.btree_id,
			     -BCH_ERR_btree_node_read_err_must_retry,
			     c, ca, b, i, NULL,
			     btree_node_bad_btree,
			     "incorrect btree id");

		btree_err_on(BTREE_NODE_LEVEL(bn) != b->c.level,
			     -BCH_ERR_btree_node_read_err_must_retry,
			     c, ca, b, i, NULL,
			     btree_node_bad_level,
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
				     -BCH_ERR_btree_node_read_err_must_retry,
				     c, ca, b, NULL, NULL,
				     btree_node_bad_min_key,
				     "incorrect min_key: got %s should be %s",
				     (printbuf_reset(&buf1),
				      bch2_bpos_to_text(&buf1, bn->min_key), buf1.buf),
				     (printbuf_reset(&buf2),
				      bch2_bpos_to_text(&buf2, bp->min_key), buf2.buf));
		}

		btree_err_on(!bpos_eq(bn->max_key, b->key.k.p),
			     -BCH_ERR_btree_node_read_err_must_retry,
			     c, ca, b, i, NULL,
			     btree_node_bad_max_key,
			     "incorrect max key %s",
			     (printbuf_reset(&buf1),
			      bch2_bpos_to_text(&buf1, bn->max_key), buf1.buf));

		if (write)
			compat_btree_node(b->c.level, b->c.btree_id, version,
					  BSET_BIG_ENDIAN(i), write, bn);

		btree_err_on(bch2_bkey_format_invalid(c, &bn->format, write, &buf1),
			     -BCH_ERR_btree_node_read_err_bad_node,
			     c, ca, b, i, NULL,
			     btree_node_bad_format,
			     "invalid bkey format: %s\n%s", buf1.buf,
			     (printbuf_reset(&buf2),
			      bch2_bkey_format_to_text(&buf2, &bn->format), buf2.buf));
		printbuf_reset(&buf1);

		compat_bformat(b->c.level, b->c.btree_id, version,
			       BSET_BIG_ENDIAN(i), write,
			       &bn->format);
	}
fsck_err:
	printbuf_exit(&buf2);
	printbuf_exit(&buf1);
	return ret;
}

static int btree_node_bkey_val_validate(struct bch_fs *c, struct btree *b,
					struct bkey_s_c k,
					enum bch_validate_flags flags)
{
	return bch2_bkey_val_validate(c, k, (struct bkey_validate_context) {
		.from	= BKEY_VALIDATE_btree_node,
		.level	= b->c.level,
		.btree	= b->c.btree_id,
		.flags	= flags
	});
}

static int bset_key_validate(struct bch_fs *c, struct btree *b,
			     struct bkey_s_c k,
			     bool updated_range,
			     enum bch_validate_flags flags)
{
	struct bkey_validate_context from = (struct bkey_validate_context) {
		.from	= BKEY_VALIDATE_btree_node,
		.level	= b->c.level,
		.btree	= b->c.btree_id,
		.flags	= flags,
	};
	return __bch2_bkey_validate(c, k, from) ?:
		(!updated_range ? bch2_bkey_in_btree_node(c, b, k, from) : 0) ?:
		(flags & BCH_VALIDATE_write ? btree_node_bkey_val_validate(c, b, k, flags) : 0);
}

static bool bkey_packed_valid(struct bch_fs *c, struct btree *b,
			 struct bset *i, struct bkey_packed *k)
{
	if (bkey_p_next(k) > vstruct_last(i))
		return false;

	if (k->format > KEY_FORMAT_CURRENT)
		return false;

	if (!bkeyp_u64s_valid(&b->format, k))
		return false;

	struct bkey tmp;
	struct bkey_s u = __bkey_disassemble(b, k, &tmp);
	return !__bch2_bkey_validate(c, u.s_c,
				     (struct bkey_validate_context) {
					.from	= BKEY_VALIDATE_btree_node,
					.level	= b->c.level,
					.btree	= b->c.btree_id,
					.flags	= BCH_VALIDATE_silent
				     });
}

static inline int btree_node_read_bkey_cmp(const struct btree *b,
				const struct bkey_packed *l,
				const struct bkey_packed *r)
{
	return bch2_bkey_cmp_packed(b, l, r)
		?: (int) bkey_deleted(r) - (int) bkey_deleted(l);
}

static int validate_bset_keys(struct bch_fs *c, struct btree *b,
			 struct bset *i, int write,
			 struct bch_io_failures *failed,
			 struct printbuf *err_msg)
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
		unsigned next_good_key;

		if (btree_err_on(bkey_p_next(k) > vstruct_last(i),
				 -BCH_ERR_btree_node_read_err_fixable,
				 c, NULL, b, i, k,
				 btree_node_bkey_past_bset_end,
				 "key extends past end of bset")) {
			i->u64s = cpu_to_le16((u64 *) k - i->_data);
			break;
		}

		if (btree_err_on(k->format > KEY_FORMAT_CURRENT,
				 -BCH_ERR_btree_node_read_err_fixable,
				 c, NULL, b, i, k,
				 btree_node_bkey_bad_format,
				 "invalid bkey format %u", k->format))
			goto drop_this_key;

		if (btree_err_on(!bkeyp_u64s_valid(&b->format, k),
				 -BCH_ERR_btree_node_read_err_fixable,
				 c, NULL, b, i, k,
				 btree_node_bkey_bad_u64s,
				 "bad k->u64s %u (min %u max %zu)", k->u64s,
				 bkeyp_key_u64s(&b->format, k),
				 U8_MAX - BKEY_U64s + bkeyp_key_u64s(&b->format, k)))
			goto drop_this_key;

		if (!write)
			bch2_bkey_compat(b->c.level, b->c.btree_id, version,
				    BSET_BIG_ENDIAN(i), write,
				    &b->format, k);

		u = __bkey_disassemble(b, k, &tmp);

		ret = bset_key_validate(c, b, u.s_c, updated_range, write);
		if (ret == -BCH_ERR_fsck_delete_bkey)
			goto drop_this_key;
		if (ret)
			goto fsck_err;

		if (write)
			bch2_bkey_compat(b->c.level, b->c.btree_id, version,
				    BSET_BIG_ENDIAN(i), write,
				    &b->format, k);

		if (prev && btree_node_read_bkey_cmp(b, prev, k) >= 0) {
			struct bkey up = bkey_unpack_key(b, prev);

			printbuf_reset(&buf);
			prt_printf(&buf, "keys out of order: ");
			bch2_bkey_to_text(&buf, &up);
			prt_printf(&buf, " > ");
			bch2_bkey_to_text(&buf, u.k);

			if (btree_err(-BCH_ERR_btree_node_read_err_fixable,
				      c, NULL, b, i, k,
				      btree_node_bkey_out_of_order,
				      "%s", buf.buf))
				goto drop_this_key;
		}

		prev = k;
		k = bkey_p_next(k);
		continue;
drop_this_key:
		next_good_key = k->u64s;

		if (!next_good_key ||
		    (BSET_BIG_ENDIAN(i) == CPU_BIG_ENDIAN &&
		     version >= bcachefs_metadata_version_snapshot)) {
			/*
			 * only do scanning if bch2_bkey_compat() has nothing to
			 * do
			 */

			if (!bkey_packed_valid(c, b, i, (void *) ((u64 *) k + next_good_key))) {
				for (next_good_key = 1;
				     next_good_key < (u64 *) vstruct_last(i) - (u64 *) k;
				     next_good_key++)
					if (bkey_packed_valid(c, b, i, (void *) ((u64 *) k + next_good_key)))
						goto got_good_key;
			}

			/*
			 * didn't find a good key, have to truncate the rest of
			 * the bset
			 */
			next_good_key = (u64 *) vstruct_last(i) - (u64 *) k;
		}
got_good_key:
		le16_add_cpu(&i->u64s, -next_good_key);
		memmove_u64s_down(k, (u64 *) k + next_good_key, (u64 *) vstruct_end(i) - (u64 *) k);
		set_btree_node_need_rewrite(b);
		set_btree_node_need_rewrite_error(b);
	}
fsck_err:
	printbuf_exit(&buf);
	return ret;
}

int bch2_btree_node_read_done(struct bch_fs *c, struct bch_dev *ca,
			      struct btree *b,
			      struct bch_io_failures *failed,
			      struct printbuf *err_msg)
{
	struct btree_node_entry *bne;
	struct sort_iter *iter;
	struct btree_node *sorted;
	struct bkey_packed *k;
	struct bset *i;
	bool used_mempool, blacklisted;
	bool updated_range = b->key.k.type == KEY_TYPE_btree_ptr_v2 &&
		BTREE_PTR_RANGE_UPDATED(&bkey_i_to_btree_ptr_v2(&b->key)->v);
	unsigned ptr_written = btree_ptr_sectors_written(bkey_i_to_s_c(&b->key));
	u64 max_journal_seq = 0;
	struct printbuf buf = PRINTBUF;
	int ret = 0, write = READ;
	u64 start_time = local_clock();

	b->version_ondisk = U16_MAX;
	/* We might get called multiple times on read retry: */
	b->written = 0;

	iter = mempool_alloc(&c->fill_iter, GFP_NOFS);
	sort_iter_init(iter, b, (btree_blocks(c) + 1) * 2);

	if (bch2_meta_read_fault("btree"))
		btree_err(-BCH_ERR_btree_node_read_err_must_retry,
			  c, ca, b, NULL, NULL,
			  btree_node_fault_injected,
			  "dynamic fault");

	btree_err_on(le64_to_cpu(b->data->magic) != bset_magic(c),
		     -BCH_ERR_btree_node_read_err_must_retry,
		     c, ca, b, NULL, NULL,
		     btree_node_bad_magic,
		     "bad magic: want %llx, got %llx",
		     bset_magic(c), le64_to_cpu(b->data->magic));

	if (b->key.k.type == KEY_TYPE_btree_ptr_v2) {
		struct bch_btree_ptr_v2 *bp =
			&bkey_i_to_btree_ptr_v2(&b->key)->v;

		bch2_bpos_to_text(&buf, b->data->min_key);
		prt_str(&buf, "-");
		bch2_bpos_to_text(&buf, b->data->max_key);

		btree_err_on(b->data->keys.seq != bp->seq,
			     -BCH_ERR_btree_node_read_err_must_retry,
			     c, ca, b, NULL, NULL,
			     btree_node_bad_seq,
			     "got wrong btree node: got\n%s",
			     (printbuf_reset(&buf),
			      bch2_btree_node_header_to_text(&buf, b->data),
			      buf.buf));
	} else {
		btree_err_on(!b->data->keys.seq,
			     -BCH_ERR_btree_node_read_err_must_retry,
			     c, ca, b, NULL, NULL,
			     btree_node_bad_seq,
			     "bad btree header: seq 0\n%s",
			     (printbuf_reset(&buf),
			      bch2_btree_node_header_to_text(&buf, b->data),
			      buf.buf));
	}

	while (b->written < (ptr_written ?: btree_sectors(c))) {
		unsigned sectors;
		bool first = !b->written;

		if (first) {
			bne = NULL;
			i = &b->data->keys;
		} else {
			bne = write_block(b);
			i = &bne->keys;

			if (i->seq != b->data->keys.seq)
				break;
		}

		struct nonce nonce = btree_nonce(i, b->written << 9);
		bool good_csum_type = bch2_checksum_type_valid(c, BSET_CSUM_TYPE(i));

		btree_err_on(!good_csum_type,
			     bch2_csum_type_is_encryption(BSET_CSUM_TYPE(i))
			     ? -BCH_ERR_btree_node_read_err_must_retry
			     : -BCH_ERR_btree_node_read_err_want_retry,
			     c, ca, b, i, NULL,
			     bset_unknown_csum,
			     "unknown checksum type %llu", BSET_CSUM_TYPE(i));

		if (first) {
			if (good_csum_type) {
				struct bch_csum csum = csum_vstruct(c, BSET_CSUM_TYPE(i), nonce, b->data);
				bool csum_bad = bch2_crc_cmp(b->data->csum, csum);
				if (csum_bad)
					bch2_io_error(ca, BCH_MEMBER_ERROR_checksum);

				btree_err_on(csum_bad,
					     -BCH_ERR_btree_node_read_err_want_retry,
					     c, ca, b, i, NULL,
					     bset_bad_csum,
					     "%s",
					     (printbuf_reset(&buf),
					      bch2_csum_err_msg(&buf, BSET_CSUM_TYPE(i), b->data->csum, csum),
					      buf.buf));

				ret = bset_encrypt(c, i, b->written << 9);
				if (bch2_fs_fatal_err_on(ret, c,
							 "decrypting btree node: %s", bch2_err_str(ret)))
					goto fsck_err;
			}

			btree_err_on(btree_node_type_is_extents(btree_node_type(b)) &&
				     !BTREE_NODE_NEW_EXTENT_OVERWRITE(b->data),
				     -BCH_ERR_btree_node_read_err_incompatible,
				     c, NULL, b, NULL, NULL,
				     btree_node_unsupported_version,
				     "btree node does not have NEW_EXTENT_OVERWRITE set");

			sectors = vstruct_sectors(b->data, c->block_bits);
		} else {
			if (good_csum_type) {
				struct bch_csum csum = csum_vstruct(c, BSET_CSUM_TYPE(i), nonce, bne);
				bool csum_bad = bch2_crc_cmp(bne->csum, csum);
				if (ca && csum_bad)
					bch2_io_error(ca, BCH_MEMBER_ERROR_checksum);

				btree_err_on(csum_bad,
					     -BCH_ERR_btree_node_read_err_want_retry,
					     c, ca, b, i, NULL,
					     bset_bad_csum,
					     "%s",
					     (printbuf_reset(&buf),
					      bch2_csum_err_msg(&buf, BSET_CSUM_TYPE(i), bne->csum, csum),
					      buf.buf));

				ret = bset_encrypt(c, i, b->written << 9);
				if (bch2_fs_fatal_err_on(ret, c,
						"decrypting btree node: %s", bch2_err_str(ret)))
					goto fsck_err;
			}

			sectors = vstruct_sectors(bne, c->block_bits);
		}

		b->version_ondisk = min(b->version_ondisk,
					le16_to_cpu(i->version));

		ret = validate_bset(c, ca, b, i, b->written, sectors, READ, failed, err_msg);
		if (ret)
			goto fsck_err;

		if (!b->written)
			btree_node_set_format(b, b->data->format);

		ret = validate_bset_keys(c, b, i, READ, failed, err_msg);
		if (ret)
			goto fsck_err;

		SET_BSET_BIG_ENDIAN(i, CPU_BIG_ENDIAN);

		blacklisted = bch2_journal_seq_is_blacklisted(c,
					le64_to_cpu(i->journal_seq),
					true);

		btree_err_on(blacklisted && first,
			     -BCH_ERR_btree_node_read_err_fixable,
			     c, ca, b, i, NULL,
			     bset_blacklisted_journal_seq,
			     "first btree node bset has blacklisted journal seq (%llu)",
			     le64_to_cpu(i->journal_seq));

		btree_err_on(blacklisted && ptr_written,
			     -BCH_ERR_btree_node_read_err_fixable,
			     c, ca, b, i, NULL,
			     first_bset_blacklisted_journal_seq,
			     "found blacklisted bset (journal seq %llu) in btree node at offset %u-%u/%u",
			     le64_to_cpu(i->journal_seq),
			     b->written, b->written + sectors, ptr_written);

		b->written = min(b->written + sectors, btree_sectors(c));

		if (blacklisted && !first)
			continue;

		sort_iter_add(iter,
			      vstruct_idx(i, 0),
			      vstruct_last(i));

		max_journal_seq = max(max_journal_seq, le64_to_cpu(i->journal_seq));
	}

	if (ptr_written) {
		btree_err_on(b->written < ptr_written,
			     -BCH_ERR_btree_node_read_err_want_retry,
			     c, ca, b, NULL, NULL,
			     btree_node_data_missing,
			     "btree node data missing: expected %u sectors, found %u",
			     ptr_written, b->written);
	} else {
		for (bne = write_block(b);
		     bset_byte_offset(b, bne) < btree_buf_bytes(b);
		     bne = (void *) bne + block_bytes(c))
			btree_err_on(bne->keys.seq == b->data->keys.seq &&
				     !bch2_journal_seq_is_blacklisted(c,
								      le64_to_cpu(bne->keys.journal_seq),
								      true),
				     -BCH_ERR_btree_node_read_err_want_retry,
				     c, ca, b, NULL, NULL,
				     btree_node_bset_after_end,
				     "found bset signature after last bset");
	}

	sorted = btree_bounce_alloc(c, btree_buf_bytes(b), &used_mempool);
	sorted->keys.u64s = 0;

	b->nr = bch2_key_sort_fix_overlapping(c, &sorted->keys, iter);
	memset((uint8_t *)(sorted + 1) + b->nr.live_u64s * sizeof(u64), 0,
			btree_buf_bytes(b) -
			sizeof(struct btree_node) -
			b->nr.live_u64s * sizeof(u64));

	b->data->keys.u64s = sorted->keys.u64s;
	*sorted = *b->data;
	swap(sorted, b->data);
	set_btree_bset(b, b->set, &b->data->keys);
	b->nsets = 1;
	b->data->keys.journal_seq = cpu_to_le64(max_journal_seq);

	BUG_ON(b->nr.live_u64s != le16_to_cpu(b->data->keys.u64s));

	btree_bounce_free(c, btree_buf_bytes(b), used_mempool, sorted);

	if (updated_range)
		bch2_btree_node_drop_keys_outside_node(b);

	i = &b->data->keys;
	for (k = i->start; k != vstruct_last(i);) {
		struct bkey tmp;
		struct bkey_s u = __bkey_disassemble(b, k, &tmp);

		ret = btree_node_bkey_val_validate(c, b, u.s_c, READ);
		if (ret == -BCH_ERR_fsck_delete_bkey ||
		    (static_branch_unlikely(&bch2_inject_invalid_keys) &&
		     !bversion_cmp(u.k->bversion, MAX_VERSION))) {
			btree_keys_account_key_drop(&b->nr, 0, k);

			i->u64s = cpu_to_le16(le16_to_cpu(i->u64s) - k->u64s);
			memmove_u64s_down(k, bkey_p_next(k),
					  (u64 *) vstruct_end(i) - (u64 *) k);
			set_btree_bset_end(b, b->set);
			set_btree_node_need_rewrite(b);
			set_btree_node_need_rewrite_error(b);
			continue;
		}
		if (ret)
			goto fsck_err;

		if (u.k->type == KEY_TYPE_btree_ptr_v2) {
			struct bkey_s_btree_ptr_v2 bp = bkey_s_to_btree_ptr_v2(u);

			bp.v->mem_ptr = 0;
		}

		k = bkey_p_next(k);
	}

	bch2_bset_build_aux_tree(b, b->set, false);

	set_needs_whiteout(btree_bset_first(b), true);

	btree_node_reset_sib_u64s(b);

	scoped_guard(rcu)
		bkey_for_each_ptr(bch2_bkey_ptrs(bkey_i_to_s(&b->key)), ptr) {
			struct bch_dev *ca2 = bch2_dev_rcu(c, ptr->dev);

			if (!ca2 || ca2->mi.state != BCH_MEMBER_STATE_rw) {
				set_btree_node_need_rewrite(b);
				set_btree_node_need_rewrite_degraded(b);
			}
		}

	if (!ptr_written) {
		set_btree_node_need_rewrite(b);
		set_btree_node_need_rewrite_ptr_written_zero(b);
	}
fsck_err:
	mempool_free(iter, &c->fill_iter);
	printbuf_exit(&buf);
	bch2_time_stats_update(&c->times[BCH_TIME_btree_node_read_done], start_time);
	return ret;
}

static void btree_node_read_work(struct work_struct *work)
{
	struct btree_read_bio *rb =
		container_of(work, struct btree_read_bio, work);
	struct bch_fs *c	= rb->c;
	struct bch_dev *ca	= rb->have_ioref ? bch2_dev_have_ref(c, rb->pick.ptr.dev) : NULL;
	struct btree *b		= rb->b;
	struct bio *bio		= &rb->bio;
	struct bch_io_failures failed = { .nr = 0 };
	int ret = 0;

	struct printbuf buf = PRINTBUF;
	bch2_log_msg_start(c, &buf);

	prt_printf(&buf, "btree node read error at btree ");
	bch2_btree_pos_to_text(&buf, c, b);
	prt_newline(&buf);

	goto start;
	while (1) {
		ret = bch2_bkey_pick_read_device(c,
					bkey_i_to_s_c(&b->key),
					&failed, &rb->pick, -1);
		if (ret) {
			set_btree_node_read_error(b);
			break;
		}

		ca = bch2_dev_get_ioref(c, rb->pick.ptr.dev, READ, BCH_DEV_READ_REF_btree_node_read);
		rb->have_ioref		= ca != NULL;
		rb->start_time		= local_clock();
		bio_reset(bio, NULL, REQ_OP_READ|REQ_SYNC|REQ_META);
		bio->bi_iter.bi_sector	= rb->pick.ptr.offset;
		bio->bi_iter.bi_size	= btree_buf_bytes(b);

		if (rb->have_ioref) {
			bio_set_dev(bio, ca->disk_sb.bdev);
			submit_bio_wait(bio);
		} else {
			bio->bi_status = BLK_STS_REMOVED;
		}

		bch2_account_io_completion(ca, BCH_MEMBER_ERROR_read,
					   rb->start_time, !bio->bi_status);
start:
		if (rb->have_ioref)
			enumerated_ref_put(&ca->io_ref[READ], BCH_DEV_READ_REF_btree_node_read);
		rb->have_ioref = false;

		if (bio->bi_status) {
			bch2_mark_io_failure(&failed, &rb->pick, false);
			continue;
		}

		ret = bch2_btree_node_read_done(c, ca, b, &failed, &buf);
		if (ret == -BCH_ERR_btree_node_read_err_want_retry ||
		    ret == -BCH_ERR_btree_node_read_err_must_retry)
			continue;

		if (ret)
			set_btree_node_read_error(b);

		break;
	}

	bch2_io_failures_to_text(&buf, c, &failed);

	if (btree_node_read_error(b))
		bch2_btree_lost_data(c, &buf, b->c.btree_id);

	/*
	 * only print retry success if we read from a replica with no errors
	 */
	if (btree_node_read_error(b))
		prt_printf(&buf, "ret %s", bch2_err_str(ret));
	else if (failed.nr) {
		if (!bch2_dev_io_failures(&failed, rb->pick.ptr.dev))
			prt_printf(&buf, "retry success");
		else
			prt_printf(&buf, "repair success");
	}

	if ((failed.nr ||
	     btree_node_need_rewrite(b)) &&
	    !btree_node_read_error(b) &&
	    c->recovery.curr_pass != BCH_RECOVERY_PASS_scan_for_btree_nodes) {
		prt_printf(&buf, " (rewriting node)");
		bch2_btree_node_rewrite_async(c, b);
	}
	prt_newline(&buf);

	if (failed.nr)
		bch2_print_str_ratelimited(c, KERN_ERR, buf.buf);

	async_object_list_del(c, btree_read_bio, rb->list_idx);
	bch2_time_stats_update(&c->times[BCH_TIME_btree_node_read],
			       rb->start_time);
	bio_put(&rb->bio);
	printbuf_exit(&buf);
	clear_btree_node_read_in_flight(b);
	smp_mb__after_atomic();
	wake_up_bit(&b->flags, BTREE_NODE_read_in_flight);
}

static void btree_node_read_endio(struct bio *bio)
{
	struct btree_read_bio *rb =
		container_of(bio, struct btree_read_bio, bio);
	struct bch_fs *c	= rb->c;
	struct bch_dev *ca	= rb->have_ioref
		? bch2_dev_have_ref(c, rb->pick.ptr.dev) : NULL;

	bch2_account_io_completion(ca, BCH_MEMBER_ERROR_read,
				   rb->start_time, !bio->bi_status);

	queue_work(c->btree_read_complete_wq, &rb->work);
}

void bch2_btree_read_bio_to_text(struct printbuf *out, struct btree_read_bio *rbio)
{
	bch2_bio_to_text(out, &rbio->bio);
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

static CLOSURE_CALLBACK(btree_node_read_all_replicas_done)
{
	closure_type(ra, struct btree_node_read_all, cl);
	struct bch_fs *c = ra->c;
	struct btree *b = ra->b;
	struct printbuf buf = PRINTBUF;
	bool dump_bset_maps = false;
	int ret = 0, best = -1, write = READ;
	unsigned i, written = 0, written2 = 0;
	__le64 seq = b->key.k.type == KEY_TYPE_btree_ptr_v2
		? bkey_i_to_btree_ptr_v2(&b->key)->v.seq : 0;
	bool _saw_error = false, *saw_error = &_saw_error;
	struct printbuf *err_msg = NULL;
	struct bch_io_failures *failed = NULL;

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
		if (btree_err_on(written2 != written, -BCH_ERR_btree_node_read_err_fixable,
				 c, NULL, b, NULL, NULL,
				 btree_node_replicas_sectors_written_mismatch,
				 "btree node sectors written mismatch: %u != %u",
				 written, written2) ||
		    btree_err_on(btree_node_has_extra_bsets(c, written2, ra->buf[i]),
				 -BCH_ERR_btree_node_read_err_fixable,
				 c, NULL, b, NULL, NULL,
				 btree_node_bset_after_end,
				 "found bset signature after last bset") ||
		    btree_err_on(memcmp(ra->buf[best], ra->buf[i], written << 9),
				 -BCH_ERR_btree_node_read_err_fixable,
				 c, NULL, b, NULL, NULL,
				 btree_node_replicas_data_mismatch,
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
		memcpy(b->data, ra->buf[best], btree_buf_bytes(b));
		ret = bch2_btree_node_read_done(c, NULL, b, NULL, NULL);
	} else {
		ret = -1;
	}

	if (ret) {
		set_btree_node_read_error(b);

		struct printbuf buf = PRINTBUF;
		bch2_btree_lost_data(c, &buf, b->c.btree_id);
		if (buf.pos)
			bch_err(c, "%s", buf.buf);
		printbuf_exit(&buf);
	} else if (*saw_error)
		bch2_btree_node_rewrite_async(c, b);

	for (i = 0; i < ra->nr; i++) {
		mempool_free(ra->buf[i], &c->btree_bounce_pool);
		bio_put(ra->bio[i]);
	}

	closure_debug_destroy(&ra->cl);
	kfree(ra);
	printbuf_exit(&buf);

	clear_btree_node_read_in_flight(b);
	smp_mb__after_atomic();
	wake_up_bit(&b->flags, BTREE_NODE_read_in_flight);
}

static void btree_node_read_all_replicas_endio(struct bio *bio)
{
	struct btree_read_bio *rb =
		container_of(bio, struct btree_read_bio, bio);
	struct bch_fs *c	= rb->c;
	struct btree_node_read_all *ra = rb->ra;

	if (rb->have_ioref) {
		struct bch_dev *ca = bch2_dev_have_ref(c, rb->pick.ptr.dev);

		bch2_latency_acct(ca, rb->start_time, READ);
		enumerated_ref_put(&ca->io_ref[READ],
			BCH_DEV_READ_REF_btree_node_read_all_replicas);
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
		return bch_err_throw(c, ENOMEM_btree_node_read_all_replicas);

	closure_init(&ra->cl, NULL);
	ra->c	= c;
	ra->b	= b;
	ra->nr	= bch2_bkey_nr_ptrs(k);

	for (i = 0; i < ra->nr; i++) {
		ra->buf[i] = mempool_alloc(&c->btree_bounce_pool, GFP_NOFS);
		ra->bio[i] = bio_alloc_bioset(NULL,
					      buf_pages(ra->buf[i], btree_buf_bytes(b)),
					      REQ_OP_READ|REQ_SYNC|REQ_META,
					      GFP_NOFS,
					      &c->btree_bio);
	}

	i = 0;
	bkey_for_each_ptr_decode(k.k, ptrs, pick, entry) {
		struct bch_dev *ca = bch2_dev_get_ioref(c, pick.ptr.dev, READ,
					BCH_DEV_READ_REF_btree_node_read_all_replicas);
		struct btree_read_bio *rb =
			container_of(ra->bio[i], struct btree_read_bio, bio);
		rb->c			= c;
		rb->b			= b;
		rb->ra			= ra;
		rb->start_time		= local_clock();
		rb->have_ioref		= ca != NULL;
		rb->idx			= i;
		rb->pick		= pick;
		rb->bio.bi_iter.bi_sector = pick.ptr.offset;
		rb->bio.bi_end_io	= btree_node_read_all_replicas_endio;
		bch2_bio_map(&rb->bio, ra->buf[i], btree_buf_bytes(b));

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
		btree_node_read_all_replicas_done(&ra->cl.work);
	} else {
		continue_at(&ra->cl, btree_node_read_all_replicas_done,
			    c->btree_read_complete_wq);
	}

	return 0;
}

void bch2_btree_node_read(struct btree_trans *trans, struct btree *b,
			  bool sync)
{
	struct bch_fs *c = trans->c;
	struct extent_ptr_decoded pick;
	struct btree_read_bio *rb;
	struct bch_dev *ca;
	struct bio *bio;
	int ret;

	trace_and_count(c, btree_node_read, trans, b);

	if (static_branch_unlikely(&bch2_verify_all_btree_replicas) &&
	    !btree_node_read_all_replicas(c, b, sync))
		return;

	ret = bch2_bkey_pick_read_device(c, bkey_i_to_s_c(&b->key),
					 NULL, &pick, -1);

	if (ret <= 0) {
		bool ratelimit = true;
		struct printbuf buf = PRINTBUF;
		bch2_log_msg_start(c, &buf);

		prt_str(&buf, "btree node read error: no device to read from\n at ");
		bch2_btree_pos_to_text(&buf, c, b);
		prt_newline(&buf);
		bch2_btree_lost_data(c, &buf, b->c.btree_id);

		if (c->recovery.passes_complete & BIT_ULL(BCH_RECOVERY_PASS_check_topology) &&
		    bch2_fs_emergency_read_only2(c, &buf))
			ratelimit = false;

		static DEFINE_RATELIMIT_STATE(rs,
					      DEFAULT_RATELIMIT_INTERVAL,
					      DEFAULT_RATELIMIT_BURST);
		if (!ratelimit || __ratelimit(&rs))
			bch2_print_str(c, KERN_ERR, buf.buf);
		printbuf_exit(&buf);

		set_btree_node_read_error(b);
		clear_btree_node_read_in_flight(b);
		smp_mb__after_atomic();
		wake_up_bit(&b->flags, BTREE_NODE_read_in_flight);
		return;
	}

	ca = bch2_dev_get_ioref(c, pick.ptr.dev, READ, BCH_DEV_READ_REF_btree_node_read);

	bio = bio_alloc_bioset(NULL,
			       buf_pages(b->data, btree_buf_bytes(b)),
			       REQ_OP_READ|REQ_SYNC|REQ_META,
			       GFP_NOFS,
			       &c->btree_bio);
	rb = container_of(bio, struct btree_read_bio, bio);
	rb->c			= c;
	rb->b			= b;
	rb->ra			= NULL;
	rb->start_time		= local_clock();
	rb->have_ioref		= ca != NULL;
	rb->pick		= pick;
	INIT_WORK(&rb->work, btree_node_read_work);
	bio->bi_iter.bi_sector	= pick.ptr.offset;
	bio->bi_end_io		= btree_node_read_endio;
	bch2_bio_map(bio, b->data, btree_buf_bytes(b));

	async_object_list_add(c, btree_read_bio, rb, &rb->list_idx);

	if (rb->have_ioref) {
		this_cpu_add(ca->io_done->sectors[READ][BCH_DATA_btree],
			     bio_sectors(bio));
		bio_set_dev(bio, ca->disk_sb.bdev);

		if (sync) {
			submit_bio_wait(bio);
			bch2_latency_acct(ca, rb->start_time, READ);
			btree_node_read_work(&rb->work);
		} else {
			submit_bio(bio);
		}
	} else {
		bio->bi_status = BLK_STS_REMOVED;

		if (sync)
			btree_node_read_work(&rb->work);
		else
			queue_work(c->btree_read_complete_wq, &rb->work);
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
		ret = bch2_btree_cache_cannibalize_lock(trans, &cl);
		closure_sync(&cl);
	} while (ret);

	b = bch2_btree_node_mem_alloc(trans, level != 0);
	bch2_btree_cache_cannibalize_unlock(trans);

	BUG_ON(IS_ERR(b));

	bkey_copy(&b->key, k);
	BUG_ON(bch2_btree_node_hash_insert(&c->btree_cache, b, level, id));

	set_btree_node_read_in_flight(b);

	/* we can't pass the trans to read_done() for fsck errors, so it must be unlocked */
	bch2_trans_unlock(trans);
	bch2_btree_node_read(trans, b, true);

	if (btree_node_read_error(b)) {
		mutex_lock(&c->btree_cache.lock);
		bch2_btree_node_hash_remove(&c->btree_cache, b);
		mutex_unlock(&c->btree_cache.lock);

		ret = bch_err_throw(c, btree_node_read_error);
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

struct btree_node_scrub {
	struct bch_fs		*c;
	struct bch_dev		*ca;
	void			*buf;
	bool			used_mempool;
	unsigned		written;

	enum btree_id		btree;
	unsigned		level;
	struct bkey_buf		key;
	__le64			seq;

	struct work_struct	work;
	struct bio		bio;
};

static bool btree_node_scrub_check(struct bch_fs *c, struct btree_node *data, unsigned ptr_written,
				   struct printbuf *err)
{
	unsigned written = 0;

	if (le64_to_cpu(data->magic) != bset_magic(c)) {
		prt_printf(err, "bad magic: want %llx, got %llx",
			   bset_magic(c), le64_to_cpu(data->magic));
		return false;
	}

	while (written < (ptr_written ?: btree_sectors(c))) {
		struct btree_node_entry *bne;
		struct bset *i;
		bool first = !written;

		if (first) {
			bne = NULL;
			i = &data->keys;
		} else {
			bne = (void *) data + (written << 9);
			i = &bne->keys;

			if (!ptr_written && i->seq != data->keys.seq)
				break;
		}

		struct nonce nonce = btree_nonce(i, written << 9);
		bool good_csum_type = bch2_checksum_type_valid(c, BSET_CSUM_TYPE(i));

		if (first) {
			if (good_csum_type) {
				struct bch_csum csum = csum_vstruct(c, BSET_CSUM_TYPE(i), nonce, data);
				if (bch2_crc_cmp(data->csum, csum)) {
					bch2_csum_err_msg(err, BSET_CSUM_TYPE(i), data->csum, csum);
					return false;
				}
			}

			written += vstruct_sectors(data, c->block_bits);
		} else {
			if (good_csum_type) {
				struct bch_csum csum = csum_vstruct(c, BSET_CSUM_TYPE(i), nonce, bne);
				if (bch2_crc_cmp(bne->csum, csum)) {
					bch2_csum_err_msg(err, BSET_CSUM_TYPE(i), bne->csum, csum);
					return false;
				}
			}

			written += vstruct_sectors(bne, c->block_bits);
		}
	}

	return true;
}

static void btree_node_scrub_work(struct work_struct *work)
{
	struct btree_node_scrub *scrub = container_of(work, struct btree_node_scrub, work);
	struct bch_fs *c = scrub->c;
	struct printbuf err = PRINTBUF;

	__bch2_btree_pos_to_text(&err, c, scrub->btree, scrub->level,
				 bkey_i_to_s_c(scrub->key.k));
	prt_newline(&err);

	if (!btree_node_scrub_check(c, scrub->buf, scrub->written, &err)) {
		struct btree_trans *trans = bch2_trans_get(c);

		struct btree_iter iter;
		bch2_trans_node_iter_init(trans, &iter, scrub->btree,
					  scrub->key.k->k.p, 0, scrub->level - 1, 0);

		struct btree *b;
		int ret = lockrestart_do(trans,
			PTR_ERR_OR_ZERO(b = bch2_btree_iter_peek_node(trans, &iter)));
		if (ret)
			goto err;

		if (bkey_i_to_btree_ptr_v2(&b->key)->v.seq == scrub->seq) {
			bch_err(c, "error validating btree node during scrub on %s at btree %s",
				scrub->ca->name, err.buf);

			ret = bch2_btree_node_rewrite(trans, &iter, b, 0, 0);
		}
err:
		bch2_trans_iter_exit(trans, &iter);
		bch2_trans_begin(trans);
		bch2_trans_put(trans);
	}

	printbuf_exit(&err);
	bch2_bkey_buf_exit(&scrub->key, c);;
	btree_bounce_free(c, c->opts.btree_node_size, scrub->used_mempool, scrub->buf);
	enumerated_ref_put(&scrub->ca->io_ref[READ], BCH_DEV_READ_REF_btree_node_scrub);
	kfree(scrub);
	enumerated_ref_put(&c->writes, BCH_WRITE_REF_btree_node_scrub);
}

static void btree_node_scrub_endio(struct bio *bio)
{
	struct btree_node_scrub *scrub = container_of(bio, struct btree_node_scrub, bio);

	queue_work(scrub->c->btree_read_complete_wq, &scrub->work);
}

int bch2_btree_node_scrub(struct btree_trans *trans,
			  enum btree_id btree, unsigned level,
			  struct bkey_s_c k, unsigned dev)
{
	if (k.k->type != KEY_TYPE_btree_ptr_v2)
		return 0;

	struct bch_fs *c = trans->c;

	if (!enumerated_ref_tryget(&c->writes, BCH_WRITE_REF_btree_node_scrub))
		return bch_err_throw(c, erofs_no_writes);

	struct extent_ptr_decoded pick;
	int ret = bch2_bkey_pick_read_device(c, k, NULL, &pick, dev);
	if (ret <= 0)
		goto err;

	struct bch_dev *ca = bch2_dev_get_ioref(c, pick.ptr.dev, READ,
						BCH_DEV_READ_REF_btree_node_scrub);
	if (!ca) {
		ret = bch_err_throw(c, device_offline);
		goto err;
	}

	bool used_mempool = false;
	void *buf = btree_bounce_alloc(c, c->opts.btree_node_size, &used_mempool);

	unsigned vecs = buf_pages(buf, c->opts.btree_node_size);

	struct btree_node_scrub *scrub =
		kzalloc(sizeof(*scrub) + sizeof(struct bio_vec) * vecs, GFP_KERNEL);
	if (!scrub) {
		ret = -ENOMEM;
		goto err_free;
	}

	scrub->c		= c;
	scrub->ca		= ca;
	scrub->buf		= buf;
	scrub->used_mempool	= used_mempool;
	scrub->written		= btree_ptr_sectors_written(k);

	scrub->btree		= btree;
	scrub->level		= level;
	bch2_bkey_buf_init(&scrub->key);
	bch2_bkey_buf_reassemble(&scrub->key, c, k);
	scrub->seq		= bkey_s_c_to_btree_ptr_v2(k).v->seq;

	INIT_WORK(&scrub->work, btree_node_scrub_work);

	bio_init(&scrub->bio, ca->disk_sb.bdev, scrub->bio.bi_inline_vecs, vecs, REQ_OP_READ);
	bch2_bio_map(&scrub->bio, scrub->buf, c->opts.btree_node_size);
	scrub->bio.bi_iter.bi_sector	= pick.ptr.offset;
	scrub->bio.bi_end_io		= btree_node_scrub_endio;
	submit_bio(&scrub->bio);
	return 0;
err_free:
	btree_bounce_free(c, c->opts.btree_node_size, used_mempool, buf);
	enumerated_ref_put(&ca->io_ref[READ], BCH_DEV_READ_REF_btree_node_scrub);
err:
	enumerated_ref_put(&c->writes, BCH_WRITE_REF_btree_node_scrub);
	return ret;
}

static void bch2_btree_complete_write(struct bch_fs *c, struct btree *b,
				      struct btree_write *w)
{
	unsigned long old, new;

	old = READ_ONCE(b->will_make_reachable);
	do {
		new = old;
		if (!(old & 1))
			break;

		new &= ~1UL;
	} while (!try_cmpxchg(&b->will_make_reachable, &old, new));

	if (old & 1)
		closure_put(&((struct btree_update *) new)->cl);

	bch2_journal_pin_drop(&c->journal, &w->journal);
}

static void __btree_node_write_done(struct bch_fs *c, struct btree *b, u64 start_time)
{
	struct btree_write *w = btree_prev_write(b);
	unsigned long old, new;
	unsigned type = 0;

	bch2_btree_complete_write(c, b, w);

	if (start_time)
		bch2_time_stats_update(&c->times[BCH_TIME_btree_node_write], start_time);

	old = READ_ONCE(b->flags);
	do {
		new = old;

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
	} while (!try_cmpxchg(&b->flags, &old, new));

	if (new & (1U << BTREE_NODE_write_in_flight))
		__bch2_btree_node_write(c, b, BTREE_WRITE_ALREADY_STARTED|type);
	else {
		smp_mb__after_atomic();
		wake_up_bit(&b->flags, BTREE_NODE_write_in_flight);
	}
}

static void btree_node_write_done(struct bch_fs *c, struct btree *b, u64 start_time)
{
	struct btree_trans *trans = bch2_trans_get(c);

	btree_node_lock_nopath_nofail(trans, &b->c, SIX_LOCK_read);

	/* we don't need transaction context anymore after we got the lock. */
	bch2_trans_put(trans);
	__btree_node_write_done(c, b, start_time);
	six_unlock_read(&b->c.lock);
}

static void btree_node_write_work(struct work_struct *work)
{
	struct btree_write_bio *wbio =
		container_of(work, struct btree_write_bio, work);
	struct bch_fs *c	= wbio->wbio.c;
	struct btree *b		= wbio->wbio.bio.bi_private;
	u64 start_time		= wbio->start_time;
	int ret = 0;

	btree_bounce_free(c,
		wbio->data_bytes,
		wbio->wbio.used_mempool,
		wbio->data);

	bch2_bkey_drop_ptrs(bkey_i_to_s(&wbio->key), ptr,
		bch2_dev_list_has_dev(wbio->wbio.failed, ptr->dev));

	if (!bch2_bkey_nr_ptrs(bkey_i_to_s_c(&wbio->key))) {
		ret = bch_err_throw(c, btree_node_write_all_failed);
		goto err;
	}

	if (wbio->wbio.first_btree_write) {
		if (wbio->wbio.failed.nr) {

		}
	} else {
		ret = bch2_trans_do(c,
			bch2_btree_node_update_key_get_iter(trans, b, &wbio->key,
					BCH_WATERMARK_interior_updates|
					BCH_TRANS_COMMIT_journal_reclaim|
					BCH_TRANS_COMMIT_no_enospc|
					BCH_TRANS_COMMIT_no_check_rw,
					!wbio->wbio.failed.nr));
		if (ret)
			goto err;
	}
out:
	async_object_list_del(c, btree_write_bio, wbio->list_idx);
	bio_put(&wbio->wbio.bio);
	btree_node_write_done(c, b, start_time);
	return;
err:
	set_btree_node_noevict(b);

	if (!bch2_err_matches(ret, EROFS)) {
		struct printbuf buf = PRINTBUF;
		prt_printf(&buf, "writing btree node: %s\n  ", bch2_err_str(ret));
		bch2_btree_pos_to_text(&buf, c, b);
		bch2_fs_fatal_error(c, "%s", buf.buf);
		printbuf_exit(&buf);
	}
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
	struct bch_dev *ca		= wbio->have_ioref ? bch2_dev_have_ref(c, wbio->dev) : NULL;

	bch2_account_io_completion(ca, BCH_MEMBER_ERROR_write,
				   wbio->submit_time, !bio->bi_status);

	if (ca && bio->bi_status) {
		struct printbuf buf = PRINTBUF;
		buf.atomic++;
		prt_printf(&buf, "btree write error: %s\n  ",
			   bch2_blk_status_to_str(bio->bi_status));
		bch2_btree_pos_to_text(&buf, c, b);
		bch_err_dev_ratelimited(ca, "%s", buf.buf);
		printbuf_exit(&buf);
	}

	if (bio->bi_status) {
		unsigned long flags;
		spin_lock_irqsave(&c->btree_write_error_lock, flags);
		bch2_dev_list_add_dev(&orig->failed, wbio->dev);
		spin_unlock_irqrestore(&c->btree_write_error_lock, flags);
	}

	/*
	 * XXX: we should be using io_ref[WRITE], but we aren't retrying failed
	 * btree writes yet (due to device removal/ro):
	 */
	if (wbio->have_ioref)
		enumerated_ref_put(&ca->io_ref[READ],
				   BCH_DEV_READ_REF_btree_node_write);

	if (parent) {
		bio_put(bio);
		bio_endio(&parent->bio);
		return;
	}

	clear_btree_node_write_in_flight_inner(b);
	smp_mb__after_atomic();
	wake_up_bit(&b->flags, BTREE_NODE_write_in_flight_inner);
	INIT_WORK(&wb->work, btree_node_write_work);
	queue_work(c->btree_write_complete_wq, &wb->work);
}

static int validate_bset_for_write(struct bch_fs *c, struct btree *b,
				   struct bset *i, unsigned sectors)
{
	int ret = bch2_bkey_validate(c, bkey_i_to_s_c(&b->key),
				     (struct bkey_validate_context) {
					.from	= BKEY_VALIDATE_btree_node,
					.level	= b->c.level + 1,
					.btree	= b->c.btree_id,
					.flags	= BCH_VALIDATE_write,
				     });
	if (ret) {
		bch2_fs_inconsistent(c, "invalid btree node key before write");
		return ret;
	}

	ret = validate_bset_keys(c, b, i, WRITE, NULL, NULL) ?:
		validate_bset(c, NULL, b, i, b->written, sectors, WRITE, NULL, NULL);
	if (ret) {
		bch2_inconsistent_error(c);
		dump_stack();
	}

	return ret;
}

static void btree_write_submit(struct work_struct *work)
{
	struct btree_write_bio *wbio = container_of(work, struct btree_write_bio, work);
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
	u64 start_time = local_clock();
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
	old = READ_ONCE(b->flags);
	do {
		new = old;

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
	} while (!try_cmpxchg_acquire(&b->flags, &old, new));

	if (new & (1U << BTREE_NODE_need_write))
		return;
do_write:
	BUG_ON((type == BTREE_WRITE_initial) != (b->written == 0));

	atomic_long_dec(&c->btree_cache.nr_dirty);

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
		      unwritten_whiteouts_start(b),
		      unwritten_whiteouts_end(b));
	SET_BSET_SEPARATE_WHITEOUTS(i, false);

	u64s = bch2_sort_keys_keep_unwritten_whiteouts(i->start, &sort_iter.iter);
	le16_add_cpu(&i->u64s, u64s);

	b->whiteout_u64s = 0;

	BUG_ON(!b->written && i->u64s != b->data->keys.u64s);

	set_needs_whiteout(i, false);

	/* do we have data to write? */
	if (b->written && !i->u64s)
		goto nowrite;

	bytes_to_write = vstruct_end(i) - data;
	sectors_to_write = round_up(bytes_to_write, block_bytes(c)) >> 9;

	if (!b->written &&
	    b->key.k.type == KEY_TYPE_btree_ptr_v2)
		BUG_ON(btree_ptr_sectors_written(bkey_i_to_s_c(&b->key)) != sectors_to_write);

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
			"encrypting btree node: %s", bch2_err_str(ret)))
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
	wbio->start_time		= start_time;
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

	async_object_list_add(c, btree_write_bio, wbio, &wbio->list_idx);

	INIT_WORK(&wbio->work, btree_write_submit);
	queue_work(c->btree_write_submit_wq, &wbio->work);
	return;
err:
	set_btree_node_noevict(b);
	b->written += sectors_to_write;
nowrite:
	btree_bounce_free(c, bytes, used_mempool, data);
	__btree_node_write_done(c, b, 0);
}

/*
 * Work that must be done with write lock held:
 */
bool bch2_btree_post_write_cleanup(struct bch_fs *c, struct btree *b)
{
	bool invalidated_iter = false;
	struct btree_node_entry *bne;

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
		btree_node_sort(c, b, 0, b->nsets);
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
		bch2_bset_init_next(b, bne);

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

void bch2_btree_node_write_trans(struct btree_trans *trans, struct btree *b,
				 enum six_lock_type lock_type_held,
				 unsigned flags)
{
	struct bch_fs *c = trans->c;

	if (lock_type_held == SIX_LOCK_intent ||
	    (lock_type_held == SIX_LOCK_read &&
	     six_lock_tryupgrade(&b->c.lock))) {
		__bch2_btree_node_write(c, b, flags);

		/* don't cycle lock unnecessarily: */
		if (btree_node_just_written(b) &&
		    six_trylock_write(&b->c.lock)) {
			bch2_btree_post_write_cleanup(c, b);
			__bch2_btree_node_unlock_write(trans, b);
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

	prt_printf(out, "\tnr\tsize\n");

	for (unsigned i = 0; i < BTREE_WRITE_TYPE_NR; i++) {
		u64 nr		= atomic64_read(&c->btree_write_stats[i].nr);
		u64 bytes	= atomic64_read(&c->btree_write_stats[i].bytes);

		prt_printf(out, "%s:\t%llu\t", bch2_btree_write_types[i], nr);
		prt_human_readable_u64(out, nr ? div64_u64(bytes, nr) : 0);
		prt_newline(out);
	}
}
