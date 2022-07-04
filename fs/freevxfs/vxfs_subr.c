// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2000-2001 Christoph Hellwig.
 */

/*
 * Veritas filesystem driver - shared subroutines.
 */
#include <linux/fs.h>
#include <linux/buffer_head.h>
#include <linux/kernel.h>
#include <linux/pagemap.h>

#include "vxfs_extern.h"


static int		vxfs_read_folio(struct file *, struct folio *);
static sector_t		vxfs_bmap(struct address_space *, sector_t);

const struct address_space_operations vxfs_aops = {
	.read_folio =		vxfs_read_folio,
	.bmap =			vxfs_bmap,
};

inline void
vxfs_put_page(struct page *pp)
{
	kunmap(pp);
	put_page(pp);
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
 * vxfs_read_folio - read one page synchronously into the pagecache
 * @file:	file context (unused)
 * @folio:	folio to fill in.
 *
 * Description:
 *   The vxfs_read_folio routine reads @folio synchronously into the
 *   pagecache.
 *
 * Returns:
 *   Zero on success, else a negative error code.
 *
 * Locking status:
 *   @folio is locked and will be unlocked.
 */
static int vxfs_read_folio(struct file *file, struct folio *folio)
{
	return block_read_full_folio(folio, vxfs_getblk);
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
