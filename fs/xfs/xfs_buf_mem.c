// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (c) 2023-2024 Oracle.  All Rights Reserved.
 * Author: Darrick J. Wong <djwong@kernel.org>
 */
#include "xfs.h"
#include "xfs_fs.h"
#include "xfs_buf.h"
#include "xfs_buf_mem.h"
#include "xfs_trace.h"
#include <linux/shmem_fs.h>

/*
 * Buffer Cache for In-Memory Files
 * ================================
 *
 * Online fsck wants to create ephemeral ordered recordsets.  The existing
 * btree infrastructure can do this, but we need the buffer cache to target
 * memory instead of block devices.
 *
 * When CONFIG_TMPFS=y, shmemfs is enough of a filesystem to meet those
 * requirements.  Therefore, the xmbuf mechanism uses an unlinked shmem file to
 * store our staging data.  This file is not installed in the file descriptor
 * table so that user programs cannot access the data, which means that the
 * xmbuf must be freed with xmbuf_destroy.
 *
 * xmbufs assume that the caller will handle all required concurrency
 * management; standard vfs locks (freezer and inode) are not taken.  Reads
 * and writes are satisfied directly from the page cache.
 *
 * The only supported block size is PAGE_SIZE, and we cannot use highmem.
 */

/*
 * shmem files used to back an in-memory buffer cache must not be exposed to
 * userspace.  Upper layers must coordinate access to the one handle returned
 * by the constructor, so establish a separate lock class for xmbufs to avoid
 * confusing lockdep.
 */
static struct lock_class_key xmbuf_i_mutex_key;

/*
 * Allocate a buffer cache target for a memory-backed file and set up the
 * buffer target.
 */
int
xmbuf_alloc(
	struct xfs_mount	*mp,
	const char		*descr,
	struct xfs_buftarg	**btpp)
{
	struct file		*file;
	struct inode		*inode;
	struct xfs_buftarg	*btp;
	int			error;

	btp = kzalloc(struct_size(btp, bt_cache, 1), GFP_KERNEL);
	if (!btp)
		return -ENOMEM;

	file = shmem_kernel_file_setup(descr, 0, 0);
	if (IS_ERR(file)) {
		error = PTR_ERR(file);
		goto out_free_btp;
	}
	inode = file_inode(file);

	/* private file, private locking */
	lockdep_set_class(&inode->i_rwsem, &xmbuf_i_mutex_key);

	/*
	 * We don't want to bother with kmapping data during repair, so don't
	 * allow highmem pages to back this mapping.
	 */
	mapping_set_gfp_mask(inode->i_mapping, GFP_KERNEL);

	/* ensure all writes are below EOF to avoid pagecache zeroing */
	i_size_write(inode, inode->i_sb->s_maxbytes);

	trace_xmbuf_create(btp);

	error = xfs_buf_cache_init(btp->bt_cache);
	if (error)
		goto out_file;

	/* Initialize buffer target */
	btp->bt_mount = mp;
	btp->bt_dev = (dev_t)-1U;
	btp->bt_bdev = NULL; /* in-memory buftargs have no bdev */
	btp->bt_file = file;
	btp->bt_meta_sectorsize = XMBUF_BLOCKSIZE;
	btp->bt_meta_sectormask = XMBUF_BLOCKSIZE - 1;

	error = xfs_init_buftarg(btp, XMBUF_BLOCKSIZE, descr);
	if (error)
		goto out_bcache;

	*btpp = btp;
	return 0;

out_bcache:
	xfs_buf_cache_destroy(btp->bt_cache);
out_file:
	fput(file);
out_free_btp:
	kfree(btp);
	return error;
}

/* Free a buffer cache target for a memory-backed buffer cache. */
void
xmbuf_free(
	struct xfs_buftarg	*btp)
{
	ASSERT(xfs_buftarg_is_mem(btp));
	ASSERT(percpu_counter_sum(&btp->bt_io_count) == 0);

	trace_xmbuf_free(btp);

	xfs_destroy_buftarg(btp);
	xfs_buf_cache_destroy(btp->bt_cache);
	fput(btp->bt_file);
	kfree(btp);
}

/* Directly map a shmem page into the buffer cache. */
int
xmbuf_map_page(
	struct xfs_buf		*bp)
{
	struct inode		*inode = file_inode(bp->b_target->bt_file);
	struct folio		*folio = NULL;
	struct page		*page;
	loff_t                  pos = BBTOB(xfs_buf_daddr(bp));
	int			error;

	ASSERT(xfs_buftarg_is_mem(bp->b_target));

	if (bp->b_map_count != 1)
		return -ENOMEM;
	if (BBTOB(bp->b_length) != XMBUF_BLOCKSIZE)
		return -ENOMEM;
	if (offset_in_page(pos) != 0) {
		ASSERT(offset_in_page(pos));
		return -ENOMEM;
	}

	error = shmem_get_folio(inode, pos >> PAGE_SHIFT, &folio, SGP_CACHE);
	if (error)
		return error;

	if (filemap_check_wb_err(inode->i_mapping, 0)) {
		folio_unlock(folio);
		folio_put(folio);
		return -EIO;
	}

	page = folio_file_page(folio, pos >> PAGE_SHIFT);

	/*
	 * Mark the page dirty so that it won't be reclaimed once we drop the
	 * (potentially last) reference in xmbuf_unmap_page.
	 */
	set_page_dirty(page);
	unlock_page(page);

	bp->b_addr = page_address(page);
	bp->b_pages = bp->b_page_array;
	bp->b_pages[0] = page;
	bp->b_page_count = 1;
	return 0;
}

/* Unmap a shmem page that was mapped into the buffer cache. */
void
xmbuf_unmap_page(
	struct xfs_buf		*bp)
{
	struct page		*page = bp->b_pages[0];

	ASSERT(xfs_buftarg_is_mem(bp->b_target));

	put_page(page);

	bp->b_addr = NULL;
	bp->b_pages[0] = NULL;
	bp->b_pages = NULL;
	bp->b_page_count = 0;
}

/* Is this a valid daddr within the buftarg? */
bool
xmbuf_verify_daddr(
	struct xfs_buftarg	*btp,
	xfs_daddr_t		daddr)
{
	struct inode		*inode = file_inode(btp->bt_file);

	ASSERT(xfs_buftarg_is_mem(btp));

	return daddr < (inode->i_sb->s_maxbytes >> BBSHIFT);
}
