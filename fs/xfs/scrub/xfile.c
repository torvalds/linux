// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2018-2023 Oracle.  All Rights Reserved.
 * Author: Darrick J. Wong <djwong@kernel.org>
 */
#include "xfs.h"
#include "xfs_fs.h"
#include "xfs_shared.h"
#include "xfs_format.h"
#include "xfs_log_format.h"
#include "xfs_trans_resv.h"
#include "xfs_mount.h"
#include "scrub/scrub.h"
#include "scrub/xfile.h"
#include "scrub/xfarray.h"
#include "scrub/trace.h"
#include <linux/shmem_fs.h>

/*
 * Swappable Temporary Memory
 * ==========================
 *
 * Online checking sometimes needs to be able to stage a large amount of data
 * in memory.  This information might not fit in the available memory and it
 * doesn't all need to be accessible at all times.  In other words, we want an
 * indexed data buffer to store data that can be paged out.
 *
 * When CONFIG_TMPFS=y, shmemfs is enough of a filesystem to meet those
 * requirements.  Therefore, the xfile mechanism uses an unlinked shmem file to
 * store our staging data.  This file is not installed in the file descriptor
 * table so that user programs cannot access the data, which means that the
 * xfile must be freed with xfile_destroy.
 *
 * xfiles assume that the caller will handle all required concurrency
 * management; standard vfs locks (freezer and inode) are not taken.  Reads
 * and writes are satisfied directly from the page cache.
 */

/*
 * xfiles must not be exposed to userspace and require upper layers to
 * coordinate access to the one handle returned by the constructor, so
 * establish a separate lock class for xfiles to avoid confusing lockdep.
 */
static struct lock_class_key xfile_i_mutex_key;

/*
 * Create an xfile of the given size.  The description will be used in the
 * trace output.
 */
int
xfile_create(
	const char		*description,
	loff_t			isize,
	struct xfile		**xfilep)
{
	struct inode		*inode;
	struct xfile		*xf;
	int			error;

	xf = kmalloc(sizeof(struct xfile), XCHK_GFP_FLAGS);
	if (!xf)
		return -ENOMEM;

	xf->file = shmem_kernel_file_setup(description, isize, VM_NORESERVE);
	if (IS_ERR(xf->file)) {
		error = PTR_ERR(xf->file);
		goto out_xfile;
	}

	inode = file_inode(xf->file);
	lockdep_set_class(&inode->i_rwsem, &xfile_i_mutex_key);

	/*
	 * We don't want to bother with kmapping data during repair, so don't
	 * allow highmem pages to back this mapping.
	 */
	mapping_set_gfp_mask(inode->i_mapping, GFP_KERNEL);

	trace_xfile_create(xf);

	*xfilep = xf;
	return 0;
out_xfile:
	kfree(xf);
	return error;
}

/* Close the file and release all resources. */
void
xfile_destroy(
	struct xfile		*xf)
{
	struct inode		*inode = file_inode(xf->file);

	trace_xfile_destroy(xf);

	lockdep_set_class(&inode->i_rwsem, &inode->i_sb->s_type->i_mutex_key);
	fput(xf->file);
	kfree(xf);
}

/*
 * Load an object.  Since we're treating this file as "memory", any error or
 * short IO is treated as a failure to allocate memory.
 */
int
xfile_load(
	struct xfile		*xf,
	void			*buf,
	size_t			count,
	loff_t			pos)
{
	struct inode		*inode = file_inode(xf->file);
	unsigned int		pflags;

	if (count > MAX_RW_COUNT)
		return -ENOMEM;
	if (inode->i_sb->s_maxbytes - pos < count)
		return -ENOMEM;

	trace_xfile_load(xf, pos, count);

	pflags = memalloc_nofs_save();
	while (count > 0) {
		struct folio	*folio;
		unsigned int	len;
		unsigned int	offset;

		if (shmem_get_folio(inode, pos >> PAGE_SHIFT, &folio,
				SGP_READ) < 0)
			break;
		if (!folio) {
			/*
			 * No data stored at this offset, just zero the output
			 * buffer until the next page boundary.
			 */
			len = min_t(ssize_t, count,
				PAGE_SIZE - offset_in_page(pos));
			memset(buf, 0, len);
		} else {
			if (filemap_check_wb_err(inode->i_mapping, 0)) {
				folio_unlock(folio);
				folio_put(folio);
				break;
			}

			offset = offset_in_folio(folio, pos);
			len = min_t(ssize_t, count, folio_size(folio) - offset);
			memcpy(buf, folio_address(folio) + offset, len);

			folio_unlock(folio);
			folio_put(folio);
		}
		count -= len;
		pos += len;
		buf += len;
	}
	memalloc_nofs_restore(pflags);

	if (count)
		return -ENOMEM;
	return 0;
}

/*
 * Store an object.  Since we're treating this file as "memory", any error or
 * short IO is treated as a failure to allocate memory.
 */
int
xfile_store(
	struct xfile		*xf,
	const void		*buf,
	size_t			count,
	loff_t			pos)
{
	struct inode		*inode = file_inode(xf->file);
	unsigned int		pflags;

	if (count > MAX_RW_COUNT)
		return -ENOMEM;
	if (inode->i_sb->s_maxbytes - pos < count)
		return -ENOMEM;

	trace_xfile_store(xf, pos, count);

	/*
	 * Increase the file size first so that shmem_get_folio(..., SGP_CACHE),
	 * actually allocates a folio instead of erroring out.
	 */
	if (pos + count > i_size_read(inode))
		i_size_write(inode, pos + count);

	pflags = memalloc_nofs_save();
	while (count > 0) {
		struct folio	*folio;
		unsigned int	len;
		unsigned int	offset;

		if (shmem_get_folio(inode, pos >> PAGE_SHIFT, &folio,
				SGP_CACHE) < 0)
			break;
		if (filemap_check_wb_err(inode->i_mapping, 0)) {
			folio_unlock(folio);
			folio_put(folio);
			break;
		}

		offset = offset_in_folio(folio, pos);
		len = min_t(ssize_t, count, folio_size(folio) - offset);
		memcpy(folio_address(folio) + offset, buf, len);

		folio_mark_dirty(folio);
		folio_unlock(folio);
		folio_put(folio);

		count -= len;
		pos += len;
		buf += len;
	}
	memalloc_nofs_restore(pflags);

	if (count)
		return -ENOMEM;
	return 0;
}

/* Find the next written area in the xfile data for a given offset. */
loff_t
xfile_seek_data(
	struct xfile		*xf,
	loff_t			pos)
{
	loff_t			ret;

	ret = vfs_llseek(xf->file, pos, SEEK_DATA);
	trace_xfile_seek_data(xf, pos, ret);
	return ret;
}

/*
 * Grab the (locked) folio for a memory object.  The object cannot span a folio
 * boundary.  Returns the locked folio if successful, NULL if there was no
 * folio or it didn't cover the range requested, or an ERR_PTR on failure.
 */
struct folio *
xfile_get_folio(
	struct xfile		*xf,
	loff_t			pos,
	size_t			len,
	unsigned int		flags)
{
	struct inode		*inode = file_inode(xf->file);
	struct folio		*folio = NULL;
	unsigned int		pflags;
	int			error;

	if (inode->i_sb->s_maxbytes - pos < len)
		return ERR_PTR(-ENOMEM);

	trace_xfile_get_folio(xf, pos, len);

	/*
	 * Increase the file size first so that shmem_get_folio(..., SGP_CACHE),
	 * actually allocates a folio instead of erroring out.
	 */
	if ((flags & XFILE_ALLOC) && pos + len > i_size_read(inode))
		i_size_write(inode, pos + len);

	pflags = memalloc_nofs_save();
	error = shmem_get_folio(inode, pos >> PAGE_SHIFT, &folio,
			(flags & XFILE_ALLOC) ? SGP_CACHE : SGP_READ);
	memalloc_nofs_restore(pflags);
	if (error)
		return ERR_PTR(error);

	if (!folio)
		return NULL;

	if (len > folio_size(folio) - offset_in_folio(folio, pos)) {
		folio_unlock(folio);
		folio_put(folio);
		return NULL;
	}

	if (filemap_check_wb_err(inode->i_mapping, 0)) {
		folio_unlock(folio);
		folio_put(folio);
		return ERR_PTR(-EIO);
	}

	/*
	 * Mark the folio dirty so that it won't be reclaimed once we drop the
	 * (potentially last) reference in xfile_put_folio.
	 */
	if (flags & XFILE_ALLOC)
		folio_mark_dirty(folio);
	return folio;
}

/*
 * Release the (locked) folio for a memory object.
 */
void
xfile_put_folio(
	struct xfile		*xf,
	struct folio		*folio)
{
	trace_xfile_put_folio(xf, folio_pos(folio), folio_size(folio));

	folio_unlock(folio);
	folio_put(folio);
}

/* Discard the page cache that's backing a range of the xfile. */
void
xfile_discard(
	struct xfile		*xf,
	loff_t			pos,
	u64			count)
{
	trace_xfile_discard(xf, pos, count);

	shmem_truncate_range(file_inode(xf->file), pos, pos + count - 1);
}
