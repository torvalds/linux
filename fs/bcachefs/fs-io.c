// SPDX-License-Identifier: GPL-2.0
#ifndef NO_BCACHEFS_FS

#include "bcachefs.h"
#include "alloc_foreground.h"
#include "bkey_buf.h"
#include "btree_update.h"
#include "buckets.h"
#include "clock.h"
#include "error.h"
#include "extents.h"
#include "extent_update.h"
#include "fs.h"
#include "fs-io.h"
#include "fs-io-buffered.h"
#include "fs-io-pagecache.h"
#include "fsck.h"
#include "inode.h"
#include "journal.h"
#include "io_misc.h"
#include "keylist.h"
#include "quota.h"
#include "reflink.h"
#include "trace.h"

#include <linux/aio.h>
#include <linux/backing-dev.h>
#include <linux/falloc.h>
#include <linux/migrate.h>
#include <linux/mmu_context.h>
#include <linux/pagevec.h>
#include <linux/rmap.h>
#include <linux/sched/signal.h>
#include <linux/task_io_accounting_ops.h>
#include <linux/uio.h>

#include <trace/events/writeback.h>

struct nocow_flush {
	struct closure	*cl;
	struct bch_dev	*ca;
	struct bio	bio;
};

static void nocow_flush_endio(struct bio *_bio)
{

	struct nocow_flush *bio = container_of(_bio, struct nocow_flush, bio);

	closure_put(bio->cl);
	percpu_ref_put(&bio->ca->io_ref);
	bio_put(&bio->bio);
}

void bch2_inode_flush_nocow_writes_async(struct bch_fs *c,
					 struct bch_inode_info *inode,
					 struct closure *cl)
{
	struct nocow_flush *bio;
	struct bch_dev *ca;
	struct bch_devs_mask devs;
	unsigned dev;

	dev = find_first_bit(inode->ei_devs_need_flush.d, BCH_SB_MEMBERS_MAX);
	if (dev == BCH_SB_MEMBERS_MAX)
		return;

	devs = inode->ei_devs_need_flush;
	memset(&inode->ei_devs_need_flush, 0, sizeof(inode->ei_devs_need_flush));

	for_each_set_bit(dev, devs.d, BCH_SB_MEMBERS_MAX) {
		rcu_read_lock();
		ca = rcu_dereference(c->devs[dev]);
		if (ca && !percpu_ref_tryget(&ca->io_ref))
			ca = NULL;
		rcu_read_unlock();

		if (!ca)
			continue;

		bio = container_of(bio_alloc_bioset(ca->disk_sb.bdev, 0,
						    REQ_OP_WRITE|REQ_PREFLUSH,
						    GFP_KERNEL,
						    &c->nocow_flush_bioset),
				   struct nocow_flush, bio);
		bio->cl			= cl;
		bio->ca			= ca;
		bio->bio.bi_end_io	= nocow_flush_endio;
		closure_bio_submit(&bio->bio, cl);
	}
}

static int bch2_inode_flush_nocow_writes(struct bch_fs *c,
					 struct bch_inode_info *inode)
{
	struct closure cl;

	closure_init_stack(&cl);
	bch2_inode_flush_nocow_writes_async(c, inode, &cl);
	closure_sync(&cl);

	return 0;
}

/* i_size updates: */

struct inode_new_size {
	loff_t		new_size;
	u64		now;
	unsigned	fields;
};

static int inode_set_size(struct btree_trans *trans,
			  struct bch_inode_info *inode,
			  struct bch_inode_unpacked *bi,
			  void *p)
{
	struct inode_new_size *s = p;

	bi->bi_size = s->new_size;
	if (s->fields & ATTR_ATIME)
		bi->bi_atime = s->now;
	if (s->fields & ATTR_MTIME)
		bi->bi_mtime = s->now;
	if (s->fields & ATTR_CTIME)
		bi->bi_ctime = s->now;

	return 0;
}

int __must_check bch2_write_inode_size(struct bch_fs *c,
				       struct bch_inode_info *inode,
				       loff_t new_size, unsigned fields)
{
	struct inode_new_size s = {
		.new_size	= new_size,
		.now		= bch2_current_time(c),
		.fields		= fields,
	};

	return bch2_write_inode(c, inode, inode_set_size, &s, fields);
}

void __bch2_i_sectors_acct(struct bch_fs *c, struct bch_inode_info *inode,
			   struct quota_res *quota_res, s64 sectors)
{
	bch2_fs_inconsistent_on((s64) inode->v.i_blocks + sectors < 0, c,
				"inode %lu i_blocks underflow: %llu + %lli < 0 (ondisk %lli)",
				inode->v.i_ino, (u64) inode->v.i_blocks, sectors,
				inode->ei_inode.bi_sectors);
	inode->v.i_blocks += sectors;

#ifdef CONFIG_BCACHEFS_QUOTA
	if (quota_res &&
	    !test_bit(EI_INODE_SNAPSHOT, &inode->ei_flags) &&
	    sectors > 0) {
		BUG_ON(sectors > quota_res->sectors);
		BUG_ON(sectors > inode->ei_quota_reserved);

		quota_res->sectors -= sectors;
		inode->ei_quota_reserved -= sectors;
	} else {
		bch2_quota_acct(c, inode->ei_qid, Q_SPC, sectors, KEY_TYPE_QUOTA_WARN);
	}
#endif
}

/* fsync: */

/*
 * inode->ei_inode.bi_journal_seq won't be up to date since it's set in an
 * insert trigger: look up the btree inode instead
 */
static int bch2_flush_inode(struct bch_fs *c,
			    struct bch_inode_info *inode)
{
	if (c->opts.journal_flush_disabled)
		return 0;

	if (!bch2_write_ref_tryget(c, BCH_WRITE_REF_fsync))
		return -EROFS;

	struct bch_inode_unpacked u;
	int ret = bch2_inode_find_by_inum(c, inode_inum(inode), &u) ?:
		  bch2_journal_flush_seq(&c->journal, u.bi_journal_seq) ?:
		  bch2_inode_flush_nocow_writes(c, inode);
	bch2_write_ref_put(c, BCH_WRITE_REF_fsync);
	return ret;
}

int bch2_fsync(struct file *file, loff_t start, loff_t end, int datasync)
{
	struct bch_inode_info *inode = file_bch_inode(file);
	struct bch_fs *c = inode->v.i_sb->s_fs_info;
	int ret, err;

	trace_bch2_fsync(file, datasync);

	ret = file_write_and_wait_range(file, start, end);
	if (ret)
		goto out;
	ret = sync_inode_metadata(&inode->v, 1);
	if (ret)
		goto out;
	ret = bch2_flush_inode(c, inode);
out:
	ret = bch2_err_class(ret);
	if (ret == -EROFS)
		ret = -EIO;

	err = file_check_and_advance_wb_err(file);
	if (!ret)
		ret = err;

	return ret;
}

/* truncate: */

static inline int range_has_data(struct bch_fs *c, u32 subvol,
				 struct bpos start,
				 struct bpos end)
{
	struct btree_trans *trans = bch2_trans_get(c);
	struct btree_iter iter;
	struct bkey_s_c k;
	int ret = 0;
retry:
	bch2_trans_begin(trans);

	ret = bch2_subvolume_get_snapshot(trans, subvol, &start.snapshot);
	if (ret)
		goto err;

	for_each_btree_key_upto_norestart(trans, iter, BTREE_ID_extents, start, end, 0, k, ret)
		if (bkey_extent_is_data(k.k) && !bkey_extent_is_unwritten(k)) {
			ret = 1;
			break;
		}
	start = iter.pos;
	bch2_trans_iter_exit(trans, &iter);
err:
	if (bch2_err_matches(ret, BCH_ERR_transaction_restart))
		goto retry;

	bch2_trans_put(trans);
	return ret;
}

static int __bch2_truncate_folio(struct bch_inode_info *inode,
				 pgoff_t index, loff_t start, loff_t end)
{
	struct bch_fs *c = inode->v.i_sb->s_fs_info;
	struct address_space *mapping = inode->v.i_mapping;
	struct bch_folio *s;
	unsigned start_offset;
	unsigned end_offset;
	unsigned i;
	struct folio *folio;
	s64 i_sectors_delta = 0;
	int ret = 0;
	u64 end_pos;

	folio = filemap_lock_folio(mapping, index);
	if (IS_ERR_OR_NULL(folio)) {
		/*
		 * XXX: we're doing two index lookups when we end up reading the
		 * folio
		 */
		ret = range_has_data(c, inode->ei_subvol,
				POS(inode->v.i_ino, (index << PAGE_SECTORS_SHIFT)),
				POS(inode->v.i_ino, (index << PAGE_SECTORS_SHIFT) + PAGE_SECTORS));
		if (ret <= 0)
			return ret;

		folio = __filemap_get_folio(mapping, index,
					    FGP_LOCK|FGP_CREAT, GFP_KERNEL);
		if (IS_ERR_OR_NULL(folio)) {
			ret = -ENOMEM;
			goto out;
		}
	}

	BUG_ON(start	>= folio_end_pos(folio));
	BUG_ON(end	<= folio_pos(folio));

	start_offset	= max(start, folio_pos(folio)) - folio_pos(folio);
	end_offset	= min_t(u64, end, folio_end_pos(folio)) - folio_pos(folio);

	/* Folio boundary? Nothing to do */
	if (start_offset == 0 &&
	    end_offset == folio_size(folio)) {
		ret = 0;
		goto unlock;
	}

	s = bch2_folio_create(folio, 0);
	if (!s) {
		ret = -ENOMEM;
		goto unlock;
	}

	if (!folio_test_uptodate(folio)) {
		ret = bch2_read_single_folio(folio, mapping);
		if (ret)
			goto unlock;
	}

	ret = bch2_folio_set(c, inode_inum(inode), &folio, 1);
	if (ret)
		goto unlock;

	for (i = round_up(start_offset, block_bytes(c)) >> 9;
	     i < round_down(end_offset, block_bytes(c)) >> 9;
	     i++) {
		s->s[i].nr_replicas	= 0;

		i_sectors_delta -= s->s[i].state == SECTOR_dirty;
		bch2_folio_sector_set(folio, s, i, SECTOR_unallocated);
	}

	bch2_i_sectors_acct(c, inode, NULL, i_sectors_delta);

	/*
	 * Caller needs to know whether this folio will be written out by
	 * writeback - doing an i_size update if necessary - or whether it will
	 * be responsible for the i_size update.
	 *
	 * Note that we shouldn't ever see a folio beyond EOF, but check and
	 * warn if so. This has been observed by failure to clean up folios
	 * after a short write and there's still a chance reclaim will fix
	 * things up.
	 */
	WARN_ON_ONCE(folio_pos(folio) >= inode->v.i_size);
	end_pos = folio_end_pos(folio);
	if (inode->v.i_size > folio_pos(folio))
		end_pos = min_t(u64, inode->v.i_size, end_pos);
	ret = s->s[folio_pos_to_s(folio, end_pos - 1)].state >= SECTOR_dirty;

	folio_zero_segment(folio, start_offset, end_offset);

	/*
	 * Bit of a hack - we don't want truncate to fail due to -ENOSPC.
	 *
	 * XXX: because we aren't currently tracking whether the folio has actual
	 * data in it (vs. just 0s, or only partially written) this wrong. ick.
	 */
	BUG_ON(bch2_get_folio_disk_reservation(c, inode, folio, false));

	/*
	 * This removes any writeable userspace mappings; we need to force
	 * .page_mkwrite to be called again before any mmapped writes, to
	 * redirty the full page:
	 */
	folio_mkclean(folio);
	filemap_dirty_folio(mapping, folio);
unlock:
	folio_unlock(folio);
	folio_put(folio);
out:
	return ret;
}

static int bch2_truncate_folio(struct bch_inode_info *inode, loff_t from)
{
	return __bch2_truncate_folio(inode, from >> PAGE_SHIFT,
				     from, ANYSINT_MAX(loff_t));
}

static int bch2_truncate_folios(struct bch_inode_info *inode,
				loff_t start, loff_t end)
{
	int ret = __bch2_truncate_folio(inode, start >> PAGE_SHIFT,
					start, end);

	if (ret >= 0 &&
	    start >> PAGE_SHIFT != end >> PAGE_SHIFT)
		ret = __bch2_truncate_folio(inode,
					(end - 1) >> PAGE_SHIFT,
					start, end);
	return ret;
}

static int bch2_extend(struct mnt_idmap *idmap,
		       struct bch_inode_info *inode,
		       struct bch_inode_unpacked *inode_u,
		       struct iattr *iattr)
{
	struct address_space *mapping = inode->v.i_mapping;
	int ret;

	/*
	 * sync appends:
	 *
	 * this has to be done _before_ extending i_size:
	 */
	ret = filemap_write_and_wait_range(mapping, inode_u->bi_size, S64_MAX);
	if (ret)
		return ret;

	truncate_setsize(&inode->v, iattr->ia_size);

	return bch2_setattr_nonsize(idmap, inode, iattr);
}

int bchfs_truncate(struct mnt_idmap *idmap,
		  struct bch_inode_info *inode, struct iattr *iattr)
{
	struct bch_fs *c = inode->v.i_sb->s_fs_info;
	struct address_space *mapping = inode->v.i_mapping;
	struct bch_inode_unpacked inode_u;
	s64 i_sectors_delta = 0;
	int ret = 0;

	/*
	 * If the truncate call with change the size of the file, the
	 * cmtimes should be updated. If the size will not change, we
	 * do not need to update the cmtimes.
	 */
	if (iattr->ia_size != inode->v.i_size) {
		if (!(iattr->ia_valid & ATTR_MTIME))
			ktime_get_coarse_real_ts64(&iattr->ia_mtime);
		if (!(iattr->ia_valid & ATTR_CTIME))
			ktime_get_coarse_real_ts64(&iattr->ia_ctime);
		iattr->ia_valid |= ATTR_MTIME|ATTR_CTIME;
	}

	inode_dio_wait(&inode->v);
	bch2_pagecache_block_get(inode);

	ret = bch2_inode_find_by_inum(c, inode_inum(inode), &inode_u);
	if (ret)
		goto err;

	/*
	 * check this before next assertion; on filesystem error our normal
	 * invariants are a bit broken (truncate has to truncate the page cache
	 * before the inode).
	 */
	ret = bch2_journal_error(&c->journal);
	if (ret)
		goto err;

	WARN_ONCE(!test_bit(EI_INODE_ERROR, &inode->ei_flags) &&
		  inode->v.i_size < inode_u.bi_size,
		  "truncate spotted in mem i_size < btree i_size: %llu < %llu\n",
		  (u64) inode->v.i_size, inode_u.bi_size);

	if (iattr->ia_size > inode->v.i_size) {
		ret = bch2_extend(idmap, inode, &inode_u, iattr);
		goto err;
	}

	iattr->ia_valid &= ~ATTR_SIZE;

	ret = bch2_truncate_folio(inode, iattr->ia_size);
	if (unlikely(ret < 0))
		goto err;

	truncate_setsize(&inode->v, iattr->ia_size);

	/*
	 * When extending, we're going to write the new i_size to disk
	 * immediately so we need to flush anything above the current on disk
	 * i_size first:
	 *
	 * Also, when extending we need to flush the page that i_size currently
	 * straddles - if it's mapped to userspace, we need to ensure that
	 * userspace has to redirty it and call .mkwrite -> set_page_dirty
	 * again to allocate the part of the page that was extended.
	 */
	if (iattr->ia_size > inode_u.bi_size)
		ret = filemap_write_and_wait_range(mapping,
				inode_u.bi_size,
				iattr->ia_size - 1);
	else if (iattr->ia_size & (PAGE_SIZE - 1))
		ret = filemap_write_and_wait_range(mapping,
				round_down(iattr->ia_size, PAGE_SIZE),
				iattr->ia_size - 1);
	if (ret)
		goto err;

	ret = bch2_truncate(c, inode_inum(inode), iattr->ia_size, &i_sectors_delta);
	bch2_i_sectors_acct(c, inode, NULL, i_sectors_delta);

	if (unlikely(ret)) {
		/*
		 * If we error here, VFS caches are now inconsistent with btree
		 */
		set_bit(EI_INODE_ERROR, &inode->ei_flags);
		goto err;
	}

	bch2_fs_inconsistent_on(!inode->v.i_size && inode->v.i_blocks &&
				!bch2_journal_error(&c->journal), c,
				"inode %lu truncated to 0 but i_blocks %llu (ondisk %lli)",
				inode->v.i_ino, (u64) inode->v.i_blocks,
				inode->ei_inode.bi_sectors);

	ret = bch2_setattr_nonsize(idmap, inode, iattr);
err:
	bch2_pagecache_block_put(inode);
	return bch2_err_class(ret);
}

/* fallocate: */

static int inode_update_times_fn(struct btree_trans *trans,
				 struct bch_inode_info *inode,
				 struct bch_inode_unpacked *bi, void *p)
{
	struct bch_fs *c = inode->v.i_sb->s_fs_info;

	bi->bi_mtime = bi->bi_ctime = bch2_current_time(c);
	return 0;
}

static noinline long bchfs_fpunch(struct bch_inode_info *inode, loff_t offset, loff_t len)
{
	struct bch_fs *c = inode->v.i_sb->s_fs_info;
	u64 end		= offset + len;
	u64 block_start	= round_up(offset, block_bytes(c));
	u64 block_end	= round_down(end, block_bytes(c));
	bool truncated_last_page;
	int ret = 0;

	ret = bch2_truncate_folios(inode, offset, end);
	if (unlikely(ret < 0))
		goto err;

	truncated_last_page = ret;

	truncate_pagecache_range(&inode->v, offset, end - 1);

	if (block_start < block_end) {
		s64 i_sectors_delta = 0;

		ret = bch2_fpunch(c, inode_inum(inode),
				  block_start >> 9, block_end >> 9,
				  &i_sectors_delta);
		bch2_i_sectors_acct(c, inode, NULL, i_sectors_delta);
	}

	mutex_lock(&inode->ei_update_lock);
	if (end >= inode->v.i_size && !truncated_last_page) {
		ret = bch2_write_inode_size(c, inode, inode->v.i_size,
					    ATTR_MTIME|ATTR_CTIME);
	} else {
		ret = bch2_write_inode(c, inode, inode_update_times_fn, NULL,
				       ATTR_MTIME|ATTR_CTIME);
	}
	mutex_unlock(&inode->ei_update_lock);
err:
	return ret;
}

static noinline long bchfs_fcollapse_finsert(struct bch_inode_info *inode,
				   loff_t offset, loff_t len,
				   bool insert)
{
	struct bch_fs *c = inode->v.i_sb->s_fs_info;
	struct address_space *mapping = inode->v.i_mapping;
	s64 i_sectors_delta = 0;
	int ret = 0;

	if ((offset | len) & (block_bytes(c) - 1))
		return -EINVAL;

	if (insert) {
		if (offset >= inode->v.i_size)
			return -EINVAL;
	} else {
		if (offset + len >= inode->v.i_size)
			return -EINVAL;
	}

	ret = bch2_write_invalidate_inode_pages_range(mapping, offset, LLONG_MAX);
	if (ret)
		return ret;

	if (insert)
		i_size_write(&inode->v, inode->v.i_size + len);

	ret = bch2_fcollapse_finsert(c, inode_inum(inode), offset >> 9, len >> 9,
				     insert, &i_sectors_delta);
	if (!ret && !insert)
		i_size_write(&inode->v, inode->v.i_size - len);
	bch2_i_sectors_acct(c, inode, NULL, i_sectors_delta);

	return ret;
}

static noinline int __bchfs_fallocate(struct bch_inode_info *inode, int mode,
			     u64 start_sector, u64 end_sector)
{
	struct bch_fs *c = inode->v.i_sb->s_fs_info;
	struct btree_trans *trans = bch2_trans_get(c);
	struct btree_iter iter;
	struct bpos end_pos = POS(inode->v.i_ino, end_sector);
	struct bch_io_opts opts;
	int ret = 0;

	bch2_inode_opts_get(&opts, c, &inode->ei_inode);

	bch2_trans_iter_init(trans, &iter, BTREE_ID_extents,
			POS(inode->v.i_ino, start_sector),
			BTREE_ITER_slots|BTREE_ITER_intent);

	while (!ret && bkey_lt(iter.pos, end_pos)) {
		s64 i_sectors_delta = 0;
		struct quota_res quota_res = { 0 };
		struct bkey_s_c k;
		unsigned sectors;
		bool is_allocation;
		u64 hole_start, hole_end;
		u32 snapshot;

		bch2_trans_begin(trans);

		ret = bch2_subvolume_get_snapshot(trans,
					inode->ei_subvol, &snapshot);
		if (ret)
			goto bkey_err;

		bch2_btree_iter_set_snapshot(&iter, snapshot);

		k = bch2_btree_iter_peek_slot(&iter);
		if ((ret = bkey_err(k)))
			goto bkey_err;

		hole_start	= iter.pos.offset;
		hole_end	= bpos_min(k.k->p, end_pos).offset;
		is_allocation	= bkey_extent_is_allocation(k.k);

		/* already reserved */
		if (bkey_extent_is_reservation(k) &&
		    bch2_bkey_nr_ptrs_fully_allocated(k) >= opts.data_replicas) {
			bch2_btree_iter_advance(&iter);
			continue;
		}

		if (bkey_extent_is_data(k.k) &&
		    !(mode & FALLOC_FL_ZERO_RANGE)) {
			bch2_btree_iter_advance(&iter);
			continue;
		}

		if (!(mode & FALLOC_FL_ZERO_RANGE)) {
			/*
			 * Lock ordering - can't be holding btree locks while
			 * blocking on a folio lock:
			 */
			if (bch2_clamp_data_hole(&inode->v,
						 &hole_start,
						 &hole_end,
						 opts.data_replicas, true))
				ret = drop_locks_do(trans,
					(bch2_clamp_data_hole(&inode->v,
							      &hole_start,
							      &hole_end,
							      opts.data_replicas, false), 0));
			bch2_btree_iter_set_pos(&iter, POS(iter.pos.inode, hole_start));

			if (ret)
				goto bkey_err;

			if (hole_start == hole_end)
				continue;
		}

		sectors	= hole_end - hole_start;

		if (!is_allocation) {
			ret = bch2_quota_reservation_add(c, inode,
					&quota_res, sectors, true);
			if (unlikely(ret))
				goto bkey_err;
		}

		ret = bch2_extent_fallocate(trans, inode_inum(inode), &iter,
					    sectors, opts, &i_sectors_delta,
					    writepoint_hashed((unsigned long) current));
		if (ret)
			goto bkey_err;

		bch2_i_sectors_acct(c, inode, &quota_res, i_sectors_delta);

		if (bch2_mark_pagecache_reserved(inode, &hole_start,
						 iter.pos.offset, true))
			drop_locks_do(trans,
				bch2_mark_pagecache_reserved(inode, &hole_start,
							     iter.pos.offset, false));
bkey_err:
		bch2_quota_reservation_put(c, inode, &quota_res);
		if (bch2_err_matches(ret, BCH_ERR_transaction_restart))
			ret = 0;
	}

	if (bch2_err_matches(ret, ENOSPC) && (mode & FALLOC_FL_ZERO_RANGE)) {
		struct quota_res quota_res = { 0 };
		s64 i_sectors_delta = 0;

		bch2_fpunch_at(trans, &iter, inode_inum(inode),
			       end_sector, &i_sectors_delta);
		bch2_i_sectors_acct(c, inode, &quota_res, i_sectors_delta);
		bch2_quota_reservation_put(c, inode, &quota_res);
	}

	bch2_trans_iter_exit(trans, &iter);
	bch2_trans_put(trans);
	return ret;
}

static noinline long bchfs_fallocate(struct bch_inode_info *inode, int mode,
			    loff_t offset, loff_t len)
{
	struct bch_fs *c = inode->v.i_sb->s_fs_info;
	u64 end		= offset + len;
	u64 block_start	= round_down(offset,	block_bytes(c));
	u64 block_end	= round_up(end,		block_bytes(c));
	bool truncated_last_page = false;
	int ret, ret2 = 0;

	if (!(mode & FALLOC_FL_KEEP_SIZE) && end > inode->v.i_size) {
		ret = inode_newsize_ok(&inode->v, end);
		if (ret)
			return ret;
	}

	if (mode & FALLOC_FL_ZERO_RANGE) {
		ret = bch2_truncate_folios(inode, offset, end);
		if (unlikely(ret < 0))
			return ret;

		truncated_last_page = ret;

		truncate_pagecache_range(&inode->v, offset, end - 1);

		block_start	= round_up(offset,	block_bytes(c));
		block_end	= round_down(end,	block_bytes(c));
	}

	ret = __bchfs_fallocate(inode, mode, block_start >> 9, block_end >> 9);

	/*
	 * On -ENOSPC in ZERO_RANGE mode, we still want to do the inode update,
	 * so that the VFS cache i_size is consistent with the btree i_size:
	 */
	if (ret &&
	    !(bch2_err_matches(ret, ENOSPC) && (mode & FALLOC_FL_ZERO_RANGE)))
		return ret;

	if (mode & FALLOC_FL_KEEP_SIZE && end > inode->v.i_size)
		end = inode->v.i_size;

	if (end >= inode->v.i_size &&
	    (((mode & FALLOC_FL_ZERO_RANGE) && !truncated_last_page) ||
	     !(mode & FALLOC_FL_KEEP_SIZE))) {
		spin_lock(&inode->v.i_lock);
		i_size_write(&inode->v, end);
		spin_unlock(&inode->v.i_lock);

		mutex_lock(&inode->ei_update_lock);
		ret2 = bch2_write_inode_size(c, inode, end, 0);
		mutex_unlock(&inode->ei_update_lock);
	}

	return ret ?: ret2;
}

long bch2_fallocate_dispatch(struct file *file, int mode,
			     loff_t offset, loff_t len)
{
	struct bch_inode_info *inode = file_bch_inode(file);
	struct bch_fs *c = inode->v.i_sb->s_fs_info;
	long ret;

	if (!bch2_write_ref_tryget(c, BCH_WRITE_REF_fallocate))
		return -EROFS;

	inode_lock(&inode->v);
	inode_dio_wait(&inode->v);
	bch2_pagecache_block_get(inode);

	ret = file_modified(file);
	if (ret)
		goto err;

	if (!(mode & ~(FALLOC_FL_KEEP_SIZE|FALLOC_FL_ZERO_RANGE)))
		ret = bchfs_fallocate(inode, mode, offset, len);
	else if (mode == (FALLOC_FL_PUNCH_HOLE|FALLOC_FL_KEEP_SIZE))
		ret = bchfs_fpunch(inode, offset, len);
	else if (mode == FALLOC_FL_INSERT_RANGE)
		ret = bchfs_fcollapse_finsert(inode, offset, len, true);
	else if (mode == FALLOC_FL_COLLAPSE_RANGE)
		ret = bchfs_fcollapse_finsert(inode, offset, len, false);
	else
		ret = -EOPNOTSUPP;
err:
	bch2_pagecache_block_put(inode);
	inode_unlock(&inode->v);
	bch2_write_ref_put(c, BCH_WRITE_REF_fallocate);

	return bch2_err_class(ret);
}

/*
 * Take a quota reservation for unallocated blocks in a given file range
 * Does not check pagecache
 */
static int quota_reserve_range(struct bch_inode_info *inode,
			       struct quota_res *res,
			       u64 start, u64 end)
{
	struct bch_fs *c = inode->v.i_sb->s_fs_info;
	struct btree_trans *trans = bch2_trans_get(c);
	struct btree_iter iter;
	struct bkey_s_c k;
	u32 snapshot;
	u64 sectors = end - start;
	u64 pos = start;
	int ret;
retry:
	bch2_trans_begin(trans);

	ret = bch2_subvolume_get_snapshot(trans, inode->ei_subvol, &snapshot);
	if (ret)
		goto err;

	bch2_trans_iter_init(trans, &iter, BTREE_ID_extents,
			     SPOS(inode->v.i_ino, pos, snapshot), 0);

	while (!(ret = btree_trans_too_many_iters(trans)) &&
	       (k = bch2_btree_iter_peek_upto(&iter, POS(inode->v.i_ino, end - 1))).k &&
	       !(ret = bkey_err(k))) {
		if (bkey_extent_is_allocation(k.k)) {
			u64 s = min(end, k.k->p.offset) -
				max(start, bkey_start_offset(k.k));
			BUG_ON(s > sectors);
			sectors -= s;
		}
		bch2_btree_iter_advance(&iter);
	}
	pos = iter.pos.offset;
	bch2_trans_iter_exit(trans, &iter);
err:
	if (bch2_err_matches(ret, BCH_ERR_transaction_restart))
		goto retry;

	bch2_trans_put(trans);

	return ret ?: bch2_quota_reservation_add(c, inode, res, sectors, true);
}

loff_t bch2_remap_file_range(struct file *file_src, loff_t pos_src,
			     struct file *file_dst, loff_t pos_dst,
			     loff_t len, unsigned remap_flags)
{
	struct bch_inode_info *src = file_bch_inode(file_src);
	struct bch_inode_info *dst = file_bch_inode(file_dst);
	struct bch_fs *c = src->v.i_sb->s_fs_info;
	struct quota_res quota_res = { 0 };
	s64 i_sectors_delta = 0;
	u64 aligned_len;
	loff_t ret = 0;

	if (remap_flags & ~(REMAP_FILE_DEDUP|REMAP_FILE_ADVISORY))
		return -EINVAL;

	if ((pos_src & (block_bytes(c) - 1)) ||
	    (pos_dst & (block_bytes(c) - 1)))
		return -EINVAL;

	if (src == dst &&
	    abs(pos_src - pos_dst) < len)
		return -EINVAL;

	lock_two_nondirectories(&src->v, &dst->v);
	bch2_lock_inodes(INODE_PAGECACHE_BLOCK, src, dst);

	inode_dio_wait(&src->v);
	inode_dio_wait(&dst->v);

	ret = generic_remap_file_range_prep(file_src, pos_src,
					    file_dst, pos_dst,
					    &len, remap_flags);
	if (ret < 0 || len == 0)
		goto err;

	aligned_len = round_up((u64) len, block_bytes(c));

	ret = bch2_write_invalidate_inode_pages_range(dst->v.i_mapping,
				pos_dst, pos_dst + len - 1);
	if (ret)
		goto err;

	ret = quota_reserve_range(dst, &quota_res, pos_dst >> 9,
				  (pos_dst + aligned_len) >> 9);
	if (ret)
		goto err;

	if (!(remap_flags & REMAP_FILE_DEDUP))
		file_update_time(file_dst);

	bch2_mark_pagecache_unallocated(src, pos_src >> 9,
				   (pos_src + aligned_len) >> 9);

	ret = bch2_remap_range(c,
			       inode_inum(dst), pos_dst >> 9,
			       inode_inum(src), pos_src >> 9,
			       aligned_len >> 9,
			       pos_dst + len, &i_sectors_delta);
	if (ret < 0)
		goto err;

	/*
	 * due to alignment, we might have remapped slightly more than requsted
	 */
	ret = min((u64) ret << 9, (u64) len);

	bch2_i_sectors_acct(c, dst, &quota_res, i_sectors_delta);

	spin_lock(&dst->v.i_lock);
	if (pos_dst + ret > dst->v.i_size)
		i_size_write(&dst->v, pos_dst + ret);
	spin_unlock(&dst->v.i_lock);

	if ((file_dst->f_flags & (__O_SYNC | O_DSYNC)) ||
	    IS_SYNC(file_inode(file_dst)))
		ret = bch2_flush_inode(c, dst);
err:
	bch2_quota_reservation_put(c, dst, &quota_res);
	bch2_unlock_inodes(INODE_PAGECACHE_BLOCK, src, dst);
	unlock_two_nondirectories(&src->v, &dst->v);

	return bch2_err_class(ret);
}

/* fseek: */

static loff_t bch2_seek_data(struct file *file, u64 offset)
{
	struct bch_inode_info *inode = file_bch_inode(file);
	struct bch_fs *c = inode->v.i_sb->s_fs_info;
	struct btree_trans *trans;
	struct btree_iter iter;
	struct bkey_s_c k;
	subvol_inum inum = inode_inum(inode);
	u64 isize, next_data = MAX_LFS_FILESIZE;
	u32 snapshot;
	int ret;

	isize = i_size_read(&inode->v);
	if (offset >= isize)
		return -ENXIO;

	trans = bch2_trans_get(c);
retry:
	bch2_trans_begin(trans);

	ret = bch2_subvolume_get_snapshot(trans, inum.subvol, &snapshot);
	if (ret)
		goto err;

	for_each_btree_key_upto_norestart(trans, iter, BTREE_ID_extents,
			   SPOS(inode->v.i_ino, offset >> 9, snapshot),
			   POS(inode->v.i_ino, U64_MAX),
			   0, k, ret) {
		if (bkey_extent_is_data(k.k)) {
			next_data = max(offset, bkey_start_offset(k.k) << 9);
			break;
		} else if (k.k->p.offset >> 9 > isize)
			break;
	}
	bch2_trans_iter_exit(trans, &iter);
err:
	if (bch2_err_matches(ret, BCH_ERR_transaction_restart))
		goto retry;

	bch2_trans_put(trans);
	if (ret)
		return ret;

	if (next_data > offset)
		next_data = bch2_seek_pagecache_data(&inode->v,
					offset, next_data, 0, false);

	if (next_data >= isize)
		return -ENXIO;

	return vfs_setpos(file, next_data, MAX_LFS_FILESIZE);
}

static loff_t bch2_seek_hole(struct file *file, u64 offset)
{
	struct bch_inode_info *inode = file_bch_inode(file);
	struct bch_fs *c = inode->v.i_sb->s_fs_info;
	struct btree_trans *trans;
	struct btree_iter iter;
	struct bkey_s_c k;
	subvol_inum inum = inode_inum(inode);
	u64 isize, next_hole = MAX_LFS_FILESIZE;
	u32 snapshot;
	int ret;

	isize = i_size_read(&inode->v);
	if (offset >= isize)
		return -ENXIO;

	trans = bch2_trans_get(c);
retry:
	bch2_trans_begin(trans);

	ret = bch2_subvolume_get_snapshot(trans, inum.subvol, &snapshot);
	if (ret)
		goto err;

	for_each_btree_key_norestart(trans, iter, BTREE_ID_extents,
			   SPOS(inode->v.i_ino, offset >> 9, snapshot),
			   BTREE_ITER_slots, k, ret) {
		if (k.k->p.inode != inode->v.i_ino) {
			next_hole = bch2_seek_pagecache_hole(&inode->v,
					offset, MAX_LFS_FILESIZE, 0, false);
			break;
		} else if (!bkey_extent_is_data(k.k)) {
			next_hole = bch2_seek_pagecache_hole(&inode->v,
					max(offset, bkey_start_offset(k.k) << 9),
					k.k->p.offset << 9, 0, false);

			if (next_hole < k.k->p.offset << 9)
				break;
		} else {
			offset = max(offset, bkey_start_offset(k.k) << 9);
		}
	}
	bch2_trans_iter_exit(trans, &iter);
err:
	if (bch2_err_matches(ret, BCH_ERR_transaction_restart))
		goto retry;

	bch2_trans_put(trans);
	if (ret)
		return ret;

	if (next_hole > isize)
		next_hole = isize;

	return vfs_setpos(file, next_hole, MAX_LFS_FILESIZE);
}

loff_t bch2_llseek(struct file *file, loff_t offset, int whence)
{
	loff_t ret;

	switch (whence) {
	case SEEK_SET:
	case SEEK_CUR:
	case SEEK_END:
		ret = generic_file_llseek(file, offset, whence);
		break;
	case SEEK_DATA:
		ret = bch2_seek_data(file, offset);
		break;
	case SEEK_HOLE:
		ret = bch2_seek_hole(file, offset);
		break;
	default:
		ret = -EINVAL;
		break;
	}

	return bch2_err_class(ret);
}

void bch2_fs_fsio_exit(struct bch_fs *c)
{
	bioset_exit(&c->nocow_flush_bioset);
}

int bch2_fs_fsio_init(struct bch_fs *c)
{
	if (bioset_init(&c->nocow_flush_bioset,
			1, offsetof(struct nocow_flush, bio), 0))
		return -BCH_ERR_ENOMEM_nocow_flush_bioset_init;

	return 0;
}

#endif /* NO_BCACHEFS_FS */
