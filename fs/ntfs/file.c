/*
 * file.c - NTFS kernel file operations.  Part of the Linux-NTFS project.
 *
 * Copyright (c) 2001-2011 Anton Altaparmakov and Tuxera Inc.
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

#include <linux/buffer_head.h>
#include <linux/gfp.h>
#include <linux/pagemap.h>
#include <linux/pagevec.h>
#include <linux/sched.h>
#include <linux/swap.h>
#include <linux/uio.h>
#include <linux/writeback.h>

#include <asm/page.h>
#include <asm/uaccess.h>

#include "attrib.h"
#include "bitmap.h"
#include "inode.h"
#include "debug.h"
#include "lcnalloc.h"
#include "malloc.h"
#include "mft.h"
#include "ntfs.h"

/**
 * ntfs_file_open - called when an inode is about to be opened
 * @vi:		inode to be opened
 * @filp:	file structure describing the inode
 *
 * Limit file size to the page cache limit on architectures where unsigned long
 * is 32-bits. This is the most we can do for now without overflowing the page
 * cache page index. Doing it this way means we don't run into problems because
 * of existing too large files. It would be better to allow the user to read
 * the beginning of the file but I doubt very much anyone is going to hit this
 * check on a 32-bit architecture, so there is no point in adding the extra
 * complexity required to support this.
 *
 * On 64-bit architectures, the check is hopefully optimized away by the
 * compiler.
 *
 * After the check passes, just call generic_file_open() to do its work.
 */
static int ntfs_file_open(struct inode *vi, struct file *filp)
{
	if (sizeof(unsigned long) < 8) {
		if (i_size_read(vi) > MAX_LFS_FILESIZE)
			return -EOVERFLOW;
	}
	return generic_file_open(vi, filp);
}

#ifdef NTFS_RW

/**
 * ntfs_attr_extend_initialized - extend the initialized size of an attribute
 * @ni:			ntfs inode of the attribute to extend
 * @new_init_size:	requested new initialized size in bytes
 * @cached_page:	store any allocated but unused page here
 * @lru_pvec:		lru-buffering pagevec of the caller
 *
 * Extend the initialized size of an attribute described by the ntfs inode @ni
 * to @new_init_size bytes.  This involves zeroing any non-sparse space between
 * the old initialized size and @new_init_size both in the page cache and on
 * disk (if relevant complete pages are already uptodate in the page cache then
 * these are simply marked dirty).
 *
 * As a side-effect, the file size (vfs inode->i_size) may be incremented as,
 * in the resident attribute case, it is tied to the initialized size and, in
 * the non-resident attribute case, it may not fall below the initialized size.
 *
 * Note that if the attribute is resident, we do not need to touch the page
 * cache at all.  This is because if the page cache page is not uptodate we
 * bring it uptodate later, when doing the write to the mft record since we
 * then already have the page mapped.  And if the page is uptodate, the
 * non-initialized region will already have been zeroed when the page was
 * brought uptodate and the region may in fact already have been overwritten
 * with new data via mmap() based writes, so we cannot just zero it.  And since
 * POSIX specifies that the behaviour of resizing a file whilst it is mmap()ped
 * is unspecified, we choose not to do zeroing and thus we do not need to touch
 * the page at all.  For a more detailed explanation see ntfs_truncate() in
 * fs/ntfs/inode.c.
 *
 * Return 0 on success and -errno on error.  In the case that an error is
 * encountered it is possible that the initialized size will already have been
 * incremented some way towards @new_init_size but it is guaranteed that if
 * this is the case, the necessary zeroing will also have happened and that all
 * metadata is self-consistent.
 *
 * Locking: i_mutex on the vfs inode corrseponsind to the ntfs inode @ni must be
 *	    held by the caller.
 */
static int ntfs_attr_extend_initialized(ntfs_inode *ni, const s64 new_init_size)
{
	s64 old_init_size;
	loff_t old_i_size;
	pgoff_t index, end_index;
	unsigned long flags;
	struct inode *vi = VFS_I(ni);
	ntfs_inode *base_ni;
	MFT_RECORD *m = NULL;
	ATTR_RECORD *a;
	ntfs_attr_search_ctx *ctx = NULL;
	struct address_space *mapping;
	struct page *page = NULL;
	u8 *kattr;
	int err;
	u32 attr_len;

	read_lock_irqsave(&ni->size_lock, flags);
	old_init_size = ni->initialized_size;
	old_i_size = i_size_read(vi);
	BUG_ON(new_init_size > ni->allocated_size);
	read_unlock_irqrestore(&ni->size_lock, flags);
	ntfs_debug("Entering for i_ino 0x%lx, attribute type 0x%x, "
			"old_initialized_size 0x%llx, "
			"new_initialized_size 0x%llx, i_size 0x%llx.",
			vi->i_ino, (unsigned)le32_to_cpu(ni->type),
			(unsigned long long)old_init_size,
			(unsigned long long)new_init_size, old_i_size);
	if (!NInoAttr(ni))
		base_ni = ni;
	else
		base_ni = ni->ext.base_ntfs_ino;
	/* Use goto to reduce indentation and we need the label below anyway. */
	if (NInoNonResident(ni))
		goto do_non_resident_extend;
	BUG_ON(old_init_size != old_i_size);
	m = map_mft_record(base_ni);
	if (IS_ERR(m)) {
		err = PTR_ERR(m);
		m = NULL;
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
	BUG_ON(a->non_resident);
	/* The total length of the attribute value. */
	attr_len = le32_to_cpu(a->data.resident.value_length);
	BUG_ON(old_i_size != (loff_t)attr_len);
	/*
	 * Do the zeroing in the mft record and update the attribute size in
	 * the mft record.
	 */
	kattr = (u8*)a + le16_to_cpu(a->data.resident.value_offset);
	memset(kattr + attr_len, 0, new_init_size - attr_len);
	a->data.resident.value_length = cpu_to_le32((u32)new_init_size);
	/* Finally, update the sizes in the vfs and ntfs inodes. */
	write_lock_irqsave(&ni->size_lock, flags);
	i_size_write(vi, new_init_size);
	ni->initialized_size = new_init_size;
	write_unlock_irqrestore(&ni->size_lock, flags);
	goto done;
do_non_resident_extend:
	/*
	 * If the new initialized size @new_init_size exceeds the current file
	 * size (vfs inode->i_size), we need to extend the file size to the
	 * new initialized size.
	 */
	if (new_init_size > old_i_size) {
		m = map_mft_record(base_ni);
		if (IS_ERR(m)) {
			err = PTR_ERR(m);
			m = NULL;
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
		BUG_ON(!a->non_resident);
		BUG_ON(old_i_size != (loff_t)
				sle64_to_cpu(a->data.non_resident.data_size));
		a->data.non_resident.data_size = cpu_to_sle64(new_init_size);
		flush_dcache_mft_record_page(ctx->ntfs_ino);
		mark_mft_record_dirty(ctx->ntfs_ino);
		/* Update the file size in the vfs inode. */
		i_size_write(vi, new_init_size);
		ntfs_attr_put_search_ctx(ctx);
		ctx = NULL;
		unmap_mft_record(base_ni);
		m = NULL;
	}
	mapping = vi->i_mapping;
	index = old_init_size >> PAGE_CACHE_SHIFT;
	end_index = (new_init_size + PAGE_CACHE_SIZE - 1) >> PAGE_CACHE_SHIFT;
	do {
		/*
		 * Read the page.  If the page is not present, this will zero
		 * the uninitialized regions for us.
		 */
		page = read_mapping_page(mapping, index, NULL);
		if (IS_ERR(page)) {
			err = PTR_ERR(page);
			goto init_err_out;
		}
		if (unlikely(PageError(page))) {
			page_cache_release(page);
			err = -EIO;
			goto init_err_out;
		}
		/*
		 * Update the initialized size in the ntfs inode.  This is
		 * enough to make ntfs_writepage() work.
		 */
		write_lock_irqsave(&ni->size_lock, flags);
		ni->initialized_size = (s64)(index + 1) << PAGE_CACHE_SHIFT;
		if (ni->initialized_size > new_init_size)
			ni->initialized_size = new_init_size;
		write_unlock_irqrestore(&ni->size_lock, flags);
		/* Set the page dirty so it gets written out. */
		set_page_dirty(page);
		page_cache_release(page);
		/*
		 * Play nice with the vm and the rest of the system.  This is
		 * very much needed as we can potentially be modifying the
		 * initialised size from a very small value to a really huge
		 * value, e.g.
		 *	f = open(somefile, O_TRUNC);
		 *	truncate(f, 10GiB);
		 *	seek(f, 10GiB);
		 *	write(f, 1);
		 * And this would mean we would be marking dirty hundreds of
		 * thousands of pages or as in the above example more than
		 * two and a half million pages!
		 *
		 * TODO: For sparse pages could optimize this workload by using
		 * the FsMisc / MiscFs page bit as a "PageIsSparse" bit.  This
		 * would be set in readpage for sparse pages and here we would
		 * not need to mark dirty any pages which have this bit set.
		 * The only caveat is that we have to clear the bit everywhere
		 * where we allocate any clusters that lie in the page or that
		 * contain the page.
		 *
		 * TODO: An even greater optimization would be for us to only
		 * call readpage() on pages which are not in sparse regions as
		 * determined from the runlist.  This would greatly reduce the
		 * number of pages we read and make dirty in the case of sparse
		 * files.
		 */
		balance_dirty_pages_ratelimited(mapping);
		cond_resched();
	} while (++index < end_index);
	read_lock_irqsave(&ni->size_lock, flags);
	BUG_ON(ni->initialized_size != new_init_size);
	read_unlock_irqrestore(&ni->size_lock, flags);
	/* Now bring in sync the initialized_size in the mft record. */
	m = map_mft_record(base_ni);
	if (IS_ERR(m)) {
		err = PTR_ERR(m);
		m = NULL;
		goto init_err_out;
	}
	ctx = ntfs_attr_get_search_ctx(base_ni, m);
	if (unlikely(!ctx)) {
		err = -ENOMEM;
		goto init_err_out;
	}
	err = ntfs_attr_lookup(ni->type, ni->name, ni->name_len,
			CASE_SENSITIVE, 0, NULL, 0, ctx);
	if (unlikely(err)) {
		if (err == -ENOENT)
			err = -EIO;
		goto init_err_out;
	}
	m = ctx->mrec;
	a = ctx->attr;
	BUG_ON(!a->non_resident);
	a->data.non_resident.initialized_size = cpu_to_sle64(new_init_size);
done:
	flush_dcache_mft_record_page(ctx->ntfs_ino);
	mark_mft_record_dirty(ctx->ntfs_ino);
	if (ctx)
		ntfs_attr_put_search_ctx(ctx);
	if (m)
		unmap_mft_record(base_ni);
	ntfs_debug("Done, initialized_size 0x%llx, i_size 0x%llx.",
			(unsigned long long)new_init_size, i_size_read(vi));
	return 0;
init_err_out:
	write_lock_irqsave(&ni->size_lock, flags);
	ni->initialized_size = old_init_size;
	write_unlock_irqrestore(&ni->size_lock, flags);
err_out:
	if (ctx)
		ntfs_attr_put_search_ctx(ctx);
	if (m)
		unmap_mft_record(base_ni);
	ntfs_debug("Failed.  Returning error code %i.", err);
	return err;
}

/**
 * ntfs_fault_in_pages_readable -
 *
 * Fault a number of userspace pages into pagetables.
 *
 * Unlike include/linux/pagemap.h::fault_in_pages_readable(), this one copes
 * with more than two userspace pages as well as handling the single page case
 * elegantly.
 *
 * If you find this difficult to understand, then think of the while loop being
 * the following code, except that we do without the integer variable ret:
 *
 *	do {
 *		ret = __get_user(c, uaddr);
 *		uaddr += PAGE_SIZE;
 *	} while (!ret && uaddr < end);
 *
 * Note, the final __get_user() may well run out-of-bounds of the user buffer,
 * but _not_ out-of-bounds of the page the user buffer belongs to, and since
 * this is only a read and not a write, and since it is still in the same page,
 * it should not matter and this makes the code much simpler.
 */
static inline void ntfs_fault_in_pages_readable(const char __user *uaddr,
		int bytes)
{
	const char __user *end;
	volatile char c;

	/* Set @end to the first byte outside the last page we care about. */
	end = (const char __user*)PAGE_ALIGN((unsigned long)uaddr + bytes);

	while (!__get_user(c, uaddr) && (uaddr += PAGE_SIZE, uaddr < end))
		;
}

/**
 * ntfs_fault_in_pages_readable_iovec -
 *
 * Same as ntfs_fault_in_pages_readable() but operates on an array of iovecs.
 */
static inline void ntfs_fault_in_pages_readable_iovec(const struct iovec *iov,
		size_t iov_ofs, int bytes)
{
	do {
		const char __user *buf;
		unsigned len;

		buf = iov->iov_base + iov_ofs;
		len = iov->iov_len - iov_ofs;
		if (len > bytes)
			len = bytes;
		ntfs_fault_in_pages_readable(buf, len);
		bytes -= len;
		iov++;
		iov_ofs = 0;
	} while (bytes);
}

/**
 * __ntfs_grab_cache_pages - obtain a number of locked pages
 * @mapping:	address space mapping from which to obtain page cache pages
 * @index:	starting index in @mapping at which to begin obtaining pages
 * @nr_pages:	number of page cache pages to obtain
 * @pages:	array of pages in which to return the obtained page cache pages
 * @cached_page: allocated but as yet unused page
 * @lru_pvec:	lru-buffering pagevec of caller
 *
 * Obtain @nr_pages locked page cache pages from the mapping @mapping and
 * starting at index @index.
 *
 * If a page is newly created, add it to lru list
 *
 * Note, the page locks are obtained in ascending page index order.
 */
static inline int __ntfs_grab_cache_pages(struct address_space *mapping,
		pgoff_t index, const unsigned nr_pages, struct page **pages,
		struct page **cached_page)
{
	int err, nr;

	BUG_ON(!nr_pages);
	err = nr = 0;
	do {
		pages[nr] = find_lock_page(mapping, index);
		if (!pages[nr]) {
			if (!*cached_page) {
				*cached_page = page_cache_alloc(mapping);
				if (unlikely(!*cached_page)) {
					err = -ENOMEM;
					goto err_out;
				}
			}
			err = add_to_page_cache_lru(*cached_page, mapping, index,
					GFP_KERNEL);
			if (unlikely(err)) {
				if (err == -EEXIST)
					continue;
				goto err_out;
			}
			pages[nr] = *cached_page;
			*cached_page = NULL;
		}
		index++;
		nr++;
	} while (nr < nr_pages);
out:
	return err;
err_out:
	while (nr > 0) {
		unlock_page(pages[--nr]);
		page_cache_release(pages[nr]);
	}
	goto out;
}

static inline int ntfs_submit_bh_for_read(struct buffer_head *bh)
{
	lock_buffer(bh);
	get_bh(bh);
	bh->b_end_io = end_buffer_read_sync;
	return submit_bh(READ, bh);
}

/**
 * ntfs_prepare_pages_for_non_resident_write - prepare pages for receiving data
 * @pages:	array of destination pages
 * @nr_pages:	number of pages in @pages
 * @pos:	byte position in file at which the write begins
 * @bytes:	number of bytes to be written
 *
 * This is called for non-resident attributes from ntfs_file_buffered_write()
 * with i_mutex held on the inode (@pages[0]->mapping->host).  There are
 * @nr_pages pages in @pages which are locked but not kmap()ped.  The source
 * data has not yet been copied into the @pages.
 * 
 * Need to fill any holes with actual clusters, allocate buffers if necessary,
 * ensure all the buffers are mapped, and bring uptodate any buffers that are
 * only partially being written to.
 *
 * If @nr_pages is greater than one, we are guaranteed that the cluster size is
 * greater than PAGE_CACHE_SIZE, that all pages in @pages are entirely inside
 * the same cluster and that they are the entirety of that cluster, and that
 * the cluster is sparse, i.e. we need to allocate a cluster to fill the hole.
 *
 * i_size is not to be modified yet.
 *
 * Return 0 on success or -errno on error.
 */
static int ntfs_prepare_pages_for_non_resident_write(struct page **pages,
		unsigned nr_pages, s64 pos, size_t bytes)
{
	VCN vcn, highest_vcn = 0, cpos, cend, bh_cpos, bh_cend;
	LCN lcn;
	s64 bh_pos, vcn_len, end, initialized_size;
	sector_t lcn_block;
	struct page *page;
	struct inode *vi;
	ntfs_inode *ni, *base_ni = NULL;
	ntfs_volume *vol;
	runlist_element *rl, *rl2;
	struct buffer_head *bh, *head, *wait[2], **wait_bh = wait;
	ntfs_attr_search_ctx *ctx = NULL;
	MFT_RECORD *m = NULL;
	ATTR_RECORD *a = NULL;
	unsigned long flags;
	u32 attr_rec_len = 0;
	unsigned blocksize, u;
	int err, mp_size;
	bool rl_write_locked, was_hole, is_retry;
	unsigned char blocksize_bits;
	struct {
		u8 runlist_merged:1;
		u8 mft_attr_mapped:1;
		u8 mp_rebuilt:1;
		u8 attr_switched:1;
	} status = { 0, 0, 0, 0 };

	BUG_ON(!nr_pages);
	BUG_ON(!pages);
	BUG_ON(!*pages);
	vi = pages[0]->mapping->host;
	ni = NTFS_I(vi);
	vol = ni->vol;
	ntfs_debug("Entering for inode 0x%lx, attribute type 0x%x, start page "
			"index 0x%lx, nr_pages 0x%x, pos 0x%llx, bytes 0x%zx.",
			vi->i_ino, ni->type, pages[0]->index, nr_pages,
			(long long)pos, bytes);
	blocksize = vol->sb->s_blocksize;
	blocksize_bits = vol->sb->s_blocksize_bits;
	u = 0;
	do {
		page = pages[u];
		BUG_ON(!page);
		/*
		 * create_empty_buffers() will create uptodate/dirty buffers if
		 * the page is uptodate/dirty.
		 */
		if (!page_has_buffers(page)) {
			create_empty_buffers(page, blocksize, 0);
			if (unlikely(!page_has_buffers(page)))
				return -ENOMEM;
		}
	} while (++u < nr_pages);
	rl_write_locked = false;
	rl = NULL;
	err = 0;
	vcn = lcn = -1;
	vcn_len = 0;
	lcn_block = -1;
	was_hole = false;
	cpos = pos >> vol->cluster_size_bits;
	end = pos + bytes;
	cend = (end + vol->cluster_size - 1) >> vol->cluster_size_bits;
	/*
	 * Loop over each page and for each page over each buffer.  Use goto to
	 * reduce indentation.
	 */
	u = 0;
do_next_page:
	page = pages[u];
	bh_pos = (s64)page->index << PAGE_CACHE_SHIFT;
	bh = head = page_buffers(page);
	do {
		VCN cdelta;
		s64 bh_end;
		unsigned bh_cofs;

		/* Clear buffer_new on all buffers to reinitialise state. */
		if (buffer_new(bh))
			clear_buffer_new(bh);
		bh_end = bh_pos + blocksize;
		bh_cpos = bh_pos >> vol->cluster_size_bits;
		bh_cofs = bh_pos & vol->cluster_size_mask;
		if (buffer_mapped(bh)) {
			/*
			 * The buffer is already mapped.  If it is uptodate,
			 * ignore it.
			 */
			if (buffer_uptodate(bh))
				continue;
			/*
			 * The buffer is not uptodate.  If the page is uptodate
			 * set the buffer uptodate and otherwise ignore it.
			 */
			if (PageUptodate(page)) {
				set_buffer_uptodate(bh);
				continue;
			}
			/*
			 * Neither the page nor the buffer are uptodate.  If
			 * the buffer is only partially being written to, we
			 * need to read it in before the write, i.e. now.
			 */
			if ((bh_pos < pos && bh_end > pos) ||
					(bh_pos < end && bh_end > end)) {
				/*
				 * If the buffer is fully or partially within
				 * the initialized size, do an actual read.
				 * Otherwise, simply zero the buffer.
				 */
				read_lock_irqsave(&ni->size_lock, flags);
				initialized_size = ni->initialized_size;
				read_unlock_irqrestore(&ni->size_lock, flags);
				if (bh_pos < initialized_size) {
					ntfs_submit_bh_for_read(bh);
					*wait_bh++ = bh;
				} else {
					zero_user(page, bh_offset(bh),
							blocksize);
					set_buffer_uptodate(bh);
				}
			}
			continue;
		}
		/* Unmapped buffer.  Need to map it. */
		bh->b_bdev = vol->sb->s_bdev;
		/*
		 * If the current buffer is in the same clusters as the map
		 * cache, there is no need to check the runlist again.  The
		 * map cache is made up of @vcn, which is the first cached file
		 * cluster, @vcn_len which is the number of cached file
		 * clusters, @lcn is the device cluster corresponding to @vcn,
		 * and @lcn_block is the block number corresponding to @lcn.
		 */
		cdelta = bh_cpos - vcn;
		if (likely(!cdelta || (cdelta > 0 && cdelta < vcn_len))) {
map_buffer_cached:
			BUG_ON(lcn < 0);
			bh->b_blocknr = lcn_block +
					(cdelta << (vol->cluster_size_bits -
					blocksize_bits)) +
					(bh_cofs >> blocksize_bits);
			set_buffer_mapped(bh);
			/*
			 * If the page is uptodate so is the buffer.  If the
			 * buffer is fully outside the write, we ignore it if
			 * it was already allocated and we mark it dirty so it
			 * gets written out if we allocated it.  On the other
			 * hand, if we allocated the buffer but we are not
			 * marking it dirty we set buffer_new so we can do
			 * error recovery.
			 */
			if (PageUptodate(page)) {
				if (!buffer_uptodate(bh))
					set_buffer_uptodate(bh);
				if (unlikely(was_hole)) {
					/* We allocated the buffer. */
					unmap_underlying_metadata(bh->b_bdev,
							bh->b_blocknr);
					if (bh_end <= pos || bh_pos >= end)
						mark_buffer_dirty(bh);
					else
						set_buffer_new(bh);
				}
				continue;
			}
			/* Page is _not_ uptodate. */
			if (likely(!was_hole)) {
				/*
				 * Buffer was already allocated.  If it is not
				 * uptodate and is only partially being written
				 * to, we need to read it in before the write,
				 * i.e. now.
				 */
				if (!buffer_uptodate(bh) && bh_pos < end &&
						bh_end > pos &&
						(bh_pos < pos ||
						bh_end > end)) {
					/*
					 * If the buffer is fully or partially
					 * within the initialized size, do an
					 * actual read.  Otherwise, simply zero
					 * the buffer.
					 */
					read_lock_irqsave(&ni->size_lock,
							flags);
					initialized_size = ni->initialized_size;
					read_unlock_irqrestore(&ni->size_lock,
							flags);
					if (bh_pos < initialized_size) {
						ntfs_submit_bh_for_read(bh);
						*wait_bh++ = bh;
					} else {
						zero_user(page, bh_offset(bh),
								blocksize);
						set_buffer_uptodate(bh);
					}
				}
				continue;
			}
			/* We allocated the buffer. */
			unmap_underlying_metadata(bh->b_bdev, bh->b_blocknr);
			/*
			 * If the buffer is fully outside the write, zero it,
			 * set it uptodate, and mark it dirty so it gets
			 * written out.  If it is partially being written to,
			 * zero region surrounding the write but leave it to
			 * commit write to do anything else.  Finally, if the
			 * buffer is fully being overwritten, do nothing.
			 */
			if (bh_end <= pos || bh_pos >= end) {
				if (!buffer_uptodate(bh)) {
					zero_user(page, bh_offset(bh),
							blocksize);
					set_buffer_uptodate(bh);
				}
				mark_buffer_dirty(bh);
				continue;
			}
			set_buffer_new(bh);
			if (!buffer_uptodate(bh) &&
					(bh_pos < pos || bh_end > end)) {
				u8 *kaddr;
				unsigned pofs;
					
				kaddr = kmap_atomic(page, KM_USER0);
				if (bh_pos < pos) {
					pofs = bh_pos & ~PAGE_CACHE_MASK;
					memset(kaddr + pofs, 0, pos - bh_pos);
				}
				if (bh_end > end) {
					pofs = end & ~PAGE_CACHE_MASK;
					memset(kaddr + pofs, 0, bh_end - end);
				}
				kunmap_atomic(kaddr, KM_USER0);
				flush_dcache_page(page);
			}
			continue;
		}
		/*
		 * Slow path: this is the first buffer in the cluster.  If it
		 * is outside allocated size and is not uptodate, zero it and
		 * set it uptodate.
		 */
		read_lock_irqsave(&ni->size_lock, flags);
		initialized_size = ni->allocated_size;
		read_unlock_irqrestore(&ni->size_lock, flags);
		if (bh_pos > initialized_size) {
			if (PageUptodate(page)) {
				if (!buffer_uptodate(bh))
					set_buffer_uptodate(bh);
			} else if (!buffer_uptodate(bh)) {
				zero_user(page, bh_offset(bh), blocksize);
				set_buffer_uptodate(bh);
			}
			continue;
		}
		is_retry = false;
		if (!rl) {
			down_read(&ni->runlist.lock);
retry_remap:
			rl = ni->runlist.rl;
		}
		if (likely(rl != NULL)) {
			/* Seek to element containing target cluster. */
			while (rl->length && rl[1].vcn <= bh_cpos)
				rl++;
			lcn = ntfs_rl_vcn_to_lcn(rl, bh_cpos);
			if (likely(lcn >= 0)) {
				/*
				 * Successful remap, setup the map cache and
				 * use that to deal with the buffer.
				 */
				was_hole = false;
				vcn = bh_cpos;
				vcn_len = rl[1].vcn - vcn;
				lcn_block = lcn << (vol->cluster_size_bits -
						blocksize_bits);
				cdelta = 0;
				/*
				 * If the number of remaining clusters touched
				 * by the write is smaller or equal to the
				 * number of cached clusters, unlock the
				 * runlist as the map cache will be used from
				 * now on.
				 */
				if (likely(vcn + vcn_len >= cend)) {
					if (rl_write_locked) {
						up_write(&ni->runlist.lock);
						rl_write_locked = false;
					} else
						up_read(&ni->runlist.lock);
					rl = NULL;
				}
				goto map_buffer_cached;
			}
		} else
			lcn = LCN_RL_NOT_MAPPED;
		/*
		 * If it is not a hole and not out of bounds, the runlist is
		 * probably unmapped so try to map it now.
		 */
		if (unlikely(lcn != LCN_HOLE && lcn != LCN_ENOENT)) {
			if (likely(!is_retry && lcn == LCN_RL_NOT_MAPPED)) {
				/* Attempt to map runlist. */
				if (!rl_write_locked) {
					/*
					 * We need the runlist locked for
					 * writing, so if it is locked for
					 * reading relock it now and retry in
					 * case it changed whilst we dropped
					 * the lock.
					 */
					up_read(&ni->runlist.lock);
					down_write(&ni->runlist.lock);
					rl_write_locked = true;
					goto retry_remap;
				}
				err = ntfs_map_runlist_nolock(ni, bh_cpos,
						NULL);
				if (likely(!err)) {
					is_retry = true;
					goto retry_remap;
				}
				/*
				 * If @vcn is out of bounds, pretend @lcn is
				 * LCN_ENOENT.  As long as the buffer is out
				 * of bounds this will work fine.
				 */
				if (err == -ENOENT) {
					lcn = LCN_ENOENT;
					err = 0;
					goto rl_not_mapped_enoent;
				}
			} else
				err = -EIO;
			/* Failed to map the buffer, even after retrying. */
			bh->b_blocknr = -1;
			ntfs_error(vol->sb, "Failed to write to inode 0x%lx, "
					"attribute type 0x%x, vcn 0x%llx, "
					"vcn offset 0x%x, because its "
					"location on disk could not be "
					"determined%s (error code %i).",
					ni->mft_no, ni->type,
					(unsigned long long)bh_cpos,
					(unsigned)bh_pos &
					vol->cluster_size_mask,
					is_retry ? " even after retrying" : "",
					err);
			break;
		}
rl_not_mapped_enoent:
		/*
		 * The buffer is in a hole or out of bounds.  We need to fill
		 * the hole, unless the buffer is in a cluster which is not
		 * touched by the write, in which case we just leave the buffer
		 * unmapped.  This can only happen when the cluster size is
		 * less than the page cache size.
		 */
		if (unlikely(vol->cluster_size < PAGE_CACHE_SIZE)) {
			bh_cend = (bh_end + vol->cluster_size - 1) >>
					vol->cluster_size_bits;
			if ((bh_cend <= cpos || bh_cpos >= cend)) {
				bh->b_blocknr = -1;
				/*
				 * If the buffer is uptodate we skip it.  If it
				 * is not but the page is uptodate, we can set
				 * the buffer uptodate.  If the page is not
				 * uptodate, we can clear the buffer and set it
				 * uptodate.  Whether this is worthwhile is
				 * debatable and this could be removed.
				 */
				if (PageUptodate(page)) {
					if (!buffer_uptodate(bh))
						set_buffer_uptodate(bh);
				} else if (!buffer_uptodate(bh)) {
					zero_user(page, bh_offset(bh),
						blocksize);
					set_buffer_uptodate(bh);
				}
				continue;
			}
		}
		/*
		 * Out of bounds buffer is invalid if it was not really out of
		 * bounds.
		 */
		BUG_ON(lcn != LCN_HOLE);
		/*
		 * We need the runlist locked for writing, so if it is locked
		 * for reading relock it now and retry in case it changed
		 * whilst we dropped the lock.
		 */
		BUG_ON(!rl);
		if (!rl_write_locked) {
			up_read(&ni->runlist.lock);
			down_write(&ni->runlist.lock);
			rl_write_locked = true;
			goto retry_remap;
		}
		/* Find the previous last allocated cluster. */
		BUG_ON(rl->lcn != LCN_HOLE);
		lcn = -1;
		rl2 = rl;
		while (--rl2 >= ni->runlist.rl) {
			if (rl2->lcn >= 0) {
				lcn = rl2->lcn + rl2->length;
				break;
			}
		}
		rl2 = ntfs_cluster_alloc(vol, bh_cpos, 1, lcn, DATA_ZONE,
				false);
		if (IS_ERR(rl2)) {
			err = PTR_ERR(rl2);
			ntfs_debug("Failed to allocate cluster, error code %i.",
					err);
			break;
		}
		lcn = rl2->lcn;
		rl = ntfs_runlists_merge(ni->runlist.rl, rl2);
		if (IS_ERR(rl)) {
			err = PTR_ERR(rl);
			if (err != -ENOMEM)
				err = -EIO;
			if (ntfs_cluster_free_from_rl(vol, rl2)) {
				ntfs_error(vol->sb, "Failed to release "
						"allocated cluster in error "
						"code path.  Run chkdsk to "
						"recover the lost cluster.");
				NVolSetErrors(vol);
			}
			ntfs_free(rl2);
			break;
		}
		ni->runlist.rl = rl;
		status.runlist_merged = 1;
		ntfs_debug("Allocated cluster, lcn 0x%llx.",
				(unsigned long long)lcn);
		/* Map and lock the mft record and get the attribute record. */
		if (!NInoAttr(ni))
			base_ni = ni;
		else
			base_ni = ni->ext.base_ntfs_ino;
		m = map_mft_record(base_ni);
		if (IS_ERR(m)) {
			err = PTR_ERR(m);
			break;
		}
		ctx = ntfs_attr_get_search_ctx(base_ni, m);
		if (unlikely(!ctx)) {
			err = -ENOMEM;
			unmap_mft_record(base_ni);
			break;
		}
		status.mft_attr_mapped = 1;
		err = ntfs_attr_lookup(ni->type, ni->name, ni->name_len,
				CASE_SENSITIVE, bh_cpos, NULL, 0, ctx);
		if (unlikely(err)) {
			if (err == -ENOENT)
				err = -EIO;
			break;
		}
		m = ctx->mrec;
		a = ctx->attr;
		/*
		 * Find the runlist element with which the attribute extent
		 * starts.  Note, we cannot use the _attr_ version because we
		 * have mapped the mft record.  That is ok because we know the
		 * runlist fragment must be mapped already to have ever gotten
		 * here, so we can just use the _rl_ version.
		 */
		vcn = sle64_to_cpu(a->data.non_resident.lowest_vcn);
		rl2 = ntfs_rl_find_vcn_nolock(rl, vcn);
		BUG_ON(!rl2);
		BUG_ON(!rl2->length);
		BUG_ON(rl2->lcn < LCN_HOLE);
		highest_vcn = sle64_to_cpu(a->data.non_resident.highest_vcn);
		/*
		 * If @highest_vcn is zero, calculate the real highest_vcn
		 * (which can really be zero).
		 */
		if (!highest_vcn)
			highest_vcn = (sle64_to_cpu(
					a->data.non_resident.allocated_size) >>
					vol->cluster_size_bits) - 1;
		/*
		 * Determine the size of the mapping pairs array for the new
		 * extent, i.e. the old extent with the hole filled.
		 */
		mp_size = ntfs_get_size_for_mapping_pairs(vol, rl2, vcn,
				highest_vcn);
		if (unlikely(mp_size <= 0)) {
			if (!(err = mp_size))
				err = -EIO;
			ntfs_debug("Failed to get size for mapping pairs "
					"array, error code %i.", err);
			break;
		}
		/*
		 * Resize the attribute record to fit the new mapping pairs
		 * array.
		 */
		attr_rec_len = le32_to_cpu(a->length);
		err = ntfs_attr_record_resize(m, a, mp_size + le16_to_cpu(
				a->data.non_resident.mapping_pairs_offset));
		if (unlikely(err)) {
			BUG_ON(err != -ENOSPC);
			// TODO: Deal with this by using the current attribute
			// and fill it with as much of the mapping pairs
			// array as possible.  Then loop over each attribute
			// extent rewriting the mapping pairs arrays as we go
			// along and if when we reach the end we have not
			// enough space, try to resize the last attribute
			// extent and if even that fails, add a new attribute
			// extent.
			// We could also try to resize at each step in the hope
			// that we will not need to rewrite every single extent.
			// Note, we may need to decompress some extents to fill
			// the runlist as we are walking the extents...
			ntfs_error(vol->sb, "Not enough space in the mft "
					"record for the extended attribute "
					"record.  This case is not "
					"implemented yet.");
			err = -EOPNOTSUPP;
			break ;
		}
		status.mp_rebuilt = 1;
		/*
		 * Generate the mapping pairs array directly into the attribute
		 * record.
		 */
		err = ntfs_mapping_pairs_build(vol, (u8*)a + le16_to_cpu(
				a->data.non_resident.mapping_pairs_offset),
				mp_size, rl2, vcn, highest_vcn, NULL);
		if (unlikely(err)) {
			ntfs_error(vol->sb, "Cannot fill hole in inode 0x%lx, "
					"attribute type 0x%x, because building "
					"the mapping pairs failed with error "
					"code %i.", vi->i_ino,
					(unsigned)le32_to_cpu(ni->type), err);
			err = -EIO;
			break;
		}
		/* Update the highest_vcn but only if it was not set. */
		if (unlikely(!a->data.non_resident.highest_vcn))
			a->data.non_resident.highest_vcn =
					cpu_to_sle64(highest_vcn);
		/*
		 * If the attribute is sparse/compressed, update the compressed
		 * size in the ntfs_inode structure and the attribute record.
		 */
		if (likely(NInoSparse(ni) || NInoCompressed(ni))) {
			/*
			 * If we are not in the first attribute extent, switch
			 * to it, but first ensure the changes will make it to
			 * disk later.
			 */
			if (a->data.non_resident.lowest_vcn) {
				flush_dcache_mft_record_page(ctx->ntfs_ino);
				mark_mft_record_dirty(ctx->ntfs_ino);
				ntfs_attr_reinit_search_ctx(ctx);
				err = ntfs_attr_lookup(ni->type, ni->name,
						ni->name_len, CASE_SENSITIVE,
						0, NULL, 0, ctx);
				if (unlikely(err)) {
					status.attr_switched = 1;
					break;
				}
				/* @m is not used any more so do not set it. */
				a = ctx->attr;
			}
			write_lock_irqsave(&ni->size_lock, flags);
			ni->itype.compressed.size += vol->cluster_size;
			a->data.non_resident.compressed_size =
					cpu_to_sle64(ni->itype.compressed.size);
			write_unlock_irqrestore(&ni->size_lock, flags);
		}
		/* Ensure the changes make it to disk. */
		flush_dcache_mft_record_page(ctx->ntfs_ino);
		mark_mft_record_dirty(ctx->ntfs_ino);
		ntfs_attr_put_search_ctx(ctx);
		unmap_mft_record(base_ni);
		/* Successfully filled the hole. */
		status.runlist_merged = 0;
		status.mft_attr_mapped = 0;
		status.mp_rebuilt = 0;
		/* Setup the map cache and use that to deal with the buffer. */
		was_hole = true;
		vcn = bh_cpos;
		vcn_len = 1;
		lcn_block = lcn << (vol->cluster_size_bits - blocksize_bits);
		cdelta = 0;
		/*
		 * If the number of remaining clusters in the @pages is smaller
		 * or equal to the number of cached clusters, unlock the
		 * runlist as the map cache will be used from now on.
		 */
		if (likely(vcn + vcn_len >= cend)) {
			up_write(&ni->runlist.lock);
			rl_write_locked = false;
			rl = NULL;
		}
		goto map_buffer_cached;
	} while (bh_pos += blocksize, (bh = bh->b_this_page) != head);
	/* If there are no errors, do the next page. */
	if (likely(!err && ++u < nr_pages))
		goto do_next_page;
	/* If there are no errors, release the runlist lock if we took it. */
	if (likely(!err)) {
		if (unlikely(rl_write_locked)) {
			up_write(&ni->runlist.lock);
			rl_write_locked = false;
		} else if (unlikely(rl))
			up_read(&ni->runlist.lock);
		rl = NULL;
	}
	/* If we issued read requests, let them complete. */
	read_lock_irqsave(&ni->size_lock, flags);
	initialized_size = ni->initialized_size;
	read_unlock_irqrestore(&ni->size_lock, flags);
	while (wait_bh > wait) {
		bh = *--wait_bh;
		wait_on_buffer(bh);
		if (likely(buffer_uptodate(bh))) {
			page = bh->b_page;
			bh_pos = ((s64)page->index << PAGE_CACHE_SHIFT) +
					bh_offset(bh);
			/*
			 * If the buffer overflows the initialized size, need
			 * to zero the overflowing region.
			 */
			if (unlikely(bh_pos + blocksize > initialized_size)) {
				int ofs = 0;

				if (likely(bh_pos < initialized_size))
					ofs = initialized_size - bh_pos;
				zero_user_segment(page, bh_offset(bh) + ofs,
						blocksize);
			}
		} else /* if (unlikely(!buffer_uptodate(bh))) */
			err = -EIO;
	}
	if (likely(!err)) {
		/* Clear buffer_new on all buffers. */
		u = 0;
		do {
			bh = head = page_buffers(pages[u]);
			do {
				if (buffer_new(bh))
					clear_buffer_new(bh);
			} while ((bh = bh->b_this_page) != head);
		} while (++u < nr_pages);
		ntfs_debug("Done.");
		return err;
	}
	if (status.attr_switched) {
		/* Get back to the attribute extent we modified. */
		ntfs_attr_reinit_search_ctx(ctx);
		if (ntfs_attr_lookup(ni->type, ni->name, ni->name_len,
				CASE_SENSITIVE, bh_cpos, NULL, 0, ctx)) {
			ntfs_error(vol->sb, "Failed to find required "
					"attribute extent of attribute in "
					"error code path.  Run chkdsk to "
					"recover.");
			write_lock_irqsave(&ni->size_lock, flags);
			ni->itype.compressed.size += vol->cluster_size;
			write_unlock_irqrestore(&ni->size_lock, flags);
			flush_dcache_mft_record_page(ctx->ntfs_ino);
			mark_mft_record_dirty(ctx->ntfs_ino);
			/*
			 * The only thing that is now wrong is the compressed
			 * size of the base attribute extent which chkdsk
			 * should be able to fix.
			 */
			NVolSetErrors(vol);
		} else {
			m = ctx->mrec;
			a = ctx->attr;
			status.attr_switched = 0;
		}
	}
	/*
	 * If the runlist has been modified, need to restore it by punching a
	 * hole into it and we then need to deallocate the on-disk cluster as
	 * well.  Note, we only modify the runlist if we are able to generate a
	 * new mapping pairs array, i.e. only when the mapped attribute extent
	 * is not switched.
	 */
	if (status.runlist_merged && !status.attr_switched) {
		BUG_ON(!rl_write_locked);
		/* Make the file cluster we allocated sparse in the runlist. */
		if (ntfs_rl_punch_nolock(vol, &ni->runlist, bh_cpos, 1)) {
			ntfs_error(vol->sb, "Failed to punch hole into "
					"attribute runlist in error code "
					"path.  Run chkdsk to recover the "
					"lost cluster.");
			NVolSetErrors(vol);
		} else /* if (success) */ {
			status.runlist_merged = 0;
			/*
			 * Deallocate the on-disk cluster we allocated but only
			 * if we succeeded in punching its vcn out of the
			 * runlist.
			 */
			down_write(&vol->lcnbmp_lock);
			if (ntfs_bitmap_clear_bit(vol->lcnbmp_ino, lcn)) {
				ntfs_error(vol->sb, "Failed to release "
						"allocated cluster in error "
						"code path.  Run chkdsk to "
						"recover the lost cluster.");
				NVolSetErrors(vol);
			}
			up_write(&vol->lcnbmp_lock);
		}
	}
	/*
	 * Resize the attribute record to its old size and rebuild the mapping
	 * pairs array.  Note, we only can do this if the runlist has been
	 * restored to its old state which also implies that the mapped
	 * attribute extent is not switched.
	 */
	if (status.mp_rebuilt && !status.runlist_merged) {
		if (ntfs_attr_record_resize(m, a, attr_rec_len)) {
			ntfs_error(vol->sb, "Failed to restore attribute "
					"record in error code path.  Run "
					"chkdsk to recover.");
			NVolSetErrors(vol);
		} else /* if (success) */ {
			if (ntfs_mapping_pairs_build(vol, (u8*)a +
					le16_to_cpu(a->data.non_resident.
					mapping_pairs_offset), attr_rec_len -
					le16_to_cpu(a->data.non_resident.
					mapping_pairs_offset), ni->runlist.rl,
					vcn, highest_vcn, NULL)) {
				ntfs_error(vol->sb, "Failed to restore "
						"mapping pairs array in error "
						"code path.  Run chkdsk to "
						"recover.");
				NVolSetErrors(vol);
			}
			flush_dcache_mft_record_page(ctx->ntfs_ino);
			mark_mft_record_dirty(ctx->ntfs_ino);
		}
	}
	/* Release the mft record and the attribute. */
	if (status.mft_attr_mapped) {
		ntfs_attr_put_search_ctx(ctx);
		unmap_mft_record(base_ni);
	}
	/* Release the runlist lock. */
	if (rl_write_locked)
		up_write(&ni->runlist.lock);
	else if (rl)
		up_read(&ni->runlist.lock);
	/*
	 * Zero out any newly allocated blocks to avoid exposing stale data.
	 * If BH_New is set, we know that the block was newly allocated above
	 * and that it has not been fully zeroed and marked dirty yet.
	 */
	nr_pages = u;
	u = 0;
	end = bh_cpos << vol->cluster_size_bits;
	do {
		page = pages[u];
		bh = head = page_buffers(page);
		do {
			if (u == nr_pages &&
					((s64)page->index << PAGE_CACHE_SHIFT) +
					bh_offset(bh) >= end)
				break;
			if (!buffer_new(bh))
				continue;
			clear_buffer_new(bh);
			if (!buffer_uptodate(bh)) {
				if (PageUptodate(page))
					set_buffer_uptodate(bh);
				else {
					zero_user(page, bh_offset(bh),
							blocksize);
					set_buffer_uptodate(bh);
				}
			}
			mark_buffer_dirty(bh);
		} while ((bh = bh->b_this_page) != head);
	} while (++u <= nr_pages);
	ntfs_error(vol->sb, "Failed.  Returning error code %i.", err);
	return err;
}

/*
 * Copy as much as we can into the pages and return the number of bytes which
 * were successfully copied.  If a fault is encountered then clear the pages
 * out to (ofs + bytes) and return the number of bytes which were copied.
 */
static inline size_t ntfs_copy_from_user(struct page **pages,
		unsigned nr_pages, unsigned ofs, const char __user *buf,
		size_t bytes)
{
	struct page **last_page = pages + nr_pages;
	char *addr;
	size_t total = 0;
	unsigned len;
	int left;

	do {
		len = PAGE_CACHE_SIZE - ofs;
		if (len > bytes)
			len = bytes;
		addr = kmap_atomic(*pages, KM_USER0);
		left = __copy_from_user_inatomic(addr + ofs, buf, len);
		kunmap_atomic(addr, KM_USER0);
		if (unlikely(left)) {
			/* Do it the slow way. */
			addr = kmap(*pages);
			left = __copy_from_user(addr + ofs, buf, len);
			kunmap(*pages);
			if (unlikely(left))
				goto err_out;
		}
		total += len;
		bytes -= len;
		if (!bytes)
			break;
		buf += len;
		ofs = 0;
	} while (++pages < last_page);
out:
	return total;
err_out:
	total += len - left;
	/* Zero the rest of the target like __copy_from_user(). */
	while (++pages < last_page) {
		bytes -= len;
		if (!bytes)
			break;
		len = PAGE_CACHE_SIZE;
		if (len > bytes)
			len = bytes;
		zero_user(*pages, 0, len);
	}
	goto out;
}

static size_t __ntfs_copy_from_user_iovec_inatomic(char *vaddr,
		const struct iovec *iov, size_t iov_ofs, size_t bytes)
{
	size_t total = 0;

	while (1) {
		const char __user *buf = iov->iov_base + iov_ofs;
		unsigned len;
		size_t left;

		len = iov->iov_len - iov_ofs;
		if (len > bytes)
			len = bytes;
		left = __copy_from_user_inatomic(vaddr, buf, len);
		total += len;
		bytes -= len;
		vaddr += len;
		if (unlikely(left)) {
			total -= left;
			break;
		}
		if (!bytes)
			break;
		iov++;
		iov_ofs = 0;
	}
	return total;
}

static inline void ntfs_set_next_iovec(const struct iovec **iovp,
		size_t *iov_ofsp, size_t bytes)
{
	const struct iovec *iov = *iovp;
	size_t iov_ofs = *iov_ofsp;

	while (bytes) {
		unsigned len;

		len = iov->iov_len - iov_ofs;
		if (len > bytes)
			len = bytes;
		bytes -= len;
		iov_ofs += len;
		if (iov->iov_len == iov_ofs) {
			iov++;
			iov_ofs = 0;
		}
	}
	*iovp = iov;
	*iov_ofsp = iov_ofs;
}

/*
 * This has the same side-effects and return value as ntfs_copy_from_user().
 * The difference is that on a fault we need to memset the remainder of the
 * pages (out to offset + bytes), to emulate ntfs_copy_from_user()'s
 * single-segment behaviour.
 *
 * We call the same helper (__ntfs_copy_from_user_iovec_inatomic()) both when
 * atomic and when not atomic.  This is ok because it calls
 * __copy_from_user_inatomic() and it is ok to call this when non-atomic.  In
 * fact, the only difference between __copy_from_user_inatomic() and
 * __copy_from_user() is that the latter calls might_sleep() and the former
 * should not zero the tail of the buffer on error.  And on many architectures
 * __copy_from_user_inatomic() is just defined to __copy_from_user() so it
 * makes no difference at all on those architectures.
 */
static inline size_t ntfs_copy_from_user_iovec(struct page **pages,
		unsigned nr_pages, unsigned ofs, const struct iovec **iov,
		size_t *iov_ofs, size_t bytes)
{
	struct page **last_page = pages + nr_pages;
	char *addr;
	size_t copied, len, total = 0;

	do {
		len = PAGE_CACHE_SIZE - ofs;
		if (len > bytes)
			len = bytes;
		addr = kmap_atomic(*pages, KM_USER0);
		copied = __ntfs_copy_from_user_iovec_inatomic(addr + ofs,
				*iov, *iov_ofs, len);
		kunmap_atomic(addr, KM_USER0);
		if (unlikely(copied != len)) {
			/* Do it the slow way. */
			addr = kmap(*pages);
			copied = __ntfs_copy_from_user_iovec_inatomic(addr +
					ofs, *iov, *iov_ofs, len);
			if (unlikely(copied != len))
				goto err_out;
			kunmap(*pages);
		}
		total += len;
		ntfs_set_next_iovec(iov, iov_ofs, len);
		bytes -= len;
		if (!bytes)
			break;
		ofs = 0;
	} while (++pages < last_page);
out:
	return total;
err_out:
	BUG_ON(copied > len);
	/* Zero the rest of the target like __copy_from_user(). */
	memset(addr + ofs + copied, 0, len - copied);
	kunmap(*pages);
	total += copied;
	ntfs_set_next_iovec(iov, iov_ofs, copied);
	while (++pages < last_page) {
		bytes -= len;
		if (!bytes)
			break;
		len = PAGE_CACHE_SIZE;
		if (len > bytes)
			len = bytes;
		zero_user(*pages, 0, len);
	}
	goto out;
}

static inline void ntfs_flush_dcache_pages(struct page **pages,
		unsigned nr_pages)
{
	BUG_ON(!nr_pages);
	/*
	 * Warning: Do not do the decrement at the same time as the call to
	 * flush_dcache_page() because it is a NULL macro on i386 and hence the
	 * decrement never happens so the loop never terminates.
	 */
	do {
		--nr_pages;
		flush_dcache_page(pages[nr_pages]);
	} while (nr_pages > 0);
}

/**
 * ntfs_commit_pages_after_non_resident_write - commit the received data
 * @pages:	array of destination pages
 * @nr_pages:	number of pages in @pages
 * @pos:	byte position in file at which the write begins
 * @bytes:	number of bytes to be written
 *
 * See description of ntfs_commit_pages_after_write(), below.
 */
static inline int ntfs_commit_pages_after_non_resident_write(
		struct page **pages, const unsigned nr_pages,
		s64 pos, size_t bytes)
{
	s64 end, initialized_size;
	struct inode *vi;
	ntfs_inode *ni, *base_ni;
	struct buffer_head *bh, *head;
	ntfs_attr_search_ctx *ctx;
	MFT_RECORD *m;
	ATTR_RECORD *a;
	unsigned long flags;
	unsigned blocksize, u;
	int err;

	vi = pages[0]->mapping->host;
	ni = NTFS_I(vi);
	blocksize = vi->i_sb->s_blocksize;
	end = pos + bytes;
	u = 0;
	do {
		s64 bh_pos;
		struct page *page;
		bool partial;

		page = pages[u];
		bh_pos = (s64)page->index << PAGE_CACHE_SHIFT;
		bh = head = page_buffers(page);
		partial = false;
		do {
			s64 bh_end;

			bh_end = bh_pos + blocksize;
			if (bh_end <= pos || bh_pos >= end) {
				if (!buffer_uptodate(bh))
					partial = true;
			} else {
				set_buffer_uptodate(bh);
				mark_buffer_dirty(bh);
			}
		} while (bh_pos += blocksize, (bh = bh->b_this_page) != head);
		/*
		 * If all buffers are now uptodate but the page is not, set the
		 * page uptodate.
		 */
		if (!partial && !PageUptodate(page))
			SetPageUptodate(page);
	} while (++u < nr_pages);
	/*
	 * Finally, if we do not need to update initialized_size or i_size we
	 * are finished.
	 */
	read_lock_irqsave(&ni->size_lock, flags);
	initialized_size = ni->initialized_size;
	read_unlock_irqrestore(&ni->size_lock, flags);
	if (end <= initialized_size) {
		ntfs_debug("Done.");
		return 0;
	}
	/*
	 * Update initialized_size/i_size as appropriate, both in the inode and
	 * the mft record.
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
	BUG_ON(!NInoNonResident(ni));
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
	BUG_ON(!a->non_resident);
	write_lock_irqsave(&ni->size_lock, flags);
	BUG_ON(end > ni->allocated_size);
	ni->initialized_size = end;
	a->data.non_resident.initialized_size = cpu_to_sle64(end);
	if (end > i_size_read(vi)) {
		i_size_write(vi, end);
		a->data.non_resident.data_size =
				a->data.non_resident.initialized_size;
	}
	write_unlock_irqrestore(&ni->size_lock, flags);
	/* Mark the mft record dirty, so it gets written back. */
	flush_dcache_mft_record_page(ctx->ntfs_ino);
	mark_mft_record_dirty(ctx->ntfs_ino);
	ntfs_attr_put_search_ctx(ctx);
	unmap_mft_record(base_ni);
	ntfs_debug("Done.");
	return 0;
err_out:
	if (ctx)
		ntfs_attr_put_search_ctx(ctx);
	if (m)
		unmap_mft_record(base_ni);
	ntfs_error(vi->i_sb, "Failed to update initialized_size/i_size (error "
			"code %i).", err);
	if (err != -ENOMEM)
		NVolSetErrors(ni->vol);
	return err;
}

/**
 * ntfs_commit_pages_after_write - commit the received data
 * @pages:	array of destination pages
 * @nr_pages:	number of pages in @pages
 * @pos:	byte position in file at which the write begins
 * @bytes:	number of bytes to be written
 *
 * This is called from ntfs_file_buffered_write() with i_mutex held on the inode
 * (@pages[0]->mapping->host).  There are @nr_pages pages in @pages which are
 * locked but not kmap()ped.  The source data has already been copied into the
 * @page.  ntfs_prepare_pages_for_non_resident_write() has been called before
 * the data was copied (for non-resident attributes only) and it returned
 * success.
 *
 * Need to set uptodate and mark dirty all buffers within the boundary of the
 * write.  If all buffers in a page are uptodate we set the page uptodate, too.
 *
 * Setting the buffers dirty ensures that they get written out later when
 * ntfs_writepage() is invoked by the VM.
 *
 * Finally, we need to update i_size and initialized_size as appropriate both
 * in the inode and the mft record.
 *
 * This is modelled after fs/buffer.c::generic_commit_write(), which marks
 * buffers uptodate and dirty, sets the page uptodate if all buffers in the
 * page are uptodate, and updates i_size if the end of io is beyond i_size.  In
 * that case, it also marks the inode dirty.
 *
 * If things have gone as outlined in
 * ntfs_prepare_pages_for_non_resident_write(), we do not need to do any page
 * content modifications here for non-resident attributes.  For resident
 * attributes we need to do the uptodate bringing here which we combine with
 * the copying into the mft record which means we save one atomic kmap.
 *
 * Return 0 on success or -errno on error.
 */
static int ntfs_commit_pages_after_write(struct page **pages,
		const unsigned nr_pages, s64 pos, size_t bytes)
{
	s64 end, initialized_size;
	loff_t i_size;
	struct inode *vi;
	ntfs_inode *ni, *base_ni;
	struct page *page;
	ntfs_attr_search_ctx *ctx;
	MFT_RECORD *m;
	ATTR_RECORD *a;
	char *kattr, *kaddr;
	unsigned long flags;
	u32 attr_len;
	int err;

	BUG_ON(!nr_pages);
	BUG_ON(!pages);
	page = pages[0];
	BUG_ON(!page);
	vi = page->mapping->host;
	ni = NTFS_I(vi);
	ntfs_debug("Entering for inode 0x%lx, attribute type 0x%x, start page "
			"index 0x%lx, nr_pages 0x%x, pos 0x%llx, bytes 0x%zx.",
			vi->i_ino, ni->type, page->index, nr_pages,
			(long long)pos, bytes);
	if (NInoNonResident(ni))
		return ntfs_commit_pages_after_non_resident_write(pages,
				nr_pages, pos, bytes);
	BUG_ON(nr_pages > 1);
	/*
	 * Attribute is resident, implying it is not compressed, encrypted, or
	 * sparse.
	 */
	if (!NInoAttr(ni))
		base_ni = ni;
	else
		base_ni = ni->ext.base_ntfs_ino;
	BUG_ON(NInoNonResident(ni));
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
	BUG_ON(a->non_resident);
	/* The total length of the attribute value. */
	attr_len = le32_to_cpu(a->data.resident.value_length);
	i_size = i_size_read(vi);
	BUG_ON(attr_len != i_size);
	BUG_ON(pos > attr_len);
	end = pos + bytes;
	BUG_ON(end > le32_to_cpu(a->length) -
			le16_to_cpu(a->data.resident.value_offset));
	kattr = (u8*)a + le16_to_cpu(a->data.resident.value_offset);
	kaddr = kmap_atomic(page, KM_USER0);
	/* Copy the received data from the page to the mft record. */
	memcpy(kattr + pos, kaddr + pos, bytes);
	/* Update the attribute length if necessary. */
	if (end > attr_len) {
		attr_len = end;
		a->data.resident.value_length = cpu_to_le32(attr_len);
	}
	/*
	 * If the page is not uptodate, bring the out of bounds area(s)
	 * uptodate by copying data from the mft record to the page.
	 */
	if (!PageUptodate(page)) {
		if (pos > 0)
			memcpy(kaddr, kattr, pos);
		if (end < attr_len)
			memcpy(kaddr + end, kattr + end, attr_len - end);
		/* Zero the region outside the end of the attribute value. */
		memset(kaddr + attr_len, 0, PAGE_CACHE_SIZE - attr_len);
		flush_dcache_page(page);
		SetPageUptodate(page);
	}
	kunmap_atomic(kaddr, KM_USER0);
	/* Update initialized_size/i_size if necessary. */
	read_lock_irqsave(&ni->size_lock, flags);
	initialized_size = ni->initialized_size;
	BUG_ON(end > ni->allocated_size);
	read_unlock_irqrestore(&ni->size_lock, flags);
	BUG_ON(initialized_size != i_size);
	if (end > initialized_size) {
		write_lock_irqsave(&ni->size_lock, flags);
		ni->initialized_size = end;
		i_size_write(vi, end);
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
	}
	if (ctx)
		ntfs_attr_put_search_ctx(ctx);
	if (m)
		unmap_mft_record(base_ni);
	return err;
}

/**
 * ntfs_file_buffered_write -
 *
 * Locking: The vfs is holding ->i_mutex on the inode.
 */
static ssize_t ntfs_file_buffered_write(struct kiocb *iocb,
		const struct iovec *iov, unsigned long nr_segs,
		loff_t pos, loff_t *ppos, size_t count)
{
	struct file *file = iocb->ki_filp;
	struct address_space *mapping = file->f_mapping;
	struct inode *vi = mapping->host;
	ntfs_inode *ni = NTFS_I(vi);
	ntfs_volume *vol = ni->vol;
	struct page *pages[NTFS_MAX_PAGES_PER_CLUSTER];
	struct page *cached_page = NULL;
	char __user *buf = NULL;
	s64 end, ll;
	VCN last_vcn;
	LCN lcn;
	unsigned long flags;
	size_t bytes, iov_ofs = 0;	/* Offset in the current iovec. */
	ssize_t status, written;
	unsigned nr_pages;
	int err;

	ntfs_debug("Entering for i_ino 0x%lx, attribute type 0x%x, "
			"pos 0x%llx, count 0x%lx.",
			vi->i_ino, (unsigned)le32_to_cpu(ni->type),
			(unsigned long long)pos, (unsigned long)count);
	if (unlikely(!count))
		return 0;
	BUG_ON(NInoMstProtected(ni));
	/*
	 * If the attribute is not an index root and it is encrypted or
	 * compressed, we cannot write to it yet.  Note we need to check for
	 * AT_INDEX_ALLOCATION since this is the type of both directory and
	 * index inodes.
	 */
	if (ni->type != AT_INDEX_ALLOCATION) {
		/* If file is encrypted, deny access, just like NT4. */
		if (NInoEncrypted(ni)) {
			/*
			 * Reminder for later: Encrypted files are _always_
			 * non-resident so that the content can always be
			 * encrypted.
			 */
			ntfs_debug("Denying write access to encrypted file.");
			return -EACCES;
		}
		if (NInoCompressed(ni)) {
			/* Only unnamed $DATA attribute can be compressed. */
			BUG_ON(ni->type != AT_DATA);
			BUG_ON(ni->name_len);
			/*
			 * Reminder for later: If resident, the data is not
			 * actually compressed.  Only on the switch to non-
			 * resident does compression kick in.  This is in
			 * contrast to encrypted files (see above).
			 */
			ntfs_error(vi->i_sb, "Writing to compressed files is "
					"not implemented yet.  Sorry.");
			return -EOPNOTSUPP;
		}
	}
	/*
	 * If a previous ntfs_truncate() failed, repeat it and abort if it
	 * fails again.
	 */
	if (unlikely(NInoTruncateFailed(ni))) {
		inode_dio_wait(vi);
		err = ntfs_truncate(vi);
		if (err || NInoTruncateFailed(ni)) {
			if (!err)
				err = -EIO;
			ntfs_error(vol->sb, "Cannot perform write to inode "
					"0x%lx, attribute type 0x%x, because "
					"ntfs_truncate() failed (error code "
					"%i).", vi->i_ino,
					(unsigned)le32_to_cpu(ni->type), err);
			return err;
		}
	}
	/* The first byte after the write. */
	end = pos + count;
	/*
	 * If the write goes beyond the allocated size, extend the allocation
	 * to cover the whole of the write, rounded up to the nearest cluster.
	 */
	read_lock_irqsave(&ni->size_lock, flags);
	ll = ni->allocated_size;
	read_unlock_irqrestore(&ni->size_lock, flags);
	if (end > ll) {
		/* Extend the allocation without changing the data size. */
		ll = ntfs_attr_extend_allocation(ni, end, -1, pos);
		if (likely(ll >= 0)) {
			BUG_ON(pos >= ll);
			/* If the extension was partial truncate the write. */
			if (end > ll) {
				ntfs_debug("Truncating write to inode 0x%lx, "
						"attribute type 0x%x, because "
						"the allocation was only "
						"partially extended.",
						vi->i_ino, (unsigned)
						le32_to_cpu(ni->type));
				end = ll;
				count = ll - pos;
			}
		} else {
			err = ll;
			read_lock_irqsave(&ni->size_lock, flags);
			ll = ni->allocated_size;
			read_unlock_irqrestore(&ni->size_lock, flags);
			/* Perform a partial write if possible or fail. */
			if (pos < ll) {
				ntfs_debug("Truncating write to inode 0x%lx, "
						"attribute type 0x%x, because "
						"extending the allocation "
						"failed (error code %i).",
						vi->i_ino, (unsigned)
						le32_to_cpu(ni->type), err);
				end = ll;
				count = ll - pos;
			} else {
				ntfs_error(vol->sb, "Cannot perform write to "
						"inode 0x%lx, attribute type "
						"0x%x, because extending the "
						"allocation failed (error "
						"code %i).", vi->i_ino,
						(unsigned)
						le32_to_cpu(ni->type), err);
				return err;
			}
		}
	}
	written = 0;
	/*
	 * If the write starts beyond the initialized size, extend it up to the
	 * beginning of the write and initialize all non-sparse space between
	 * the old initialized size and the new one.  This automatically also
	 * increments the vfs inode->i_size to keep it above or equal to the
	 * initialized_size.
	 */
	read_lock_irqsave(&ni->size_lock, flags);
	ll = ni->initialized_size;
	read_unlock_irqrestore(&ni->size_lock, flags);
	if (pos > ll) {
		err = ntfs_attr_extend_initialized(ni, pos);
		if (err < 0) {
			ntfs_error(vol->sb, "Cannot perform write to inode "
					"0x%lx, attribute type 0x%x, because "
					"extending the initialized size "
					"failed (error code %i).", vi->i_ino,
					(unsigned)le32_to_cpu(ni->type), err);
			status = err;
			goto err_out;
		}
	}
	/*
	 * Determine the number of pages per cluster for non-resident
	 * attributes.
	 */
	nr_pages = 1;
	if (vol->cluster_size > PAGE_CACHE_SIZE && NInoNonResident(ni))
		nr_pages = vol->cluster_size >> PAGE_CACHE_SHIFT;
	/* Finally, perform the actual write. */
	last_vcn = -1;
	if (likely(nr_segs == 1))
		buf = iov->iov_base;
	do {
		VCN vcn;
		pgoff_t idx, start_idx;
		unsigned ofs, do_pages, u;
		size_t copied;

		start_idx = idx = pos >> PAGE_CACHE_SHIFT;
		ofs = pos & ~PAGE_CACHE_MASK;
		bytes = PAGE_CACHE_SIZE - ofs;
		do_pages = 1;
		if (nr_pages > 1) {
			vcn = pos >> vol->cluster_size_bits;
			if (vcn != last_vcn) {
				last_vcn = vcn;
				/*
				 * Get the lcn of the vcn the write is in.  If
				 * it is a hole, need to lock down all pages in
				 * the cluster.
				 */
				down_read(&ni->runlist.lock);
				lcn = ntfs_attr_vcn_to_lcn_nolock(ni, pos >>
						vol->cluster_size_bits, false);
				up_read(&ni->runlist.lock);
				if (unlikely(lcn < LCN_HOLE)) {
					status = -EIO;
					if (lcn == LCN_ENOMEM)
						status = -ENOMEM;
					else
						ntfs_error(vol->sb, "Cannot "
							"perform write to "
							"inode 0x%lx, "
							"attribute type 0x%x, "
							"because the attribute "
							"is corrupt.",
							vi->i_ino, (unsigned)
							le32_to_cpu(ni->type));
					break;
				}
				if (lcn == LCN_HOLE) {
					start_idx = (pos & ~(s64)
							vol->cluster_size_mask)
							>> PAGE_CACHE_SHIFT;
					bytes = vol->cluster_size - (pos &
							vol->cluster_size_mask);
					do_pages = nr_pages;
				}
			}
		}
		if (bytes > count)
			bytes = count;
		/*
		 * Bring in the user page(s) that we will copy from _first_.
		 * Otherwise there is a nasty deadlock on copying from the same
		 * page(s) as we are writing to, without it/them being marked
		 * up-to-date.  Note, at present there is nothing to stop the
		 * pages being swapped out between us bringing them into memory
		 * and doing the actual copying.
		 */
		if (likely(nr_segs == 1))
			ntfs_fault_in_pages_readable(buf, bytes);
		else
			ntfs_fault_in_pages_readable_iovec(iov, iov_ofs, bytes);
		/* Get and lock @do_pages starting at index @start_idx. */
		status = __ntfs_grab_cache_pages(mapping, start_idx, do_pages,
				pages, &cached_page);
		if (unlikely(status))
			break;
		/*
		 * For non-resident attributes, we need to fill any holes with
		 * actual clusters and ensure all bufferes are mapped.  We also
		 * need to bring uptodate any buffers that are only partially
		 * being written to.
		 */
		if (NInoNonResident(ni)) {
			status = ntfs_prepare_pages_for_non_resident_write(
					pages, do_pages, pos, bytes);
			if (unlikely(status)) {
				loff_t i_size;

				do {
					unlock_page(pages[--do_pages]);
					page_cache_release(pages[do_pages]);
				} while (do_pages);
				/*
				 * The write preparation may have instantiated
				 * allocated space outside i_size.  Trim this
				 * off again.  We can ignore any errors in this
				 * case as we will just be waisting a bit of
				 * allocated space, which is not a disaster.
				 */
				i_size = i_size_read(vi);
				if (pos + bytes > i_size)
					vmtruncate(vi, i_size);
				break;
			}
		}
		u = (pos >> PAGE_CACHE_SHIFT) - pages[0]->index;
		if (likely(nr_segs == 1)) {
			copied = ntfs_copy_from_user(pages + u, do_pages - u,
					ofs, buf, bytes);
			buf += copied;
		} else
			copied = ntfs_copy_from_user_iovec(pages + u,
					do_pages - u, ofs, &iov, &iov_ofs,
					bytes);
		ntfs_flush_dcache_pages(pages + u, do_pages - u);
		status = ntfs_commit_pages_after_write(pages, do_pages, pos,
				bytes);
		if (likely(!status)) {
			written += copied;
			count -= copied;
			pos += copied;
			if (unlikely(copied != bytes))
				status = -EFAULT;
		}
		do {
			unlock_page(pages[--do_pages]);
			mark_page_accessed(pages[do_pages]);
			page_cache_release(pages[do_pages]);
		} while (do_pages);
		if (unlikely(status))
			break;
		balance_dirty_pages_ratelimited(mapping);
		cond_resched();
	} while (count);
err_out:
	*ppos = pos;
	if (cached_page)
		page_cache_release(cached_page);
	ntfs_debug("Done.  Returning %s (written 0x%lx, status %li).",
			written ? "written" : "status", (unsigned long)written,
			(long)status);
	return written ? written : status;
}

/**
 * ntfs_file_aio_write_nolock -
 */
static ssize_t ntfs_file_aio_write_nolock(struct kiocb *iocb,
		const struct iovec *iov, unsigned long nr_segs, loff_t *ppos)
{
	struct file *file = iocb->ki_filp;
	struct address_space *mapping = file->f_mapping;
	struct inode *inode = mapping->host;
	loff_t pos;
	size_t count;		/* after file limit checks */
	ssize_t written, err;

	count = 0;
	err = generic_segment_checks(iov, &nr_segs, &count, VERIFY_READ);
	if (err)
		return err;
	pos = *ppos;
	vfs_check_frozen(inode->i_sb, SB_FREEZE_WRITE);
	/* We can write back this queue in page reclaim. */
	current->backing_dev_info = mapping->backing_dev_info;
	written = 0;
	err = generic_write_checks(file, &pos, &count, S_ISBLK(inode->i_mode));
	if (err)
		goto out;
	if (!count)
		goto out;
	err = file_remove_suid(file);
	if (err)
		goto out;
	file_update_time(file);
	written = ntfs_file_buffered_write(iocb, iov, nr_segs, pos, ppos,
			count);
out:
	current->backing_dev_info = NULL;
	return written ? written : err;
}

/**
 * ntfs_file_aio_write -
 */
static ssize_t ntfs_file_aio_write(struct kiocb *iocb, const struct iovec *iov,
		unsigned long nr_segs, loff_t pos)
{
	struct file *file = iocb->ki_filp;
	struct address_space *mapping = file->f_mapping;
	struct inode *inode = mapping->host;
	ssize_t ret;

	BUG_ON(iocb->ki_pos != pos);

	mutex_lock(&inode->i_mutex);
	ret = ntfs_file_aio_write_nolock(iocb, iov, nr_segs, &iocb->ki_pos);
	mutex_unlock(&inode->i_mutex);
	if (ret > 0) {
		int err = generic_write_sync(file, pos, ret);
		if (err < 0)
			ret = err;
	}
	return ret;
}

/**
 * ntfs_file_fsync - sync a file to disk
 * @filp:	file to be synced
 * @datasync:	if non-zero only flush user data and not metadata
 *
 * Data integrity sync of a file to disk.  Used for fsync, fdatasync, and msync
 * system calls.  This function is inspired by fs/buffer.c::file_fsync().
 *
 * If @datasync is false, write the mft record and all associated extent mft
 * records as well as the $DATA attribute and then sync the block device.
 *
 * If @datasync is true and the attribute is non-resident, we skip the writing
 * of the mft record and all associated extent mft records (this might still
 * happen due to the write_inode_now() call).
 *
 * Also, if @datasync is true, we do not wait on the inode to be written out
 * but we always wait on the page cache pages to be written out.
 *
 * Locking: Caller must hold i_mutex on the inode.
 *
 * TODO: We should probably also write all attribute/index inodes associated
 * with this inode but since we have no simple way of getting to them we ignore
 * this problem for now.
 */
static int ntfs_file_fsync(struct file *filp, loff_t start, loff_t end,
			   int datasync)
{
	struct inode *vi = filp->f_mapping->host;
	int err, ret = 0;

	ntfs_debug("Entering for inode 0x%lx.", vi->i_ino);

	err = filemap_write_and_wait_range(vi->i_mapping, start, end);
	if (err)
		return err;
	mutex_lock(&vi->i_mutex);

	BUG_ON(S_ISDIR(vi->i_mode));
	if (!datasync || !NInoNonResident(NTFS_I(vi)))
		ret = __ntfs_write_inode(vi, 1);
	write_inode_now(vi, !datasync);
	/*
	 * NOTE: If we were to use mapping->private_list (see ext2 and
	 * fs/buffer.c) for dirty blocks then we could optimize the below to be
	 * sync_mapping_buffers(vi->i_mapping).
	 */
	err = sync_blockdev(vi->i_sb->s_bdev);
	if (unlikely(err && !ret))
		ret = err;
	if (likely(!ret))
		ntfs_debug("Done.");
	else
		ntfs_warning(vi->i_sb, "Failed to f%ssync inode 0x%lx.  Error "
				"%u.", datasync ? "data" : "", vi->i_ino, -ret);
	mutex_unlock(&vi->i_mutex);
	return ret;
}

#endif /* NTFS_RW */

const struct file_operations ntfs_file_ops = {
	.llseek		= generic_file_llseek,	 /* Seek inside file. */
	.read		= do_sync_read,		 /* Read from file. */
	.aio_read	= generic_file_aio_read, /* Async read from file. */
#ifdef NTFS_RW
	.write		= do_sync_write,	 /* Write to file. */
	.aio_write	= ntfs_file_aio_write,	 /* Async write to file. */
	/*.release	= ,*/			 /* Last file is closed.  See
						    fs/ext2/file.c::
						    ext2_release_file() for
						    how to use this to discard
						    preallocated space for
						    write opened files. */
	.fsync		= ntfs_file_fsync,	 /* Sync a file to disk. */
	/*.aio_fsync	= ,*/			 /* Sync all outstanding async
						    i/o operations on a
						    kiocb. */
#endif /* NTFS_RW */
	/*.ioctl	= ,*/			 /* Perform function on the
						    mounted filesystem. */
	.mmap		= generic_file_mmap,	 /* Mmap file. */
	.open		= ntfs_file_open,	 /* Open file. */
	.splice_read	= generic_file_splice_read /* Zero-copy data send with
						    the data source being on
						    the ntfs partition.  We do
						    not need to care about the
						    data destination. */
	/*.sendpage	= ,*/			 /* Zero-copy data send with
						    the data destination being
						    on the ntfs partition.  We
						    do not need to care about
						    the data source. */
};

const struct inode_operations ntfs_file_inode_ops = {
#ifdef NTFS_RW
	.truncate	= ntfs_truncate_vfs,
	.setattr	= ntfs_setattr,
#endif /* NTFS_RW */
};

const struct file_operations ntfs_empty_file_ops = {};

const struct inode_operations ntfs_empty_inode_ops = {};
