// SPDX-License-Identifier: GPL-2.0
/*
 * Code for manipulating bucket marks for garbage collection.
 *
 * Copyright 2014 Datera, Inc.
 */

#include "bcachefs.h"
#include "alloc_background.h"
#include "backpointers.h"
#include "bset.h"
#include "btree_gc.h"
#include "btree_update.h"
#include "buckets.h"
#include "buckets_waiting_for_journal.h"
#include "disk_accounting.h"
#include "ec.h"
#include "error.h"
#include "inode.h"
#include "movinggc.h"
#include "rebalance.h"
#include "recovery.h"
#include "recovery_passes.h"
#include "reflink.h"
#include "replicas.h"
#include "subvolume.h"
#include "trace.h"

#include <linux/preempt.h>

void bch2_dev_usage_read_fast(struct bch_dev *ca, struct bch_dev_usage *usage)
{
	memset(usage, 0, sizeof(*usage));
	acc_u64s_percpu((u64 *) usage, (u64 __percpu *) ca->usage, dev_usage_u64s());
}

static u64 reserve_factor(u64 r)
{
	return r + (round_up(r, (1 << RESERVE_FACTOR)) >> RESERVE_FACTOR);
}

static struct bch_fs_usage_short
__bch2_fs_usage_read_short(struct bch_fs *c)
{
	struct bch_fs_usage_short ret;
	u64 data, reserved;

	ret.capacity = c->capacity -
		percpu_u64_get(&c->usage->hidden);

	data		= percpu_u64_get(&c->usage->data) +
		percpu_u64_get(&c->usage->btree);
	reserved	= percpu_u64_get(&c->usage->reserved) +
		percpu_u64_get(c->online_reserved);

	ret.used	= min(ret.capacity, data + reserve_factor(reserved));
	ret.free	= ret.capacity - ret.used;

	ret.nr_inodes	= percpu_u64_get(&c->usage->nr_inodes);

	return ret;
}

struct bch_fs_usage_short
bch2_fs_usage_read_short(struct bch_fs *c)
{
	struct bch_fs_usage_short ret;

	percpu_down_read(&c->mark_lock);
	ret = __bch2_fs_usage_read_short(c);
	percpu_up_read(&c->mark_lock);

	return ret;
}

void bch2_dev_usage_to_text(struct printbuf *out,
			    struct bch_dev *ca,
			    struct bch_dev_usage *usage)
{
	if (out->nr_tabstops < 5) {
		printbuf_tabstops_reset(out);
		printbuf_tabstop_push(out, 12);
		printbuf_tabstop_push(out, 16);
		printbuf_tabstop_push(out, 16);
		printbuf_tabstop_push(out, 16);
		printbuf_tabstop_push(out, 16);
	}

	prt_printf(out, "\tbuckets\rsectors\rfragmented\r\n");

	for (unsigned i = 0; i < BCH_DATA_NR; i++) {
		bch2_prt_data_type(out, i);
		prt_printf(out, "\t%llu\r%llu\r%llu\r\n",
			   usage->d[i].buckets,
			   usage->d[i].sectors,
			   usage->d[i].fragmented);
	}

	prt_printf(out, "capacity\t%llu\r\n", ca->mi.nbuckets);
}

static int bch2_check_fix_ptr(struct btree_trans *trans,
			      struct bkey_s_c k,
			      struct extent_ptr_decoded p,
			      const union bch_extent_entry *entry,
			      bool *do_update)
{
	struct bch_fs *c = trans->c;
	struct printbuf buf = PRINTBUF;
	int ret = 0;

	struct bch_dev *ca = bch2_dev_tryget(c, p.ptr.dev);
	if (!ca) {
		if (fsck_err_on(p.ptr.dev != BCH_SB_MEMBER_INVALID,
				trans, ptr_to_invalid_device,
				"pointer to missing device %u\n"
				"while marking %s",
				p.ptr.dev,
				(printbuf_reset(&buf),
				 bch2_bkey_val_to_text(&buf, c, k), buf.buf)))
			*do_update = true;
		return 0;
	}

	struct bucket *g = PTR_GC_BUCKET(ca, &p.ptr);
	if (!g) {
		if (fsck_err(trans, ptr_to_invalid_device,
			     "pointer to invalid bucket on device %u\n"
			     "while marking %s",
			     p.ptr.dev,
			     (printbuf_reset(&buf),
			      bch2_bkey_val_to_text(&buf, c, k), buf.buf)))
			*do_update = true;
		goto out;
	}

	enum bch_data_type data_type = bch2_bkey_ptr_data_type(k, p, entry);

	if (fsck_err_on(!g->gen_valid,
			trans, ptr_to_missing_alloc_key,
			"bucket %u:%zu data type %s ptr gen %u missing in alloc btree\n"
			"while marking %s",
			p.ptr.dev, PTR_BUCKET_NR(ca, &p.ptr),
			bch2_data_type_str(ptr_data_type(k.k, &p.ptr)),
			p.ptr.gen,
			(printbuf_reset(&buf),
			 bch2_bkey_val_to_text(&buf, c, k), buf.buf))) {
		if (!p.ptr.cached) {
			g->gen_valid		= true;
			g->gen			= p.ptr.gen;
		} else {
			*do_update = true;
		}
	}

	if (fsck_err_on(gen_cmp(p.ptr.gen, g->gen) > 0,
			trans, ptr_gen_newer_than_bucket_gen,
			"bucket %u:%zu data type %s ptr gen in the future: %u > %u\n"
			"while marking %s",
			p.ptr.dev, PTR_BUCKET_NR(ca, &p.ptr),
			bch2_data_type_str(ptr_data_type(k.k, &p.ptr)),
			p.ptr.gen, g->gen,
			(printbuf_reset(&buf),
			 bch2_bkey_val_to_text(&buf, c, k), buf.buf))) {
		if (!p.ptr.cached &&
		    (g->data_type != BCH_DATA_btree ||
		     data_type == BCH_DATA_btree)) {
			g->gen_valid		= true;
			g->gen			= p.ptr.gen;
			g->data_type		= 0;
			g->stripe_sectors	= 0;
			g->dirty_sectors	= 0;
			g->cached_sectors	= 0;
		} else {
			*do_update = true;
		}
	}

	if (fsck_err_on(gen_cmp(g->gen, p.ptr.gen) > BUCKET_GC_GEN_MAX,
			trans, ptr_gen_newer_than_bucket_gen,
			"bucket %u:%zu gen %u data type %s: ptr gen %u too stale\n"
			"while marking %s",
			p.ptr.dev, PTR_BUCKET_NR(ca, &p.ptr), g->gen,
			bch2_data_type_str(ptr_data_type(k.k, &p.ptr)),
			p.ptr.gen,
			(printbuf_reset(&buf),
			 bch2_bkey_val_to_text(&buf, c, k), buf.buf)))
		*do_update = true;

	if (fsck_err_on(!p.ptr.cached && gen_cmp(p.ptr.gen, g->gen) < 0,
			trans, stale_dirty_ptr,
			"bucket %u:%zu data type %s stale dirty ptr: %u < %u\n"
			"while marking %s",
			p.ptr.dev, PTR_BUCKET_NR(ca, &p.ptr),
			bch2_data_type_str(ptr_data_type(k.k, &p.ptr)),
			p.ptr.gen, g->gen,
			(printbuf_reset(&buf),
			 bch2_bkey_val_to_text(&buf, c, k), buf.buf)))
		*do_update = true;

	if (data_type != BCH_DATA_btree && p.ptr.gen != g->gen)
		goto out;

	if (fsck_err_on(bucket_data_type_mismatch(g->data_type, data_type),
			trans, ptr_bucket_data_type_mismatch,
			"bucket %u:%zu gen %u different types of data in same bucket: %s, %s\n"
			"while marking %s",
			p.ptr.dev, PTR_BUCKET_NR(ca, &p.ptr), g->gen,
			bch2_data_type_str(g->data_type),
			bch2_data_type_str(data_type),
			(printbuf_reset(&buf),
			 bch2_bkey_val_to_text(&buf, c, k), buf.buf))) {
		if (data_type == BCH_DATA_btree) {
			g->gen_valid		= true;
			g->gen			= p.ptr.gen;
			g->data_type		= data_type;
			g->stripe_sectors	= 0;
			g->dirty_sectors	= 0;
			g->cached_sectors	= 0;
		} else {
			*do_update = true;
		}
	}

	if (p.has_ec) {
		struct gc_stripe *m = genradix_ptr(&c->gc_stripes, p.ec.idx);

		if (fsck_err_on(!m || !m->alive,
				trans, ptr_to_missing_stripe,
				"pointer to nonexistent stripe %llu\n"
				"while marking %s",
				(u64) p.ec.idx,
				(printbuf_reset(&buf),
				 bch2_bkey_val_to_text(&buf, c, k), buf.buf)))
			*do_update = true;

		if (fsck_err_on(m && m->alive && !bch2_ptr_matches_stripe_m(m, p),
				trans, ptr_to_incorrect_stripe,
				"pointer does not match stripe %llu\n"
				"while marking %s",
				(u64) p.ec.idx,
				(printbuf_reset(&buf),
				 bch2_bkey_val_to_text(&buf, c, k), buf.buf)))
			*do_update = true;
	}
out:
fsck_err:
	bch2_dev_put(ca);
	printbuf_exit(&buf);
	return ret;
}

int bch2_check_fix_ptrs(struct btree_trans *trans,
			enum btree_id btree, unsigned level, struct bkey_s_c k,
			enum btree_iter_update_trigger_flags flags)
{
	struct bch_fs *c = trans->c;
	struct bkey_ptrs_c ptrs_c = bch2_bkey_ptrs_c(k);
	const union bch_extent_entry *entry_c;
	struct extent_ptr_decoded p = { 0 };
	bool do_update = false;
	struct printbuf buf = PRINTBUF;
	int ret = 0;

	bkey_for_each_ptr_decode(k.k, ptrs_c, p, entry_c) {
		ret = bch2_check_fix_ptr(trans, k, p, entry_c, &do_update);
		if (ret)
			goto err;
	}

	if (do_update) {
		if (flags & BTREE_TRIGGER_is_root) {
			bch_err(c, "cannot update btree roots yet");
			ret = -EINVAL;
			goto err;
		}

		struct bkey_i *new = bch2_bkey_make_mut_noupdate(trans, k);
		ret = PTR_ERR_OR_ZERO(new);
		if (ret)
			goto err;

		rcu_read_lock();
		bch2_bkey_drop_ptrs(bkey_i_to_s(new), ptr, !bch2_dev_exists(c, ptr->dev));
		rcu_read_unlock();

		if (level) {
			/*
			 * We don't want to drop btree node pointers - if the
			 * btree node isn't there anymore, the read path will
			 * sort it out:
			 */
			struct bkey_ptrs ptrs = bch2_bkey_ptrs(bkey_i_to_s(new));
			rcu_read_lock();
			bkey_for_each_ptr(ptrs, ptr) {
				struct bch_dev *ca = bch2_dev_rcu(c, ptr->dev);
				struct bucket *g = PTR_GC_BUCKET(ca, ptr);

				ptr->gen = g->gen;
			}
			rcu_read_unlock();
		} else {
			struct bkey_ptrs ptrs;
			union bch_extent_entry *entry;

			rcu_read_lock();
restart_drop_ptrs:
			ptrs = bch2_bkey_ptrs(bkey_i_to_s(new));
			bkey_for_each_ptr_decode(bkey_i_to_s(new).k, ptrs, p, entry) {
				struct bch_dev *ca = bch2_dev_rcu(c, p.ptr.dev);
				struct bucket *g = PTR_GC_BUCKET(ca, &p.ptr);
				enum bch_data_type data_type = bch2_bkey_ptr_data_type(bkey_i_to_s_c(new), p, entry);

				if ((p.ptr.cached &&
				     (!g->gen_valid || gen_cmp(p.ptr.gen, g->gen) > 0)) ||
				    (!p.ptr.cached &&
				     gen_cmp(p.ptr.gen, g->gen) < 0) ||
				    gen_cmp(g->gen, p.ptr.gen) > BUCKET_GC_GEN_MAX ||
				    (g->data_type &&
				     g->data_type != data_type)) {
					bch2_bkey_drop_ptr(bkey_i_to_s(new), &entry->ptr);
					goto restart_drop_ptrs;
				}
			}
			rcu_read_unlock();
again:
			ptrs = bch2_bkey_ptrs(bkey_i_to_s(new));
			bkey_extent_entry_for_each(ptrs, entry) {
				if (extent_entry_type(entry) == BCH_EXTENT_ENTRY_stripe_ptr) {
					struct gc_stripe *m = genradix_ptr(&c->gc_stripes,
									entry->stripe_ptr.idx);
					union bch_extent_entry *next_ptr;

					bkey_extent_entry_for_each_from(ptrs, next_ptr, entry)
						if (extent_entry_type(next_ptr) == BCH_EXTENT_ENTRY_ptr)
							goto found;
					next_ptr = NULL;
found:
					if (!next_ptr) {
						bch_err(c, "aieee, found stripe ptr with no data ptr");
						continue;
					}

					if (!m || !m->alive ||
					    !__bch2_ptr_matches_stripe(&m->ptrs[entry->stripe_ptr.block],
								       &next_ptr->ptr,
								       m->sectors)) {
						bch2_bkey_extent_entry_drop(new, entry);
						goto again;
					}
				}
			}
		}

		if (0) {
			printbuf_reset(&buf);
			bch2_bkey_val_to_text(&buf, c, k);
			bch_info(c, "updated %s", buf.buf);

			printbuf_reset(&buf);
			bch2_bkey_val_to_text(&buf, c, bkey_i_to_s_c(new));
			bch_info(c, "new key %s", buf.buf);
		}

		struct btree_iter iter;
		bch2_trans_node_iter_init(trans, &iter, btree, new->k.p, 0, level,
					  BTREE_ITER_intent|BTREE_ITER_all_snapshots);
		ret =   bch2_btree_iter_traverse(&iter) ?:
			bch2_trans_update(trans, &iter, new,
					  BTREE_UPDATE_internal_snapshot_node|
					  BTREE_TRIGGER_norun);
		bch2_trans_iter_exit(trans, &iter);
		if (ret)
			goto err;

		if (level)
			bch2_btree_node_update_key_early(trans, btree, level - 1, k, new);
	}
err:
	printbuf_exit(&buf);
	return ret;
}

static int bucket_ref_update_err(struct btree_trans *trans, struct printbuf *buf,
				 struct bkey_s_c k, bool insert, enum bch_sb_error_id id)
{
	struct bch_fs *c = trans->c;
	bool repeat = false, print = true, suppress = false;

	prt_printf(buf, "\nwhile marking ");
	bch2_bkey_val_to_text(buf, c, k);
	prt_newline(buf);

	__bch2_count_fsck_err(c, id, buf->buf, &repeat, &print, &suppress);

	int ret = bch2_run_explicit_recovery_pass(c, BCH_RECOVERY_PASS_check_allocations);

	if (insert) {
		print = true;
		suppress = false;

		bch2_trans_updates_to_text(buf, trans);
		__bch2_inconsistent_error(c, buf);
		ret = -BCH_ERR_bucket_ref_update;
	}

	if (suppress)
		prt_printf(buf, "Ratelimiting new instances of previous error\n");
	if (print)
		bch2_print_string_as_lines(KERN_ERR, buf->buf);
	return ret;
}

int bch2_bucket_ref_update(struct btree_trans *trans, struct bch_dev *ca,
			   struct bkey_s_c k,
			   const struct bch_extent_ptr *ptr,
			   s64 sectors, enum bch_data_type ptr_data_type,
			   u8 b_gen, u8 bucket_data_type,
			   u32 *bucket_sectors)
{
	struct bch_fs *c = trans->c;
	size_t bucket_nr = PTR_BUCKET_NR(ca, ptr);
	struct printbuf buf = PRINTBUF;
	bool inserting = sectors > 0;
	int ret = 0;

	BUG_ON(!sectors);

	if (unlikely(gen_after(ptr->gen, b_gen))) {
		bch2_log_msg_start(c, &buf);
		prt_printf(&buf,
			"bucket %u:%zu gen %u data type %s: ptr gen %u newer than bucket gen",
			ptr->dev, bucket_nr, b_gen,
			bch2_data_type_str(bucket_data_type ?: ptr_data_type),
			ptr->gen);

		ret = bucket_ref_update_err(trans, &buf, k, inserting,
					    BCH_FSCK_ERR_ptr_gen_newer_than_bucket_gen);
		goto out;
	}

	if (unlikely(gen_cmp(b_gen, ptr->gen) > BUCKET_GC_GEN_MAX)) {
		bch2_log_msg_start(c, &buf);
		prt_printf(&buf,
			"bucket %u:%zu gen %u data type %s: ptr gen %u too stale",
			ptr->dev, bucket_nr, b_gen,
			bch2_data_type_str(bucket_data_type ?: ptr_data_type),
			ptr->gen);

		ret = bucket_ref_update_err(trans, &buf, k, inserting,
					    BCH_FSCK_ERR_ptr_too_stale);
		goto out;
	}

	if (b_gen != ptr->gen && ptr->cached) {
		ret = 1;
		goto out;
	}

	if (unlikely(b_gen != ptr->gen)) {
		bch2_log_msg_start(c, &buf);
		prt_printf(&buf,
			"bucket %u:%zu gen %u (mem gen %u) data type %s: stale dirty ptr (gen %u)",
			ptr->dev, bucket_nr, b_gen,
			bucket_gen_get(ca, bucket_nr),
			bch2_data_type_str(bucket_data_type ?: ptr_data_type),
			ptr->gen);

		ret = bucket_ref_update_err(trans, &buf, k, inserting,
					    BCH_FSCK_ERR_stale_dirty_ptr);
		goto out;
	}

	if (unlikely(bucket_data_type_mismatch(bucket_data_type, ptr_data_type))) {
		bch2_log_msg_start(c, &buf);
		prt_printf(&buf, "bucket %u:%zu gen %u different types of data in same bucket: %s, %s",
			   ptr->dev, bucket_nr, b_gen,
			   bch2_data_type_str(bucket_data_type),
			   bch2_data_type_str(ptr_data_type));

		ret = bucket_ref_update_err(trans, &buf, k, inserting,
					    BCH_FSCK_ERR_ptr_bucket_data_type_mismatch);
		goto out;
	}

	if (unlikely((u64) *bucket_sectors + sectors > U32_MAX)) {
		bch2_log_msg_start(c, &buf);
		prt_printf(&buf,
			"bucket %u:%zu gen %u data type %s sector count overflow: %u + %lli > U32_MAX",
			ptr->dev, bucket_nr, b_gen,
			bch2_data_type_str(bucket_data_type ?: ptr_data_type),
			*bucket_sectors, sectors);

		ret = bucket_ref_update_err(trans, &buf, k, inserting,
					    BCH_FSCK_ERR_bucket_sector_count_overflow);
		sectors = -*bucket_sectors;
		goto out;
	}

	*bucket_sectors += sectors;
out:
	printbuf_exit(&buf);
	return ret;
}

void bch2_trans_account_disk_usage_change(struct btree_trans *trans)
{
	struct bch_fs *c = trans->c;
	u64 disk_res_sectors = trans->disk_res ? trans->disk_res->sectors : 0;
	static int warned_disk_usage = 0;
	bool warn = false;

	percpu_down_read(&c->mark_lock);
	struct bch_fs_usage_base *src = &trans->fs_usage_delta;

	s64 added = src->btree + src->data + src->reserved;

	/*
	 * Not allowed to reduce sectors_available except by getting a
	 * reservation:
	 */
	s64 should_not_have_added = added - (s64) disk_res_sectors;
	if (unlikely(should_not_have_added > 0)) {
		u64 old, new;

		old = atomic64_read(&c->sectors_available);
		do {
			new = max_t(s64, 0, old - should_not_have_added);
		} while (!atomic64_try_cmpxchg(&c->sectors_available,
					       &old, new));

		added -= should_not_have_added;
		warn = true;
	}

	if (added > 0) {
		trans->disk_res->sectors -= added;
		this_cpu_sub(*c->online_reserved, added);
	}

	preempt_disable();
	struct bch_fs_usage_base *dst = this_cpu_ptr(c->usage);
	acc_u64s((u64 *) dst, (u64 *) src, sizeof(*src) / sizeof(u64));
	preempt_enable();
	percpu_up_read(&c->mark_lock);

	if (unlikely(warn) && !xchg(&warned_disk_usage, 1))
		bch2_trans_inconsistent(trans,
					"disk usage increased %lli more than %llu sectors reserved)",
					should_not_have_added, disk_res_sectors);
}

/* KEY_TYPE_extent: */

static int __mark_pointer(struct btree_trans *trans, struct bch_dev *ca,
			  struct bkey_s_c k,
			  const struct extent_ptr_decoded *p,
			  s64 sectors, enum bch_data_type ptr_data_type,
			  struct bch_alloc_v4 *a,
			  bool insert)
{
	u32 *dst_sectors = p->has_ec	? &a->stripe_sectors :
		!p->ptr.cached		? &a->dirty_sectors :
					  &a->cached_sectors;
	int ret = bch2_bucket_ref_update(trans, ca, k, &p->ptr, sectors, ptr_data_type,
					 a->gen, a->data_type, dst_sectors);

	if (ret)
		return ret;
	if (insert)
		alloc_data_type_set(a, ptr_data_type);
	return 0;
}

static int bch2_trigger_pointer(struct btree_trans *trans,
			enum btree_id btree_id, unsigned level,
			struct bkey_s_c k, struct extent_ptr_decoded p,
			const union bch_extent_entry *entry,
			s64 *sectors,
			enum btree_iter_update_trigger_flags flags)
{
	struct bch_fs *c = trans->c;
	bool insert = !(flags & BTREE_TRIGGER_overwrite);
	struct printbuf buf = PRINTBUF;
	int ret = 0;

	struct bkey_i_backpointer bp;
	bch2_extent_ptr_to_bp(c, btree_id, level, k, p, entry, &bp);

	*sectors = insert ? bp.v.bucket_len : -(s64) bp.v.bucket_len;

	struct bch_dev *ca = bch2_dev_tryget(c, p.ptr.dev);
	if (unlikely(!ca)) {
		if (insert && p.ptr.dev != BCH_SB_MEMBER_INVALID)
			ret = -BCH_ERR_trigger_pointer;
		goto err;
	}

	struct bpos bucket = PTR_BUCKET_POS(ca, &p.ptr);

	if (flags & BTREE_TRIGGER_transactional) {
		struct bkey_i_alloc_v4 *a = bch2_trans_start_alloc_update(trans, bucket, 0);
		ret = PTR_ERR_OR_ZERO(a) ?:
			__mark_pointer(trans, ca, k, &p, *sectors, bp.v.data_type, &a->v, insert);
		if (ret)
			goto err;

		ret = bch2_bucket_backpointer_mod(trans, k, &bp, insert);
		if (ret)
			goto err;
	}

	if (flags & BTREE_TRIGGER_gc) {
		struct bucket *g = gc_bucket(ca, bucket.offset);
		if (bch2_fs_inconsistent_on(!g, c, "reference to invalid bucket on device %u\n  %s",
					    p.ptr.dev,
					    (bch2_bkey_val_to_text(&buf, c, k), buf.buf))) {
			ret = -BCH_ERR_trigger_pointer;
			goto err;
		}

		bucket_lock(g);
		struct bch_alloc_v4 old = bucket_m_to_alloc(*g), new = old;
		ret = __mark_pointer(trans, ca, k, &p, *sectors, bp.v.data_type, &new, insert);
		alloc_to_bucket(g, new);
		bucket_unlock(g);

		if (!ret)
			ret = bch2_alloc_key_to_dev_counters(trans, ca, &old, &new, flags);
	}
err:
	bch2_dev_put(ca);
	printbuf_exit(&buf);
	return ret;
}

static int bch2_trigger_stripe_ptr(struct btree_trans *trans,
				struct bkey_s_c k,
				struct extent_ptr_decoded p,
				enum bch_data_type data_type,
				s64 sectors,
				enum btree_iter_update_trigger_flags flags)
{
	if (flags & BTREE_TRIGGER_transactional) {
		struct btree_iter iter;
		struct bkey_i_stripe *s = bch2_bkey_get_mut_typed(trans, &iter,
				BTREE_ID_stripes, POS(0, p.ec.idx),
				BTREE_ITER_with_updates, stripe);
		int ret = PTR_ERR_OR_ZERO(s);
		if (unlikely(ret)) {
			bch2_trans_inconsistent_on(bch2_err_matches(ret, ENOENT), trans,
				"pointer to nonexistent stripe %llu",
				(u64) p.ec.idx);
			goto err;
		}

		if (!bch2_ptr_matches_stripe(&s->v, p)) {
			bch2_trans_inconsistent(trans,
				"stripe pointer doesn't match stripe %llu",
				(u64) p.ec.idx);
			ret = -BCH_ERR_trigger_stripe_pointer;
			goto err;
		}

		stripe_blockcount_set(&s->v, p.ec.block,
			stripe_blockcount_get(&s->v, p.ec.block) +
			sectors);

		struct disk_accounting_pos acc;
		memset(&acc, 0, sizeof(acc));
		acc.type = BCH_DISK_ACCOUNTING_replicas;
		bch2_bkey_to_replicas(&acc.replicas, bkey_i_to_s_c(&s->k_i));
		acc.replicas.data_type = data_type;
		ret = bch2_disk_accounting_mod(trans, &acc, &sectors, 1, false);
err:
		bch2_trans_iter_exit(trans, &iter);
		return ret;
	}

	if (flags & BTREE_TRIGGER_gc) {
		struct bch_fs *c = trans->c;

		struct gc_stripe *m = genradix_ptr_alloc(&c->gc_stripes, p.ec.idx, GFP_KERNEL);
		if (!m) {
			bch_err(c, "error allocating memory for gc_stripes, idx %llu",
				(u64) p.ec.idx);
			return -BCH_ERR_ENOMEM_mark_stripe_ptr;
		}

		gc_stripe_lock(m);

		if (!m || !m->alive) {
			gc_stripe_unlock(m);
			struct printbuf buf = PRINTBUF;
			bch2_log_msg_start(c, &buf);
			prt_printf(&buf, "pointer to nonexistent stripe %llu\n  while marking ",
				   (u64) p.ec.idx);
			bch2_bkey_val_to_text(&buf, c, k);
			__bch2_inconsistent_error(c, &buf);
			bch2_print_string_as_lines(KERN_ERR, buf.buf);
			printbuf_exit(&buf);
			return -BCH_ERR_trigger_stripe_pointer;
		}

		m->block_sectors[p.ec.block] += sectors;

		struct disk_accounting_pos acc;
		memset(&acc, 0, sizeof(acc));
		acc.type = BCH_DISK_ACCOUNTING_replicas;
		memcpy(&acc.replicas, &m->r.e, replicas_entry_bytes(&m->r.e));
		gc_stripe_unlock(m);

		acc.replicas.data_type = data_type;
		int ret = bch2_disk_accounting_mod(trans, &acc, &sectors, 1, true);
		if (ret)
			return ret;
	}

	return 0;
}

static int __trigger_extent(struct btree_trans *trans,
			    enum btree_id btree_id, unsigned level,
			    struct bkey_s_c k,
			    enum btree_iter_update_trigger_flags flags,
			    s64 *replicas_sectors)
{
	bool gc = flags & BTREE_TRIGGER_gc;
	struct bkey_ptrs_c ptrs = bch2_bkey_ptrs_c(k);
	const union bch_extent_entry *entry;
	struct extent_ptr_decoded p;
	enum bch_data_type data_type = bkey_is_btree_ptr(k.k)
		? BCH_DATA_btree
		: BCH_DATA_user;
	int ret = 0;

	struct disk_accounting_pos acc_replicas_key;
	memset(&acc_replicas_key, 0, sizeof(acc_replicas_key));
	acc_replicas_key.type = BCH_DISK_ACCOUNTING_replicas;
	acc_replicas_key.replicas.data_type	= data_type;
	acc_replicas_key.replicas.nr_devs	= 0;
	acc_replicas_key.replicas.nr_required	= 1;

	unsigned cur_compression_type = 0;
	u64 compression_acct[3] = { 1, 0, 0 };

	bkey_for_each_ptr_decode(k.k, ptrs, p, entry) {
		s64 disk_sectors = 0;
		ret = bch2_trigger_pointer(trans, btree_id, level, k, p, entry, &disk_sectors, flags);
		if (ret < 0)
			return ret;

		bool stale = ret > 0;

		if (p.ptr.cached && stale)
			continue;

		if (p.ptr.cached) {
			ret = bch2_mod_dev_cached_sectors(trans, p.ptr.dev, disk_sectors, gc);
			if (ret)
				return ret;
		} else if (!p.has_ec) {
			*replicas_sectors       += disk_sectors;
			replicas_entry_add_dev(&acc_replicas_key.replicas, p.ptr.dev);
		} else {
			ret = bch2_trigger_stripe_ptr(trans, k, p, data_type, disk_sectors, flags);
			if (ret)
				return ret;

			/*
			 * There may be other dirty pointers in this extent, but
			 * if so they're not required for mounting if we have an
			 * erasure coded pointer in this extent:
			 */
			acc_replicas_key.replicas.nr_required = 0;
		}

		if (cur_compression_type &&
		    cur_compression_type != p.crc.compression_type) {
			if (flags & BTREE_TRIGGER_overwrite)
				bch2_u64s_neg(compression_acct, ARRAY_SIZE(compression_acct));

			ret = bch2_disk_accounting_mod2(trans, gc, compression_acct,
							compression, cur_compression_type);
			if (ret)
				return ret;

			compression_acct[0] = 1;
			compression_acct[1] = 0;
			compression_acct[2] = 0;
		}

		cur_compression_type = p.crc.compression_type;
		if (p.crc.compression_type) {
			compression_acct[1] += p.crc.uncompressed_size;
			compression_acct[2] += p.crc.compressed_size;
		}
	}

	if (acc_replicas_key.replicas.nr_devs) {
		ret = bch2_disk_accounting_mod(trans, &acc_replicas_key, replicas_sectors, 1, gc);
		if (ret)
			return ret;
	}

	if (acc_replicas_key.replicas.nr_devs && !level && k.k->p.snapshot) {
		ret = bch2_disk_accounting_mod2_nr(trans, gc, replicas_sectors, 1, snapshot, k.k->p.snapshot);
		if (ret)
			return ret;
	}

	if (cur_compression_type) {
		if (flags & BTREE_TRIGGER_overwrite)
			bch2_u64s_neg(compression_acct, ARRAY_SIZE(compression_acct));

		ret = bch2_disk_accounting_mod2(trans, gc, compression_acct,
						compression, cur_compression_type);
		if (ret)
			return ret;
	}

	if (level) {
		ret = bch2_disk_accounting_mod2_nr(trans, gc, replicas_sectors, 1, btree, btree_id);
		if (ret)
			return ret;
	} else {
		bool insert = !(flags & BTREE_TRIGGER_overwrite);

		s64 v[3] = {
			insert ? 1 : -1,
			insert ? k.k->size : -((s64) k.k->size),
			*replicas_sectors,
		};
		ret = bch2_disk_accounting_mod2(trans, gc, v, inum, k.k->p.inode);
		if (ret)
			return ret;
	}

	return 0;
}

int bch2_trigger_extent(struct btree_trans *trans,
			enum btree_id btree, unsigned level,
			struct bkey_s_c old, struct bkey_s new,
			enum btree_iter_update_trigger_flags flags)
{
	struct bch_fs *c = trans->c;
	struct bkey_ptrs_c new_ptrs = bch2_bkey_ptrs_c(new.s_c);
	struct bkey_ptrs_c old_ptrs = bch2_bkey_ptrs_c(old);
	unsigned new_ptrs_bytes = (void *) new_ptrs.end - (void *) new_ptrs.start;
	unsigned old_ptrs_bytes = (void *) old_ptrs.end - (void *) old_ptrs.start;

	if (unlikely(flags & BTREE_TRIGGER_check_repair))
		return bch2_check_fix_ptrs(trans, btree, level, new.s_c, flags);

	/* if pointers aren't changing - nothing to do: */
	if (new_ptrs_bytes == old_ptrs_bytes &&
	    !memcmp(new_ptrs.start,
		    old_ptrs.start,
		    new_ptrs_bytes))
		return 0;

	if (flags & (BTREE_TRIGGER_transactional|BTREE_TRIGGER_gc)) {
		s64 old_replicas_sectors = 0, new_replicas_sectors = 0;

		if (old.k->type) {
			int ret = __trigger_extent(trans, btree, level, old,
						   flags & ~BTREE_TRIGGER_insert,
						   &old_replicas_sectors);
			if (ret)
				return ret;
		}

		if (new.k->type) {
			int ret = __trigger_extent(trans, btree, level, new.s_c,
						   flags & ~BTREE_TRIGGER_overwrite,
						   &new_replicas_sectors);
			if (ret)
				return ret;
		}

		int need_rebalance_delta = 0;
		s64 need_rebalance_sectors_delta[1] = { 0 };

		s64 s = bch2_bkey_sectors_need_rebalance(c, old);
		need_rebalance_delta -= s != 0;
		need_rebalance_sectors_delta[0] -= s;

		s = bch2_bkey_sectors_need_rebalance(c, new.s_c);
		need_rebalance_delta += s != 0;
		need_rebalance_sectors_delta[0] += s;

		if ((flags & BTREE_TRIGGER_transactional) && need_rebalance_delta) {
			int ret = bch2_btree_bit_mod_buffered(trans, BTREE_ID_rebalance_work,
							  new.k->p, need_rebalance_delta > 0);
			if (ret)
				return ret;
		}

		if (need_rebalance_sectors_delta[0]) {
			int ret = bch2_disk_accounting_mod2(trans, flags & BTREE_TRIGGER_gc,
							    need_rebalance_sectors_delta, rebalance_work);
			if (ret)
				return ret;
		}
	}

	return 0;
}

/* KEY_TYPE_reservation */

static int __trigger_reservation(struct btree_trans *trans,
			enum btree_id btree_id, unsigned level, struct bkey_s_c k,
			enum btree_iter_update_trigger_flags flags)
{
	if (flags & (BTREE_TRIGGER_transactional|BTREE_TRIGGER_gc)) {
		s64 sectors[1] = { k.k->size };

		if (flags & BTREE_TRIGGER_overwrite)
			sectors[0] = -sectors[0];

		return bch2_disk_accounting_mod2(trans, flags & BTREE_TRIGGER_gc, sectors,
				persistent_reserved, bkey_s_c_to_reservation(k).v->nr_replicas);
	}

	return 0;
}

int bch2_trigger_reservation(struct btree_trans *trans,
			  enum btree_id btree_id, unsigned level,
			  struct bkey_s_c old, struct bkey_s new,
			  enum btree_iter_update_trigger_flags flags)
{
	return trigger_run_overwrite_then_insert(__trigger_reservation, trans, btree_id, level, old, new, flags);
}

/* Mark superblocks: */

static int __bch2_trans_mark_metadata_bucket(struct btree_trans *trans,
				    struct bch_dev *ca, u64 b,
				    enum bch_data_type type,
				    unsigned sectors)
{
	struct bch_fs *c = trans->c;
	struct btree_iter iter;
	int ret = 0;

	struct bkey_i_alloc_v4 *a =
		bch2_trans_start_alloc_update_noupdate(trans, &iter, POS(ca->dev_idx, b));
	if (IS_ERR(a))
		return PTR_ERR(a);

	if (a->v.data_type && type && a->v.data_type != type) {
		bch2_run_explicit_recovery_pass(c, BCH_RECOVERY_PASS_check_allocations);
		log_fsck_err(trans, bucket_metadata_type_mismatch,
			"bucket %llu:%llu gen %u different types of data in same bucket: %s, %s\n"
			"while marking %s",
			iter.pos.inode, iter.pos.offset, a->v.gen,
			bch2_data_type_str(a->v.data_type),
			bch2_data_type_str(type),
			bch2_data_type_str(type));
		ret = -BCH_ERR_metadata_bucket_inconsistency;
		goto err;
	}

	if (a->v.data_type	!= type ||
	    a->v.dirty_sectors	!= sectors) {
		a->v.data_type		= type;
		a->v.dirty_sectors	= sectors;
		ret = bch2_trans_update(trans, &iter, &a->k_i, 0);
	}
err:
fsck_err:
	bch2_trans_iter_exit(trans, &iter);
	return ret;
}

static int bch2_mark_metadata_bucket(struct btree_trans *trans, struct bch_dev *ca,
			u64 b, enum bch_data_type data_type, unsigned sectors,
			enum btree_iter_update_trigger_flags flags)
{
	struct bch_fs *c = trans->c;
	int ret = 0;

	struct bucket *g = gc_bucket(ca, b);
	if (bch2_fs_inconsistent_on(!g, c, "reference to invalid bucket on device %u when marking metadata type %s",
				    ca->dev_idx, bch2_data_type_str(data_type)))
		goto err;

	bucket_lock(g);
	struct bch_alloc_v4 old = bucket_m_to_alloc(*g);

	if (bch2_fs_inconsistent_on(g->data_type &&
			g->data_type != data_type, c,
			"different types of data in same bucket: %s, %s",
			bch2_data_type_str(g->data_type),
			bch2_data_type_str(data_type)))
		goto err_unlock;

	if (bch2_fs_inconsistent_on((u64) g->dirty_sectors + sectors > ca->mi.bucket_size, c,
			"bucket %u:%llu gen %u data type %s sector count overflow: %u + %u > bucket size",
			ca->dev_idx, b, g->gen,
			bch2_data_type_str(g->data_type ?: data_type),
			g->dirty_sectors, sectors))
		goto err_unlock;

	g->data_type = data_type;
	g->dirty_sectors += sectors;
	struct bch_alloc_v4 new = bucket_m_to_alloc(*g);
	bucket_unlock(g);
	ret = bch2_alloc_key_to_dev_counters(trans, ca, &old, &new, flags);
	return ret;
err_unlock:
	bucket_unlock(g);
err:
	return -BCH_ERR_metadata_bucket_inconsistency;
}

int bch2_trans_mark_metadata_bucket(struct btree_trans *trans,
			struct bch_dev *ca, u64 b,
			enum bch_data_type type, unsigned sectors,
			enum btree_iter_update_trigger_flags flags)
{
	BUG_ON(type != BCH_DATA_free &&
	       type != BCH_DATA_sb &&
	       type != BCH_DATA_journal);

	/*
	 * Backup superblock might be past the end of our normal usable space:
	 */
	if (b >= ca->mi.nbuckets)
		return 0;

	if (flags & BTREE_TRIGGER_gc)
		return bch2_mark_metadata_bucket(trans, ca, b, type, sectors, flags);
	else if (flags & BTREE_TRIGGER_transactional)
		return commit_do(trans, NULL, NULL, 0,
				 __bch2_trans_mark_metadata_bucket(trans, ca, b, type, sectors));
	else
		BUG();
}

static int bch2_trans_mark_metadata_sectors(struct btree_trans *trans,
			struct bch_dev *ca, u64 start, u64 end,
			enum bch_data_type type, u64 *bucket, unsigned *bucket_sectors,
			enum btree_iter_update_trigger_flags flags)
{
	do {
		u64 b = sector_to_bucket(ca, start);
		unsigned sectors =
			min_t(u64, bucket_to_sector(ca, b + 1), end) - start;

		if (b != *bucket && *bucket_sectors) {
			int ret = bch2_trans_mark_metadata_bucket(trans, ca, *bucket,
							type, *bucket_sectors, flags);
			if (ret)
				return ret;

			*bucket_sectors = 0;
		}

		*bucket		= b;
		*bucket_sectors	+= sectors;
		start += sectors;
	} while (start < end);

	return 0;
}

static int __bch2_trans_mark_dev_sb(struct btree_trans *trans, struct bch_dev *ca,
			enum btree_iter_update_trigger_flags flags)
{
	struct bch_fs *c = trans->c;

	mutex_lock(&c->sb_lock);
	struct bch_sb_layout layout = ca->disk_sb.sb->layout;
	mutex_unlock(&c->sb_lock);

	u64 bucket = 0;
	unsigned i, bucket_sectors = 0;
	int ret;

	for (i = 0; i < layout.nr_superblocks; i++) {
		u64 offset = le64_to_cpu(layout.sb_offset[i]);

		if (offset == BCH_SB_SECTOR) {
			ret = bch2_trans_mark_metadata_sectors(trans, ca,
						0, BCH_SB_SECTOR,
						BCH_DATA_sb, &bucket, &bucket_sectors, flags);
			if (ret)
				return ret;
		}

		ret = bch2_trans_mark_metadata_sectors(trans, ca, offset,
				      offset + (1 << layout.sb_max_size_bits),
				      BCH_DATA_sb, &bucket, &bucket_sectors, flags);
		if (ret)
			return ret;
	}

	if (bucket_sectors) {
		ret = bch2_trans_mark_metadata_bucket(trans, ca,
				bucket, BCH_DATA_sb, bucket_sectors, flags);
		if (ret)
			return ret;
	}

	for (i = 0; i < ca->journal.nr; i++) {
		ret = bch2_trans_mark_metadata_bucket(trans, ca,
				ca->journal.buckets[i],
				BCH_DATA_journal, ca->mi.bucket_size, flags);
		if (ret)
			return ret;
	}

	return 0;
}

int bch2_trans_mark_dev_sb(struct bch_fs *c, struct bch_dev *ca,
			enum btree_iter_update_trigger_flags flags)
{
	int ret = bch2_trans_run(c,
		__bch2_trans_mark_dev_sb(trans, ca, flags));
	bch_err_fn(c, ret);
	return ret;
}

int bch2_trans_mark_dev_sbs_flags(struct bch_fs *c,
			enum btree_iter_update_trigger_flags flags)
{
	for_each_online_member(c, ca) {
		int ret = bch2_trans_mark_dev_sb(c, ca, flags);
		if (ret) {
			percpu_ref_put(&ca->io_ref);
			return ret;
		}
	}

	return 0;
}

int bch2_trans_mark_dev_sbs(struct bch_fs *c)
{
	return bch2_trans_mark_dev_sbs_flags(c, BTREE_TRIGGER_transactional);
}

bool bch2_is_superblock_bucket(struct bch_dev *ca, u64 b)
{
	struct bch_sb_layout *layout = &ca->disk_sb.sb->layout;
	u64 b_offset	= bucket_to_sector(ca, b);
	u64 b_end	= bucket_to_sector(ca, b + 1);
	unsigned i;

	if (!b)
		return true;

	for (i = 0; i < layout->nr_superblocks; i++) {
		u64 offset = le64_to_cpu(layout->sb_offset[i]);
		u64 end = offset + (1 << layout->sb_max_size_bits);

		if (!(offset >= b_end || end <= b_offset))
			return true;
	}

	for (i = 0; i < ca->journal.nr; i++)
		if (b == ca->journal.buckets[i])
			return true;

	return false;
}

/* Disk reservations: */

#define SECTORS_CACHE	1024

int __bch2_disk_reservation_add(struct bch_fs *c, struct disk_reservation *res,
				u64 sectors, enum bch_reservation_flags flags)
{
	struct bch_fs_pcpu *pcpu;
	u64 old, get;
	u64 sectors_available;
	int ret;

	percpu_down_read(&c->mark_lock);
	preempt_disable();
	pcpu = this_cpu_ptr(c->pcpu);

	if (sectors <= pcpu->sectors_available)
		goto out;

	old = atomic64_read(&c->sectors_available);
	do {
		get = min((u64) sectors + SECTORS_CACHE, old);

		if (get < sectors) {
			preempt_enable();
			goto recalculate;
		}
	} while (!atomic64_try_cmpxchg(&c->sectors_available,
				       &old, old - get));

	pcpu->sectors_available		+= get;

out:
	pcpu->sectors_available		-= sectors;
	this_cpu_add(*c->online_reserved, sectors);
	res->sectors			+= sectors;

	preempt_enable();
	percpu_up_read(&c->mark_lock);
	return 0;

recalculate:
	mutex_lock(&c->sectors_available_lock);

	percpu_u64_set(&c->pcpu->sectors_available, 0);
	sectors_available = avail_factor(__bch2_fs_usage_read_short(c).free);

	if (sectors_available && (flags & BCH_DISK_RESERVATION_PARTIAL))
		sectors = min(sectors, sectors_available);

	if (sectors <= sectors_available ||
	    (flags & BCH_DISK_RESERVATION_NOFAIL)) {
		atomic64_set(&c->sectors_available,
			     max_t(s64, 0, sectors_available - sectors));
		this_cpu_add(*c->online_reserved, sectors);
		res->sectors			+= sectors;
		ret = 0;
	} else {
		atomic64_set(&c->sectors_available, sectors_available);
		ret = -BCH_ERR_ENOSPC_disk_reservation;
	}

	mutex_unlock(&c->sectors_available_lock);
	percpu_up_read(&c->mark_lock);

	return ret;
}

/* Startup/shutdown: */

void bch2_buckets_nouse_free(struct bch_fs *c)
{
	for_each_member_device(c, ca) {
		kvfree_rcu_mightsleep(ca->buckets_nouse);
		ca->buckets_nouse = NULL;
	}
}

int bch2_buckets_nouse_alloc(struct bch_fs *c)
{
	for_each_member_device(c, ca) {
		BUG_ON(ca->buckets_nouse);

		ca->buckets_nouse = bch2_kvmalloc(BITS_TO_LONGS(ca->mi.nbuckets) *
					    sizeof(unsigned long),
					    GFP_KERNEL|__GFP_ZERO);
		if (!ca->buckets_nouse) {
			bch2_dev_put(ca);
			return -BCH_ERR_ENOMEM_buckets_nouse;
		}
	}

	return 0;
}

static void bucket_gens_free_rcu(struct rcu_head *rcu)
{
	struct bucket_gens *buckets =
		container_of(rcu, struct bucket_gens, rcu);

	kvfree(buckets);
}

int bch2_dev_buckets_resize(struct bch_fs *c, struct bch_dev *ca, u64 nbuckets)
{
	struct bucket_gens *bucket_gens = NULL, *old_bucket_gens = NULL;
	bool resize = ca->bucket_gens != NULL;
	int ret;

	if (resize)
		lockdep_assert_held(&c->state_lock);

	if (resize && ca->buckets_nouse)
		return -BCH_ERR_no_resize_with_buckets_nouse;

	bucket_gens = bch2_kvmalloc(struct_size(bucket_gens, b, nbuckets),
				    GFP_KERNEL|__GFP_ZERO);
	if (!bucket_gens) {
		ret = -BCH_ERR_ENOMEM_bucket_gens;
		goto err;
	}

	bucket_gens->first_bucket = ca->mi.first_bucket;
	bucket_gens->nbuckets	= nbuckets;
	bucket_gens->nbuckets_minus_first =
		bucket_gens->nbuckets - bucket_gens->first_bucket;

	old_bucket_gens = rcu_dereference_protected(ca->bucket_gens, 1);

	if (resize) {
		bucket_gens->nbuckets = min(bucket_gens->nbuckets,
					    old_bucket_gens->nbuckets);
		bucket_gens->nbuckets_minus_first =
			bucket_gens->nbuckets - bucket_gens->first_bucket;
		memcpy(bucket_gens->b,
		       old_bucket_gens->b,
		       bucket_gens->nbuckets);
	}

	rcu_assign_pointer(ca->bucket_gens, bucket_gens);
	bucket_gens	= old_bucket_gens;

	nbuckets = ca->mi.nbuckets;

	ret = 0;
err:
	if (bucket_gens)
		call_rcu(&bucket_gens->rcu, bucket_gens_free_rcu);

	return ret;
}

void bch2_dev_buckets_free(struct bch_dev *ca)
{
	kvfree(ca->buckets_nouse);
	kvfree(rcu_dereference_protected(ca->bucket_gens, 1));
	free_percpu(ca->usage);
}

int bch2_dev_buckets_alloc(struct bch_fs *c, struct bch_dev *ca)
{
	ca->usage = alloc_percpu(struct bch_dev_usage);
	if (!ca->usage)
		return -BCH_ERR_ENOMEM_usage_init;

	return bch2_dev_buckets_resize(c, ca, ca->mi.nbuckets);
}
