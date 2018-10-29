/* -*- mode: c; c-basic-offset: 8; -*-
 * vim: noexpandtab sw=8 ts=8 sts=0:
 *
 * Copyright (C) 2002, 2004 Oracle.  All rights reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this program; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 021110-1307, USA.
 */

#include <linux/fs.h>
#include <linux/slab.h>
#include <linux/highmem.h>
#include <linux/pagemap.h>
#include <asm/byteorder.h>
#include <linux/swap.h>
#include <linux/pipe_fs_i.h>
#include <linux/mpage.h>
#include <linux/quotaops.h>
#include <linux/blkdev.h>
#include <linux/uio.h>

#include <cluster/masklog.h>

#include "ocfs2.h"

#include "alloc.h"
#include "aops.h"
#include "dlmglue.h"
#include "extent_map.h"
#include "file.h"
#include "inode.h"
#include "journal.h"
#include "suballoc.h"
#include "super.h"
#include "symlink.h"
#include "refcounttree.h"
#include "ocfs2_trace.h"

#include "buffer_head_io.h"
#include "dir.h"
#include "namei.h"
#include "sysfile.h"

static int ocfs2_symlink_get_block(struct inode *inode, sector_t iblock,
				   struct buffer_head *bh_result, int create)
{
	int err = -EIO;
	int status;
	struct ocfs2_dinode *fe = NULL;
	struct buffer_head *bh = NULL;
	struct buffer_head *buffer_cache_bh = NULL;
	struct ocfs2_super *osb = OCFS2_SB(inode->i_sb);
	void *kaddr;

	trace_ocfs2_symlink_get_block(
			(unsigned long long)OCFS2_I(inode)->ip_blkno,
			(unsigned long long)iblock, bh_result, create);

	BUG_ON(ocfs2_inode_is_fast_symlink(inode));

	if ((iblock << inode->i_sb->s_blocksize_bits) > PATH_MAX + 1) {
		mlog(ML_ERROR, "block offset > PATH_MAX: %llu",
		     (unsigned long long)iblock);
		goto bail;
	}

	status = ocfs2_read_inode_block(inode, &bh);
	if (status < 0) {
		mlog_errno(status);
		goto bail;
	}
	fe = (struct ocfs2_dinode *) bh->b_data;

	if ((u64)iblock >= ocfs2_clusters_to_blocks(inode->i_sb,
						    le32_to_cpu(fe->i_clusters))) {
		err = -ENOMEM;
		mlog(ML_ERROR, "block offset is outside the allocated size: "
		     "%llu\n", (unsigned long long)iblock);
		goto bail;
	}

	/* We don't use the page cache to create symlink data, so if
	 * need be, copy it over from the buffer cache. */
	if (!buffer_uptodate(bh_result) && ocfs2_inode_is_new(inode)) {
		u64 blkno = le64_to_cpu(fe->id2.i_list.l_recs[0].e_blkno) +
			    iblock;
		buffer_cache_bh = sb_getblk(osb->sb, blkno);
		if (!buffer_cache_bh) {
			err = -ENOMEM;
			mlog(ML_ERROR, "couldn't getblock for symlink!\n");
			goto bail;
		}

		/* we haven't locked out transactions, so a commit
		 * could've happened. Since we've got a reference on
		 * the bh, even if it commits while we're doing the
		 * copy, the data is still good. */
		if (buffer_jbd(buffer_cache_bh)
		    && ocfs2_inode_is_new(inode)) {
			kaddr = kmap_atomic(bh_result->b_page);
			if (!kaddr) {
				mlog(ML_ERROR, "couldn't kmap!\n");
				goto bail;
			}
			memcpy(kaddr + (bh_result->b_size * iblock),
			       buffer_cache_bh->b_data,
			       bh_result->b_size);
			kunmap_atomic(kaddr);
			set_buffer_uptodate(bh_result);
		}
		brelse(buffer_cache_bh);
	}

	map_bh(bh_result, inode->i_sb,
	       le64_to_cpu(fe->id2.i_list.l_recs[0].e_blkno) + iblock);

	err = 0;

bail:
	brelse(bh);

	return err;
}

static int ocfs2_lock_get_block(struct inode *inode, sector_t iblock,
		    struct buffer_head *bh_result, int create)
{
	int ret = 0;
	struct ocfs2_inode_info *oi = OCFS2_I(inode);

	down_read(&oi->ip_alloc_sem);
	ret = ocfs2_get_block(inode, iblock, bh_result, create);
	up_read(&oi->ip_alloc_sem);

	return ret;
}

int ocfs2_get_block(struct inode *inode, sector_t iblock,
		    struct buffer_head *bh_result, int create)
{
	int err = 0;
	unsigned int ext_flags;
	u64 max_blocks = bh_result->b_size >> inode->i_blkbits;
	u64 p_blkno, count, past_eof;
	struct ocfs2_super *osb = OCFS2_SB(inode->i_sb);

	trace_ocfs2_get_block((unsigned long long)OCFS2_I(inode)->ip_blkno,
			      (unsigned long long)iblock, bh_result, create);

	if (OCFS2_I(inode)->ip_flags & OCFS2_INODE_SYSTEM_FILE)
		mlog(ML_NOTICE, "get_block on system inode 0x%p (%lu)\n",
		     inode, inode->i_ino);

	if (S_ISLNK(inode->i_mode)) {
		/* this always does I/O for some reason. */
		err = ocfs2_symlink_get_block(inode, iblock, bh_result, create);
		goto bail;
	}

	err = ocfs2_extent_map_get_blocks(inode, iblock, &p_blkno, &count,
					  &ext_flags);
	if (err) {
		mlog(ML_ERROR, "Error %d from get_blocks(0x%p, %llu, 1, "
		     "%llu, NULL)\n", err, inode, (unsigned long long)iblock,
		     (unsigned long long)p_blkno);
		goto bail;
	}

	if (max_blocks < count)
		count = max_blocks;

	/*
	 * ocfs2 never allocates in this function - the only time we
	 * need to use BH_New is when we're extending i_size on a file
	 * system which doesn't support holes, in which case BH_New
	 * allows __block_write_begin() to zero.
	 *
	 * If we see this on a sparse file system, then a truncate has
	 * raced us and removed the cluster. In this case, we clear
	 * the buffers dirty and uptodate bits and let the buffer code
	 * ignore it as a hole.
	 */
	if (create && p_blkno == 0 && ocfs2_sparse_alloc(osb)) {
		clear_buffer_dirty(bh_result);
		clear_buffer_uptodate(bh_result);
		goto bail;
	}

	/* Treat the unwritten extent as a hole for zeroing purposes. */
	if (p_blkno && !(ext_flags & OCFS2_EXT_UNWRITTEN))
		map_bh(bh_result, inode->i_sb, p_blkno);

	bh_result->b_size = count << inode->i_blkbits;

	if (!ocfs2_sparse_alloc(osb)) {
		if (p_blkno == 0) {
			err = -EIO;
			mlog(ML_ERROR,
			     "iblock = %llu p_blkno = %llu blkno=(%llu)\n",
			     (unsigned long long)iblock,
			     (unsigned long long)p_blkno,
			     (unsigned long long)OCFS2_I(inode)->ip_blkno);
			mlog(ML_ERROR, "Size %llu, clusters %u\n", (unsigned long long)i_size_read(inode), OCFS2_I(inode)->ip_clusters);
			dump_stack();
			goto bail;
		}
	}

	past_eof = ocfs2_blocks_for_bytes(inode->i_sb, i_size_read(inode));

	trace_ocfs2_get_block_end((unsigned long long)OCFS2_I(inode)->ip_blkno,
				  (unsigned long long)past_eof);
	if (create && (iblock >= past_eof))
		set_buffer_new(bh_result);

bail:
	if (err < 0)
		err = -EIO;

	return err;
}

int ocfs2_read_inline_data(struct inode *inode, struct page *page,
			   struct buffer_head *di_bh)
{
	void *kaddr;
	loff_t size;
	struct ocfs2_dinode *di = (struct ocfs2_dinode *)di_bh->b_data;

	if (!(le16_to_cpu(di->i_dyn_features) & OCFS2_INLINE_DATA_FL)) {
		ocfs2_error(inode->i_sb, "Inode %llu lost inline data flag\n",
			    (unsigned long long)OCFS2_I(inode)->ip_blkno);
		return -EROFS;
	}

	size = i_size_read(inode);

	if (size > PAGE_SIZE ||
	    size > ocfs2_max_inline_data_with_xattr(inode->i_sb, di)) {
		ocfs2_error(inode->i_sb,
			    "Inode %llu has with inline data has bad size: %Lu\n",
			    (unsigned long long)OCFS2_I(inode)->ip_blkno,
			    (unsigned long long)size);
		return -EROFS;
	}

	kaddr = kmap_atomic(page);
	if (size)
		memcpy(kaddr, di->id2.i_data.id_data, size);
	/* Clear the remaining part of the page */
	memset(kaddr + size, 0, PAGE_SIZE - size);
	flush_dcache_page(page);
	kunmap_atomic(kaddr);

	SetPageUptodate(page);

	return 0;
}

static int ocfs2_readpage_inline(struct inode *inode, struct page *page)
{
	int ret;
	struct buffer_head *di_bh = NULL;

	BUG_ON(!PageLocked(page));
	BUG_ON(!(OCFS2_I(inode)->ip_dyn_features & OCFS2_INLINE_DATA_FL));

	ret = ocfs2_read_inode_block(inode, &di_bh);
	if (ret) {
		mlog_errno(ret);
		goto out;
	}

	ret = ocfs2_read_inline_data(inode, page, di_bh);
out:
	unlock_page(page);

	brelse(di_bh);
	return ret;
}

static int ocfs2_readpage(struct file *file, struct page *page)
{
	struct inode *inode = page->mapping->host;
	struct ocfs2_inode_info *oi = OCFS2_I(inode);
	loff_t start = (loff_t)page->index << PAGE_SHIFT;
	int ret, unlock = 1;

	trace_ocfs2_readpage((unsigned long long)oi->ip_blkno,
			     (page ? page->index : 0));

	ret = ocfs2_inode_lock_with_page(inode, NULL, 0, page);
	if (ret != 0) {
		if (ret == AOP_TRUNCATED_PAGE)
			unlock = 0;
		mlog_errno(ret);
		goto out;
	}

	if (down_read_trylock(&oi->ip_alloc_sem) == 0) {
		/*
		 * Unlock the page and cycle ip_alloc_sem so that we don't
		 * busyloop waiting for ip_alloc_sem to unlock
		 */
		ret = AOP_TRUNCATED_PAGE;
		unlock_page(page);
		unlock = 0;
		down_read(&oi->ip_alloc_sem);
		up_read(&oi->ip_alloc_sem);
		goto out_inode_unlock;
	}

	/*
	 * i_size might have just been updated as we grabed the meta lock.  We
	 * might now be discovering a truncate that hit on another node.
	 * block_read_full_page->get_block freaks out if it is asked to read
	 * beyond the end of a file, so we check here.  Callers
	 * (generic_file_read, vm_ops->fault) are clever enough to check i_size
	 * and notice that the page they just read isn't needed.
	 *
	 * XXX sys_readahead() seems to get that wrong?
	 */
	if (start >= i_size_read(inode)) {
		zero_user(page, 0, PAGE_SIZE);
		SetPageUptodate(page);
		ret = 0;
		goto out_alloc;
	}

	if (oi->ip_dyn_features & OCFS2_INLINE_DATA_FL)
		ret = ocfs2_readpage_inline(inode, page);
	else
		ret = block_read_full_page(page, ocfs2_get_block);
	unlock = 0;

out_alloc:
	up_read(&oi->ip_alloc_sem);
out_inode_unlock:
	ocfs2_inode_unlock(inode, 0);
out:
	if (unlock)
		unlock_page(page);
	return ret;
}

/*
 * This is used only for read-ahead. Failures or difficult to handle
 * situations are safe to ignore.
 *
 * Right now, we don't bother with BH_Boundary - in-inode extent lists
 * are quite large (243 extents on 4k blocks), so most inodes don't
 * grow out to a tree. If need be, detecting boundary extents could
 * trivially be added in a future version of ocfs2_get_block().
 */
static int ocfs2_readpages(struct file *filp, struct address_space *mapping,
			   struct list_head *pages, unsigned nr_pages)
{
	int ret, err = -EIO;
	struct inode *inode = mapping->host;
	struct ocfs2_inode_info *oi = OCFS2_I(inode);
	loff_t start;
	struct page *last;

	/*
	 * Use the nonblocking flag for the dlm code to avoid page
	 * lock inversion, but don't bother with retrying.
	 */
	ret = ocfs2_inode_lock_full(inode, NULL, 0, OCFS2_LOCK_NONBLOCK);
	if (ret)
		return err;

	if (down_read_trylock(&oi->ip_alloc_sem) == 0) {
		ocfs2_inode_unlock(inode, 0);
		return err;
	}

	/*
	 * Don't bother with inline-data. There isn't anything
	 * to read-ahead in that case anyway...
	 */
	if (oi->ip_dyn_features & OCFS2_INLINE_DATA_FL)
		goto out_unlock;

	/*
	 * Check whether a remote node truncated this file - we just
	 * drop out in that case as it's not worth handling here.
	 */
	last = list_entry(pages->prev, struct page, lru);
	start = (loff_t)last->index << PAGE_SHIFT;
	if (start >= i_size_read(inode))
		goto out_unlock;

	err = mpage_readpages(mapping, pages, nr_pages, ocfs2_get_block);

out_unlock:
	up_read(&oi->ip_alloc_sem);
	ocfs2_inode_unlock(inode, 0);

	return err;
}

/* Note: Because we don't support holes, our allocation has
 * already happened (allocation writes zeros to the file data)
 * so we don't have to worry about ordered writes in
 * ocfs2_writepage.
 *
 * ->writepage is called during the process of invalidating the page cache
 * during blocked lock processing.  It can't block on any cluster locks
 * to during block mapping.  It's relying on the fact that the block
 * mapping can't have disappeared under the dirty pages that it is
 * being asked to write back.
 */
static int ocfs2_writepage(struct page *page, struct writeback_control *wbc)
{
	trace_ocfs2_writepage(
		(unsigned long long)OCFS2_I(page->mapping->host)->ip_blkno,
		page->index);

	return block_write_full_page(page, ocfs2_get_block, wbc);
}

/* Taken from ext3. We don't necessarily need the full blown
 * functionality yet, but IMHO it's better to cut and paste the whole
 * thing so we can avoid introducing our own bugs (and easily pick up
 * their fixes when they happen) --Mark */
int walk_page_buffers(	handle_t *handle,
			struct buffer_head *head,
			unsigned from,
			unsigned to,
			int *partial,
			int (*fn)(	handle_t *handle,
					struct buffer_head *bh))
{
	struct buffer_head *bh;
	unsigned block_start, block_end;
	unsigned blocksize = head->b_size;
	int err, ret = 0;
	struct buffer_head *next;

	for (	bh = head, block_start = 0;
		ret == 0 && (bh != head || !block_start);
	    	block_start = block_end, bh = next)
	{
		next = bh->b_this_page;
		block_end = block_start + blocksize;
		if (block_end <= from || block_start >= to) {
			if (partial && !buffer_uptodate(bh))
				*partial = 1;
			continue;
		}
		err = (*fn)(handle, bh);
		if (!ret)
			ret = err;
	}
	return ret;
}

static sector_t ocfs2_bmap(struct address_space *mapping, sector_t block)
{
	sector_t status;
	u64 p_blkno = 0;
	int err = 0;
	struct inode *inode = mapping->host;

	trace_ocfs2_bmap((unsigned long long)OCFS2_I(inode)->ip_blkno,
			 (unsigned long long)block);

	/*
	 * The swap code (ab-)uses ->bmap to get a block mapping and then
	 * bypasseѕ the file system for actual I/O.  We really can't allow
	 * that on refcounted inodes, so we have to skip out here.  And yes,
	 * 0 is the magic code for a bmap error..
	 */
	if (ocfs2_is_refcount_inode(inode))
		return 0;

	/* We don't need to lock journal system files, since they aren't
	 * accessed concurrently from multiple nodes.
	 */
	if (!INODE_JOURNAL(inode)) {
		err = ocfs2_inode_lock(inode, NULL, 0);
		if (err) {
			if (err != -ENOENT)
				mlog_errno(err);
			goto bail;
		}
		down_read(&OCFS2_I(inode)->ip_alloc_sem);
	}

	if (!(OCFS2_I(inode)->ip_dyn_features & OCFS2_INLINE_DATA_FL))
		err = ocfs2_extent_map_get_blocks(inode, block, &p_blkno, NULL,
						  NULL);

	if (!INODE_JOURNAL(inode)) {
		up_read(&OCFS2_I(inode)->ip_alloc_sem);
		ocfs2_inode_unlock(inode, 0);
	}

	if (err) {
		mlog(ML_ERROR, "get_blocks() failed, block = %llu\n",
		     (unsigned long long)block);
		mlog_errno(err);
		goto bail;
	}

bail:
	status = err ? 0 : p_blkno;

	return status;
}

static int ocfs2_releasepage(struct page *page, gfp_t wait)
{
	if (!page_has_buffers(page))
		return 0;
	return try_to_free_buffers(page);
}

static void ocfs2_figure_cluster_boundaries(struct ocfs2_super *osb,
					    u32 cpos,
					    unsigned int *start,
					    unsigned int *end)
{
	unsigned int cluster_start = 0, cluster_end = PAGE_SIZE;

	if (unlikely(PAGE_SHIFT > osb->s_clustersize_bits)) {
		unsigned int cpp;

		cpp = 1 << (PAGE_SHIFT - osb->s_clustersize_bits);

		cluster_start = cpos % cpp;
		cluster_start = cluster_start << osb->s_clustersize_bits;

		cluster_end = cluster_start + osb->s_clustersize;
	}

	BUG_ON(cluster_start > PAGE_SIZE);
	BUG_ON(cluster_end > PAGE_SIZE);

	if (start)
		*start = cluster_start;
	if (end)
		*end = cluster_end;
}

/*
 * 'from' and 'to' are the region in the page to avoid zeroing.
 *
 * If pagesize > clustersize, this function will avoid zeroing outside
 * of the cluster boundary.
 *
 * from == to == 0 is code for "zero the entire cluster region"
 */
static void ocfs2_clear_page_regions(struct page *page,
				     struct ocfs2_super *osb, u32 cpos,
				     unsigned from, unsigned to)
{
	void *kaddr;
	unsigned int cluster_start, cluster_end;

	ocfs2_figure_cluster_boundaries(osb, cpos, &cluster_start, &cluster_end);

	kaddr = kmap_atomic(page);

	if (from || to) {
		if (from > cluster_start)
			memset(kaddr + cluster_start, 0, from - cluster_start);
		if (to < cluster_end)
			memset(kaddr + to, 0, cluster_end - to);
	} else {
		memset(kaddr + cluster_start, 0, cluster_end - cluster_start);
	}

	kunmap_atomic(kaddr);
}

/*
 * Nonsparse file systems fully allocate before we get to the write
 * code. This prevents ocfs2_write() from tagging the write as an
 * allocating one, which means ocfs2_map_page_blocks() might try to
 * read-in the blocks at the tail of our file. Avoid reading them by
 * testing i_size against each block offset.
 */
static int ocfs2_should_read_blk(struct inode *inode, struct page *page,
				 unsigned int block_start)
{
	u64 offset = page_offset(page) + block_start;

	if (ocfs2_sparse_alloc(OCFS2_SB(inode->i_sb)))
		return 1;

	if (i_size_read(inode) > offset)
		return 1;

	return 0;
}

/*
 * Some of this taken from __block_write_begin(). We already have our
 * mapping by now though, and the entire write will be allocating or
 * it won't, so not much need to use BH_New.
 *
 * This will also skip zeroing, which is handled externally.
 */
int ocfs2_map_page_blocks(struct page *page, u64 *p_blkno,
			  struct inode *inode, unsigned int from,
			  unsigned int to, int new)
{
	int ret = 0;
	struct buffer_head *head, *bh, *wait[2], **wait_bh = wait;
	unsigned int block_end, block_start;
	unsigned int bsize = i_blocksize(inode);

	if (!page_has_buffers(page))
		create_empty_buffers(page, bsize, 0);

	head = page_buffers(page);
	for (bh = head, block_start = 0; bh != head || !block_start;
	     bh = bh->b_this_page, block_start += bsize) {
		block_end = block_start + bsize;

		clear_buffer_new(bh);

		/*
		 * Ignore blocks outside of our i/o range -
		 * they may belong to unallocated clusters.
		 */
		if (block_start >= to || block_end <= from) {
			if (PageUptodate(page))
				set_buffer_uptodate(bh);
			continue;
		}

		/*
		 * For an allocating write with cluster size >= page
		 * size, we always write the entire page.
		 */
		if (new)
			set_buffer_new(bh);

		if (!buffer_mapped(bh)) {
			map_bh(bh, inode->i_sb, *p_blkno);
			clean_bdev_bh_alias(bh);
		}

		if (PageUptodate(page)) {
			if (!buffer_uptodate(bh))
				set_buffer_uptodate(bh);
		} else if (!buffer_uptodate(bh) && !buffer_delay(bh) &&
			   !buffer_new(bh) &&
			   ocfs2_should_read_blk(inode, page, block_start) &&
			   (block_start < from || block_end > to)) {
			ll_rw_block(REQ_OP_READ, 0, 1, &bh);
			*wait_bh++=bh;
		}

		*p_blkno = *p_blkno + 1;
	}

	/*
	 * If we issued read requests - let them complete.
	 */
	while(wait_bh > wait) {
		wait_on_buffer(*--wait_bh);
		if (!buffer_uptodate(*wait_bh))
			ret = -EIO;
	}

	if (ret == 0 || !new)
		return ret;

	/*
	 * If we get -EIO above, zero out any newly allocated blocks
	 * to avoid exposing stale data.
	 */
	bh = head;
	block_start = 0;
	do {
		block_end = block_start + bsize;
		if (block_end <= from)
			goto next_bh;
		if (block_start >= to)
			break;

		zero_user(page, block_start, bh->b_size);
		set_buffer_uptodate(bh);
		mark_buffer_dirty(bh);

next_bh:
		block_start = block_end;
		bh = bh->b_this_page;
	} while (bh != head);

	return ret;
}

#if (PAGE_SIZE >= OCFS2_MAX_CLUSTERSIZE)
#define OCFS2_MAX_CTXT_PAGES	1
#else
#define OCFS2_MAX_CTXT_PAGES	(OCFS2_MAX_CLUSTERSIZE / PAGE_SIZE)
#endif

#define OCFS2_MAX_CLUSTERS_PER_PAGE	(PAGE_SIZE / OCFS2_MIN_CLUSTERSIZE)

struct ocfs2_unwritten_extent {
	struct list_head	ue_node;
	struct list_head	ue_ip_node;
	u32			ue_cpos;
	u32			ue_phys;
};

/*
 * Describe the state of a single cluster to be written to.
 */
struct ocfs2_write_cluster_desc {
	u32		c_cpos;
	u32		c_phys;
	/*
	 * Give this a unique field because c_phys eventually gets
	 * filled.
	 */
	unsigned	c_new;
	unsigned	c_clear_unwritten;
	unsigned	c_needs_zero;
};

struct ocfs2_write_ctxt {
	/* Logical cluster position / len of write */
	u32				w_cpos;
	u32				w_clen;

	/* First cluster allocated in a nonsparse extend */
	u32				w_first_new_cpos;

	/* Type of caller. Must be one of buffer, mmap, direct.  */
	ocfs2_write_type_t		w_type;

	struct ocfs2_write_cluster_desc	w_desc[OCFS2_MAX_CLUSTERS_PER_PAGE];

	/*
	 * This is true if page_size > cluster_size.
	 *
	 * It triggers a set of special cases during write which might
	 * have to deal with allocating writes to partial pages.
	 */
	unsigned int			w_large_pages;

	/*
	 * Pages involved in this write.
	 *
	 * w_target_page is the page being written to by the user.
	 *
	 * w_pages is an array of pages which always contains
	 * w_target_page, and in the case of an allocating write with
	 * page_size < cluster size, it will contain zero'd and mapped
	 * pages adjacent to w_target_page which need to be written
	 * out in so that future reads from that region will get
	 * zero's.
	 */
	unsigned int			w_num_pages;
	struct page			*w_pages[OCFS2_MAX_CTXT_PAGES];
	struct page			*w_target_page;

	/*
	 * w_target_locked is used for page_mkwrite path indicating no unlocking
	 * against w_target_page in ocfs2_write_end_nolock.
	 */
	unsigned int			w_target_locked:1;

	/*
	 * ocfs2_write_end() uses this to know what the real range to
	 * write in the target should be.
	 */
	unsigned int			w_target_from;
	unsigned int			w_target_to;

	/*
	 * We could use journal_current_handle() but this is cleaner,
	 * IMHO -Mark
	 */
	handle_t			*w_handle;

	struct buffer_head		*w_di_bh;

	struct ocfs2_cached_dealloc_ctxt w_dealloc;

	struct list_head		w_unwritten_list;
	unsigned int			w_unwritten_count;
};

void ocfs2_unlock_and_free_pages(struct page **pages, int num_pages)
{
	int i;

	for(i = 0; i < num_pages; i++) {
		if (pages[i]) {
			unlock_page(pages[i]);
			mark_page_accessed(pages[i]);
			put_page(pages[i]);
		}
	}
}

static void ocfs2_unlock_pages(struct ocfs2_write_ctxt *wc)
{
	int i;

	/*
	 * w_target_locked is only set to true in the page_mkwrite() case.
	 * The intent is to allow us to lock the target page from write_begin()
	 * to write_end(). The caller must hold a ref on w_target_page.
	 */
	if (wc->w_target_locked) {
		BUG_ON(!wc->w_target_page);
		for (i = 0; i < wc->w_num_pages; i++) {
			if (wc->w_target_page == wc->w_pages[i]) {
				wc->w_pages[i] = NULL;
				break;
			}
		}
		mark_page_accessed(wc->w_target_page);
		put_page(wc->w_target_page);
	}
	ocfs2_unlock_and_free_pages(wc->w_pages, wc->w_num_pages);
}

static void ocfs2_free_unwritten_list(struct inode *inode,
				 struct list_head *head)
{
	struct ocfs2_inode_info *oi = OCFS2_I(inode);
	struct ocfs2_unwritten_extent *ue = NULL, *tmp = NULL;

	list_for_each_entry_safe(ue, tmp, head, ue_node) {
		list_del(&ue->ue_node);
		spin_lock(&oi->ip_lock);
		list_del(&ue->ue_ip_node);
		spin_unlock(&oi->ip_lock);
		kfree(ue);
	}
}

static void ocfs2_free_write_ctxt(struct inode *inode,
				  struct ocfs2_write_ctxt *wc)
{
	ocfs2_free_unwritten_list(inode, &wc->w_unwritten_list);
	ocfs2_unlock_pages(wc);
	brelse(wc->w_di_bh);
	kfree(wc);
}

static int ocfs2_alloc_write_ctxt(struct ocfs2_write_ctxt **wcp,
				  struct ocfs2_super *osb, loff_t pos,
				  unsigned len, ocfs2_write_type_t type,
				  struct buffer_head *di_bh)
{
	u32 cend;
	struct ocfs2_write_ctxt *wc;

	wc = kzalloc(sizeof(struct ocfs2_write_ctxt), GFP_NOFS);
	if (!wc)
		return -ENOMEM;

	wc->w_cpos = pos >> osb->s_clustersize_bits;
	wc->w_first_new_cpos = UINT_MAX;
	cend = (pos + len - 1) >> osb->s_clustersize_bits;
	wc->w_clen = cend - wc->w_cpos + 1;
	get_bh(di_bh);
	wc->w_di_bh = di_bh;
	wc->w_type = type;

	if (unlikely(PAGE_SHIFT > osb->s_clustersize_bits))
		wc->w_large_pages = 1;
	else
		wc->w_large_pages = 0;

	ocfs2_init_dealloc_ctxt(&wc->w_dealloc);
	INIT_LIST_HEAD(&wc->w_unwritten_list);

	*wcp = wc;

	return 0;
}

/*
 * If a page has any new buffers, zero them out here, and mark them uptodate
 * and dirty so they'll be written out (in order to prevent uninitialised
 * block data from leaking). And clear the new bit.
 */
static void ocfs2_zero_new_buffers(struct page *page, unsigned from, unsigned to)
{
	unsigned int block_start, block_end;
	struct buffer_head *head, *bh;

	BUG_ON(!PageLocked(page));
	if (!page_has_buffers(page))
		return;

	bh = head = page_buffers(page);
	block_start = 0;
	do {
		block_end = block_start + bh->b_size;

		if (buffer_new(bh)) {
			if (block_end > from && block_start < to) {
				if (!PageUptodate(page)) {
					unsigned start, end;

					start = max(from, block_start);
					end = min(to, block_end);

					zero_user_segment(page, start, end);
					set_buffer_uptodate(bh);
				}

				clear_buffer_new(bh);
				mark_buffer_dirty(bh);
			}
		}

		block_start = block_end;
		bh = bh->b_this_page;
	} while (bh != head);
}

/*
 * Only called when we have a failure during allocating write to write
 * zero's to the newly allocated region.
 */
static void ocfs2_write_failure(struct inode *inode,
				struct ocfs2_write_ctxt *wc,
				loff_t user_pos, unsigned user_len)
{
	int i;
	unsigned from = user_pos & (PAGE_SIZE - 1),
		to = user_pos + user_len;
	struct page *tmppage;

	if (wc->w_target_page)
		ocfs2_zero_new_buffers(wc->w_target_page, from, to);

	for(i = 0; i < wc->w_num_pages; i++) {
		tmppage = wc->w_pages[i];

		if (tmppage && page_has_buffers(tmppage)) {
			if (ocfs2_should_order_data(inode))
				ocfs2_jbd2_file_inode(wc->w_handle, inode);

			block_commit_write(tmppage, from, to);
		}
	}
}

static int ocfs2_prepare_page_for_write(struct inode *inode, u64 *p_blkno,
					struct ocfs2_write_ctxt *wc,
					struct page *page, u32 cpos,
					loff_t user_pos, unsigned user_len,
					int new)
{
	int ret;
	unsigned int map_from = 0, map_to = 0;
	unsigned int cluster_start, cluster_end;
	unsigned int user_data_from = 0, user_data_to = 0;

	ocfs2_figure_cluster_boundaries(OCFS2_SB(inode->i_sb), cpos,
					&cluster_start, &cluster_end);

	/* treat the write as new if the a hole/lseek spanned across
	 * the page boundary.
	 */
	new = new | ((i_size_read(inode) <= page_offset(page)) &&
			(page_offset(page) <= user_pos));

	if (page == wc->w_target_page) {
		map_from = user_pos & (PAGE_SIZE - 1);
		map_to = map_from + user_len;

		if (new)
			ret = ocfs2_map_page_blocks(page, p_blkno, inode,
						    cluster_start, cluster_end,
						    new);
		else
			ret = ocfs2_map_page_blocks(page, p_blkno, inode,
						    map_from, map_to, new);
		if (ret) {
			mlog_errno(ret);
			goto out;
		}

		user_data_from = map_from;
		user_data_to = map_to;
		if (new) {
			map_from = cluster_start;
			map_to = cluster_end;
		}
	} else {
		/*
		 * If we haven't allocated the new page yet, we
		 * shouldn't be writing it out without copying user
		 * data. This is likely a math error from the caller.
		 */
		BUG_ON(!new);

		map_from = cluster_start;
		map_to = cluster_end;

		ret = ocfs2_map_page_blocks(page, p_blkno, inode,
					    cluster_start, cluster_end, new);
		if (ret) {
			mlog_errno(ret);
			goto out;
		}
	}

	/*
	 * Parts of newly allocated pages need to be zero'd.
	 *
	 * Above, we have also rewritten 'to' and 'from' - as far as
	 * the rest of the function is concerned, the entire cluster
	 * range inside of a page needs to be written.
	 *
	 * We can skip this if the page is up to date - it's already
	 * been zero'd from being read in as a hole.
	 */
	if (new && !PageUptodate(page))
		ocfs2_clear_page_regions(page, OCFS2_SB(inode->i_sb),
					 cpos, user_data_from, user_data_to);

	flush_dcache_page(page);

out:
	return ret;
}

/*
 * This function will only grab one clusters worth of pages.
 */
static int ocfs2_grab_pages_for_write(struct address_space *mapping,
				      struct ocfs2_write_ctxt *wc,
				      u32 cpos, loff_t user_pos,
				      unsigned user_len, int new,
				      struct page *mmap_page)
{
	int ret = 0, i;
	unsigned long start, target_index, end_index, index;
	struct inode *inode = mapping->host;
	loff_t last_byte;

	target_index = user_pos >> PAGE_SHIFT;

	/*
	 * Figure out how many pages we'll be manipulating here. For
	 * non allocating write, we just change the one
	 * page. Otherwise, we'll need a whole clusters worth.  If we're
	 * writing past i_size, we only need enough pages to cover the
	 * last page of the write.
	 */
	if (new) {
		wc->w_num_pages = ocfs2_pages_per_cluster(inode->i_sb);
		start = ocfs2_align_clusters_to_page_index(inode->i_sb, cpos);
		/*
		 * We need the index *past* the last page we could possibly
		 * touch.  This is the page past the end of the write or
		 * i_size, whichever is greater.
		 */
		last_byte = max(user_pos + user_len, i_size_read(inode));
		BUG_ON(last_byte < 1);
		end_index = ((last_byte - 1) >> PAGE_SHIFT) + 1;
		if ((start + wc->w_num_pages) > end_index)
			wc->w_num_pages = end_index - start;
	} else {
		wc->w_num_pages = 1;
		start = target_index;
	}
	end_index = (user_pos + user_len - 1) >> PAGE_SHIFT;

	for(i = 0; i < wc->w_num_pages; i++) {
		index = start + i;

		if (index >= target_index && index <= end_index &&
		    wc->w_type == OCFS2_WRITE_MMAP) {
			/*
			 * ocfs2_pagemkwrite() is a little different
			 * and wants us to directly use the page
			 * passed in.
			 */
			lock_page(mmap_page);

			/* Exit and let the caller retry */
			if (mmap_page->mapping != mapping) {
				WARN_ON(mmap_page->mapping);
				unlock_page(mmap_page);
				ret = -EAGAIN;
				goto out;
			}

			get_page(mmap_page);
			wc->w_pages[i] = mmap_page;
			wc->w_target_locked = true;
		} else if (index >= target_index && index <= end_index &&
			   wc->w_type == OCFS2_WRITE_DIRECT) {
			/* Direct write has no mapping page. */
			wc->w_pages[i] = NULL;
			continue;
		} else {
			wc->w_pages[i] = find_or_create_page(mapping, index,
							     GFP_NOFS);
			if (!wc->w_pages[i]) {
				ret = -ENOMEM;
				mlog_errno(ret);
				goto out;
			}
		}
		wait_for_stable_page(wc->w_pages[i]);

		if (index == target_index)
			wc->w_target_page = wc->w_pages[i];
	}
out:
	if (ret)
		wc->w_target_locked = false;
	return ret;
}

/*
 * Prepare a single cluster for write one cluster into the file.
 */
static int ocfs2_write_cluster(struct address_space *mapping,
			       u32 *phys, unsigned int new,
			       unsigned int clear_unwritten,
			       unsigned int should_zero,
			       struct ocfs2_alloc_context *data_ac,
			       struct ocfs2_alloc_context *meta_ac,
			       struct ocfs2_write_ctxt *wc, u32 cpos,
			       loff_t user_pos, unsigned user_len)
{
	int ret, i;
	u64 p_blkno;
	struct inode *inode = mapping->host;
	struct ocfs2_extent_tree et;
	int bpc = ocfs2_clusters_to_blocks(inode->i_sb, 1);

	if (new) {
		u32 tmp_pos;

		/*
		 * This is safe to call with the page locks - it won't take
		 * any additional semaphores or cluster locks.
		 */
		tmp_pos = cpos;
		ret = ocfs2_add_inode_data(OCFS2_SB(inode->i_sb), inode,
					   &tmp_pos, 1, !clear_unwritten,
					   wc->w_di_bh, wc->w_handle,
					   data_ac, meta_ac, NULL);
		/*
		 * This shouldn't happen because we must have already
		 * calculated the correct meta data allocation required. The
		 * internal tree allocation code should know how to increase
		 * transaction credits itself.
		 *
		 * If need be, we could handle -EAGAIN for a
		 * RESTART_TRANS here.
		 */
		mlog_bug_on_msg(ret == -EAGAIN,
				"Inode %llu: EAGAIN return during allocation.\n",
				(unsigned long long)OCFS2_I(inode)->ip_blkno);
		if (ret < 0) {
			mlog_errno(ret);
			goto out;
		}
	} else if (clear_unwritten) {
		ocfs2_init_dinode_extent_tree(&et, INODE_CACHE(inode),
					      wc->w_di_bh);
		ret = ocfs2_mark_extent_written(inode, &et,
						wc->w_handle, cpos, 1, *phys,
						meta_ac, &wc->w_dealloc);
		if (ret < 0) {
			mlog_errno(ret);
			goto out;
		}
	}

	/*
	 * The only reason this should fail is due to an inability to
	 * find the extent added.
	 */
	ret = ocfs2_get_clusters(inode, cpos, phys, NULL, NULL);
	if (ret < 0) {
		mlog(ML_ERROR, "Get physical blkno failed for inode %llu, "
			    "at logical cluster %u",
			    (unsigned long long)OCFS2_I(inode)->ip_blkno, cpos);
		goto out;
	}

	BUG_ON(*phys == 0);

	p_blkno = ocfs2_clusters_to_blocks(inode->i_sb, *phys);
	if (!should_zero)
		p_blkno += (user_pos >> inode->i_sb->s_blocksize_bits) & (u64)(bpc - 1);

	for(i = 0; i < wc->w_num_pages; i++) {
		int tmpret;

		/* This is the direct io target page. */
		if (wc->w_pages[i] == NULL) {
			p_blkno++;
			continue;
		}

		tmpret = ocfs2_prepare_page_for_write(inode, &p_blkno, wc,
						      wc->w_pages[i], cpos,
						      user_pos, user_len,
						      should_zero);
		if (tmpret) {
			mlog_errno(tmpret);
			if (ret == 0)
				ret = tmpret;
		}
	}

	/*
	 * We only have cleanup to do in case of allocating write.
	 */
	if (ret && new)
		ocfs2_write_failure(inode, wc, user_pos, user_len);

out:

	return ret;
}

static int ocfs2_write_cluster_by_desc(struct address_space *mapping,
				       struct ocfs2_alloc_context *data_ac,
				       struct ocfs2_alloc_context *meta_ac,
				       struct ocfs2_write_ctxt *wc,
				       loff_t pos, unsigned len)
{
	int ret, i;
	loff_t cluster_off;
	unsigned int local_len = len;
	struct ocfs2_write_cluster_desc *desc;
	struct ocfs2_super *osb = OCFS2_SB(mapping->host->i_sb);

	for (i = 0; i < wc->w_clen; i++) {
		desc = &wc->w_desc[i];

		/*
		 * We have to make sure that the total write passed in
		 * doesn't extend past a single cluster.
		 */
		local_len = len;
		cluster_off = pos & (osb->s_clustersize - 1);
		if ((cluster_off + local_len) > osb->s_clustersize)
			local_len = osb->s_clustersize - cluster_off;

		ret = ocfs2_write_cluster(mapping, &desc->c_phys,
					  desc->c_new,
					  desc->c_clear_unwritten,
					  desc->c_needs_zero,
					  data_ac, meta_ac,
					  wc, desc->c_cpos, pos, local_len);
		if (ret) {
			mlog_errno(ret);
			goto out;
		}

		len -= local_len;
		pos += local_len;
	}

	ret = 0;
out:
	return ret;
}

/*
 * ocfs2_write_end() wants to know which parts of the target page it
 * should complete the write on. It's easiest to compute them ahead of
 * time when a more complete view of the write is available.
 */
static void ocfs2_set_target_boundaries(struct ocfs2_super *osb,
					struct ocfs2_write_ctxt *wc,
					loff_t pos, unsigned len, int alloc)
{
	struct ocfs2_write_cluster_desc *desc;

	wc->w_target_from = pos & (PAGE_SIZE - 1);
	wc->w_target_to = wc->w_target_from + len;

	if (alloc == 0)
		return;

	/*
	 * Allocating write - we may have different boundaries based
	 * on page size and cluster size.
	 *
	 * NOTE: We can no longer compute one value from the other as
	 * the actual write length and user provided length may be
	 * different.
	 */

	if (wc->w_large_pages) {
		/*
		 * We only care about the 1st and last cluster within
		 * our range and whether they should be zero'd or not. Either
		 * value may be extended out to the start/end of a
		 * newly allocated cluster.
		 */
		desc = &wc->w_desc[0];
		if (desc->c_needs_zero)
			ocfs2_figure_cluster_boundaries(osb,
							desc->c_cpos,
							&wc->w_target_from,
							NULL);

		desc = &wc->w_desc[wc->w_clen - 1];
		if (desc->c_needs_zero)
			ocfs2_figure_cluster_boundaries(osb,
							desc->c_cpos,
							NULL,
							&wc->w_target_to);
	} else {
		wc->w_target_from = 0;
		wc->w_target_to = PAGE_SIZE;
	}
}

/*
 * Check if this extent is marked UNWRITTEN by direct io. If so, we need not to
 * do the zero work. And should not to clear UNWRITTEN since it will be cleared
 * by the direct io procedure.
 * If this is a new extent that allocated by direct io, we should mark it in
 * the ip_unwritten_list.
 */
static int ocfs2_unwritten_check(struct inode *inode,
				 struct ocfs2_write_ctxt *wc,
				 struct ocfs2_write_cluster_desc *desc)
{
	struct ocfs2_inode_info *oi = OCFS2_I(inode);
	struct ocfs2_unwritten_extent *ue = NULL, *new = NULL;
	int ret = 0;

	if (!desc->c_needs_zero)
		return 0;

retry:
	spin_lock(&oi->ip_lock);
	/* Needs not to zero no metter buffer or direct. The one who is zero
	 * the cluster is doing zero. And he will clear unwritten after all
	 * cluster io finished. */
	list_for_each_entry(ue, &oi->ip_unwritten_list, ue_ip_node) {
		if (desc->c_cpos == ue->ue_cpos) {
			BUG_ON(desc->c_new);
			desc->c_needs_zero = 0;
			desc->c_clear_unwritten = 0;
			goto unlock;
		}
	}

	if (wc->w_type != OCFS2_WRITE_DIRECT)
		goto unlock;

	if (new == NULL) {
		spin_unlock(&oi->ip_lock);
		new = kmalloc(sizeof(struct ocfs2_unwritten_extent),
			     GFP_NOFS);
		if (new == NULL) {
			ret = -ENOMEM;
			goto out;
		}
		goto retry;
	}
	/* This direct write will doing zero. */
	new->ue_cpos = desc->c_cpos;
	new->ue_phys = desc->c_phys;
	desc->c_clear_unwritten = 0;
	list_add_tail(&new->ue_ip_node, &oi->ip_unwritten_list);
	list_add_tail(&new->ue_node, &wc->w_unwritten_list);
	wc->w_unwritten_count++;
	new = NULL;
unlock:
	spin_unlock(&oi->ip_lock);
out:
	kfree(new);
	return ret;
}

/*
 * Populate each single-cluster write descriptor in the write context
 * with information about the i/o to be done.
 *
 * Returns the number of clusters that will have to be allocated, as
 * well as a worst case estimate of the number of extent records that
 * would have to be created during a write to an unwritten region.
 */
static int ocfs2_populate_write_desc(struct inode *inode,
				     struct ocfs2_write_ctxt *wc,
				     unsigned int *clusters_to_alloc,
				     unsigned int *extents_to_split)
{
	int ret;
	struct ocfs2_write_cluster_desc *desc;
	unsigned int num_clusters = 0;
	unsigned int ext_flags = 0;
	u32 phys = 0;
	int i;

	*clusters_to_alloc = 0;
	*extents_to_split = 0;

	for (i = 0; i < wc->w_clen; i++) {
		desc = &wc->w_desc[i];
		desc->c_cpos = wc->w_cpos + i;

		if (num_clusters == 0) {
			/*
			 * Need to look up the next extent record.
			 */
			ret = ocfs2_get_clusters(inode, desc->c_cpos, &phys,
						 &num_clusters, &ext_flags);
			if (ret) {
				mlog_errno(ret);
				goto out;
			}

			/* We should already CoW the refcountd extent. */
			BUG_ON(ext_flags & OCFS2_EXT_REFCOUNTED);

			/*
			 * Assume worst case - that we're writing in
			 * the middle of the extent.
			 *
			 * We can assume that the write proceeds from
			 * left to right, in which case the extent
			 * insert code is smart enough to coalesce the
			 * next splits into the previous records created.
			 */
			if (ext_flags & OCFS2_EXT_UNWRITTEN)
				*extents_to_split = *extents_to_split + 2;
		} else if (phys) {
			/*
			 * Only increment phys if it doesn't describe
			 * a hole.
			 */
			phys++;
		}

		/*
		 * If w_first_new_cpos is < UINT_MAX, we have a non-sparse
		 * file that got extended.  w_first_new_cpos tells us
		 * where the newly allocated clusters are so we can
		 * zero them.
		 */
		if (desc->c_cpos >= wc->w_first_new_cpos) {
			BUG_ON(phys == 0);
			desc->c_needs_zero = 1;
		}

		desc->c_phys = phys;
		if (phys == 0) {
			desc->c_new = 1;
			desc->c_needs_zero = 1;
			desc->c_clear_unwritten = 1;
			*clusters_to_alloc = *clusters_to_alloc + 1;
		}

		if (ext_flags & OCFS2_EXT_UNWRITTEN) {
			desc->c_clear_unwritten = 1;
			desc->c_needs_zero = 1;
		}

		ret = ocfs2_unwritten_check(inode, wc, desc);
		if (ret) {
			mlog_errno(ret);
			goto out;
		}

		num_clusters--;
	}

	ret = 0;
out:
	return ret;
}

static int ocfs2_write_begin_inline(struct address_space *mapping,
				    struct inode *inode,
				    struct ocfs2_write_ctxt *wc)
{
	int ret;
	struct ocfs2_super *osb = OCFS2_SB(inode->i_sb);
	struct page *page;
	handle_t *handle;
	struct ocfs2_dinode *di = (struct ocfs2_dinode *)wc->w_di_bh->b_data;

	handle = ocfs2_start_trans(osb, OCFS2_INODE_UPDATE_CREDITS);
	if (IS_ERR(handle)) {
		ret = PTR_ERR(handle);
		mlog_errno(ret);
		goto out;
	}

	page = find_or_create_page(mapping, 0, GFP_NOFS);
	if (!page) {
		ocfs2_commit_trans(osb, handle);
		ret = -ENOMEM;
		mlog_errno(ret);
		goto out;
	}
	/*
	 * If we don't set w_num_pages then this page won't get unlocked
	 * and freed on cleanup of the write context.
	 */
	wc->w_pages[0] = wc->w_target_page = page;
	wc->w_num_pages = 1;

	ret = ocfs2_journal_access_di(handle, INODE_CACHE(inode), wc->w_di_bh,
				      OCFS2_JOURNAL_ACCESS_WRITE);
	if (ret) {
		ocfs2_commit_trans(osb, handle);

		mlog_errno(ret);
		goto out;
	}

	if (!(OCFS2_I(inode)->ip_dyn_features & OCFS2_INLINE_DATA_FL))
		ocfs2_set_inode_data_inline(inode, di);

	if (!PageUptodate(page)) {
		ret = ocfs2_read_inline_data(inode, page, wc->w_di_bh);
		if (ret) {
			ocfs2_commit_trans(osb, handle);

			goto out;
		}
	}

	wc->w_handle = handle;
out:
	return ret;
}

int ocfs2_size_fits_inline_data(struct buffer_head *di_bh, u64 new_size)
{
	struct ocfs2_dinode *di = (struct ocfs2_dinode *)di_bh->b_data;

	if (new_size <= le16_to_cpu(di->id2.i_data.id_count))
		return 1;
	return 0;
}

static int ocfs2_try_to_write_inline_data(struct address_space *mapping,
					  struct inode *inode, loff_t pos,
					  unsigned len, struct page *mmap_page,
					  struct ocfs2_write_ctxt *wc)
{
	int ret, written = 0;
	loff_t end = pos + len;
	struct ocfs2_inode_info *oi = OCFS2_I(inode);
	struct ocfs2_dinode *di = NULL;

	trace_ocfs2_try_to_write_inline_data((unsigned long long)oi->ip_blkno,
					     len, (unsigned long long)pos,
					     oi->ip_dyn_features);

	/*
	 * Handle inodes which already have inline data 1st.
	 */
	if (oi->ip_dyn_features & OCFS2_INLINE_DATA_FL) {
		if (mmap_page == NULL &&
		    ocfs2_size_fits_inline_data(wc->w_di_bh, end))
			goto do_inline_write;

		/*
		 * The write won't fit - we have to give this inode an
		 * inline extent list now.
		 */
		ret = ocfs2_convert_inline_data_to_extents(inode, wc->w_di_bh);
		if (ret)
			mlog_errno(ret);
		goto out;
	}

	/*
	 * Check whether the inode can accept inline data.
	 */
	if (oi->ip_clusters != 0 || i_size_read(inode) != 0)
		return 0;

	/*
	 * Check whether the write can fit.
	 */
	di = (struct ocfs2_dinode *)wc->w_di_bh->b_data;
	if (mmap_page ||
	    end > ocfs2_max_inline_data_with_xattr(inode->i_sb, di))
		return 0;

do_inline_write:
	ret = ocfs2_write_begin_inline(mapping, inode, wc);
	if (ret) {
		mlog_errno(ret);
		goto out;
	}

	/*
	 * This signals to the caller that the data can be written
	 * inline.
	 */
	written = 1;
out:
	return written ? written : ret;
}

/*
 * This function only does anything for file systems which can't
 * handle sparse files.
 *
 * What we want to do here is fill in any hole between the current end
 * of allocation and the end of our write. That way the rest of the
 * write path can treat it as an non-allocating write, which has no
 * special case code for sparse/nonsparse files.
 */
static int ocfs2_expand_nonsparse_inode(struct inode *inode,
					struct buffer_head *di_bh,
					loff_t pos, unsigned len,
					struct ocfs2_write_ctxt *wc)
{
	int ret;
	loff_t newsize = pos + len;

	BUG_ON(ocfs2_sparse_alloc(OCFS2_SB(inode->i_sb)));

	if (newsize <= i_size_read(inode))
		return 0;

	ret = ocfs2_extend_no_holes(inode, di_bh, newsize, pos);
	if (ret)
		mlog_errno(ret);

	/* There is no wc if this is call from direct. */
	if (wc)
		wc->w_first_new_cpos =
			ocfs2_clusters_for_bytes(inode->i_sb, i_size_read(inode));

	return ret;
}

static int ocfs2_zero_tail(struct inode *inode, struct buffer_head *di_bh,
			   loff_t pos)
{
	int ret = 0;

	BUG_ON(!ocfs2_sparse_alloc(OCFS2_SB(inode->i_sb)));
	if (pos > i_size_read(inode))
		ret = ocfs2_zero_extend(inode, di_bh, pos);

	return ret;
}

int ocfs2_write_begin_nolock(struct address_space *mapping,
			     loff_t pos, unsigned len, ocfs2_write_type_t type,
			     struct page **pagep, void **fsdata,
			     struct buffer_head *di_bh, struct page *mmap_page)
{
	int ret, cluster_of_pages, credits = OCFS2_INODE_UPDATE_CREDITS;
	unsigned int clusters_to_alloc, extents_to_split, clusters_need = 0;
	struct ocfs2_write_ctxt *wc;
	struct inode *inode = mapping->host;
	struct ocfs2_super *osb = OCFS2_SB(inode->i_sb);
	struct ocfs2_dinode *di;
	struct ocfs2_alloc_context *data_ac = NULL;
	struct ocfs2_alloc_context *meta_ac = NULL;
	handle_t *handle;
	struct ocfs2_extent_tree et;
	int try_free = 1, ret1;

try_again:
	ret = ocfs2_alloc_write_ctxt(&wc, osb, pos, len, type, di_bh);
	if (ret) {
		mlog_errno(ret);
		return ret;
	}

	if (ocfs2_supports_inline_data(osb)) {
		ret = ocfs2_try_to_write_inline_data(mapping, inode, pos, len,
						     mmap_page, wc);
		if (ret == 1) {
			ret = 0;
			goto success;
		}
		if (ret < 0) {
			mlog_errno(ret);
			goto out;
		}
	}

	/* Direct io change i_size late, should not zero tail here. */
	if (type != OCFS2_WRITE_DIRECT) {
		if (ocfs2_sparse_alloc(osb))
			ret = ocfs2_zero_tail(inode, di_bh, pos);
		else
			ret = ocfs2_expand_nonsparse_inode(inode, di_bh, pos,
							   len, wc);
		if (ret) {
			mlog_errno(ret);
			goto out;
		}
	}

	ret = ocfs2_check_range_for_refcount(inode, pos, len);
	if (ret < 0) {
		mlog_errno(ret);
		goto out;
	} else if (ret == 1) {
		clusters_need = wc->w_clen;
		ret = ocfs2_refcount_cow(inode, di_bh,
					 wc->w_cpos, wc->w_clen, UINT_MAX);
		if (ret) {
			mlog_errno(ret);
			goto out;
		}
	}

	ret = ocfs2_populate_write_desc(inode, wc, &clusters_to_alloc,
					&extents_to_split);
	if (ret) {
		mlog_errno(ret);
		goto out;
	}
	clusters_need += clusters_to_alloc;

	di = (struct ocfs2_dinode *)wc->w_di_bh->b_data;

	trace_ocfs2_write_begin_nolock(
			(unsigned long long)OCFS2_I(inode)->ip_blkno,
			(long long)i_size_read(inode),
			le32_to_cpu(di->i_clusters),
			pos, len, type, mmap_page,
			clusters_to_alloc, extents_to_split);

	/*
	 * We set w_target_from, w_target_to here so that
	 * ocfs2_write_end() knows which range in the target page to
	 * write out. An allocation requires that we write the entire
	 * cluster range.
	 */
	if (clusters_to_alloc || extents_to_split) {
		/*
		 * XXX: We are stretching the limits of
		 * ocfs2_lock_allocators(). It greatly over-estimates
		 * the work to be done.
		 */
		ocfs2_init_dinode_extent_tree(&et, INODE_CACHE(inode),
					      wc->w_di_bh);
		ret = ocfs2_lock_allocators(inode, &et,
					    clusters_to_alloc, extents_to_split,
					    &data_ac, &meta_ac);
		if (ret) {
			mlog_errno(ret);
			goto out;
		}

		if (data_ac)
			data_ac->ac_resv = &OCFS2_I(inode)->ip_la_data_resv;

		credits = ocfs2_calc_extend_credits(inode->i_sb,
						    &di->id2.i_list);
	} else if (type == OCFS2_WRITE_DIRECT)
		/* direct write needs not to start trans if no extents alloc. */
		goto success;

	/*
	 * We have to zero sparse allocated clusters, unwritten extent clusters,
	 * and non-sparse clusters we just extended.  For non-sparse writes,
	 * we know zeros will only be needed in the first and/or last cluster.
	 */
	if (wc->w_clen && (wc->w_desc[0].c_needs_zero ||
			   wc->w_desc[wc->w_clen - 1].c_needs_zero))
		cluster_of_pages = 1;
	else
		cluster_of_pages = 0;

	ocfs2_set_target_boundaries(osb, wc, pos, len, cluster_of_pages);

	handle = ocfs2_start_trans(osb, credits);
	if (IS_ERR(handle)) {
		ret = PTR_ERR(handle);
		mlog_errno(ret);
		goto out;
	}

	wc->w_handle = handle;

	if (clusters_to_alloc) {
		ret = dquot_alloc_space_nodirty(inode,
			ocfs2_clusters_to_bytes(osb->sb, clusters_to_alloc));
		if (ret)
			goto out_commit;
	}

	ret = ocfs2_journal_access_di(handle, INODE_CACHE(inode), wc->w_di_bh,
				      OCFS2_JOURNAL_ACCESS_WRITE);
	if (ret) {
		mlog_errno(ret);
		goto out_quota;
	}

	/*
	 * Fill our page array first. That way we've grabbed enough so
	 * that we can zero and flush if we error after adding the
	 * extent.
	 */
	ret = ocfs2_grab_pages_for_write(mapping, wc, wc->w_cpos, pos, len,
					 cluster_of_pages, mmap_page);
	if (ret && ret != -EAGAIN) {
		mlog_errno(ret);
		goto out_quota;
	}

	/*
	 * ocfs2_grab_pages_for_write() returns -EAGAIN if it could not lock
	 * the target page. In this case, we exit with no error and no target
	 * page. This will trigger the caller, page_mkwrite(), to re-try
	 * the operation.
	 */
	if (ret == -EAGAIN) {
		BUG_ON(wc->w_target_page);
		ret = 0;
		goto out_quota;
	}

	ret = ocfs2_write_cluster_by_desc(mapping, data_ac, meta_ac, wc, pos,
					  len);
	if (ret) {
		mlog_errno(ret);
		goto out_quota;
	}

	if (data_ac)
		ocfs2_free_alloc_context(data_ac);
	if (meta_ac)
		ocfs2_free_alloc_context(meta_ac);

success:
	if (pagep)
		*pagep = wc->w_target_page;
	*fsdata = wc;
	return 0;
out_quota:
	if (clusters_to_alloc)
		dquot_free_space(inode,
			  ocfs2_clusters_to_bytes(osb->sb, clusters_to_alloc));
out_commit:
	ocfs2_commit_trans(osb, handle);

out:
	/*
	 * The mmapped page won't be unlocked in ocfs2_free_write_ctxt(),
	 * even in case of error here like ENOSPC and ENOMEM. So, we need
	 * to unlock the target page manually to prevent deadlocks when
	 * retrying again on ENOSPC, or when returning non-VM_FAULT_LOCKED
	 * to VM code.
	 */
	if (wc->w_target_locked)
		unlock_page(mmap_page);

	ocfs2_free_write_ctxt(inode, wc);

	if (data_ac) {
		ocfs2_free_alloc_context(data_ac);
		data_ac = NULL;
	}
	if (meta_ac) {
		ocfs2_free_alloc_context(meta_ac);
		meta_ac = NULL;
	}

	if (ret == -ENOSPC && try_free) {
		/*
		 * Try to free some truncate log so that we can have enough
		 * clusters to allocate.
		 */
		try_free = 0;

		ret1 = ocfs2_try_to_free_truncate_log(osb, clusters_need);
		if (ret1 == 1)
			goto try_again;

		if (ret1 < 0)
			mlog_errno(ret1);
	}

	return ret;
}

static int ocfs2_write_begin(struct file *file, struct address_space *mapping,
			     loff_t pos, unsigned len, unsigned flags,
			     struct page **pagep, void **fsdata)
{
	int ret;
	struct buffer_head *di_bh = NULL;
	struct inode *inode = mapping->host;

	ret = ocfs2_inode_lock(inode, &di_bh, 1);
	if (ret) {
		mlog_errno(ret);
		return ret;
	}

	/*
	 * Take alloc sem here to prevent concurrent lookups. That way
	 * the mapping, zeroing and tree manipulation within
	 * ocfs2_write() will be safe against ->readpage(). This
	 * should also serve to lock out allocation from a shared
	 * writeable region.
	 */
	down_write(&OCFS2_I(inode)->ip_alloc_sem);

	ret = ocfs2_write_begin_nolock(mapping, pos, len, OCFS2_WRITE_BUFFER,
				       pagep, fsdata, di_bh, NULL);
	if (ret) {
		mlog_errno(ret);
		goto out_fail;
	}

	brelse(di_bh);

	return 0;

out_fail:
	up_write(&OCFS2_I(inode)->ip_alloc_sem);

	brelse(di_bh);
	ocfs2_inode_unlock(inode, 1);

	return ret;
}

static void ocfs2_write_end_inline(struct inode *inode, loff_t pos,
				   unsigned len, unsigned *copied,
				   struct ocfs2_dinode *di,
				   struct ocfs2_write_ctxt *wc)
{
	void *kaddr;

	if (unlikely(*copied < len)) {
		if (!PageUptodate(wc->w_target_page)) {
			*copied = 0;
			return;
		}
	}

	kaddr = kmap_atomic(wc->w_target_page);
	memcpy(di->id2.i_data.id_data + pos, kaddr + pos, *copied);
	kunmap_atomic(kaddr);

	trace_ocfs2_write_end_inline(
	     (unsigned long long)OCFS2_I(inode)->ip_blkno,
	     (unsigned long long)pos, *copied,
	     le16_to_cpu(di->id2.i_data.id_count),
	     le16_to_cpu(di->i_dyn_features));
}

int ocfs2_write_end_nolock(struct address_space *mapping,
			   loff_t pos, unsigned len, unsigned copied, void *fsdata)
{
	int i, ret;
	unsigned from, to, start = pos & (PAGE_SIZE - 1);
	struct inode *inode = mapping->host;
	struct ocfs2_super *osb = OCFS2_SB(inode->i_sb);
	struct ocfs2_write_ctxt *wc = fsdata;
	struct ocfs2_dinode *di = (struct ocfs2_dinode *)wc->w_di_bh->b_data;
	handle_t *handle = wc->w_handle;
	struct page *tmppage;

	BUG_ON(!list_empty(&wc->w_unwritten_list));

	if (handle) {
		ret = ocfs2_journal_access_di(handle, INODE_CACHE(inode),
				wc->w_di_bh, OCFS2_JOURNAL_ACCESS_WRITE);
		if (ret) {
			copied = ret;
			mlog_errno(ret);
			goto out;
		}
	}

	if (OCFS2_I(inode)->ip_dyn_features & OCFS2_INLINE_DATA_FL) {
		ocfs2_write_end_inline(inode, pos, len, &copied, di, wc);
		goto out_write_size;
	}

	if (unlikely(copied < len) && wc->w_target_page) {
		if (!PageUptodate(wc->w_target_page))
			copied = 0;

		ocfs2_zero_new_buffers(wc->w_target_page, start+copied,
				       start+len);
	}
	if (wc->w_target_page)
		flush_dcache_page(wc->w_target_page);

	for(i = 0; i < wc->w_num_pages; i++) {
		tmppage = wc->w_pages[i];

		/* This is the direct io target page. */
		if (tmppage == NULL)
			continue;

		if (tmppage == wc->w_target_page) {
			from = wc->w_target_from;
			to = wc->w_target_to;

			BUG_ON(from > PAGE_SIZE ||
			       to > PAGE_SIZE ||
			       to < from);
		} else {
			/*
			 * Pages adjacent to the target (if any) imply
			 * a hole-filling write in which case we want
			 * to flush their entire range.
			 */
			from = 0;
			to = PAGE_SIZE;
		}

		if (page_has_buffers(tmppage)) {
			if (handle && ocfs2_should_order_data(inode))
				ocfs2_jbd2_file_inode(handle, inode);
			block_commit_write(tmppage, from, to);
		}
	}

out_write_size:
	/* Direct io do not update i_size here. */
	if (wc->w_type != OCFS2_WRITE_DIRECT) {
		pos += copied;
		if (pos > i_size_read(inode)) {
			i_size_write(inode, pos);
			mark_inode_dirty(inode);
		}
		inode->i_blocks = ocfs2_inode_sector_count(inode);
		di->i_size = cpu_to_le64((u64)i_size_read(inode));
		inode->i_mtime = inode->i_ctime = current_time(inode);
		di->i_mtime = di->i_ctime = cpu_to_le64(inode->i_mtime.tv_sec);
		di->i_mtime_nsec = di->i_ctime_nsec = cpu_to_le32(inode->i_mtime.tv_nsec);
		ocfs2_update_inode_fsync_trans(handle, inode, 1);
	}
	if (handle)
		ocfs2_journal_dirty(handle, wc->w_di_bh);

out:
	/* unlock pages before dealloc since it needs acquiring j_trans_barrier
	 * lock, or it will cause a deadlock since journal commit threads holds
	 * this lock and will ask for the page lock when flushing the data.
	 * put it here to preserve the unlock order.
	 */
	ocfs2_unlock_pages(wc);

	if (handle)
		ocfs2_commit_trans(osb, handle);

	ocfs2_run_deallocs(osb, &wc->w_dealloc);

	brelse(wc->w_di_bh);
	kfree(wc);

	return copied;
}

static int ocfs2_write_end(struct file *file, struct address_space *mapping,
			   loff_t pos, unsigned len, unsigned copied,
			   struct page *page, void *fsdata)
{
	int ret;
	struct inode *inode = mapping->host;

	ret = ocfs2_write_end_nolock(mapping, pos, len, copied, fsdata);

	up_write(&OCFS2_I(inode)->ip_alloc_sem);
	ocfs2_inode_unlock(inode, 1);

	return ret;
}

struct ocfs2_dio_write_ctxt {
	struct list_head	dw_zero_list;
	unsigned		dw_zero_count;
	int			dw_orphaned;
	pid_t			dw_writer_pid;
};

static struct ocfs2_dio_write_ctxt *
ocfs2_dio_alloc_write_ctx(struct buffer_head *bh, int *alloc)
{
	struct ocfs2_dio_write_ctxt *dwc = NULL;

	if (bh->b_private)
		return bh->b_private;

	dwc = kmalloc(sizeof(struct ocfs2_dio_write_ctxt), GFP_NOFS);
	if (dwc == NULL)
		return NULL;
	INIT_LIST_HEAD(&dwc->dw_zero_list);
	dwc->dw_zero_count = 0;
	dwc->dw_orphaned = 0;
	dwc->dw_writer_pid = task_pid_nr(current);
	bh->b_private = dwc;
	*alloc = 1;

	return dwc;
}

static void ocfs2_dio_free_write_ctx(struct inode *inode,
				     struct ocfs2_dio_write_ctxt *dwc)
{
	ocfs2_free_unwritten_list(inode, &dwc->dw_zero_list);
	kfree(dwc);
}

/*
 * TODO: Make this into a generic get_blocks function.
 *
 * From do_direct_io in direct-io.c:
 *  "So what we do is to permit the ->get_blocks function to populate
 *   bh.b_size with the size of IO which is permitted at this offset and
 *   this i_blkbits."
 *
 * This function is called directly from get_more_blocks in direct-io.c.
 *
 * called like this: dio->get_blocks(dio->inode, fs_startblk,
 * 					fs_count, map_bh, dio->rw == WRITE);
 */
static int ocfs2_dio_wr_get_block(struct inode *inode, sector_t iblock,
			       struct buffer_head *bh_result, int create)
{
	struct ocfs2_super *osb = OCFS2_SB(inode->i_sb);
	struct ocfs2_inode_info *oi = OCFS2_I(inode);
	struct ocfs2_write_ctxt *wc;
	struct ocfs2_write_cluster_desc *desc = NULL;
	struct ocfs2_dio_write_ctxt *dwc = NULL;
	struct buffer_head *di_bh = NULL;
	u64 p_blkno;
	loff_t pos = iblock << inode->i_sb->s_blocksize_bits;
	unsigned len, total_len = bh_result->b_size;
	int ret = 0, first_get_block = 0;

	len = osb->s_clustersize - (pos & (osb->s_clustersize - 1));
	len = min(total_len, len);

	mlog(0, "get block of %lu at %llu:%u req %u\n",
			inode->i_ino, pos, len, total_len);

	/*
	 * Because we need to change file size in ocfs2_dio_end_io_write(), or
	 * we may need to add it to orphan dir. So can not fall to fast path
	 * while file size will be changed.
	 */
	if (pos + total_len <= i_size_read(inode)) {

		/* This is the fast path for re-write. */
		ret = ocfs2_lock_get_block(inode, iblock, bh_result, create);
		if (buffer_mapped(bh_result) &&
		    !buffer_new(bh_result) &&
		    ret == 0)
			goto out;

		/* Clear state set by ocfs2_get_block. */
		bh_result->b_state = 0;
	}

	dwc = ocfs2_dio_alloc_write_ctx(bh_result, &first_get_block);
	if (unlikely(dwc == NULL)) {
		ret = -ENOMEM;
		mlog_errno(ret);
		goto out;
	}

	if (ocfs2_clusters_for_bytes(inode->i_sb, pos + total_len) >
	    ocfs2_clusters_for_bytes(inode->i_sb, i_size_read(inode)) &&
	    !dwc->dw_orphaned) {
		/*
		 * when we are going to alloc extents beyond file size, add the
		 * inode to orphan dir, so we can recall those spaces when
		 * system crashed during write.
		 */
		ret = ocfs2_add_inode_to_orphan(osb, inode);
		if (ret < 0) {
			mlog_errno(ret);
			goto out;
		}
		dwc->dw_orphaned = 1;
	}

	ret = ocfs2_inode_lock(inode, &di_bh, 1);
	if (ret) {
		mlog_errno(ret);
		goto out;
	}

	down_write(&oi->ip_alloc_sem);

	if (first_get_block) {
		if (ocfs2_sparse_alloc(osb))
			ret = ocfs2_zero_tail(inode, di_bh, pos);
		else
			ret = ocfs2_expand_nonsparse_inode(inode, di_bh, pos,
							   total_len, NULL);
		if (ret < 0) {
			mlog_errno(ret);
			goto unlock;
		}
	}

	ret = ocfs2_write_begin_nolock(inode->i_mapping, pos, len,
				       OCFS2_WRITE_DIRECT, NULL,
				       (void **)&wc, di_bh, NULL);
	if (ret) {
		mlog_errno(ret);
		goto unlock;
	}

	desc = &wc->w_desc[0];

	p_blkno = ocfs2_clusters_to_blocks(inode->i_sb, desc->c_phys);
	BUG_ON(p_blkno == 0);
	p_blkno += iblock & (u64)(ocfs2_clusters_to_blocks(inode->i_sb, 1) - 1);

	map_bh(bh_result, inode->i_sb, p_blkno);
	bh_result->b_size = len;
	if (desc->c_needs_zero)
		set_buffer_new(bh_result);

	/* May sleep in end_io. It should not happen in a irq context. So defer
	 * it to dio work queue. */
	set_buffer_defer_completion(bh_result);

	if (!list_empty(&wc->w_unwritten_list)) {
		struct ocfs2_unwritten_extent *ue = NULL;

		ue = list_first_entry(&wc->w_unwritten_list,
				      struct ocfs2_unwritten_extent,
				      ue_node);
		BUG_ON(ue->ue_cpos != desc->c_cpos);
		/* The physical address may be 0, fill it. */
		ue->ue_phys = desc->c_phys;

		list_splice_tail_init(&wc->w_unwritten_list, &dwc->dw_zero_list);
		dwc->dw_zero_count += wc->w_unwritten_count;
	}

	ret = ocfs2_write_end_nolock(inode->i_mapping, pos, len, len, wc);
	BUG_ON(ret != len);
	ret = 0;
unlock:
	up_write(&oi->ip_alloc_sem);
	ocfs2_inode_unlock(inode, 1);
	brelse(di_bh);
out:
	if (ret < 0)
		ret = -EIO;
	return ret;
}

static int ocfs2_dio_end_io_write(struct inode *inode,
				  struct ocfs2_dio_write_ctxt *dwc,
				  loff_t offset,
				  ssize_t bytes)
{
	struct ocfs2_cached_dealloc_ctxt dealloc;
	struct ocfs2_extent_tree et;
	struct ocfs2_super *osb = OCFS2_SB(inode->i_sb);
	struct ocfs2_inode_info *oi = OCFS2_I(inode);
	struct ocfs2_unwritten_extent *ue = NULL;
	struct buffer_head *di_bh = NULL;
	struct ocfs2_dinode *di;
	struct ocfs2_alloc_context *data_ac = NULL;
	struct ocfs2_alloc_context *meta_ac = NULL;
	handle_t *handle = NULL;
	loff_t end = offset + bytes;
	int ret = 0, credits = 0, locked = 0;

	ocfs2_init_dealloc_ctxt(&dealloc);

	/* We do clear unwritten, delete orphan, change i_size here. If neither
	 * of these happen, we can skip all this. */
	if (list_empty(&dwc->dw_zero_list) &&
	    end <= i_size_read(inode) &&
	    !dwc->dw_orphaned)
		goto out;

	/* ocfs2_file_write_iter will get i_mutex, so we need not lock if we
	 * are in that context. */
	if (dwc->dw_writer_pid != task_pid_nr(current)) {
		inode_lock(inode);
		locked = 1;
	}

	ret = ocfs2_inode_lock(inode, &di_bh, 1);
	if (ret < 0) {
		mlog_errno(ret);
		goto out;
	}

	down_write(&oi->ip_alloc_sem);

	/* Delete orphan before acquire i_mutex. */
	if (dwc->dw_orphaned) {
		BUG_ON(dwc->dw_writer_pid != task_pid_nr(current));

		end = end > i_size_read(inode) ? end : 0;

		ret = ocfs2_del_inode_from_orphan(osb, inode, di_bh,
				!!end, end);
		if (ret < 0)
			mlog_errno(ret);
	}

	di = (struct ocfs2_dinode *)di_bh->b_data;

	ocfs2_init_dinode_extent_tree(&et, INODE_CACHE(inode), di_bh);

	/* Attach dealloc with extent tree in case that we may reuse extents
	 * which are already unlinked from current extent tree due to extent
	 * rotation and merging.
	 */
	et.et_dealloc = &dealloc;

	ret = ocfs2_lock_allocators(inode, &et, 0, dwc->dw_zero_count*2,
				    &data_ac, &meta_ac);
	if (ret) {
		mlog_errno(ret);
		goto unlock;
	}

	credits = ocfs2_calc_extend_credits(inode->i_sb, &di->id2.i_list);

	handle = ocfs2_start_trans(osb, credits);
	if (IS_ERR(handle)) {
		ret = PTR_ERR(handle);
		mlog_errno(ret);
		goto unlock;
	}
	ret = ocfs2_journal_access_di(handle, INODE_CACHE(inode), di_bh,
				      OCFS2_JOURNAL_ACCESS_WRITE);
	if (ret) {
		mlog_errno(ret);
		goto commit;
	}

	list_for_each_entry(ue, &dwc->dw_zero_list, ue_node) {
		ret = ocfs2_mark_extent_written(inode, &et, handle,
						ue->ue_cpos, 1,
						ue->ue_phys,
						meta_ac, &dealloc);
		if (ret < 0) {
			mlog_errno(ret);
			break;
		}
	}

	if (end > i_size_read(inode)) {
		ret = ocfs2_set_inode_size(handle, inode, di_bh, end);
		if (ret < 0)
			mlog_errno(ret);
	}
commit:
	ocfs2_commit_trans(osb, handle);
unlock:
	up_write(&oi->ip_alloc_sem);
	ocfs2_inode_unlock(inode, 1);
	brelse(di_bh);
out:
	if (data_ac)
		ocfs2_free_alloc_context(data_ac);
	if (meta_ac)
		ocfs2_free_alloc_context(meta_ac);
	ocfs2_run_deallocs(osb, &dealloc);
	if (locked)
		inode_unlock(inode);
	ocfs2_dio_free_write_ctx(inode, dwc);

	return ret;
}

/*
 * ocfs2_dio_end_io is called by the dio core when a dio is finished.  We're
 * particularly interested in the aio/dio case.  We use the rw_lock DLM lock
 * to protect io on one node from truncation on another.
 */
static int ocfs2_dio_end_io(struct kiocb *iocb,
			    loff_t offset,
			    ssize_t bytes,
			    void *private)
{
	struct inode *inode = file_inode(iocb->ki_filp);
	int level;
	int ret = 0;

	/* this io's submitter should not have unlocked this before we could */
	BUG_ON(!ocfs2_iocb_is_rw_locked(iocb));

	if (bytes > 0 && private)
		ret = ocfs2_dio_end_io_write(inode, private, offset, bytes);

	ocfs2_iocb_clear_rw_locked(iocb);

	level = ocfs2_iocb_rw_locked_level(iocb);
	ocfs2_rw_unlock(inode, level);
	return ret;
}

static ssize_t ocfs2_direct_IO(struct kiocb *iocb, struct iov_iter *iter)
{
	struct file *file = iocb->ki_filp;
	struct inode *inode = file->f_mapping->host;
	struct ocfs2_super *osb = OCFS2_SB(inode->i_sb);
	get_block_t *get_block;

	/*
	 * Fallback to buffered I/O if we see an inode without
	 * extents.
	 */
	if (OCFS2_I(inode)->ip_dyn_features & OCFS2_INLINE_DATA_FL)
		return 0;

	/* Fallback to buffered I/O if we do not support append dio. */
	if (iocb->ki_pos + iter->count > i_size_read(inode) &&
	    !ocfs2_supports_append_dio(osb))
		return 0;

	if (iov_iter_rw(iter) == READ)
		get_block = ocfs2_lock_get_block;
	else
		get_block = ocfs2_dio_wr_get_block;

	return __blockdev_direct_IO(iocb, inode, inode->i_sb->s_bdev,
				    iter, get_block,
				    ocfs2_dio_end_io, NULL, 0);
}

const struct address_space_operations ocfs2_aops = {
	.readpage		= ocfs2_readpage,
	.readpages		= ocfs2_readpages,
	.writepage		= ocfs2_writepage,
	.write_begin		= ocfs2_write_begin,
	.write_end		= ocfs2_write_end,
	.bmap			= ocfs2_bmap,
	.direct_IO		= ocfs2_direct_IO,
	.invalidatepage		= block_invalidatepage,
	.releasepage		= ocfs2_releasepage,
	.migratepage		= buffer_migrate_page,
	.is_partially_uptodate	= block_is_partially_uptodate,
	.error_remove_page	= generic_error_remove_page,
};
