/**
 * compress.c - NTFS kernel compressed attributes handling.
 *		Part of the Linux-NTFS project.
 *
 * Copyright (c) 2001-2004 Anton Altaparmakov
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

#include <linux/fs.h>
#include <linux/buffer_head.h>
#include <linux/blkdev.h>
#include <linux/vmalloc.h>
#include <linux/slab.h>

#include "attrib.h"
#include "inode.h"
#include "debug.h"
#include "ntfs.h"

/**
 * ntfs_compression_constants - enum of constants used in the compression code
 */
typedef enum {
	/* Token types and access mask. */
	NTFS_SYMBOL_TOKEN	=	0,
	NTFS_PHRASE_TOKEN	=	1,
	NTFS_TOKEN_MASK		=	1,

	/* Compression sub-block constants. */
	NTFS_SB_SIZE_MASK	=	0x0fff,
	NTFS_SB_SIZE		=	0x1000,
	NTFS_SB_IS_COMPRESSED	=	0x8000,

	/*
	 * The maximum compression block size is by definition 16 * the cluster
	 * size, with the maximum supported cluster size being 4kiB. Thus the
	 * maximum compression buffer size is 64kiB, so we use this when
	 * initializing the compression buffer.
	 */
	NTFS_MAX_CB_SIZE	= 64 * 1024,
} ntfs_compression_constants;

/**
 * ntfs_compression_buffer - one buffer for the decompression engine
 */
static u8 *ntfs_compression_buffer;

/**
 * ntfs_cb_lock - spinlock which protects ntfs_compression_buffer
 */
static DEFINE_SPINLOCK(ntfs_cb_lock);

/**
 * allocate_compression_buffers - allocate the decompression buffers
 *
 * Caller has to hold the ntfs_lock mutex.
 *
 * Return 0 on success or -ENOMEM if the allocations failed.
 */
int allocate_compression_buffers(void)
{
	BUG_ON(ntfs_compression_buffer);

	ntfs_compression_buffer = vmalloc(NTFS_MAX_CB_SIZE);
	if (!ntfs_compression_buffer)
		return -ENOMEM;
	return 0;
}

/**
 * free_compression_buffers - free the decompression buffers
 *
 * Caller has to hold the ntfs_lock mutex.
 */
void free_compression_buffers(void)
{
	BUG_ON(!ntfs_compression_buffer);
	vfree(ntfs_compression_buffer);
	ntfs_compression_buffer = NULL;
}

/**
 * zero_partial_compressed_page - zero out of bounds compressed page region
 */
static void zero_partial_compressed_page(struct page *page,
		const s64 initialized_size)
{
	u8 *kp = page_address(page);
	unsigned int kp_ofs;

	ntfs_debug("Zeroing page region outside initialized size.");
	if (((s64)page->index << PAGE_SHIFT) >= initialized_size) {
		clear_page(kp);
		return;
	}
	kp_ofs = initialized_size & ~PAGE_MASK;
	memset(kp + kp_ofs, 0, PAGE_SIZE - kp_ofs);
	return;
}

/**
 * handle_bounds_compressed_page - test for&handle out of bounds compressed page
 */
static inline void handle_bounds_compressed_page(struct page *page,
		const loff_t i_size, const s64 initialized_size)
{
	if ((page->index >= (initialized_size >> PAGE_SHIFT)) &&
			(initialized_size < i_size))
		zero_partial_compressed_page(page, initialized_size);
	return;
}

/**
 * ntfs_decompress - decompress a compression block into an array of pages
 * @dest_pages:		destination array of pages
 * @dest_index:		current index into @dest_pages (IN/OUT)
 * @dest_ofs:		current offset within @dest_pages[@dest_index] (IN/OUT)
 * @dest_max_index:	maximum index into @dest_pages (IN)
 * @dest_max_ofs:	maximum offset within @dest_pages[@dest_max_index] (IN)
 * @xpage:		the target page (-1 if none) (IN)
 * @xpage_done:		set to 1 if xpage was completed successfully (IN/OUT)
 * @cb_start:		compression block to decompress (IN)
 * @cb_size:		size of compression block @cb_start in bytes (IN)
 * @i_size:		file size when we started the read (IN)
 * @initialized_size:	initialized file size when we started the read (IN)
 *
 * The caller must have disabled preemption. ntfs_decompress() reenables it when
 * the critical section is finished.
 *
 * This decompresses the compression block @cb_start into the array of
 * destination pages @dest_pages starting at index @dest_index into @dest_pages
 * and at offset @dest_pos into the page @dest_pages[@dest_index].
 *
 * When the page @dest_pages[@xpage] is completed, @xpage_done is set to 1.
 * If xpage is -1 or @xpage has not been completed, @xpage_done is not modified.
 *
 * @cb_start is a pointer to the compression block which needs decompressing
 * and @cb_size is the size of @cb_start in bytes (8-64kiB).
 *
 * Return 0 if success or -EOVERFLOW on error in the compressed stream.
 * @xpage_done indicates whether the target page (@dest_pages[@xpage]) was
 * completed during the decompression of the compression block (@cb_start).
 *
 * Warning: This function *REQUIRES* PAGE_SIZE >= 4096 or it will blow up
 * unpredicatbly! You have been warned!
 *
 * Note to hackers: This function may not sleep until it has finished accessing
 * the compression block @cb_start as it is a per-CPU buffer.
 */
static int ntfs_decompress(struct page *dest_pages[], int *dest_index,
		int *dest_ofs, const int dest_max_index, const int dest_max_ofs,
		const int xpage, char *xpage_done, u8 *const cb_start,
		const u32 cb_size, const loff_t i_size,
		const s64 initialized_size)
{
	/*
	 * Pointers into the compressed data, i.e. the compression block (cb),
	 * and the therein contained sub-blocks (sb).
	 */
	u8 *cb_end = cb_start + cb_size; /* End of cb. */
	u8 *cb = cb_start;	/* Current position in cb. */
	u8 *cb_sb_start = cb;	/* Beginning of the current sb in the cb. */
	u8 *cb_sb_end;		/* End of current sb / beginning of next sb. */

	/* Variables for uncompressed data / destination. */
	struct page *dp;	/* Current destination page being worked on. */
	u8 *dp_addr;		/* Current pointer into dp. */
	u8 *dp_sb_start;	/* Start of current sub-block in dp. */
	u8 *dp_sb_end;		/* End of current sb in dp (dp_sb_start +
				   NTFS_SB_SIZE). */
	u16 do_sb_start;	/* @dest_ofs when starting this sub-block. */
	u16 do_sb_end;		/* @dest_ofs of end of this sb (do_sb_start +
				   NTFS_SB_SIZE). */

	/* Variables for tag and token parsing. */
	u8 tag;			/* Current tag. */
	int token;		/* Loop counter for the eight tokens in tag. */

	/* Need this because we can't sleep, so need two stages. */
	int completed_pages[dest_max_index - *dest_index + 1];
	int nr_completed_pages = 0;

	/* Default error code. */
	int err = -EOVERFLOW;

	ntfs_debug("Entering, cb_size = 0x%x.", cb_size);
do_next_sb:
	ntfs_debug("Beginning sub-block at offset = 0x%zx in the cb.",
			cb - cb_start);
	/*
	 * Have we reached the end of the compression block or the end of the
	 * decompressed data?  The latter can happen for example if the current
	 * position in the compression block is one byte before its end so the
	 * first two checks do not detect it.
	 */
	if (cb == cb_end || !le16_to_cpup((le16*)cb) ||
			(*dest_index == dest_max_index &&
			*dest_ofs == dest_max_ofs)) {
		int i;

		ntfs_debug("Completed. Returning success (0).");
		err = 0;
return_error:
		/* We can sleep from now on, so we drop lock. */
		spin_unlock(&ntfs_cb_lock);
		/* Second stage: finalize completed pages. */
		if (nr_completed_pages > 0) {
			for (i = 0; i < nr_completed_pages; i++) {
				int di = completed_pages[i];

				dp = dest_pages[di];
				/*
				 * If we are outside the initialized size, zero
				 * the out of bounds page range.
				 */
				handle_bounds_compressed_page(dp, i_size,
						initialized_size);
				flush_dcache_page(dp);
				kunmap(dp);
				SetPageUptodate(dp);
				unlock_page(dp);
				if (di == xpage)
					*xpage_done = 1;
				else
					put_page(dp);
				dest_pages[di] = NULL;
			}
		}
		return err;
	}

	/* Setup offsets for the current sub-block destination. */
	do_sb_start = *dest_ofs;
	do_sb_end = do_sb_start + NTFS_SB_SIZE;

	/* Check that we are still within allowed boundaries. */
	if (*dest_index == dest_max_index && do_sb_end > dest_max_ofs)
		goto return_overflow;

	/* Does the minimum size of a compressed sb overflow valid range? */
	if (cb + 6 > cb_end)
		goto return_overflow;

	/* Setup the current sub-block source pointers and validate range. */
	cb_sb_start = cb;
	cb_sb_end = cb_sb_start + (le16_to_cpup((le16*)cb) & NTFS_SB_SIZE_MASK)
			+ 3;
	if (cb_sb_end > cb_end)
		goto return_overflow;

	/* Get the current destination page. */
	dp = dest_pages[*dest_index];
	if (!dp) {
		/* No page present. Skip decompression of this sub-block. */
		cb = cb_sb_end;

		/* Advance destination position to next sub-block. */
		*dest_ofs = (*dest_ofs + NTFS_SB_SIZE) & ~PAGE_MASK;
		if (!*dest_ofs && (++*dest_index > dest_max_index))
			goto return_overflow;
		goto do_next_sb;
	}

	/* We have a valid destination page. Setup the destination pointers. */
	dp_addr = (u8*)page_address(dp) + do_sb_start;

	/* Now, we are ready to process the current sub-block (sb). */
	if (!(le16_to_cpup((le16*)cb) & NTFS_SB_IS_COMPRESSED)) {
		ntfs_debug("Found uncompressed sub-block.");
		/* This sb is not compressed, just copy it into destination. */

		/* Advance source position to first data byte. */
		cb += 2;

		/* An uncompressed sb must be full size. */
		if (cb_sb_end - cb != NTFS_SB_SIZE)
			goto return_overflow;

		/* Copy the block and advance the source position. */
		memcpy(dp_addr, cb, NTFS_SB_SIZE);
		cb += NTFS_SB_SIZE;

		/* Advance destination position to next sub-block. */
		*dest_ofs += NTFS_SB_SIZE;
		if (!(*dest_ofs &= ~PAGE_MASK)) {
finalize_page:
			/*
			 * First stage: add current page index to array of
			 * completed pages.
			 */
			completed_pages[nr_completed_pages++] = *dest_index;
			if (++*dest_index > dest_max_index)
				goto return_overflow;
		}
		goto do_next_sb;
	}
	ntfs_debug("Found compressed sub-block.");
	/* This sb is compressed, decompress it into destination. */

	/* Setup destination pointers. */
	dp_sb_start = dp_addr;
	dp_sb_end = dp_sb_start + NTFS_SB_SIZE;

	/* Forward to the first tag in the sub-block. */
	cb += 2;
do_next_tag:
	if (cb == cb_sb_end) {
		/* Check if the decompressed sub-block was not full-length. */
		if (dp_addr < dp_sb_end) {
			int nr_bytes = do_sb_end - *dest_ofs;

			ntfs_debug("Filling incomplete sub-block with "
					"zeroes.");
			/* Zero remainder and update destination position. */
			memset(dp_addr, 0, nr_bytes);
			*dest_ofs += nr_bytes;
		}
		/* We have finished the current sub-block. */
		if (!(*dest_ofs &= ~PAGE_MASK))
			goto finalize_page;
		goto do_next_sb;
	}

	/* Check we are still in range. */
	if (cb > cb_sb_end || dp_addr > dp_sb_end)
		goto return_overflow;

	/* Get the next tag and advance to first token. */
	tag = *cb++;

	/* Parse the eight tokens described by the tag. */
	for (token = 0; token < 8; token++, tag >>= 1) {
		u16 lg, pt, length, max_non_overlap;
		register u16 i;
		u8 *dp_back_addr;

		/* Check if we are done / still in range. */
		if (cb >= cb_sb_end || dp_addr > dp_sb_end)
			break;

		/* Determine token type and parse appropriately.*/
		if ((tag & NTFS_TOKEN_MASK) == NTFS_SYMBOL_TOKEN) {
			/*
			 * We have a symbol token, copy the symbol across, and
			 * advance the source and destination positions.
			 */
			*dp_addr++ = *cb++;
			++*dest_ofs;

			/* Continue with the next token. */
			continue;
		}

		/*
		 * We have a phrase token. Make sure it is not the first tag in
		 * the sb as this is illegal and would confuse the code below.
		 */
		if (dp_addr == dp_sb_start)
			goto return_overflow;

		/*
		 * Determine the number of bytes to go back (p) and the number
		 * of bytes to copy (l). We use an optimized algorithm in which
		 * we first calculate log2(current destination position in sb),
		 * which allows determination of l and p in O(1) rather than
		 * O(n). We just need an arch-optimized log2() function now.
		 */
		lg = 0;
		for (i = *dest_ofs - do_sb_start - 1; i >= 0x10; i >>= 1)
			lg++;

		/* Get the phrase token into i. */
		pt = le16_to_cpup((le16*)cb);

		/*
		 * Calculate starting position of the byte sequence in
		 * the destination using the fact that p = (pt >> (12 - lg)) + 1
		 * and make sure we don't go too far back.
		 */
		dp_back_addr = dp_addr - (pt >> (12 - lg)) - 1;
		if (dp_back_addr < dp_sb_start)
			goto return_overflow;

		/* Now calculate the length of the byte sequence. */
		length = (pt & (0xfff >> lg)) + 3;

		/* Advance destination position and verify it is in range. */
		*dest_ofs += length;
		if (*dest_ofs > do_sb_end)
			goto return_overflow;

		/* The number of non-overlapping bytes. */
		max_non_overlap = dp_addr - dp_back_addr;

		if (length <= max_non_overlap) {
			/* The byte sequence doesn't overlap, just copy it. */
			memcpy(dp_addr, dp_back_addr, length);

			/* Advance destination pointer. */
			dp_addr += length;
		} else {
			/*
			 * The byte sequence does overlap, copy non-overlapping
			 * part and then do a slow byte by byte copy for the
			 * overlapping part. Also, advance the destination
			 * pointer.
			 */
			memcpy(dp_addr, dp_back_addr, max_non_overlap);
			dp_addr += max_non_overlap;
			dp_back_addr += max_non_overlap;
			length -= max_non_overlap;
			while (length--)
				*dp_addr++ = *dp_back_addr++;
		}

		/* Advance source position and continue with the next token. */
		cb += 2;
	}

	/* No tokens left in the current tag. Continue with the next tag. */
	goto do_next_tag;

return_overflow:
	ntfs_error(NULL, "Failed. Returning -EOVERFLOW.");
	goto return_error;
}

/**
 * ntfs_read_compressed_block - read a compressed block into the page cache
 * @page:	locked page in the compression block(s) we need to read
 *
 * When we are called the page has already been verified to be locked and the
 * attribute is known to be non-resident, not encrypted, but compressed.
 *
 * 1. Determine which compression block(s) @page is in.
 * 2. Get hold of all pages corresponding to this/these compression block(s).
 * 3. Read the (first) compression block.
 * 4. Decompress it into the corresponding pages.
 * 5. Throw the compressed data away and proceed to 3. for the next compression
 *    block or return success if no more compression blocks left.
 *
 * Warning: We have to be careful what we do about existing pages. They might
 * have been written to so that we would lose data if we were to just overwrite
 * them with the out-of-date uncompressed data.
 *
 * FIXME: For PAGE_SIZE > cb_size we are not doing the Right Thing(TM) at
 * the end of the file I think. We need to detect this case and zero the out
 * of bounds remainder of the page in question and mark it as handled. At the
 * moment we would just return -EIO on such a page. This bug will only become
 * apparent if pages are above 8kiB and the NTFS volume only uses 512 byte
 * clusters so is probably not going to be seen by anyone. Still this should
 * be fixed. (AIA)
 *
 * FIXME: Again for PAGE_SIZE > cb_size we are screwing up both in
 * handling sparse and compressed cbs. (AIA)
 *
 * FIXME: At the moment we don't do any zeroing out in the case that
 * initialized_size is less than data_size. This should be safe because of the
 * nature of the compression algorithm used. Just in case we check and output
 * an error message in read inode if the two sizes are not equal for a
 * compressed file. (AIA)
 */
int ntfs_read_compressed_block(struct page *page)
{
	loff_t i_size;
	s64 initialized_size;
	struct address_space *mapping = page->mapping;
	ntfs_inode *ni = NTFS_I(mapping->host);
	ntfs_volume *vol = ni->vol;
	struct super_block *sb = vol->sb;
	runlist_element *rl;
	unsigned long flags, block_size = sb->s_blocksize;
	unsigned char block_size_bits = sb->s_blocksize_bits;
	u8 *cb, *cb_pos, *cb_end;
	struct buffer_head **bhs;
	unsigned long offset, index = page->index;
	u32 cb_size = ni->itype.compressed.block_size;
	u64 cb_size_mask = cb_size - 1UL;
	VCN vcn;
	LCN lcn;
	/* The first wanted vcn (minimum alignment is PAGE_SIZE). */
	VCN start_vcn = (((s64)index << PAGE_SHIFT) & ~cb_size_mask) >>
			vol->cluster_size_bits;
	/*
	 * The first vcn after the last wanted vcn (minimum alignment is again
	 * PAGE_SIZE.
	 */
	VCN end_vcn = ((((s64)(index + 1UL) << PAGE_SHIFT) + cb_size - 1)
			& ~cb_size_mask) >> vol->cluster_size_bits;
	/* Number of compression blocks (cbs) in the wanted vcn range. */
	unsigned int nr_cbs = (end_vcn - start_vcn) << vol->cluster_size_bits
			>> ni->itype.compressed.block_size_bits;
	/*
	 * Number of pages required to store the uncompressed data from all
	 * compression blocks (cbs) overlapping @page. Due to alignment
	 * guarantees of start_vcn and end_vcn, no need to round up here.
	 */
	unsigned int nr_pages = (end_vcn - start_vcn) <<
			vol->cluster_size_bits >> PAGE_SHIFT;
	unsigned int xpage, max_page, cur_page, cur_ofs, i;
	unsigned int cb_clusters, cb_max_ofs;
	int block, max_block, cb_max_page, bhs_size, nr_bhs, err = 0;
	struct page **pages;
	unsigned char xpage_done = 0;

	ntfs_debug("Entering, page->index = 0x%lx, cb_size = 0x%x, nr_pages = "
			"%i.", index, cb_size, nr_pages);
	/*
	 * Bad things happen if we get here for anything that is not an
	 * unnamed $DATA attribute.
	 */
	BUG_ON(ni->type != AT_DATA);
	BUG_ON(ni->name_len);

	pages = kmalloc(nr_pages * sizeof(struct page *), GFP_NOFS);

	/* Allocate memory to store the buffer heads we need. */
	bhs_size = cb_size / block_size * sizeof(struct buffer_head *);
	bhs = kmalloc(bhs_size, GFP_NOFS);

	if (unlikely(!pages || !bhs)) {
		kfree(bhs);
		kfree(pages);
		unlock_page(page);
		ntfs_error(vol->sb, "Failed to allocate internal buffers.");
		return -ENOMEM;
	}

	/*
	 * We have already been given one page, this is the one we must do.
	 * Once again, the alignment guarantees keep it simple.
	 */
	offset = start_vcn << vol->cluster_size_bits >> PAGE_SHIFT;
	xpage = index - offset;
	pages[xpage] = page;
	/*
	 * The remaining pages need to be allocated and inserted into the page
	 * cache, alignment guarantees keep all the below much simpler. (-8
	 */
	read_lock_irqsave(&ni->size_lock, flags);
	i_size = i_size_read(VFS_I(ni));
	initialized_size = ni->initialized_size;
	read_unlock_irqrestore(&ni->size_lock, flags);
	max_page = ((i_size + PAGE_SIZE - 1) >> PAGE_SHIFT) -
			offset;
	/* Is the page fully outside i_size? (truncate in progress) */
	if (xpage >= max_page) {
		kfree(bhs);
		kfree(pages);
		zero_user(page, 0, PAGE_SIZE);
		ntfs_debug("Compressed read outside i_size - truncated?");
		SetPageUptodate(page);
		unlock_page(page);
		return 0;
	}
	if (nr_pages < max_page)
		max_page = nr_pages;
	for (i = 0; i < max_page; i++, offset++) {
		if (i != xpage)
			pages[i] = grab_cache_page_nowait(mapping, offset);
		page = pages[i];
		if (page) {
			/*
			 * We only (re)read the page if it isn't already read
			 * in and/or dirty or we would be losing data or at
			 * least wasting our time.
			 */
			if (!PageDirty(page) && (!PageUptodate(page) ||
					PageError(page))) {
				ClearPageError(page);
				kmap(page);
				continue;
			}
			unlock_page(page);
			put_page(page);
			pages[i] = NULL;
		}
	}

	/*
	 * We have the runlist, and all the destination pages we need to fill.
	 * Now read the first compression block.
	 */
	cur_page = 0;
	cur_ofs = 0;
	cb_clusters = ni->itype.compressed.block_clusters;
do_next_cb:
	nr_cbs--;
	nr_bhs = 0;

	/* Read all cb buffer heads one cluster at a time. */
	rl = NULL;
	for (vcn = start_vcn, start_vcn += cb_clusters; vcn < start_vcn;
			vcn++) {
		bool is_retry = false;

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
		ntfs_debug("Reading vcn = 0x%llx, lcn = 0x%llx.",
				(unsigned long long)vcn,
				(unsigned long long)lcn);
		if (lcn < 0) {
			/*
			 * When we reach the first sparse cluster we have
			 * finished with the cb.
			 */
			if (lcn == LCN_HOLE)
				break;
			if (is_retry || lcn != LCN_RL_NOT_MAPPED)
				goto rl_err;
			is_retry = true;
			/*
			 * Attempt to map runlist, dropping lock for the
			 * duration.
			 */
			up_read(&ni->runlist.lock);
			if (!ntfs_map_runlist(ni, vcn))
				goto lock_retry_remap;
			goto map_rl_err;
		}
		block = lcn << vol->cluster_size_bits >> block_size_bits;
		/* Read the lcn from device in chunks of block_size bytes. */
		max_block = block + (vol->cluster_size >> block_size_bits);
		do {
			ntfs_debug("block = 0x%x.", block);
			if (unlikely(!(bhs[nr_bhs] = sb_getblk(sb, block))))
				goto getblk_err;
			nr_bhs++;
		} while (++block < max_block);
	}

	/* Release the lock if we took it. */
	if (rl)
		up_read(&ni->runlist.lock);

	/* Setup and initiate io on all buffer heads. */
	for (i = 0; i < nr_bhs; i++) {
		struct buffer_head *tbh = bhs[i];

		if (!trylock_buffer(tbh))
			continue;
		if (unlikely(buffer_uptodate(tbh))) {
			unlock_buffer(tbh);
			continue;
		}
		get_bh(tbh);
		tbh->b_end_io = end_buffer_read_sync;
		submit_bh(READ, tbh);
	}

	/* Wait for io completion on all buffer heads. */
	for (i = 0; i < nr_bhs; i++) {
		struct buffer_head *tbh = bhs[i];

		if (buffer_uptodate(tbh))
			continue;
		wait_on_buffer(tbh);
		/*
		 * We need an optimization barrier here, otherwise we start
		 * hitting the below fixup code when accessing a loopback
		 * mounted ntfs partition. This indicates either there is a
		 * race condition in the loop driver or, more likely, gcc
		 * overoptimises the code without the barrier and it doesn't
		 * do the Right Thing(TM).
		 */
		barrier();
		if (unlikely(!buffer_uptodate(tbh))) {
			ntfs_warning(vol->sb, "Buffer is unlocked but not "
					"uptodate! Unplugging the disk queue "
					"and rescheduling.");
			get_bh(tbh);
			io_schedule();
			put_bh(tbh);
			if (unlikely(!buffer_uptodate(tbh)))
				goto read_err;
			ntfs_warning(vol->sb, "Buffer is now uptodate. Good.");
		}
	}

	/*
	 * Get the compression buffer. We must not sleep any more
	 * until we are finished with it.
	 */
	spin_lock(&ntfs_cb_lock);
	cb = ntfs_compression_buffer;

	BUG_ON(!cb);

	cb_pos = cb;
	cb_end = cb + cb_size;

	/* Copy the buffer heads into the contiguous buffer. */
	for (i = 0; i < nr_bhs; i++) {
		memcpy(cb_pos, bhs[i]->b_data, block_size);
		cb_pos += block_size;
	}

	/* Just a precaution. */
	if (cb_pos + 2 <= cb + cb_size)
		*(u16*)cb_pos = 0;

	/* Reset cb_pos back to the beginning. */
	cb_pos = cb;

	/* We now have both source (if present) and destination. */
	ntfs_debug("Successfully read the compression block.");

	/* The last page and maximum offset within it for the current cb. */
	cb_max_page = (cur_page << PAGE_SHIFT) + cur_ofs + cb_size;
	cb_max_ofs = cb_max_page & ~PAGE_MASK;
	cb_max_page >>= PAGE_SHIFT;

	/* Catch end of file inside a compression block. */
	if (cb_max_page > max_page)
		cb_max_page = max_page;

	if (vcn == start_vcn - cb_clusters) {
		/* Sparse cb, zero out page range overlapping the cb. */
		ntfs_debug("Found sparse compression block.");
		/* We can sleep from now on, so we drop lock. */
		spin_unlock(&ntfs_cb_lock);
		if (cb_max_ofs)
			cb_max_page--;
		for (; cur_page < cb_max_page; cur_page++) {
			page = pages[cur_page];
			if (page) {
				if (likely(!cur_ofs))
					clear_page(page_address(page));
				else
					memset(page_address(page) + cur_ofs, 0,
							PAGE_SIZE -
							cur_ofs);
				flush_dcache_page(page);
				kunmap(page);
				SetPageUptodate(page);
				unlock_page(page);
				if (cur_page == xpage)
					xpage_done = 1;
				else
					put_page(page);
				pages[cur_page] = NULL;
			}
			cb_pos += PAGE_SIZE - cur_ofs;
			cur_ofs = 0;
			if (cb_pos >= cb_end)
				break;
		}
		/* If we have a partial final page, deal with it now. */
		if (cb_max_ofs && cb_pos < cb_end) {
			page = pages[cur_page];
			if (page)
				memset(page_address(page) + cur_ofs, 0,
						cb_max_ofs - cur_ofs);
			/*
			 * No need to update cb_pos at this stage:
			 *	cb_pos += cb_max_ofs - cur_ofs;
			 */
			cur_ofs = cb_max_ofs;
		}
	} else if (vcn == start_vcn) {
		/* We can't sleep so we need two stages. */
		unsigned int cur2_page = cur_page;
		unsigned int cur_ofs2 = cur_ofs;
		u8 *cb_pos2 = cb_pos;

		ntfs_debug("Found uncompressed compression block.");
		/* Uncompressed cb, copy it to the destination pages. */
		/*
		 * TODO: As a big optimization, we could detect this case
		 * before we read all the pages and use block_read_full_page()
		 * on all full pages instead (we still have to treat partial
		 * pages especially but at least we are getting rid of the
		 * synchronous io for the majority of pages.
		 * Or if we choose not to do the read-ahead/-behind stuff, we
		 * could just return block_read_full_page(pages[xpage]) as long
		 * as PAGE_SIZE <= cb_size.
		 */
		if (cb_max_ofs)
			cb_max_page--;
		/* First stage: copy data into destination pages. */
		for (; cur_page < cb_max_page; cur_page++) {
			page = pages[cur_page];
			if (page)
				memcpy(page_address(page) + cur_ofs, cb_pos,
						PAGE_SIZE - cur_ofs);
			cb_pos += PAGE_SIZE - cur_ofs;
			cur_ofs = 0;
			if (cb_pos >= cb_end)
				break;
		}
		/* If we have a partial final page, deal with it now. */
		if (cb_max_ofs && cb_pos < cb_end) {
			page = pages[cur_page];
			if (page)
				memcpy(page_address(page) + cur_ofs, cb_pos,
						cb_max_ofs - cur_ofs);
			cb_pos += cb_max_ofs - cur_ofs;
			cur_ofs = cb_max_ofs;
		}
		/* We can sleep from now on, so drop lock. */
		spin_unlock(&ntfs_cb_lock);
		/* Second stage: finalize pages. */
		for (; cur2_page < cb_max_page; cur2_page++) {
			page = pages[cur2_page];
			if (page) {
				/*
				 * If we are outside the initialized size, zero
				 * the out of bounds page range.
				 */
				handle_bounds_compressed_page(page, i_size,
						initialized_size);
				flush_dcache_page(page);
				kunmap(page);
				SetPageUptodate(page);
				unlock_page(page);
				if (cur2_page == xpage)
					xpage_done = 1;
				else
					put_page(page);
				pages[cur2_page] = NULL;
			}
			cb_pos2 += PAGE_SIZE - cur_ofs2;
			cur_ofs2 = 0;
			if (cb_pos2 >= cb_end)
				break;
		}
	} else {
		/* Compressed cb, decompress it into the destination page(s). */
		unsigned int prev_cur_page = cur_page;

		ntfs_debug("Found compressed compression block.");
		err = ntfs_decompress(pages, &cur_page, &cur_ofs,
				cb_max_page, cb_max_ofs, xpage, &xpage_done,
				cb_pos,	cb_size - (cb_pos - cb), i_size,
				initialized_size);
		/*
		 * We can sleep from now on, lock already dropped by
		 * ntfs_decompress().
		 */
		if (err) {
			ntfs_error(vol->sb, "ntfs_decompress() failed in inode "
					"0x%lx with error code %i. Skipping "
					"this compression block.",
					ni->mft_no, -err);
			/* Release the unfinished pages. */
			for (; prev_cur_page < cur_page; prev_cur_page++) {
				page = pages[prev_cur_page];
				if (page) {
					flush_dcache_page(page);
					kunmap(page);
					unlock_page(page);
					if (prev_cur_page != xpage)
						put_page(page);
					pages[prev_cur_page] = NULL;
				}
			}
		}
	}

	/* Release the buffer heads. */
	for (i = 0; i < nr_bhs; i++)
		brelse(bhs[i]);

	/* Do we have more work to do? */
	if (nr_cbs)
		goto do_next_cb;

	/* We no longer need the list of buffer heads. */
	kfree(bhs);

	/* Clean up if we have any pages left. Should never happen. */
	for (cur_page = 0; cur_page < max_page; cur_page++) {
		page = pages[cur_page];
		if (page) {
			ntfs_error(vol->sb, "Still have pages left! "
					"Terminating them with extreme "
					"prejudice.  Inode 0x%lx, page index "
					"0x%lx.", ni->mft_no, page->index);
			flush_dcache_page(page);
			kunmap(page);
			unlock_page(page);
			if (cur_page != xpage)
				put_page(page);
			pages[cur_page] = NULL;
		}
	}

	/* We no longer need the list of pages. */
	kfree(pages);

	/* If we have completed the requested page, we return success. */
	if (likely(xpage_done))
		return 0;

	ntfs_debug("Failed. Returning error code %s.", err == -EOVERFLOW ?
			"EOVERFLOW" : (!err ? "EIO" : "unknown error"));
	return err < 0 ? err : -EIO;

read_err:
	ntfs_error(vol->sb, "IO error while reading compressed data.");
	/* Release the buffer heads. */
	for (i = 0; i < nr_bhs; i++)
		brelse(bhs[i]);
	goto err_out;

map_rl_err:
	ntfs_error(vol->sb, "ntfs_map_runlist() failed. Cannot read "
			"compression block.");
	goto err_out;

rl_err:
	up_read(&ni->runlist.lock);
	ntfs_error(vol->sb, "ntfs_rl_vcn_to_lcn() failed. Cannot read "
			"compression block.");
	goto err_out;

getblk_err:
	up_read(&ni->runlist.lock);
	ntfs_error(vol->sb, "getblk() failed. Cannot read compression block.");

err_out:
	kfree(bhs);
	for (i = cur_page; i < max_page; i++) {
		page = pages[i];
		if (page) {
			flush_dcache_page(page);
			kunmap(page);
			unlock_page(page);
			if (i != xpage)
				put_page(page);
		}
	}
	kfree(pages);
	return -EIO;
}
