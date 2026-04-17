// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * NTFS kernel compressed attributes handling.
 *
 * Copyright (c) 2001-2004 Anton Altaparmakov
 * Copyright (c) 2002 Richard Russon
 * Copyright (c) 2025 LG Electronics Co., Ltd.
 *
 * Part of this file is based on code from the NTFS-3G.
 * and is copyrighted by the respective authors below:
 * Copyright (c) 2004-2005 Anton Altaparmakov
 * Copyright (c) 2004-2006 Szabolcs Szakacsits
 * Copyright (c)      2005 Yura Pakhuchiy
 * Copyright (c) 2009-2014 Jean-Pierre Andre
 * Copyright (c)      2014 Eric Biggers
 */

#include <linux/fs.h>
#include <linux/blkdev.h>
#include <linux/vmalloc.h>
#include <linux/slab.h>

#include "attrib.h"
#include "inode.h"
#include "debug.h"
#include "ntfs.h"
#include "lcnalloc.h"
#include "mft.h"

/*
 * Constants used in the compression code
 */
enum {
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
};

/*
 * ntfs_compression_buffer - one buffer for the decompression engine
 */
static u8 *ntfs_compression_buffer;

/*
 * ntfs_cb_lock - mutex lock which protects ntfs_compression_buffer
 */
static DEFINE_MUTEX(ntfs_cb_lock);

/*
 * allocate_compression_buffers - allocate the decompression buffers
 *
 * Caller has to hold the ntfs_lock mutex.
 *
 * Return 0 on success or -ENOMEM if the allocations failed.
 */
int allocate_compression_buffers(void)
{
	if (ntfs_compression_buffer)
		return 0;

	ntfs_compression_buffer = vmalloc(NTFS_MAX_CB_SIZE);
	if (!ntfs_compression_buffer)
		return -ENOMEM;
	return 0;
}

/*
 * free_compression_buffers - free the decompression buffers
 *
 * Caller has to hold the ntfs_lock mutex.
 */
void free_compression_buffers(void)
{
	mutex_lock(&ntfs_cb_lock);
	if (!ntfs_compression_buffer) {
		mutex_unlock(&ntfs_cb_lock);
		return;
	}

	vfree(ntfs_compression_buffer);
	ntfs_compression_buffer = NULL;
	mutex_unlock(&ntfs_cb_lock);
}

/*
 * zero_partial_compressed_page - zero out of bounds compressed page region
 * @page: page to zero
 * @initialized_size: initialized size of the attribute
 */
static void zero_partial_compressed_page(struct page *page,
		const s64 initialized_size)
{
	u8 *kp = page_address(page);
	unsigned int kp_ofs;

	ntfs_debug("Zeroing page region outside initialized size.");
	if (((s64)page->__folio_index << PAGE_SHIFT) >= initialized_size) {
		clear_page(kp);
		return;
	}
	kp_ofs = initialized_size & ~PAGE_MASK;
	memset(kp + kp_ofs, 0, PAGE_SIZE - kp_ofs);
}

/*
 * handle_bounds_compressed_page - test for&handle out of bounds compressed page
 * @page: page to check and handle
 * @i_size: file size
 * @initialized_size: initialized size of the attribute
 */
static inline void handle_bounds_compressed_page(struct page *page,
		const loff_t i_size, const s64 initialized_size)
{
	if ((page->__folio_index >= (initialized_size >> PAGE_SHIFT)) &&
			(initialized_size < i_size))
		zero_partial_compressed_page(page, initialized_size);
}

/*
 * ntfs_decompress - decompress a compression block into an array of pages
 * @dest_pages:		destination array of pages
 * @completed_pages:	scratch space to track completed pages
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
static int ntfs_decompress(struct page *dest_pages[], int completed_pages[],
		int *dest_index, int *dest_ofs, const int dest_max_index,
		const int dest_max_ofs, const int xpage, char *xpage_done,
		u8 *const cb_start, const u32 cb_size, const loff_t i_size,
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
	u8 *dp_sb_end;		/* End of current sb in dp (dp_sb_start + NTFS_SB_SIZE). */
	u16 do_sb_start;	/* @dest_ofs when starting this sub-block. */
	u16 do_sb_end;		/* @dest_ofs of end of this sb (do_sb_start + NTFS_SB_SIZE). */

	/* Variables for tag and token parsing. */
	u8 tag;			/* Current tag. */
	int token;		/* Loop counter for the eight tokens in tag. */
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
	if (cb == cb_end || !le16_to_cpup((__le16 *)cb) ||
			(*dest_index == dest_max_index &&
			*dest_ofs == dest_max_ofs)) {
		int i;

		ntfs_debug("Completed. Returning success (0).");
		err = 0;
return_error:
		/* We can sleep from now on, so we drop lock. */
		mutex_unlock(&ntfs_cb_lock);
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
				kunmap_local(page_address(dp));
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
	cb_sb_end = cb_sb_start + (le16_to_cpup((__le16 *)cb) & NTFS_SB_SIZE_MASK)
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
	dp_addr = (u8 *)page_address(dp) + do_sb_start;

	/* Now, we are ready to process the current sub-block (sb). */
	if (!(le16_to_cpup((__le16 *)cb) & NTFS_SB_IS_COMPRESSED)) {
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
		*dest_ofs &= ~PAGE_MASK;
		if (!(*dest_ofs)) {
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

			ntfs_debug("Filling incomplete sub-block with zeroes.");
			/* Zero remainder and update destination position. */
			memset(dp_addr, 0, nr_bytes);
			*dest_ofs += nr_bytes;
		}
		/* We have finished the current sub-block. */
		*dest_ofs &= ~PAGE_MASK;
		if (!(*dest_ofs))
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
		register u16 i;
		u16 lg, pt, length, max_non_overlap;
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
		pt = le16_to_cpup((__le16 *)cb);

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

/*
 * ntfs_read_compressed_block - read a compressed block into the page cache
 * @folio:	locked folio in the compression block(s) we need to read
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
 */
int ntfs_read_compressed_block(struct folio *folio)
{
	struct page *page = &folio->page;
	loff_t i_size;
	s64 initialized_size;
	struct address_space *mapping = page->mapping;
	struct ntfs_inode *ni = NTFS_I(mapping->host);
	struct ntfs_volume *vol = ni->vol;
	struct super_block *sb = vol->sb;
	struct runlist_element *rl;
	unsigned long flags;
	u8 *cb, *cb_pos, *cb_end;
	unsigned long offset, index = page->__folio_index;
	u32 cb_size = ni->itype.compressed.block_size;
	u64 cb_size_mask = cb_size - 1UL;
	s64 vcn;
	s64 lcn;
	/* The first wanted vcn (minimum alignment is PAGE_SIZE). */
	s64 start_vcn = (((s64)index << PAGE_SHIFT) & ~cb_size_mask) >>
			vol->cluster_size_bits;
	/*
	 * The first vcn after the last wanted vcn (minimum alignment is again
	 * PAGE_SIZE.
	 */
	s64 end_vcn = ((((s64)(index + 1UL) << PAGE_SHIFT) + cb_size - 1)
			& ~cb_size_mask) >> vol->cluster_size_bits;
	/* Number of compression blocks (cbs) in the wanted vcn range. */
	unsigned int nr_cbs = ntfs_cluster_to_bytes(vol, end_vcn - start_vcn) >>
			ni->itype.compressed.block_size_bits;
	/*
	 * Number of pages required to store the uncompressed data from all
	 * compression blocks (cbs) overlapping @page. Due to alignment
	 * guarantees of start_vcn and end_vcn, no need to round up here.
	 */
	unsigned int nr_pages = ntfs_cluster_to_pidx(vol, end_vcn - start_vcn);
	unsigned int xpage, max_page, cur_page, cur_ofs, i, page_ofs, page_index;
	unsigned int cb_clusters, cb_max_ofs;
	int cb_max_page, err = 0;
	struct page **pages;
	int *completed_pages;
	unsigned char xpage_done = 0;
	struct page *lpage;

	ntfs_debug("Entering, page->index = 0x%lx, cb_size = 0x%x, nr_pages = %i.",
			index, cb_size, nr_pages);
	/*
	 * Bad things happen if we get here for anything that is not an
	 * unnamed $DATA attribute.
	 */
	if (ni->type != AT_DATA || ni->name_len) {
		unlock_page(page);
		return -EIO;
	}

	pages = kmalloc_array(nr_pages, sizeof(struct page *), GFP_NOFS);
	completed_pages = kmalloc_array(nr_pages + 1, sizeof(int), GFP_NOFS);

	if (unlikely(!pages || !completed_pages)) {
		kfree(pages);
		kfree(completed_pages);
		unlock_page(page);
		ntfs_error(vol->sb, "Failed to allocate internal buffers.");
		return -ENOMEM;
	}

	/*
	 * We have already been given one page, this is the one we must do.
	 * Once again, the alignment guarantees keep it simple.
	 */
	offset = ntfs_cluster_to_pidx(vol, start_vcn);
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
		kfree(pages);
		kfree(completed_pages);
		zero_user_segments(page, 0, PAGE_SIZE, 0, 0);
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
			if (!PageDirty(page) && (!PageUptodate(page))) {
				kmap_local_page(page);
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

	mutex_lock(&ntfs_cb_lock);
	if (!ntfs_compression_buffer)
		if (allocate_compression_buffers()) {
			mutex_unlock(&ntfs_cb_lock);
			goto err_out;
		}


	cb = ntfs_compression_buffer;
	cb_pos = cb;
	cb_end = cb + cb_size;

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
			if (is_retry || lcn != LCN_RL_NOT_MAPPED) {
				mutex_unlock(&ntfs_cb_lock);
				goto rl_err;
			}
			is_retry = true;
			/*
			 * Attempt to map runlist, dropping lock for the
			 * duration.
			 */
			up_read(&ni->runlist.lock);
			if (!ntfs_map_runlist(ni, vcn))
				goto lock_retry_remap;
			mutex_unlock(&ntfs_cb_lock);
			goto map_rl_err;
		}

		page_ofs = ntfs_cluster_to_poff(vol, lcn);
		page_index = ntfs_cluster_to_pidx(vol, lcn);

		lpage = read_mapping_page(sb->s_bdev->bd_mapping,
					  page_index, NULL);
		if (IS_ERR(lpage)) {
			err = PTR_ERR(lpage);
			mutex_unlock(&ntfs_cb_lock);
			goto read_err;
		}

		lock_page(lpage);
		memcpy(cb_pos, page_address(lpage) + page_ofs,
		       vol->cluster_size);
		unlock_page(lpage);
		put_page(lpage);
		cb_pos += vol->cluster_size;
	}

	/* Release the lock if we took it. */
	if (rl)
		up_read(&ni->runlist.lock);

	/* Just a precaution. */
	if (cb_pos + 2 <= cb + cb_size)
		*(u16 *)cb_pos = 0;

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
		mutex_unlock(&ntfs_cb_lock);
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
				kunmap_local(page_address(page));
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
		mutex_unlock(&ntfs_cb_lock);
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
				kunmap_local(page_address(page));
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
		err = ntfs_decompress(pages, completed_pages, &cur_page,
				&cur_ofs, cb_max_page, cb_max_ofs, xpage,
				&xpage_done, cb_pos, cb_size - (cb_pos - cb),
				i_size, initialized_size);
		/*
		 * We can sleep from now on, lock already dropped by
		 * ntfs_decompress().
		 */
		if (err) {
			ntfs_error(vol->sb,
				"ntfs_decompress() failed in inode 0x%llx with error code %i. Skipping this compression block.",
				ni->mft_no, -err);
			/* Release the unfinished pages. */
			for (; prev_cur_page < cur_page; prev_cur_page++) {
				page = pages[prev_cur_page];
				if (page) {
					flush_dcache_page(page);
					kunmap_local(page_address(page));
					unlock_page(page);
					if (prev_cur_page != xpage)
						put_page(page);
					pages[prev_cur_page] = NULL;
				}
			}
		}
	}

	/* Do we have more work to do? */
	if (nr_cbs)
		goto do_next_cb;

	/* Clean up if we have any pages left. Should never happen. */
	for (cur_page = 0; cur_page < max_page; cur_page++) {
		page = pages[cur_page];
		if (page) {
			ntfs_error(vol->sb,
				"Still have pages left! Terminating them with extreme prejudice.  Inode 0x%llx, page index 0x%lx.",
				ni->mft_no, page->__folio_index);
			flush_dcache_page(page);
			kunmap_local(page_address(page));
			unlock_page(page);
			if (cur_page != xpage)
				put_page(page);
			pages[cur_page] = NULL;
		}
	}

	/* We no longer need the list of pages. */
	kfree(pages);
	kfree(completed_pages);

	/* If we have completed the requested page, we return success. */
	if (likely(xpage_done))
		return 0;

	ntfs_debug("Failed. Returning error code %s.", err == -EOVERFLOW ?
			"EOVERFLOW" : (!err ? "EIO" : "unknown error"));
	return err < 0 ? err : -EIO;

map_rl_err:
	ntfs_error(vol->sb, "ntfs_map_runlist() failed. Cannot read compression block.");
	goto err_out;

rl_err:
	up_read(&ni->runlist.lock);
	ntfs_error(vol->sb, "ntfs_rl_vcn_to_lcn() failed. Cannot read compression block.");
	goto err_out;

read_err:
	up_read(&ni->runlist.lock);
	ntfs_error(vol->sb, "IO error while reading compressed data.");

err_out:
	for (i = cur_page; i < max_page; i++) {
		page = pages[i];
		if (page) {
			flush_dcache_page(page);
			kunmap_local(page_address(page));
			unlock_page(page);
			if (i != xpage)
				put_page(page);
		}
	}
	kfree(pages);
	kfree(completed_pages);
	return -EIO;
}

/*
 * Match length at or above which ntfs_best_match() will stop searching for
 * longer matches.
 */
#define NICE_MATCH_LEN		18

/*
 * Maximum number of potential matches that ntfs_best_match() will consider at
 * each position.
 */
#define MAX_SEARCH_DEPTH	24

/* log base 2 of the number of entries in the hash table for match-finding.  */
#define HASH_SHIFT		14

/*
 * Constant for the multiplicative hash function. These hashing constants
 * are used solely for the match-finding algorithm during compression.
 * They are NOT part of the on-disk format. The decompressor does not
 * utilize this hash.
 */
#define HASH_MULTIPLIER		0x1E35A7BD

struct compress_context {
	const unsigned char *inbuf;
	int bufsize;
	int size;
	int rel;
	int mxsz;
	s16 head[1 << HASH_SHIFT];
	s16 prev[NTFS_SB_SIZE];
};

/*
 * Hash the next 3-byte sequence in the input buffer
 */
static inline unsigned int ntfs_hash(const u8 *p)
{
	u32 str;
	u32 hash;

	/*
	 * Unaligned access allowed, and little endian CPU.
	 * Callers ensure that at least 4 (not 3) bytes are remaining.
	 */
	str = *(const u32 *)p & 0xFFFFFF;
	hash = str * HASH_MULTIPLIER;

	/* High bits are more random than the low bits.  */
	return hash >> (32 - HASH_SHIFT);
}

/*
 * Search for the longest sequence matching current position
 *
 * A hash table, each entry of which points to a chain of sequence
 * positions sharing the corresponding hash code, is maintained to speed up
 * searching for matches.  To maintain the hash table, either
 * ntfs_best_match() or ntfs_skip_position() has to be called for each
 * consecutive position.
 *
 * This function is heavily used; it has to be optimized carefully.
 *
 * This function sets pctx->size and pctx->rel to the length and offset,
 * respectively, of the longest match found.
 *
 * The minimum match length is assumed to be 3, and the maximum match
 * length is assumed to be pctx->mxsz.  If this function produces
 * pctx->size < 3, then no match was found.
 *
 * Note: for the following reasons, this function is not guaranteed to find
 * *the* longest match up to pctx->mxsz:
 *
 *      (1) If this function finds a match of NICE_MATCH_LEN bytes or greater,
 *          it ends early because a match this long is good enough and it's not
 *          worth spending more time searching.
 *
 *      (2) If this function considers MAX_SEARCH_DEPTH matches with a single
 *          position, it ends early and returns the longest match found so far.
 *          This saves a lot of time on degenerate inputs.
 */
static void ntfs_best_match(struct compress_context *pctx, const int i,
		int best_len)
{
	const u8 * const inbuf = pctx->inbuf;
	const u8 * const strptr = &inbuf[i]; /* String we're matching against */
	s16 * const prev = pctx->prev;
	const int max_len = min(pctx->bufsize - i, pctx->mxsz);
	const int nice_len = min(NICE_MATCH_LEN, max_len);
	int depth_remaining = MAX_SEARCH_DEPTH;
	const u8 *best_matchptr = strptr;
	unsigned int hash;
	s16 cur_match;
	const u8 *matchptr;
	int len;

	if (max_len < 4)
		goto out;

	/* Insert the current sequence into the appropriate hash chain. */
	hash = ntfs_hash(strptr);
	cur_match = pctx->head[hash];
	prev[i] = cur_match;
	pctx->head[hash] = i;

	if (best_len >= max_len) {
		/*
		 * Lazy match is being attempted, but there aren't enough length
		 * bits remaining to code a longer match.
		 */
		goto out;
	}

	/* Search the appropriate hash chain for matches. */

	for (; cur_match >= 0 && depth_remaining--; cur_match = prev[cur_match]) {
		matchptr = &inbuf[cur_match];

		/*
		 * Considering the potential match at 'matchptr':  is it longer
		 * than 'best_len'?
		 *
		 * The bytes at index 'best_len' are the most likely to differ,
		 * so check them first.
		 *
		 * The bytes at indices 'best_len - 1' and '0' are less
		 * important to check separately.  But doing so still gives a
		 * slight performance improvement, at least on x86_64, probably
		 * because they create separate branches for the CPU to predict
		 * independently of the branches in the main comparison loops.
		 */
		if (matchptr[best_len] != strptr[best_len] ||
				matchptr[best_len - 1] != strptr[best_len - 1] ||
				matchptr[0] != strptr[0])
			goto next_match;

		for (len = 1; len < best_len - 1; len++)
			if (matchptr[len] != strptr[len])
				goto next_match;

		/*
		 * The match is the longest found so far ---
		 * at least 'best_len' + 1 bytes.  Continue extending it.
		 */

		best_matchptr = matchptr;

		do {
			if (++best_len >= nice_len) {
				/*
				 * 'nice_len' reached; don't waste time
				 * searching for longer matches.  Extend the
				 * match as far as possible and terminate the
				 * search.
				 */
				while (best_len < max_len &&
				       (best_matchptr[best_len] ==
					strptr[best_len]))
					best_len++;
				goto out;
			}
		} while (best_matchptr[best_len] == strptr[best_len]);

		/* Found a longer match, but 'nice_len' not yet reached.  */

next_match:
		/* Continue to next match in the chain.  */
		;
	}

	/*
	 * Reached end of chain, or ended early due to reaching the maximum
	 * search depth.
	 */

out:
	/* Return the longest match we were able to find.  */
	pctx->size = best_len;
	pctx->rel = best_matchptr - strptr; /* given as a negative number! */
}

/*
 * Advance the match-finder, but don't search for matches.
 */
static void ntfs_skip_position(struct compress_context *pctx, const int i)
{
	unsigned int hash;

	if (pctx->bufsize - i < 4)
		return;

	/* Insert the current sequence into the appropriate hash chain.  */
	hash = ntfs_hash(pctx->inbuf + i);
	pctx->prev[i] = pctx->head[hash];
	pctx->head[hash] = i;
}

/*
 * Compress a 4096-byte block
 *
 * Returns a header of two bytes followed by the compressed data.
 * If compression is not effective, the header and an uncompressed
 * block is returned.
 *
 * Note : two bytes may be output before output buffer overflow
 * is detected, so a 4100-bytes output buffer must be reserved.
 *
 * Returns the size of the compressed block, including the
 * header (minimal size is 2, maximum size is 4098)
 * 0 if an error has been met.
 */
static unsigned int ntfs_compress_block(const char *inbuf, const int bufsize,
		char *outbuf)
{
	struct compress_context *pctx;
	int i; /* current position */
	int j; /* end of best match from current position */
	int k; /* end of best match from next position */
	int offs; /* offset to best match */
	int bp; /* bits to store offset */
	int bp_cur; /* saved bits to store offset at current position */
	int mxoff; /* max match offset : 1 << bp */
	unsigned int xout;
	unsigned int q; /* aggregated offset and size */
	int have_match; /* do we have a match at the current position? */
	char *ptag; /* location reserved for a tag */
	int tag;    /* current value of tag */
	int ntag;   /* count of bits still undefined in tag */

	pctx = kvzalloc(sizeof(struct compress_context), GFP_NOFS);
	if (!pctx)
		return -ENOMEM;

	/*
	 * All hash chains start as empty.  The special value '-1' indicates the
	 * end of each hash chain.
	 */
	memset(pctx->head, 0xFF, sizeof(pctx->head));

	pctx->inbuf = (const unsigned char *)inbuf;
	pctx->bufsize = bufsize;
	xout = 2;
	i = 0;
	bp = 4;
	mxoff = 1 << bp;
	pctx->mxsz = (1 << (16 - bp)) + 2;
	have_match = 0;
	tag = 0;
	ntag = 8;
	ptag = &outbuf[xout++];

	while ((i < bufsize) && (xout < (NTFS_SB_SIZE + 2))) {

		/*
		 * This implementation uses "lazy" parsing: it always chooses
		 * the longest match, unless the match at the next position is
		 * longer.  This is the same strategy used by the high
		 * compression modes of zlib.
		 */
		if (!have_match) {
			/*
			 * Find the longest match at the current position.  But
			 * first adjust the maximum match length if needed.
			 * (This loop might need to run more than one time in
			 * the case that we just output a long match.)
			 */
			while (mxoff < i) {
				bp++;
				mxoff <<= 1;
				pctx->mxsz = (pctx->mxsz + 2) >> 1;
			}
			ntfs_best_match(pctx, i, 2);
		}

		if (pctx->size >= 3) {
			/* Found a match at the current position.  */
			j = i + pctx->size;
			bp_cur = bp;
			offs = pctx->rel;

			if (pctx->size >= NICE_MATCH_LEN) {
				/* Choose long matches immediately.  */
				q = (~offs << (16 - bp_cur)) + (j - i - 3);
				outbuf[xout++] = q & 255;
				outbuf[xout++] = (q >> 8) & 255;
				tag |= (1 << (8 - ntag));

				if (j == bufsize) {
					/*
					 * Shortcut if the match extends to the
					 * end of the buffer.
					 */
					i = j;
					--ntag;
					break;
				}
				i += 1;
				do {
					ntfs_skip_position(pctx, i);
				} while (++i != j);
				have_match = 0;
			} else {
				/*
				 * Check for a longer match at the next
				 * position.
				 */

				/*
				 * Doesn't need to be while() since we just
				 * adjusted the maximum match length at the
				 * previous position.
				 */
				if (mxoff < i + 1) {
					bp++;
					mxoff <<= 1;
					pctx->mxsz = (pctx->mxsz + 2) >> 1;
				}
				ntfs_best_match(pctx, i + 1, pctx->size);
				k = i + 1 + pctx->size;

				if (k > (j + 1)) {
					/*
					 * Next match is longer.
					 * Output a literal.
					 */
					outbuf[xout++] = inbuf[i++];
					have_match = 1;
				} else {
					/*
					 * Next match isn't longer.
					 * Output the current match.
					 */
					q = (~offs << (16 - bp_cur)) +
						(j - i - 3);
					outbuf[xout++] = q & 255;
					outbuf[xout++] = (q >> 8) & 255;
					tag |= (1 << (8 - ntag));

					/*
					 * The minimum match length is 3, and
					 * we've run two bytes through the
					 * matchfinder already.  So the minimum
					 * number of positions we need to skip
					 * is 1.
					 */
					i += 2;
					do {
						ntfs_skip_position(pctx, i);
					} while (++i != j);
					have_match = 0;
				}
			}
		} else {
			/* No match at current position.  Output a literal. */
			outbuf[xout++] = inbuf[i++];
			have_match = 0;
		}

		/* Store the tag if fully used. */
		if (!--ntag) {
			*ptag = tag;
			ntag = 8;
			ptag = &outbuf[xout++];
			tag = 0;
		}
	}

	/* Store the last tag if partially used. */
	if (ntag == 8)
		xout--;
	else
		*ptag = tag;

	/* Determine whether to store the data compressed or uncompressed. */
	if ((i >= bufsize) && (xout < (NTFS_SB_SIZE + 2))) {
		/* Compressed. */
		outbuf[0] = (xout - 3) & 255;
		outbuf[1] = 0xb0 + (((xout - 3) >> 8) & 15);
	} else {
		/* Uncompressed.  */
		memcpy(&outbuf[2], inbuf, bufsize);
		if (bufsize < NTFS_SB_SIZE)
			memset(&outbuf[bufsize + 2], 0, NTFS_SB_SIZE - bufsize);
		outbuf[0] = 0xff;
		outbuf[1] = 0x3f;
		xout = NTFS_SB_SIZE + 2;
	}

	/*
	 * Free the compression context and return the total number of bytes
	 * written to 'outbuf'.
	 */
	kvfree(pctx);
	return xout;
}

static int ntfs_write_cb(struct ntfs_inode *ni, loff_t pos, struct page **pages,
		int pages_per_cb)
{
	struct ntfs_volume *vol = ni->vol;
	char *outbuf = NULL, *pbuf, *inbuf;
	u32 compsz, p, insz = pages_per_cb << PAGE_SHIFT;
	s32 rounded, bio_size;
	unsigned int sz, bsz;
	bool fail = false, allzeroes;
	/* a single compressed zero */
	static char onezero[] = {0x01, 0xb0, 0x00, 0x00};
	/* a couple of compressed zeroes */
	static char twozeroes[] = {0x02, 0xb0, 0x00, 0x00, 0x00};
	/* more compressed zeroes, to be followed by some count */
	static char morezeroes[] = {0x03, 0xb0, 0x02, 0x00};
	struct page **pages_disk = NULL, *pg;
	s64 bio_lcn;
	struct runlist_element *rlc, *rl;
	int i, err;
	int pages_count = (round_up(ni->itype.compressed.block_size + 2 *
		(ni->itype.compressed.block_size / NTFS_SB_SIZE) + 2, PAGE_SIZE)) / PAGE_SIZE;
	size_t new_rl_count;
	struct bio *bio = NULL;
	loff_t new_length;
	s64 new_vcn;

	inbuf = vmap(pages, pages_per_cb, VM_MAP, PAGE_KERNEL_RO);
	if (!inbuf)
		return -ENOMEM;

	/* may need 2 extra bytes per block and 2 more bytes */
	pages_disk = kcalloc(pages_count, sizeof(struct page *), GFP_NOFS);
	if (!pages_disk) {
		vunmap(inbuf);
		return -ENOMEM;
	}

	for (i = 0; i < pages_count; i++) {
		pg = alloc_page(GFP_KERNEL);
		if (!pg) {
			err = -ENOMEM;
			goto out;
		}
		pages_disk[i] = pg;
		lock_page(pg);
		kmap_local_page(pg);
	}

	outbuf = vmap(pages_disk, pages_count, VM_MAP, PAGE_KERNEL);
	if (!outbuf) {
		err = -ENOMEM;
		goto out;
	}

	compsz = 0;
	allzeroes = true;
	for (p = 0; (p < insz) && !fail; p += NTFS_SB_SIZE) {
		if ((p + NTFS_SB_SIZE) < insz)
			bsz = NTFS_SB_SIZE;
		else
			bsz = insz - p;
		pbuf = &outbuf[compsz];
		sz = ntfs_compress_block(&inbuf[p], bsz, pbuf);
		/* fail if all the clusters (or more) are needed */
		if (!sz || ((compsz + sz + vol->cluster_size + 2) >
			    ni->itype.compressed.block_size))
			fail = true;
		else {
			if (allzeroes) {
				/* check whether this is all zeroes */
				switch (sz) {
				case 4:
					allzeroes = !memcmp(pbuf, onezero, 4);
					break;
				case 5:
					allzeroes = !memcmp(pbuf, twozeroes, 5);
					break;
				case 6:
					allzeroes = !memcmp(pbuf, morezeroes, 4);
					break;
				default:
					allzeroes = false;
					break;
				}
			}
			compsz += sz;
		}
	}

	if (!fail && !allzeroes) {
		outbuf[compsz++] = 0;
		outbuf[compsz++] = 0;
		rounded = ((compsz - 1) | (vol->cluster_size - 1)) + 1;
		memset(&outbuf[compsz], 0, rounded - compsz);
		bio_size = rounded;
		pages = pages_disk;
	} else if (allzeroes) {
		err = 0;
		goto out;
	} else {
		bio_size = insz;
	}

	new_vcn = ntfs_bytes_to_cluster(vol, pos & ~(ni->itype.compressed.block_size - 1));
	new_length = ntfs_bytes_to_cluster(vol, round_up(bio_size, vol->cluster_size));

	err = ntfs_non_resident_attr_punch_hole(ni, new_vcn, ni->itype.compressed.block_clusters);
	if (err < 0)
		goto out;

	rlc = ntfs_cluster_alloc(vol, new_vcn, new_length, -1, DATA_ZONE,
			false, true, true);
	if (IS_ERR(rlc)) {
		err = PTR_ERR(rlc);
		goto out;
	}

	bio_lcn = rlc->lcn;
	down_write(&ni->runlist.lock);
	rl = ntfs_runlists_merge(&ni->runlist, rlc, 0, &new_rl_count);
	if (IS_ERR(rl)) {
		up_write(&ni->runlist.lock);
		ntfs_error(vol->sb, "Failed to merge runlists");
		err = PTR_ERR(rl);
		if (ntfs_cluster_free_from_rl(vol, rlc))
			ntfs_error(vol->sb, "Failed to free hot clusters.");
		kvfree(rlc);
		goto out;
	}

	ni->runlist.count = new_rl_count;
	ni->runlist.rl = rl;

	err = ntfs_attr_update_mapping_pairs(ni, 0);
	up_write(&ni->runlist.lock);
	if (err) {
		err = -EIO;
		goto out;
	}

	i = 0;
	while (bio_size > 0) {
		int page_size;

		if (bio_size >= PAGE_SIZE) {
			page_size = PAGE_SIZE;
			bio_size -= PAGE_SIZE;
		} else {
			page_size = bio_size;
			bio_size = 0;
		}

setup_bio:
		if (!bio) {
			bio = bio_alloc(vol->sb->s_bdev, 1, REQ_OP_WRITE,
					GFP_NOIO);
			bio->bi_iter.bi_sector =
				ntfs_bytes_to_sector(vol,
						ntfs_cluster_to_bytes(vol, bio_lcn + i));
		}

		if (!bio_add_page(bio, pages[i], page_size, 0)) {
			err = submit_bio_wait(bio);
			bio_put(bio);
			if (err)
				goto out;
			bio = NULL;
			goto setup_bio;
		}
		i++;
	}

	err = submit_bio_wait(bio);
	bio_put(bio);
out:
	vunmap(outbuf);
	for (i = 0; i < pages_count; i++) {
		pg = pages_disk[i];
		if (pg) {
			kunmap_local(page_address(pg));
			unlock_page(pg);
			put_page(pg);
		}
	}
	kfree(pages_disk);
	vunmap(inbuf);
	NInoSetFileNameDirty(ni);
	mark_mft_record_dirty(ni);

	return err;
}

int ntfs_compress_write(struct ntfs_inode *ni, loff_t pos, size_t count,
		struct iov_iter *from)
{
	struct folio *folio;
	struct page **pages = NULL, *page;
	int pages_per_cb = ni->itype.compressed.block_size >> PAGE_SHIFT;
	int cb_size = ni->itype.compressed.block_size, cb_off, err = 0;
	int i, ip;
	size_t written = 0;
	struct address_space *mapping = VFS_I(ni)->i_mapping;

	if (NInoCompressed(ni) && pos + count > ni->allocated_size) {
		int err;
		loff_t end = pos + count;

		err = ntfs_attr_expand(ni, end,
				round_up(end, ni->itype.compressed.block_size));
		if (err)
			return err;
	}

	pages = kmalloc_array(pages_per_cb, sizeof(struct page *), GFP_NOFS);
	if (!pages)
		return -ENOMEM;

	while (count) {
		pgoff_t index;
		size_t copied, bytes;
		int off;

		off = pos & (cb_size - 1);
		bytes = cb_size - off;
		if (bytes > count)
			bytes = count;

		cb_off = pos & ~(cb_size - 1);
		index = cb_off >> PAGE_SHIFT;

		if (unlikely(fault_in_iov_iter_readable(from, bytes))) {
			err = -EFAULT;
			goto out;
		}

		for (i = 0; i < pages_per_cb; i++) {
			folio = read_mapping_folio(mapping, index + i, NULL);
			if (IS_ERR(folio)) {
				for (ip = 0; ip < i; ip++) {
					folio_unlock(page_folio(pages[ip]));
					folio_put(page_folio(pages[ip]));
				}
				err = PTR_ERR(folio);
				goto out;
			}

			folio_lock(folio);
			pages[i] = folio_page(folio, 0);
		}

		WARN_ON(!bytes);
		copied = 0;
		ip = off >> PAGE_SHIFT;
		off = offset_in_page(pos);

		for (;;) {
			size_t cp, tail = PAGE_SIZE - off;

			page = pages[ip];
			cp = copy_folio_from_iter_atomic(page_folio(page), off,
					min(tail, bytes), from);
			flush_dcache_page(page);

			copied += cp;
			bytes -= cp;
			if (!bytes || !cp)
				break;

			if (cp < tail) {
				off += cp;
			} else {
				ip++;
				off = 0;
			}
		}

		err = ntfs_write_cb(ni, pos, pages, pages_per_cb);

		for (i = 0; i < pages_per_cb; i++) {
			folio = page_folio(pages[i]);
			if (i < ip) {
				folio_clear_dirty(folio);
				folio_mark_uptodate(folio);
			}
			folio_unlock(folio);
			folio_put(folio);
		}

		if (err)
			goto out;

		cond_resched();
		pos += copied;
		written += copied;
		count = iov_iter_count(from);
	}

out:
	kfree(pages);
	if (err < 0)
		written = err;

	return written;
}
