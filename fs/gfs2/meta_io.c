/*
 * Copyright (C) Sistina Software, Inc.  1997-2003 All rights reserved.
 * Copyright (C) 2004-2005 Red Hat, Inc.  All rights reserved.
 *
 * This copyrighted material is made available to anyone wishing to use,
 * modify, copy, or redistribute it subject to the terms and conditions
 * of the GNU General Public License v.2.
 */

#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/completion.h>
#include <linux/buffer_head.h>
#include <linux/mm.h>
#include <linux/pagemap.h>
#include <linux/writeback.h>
#include <linux/swap.h>
#include <linux/delay.h>
#include <asm/semaphore.h>

#include "gfs2.h"
#include "glock.h"
#include "glops.h"
#include "inode.h"
#include "log.h"
#include "lops.h"
#include "meta_io.h"
#include "rgrp.h"
#include "trans.h"

#define buffer_busy(bh) \
((bh)->b_state & ((1ul << BH_Dirty) | (1ul << BH_Lock) | (1ul << BH_Pinned)))
#define buffer_in_io(bh) \
((bh)->b_state & ((1ul << BH_Dirty) | (1ul << BH_Lock)))

static int aspace_get_block(struct inode *inode, sector_t lblock,
			    struct buffer_head *bh_result, int create)
{
	gfs2_assert_warn(get_v2sdp(inode->i_sb), 0);
	return -EOPNOTSUPP;
}

static int gfs2_aspace_writepage(struct page *page,
				 struct writeback_control *wbc)
{
	return block_write_full_page(page, aspace_get_block, wbc);
}

/**
 * stuck_releasepage - We're stuck in gfs2_releasepage().  Print stuff out.
 * @bh: the buffer we're stuck on
 *
 */

static void stuck_releasepage(struct buffer_head *bh)
{
	struct gfs2_sbd *sdp = get_v2sdp(bh->b_page->mapping->host->i_sb);
	struct gfs2_bufdata *bd = get_v2bd(bh);
	struct gfs2_glock *gl;

	fs_warn(sdp, "stuck in gfs2_releasepage()\n");
	fs_warn(sdp, "blkno = %llu, bh->b_count = %d\n",
		(uint64_t)bh->b_blocknr, atomic_read(&bh->b_count));
	fs_warn(sdp, "pinned = %u\n", buffer_pinned(bh));
	fs_warn(sdp, "get_v2bd(bh) = %s\n", (bd) ? "!NULL" : "NULL");

	if (!bd)
		return;

	gl = bd->bd_gl;

	fs_warn(sdp, "gl = (%u, %llu)\n", 
		gl->gl_name.ln_type, gl->gl_name.ln_number);

	fs_warn(sdp, "bd_list_tr = %s, bd_le.le_list = %s\n",
		(list_empty(&bd->bd_list_tr)) ? "no" : "yes",
		(list_empty(&bd->bd_le.le_list)) ? "no" : "yes");

	if (gl->gl_ops == &gfs2_inode_glops) {
		struct gfs2_inode *ip = get_gl2ip(gl);
		unsigned int x;

		if (!ip)
			return;

		fs_warn(sdp, "ip = %llu %llu\n",
			ip->i_num.no_formal_ino, ip->i_num.no_addr);
		fs_warn(sdp, "ip->i_count = %d, ip->i_vnode = %s\n",
			atomic_read(&ip->i_count),
			(ip->i_vnode) ? "!NULL" : "NULL");

		for (x = 0; x < GFS2_MAX_META_HEIGHT; x++)
			fs_warn(sdp, "ip->i_cache[%u] = %s\n",
				x, (ip->i_cache[x]) ? "!NULL" : "NULL");
	}
}

/**
 * gfs2_aspace_releasepage - free the metadata associated with a page
 * @page: the page that's being released
 * @gfp_mask: passed from Linux VFS, ignored by us
 *
 * Call try_to_free_buffers() if the buffers in this page can be
 * released.
 *
 * Returns: 0
 */

static int gfs2_aspace_releasepage(struct page *page, gfp_t gfp_mask)
{
	struct inode *aspace = page->mapping->host;
	struct gfs2_sbd *sdp = get_v2sdp(aspace->i_sb);
	struct buffer_head *bh, *head;
	struct gfs2_bufdata *bd;
	unsigned long t;

	if (!page_has_buffers(page))
		goto out;

	head = bh = page_buffers(page);
	do {
		t = jiffies;

		while (atomic_read(&bh->b_count)) {
			if (atomic_read(&aspace->i_writecount)) {
				if (time_after_eq(jiffies, t +
				    gfs2_tune_get(sdp, gt_stall_secs) * HZ)) {
					stuck_releasepage(bh);
					t = jiffies;
				}

				yield();
				continue;
			}

			return 0;
		}

		gfs2_assert_warn(sdp, !buffer_pinned(bh));

		bd = get_v2bd(bh);
		if (bd) {
			gfs2_assert_warn(sdp, bd->bd_bh == bh);
			gfs2_assert_warn(sdp, list_empty(&bd->bd_list_tr));
			gfs2_assert_warn(sdp, list_empty(&bd->bd_le.le_list));
			gfs2_assert_warn(sdp, !bd->bd_ail);
			kmem_cache_free(gfs2_bufdata_cachep, bd);
			atomic_dec(&sdp->sd_bufdata_count);
			set_v2bd(bh, NULL);
		}

		bh = bh->b_this_page;
	}
	while (bh != head);

 out:
	return try_to_free_buffers(page);
}

static struct address_space_operations aspace_aops = {
	.writepage = gfs2_aspace_writepage,
	.releasepage = gfs2_aspace_releasepage,
};

/**
 * gfs2_aspace_get - Create and initialize a struct inode structure
 * @sdp: the filesystem the aspace is in
 *
 * Right now a struct inode is just a struct inode.  Maybe Linux
 * will supply a more lightweight address space construct (that works)
 * in the future.
 *
 * Make sure pages/buffers in this aspace aren't in high memory.
 *
 * Returns: the aspace
 */

struct inode *gfs2_aspace_get(struct gfs2_sbd *sdp)
{
	struct inode *aspace;

	aspace = new_inode(sdp->sd_vfs);
	if (aspace) {
		mapping_set_gfp_mask(aspace->i_mapping, GFP_KERNEL);
		aspace->i_mapping->a_ops = &aspace_aops;
		aspace->i_size = ~0ULL;
		set_v2ip(aspace, NULL);
		insert_inode_hash(aspace);
	}

	return aspace;
}

void gfs2_aspace_put(struct inode *aspace)
{
	remove_inode_hash(aspace);
	iput(aspace);
}

/**
 * gfs2_ail1_start_one - Start I/O on a part of the AIL
 * @sdp: the filesystem
 * @tr: the part of the AIL
 *
 */

void gfs2_ail1_start_one(struct gfs2_sbd *sdp, struct gfs2_ail *ai)
{
	struct gfs2_bufdata *bd, *s;
	struct buffer_head *bh;
	int retry;

	do {
		retry = 0;

		list_for_each_entry_safe_reverse(bd, s, &ai->ai_ail1_list,
						 bd_ail_st_list) {
			bh = bd->bd_bh;

			gfs2_assert(sdp, bd->bd_ail == ai);

			if (!buffer_busy(bh)) {
				if (!buffer_uptodate(bh))
					gfs2_io_error_bh(sdp, bh);
				list_move(&bd->bd_ail_st_list,
					  &ai->ai_ail2_list);
				continue;
			}

			if (!buffer_dirty(bh))
				continue;

			list_move(&bd->bd_ail_st_list, &ai->ai_ail1_list);

			gfs2_log_unlock(sdp);
			wait_on_buffer(bh);
			ll_rw_block(WRITE, 1, &bh);
			gfs2_log_lock(sdp);

			retry = 1;
			break;
		}
	} while (retry);
}

/**
 * gfs2_ail1_empty_one - Check whether or not a trans in the AIL has been synced
 * @sdp: the filesystem
 * @ai: the AIL entry
 *
 */

int gfs2_ail1_empty_one(struct gfs2_sbd *sdp, struct gfs2_ail *ai, int flags)
{
	struct gfs2_bufdata *bd, *s;
	struct buffer_head *bh;

	list_for_each_entry_safe_reverse(bd, s, &ai->ai_ail1_list,
					 bd_ail_st_list) {
		bh = bd->bd_bh;

		gfs2_assert(sdp, bd->bd_ail == ai);

		if (buffer_busy(bh)) {
			if (flags & DIO_ALL)
				continue;
			else
				break;
		}

		if (!buffer_uptodate(bh))
			gfs2_io_error_bh(sdp, bh);

		list_move(&bd->bd_ail_st_list, &ai->ai_ail2_list);
	}

	return list_empty(&ai->ai_ail1_list);
}

/**
 * gfs2_ail2_empty_one - Check whether or not a trans in the AIL has been synced
 * @sdp: the filesystem
 * @ai: the AIL entry
 *
 */

void gfs2_ail2_empty_one(struct gfs2_sbd *sdp, struct gfs2_ail *ai)
{
	struct list_head *head = &ai->ai_ail2_list;
	struct gfs2_bufdata *bd;

	while (!list_empty(head)) {
		bd = list_entry(head->prev, struct gfs2_bufdata,
				bd_ail_st_list);
		gfs2_assert(sdp, bd->bd_ail == ai);
		bd->bd_ail = NULL;
		list_del(&bd->bd_ail_st_list);
		list_del(&bd->bd_ail_gl_list);
		atomic_dec(&bd->bd_gl->gl_ail_count);
		brelse(bd->bd_bh);
	}
}

/**
 * ail_empty_gl - remove all buffers for a given lock from the AIL
 * @gl: the glock
 *
 * None of the buffers should be dirty, locked, or pinned.
 */

void gfs2_ail_empty_gl(struct gfs2_glock *gl)
{
	struct gfs2_sbd *sdp = gl->gl_sbd;
	unsigned int blocks;
	struct list_head *head = &gl->gl_ail_list;
	struct gfs2_bufdata *bd;
	struct buffer_head *bh;
	uint64_t blkno;
	int error;

	blocks = atomic_read(&gl->gl_ail_count);
	if (!blocks)
		return;

	error = gfs2_trans_begin(sdp, 0, blocks);
	if (gfs2_assert_withdraw(sdp, !error))
		return;

	gfs2_log_lock(sdp);
	while (!list_empty(head)) {
		bd = list_entry(head->next, struct gfs2_bufdata,
				bd_ail_gl_list);
		bh = bd->bd_bh;
		blkno = bh->b_blocknr;
		gfs2_assert_withdraw(sdp, !buffer_busy(bh));

		bd->bd_ail = NULL;
		list_del(&bd->bd_ail_st_list);
		list_del(&bd->bd_ail_gl_list);
		atomic_dec(&gl->gl_ail_count);
		brelse(bh);
		gfs2_log_unlock(sdp);

		gfs2_trans_add_revoke(sdp, blkno);

		gfs2_log_lock(sdp);
	}
	gfs2_assert_withdraw(sdp, !atomic_read(&gl->gl_ail_count));
	gfs2_log_unlock(sdp);

	gfs2_trans_end(sdp);
	gfs2_log_flush(sdp);
}

/**
 * gfs2_meta_inval - Invalidate all buffers associated with a glock
 * @gl: the glock
 *
 */

void gfs2_meta_inval(struct gfs2_glock *gl)
{
	struct gfs2_sbd *sdp = gl->gl_sbd;
	struct inode *aspace = gl->gl_aspace;
	struct address_space *mapping = gl->gl_aspace->i_mapping;

	gfs2_assert_withdraw(sdp, !atomic_read(&gl->gl_ail_count));

	atomic_inc(&aspace->i_writecount);
	truncate_inode_pages(mapping, 0);
	atomic_dec(&aspace->i_writecount);

	gfs2_assert_withdraw(sdp, !mapping->nrpages);
}

/**
 * gfs2_meta_sync - Sync all buffers associated with a glock
 * @gl: The glock
 * @flags: DIO_START | DIO_WAIT
 *
 */

void gfs2_meta_sync(struct gfs2_glock *gl, int flags)
{
	struct address_space *mapping = gl->gl_aspace->i_mapping;
	int error = 0;

	if (flags & DIO_START)
		filemap_fdatawrite(mapping);
	if (!error && (flags & DIO_WAIT))
		error = filemap_fdatawait(mapping);

	if (error)
		gfs2_io_error(gl->gl_sbd);
}

/**
 * getbuf - Get a buffer with a given address space
 * @sdp: the filesystem
 * @aspace: the address space
 * @blkno: the block number (filesystem scope)
 * @create: 1 if the buffer should be created
 *
 * Returns: the buffer
 */

static struct buffer_head *getbuf(struct gfs2_sbd *sdp, struct inode *aspace,
				  uint64_t blkno, int create)
{
	struct page *page;
	struct buffer_head *bh;
	unsigned int shift;
	unsigned long index;
	unsigned int bufnum;

	shift = PAGE_CACHE_SHIFT - sdp->sd_sb.sb_bsize_shift;
	index = blkno >> shift;             /* convert block to page */
	bufnum = blkno - (index << shift);  /* block buf index within page */

	if (create) {
		for (;;) {
			page = grab_cache_page(aspace->i_mapping, index);
			if (page)
				break;
			yield();
		}
	} else {
		page = find_lock_page(aspace->i_mapping, index);
		if (!page)
			return NULL;
	}

	if (!page_has_buffers(page))
		create_empty_buffers(page, sdp->sd_sb.sb_bsize, 0);

	/* Locate header for our buffer within our page */
	for (bh = page_buffers(page); bufnum--; bh = bh->b_this_page)
		/* Do nothing */;
	get_bh(bh);

	if (!buffer_mapped(bh))
		map_bh(bh, sdp->sd_vfs, blkno);

	unlock_page(page);
	mark_page_accessed(page);
	page_cache_release(page);

	return bh;
}

static void meta_prep_new(struct buffer_head *bh)
{
	struct gfs2_meta_header *mh = (struct gfs2_meta_header *)bh->b_data;

	lock_buffer(bh);
	clear_buffer_dirty(bh);
	set_buffer_uptodate(bh);
	unlock_buffer(bh);

	mh->mh_magic = cpu_to_be32(GFS2_MAGIC);
}

/**
 * gfs2_meta_new - Get a block
 * @gl: The glock associated with this block
 * @blkno: The block number
 *
 * Returns: The buffer
 */

struct buffer_head *gfs2_meta_new(struct gfs2_glock *gl, uint64_t blkno)
{
	struct buffer_head *bh;
	bh = getbuf(gl->gl_sbd, gl->gl_aspace, blkno, CREATE);
	meta_prep_new(bh);
	return bh;
}

/**
 * gfs2_meta_read - Read a block from disk
 * @gl: The glock covering the block
 * @blkno: The block number
 * @flags: flags to gfs2_dreread()
 * @bhp: the place where the buffer is returned (NULL on failure)
 *
 * Returns: errno
 */

int gfs2_meta_read(struct gfs2_glock *gl, uint64_t blkno, int flags,
		   struct buffer_head **bhp)
{
	int error;

	*bhp = getbuf(gl->gl_sbd, gl->gl_aspace, blkno, CREATE);
	error = gfs2_meta_reread(gl->gl_sbd, *bhp, flags);
	if (error)
		brelse(*bhp);

	return error;
}

/**
 * gfs2_meta_reread - Reread a block from disk
 * @sdp: the filesystem
 * @bh: The block to read
 * @flags: Flags that control the read
 *
 * Returns: errno
 */

int gfs2_meta_reread(struct gfs2_sbd *sdp, struct buffer_head *bh, int flags)
{
	if (unlikely(test_bit(SDF_SHUTDOWN, &sdp->sd_flags)))
		return -EIO;

	if (flags & DIO_FORCE)
		clear_buffer_uptodate(bh);

	if ((flags & DIO_START) && !buffer_uptodate(bh))
		ll_rw_block(READ, 1, &bh);

	if (flags & DIO_WAIT) {
		wait_on_buffer(bh);

		if (!buffer_uptodate(bh)) {
			struct gfs2_trans *tr = get_transaction;
			if (tr && tr->tr_touched)
				gfs2_io_error_bh(sdp, bh);
			return -EIO;
		}
		if (unlikely(test_bit(SDF_SHUTDOWN, &sdp->sd_flags)))
			return -EIO;
	}

	return 0;
}

/**
 * gfs2_attach_bufdata - attach a struct gfs2_bufdata structure to a buffer
 * @gl: the glock the buffer belongs to
 * @bh: The buffer to be attached to
 * @meta: Flag to indicate whether its metadata or not
 */

void gfs2_attach_bufdata(struct gfs2_glock *gl, struct buffer_head *bh, int meta)
{
	struct gfs2_bufdata *bd;

	lock_page(bh->b_page);

	if (get_v2bd(bh)) {
		unlock_page(bh->b_page);
		return;
	}

	bd = kmem_cache_alloc(gfs2_bufdata_cachep, GFP_KERNEL | __GFP_NOFAIL),
	atomic_inc(&gl->gl_sbd->sd_bufdata_count);

	memset(bd, 0, sizeof(struct gfs2_bufdata));

	bd->bd_bh = bh;
	bd->bd_gl = gl;

	INIT_LIST_HEAD(&bd->bd_list_tr);
	if (meta)
		lops_init_le(&bd->bd_le, &gfs2_buf_lops);
	else
		lops_init_le(&bd->bd_le, &gfs2_databuf_lops);

	set_v2bd(bh, bd);

	unlock_page(bh->b_page);
}

/**
 * gfs2_meta_pin - Pin a metadata buffer in memory
 * @sdp: the filesystem the buffer belongs to
 * @bh: The buffer to be pinned
 *
 */

void gfs2_meta_pin(struct gfs2_sbd *sdp, struct buffer_head *bh)
{
	struct gfs2_bufdata *bd = get_v2bd(bh);

	gfs2_assert_withdraw(sdp, test_bit(SDF_JOURNAL_LIVE, &sdp->sd_flags));

	if (test_set_buffer_pinned(bh))
		gfs2_assert_withdraw(sdp, 0);

	wait_on_buffer(bh);

	/* If this buffer is in the AIL and it has already been written
	   to in-place disk block, remove it from the AIL. */

	gfs2_log_lock(sdp);
	if (bd->bd_ail && !buffer_in_io(bh))
		list_move(&bd->bd_ail_st_list, &bd->bd_ail->ai_ail2_list);
	gfs2_log_unlock(sdp);

	clear_buffer_dirty(bh);
	wait_on_buffer(bh);

	if (!buffer_uptodate(bh))
		gfs2_io_error_bh(sdp, bh);

	get_bh(bh);
}

/**
 * gfs2_meta_unpin - Unpin a buffer
 * @sdp: the filesystem the buffer belongs to
 * @bh: The buffer to unpin
 * @ai:
 *
 */

void gfs2_meta_unpin(struct gfs2_sbd *sdp, struct buffer_head *bh,
		     struct gfs2_ail *ai)
{
	struct gfs2_bufdata *bd = get_v2bd(bh);

	gfs2_assert_withdraw(sdp, buffer_uptodate(bh));

	if (!buffer_pinned(bh))
		gfs2_assert_withdraw(sdp, 0);

	mark_buffer_dirty(bh);
	clear_buffer_pinned(bh);

	gfs2_log_lock(sdp);
	if (bd->bd_ail) {
		list_del(&bd->bd_ail_st_list);
		brelse(bh);
	} else {
		struct gfs2_glock *gl = bd->bd_gl;
		list_add(&bd->bd_ail_gl_list, &gl->gl_ail_list);
		atomic_inc(&gl->gl_ail_count);
	}
	bd->bd_ail = ai;
	list_add(&bd->bd_ail_st_list, &ai->ai_ail1_list);
	gfs2_log_unlock(sdp);
}

/**
 * gfs2_meta_wipe - make inode's buffers so they aren't dirty/pinned anymore
 * @ip: the inode who owns the buffers
 * @bstart: the first buffer in the run
 * @blen: the number of buffers in the run
 *
 */

void gfs2_meta_wipe(struct gfs2_inode *ip, uint64_t bstart, uint32_t blen)
{
	struct gfs2_sbd *sdp = ip->i_sbd;
	struct inode *aspace = ip->i_gl->gl_aspace;
	struct buffer_head *bh;

	while (blen) {
		bh = getbuf(sdp, aspace, bstart, NO_CREATE);
		if (bh) {
			struct gfs2_bufdata *bd = get_v2bd(bh);

			if (test_clear_buffer_pinned(bh)) {
				gfs2_log_lock(sdp);
				list_del_init(&bd->bd_le.le_list);
				gfs2_assert_warn(sdp, sdp->sd_log_num_buf);
				sdp->sd_log_num_buf--;
				gfs2_log_unlock(sdp);
				get_transaction->tr_num_buf_rm++;
				brelse(bh);
			}
			if (bd) {
				gfs2_log_lock(sdp);
				if (bd->bd_ail) {
					uint64_t blkno = bh->b_blocknr;
					bd->bd_ail = NULL;
					list_del(&bd->bd_ail_st_list);
					list_del(&bd->bd_ail_gl_list);
					atomic_dec(&bd->bd_gl->gl_ail_count);
					brelse(bh);
					gfs2_log_unlock(sdp);
					gfs2_trans_add_revoke(sdp, blkno);
				} else
					gfs2_log_unlock(sdp);
			}

			lock_buffer(bh);
			clear_buffer_dirty(bh);
			clear_buffer_uptodate(bh);
			unlock_buffer(bh);

			brelse(bh);
		}

		bstart++;
		blen--;
	}
}

/**
 * gfs2_meta_cache_flush - get rid of any references on buffers for this inode
 * @ip: The GFS2 inode
 *
 * This releases buffers that are in the most-recently-used array of
 * blocks used for indirect block addressing for this inode.
 */

void gfs2_meta_cache_flush(struct gfs2_inode *ip)
{
	struct buffer_head **bh_slot;
	unsigned int x;

	spin_lock(&ip->i_spin);

	for (x = 0; x < GFS2_MAX_META_HEIGHT; x++) {
		bh_slot = &ip->i_cache[x];
		if (!*bh_slot)
			break;
		brelse(*bh_slot);
		*bh_slot = NULL;
	}

	spin_unlock(&ip->i_spin);
}

/**
 * gfs2_meta_indirect_buffer - Get a metadata buffer
 * @ip: The GFS2 inode
 * @height: The level of this buf in the metadata (indir addr) tree (if any)
 * @num: The block number (device relative) of the buffer
 * @new: Non-zero if we may create a new buffer
 * @bhp: the buffer is returned here
 *
 * Try to use the gfs2_inode's MRU metadata tree cache.
 *
 * Returns: errno
 */

int gfs2_meta_indirect_buffer(struct gfs2_inode *ip, int height, uint64_t num,
			      int new, struct buffer_head **bhp)
{
	struct buffer_head *bh, **bh_slot = ip->i_cache + height;
	int error;

	spin_lock(&ip->i_spin);
	bh = *bh_slot;
	if (bh) {
		if (bh->b_blocknr == num)
			get_bh(bh);
		else
			bh = NULL;
	}
	spin_unlock(&ip->i_spin);

	if (bh) {
		if (new)
			meta_prep_new(bh);
		else {
			error = gfs2_meta_reread(ip->i_sbd, bh,
						 DIO_START | DIO_WAIT);
			if (error) {
				brelse(bh);
				return error;
			}
		}
	} else {
		if (new)
			bh = gfs2_meta_new(ip->i_gl, num);
		else {
			error = gfs2_meta_read(ip->i_gl, num,
					       DIO_START | DIO_WAIT, &bh);
			if (error)
				return error;
		}

		spin_lock(&ip->i_spin);
		if (*bh_slot != bh) {
			brelse(*bh_slot);
			*bh_slot = bh;
			get_bh(bh);
		}
		spin_unlock(&ip->i_spin);
	}

	if (new) {
		if (gfs2_assert_warn(ip->i_sbd, height)) {
			brelse(bh);
			return -EIO;
		}
		gfs2_trans_add_bh(ip->i_gl, bh, 1);
		gfs2_metatype_set(bh, GFS2_METATYPE_IN, GFS2_FORMAT_IN);
		gfs2_buffer_clear_tail(bh, sizeof(struct gfs2_meta_header));

	} else if (gfs2_metatype_check(ip->i_sbd, bh,
			     (height) ? GFS2_METATYPE_IN : GFS2_METATYPE_DI)) {
		brelse(bh);
		return -EIO;
	}

	*bhp = bh;

	return 0;
}

/**
 * gfs2_meta_ra - start readahead on an extent of a file
 * @gl: the glock the blocks belong to
 * @dblock: the starting disk block
 * @extlen: the number of blocks in the extent
 *
 */

void gfs2_meta_ra(struct gfs2_glock *gl, uint64_t dblock, uint32_t extlen)
{
	struct gfs2_sbd *sdp = gl->gl_sbd;
	struct inode *aspace = gl->gl_aspace;
	struct buffer_head *first_bh, *bh;
	uint32_t max_ra = gfs2_tune_get(sdp, gt_max_readahead) >> sdp->sd_sb.sb_bsize_shift;
	int error;

	if (!extlen || !max_ra)
		return;
	if (extlen > max_ra)
		extlen = max_ra;

	first_bh = getbuf(sdp, aspace, dblock, CREATE);

	if (buffer_uptodate(first_bh))
		goto out;
	if (!buffer_locked(first_bh)) {
		error = gfs2_meta_reread(sdp, first_bh, DIO_START);
		if (error)
			goto out;
	}

	dblock++;
	extlen--;

	while (extlen) {
		bh = getbuf(sdp, aspace, dblock, CREATE);

		if (!buffer_uptodate(bh) && !buffer_locked(bh)) {
			error = gfs2_meta_reread(sdp, bh, DIO_START);
			brelse(bh);
			if (error)
				goto out;
		} else
			brelse(bh);

		dblock++;
		extlen--;

		if (buffer_uptodate(first_bh))
			break;
	}

 out:
	brelse(first_bh);
}

/**
 * gfs2_meta_syncfs - sync all the buffers in a filesystem
 * @sdp: the filesystem
 *
 */

void gfs2_meta_syncfs(struct gfs2_sbd *sdp)
{
	gfs2_log_flush(sdp);
	for (;;) {
		gfs2_ail1_start(sdp, DIO_ALL);
		if (gfs2_ail1_empty(sdp, DIO_ALL))
			break;
		msleep(100);
	}
}

