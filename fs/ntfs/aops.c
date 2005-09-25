/**
 * aops.c - NTFS kernel address space operations and page cache handling.
 *	    Part of the Linux-NTFS project.
 *
 * Copyright (c) 2001-2005 Anton Altaparmakov
 * Copyright (c) 2002 Richard Russon
 *
 * This program/include file is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as published
 * by the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program/include file is distributed in the hope that it will be
 * useful, but WITHOUT ANY WARRANTY; without even the implied warranty
 * of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program (in the main directory of the Linux-NTFS
 * distribution in the file COPYING); if not, write to the Free Software
 * Foundation,Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include <linux/errno.h>
#include <linux/mm.h>
#include <linux/pagemap.h>
#include <linux/swap.h>
#include <linux/buffer_head.h>
#include <linux/writeback.h>
#include <linux/bit_spinlock.h>

#include "aops.h"
#include "attrib.h"
#include "debug.h"
#include "inode.h"
#include "mft.h"
#include "runlist.h"
#include "types.h"
#include "ntfs.h"

/**
 * ntfs_end_buffer_async_read - async io completion for reading attributes
 * @bh:		buffer head on which io is completed
 * @uptodate:	whether @bh is now uptodate or not
 *
 * Asynchronous I/O completion handler for reading pages belonging to the
 * attribute address space of an inode.  The inodes can either be files or
 * directories or they can be fake inodes describing some attribute.
 *
 * If NInoMstProtected(), perform the post read mst fixups when all IO on the
 * page has been completed and mark the page uptodate or set the error bit on
 * the page.  To determine the size of the records that need fixing up, we
 * cheat a little bit by setting the index_block_size in ntfs_inode to the ntfs
 * record size, and index_block_size_bits, to the log(base 2) of the ntfs
 * record size.
 */
static void ntfs_end_buffer_async_read(struct buffer_head *bh, int uptodate)
{
	unsigned long flags;
	struct buffer_head *first, *tmp;
	struct page *page;
	struct inode *vi;
	ntfs_inode *ni;
	int page_uptodate = 1;

	page = bh->b_page;
	vi = page->mapping->host;
	ni = NTFS_I(vi);

	if (likely(uptodate)) {
		loff_t i_size;
		s64 file_ofs, init_size;

		set_buffer_uptodate(bh);

		file_ofs = ((s64)page->index << PAGE_CACHE_SHIFT) +
				bh_offset(bh);
		read_lock_irqsave(&ni->size_lock, flags);
		init_size = ni->initialized_size;
		i_size = i_size_read(vi);
		read_unlock_irqrestore(&ni->size_lock, flags);
		if (unlikely(init_size > i_size)) {
			/* Race with shrinking truncate. */
			init_size = i_size;
		}
		/* Check for the current buffer head overflowing. */
		if (unlikely(file_ofs + bh->b_size > init_size)) {
			u8 *kaddr;
			int ofs;

			ofs = 0;
			if (file_ofs < init_size)
				ofs = init_size - file_ofs;
			kaddr = kmap_atomic(page, KM_BIO_SRC_IRQ);
			memset(kaddr + bh_offset(bh) + ofs, 0,
					bh->b_size - ofs);
			kunmap_atomic(kaddr, KM_BIO_SRC_IRQ);
			flush_dcache_page(page);
		}
	} else {
		clear_buffer_uptodate(bh);
		SetPageError(page);
		ntfs_error(ni->vol->sb, "Buffer I/O error, logical block "
				"0x%llx.", (unsigned long long)bh->b_blocknr);
	}
	first = page_buffers(page);
	local_irq_save(flags);
	bit_spin_lock(BH_Uptodate_Lock, &first->b_state);
	clear_buffer_async_read(bh);
	unlock_buffer(bh);
	tmp = bh;
	do {
		if (!buffer_uptodate(tmp))
			page_uptodate = 0;
		if (buffer_async_read(tmp)) {
			if (likely(buffer_locked(tmp)))
				goto still_busy;
			/* Async buffers must be locked. */
			BUG();
		}
		tmp = tmp->b_this_page;
	} while (tmp != bh);
	bit_spin_unlock(BH_Uptodate_Lock, &first->b_state);
	local_irq_restore(flags);
	/*
	 * If none of the buffers had errors then we can set the page uptodate,
	 * but we first have to perform the post read mst fixups, if the
	 * attribute is mst protected, i.e. if NInoMstProteced(ni) is true.
	 * Note we ignore fixup errors as those are detected when
	 * map_mft_record() is called which gives us per record granularity
	 * rather than per page granularity.
	 */
	if (!NInoMstProtected(ni)) {
		if (likely(page_uptodate && !PageError(page)))
			SetPageUptodate(page);
	} else {
		u8 *kaddr;
		unsigned int i, recs;
		u32 rec_size;

		rec_size = ni->itype.index.block_size;
		recs = PAGE_CACHE_SIZE / rec_size;
		/* Should have been verified before we got here... */
		BUG_ON(!recs);
		kaddr = kmap_atomic(page, KM_BIO_SRC_IRQ);
		for (i = 0; i < recs; i++)
			post_read_mst_fixup((NTFS_RECORD*)(kaddr +
					i * rec_size), rec_size);
		kunmap_atomic(kaddr, KM_BIO_SRC_IRQ);
		flush_dcache_page(page);
		if (likely(page_uptodate && !PageError(page)))
			SetPageUptodate(page);
	}
	unlock_page(page);
	return;
still_busy:
	bit_spin_unlock(BH_Uptodate_Lock, &first->b_state);
	local_irq_restore(flags);
	return;
}

/**
 * ntfs_read_block - fill a @page of an address space with data
 * @page:	page cache page to fill with data
 *
 * Fill the page @page of the address space belonging to the @page->host inode.
 * We read each buffer asynchronously and when all buffers are read in, our io
 * completion handler ntfs_end_buffer_read_async(), if required, automatically
 * applies the mst fixups to the page before finally marking it uptodate and
 * unlocking it.
 *
 * We only enforce allocated_size limit because i_size is checked for in
 * generic_file_read().
 *
 * Return 0 on success and -errno on error.
 *
 * Contains an adapted version of fs/buffer.c::block_read_full_page().
 */
static int ntfs_read_block(struct page *page)
{
	loff_t i_size;
	VCN vcn;
	LCN lcn;
	s64 init_size;
	struct inode *vi;
	ntfs_inode *ni;
	ntfs_volume *vol;
	runlist_element *rl;
	struct buffer_head *bh, *head, *arr[MAX_BUF_PER_PAGE];
	sector_t iblock, lblock, zblock;
	unsigned long flags;
	unsigned int blocksize, vcn_ofs;
	int i, nr;
	unsigned char blocksize_bits;

	vi = page->mapping->host;
	ni = NTFS_I(vi);
	vol = ni->vol;

	/* $MFT/$DATA must have its complete runlist in memory at all times. */
	BUG_ON(!ni->runlist.rl && !ni->mft_no && !NInoAttr(ni));

	blocksize_bits = VFS_I(ni)->i_blkbits;
	blocksize = 1 << blocksize_bits;

	if (!page_has_buffers(page)) {
		create_empty_buffers(page, blocksize, 0);
		if (unlikely(!page_has_buffers(page))) {
			unlock_page(page);
			return -ENOMEM;
		}
	}
	bh = head = page_buffers(page);
	BUG_ON(!bh);

	/*
	 * We may be racing with truncate.  To avoid some of the problems we
	 * now take a snapshot of the various sizes and use those for the whole
	 * of the function.  In case of an extending truncate it just means we
	 * may leave some buffers unmapped which are now allocated.  This is
	 * not a problem since these buffers will just get mapped when a write
	 * occurs.  In case of a shrinking truncate, we will detect this later
	 * on due to the runlist being incomplete and if the page is being
	 * fully truncated, truncate will throw it away as soon as we unlock
	 * it so no need to worry what we do with it.
	 */
	iblock = (s64)page->index << (PAGE_CACHE_SHIFT - blocksize_bits);
	read_lock_irqsave(&ni->size_lock, flags);
	lblock = (ni->allocated_size + blocksize - 1) >> blocksize_bits;
	init_size = ni->initialized_size;
	i_size = i_size_read(vi);
	read_unlock_irqrestore(&ni->size_lock, flags);
	if (unlikely(init_size > i_size)) {
		/* Race with shrinking truncate. */
		init_size = i_size;
	}
	zblock = (init_size + blocksize - 1) >> blocksize_bits;

	/* Loop through all the buffers in the page. */
	rl = NULL;
	nr = i = 0;
	do {
		u8 *kaddr;
		int err;

		if (unlikely(buffer_uptodate(bh)))
			continue;
		if (unlikely(buffer_mapped(bh))) {
			arr[nr++] = bh;
			continue;
		}
		err = 0;
		bh->b_bdev = vol->sb->s_bdev;
		/* Is the block within the allowed limits? */
		if (iblock < lblock) {
			BOOL is_retry = FALSE;

			/* Convert iblock into corresponding vcn and offset. */
			vcn = (VCN)iblock << blocksize_bits >>
					vol->cluster_size_bits;
			vcn_ofs = ((VCN)iblock << blocksize_bits) &
					vol->cluster_size_mask;
			if (!rl) {
lock_retry_remap:
				down_read(&ni->runlist.lock);
				rl = ni->runlist.rl;
			}
			if (likely(rl != NULL)) {
				/* Seek to element containing target vcn. */
				while (rl->length && rl[1].vcn <= vcn)
					rl++;
				lcn = ntfs_rl_vcn_to_lcn(rl, vcn);
			} else
				lcn = LCN_RL_NOT_MAPPED;
			/* Successful remap. */
			if (lcn >= 0) {
				/* Setup buffer head to correct block. */
				bh->b_blocknr = ((lcn << vol->cluster_size_bits)
						+ vcn_ofs) >> blocksize_bits;
				set_buffer_mapped(bh);
				/* Only read initialized data blocks. */
				if (iblock < zblock) {
					arr[nr++] = bh;
					continue;
				}
				/* Fully non-initialized data block, zero it. */
				goto handle_zblock;
			}
			/* It is a hole, need to zero it. */
			if (lcn == LCN_HOLE)
				goto handle_hole;
			/* If first try and runlist unmapped, map and retry. */
			if (!is_retry && lcn == LCN_RL_NOT_MAPPED) {
				is_retry = TRUE;
				/*
				 * Attempt to map runlist, dropping lock for
				 * the duration.
				 */
				up_read(&ni->runlist.lock);
				err = ntfs_map_runlist(ni, vcn);
				if (likely(!err))
					goto lock_retry_remap;
				rl = NULL;
			} else if (!rl)
				up_read(&ni->runlist.lock);
			/*
			 * If buffer is outside the runlist, treat it as a
			 * hole.  This can happen due to concurrent truncate
			 * for example.
			 */
			if (err == -ENOENT || lcn == LCN_ENOENT) {
				err = 0;
				goto handle_hole;
			}
			/* Hard error, zero out region. */
			if (!err)
				err = -EIO;
			bh->b_blocknr = -1;
			SetPageError(page);
			ntfs_error(vol->sb, "Failed to read from inode 0x%lx, "
					"attribute type 0x%x, vcn 0x%llx, "
					"offset 0x%x because its location on "
					"disk could not be determined%s "
					"(error code %i).", ni->mft_no,
					ni->type, (unsigned long long)vcn,
					vcn_ofs, is_retry ? " even after "
					"retrying" : "", err);
		}
		/*
		 * Either iblock was outside lblock limits or
		 * ntfs_rl_vcn_to_lcn() returned error.  Just zero that portion
		 * of the page and set the buffer uptodate.
		 */
handle_hole:
		bh->b_blocknr = -1UL;
		clear_buffer_mapped(bh);
handle_zblock:
		kaddr = kmap_atomic(page, KM_USER0);
		memset(kaddr + i * blocksize, 0, blocksize);
		kunmap_atomic(kaddr, KM_USER0);
		flush_dcache_page(page);
		if (likely(!err))
			set_buffer_uptodate(bh);
	} while (i++, iblock++, (bh = bh->b_this_page) != head);

	/* Release the lock if we took it. */
	if (rl)
		up_read(&ni->runlist.lock);

	/* Check we have at least one buffer ready for i/o. */
	if (nr) {
		struct buffer_head *tbh;

		/* Lock the buffers. */
		for (i = 0; i < nr; i++) {
			tbh = arr[i];
			lock_buffer(tbh);
			tbh->b_end_io = ntfs_end_buffer_async_read;
			set_buffer_async_read(tbh);
		}
		/* Finally, start i/o on the buffers. */
		for (i = 0; i < nr; i++) {
			tbh = arr[i];
			if (likely(!buffer_uptodate(tbh)))
				submit_bh(READ, tbh);
			else
				ntfs_end_buffer_async_read(tbh, 1);
		}
		return 0;
	}
	/* No i/o was scheduled on any of the buffers. */
	if (likely(!PageError(page)))
		SetPageUptodate(page);
	else /* Signal synchronous i/o error. */
		nr = -EIO;
	unlock_page(page);
	return nr;
}

/**
 * ntfs_readpage - fill a @page of a @file with data from the device
 * @file:	open file to which the page @page belongs or NULL
 * @page:	page cache page to fill with data
 *
 * For non-resident attributes, ntfs_readpage() fills the @page of the open
 * file @file by calling the ntfs version of the generic block_read_full_page()
 * function, ntfs_read_block(), which in turn creates and reads in the buffers
 * associated with the page asynchronously.
 *
 * For resident attributes, OTOH, ntfs_readpage() fills @page by copying the
 * data from the mft record (which at this stage is most likely in memory) and
 * fills the remainder with zeroes. Thus, in this case, I/O is synchronous, as
 * even if the mft record is not cached at this point in time, we need to wait
 * for it to be read in before we can do the copy.
 *
 * Return 0 on success and -errno on error.
 */
static int ntfs_readpage(struct file *file, struct page *page)
{
	loff_t i_size;
	struct inode *vi;
	ntfs_inode *ni, *base_ni;
	u8 *kaddr;
	ntfs_attr_search_ctx *ctx;
	MFT_RECORD *mrec;
	unsigned long flags;
	u32 attr_len;
	int err = 0;

retry_readpage:
	BUG_ON(!PageLocked(page));
	/*
	 * This can potentially happen because we clear PageUptodate() during
	 * ntfs_writepage() of MstProtected() attributes.
	 */
	if (PageUptodate(page)) {
		unlock_page(page);
		return 0;
	}
	vi = page->mapping->host;
	ni = NTFS_I(vi);
	/*
	 * Only $DATA attributes can be encrypted and only unnamed $DATA
	 * attributes can be compressed.  Index root can have the flags set but
	 * this means to create compressed/encrypted files, not that the
	 * attribute is compressed/encrypted.  Note we need to check for
	 * AT_INDEX_ALLOCATION since this is the type of both directory and
	 * index inodes.
	 */
	if (ni->type != AT_INDEX_ALLOCATION) {
		/* If attribute is encrypted, deny access, just like NT4. */
		if (NInoEncrypted(ni)) {
			BUG_ON(ni->type != AT_DATA);
			err = -EACCES;
			goto err_out;
		}
		/* Compressed data streams are handled in compress.c. */
		if (NInoNonResident(ni) && NInoCompressed(ni)) {
			BUG_ON(ni->type != AT_DATA);
			BUG_ON(ni->name_len);
			return ntfs_read_compressed_block(page);
		}
	}
	/* NInoNonResident() == NInoIndexAllocPresent() */
	if (NInoNonResident(ni)) {
		/* Normal, non-resident data stream. */
		return ntfs_read_block(page);
	}
	/*
	 * Attribute is resident, implying it is not compressed or encrypted.
	 * This also means the attribute is smaller than an mft record and
	 * hence smaller than a page, so can simply zero out any pages with
	 * index above 0.  Note the attribute can actually be marked compressed
	 * but if it is resident the actual data is not compressed so we are
	 * ok to ignore the compressed flag here.
	 */
	if (unlikely(page->index > 0)) {
		kaddr = kmap_atomic(page, KM_USER0);
		memset(kaddr, 0, PAGE_CACHE_SIZE);
		flush_dcache_page(page);
		kunmap_atomic(kaddr, KM_USER0);
		goto done;
	}
	if (!NInoAttr(ni))
		base_ni = ni;
	else
		base_ni = ni->ext.base_ntfs_ino;
	/* Map, pin, and lock the mft record. */
	mrec = map_mft_record(base_ni);
	if (IS_ERR(mrec)) {
		err = PTR_ERR(mrec);
		goto err_out;
	}
	/*
	 * If a parallel write made the attribute non-resident, drop the mft
	 * record and retry the readpage.
	 */
	if (unlikely(NInoNonResident(ni))) {
		unmap_mft_record(base_ni);
		goto retry_readpage;
	}
	ctx = ntfs_attr_get_search_ctx(base_ni, mrec);
	if (unlikely(!ctx)) {
		err = -ENOMEM;
		goto unm_err_out;
	}
	err = ntfs_attr_lookup(ni->type, ni->name, ni->name_len,
			CASE_SENSITIVE, 0, NULL, 0, ctx);
	if (unlikely(err))
		goto put_unm_err_out;
	attr_len = le32_to_cpu(ctx->attr->data.resident.value_length);
	read_lock_irqsave(&ni->size_lock, flags);
	if (unlikely(attr_len > ni->initialized_size))
		attr_len = ni->initialized_size;
	i_size = i_size_read(vi);
	read_unlock_irqrestore(&ni->size_lock, flags);
	if (unlikely(attr_len > i_size)) {
		/* Race with shrinking truncate. */
		attr_len = i_size;
	}
	kaddr = kmap_atomic(page, KM_USER0);
	/* Copy the data to the page. */
	memcpy(kaddr, (u8*)ctx->attr +
			le16_to_cpu(ctx->attr->data.resident.value_offset),
			attr_len);
	/* Zero the remainder of the page. */
	memset(kaddr + attr_len, 0, PAGE_CACHE_SIZE - attr_len);
	flush_dcache_page(page);
	kunmap_atomic(kaddr, KM_USER0);
put_unm_err_out:
	ntfs_attr_put_search_ctx(ctx);
unm_err_out:
	unmap_mft_record(base_ni);
done:
	SetPageUptodate(page);
err_out:
	unlock_page(page);
	return err;
}

#ifdef NTFS_RW

/**
 * ntfs_write_block - write a @page to the backing store
 * @page:	page cache page to write out
 * @wbc:	writeback control structure
 *
 * This function is for writing pages belonging to non-resident, non-mst
 * protected attributes to their backing store.
 *
 * For a page with buffers, map and write the dirty buffers asynchronously
 * under page writeback. For a page without buffers, create buffers for the
 * page, then proceed as above.
 *
 * If a page doesn't have buffers the page dirty state is definitive. If a page
 * does have buffers, the page dirty state is just a hint, and the buffer dirty
 * state is definitive. (A hint which has rules: dirty buffers against a clean
 * page is illegal. Other combinations are legal and need to be handled. In
 * particular a dirty page containing clean buffers for example.)
 *
 * Return 0 on success and -errno on error.
 *
 * Based on ntfs_read_block() and __block_write_full_page().
 */
static int ntfs_write_block(struct page *page, struct writeback_control *wbc)
{
	VCN vcn;
	LCN lcn;
	s64 initialized_size;
	loff_t i_size;
	sector_t block, dblock, iblock;
	struct inode *vi;
	ntfs_inode *ni;
	ntfs_volume *vol;
	runlist_element *rl;
	struct buffer_head *bh, *head;
	unsigned long flags;
	unsigned int blocksize, vcn_ofs;
	int err;
	BOOL need_end_writeback;
	unsigned char blocksize_bits;

	vi = page->mapping->host;
	ni = NTFS_I(vi);
	vol = ni->vol;

	ntfs_debug("Entering for inode 0x%lx, attribute type 0x%x, page index "
			"0x%lx.", ni->mft_no, ni->type, page->index);

	BUG_ON(!NInoNonResident(ni));
	BUG_ON(NInoMstProtected(ni));

	blocksize_bits = vi->i_blkbits;
	blocksize = 1 << blocksize_bits;

	if (!page_has_buffers(page)) {
		BUG_ON(!PageUptodate(page));
		create_empty_buffers(page, blocksize,
				(1 << BH_Uptodate) | (1 << BH_Dirty));
		if (unlikely(!page_has_buffers(page))) {
			ntfs_warning(vol->sb, "Error allocating page "
					"buffers.  Redirtying page so we try "
					"again later.");
			/*
			 * Put the page back on mapping->dirty_pages, but leave
			 * its buffers' dirty state as-is.
			 */
			redirty_page_for_writepage(wbc, page);
			unlock_page(page);
			return 0;
		}
	}
	bh = head = page_buffers(page);
	BUG_ON(!bh);

	/* NOTE: Different naming scheme to ntfs_read_block()! */

	/* The first block in the page. */
	block = (s64)page->index << (PAGE_CACHE_SHIFT - blocksize_bits);

	read_lock_irqsave(&ni->size_lock, flags);
	i_size = i_size_read(vi);
	initialized_size = ni->initialized_size;
	read_unlock_irqrestore(&ni->size_lock, flags);

	/* The first out of bounds block for the data size. */
	dblock = (i_size + blocksize - 1) >> blocksize_bits;

	/* The last (fully or partially) initialized block. */
	iblock = initialized_size >> blocksize_bits;

	/*
	 * Be very careful.  We have no exclusion from __set_page_dirty_buffers
	 * here, and the (potentially unmapped) buffers may become dirty at
	 * any time.  If a buffer becomes dirty here after we've inspected it
	 * then we just miss that fact, and the page stays dirty.
	 *
	 * Buffers outside i_size may be dirtied by __set_page_dirty_buffers;
	 * handle that here by just cleaning them.
	 */

	/*
	 * Loop through all the buffers in the page, mapping all the dirty
	 * buffers to disk addresses and handling any aliases from the
	 * underlying block device's mapping.
	 */
	rl = NULL;
	err = 0;
	do {
		BOOL is_retry = FALSE;

		if (unlikely(block >= dblock)) {
			/*
			 * Mapped buffers outside i_size will occur, because
			 * this page can be outside i_size when there is a
			 * truncate in progress. The contents of such buffers
			 * were zeroed by ntfs_writepage().
			 *
			 * FIXME: What about the small race window where
			 * ntfs_writepage() has not done any clearing because
			 * the page was within i_size but before we get here,
			 * vmtruncate() modifies i_size?
			 */
			clear_buffer_dirty(bh);
			set_buffer_uptodate(bh);
			continue;
		}

		/* Clean buffers are not written out, so no need to map them. */
		if (!buffer_dirty(bh))
			continue;

		/* Make sure we have enough initialized size. */
		if (unlikely((block >= iblock) &&
				(initialized_size < i_size))) {
			/*
			 * If this page is fully outside initialized size, zero
			 * out all pages between the current initialized size
			 * and the current page. Just use ntfs_readpage() to do
			 * the zeroing transparently.
			 */
			if (block > iblock) {
				// TODO:
				// For each page do:
				// - read_cache_page()
				// Again for each page do:
				// - wait_on_page_locked()
				// - Check (PageUptodate(page) &&
				//			!PageError(page))
				// Update initialized size in the attribute and
				// in the inode.
				// Again, for each page do:
				//	__set_page_dirty_buffers();
				// page_cache_release()
				// We don't need to wait on the writes.
				// Update iblock.
			}
			/*
			 * The current page straddles initialized size. Zero
			 * all non-uptodate buffers and set them uptodate (and
			 * dirty?). Note, there aren't any non-uptodate buffers
			 * if the page is uptodate.
			 * FIXME: For an uptodate page, the buffers may need to
			 * be written out because they were not initialized on
			 * disk before.
			 */
			if (!PageUptodate(page)) {
				// TODO:
				// Zero any non-uptodate buffers up to i_size.
				// Set them uptodate and dirty.
			}
			// TODO:
			// Update initialized size in the attribute and in the
			// inode (up to i_size).
			// Update iblock.
			// FIXME: This is inefficient. Try to batch the two
			// size changes to happen in one go.
			ntfs_error(vol->sb, "Writing beyond initialized size "
					"is not supported yet. Sorry.");
			err = -EOPNOTSUPP;
			break;
			// Do NOT set_buffer_new() BUT DO clear buffer range
			// outside write request range.
			// set_buffer_uptodate() on complete buffers as well as
			// set_buffer_dirty().
		}

		/* No need to map buffers that are already mapped. */
		if (buffer_mapped(bh))
			continue;

		/* Unmapped, dirty buffer. Need to map it. */
		bh->b_bdev = vol->sb->s_bdev;

		/* Convert block into corresponding vcn and offset. */
		vcn = (VCN)block << blocksize_bits;
		vcn_ofs = vcn & vol->cluster_size_mask;
		vcn >>= vol->cluster_size_bits;
		if (!rl) {
lock_retry_remap:
			down_read(&ni->runlist.lock);
			rl = ni->runlist.rl;
		}
		if (likely(rl != NULL)) {
			/* Seek to element containing target vcn. */
			while (rl->length && rl[1].vcn <= vcn)
				rl++;
			lcn = ntfs_rl_vcn_to_lcn(rl, vcn);
		} else
			lcn = LCN_RL_NOT_MAPPED;
		/* Successful remap. */
		if (lcn >= 0) {
			/* Setup buffer head to point to correct block. */
			bh->b_blocknr = ((lcn << vol->cluster_size_bits) +
					vcn_ofs) >> blocksize_bits;
			set_buffer_mapped(bh);
			continue;
		}
		/* It is a hole, need to instantiate it. */
		if (lcn == LCN_HOLE) {
			u8 *kaddr;
			unsigned long *bpos, *bend;

			/* Check if the buffer is zero. */
			kaddr = kmap_atomic(page, KM_USER0);
			bpos = (unsigned long *)(kaddr + bh_offset(bh));
			bend = (unsigned long *)((u8*)bpos + blocksize);
			do {
				if (unlikely(*bpos))
					break;
			} while (likely(++bpos < bend));
			kunmap_atomic(kaddr, KM_USER0);
			if (bpos == bend) {
				/*
				 * Buffer is zero and sparse, no need to write
				 * it.
				 */
				bh->b_blocknr = -1;
				clear_buffer_dirty(bh);
				continue;
			}
			// TODO: Instantiate the hole.
			// clear_buffer_new(bh);
			// unmap_underlying_metadata(bh->b_bdev, bh->b_blocknr);
			ntfs_error(vol->sb, "Writing into sparse regions is "
					"not supported yet. Sorry.");
			err = -EOPNOTSUPP;
			break;
		}
		/* If first try and runlist unmapped, map and retry. */
		if (!is_retry && lcn == LCN_RL_NOT_MAPPED) {
			is_retry = TRUE;
			/*
			 * Attempt to map runlist, dropping lock for
			 * the duration.
			 */
			up_read(&ni->runlist.lock);
			err = ntfs_map_runlist(ni, vcn);
			if (likely(!err))
				goto lock_retry_remap;
			rl = NULL;
		} else if (!rl)
			up_read(&ni->runlist.lock);
		/*
		 * If buffer is outside the runlist, truncate has cut it out
		 * of the runlist.  Just clean and clear the buffer and set it
		 * uptodate so it can get discarded by the VM.
		 */
		if (err == -ENOENT || lcn == LCN_ENOENT) {
			u8 *kaddr;

			bh->b_blocknr = -1;
			clear_buffer_dirty(bh);
			kaddr = kmap_atomic(page, KM_USER0);
			memset(kaddr + bh_offset(bh), 0, blocksize);
			kunmap_atomic(kaddr, KM_USER0);
			flush_dcache_page(page);
			set_buffer_uptodate(bh);
			err = 0;
			continue;
		}
		/* Failed to map the buffer, even after retrying. */
		if (!err)
			err = -EIO;
		bh->b_blocknr = -1;
		ntfs_error(vol->sb, "Failed to write to inode 0x%lx, "
				"attribute type 0x%x, vcn 0x%llx, offset 0x%x "
				"because its location on disk could not be "
				"determined%s (error code %i).", ni->mft_no,
				ni->type, (unsigned long long)vcn,
				vcn_ofs, is_retry ? " even after "
				"retrying" : "", err);
		break;
	} while (block++, (bh = bh->b_this_page) != head);

	/* Release the lock if we took it. */
	if (rl)
		up_read(&ni->runlist.lock);

	/* For the error case, need to reset bh to the beginning. */
	bh = head;

	/* Just an optimization, so ->readpage() is not called later. */
	if (unlikely(!PageUptodate(page))) {
		int uptodate = 1;
		do {
			if (!buffer_uptodate(bh)) {
				uptodate = 0;
				bh = head;
				break;
			}
		} while ((bh = bh->b_this_page) != head);
		if (uptodate)
			SetPageUptodate(page);
	}

	/* Setup all mapped, dirty buffers for async write i/o. */
	do {
		if (buffer_mapped(bh) && buffer_dirty(bh)) {
			lock_buffer(bh);
			if (test_clear_buffer_dirty(bh)) {
				BUG_ON(!buffer_uptodate(bh));
				mark_buffer_async_write(bh);
			} else
				unlock_buffer(bh);
		} else if (unlikely(err)) {
			/*
			 * For the error case. The buffer may have been set
			 * dirty during attachment to a dirty page.
			 */
			if (err != -ENOMEM)
				clear_buffer_dirty(bh);
		}
	} while ((bh = bh->b_this_page) != head);

	if (unlikely(err)) {
		// TODO: Remove the -EOPNOTSUPP check later on...
		if (unlikely(err == -EOPNOTSUPP))
			err = 0;
		else if (err == -ENOMEM) {
			ntfs_warning(vol->sb, "Error allocating memory. "
					"Redirtying page so we try again "
					"later.");
			/*
			 * Put the page back on mapping->dirty_pages, but
			 * leave its buffer's dirty state as-is.
			 */
			redirty_page_for_writepage(wbc, page);
			err = 0;
		} else
			SetPageError(page);
	}

	BUG_ON(PageWriteback(page));
	set_page_writeback(page);	/* Keeps try_to_free_buffers() away. */

	/* Submit the prepared buffers for i/o. */
	need_end_writeback = TRUE;
	do {
		struct buffer_head *next = bh->b_this_page;
		if (buffer_async_write(bh)) {
			submit_bh(WRITE, bh);
			need_end_writeback = FALSE;
		}
		bh = next;
	} while (bh != head);
	unlock_page(page);

	/* If no i/o was started, need to end_page_writeback(). */
	if (unlikely(need_end_writeback))
		end_page_writeback(page);

	ntfs_debug("Done.");
	return err;
}

/**
 * ntfs_write_mst_block - write a @page to the backing store
 * @page:	page cache page to write out
 * @wbc:	writeback control structure
 *
 * This function is for writing pages belonging to non-resident, mst protected
 * attributes to their backing store.  The only supported attributes are index
 * allocation and $MFT/$DATA.  Both directory inodes and index inodes are
 * supported for the index allocation case.
 *
 * The page must remain locked for the duration of the write because we apply
 * the mst fixups, write, and then undo the fixups, so if we were to unlock the
 * page before undoing the fixups, any other user of the page will see the
 * page contents as corrupt.
 *
 * We clear the page uptodate flag for the duration of the function to ensure
 * exclusion for the $MFT/$DATA case against someone mapping an mft record we
 * are about to apply the mst fixups to.
 *
 * Return 0 on success and -errno on error.
 *
 * Based on ntfs_write_block(), ntfs_mft_writepage(), and
 * write_mft_record_nolock().
 */
static int ntfs_write_mst_block(struct page *page,
		struct writeback_control *wbc)
{
	sector_t block, dblock, rec_block;
	struct inode *vi = page->mapping->host;
	ntfs_inode *ni = NTFS_I(vi);
	ntfs_volume *vol = ni->vol;
	u8 *kaddr;
	unsigned int rec_size = ni->itype.index.block_size;
	ntfs_inode *locked_nis[PAGE_CACHE_SIZE / rec_size];
	struct buffer_head *bh, *head, *tbh, *rec_start_bh;
	struct buffer_head *bhs[MAX_BUF_PER_PAGE];
	runlist_element *rl;
	int i, nr_locked_nis, nr_recs, nr_bhs, max_bhs, bhs_per_rec, err, err2;
	unsigned bh_size, rec_size_bits;
	BOOL sync, is_mft, page_is_dirty, rec_is_dirty;
	unsigned char bh_size_bits;

	ntfs_debug("Entering for inode 0x%lx, attribute type 0x%x, page index "
			"0x%lx.", vi->i_ino, ni->type, page->index);
	BUG_ON(!NInoNonResident(ni));
	BUG_ON(!NInoMstProtected(ni));
	is_mft = (S_ISREG(vi->i_mode) && !vi->i_ino);
	/*
	 * NOTE: ntfs_write_mst_block() would be called for $MFTMirr if a page
	 * in its page cache were to be marked dirty.  However this should
	 * never happen with the current driver and considering we do not
	 * handle this case here we do want to BUG(), at least for now.
	 */
	BUG_ON(!(is_mft || S_ISDIR(vi->i_mode) ||
			(NInoAttr(ni) && ni->type == AT_INDEX_ALLOCATION)));
	bh_size_bits = vi->i_blkbits;
	bh_size = 1 << bh_size_bits;
	max_bhs = PAGE_CACHE_SIZE / bh_size;
	BUG_ON(!max_bhs);
	BUG_ON(max_bhs > MAX_BUF_PER_PAGE);

	/* Were we called for sync purposes? */
	sync = (wbc->sync_mode == WB_SYNC_ALL);

	/* Make sure we have mapped buffers. */
	bh = head = page_buffers(page);
	BUG_ON(!bh);

	rec_size_bits = ni->itype.index.block_size_bits;
	BUG_ON(!(PAGE_CACHE_SIZE >> rec_size_bits));
	bhs_per_rec = rec_size >> bh_size_bits;
	BUG_ON(!bhs_per_rec);

	/* The first block in the page. */
	rec_block = block = (sector_t)page->index <<
			(PAGE_CACHE_SHIFT - bh_size_bits);

	/* The first out of bounds block for the data size. */
	dblock = (i_size_read(vi) + bh_size - 1) >> bh_size_bits;

	rl = NULL;
	err = err2 = nr_bhs = nr_recs = nr_locked_nis = 0;
	page_is_dirty = rec_is_dirty = FALSE;
	rec_start_bh = NULL;
	do {
		BOOL is_retry = FALSE;

		if (likely(block < rec_block)) {
			if (unlikely(block >= dblock)) {
				clear_buffer_dirty(bh);
				set_buffer_uptodate(bh);
				continue;
			}
			/*
			 * This block is not the first one in the record.  We
			 * ignore the buffer's dirty state because we could
			 * have raced with a parallel mark_ntfs_record_dirty().
			 */
			if (!rec_is_dirty)
				continue;
			if (unlikely(err2)) {
				if (err2 != -ENOMEM)
					clear_buffer_dirty(bh);
				continue;
			}
		} else /* if (block == rec_block) */ {
			BUG_ON(block > rec_block);
			/* This block is the first one in the record. */
			rec_block += bhs_per_rec;
			err2 = 0;
			if (unlikely(block >= dblock)) {
				clear_buffer_dirty(bh);
				continue;
			}
			if (!buffer_dirty(bh)) {
				/* Clean records are not written out. */
				rec_is_dirty = FALSE;
				continue;
			}
			rec_is_dirty = TRUE;
			rec_start_bh = bh;
		}
		/* Need to map the buffer if it is not mapped already. */
		if (unlikely(!buffer_mapped(bh))) {
			VCN vcn;
			LCN lcn;
			unsigned int vcn_ofs;

			bh->b_bdev = vol->sb->s_bdev;
			/* Obtain the vcn and offset of the current block. */
			vcn = (VCN)block << bh_size_bits;
			vcn_ofs = vcn & vol->cluster_size_mask;
			vcn >>= vol->cluster_size_bits;
			if (!rl) {
lock_retry_remap:
				down_read(&ni->runlist.lock);
				rl = ni->runlist.rl;
			}
			if (likely(rl != NULL)) {
				/* Seek to element containing target vcn. */
				while (rl->length && rl[1].vcn <= vcn)
					rl++;
				lcn = ntfs_rl_vcn_to_lcn(rl, vcn);
			} else
				lcn = LCN_RL_NOT_MAPPED;
			/* Successful remap. */
			if (likely(lcn >= 0)) {
				/* Setup buffer head to correct block. */
				bh->b_blocknr = ((lcn <<
						vol->cluster_size_bits) +
						vcn_ofs) >> bh_size_bits;
				set_buffer_mapped(bh);
			} else {
				/*
				 * Remap failed.  Retry to map the runlist once
				 * unless we are working on $MFT which always
				 * has the whole of its runlist in memory.
				 */
				if (!is_mft && !is_retry &&
						lcn == LCN_RL_NOT_MAPPED) {
					is_retry = TRUE;
					/*
					 * Attempt to map runlist, dropping
					 * lock for the duration.
					 */
					up_read(&ni->runlist.lock);
					err2 = ntfs_map_runlist(ni, vcn);
					if (likely(!err2))
						goto lock_retry_remap;
					if (err2 == -ENOMEM)
						page_is_dirty = TRUE;
					lcn = err2;
				} else {
					err2 = -EIO;
					if (!rl)
						up_read(&ni->runlist.lock);
				}
				/* Hard error.  Abort writing this record. */
				if (!err || err == -ENOMEM)
					err = err2;
				bh->b_blocknr = -1;
				ntfs_error(vol->sb, "Cannot write ntfs record "
						"0x%llx (inode 0x%lx, "
						"attribute type 0x%x) because "
						"its location on disk could "
						"not be determined (error "
						"code %lli).",
						(long long)block <<
						bh_size_bits >>
						vol->mft_record_size_bits,
						ni->mft_no, ni->type,
						(long long)lcn);
				/*
				 * If this is not the first buffer, remove the
				 * buffers in this record from the list of
				 * buffers to write and clear their dirty bit
				 * if not error -ENOMEM.
				 */
				if (rec_start_bh != bh) {
					while (bhs[--nr_bhs] != rec_start_bh)
						;
					if (err2 != -ENOMEM) {
						do {
							clear_buffer_dirty(
								rec_start_bh);
						} while ((rec_start_bh =
								rec_start_bh->
								b_this_page) !=
								bh);
					}
				}
				continue;
			}
		}
		BUG_ON(!buffer_uptodate(bh));
		BUG_ON(nr_bhs >= max_bhs);
		bhs[nr_bhs++] = bh;
	} while (block++, (bh = bh->b_this_page) != head);
	if (unlikely(rl))
		up_read(&ni->runlist.lock);
	/* If there were no dirty buffers, we are done. */
	if (!nr_bhs)
		goto done;
	/* Map the page so we can access its contents. */
	kaddr = kmap(page);
	/* Clear the page uptodate flag whilst the mst fixups are applied. */
	BUG_ON(!PageUptodate(page));
	ClearPageUptodate(page);
	for (i = 0; i < nr_bhs; i++) {
		unsigned int ofs;

		/* Skip buffers which are not at the beginning of records. */
		if (i % bhs_per_rec)
			continue;
		tbh = bhs[i];
		ofs = bh_offset(tbh);
		if (is_mft) {
			ntfs_inode *tni;
			unsigned long mft_no;

			/* Get the mft record number. */
			mft_no = (((s64)page->index << PAGE_CACHE_SHIFT) + ofs)
					>> rec_size_bits;
			/* Check whether to write this mft record. */
			tni = NULL;
			if (!ntfs_may_write_mft_record(vol, mft_no,
					(MFT_RECORD*)(kaddr + ofs), &tni)) {
				/*
				 * The record should not be written.  This
				 * means we need to redirty the page before
				 * returning.
				 */
				page_is_dirty = TRUE;
				/*
				 * Remove the buffers in this mft record from
				 * the list of buffers to write.
				 */
				do {
					bhs[i] = NULL;
				} while (++i % bhs_per_rec);
				continue;
			}
			/*
			 * The record should be written.  If a locked ntfs
			 * inode was returned, add it to the array of locked
			 * ntfs inodes.
			 */
			if (tni)
				locked_nis[nr_locked_nis++] = tni;
		}
		/* Apply the mst protection fixups. */
		err2 = pre_write_mst_fixup((NTFS_RECORD*)(kaddr + ofs),
				rec_size);
		if (unlikely(err2)) {
			if (!err || err == -ENOMEM)
				err = -EIO;
			ntfs_error(vol->sb, "Failed to apply mst fixups "
					"(inode 0x%lx, attribute type 0x%x, "
					"page index 0x%lx, page offset 0x%x)!"
					"  Unmount and run chkdsk.", vi->i_ino,
					ni->type, page->index, ofs);
			/*
			 * Mark all the buffers in this record clean as we do
			 * not want to write corrupt data to disk.
			 */
			do {
				clear_buffer_dirty(bhs[i]);
				bhs[i] = NULL;
			} while (++i % bhs_per_rec);
			continue;
		}
		nr_recs++;
	}
	/* If no records are to be written out, we are done. */
	if (!nr_recs)
		goto unm_done;
	flush_dcache_page(page);
	/* Lock buffers and start synchronous write i/o on them. */
	for (i = 0; i < nr_bhs; i++) {
		tbh = bhs[i];
		if (!tbh)
			continue;
		if (unlikely(test_set_buffer_locked(tbh)))
			BUG();
		/* The buffer dirty state is now irrelevant, just clean it. */
		clear_buffer_dirty(tbh);
		BUG_ON(!buffer_uptodate(tbh));
		BUG_ON(!buffer_mapped(tbh));
		get_bh(tbh);
		tbh->b_end_io = end_buffer_write_sync;
		submit_bh(WRITE, tbh);
	}
	/* Synchronize the mft mirror now if not @sync. */
	if (is_mft && !sync)
		goto do_mirror;
do_wait:
	/* Wait on i/o completion of buffers. */
	for (i = 0; i < nr_bhs; i++) {
		tbh = bhs[i];
		if (!tbh)
			continue;
		wait_on_buffer(tbh);
		if (unlikely(!buffer_uptodate(tbh))) {
			ntfs_error(vol->sb, "I/O error while writing ntfs "
					"record buffer (inode 0x%lx, "
					"attribute type 0x%x, page index "
					"0x%lx, page offset 0x%lx)!  Unmount "
					"and run chkdsk.", vi->i_ino, ni->type,
					page->index, bh_offset(tbh));
			if (!err || err == -ENOMEM)
				err = -EIO;
			/*
			 * Set the buffer uptodate so the page and buffer
			 * states do not become out of sync.
			 */
			set_buffer_uptodate(tbh);
		}
	}
	/* If @sync, now synchronize the mft mirror. */
	if (is_mft && sync) {
do_mirror:
		for (i = 0; i < nr_bhs; i++) {
			unsigned long mft_no;
			unsigned int ofs;

			/*
			 * Skip buffers which are not at the beginning of
			 * records.
			 */
			if (i % bhs_per_rec)
				continue;
			tbh = bhs[i];
			/* Skip removed buffers (and hence records). */
			if (!tbh)
				continue;
			ofs = bh_offset(tbh);
			/* Get the mft record number. */
			mft_no = (((s64)page->index << PAGE_CACHE_SHIFT) + ofs)
					>> rec_size_bits;
			if (mft_no < vol->mftmirr_size)
				ntfs_sync_mft_mirror(vol, mft_no,
						(MFT_RECORD*)(kaddr + ofs),
						sync);
		}
		if (!sync)
			goto do_wait;
	}
	/* Remove the mst protection fixups again. */
	for (i = 0; i < nr_bhs; i++) {
		if (!(i % bhs_per_rec)) {
			tbh = bhs[i];
			if (!tbh)
				continue;
			post_write_mst_fixup((NTFS_RECORD*)(kaddr +
					bh_offset(tbh)));
		}
	}
	flush_dcache_page(page);
unm_done:
	/* Unlock any locked inodes. */
	while (nr_locked_nis-- > 0) {
		ntfs_inode *tni, *base_tni;
		
		tni = locked_nis[nr_locked_nis];
		/* Get the base inode. */
		down(&tni->extent_lock);
		if (tni->nr_extents >= 0)
			base_tni = tni;
		else {
			base_tni = tni->ext.base_ntfs_ino;
			BUG_ON(!base_tni);
		}
		up(&tni->extent_lock);
		ntfs_debug("Unlocking %s inode 0x%lx.",
				tni == base_tni ? "base" : "extent",
				tni->mft_no);
		up(&tni->mrec_lock);
		atomic_dec(&tni->count);
		iput(VFS_I(base_tni));
	}
	SetPageUptodate(page);
	kunmap(page);
done:
	if (unlikely(err && err != -ENOMEM)) {
		/*
		 * Set page error if there is only one ntfs record in the page.
		 * Otherwise we would loose per-record granularity.
		 */
		if (ni->itype.index.block_size == PAGE_CACHE_SIZE)
			SetPageError(page);
		NVolSetErrors(vol);
	}
	if (page_is_dirty) {
		ntfs_debug("Page still contains one or more dirty ntfs "
				"records.  Redirtying the page starting at "
				"record 0x%lx.", page->index <<
				(PAGE_CACHE_SHIFT - rec_size_bits));
		redirty_page_for_writepage(wbc, page);
		unlock_page(page);
	} else {
		/*
		 * Keep the VM happy.  This must be done otherwise the
		 * radix-tree tag PAGECACHE_TAG_DIRTY remains set even though
		 * the page is clean.
		 */
		BUG_ON(PageWriteback(page));
		set_page_writeback(page);
		unlock_page(page);
		end_page_writeback(page);
	}
	if (likely(!err))
		ntfs_debug("Done.");
	return err;
}

/**
 * ntfs_writepage - write a @page to the backing store
 * @page:	page cache page to write out
 * @wbc:	writeback control structure
 *
 * This is called from the VM when it wants to have a dirty ntfs page cache
 * page cleaned.  The VM has already locked the page and marked it clean.
 *
 * For non-resident attributes, ntfs_writepage() writes the @page by calling
 * the ntfs version of the generic block_write_full_page() function,
 * ntfs_write_block(), which in turn if necessary creates and writes the
 * buffers associated with the page asynchronously.
 *
 * For resident attributes, OTOH, ntfs_writepage() writes the @page by copying
 * the data to the mft record (which at this stage is most likely in memory).
 * The mft record is then marked dirty and written out asynchronously via the
 * vfs inode dirty code path for the inode the mft record belongs to or via the
 * vm page dirty code path for the page the mft record is in.
 *
 * Based on ntfs_readpage() and fs/buffer.c::block_write_full_page().
 *
 * Return 0 on success and -errno on error.
 */
static int ntfs_writepage(struct page *page, struct writeback_control *wbc)
{
	loff_t i_size;
	struct inode *vi = page->mapping->host;
	ntfs_inode *base_ni = NULL, *ni = NTFS_I(vi);
	char *kaddr;
	ntfs_attr_search_ctx *ctx = NULL;
	MFT_RECORD *m = NULL;
	u32 attr_len;
	int err;

retry_writepage:
	BUG_ON(!PageLocked(page));
	i_size = i_size_read(vi);
	/* Is the page fully outside i_size? (truncate in progress) */
	if (unlikely(page->index >= (i_size + PAGE_CACHE_SIZE - 1) >>
			PAGE_CACHE_SHIFT)) {
		/*
		 * The page may have dirty, unmapped buffers.  Make them
		 * freeable here, so the page does not leak.
		 */
		block_invalidatepage(page, 0);
		unlock_page(page);
		ntfs_debug("Write outside i_size - truncated?");
		return 0;
	}
	/*
	 * Only $DATA attributes can be encrypted and only unnamed $DATA
	 * attributes can be compressed.  Index root can have the flags set but
	 * this means to create compressed/encrypted files, not that the
	 * attribute is compressed/encrypted.  Note we need to check for
	 * AT_INDEX_ALLOCATION since this is the type of both directory and
	 * index inodes.
	 */
	if (ni->type != AT_INDEX_ALLOCATION) {
		/* If file is encrypted, deny access, just like NT4. */
		if (NInoEncrypted(ni)) {
			unlock_page(page);
			BUG_ON(ni->type != AT_DATA);
			ntfs_debug("Denying write access to encrypted "
					"file.");
			return -EACCES;
		}
		/* Compressed data streams are handled in compress.c. */
		if (NInoNonResident(ni) && NInoCompressed(ni)) {
			BUG_ON(ni->type != AT_DATA);
			BUG_ON(ni->name_len);
			// TODO: Implement and replace this with
			// return ntfs_write_compressed_block(page);
			unlock_page(page);
			ntfs_error(vi->i_sb, "Writing to compressed files is "
					"not supported yet.  Sorry.");
			return -EOPNOTSUPP;
		}
		// TODO: Implement and remove this check.
		if (NInoNonResident(ni) && NInoSparse(ni)) {
			unlock_page(page);
			ntfs_error(vi->i_sb, "Writing to sparse files is not "
					"supported yet.  Sorry.");
			return -EOPNOTSUPP;
		}
	}
	/* NInoNonResident() == NInoIndexAllocPresent() */
	if (NInoNonResident(ni)) {
		/* We have to zero every time due to mmap-at-end-of-file. */
		if (page->index >= (i_size >> PAGE_CACHE_SHIFT)) {
			/* The page straddles i_size. */
			unsigned int ofs = i_size & ~PAGE_CACHE_MASK;
			kaddr = kmap_atomic(page, KM_USER0);
			memset(kaddr + ofs, 0, PAGE_CACHE_SIZE - ofs);
			kunmap_atomic(kaddr, KM_USER0);
			flush_dcache_page(page);
		}
		/* Handle mst protected attributes. */
		if (NInoMstProtected(ni))
			return ntfs_write_mst_block(page, wbc);
		/* Normal, non-resident data stream. */
		return ntfs_write_block(page, wbc);
	}
	/*
	 * Attribute is resident, implying it is not compressed, encrypted, or
	 * mst protected.  This also means the attribute is smaller than an mft
	 * record and hence smaller than a page, so can simply return error on
	 * any pages with index above 0.  Note the attribute can actually be
	 * marked compressed but if it is resident the actual data is not
	 * compressed so we are ok to ignore the compressed flag here.
	 */
	BUG_ON(page_has_buffers(page));
	BUG_ON(!PageUptodate(page));
	if (unlikely(page->index > 0)) {
		ntfs_error(vi->i_sb, "BUG()! page->index (0x%lx) > 0.  "
				"Aborting write.", page->index);
		BUG_ON(PageWriteback(page));
		set_page_writeback(page);
		unlock_page(page);
		end_page_writeback(page);
		return -EIO;
	}
	if (!NInoAttr(ni))
		base_ni = ni;
	else
		base_ni = ni->ext.base_ntfs_ino;
	/* Map, pin, and lock the mft record. */
	m = map_mft_record(base_ni);
	if (IS_ERR(m)) {
		err = PTR_ERR(m);
		m = NULL;
		ctx = NULL;
		goto err_out;
	}
	/*
	 * If a parallel write made the attribute non-resident, drop the mft
	 * record and retry the writepage.
	 */
	if (unlikely(NInoNonResident(ni))) {
		unmap_mft_record(base_ni);
		goto retry_writepage;
	}
	ctx = ntfs_attr_get_search_ctx(base_ni, m);
	if (unlikely(!ctx)) {
		err = -ENOMEM;
		goto err_out;
	}
	err = ntfs_attr_lookup(ni->type, ni->name, ni->name_len,
			CASE_SENSITIVE, 0, NULL, 0, ctx);
	if (unlikely(err))
		goto err_out;
	/*
	 * Keep the VM happy.  This must be done otherwise the radix-tree tag
	 * PAGECACHE_TAG_DIRTY remains set even though the page is clean.
	 */
	BUG_ON(PageWriteback(page));
	set_page_writeback(page);
	unlock_page(page);
	attr_len = le32_to_cpu(ctx->attr->data.resident.value_length);
	i_size = i_size_read(vi);
	if (unlikely(attr_len > i_size)) {
		/* Race with shrinking truncate or a failed truncate. */
		attr_len = i_size;
		/*
		 * If the truncate failed, fix it up now.  If a concurrent
		 * truncate, we do its job, so it does not have to do anything.
		 */
		err = ntfs_resident_attr_value_resize(ctx->mrec, ctx->attr,
				attr_len);
		/* Shrinking cannot fail. */
		BUG_ON(err);
	}
	kaddr = kmap_atomic(page, KM_USER0);
	/* Copy the data from the page to the mft record. */
	memcpy((u8*)ctx->attr +
			le16_to_cpu(ctx->attr->data.resident.value_offset),
			kaddr, attr_len);
	/* Zero out of bounds area in the page cache page. */
	memset(kaddr + attr_len, 0, PAGE_CACHE_SIZE - attr_len);
	kunmap_atomic(kaddr, KM_USER0);
	flush_dcache_mft_record_page(ctx->ntfs_ino);
	flush_dcache_page(page);
	/* We are done with the page. */
	end_page_writeback(page);
	/* Finally, mark the mft record dirty, so it gets written back. */
	mark_mft_record_dirty(ctx->ntfs_ino);
	ntfs_attr_put_search_ctx(ctx);
	unmap_mft_record(base_ni);
	return 0;
err_out:
	if (err == -ENOMEM) {
		ntfs_warning(vi->i_sb, "Error allocating memory. Redirtying "
				"page so we try again later.");
		/*
		 * Put the page back on mapping->dirty_pages, but leave its
		 * buffers' dirty state as-is.
		 */
		redirty_page_for_writepage(wbc, page);
		err = 0;
	} else {
		ntfs_error(vi->i_sb, "Resident attribute write failed with "
				"error %i.", err);
		SetPageError(page);
		NVolSetErrors(ni->vol);
		make_bad_inode(vi);
	}
	unlock_page(page);
	if (ctx)
		ntfs_attr_put_search_ctx(ctx);
	if (m)
		unmap_mft_record(base_ni);
	return err;
}

/**
 * ntfs_prepare_nonresident_write -
 *
 */
static int ntfs_prepare_nonresident_write(struct page *page,
		unsigned from, unsigned to)
{
	VCN vcn;
	LCN lcn;
	s64 initialized_size;
	loff_t i_size;
	sector_t block, ablock, iblock;
	struct inode *vi;
	ntfs_inode *ni;
	ntfs_volume *vol;
	runlist_element *rl;
	struct buffer_head *bh, *head, *wait[2], **wait_bh = wait;
	unsigned long flags;
	unsigned int vcn_ofs, block_start, block_end, blocksize;
	int err;
	BOOL is_retry;
	unsigned char blocksize_bits;

	vi = page->mapping->host;
	ni = NTFS_I(vi);
	vol = ni->vol;

	ntfs_debug("Entering for inode 0x%lx, attribute type 0x%x, page index "
			"0x%lx, from = %u, to = %u.", ni->mft_no, ni->type,
			page->index, from, to);

	BUG_ON(!NInoNonResident(ni));

	blocksize_bits = vi->i_blkbits;
	blocksize = 1 << blocksize_bits;

	/*
	 * create_empty_buffers() will create uptodate/dirty buffers if the
	 * page is uptodate/dirty.
	 */
	if (!page_has_buffers(page))
		create_empty_buffers(page, blocksize, 0);
	bh = head = page_buffers(page);
	if (unlikely(!bh))
		return -ENOMEM;

	/* The first block in the page. */
	block = (s64)page->index << (PAGE_CACHE_SHIFT - blocksize_bits);

	read_lock_irqsave(&ni->size_lock, flags);
	/*
	 * The first out of bounds block for the allocated size.  No need to
	 * round up as allocated_size is in multiples of cluster size and the
	 * minimum cluster size is 512 bytes, which is equal to the smallest
	 * blocksize.
	 */
	ablock = ni->allocated_size >> blocksize_bits;
	i_size = i_size_read(vi);
	initialized_size = ni->initialized_size;
	read_unlock_irqrestore(&ni->size_lock, flags);

	/* The last (fully or partially) initialized block. */
	iblock = initialized_size >> blocksize_bits;

	/* Loop through all the buffers in the page. */
	block_start = 0;
	rl = NULL;
	err = 0;
	do {
		block_end = block_start + blocksize;
		/*
		 * If buffer @bh is outside the write, just mark it uptodate
		 * if the page is uptodate and continue with the next buffer.
		 */
		if (block_end <= from || block_start >= to) {
			if (PageUptodate(page)) {
				if (!buffer_uptodate(bh))
					set_buffer_uptodate(bh);
			}
			continue;
		}
		/*
		 * @bh is at least partially being written to.
		 * Make sure it is not marked as new.
		 */
		//if (buffer_new(bh))
		//	clear_buffer_new(bh);

		if (block >= ablock) {
			// TODO: block is above allocated_size, need to
			// allocate it. Best done in one go to accommodate not
			// only block but all above blocks up to and including:
			// ((page->index << PAGE_CACHE_SHIFT) + to + blocksize
			// - 1) >> blobksize_bits. Obviously will need to round
			// up to next cluster boundary, too. This should be
			// done with a helper function, so it can be reused.
			ntfs_error(vol->sb, "Writing beyond allocated size "
					"is not supported yet. Sorry.");
			err = -EOPNOTSUPP;
			goto err_out;
			// Need to update ablock.
			// Need to set_buffer_new() on all block bhs that are
			// newly allocated.
		}
		/*
		 * Now we have enough allocated size to fulfill the whole
		 * request, i.e. block < ablock is true.
		 */
		if (unlikely((block >= iblock) &&
				(initialized_size < i_size))) {
			/*
			 * If this page is fully outside initialized size, zero
			 * out all pages between the current initialized size
			 * and the current page. Just use ntfs_readpage() to do
			 * the zeroing transparently.
			 */
			if (block > iblock) {
				// TODO:
				// For each page do:
				// - read_cache_page()
				// Again for each page do:
				// - wait_on_page_locked()
				// - Check (PageUptodate(page) &&
				//			!PageError(page))
				// Update initialized size in the attribute and
				// in the inode.
				// Again, for each page do:
				//	__set_page_dirty_buffers();
				// page_cache_release()
				// We don't need to wait on the writes.
				// Update iblock.
			}
			/*
			 * The current page straddles initialized size. Zero
			 * all non-uptodate buffers and set them uptodate (and
			 * dirty?). Note, there aren't any non-uptodate buffers
			 * if the page is uptodate.
			 * FIXME: For an uptodate page, the buffers may need to
			 * be written out because they were not initialized on
			 * disk before.
			 */
			if (!PageUptodate(page)) {
				// TODO:
				// Zero any non-uptodate buffers up to i_size.
				// Set them uptodate and dirty.
			}
			// TODO:
			// Update initialized size in the attribute and in the
			// inode (up to i_size).
			// Update iblock.
			// FIXME: This is inefficient. Try to batch the two
			// size changes to happen in one go.
			ntfs_error(vol->sb, "Writing beyond initialized size "
					"is not supported yet. Sorry.");
			err = -EOPNOTSUPP;
			goto err_out;
			// Do NOT set_buffer_new() BUT DO clear buffer range
			// outside write request range.
			// set_buffer_uptodate() on complete buffers as well as
			// set_buffer_dirty().
		}

		/* Need to map unmapped buffers. */
		if (!buffer_mapped(bh)) {
			/* Unmapped buffer. Need to map it. */
			bh->b_bdev = vol->sb->s_bdev;

			/* Convert block into corresponding vcn and offset. */
			vcn = (VCN)block << blocksize_bits >>
					vol->cluster_size_bits;
			vcn_ofs = ((VCN)block << blocksize_bits) &
					vol->cluster_size_mask;

			is_retry = FALSE;
			if (!rl) {
lock_retry_remap:
				down_read(&ni->runlist.lock);
				rl = ni->runlist.rl;
			}
			if (likely(rl != NULL)) {
				/* Seek to element containing target vcn. */
				while (rl->length && rl[1].vcn <= vcn)
					rl++;
				lcn = ntfs_rl_vcn_to_lcn(rl, vcn);
			} else
				lcn = LCN_RL_NOT_MAPPED;
			if (unlikely(lcn < 0)) {
				/*
				 * We extended the attribute allocation above.
				 * If we hit an ENOENT here it means that the
				 * allocation was insufficient which is a bug.
				 */
				BUG_ON(lcn == LCN_ENOENT);

				/* It is a hole, need to instantiate it. */
				if (lcn == LCN_HOLE) {
					// TODO: Instantiate the hole.
					// clear_buffer_new(bh);
					// unmap_underlying_metadata(bh->b_bdev,
					//		bh->b_blocknr);
					// For non-uptodate buffers, need to
					// zero out the region outside the
					// request in this bh or all bhs,
					// depending on what we implemented
					// above.
					// Need to flush_dcache_page().
					// Or could use set_buffer_new()
					// instead?
					ntfs_error(vol->sb, "Writing into "
							"sparse regions is "
							"not supported yet. "
							"Sorry.");
					err = -EOPNOTSUPP;
					if (!rl)
						up_read(&ni->runlist.lock);
					goto err_out;
				} else if (!is_retry &&
						lcn == LCN_RL_NOT_MAPPED) {
					is_retry = TRUE;
					/*
					 * Attempt to map runlist, dropping
					 * lock for the duration.
					 */
					up_read(&ni->runlist.lock);
					err = ntfs_map_runlist(ni, vcn);
					if (likely(!err))
						goto lock_retry_remap;
					rl = NULL;
				} else if (!rl)
					up_read(&ni->runlist.lock);
				/*
				 * Failed to map the buffer, even after
				 * retrying.
				 */
				if (!err)
					err = -EIO;
				bh->b_blocknr = -1;
				ntfs_error(vol->sb, "Failed to write to inode "
						"0x%lx, attribute type 0x%x, "
						"vcn 0x%llx, offset 0x%x "
						"because its location on disk "
						"could not be determined%s "
						"(error code %i).",
						ni->mft_no, ni->type,
						(unsigned long long)vcn,
						vcn_ofs, is_retry ? " even "
						"after retrying" : "", err);
				goto err_out;
			}
			/* We now have a successful remap, i.e. lcn >= 0. */

			/* Setup buffer head to correct block. */
			bh->b_blocknr = ((lcn << vol->cluster_size_bits)
					+ vcn_ofs) >> blocksize_bits;
			set_buffer_mapped(bh);

			// FIXME: Something analogous to this is needed for
			// each newly allocated block, i.e. BH_New.
			// FIXME: Might need to take this out of the
			// if (!buffer_mapped(bh)) {}, depending on how we
			// implement things during the allocated_size and
			// initialized_size extension code above.
			if (buffer_new(bh)) {
				clear_buffer_new(bh);
				unmap_underlying_metadata(bh->b_bdev,
						bh->b_blocknr);
				if (PageUptodate(page)) {
					set_buffer_uptodate(bh);
					continue;
				}
				/*
				 * Page is _not_ uptodate, zero surrounding
				 * region. NOTE: This is how we decide if to
				 * zero or not!
				 */
				if (block_end > to || block_start < from) {
					void *kaddr;

					kaddr = kmap_atomic(page, KM_USER0);
					if (block_end > to)
						memset(kaddr + to, 0,
								block_end - to);
					if (block_start < from)
						memset(kaddr + block_start, 0,
								from -
								block_start);
					flush_dcache_page(page);
					kunmap_atomic(kaddr, KM_USER0);
				}
				continue;
			}
		}
		/* @bh is mapped, set it uptodate if the page is uptodate. */
		if (PageUptodate(page)) {
			if (!buffer_uptodate(bh))
				set_buffer_uptodate(bh);
			continue;
		}
		/*
		 * The page is not uptodate. The buffer is mapped. If it is not
		 * uptodate, and it is only partially being written to, we need
		 * to read the buffer in before the write, i.e. right now.
		 */
		if (!buffer_uptodate(bh) &&
				(block_start < from || block_end > to)) {
			ll_rw_block(READ, 1, &bh);
			*wait_bh++ = bh;
		}
	} while (block++, block_start = block_end,
			(bh = bh->b_this_page) != head);

	/* Release the lock if we took it. */
	if (rl) {
		up_read(&ni->runlist.lock);
		rl = NULL;
	}

	/* If we issued read requests, let them complete. */
	while (wait_bh > wait) {
		wait_on_buffer(*--wait_bh);
		if (!buffer_uptodate(*wait_bh))
			return -EIO;
	}

	ntfs_debug("Done.");
	return 0;
err_out:
	/*
	 * Zero out any newly allocated blocks to avoid exposing stale data.
	 * If BH_New is set, we know that the block was newly allocated in the
	 * above loop.
	 * FIXME: What about initialized_size increments? Have we done all the
	 * required zeroing above? If not this error handling is broken, and
	 * in particular the if (block_end <= from) check is completely bogus.
	 */
	bh = head;
	block_start = 0;
	is_retry = FALSE;
	do {
		block_end = block_start + blocksize;
		if (block_end <= from)
			continue;
		if (block_start >= to)
			break;
		if (buffer_new(bh)) {
			void *kaddr;

			clear_buffer_new(bh);
			kaddr = kmap_atomic(page, KM_USER0);
			memset(kaddr + block_start, 0, bh->b_size);
			kunmap_atomic(kaddr, KM_USER0);
			set_buffer_uptodate(bh);
			mark_buffer_dirty(bh);
			is_retry = TRUE;
		}
	} while (block_start = block_end, (bh = bh->b_this_page) != head);
	if (is_retry)
		flush_dcache_page(page);
	if (rl)
		up_read(&ni->runlist.lock);
	return err;
}

/**
 * ntfs_prepare_write - prepare a page for receiving data
 *
 * This is called from generic_file_write() with i_sem held on the inode
 * (@page->mapping->host).  The @page is locked but not kmap()ped.  The source
 * data has not yet been copied into the @page.
 *
 * Need to extend the attribute/fill in holes if necessary, create blocks and
 * make partially overwritten blocks uptodate,
 *
 * i_size is not to be modified yet.
 *
 * Return 0 on success or -errno on error.
 *
 * Should be using block_prepare_write() [support for sparse files] or
 * cont_prepare_write() [no support for sparse files].  Cannot do that due to
 * ntfs specifics but can look at them for implementation guidance.
 *
 * Note: In the range, @from is inclusive and @to is exclusive, i.e. @from is
 * the first byte in the page that will be written to and @to is the first byte
 * after the last byte that will be written to.
 */
static int ntfs_prepare_write(struct file *file, struct page *page,
		unsigned from, unsigned to)
{
	s64 new_size;
	loff_t i_size;
	struct inode *vi = page->mapping->host;
	ntfs_inode *base_ni = NULL, *ni = NTFS_I(vi);
	ntfs_volume *vol = ni->vol;
	ntfs_attr_search_ctx *ctx = NULL;
	MFT_RECORD *m = NULL;
	ATTR_RECORD *a;
	u8 *kaddr;
	u32 attr_len;
	int err;

	ntfs_debug("Entering for inode 0x%lx, attribute type 0x%x, page index "
			"0x%lx, from = %u, to = %u.", vi->i_ino, ni->type,
			page->index, from, to);
	BUG_ON(!PageLocked(page));
	BUG_ON(from > PAGE_CACHE_SIZE);
	BUG_ON(to > PAGE_CACHE_SIZE);
	BUG_ON(from > to);
	BUG_ON(NInoMstProtected(ni));
	/*
	 * If a previous ntfs_truncate() failed, repeat it and abort if it
	 * fails again.
	 */
	if (unlikely(NInoTruncateFailed(ni))) {
		down_write(&vi->i_alloc_sem);
		err = ntfs_truncate(vi);
		up_write(&vi->i_alloc_sem);
		if (err || NInoTruncateFailed(ni)) {
			if (!err)
				err = -EIO;
			goto err_out;
		}
	}
	/* If the attribute is not resident, deal with it elsewhere. */
	if (NInoNonResident(ni)) {
		/*
		 * Only unnamed $DATA attributes can be compressed, encrypted,
		 * and/or sparse.
		 */
		if (ni->type == AT_DATA && !ni->name_len) {
			/* If file is encrypted, deny access, just like NT4. */
			if (NInoEncrypted(ni)) {
				ntfs_debug("Denying write access to encrypted "
						"file.");
				return -EACCES;
			}
			/* Compressed data streams are handled in compress.c. */
			if (NInoCompressed(ni)) {
				// TODO: Implement and replace this check with
				// return ntfs_write_compressed_block(page);
				ntfs_error(vi->i_sb, "Writing to compressed "
						"files is not supported yet. "
						"Sorry.");
				return -EOPNOTSUPP;
			}
			// TODO: Implement and remove this check.
			if (NInoSparse(ni)) {
				ntfs_error(vi->i_sb, "Writing to sparse files "
						"is not supported yet. Sorry.");
				return -EOPNOTSUPP;
			}
		}
		/* Normal data stream. */
		return ntfs_prepare_nonresident_write(page, from, to);
	}
	/*
	 * Attribute is resident, implying it is not compressed, encrypted, or
	 * sparse.
	 */
	BUG_ON(page_has_buffers(page));
	new_size = ((s64)page->index << PAGE_CACHE_SHIFT) + to;
	/* If we do not need to resize the attribute allocation we are done. */
	if (new_size <= i_size_read(vi))
		goto done;
	/* Map, pin, and lock the (base) mft record. */
	if (!NInoAttr(ni))
		base_ni = ni;
	else
		base_ni = ni->ext.base_ntfs_ino;
	m = map_mft_record(base_ni);
	if (IS_ERR(m)) {
		err = PTR_ERR(m);
		m = NULL;
		ctx = NULL;
		goto err_out;
	}
	ctx = ntfs_attr_get_search_ctx(base_ni, m);
	if (unlikely(!ctx)) {
		err = -ENOMEM;
		goto err_out;
	}
	err = ntfs_attr_lookup(ni->type, ni->name, ni->name_len,
			CASE_SENSITIVE, 0, NULL, 0, ctx);
	if (unlikely(err)) {
		if (err == -ENOENT)
			err = -EIO;
		goto err_out;
	}
	m = ctx->mrec;
	a = ctx->attr;
	/* The total length of the attribute value. */
	attr_len = le32_to_cpu(a->data.resident.value_length);
	/* Fix an eventual previous failure of ntfs_commit_write(). */
	i_size = i_size_read(vi);
	if (unlikely(attr_len > i_size)) {
		attr_len = i_size;
		a->data.resident.value_length = cpu_to_le32(attr_len);
	}
	/* If we do not need to resize the attribute allocation we are done. */
	if (new_size <= attr_len)
		goto done_unm;
	/* Check if new size is allowed in $AttrDef. */
	err = ntfs_attr_size_bounds_check(vol, ni->type, new_size);
	if (unlikely(err)) {
		if (err == -ERANGE) {
			ntfs_error(vol->sb, "Write would cause the inode "
					"0x%lx to exceed the maximum size for "
					"its attribute type (0x%x).  Aborting "
					"write.", vi->i_ino,
					le32_to_cpu(ni->type));
		} else {
			ntfs_error(vol->sb, "Inode 0x%lx has unknown "
					"attribute type 0x%x.  Aborting "
					"write.", vi->i_ino,
					le32_to_cpu(ni->type));
			err = -EIO;
		}
		goto err_out2;
	}
	/*
	 * Extend the attribute record to be able to store the new attribute
	 * size.
	 */
	if (new_size >= vol->mft_record_size || ntfs_attr_record_resize(m, a,
			le16_to_cpu(a->data.resident.value_offset) +
			new_size)) {
		/* Not enough space in the mft record. */
		ntfs_error(vol->sb, "Not enough space in the mft record for "
				"the resized attribute value.  This is not "
				"supported yet.  Aborting write.");
		err = -EOPNOTSUPP;
		goto err_out2;
	}
	/*
	 * We have enough space in the mft record to fit the write.  This
	 * implies the attribute is smaller than the mft record and hence the
	 * attribute must be in a single page and hence page->index must be 0.
	 */
	BUG_ON(page->index);
	/*
	 * If the beginning of the write is past the old size, enlarge the
	 * attribute value up to the beginning of the write and fill it with
	 * zeroes.
	 */
	if (from > attr_len) {
		memset((u8*)a + le16_to_cpu(a->data.resident.value_offset) +
				attr_len, 0, from - attr_len);
		a->data.resident.value_length = cpu_to_le32(from);
		/* Zero the corresponding area in the page as well. */
		if (PageUptodate(page)) {
			kaddr = kmap_atomic(page, KM_USER0);
			memset(kaddr + attr_len, 0, from - attr_len);
			kunmap_atomic(kaddr, KM_USER0);
			flush_dcache_page(page);
		}
	}
	flush_dcache_mft_record_page(ctx->ntfs_ino);
	mark_mft_record_dirty(ctx->ntfs_ino);
done_unm:
	ntfs_attr_put_search_ctx(ctx);
	unmap_mft_record(base_ni);
	/*
	 * Because resident attributes are handled by memcpy() to/from the
	 * corresponding MFT record, and because this form of i/o is byte
	 * aligned rather than block aligned, there is no need to bring the
	 * page uptodate here as in the non-resident case where we need to
	 * bring the buffers straddled by the write uptodate before
	 * generic_file_write() does the copying from userspace.
	 *
	 * We thus defer the uptodate bringing of the page region outside the
	 * region written to to ntfs_commit_write(), which makes the code
	 * simpler and saves one atomic kmap which is good.
	 */
done:
	ntfs_debug("Done.");
	return 0;
err_out:
	if (err == -ENOMEM)
		ntfs_warning(vi->i_sb, "Error allocating memory required to "
				"prepare the write.");
	else {
		ntfs_error(vi->i_sb, "Resident attribute prepare write failed "
				"with error %i.", err);
		NVolSetErrors(vol);
		make_bad_inode(vi);
	}
err_out2:
	if (ctx)
		ntfs_attr_put_search_ctx(ctx);
	if (m)
		unmap_mft_record(base_ni);
	return err;
}

/**
 * ntfs_commit_nonresident_write -
 *
 */
static int ntfs_commit_nonresident_write(struct page *page,
		unsigned from, unsigned to)
{
	s64 pos = ((s64)page->index << PAGE_CACHE_SHIFT) + to;
	struct inode *vi = page->mapping->host;
	struct buffer_head *bh, *head;
	unsigned int block_start, block_end, blocksize;
	BOOL partial;

	ntfs_debug("Entering for inode 0x%lx, attribute type 0x%x, page index "
			"0x%lx, from = %u, to = %u.", vi->i_ino,
			NTFS_I(vi)->type, page->index, from, to);
	blocksize = 1 << vi->i_blkbits;

	// FIXME: We need a whole slew of special cases in here for compressed
	// files for example...
	// For now, we know ntfs_prepare_write() would have failed so we can't
	// get here in any of the cases which we have to special case, so we
	// are just a ripped off, unrolled generic_commit_write().

	bh = head = page_buffers(page);
	block_start = 0;
	partial = FALSE;
	do {
		block_end = block_start + blocksize;
		if (block_end <= from || block_start >= to) {
			if (!buffer_uptodate(bh))
				partial = TRUE;
		} else {
			set_buffer_uptodate(bh);
			mark_buffer_dirty(bh);
		}
	} while (block_start = block_end, (bh = bh->b_this_page) != head);
	/*
	 * If this is a partial write which happened to make all buffers
	 * uptodate then we can optimize away a bogus ->readpage() for the next
	 * read().  Here we 'discover' whether the page went uptodate as a
	 * result of this (potentially partial) write.
	 */
	if (!partial)
		SetPageUptodate(page);
	/*
	 * Not convinced about this at all.  See disparity comment above.  For
	 * now we know ntfs_prepare_write() would have failed in the write
	 * exceeds i_size case, so this will never trigger which is fine.
	 */
	if (pos > i_size_read(vi)) {
		ntfs_error(vi->i_sb, "Writing beyond the existing file size is "
				"not supported yet.  Sorry.");
		return -EOPNOTSUPP;
		// vi->i_size = pos;
		// mark_inode_dirty(vi);
	}
	ntfs_debug("Done.");
	return 0;
}

/**
 * ntfs_commit_write - commit the received data
 *
 * This is called from generic_file_write() with i_sem held on the inode
 * (@page->mapping->host).  The @page is locked but not kmap()ped.  The source
 * data has already been copied into the @page.  ntfs_prepare_write() has been
 * called before the data copied and it returned success so we can take the
 * results of various BUG checks and some error handling for granted.
 *
 * Need to mark modified blocks dirty so they get written out later when
 * ntfs_writepage() is invoked by the VM.
 *
 * Return 0 on success or -errno on error.
 *
 * Should be using generic_commit_write().  This marks buffers uptodate and
 * dirty, sets the page uptodate if all buffers in the page are uptodate, and
 * updates i_size if the end of io is beyond i_size.  In that case, it also
 * marks the inode dirty.
 *
 * Cannot use generic_commit_write() due to ntfs specialities but can look at
 * it for implementation guidance.
 *
 * If things have gone as outlined in ntfs_prepare_write(), then we do not
 * need to do any page content modifications here at all, except in the write
 * to resident attribute case, where we need to do the uptodate bringing here
 * which we combine with the copying into the mft record which means we save
 * one atomic kmap.
 */
static int ntfs_commit_write(struct file *file, struct page *page,
		unsigned from, unsigned to)
{
	struct inode *vi = page->mapping->host;
	ntfs_inode *base_ni, *ni = NTFS_I(vi);
	char *kaddr, *kattr;
	ntfs_attr_search_ctx *ctx;
	MFT_RECORD *m;
	ATTR_RECORD *a;
	u32 attr_len;
	int err;

	ntfs_debug("Entering for inode 0x%lx, attribute type 0x%x, page index "
			"0x%lx, from = %u, to = %u.", vi->i_ino, ni->type,
			page->index, from, to);
	/* If the attribute is not resident, deal with it elsewhere. */
	if (NInoNonResident(ni)) {
		/* Only unnamed $DATA attributes can be compressed/encrypted. */
		if (ni->type == AT_DATA && !ni->name_len) {
			/* Encrypted files need separate handling. */
			if (NInoEncrypted(ni)) {
				// We never get here at present!
				BUG();
			}
			/* Compressed data streams are handled in compress.c. */
			if (NInoCompressed(ni)) {
				// TODO: Implement this!
				// return ntfs_write_compressed_block(page);
				// We never get here at present!
				BUG();
			}
		}
		/* Normal data stream. */
		return ntfs_commit_nonresident_write(page, from, to);
	}
	/*
	 * Attribute is resident, implying it is not compressed, encrypted, or
	 * sparse.
	 */
	if (!NInoAttr(ni))
		base_ni = ni;
	else
		base_ni = ni->ext.base_ntfs_ino;
	/* Map, pin, and lock the mft record. */
	m = map_mft_record(base_ni);
	if (IS_ERR(m)) {
		err = PTR_ERR(m);
		m = NULL;
		ctx = NULL;
		goto err_out;
	}
	ctx = ntfs_attr_get_search_ctx(base_ni, m);
	if (unlikely(!ctx)) {
		err = -ENOMEM;
		goto err_out;
	}
	err = ntfs_attr_lookup(ni->type, ni->name, ni->name_len,
			CASE_SENSITIVE, 0, NULL, 0, ctx);
	if (unlikely(err)) {
		if (err == -ENOENT)
			err = -EIO;
		goto err_out;
	}
	a = ctx->attr;
	/* The total length of the attribute value. */
	attr_len = le32_to_cpu(a->data.resident.value_length);
	BUG_ON(from > attr_len);
	kattr = (u8*)a + le16_to_cpu(a->data.resident.value_offset);
	kaddr = kmap_atomic(page, KM_USER0);
	/* Copy the received data from the page to the mft record. */
	memcpy(kattr + from, kaddr + from, to - from);
	/* Update the attribute length if necessary. */
	if (to > attr_len) {
		attr_len = to;
		a->data.resident.value_length = cpu_to_le32(attr_len);
	}
	/*
	 * If the page is not uptodate, bring the out of bounds area(s)
	 * uptodate by copying data from the mft record to the page.
	 */
	if (!PageUptodate(page)) {
		if (from > 0)
			memcpy(kaddr, kattr, from);
		if (to < attr_len)
			memcpy(kaddr + to, kattr + to, attr_len - to);
		/* Zero the region outside the end of the attribute value. */
		if (attr_len < PAGE_CACHE_SIZE)
			memset(kaddr + attr_len, 0, PAGE_CACHE_SIZE - attr_len);
		/*
		 * The probability of not having done any of the above is
		 * extremely small, so we just flush unconditionally.
		 */
		flush_dcache_page(page);
		SetPageUptodate(page);
	}
	kunmap_atomic(kaddr, KM_USER0);
	/* Update i_size if necessary. */
	if (i_size_read(vi) < attr_len) {
		unsigned long flags;

		write_lock_irqsave(&ni->size_lock, flags);
		ni->allocated_size = ni->initialized_size = attr_len;
		i_size_write(vi, attr_len);
		write_unlock_irqrestore(&ni->size_lock, flags);
	}
	/* Mark the mft record dirty, so it gets written back. */
	flush_dcache_mft_record_page(ctx->ntfs_ino);
	mark_mft_record_dirty(ctx->ntfs_ino);
	ntfs_attr_put_search_ctx(ctx);
	unmap_mft_record(base_ni);
	ntfs_debug("Done.");
	return 0;
err_out:
	if (err == -ENOMEM) {
		ntfs_warning(vi->i_sb, "Error allocating memory required to "
				"commit the write.");
		if (PageUptodate(page)) {
			ntfs_warning(vi->i_sb, "Page is uptodate, setting "
					"dirty so the write will be retried "
					"later on by the VM.");
			/*
			 * Put the page on mapping->dirty_pages, but leave its
			 * buffers' dirty state as-is.
			 */
			__set_page_dirty_nobuffers(page);
			err = 0;
		} else
			ntfs_error(vi->i_sb, "Page is not uptodate.  Written "
					"data has been lost.");
	} else {
		ntfs_error(vi->i_sb, "Resident attribute commit write failed "
				"with error %i.", err);
		NVolSetErrors(ni->vol);
		make_bad_inode(vi);
	}
	if (ctx)
		ntfs_attr_put_search_ctx(ctx);
	if (m)
		unmap_mft_record(base_ni);
	return err;
}

#endif	/* NTFS_RW */

/**
 * ntfs_aops - general address space operations for inodes and attributes
 */
struct address_space_operations ntfs_aops = {
	.readpage	= ntfs_readpage,	/* Fill page with data. */
	.sync_page	= block_sync_page,	/* Currently, just unplugs the
						   disk request queue. */
#ifdef NTFS_RW
	.writepage	= ntfs_writepage,	/* Write dirty page to disk. */
	.prepare_write	= ntfs_prepare_write,	/* Prepare page and buffers
						   ready to receive data. */
	.commit_write	= ntfs_commit_write,	/* Commit received data. */
#endif /* NTFS_RW */
};

/**
 * ntfs_mst_aops - general address space operations for mst protecteed inodes
 *		   and attributes
 */
struct address_space_operations ntfs_mst_aops = {
	.readpage	= ntfs_readpage,	/* Fill page with data. */
	.sync_page	= block_sync_page,	/* Currently, just unplugs the
						   disk request queue. */
#ifdef NTFS_RW
	.writepage	= ntfs_writepage,	/* Write dirty page to disk. */
	.set_page_dirty	= __set_page_dirty_nobuffers,	/* Set the page dirty
						   without touching the buffers
						   belonging to the page. */
#endif /* NTFS_RW */
};

#ifdef NTFS_RW

/**
 * mark_ntfs_record_dirty - mark an ntfs record dirty
 * @page:	page containing the ntfs record to mark dirty
 * @ofs:	byte offset within @page at which the ntfs record begins
 *
 * Set the buffers and the page in which the ntfs record is located dirty.
 *
 * The latter also marks the vfs inode the ntfs record belongs to dirty
 * (I_DIRTY_PAGES only).
 *
 * If the page does not have buffers, we create them and set them uptodate.
 * The page may not be locked which is why we need to handle the buffers under
 * the mapping->private_lock.  Once the buffers are marked dirty we no longer
 * need the lock since try_to_free_buffers() does not free dirty buffers.
 */
void mark_ntfs_record_dirty(struct page *page, const unsigned int ofs) {
	struct address_space *mapping = page->mapping;
	ntfs_inode *ni = NTFS_I(mapping->host);
	struct buffer_head *bh, *head, *buffers_to_free = NULL;
	unsigned int end, bh_size, bh_ofs;

	BUG_ON(!PageUptodate(page));
	end = ofs + ni->itype.index.block_size;
	bh_size = 1 << VFS_I(ni)->i_blkbits;
	spin_lock(&mapping->private_lock);
	if (unlikely(!page_has_buffers(page))) {
		spin_unlock(&mapping->private_lock);
		bh = head = alloc_page_buffers(page, bh_size, 1);
		spin_lock(&mapping->private_lock);
		if (likely(!page_has_buffers(page))) {
			struct buffer_head *tail;

			do {
				set_buffer_uptodate(bh);
				tail = bh;
				bh = bh->b_this_page;
			} while (bh);
			tail->b_this_page = head;
			attach_page_buffers(page, head);
		} else
			buffers_to_free = bh;
	}
	bh = head = page_buffers(page);
	BUG_ON(!bh);
	do {
		bh_ofs = bh_offset(bh);
		if (bh_ofs + bh_size <= ofs)
			continue;
		if (unlikely(bh_ofs >= end))
			break;
		set_buffer_dirty(bh);
	} while ((bh = bh->b_this_page) != head);
	spin_unlock(&mapping->private_lock);
	__set_page_dirty_nobuffers(page);
	if (unlikely(buffers_to_free)) {
		do {
			bh = buffers_to_free->b_this_page;
			free_buffer_head(buffers_to_free);
			buffers_to_free = bh;
		} while (buffers_to_free);
	}
}

#endif /* NTFS_RW */
