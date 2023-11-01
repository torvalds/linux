// SPDX-License-Identifier: GPL-2.0-only
/*
 * inode.c
 *
 * PURPOSE
 *  Inode handling routines for the OSTA-UDF(tm) filesystem.
 *
 * COPYRIGHT
 *  (C) 1998 Dave Boynton
 *  (C) 1998-2004 Ben Fennema
 *  (C) 1999-2000 Stelias Computing Inc
 *
 * HISTORY
 *
 *  10/04/98 dgb  Added rudimentary directory functions
 *  10/07/98      Fully working udf_block_map! It works!
 *  11/25/98      bmap altered to better support extents
 *  12/06/98 blf  partition support in udf_iget, udf_block_map
 *                and udf_read_inode
 *  12/12/98      rewrote udf_block_map to handle next extents and descs across
 *                block boundaries (which is not actually allowed)
 *  12/20/98      added support for strategy 4096
 *  03/07/99      rewrote udf_block_map (again)
 *                New funcs, inode_bmap, udf_next_aext
 *  04/19/99      Support for writing device EA's for major/minor #
 */

#include "udfdecl.h"
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/pagemap.h>
#include <linux/writeback.h>
#include <linux/slab.h>
#include <linux/crc-itu-t.h>
#include <linux/mpage.h>
#include <linux/uio.h>
#include <linux/bio.h>

#include "udf_i.h"
#include "udf_sb.h"

#define EXTENT_MERGE_SIZE 5

#define FE_MAPPED_PERMS	(FE_PERM_U_READ | FE_PERM_U_WRITE | FE_PERM_U_EXEC | \
			 FE_PERM_G_READ | FE_PERM_G_WRITE | FE_PERM_G_EXEC | \
			 FE_PERM_O_READ | FE_PERM_O_WRITE | FE_PERM_O_EXEC)

#define FE_DELETE_PERMS	(FE_PERM_U_DELETE | FE_PERM_G_DELETE | \
			 FE_PERM_O_DELETE)

struct udf_map_rq;

static umode_t udf_convert_permissions(struct fileEntry *);
static int udf_update_inode(struct inode *, int);
static int udf_sync_inode(struct inode *inode);
static int udf_alloc_i_data(struct inode *inode, size_t size);
static int inode_getblk(struct inode *inode, struct udf_map_rq *map);
static int udf_insert_aext(struct inode *, struct extent_position,
			   struct kernel_lb_addr, uint32_t);
static void udf_split_extents(struct inode *, int *, int, udf_pblk_t,
			      struct kernel_long_ad *, int *);
static void udf_prealloc_extents(struct inode *, int, int,
				 struct kernel_long_ad *, int *);
static void udf_merge_extents(struct inode *, struct kernel_long_ad *, int *);
static int udf_update_extents(struct inode *, struct kernel_long_ad *, int,
			      int, struct extent_position *);
static int udf_get_block_wb(struct inode *inode, sector_t block,
			    struct buffer_head *bh_result, int create);

static void __udf_clear_extent_cache(struct inode *inode)
{
	struct udf_inode_info *iinfo = UDF_I(inode);

	if (iinfo->cached_extent.lstart != -1) {
		brelse(iinfo->cached_extent.epos.bh);
		iinfo->cached_extent.lstart = -1;
	}
}

/* Invalidate extent cache */
static void udf_clear_extent_cache(struct inode *inode)
{
	struct udf_inode_info *iinfo = UDF_I(inode);

	spin_lock(&iinfo->i_extent_cache_lock);
	__udf_clear_extent_cache(inode);
	spin_unlock(&iinfo->i_extent_cache_lock);
}

/* Return contents of extent cache */
static int udf_read_extent_cache(struct inode *inode, loff_t bcount,
				 loff_t *lbcount, struct extent_position *pos)
{
	struct udf_inode_info *iinfo = UDF_I(inode);
	int ret = 0;

	spin_lock(&iinfo->i_extent_cache_lock);
	if ((iinfo->cached_extent.lstart <= bcount) &&
	    (iinfo->cached_extent.lstart != -1)) {
		/* Cache hit */
		*lbcount = iinfo->cached_extent.lstart;
		memcpy(pos, &iinfo->cached_extent.epos,
		       sizeof(struct extent_position));
		if (pos->bh)
			get_bh(pos->bh);
		ret = 1;
	}
	spin_unlock(&iinfo->i_extent_cache_lock);
	return ret;
}

/* Add extent to extent cache */
static void udf_update_extent_cache(struct inode *inode, loff_t estart,
				    struct extent_position *pos)
{
	struct udf_inode_info *iinfo = UDF_I(inode);

	spin_lock(&iinfo->i_extent_cache_lock);
	/* Invalidate previously cached extent */
	__udf_clear_extent_cache(inode);
	if (pos->bh)
		get_bh(pos->bh);
	memcpy(&iinfo->cached_extent.epos, pos, sizeof(*pos));
	iinfo->cached_extent.lstart = estart;
	switch (iinfo->i_alloc_type) {
	case ICBTAG_FLAG_AD_SHORT:
		iinfo->cached_extent.epos.offset -= sizeof(struct short_ad);
		break;
	case ICBTAG_FLAG_AD_LONG:
		iinfo->cached_extent.epos.offset -= sizeof(struct long_ad);
		break;
	}
	spin_unlock(&iinfo->i_extent_cache_lock);
}

void udf_evict_inode(struct inode *inode)
{
	struct udf_inode_info *iinfo = UDF_I(inode);
	int want_delete = 0;

	if (!is_bad_inode(inode)) {
		if (!inode->i_nlink) {
			want_delete = 1;
			udf_setsize(inode, 0);
			udf_update_inode(inode, IS_SYNC(inode));
		}
		if (iinfo->i_alloc_type != ICBTAG_FLAG_AD_IN_ICB &&
		    inode->i_size != iinfo->i_lenExtents) {
			udf_warn(inode->i_sb,
				 "Inode %lu (mode %o) has inode size %llu different from extent length %llu. Filesystem need not be standards compliant.\n",
				 inode->i_ino, inode->i_mode,
				 (unsigned long long)inode->i_size,
				 (unsigned long long)iinfo->i_lenExtents);
		}
	}
	truncate_inode_pages_final(&inode->i_data);
	invalidate_inode_buffers(inode);
	clear_inode(inode);
	kfree(iinfo->i_data);
	iinfo->i_data = NULL;
	udf_clear_extent_cache(inode);
	if (want_delete) {
		udf_free_inode(inode);
	}
}

static void udf_write_failed(struct address_space *mapping, loff_t to)
{
	struct inode *inode = mapping->host;
	struct udf_inode_info *iinfo = UDF_I(inode);
	loff_t isize = inode->i_size;

	if (to > isize) {
		truncate_pagecache(inode, isize);
		if (iinfo->i_alloc_type != ICBTAG_FLAG_AD_IN_ICB) {
			down_write(&iinfo->i_data_sem);
			udf_clear_extent_cache(inode);
			udf_truncate_extents(inode);
			up_write(&iinfo->i_data_sem);
		}
	}
}

static int udf_adinicb_writepage(struct folio *folio,
				 struct writeback_control *wbc, void *data)
{
	struct inode *inode = folio->mapping->host;
	struct udf_inode_info *iinfo = UDF_I(inode);

	BUG_ON(!folio_test_locked(folio));
	BUG_ON(folio->index != 0);
	memcpy_from_file_folio(iinfo->i_data + iinfo->i_lenEAttr, folio, 0,
		       i_size_read(inode));
	folio_unlock(folio);
	mark_inode_dirty(inode);

	return 0;
}

static int udf_writepages(struct address_space *mapping,
			  struct writeback_control *wbc)
{
	struct inode *inode = mapping->host;
	struct udf_inode_info *iinfo = UDF_I(inode);

	if (iinfo->i_alloc_type != ICBTAG_FLAG_AD_IN_ICB)
		return mpage_writepages(mapping, wbc, udf_get_block_wb);
	return write_cache_pages(mapping, wbc, udf_adinicb_writepage, NULL);
}

static void udf_adinicb_readpage(struct page *page)
{
	struct inode *inode = page->mapping->host;
	char *kaddr;
	struct udf_inode_info *iinfo = UDF_I(inode);
	loff_t isize = i_size_read(inode);

	kaddr = kmap_local_page(page);
	memcpy(kaddr, iinfo->i_data + iinfo->i_lenEAttr, isize);
	memset(kaddr + isize, 0, PAGE_SIZE - isize);
	flush_dcache_page(page);
	SetPageUptodate(page);
	kunmap_local(kaddr);
}

static int udf_read_folio(struct file *file, struct folio *folio)
{
	struct udf_inode_info *iinfo = UDF_I(file_inode(file));

	if (iinfo->i_alloc_type == ICBTAG_FLAG_AD_IN_ICB) {
		udf_adinicb_readpage(&folio->page);
		folio_unlock(folio);
		return 0;
	}
	return mpage_read_folio(folio, udf_get_block);
}

static void udf_readahead(struct readahead_control *rac)
{
	struct udf_inode_info *iinfo = UDF_I(rac->mapping->host);

	/*
	 * No readahead needed for in-ICB files and udf_get_block() would get
	 * confused for such file anyway.
	 */
	if (iinfo->i_alloc_type == ICBTAG_FLAG_AD_IN_ICB)
		return;

	mpage_readahead(rac, udf_get_block);
}

static int udf_write_begin(struct file *file, struct address_space *mapping,
			   loff_t pos, unsigned len,
			   struct page **pagep, void **fsdata)
{
	struct udf_inode_info *iinfo = UDF_I(file_inode(file));
	struct page *page;
	int ret;

	if (iinfo->i_alloc_type != ICBTAG_FLAG_AD_IN_ICB) {
		ret = block_write_begin(mapping, pos, len, pagep,
					udf_get_block);
		if (unlikely(ret))
			udf_write_failed(mapping, pos + len);
		return ret;
	}
	if (WARN_ON_ONCE(pos >= PAGE_SIZE))
		return -EIO;
	page = grab_cache_page_write_begin(mapping, 0);
	if (!page)
		return -ENOMEM;
	*pagep = page;
	if (!PageUptodate(page))
		udf_adinicb_readpage(page);
	return 0;
}

static int udf_write_end(struct file *file, struct address_space *mapping,
			 loff_t pos, unsigned len, unsigned copied,
			 struct page *page, void *fsdata)
{
	struct inode *inode = file_inode(file);
	loff_t last_pos;

	if (UDF_I(inode)->i_alloc_type != ICBTAG_FLAG_AD_IN_ICB)
		return generic_write_end(file, mapping, pos, len, copied, page,
					 fsdata);
	last_pos = pos + copied;
	if (last_pos > inode->i_size)
		i_size_write(inode, last_pos);
	set_page_dirty(page);
	unlock_page(page);
	put_page(page);

	return copied;
}

static ssize_t udf_direct_IO(struct kiocb *iocb, struct iov_iter *iter)
{
	struct file *file = iocb->ki_filp;
	struct address_space *mapping = file->f_mapping;
	struct inode *inode = mapping->host;
	size_t count = iov_iter_count(iter);
	ssize_t ret;

	/* Fallback to buffered IO for in-ICB files */
	if (UDF_I(inode)->i_alloc_type == ICBTAG_FLAG_AD_IN_ICB)
		return 0;
	ret = blockdev_direct_IO(iocb, inode, iter, udf_get_block);
	if (unlikely(ret < 0 && iov_iter_rw(iter) == WRITE))
		udf_write_failed(mapping, iocb->ki_pos + count);
	return ret;
}

static sector_t udf_bmap(struct address_space *mapping, sector_t block)
{
	struct udf_inode_info *iinfo = UDF_I(mapping->host);

	if (iinfo->i_alloc_type == ICBTAG_FLAG_AD_IN_ICB)
		return -EINVAL;
	return generic_block_bmap(mapping, block, udf_get_block);
}

const struct address_space_operations udf_aops = {
	.dirty_folio	= block_dirty_folio,
	.invalidate_folio = block_invalidate_folio,
	.read_folio	= udf_read_folio,
	.readahead	= udf_readahead,
	.writepages	= udf_writepages,
	.write_begin	= udf_write_begin,
	.write_end	= udf_write_end,
	.direct_IO	= udf_direct_IO,
	.bmap		= udf_bmap,
	.migrate_folio	= buffer_migrate_folio,
};

/*
 * Expand file stored in ICB to a normal one-block-file
 *
 * This function requires i_mutex held
 */
int udf_expand_file_adinicb(struct inode *inode)
{
	struct page *page;
	struct udf_inode_info *iinfo = UDF_I(inode);
	int err;

	WARN_ON_ONCE(!inode_is_locked(inode));
	if (!iinfo->i_lenAlloc) {
		down_write(&iinfo->i_data_sem);
		if (UDF_QUERY_FLAG(inode->i_sb, UDF_FLAG_USE_SHORT_AD))
			iinfo->i_alloc_type = ICBTAG_FLAG_AD_SHORT;
		else
			iinfo->i_alloc_type = ICBTAG_FLAG_AD_LONG;
		up_write(&iinfo->i_data_sem);
		mark_inode_dirty(inode);
		return 0;
	}

	page = find_or_create_page(inode->i_mapping, 0, GFP_NOFS);
	if (!page)
		return -ENOMEM;

	if (!PageUptodate(page))
		udf_adinicb_readpage(page);
	down_write(&iinfo->i_data_sem);
	memset(iinfo->i_data + iinfo->i_lenEAttr, 0x00,
	       iinfo->i_lenAlloc);
	iinfo->i_lenAlloc = 0;
	if (UDF_QUERY_FLAG(inode->i_sb, UDF_FLAG_USE_SHORT_AD))
		iinfo->i_alloc_type = ICBTAG_FLAG_AD_SHORT;
	else
		iinfo->i_alloc_type = ICBTAG_FLAG_AD_LONG;
	set_page_dirty(page);
	unlock_page(page);
	up_write(&iinfo->i_data_sem);
	err = filemap_fdatawrite(inode->i_mapping);
	if (err) {
		/* Restore everything back so that we don't lose data... */
		lock_page(page);
		down_write(&iinfo->i_data_sem);
		memcpy_to_page(page, 0, iinfo->i_data + iinfo->i_lenEAttr,
			       inode->i_size);
		unlock_page(page);
		iinfo->i_alloc_type = ICBTAG_FLAG_AD_IN_ICB;
		iinfo->i_lenAlloc = inode->i_size;
		up_write(&iinfo->i_data_sem);
	}
	put_page(page);
	mark_inode_dirty(inode);

	return err;
}

#define UDF_MAP_CREATE		0x01	/* Mapping can allocate new blocks */
#define UDF_MAP_NOPREALLOC	0x02	/* Do not preallocate blocks */

#define UDF_BLK_MAPPED	0x01	/* Block was successfully mapped */
#define UDF_BLK_NEW	0x02	/* Block was freshly allocated */

struct udf_map_rq {
	sector_t lblk;
	udf_pblk_t pblk;
	int iflags;		/* UDF_MAP_ flags determining behavior */
	int oflags;		/* UDF_BLK_ flags reporting results */
};

static int udf_map_block(struct inode *inode, struct udf_map_rq *map)
{
	int err;
	struct udf_inode_info *iinfo = UDF_I(inode);

	if (WARN_ON_ONCE(iinfo->i_alloc_type == ICBTAG_FLAG_AD_IN_ICB))
		return -EFSCORRUPTED;

	map->oflags = 0;
	if (!(map->iflags & UDF_MAP_CREATE)) {
		struct kernel_lb_addr eloc;
		uint32_t elen;
		sector_t offset;
		struct extent_position epos = {};

		down_read(&iinfo->i_data_sem);
		if (inode_bmap(inode, map->lblk, &epos, &eloc, &elen, &offset)
				== (EXT_RECORDED_ALLOCATED >> 30)) {
			map->pblk = udf_get_lb_pblock(inode->i_sb, &eloc,
							offset);
			map->oflags |= UDF_BLK_MAPPED;
		}
		up_read(&iinfo->i_data_sem);
		brelse(epos.bh);

		return 0;
	}

	down_write(&iinfo->i_data_sem);
	/*
	 * Block beyond EOF and prealloc extents? Just discard preallocation
	 * as it is not useful and complicates things.
	 */
	if (((loff_t)map->lblk) << inode->i_blkbits >= iinfo->i_lenExtents)
		udf_discard_prealloc(inode);
	udf_clear_extent_cache(inode);
	err = inode_getblk(inode, map);
	up_write(&iinfo->i_data_sem);
	return err;
}

static int __udf_get_block(struct inode *inode, sector_t block,
			   struct buffer_head *bh_result, int flags)
{
	int err;
	struct udf_map_rq map = {
		.lblk = block,
		.iflags = flags,
	};

	err = udf_map_block(inode, &map);
	if (err < 0)
		return err;
	if (map.oflags & UDF_BLK_MAPPED) {
		map_bh(bh_result, inode->i_sb, map.pblk);
		if (map.oflags & UDF_BLK_NEW)
			set_buffer_new(bh_result);
	}
	return 0;
}

int udf_get_block(struct inode *inode, sector_t block,
		  struct buffer_head *bh_result, int create)
{
	int flags = create ? UDF_MAP_CREATE : 0;

	/*
	 * We preallocate blocks only for regular files. It also makes sense
	 * for directories but there's a problem when to drop the
	 * preallocation. We might use some delayed work for that but I feel
	 * it's overengineering for a filesystem like UDF.
	 */
	if (!S_ISREG(inode->i_mode))
		flags |= UDF_MAP_NOPREALLOC;
	return __udf_get_block(inode, block, bh_result, flags);
}

/*
 * We shouldn't be allocating blocks on page writeback since we allocate them
 * on page fault. We can spot dirty buffers without allocated blocks though
 * when truncate expands file. These however don't have valid data so we can
 * safely ignore them. So never allocate blocks from page writeback.
 */
static int udf_get_block_wb(struct inode *inode, sector_t block,
			    struct buffer_head *bh_result, int create)
{
	return __udf_get_block(inode, block, bh_result, 0);
}

/* Extend the file with new blocks totaling 'new_block_bytes',
 * return the number of extents added
 */
static int udf_do_extend_file(struct inode *inode,
			      struct extent_position *last_pos,
			      struct kernel_long_ad *last_ext,
			      loff_t new_block_bytes)
{
	uint32_t add;
	int count = 0, fake = !(last_ext->extLength & UDF_EXTENT_LENGTH_MASK);
	struct super_block *sb = inode->i_sb;
	struct udf_inode_info *iinfo;
	int err;

	/* The previous extent is fake and we should not extend by anything
	 * - there's nothing to do... */
	if (!new_block_bytes && fake)
		return 0;

	iinfo = UDF_I(inode);
	/* Round the last extent up to a multiple of block size */
	if (last_ext->extLength & (sb->s_blocksize - 1)) {
		last_ext->extLength =
			(last_ext->extLength & UDF_EXTENT_FLAG_MASK) |
			(((last_ext->extLength & UDF_EXTENT_LENGTH_MASK) +
			  sb->s_blocksize - 1) & ~(sb->s_blocksize - 1));
		iinfo->i_lenExtents =
			(iinfo->i_lenExtents + sb->s_blocksize - 1) &
			~(sb->s_blocksize - 1);
	}

	add = 0;
	/* Can we merge with the previous extent? */
	if ((last_ext->extLength & UDF_EXTENT_FLAG_MASK) ==
					EXT_NOT_RECORDED_NOT_ALLOCATED) {
		add = (1 << 30) - sb->s_blocksize -
			(last_ext->extLength & UDF_EXTENT_LENGTH_MASK);
		if (add > new_block_bytes)
			add = new_block_bytes;
		new_block_bytes -= add;
		last_ext->extLength += add;
	}

	if (fake) {
		err = udf_add_aext(inode, last_pos, &last_ext->extLocation,
				   last_ext->extLength, 1);
		if (err < 0)
			goto out_err;
		count++;
	} else {
		struct kernel_lb_addr tmploc;
		uint32_t tmplen;

		udf_write_aext(inode, last_pos, &last_ext->extLocation,
				last_ext->extLength, 1);

		/*
		 * We've rewritten the last extent. If we are going to add
		 * more extents, we may need to enter possible following
		 * empty indirect extent.
		 */
		if (new_block_bytes)
			udf_next_aext(inode, last_pos, &tmploc, &tmplen, 0);
	}
	iinfo->i_lenExtents += add;

	/* Managed to do everything necessary? */
	if (!new_block_bytes)
		goto out;

	/* All further extents will be NOT_RECORDED_NOT_ALLOCATED */
	last_ext->extLocation.logicalBlockNum = 0;
	last_ext->extLocation.partitionReferenceNum = 0;
	add = (1 << 30) - sb->s_blocksize;
	last_ext->extLength = EXT_NOT_RECORDED_NOT_ALLOCATED | add;

	/* Create enough extents to cover the whole hole */
	while (new_block_bytes > add) {
		new_block_bytes -= add;
		err = udf_add_aext(inode, last_pos, &last_ext->extLocation,
				   last_ext->extLength, 1);
		if (err)
			goto out_err;
		iinfo->i_lenExtents += add;
		count++;
	}
	if (new_block_bytes) {
		last_ext->extLength = EXT_NOT_RECORDED_NOT_ALLOCATED |
			new_block_bytes;
		err = udf_add_aext(inode, last_pos, &last_ext->extLocation,
				   last_ext->extLength, 1);
		if (err)
			goto out_err;
		iinfo->i_lenExtents += new_block_bytes;
		count++;
	}

out:
	/* last_pos should point to the last written extent... */
	if (iinfo->i_alloc_type == ICBTAG_FLAG_AD_SHORT)
		last_pos->offset -= sizeof(struct short_ad);
	else if (iinfo->i_alloc_type == ICBTAG_FLAG_AD_LONG)
		last_pos->offset -= sizeof(struct long_ad);
	else
		return -EIO;

	return count;
out_err:
	/* Remove extents we've created so far */
	udf_clear_extent_cache(inode);
	udf_truncate_extents(inode);
	return err;
}

/* Extend the final block of the file to final_block_len bytes */
static void udf_do_extend_final_block(struct inode *inode,
				      struct extent_position *last_pos,
				      struct kernel_long_ad *last_ext,
				      uint32_t new_elen)
{
	uint32_t added_bytes;

	/*
	 * Extent already large enough? It may be already rounded up to block
	 * size...
	 */
	if (new_elen <= (last_ext->extLength & UDF_EXTENT_LENGTH_MASK))
		return;
	added_bytes = new_elen - (last_ext->extLength & UDF_EXTENT_LENGTH_MASK);
	last_ext->extLength += added_bytes;
	UDF_I(inode)->i_lenExtents += added_bytes;

	udf_write_aext(inode, last_pos, &last_ext->extLocation,
			last_ext->extLength, 1);
}

static int udf_extend_file(struct inode *inode, loff_t newsize)
{

	struct extent_position epos;
	struct kernel_lb_addr eloc;
	uint32_t elen;
	int8_t etype;
	struct super_block *sb = inode->i_sb;
	sector_t first_block = newsize >> sb->s_blocksize_bits, offset;
	loff_t new_elen;
	int adsize;
	struct udf_inode_info *iinfo = UDF_I(inode);
	struct kernel_long_ad extent;
	int err = 0;
	bool within_last_ext;

	if (iinfo->i_alloc_type == ICBTAG_FLAG_AD_SHORT)
		adsize = sizeof(struct short_ad);
	else if (iinfo->i_alloc_type == ICBTAG_FLAG_AD_LONG)
		adsize = sizeof(struct long_ad);
	else
		BUG();

	down_write(&iinfo->i_data_sem);
	/*
	 * When creating hole in file, just don't bother with preserving
	 * preallocation. It likely won't be very useful anyway.
	 */
	udf_discard_prealloc(inode);

	etype = inode_bmap(inode, first_block, &epos, &eloc, &elen, &offset);
	within_last_ext = (etype != -1);
	/* We don't expect extents past EOF... */
	WARN_ON_ONCE(within_last_ext &&
		     elen > ((loff_t)offset + 1) << inode->i_blkbits);

	if ((!epos.bh && epos.offset == udf_file_entry_alloc_offset(inode)) ||
	    (epos.bh && epos.offset == sizeof(struct allocExtDesc))) {
		/* File has no extents at all or has empty last
		 * indirect extent! Create a fake extent... */
		extent.extLocation.logicalBlockNum = 0;
		extent.extLocation.partitionReferenceNum = 0;
		extent.extLength = EXT_NOT_RECORDED_NOT_ALLOCATED;
	} else {
		epos.offset -= adsize;
		etype = udf_next_aext(inode, &epos, &extent.extLocation,
				      &extent.extLength, 0);
		extent.extLength |= etype << 30;
	}

	new_elen = ((loff_t)offset << inode->i_blkbits) |
					(newsize & (sb->s_blocksize - 1));

	/* File has extent covering the new size (could happen when extending
	 * inside a block)?
	 */
	if (within_last_ext) {
		/* Extending file within the last file block */
		udf_do_extend_final_block(inode, &epos, &extent, new_elen);
	} else {
		err = udf_do_extend_file(inode, &epos, &extent, new_elen);
	}

	if (err < 0)
		goto out;
	err = 0;
out:
	brelse(epos.bh);
	up_write(&iinfo->i_data_sem);
	return err;
}

static int inode_getblk(struct inode *inode, struct udf_map_rq *map)
{
	struct kernel_long_ad laarr[EXTENT_MERGE_SIZE];
	struct extent_position prev_epos, cur_epos, next_epos;
	int count = 0, startnum = 0, endnum = 0;
	uint32_t elen = 0, tmpelen;
	struct kernel_lb_addr eloc, tmpeloc;
	int c = 1;
	loff_t lbcount = 0, b_off = 0;
	udf_pblk_t newblocknum;
	sector_t offset = 0;
	int8_t etype;
	struct udf_inode_info *iinfo = UDF_I(inode);
	udf_pblk_t goal = 0, pgoal = iinfo->i_location.logicalBlockNum;
	int lastblock = 0;
	bool isBeyondEOF;
	int ret = 0;

	prev_epos.offset = udf_file_entry_alloc_offset(inode);
	prev_epos.block = iinfo->i_location;
	prev_epos.bh = NULL;
	cur_epos = next_epos = prev_epos;
	b_off = (loff_t)map->lblk << inode->i_sb->s_blocksize_bits;

	/* find the extent which contains the block we are looking for.
	   alternate between laarr[0] and laarr[1] for locations of the
	   current extent, and the previous extent */
	do {
		if (prev_epos.bh != cur_epos.bh) {
			brelse(prev_epos.bh);
			get_bh(cur_epos.bh);
			prev_epos.bh = cur_epos.bh;
		}
		if (cur_epos.bh != next_epos.bh) {
			brelse(cur_epos.bh);
			get_bh(next_epos.bh);
			cur_epos.bh = next_epos.bh;
		}

		lbcount += elen;

		prev_epos.block = cur_epos.block;
		cur_epos.block = next_epos.block;

		prev_epos.offset = cur_epos.offset;
		cur_epos.offset = next_epos.offset;

		etype = udf_next_aext(inode, &next_epos, &eloc, &elen, 1);
		if (etype == -1)
			break;

		c = !c;

		laarr[c].extLength = (etype << 30) | elen;
		laarr[c].extLocation = eloc;

		if (etype != (EXT_NOT_RECORDED_NOT_ALLOCATED >> 30))
			pgoal = eloc.logicalBlockNum +
				((elen + inode->i_sb->s_blocksize - 1) >>
				 inode->i_sb->s_blocksize_bits);

		count++;
	} while (lbcount + elen <= b_off);

	b_off -= lbcount;
	offset = b_off >> inode->i_sb->s_blocksize_bits;
	/*
	 * Move prev_epos and cur_epos into indirect extent if we are at
	 * the pointer to it
	 */
	udf_next_aext(inode, &prev_epos, &tmpeloc, &tmpelen, 0);
	udf_next_aext(inode, &cur_epos, &tmpeloc, &tmpelen, 0);

	/* if the extent is allocated and recorded, return the block
	   if the extent is not a multiple of the blocksize, round up */

	if (etype == (EXT_RECORDED_ALLOCATED >> 30)) {
		if (elen & (inode->i_sb->s_blocksize - 1)) {
			elen = EXT_RECORDED_ALLOCATED |
				((elen + inode->i_sb->s_blocksize - 1) &
				 ~(inode->i_sb->s_blocksize - 1));
			iinfo->i_lenExtents =
				ALIGN(iinfo->i_lenExtents,
				      inode->i_sb->s_blocksize);
			udf_write_aext(inode, &cur_epos, &eloc, elen, 1);
		}
		map->oflags = UDF_BLK_MAPPED;
		map->pblk = udf_get_lb_pblock(inode->i_sb, &eloc, offset);
		goto out_free;
	}

	/* Are we beyond EOF and preallocated extent? */
	if (etype == -1) {
		loff_t hole_len;

		isBeyondEOF = true;
		if (count) {
			if (c)
				laarr[0] = laarr[1];
			startnum = 1;
		} else {
			/* Create a fake extent when there's not one */
			memset(&laarr[0].extLocation, 0x00,
				sizeof(struct kernel_lb_addr));
			laarr[0].extLength = EXT_NOT_RECORDED_NOT_ALLOCATED;
			/* Will udf_do_extend_file() create real extent from
			   a fake one? */
			startnum = (offset > 0);
		}
		/* Create extents for the hole between EOF and offset */
		hole_len = (loff_t)offset << inode->i_blkbits;
		ret = udf_do_extend_file(inode, &prev_epos, laarr, hole_len);
		if (ret < 0)
			goto out_free;
		c = 0;
		offset = 0;
		count += ret;
		/*
		 * Is there any real extent? - otherwise we overwrite the fake
		 * one...
		 */
		if (count)
			c = !c;
		laarr[c].extLength = EXT_NOT_RECORDED_NOT_ALLOCATED |
			inode->i_sb->s_blocksize;
		memset(&laarr[c].extLocation, 0x00,
			sizeof(struct kernel_lb_addr));
		count++;
		endnum = c + 1;
		lastblock = 1;
	} else {
		isBeyondEOF = false;
		endnum = startnum = ((count > 2) ? 2 : count);

		/* if the current extent is in position 0,
		   swap it with the previous */
		if (!c && count != 1) {
			laarr[2] = laarr[0];
			laarr[0] = laarr[1];
			laarr[1] = laarr[2];
			c = 1;
		}

		/* if the current block is located in an extent,
		   read the next extent */
		etype = udf_next_aext(inode, &next_epos, &eloc, &elen, 0);
		if (etype != -1) {
			laarr[c + 1].extLength = (etype << 30) | elen;
			laarr[c + 1].extLocation = eloc;
			count++;
			startnum++;
			endnum++;
		} else
			lastblock = 1;
	}

	/* if the current extent is not recorded but allocated, get the
	 * block in the extent corresponding to the requested block */
	if ((laarr[c].extLength >> 30) == (EXT_NOT_RECORDED_ALLOCATED >> 30))
		newblocknum = laarr[c].extLocation.logicalBlockNum + offset;
	else { /* otherwise, allocate a new block */
		if (iinfo->i_next_alloc_block == map->lblk)
			goal = iinfo->i_next_alloc_goal;

		if (!goal) {
			if (!(goal = pgoal)) /* XXX: what was intended here? */
				goal = iinfo->i_location.logicalBlockNum + 1;
		}

		newblocknum = udf_new_block(inode->i_sb, inode,
				iinfo->i_location.partitionReferenceNum,
				goal, &ret);
		if (!newblocknum)
			goto out_free;
		if (isBeyondEOF)
			iinfo->i_lenExtents += inode->i_sb->s_blocksize;
	}

	/* if the extent the requsted block is located in contains multiple
	 * blocks, split the extent into at most three extents. blocks prior
	 * to requested block, requested block, and blocks after requested
	 * block */
	udf_split_extents(inode, &c, offset, newblocknum, laarr, &endnum);

	if (!(map->iflags & UDF_MAP_NOPREALLOC))
		udf_prealloc_extents(inode, c, lastblock, laarr, &endnum);

	/* merge any continuous blocks in laarr */
	udf_merge_extents(inode, laarr, &endnum);

	/* write back the new extents, inserting new extents if the new number
	 * of extents is greater than the old number, and deleting extents if
	 * the new number of extents is less than the old number */
	ret = udf_update_extents(inode, laarr, startnum, endnum, &prev_epos);
	if (ret < 0)
		goto out_free;

	map->pblk = udf_get_pblock(inode->i_sb, newblocknum,
				iinfo->i_location.partitionReferenceNum, 0);
	if (!map->pblk) {
		ret = -EFSCORRUPTED;
		goto out_free;
	}
	map->oflags = UDF_BLK_NEW | UDF_BLK_MAPPED;
	iinfo->i_next_alloc_block = map->lblk + 1;
	iinfo->i_next_alloc_goal = newblocknum + 1;
	inode_set_ctime_current(inode);

	if (IS_SYNC(inode))
		udf_sync_inode(inode);
	else
		mark_inode_dirty(inode);
	ret = 0;
out_free:
	brelse(prev_epos.bh);
	brelse(cur_epos.bh);
	brelse(next_epos.bh);
	return ret;
}

static void udf_split_extents(struct inode *inode, int *c, int offset,
			       udf_pblk_t newblocknum,
			       struct kernel_long_ad *laarr, int *endnum)
{
	unsigned long blocksize = inode->i_sb->s_blocksize;
	unsigned char blocksize_bits = inode->i_sb->s_blocksize_bits;

	if ((laarr[*c].extLength >> 30) == (EXT_NOT_RECORDED_ALLOCATED >> 30) ||
	    (laarr[*c].extLength >> 30) ==
				(EXT_NOT_RECORDED_NOT_ALLOCATED >> 30)) {
		int curr = *c;
		int blen = ((laarr[curr].extLength & UDF_EXTENT_LENGTH_MASK) +
			    blocksize - 1) >> blocksize_bits;
		int8_t etype = (laarr[curr].extLength >> 30);

		if (blen == 1)
			;
		else if (!offset || blen == offset + 1) {
			laarr[curr + 2] = laarr[curr + 1];
			laarr[curr + 1] = laarr[curr];
		} else {
			laarr[curr + 3] = laarr[curr + 1];
			laarr[curr + 2] = laarr[curr + 1] = laarr[curr];
		}

		if (offset) {
			if (etype == (EXT_NOT_RECORDED_ALLOCATED >> 30)) {
				udf_free_blocks(inode->i_sb, inode,
						&laarr[curr].extLocation,
						0, offset);
				laarr[curr].extLength =
					EXT_NOT_RECORDED_NOT_ALLOCATED |
					(offset << blocksize_bits);
				laarr[curr].extLocation.logicalBlockNum = 0;
				laarr[curr].extLocation.
						partitionReferenceNum = 0;
			} else
				laarr[curr].extLength = (etype << 30) |
					(offset << blocksize_bits);
			curr++;
			(*c)++;
			(*endnum)++;
		}

		laarr[curr].extLocation.logicalBlockNum = newblocknum;
		if (etype == (EXT_NOT_RECORDED_NOT_ALLOCATED >> 30))
			laarr[curr].extLocation.partitionReferenceNum =
				UDF_I(inode)->i_location.partitionReferenceNum;
		laarr[curr].extLength = EXT_RECORDED_ALLOCATED |
			blocksize;
		curr++;

		if (blen != offset + 1) {
			if (etype == (EXT_NOT_RECORDED_ALLOCATED >> 30))
				laarr[curr].extLocation.logicalBlockNum +=
								offset + 1;
			laarr[curr].extLength = (etype << 30) |
				((blen - (offset + 1)) << blocksize_bits);
			curr++;
			(*endnum)++;
		}
	}
}

static void udf_prealloc_extents(struct inode *inode, int c, int lastblock,
				 struct kernel_long_ad *laarr,
				 int *endnum)
{
	int start, length = 0, currlength = 0, i;

	if (*endnum >= (c + 1)) {
		if (!lastblock)
			return;
		else
			start = c;
	} else {
		if ((laarr[c + 1].extLength >> 30) ==
					(EXT_NOT_RECORDED_ALLOCATED >> 30)) {
			start = c + 1;
			length = currlength =
				(((laarr[c + 1].extLength &
					UDF_EXTENT_LENGTH_MASK) +
				inode->i_sb->s_blocksize - 1) >>
				inode->i_sb->s_blocksize_bits);
		} else
			start = c;
	}

	for (i = start + 1; i <= *endnum; i++) {
		if (i == *endnum) {
			if (lastblock)
				length += UDF_DEFAULT_PREALLOC_BLOCKS;
		} else if ((laarr[i].extLength >> 30) ==
				(EXT_NOT_RECORDED_NOT_ALLOCATED >> 30)) {
			length += (((laarr[i].extLength &
						UDF_EXTENT_LENGTH_MASK) +
				    inode->i_sb->s_blocksize - 1) >>
				    inode->i_sb->s_blocksize_bits);
		} else
			break;
	}

	if (length) {
		int next = laarr[start].extLocation.logicalBlockNum +
			(((laarr[start].extLength & UDF_EXTENT_LENGTH_MASK) +
			  inode->i_sb->s_blocksize - 1) >>
			  inode->i_sb->s_blocksize_bits);
		int numalloc = udf_prealloc_blocks(inode->i_sb, inode,
				laarr[start].extLocation.partitionReferenceNum,
				next, (UDF_DEFAULT_PREALLOC_BLOCKS > length ?
				length : UDF_DEFAULT_PREALLOC_BLOCKS) -
				currlength);
		if (numalloc) 	{
			if (start == (c + 1))
				laarr[start].extLength +=
					(numalloc <<
					 inode->i_sb->s_blocksize_bits);
			else {
				memmove(&laarr[c + 2], &laarr[c + 1],
					sizeof(struct long_ad) * (*endnum - (c + 1)));
				(*endnum)++;
				laarr[c + 1].extLocation.logicalBlockNum = next;
				laarr[c + 1].extLocation.partitionReferenceNum =
					laarr[c].extLocation.
							partitionReferenceNum;
				laarr[c + 1].extLength =
					EXT_NOT_RECORDED_ALLOCATED |
					(numalloc <<
					 inode->i_sb->s_blocksize_bits);
				start = c + 1;
			}

			for (i = start + 1; numalloc && i < *endnum; i++) {
				int elen = ((laarr[i].extLength &
						UDF_EXTENT_LENGTH_MASK) +
					    inode->i_sb->s_blocksize - 1) >>
					    inode->i_sb->s_blocksize_bits;

				if (elen > numalloc) {
					laarr[i].extLength -=
						(numalloc <<
						 inode->i_sb->s_blocksize_bits);
					numalloc = 0;
				} else {
					numalloc -= elen;
					if (*endnum > (i + 1))
						memmove(&laarr[i],
							&laarr[i + 1],
							sizeof(struct long_ad) *
							(*endnum - (i + 1)));
					i--;
					(*endnum)--;
				}
			}
			UDF_I(inode)->i_lenExtents +=
				numalloc << inode->i_sb->s_blocksize_bits;
		}
	}
}

static void udf_merge_extents(struct inode *inode, struct kernel_long_ad *laarr,
			      int *endnum)
{
	int i;
	unsigned long blocksize = inode->i_sb->s_blocksize;
	unsigned char blocksize_bits = inode->i_sb->s_blocksize_bits;

	for (i = 0; i < (*endnum - 1); i++) {
		struct kernel_long_ad *li /*l[i]*/ = &laarr[i];
		struct kernel_long_ad *lip1 /*l[i plus 1]*/ = &laarr[i + 1];

		if (((li->extLength >> 30) == (lip1->extLength >> 30)) &&
			(((li->extLength >> 30) ==
				(EXT_NOT_RECORDED_NOT_ALLOCATED >> 30)) ||
			((lip1->extLocation.logicalBlockNum -
			  li->extLocation.logicalBlockNum) ==
			(((li->extLength & UDF_EXTENT_LENGTH_MASK) +
			blocksize - 1) >> blocksize_bits)))) {

			if (((li->extLength & UDF_EXTENT_LENGTH_MASK) +
			     (lip1->extLength & UDF_EXTENT_LENGTH_MASK) +
			     blocksize - 1) <= UDF_EXTENT_LENGTH_MASK) {
				li->extLength = lip1->extLength +
					(((li->extLength &
						UDF_EXTENT_LENGTH_MASK) +
					 blocksize - 1) & ~(blocksize - 1));
				if (*endnum > (i + 2))
					memmove(&laarr[i + 1], &laarr[i + 2],
						sizeof(struct long_ad) *
						(*endnum - (i + 2)));
				i--;
				(*endnum)--;
			}
		} else if (((li->extLength >> 30) ==
				(EXT_NOT_RECORDED_ALLOCATED >> 30)) &&
			   ((lip1->extLength >> 30) ==
				(EXT_NOT_RECORDED_NOT_ALLOCATED >> 30))) {
			udf_free_blocks(inode->i_sb, inode, &li->extLocation, 0,
					((li->extLength &
					  UDF_EXTENT_LENGTH_MASK) +
					 blocksize - 1) >> blocksize_bits);
			li->extLocation.logicalBlockNum = 0;
			li->extLocation.partitionReferenceNum = 0;

			if (((li->extLength & UDF_EXTENT_LENGTH_MASK) +
			     (lip1->extLength & UDF_EXTENT_LENGTH_MASK) +
			     blocksize - 1) & ~UDF_EXTENT_LENGTH_MASK) {
				lip1->extLength = (lip1->extLength -
						   (li->extLength &
						   UDF_EXTENT_LENGTH_MASK) +
						   UDF_EXTENT_LENGTH_MASK) &
						   ~(blocksize - 1);
				li->extLength = (li->extLength &
						 UDF_EXTENT_FLAG_MASK) +
						(UDF_EXTENT_LENGTH_MASK + 1) -
						blocksize;
			} else {
				li->extLength = lip1->extLength +
					(((li->extLength &
						UDF_EXTENT_LENGTH_MASK) +
					  blocksize - 1) & ~(blocksize - 1));
				if (*endnum > (i + 2))
					memmove(&laarr[i + 1], &laarr[i + 2],
						sizeof(struct long_ad) *
						(*endnum - (i + 2)));
				i--;
				(*endnum)--;
			}
		} else if ((li->extLength >> 30) ==
					(EXT_NOT_RECORDED_ALLOCATED >> 30)) {
			udf_free_blocks(inode->i_sb, inode,
					&li->extLocation, 0,
					((li->extLength &
						UDF_EXTENT_LENGTH_MASK) +
					 blocksize - 1) >> blocksize_bits);
			li->extLocation.logicalBlockNum = 0;
			li->extLocation.partitionReferenceNum = 0;
			li->extLength = (li->extLength &
						UDF_EXTENT_LENGTH_MASK) |
						EXT_NOT_RECORDED_NOT_ALLOCATED;
		}
	}
}

static int udf_update_extents(struct inode *inode, struct kernel_long_ad *laarr,
			      int startnum, int endnum,
			      struct extent_position *epos)
{
	int start = 0, i;
	struct kernel_lb_addr tmploc;
	uint32_t tmplen;
	int err;

	if (startnum > endnum) {
		for (i = 0; i < (startnum - endnum); i++)
			udf_delete_aext(inode, *epos);
	} else if (startnum < endnum) {
		for (i = 0; i < (endnum - startnum); i++) {
			err = udf_insert_aext(inode, *epos,
					      laarr[i].extLocation,
					      laarr[i].extLength);
			/*
			 * If we fail here, we are likely corrupting the extent
			 * list and leaking blocks. At least stop early to
			 * limit the damage.
			 */
			if (err < 0)
				return err;
			udf_next_aext(inode, epos, &laarr[i].extLocation,
				      &laarr[i].extLength, 1);
			start++;
		}
	}

	for (i = start; i < endnum; i++) {
		udf_next_aext(inode, epos, &tmploc, &tmplen, 0);
		udf_write_aext(inode, epos, &laarr[i].extLocation,
			       laarr[i].extLength, 1);
	}
	return 0;
}

struct buffer_head *udf_bread(struct inode *inode, udf_pblk_t block,
			      int create, int *err)
{
	struct buffer_head *bh = NULL;
	struct udf_map_rq map = {
		.lblk = block,
		.iflags = UDF_MAP_NOPREALLOC | (create ? UDF_MAP_CREATE : 0),
	};

	*err = udf_map_block(inode, &map);
	if (*err || !(map.oflags & UDF_BLK_MAPPED))
		return NULL;

	bh = sb_getblk(inode->i_sb, map.pblk);
	if (!bh) {
		*err = -ENOMEM;
		return NULL;
	}
	if (map.oflags & UDF_BLK_NEW) {
		lock_buffer(bh);
		memset(bh->b_data, 0x00, inode->i_sb->s_blocksize);
		set_buffer_uptodate(bh);
		unlock_buffer(bh);
		mark_buffer_dirty_inode(bh, inode);
		return bh;
	}

	if (bh_read(bh, 0) >= 0)
		return bh;

	brelse(bh);
	*err = -EIO;
	return NULL;
}

int udf_setsize(struct inode *inode, loff_t newsize)
{
	int err = 0;
	struct udf_inode_info *iinfo;
	unsigned int bsize = i_blocksize(inode);

	if (!(S_ISREG(inode->i_mode) || S_ISDIR(inode->i_mode) ||
	      S_ISLNK(inode->i_mode)))
		return -EINVAL;
	if (IS_APPEND(inode) || IS_IMMUTABLE(inode))
		return -EPERM;

	filemap_invalidate_lock(inode->i_mapping);
	iinfo = UDF_I(inode);
	if (newsize > inode->i_size) {
		if (iinfo->i_alloc_type == ICBTAG_FLAG_AD_IN_ICB) {
			if (bsize >=
			    (udf_file_entry_alloc_offset(inode) + newsize)) {
				down_write(&iinfo->i_data_sem);
				iinfo->i_lenAlloc = newsize;
				up_write(&iinfo->i_data_sem);
				goto set_size;
			}
			err = udf_expand_file_adinicb(inode);
			if (err)
				goto out_unlock;
		}
		err = udf_extend_file(inode, newsize);
		if (err)
			goto out_unlock;
set_size:
		truncate_setsize(inode, newsize);
	} else {
		if (iinfo->i_alloc_type == ICBTAG_FLAG_AD_IN_ICB) {
			down_write(&iinfo->i_data_sem);
			udf_clear_extent_cache(inode);
			memset(iinfo->i_data + iinfo->i_lenEAttr + newsize,
			       0x00, bsize - newsize -
			       udf_file_entry_alloc_offset(inode));
			iinfo->i_lenAlloc = newsize;
			truncate_setsize(inode, newsize);
			up_write(&iinfo->i_data_sem);
			goto update_time;
		}
		err = block_truncate_page(inode->i_mapping, newsize,
					  udf_get_block);
		if (err)
			goto out_unlock;
		truncate_setsize(inode, newsize);
		down_write(&iinfo->i_data_sem);
		udf_clear_extent_cache(inode);
		err = udf_truncate_extents(inode);
		up_write(&iinfo->i_data_sem);
		if (err)
			goto out_unlock;
	}
update_time:
	inode_set_mtime_to_ts(inode, inode_set_ctime_current(inode));
	if (IS_SYNC(inode))
		udf_sync_inode(inode);
	else
		mark_inode_dirty(inode);
out_unlock:
	filemap_invalidate_unlock(inode->i_mapping);
	return err;
}

/*
 * Maximum length of linked list formed by ICB hierarchy. The chosen number is
 * arbitrary - just that we hopefully don't limit any real use of rewritten
 * inode on write-once media but avoid looping for too long on corrupted media.
 */
#define UDF_MAX_ICB_NESTING 1024

static int udf_read_inode(struct inode *inode, bool hidden_inode)
{
	struct buffer_head *bh = NULL;
	struct fileEntry *fe;
	struct extendedFileEntry *efe;
	uint16_t ident;
	struct udf_inode_info *iinfo = UDF_I(inode);
	struct udf_sb_info *sbi = UDF_SB(inode->i_sb);
	struct kernel_lb_addr *iloc = &iinfo->i_location;
	unsigned int link_count;
	unsigned int indirections = 0;
	int bs = inode->i_sb->s_blocksize;
	int ret = -EIO;
	uint32_t uid, gid;
	struct timespec64 ts;

reread:
	if (iloc->partitionReferenceNum >= sbi->s_partitions) {
		udf_debug("partition reference: %u > logical volume partitions: %u\n",
			  iloc->partitionReferenceNum, sbi->s_partitions);
		return -EIO;
	}

	if (iloc->logicalBlockNum >=
	    sbi->s_partmaps[iloc->partitionReferenceNum].s_partition_len) {
		udf_debug("block=%u, partition=%u out of range\n",
			  iloc->logicalBlockNum, iloc->partitionReferenceNum);
		return -EIO;
	}

	/*
	 * Set defaults, but the inode is still incomplete!
	 * Note: get_new_inode() sets the following on a new inode:
	 *      i_sb = sb
	 *      i_no = ino
	 *      i_flags = sb->s_flags
	 *      i_state = 0
	 * clean_inode(): zero fills and sets
	 *      i_count = 1
	 *      i_nlink = 1
	 *      i_op = NULL;
	 */
	bh = udf_read_ptagged(inode->i_sb, iloc, 0, &ident);
	if (!bh) {
		udf_err(inode->i_sb, "(ino %lu) failed !bh\n", inode->i_ino);
		return -EIO;
	}

	if (ident != TAG_IDENT_FE && ident != TAG_IDENT_EFE &&
	    ident != TAG_IDENT_USE) {
		udf_err(inode->i_sb, "(ino %lu) failed ident=%u\n",
			inode->i_ino, ident);
		goto out;
	}

	fe = (struct fileEntry *)bh->b_data;
	efe = (struct extendedFileEntry *)bh->b_data;

	if (fe->icbTag.strategyType == cpu_to_le16(4096)) {
		struct buffer_head *ibh;

		ibh = udf_read_ptagged(inode->i_sb, iloc, 1, &ident);
		if (ident == TAG_IDENT_IE && ibh) {
			struct kernel_lb_addr loc;
			struct indirectEntry *ie;

			ie = (struct indirectEntry *)ibh->b_data;
			loc = lelb_to_cpu(ie->indirectICB.extLocation);

			if (ie->indirectICB.extLength) {
				brelse(ibh);
				memcpy(&iinfo->i_location, &loc,
				       sizeof(struct kernel_lb_addr));
				if (++indirections > UDF_MAX_ICB_NESTING) {
					udf_err(inode->i_sb,
						"too many ICBs in ICB hierarchy"
						" (max %d supported)\n",
						UDF_MAX_ICB_NESTING);
					goto out;
				}
				brelse(bh);
				goto reread;
			}
		}
		brelse(ibh);
	} else if (fe->icbTag.strategyType != cpu_to_le16(4)) {
		udf_err(inode->i_sb, "unsupported strategy type: %u\n",
			le16_to_cpu(fe->icbTag.strategyType));
		goto out;
	}
	if (fe->icbTag.strategyType == cpu_to_le16(4))
		iinfo->i_strat4096 = 0;
	else /* if (fe->icbTag.strategyType == cpu_to_le16(4096)) */
		iinfo->i_strat4096 = 1;

	iinfo->i_alloc_type = le16_to_cpu(fe->icbTag.flags) &
							ICBTAG_FLAG_AD_MASK;
	if (iinfo->i_alloc_type != ICBTAG_FLAG_AD_SHORT &&
	    iinfo->i_alloc_type != ICBTAG_FLAG_AD_LONG &&
	    iinfo->i_alloc_type != ICBTAG_FLAG_AD_IN_ICB) {
		ret = -EIO;
		goto out;
	}
	iinfo->i_hidden = hidden_inode;
	iinfo->i_unique = 0;
	iinfo->i_lenEAttr = 0;
	iinfo->i_lenExtents = 0;
	iinfo->i_lenAlloc = 0;
	iinfo->i_next_alloc_block = 0;
	iinfo->i_next_alloc_goal = 0;
	if (fe->descTag.tagIdent == cpu_to_le16(TAG_IDENT_EFE)) {
		iinfo->i_efe = 1;
		iinfo->i_use = 0;
		ret = udf_alloc_i_data(inode, bs -
					sizeof(struct extendedFileEntry));
		if (ret)
			goto out;
		memcpy(iinfo->i_data,
		       bh->b_data + sizeof(struct extendedFileEntry),
		       bs - sizeof(struct extendedFileEntry));
	} else if (fe->descTag.tagIdent == cpu_to_le16(TAG_IDENT_FE)) {
		iinfo->i_efe = 0;
		iinfo->i_use = 0;
		ret = udf_alloc_i_data(inode, bs - sizeof(struct fileEntry));
		if (ret)
			goto out;
		memcpy(iinfo->i_data,
		       bh->b_data + sizeof(struct fileEntry),
		       bs - sizeof(struct fileEntry));
	} else if (fe->descTag.tagIdent == cpu_to_le16(TAG_IDENT_USE)) {
		iinfo->i_efe = 0;
		iinfo->i_use = 1;
		iinfo->i_lenAlloc = le32_to_cpu(
				((struct unallocSpaceEntry *)bh->b_data)->
				 lengthAllocDescs);
		ret = udf_alloc_i_data(inode, bs -
					sizeof(struct unallocSpaceEntry));
		if (ret)
			goto out;
		memcpy(iinfo->i_data,
		       bh->b_data + sizeof(struct unallocSpaceEntry),
		       bs - sizeof(struct unallocSpaceEntry));
		return 0;
	}

	ret = -EIO;
	read_lock(&sbi->s_cred_lock);
	uid = le32_to_cpu(fe->uid);
	if (uid == UDF_INVALID_ID ||
	    UDF_QUERY_FLAG(inode->i_sb, UDF_FLAG_UID_SET))
		inode->i_uid = sbi->s_uid;
	else
		i_uid_write(inode, uid);

	gid = le32_to_cpu(fe->gid);
	if (gid == UDF_INVALID_ID ||
	    UDF_QUERY_FLAG(inode->i_sb, UDF_FLAG_GID_SET))
		inode->i_gid = sbi->s_gid;
	else
		i_gid_write(inode, gid);

	if (fe->icbTag.fileType != ICBTAG_FILE_TYPE_DIRECTORY &&
			sbi->s_fmode != UDF_INVALID_MODE)
		inode->i_mode = sbi->s_fmode;
	else if (fe->icbTag.fileType == ICBTAG_FILE_TYPE_DIRECTORY &&
			sbi->s_dmode != UDF_INVALID_MODE)
		inode->i_mode = sbi->s_dmode;
	else
		inode->i_mode = udf_convert_permissions(fe);
	inode->i_mode &= ~sbi->s_umask;
	iinfo->i_extraPerms = le32_to_cpu(fe->permissions) & ~FE_MAPPED_PERMS;

	read_unlock(&sbi->s_cred_lock);

	link_count = le16_to_cpu(fe->fileLinkCount);
	if (!link_count) {
		if (!hidden_inode) {
			ret = -ESTALE;
			goto out;
		}
		link_count = 1;
	}
	set_nlink(inode, link_count);

	inode->i_size = le64_to_cpu(fe->informationLength);
	iinfo->i_lenExtents = inode->i_size;

	if (iinfo->i_efe == 0) {
		inode->i_blocks = le64_to_cpu(fe->logicalBlocksRecorded) <<
			(inode->i_sb->s_blocksize_bits - 9);

		udf_disk_stamp_to_time(&ts, fe->accessTime);
		inode_set_atime_to_ts(inode, ts);
		udf_disk_stamp_to_time(&ts, fe->modificationTime);
		inode_set_mtime_to_ts(inode, ts);
		udf_disk_stamp_to_time(&ts, fe->attrTime);
		inode_set_ctime_to_ts(inode, ts);

		iinfo->i_unique = le64_to_cpu(fe->uniqueID);
		iinfo->i_lenEAttr = le32_to_cpu(fe->lengthExtendedAttr);
		iinfo->i_lenAlloc = le32_to_cpu(fe->lengthAllocDescs);
		iinfo->i_checkpoint = le32_to_cpu(fe->checkpoint);
		iinfo->i_streamdir = 0;
		iinfo->i_lenStreams = 0;
	} else {
		inode->i_blocks = le64_to_cpu(efe->logicalBlocksRecorded) <<
		    (inode->i_sb->s_blocksize_bits - 9);

		udf_disk_stamp_to_time(&ts, efe->accessTime);
		inode_set_atime_to_ts(inode, ts);
		udf_disk_stamp_to_time(&ts, efe->modificationTime);
		inode_set_mtime_to_ts(inode, ts);
		udf_disk_stamp_to_time(&ts, efe->attrTime);
		inode_set_ctime_to_ts(inode, ts);
		udf_disk_stamp_to_time(&iinfo->i_crtime, efe->createTime);

		iinfo->i_unique = le64_to_cpu(efe->uniqueID);
		iinfo->i_lenEAttr = le32_to_cpu(efe->lengthExtendedAttr);
		iinfo->i_lenAlloc = le32_to_cpu(efe->lengthAllocDescs);
		iinfo->i_checkpoint = le32_to_cpu(efe->checkpoint);

		/* Named streams */
		iinfo->i_streamdir = (efe->streamDirectoryICB.extLength != 0);
		iinfo->i_locStreamdir =
			lelb_to_cpu(efe->streamDirectoryICB.extLocation);
		iinfo->i_lenStreams = le64_to_cpu(efe->objectSize);
		if (iinfo->i_lenStreams >= inode->i_size)
			iinfo->i_lenStreams -= inode->i_size;
		else
			iinfo->i_lenStreams = 0;
	}
	inode->i_generation = iinfo->i_unique;

	/*
	 * Sanity check length of allocation descriptors and extended attrs to
	 * avoid integer overflows
	 */
	if (iinfo->i_lenEAttr > bs || iinfo->i_lenAlloc > bs)
		goto out;
	/* Now do exact checks */
	if (udf_file_entry_alloc_offset(inode) + iinfo->i_lenAlloc > bs)
		goto out;
	/* Sanity checks for files in ICB so that we don't get confused later */
	if (iinfo->i_alloc_type == ICBTAG_FLAG_AD_IN_ICB) {
		/*
		 * For file in ICB data is stored in allocation descriptor
		 * so sizes should match
		 */
		if (iinfo->i_lenAlloc != inode->i_size)
			goto out;
		/* File in ICB has to fit in there... */
		if (inode->i_size > bs - udf_file_entry_alloc_offset(inode))
			goto out;
	}

	switch (fe->icbTag.fileType) {
	case ICBTAG_FILE_TYPE_DIRECTORY:
		inode->i_op = &udf_dir_inode_operations;
		inode->i_fop = &udf_dir_operations;
		inode->i_mode |= S_IFDIR;
		inc_nlink(inode);
		break;
	case ICBTAG_FILE_TYPE_REALTIME:
	case ICBTAG_FILE_TYPE_REGULAR:
	case ICBTAG_FILE_TYPE_UNDEF:
	case ICBTAG_FILE_TYPE_VAT20:
		inode->i_data.a_ops = &udf_aops;
		inode->i_op = &udf_file_inode_operations;
		inode->i_fop = &udf_file_operations;
		inode->i_mode |= S_IFREG;
		break;
	case ICBTAG_FILE_TYPE_BLOCK:
		inode->i_mode |= S_IFBLK;
		break;
	case ICBTAG_FILE_TYPE_CHAR:
		inode->i_mode |= S_IFCHR;
		break;
	case ICBTAG_FILE_TYPE_FIFO:
		init_special_inode(inode, inode->i_mode | S_IFIFO, 0);
		break;
	case ICBTAG_FILE_TYPE_SOCKET:
		init_special_inode(inode, inode->i_mode | S_IFSOCK, 0);
		break;
	case ICBTAG_FILE_TYPE_SYMLINK:
		inode->i_data.a_ops = &udf_symlink_aops;
		inode->i_op = &udf_symlink_inode_operations;
		inode_nohighmem(inode);
		inode->i_mode = S_IFLNK | 0777;
		break;
	case ICBTAG_FILE_TYPE_MAIN:
		udf_debug("METADATA FILE-----\n");
		break;
	case ICBTAG_FILE_TYPE_MIRROR:
		udf_debug("METADATA MIRROR FILE-----\n");
		break;
	case ICBTAG_FILE_TYPE_BITMAP:
		udf_debug("METADATA BITMAP FILE-----\n");
		break;
	default:
		udf_err(inode->i_sb, "(ino %lu) failed unknown file type=%u\n",
			inode->i_ino, fe->icbTag.fileType);
		goto out;
	}
	if (S_ISCHR(inode->i_mode) || S_ISBLK(inode->i_mode)) {
		struct deviceSpec *dsea =
			(struct deviceSpec *)udf_get_extendedattr(inode, 12, 1);
		if (dsea) {
			init_special_inode(inode, inode->i_mode,
				MKDEV(le32_to_cpu(dsea->majorDeviceIdent),
				      le32_to_cpu(dsea->minorDeviceIdent)));
			/* Developer ID ??? */
		} else
			goto out;
	}
	ret = 0;
out:
	brelse(bh);
	return ret;
}

static int udf_alloc_i_data(struct inode *inode, size_t size)
{
	struct udf_inode_info *iinfo = UDF_I(inode);
	iinfo->i_data = kmalloc(size, GFP_KERNEL);
	if (!iinfo->i_data)
		return -ENOMEM;
	return 0;
}

static umode_t udf_convert_permissions(struct fileEntry *fe)
{
	umode_t mode;
	uint32_t permissions;
	uint32_t flags;

	permissions = le32_to_cpu(fe->permissions);
	flags = le16_to_cpu(fe->icbTag.flags);

	mode =	((permissions) & 0007) |
		((permissions >> 2) & 0070) |
		((permissions >> 4) & 0700) |
		((flags & ICBTAG_FLAG_SETUID) ? S_ISUID : 0) |
		((flags & ICBTAG_FLAG_SETGID) ? S_ISGID : 0) |
		((flags & ICBTAG_FLAG_STICKY) ? S_ISVTX : 0);

	return mode;
}

void udf_update_extra_perms(struct inode *inode, umode_t mode)
{
	struct udf_inode_info *iinfo = UDF_I(inode);

	/*
	 * UDF 2.01 sec. 3.3.3.3 Note 2:
	 * In Unix, delete permission tracks write
	 */
	iinfo->i_extraPerms &= ~FE_DELETE_PERMS;
	if (mode & 0200)
		iinfo->i_extraPerms |= FE_PERM_U_DELETE;
	if (mode & 0020)
		iinfo->i_extraPerms |= FE_PERM_G_DELETE;
	if (mode & 0002)
		iinfo->i_extraPerms |= FE_PERM_O_DELETE;
}

int udf_write_inode(struct inode *inode, struct writeback_control *wbc)
{
	return udf_update_inode(inode, wbc->sync_mode == WB_SYNC_ALL);
}

static int udf_sync_inode(struct inode *inode)
{
	return udf_update_inode(inode, 1);
}

static void udf_adjust_time(struct udf_inode_info *iinfo, struct timespec64 time)
{
	if (iinfo->i_crtime.tv_sec > time.tv_sec ||
	    (iinfo->i_crtime.tv_sec == time.tv_sec &&
	     iinfo->i_crtime.tv_nsec > time.tv_nsec))
		iinfo->i_crtime = time;
}

static int udf_update_inode(struct inode *inode, int do_sync)
{
	struct buffer_head *bh = NULL;
	struct fileEntry *fe;
	struct extendedFileEntry *efe;
	uint64_t lb_recorded;
	uint32_t udfperms;
	uint16_t icbflags;
	uint16_t crclen;
	int err = 0;
	struct udf_sb_info *sbi = UDF_SB(inode->i_sb);
	unsigned char blocksize_bits = inode->i_sb->s_blocksize_bits;
	struct udf_inode_info *iinfo = UDF_I(inode);

	bh = sb_getblk(inode->i_sb,
			udf_get_lb_pblock(inode->i_sb, &iinfo->i_location, 0));
	if (!bh) {
		udf_debug("getblk failure\n");
		return -EIO;
	}

	lock_buffer(bh);
	memset(bh->b_data, 0, inode->i_sb->s_blocksize);
	fe = (struct fileEntry *)bh->b_data;
	efe = (struct extendedFileEntry *)bh->b_data;

	if (iinfo->i_use) {
		struct unallocSpaceEntry *use =
			(struct unallocSpaceEntry *)bh->b_data;

		use->lengthAllocDescs = cpu_to_le32(iinfo->i_lenAlloc);
		memcpy(bh->b_data + sizeof(struct unallocSpaceEntry),
		       iinfo->i_data, inode->i_sb->s_blocksize -
					sizeof(struct unallocSpaceEntry));
		use->descTag.tagIdent = cpu_to_le16(TAG_IDENT_USE);
		crclen = sizeof(struct unallocSpaceEntry);

		goto finish;
	}

	if (UDF_QUERY_FLAG(inode->i_sb, UDF_FLAG_UID_FORGET))
		fe->uid = cpu_to_le32(UDF_INVALID_ID);
	else
		fe->uid = cpu_to_le32(i_uid_read(inode));

	if (UDF_QUERY_FLAG(inode->i_sb, UDF_FLAG_GID_FORGET))
		fe->gid = cpu_to_le32(UDF_INVALID_ID);
	else
		fe->gid = cpu_to_le32(i_gid_read(inode));

	udfperms = ((inode->i_mode & 0007)) |
		   ((inode->i_mode & 0070) << 2) |
		   ((inode->i_mode & 0700) << 4);

	udfperms |= iinfo->i_extraPerms;
	fe->permissions = cpu_to_le32(udfperms);

	if (S_ISDIR(inode->i_mode) && inode->i_nlink > 0)
		fe->fileLinkCount = cpu_to_le16(inode->i_nlink - 1);
	else {
		if (iinfo->i_hidden)
			fe->fileLinkCount = cpu_to_le16(0);
		else
			fe->fileLinkCount = cpu_to_le16(inode->i_nlink);
	}

	fe->informationLength = cpu_to_le64(inode->i_size);

	if (S_ISCHR(inode->i_mode) || S_ISBLK(inode->i_mode)) {
		struct regid *eid;
		struct deviceSpec *dsea =
			(struct deviceSpec *)udf_get_extendedattr(inode, 12, 1);
		if (!dsea) {
			dsea = (struct deviceSpec *)
				udf_add_extendedattr(inode,
						     sizeof(struct deviceSpec) +
						     sizeof(struct regid), 12, 0x3);
			dsea->attrType = cpu_to_le32(12);
			dsea->attrSubtype = 1;
			dsea->attrLength = cpu_to_le32(
						sizeof(struct deviceSpec) +
						sizeof(struct regid));
			dsea->impUseLength = cpu_to_le32(sizeof(struct regid));
		}
		eid = (struct regid *)dsea->impUse;
		memset(eid, 0, sizeof(*eid));
		strcpy(eid->ident, UDF_ID_DEVELOPER);
		eid->identSuffix[0] = UDF_OS_CLASS_UNIX;
		eid->identSuffix[1] = UDF_OS_ID_LINUX;
		dsea->majorDeviceIdent = cpu_to_le32(imajor(inode));
		dsea->minorDeviceIdent = cpu_to_le32(iminor(inode));
	}

	if (iinfo->i_alloc_type == ICBTAG_FLAG_AD_IN_ICB)
		lb_recorded = 0; /* No extents => no blocks! */
	else
		lb_recorded =
			(inode->i_blocks + (1 << (blocksize_bits - 9)) - 1) >>
			(blocksize_bits - 9);

	if (iinfo->i_efe == 0) {
		memcpy(bh->b_data + sizeof(struct fileEntry),
		       iinfo->i_data,
		       inode->i_sb->s_blocksize - sizeof(struct fileEntry));
		fe->logicalBlocksRecorded = cpu_to_le64(lb_recorded);

		udf_time_to_disk_stamp(&fe->accessTime, inode_get_atime(inode));
		udf_time_to_disk_stamp(&fe->modificationTime, inode_get_mtime(inode));
		udf_time_to_disk_stamp(&fe->attrTime, inode_get_ctime(inode));
		memset(&(fe->impIdent), 0, sizeof(struct regid));
		strcpy(fe->impIdent.ident, UDF_ID_DEVELOPER);
		fe->impIdent.identSuffix[0] = UDF_OS_CLASS_UNIX;
		fe->impIdent.identSuffix[1] = UDF_OS_ID_LINUX;
		fe->uniqueID = cpu_to_le64(iinfo->i_unique);
		fe->lengthExtendedAttr = cpu_to_le32(iinfo->i_lenEAttr);
		fe->lengthAllocDescs = cpu_to_le32(iinfo->i_lenAlloc);
		fe->checkpoint = cpu_to_le32(iinfo->i_checkpoint);
		fe->descTag.tagIdent = cpu_to_le16(TAG_IDENT_FE);
		crclen = sizeof(struct fileEntry);
	} else {
		memcpy(bh->b_data + sizeof(struct extendedFileEntry),
		       iinfo->i_data,
		       inode->i_sb->s_blocksize -
					sizeof(struct extendedFileEntry));
		efe->objectSize =
			cpu_to_le64(inode->i_size + iinfo->i_lenStreams);
		efe->logicalBlocksRecorded = cpu_to_le64(lb_recorded);

		if (iinfo->i_streamdir) {
			struct long_ad *icb_lad = &efe->streamDirectoryICB;

			icb_lad->extLocation =
				cpu_to_lelb(iinfo->i_locStreamdir);
			icb_lad->extLength =
				cpu_to_le32(inode->i_sb->s_blocksize);
		}

		udf_adjust_time(iinfo, inode_get_atime(inode));
		udf_adjust_time(iinfo, inode_get_mtime(inode));
		udf_adjust_time(iinfo, inode_get_ctime(inode));

		udf_time_to_disk_stamp(&efe->accessTime,
				       inode_get_atime(inode));
		udf_time_to_disk_stamp(&efe->modificationTime,
				       inode_get_mtime(inode));
		udf_time_to_disk_stamp(&efe->createTime, iinfo->i_crtime);
		udf_time_to_disk_stamp(&efe->attrTime, inode_get_ctime(inode));

		memset(&(efe->impIdent), 0, sizeof(efe->impIdent));
		strcpy(efe->impIdent.ident, UDF_ID_DEVELOPER);
		efe->impIdent.identSuffix[0] = UDF_OS_CLASS_UNIX;
		efe->impIdent.identSuffix[1] = UDF_OS_ID_LINUX;
		efe->uniqueID = cpu_to_le64(iinfo->i_unique);
		efe->lengthExtendedAttr = cpu_to_le32(iinfo->i_lenEAttr);
		efe->lengthAllocDescs = cpu_to_le32(iinfo->i_lenAlloc);
		efe->checkpoint = cpu_to_le32(iinfo->i_checkpoint);
		efe->descTag.tagIdent = cpu_to_le16(TAG_IDENT_EFE);
		crclen = sizeof(struct extendedFileEntry);
	}

finish:
	if (iinfo->i_strat4096) {
		fe->icbTag.strategyType = cpu_to_le16(4096);
		fe->icbTag.strategyParameter = cpu_to_le16(1);
		fe->icbTag.numEntries = cpu_to_le16(2);
	} else {
		fe->icbTag.strategyType = cpu_to_le16(4);
		fe->icbTag.numEntries = cpu_to_le16(1);
	}

	if (iinfo->i_use)
		fe->icbTag.fileType = ICBTAG_FILE_TYPE_USE;
	else if (S_ISDIR(inode->i_mode))
		fe->icbTag.fileType = ICBTAG_FILE_TYPE_DIRECTORY;
	else if (S_ISREG(inode->i_mode))
		fe->icbTag.fileType = ICBTAG_FILE_TYPE_REGULAR;
	else if (S_ISLNK(inode->i_mode))
		fe->icbTag.fileType = ICBTAG_FILE_TYPE_SYMLINK;
	else if (S_ISBLK(inode->i_mode))
		fe->icbTag.fileType = ICBTAG_FILE_TYPE_BLOCK;
	else if (S_ISCHR(inode->i_mode))
		fe->icbTag.fileType = ICBTAG_FILE_TYPE_CHAR;
	else if (S_ISFIFO(inode->i_mode))
		fe->icbTag.fileType = ICBTAG_FILE_TYPE_FIFO;
	else if (S_ISSOCK(inode->i_mode))
		fe->icbTag.fileType = ICBTAG_FILE_TYPE_SOCKET;

	icbflags =	iinfo->i_alloc_type |
			((inode->i_mode & S_ISUID) ? ICBTAG_FLAG_SETUID : 0) |
			((inode->i_mode & S_ISGID) ? ICBTAG_FLAG_SETGID : 0) |
			((inode->i_mode & S_ISVTX) ? ICBTAG_FLAG_STICKY : 0) |
			(le16_to_cpu(fe->icbTag.flags) &
				~(ICBTAG_FLAG_AD_MASK | ICBTAG_FLAG_SETUID |
				ICBTAG_FLAG_SETGID | ICBTAG_FLAG_STICKY));

	fe->icbTag.flags = cpu_to_le16(icbflags);
	if (sbi->s_udfrev >= 0x0200)
		fe->descTag.descVersion = cpu_to_le16(3);
	else
		fe->descTag.descVersion = cpu_to_le16(2);
	fe->descTag.tagSerialNum = cpu_to_le16(sbi->s_serial_number);
	fe->descTag.tagLocation = cpu_to_le32(
					iinfo->i_location.logicalBlockNum);
	crclen += iinfo->i_lenEAttr + iinfo->i_lenAlloc - sizeof(struct tag);
	fe->descTag.descCRCLength = cpu_to_le16(crclen);
	fe->descTag.descCRC = cpu_to_le16(crc_itu_t(0, (char *)fe + sizeof(struct tag),
						  crclen));
	fe->descTag.tagChecksum = udf_tag_checksum(&fe->descTag);

	set_buffer_uptodate(bh);
	unlock_buffer(bh);

	/* write the data blocks */
	mark_buffer_dirty(bh);
	if (do_sync) {
		sync_dirty_buffer(bh);
		if (buffer_write_io_error(bh)) {
			udf_warn(inode->i_sb, "IO error syncing udf inode [%08lx]\n",
				 inode->i_ino);
			err = -EIO;
		}
	}
	brelse(bh);

	return err;
}

struct inode *__udf_iget(struct super_block *sb, struct kernel_lb_addr *ino,
			 bool hidden_inode)
{
	unsigned long block = udf_get_lb_pblock(sb, ino, 0);
	struct inode *inode = iget_locked(sb, block);
	int err;

	if (!inode)
		return ERR_PTR(-ENOMEM);

	if (!(inode->i_state & I_NEW)) {
		if (UDF_I(inode)->i_hidden != hidden_inode) {
			iput(inode);
			return ERR_PTR(-EFSCORRUPTED);
		}
		return inode;
	}

	memcpy(&UDF_I(inode)->i_location, ino, sizeof(struct kernel_lb_addr));
	err = udf_read_inode(inode, hidden_inode);
	if (err < 0) {
		iget_failed(inode);
		return ERR_PTR(err);
	}
	unlock_new_inode(inode);

	return inode;
}

int udf_setup_indirect_aext(struct inode *inode, udf_pblk_t block,
			    struct extent_position *epos)
{
	struct super_block *sb = inode->i_sb;
	struct buffer_head *bh;
	struct allocExtDesc *aed;
	struct extent_position nepos;
	struct kernel_lb_addr neloc;
	int ver, adsize;

	if (UDF_I(inode)->i_alloc_type == ICBTAG_FLAG_AD_SHORT)
		adsize = sizeof(struct short_ad);
	else if (UDF_I(inode)->i_alloc_type == ICBTAG_FLAG_AD_LONG)
		adsize = sizeof(struct long_ad);
	else
		return -EIO;

	neloc.logicalBlockNum = block;
	neloc.partitionReferenceNum = epos->block.partitionReferenceNum;

	bh = sb_getblk(sb, udf_get_lb_pblock(sb, &neloc, 0));
	if (!bh)
		return -EIO;
	lock_buffer(bh);
	memset(bh->b_data, 0x00, sb->s_blocksize);
	set_buffer_uptodate(bh);
	unlock_buffer(bh);
	mark_buffer_dirty_inode(bh, inode);

	aed = (struct allocExtDesc *)(bh->b_data);
	if (!UDF_QUERY_FLAG(sb, UDF_FLAG_STRICT)) {
		aed->previousAllocExtLocation =
				cpu_to_le32(epos->block.logicalBlockNum);
	}
	aed->lengthAllocDescs = cpu_to_le32(0);
	if (UDF_SB(sb)->s_udfrev >= 0x0200)
		ver = 3;
	else
		ver = 2;
	udf_new_tag(bh->b_data, TAG_IDENT_AED, ver, 1, block,
		    sizeof(struct tag));

	nepos.block = neloc;
	nepos.offset = sizeof(struct allocExtDesc);
	nepos.bh = bh;

	/*
	 * Do we have to copy current last extent to make space for indirect
	 * one?
	 */
	if (epos->offset + adsize > sb->s_blocksize) {
		struct kernel_lb_addr cp_loc;
		uint32_t cp_len;
		int cp_type;

		epos->offset -= adsize;
		cp_type = udf_current_aext(inode, epos, &cp_loc, &cp_len, 0);
		cp_len |= ((uint32_t)cp_type) << 30;

		__udf_add_aext(inode, &nepos, &cp_loc, cp_len, 1);
		udf_write_aext(inode, epos, &nepos.block,
			       sb->s_blocksize | EXT_NEXT_EXTENT_ALLOCDESCS, 0);
	} else {
		__udf_add_aext(inode, epos, &nepos.block,
			       sb->s_blocksize | EXT_NEXT_EXTENT_ALLOCDESCS, 0);
	}

	brelse(epos->bh);
	*epos = nepos;

	return 0;
}

/*
 * Append extent at the given position - should be the first free one in inode
 * / indirect extent. This function assumes there is enough space in the inode
 * or indirect extent. Use udf_add_aext() if you didn't check for this before.
 */
int __udf_add_aext(struct inode *inode, struct extent_position *epos,
		   struct kernel_lb_addr *eloc, uint32_t elen, int inc)
{
	struct udf_inode_info *iinfo = UDF_I(inode);
	struct allocExtDesc *aed;
	int adsize;

	if (iinfo->i_alloc_type == ICBTAG_FLAG_AD_SHORT)
		adsize = sizeof(struct short_ad);
	else if (iinfo->i_alloc_type == ICBTAG_FLAG_AD_LONG)
		adsize = sizeof(struct long_ad);
	else
		return -EIO;

	if (!epos->bh) {
		WARN_ON(iinfo->i_lenAlloc !=
			epos->offset - udf_file_entry_alloc_offset(inode));
	} else {
		aed = (struct allocExtDesc *)epos->bh->b_data;
		WARN_ON(le32_to_cpu(aed->lengthAllocDescs) !=
			epos->offset - sizeof(struct allocExtDesc));
		WARN_ON(epos->offset + adsize > inode->i_sb->s_blocksize);
	}

	udf_write_aext(inode, epos, eloc, elen, inc);

	if (!epos->bh) {
		iinfo->i_lenAlloc += adsize;
		mark_inode_dirty(inode);
	} else {
		aed = (struct allocExtDesc *)epos->bh->b_data;
		le32_add_cpu(&aed->lengthAllocDescs, adsize);
		if (!UDF_QUERY_FLAG(inode->i_sb, UDF_FLAG_STRICT) ||
				UDF_SB(inode->i_sb)->s_udfrev >= 0x0201)
			udf_update_tag(epos->bh->b_data,
					epos->offset + (inc ? 0 : adsize));
		else
			udf_update_tag(epos->bh->b_data,
					sizeof(struct allocExtDesc));
		mark_buffer_dirty_inode(epos->bh, inode);
	}

	return 0;
}

/*
 * Append extent at given position - should be the first free one in inode
 * / indirect extent. Takes care of allocating and linking indirect blocks.
 */
int udf_add_aext(struct inode *inode, struct extent_position *epos,
		 struct kernel_lb_addr *eloc, uint32_t elen, int inc)
{
	int adsize;
	struct super_block *sb = inode->i_sb;

	if (UDF_I(inode)->i_alloc_type == ICBTAG_FLAG_AD_SHORT)
		adsize = sizeof(struct short_ad);
	else if (UDF_I(inode)->i_alloc_type == ICBTAG_FLAG_AD_LONG)
		adsize = sizeof(struct long_ad);
	else
		return -EIO;

	if (epos->offset + (2 * adsize) > sb->s_blocksize) {
		int err;
		udf_pblk_t new_block;

		new_block = udf_new_block(sb, NULL,
					  epos->block.partitionReferenceNum,
					  epos->block.logicalBlockNum, &err);
		if (!new_block)
			return -ENOSPC;

		err = udf_setup_indirect_aext(inode, new_block, epos);
		if (err)
			return err;
	}

	return __udf_add_aext(inode, epos, eloc, elen, inc);
}

void udf_write_aext(struct inode *inode, struct extent_position *epos,
		    struct kernel_lb_addr *eloc, uint32_t elen, int inc)
{
	int adsize;
	uint8_t *ptr;
	struct short_ad *sad;
	struct long_ad *lad;
	struct udf_inode_info *iinfo = UDF_I(inode);

	if (!epos->bh)
		ptr = iinfo->i_data + epos->offset -
			udf_file_entry_alloc_offset(inode) +
			iinfo->i_lenEAttr;
	else
		ptr = epos->bh->b_data + epos->offset;

	switch (iinfo->i_alloc_type) {
	case ICBTAG_FLAG_AD_SHORT:
		sad = (struct short_ad *)ptr;
		sad->extLength = cpu_to_le32(elen);
		sad->extPosition = cpu_to_le32(eloc->logicalBlockNum);
		adsize = sizeof(struct short_ad);
		break;
	case ICBTAG_FLAG_AD_LONG:
		lad = (struct long_ad *)ptr;
		lad->extLength = cpu_to_le32(elen);
		lad->extLocation = cpu_to_lelb(*eloc);
		memset(lad->impUse, 0x00, sizeof(lad->impUse));
		adsize = sizeof(struct long_ad);
		break;
	default:
		return;
	}

	if (epos->bh) {
		if (!UDF_QUERY_FLAG(inode->i_sb, UDF_FLAG_STRICT) ||
		    UDF_SB(inode->i_sb)->s_udfrev >= 0x0201) {
			struct allocExtDesc *aed =
				(struct allocExtDesc *)epos->bh->b_data;
			udf_update_tag(epos->bh->b_data,
				       le32_to_cpu(aed->lengthAllocDescs) +
				       sizeof(struct allocExtDesc));
		}
		mark_buffer_dirty_inode(epos->bh, inode);
	} else {
		mark_inode_dirty(inode);
	}

	if (inc)
		epos->offset += adsize;
}

/*
 * Only 1 indirect extent in a row really makes sense but allow upto 16 in case
 * someone does some weird stuff.
 */
#define UDF_MAX_INDIR_EXTS 16

int8_t udf_next_aext(struct inode *inode, struct extent_position *epos,
		     struct kernel_lb_addr *eloc, uint32_t *elen, int inc)
{
	int8_t etype;
	unsigned int indirections = 0;

	while ((etype = udf_current_aext(inode, epos, eloc, elen, inc)) ==
	       (EXT_NEXT_EXTENT_ALLOCDESCS >> 30)) {
		udf_pblk_t block;

		if (++indirections > UDF_MAX_INDIR_EXTS) {
			udf_err(inode->i_sb,
				"too many indirect extents in inode %lu\n",
				inode->i_ino);
			return -1;
		}

		epos->block = *eloc;
		epos->offset = sizeof(struct allocExtDesc);
		brelse(epos->bh);
		block = udf_get_lb_pblock(inode->i_sb, &epos->block, 0);
		epos->bh = sb_bread(inode->i_sb, block);
		if (!epos->bh) {
			udf_debug("reading block %u failed!\n", block);
			return -1;
		}
	}

	return etype;
}

int8_t udf_current_aext(struct inode *inode, struct extent_position *epos,
			struct kernel_lb_addr *eloc, uint32_t *elen, int inc)
{
	int alen;
	int8_t etype;
	uint8_t *ptr;
	struct short_ad *sad;
	struct long_ad *lad;
	struct udf_inode_info *iinfo = UDF_I(inode);

	if (!epos->bh) {
		if (!epos->offset)
			epos->offset = udf_file_entry_alloc_offset(inode);
		ptr = iinfo->i_data + epos->offset -
			udf_file_entry_alloc_offset(inode) +
			iinfo->i_lenEAttr;
		alen = udf_file_entry_alloc_offset(inode) +
							iinfo->i_lenAlloc;
	} else {
		if (!epos->offset)
			epos->offset = sizeof(struct allocExtDesc);
		ptr = epos->bh->b_data + epos->offset;
		alen = sizeof(struct allocExtDesc) +
			le32_to_cpu(((struct allocExtDesc *)epos->bh->b_data)->
							lengthAllocDescs);
	}

	switch (iinfo->i_alloc_type) {
	case ICBTAG_FLAG_AD_SHORT:
		sad = udf_get_fileshortad(ptr, alen, &epos->offset, inc);
		if (!sad)
			return -1;
		etype = le32_to_cpu(sad->extLength) >> 30;
		eloc->logicalBlockNum = le32_to_cpu(sad->extPosition);
		eloc->partitionReferenceNum =
				iinfo->i_location.partitionReferenceNum;
		*elen = le32_to_cpu(sad->extLength) & UDF_EXTENT_LENGTH_MASK;
		break;
	case ICBTAG_FLAG_AD_LONG:
		lad = udf_get_filelongad(ptr, alen, &epos->offset, inc);
		if (!lad)
			return -1;
		etype = le32_to_cpu(lad->extLength) >> 30;
		*eloc = lelb_to_cpu(lad->extLocation);
		*elen = le32_to_cpu(lad->extLength) & UDF_EXTENT_LENGTH_MASK;
		break;
	default:
		udf_debug("alloc_type = %u unsupported\n", iinfo->i_alloc_type);
		return -1;
	}

	return etype;
}

static int udf_insert_aext(struct inode *inode, struct extent_position epos,
			   struct kernel_lb_addr neloc, uint32_t nelen)
{
	struct kernel_lb_addr oeloc;
	uint32_t oelen;
	int8_t etype;
	int err;

	if (epos.bh)
		get_bh(epos.bh);

	while ((etype = udf_next_aext(inode, &epos, &oeloc, &oelen, 0)) != -1) {
		udf_write_aext(inode, &epos, &neloc, nelen, 1);
		neloc = oeloc;
		nelen = (etype << 30) | oelen;
	}
	err = udf_add_aext(inode, &epos, &neloc, nelen, 1);
	brelse(epos.bh);

	return err;
}

int8_t udf_delete_aext(struct inode *inode, struct extent_position epos)
{
	struct extent_position oepos;
	int adsize;
	int8_t etype;
	struct allocExtDesc *aed;
	struct udf_inode_info *iinfo;
	struct kernel_lb_addr eloc;
	uint32_t elen;

	if (epos.bh) {
		get_bh(epos.bh);
		get_bh(epos.bh);
	}

	iinfo = UDF_I(inode);
	if (iinfo->i_alloc_type == ICBTAG_FLAG_AD_SHORT)
		adsize = sizeof(struct short_ad);
	else if (iinfo->i_alloc_type == ICBTAG_FLAG_AD_LONG)
		adsize = sizeof(struct long_ad);
	else
		adsize = 0;

	oepos = epos;
	if (udf_next_aext(inode, &epos, &eloc, &elen, 1) == -1)
		return -1;

	while ((etype = udf_next_aext(inode, &epos, &eloc, &elen, 1)) != -1) {
		udf_write_aext(inode, &oepos, &eloc, (etype << 30) | elen, 1);
		if (oepos.bh != epos.bh) {
			oepos.block = epos.block;
			brelse(oepos.bh);
			get_bh(epos.bh);
			oepos.bh = epos.bh;
			oepos.offset = epos.offset - adsize;
		}
	}
	memset(&eloc, 0x00, sizeof(struct kernel_lb_addr));
	elen = 0;

	if (epos.bh != oepos.bh) {
		udf_free_blocks(inode->i_sb, inode, &epos.block, 0, 1);
		udf_write_aext(inode, &oepos, &eloc, elen, 1);
		udf_write_aext(inode, &oepos, &eloc, elen, 1);
		if (!oepos.bh) {
			iinfo->i_lenAlloc -= (adsize * 2);
			mark_inode_dirty(inode);
		} else {
			aed = (struct allocExtDesc *)oepos.bh->b_data;
			le32_add_cpu(&aed->lengthAllocDescs, -(2 * adsize));
			if (!UDF_QUERY_FLAG(inode->i_sb, UDF_FLAG_STRICT) ||
			    UDF_SB(inode->i_sb)->s_udfrev >= 0x0201)
				udf_update_tag(oepos.bh->b_data,
						oepos.offset - (2 * adsize));
			else
				udf_update_tag(oepos.bh->b_data,
						sizeof(struct allocExtDesc));
			mark_buffer_dirty_inode(oepos.bh, inode);
		}
	} else {
		udf_write_aext(inode, &oepos, &eloc, elen, 1);
		if (!oepos.bh) {
			iinfo->i_lenAlloc -= adsize;
			mark_inode_dirty(inode);
		} else {
			aed = (struct allocExtDesc *)oepos.bh->b_data;
			le32_add_cpu(&aed->lengthAllocDescs, -adsize);
			if (!UDF_QUERY_FLAG(inode->i_sb, UDF_FLAG_STRICT) ||
			    UDF_SB(inode->i_sb)->s_udfrev >= 0x0201)
				udf_update_tag(oepos.bh->b_data,
						epos.offset - adsize);
			else
				udf_update_tag(oepos.bh->b_data,
						sizeof(struct allocExtDesc));
			mark_buffer_dirty_inode(oepos.bh, inode);
		}
	}

	brelse(epos.bh);
	brelse(oepos.bh);

	return (elen >> 30);
}

int8_t inode_bmap(struct inode *inode, sector_t block,
		  struct extent_position *pos, struct kernel_lb_addr *eloc,
		  uint32_t *elen, sector_t *offset)
{
	unsigned char blocksize_bits = inode->i_sb->s_blocksize_bits;
	loff_t lbcount = 0, bcount = (loff_t) block << blocksize_bits;
	int8_t etype;
	struct udf_inode_info *iinfo;

	iinfo = UDF_I(inode);
	if (!udf_read_extent_cache(inode, bcount, &lbcount, pos)) {
		pos->offset = 0;
		pos->block = iinfo->i_location;
		pos->bh = NULL;
	}
	*elen = 0;
	do {
		etype = udf_next_aext(inode, pos, eloc, elen, 1);
		if (etype == -1) {
			*offset = (bcount - lbcount) >> blocksize_bits;
			iinfo->i_lenExtents = lbcount;
			return -1;
		}
		lbcount += *elen;
	} while (lbcount <= bcount);
	/* update extent cache */
	udf_update_extent_cache(inode, lbcount - *elen, pos);
	*offset = (bcount + *elen - lbcount) >> blocksize_bits;

	return etype;
}
