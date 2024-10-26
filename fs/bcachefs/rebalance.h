/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _BCACHEFS_REBALANCE_H
#define _BCACHEFS_REBALANCE_H

#include "compress.h"
#include "disk_groups.h"
#include "rebalance_types.h"

static inline unsigned bch2_bkey_ptrs_need_compress(struct bch_fs *c,
					   struct bch_io_opts *opts,
					   struct bkey_s_c k,
					   struct bkey_ptrs_c ptrs)
{
	if (!opts->background_compression)
		return 0;

	unsigned compression_type = bch2_compression_opt_to_type(opts->background_compression);
	const union bch_extent_entry *entry;
	struct extent_ptr_decoded p;
	unsigned ptr_bit = 1;
	unsigned rewrite_ptrs = 0;

	bkey_for_each_ptr_decode(k.k, ptrs, p, entry) {
		if (p.crc.compression_type == BCH_COMPRESSION_TYPE_incompressible ||
		    p.ptr.unwritten)
			return 0;

		if (!p.ptr.cached && p.crc.compression_type != compression_type)
			rewrite_ptrs |= ptr_bit;
		ptr_bit <<= 1;
	}

	return rewrite_ptrs;
}

static inline unsigned bch2_bkey_ptrs_need_move(struct bch_fs *c,
				       struct bch_io_opts *opts,
				       struct bkey_ptrs_c ptrs)
{
	if (!opts->background_target ||
	    !bch2_target_accepts_data(c, BCH_DATA_user, opts->background_target))
		return 0;

	unsigned ptr_bit = 1;
	unsigned rewrite_ptrs = 0;

	bkey_for_each_ptr(ptrs, ptr) {
		if (!ptr->cached && !bch2_dev_in_target(c, ptr->dev, opts->background_target))
			rewrite_ptrs |= ptr_bit;
		ptr_bit <<= 1;
	}

	return rewrite_ptrs;
}

int bch2_set_rebalance_needs_scan_trans(struct btree_trans *, u64);
int bch2_set_rebalance_needs_scan(struct bch_fs *, u64 inum);
int bch2_set_fs_needs_rebalance(struct bch_fs *);

static inline void rebalance_wakeup(struct bch_fs *c)
{
	struct task_struct *p;

	rcu_read_lock();
	p = rcu_dereference(c->rebalance.thread);
	if (p)
		wake_up_process(p);
	rcu_read_unlock();
}

void bch2_rebalance_status_to_text(struct printbuf *, struct bch_fs *);

void bch2_rebalance_stop(struct bch_fs *);
int bch2_rebalance_start(struct bch_fs *);
void bch2_fs_rebalance_init(struct bch_fs *);

#endif /* _BCACHEFS_REBALANCE_H */
