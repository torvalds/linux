// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2010 Kent Overstreet <kent.overstreet@gmail.com>
 * Copyright (C) 2014 Datera Inc.
 */

#include "bcachefs.h"
#include "alloc_background.h"
#include "alloc_foreground.h"
#include "bkey_methods.h"
#include "btree_locking.h"
#include "btree_update_interior.h"
#include "btree_io.h"
#include "btree_gc.h"
#include "buckets.h"
#include "clock.h"
#include "debug.h"
#include "ec.h"
#include "error.h"
#include "extents.h"
#include "journal.h"
#include "keylist.h"
#include "move.h"
#include "recovery.h"
#include "replicas.h"
#include "super-io.h"
#include "trace.h"

#include <linux/slab.h>
#include <linux/bitops.h>
#include <linux/freezer.h>
#include <linux/kthread.h>
#include <linux/preempt.h>
#include <linux/rcupdate.h>
#include <linux/sched/task.h>

static inline void __gc_pos_set(struct bch_fs *c, struct gc_pos new_pos)
{
	preempt_disable();
	write_seqcount_begin(&c->gc_pos_lock);
	c->gc_pos = new_pos;
	write_seqcount_end(&c->gc_pos_lock);
	preempt_enable();
}

static inline void gc_pos_set(struct bch_fs *c, struct gc_pos new_pos)
{
	BUG_ON(gc_pos_cmp(new_pos, c->gc_pos) <= 0);
	__gc_pos_set(c, new_pos);
}

static int bch2_gc_check_topology(struct bch_fs *c,
				  struct bkey_s_c k,
				  struct bpos *expected_start,
				  struct bpos expected_end,
				  bool is_last)
{
	int ret = 0;

	if (k.k->type == KEY_TYPE_btree_ptr_v2) {
		struct bkey_s_c_btree_ptr_v2 bp = bkey_s_c_to_btree_ptr_v2(k);

		if (fsck_err_on(bkey_cmp(*expected_start, bp.v->min_key), c,
				"btree node with incorrect min_key: got %llu:%llu, should be %llu:%llu",
				bp.v->min_key.inode,
				bp.v->min_key.offset,
				expected_start->inode,
				expected_start->offset)) {
			BUG();
		}
	}

	*expected_start = bkey_cmp(k.k->p, POS_MAX)
		? bkey_successor(k.k->p)
		: k.k->p;

	if (fsck_err_on(is_last &&
			bkey_cmp(k.k->p, expected_end), c,
			"btree node with incorrect max_key: got %llu:%llu, should be %llu:%llu",
			k.k->p.inode,
			k.k->p.offset,
			expected_end.inode,
			expected_end.offset)) {
		BUG();
	}
fsck_err:
	return ret;
}

/* marking of btree keys/nodes: */

static int bch2_gc_mark_key(struct bch_fs *c, struct bkey_s_c k,
			    u8 *max_stale, bool initial)
{
	struct bkey_ptrs_c ptrs = bch2_bkey_ptrs_c(k);
	const struct bch_extent_ptr *ptr;
	unsigned flags =
		BTREE_TRIGGER_GC|
		(initial ? BTREE_TRIGGER_NOATOMIC : 0);
	int ret = 0;

	if (initial) {
		BUG_ON(journal_seq_verify(c) &&
		       k.k->version.lo > journal_cur_seq(&c->journal));

		/* XXX change to fsck check */
		if (fsck_err_on(k.k->version.lo > atomic64_read(&c->key_version), c,
				"key version number higher than recorded: %llu > %llu",
				k.k->version.lo,
				atomic64_read(&c->key_version)))
			atomic64_set(&c->key_version, k.k->version.lo);

		if (test_bit(BCH_FS_REBUILD_REPLICAS, &c->flags) ||
		    fsck_err_on(!bch2_bkey_replicas_marked(c, k, false), c,
				"superblock not marked as containing replicas (type %u)",
				k.k->type)) {
			ret = bch2_mark_bkey_replicas(c, k);
			if (ret)
				return ret;
		}

		bkey_for_each_ptr(ptrs, ptr) {
			struct bch_dev *ca = bch_dev_bkey_exists(c, ptr->dev);
			struct bucket *g = PTR_BUCKET(ca, ptr, true);
			struct bucket *g2 = PTR_BUCKET(ca, ptr, false);

			if (mustfix_fsck_err_on(!g->gen_valid, c,
					"bucket %u:%zu data type %s ptr gen %u missing in alloc btree",
					ptr->dev, PTR_BUCKET_NR(ca, ptr),
					bch2_data_types[ptr_data_type(k.k, ptr)],
					ptr->gen)) {
				g2->_mark.gen	= g->_mark.gen		= ptr->gen;
				g2->gen_valid	= g->gen_valid		= true;
			}

			if (mustfix_fsck_err_on(gen_cmp(ptr->gen, g->mark.gen) > 0, c,
					"bucket %u:%zu data type %s ptr gen in the future: %u > %u",
					ptr->dev, PTR_BUCKET_NR(ca, ptr),
					bch2_data_types[ptr_data_type(k.k, ptr)],
					ptr->gen, g->mark.gen)) {
				g2->_mark.gen	= g->_mark.gen		= ptr->gen;
				g2->gen_valid	= g->gen_valid		= true;
				g2->_mark.data_type		= 0;
				g2->_mark.dirty_sectors		= 0;
				g2->_mark.cached_sectors	= 0;
				set_bit(BCH_FS_FIXED_GENS, &c->flags);
			}
		}
	}

	bkey_for_each_ptr(ptrs, ptr) {
		struct bch_dev *ca = bch_dev_bkey_exists(c, ptr->dev);
		struct bucket *g = PTR_BUCKET(ca, ptr, true);

		if (gen_after(g->oldest_gen, ptr->gen))
			g->oldest_gen = ptr->gen;

		*max_stale = max(*max_stale, ptr_stale(ca, ptr));
	}

	bch2_mark_key(c, k, 0, k.k->size, NULL, 0, flags);
fsck_err:
	return ret;
}

static int btree_gc_mark_node(struct bch_fs *c, struct btree *b, u8 *max_stale,
			      bool initial)
{
	struct bpos next_node_start = b->data->min_key;
	struct btree_node_iter iter;
	struct bkey unpacked;
	struct bkey_s_c k;
	int ret = 0;

	*max_stale = 0;

	if (!btree_node_type_needs_gc(btree_node_type(b)))
		return 0;

	bch2_btree_node_iter_init_from_start(&iter, b);

	while ((k = bch2_btree_node_iter_peek_unpack(&iter, b, &unpacked)).k) {
		bch2_bkey_debugcheck(c, b, k);

		ret = bch2_gc_mark_key(c, k, max_stale, initial);
		if (ret)
			break;

		bch2_btree_node_iter_advance(&iter, b);

		if (b->c.level) {
			ret = bch2_gc_check_topology(c, k,
					&next_node_start,
					b->data->max_key,
					bch2_btree_node_iter_end(&iter));
			if (ret)
				break;
		}
	}

	return ret;
}

static int bch2_gc_btree(struct bch_fs *c, enum btree_id btree_id,
			 bool initial, bool metadata_only)
{
	struct btree_trans trans;
	struct btree_iter *iter;
	struct btree *b;
	unsigned depth = metadata_only			? 1
		: expensive_debug_checks(c)		? 0
		: !btree_node_type_needs_gc(btree_id)	? 1
		: 0;
	u8 max_stale = 0;
	int ret = 0;

	bch2_trans_init(&trans, c, 0, 0);

	gc_pos_set(c, gc_pos_btree(btree_id, POS_MIN, 0));

	__for_each_btree_node(&trans, iter, btree_id, POS_MIN,
			      0, depth, BTREE_ITER_PREFETCH, b) {
		bch2_verify_btree_nr_keys(b);

		gc_pos_set(c, gc_pos_btree_node(b));

		ret = btree_gc_mark_node(c, b, &max_stale, initial);
		if (ret)
			break;

		if (!initial) {
			if (max_stale > 64)
				bch2_btree_node_rewrite(c, iter,
						b->data->keys.seq,
						BTREE_INSERT_USE_RESERVE|
						BTREE_INSERT_NOWAIT|
						BTREE_INSERT_GC_LOCK_HELD);
			else if (!btree_gc_rewrite_disabled(c) &&
				 (btree_gc_always_rewrite(c) || max_stale > 16))
				bch2_btree_node_rewrite(c, iter,
						b->data->keys.seq,
						BTREE_INSERT_NOWAIT|
						BTREE_INSERT_GC_LOCK_HELD);
		}

		bch2_trans_cond_resched(&trans);
	}
	ret = bch2_trans_exit(&trans) ?: ret;
	if (ret)
		return ret;

	mutex_lock(&c->btree_root_lock);
	b = c->btree_roots[btree_id].b;
	if (!btree_node_fake(b))
		ret = bch2_gc_mark_key(c, bkey_i_to_s_c(&b->key),
				       &max_stale, initial);
	gc_pos_set(c, gc_pos_btree_root(b->c.btree_id));
	mutex_unlock(&c->btree_root_lock);

	return ret;
}

static int bch2_gc_btree_init_recurse(struct bch_fs *c, struct btree *b,
				      struct journal_keys *journal_keys,
				      unsigned target_depth)
{
	struct btree_and_journal_iter iter;
	struct bkey_s_c k;
	struct bpos next_node_start = b->data->min_key;
	u8 max_stale = 0;
	int ret = 0;

	bch2_btree_and_journal_iter_init_node_iter(&iter, journal_keys, b);

	while ((k = bch2_btree_and_journal_iter_peek(&iter)).k) {
		bch2_bkey_debugcheck(c, b, k);

		BUG_ON(bkey_cmp(k.k->p, b->data->min_key) < 0);
		BUG_ON(bkey_cmp(k.k->p, b->data->max_key) > 0);

		ret = bch2_gc_mark_key(c, k, &max_stale, true);
		if (ret)
			break;

		if (b->c.level) {
			struct btree *child;
			BKEY_PADDED(k) tmp;

			bkey_reassemble(&tmp.k, k);
			k = bkey_i_to_s_c(&tmp.k);

			bch2_btree_and_journal_iter_advance(&iter);

			ret = bch2_gc_check_topology(c, k,
					&next_node_start,
					b->data->max_key,
					!bch2_btree_and_journal_iter_peek(&iter).k);
			if (ret)
				break;

			if (b->c.level > target_depth) {
				child = bch2_btree_node_get_noiter(c, &tmp.k,
							b->c.btree_id, b->c.level - 1);
				ret = PTR_ERR_OR_ZERO(child);
				if (ret)
					break;

				ret = bch2_gc_btree_init_recurse(c, child,
						journal_keys, target_depth);
				six_unlock_read(&child->c.lock);

				if (ret)
					break;
			}
		} else {
			bch2_btree_and_journal_iter_advance(&iter);
		}
	}

	return ret;
}

static int bch2_gc_btree_init(struct bch_fs *c,
			      struct journal_keys *journal_keys,
			      enum btree_id btree_id,
			      bool metadata_only)
{
	struct btree *b;
	unsigned target_depth = metadata_only		? 1
		: expensive_debug_checks(c)		? 0
		: !btree_node_type_needs_gc(btree_id)	? 1
		: 0;
	u8 max_stale = 0;
	int ret = 0;

	b = c->btree_roots[btree_id].b;

	if (btree_node_fake(b))
		return 0;

	six_lock_read(&b->c.lock, NULL, NULL);
	if (fsck_err_on(bkey_cmp(b->data->min_key, POS_MIN), c,
			"btree root with incorrect min_key: %llu:%llu",
			b->data->min_key.inode,
			b->data->min_key.offset)) {
		BUG();
	}

	if (fsck_err_on(bkey_cmp(b->data->max_key, POS_MAX), c,
			"btree root with incorrect min_key: %llu:%llu",
			b->data->max_key.inode,
			b->data->max_key.offset)) {
		BUG();
	}

	if (b->c.level >= target_depth)
		ret = bch2_gc_btree_init_recurse(c, b,
					journal_keys, target_depth);

	if (!ret)
		ret = bch2_gc_mark_key(c, bkey_i_to_s_c(&b->key),
				       &max_stale, true);
fsck_err:
	six_unlock_read(&b->c.lock);

	return ret;
}

static inline int btree_id_gc_phase_cmp(enum btree_id l, enum btree_id r)
{
	return  (int) btree_id_to_gc_phase(l) -
		(int) btree_id_to_gc_phase(r);
}

static int bch2_gc_btrees(struct bch_fs *c, struct journal_keys *journal_keys,
			  bool initial, bool metadata_only)
{
	enum btree_id ids[BTREE_ID_NR];
	unsigned i;

	for (i = 0; i < BTREE_ID_NR; i++)
		ids[i] = i;
	bubble_sort(ids, BTREE_ID_NR, btree_id_gc_phase_cmp);

	for (i = 0; i < BTREE_ID_NR; i++) {
		enum btree_id id = ids[i];
		int ret = initial
			? bch2_gc_btree_init(c, journal_keys,
					     id, metadata_only)
			: bch2_gc_btree(c, id, initial, metadata_only);
		if (ret)
			return ret;
	}

	return 0;
}

static void mark_metadata_sectors(struct bch_fs *c, struct bch_dev *ca,
				  u64 start, u64 end,
				  enum bch_data_type type,
				  unsigned flags)
{
	u64 b = sector_to_bucket(ca, start);

	do {
		unsigned sectors =
			min_t(u64, bucket_to_sector(ca, b + 1), end) - start;

		bch2_mark_metadata_bucket(c, ca, b, type, sectors,
					  gc_phase(GC_PHASE_SB), flags);
		b++;
		start += sectors;
	} while (start < end);
}

void bch2_mark_dev_superblock(struct bch_fs *c, struct bch_dev *ca,
			      unsigned flags)
{
	struct bch_sb_layout *layout = &ca->disk_sb.sb->layout;
	unsigned i;
	u64 b;

	/*
	 * This conditional is kind of gross, but we may be called from the
	 * device add path, before the new device has actually been added to the
	 * running filesystem:
	 */
	if (c) {
		lockdep_assert_held(&c->sb_lock);
		percpu_down_read(&c->mark_lock);
	}

	for (i = 0; i < layout->nr_superblocks; i++) {
		u64 offset = le64_to_cpu(layout->sb_offset[i]);

		if (offset == BCH_SB_SECTOR)
			mark_metadata_sectors(c, ca, 0, BCH_SB_SECTOR,
					      BCH_DATA_SB, flags);

		mark_metadata_sectors(c, ca, offset,
				      offset + (1 << layout->sb_max_size_bits),
				      BCH_DATA_SB, flags);
	}

	for (i = 0; i < ca->journal.nr; i++) {
		b = ca->journal.buckets[i];
		bch2_mark_metadata_bucket(c, ca, b, BCH_DATA_JOURNAL,
					  ca->mi.bucket_size,
					  gc_phase(GC_PHASE_SB), flags);
	}

	if (c)
		percpu_up_read(&c->mark_lock);
}

static void bch2_mark_superblocks(struct bch_fs *c)
{
	struct bch_dev *ca;
	unsigned i;

	mutex_lock(&c->sb_lock);
	gc_pos_set(c, gc_phase(GC_PHASE_SB));

	for_each_online_member(ca, c, i)
		bch2_mark_dev_superblock(c, ca, BTREE_TRIGGER_GC);
	mutex_unlock(&c->sb_lock);
}

/* Also see bch2_pending_btree_node_free_insert_done() */
static void bch2_mark_pending_btree_node_frees(struct bch_fs *c)
{
	struct btree_update *as;
	struct pending_btree_node_free *d;

	mutex_lock(&c->btree_interior_update_lock);
	gc_pos_set(c, gc_phase(GC_PHASE_PENDING_DELETE));

	for_each_pending_btree_node_free(c, as, d)
		if (d->index_update_done)
			bch2_mark_key(c, bkey_i_to_s_c(&d->key),
				      0, 0, NULL, 0,
				      BTREE_TRIGGER_GC);

	mutex_unlock(&c->btree_interior_update_lock);
}

static void bch2_mark_allocator_buckets(struct bch_fs *c)
{
	struct bch_dev *ca;
	struct open_bucket *ob;
	size_t i, j, iter;
	unsigned ci;

	percpu_down_read(&c->mark_lock);

	spin_lock(&c->freelist_lock);
	gc_pos_set(c, gc_pos_alloc(c, NULL));

	for_each_member_device(ca, c, ci) {
		fifo_for_each_entry(i, &ca->free_inc, iter)
			bch2_mark_alloc_bucket(c, ca, i, true,
					       gc_pos_alloc(c, NULL),
					       BTREE_TRIGGER_GC);



		for (j = 0; j < RESERVE_NR; j++)
			fifo_for_each_entry(i, &ca->free[j], iter)
				bch2_mark_alloc_bucket(c, ca, i, true,
						       gc_pos_alloc(c, NULL),
						       BTREE_TRIGGER_GC);
	}

	spin_unlock(&c->freelist_lock);

	for (ob = c->open_buckets;
	     ob < c->open_buckets + ARRAY_SIZE(c->open_buckets);
	     ob++) {
		spin_lock(&ob->lock);
		if (ob->valid) {
			gc_pos_set(c, gc_pos_alloc(c, ob));
			ca = bch_dev_bkey_exists(c, ob->ptr.dev);
			bch2_mark_alloc_bucket(c, ca, PTR_BUCKET_NR(ca, &ob->ptr), true,
					       gc_pos_alloc(c, ob),
					       BTREE_TRIGGER_GC);
		}
		spin_unlock(&ob->lock);
	}

	percpu_up_read(&c->mark_lock);
}

static void bch2_gc_free(struct bch_fs *c)
{
	struct bch_dev *ca;
	unsigned i;

	genradix_free(&c->stripes[1]);

	for_each_member_device(ca, c, i) {
		kvpfree(rcu_dereference_protected(ca->buckets[1], 1),
			sizeof(struct bucket_array) +
			ca->mi.nbuckets * sizeof(struct bucket));
		ca->buckets[1] = NULL;

		free_percpu(ca->usage[1]);
		ca->usage[1] = NULL;
	}

	free_percpu(c->usage_gc);
	c->usage_gc = NULL;
}

static int bch2_gc_done(struct bch_fs *c,
			bool initial, bool metadata_only)
{
	struct bch_dev *ca;
	bool verify = !metadata_only &&
		(!initial ||
		 (c->sb.compat & (1ULL << BCH_COMPAT_FEAT_ALLOC_INFO)));
	unsigned i;
	int ret = 0;

#define copy_field(_f, _msg, ...)					\
	if (dst->_f != src->_f) {					\
		if (verify)						\
			fsck_err(c, _msg ": got %llu, should be %llu"	\
				, ##__VA_ARGS__, dst->_f, src->_f);	\
		dst->_f = src->_f;					\
	}
#define copy_stripe_field(_f, _msg, ...)				\
	if (dst->_f != src->_f) {					\
		if (verify)						\
			fsck_err(c, "stripe %zu has wrong "_msg		\
				": got %u, should be %u",		\
				dst_iter.pos, ##__VA_ARGS__,		\
				dst->_f, src->_f);			\
		dst->_f = src->_f;					\
		dst->dirty = true;					\
	}
#define copy_bucket_field(_f)						\
	if (dst->b[b].mark._f != src->b[b].mark._f) {			\
		if (verify)						\
			fsck_err(c, "dev %u bucket %zu has wrong " #_f	\
				": got %u, should be %u", i, b,		\
				dst->b[b].mark._f, src->b[b].mark._f);	\
		dst->b[b]._mark._f = src->b[b].mark._f;			\
	}
#define copy_dev_field(_f, _msg, ...)					\
	copy_field(_f, "dev %u has wrong " _msg, i, ##__VA_ARGS__)
#define copy_fs_field(_f, _msg, ...)					\
	copy_field(_f, "fs has wrong " _msg, ##__VA_ARGS__)

	if (!metadata_only) {
		struct genradix_iter dst_iter = genradix_iter_init(&c->stripes[0], 0);
		struct genradix_iter src_iter = genradix_iter_init(&c->stripes[1], 0);
		struct stripe *dst, *src;
		unsigned i;

		c->ec_stripes_heap.used = 0;

		while ((dst = genradix_iter_peek(&dst_iter, &c->stripes[0])) &&
		       (src = genradix_iter_peek(&src_iter, &c->stripes[1]))) {
			BUG_ON(src_iter.pos != dst_iter.pos);

			copy_stripe_field(alive,	"alive");
			copy_stripe_field(sectors,	"sectors");
			copy_stripe_field(algorithm,	"algorithm");
			copy_stripe_field(nr_blocks,	"nr_blocks");
			copy_stripe_field(nr_redundant,	"nr_redundant");
			copy_stripe_field(blocks_nonempty,
					  "blocks_nonempty");

			for (i = 0; i < ARRAY_SIZE(dst->block_sectors); i++)
				copy_stripe_field(block_sectors[i],
						  "block_sectors[%u]", i);

			if (dst->alive)
				bch2_stripes_heap_insert(c, dst, dst_iter.pos);

			genradix_iter_advance(&dst_iter, &c->stripes[0]);
			genradix_iter_advance(&src_iter, &c->stripes[1]);
		}
	}

	for_each_member_device(ca, c, i) {
		struct bucket_array *dst = __bucket_array(ca, 0);
		struct bucket_array *src = __bucket_array(ca, 1);
		size_t b;

		for (b = 0; b < src->nbuckets; b++) {
			copy_bucket_field(gen);
			copy_bucket_field(data_type);
			copy_bucket_field(owned_by_allocator);
			copy_bucket_field(stripe);
			copy_bucket_field(dirty_sectors);
			copy_bucket_field(cached_sectors);

			dst->b[b].oldest_gen = src->b[b].oldest_gen;
		}
	};

	for (i = 0; i < ARRAY_SIZE(c->usage); i++)
		bch2_fs_usage_acc_to_base(c, i);

	bch2_dev_usage_from_buckets(c);

	{
		unsigned nr = fs_usage_u64s(c);
		struct bch_fs_usage *dst = c->usage_base;
		struct bch_fs_usage *src = (void *)
			bch2_acc_percpu_u64s((void *) c->usage_gc, nr);

		copy_fs_field(hidden,		"hidden");
		copy_fs_field(btree,		"btree");

		if (!metadata_only) {
			copy_fs_field(data,	"data");
			copy_fs_field(cached,	"cached");
			copy_fs_field(reserved,	"reserved");
			copy_fs_field(nr_inodes,"nr_inodes");

			for (i = 0; i < BCH_REPLICAS_MAX; i++)
				copy_fs_field(persistent_reserved[i],
					      "persistent_reserved[%i]", i);
		}

		for (i = 0; i < c->replicas.nr; i++) {
			struct bch_replicas_entry *e =
				cpu_replicas_entry(&c->replicas, i);
			char buf[80];

			if (metadata_only &&
			    (e->data_type == BCH_DATA_USER ||
			     e->data_type == BCH_DATA_CACHED))
				continue;

			bch2_replicas_entry_to_text(&PBUF(buf), e);

			copy_fs_field(replicas[i], "%s", buf);
		}
	}

#undef copy_fs_field
#undef copy_dev_field
#undef copy_bucket_field
#undef copy_stripe_field
#undef copy_field
fsck_err:
	return ret;
}

static int bch2_gc_start(struct bch_fs *c,
			 bool metadata_only)
{
	struct bch_dev *ca;
	unsigned i;
	int ret;

	BUG_ON(c->usage_gc);

	c->usage_gc = __alloc_percpu_gfp(fs_usage_u64s(c) * sizeof(u64),
					 sizeof(u64), GFP_KERNEL);
	if (!c->usage_gc) {
		bch_err(c, "error allocating c->usage_gc");
		return -ENOMEM;
	}

	for_each_member_device(ca, c, i) {
		BUG_ON(ca->buckets[1]);
		BUG_ON(ca->usage[1]);

		ca->buckets[1] = kvpmalloc(sizeof(struct bucket_array) +
				ca->mi.nbuckets * sizeof(struct bucket),
				GFP_KERNEL|__GFP_ZERO);
		if (!ca->buckets[1]) {
			percpu_ref_put(&ca->ref);
			bch_err(c, "error allocating ca->buckets[gc]");
			return -ENOMEM;
		}

		ca->usage[1] = alloc_percpu(struct bch_dev_usage);
		if (!ca->usage[1]) {
			bch_err(c, "error allocating ca->usage[gc]");
			percpu_ref_put(&ca->ref);
			return -ENOMEM;
		}
	}

	ret = bch2_ec_mem_alloc(c, true);
	if (ret) {
		bch_err(c, "error allocating ec gc mem");
		return ret;
	}

	percpu_down_write(&c->mark_lock);

	/*
	 * indicate to stripe code that we need to allocate for the gc stripes
	 * radix tree, too
	 */
	gc_pos_set(c, gc_phase(GC_PHASE_START));

	for_each_member_device(ca, c, i) {
		struct bucket_array *dst = __bucket_array(ca, 1);
		struct bucket_array *src = __bucket_array(ca, 0);
		size_t b;

		dst->first_bucket	= src->first_bucket;
		dst->nbuckets		= src->nbuckets;

		for (b = 0; b < src->nbuckets; b++) {
			struct bucket *d = &dst->b[b];
			struct bucket *s = &src->b[b];

			d->_mark.gen = dst->b[b].oldest_gen = s->mark.gen;
			d->gen_valid = s->gen_valid;

			if (metadata_only &&
			    (s->mark.data_type == BCH_DATA_USER ||
			     s->mark.data_type == BCH_DATA_CACHED)) {
				d->_mark = s->mark;
				d->_mark.owned_by_allocator = 0;
			}
		}
	};

	percpu_up_write(&c->mark_lock);

	return 0;
}

/**
 * bch2_gc - walk _all_ references to buckets, and recompute them:
 *
 * Order matters here:
 *  - Concurrent GC relies on the fact that we have a total ordering for
 *    everything that GC walks - see  gc_will_visit_node(),
 *    gc_will_visit_root()
 *
 *  - also, references move around in the course of index updates and
 *    various other crap: everything needs to agree on the ordering
 *    references are allowed to move around in - e.g., we're allowed to
 *    start with a reference owned by an open_bucket (the allocator) and
 *    move it to the btree, but not the reverse.
 *
 *    This is necessary to ensure that gc doesn't miss references that
 *    move around - if references move backwards in the ordering GC
 *    uses, GC could skip past them
 */
int bch2_gc(struct bch_fs *c, struct journal_keys *journal_keys,
	    bool initial, bool metadata_only)
{
	struct bch_dev *ca;
	u64 start_time = local_clock();
	unsigned i, iter = 0;
	int ret;

	trace_gc_start(c);

	down_write(&c->gc_lock);
again:
	ret = bch2_gc_start(c, metadata_only);
	if (ret)
		goto out;

	bch2_mark_superblocks(c);

	ret = bch2_gc_btrees(c, journal_keys, initial, metadata_only);
	if (ret)
		goto out;

	bch2_mark_pending_btree_node_frees(c);
	bch2_mark_allocator_buckets(c);

	c->gc_count++;
out:
	if (!ret &&
	    (test_bit(BCH_FS_FIXED_GENS, &c->flags) ||
	     (!iter && test_restart_gc(c)))) {
		/*
		 * XXX: make sure gens we fixed got saved
		 */
		if (iter++ <= 2) {
			bch_info(c, "Fixed gens, restarting mark and sweep:");
			clear_bit(BCH_FS_FIXED_GENS, &c->flags);
			__gc_pos_set(c, gc_phase(GC_PHASE_NOT_RUNNING));

			percpu_down_write(&c->mark_lock);
			bch2_gc_free(c);
			percpu_up_write(&c->mark_lock);
			/* flush fsck errors, reset counters */
			bch2_flush_fsck_errs(c);

			goto again;
		}

		bch_info(c, "Unable to fix bucket gens, looping");
		ret = -EINVAL;
	}

	if (!ret) {
		bch2_journal_block(&c->journal);

		percpu_down_write(&c->mark_lock);
		ret = bch2_gc_done(c, initial, metadata_only);

		bch2_journal_unblock(&c->journal);
	} else {
		percpu_down_write(&c->mark_lock);
	}

	/* Indicates that gc is no longer in progress: */
	__gc_pos_set(c, gc_phase(GC_PHASE_NOT_RUNNING));

	bch2_gc_free(c);
	percpu_up_write(&c->mark_lock);

	up_write(&c->gc_lock);

	trace_gc_end(c);
	bch2_time_stats_update(&c->times[BCH_TIME_btree_gc], start_time);

	/*
	 * Wake up allocator in case it was waiting for buckets
	 * because of not being able to inc gens
	 */
	for_each_member_device(ca, c, i)
		bch2_wake_allocator(ca);

	/*
	 * At startup, allocations can happen directly instead of via the
	 * allocator thread - issue wakeup in case they blocked on gc_lock:
	 */
	closure_wake_up(&c->freelist_wait);
	return ret;
}

/* Btree coalescing */

static void recalc_packed_keys(struct btree *b)
{
	struct bset *i = btree_bset_first(b);
	struct bkey_packed *k;

	memset(&b->nr, 0, sizeof(b->nr));

	BUG_ON(b->nsets != 1);

	vstruct_for_each(i, k)
		btree_keys_account_key_add(&b->nr, 0, k);
}

static void bch2_coalesce_nodes(struct bch_fs *c, struct btree_iter *iter,
				struct btree *old_nodes[GC_MERGE_NODES])
{
	struct btree *parent = btree_node_parent(iter, old_nodes[0]);
	unsigned i, nr_old_nodes, nr_new_nodes, u64s = 0;
	unsigned blocks = btree_blocks(c) * 2 / 3;
	struct btree *new_nodes[GC_MERGE_NODES];
	struct btree_update *as;
	struct keylist keylist;
	struct bkey_format_state format_state;
	struct bkey_format new_format;

	memset(new_nodes, 0, sizeof(new_nodes));
	bch2_keylist_init(&keylist, NULL);

	/* Count keys that are not deleted */
	for (i = 0; i < GC_MERGE_NODES && old_nodes[i]; i++)
		u64s += old_nodes[i]->nr.live_u64s;

	nr_old_nodes = nr_new_nodes = i;

	/* Check if all keys in @old_nodes could fit in one fewer node */
	if (nr_old_nodes <= 1 ||
	    __vstruct_blocks(struct btree_node, c->block_bits,
			     DIV_ROUND_UP(u64s, nr_old_nodes - 1)) > blocks)
		return;

	/* Find a format that all keys in @old_nodes can pack into */
	bch2_bkey_format_init(&format_state);

	for (i = 0; i < nr_old_nodes; i++)
		__bch2_btree_calc_format(&format_state, old_nodes[i]);

	new_format = bch2_bkey_format_done(&format_state);

	/* Check if repacking would make any nodes too big to fit */
	for (i = 0; i < nr_old_nodes; i++)
		if (!bch2_btree_node_format_fits(c, old_nodes[i], &new_format)) {
			trace_btree_gc_coalesce_fail(c,
					BTREE_GC_COALESCE_FAIL_FORMAT_FITS);
			return;
		}

	if (bch2_keylist_realloc(&keylist, NULL, 0,
			(BKEY_U64s + BKEY_EXTENT_U64s_MAX) * nr_old_nodes)) {
		trace_btree_gc_coalesce_fail(c,
				BTREE_GC_COALESCE_FAIL_KEYLIST_REALLOC);
		return;
	}

	as = bch2_btree_update_start(iter->trans, iter->btree_id,
			btree_update_reserve_required(c, parent) + nr_old_nodes,
			BTREE_INSERT_NOFAIL|
			BTREE_INSERT_USE_RESERVE,
			NULL);
	if (IS_ERR(as)) {
		trace_btree_gc_coalesce_fail(c,
				BTREE_GC_COALESCE_FAIL_RESERVE_GET);
		bch2_keylist_free(&keylist, NULL);
		return;
	}

	trace_btree_gc_coalesce(c, old_nodes[0]);

	for (i = 0; i < nr_old_nodes; i++)
		bch2_btree_interior_update_will_free_node(as, old_nodes[i]);

	/* Repack everything with @new_format and sort down to one bset */
	for (i = 0; i < nr_old_nodes; i++)
		new_nodes[i] =
			__bch2_btree_node_alloc_replacement(as, old_nodes[i],
							    new_format);

	/*
	 * Conceptually we concatenate the nodes together and slice them
	 * up at different boundaries.
	 */
	for (i = nr_new_nodes - 1; i > 0; --i) {
		struct btree *n1 = new_nodes[i];
		struct btree *n2 = new_nodes[i - 1];

		struct bset *s1 = btree_bset_first(n1);
		struct bset *s2 = btree_bset_first(n2);
		struct bkey_packed *k, *last = NULL;

		/* Calculate how many keys from @n2 we could fit inside @n1 */
		u64s = 0;

		for (k = s2->start;
		     k < vstruct_last(s2) &&
		     vstruct_blocks_plus(n1->data, c->block_bits,
					 u64s + k->u64s) <= blocks;
		     k = bkey_next_skip_noops(k, vstruct_last(s2))) {
			last = k;
			u64s += k->u64s;
		}

		if (u64s == le16_to_cpu(s2->u64s)) {
			/* n2 fits entirely in n1 */
			n1->key.k.p = n1->data->max_key = n2->data->max_key;

			memcpy_u64s(vstruct_last(s1),
				    s2->start,
				    le16_to_cpu(s2->u64s));
			le16_add_cpu(&s1->u64s, le16_to_cpu(s2->u64s));

			set_btree_bset_end(n1, n1->set);

			six_unlock_write(&n2->c.lock);
			bch2_btree_node_free_never_inserted(c, n2);
			six_unlock_intent(&n2->c.lock);

			memmove(new_nodes + i - 1,
				new_nodes + i,
				sizeof(new_nodes[0]) * (nr_new_nodes - i));
			new_nodes[--nr_new_nodes] = NULL;
		} else if (u64s) {
			/* move part of n2 into n1 */
			n1->key.k.p = n1->data->max_key =
				bkey_unpack_pos(n1, last);

			n2->data->min_key = bkey_successor(n1->data->max_key);

			memcpy_u64s(vstruct_last(s1),
				    s2->start, u64s);
			le16_add_cpu(&s1->u64s, u64s);

			memmove(s2->start,
				vstruct_idx(s2, u64s),
				(le16_to_cpu(s2->u64s) - u64s) * sizeof(u64));
			s2->u64s = cpu_to_le16(le16_to_cpu(s2->u64s) - u64s);

			set_btree_bset_end(n1, n1->set);
			set_btree_bset_end(n2, n2->set);
		}
	}

	for (i = 0; i < nr_new_nodes; i++) {
		struct btree *n = new_nodes[i];

		recalc_packed_keys(n);
		btree_node_reset_sib_u64s(n);

		bch2_btree_build_aux_trees(n);
		six_unlock_write(&n->c.lock);

		bch2_btree_node_write(c, n, SIX_LOCK_intent);
	}

	/*
	 * The keys for the old nodes get deleted. We don't want to insert keys
	 * that compare equal to the keys for the new nodes we'll also be
	 * inserting - we can't because keys on a keylist must be strictly
	 * greater than the previous keys, and we also don't need to since the
	 * key for the new node will serve the same purpose (overwriting the key
	 * for the old node).
	 */
	for (i = 0; i < nr_old_nodes; i++) {
		struct bkey_i delete;
		unsigned j;

		for (j = 0; j < nr_new_nodes; j++)
			if (!bkey_cmp(old_nodes[i]->key.k.p,
				      new_nodes[j]->key.k.p))
				goto next;

		bkey_init(&delete.k);
		delete.k.p = old_nodes[i]->key.k.p;
		bch2_keylist_add_in_order(&keylist, &delete);
next:
		i = i;
	}

	/*
	 * Keys for the new nodes get inserted: bch2_btree_insert_keys() only
	 * does the lookup once and thus expects the keys to be in sorted order
	 * so we have to make sure the new keys are correctly ordered with
	 * respect to the deleted keys added in the previous loop
	 */
	for (i = 0; i < nr_new_nodes; i++)
		bch2_keylist_add_in_order(&keylist, &new_nodes[i]->key);

	/* Insert the newly coalesced nodes */
	bch2_btree_insert_node(as, parent, iter, &keylist, 0);

	BUG_ON(!bch2_keylist_empty(&keylist));

	BUG_ON(iter->l[old_nodes[0]->c.level].b != old_nodes[0]);

	bch2_btree_iter_node_replace(iter, new_nodes[0]);

	for (i = 0; i < nr_new_nodes; i++)
		bch2_open_buckets_put(c, &new_nodes[i]->ob);

	/* Free the old nodes and update our sliding window */
	for (i = 0; i < nr_old_nodes; i++) {
		bch2_btree_node_free_inmem(c, old_nodes[i], iter);

		/*
		 * the index update might have triggered a split, in which case
		 * the nodes we coalesced - the new nodes we just created -
		 * might not be sibling nodes anymore - don't add them to the
		 * sliding window (except the first):
		 */
		if (!i) {
			old_nodes[i] = new_nodes[i];
		} else {
			old_nodes[i] = NULL;
		}
	}

	for (i = 0; i < nr_new_nodes; i++)
		six_unlock_intent(&new_nodes[i]->c.lock);

	bch2_btree_update_done(as);
	bch2_keylist_free(&keylist, NULL);
}

static int bch2_coalesce_btree(struct bch_fs *c, enum btree_id btree_id)
{
	struct btree_trans trans;
	struct btree_iter *iter;
	struct btree *b;
	bool kthread = (current->flags & PF_KTHREAD) != 0;
	unsigned i;

	/* Sliding window of adjacent btree nodes */
	struct btree *merge[GC_MERGE_NODES];
	u32 lock_seq[GC_MERGE_NODES];

	bch2_trans_init(&trans, c, 0, 0);

	/*
	 * XXX: We don't have a good way of positively matching on sibling nodes
	 * that have the same parent - this code works by handling the cases
	 * where they might not have the same parent, and is thus fragile. Ugh.
	 *
	 * Perhaps redo this to use multiple linked iterators?
	 */
	memset(merge, 0, sizeof(merge));

	__for_each_btree_node(&trans, iter, btree_id, POS_MIN,
			      BTREE_MAX_DEPTH, 0,
			      BTREE_ITER_PREFETCH, b) {
		memmove(merge + 1, merge,
			sizeof(merge) - sizeof(merge[0]));
		memmove(lock_seq + 1, lock_seq,
			sizeof(lock_seq) - sizeof(lock_seq[0]));

		merge[0] = b;

		for (i = 1; i < GC_MERGE_NODES; i++) {
			if (!merge[i] ||
			    !six_relock_intent(&merge[i]->c.lock, lock_seq[i]))
				break;

			if (merge[i]->c.level != merge[0]->c.level) {
				six_unlock_intent(&merge[i]->c.lock);
				break;
			}
		}
		memset(merge + i, 0, (GC_MERGE_NODES - i) * sizeof(merge[0]));

		bch2_coalesce_nodes(c, iter, merge);

		for (i = 1; i < GC_MERGE_NODES && merge[i]; i++) {
			lock_seq[i] = merge[i]->c.lock.state.seq;
			six_unlock_intent(&merge[i]->c.lock);
		}

		lock_seq[0] = merge[0]->c.lock.state.seq;

		if (kthread && kthread_should_stop()) {
			bch2_trans_exit(&trans);
			return -ESHUTDOWN;
		}

		bch2_trans_cond_resched(&trans);

		/*
		 * If the parent node wasn't relocked, it might have been split
		 * and the nodes in our sliding window might not have the same
		 * parent anymore - blow away the sliding window:
		 */
		if (btree_iter_node(iter, iter->level + 1) &&
		    !btree_node_intent_locked(iter, iter->level + 1))
			memset(merge + 1, 0,
			       (GC_MERGE_NODES - 1) * sizeof(merge[0]));
	}
	return bch2_trans_exit(&trans);
}

/**
 * bch_coalesce - coalesce adjacent nodes with low occupancy
 */
void bch2_coalesce(struct bch_fs *c)
{
	enum btree_id id;

	down_read(&c->gc_lock);
	trace_gc_coalesce_start(c);

	for (id = 0; id < BTREE_ID_NR; id++) {
		int ret = c->btree_roots[id].b
			? bch2_coalesce_btree(c, id)
			: 0;

		if (ret) {
			if (ret != -ESHUTDOWN)
				bch_err(c, "btree coalescing failed: %d", ret);
			return;
		}
	}

	trace_gc_coalesce_end(c);
	up_read(&c->gc_lock);
}

static int bch2_gc_thread(void *arg)
{
	struct bch_fs *c = arg;
	struct io_clock *clock = &c->io_clock[WRITE];
	unsigned long last = atomic_long_read(&clock->now);
	unsigned last_kick = atomic_read(&c->kick_gc);
	int ret;

	set_freezable();

	while (1) {
		while (1) {
			set_current_state(TASK_INTERRUPTIBLE);

			if (kthread_should_stop()) {
				__set_current_state(TASK_RUNNING);
				return 0;
			}

			if (atomic_read(&c->kick_gc) != last_kick)
				break;

			if (c->btree_gc_periodic) {
				unsigned long next = last + c->capacity / 16;

				if (atomic_long_read(&clock->now) >= next)
					break;

				bch2_io_clock_schedule_timeout(clock, next);
			} else {
				schedule();
			}

			try_to_freeze();
		}
		__set_current_state(TASK_RUNNING);

		last = atomic_long_read(&clock->now);
		last_kick = atomic_read(&c->kick_gc);

		ret = bch2_gc(c, NULL, false, false);
		if (ret)
			bch_err(c, "btree gc failed: %i", ret);

		debug_check_no_locks_held();
	}

	return 0;
}

void bch2_gc_thread_stop(struct bch_fs *c)
{
	struct task_struct *p;

	p = c->gc_thread;
	c->gc_thread = NULL;

	if (p) {
		kthread_stop(p);
		put_task_struct(p);
	}
}

int bch2_gc_thread_start(struct bch_fs *c)
{
	struct task_struct *p;

	BUG_ON(c->gc_thread);

	p = kthread_create(bch2_gc_thread, c, "bch_gc");
	if (IS_ERR(p))
		return PTR_ERR(p);

	get_task_struct(p);
	c->gc_thread = p;
	wake_up_process(p);
	return 0;
}
