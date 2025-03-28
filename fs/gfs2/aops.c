// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) Sistina Software, Inc.  1997-2003 All rights reserved.
 * Copyright (C) 2004-2008 Red Hat, Inc.  All rights reserved.
 */

#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/completion.h>
#include <linux/buffer_head.h>
#include <linux/pagemap.h>
#include <linux/pagevec.h>
#include <linux/mpage.h>
#include <linux/fs.h>
#include <linux/writeback.h>
#include <linux/swap.h>
#include <linux/gfs2_ondisk.h>
#include <linux/backing-dev.h>
#include <linux/uio.h>
#include <trace/events/writeback.h>
#include <linux/sched/signal.h>

#include "gfs2.h"
#include "incore.h"
#include "bmap.h"
#include "glock.h"
#include "inode.h"
#include "log.h"
#include "meta_io.h"
#include "quota.h"
#include "trans.h"
#include "rgrp.h"
#include "super.h"
#include "util.h"
#include "glops.h"
#include "aops.h"


/**
 * gfs2_get_block_noalloc - Fills in a buffer head with details about a block
 * @inode: The inode
 * @lblock: The block number to look up
 * @bh_result: The buffer head to return the result in
 * @create: Non-zero if we may add block to the file
 *
 * Returns: errno
 */

static int gfs2_get_block_noalloc(struct inode *inode, sector_t lblock,
				  struct buffer_head *bh_result, int create)
{
	int error;

	error = gfs2_block_map(inode, lblock, bh_result, 0);
	if (error)
		return error;
	if (!buffer_mapped(bh_result))
		return -ENODATA;
	return 0;
}

/**
 * gfs2_write_jdata_folio - gfs2 jdata-specific version of block_write_full_folio
 * @folio: The folio to write
 * @wbc: The writeback control
 *
 * This is the same as calling block_write_full_folio, but it also
 * writes pages outside of i_size
 */
static int gfs2_write_jdata_folio(struct folio *folio,
				 struct writeback_control *wbc)
{
	struct inode * const inode = folio->mapping->host;
	loff_t i_size = i_size_read(inode);

	/*
	 * The folio straddles i_size.  It must be zeroed out on each and every
	 * writepage invocation because it may be mmapped.  "A file is mapped
	 * in multiples of the page size.  For a file that is not a multiple of
	 * the page size, the remaining memory is zeroed when mapped, and
	 * writes to that region are not written out to the file."
	 */
	if (folio_pos(folio) < i_size &&
	    i_size < folio_pos(folio) + folio_size(folio))
		folio_zero_segment(folio, offset_in_folio(folio, i_size),
				folio_size(folio));

	return __block_write_full_folio(inode, folio, gfs2_get_block_noalloc,
			wbc);
}

/**
 * __gfs2_jdata_write_folio - The core of jdata writepage
 * @folio: The folio to write
 * @wbc: The writeback control
 *
 * Implements the core of write back. If a transaction is required then
 * the checked flag will have been set and the transaction will have
 * already been started before this is called.
 */
static int __gfs2_jdata_write_folio(struct folio *folio,
		struct writeback_control *wbc)
{
	struct inode *inode = folio->mapping->host;
	struct gfs2_inode *ip = GFS2_I(inode);

	if (folio_test_checked(folio)) {
		folio_clear_checked(folio);
		if (!folio_buffers(folio)) {
			create_empty_buffers(folio,
					inode->i_sb->s_blocksize,
					BIT(BH_Dirty)|BIT(BH_Uptodate));
		}
		gfs2_trans_add_databufs(ip->i_gl, folio, 0, folio_size(folio));
	}
	return gfs2_write_jdata_folio(folio, wbc);
}

/**
 * gfs2_jdata_writeback - Write jdata folios to the log
 * @mapping: The mapping to write
 * @wbc: The writeback control
 *
 * Returns: errno
 */
int gfs2_jdata_writeback(struct address_space *mapping, struct writeback_control *wbc)
{
	struct inode *inode = mapping->host;
	struct gfs2_inode *ip = GFS2_I(inode);
	struct gfs2_sbd *sdp = GFS2_SB(mapping->host);
	struct folio *folio = NULL;
	int error;

	BUG_ON(current->journal_info);
	if (gfs2_assert_withdraw(sdp, ip->i_gl->gl_state == LM_ST_EXCLUSIVE))
		return 0;

	while ((folio = writeback_iter(mapping, wbc, folio, &error))) {
		if (folio_test_checked(folio)) {
			folio_redirty_for_writepage(wbc, folio);
			folio_unlock(folio);
			continue;
		}
		error = __gfs2_jdata_write_folio(folio, wbc);
	}

	return error;
}

/**
 * gfs2_writepages - Write a bunch of dirty pages back to disk
 * @mapping: The mapping to write
 * @wbc: Write-back control
 *
 * Used for both ordered and writeback modes.
 */
static int gfs2_writepages(struct address_space *mapping,
			   struct writeback_control *wbc)
{
	struct gfs2_sbd *sdp = gfs2_mapping2sbd(mapping);
	struct iomap_writepage_ctx wpc = { };
	int ret;

	/*
	 * Even if we didn't write enough pages here, we might still be holding
	 * dirty pages in the ail. We forcibly flush the ail because we don't
	 * want balance_dirty_pages() to loop indefinitely trying to write out
	 * pages held in the ail that it can't find.
	 */
	ret = iomap_writepages(mapping, wbc, &wpc, &gfs2_writeback_ops);
	if (ret == 0 && wbc->nr_to_write > 0)
		set_bit(SDF_FORCE_AIL_FLUSH, &sdp->sd_flags);
	return ret;
}

/**
 * gfs2_write_jdata_batch - Write back a folio batch's worth of folios
 * @mapping: The mapping
 * @wbc: The writeback control
 * @fbatch: The batch of folios
 * @done_index: Page index
 *
 * Returns: non-zero if loop should terminate, zero otherwise
 */

static int gfs2_write_jdata_batch(struct address_space *mapping,
				    struct writeback_control *wbc,
				    struct folio_batch *fbatch,
				    pgoff_t *done_index)
{
	struct inode *inode = mapping->host;
	struct gfs2_sbd *sdp = GFS2_SB(inode);
	unsigned nrblocks;
	int i;
	int ret;
	size_t size = 0;
	int nr_folios = folio_batch_count(fbatch);

	for (i = 0; i < nr_folios; i++)
		size += folio_size(fbatch->folios[i]);
	nrblocks = size >> inode->i_blkbits;

	ret = gfs2_trans_begin(sdp, nrblocks, nrblocks);
	if (ret < 0)
		return ret;

	for (i = 0; i < nr_folios; i++) {
		struct folio *folio = fbatch->folios[i];

		*done_index = folio->index;

		folio_lock(folio);

		if (unlikely(folio->mapping != mapping)) {
continue_unlock:
			folio_unlock(folio);
			continue;
		}

		if (!folio_test_dirty(folio)) {
			/* someone wrote it for us */
			goto continue_unlock;
		}

		if (folio_test_writeback(folio)) {
			if (wbc->sync_mode != WB_SYNC_NONE)
				folio_wait_writeback(folio);
			else
				goto continue_unlock;
		}

		BUG_ON(folio_test_writeback(folio));
		if (!folio_clear_dirty_for_io(folio))
			goto continue_unlock;

		trace_wbc_writepage(wbc, inode_to_bdi(inode));

		ret = __gfs2_jdata_write_folio(folio, wbc);
		if (unlikely(ret)) {
			/*
			 * done_index is set past this page, so media errors
			 * will not choke background writeout for the entire
			 * file. This has consequences for range_cyclic
			 * semantics (ie. it may not be suitable for data
			 * integrity writeout).
			 */
			*done_index = folio_next_index(folio);
			ret = 1;
			break;
		}

		/*
		 * We stop writing back only if we are not doing
		 * integrity sync. In case of integrity sync we have to
		 * keep going until we have written all the pages
		 * we tagged for writeback prior to entering this loop.
		 */
		if (--wbc->nr_to_write <= 0 && wbc->sync_mode == WB_SYNC_NONE) {
			ret = 1;
			break;
		}

	}
	gfs2_trans_end(sdp);
	return ret;
}

/**
 * gfs2_write_cache_jdata - Like write_cache_pages but different
 * @mapping: The mapping to write
 * @wbc: The writeback control
 *
 * The reason that we use our own function here is that we need to
 * start transactions before we grab page locks. This allows us
 * to get the ordering right.
 */

static int gfs2_write_cache_jdata(struct address_space *mapping,
				  struct writeback_control *wbc)
{
	int ret = 0;
	int done = 0;
	struct folio_batch fbatch;
	int nr_folios;
	pgoff_t writeback_index;
	pgoff_t index;
	pgoff_t end;
	pgoff_t done_index;
	int cycled;
	int range_whole = 0;
	xa_mark_t tag;

	folio_batch_init(&fbatch);
	if (wbc->range_cyclic) {
		writeback_index = mapping->writeback_index; /* prev offset */
		index = writeback_index;
		if (index == 0)
			cycled = 1;
		else
			cycled = 0;
		end = -1;
	} else {
		index = wbc->range_start >> PAGE_SHIFT;
		end = wbc->range_end >> PAGE_SHIFT;
		if (wbc->range_start == 0 && wbc->range_end == LLONG_MAX)
			range_whole = 1;
		cycled = 1; /* ignore range_cyclic tests */
	}
	if (wbc->sync_mode == WB_SYNC_ALL || wbc->tagged_writepages)
		tag = PAGECACHE_TAG_TOWRITE;
	else
		tag = PAGECACHE_TAG_DIRTY;

retry:
	if (wbc->sync_mode == WB_SYNC_ALL || wbc->tagged_writepages)
		tag_pages_for_writeback(mapping, index, end);
	done_index = index;
	while (!done && (index <= end)) {
		nr_folios = filemap_get_folios_tag(mapping, &index, end,
				tag, &fbatch);
		if (nr_folios == 0)
			break;

		ret = gfs2_write_jdata_batch(mapping, wbc, &fbatch,
				&done_index);
		if (ret)
			done = 1;
		if (ret > 0)
			ret = 0;
		folio_batch_release(&fbatch);
		cond_resched();
	}

	if (!cycled && !done) {
		/*
		 * range_cyclic:
		 * We hit the last page and there is more work to be done: wrap
		 * back to the start of the file
		 */
		cycled = 1;
		index = 0;
		end = writeback_index - 1;
		goto retry;
	}

	if (wbc->range_cyclic || (range_whole && wbc->nr_to_write > 0))
		mapping->writeback_index = done_index;

	return ret;
}


/**
 * gfs2_jdata_writepages - Write a bunch of dirty pages back to disk
 * @mapping: The mapping to write
 * @wbc: The writeback control
 * 
 */

static int gfs2_jdata_writepages(struct address_space *mapping,
				 struct writeback_control *wbc)
{
	struct gfs2_inode *ip = GFS2_I(mapping->host);
	struct gfs2_sbd *sdp = GFS2_SB(mapping->host);
	int ret;

	ret = gfs2_write_cache_jdata(mapping, wbc);
	if (ret == 0 && wbc->sync_mode == WB_SYNC_ALL) {
		gfs2_log_flush(sdp, ip->i_gl, GFS2_LOG_HEAD_FLUSH_NORMAL |
			       GFS2_LFC_JDATA_WPAGES);
		ret = gfs2_write_cache_jdata(mapping, wbc);
	}
	return ret;
}

/**
 * stuffed_read_folio - Fill in a Linux folio with stuffed file data
 * @ip: the inode
 * @folio: the folio
 *
 * Returns: errno
 */
static int stuffed_read_folio(struct gfs2_inode *ip, struct folio *folio)
{
	struct buffer_head *dibh = NULL;
	size_t dsize = i_size_read(&ip->i_inode);
	void *from = NULL;
	int error = 0;

	/*
	 * Due to the order of unstuffing files and ->fault(), we can be
	 * asked for a zero folio in the case of a stuffed file being extended,
	 * so we need to supply one here. It doesn't happen often.
	 */
	if (unlikely(folio->index)) {
		dsize = 0;
	} else {
		error = gfs2_meta_inode_buffer(ip, &dibh);
		if (error)
			goto out;
		from = dibh->b_data + sizeof(struct gfs2_dinode);
	}

	folio_fill_tail(folio, 0, from, dsize);
	brelse(dibh);
out:
	folio_end_read(folio, error == 0);

	return error;
}

/**
 * gfs2_read_folio - read a folio from a file
 * @file: The file to read
 * @folio: The folio in the file
 */
static int gfs2_read_folio(struct file *file, struct folio *folio)
{
	struct inode *inode = folio->mapping->host;
	struct gfs2_inode *ip = GFS2_I(inode);
	struct gfs2_sbd *sdp = GFS2_SB(inode);
	int error;

	if (!gfs2_is_jdata(ip) ||
	    (i_blocksize(inode) == PAGE_SIZE && !folio_buffers(folio))) {
		error = iomap_read_folio(folio, &gfs2_iomap_ops);
	} else if (gfs2_is_stuffed(ip)) {
		error = stuffed_read_folio(ip, folio);
	} else {
		error = mpage_read_folio(folio, gfs2_block_map);
	}

	if (gfs2_withdrawing_or_withdrawn(sdp))
		return -EIO;

	return error;
}

/**
 * gfs2_internal_read - read an internal file
 * @ip: The gfs2 inode
 * @buf: The buffer to fill
 * @pos: The file position
 * @size: The amount to read
 *
 */

ssize_t gfs2_internal_read(struct gfs2_inode *ip, char *buf, loff_t *pos,
			   size_t size)
{
	struct address_space *mapping = ip->i_inode.i_mapping;
	unsigned long index = *pos >> PAGE_SHIFT;
	size_t copied = 0;

	do {
		size_t offset, chunk;
		struct folio *folio;

		folio = read_cache_folio(mapping, index, gfs2_read_folio, NULL);
		if (IS_ERR(folio)) {
			if (PTR_ERR(folio) == -EINTR)
				continue;
			return PTR_ERR(folio);
		}
		offset = *pos + copied - folio_pos(folio);
		chunk = min(size - copied, folio_size(folio) - offset);
		memcpy_from_folio(buf + copied, folio, offset, chunk);
		index = folio_next_index(folio);
		folio_put(folio);
		copied += chunk;
	} while(copied < size);
	(*pos) += size;
	return size;
}

/**
 * gfs2_readahead - Read a bunch of pages at once
 * @rac: Read-ahead control structure
 *
 * Some notes:
 * 1. This is only for readahead, so we can simply ignore any things
 *    which are slightly inconvenient (such as locking conflicts between
 *    the page lock and the glock) and return having done no I/O. Its
 *    obviously not something we'd want to do on too regular a basis.
 *    Any I/O we ignore at this time will be done via readpage later.
 * 2. We don't handle stuffed files here we let readpage do the honours.
 * 3. mpage_readahead() does most of the heavy lifting in the common case.
 * 4. gfs2_block_map() is relied upon to set BH_Boundary in the right places.
 */

static void gfs2_readahead(struct readahead_control *rac)
{
	struct inode *inode = rac->mapping->host;
	struct gfs2_inode *ip = GFS2_I(inode);

	if (gfs2_is_stuffed(ip))
		;
	else if (gfs2_is_jdata(ip))
		mpage_readahead(rac, gfs2_block_map);
	else
		iomap_readahead(rac, &gfs2_iomap_ops);
}

/**
 * adjust_fs_space - Adjusts the free space available due to gfs2_grow
 * @inode: the rindex inode
 */
void adjust_fs_space(struct inode *inode)
{
	struct gfs2_sbd *sdp = GFS2_SB(inode);
	struct gfs2_inode *m_ip = GFS2_I(sdp->sd_statfs_inode);
	struct gfs2_statfs_change_host *m_sc = &sdp->sd_statfs_master;
	struct gfs2_statfs_change_host *l_sc = &sdp->sd_statfs_local;
	struct buffer_head *m_bh;
	u64 fs_total, new_free;

	if (gfs2_trans_begin(sdp, 2 * RES_STATFS, 0) != 0)
		return;

	/* Total up the file system space, according to the latest rindex. */
	fs_total = gfs2_ri_total(sdp);
	if (gfs2_meta_inode_buffer(m_ip, &m_bh) != 0)
		goto out;

	spin_lock(&sdp->sd_statfs_spin);
	gfs2_statfs_change_in(m_sc, m_bh->b_data +
			      sizeof(struct gfs2_dinode));
	if (fs_total > (m_sc->sc_total + l_sc->sc_total))
		new_free = fs_total - (m_sc->sc_total + l_sc->sc_total);
	else
		new_free = 0;
	spin_unlock(&sdp->sd_statfs_spin);
	fs_warn(sdp, "File system extended by %llu blocks.\n",
		(unsigned long long)new_free);
	gfs2_statfs_change(sdp, new_free, new_free, 0);

	update_statfs(sdp, m_bh);
	brelse(m_bh);
out:
	sdp->sd_rindex_uptodate = 0;
	gfs2_trans_end(sdp);
}

static bool gfs2_jdata_dirty_folio(struct address_space *mapping,
		struct folio *folio)
{
	if (current->journal_info)
		folio_set_checked(folio);
	return block_dirty_folio(mapping, folio);
}

/**
 * gfs2_bmap - Block map function
 * @mapping: Address space info
 * @lblock: The block to map
 *
 * Returns: The disk address for the block or 0 on hole or error
 */

static sector_t gfs2_bmap(struct address_space *mapping, sector_t lblock)
{
	struct gfs2_inode *ip = GFS2_I(mapping->host);
	struct gfs2_holder i_gh;
	sector_t dblock = 0;
	int error;

	error = gfs2_glock_nq_init(ip->i_gl, LM_ST_SHARED, LM_FLAG_ANY, &i_gh);
	if (error)
		return 0;

	if (!gfs2_is_stuffed(ip))
		dblock = iomap_bmap(mapping, lblock, &gfs2_iomap_ops);

	gfs2_glock_dq_uninit(&i_gh);

	return dblock;
}

static void gfs2_discard(struct gfs2_sbd *sdp, struct buffer_head *bh)
{
	struct gfs2_bufdata *bd;

	lock_buffer(bh);
	gfs2_log_lock(sdp);
	clear_buffer_dirty(bh);
	bd = bh->b_private;
	if (bd) {
		if (!list_empty(&bd->bd_list) && !buffer_pinned(bh))
			list_del_init(&bd->bd_list);
		else {
			spin_lock(&sdp->sd_ail_lock);
			gfs2_remove_from_journal(bh, REMOVE_JDATA);
			spin_unlock(&sdp->sd_ail_lock);
		}
	}
	bh->b_bdev = NULL;
	clear_buffer_mapped(bh);
	clear_buffer_req(bh);
	clear_buffer_new(bh);
	gfs2_log_unlock(sdp);
	unlock_buffer(bh);
}

static void gfs2_invalidate_folio(struct folio *folio, size_t offset,
				size_t length)
{
	struct gfs2_sbd *sdp = GFS2_SB(folio->mapping->host);
	size_t stop = offset + length;
	int partial_page = (offset || length < folio_size(folio));
	struct buffer_head *bh, *head;
	unsigned long pos = 0;

	BUG_ON(!folio_test_locked(folio));
	if (!partial_page)
		folio_clear_checked(folio);
	head = folio_buffers(folio);
	if (!head)
		goto out;

	bh = head;
	do {
		if (pos + bh->b_size > stop)
			return;

		if (offset <= pos)
			gfs2_discard(sdp, bh);
		pos += bh->b_size;
		bh = bh->b_this_page;
	} while (bh != head);
out:
	if (!partial_page)
		filemap_release_folio(folio, 0);
}

/**
 * gfs2_release_folio - free the metadata associated with a folio
 * @folio: the folio that's being released
 * @gfp_mask: passed from Linux VFS, ignored by us
 *
 * Calls try_to_free_buffers() to free the buffers and put the folio if the
 * buffers can be released.
 *
 * Returns: true if the folio was put or else false
 */

bool gfs2_release_folio(struct folio *folio, gfp_t gfp_mask)
{
	struct address_space *mapping = folio->mapping;
	struct gfs2_sbd *sdp = gfs2_mapping2sbd(mapping);
	struct buffer_head *bh, *head;
	struct gfs2_bufdata *bd;

	head = folio_buffers(folio);
	if (!head)
		return false;

	/*
	 * mm accommodates an old ext3 case where clean folios might
	 * not have had the dirty bit cleared.	Thus, it can send actual
	 * dirty folios to ->release_folio() via shrink_active_list().
	 *
	 * As a workaround, we skip folios that contain dirty buffers
	 * below.  Once ->release_folio isn't called on dirty folios
	 * anymore, we can warn on dirty buffers like we used to here
	 * again.
	 */

	gfs2_log_lock(sdp);
	bh = head;
	do {
		if (atomic_read(&bh->b_count))
			goto cannot_release;
		bd = bh->b_private;
		if (bd && bd->bd_tr)
			goto cannot_release;
		if (buffer_dirty(bh) || WARN_ON(buffer_pinned(bh)))
			goto cannot_release;
		bh = bh->b_this_page;
	} while (bh != head);

	bh = head;
	do {
		bd = bh->b_private;
		if (bd) {
			gfs2_assert_warn(sdp, bd->bd_bh == bh);
			bd->bd_bh = NULL;
			bh->b_private = NULL;
			/*
			 * The bd may still be queued as a revoke, in which
			 * case we must not dequeue nor free it.
			 */
			if (!bd->bd_blkno && !list_empty(&bd->bd_list))
				list_del_init(&bd->bd_list);
			if (list_empty(&bd->bd_list))
				kmem_cache_free(gfs2_bufdata_cachep, bd);
		}

		bh = bh->b_this_page;
	} while (bh != head);
	gfs2_log_unlock(sdp);

	return try_to_free_buffers(folio);

cannot_release:
	gfs2_log_unlock(sdp);
	return false;
}

static const struct address_space_operations gfs2_aops = {
	.writepages = gfs2_writepages,
	.read_folio = gfs2_read_folio,
	.readahead = gfs2_readahead,
	.dirty_folio = iomap_dirty_folio,
	.release_folio = iomap_release_folio,
	.invalidate_folio = iomap_invalidate_folio,
	.bmap = gfs2_bmap,
	.migrate_folio = filemap_migrate_folio,
	.is_partially_uptodate = iomap_is_partially_uptodate,
	.error_remove_folio = generic_error_remove_folio,
};

static const struct address_space_operations gfs2_jdata_aops = {
	.writepages = gfs2_jdata_writepages,
	.read_folio = gfs2_read_folio,
	.readahead = gfs2_readahead,
	.dirty_folio = gfs2_jdata_dirty_folio,
	.bmap = gfs2_bmap,
	.migrate_folio = buffer_migrate_folio,
	.invalidate_folio = gfs2_invalidate_folio,
	.release_folio = gfs2_release_folio,
	.is_partially_uptodate = block_is_partially_uptodate,
	.error_remove_folio = generic_error_remove_folio,
};

void gfs2_set_aops(struct inode *inode)
{
	if (gfs2_is_jdata(GFS2_I(inode)))
		inode->i_mapping->a_ops = &gfs2_jdata_aops;
	else
		inode->i_mapping->a_ops = &gfs2_aops;
}
