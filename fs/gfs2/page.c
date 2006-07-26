/*
 * Copyright (C) Sistina Software, Inc.  1997-2003 All rights reserved.
 * Copyright (C) 2004-2006 Red Hat, Inc.  All rights reserved.
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
#include <linux/pagemap.h>
#include <linux/mm.h>
#include <linux/gfs2_ondisk.h>

#include "gfs2.h"
#include "lm_interface.h"
#include "incore.h"
#include "bmap.h"
#include "inode.h"
#include "page.h"
#include "trans.h"
#include "ops_address.h"
#include "util.h"

/**
 * gfs2_pte_inval - Sync and invalidate all PTEs associated with a glock
 * @gl: the glock
 *
 */

void gfs2_pte_inval(struct gfs2_glock *gl)
{
	struct gfs2_inode *ip;
	struct inode *inode;

	ip = gl->gl_object;
	inode = &ip->i_inode;
	if (!ip || !S_ISREG(ip->i_di.di_mode))
		return;

	if (!test_bit(GIF_PAGED, &ip->i_flags))
		return;

	unmap_shared_mapping_range(inode->i_mapping, 0, 0);

	if (test_bit(GIF_SW_PAGED, &ip->i_flags))
		set_bit(GLF_DIRTY, &gl->gl_flags);

	clear_bit(GIF_SW_PAGED, &ip->i_flags);
}

/**
 * gfs2_page_inval - Invalidate all pages associated with a glock
 * @gl: the glock
 *
 */

void gfs2_page_inval(struct gfs2_glock *gl)
{
	struct gfs2_inode *ip;
	struct inode *inode;

	ip = gl->gl_object;
	inode = &ip->i_inode;
	if (!ip || !S_ISREG(ip->i_di.di_mode))
		return;

	truncate_inode_pages(inode->i_mapping, 0);
	gfs2_assert_withdraw(GFS2_SB(&ip->i_inode), !inode->i_mapping->nrpages);
	clear_bit(GIF_PAGED, &ip->i_flags);
}

/**
 * gfs2_page_sync - Sync the data pages (not metadata) associated with a glock
 * @gl: the glock
 * @flags: DIO_START | DIO_WAIT
 *
 * Syncs data (not metadata) for a regular file.
 * No-op for all other types.
 */

void gfs2_page_sync(struct gfs2_glock *gl, int flags)
{
	struct gfs2_inode *ip;
	struct inode *inode;
	struct address_space *mapping;
	int error = 0;

	ip = gl->gl_object;
	inode = &ip->i_inode;
	if (!ip || !S_ISREG(ip->i_di.di_mode))
		return;

	mapping = inode->i_mapping;

	if (flags & DIO_START)
		filemap_fdatawrite(mapping);
	if (!error && (flags & DIO_WAIT))
		error = filemap_fdatawait(mapping);

	/* Put back any errors cleared by filemap_fdatawait()
	   so they can be caught by someone who can pass them
	   up to user space. */

	if (error == -ENOSPC)
		set_bit(AS_ENOSPC, &mapping->flags);
	else if (error)
		set_bit(AS_EIO, &mapping->flags);

}

/**
 * gfs2_block_truncate_page - Deal with zeroing out data for truncate
 *
 * This is partly borrowed from ext3.
 */
int gfs2_block_truncate_page(struct address_space *mapping)
{
	struct inode *inode = mapping->host;
	struct gfs2_inode *ip = GFS2_I(inode);
	struct gfs2_sbd *sdp = GFS2_SB(inode);
	loff_t from = inode->i_size;
	unsigned long index = from >> PAGE_CACHE_SHIFT;
	unsigned offset = from & (PAGE_CACHE_SIZE-1);
	unsigned blocksize, iblock, length, pos;
	struct buffer_head *bh;
	struct page *page;
	void *kaddr;
	int err;

	page = grab_cache_page(mapping, index);
	if (!page)
		return 0;

	blocksize = inode->i_sb->s_blocksize;
	length = blocksize - (offset & (blocksize - 1));
	iblock = index << (PAGE_CACHE_SHIFT - inode->i_sb->s_blocksize_bits);

	if (!page_has_buffers(page))
		create_empty_buffers(page, blocksize, 0);

	/* Find the buffer that contains "offset" */
	bh = page_buffers(page);
	pos = blocksize;
	while (offset >= pos) {
		bh = bh->b_this_page;
		iblock++;
		pos += blocksize;
	}

	err = 0;

	if (!buffer_mapped(bh)) {
		gfs2_get_block(inode, iblock, bh, 0);
		/* unmapped? It's a hole - nothing to do */
		if (!buffer_mapped(bh))
			goto unlock;
	}

	/* Ok, it's mapped. Make sure it's up-to-date */
	if (PageUptodate(page))
		set_buffer_uptodate(bh);

	if (!buffer_uptodate(bh)) {
		err = -EIO;
		ll_rw_block(READ, 1, &bh);
		wait_on_buffer(bh);
		/* Uhhuh. Read error. Complain and punt. */
		if (!buffer_uptodate(bh))
			goto unlock;
	}

	if (sdp->sd_args.ar_data == GFS2_DATA_ORDERED || gfs2_is_jdata(ip))
		gfs2_trans_add_bh(ip->i_gl, bh, 0);

	kaddr = kmap_atomic(page, KM_USER0);
	memset(kaddr + offset, 0, length);
	flush_dcache_page(page);
	kunmap_atomic(kaddr, KM_USER0);

unlock:
	unlock_page(page);
	page_cache_release(page);
	return err;
}

void gfs2_page_add_databufs(struct gfs2_inode *ip, struct page *page,
			    unsigned int from, unsigned int to)
{
	struct buffer_head *head = page_buffers(page);
	unsigned int bsize = head->b_size;
	struct buffer_head *bh;
	unsigned int start, end;

	for (bh = head, start = 0; bh != head || !start;
	     bh = bh->b_this_page, start = end) {
		end = start + bsize;
		if (end <= from || start >= to)
			continue;
		gfs2_trans_add_bh(ip->i_gl, bh, 0);
	}
}

