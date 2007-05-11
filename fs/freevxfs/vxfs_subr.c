/*
 * Copyright (c) 2000-2001 Christoph Hellwig.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions, and the following disclaimer,
 *    without modification.
 * 2. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * Alternatively, this software may be distributed under the terms of the
 * GNU General Public License ("GPL").
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE FOR
 * ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

/*
 * Veritas filesystem driver - shared subroutines.
 */
#include <linux/fs.h>
#include <linux/buffer_head.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/pagemap.h>

#include "vxfs_extern.h"


static int		vxfs_readpage(struct file *, struct page *);
static sector_t		vxfs_bmap(struct address_space *, sector_t);

const struct address_space_operations vxfs_aops = {
	.readpage =		vxfs_readpage,
	.bmap =			vxfs_bmap,
	.sync_page =		block_sync_page,
};

inline void
vxfs_put_page(struct page *pp)
{
	kunmap(pp);
	page_cache_release(pp);
}

/**
 * vxfs_get_page - read a page into memory.
 * @ip:		inode to read from
 * @n:		page number
 *
 * Description:
 *   vxfs_get_page reads the @n th page of @ip into the pagecache.
 *
 * Returns:
 *   The wanted page on success, else a NULL pointer.
 */
struct page *
vxfs_get_page(struct address_space *mapping, u_long n)
{
	struct page *			pp;

	pp = read_mapping_page(mapping, n, NULL);

	if (!IS_ERR(pp)) {
		kmap(pp);
		/** if (!PageChecked(pp)) **/
			/** vxfs_check_page(pp); **/
		if (PageError(pp))
			goto fail;
	}
	
	return (pp);
		 
fail:
	vxfs_put_page(pp);
	return ERR_PTR(-EIO);
}

/**
 * vxfs_bread - read buffer for a give inode,block tuple
 * @ip:		inode
 * @block:	logical block
 *
 * Description:
 *   The vxfs_bread function reads block no @block  of
 *   @ip into the buffercache.
 *
 * Returns:
 *   The resulting &struct buffer_head.
 */
struct buffer_head *
vxfs_bread(struct inode *ip, int block)
{
	struct buffer_head	*bp;
	daddr_t			pblock;

	pblock = vxfs_bmap1(ip, block);
	bp = sb_bread(ip->i_sb, pblock);

	return (bp);
}

/**
 * vxfs_get_block - locate buffer for given inode,block tuple 
 * @ip:		inode
 * @iblock:	logical block
 * @bp:		buffer skeleton
 * @create:	%TRUE if blocks may be newly allocated.
 *
 * Description:
 *   The vxfs_get_block function fills @bp with the right physical
 *   block and device number to perform a lowlevel read/write on
 *   it.
 *
 * Returns:
 *   Zero on success, else a negativ error code (-EIO).
 */
static int
vxfs_getblk(struct inode *ip, sector_t iblock,
	    struct buffer_head *bp, int create)
{
	daddr_t			pblock;

	pblock = vxfs_bmap1(ip, iblock);
	if (pblock != 0) {
		map_bh(bp, ip->i_sb, pblock);
		return 0;
	}

	return -EIO;
}

/**
 * vxfs_readpage - read one page synchronously into the pagecache
 * @file:	file context (unused)
 * @page:	page frame to fill in.
 *
 * Description:
 *   The vxfs_readpage routine reads @page synchronously into the
 *   pagecache.
 *
 * Returns:
 *   Zero on success, else a negative error code.
 *
 * Locking status:
 *   @page is locked and will be unlocked.
 */
static int
vxfs_readpage(struct file *file, struct page *page)
{
	return block_read_full_page(page, vxfs_getblk);
}
 
/**
 * vxfs_bmap - perform logical to physical block mapping
 * @mapping:	logical to physical mapping to use
 * @block:	logical block (relative to @mapping).
 *
 * Description:
 *   Vxfs_bmap find out the corresponding phsical block to the
 *   @mapping, @block pair.
 *
 * Returns:
 *   Physical block number on success, else Zero.
 *
 * Locking status:
 *   We are under the bkl.
 */
static sector_t
vxfs_bmap(struct address_space *mapping, sector_t block)
{
	return generic_block_bmap(mapping, block, vxfs_getblk);
}
