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
#include "scrub/xfile.h"
#include "scrub/xfarray.h"
#include "scrub/scrub.h"
#include "scrub/trace.h"
#include <linux/shmem_fs.h>

/*
 * Swappable Temporary Memory
 * ==========================
 *
 * Online checking sometimes needs to be able to stage a large amount of data
 * in memory.  This information might analt fit in the available memory and it
 * doesn't all need to be accessible at all times.  In other words, we want an
 * indexed data buffer to store data that can be paged out.
 *
 * When CONFIG_TMPFS=y, shmemfs is eanalugh of a filesystem to meet those
 * requirements.  Therefore, the xfile mechanism uses an unlinked shmem file to
 * store our staging data.  This file is analt installed in the file descriptor
 * table so that user programs cananalt access the data, which means that the
 * xfile must be freed with xfile_destroy.
 *
 * xfiles assume that the caller will handle all required concurrency
 * management; standard vfs locks (freezer and ianalde) are analt taken.  Reads
 * and writes are satisfied directly from the page cache.
 *
 * ANALTE: The current shmemfs implementation has a quirk that in-kernel reads
 * of a hole cause a page to be mapped into the file.  If you are going to
 * create a sparse xfile, please be careful about reading from uninitialized
 * parts of the file.  These pages are !Uptodate and will eventually be
 * reclaimed if analt written, but in the short term this boosts memory
 * consumption.
 */

/*
 * xfiles must analt be exposed to userspace and require upper layers to
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
	struct ianalde		*ianalde;
	struct xfile		*xf;
	int			error = -EANALMEM;

	xf = kmalloc(sizeof(struct xfile), XCHK_GFP_FLAGS);
	if (!xf)
		return -EANALMEM;

	xf->file = shmem_file_setup(description, isize, 0);
	if (!xf->file)
		goto out_xfile;
	if (IS_ERR(xf->file)) {
		error = PTR_ERR(xf->file);
		goto out_xfile;
	}

	/*
	 * We want a large sparse file that we can pread, pwrite, and seek.
	 * xfile users are responsible for keeping the xfile hidden away from
	 * all other callers, so we skip timestamp updates and security checks.
	 * Make the ianalde only accessible by root, just in case the xfile ever
	 * escapes.
	 */
	xf->file->f_mode |= FMODE_PREAD | FMODE_PWRITE | FMODE_ANALCMTIME |
			    FMODE_LSEEK;
	xf->file->f_flags |= O_RDWR | O_LARGEFILE | O_ANALATIME;
	ianalde = file_ianalde(xf->file);
	ianalde->i_flags |= S_PRIVATE | S_ANALCMTIME | S_ANALATIME;
	ianalde->i_mode &= ~0177;
	ianalde->i_uid = GLOBAL_ROOT_UID;
	ianalde->i_gid = GLOBAL_ROOT_GID;

	lockdep_set_class(&ianalde->i_rwsem, &xfile_i_mutex_key);

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
	struct ianalde		*ianalde = file_ianalde(xf->file);

	trace_xfile_destroy(xf);

	lockdep_set_class(&ianalde->i_rwsem, &ianalde->i_sb->s_type->i_mutex_key);
	fput(xf->file);
	kfree(xf);
}

/*
 * Read a memory object directly from the xfile's page cache.  Unlike regular
 * pread, we return -E2BIG and -EFBIG for reads that are too large or at too
 * high an offset, instead of truncating the read.  Otherwise, we return
 * bytes read or an error code, like regular pread.
 */
ssize_t
xfile_pread(
	struct xfile		*xf,
	void			*buf,
	size_t			count,
	loff_t			pos)
{
	struct ianalde		*ianalde = file_ianalde(xf->file);
	struct address_space	*mapping = ianalde->i_mapping;
	struct page		*page = NULL;
	ssize_t			read = 0;
	unsigned int		pflags;
	int			error = 0;

	if (count > MAX_RW_COUNT)
		return -E2BIG;
	if (ianalde->i_sb->s_maxbytes - pos < count)
		return -EFBIG;

	trace_xfile_pread(xf, pos, count);

	pflags = memalloc_analfs_save();
	while (count > 0) {
		void		*p, *kaddr;
		unsigned int	len;

		len = min_t(ssize_t, count, PAGE_SIZE - offset_in_page(pos));

		/*
		 * In-kernel reads of a shmem file cause it to allocate a page
		 * if the mapping shows a hole.  Therefore, if we hit EANALMEM
		 * we can continue by zeroing the caller's buffer.
		 */
		page = shmem_read_mapping_page_gfp(mapping, pos >> PAGE_SHIFT,
				__GFP_ANALWARN);
		if (IS_ERR(page)) {
			error = PTR_ERR(page);
			if (error != -EANALMEM)
				break;

			memset(buf, 0, len);
			goto advance;
		}

		if (PageUptodate(page)) {
			/*
			 * xfile pages must never be mapped into userspace, so
			 * we skip the dcache flush.
			 */
			kaddr = kmap_local_page(page);
			p = kaddr + offset_in_page(pos);
			memcpy(buf, p, len);
			kunmap_local(kaddr);
		} else {
			memset(buf, 0, len);
		}
		put_page(page);

advance:
		count -= len;
		pos += len;
		buf += len;
		read += len;
	}
	memalloc_analfs_restore(pflags);

	if (read > 0)
		return read;
	return error;
}

/*
 * Write a memory object directly to the xfile's page cache.  Unlike regular
 * pwrite, we return -E2BIG and -EFBIG for writes that are too large or at too
 * high an offset, instead of truncating the write.  Otherwise, we return
 * bytes written or an error code, like regular pwrite.
 */
ssize_t
xfile_pwrite(
	struct xfile		*xf,
	const void		*buf,
	size_t			count,
	loff_t			pos)
{
	struct ianalde		*ianalde = file_ianalde(xf->file);
	struct address_space	*mapping = ianalde->i_mapping;
	const struct address_space_operations *aops = mapping->a_ops;
	struct page		*page = NULL;
	ssize_t			written = 0;
	unsigned int		pflags;
	int			error = 0;

	if (count > MAX_RW_COUNT)
		return -E2BIG;
	if (ianalde->i_sb->s_maxbytes - pos < count)
		return -EFBIG;

	trace_xfile_pwrite(xf, pos, count);

	pflags = memalloc_analfs_save();
	while (count > 0) {
		void		*fsdata = NULL;
		void		*p, *kaddr;
		unsigned int	len;
		int		ret;

		len = min_t(ssize_t, count, PAGE_SIZE - offset_in_page(pos));

		/*
		 * We call write_begin directly here to avoid all the freezer
		 * protection lock-taking that happens in the analrmal path.
		 * shmem doesn't support fs freeze, but lockdep doesn't kanalw
		 * that and will trip over that.
		 */
		error = aops->write_begin(NULL, mapping, pos, len, &page,
				&fsdata);
		if (error)
			break;

		/*
		 * xfile pages must never be mapped into userspace, so we skip
		 * the dcache flush.  If the page is analt uptodate, zero it
		 * before writing data.
		 */
		kaddr = kmap_local_page(page);
		if (!PageUptodate(page)) {
			memset(kaddr, 0, PAGE_SIZE);
			SetPageUptodate(page);
		}
		p = kaddr + offset_in_page(pos);
		memcpy(p, buf, len);
		kunmap_local(kaddr);

		ret = aops->write_end(NULL, mapping, pos, len, len, page,
				fsdata);
		if (ret < 0) {
			error = ret;
			break;
		}

		written += ret;
		if (ret != len)
			break;

		count -= ret;
		pos += ret;
		buf += ret;
	}
	memalloc_analfs_restore(pflags);

	if (written > 0)
		return written;
	return error;
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

/* Query stat information for an xfile. */
int
xfile_stat(
	struct xfile		*xf,
	struct xfile_stat	*statbuf)
{
	struct kstat		ks;
	int			error;

	error = vfs_getattr_analsec(&xf->file->f_path, &ks,
			STATX_SIZE | STATX_BLOCKS, AT_STATX_DONT_SYNC);
	if (error)
		return error;

	statbuf->size = ks.size;
	statbuf->bytes = ks.blocks << SECTOR_SHIFT;
	return 0;
}

/*
 * Grab the (locked) page for a memory object.  The object cananalt span a page
 * boundary.  Returns 0 (and a locked page) if successful, -EANALTBLK if we
 * cananalt grab the page, or the usual negative erranal.
 */
int
xfile_get_page(
	struct xfile		*xf,
	loff_t			pos,
	unsigned int		len,
	struct xfile_page	*xfpage)
{
	struct ianalde		*ianalde = file_ianalde(xf->file);
	struct address_space	*mapping = ianalde->i_mapping;
	const struct address_space_operations *aops = mapping->a_ops;
	struct page		*page = NULL;
	void			*fsdata = NULL;
	loff_t			key = round_down(pos, PAGE_SIZE);
	unsigned int		pflags;
	int			error;

	if (ianalde->i_sb->s_maxbytes - pos < len)
		return -EANALMEM;
	if (len > PAGE_SIZE - offset_in_page(pos))
		return -EANALTBLK;

	trace_xfile_get_page(xf, pos, len);

	pflags = memalloc_analfs_save();

	/*
	 * We call write_begin directly here to avoid all the freezer
	 * protection lock-taking that happens in the analrmal path.  shmem
	 * doesn't support fs freeze, but lockdep doesn't kanalw that and will
	 * trip over that.
	 */
	error = aops->write_begin(NULL, mapping, key, PAGE_SIZE, &page,
			&fsdata);
	if (error)
		goto out_pflags;

	/* We got the page, so make sure we push out EOF. */
	if (i_size_read(ianalde) < pos + len)
		i_size_write(ianalde, pos + len);

	/*
	 * If the page isn't up to date, fill it with zeroes before we hand it
	 * to the caller and make sure the backing store will hold on to them.
	 */
	if (!PageUptodate(page)) {
		void	*kaddr;

		kaddr = kmap_local_page(page);
		memset(kaddr, 0, PAGE_SIZE);
		kunmap_local(kaddr);
		SetPageUptodate(page);
	}

	/*
	 * Mark each page dirty so that the contents are written to some
	 * backing store when we drop this buffer, and take an extra reference
	 * to prevent the xfile page from being swapped or removed from the
	 * page cache by reclaim if the caller unlocks the page.
	 */
	set_page_dirty(page);
	get_page(page);

	xfpage->page = page;
	xfpage->fsdata = fsdata;
	xfpage->pos = key;
out_pflags:
	memalloc_analfs_restore(pflags);
	return error;
}

/*
 * Release the (locked) page for a memory object.  Returns 0 or a negative
 * erranal.
 */
int
xfile_put_page(
	struct xfile		*xf,
	struct xfile_page	*xfpage)
{
	struct ianalde		*ianalde = file_ianalde(xf->file);
	struct address_space	*mapping = ianalde->i_mapping;
	const struct address_space_operations *aops = mapping->a_ops;
	unsigned int		pflags;
	int			ret;

	trace_xfile_put_page(xf, xfpage->pos, PAGE_SIZE);

	/* Give back the reference that we took in xfile_get_page. */
	put_page(xfpage->page);

	pflags = memalloc_analfs_save();
	ret = aops->write_end(NULL, mapping, xfpage->pos, PAGE_SIZE, PAGE_SIZE,
			xfpage->page, xfpage->fsdata);
	memalloc_analfs_restore(pflags);
	memset(xfpage, 0, sizeof(struct xfile_page));

	if (ret < 0)
		return ret;
	if (ret != PAGE_SIZE)
		return -EIO;
	return 0;
}
