// SPDX-License-Identifier: GPL-2.0
#ifndef NO_BCACHEFS_FS

#include "bcachefs.h"
#include "alloc_foreground.h"
#include "btree_update.h"
#include "buckets.h"
#include "clock.h"
#include "error.h"
#include "extents.h"
#include "fs.h"
#include "fs-io.h"
#include "fsck.h"
#include "inode.h"
#include "journal.h"
#include "io.h"
#include "keylist.h"
#include "quota.h"
#include "trace.h"

#include <linux/aio.h>
#include <linux/backing-dev.h>
#include <linux/falloc.h>
#include <linux/migrate.h>
#include <linux/mmu_context.h>
#include <linux/pagevec.h>
#include <linux/sched/signal.h>
#include <linux/task_io_accounting_ops.h>
#include <linux/uio.h>
#include <linux/writeback.h>

#include <trace/events/writeback.h>

struct quota_res {
	u64				sectors;
};

struct bchfs_write_op {
	struct bch_inode_info		*inode;
	s64				sectors_added;
	bool				is_dio;
	bool				unalloc;
	u64				new_i_size;

	/* must be last: */
	struct bch_write_op		op;
};

struct bch_writepage_io {
	struct closure			cl;
	u64				new_sectors;

	/* must be last: */
	struct bchfs_write_op		op;
};

struct dio_write {
	struct closure			cl;
	struct kiocb			*req;
	struct mm_struct		*mm;
	unsigned			loop:1,
					sync:1,
					free_iov:1;
	struct quota_res		quota_res;

	struct iov_iter			iter;
	struct iovec			inline_vecs[2];

	/* must be last: */
	struct bchfs_write_op		iop;
};

struct dio_read {
	struct closure			cl;
	struct kiocb			*req;
	long				ret;
	struct bch_read_bio		rbio;
};

/* pagecache_block must be held */
static int write_invalidate_inode_pages_range(struct address_space *mapping,
					      loff_t start, loff_t end)
{
	int ret;

	/*
	 * XXX: the way this is currently implemented, we can spin if a process
	 * is continually redirtying a specific page
	 */
	do {
		if (!mapping->nrpages)
			return 0;

		ret = filemap_write_and_wait_range(mapping, start, end);
		if (ret)
			break;

		if (!mapping->nrpages)
			return 0;

		ret = invalidate_inode_pages2_range(mapping,
				start >> PAGE_SHIFT,
				end >> PAGE_SHIFT);
	} while (ret == -EBUSY);

	return ret;
}

/* quotas */

#ifdef CONFIG_BCACHEFS_QUOTA

static void bch2_quota_reservation_put(struct bch_fs *c,
				       struct bch_inode_info *inode,
				       struct quota_res *res)
{
	if (!res->sectors)
		return;

	mutex_lock(&inode->ei_quota_lock);
	BUG_ON(res->sectors > inode->ei_quota_reserved);

	bch2_quota_acct(c, inode->ei_qid, Q_SPC,
			-((s64) res->sectors), KEY_TYPE_QUOTA_PREALLOC);
	inode->ei_quota_reserved -= res->sectors;
	mutex_unlock(&inode->ei_quota_lock);

	res->sectors = 0;
}

static int bch2_quota_reservation_add(struct bch_fs *c,
				      struct bch_inode_info *inode,
				      struct quota_res *res,
				      unsigned sectors,
				      bool check_enospc)
{
	int ret;

	mutex_lock(&inode->ei_quota_lock);
	ret = bch2_quota_acct(c, inode->ei_qid, Q_SPC, sectors,
			      check_enospc ? KEY_TYPE_QUOTA_PREALLOC : KEY_TYPE_QUOTA_NOCHECK);
	if (likely(!ret)) {
		inode->ei_quota_reserved += sectors;
		res->sectors += sectors;
	}
	mutex_unlock(&inode->ei_quota_lock);

	return ret;
}

#else

static void bch2_quota_reservation_put(struct bch_fs *c,
				       struct bch_inode_info *inode,
				       struct quota_res *res)
{
}

static int bch2_quota_reservation_add(struct bch_fs *c,
				      struct bch_inode_info *inode,
				      struct quota_res *res,
				      unsigned sectors,
				      bool check_enospc)
{
	return 0;
}

#endif

/* i_size updates: */

struct inode_new_size {
	loff_t		new_size;
	u64		now;
	unsigned	fields;
};

static int inode_set_size(struct bch_inode_info *inode,
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

static int __must_check bch2_write_inode_size(struct bch_fs *c,
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

static void i_sectors_acct(struct bch_fs *c, struct bch_inode_info *inode,
			   struct quota_res *quota_res, s64 sectors)
{
	if (!sectors)
		return;

	mutex_lock(&inode->ei_quota_lock);
#ifdef CONFIG_BCACHEFS_QUOTA
	if (quota_res && sectors > 0) {
		BUG_ON(sectors > quota_res->sectors);
		BUG_ON(sectors > inode->ei_quota_reserved);

		quota_res->sectors -= sectors;
		inode->ei_quota_reserved -= sectors;
	} else {
		bch2_quota_acct(c, inode->ei_qid, Q_SPC, sectors, KEY_TYPE_QUOTA_WARN);
	}
#endif
	inode->v.i_blocks += sectors;
	mutex_unlock(&inode->ei_quota_lock);
}

/* normal i_size/i_sectors update machinery: */

static int sum_sector_overwrites(struct btree_trans *trans,
				 struct btree_iter *extent_iter,
				 struct bkey_i *new, bool *allocating,
				 s64 *delta)
{
	struct btree_iter *iter;
	struct bkey_s_c old;

	*delta = 0;

	iter = bch2_trans_copy_iter(trans, extent_iter);
	if (IS_ERR(iter))
		return PTR_ERR(iter);

	old = bch2_btree_iter_peek_slot(iter);

	while (1) {
		/*
		 * should not be possible to get an error here, since we're
		 * carefully not advancing past @new and thus whatever leaf node
		 * @_iter currently points to:
		 */
		BUG_ON(bkey_err(old));

		if (allocating &&
		    !*allocating &&
		    bch2_bkey_nr_ptrs_allocated(old) <
		    bch2_bkey_nr_dirty_ptrs(bkey_i_to_s_c(new)))
			*allocating = true;

		*delta += (min(new->k.p.offset,
			      old.k->p.offset) -
			  max(bkey_start_offset(&new->k),
			      bkey_start_offset(old.k))) *
			(bkey_extent_is_allocation(&new->k) -
			 bkey_extent_is_allocation(old.k));

		if (bkey_cmp(old.k->p, new->k.p) >= 0)
			break;

		old = bch2_btree_iter_next_slot(iter);
	}

	bch2_trans_iter_free(trans, iter);
	return 0;
}

int bch2_extent_update(struct btree_trans *trans,
		       struct bch_inode_info *inode,
		       struct disk_reservation *disk_res,
		       struct quota_res *quota_res,
		       struct btree_iter *extent_iter,
		       struct bkey_i *k,
		       u64 new_i_size,
		       bool may_allocate,
		       bool direct,
		       s64 *total_delta)
{
	struct bch_fs *c = trans->c;
	struct btree_iter *inode_iter = NULL;
	struct bch_inode_unpacked inode_u;
	struct bkey_inode_buf inode_p;
	bool allocating = false;
	bool extended = false;
	bool inode_locked = false;
	s64 i_sectors_delta;
	int ret;

	ret = bch2_btree_iter_traverse(extent_iter);
	if (ret)
		return ret;

	bch2_extent_trim_atomic(k, extent_iter);

	ret = sum_sector_overwrites(trans, extent_iter,
				    k, &allocating,
				    &i_sectors_delta);
	if (ret)
		return ret;

	if (!may_allocate && allocating)
		return -ENOSPC;

	bch2_trans_update(trans, BTREE_INSERT_ENTRY(extent_iter, k));

	new_i_size = min(k->k.p.offset << 9, new_i_size);

	/* XXX: inode->i_size locking */
	if (i_sectors_delta ||
	    new_i_size > inode->ei_inode.bi_size) {
		if (c->opts.new_inode_updates) {
			bch2_trans_unlock(trans);
			mutex_lock(&inode->ei_update_lock);

			if (!bch2_trans_relock(trans)) {
				mutex_unlock(&inode->ei_update_lock);
				return -EINTR;
			}

			inode_locked = true;

			if (!inode->ei_inode_update)
				inode->ei_inode_update =
					bch2_deferred_update_alloc(c,
								BTREE_ID_INODES, 64);

			inode_u = inode->ei_inode;
			inode_u.bi_sectors += i_sectors_delta;

			/* XXX: this is slightly suspect */
			if (!(inode_u.bi_flags & BCH_INODE_I_SIZE_DIRTY) &&
			    new_i_size > inode_u.bi_size) {
				inode_u.bi_size = new_i_size;
				extended = true;
			}

			bch2_inode_pack(&inode_p, &inode_u);
			bch2_trans_update(trans,
				BTREE_INSERT_DEFERRED(inode->ei_inode_update,
						      &inode_p.inode.k_i));
		} else {
			inode_iter = bch2_trans_get_iter(trans,
				BTREE_ID_INODES,
				POS(k->k.p.inode, 0),
				BTREE_ITER_SLOTS|BTREE_ITER_INTENT);
			if (IS_ERR(inode_iter))
				return PTR_ERR(inode_iter);

			ret = bch2_btree_iter_traverse(inode_iter);
			if (ret)
				goto err;

			inode_u = inode->ei_inode;
			inode_u.bi_sectors += i_sectors_delta;

			/* XXX: this is slightly suspect */
			if (!(inode_u.bi_flags & BCH_INODE_I_SIZE_DIRTY) &&
			    new_i_size > inode_u.bi_size) {
				inode_u.bi_size = new_i_size;
				extended = true;
			}

			bch2_inode_pack(&inode_p, &inode_u);
			bch2_trans_update(trans,
				BTREE_INSERT_ENTRY(inode_iter, &inode_p.inode.k_i));
		}
	}

	ret = bch2_trans_commit(trans, disk_res,
				&inode->ei_journal_seq,
				BTREE_INSERT_NOFAIL|
				BTREE_INSERT_ATOMIC|
				BTREE_INSERT_NOUNLOCK|
				BTREE_INSERT_USE_RESERVE);
	if (ret)
		goto err;

	inode->ei_inode.bi_sectors += i_sectors_delta;

	EBUG_ON(i_sectors_delta &&
		inode->ei_inode.bi_sectors != inode_u.bi_sectors);

	if (extended) {
		inode->ei_inode.bi_size = new_i_size;

		if (direct) {
			spin_lock(&inode->v.i_lock);
			if (new_i_size > inode->v.i_size)
				i_size_write(&inode->v, new_i_size);
			spin_unlock(&inode->v.i_lock);
		}
	}

	if (direct)
		i_sectors_acct(c, inode, quota_res, i_sectors_delta);

	if (total_delta)
		*total_delta += i_sectors_delta;
err:
	if (!IS_ERR_OR_NULL(inode_iter))
		bch2_trans_iter_put(trans, inode_iter);
	if (inode_locked)
		mutex_unlock(&inode->ei_update_lock);

	return ret;
}

static int bchfs_write_index_update(struct bch_write_op *wop)
{
	struct bch_fs *c = wop->c;
	struct bchfs_write_op *op = container_of(wop,
				struct bchfs_write_op, op);
	struct quota_res *quota_res = op->is_dio
		? &container_of(op, struct dio_write, iop)->quota_res
		: NULL;
	struct bch_inode_info *inode = op->inode;
	struct keylist *keys = &op->op.insert_keys;
	struct bkey_i *k = bch2_keylist_front(keys);
	struct btree_trans trans;
	struct btree_iter *iter;
	int ret;

	BUG_ON(k->k.p.inode != inode->v.i_ino);

	bch2_trans_init(&trans, c, BTREE_ITER_MAX, 1024);

	iter = bch2_trans_get_iter(&trans,
				BTREE_ID_EXTENTS,
				bkey_start_pos(&k->k),
				BTREE_ITER_INTENT);

	do {
		BKEY_PADDED(k) tmp;

		bkey_copy(&tmp.k, bch2_keylist_front(keys));

		bch2_trans_begin_updates(&trans);

		ret = bch2_extent_update(&trans, inode,
				&wop->res, quota_res,
				iter, &tmp.k,
				op->new_i_size,
				!op->unalloc,
				op->is_dio,
				&op->sectors_added);
		if (ret == -EINTR)
			continue;
		if (ret)
			break;

		if (bkey_cmp(iter->pos, bch2_keylist_front(keys)->k.p) < 0)
			bch2_cut_front(iter->pos, bch2_keylist_front(keys));
		else
			bch2_keylist_pop_front(keys);
	} while (!bch2_keylist_empty(keys));

	bch2_trans_exit(&trans);

	return ret;
}

static inline void bch2_fswrite_op_init(struct bchfs_write_op *op,
					struct bch_fs *c,
					struct bch_inode_info *inode,
					struct bch_io_opts opts,
					bool is_dio)
{
	op->inode		= inode;
	op->sectors_added	= 0;
	op->is_dio		= is_dio;
	op->unalloc		= false;
	op->new_i_size		= U64_MAX;

	bch2_write_op_init(&op->op, c, opts);
	op->op.target		= opts.foreground_target;
	op->op.index_update_fn	= bchfs_write_index_update;
	op_journal_seq_set(&op->op, &inode->ei_journal_seq);
}

static inline struct bch_io_opts io_opts(struct bch_fs *c, struct bch_inode_info *inode)
{
	struct bch_io_opts opts = bch2_opts_to_inode_opts(c->opts);

	bch2_io_opts_apply(&opts, bch2_inode_opts_get(&inode->ei_inode));
	return opts;
}

/* page state: */

/* stored in page->private: */

struct bch_page_sector {
	/* Uncompressed, fully allocated replicas: */
	unsigned		nr_replicas:3;

	/* Owns PAGE_SECTORS * replicas_reserved sized reservation: */
	unsigned		replicas_reserved:3;

	/* i_sectors: */
	enum {
		SECTOR_UNALLOCATED,
		SECTOR_QUOTA_RESERVED,
		SECTOR_DIRTY,
		SECTOR_ALLOCATED,
	}			state:2;
};

struct bch_page_state {
	struct bch_page_sector	s[PAGE_SECTORS];
};

static inline struct bch_page_state *__bch2_page_state(struct page *page)
{
	return page_has_private(page)
		? (struct bch_page_state *) page_private(page)
		: NULL;
}

static inline struct bch_page_state *bch2_page_state(struct page *page)
{
	EBUG_ON(!PageLocked(page));

	return __bch2_page_state(page);
}

/* for newly allocated pages: */
static void __bch2_page_state_release(struct page *page)
{
	struct bch_page_state *s = __bch2_page_state(page);

	if (!s)
		return;

	ClearPagePrivate(page);
	set_page_private(page, 0);
	put_page(page);
	kfree(s);
}

static void bch2_page_state_release(struct page *page)
{
	struct bch_page_state *s = bch2_page_state(page);

	if (!s)
		return;

	ClearPagePrivate(page);
	set_page_private(page, 0);
	put_page(page);
	kfree(s);
}

/* for newly allocated pages: */
static struct bch_page_state *__bch2_page_state_create(struct page *page,
						       gfp_t gfp)
{
	struct bch_page_state *s;

	s = kzalloc(sizeof(*s), GFP_NOFS|gfp);
	if (!s)
		return NULL;

	/*
	 * migrate_page_move_mapping() assumes that pages with private data
	 * have their count elevated by 1.
	 */
	get_page(page);
	set_page_private(page, (unsigned long) s);
	SetPagePrivate(page);
	return s;
}

static struct bch_page_state *bch2_page_state_create(struct page *page,
						     gfp_t gfp)
{
	return bch2_page_state(page) ?: __bch2_page_state_create(page, gfp);
}

static void bch2_put_page_reservation(struct bch_fs *c, struct bch_inode_info *inode,
				      struct page *page)
{
	struct bch_page_state *s = bch2_page_state(page);
	struct disk_reservation disk_res = { 0 };
	struct quota_res quota_res = { 0 };
	unsigned i;

	if (!s)
		return;

	for (i = 0; i < ARRAY_SIZE(s->s); i++) {
		disk_res.sectors += s->s[i].replicas_reserved;
		s->s[i].replicas_reserved = 0;

		if (s->s[i].state == SECTOR_QUOTA_RESERVED) {
			quota_res.sectors++;
			s->s[i].state = SECTOR_UNALLOCATED;
		}
	}

	bch2_quota_reservation_put(c, inode, &quota_res);
	bch2_disk_reservation_put(c, &disk_res);
}

static inline unsigned inode_nr_replicas(struct bch_fs *c, struct bch_inode_info *inode)
{
	/* XXX: this should not be open coded */
	return inode->ei_inode.bi_data_replicas
		? inode->ei_inode.bi_data_replicas - 1
		: c->opts.data_replicas;
}

static inline unsigned sectors_to_reserve(struct bch_page_sector *s,
						  unsigned nr_replicas)
{
	return max(0, (int) nr_replicas -
		   s->nr_replicas -
		   s->replicas_reserved);
}

static int bch2_get_page_disk_reservation(struct bch_fs *c,
				struct bch_inode_info *inode,
				struct page *page, bool check_enospc)
{
	struct bch_page_state *s = bch2_page_state_create(page, 0);
	unsigned nr_replicas = inode_nr_replicas(c, inode);
	struct disk_reservation disk_res = { 0 };
	unsigned i, disk_res_sectors = 0;
	int ret;

	if (!s)
		return -ENOMEM;

	for (i = 0; i < ARRAY_SIZE(s->s); i++)
		disk_res_sectors += sectors_to_reserve(&s->s[i], nr_replicas);

	if (!disk_res_sectors)
		return 0;

	ret = bch2_disk_reservation_get(c, &disk_res,
					disk_res_sectors, 1,
					!check_enospc
					? BCH_DISK_RESERVATION_NOFAIL
					: 0);
	if (unlikely(ret))
		return ret;

	for (i = 0; i < ARRAY_SIZE(s->s); i++)
		s->s[i].replicas_reserved +=
			sectors_to_reserve(&s->s[i], nr_replicas);

	return 0;
}

static int bch2_get_page_quota_reservation(struct bch_fs *c,
			struct bch_inode_info *inode,
			struct page *page, bool check_enospc)
{
	struct bch_page_state *s = bch2_page_state_create(page, 0);
	struct quota_res quota_res = { 0 };
	unsigned i, quota_res_sectors = 0;
	int ret;

	if (!s)
		return -ENOMEM;

	for (i = 0; i < ARRAY_SIZE(s->s); i++)
		quota_res_sectors += s->s[i].state == SECTOR_UNALLOCATED;

	if (!quota_res_sectors)
		return 0;

	ret = bch2_quota_reservation_add(c, inode, &quota_res,
					 quota_res_sectors,
					 check_enospc);
	if (unlikely(ret))
		return ret;

	for (i = 0; i < ARRAY_SIZE(s->s); i++)
		if (s->s[i].state == SECTOR_UNALLOCATED)
			s->s[i].state = SECTOR_QUOTA_RESERVED;

	return 0;
}

static int bch2_get_page_reservation(struct bch_fs *c, struct bch_inode_info *inode,
				     struct page *page, bool check_enospc)
{
	return bch2_get_page_disk_reservation(c, inode, page, check_enospc) ?:
		bch2_get_page_quota_reservation(c, inode, page, check_enospc);
}

static void bch2_clear_page_bits(struct page *page)
{
	struct bch_inode_info *inode = to_bch_ei(page->mapping->host);
	struct bch_fs *c = inode->v.i_sb->s_fs_info;
	struct bch_page_state *s = bch2_page_state(page);
	int i, dirty_sectors = 0;

	if (!s)
		return;

	for (i = 0; i < ARRAY_SIZE(s->s); i++) {
		if (s->s[i].state == SECTOR_DIRTY) {
			dirty_sectors++;
			s->s[i].state = SECTOR_UNALLOCATED;
		}
	}

	if (dirty_sectors)
		i_sectors_acct(c, inode, NULL, -dirty_sectors);
	bch2_put_page_reservation(c, inode, page);

	bch2_page_state_release(page);
}

static void __bch2_set_page_dirty(struct address_space *mapping, struct folio *folio)
{
	struct bch_inode_info *inode = to_bch_ei(mapping->host);
	struct bch_fs *c = inode->v.i_sb->s_fs_info;
	struct bch_page_state *s = bch2_page_state(&folio->page);
	struct quota_res quota_res = { 0 };
	unsigned i, dirty_sectors = 0;

	BUG_ON(!s);

	for (i = 0; i < ARRAY_SIZE(s->s); i++) {
		if (s->s[i].state == SECTOR_QUOTA_RESERVED)
			quota_res.sectors++;

		if (s->s[i].state == SECTOR_UNALLOCATED ||
		    s->s[i].state == SECTOR_QUOTA_RESERVED) {
			s->s[i].state = SECTOR_DIRTY;
			dirty_sectors++;
		}
	}

	if (dirty_sectors)
		i_sectors_acct(c, inode, &quota_res, dirty_sectors);
	bch2_quota_reservation_put(c, inode, &quota_res);
}

static void bch2_set_page_dirty(struct address_space *mapping, struct page *page)
{
	struct folio *folio = page_folio(page);

	__bch2_set_page_dirty(mapping, folio);
	filemap_dirty_folio(mapping, folio);
}

vm_fault_t bch2_page_fault(struct vm_fault *vmf)
{
	struct file *file = vmf->vma->vm_file;
	struct bch_inode_info *inode = file_bch_inode(file);
	int ret;

	bch2_pagecache_add_get(&inode->ei_pagecache_lock);
	ret = filemap_fault(vmf);
	bch2_pagecache_add_put(&inode->ei_pagecache_lock);

	return ret;
}

vm_fault_t bch2_page_mkwrite(struct vm_fault *vmf)
{
	struct page *page = vmf->page;
	struct file *file = vmf->vma->vm_file;
	struct bch_inode_info *inode = file_bch_inode(file);
	struct address_space *mapping = file->f_mapping;
	struct bch_fs *c = inode->v.i_sb->s_fs_info;
	int ret = VM_FAULT_LOCKED;

	sb_start_pagefault(inode->v.i_sb);
	file_update_time(file);

	/*
	 * Not strictly necessary, but helps avoid dio writes livelocking in
	 * write_invalidate_inode_pages_range() - can drop this if/when we get
	 * a write_invalidate_inode_pages_range() that works without dropping
	 * page lock before invalidating page
	 */
	bch2_pagecache_add_get(&inode->ei_pagecache_lock);

	lock_page(page);
	if (page->mapping != mapping ||
	    page_offset(page) > i_size_read(&inode->v)) {
		unlock_page(page);
		ret = VM_FAULT_NOPAGE;
		goto out;
	}

	if (bch2_get_page_reservation(c, inode, page, true)) {
		unlock_page(page);
		ret = VM_FAULT_SIGBUS;
		goto out;
	}

	if (!PageDirty(page))
		bch2_set_page_dirty(mapping, page);
	wait_for_stable_page(page);
out:
	bch2_pagecache_add_put(&inode->ei_pagecache_lock);
	sb_end_pagefault(inode->v.i_sb);
	return ret;
}

void bch2_invalidate_folio(struct folio *folio, size_t offset, size_t length)
{
	if (offset || length < folio_size(folio))
		return;

	bch2_clear_page_bits(&folio->page);
}

bool bch2_release_folio(struct folio *folio, gfp_t gfp_mask)
{
	/* XXX: this can't take locks that are held while we allocate memory */
	EBUG_ON(!PageLocked(&folio->page));
	EBUG_ON(folio_test_writeback(folio));

	if (folio_test_dirty(folio) || folio_test_writeback(folio))
		return false;

	bch2_clear_page_bits(&folio->page);
	return true;
}

/* readpages/writepages: */

static bool bio_can_add_page_contig(struct bio *bio, struct page *page)
{
	sector_t offset = (sector_t) page->index << PAGE_SECTOR_SHIFT;

	return bio->bi_vcnt < bio->bi_max_vecs &&
		bio_end_sector(bio) == offset;
}

static int bio_add_page_contig(struct bio *bio, struct page *page)
{
	sector_t offset = (sector_t) page->index << PAGE_SECTOR_SHIFT;

	EBUG_ON(!bio->bi_max_vecs);

	if (!bio->bi_vcnt)
		bio->bi_iter.bi_sector = offset;
	else if (!bio_can_add_page_contig(bio, page))
		return -1;

	BUG_ON(!bio_add_page(bio, page, PAGE_SIZE, 0));
	return 0;
}

/* readpage(s): */

static void bch2_readpages_end_io(struct bio *bio)
{
	struct bvec_iter_all iter;
	struct bio_vec *bv;

	bio_for_each_segment_all(bv, bio, iter) {
		struct page *page = bv->bv_page;

		if (!bio->bi_status) {
			SetPageUptodate(page);
		} else {
			ClearPageUptodate(page);
			SetPageError(page);
		}
		unlock_page(page);
	}

	bio_put(bio);
}

static inline void page_state_init_for_read(struct page *page)
{
	SetPagePrivate(page);
	page->private = 0;
}

struct readpages_iter {
	struct address_space	*mapping;
	struct page		**pages;
	unsigned		nr_pages;
	unsigned		idx;
	pgoff_t			offset;
};

static int readpages_iter_init(struct readpages_iter *iter,
			       struct readahead_control *ractl)
{
	unsigned i, nr_pages = readahead_count(ractl);

	memset(iter, 0, sizeof(*iter));

	iter->mapping	= ractl->mapping;
	iter->offset	= readahead_index(ractl);
	iter->nr_pages	= nr_pages;

	iter->pages = kmalloc_array(nr_pages, sizeof(struct page *), GFP_NOFS);
	if (!iter->pages)
		return -ENOMEM;

	__readahead_batch(ractl, iter->pages, nr_pages);
	for (i = 0; i < nr_pages; i++) {
		__bch2_page_state_create(iter->pages[i], __GFP_NOFAIL);
		put_page(iter->pages[i]);
	}

	return 0;
}

static inline struct page *readpage_iter_next(struct readpages_iter *iter)
{
	if (iter->idx >= iter->nr_pages)
		return NULL;

	EBUG_ON(iter->pages[iter->idx]->index != iter->offset + iter->idx);

	return iter->pages[iter->idx];
}

static void bch2_add_page_sectors(struct bio *bio, struct bkey_s_c k)
{
	struct bvec_iter iter;
	struct bio_vec bv;
	unsigned nr_ptrs = bch2_bkey_nr_ptrs_allocated(k);

	BUG_ON(bio->bi_iter.bi_sector	< bkey_start_offset(k.k));
	BUG_ON(bio_end_sector(bio)	> k.k->p.offset);


	bio_for_each_segment(bv, bio, iter) {
		struct bch_page_state *s = bch2_page_state(bv.bv_page);
		unsigned i;

		for (i = bv.bv_offset >> 9;
		     i < (bv.bv_offset + bv.bv_len) >> 9;
		     i++) {
			s->s[i].nr_replicas = nr_ptrs;
			s->s[i].state = SECTOR_ALLOCATED;
		}
	}
}

static void readpage_bio_extend(struct readpages_iter *iter,
				struct bio *bio, u64 offset,
				bool get_more)
{
	while (bio_end_sector(bio) < offset &&
	       bio->bi_vcnt < bio->bi_max_vecs) {
		pgoff_t page_offset = bio_end_sector(bio) >> PAGE_SECTOR_SHIFT;
		struct page *page = readpage_iter_next(iter);
		int ret;

		if (page) {
			if (iter->offset + iter->idx != page_offset)
				break;

			iter->idx++;
		} else {
			if (!get_more)
				break;

			page = xa_load(&iter->mapping->i_pages, page_offset);
			if (page && !xa_is_value(page))
				break;

			page = __page_cache_alloc(readahead_gfp_mask(iter->mapping));
			if (!page)
				break;

			if (!__bch2_page_state_create(page, 0)) {
				put_page(page);
				break;
			}

			ret = add_to_page_cache_lru(page, iter->mapping,
						    page_offset, GFP_NOFS);
			if (ret) {
				__bch2_page_state_release(page);
				put_page(page);
				break;
			}

			put_page(page);
		}

		BUG_ON(!bio_add_page(bio, page, PAGE_SIZE, 0));
	}
}

static void bchfs_read(struct btree_trans *trans, struct btree_iter *iter,
		       struct bch_read_bio *rbio, u64 inum,
		       struct readpages_iter *readpages_iter)
{
	struct bch_fs *c = trans->c;
	int flags = BCH_READ_RETRY_IF_STALE|
		BCH_READ_MAY_PROMOTE;

	rbio->c = c;
	rbio->start_time = local_clock();

	while (1) {
		BKEY_PADDED(k) tmp;
		struct bkey_s_c k;
		unsigned bytes, offset_into_extent;

		bch2_btree_iter_set_pos(iter,
				POS(inum, rbio->bio.bi_iter.bi_sector));

		k = bch2_btree_iter_peek_slot(iter);
		BUG_ON(!k.k);

		if (IS_ERR(k.k)) {
			int ret = btree_iter_err(iter);
			BUG_ON(!ret);
			bcache_io_error(c, &rbio->bio, "btree IO error %i", ret);
			bio_endio(&rbio->bio);
			return;
		}

		bkey_reassemble(&tmp.k, k);
		bch2_trans_unlock(trans);
		k = bkey_i_to_s_c(&tmp.k);

		offset_into_extent = iter->pos.offset -
			bkey_start_offset(k.k);

		if (readpages_iter) {
			bool want_full_extent = false;

			if (bkey_extent_is_data(k.k)) {
				struct bkey_s_c_extent e = bkey_s_c_to_extent(k);
				const union bch_extent_entry *i;
				struct extent_ptr_decoded p;

				extent_for_each_ptr_decode(e, p, i)
					want_full_extent |= ((p.crc.csum_type != 0) |
							     (p.crc.compression_type != 0));
			}

			readpage_bio_extend(readpages_iter,
					    &rbio->bio, k.k->p.offset,
					    want_full_extent);
		}

		bytes = min_t(unsigned, bio_sectors(&rbio->bio),
			      (k.k->size - offset_into_extent)) << 9;
		swap(rbio->bio.bi_iter.bi_size, bytes);

		if (rbio->bio.bi_iter.bi_size == bytes)
			flags |= BCH_READ_LAST_FRAGMENT;

		if (bkey_extent_is_allocation(k.k))
			bch2_add_page_sectors(&rbio->bio, k);

		bch2_read_extent(c, rbio, k, offset_into_extent, flags);

		if (flags & BCH_READ_LAST_FRAGMENT)
			return;

		swap(rbio->bio.bi_iter.bi_size, bytes);
		bio_advance(&rbio->bio, bytes);
	}
}

void bch2_readahead(struct readahead_control *ractl)
{
	struct bch_inode_info *inode = to_bch_ei(ractl->mapping->host);
	struct bch_fs *c = inode->v.i_sb->s_fs_info;
	struct bch_io_opts opts = io_opts(c, inode);
	struct btree_trans trans;
	struct btree_iter *iter;
	struct page *page;
	struct readpages_iter readpages_iter;
	int ret;

	ret = readpages_iter_init(&readpages_iter, ractl);
	BUG_ON(ret);

	bch2_trans_init(&trans, c, 0, 0);

	iter = bch2_trans_get_iter(&trans, BTREE_ID_EXTENTS, POS_MIN,
				   BTREE_ITER_SLOTS);

	bch2_pagecache_add_get(&inode->ei_pagecache_lock);

	while ((page = readpage_iter_next(&readpages_iter))) {
		pgoff_t index = readpages_iter.offset + readpages_iter.idx;
		unsigned n = min_t(unsigned,
				   readpages_iter.nr_pages -
				   readpages_iter.idx,
				   BIO_MAX_VECS);
		struct bch_read_bio *rbio =
			rbio_init(bio_alloc_bioset(NULL, n, REQ_OP_READ,
						   GFP_NOFS, &c->bio_read),
				  opts);

		readpages_iter.idx++;

		rbio->bio.bi_iter.bi_sector = (sector_t) index << PAGE_SECTOR_SHIFT;
		rbio->bio.bi_end_io = bch2_readpages_end_io;
		BUG_ON(!bio_add_page(&rbio->bio, page, PAGE_SIZE, 0));

		bchfs_read(&trans, iter, rbio, inode->v.i_ino,
			   &readpages_iter);
	}

	bch2_pagecache_add_put(&inode->ei_pagecache_lock);

	bch2_trans_exit(&trans);
	kfree(readpages_iter.pages);
}

static void __bchfs_readpage(struct bch_fs *c, struct bch_read_bio *rbio,
			     u64 inum, struct page *page)
{
	struct btree_trans trans;
	struct btree_iter *iter;

	bch2_page_state_create(page, __GFP_NOFAIL);

	rbio->bio.bi_opf = REQ_OP_READ|REQ_SYNC;
	bio_add_page_contig(&rbio->bio, page);

	bch2_trans_init(&trans, c, 0, 0);
	iter = bch2_trans_get_iter(&trans, BTREE_ID_EXTENTS, POS_MIN,
				   BTREE_ITER_SLOTS);

	bchfs_read(&trans, iter, rbio, inum, NULL);

	bch2_trans_exit(&trans);
}

static void bch2_read_single_page_end_io(struct bio *bio)
{
	complete(bio->bi_private);
}

static int bch2_read_single_page(struct page *page,
				 struct address_space *mapping)
{
	struct bch_inode_info *inode = to_bch_ei(mapping->host);
	struct bch_fs *c = inode->v.i_sb->s_fs_info;
	struct bch_read_bio *rbio;
	int ret;
	DECLARE_COMPLETION_ONSTACK(done);

	rbio = rbio_init(bio_alloc_bioset(NULL, 1, REQ_OP_READ, GFP_NOFS, &c->bio_read),
			 io_opts(c, inode));
	rbio->bio.bi_private = &done;
	rbio->bio.bi_end_io = bch2_read_single_page_end_io;

	__bchfs_readpage(c, rbio, inode->v.i_ino, page);
	wait_for_completion(&done);

	ret = blk_status_to_errno(rbio->bio.bi_status);
	bio_put(&rbio->bio);

	if (ret < 0)
		return ret;

	SetPageUptodate(page);
	return 0;
}

int bch2_read_folio(struct file *file, struct folio *folio)
{
	struct page *page = &folio->page;
	int ret;

	ret = bch2_read_single_page(page, page->mapping);
	folio_unlock(folio);
	return ret;
}

/* writepages: */

struct bch_writepage_state {
	struct bch_writepage_io	*io;
	struct bch_io_opts	opts;
};

static inline struct bch_writepage_state bch_writepage_state_init(struct bch_fs *c,
								  struct bch_inode_info *inode)
{
	return (struct bch_writepage_state) { .opts = io_opts(c, inode) };
}

static void bch2_writepage_io_free(struct closure *cl)
{
	struct bch_writepage_io *io = container_of(cl,
					struct bch_writepage_io, cl);

	bio_put(&io->op.op.wbio.bio);
}

static void bch2_writepage_io_done(struct closure *cl)
{
	struct bch_writepage_io *io = container_of(cl,
					struct bch_writepage_io, cl);
	struct bch_fs *c = io->op.op.c;
	struct bio *bio = &io->op.op.wbio.bio;
	struct bvec_iter_all iter;
	struct bio_vec *bvec;

	if (io->op.op.error) {
		bio_for_each_segment_all(bvec, bio, iter) {
			SetPageError(bvec->bv_page);
			mapping_set_error(bvec->bv_page->mapping, -EIO);
		}
	}

	/*
	 * racing with fallocate can cause us to add fewer sectors than
	 * expected - but we shouldn't add more sectors than expected:
	 */
	BUG_ON(io->op.sectors_added > (s64) io->new_sectors);

	/*
	 * (error (due to going RO) halfway through a page can screw that up
	 * slightly)
	 * XXX wtf?
	   BUG_ON(io->op.sectors_added - io->new_sectors >= (s64) PAGE_SECTORS);
	 */

	/*
	 * PageWriteback is effectively our ref on the inode - fixup i_blocks
	 * before calling end_page_writeback:
	 */
	if (io->op.sectors_added != io->new_sectors)
		i_sectors_acct(c, io->op.inode, NULL,
			       io->op.sectors_added - (s64) io->new_sectors);

	bio_for_each_segment_all(bvec, bio, iter)
		end_page_writeback(bvec->bv_page);

	closure_return_with_destructor(&io->cl, bch2_writepage_io_free);
}

static void bch2_writepage_do_io(struct bch_writepage_state *w)
{
	struct bch_writepage_io *io = w->io;

	w->io = NULL;
	closure_call(&io->op.op.cl, bch2_write, NULL, &io->cl);
	continue_at(&io->cl, bch2_writepage_io_done, NULL);
}

/*
 * Get a bch_writepage_io and add @page to it - appending to an existing one if
 * possible, else allocating a new one:
 */
static void bch2_writepage_io_alloc(struct bch_fs *c,
				    struct bch_writepage_state *w,
				    struct bch_inode_info *inode,
				    struct page *page,
				    unsigned nr_replicas)
{
	struct bch_write_op *op;
	u64 offset = (u64) page->index << PAGE_SECTOR_SHIFT;

	w->io = container_of(bio_alloc_bioset(NULL, BIO_MAX_VECS,
					      REQ_OP_WRITE,
					      GFP_NOFS,
					      &c->writepage_bioset),
			     struct bch_writepage_io, op.op.wbio.bio);

	closure_init(&w->io->cl, NULL);
	w->io->new_sectors	= 0;
	bch2_fswrite_op_init(&w->io->op, c, inode, w->opts, false);
	op			= &w->io->op.op;
	op->nr_replicas		= nr_replicas;
	op->res.nr_replicas	= nr_replicas;
	op->write_point		= writepoint_hashed(inode->ei_last_dirtied);
	op->pos			= POS(inode->v.i_ino, offset);
	op->wbio.bio.bi_iter.bi_sector = offset;
}

static int __bch2_writepage(struct folio *folio,
			    struct writeback_control *wbc,
			    void *data)
{
	struct page *page = &folio->page;
	struct bch_inode_info *inode = to_bch_ei(page->mapping->host);
	struct bch_fs *c = inode->v.i_sb->s_fs_info;
	struct bch_writepage_state *w = data;
	struct bch_page_state *s;
	unsigned offset, nr_replicas_this_write = U32_MAX;
	unsigned dirty_sectors = 0, reserved_sectors = 0;
	loff_t i_size = i_size_read(&inode->v);
	pgoff_t end_index = i_size >> PAGE_SHIFT;
	unsigned i;
	int ret;

	EBUG_ON(!PageUptodate(page));

	/* Is the page fully inside i_size? */
	if (page->index < end_index)
		goto do_io;

	/* Is the page fully outside i_size? (truncate in progress) */
	offset = i_size & (PAGE_SIZE - 1);
	if (page->index > end_index || !offset) {
		unlock_page(page);
		return 0;
	}

	/*
	 * The page straddles i_size.  It must be zeroed out on each and every
	 * writepage invocation because it may be mmapped.  "A file is mapped
	 * in multiples of the page size.  For a file that is not a multiple of
	 * the  page size, the remaining memory is zeroed when mapped, and
	 * writes to that region are not written out to the file."
	 */
	zero_user_segment(page, offset, PAGE_SIZE);
do_io:
	s = bch2_page_state_create(page, __GFP_NOFAIL);

	ret = bch2_get_page_disk_reservation(c, inode, page, true);
	if (ret) {
		SetPageError(page);
		mapping_set_error(page->mapping, ret);
		unlock_page(page);
		return 0;
	}

	for (i = 0; i < PAGE_SECTORS; i++)
		nr_replicas_this_write =
			min_t(unsigned, nr_replicas_this_write,
			      s->s[i].nr_replicas +
			      s->s[i].replicas_reserved);

	/* Before unlocking the page, transfer reservation to w->io: */

	for (i = 0; i < PAGE_SECTORS; i++) {
		s->s[i].nr_replicas = w->opts.compression
			? 0 : nr_replicas_this_write;

		reserved_sectors += s->s[i].replicas_reserved;
		s->s[i].replicas_reserved = 0;

		dirty_sectors += s->s[i].state == SECTOR_DIRTY;
		s->s[i].state = SECTOR_ALLOCATED;
	}

	BUG_ON(PageWriteback(page));
	set_page_writeback(page);
	unlock_page(page);

	if (w->io &&
	    (w->io->op.op.res.nr_replicas != nr_replicas_this_write ||
	     !bio_can_add_page_contig(&w->io->op.op.wbio.bio, page)))
		bch2_writepage_do_io(w);

	if (!w->io)
		bch2_writepage_io_alloc(c, w, inode, page,
					nr_replicas_this_write);

	w->io->new_sectors += dirty_sectors;

	BUG_ON(inode != w->io->op.inode);
	BUG_ON(bio_add_page_contig(&w->io->op.op.wbio.bio, page));

	w->io->op.op.res.sectors += reserved_sectors;
	w->io->op.new_i_size = i_size;

	if (wbc->sync_mode == WB_SYNC_ALL)
		w->io->op.op.wbio.bio.bi_opf |= REQ_SYNC;

	return 0;
}

int bch2_writepages(struct address_space *mapping, struct writeback_control *wbc)
{
	struct bch_fs *c = mapping->host->i_sb->s_fs_info;
	struct bch_writepage_state w =
		bch_writepage_state_init(c, to_bch_ei(mapping->host));
	struct blk_plug plug;
	int ret;

	blk_start_plug(&plug);
	ret = write_cache_pages(mapping, wbc, __bch2_writepage, &w);
	if (w.io)
		bch2_writepage_do_io(&w);
	blk_finish_plug(&plug);
	return ret;
}

int bch2_writepage(struct page *page, struct writeback_control *wbc)
{
	struct bch_fs *c = page->mapping->host->i_sb->s_fs_info;
	struct bch_writepage_state w =
		bch_writepage_state_init(c, to_bch_ei(page->mapping->host));
	int ret;

	ret = __bch2_writepage(page_folio(page), wbc, &w);
	if (w.io)
		bch2_writepage_do_io(&w);

	return ret;
}

/* buffered writes: */

int bch2_write_begin(struct file *file, struct address_space *mapping,
		     loff_t pos, unsigned len,
		     struct page **pagep, void **fsdata)
{
	struct bch_inode_info *inode = to_bch_ei(mapping->host);
	struct bch_fs *c = inode->v.i_sb->s_fs_info;
	pgoff_t index = pos >> PAGE_SHIFT;
	unsigned offset = pos & (PAGE_SIZE - 1);
	struct page *page;
	int ret = -ENOMEM;

	BUG_ON(inode_unhashed(&inode->v));

	bch2_pagecache_add_get(&inode->ei_pagecache_lock);

	page = grab_cache_page_write_begin(mapping, index);
	if (!page)
		goto err_unlock;

	if (PageUptodate(page))
		goto out;

	/* If we're writing entire page, don't need to read it in first: */
	if (len == PAGE_SIZE)
		goto out;

	if (!offset && pos + len >= inode->v.i_size) {
		zero_user_segment(page, len, PAGE_SIZE);
		flush_dcache_page(page);
		goto out;
	}

	if (index > inode->v.i_size >> PAGE_SHIFT) {
		zero_user_segments(page, 0, offset, offset + len, PAGE_SIZE);
		flush_dcache_page(page);
		goto out;
	}
readpage:
	ret = bch2_read_single_page(page, mapping);
	if (ret)
		goto err;
out:
	ret = bch2_get_page_reservation(c, inode, page, true);
	if (ret) {
		if (!PageUptodate(page)) {
			/*
			 * If the page hasn't been read in, we won't know if we
			 * actually need a reservation - we don't actually need
			 * to read here, we just need to check if the page is
			 * fully backed by uncompressed data:
			 */
			goto readpage;
		}

		goto err;
	}

	*pagep = page;
	return 0;
err:
	unlock_page(page);
	put_page(page);
	*pagep = NULL;
err_unlock:
	bch2_pagecache_add_put(&inode->ei_pagecache_lock);
	return ret;
}

int bch2_write_end(struct file *file, struct address_space *mapping,
		   loff_t pos, unsigned len, unsigned copied,
		   struct page *page, void *fsdata)
{
	struct bch_inode_info *inode = to_bch_ei(mapping->host);
	struct bch_fs *c = inode->v.i_sb->s_fs_info;

	lockdep_assert_held(&inode->v.i_rwsem);

	if (unlikely(copied < len && !PageUptodate(page))) {
		/*
		 * The page needs to be read in, but that would destroy
		 * our partial write - simplest thing is to just force
		 * userspace to redo the write:
		 */
		zero_user(page, 0, PAGE_SIZE);
		flush_dcache_page(page);
		copied = 0;
	}

	spin_lock(&inode->v.i_lock);
	if (pos + copied > inode->v.i_size)
		i_size_write(&inode->v, pos + copied);
	spin_unlock(&inode->v.i_lock);

	if (copied) {
		if (!PageUptodate(page))
			SetPageUptodate(page);
		if (!PageDirty(page))
			bch2_set_page_dirty(mapping, page);

		inode->ei_last_dirtied = (unsigned long) current;
	} else {
		bch2_put_page_reservation(c, inode, page);
	}

	unlock_page(page);
	put_page(page);
	bch2_pagecache_add_put(&inode->ei_pagecache_lock);

	return copied;
}

#define WRITE_BATCH_PAGES	32

static int __bch2_buffered_write(struct bch_inode_info *inode,
				 struct address_space *mapping,
				 struct iov_iter *iter,
				 loff_t pos, unsigned len)
{
	struct bch_fs *c = inode->v.i_sb->s_fs_info;
	struct page *pages[WRITE_BATCH_PAGES];
	unsigned long index = pos >> PAGE_SHIFT;
	unsigned offset = pos & (PAGE_SIZE - 1);
	unsigned nr_pages = DIV_ROUND_UP(offset + len, PAGE_SIZE);
	unsigned i, copied = 0, nr_pages_copied = 0;
	int ret = 0;

	BUG_ON(!len);
	BUG_ON(nr_pages > ARRAY_SIZE(pages));

	for (i = 0; i < nr_pages; i++) {
		pages[i] = grab_cache_page_write_begin(mapping, index + i);
		if (!pages[i]) {
			nr_pages = i;
			ret = -ENOMEM;
			goto out;
		}
	}

	if (offset && !PageUptodate(pages[0])) {
		ret = bch2_read_single_page(pages[0], mapping);
		if (ret)
			goto out;
	}

	if ((pos + len) & (PAGE_SIZE - 1) &&
	    !PageUptodate(pages[nr_pages - 1])) {
		if ((index + nr_pages - 1) << PAGE_SHIFT >= inode->v.i_size) {
			zero_user(pages[nr_pages - 1], 0, PAGE_SIZE);
		} else {
			ret = bch2_read_single_page(pages[nr_pages - 1], mapping);
			if (ret)
				goto out;
		}
	}

	for (i = 0; i < nr_pages; i++) {
		ret = bch2_get_page_reservation(c, inode, pages[i], true);

		if (ret && !PageUptodate(pages[i])) {
			ret = bch2_read_single_page(pages[i], mapping);
			if (ret)
				goto out;

			ret = bch2_get_page_reservation(c, inode, pages[i], true);
		}

		if (ret)
			goto out;
	}

	if (mapping_writably_mapped(mapping))
		for (i = 0; i < nr_pages; i++)
			flush_dcache_page(pages[i]);

	while (copied < len) {
		struct page *page = pages[(offset + copied) >> PAGE_SHIFT];
		unsigned pg_offset = (offset + copied) & (PAGE_SIZE - 1);
		unsigned pg_bytes = min_t(unsigned, len - copied,
					  PAGE_SIZE - pg_offset);
		unsigned pg_copied = copy_page_from_iter_atomic(page,
						pg_offset, pg_bytes, iter);

		flush_dcache_page(page);
		copied += pg_copied;

		if (pg_copied != pg_bytes)
			break;
	}

	if (!copied)
		goto out;

	nr_pages_copied = DIV_ROUND_UP(offset + copied, PAGE_SIZE);
	inode->ei_last_dirtied = (unsigned long) current;

	spin_lock(&inode->v.i_lock);
	if (pos + copied > inode->v.i_size)
		i_size_write(&inode->v, pos + copied);
	spin_unlock(&inode->v.i_lock);

	if (copied < len &&
	    ((offset + copied) & (PAGE_SIZE - 1))) {
		struct page *page = pages[(offset + copied) >> PAGE_SHIFT];

		if (!PageUptodate(page)) {
			zero_user(page, 0, PAGE_SIZE);
			copied -= (offset + copied) & (PAGE_SIZE - 1);
		}
	}
out:
	for (i = 0; i < nr_pages_copied; i++) {
		if (!PageUptodate(pages[i]))
			SetPageUptodate(pages[i]);
		if (!PageDirty(pages[i]))
			bch2_set_page_dirty(mapping, pages[i]);
		unlock_page(pages[i]);
		put_page(pages[i]);
	}

	for (i = nr_pages_copied; i < nr_pages; i++) {
		if (!PageDirty(pages[i]))
			bch2_put_page_reservation(c, inode, pages[i]);
		unlock_page(pages[i]);
		put_page(pages[i]);
	}

	return copied ?: ret;
}

static ssize_t bch2_buffered_write(struct kiocb *iocb, struct iov_iter *iter)
{
	struct file *file = iocb->ki_filp;
	struct address_space *mapping = file->f_mapping;
	struct bch_inode_info *inode = file_bch_inode(file);
	loff_t pos = iocb->ki_pos;
	ssize_t written = 0;
	int ret = 0;

	bch2_pagecache_add_get(&inode->ei_pagecache_lock);

	do {
		unsigned offset = pos & (PAGE_SIZE - 1);
		unsigned bytes = min_t(unsigned long, iov_iter_count(iter),
			      PAGE_SIZE * WRITE_BATCH_PAGES - offset);
again:
		/*
		 * Bring in the user page that we will copy from _first_.
		 * Otherwise there's a nasty deadlock on copying from the
		 * same page as we're writing to, without it being marked
		 * up-to-date.
		 *
		 * Not only is this an optimisation, but it is also required
		 * to check that the address is actually valid, when atomic
		 * usercopies are used, below.
		 */
		if (unlikely(fault_in_iov_iter_readable(iter, bytes))) {
			bytes = min_t(unsigned long, iov_iter_count(iter),
				      PAGE_SIZE - offset);

			if (unlikely(fault_in_iov_iter_readable(iter, bytes))) {
				ret = -EFAULT;
				break;
			}
		}

		if (unlikely(fatal_signal_pending(current))) {
			ret = -EINTR;
			break;
		}

		ret = __bch2_buffered_write(inode, mapping, iter, pos, bytes);
		if (unlikely(ret < 0))
			break;

		cond_resched();

		if (unlikely(ret == 0)) {
			/*
			 * If we were unable to copy any data at all, we must
			 * fall back to a single segment length write.
			 *
			 * If we didn't fallback here, we could livelock
			 * because not all segments in the iov can be copied at
			 * once without a pagefault.
			 */
			bytes = min_t(unsigned long, PAGE_SIZE - offset,
				      iov_iter_single_seg_count(iter));
			goto again;
		}
		pos += ret;
		written += ret;

		balance_dirty_pages_ratelimited(mapping);
	} while (iov_iter_count(iter));

	bch2_pagecache_add_put(&inode->ei_pagecache_lock);

	return written ? written : ret;
}

/* O_DIRECT reads */

static void bch2_dio_read_complete(struct closure *cl)
{
	struct dio_read *dio = container_of(cl, struct dio_read, cl);

	dio->req->ki_complete(dio->req, dio->ret);
	bio_check_pages_dirty(&dio->rbio.bio);	/* transfers ownership */
}

static void bch2_direct_IO_read_endio(struct bio *bio)
{
	struct dio_read *dio = bio->bi_private;

	if (bio->bi_status)
		dio->ret = blk_status_to_errno(bio->bi_status);

	closure_put(&dio->cl);
}

static void bch2_direct_IO_read_split_endio(struct bio *bio)
{
	bch2_direct_IO_read_endio(bio);
	bio_check_pages_dirty(bio);	/* transfers ownership */
}

static int bch2_direct_IO_read(struct kiocb *req, struct iov_iter *iter)
{
	struct file *file = req->ki_filp;
	struct bch_inode_info *inode = file_bch_inode(file);
	struct bch_fs *c = inode->v.i_sb->s_fs_info;
	struct bch_io_opts opts = io_opts(c, inode);
	struct dio_read *dio;
	struct bio *bio;
	loff_t offset = req->ki_pos;
	bool sync = is_sync_kiocb(req);
	size_t shorten;
	ssize_t ret;

	if ((offset|iter->count) & (block_bytes(c) - 1))
		return -EINVAL;

	ret = min_t(loff_t, iter->count,
		    max_t(loff_t, 0, i_size_read(&inode->v) - offset));

	if (!ret)
		return ret;

	shorten = iov_iter_count(iter) - round_up(ret, block_bytes(c));
	iter->count -= shorten;

	bio = bio_alloc_bioset(NULL,
			       iov_iter_npages(iter, BIO_MAX_VECS),
			       REQ_OP_READ,
			       GFP_KERNEL,
			       &c->dio_read_bioset);

	bio->bi_end_io = bch2_direct_IO_read_endio;

	dio = container_of(bio, struct dio_read, rbio.bio);
	closure_init(&dio->cl, NULL);

	/*
	 * this is a _really_ horrible hack just to avoid an atomic sub at the
	 * end:
	 */
	if (!sync) {
		set_closure_fn(&dio->cl, bch2_dio_read_complete, NULL);
		atomic_set(&dio->cl.remaining,
			   CLOSURE_REMAINING_INITIALIZER -
			   CLOSURE_RUNNING +
			   CLOSURE_DESTRUCTOR);
	} else {
		atomic_set(&dio->cl.remaining,
			   CLOSURE_REMAINING_INITIALIZER + 1);
	}

	dio->req	= req;
	dio->ret	= ret;

	goto start;
	while (iter->count) {
		bio = bio_alloc_bioset(NULL,
				       iov_iter_npages(iter, BIO_MAX_VECS),
				       REQ_OP_READ,
				       GFP_KERNEL,
				       &c->bio_read);
		bio->bi_end_io		= bch2_direct_IO_read_split_endio;
start:
		bio->bi_opf		= REQ_OP_READ|REQ_SYNC;
		bio->bi_iter.bi_sector	= offset >> 9;
		bio->bi_private		= dio;

		ret = bio_iov_iter_get_pages(bio, iter);
		if (ret < 0) {
			/* XXX: fault inject this path */
			bio->bi_status = BLK_STS_RESOURCE;
			bio_endio(bio);
			break;
		}

		offset += bio->bi_iter.bi_size;
		bio_set_pages_dirty(bio);

		if (iter->count)
			closure_get(&dio->cl);

		bch2_read(c, rbio_init(bio, opts), inode->v.i_ino);
	}

	iter->count += shorten;

	if (sync) {
		closure_sync(&dio->cl);
		closure_debug_destroy(&dio->cl);
		ret = dio->ret;
		bio_check_pages_dirty(&dio->rbio.bio); /* transfers ownership */
		return ret;
	} else {
		return -EIOCBQUEUED;
	}
}

ssize_t bch2_read_iter(struct kiocb *iocb, struct iov_iter *iter)
{
	struct file *file = iocb->ki_filp;
	struct bch_inode_info *inode = file_bch_inode(file);
	struct address_space *mapping = file->f_mapping;
	size_t count = iov_iter_count(iter);
	ssize_t ret;

	if (!count)
		return 0; /* skip atime */

	if (iocb->ki_flags & IOCB_DIRECT) {
		struct blk_plug plug;

		ret = filemap_write_and_wait_range(mapping,
					iocb->ki_pos,
					iocb->ki_pos + count - 1);
		if (ret < 0)
			return ret;

		file_accessed(file);

		blk_start_plug(&plug);
		ret = bch2_direct_IO_read(iocb, iter);
		blk_finish_plug(&plug);

		if (ret >= 0)
			iocb->ki_pos += ret;
	} else {
		bch2_pagecache_add_get(&inode->ei_pagecache_lock);
		ret = generic_file_read_iter(iocb, iter);
		bch2_pagecache_add_put(&inode->ei_pagecache_lock);
	}

	return ret;
}

/* O_DIRECT writes */

/*
 * We're going to return -EIOCBQUEUED, but we haven't finished consuming the
 * iov_iter yet, so we need to stash a copy of the iovec: it might be on the
 * caller's stack, we're not guaranteed that it will live for the duration of
 * the IO:
 */
static noinline int bch2_dio_write_copy_iov(struct dio_write *dio)
{
	struct iovec *iov = dio->inline_vecs;

	/*
	 * iov_iter has a single embedded iovec - nothing to do:
	 */
	if (iter_is_ubuf(&dio->iter))
		return 0;

	/*
	 * We don't currently handle non-iovec iov_iters here - return an error,
	 * and we'll fall back to doing the IO synchronously:
	 */
	if (!iter_is_iovec(&dio->iter))
		return -1;

	if (dio->iter.nr_segs > ARRAY_SIZE(dio->inline_vecs)) {
		iov = kmalloc_array(dio->iter.nr_segs, sizeof(*iov),
				    GFP_KERNEL);
		if (unlikely(!iov))
			return -ENOMEM;

		dio->free_iov = true;
	}

	memcpy(iov, dio->iter.__iov, dio->iter.nr_segs * sizeof(*iov));
	dio->iter.__iov = iov;
	return 0;
}

static void bch2_dio_write_loop_async(struct closure *);

static long bch2_dio_write_loop(struct dio_write *dio)
{
	bool kthread = (current->flags & PF_KTHREAD) != 0;
	struct kiocb *req = dio->req;
	struct address_space *mapping = req->ki_filp->f_mapping;
	struct bch_inode_info *inode = dio->iop.inode;
	struct bio *bio = &dio->iop.op.wbio.bio;
	struct bvec_iter_all iter;
	struct bio_vec *bv;
	loff_t offset;
	bool sync;
	long ret;

	if (dio->loop)
		goto loop;

	inode_dio_begin(&inode->v);
	bch2_pagecache_block_get(&inode->ei_pagecache_lock);

	/* Write and invalidate pagecache range that we're writing to: */
	offset = req->ki_pos + (dio->iop.op.written << 9);
	ret = write_invalidate_inode_pages_range(mapping,
					offset,
					offset + iov_iter_count(&dio->iter) - 1);
	if (unlikely(ret))
		goto err;

	while (1) {
		offset = req->ki_pos + (dio->iop.op.written << 9);

		if (kthread)
			kthread_use_mm(dio->mm);
		BUG_ON(current->faults_disabled_mapping);
		current->faults_disabled_mapping = mapping;

		ret = bio_iov_iter_get_pages(bio, &dio->iter);

		current->faults_disabled_mapping = NULL;
		if (kthread)
			kthread_unuse_mm(dio->mm);

		if (unlikely(ret < 0))
			goto err;

		/* gup might have faulted pages back in: */
		ret = write_invalidate_inode_pages_range(mapping,
				offset,
				offset + bio->bi_iter.bi_size - 1);
		if (unlikely(ret))
			goto err;

		dio->iop.op.pos = POS(inode->v.i_ino, offset >> 9);

		task_io_account_write(bio->bi_iter.bi_size);

		closure_call(&dio->iop.op.cl, bch2_write, NULL, &dio->cl);

		if (!dio->sync && !dio->loop && dio->iter.count) {
			if (bch2_dio_write_copy_iov(dio)) {
				dio->iop.op.error = -ENOMEM;
				goto err_wait_io;
			}
		}
err_wait_io:
		dio->loop = true;

		if (!dio->sync) {
			continue_at(&dio->cl, bch2_dio_write_loop_async, NULL);
			return -EIOCBQUEUED;
		}

		closure_sync(&dio->cl);
loop:
		bio_for_each_segment_all(bv, bio, iter)
			put_page(bv->bv_page);
		if (!dio->iter.count || dio->iop.op.error)
			break;
		bio_reset(bio, NULL, REQ_OP_WRITE);
	}

	ret = dio->iop.op.error ?: ((long) dio->iop.op.written << 9);
err:
	bch2_pagecache_block_put(&inode->ei_pagecache_lock);
	bch2_disk_reservation_put(dio->iop.op.c, &dio->iop.op.res);
	bch2_quota_reservation_put(dio->iop.op.c, inode, &dio->quota_res);

	if (dio->free_iov)
		kfree(dio->iter.__iov);

	closure_debug_destroy(&dio->cl);

	sync = dio->sync;
	bio_put(bio);

	/* inode->i_dio_count is our ref on inode and thus bch_fs */
	inode_dio_end(&inode->v);

	if (!sync) {
		req->ki_complete(req, ret);
		ret = -EIOCBQUEUED;
	}
	return ret;
}

static void bch2_dio_write_loop_async(struct closure *cl)
{
	struct dio_write *dio = container_of(cl, struct dio_write, cl);

	bch2_dio_write_loop(dio);
}

static noinline
ssize_t bch2_direct_write(struct kiocb *req, struct iov_iter *iter)
{
	struct file *file = req->ki_filp;
	struct bch_inode_info *inode = file_bch_inode(file);
	struct bch_fs *c = inode->v.i_sb->s_fs_info;
	struct dio_write *dio;
	struct bio *bio;
	ssize_t ret;

	lockdep_assert_held(&inode->v.i_rwsem);

	if (unlikely(!iter->count))
		return 0;

	if (unlikely((req->ki_pos|iter->count) & (block_bytes(c) - 1)))
		return -EINVAL;

	bio = bio_alloc_bioset(NULL,
			       iov_iter_npages(iter, BIO_MAX_VECS),
			       REQ_OP_WRITE,
			       GFP_KERNEL,
			       &c->dio_write_bioset);
	dio = container_of(bio, struct dio_write, iop.op.wbio.bio);
	closure_init(&dio->cl, NULL);
	dio->req		= req;
	dio->mm			= current->mm;
	dio->loop		= false;
	dio->sync		= is_sync_kiocb(req) ||
		req->ki_pos + iter->count > inode->v.i_size;
	dio->free_iov		= false;
	dio->quota_res.sectors	= 0;
	dio->iter		= *iter;
	bch2_fswrite_op_init(&dio->iop, c, inode, io_opts(c, inode), true);
	dio->iop.op.write_point	= writepoint_hashed((unsigned long) current);
	dio->iop.op.flags |= BCH_WRITE_NOPUT_RESERVATION;

	if ((req->ki_flags & IOCB_DSYNC) &&
	    !c->opts.journal_flush_disabled)
		dio->iop.op.flags |= BCH_WRITE_FLUSH;

	ret = bch2_quota_reservation_add(c, inode, &dio->quota_res,
					 iter->count >> 9, true);
	if (unlikely(ret))
		goto err;

	dio->iop.op.nr_replicas	= dio->iop.op.opts.data_replicas;

	ret = bch2_disk_reservation_get(c, &dio->iop.op.res, iter->count >> 9,
					dio->iop.op.opts.data_replicas, 0);
	if (unlikely(ret)) {
		if (!bch2_check_range_allocated(c, POS(inode->v.i_ino,
						       req->ki_pos >> 9),
						iter->count >> 9,
						dio->iop.op.opts.data_replicas))
			goto err;

		dio->iop.unalloc = true;
	}

	return bch2_dio_write_loop(dio);
err:
	bch2_disk_reservation_put(c, &dio->iop.op.res);
	bch2_quota_reservation_put(c, inode, &dio->quota_res);
	closure_debug_destroy(&dio->cl);
	bio_put(bio);
	return ret;
}

static ssize_t __bch2_write_iter(struct kiocb *iocb, struct iov_iter *from)
{
	struct file *file = iocb->ki_filp;
	ssize_t	ret;

	if (iocb->ki_flags & IOCB_DIRECT)
		return bch2_direct_write(iocb, from);

	ret = file_remove_privs(file);
	if (ret)
		return ret;

	ret = file_update_time(file);
	if (ret)
		return ret;

	ret = iocb->ki_flags & IOCB_DIRECT
		? bch2_direct_write(iocb, from)
		: bch2_buffered_write(iocb, from);

	if (likely(ret > 0))
		iocb->ki_pos += ret;

	return ret;
}

ssize_t bch2_write_iter(struct kiocb *iocb, struct iov_iter *from)
{
	struct bch_inode_info *inode = file_bch_inode(iocb->ki_filp);
	bool direct = iocb->ki_flags & IOCB_DIRECT;
	ssize_t ret;

	inode_lock(&inode->v);
	ret = generic_write_checks(iocb, from);
	if (ret > 0)
		ret = __bch2_write_iter(iocb, from);
	inode_unlock(&inode->v);

	if (ret > 0 && !direct)
		ret = generic_write_sync(iocb, ret);

	return ret;
}

/* fsync: */

int bch2_fsync(struct file *file, loff_t start, loff_t end, int datasync)
{
	struct bch_inode_info *inode = file_bch_inode(file);
	struct bch_fs *c = inode->v.i_sb->s_fs_info;
	int ret, ret2;

	ret = file_write_and_wait_range(file, start, end);
	if (ret)
		return ret;

	if (datasync && !(inode->v.i_state & I_DIRTY_DATASYNC))
		goto out;

	ret = sync_inode_metadata(&inode->v, 1);
	if (ret)
		return ret;
out:
	if (!c->opts.journal_flush_disabled)
		ret = bch2_journal_flush_seq(&c->journal,
					     inode->ei_journal_seq);
	ret2 = file_check_and_advance_wb_err(file);

	return ret ?: ret2;
}

/* truncate: */

static int __bch2_fpunch(struct bch_fs *c, struct bch_inode_info *inode,
			 u64 start_offset, u64 end_offset, u64 *journal_seq)
{
	struct bpos start	= POS(inode->v.i_ino, start_offset);
	struct bpos end		= POS(inode->v.i_ino, end_offset);
	unsigned max_sectors	= KEY_SIZE_MAX & (~0 << c->block_bits);
	struct btree_trans trans;
	struct btree_iter *iter;
	struct bkey_s_c k;
	int ret = 0;

	bch2_trans_init(&trans, c, BTREE_ITER_MAX, 1024);

	iter = bch2_trans_get_iter(&trans, BTREE_ID_EXTENTS, start,
				   BTREE_ITER_INTENT);

	while ((k = bch2_btree_iter_peek(iter)).k &&
	       !(ret = bkey_err(k)) &&
	       bkey_cmp(iter->pos, end) < 0) {
		struct disk_reservation disk_res =
			bch2_disk_reservation_init(c, 0);
		struct bkey_i delete;

		bkey_init(&delete.k);
		delete.k.p = iter->pos;

		/* create the biggest key we can */
		bch2_key_resize(&delete.k, max_sectors);
		bch2_cut_back(end, &delete.k);

		bch2_trans_begin_updates(&trans);

		ret = bch2_extent_update(&trans, inode,
				&disk_res, NULL, iter, &delete,
				0, true, true, NULL);
		bch2_disk_reservation_put(c, &disk_res);

		if (ret == -EINTR)
			ret = 0;
		if (ret)
			break;

		bch2_trans_cond_resched(&trans);
	}

	bch2_trans_exit(&trans);

	return ret;
}

static inline int range_has_data(struct bch_fs *c,
				  struct bpos start,
				  struct bpos end)
{
	struct btree_trans trans;
	struct btree_iter *iter;
	struct bkey_s_c k;
	int ret = 0;

	bch2_trans_init(&trans, c, 0, 0);

	for_each_btree_key(&trans, iter, BTREE_ID_EXTENTS, start, 0, k, ret) {
		if (bkey_cmp(bkey_start_pos(k.k), end) >= 0)
			break;

		if (bkey_extent_is_data(k.k)) {
			ret = 1;
			break;
		}
	}

	return bch2_trans_exit(&trans) ?: ret;
}

static int __bch2_truncate_page(struct bch_inode_info *inode,
				pgoff_t index, loff_t start, loff_t end)
{
	struct bch_fs *c = inode->v.i_sb->s_fs_info;
	struct address_space *mapping = inode->v.i_mapping;
	unsigned start_offset = start & (PAGE_SIZE - 1);
	unsigned end_offset = ((end - 1) & (PAGE_SIZE - 1)) + 1;
	struct page *page;
	int ret = 0;

	/* Page boundary? Nothing to do */
	if (!((index == start >> PAGE_SHIFT && start_offset) ||
	      (index == end >> PAGE_SHIFT && end_offset != PAGE_SIZE)))
		return 0;

	/* Above i_size? */
	if (index << PAGE_SHIFT >= inode->v.i_size)
		return 0;

	page = find_lock_page(mapping, index);
	if (!page) {
		/*
		 * XXX: we're doing two index lookups when we end up reading the
		 * page
		 */
		ret = range_has_data(c,
				POS(inode->v.i_ino, index << PAGE_SECTOR_SHIFT),
				POS(inode->v.i_ino, (index + 1) << PAGE_SECTOR_SHIFT));
		if (ret <= 0)
			return ret;

		page = find_or_create_page(mapping, index, GFP_KERNEL);
		if (unlikely(!page)) {
			ret = -ENOMEM;
			goto out;
		}
	}

	if (!PageUptodate(page)) {
		ret = bch2_read_single_page(page, mapping);
		if (ret)
			goto unlock;
	}

	/*
	 * Bit of a hack - we don't want truncate to fail due to -ENOSPC.
	 *
	 * XXX: because we aren't currently tracking whether the page has actual
	 * data in it (vs. just 0s, or only partially written) this wrong. ick.
	 */
	ret = bch2_get_page_reservation(c, inode, page, false);
	BUG_ON(ret);

	if (index == start >> PAGE_SHIFT &&
	    index == end >> PAGE_SHIFT)
		zero_user_segment(page, start_offset, end_offset);
	else if (index == start >> PAGE_SHIFT)
		zero_user_segment(page, start_offset, PAGE_SIZE);
	else if (index == end >> PAGE_SHIFT)
		zero_user_segment(page, 0, end_offset);

	if (!PageDirty(page))
		bch2_set_page_dirty(mapping, page);
unlock:
	unlock_page(page);
	put_page(page);
out:
	return ret;
}

static int bch2_truncate_page(struct bch_inode_info *inode, loff_t from)
{
	return __bch2_truncate_page(inode, from >> PAGE_SHIFT,
				    from, from + PAGE_SIZE);
}

static int bch2_extend(struct bch_inode_info *inode, struct iattr *iattr)
{
	struct bch_fs *c = inode->v.i_sb->s_fs_info;
	struct address_space *mapping = inode->v.i_mapping;
	int ret;

	ret = filemap_write_and_wait_range(mapping,
			inode->ei_inode.bi_size, S64_MAX);
	if (ret)
		return ret;

	truncate_setsize(&inode->v, iattr->ia_size);
	/* ATTR_MODE will never be set here, ns argument isn't needed: */
	setattr_copy(NULL, &inode->v, iattr);

	mutex_lock(&inode->ei_update_lock);
	ret = bch2_write_inode_size(c, inode, inode->v.i_size,
				    ATTR_MTIME|ATTR_CTIME);
	mutex_unlock(&inode->ei_update_lock);

	return ret;
}

static int bch2_truncate_finish_fn(struct bch_inode_info *inode,
				   struct bch_inode_unpacked *bi,
				   void *p)
{
	struct bch_fs *c = inode->v.i_sb->s_fs_info;

	bi->bi_flags &= ~BCH_INODE_I_SIZE_DIRTY;
	bi->bi_mtime = bi->bi_ctime = bch2_current_time(c);
	return 0;
}

static int bch2_truncate_start_fn(struct bch_inode_info *inode,
				  struct bch_inode_unpacked *bi, void *p)
{
	u64 *new_i_size = p;

	bi->bi_flags |= BCH_INODE_I_SIZE_DIRTY;
	bi->bi_size = *new_i_size;
	return 0;
}

int bch2_truncate(struct bch_inode_info *inode, struct iattr *iattr)
{
	struct bch_fs *c = inode->v.i_sb->s_fs_info;
	struct address_space *mapping = inode->v.i_mapping;
	u64 new_i_size = iattr->ia_size;
	bool shrink;
	int ret = 0;

	inode_dio_wait(&inode->v);
	bch2_pagecache_block_get(&inode->ei_pagecache_lock);

	BUG_ON(inode->v.i_size < inode->ei_inode.bi_size);

	shrink = iattr->ia_size <= inode->v.i_size;

	if (!shrink) {
		ret = bch2_extend(inode, iattr);
		goto err;
	}

	ret = bch2_truncate_page(inode, iattr->ia_size);
	if (unlikely(ret))
		goto err;

	if (iattr->ia_size > inode->ei_inode.bi_size)
		ret = filemap_write_and_wait_range(mapping,
				inode->ei_inode.bi_size,
				iattr->ia_size - 1);
	else if (iattr->ia_size & (PAGE_SIZE - 1))
		ret = filemap_write_and_wait_range(mapping,
				round_down(iattr->ia_size, PAGE_SIZE),
				iattr->ia_size - 1);
	if (ret)
		goto err;

	mutex_lock(&inode->ei_update_lock);
	ret = bch2_write_inode(c, inode, bch2_truncate_start_fn,
			       &new_i_size, 0);
	mutex_unlock(&inode->ei_update_lock);

	if (unlikely(ret))
		goto err;

	truncate_setsize(&inode->v, iattr->ia_size);

	/*
	 * XXX: need a comment explaining why PAGE_SIZE and not block_bytes()
	 * here:
	 */
	ret = __bch2_fpunch(c, inode,
			round_up(iattr->ia_size, PAGE_SIZE) >> 9,
			U64_MAX, &inode->ei_journal_seq);
	if (unlikely(ret))
		goto err;

	/* ATTR_MODE will never be set here, ns argument isn't needed: */
	setattr_copy(NULL, &inode->v, iattr);

	mutex_lock(&inode->ei_update_lock);
	ret = bch2_write_inode(c, inode, bch2_truncate_finish_fn, NULL,
			       ATTR_MTIME|ATTR_CTIME);
	mutex_unlock(&inode->ei_update_lock);
err:
	bch2_pagecache_block_put(&inode->ei_pagecache_lock);
	return ret;
}

/* fallocate: */

static long bch2_fpunch(struct bch_inode_info *inode, loff_t offset, loff_t len)
{
	struct bch_fs *c = inode->v.i_sb->s_fs_info;
	u64 discard_start = round_up(offset, PAGE_SIZE) >> 9;
	u64 discard_end = round_down(offset + len, PAGE_SIZE) >> 9;
	int ret = 0;

	inode_lock(&inode->v);
	inode_dio_wait(&inode->v);
	bch2_pagecache_block_get(&inode->ei_pagecache_lock);

	ret = __bch2_truncate_page(inode,
				   offset >> PAGE_SHIFT,
				   offset, offset + len);
	if (unlikely(ret))
		goto err;

	if (offset >> PAGE_SHIFT !=
	    (offset + len) >> PAGE_SHIFT) {
		ret = __bch2_truncate_page(inode,
					   (offset + len) >> PAGE_SHIFT,
					   offset, offset + len);
		if (unlikely(ret))
			goto err;
	}

	truncate_pagecache_range(&inode->v, offset, offset + len - 1);

	if (discard_start < discard_end)
		ret = __bch2_fpunch(c, inode, discard_start, discard_end,
				    &inode->ei_journal_seq);
err:
	bch2_pagecache_block_put(&inode->ei_pagecache_lock);
	inode_unlock(&inode->v);

	return ret;
}

static long bch2_fcollapse(struct bch_inode_info *inode,
			   loff_t offset, loff_t len)
{
	struct bch_fs *c = inode->v.i_sb->s_fs_info;
	struct address_space *mapping = inode->v.i_mapping;
	struct btree_trans trans;
	struct btree_iter *src, *dst;
	BKEY_PADDED(k) copy;
	struct bkey_s_c k;
	loff_t new_size;
	int ret;

	if ((offset | len) & (block_bytes(c) - 1))
		return -EINVAL;

	bch2_trans_init(&trans, c, BTREE_ITER_MAX, 256);

	/*
	 * We need i_mutex to keep the page cache consistent with the extents
	 * btree, and the btree consistent with i_size - we don't need outside
	 * locking for the extents btree itself, because we're using linked
	 * iterators
	 */
	inode_lock(&inode->v);
	inode_dio_wait(&inode->v);
	bch2_pagecache_block_get(&inode->ei_pagecache_lock);

	ret = -EINVAL;
	if (offset + len >= inode->v.i_size)
		goto err;

	if (inode->v.i_size < len)
		goto err;

	new_size = inode->v.i_size - len;

	ret = write_invalidate_inode_pages_range(mapping, offset, LLONG_MAX);
	if (ret)
		goto err;

	dst = bch2_trans_get_iter(&trans, BTREE_ID_EXTENTS,
			POS(inode->v.i_ino, offset >> 9),
			BTREE_ITER_SLOTS|BTREE_ITER_INTENT);
	BUG_ON(IS_ERR_OR_NULL(dst));

	src = bch2_trans_get_iter(&trans, BTREE_ID_EXTENTS,
			POS_MIN, BTREE_ITER_SLOTS);
	BUG_ON(IS_ERR_OR_NULL(src));

	while (bkey_cmp(dst->pos,
			POS(inode->v.i_ino,
			    round_up(new_size, PAGE_SIZE) >> 9)) < 0) {
		struct disk_reservation disk_res;

		ret = bch2_btree_iter_traverse(dst);
		if (ret)
			goto bkey_err;

		bch2_btree_iter_set_pos(src,
			POS(dst->pos.inode, dst->pos.offset + (len >> 9)));

		k = bch2_btree_iter_peek_slot(src);
		if ((ret = bkey_err(k)))
			goto bkey_err;

		bkey_reassemble(&copy.k, k);

		bch2_cut_front(src->pos, &copy.k);
		copy.k.k.p.offset -= len >> 9;

		bch2_extent_trim_atomic(&copy.k, dst);

		BUG_ON(bkey_cmp(dst->pos, bkey_start_pos(&copy.k.k)));

		ret = bch2_disk_reservation_get(c, &disk_res, copy.k.k.size,
				bch2_bkey_nr_dirty_ptrs(bkey_i_to_s_c(&copy.k)),
				BCH_DISK_RESERVATION_NOFAIL);
		BUG_ON(ret);

		bch2_trans_begin_updates(&trans);

		ret = bch2_extent_update(&trans, inode,
				&disk_res, NULL,
				dst, &copy.k,
				0, true, true, NULL);
		bch2_disk_reservation_put(c, &disk_res);
bkey_err:
		if (ret == -EINTR)
			ret = 0;
		if (ret)
			goto err;
		/*
		 * XXX: if we error here we've left data with multiple
		 * pointers... which isn't a _super_ serious problem...
		 */

		bch2_trans_cond_resched(&trans);
	}
	bch2_trans_unlock(&trans);

	ret = __bch2_fpunch(c, inode,
			round_up(new_size, block_bytes(c)) >> 9,
			U64_MAX, &inode->ei_journal_seq);
	if (ret)
		goto err;

	i_size_write(&inode->v, new_size);
	mutex_lock(&inode->ei_update_lock);
	ret = bch2_write_inode_size(c, inode, new_size,
				    ATTR_MTIME|ATTR_CTIME);
	mutex_unlock(&inode->ei_update_lock);
err:
	bch2_trans_exit(&trans);
	bch2_pagecache_block_put(&inode->ei_pagecache_lock);
	inode_unlock(&inode->v);
	return ret;
}

static long bch2_fallocate(struct bch_inode_info *inode, int mode,
			   loff_t offset, loff_t len)
{
	struct address_space *mapping = inode->v.i_mapping;
	struct bch_fs *c = inode->v.i_sb->s_fs_info;
	struct btree_trans trans;
	struct btree_iter *iter;
	struct bpos end_pos;
	loff_t block_start, block_end;
	loff_t end = offset + len;
	unsigned sectors;
	unsigned replicas = io_opts(c, inode).data_replicas;
	int ret;

	bch2_trans_init(&trans, c, BTREE_ITER_MAX, 0);

	inode_lock(&inode->v);
	inode_dio_wait(&inode->v);
	bch2_pagecache_block_get(&inode->ei_pagecache_lock);

	if (!(mode & FALLOC_FL_KEEP_SIZE) && end > inode->v.i_size) {
		ret = inode_newsize_ok(&inode->v, end);
		if (ret)
			goto err;
	}

	if (mode & FALLOC_FL_ZERO_RANGE) {
		ret = __bch2_truncate_page(inode,
					   offset >> PAGE_SHIFT,
					   offset, end);

		if (!ret &&
		    offset >> PAGE_SHIFT != end >> PAGE_SHIFT)
			ret = __bch2_truncate_page(inode,
						   end >> PAGE_SHIFT,
						   offset, end);

		if (unlikely(ret))
			goto err;

		truncate_pagecache_range(&inode->v, offset, end - 1);

		block_start	= round_up(offset, PAGE_SIZE);
		block_end	= round_down(end, PAGE_SIZE);
	} else {
		block_start	= round_down(offset, PAGE_SIZE);
		block_end	= round_up(end, PAGE_SIZE);
	}

	iter = bch2_trans_get_iter(&trans, BTREE_ID_EXTENTS,
			POS(inode->v.i_ino, block_start >> 9),
			BTREE_ITER_SLOTS|BTREE_ITER_INTENT);
	end_pos = POS(inode->v.i_ino, block_end >> 9);

	while (bkey_cmp(iter->pos, end_pos) < 0) {
		struct disk_reservation disk_res = { 0 };
		struct quota_res quota_res = { 0 };
		struct bkey_i_reservation reservation;
		struct bkey_s_c k;

		k = bch2_btree_iter_peek_slot(iter);
		if ((ret = bkey_err(k)))
			goto bkey_err;

		/* already reserved */
		if (k.k->type == KEY_TYPE_reservation &&
		    bkey_s_c_to_reservation(k).v->nr_replicas >= replicas) {
			bch2_btree_iter_next_slot(iter);
			continue;
		}

		if (bkey_extent_is_data(k.k) &&
		    !(mode & FALLOC_FL_ZERO_RANGE)) {
			bch2_btree_iter_next_slot(iter);
			continue;
		}

		bkey_reservation_init(&reservation.k_i);
		reservation.k.type	= KEY_TYPE_reservation;
		reservation.k.p		= k.k->p;
		reservation.k.size	= k.k->size;

		bch2_cut_front(iter->pos, &reservation.k_i);
		bch2_cut_back(end_pos, &reservation.k);

		sectors = reservation.k.size;
		reservation.v.nr_replicas = bch2_bkey_nr_dirty_ptrs(k);

		if (!bkey_extent_is_allocation(k.k)) {
			ret = bch2_quota_reservation_add(c, inode,
					&quota_res,
					sectors, true);
			if (unlikely(ret))
				goto bkey_err;
		}

		if (reservation.v.nr_replicas < replicas ||
		    bch2_extent_is_compressed(k)) {
			ret = bch2_disk_reservation_get(c, &disk_res, sectors,
							replicas, 0);
			if (unlikely(ret))
				goto bkey_err;

			reservation.v.nr_replicas = disk_res.nr_replicas;
		}

		bch2_trans_begin_updates(&trans);

		ret = bch2_extent_update(&trans, inode,
				&disk_res, &quota_res,
				iter, &reservation.k_i,
				0, true, true, NULL);
bkey_err:
		bch2_quota_reservation_put(c, inode, &quota_res);
		bch2_disk_reservation_put(c, &disk_res);
		if (ret == -EINTR)
			ret = 0;
		if (ret)
			goto err;
	}
	bch2_trans_unlock(&trans);

	if (!(mode & FALLOC_FL_KEEP_SIZE) &&
	    end > inode->v.i_size) {
		i_size_write(&inode->v, end);

		mutex_lock(&inode->ei_update_lock);
		ret = bch2_write_inode_size(c, inode, inode->v.i_size, 0);
		mutex_unlock(&inode->ei_update_lock);
	}

	/* blech */
	if ((mode & FALLOC_FL_KEEP_SIZE) &&
	    (mode & FALLOC_FL_ZERO_RANGE) &&
	    inode->ei_inode.bi_size != inode->v.i_size) {
		/* sync appends.. */
		ret = filemap_write_and_wait_range(mapping,
					inode->ei_inode.bi_size, S64_MAX);
		if (ret)
			goto err;

		if (inode->ei_inode.bi_size != inode->v.i_size) {
			mutex_lock(&inode->ei_update_lock);
			ret = bch2_write_inode_size(c, inode,
						    inode->v.i_size, 0);
			mutex_unlock(&inode->ei_update_lock);
		}
	}
err:
	bch2_trans_exit(&trans);
	bch2_pagecache_block_put(&inode->ei_pagecache_lock);
	inode_unlock(&inode->v);
	return ret;
}

long bch2_fallocate_dispatch(struct file *file, int mode,
			     loff_t offset, loff_t len)
{
	struct bch_inode_info *inode = file_bch_inode(file);

	if (!(mode & ~(FALLOC_FL_KEEP_SIZE|FALLOC_FL_ZERO_RANGE)))
		return bch2_fallocate(inode, mode, offset, len);

	if (mode == (FALLOC_FL_PUNCH_HOLE|FALLOC_FL_KEEP_SIZE))
		return bch2_fpunch(inode, offset, len);

	if (mode == FALLOC_FL_COLLAPSE_RANGE)
		return bch2_fcollapse(inode, offset, len);

	return -EOPNOTSUPP;
}

/* fseek: */

static bool folio_is_data(struct folio *folio)
{
	struct bch_page_state *s = bch2_page_state(&folio->page);
	unsigned i;

	if (!s)
		return false;

	for (i = 0; i < PAGE_SECTORS; i++)
		if (s->s[i].state >= SECTOR_DIRTY)
			return true;

	return false;
}

static loff_t bch2_next_pagecache_data(struct inode *vinode,
				       loff_t start_offset,
				       loff_t end_offset)
{
	struct folio_batch fbatch;
	pgoff_t start_index	= start_offset >> PAGE_SHIFT;
	pgoff_t end_index	= end_offset >> PAGE_SHIFT;
	pgoff_t index		= start_index;
	unsigned i;

	folio_batch_init(&fbatch);

	while (filemap_get_folios(vinode->i_mapping,
				  &index, end_index, &fbatch)) {
		for (i = 0; i < folio_batch_count(&fbatch); i++) {
			struct folio *folio = fbatch.folios[i];

			folio_lock(folio);
			if (folio_is_data(folio)) {
				end_offset =
					min(end_offset,
					    max(start_offset,
						((loff_t) index) << PAGE_SHIFT));
				folio_unlock(folio);
				folio_batch_release(&fbatch);
				return end_offset;
			}
			folio_unlock(folio);
		}
		folio_batch_release(&fbatch);
		cond_resched();
	}

	return end_offset;
}

static loff_t bch2_seek_data(struct file *file, u64 offset)
{
	struct bch_inode_info *inode = file_bch_inode(file);
	struct bch_fs *c = inode->v.i_sb->s_fs_info;
	struct btree_trans trans;
	struct btree_iter *iter;
	struct bkey_s_c k;
	u64 isize, next_data = MAX_LFS_FILESIZE;
	int ret;

	isize = i_size_read(&inode->v);
	if (offset >= isize)
		return -ENXIO;

	bch2_trans_init(&trans, c, 0, 0);

	for_each_btree_key(&trans, iter, BTREE_ID_EXTENTS,
			   POS(inode->v.i_ino, offset >> 9), 0, k, ret) {
		if (k.k->p.inode != inode->v.i_ino) {
			break;
		} else if (bkey_extent_is_data(k.k)) {
			next_data = max(offset, bkey_start_offset(k.k) << 9);
			break;
		} else if (k.k->p.offset >> 9 > isize)
			break;
	}

	ret = bch2_trans_exit(&trans) ?: ret;
	if (ret)
		return ret;

	if (next_data > offset)
		next_data = bch2_next_pagecache_data(&inode->v,
						     offset, next_data);

	if (next_data > isize)
		return -ENXIO;

	return vfs_setpos(file, next_data, MAX_LFS_FILESIZE);
}

static bool page_slot_is_data(struct address_space *mapping, pgoff_t index)
{
	struct page *page;
	bool ret;

	page = find_lock_page(mapping, index);
	if (!page)
		return false;

	ret = folio_is_data(page_folio(page));
	unlock_page(page);

	return ret;
}

static loff_t bch2_next_pagecache_hole(struct inode *vinode,
				       loff_t start_offset,
				       loff_t end_offset)
{
	struct address_space *mapping = vinode->i_mapping;
	pgoff_t index;

	for (index = start_offset >> PAGE_SHIFT;
	     index < end_offset >> PAGE_SHIFT;
	     index++)
		if (!page_slot_is_data(mapping, index))
			end_offset = max(start_offset,
					 ((loff_t) index) << PAGE_SHIFT);

	return end_offset;
}

static loff_t bch2_seek_hole(struct file *file, u64 offset)
{
	struct bch_inode_info *inode = file_bch_inode(file);
	struct bch_fs *c = inode->v.i_sb->s_fs_info;
	struct btree_trans trans;
	struct btree_iter *iter;
	struct bkey_s_c k;
	u64 isize, next_hole = MAX_LFS_FILESIZE;
	int ret;

	isize = i_size_read(&inode->v);
	if (offset >= isize)
		return -ENXIO;

	bch2_trans_init(&trans, c, 0, 0);

	for_each_btree_key(&trans, iter, BTREE_ID_EXTENTS,
			   POS(inode->v.i_ino, offset >> 9),
			   BTREE_ITER_SLOTS, k, ret) {
		if (k.k->p.inode != inode->v.i_ino) {
			next_hole = bch2_next_pagecache_hole(&inode->v,
					offset, MAX_LFS_FILESIZE);
			break;
		} else if (!bkey_extent_is_data(k.k)) {
			next_hole = bch2_next_pagecache_hole(&inode->v,
					max(offset, bkey_start_offset(k.k) << 9),
					k.k->p.offset << 9);

			if (next_hole < k.k->p.offset << 9)
				break;
		} else {
			offset = max(offset, bkey_start_offset(k.k) << 9);
		}
	}

	ret = bch2_trans_exit(&trans) ?: ret;
	if (ret)
		return ret;

	if (next_hole > isize)
		next_hole = isize;

	return vfs_setpos(file, next_hole, MAX_LFS_FILESIZE);
}

loff_t bch2_llseek(struct file *file, loff_t offset, int whence)
{
	switch (whence) {
	case SEEK_SET:
	case SEEK_CUR:
	case SEEK_END:
		return generic_file_llseek(file, offset, whence);
	case SEEK_DATA:
		return bch2_seek_data(file, offset);
	case SEEK_HOLE:
		return bch2_seek_hole(file, offset);
	}

	return -EINVAL;
}

void bch2_fs_fsio_exit(struct bch_fs *c)
{
	bioset_exit(&c->dio_write_bioset);
	bioset_exit(&c->dio_read_bioset);
	bioset_exit(&c->writepage_bioset);
}

int bch2_fs_fsio_init(struct bch_fs *c)
{
	int ret = 0;

	pr_verbose_init(c->opts, "");

	if (bioset_init(&c->writepage_bioset,
			4, offsetof(struct bch_writepage_io, op.op.wbio.bio),
			BIOSET_NEED_BVECS) ||
	    bioset_init(&c->dio_read_bioset,
			4, offsetof(struct dio_read, rbio.bio),
			BIOSET_NEED_BVECS) ||
	    bioset_init(&c->dio_write_bioset,
			4, offsetof(struct dio_write, iop.op.wbio.bio),
			BIOSET_NEED_BVECS))
		ret = -ENOMEM;

	pr_verbose_init(c->opts, "ret %i", ret);
	return ret;
}

#endif /* NO_BCACHEFS_FS */
