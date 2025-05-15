// SPDX-License-Identifier: GPL-2.0
#ifndef NO_BCACHEFS_FS

#include "bcachefs.h"
#include "btree_iter.h"
#include "extents.h"
#include "fs-io.h"
#include "fs-io-pagecache.h"
#include "subvolume.h"

#include <linux/pagevec.h>
#include <linux/writeback.h>

int bch2_filemap_get_contig_folios_d(struct address_space *mapping,
				     loff_t start, u64 end,
				     fgf_t fgp_flags, gfp_t gfp,
				     folios *fs)
{
	struct folio *f;
	u64 pos = start;
	int ret = 0;

	while (pos < end) {
		if ((u64) pos >= (u64) start + (1ULL << 20))
			fgp_flags &= ~FGP_CREAT;

		ret = darray_make_room_gfp(fs, 1, gfp & GFP_KERNEL);
		if (ret)
			break;

		f = __filemap_get_folio(mapping, pos >> PAGE_SHIFT, fgp_flags, gfp);
		if (IS_ERR(f))
			break;

		BUG_ON(fs->nr && folio_pos(f) != pos);

		pos = folio_end_pos(f);
		darray_push(fs, f);
	}

	if (!fs->nr && !ret && (fgp_flags & FGP_CREAT))
		ret = -ENOMEM;

	return fs->nr ? 0 : ret;
}

/* pagecache_block must be held */
int bch2_write_invalidate_inode_pages_range(struct address_space *mapping,
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

#if 0
/* Useful for debug tracing: */
static const char * const bch2_folio_sector_states[] = {
#define x(n)	#n,
	BCH_FOLIO_SECTOR_STATE()
#undef x
	NULL
};
#endif

static inline enum bch_folio_sector_state
folio_sector_dirty(enum bch_folio_sector_state state)
{
	switch (state) {
	case SECTOR_unallocated:
		return SECTOR_dirty;
	case SECTOR_reserved:
		return SECTOR_dirty_reserved;
	default:
		return state;
	}
}

static inline enum bch_folio_sector_state
folio_sector_undirty(enum bch_folio_sector_state state)
{
	switch (state) {
	case SECTOR_dirty:
		return SECTOR_unallocated;
	case SECTOR_dirty_reserved:
		return SECTOR_reserved;
	default:
		return state;
	}
}

static inline enum bch_folio_sector_state
folio_sector_reserve(enum bch_folio_sector_state state)
{
	switch (state) {
	case SECTOR_unallocated:
		return SECTOR_reserved;
	case SECTOR_dirty:
		return SECTOR_dirty_reserved;
	default:
		return state;
	}
}

/* for newly allocated folios: */
struct bch_folio *__bch2_folio_create(struct folio *folio, gfp_t gfp)
{
	struct bch_folio *s;

	s = kzalloc(sizeof(*s) +
		    sizeof(struct bch_folio_sector) *
		    folio_sectors(folio), gfp);
	if (!s)
		return NULL;

	spin_lock_init(&s->lock);
	folio_attach_private(folio, s);
	return s;
}

struct bch_folio *bch2_folio_create(struct folio *folio, gfp_t gfp)
{
	return bch2_folio(folio) ?: __bch2_folio_create(folio, gfp);
}

static unsigned bkey_to_sector_state(struct bkey_s_c k)
{
	if (bkey_extent_is_reservation(k))
		return SECTOR_reserved;
	if (bkey_extent_is_allocation(k.k))
		return SECTOR_allocated;
	return SECTOR_unallocated;
}

static void __bch2_folio_set(struct folio *folio,
			     unsigned pg_offset, unsigned pg_len,
			     unsigned nr_ptrs, unsigned state)
{
	struct bch_folio *s = bch2_folio(folio);
	unsigned i, sectors = folio_sectors(folio);

	BUG_ON(pg_offset >= sectors);
	BUG_ON(pg_offset + pg_len > sectors);

	spin_lock(&s->lock);

	for (i = pg_offset; i < pg_offset + pg_len; i++) {
		s->s[i].nr_replicas	= nr_ptrs;
		bch2_folio_sector_set(folio, s, i, state);
	}

	if (i == sectors)
		s->uptodate = true;

	spin_unlock(&s->lock);
}

/*
 * Initialize bch_folio state (allocated/unallocated, nr_replicas) from the
 * extents btree:
 */
int bch2_folio_set(struct bch_fs *c, subvol_inum inum,
		   struct folio **fs, unsigned nr_folios)
{
	u64 offset = folio_sector(fs[0]);
	bool need_set = false;

	for (unsigned folio_idx = 0; folio_idx < nr_folios; folio_idx++) {
		struct bch_folio *s = bch2_folio_create(fs[folio_idx], GFP_KERNEL);
		if (!s)
			return -ENOMEM;

		need_set |= !s->uptodate;
	}

	if (!need_set)
		return 0;

	unsigned folio_idx = 0;

	return bch2_trans_run(c,
		for_each_btree_key_in_subvolume_max(trans, iter, BTREE_ID_extents,
				   POS(inum.inum, offset),
				   POS(inum.inum, U64_MAX),
				   inum.subvol, BTREE_ITER_slots, k, ({
			unsigned nr_ptrs = bch2_bkey_nr_ptrs_fully_allocated(k);
			unsigned state = bkey_to_sector_state(k);

			while (folio_idx < nr_folios) {
				struct folio *folio = fs[folio_idx];
				u64 folio_start	= folio_sector(folio);
				u64 folio_end	= folio_end_sector(folio);
				unsigned folio_offset = max(bkey_start_offset(k.k), folio_start) -
					folio_start;
				unsigned folio_len = min(k.k->p.offset, folio_end) -
					folio_offset - folio_start;

				BUG_ON(k.k->p.offset < folio_start);
				BUG_ON(bkey_start_offset(k.k) > folio_end);

				if (!bch2_folio(folio)->uptodate)
					__bch2_folio_set(folio, folio_offset, folio_len, nr_ptrs, state);

				if (k.k->p.offset < folio_end)
					break;
				folio_idx++;
			}

			if (folio_idx == nr_folios)
				break;
			0;
		})));
}

void bch2_bio_page_state_set(struct bio *bio, struct bkey_s_c k)
{
	struct bvec_iter iter;
	struct folio_vec fv;
	unsigned nr_ptrs = k.k->type == KEY_TYPE_reflink_v
		? 0 : bch2_bkey_nr_ptrs_fully_allocated(k);
	unsigned state = bkey_to_sector_state(k);

	bio_for_each_folio(fv, bio, iter)
		__bch2_folio_set(fv.fv_folio,
				 fv.fv_offset >> 9,
				 fv.fv_len >> 9,
				 nr_ptrs, state);
}

void bch2_mark_pagecache_unallocated(struct bch_inode_info *inode,
				     u64 start, u64 end)
{
	pgoff_t index = start >> PAGE_SECTORS_SHIFT;
	pgoff_t end_index = (end - 1) >> PAGE_SECTORS_SHIFT;
	struct folio_batch fbatch;
	unsigned i, j;

	if (end <= start)
		return;

	folio_batch_init(&fbatch);

	while (filemap_get_folios(inode->v.i_mapping,
				  &index, end_index, &fbatch)) {
		for (i = 0; i < folio_batch_count(&fbatch); i++) {
			struct folio *folio = fbatch.folios[i];
			u64 folio_start = folio_sector(folio);
			u64 folio_end = folio_end_sector(folio);
			unsigned folio_offset = max(start, folio_start) - folio_start;
			unsigned folio_len = min(end, folio_end) - folio_offset - folio_start;
			struct bch_folio *s;

			BUG_ON(end <= folio_start);

			folio_lock(folio);
			s = bch2_folio(folio);

			if (s) {
				spin_lock(&s->lock);
				for (j = folio_offset; j < folio_offset + folio_len; j++)
					s->s[j].nr_replicas = 0;
				spin_unlock(&s->lock);
			}

			folio_unlock(folio);
		}
		folio_batch_release(&fbatch);
		cond_resched();
	}
}

int bch2_mark_pagecache_reserved(struct bch_inode_info *inode,
				 u64 *start, u64 end,
				 bool nonblocking)
{
	struct bch_fs *c = inode->v.i_sb->s_fs_info;
	pgoff_t index = *start >> PAGE_SECTORS_SHIFT;
	pgoff_t end_index = (end - 1) >> PAGE_SECTORS_SHIFT;
	struct folio_batch fbatch;
	s64 i_sectors_delta = 0;
	int ret = 0;

	if (end <= *start)
		return 0;

	folio_batch_init(&fbatch);

	while (filemap_get_folios(inode->v.i_mapping,
				  &index, end_index, &fbatch)) {
		for (unsigned i = 0; i < folio_batch_count(&fbatch); i++) {
			struct folio *folio = fbatch.folios[i];

			if (!nonblocking)
				folio_lock(folio);
			else if (!folio_trylock(folio)) {
				folio_batch_release(&fbatch);
				ret = -EAGAIN;
				break;
			}

			u64 folio_start = folio_sector(folio);
			u64 folio_end = folio_end_sector(folio);

			BUG_ON(end <= folio_start);

			*start = min(end, folio_end);

			struct bch_folio *s = bch2_folio(folio);
			if (s) {
				unsigned folio_offset = max(*start, folio_start) - folio_start;
				unsigned folio_len = min(end, folio_end) - folio_offset - folio_start;

				spin_lock(&s->lock);
				for (unsigned j = folio_offset; j < folio_offset + folio_len; j++) {
					i_sectors_delta -= s->s[j].state == SECTOR_dirty;
					bch2_folio_sector_set(folio, s, j,
						folio_sector_reserve(s->s[j].state));
				}
				spin_unlock(&s->lock);
			}

			folio_unlock(folio);
		}
		folio_batch_release(&fbatch);
		cond_resched();
	}

	bch2_i_sectors_acct(c, inode, NULL, i_sectors_delta);
	return ret;
}

static inline unsigned sectors_to_reserve(struct bch_folio_sector *s,
					  unsigned nr_replicas)
{
	return max(0, (int) nr_replicas -
		   s->nr_replicas -
		   s->replicas_reserved);
}

int bch2_get_folio_disk_reservation(struct bch_fs *c,
				struct bch_inode_info *inode,
				struct folio *folio, bool check_enospc)
{
	struct bch_folio *s = bch2_folio_create(folio, 0);
	unsigned nr_replicas = inode_nr_replicas(c, inode);
	struct disk_reservation disk_res = { 0 };
	unsigned i, sectors = folio_sectors(folio), disk_res_sectors = 0;
	int ret;

	if (!s)
		return -ENOMEM;

	for (i = 0; i < sectors; i++)
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

	for (i = 0; i < sectors; i++)
		s->s[i].replicas_reserved +=
			sectors_to_reserve(&s->s[i], nr_replicas);

	return 0;
}

void bch2_folio_reservation_put(struct bch_fs *c,
			struct bch_inode_info *inode,
			struct bch2_folio_reservation *res)
{
	bch2_disk_reservation_put(c, &res->disk);
	bch2_quota_reservation_put(c, inode, &res->quota);
}

static int __bch2_folio_reservation_get(struct bch_fs *c,
			struct bch_inode_info *inode,
			struct folio *folio,
			struct bch2_folio_reservation *res,
			size_t offset, size_t len,
			bool partial)
{
	struct bch_folio *s = bch2_folio_create(folio, 0);
	unsigned i, disk_sectors = 0, quota_sectors = 0;
	struct disk_reservation disk_res = {};
	size_t reserved = len;
	int ret;

	if (!s)
		return -ENOMEM;

	BUG_ON(!s->uptodate);

	for (i = round_down(offset, block_bytes(c)) >> 9;
	     i < round_up(offset + len, block_bytes(c)) >> 9;
	     i++) {
		disk_sectors += sectors_to_reserve(&s->s[i], res->disk.nr_replicas);
		quota_sectors += s->s[i].state == SECTOR_unallocated;
	}

	if (disk_sectors) {
		ret = bch2_disk_reservation_add(c, &disk_res, disk_sectors,
				partial ? BCH_DISK_RESERVATION_PARTIAL : 0);
		if (unlikely(ret))
			return ret;

		if (unlikely(disk_res.sectors != disk_sectors)) {
			disk_sectors = quota_sectors = 0;

			for (i = round_down(offset, block_bytes(c)) >> 9;
			     i < round_up(offset + len, block_bytes(c)) >> 9;
			     i++) {
				disk_sectors += sectors_to_reserve(&s->s[i], res->disk.nr_replicas);
				if (disk_sectors > disk_res.sectors) {
					/*
					 * Make sure to get a reservation that's
					 * aligned to the filesystem blocksize:
					 */
					unsigned reserved_offset = round_down(i << 9, block_bytes(c));
					reserved = clamp(reserved_offset, offset, offset + len) - offset;

					if (!reserved) {
						bch2_disk_reservation_put(c, &disk_res);
						return -BCH_ERR_ENOSPC_disk_reservation;
					}
					break;
				}
				quota_sectors += s->s[i].state == SECTOR_unallocated;
			}
		}
	}

	if (quota_sectors) {
		ret = bch2_quota_reservation_add(c, inode, &res->quota, quota_sectors, true);
		if (unlikely(ret)) {
			bch2_disk_reservation_put(c, &disk_res);
			return ret;
		}
	}

	res->disk.sectors += disk_res.sectors;
	return partial ? reserved : 0;
}

int bch2_folio_reservation_get(struct bch_fs *c,
			struct bch_inode_info *inode,
			struct folio *folio,
			struct bch2_folio_reservation *res,
			size_t offset, size_t len)
{
	return __bch2_folio_reservation_get(c, inode, folio, res, offset, len, false);
}

ssize_t bch2_folio_reservation_get_partial(struct bch_fs *c,
			struct bch_inode_info *inode,
			struct folio *folio,
			struct bch2_folio_reservation *res,
			size_t offset, size_t len)
{
	return __bch2_folio_reservation_get(c, inode, folio, res, offset, len, true);
}

static void bch2_clear_folio_bits(struct folio *folio)
{
	struct bch_inode_info *inode = to_bch_ei(folio->mapping->host);
	struct bch_fs *c = inode->v.i_sb->s_fs_info;
	struct bch_folio *s = bch2_folio(folio);
	struct disk_reservation disk_res = { 0 };
	int i, sectors = folio_sectors(folio), dirty_sectors = 0;

	if (!s)
		return;

	EBUG_ON(!folio_test_locked(folio));
	EBUG_ON(folio_test_writeback(folio));

	for (i = 0; i < sectors; i++) {
		disk_res.sectors += s->s[i].replicas_reserved;
		s->s[i].replicas_reserved = 0;

		dirty_sectors -= s->s[i].state == SECTOR_dirty;
		bch2_folio_sector_set(folio, s, i, folio_sector_undirty(s->s[i].state));
	}

	bch2_disk_reservation_put(c, &disk_res);

	bch2_i_sectors_acct(c, inode, NULL, dirty_sectors);

	bch2_folio_release(folio);
}

void bch2_set_folio_dirty(struct bch_fs *c,
			  struct bch_inode_info *inode,
			  struct folio *folio,
			  struct bch2_folio_reservation *res,
			  unsigned offset, unsigned len)
{
	struct bch_folio *s = bch2_folio(folio);
	unsigned i, dirty_sectors = 0;

	WARN_ON((u64) folio_pos(folio) + offset + len >
		round_up((u64) i_size_read(&inode->v), block_bytes(c)));

	BUG_ON(!s->uptodate);

	spin_lock(&s->lock);

	for (i = round_down(offset, block_bytes(c)) >> 9;
	     i < round_up(offset + len, block_bytes(c)) >> 9;
	     i++) {
		unsigned sectors = sectors_to_reserve(&s->s[i],
						res->disk.nr_replicas);

		/*
		 * This can happen if we race with the error path in
		 * bch2_writepage_io_done():
		 */
		sectors = min_t(unsigned, sectors, res->disk.sectors);

		s->s[i].replicas_reserved += sectors;
		res->disk.sectors -= sectors;

		dirty_sectors += s->s[i].state == SECTOR_unallocated;

		bch2_folio_sector_set(folio, s, i, folio_sector_dirty(s->s[i].state));
	}

	spin_unlock(&s->lock);

	bch2_i_sectors_acct(c, inode, &res->quota, dirty_sectors);

	if (!folio_test_dirty(folio))
		filemap_dirty_folio(inode->v.i_mapping, folio);
}

vm_fault_t bch2_page_fault(struct vm_fault *vmf)
{
	struct file *file = vmf->vma->vm_file;
	struct address_space *mapping = file->f_mapping;
	struct address_space *fdm = faults_disabled_mapping();
	struct bch_inode_info *inode = file_bch_inode(file);
	vm_fault_t ret;

	if (fdm == mapping)
		return VM_FAULT_SIGBUS;

	/* Lock ordering: */
	if (fdm > mapping) {
		struct bch_inode_info *fdm_host = to_bch_ei(fdm->host);

		if (bch2_pagecache_add_tryget(inode))
			goto got_lock;

		bch2_pagecache_block_put(fdm_host);

		bch2_pagecache_add_get(inode);
		bch2_pagecache_add_put(inode);

		bch2_pagecache_block_get(fdm_host);

		/* Signal that lock has been dropped: */
		set_fdm_dropped_locks();
		return VM_FAULT_SIGBUS;
	}

	bch2_pagecache_add_get(inode);
got_lock:
	ret = filemap_fault(vmf);
	bch2_pagecache_add_put(inode);

	return ret;
}

vm_fault_t bch2_page_mkwrite(struct vm_fault *vmf)
{
	struct folio *folio = page_folio(vmf->page);
	struct file *file = vmf->vma->vm_file;
	struct bch_inode_info *inode = file_bch_inode(file);
	struct address_space *mapping = file->f_mapping;
	struct bch_fs *c = inode->v.i_sb->s_fs_info;
	struct bch2_folio_reservation res;
	vm_fault_t ret;

	loff_t file_offset = round_down(vmf->pgoff << PAGE_SHIFT, block_bytes(c));
	unsigned offset = file_offset - folio_pos(folio);
	unsigned len = max(PAGE_SIZE, block_bytes(c));

	BUG_ON(offset + len > folio_size(folio));

	bch2_folio_reservation_init(c, inode, &res);

	sb_start_pagefault(inode->v.i_sb);
	file_update_time(file);

	/*
	 * Not strictly necessary, but helps avoid dio writes livelocking in
	 * bch2_write_invalidate_inode_pages_range() - can drop this if/when we get
	 * a bch2_write_invalidate_inode_pages_range() that works without dropping
	 * page lock before invalidating page
	 */
	bch2_pagecache_add_get(inode);

	folio_lock(folio);
	u64 isize = i_size_read(&inode->v);

	if (folio->mapping != mapping || file_offset >= isize) {
		folio_unlock(folio);
		ret = VM_FAULT_NOPAGE;
		goto out;
	}

	len = min_t(unsigned, len, isize - file_offset);

	if (bch2_folio_set(c, inode_inum(inode), &folio, 1) ?:
	    bch2_folio_reservation_get(c, inode, folio, &res, offset, len)) {
		folio_unlock(folio);
		ret = VM_FAULT_SIGBUS;
		goto out;
	}

	bch2_set_folio_dirty(c, inode, folio, &res, offset, len);
	bch2_folio_reservation_put(c, inode, &res);

	folio_wait_stable(folio);
	ret = VM_FAULT_LOCKED;
out:
	bch2_pagecache_add_put(inode);
	sb_end_pagefault(inode->v.i_sb);

	return ret;
}

void bch2_invalidate_folio(struct folio *folio, size_t offset, size_t length)
{
	if (offset || length < folio_size(folio))
		return;

	bch2_clear_folio_bits(folio);
}

bool bch2_release_folio(struct folio *folio, gfp_t gfp_mask)
{
	if (folio_test_dirty(folio) || folio_test_writeback(folio))
		return false;

	bch2_clear_folio_bits(folio);
	return true;
}

/* fseek: */

static int folio_data_offset(struct folio *folio, loff_t pos,
			     unsigned min_replicas)
{
	struct bch_folio *s = bch2_folio(folio);
	unsigned i, sectors = folio_sectors(folio);

	if (s)
		for (i = folio_pos_to_s(folio, pos); i < sectors; i++)
			if (s->s[i].state >= SECTOR_dirty &&
			    s->s[i].nr_replicas + s->s[i].replicas_reserved >= min_replicas)
				return i << SECTOR_SHIFT;

	return -1;
}

loff_t bch2_seek_pagecache_data(struct inode *vinode,
				loff_t start_offset,
				loff_t end_offset,
				unsigned min_replicas,
				bool nonblock)
{
	struct folio_batch fbatch;
	pgoff_t start_index	= start_offset >> PAGE_SHIFT;
	pgoff_t end_index	= end_offset >> PAGE_SHIFT;
	pgoff_t index		= start_index;
	unsigned i;
	loff_t ret;
	int offset;

	folio_batch_init(&fbatch);

	while (filemap_get_folios(vinode->i_mapping,
				  &index, end_index, &fbatch)) {
		for (i = 0; i < folio_batch_count(&fbatch); i++) {
			struct folio *folio = fbatch.folios[i];

			if (!nonblock) {
				folio_lock(folio);
			} else if (!folio_trylock(folio)) {
				folio_batch_release(&fbatch);
				return -EAGAIN;
			}

			offset = folio_data_offset(folio,
					max(folio_pos(folio), start_offset),
					min_replicas);
			if (offset >= 0) {
				ret = clamp(folio_pos(folio) + offset,
					    start_offset, end_offset);
				folio_unlock(folio);
				folio_batch_release(&fbatch);
				return ret;
			}
			folio_unlock(folio);
		}
		folio_batch_release(&fbatch);
		cond_resched();
	}

	return end_offset;
}

/*
 * Search for a hole in a folio.
 *
 * The filemap layer returns -ENOENT if no folio exists, so reuse the same error
 * code to indicate a pagecache hole exists at the returned offset. Otherwise
 * return 0 if the folio is filled with data, or an error code. This function
 * can return -EAGAIN if nonblock is specified.
 */
static int folio_hole_offset(struct address_space *mapping, loff_t *offset,
			      unsigned min_replicas, bool nonblock)
{
	struct folio *folio;
	struct bch_folio *s;
	unsigned i, sectors;
	int ret = -ENOENT;

	folio = __filemap_get_folio(mapping, *offset >> PAGE_SHIFT,
				    FGP_LOCK|(nonblock ? FGP_NOWAIT : 0), 0);
	if (IS_ERR(folio))
		return PTR_ERR(folio);

	s = bch2_folio(folio);
	if (!s)
		goto unlock;

	sectors = folio_sectors(folio);
	for (i = folio_pos_to_s(folio, *offset); i < sectors; i++)
		if (s->s[i].state < SECTOR_dirty ||
		    s->s[i].nr_replicas + s->s[i].replicas_reserved < min_replicas) {
			*offset = max(*offset,
				      folio_pos(folio) + (i << SECTOR_SHIFT));
			goto unlock;
		}

	*offset = folio_end_pos(folio);
	ret = 0;
unlock:
	folio_unlock(folio);
	folio_put(folio);
	return ret;
}

loff_t bch2_seek_pagecache_hole(struct inode *vinode,
				loff_t start_offset,
				loff_t end_offset,
				unsigned min_replicas,
				bool nonblock)
{
	struct address_space *mapping = vinode->i_mapping;
	loff_t offset = start_offset;
	loff_t ret = 0;

	while (!ret && offset < end_offset)
		ret = folio_hole_offset(mapping, &offset, min_replicas, nonblock);

	if (ret && ret != -ENOENT)
		return ret;
	return min(offset, end_offset);
}

int bch2_clamp_data_hole(struct inode *inode,
			 u64 *hole_start,
			 u64 *hole_end,
			 unsigned min_replicas,
			 bool nonblock)
{
	loff_t ret;

	ret = bch2_seek_pagecache_hole(inode,
		*hole_start << 9, *hole_end << 9, min_replicas, nonblock) >> 9;
	if (ret < 0)
		return ret;

	*hole_start = ret;

	if (*hole_start == *hole_end)
		return 0;

	ret = bch2_seek_pagecache_data(inode,
		*hole_start << 9, *hole_end << 9, min_replicas, nonblock) >> 9;
	if (ret < 0)
		return ret;

	*hole_end = ret;
	return 0;
}

#endif /* NO_BCACHEFS_FS */
