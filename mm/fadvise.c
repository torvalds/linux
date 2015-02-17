/*
 * mm/fadvise.c
 *
 * Copyright (C) 2002, Linus Torvalds
 *
 * 11Jan2003	Andrew Morton
 *		Initial version.
 */

#include <linux/kernel.h>
#include <linux/file.h>
#include <linux/fs.h>
#include <linux/mm.h>
#include <linux/pagemap.h>
#include <linux/backing-dev.h>
#include <linux/pagevec.h>
#include <linux/fadvise.h>
#include <linux/writeback.h>
#include <linux/syscalls.h>
#include <linux/swap.h>

#include <asm/unistd.h>

/*
 * POSIX_FADV_WILLNEED could set PG_Referenced, and POSIX_FADV_NOREUSE could
 * deactivate the pages and clear PG_Referenced.
 */
SYSCALL_DEFINE4(fadvise64_64, int, fd, loff_t, offset, loff_t, len, int, advice)
{
	struct fd f = fdget(fd);
	struct inode *inode;
	struct address_space *mapping;
	struct backing_dev_info *bdi;
	loff_t endbyte;			/* inclusive */
	pgoff_t start_index;
	pgoff_t end_index;
	unsigned long nrpages;
	int ret = 0;

	if (!f.file)
		return -EBADF;

	inode = file_inode(f.file);
	if (S_ISFIFO(inode->i_mode)) {
		ret = -ESPIPE;
		goto out;
	}

	mapping = f.file->f_mapping;
	if (!mapping || len < 0) {
		ret = -EINVAL;
		goto out;
	}

	if (IS_DAX(inode)) {
		switch (advice) {
		case POSIX_FADV_NORMAL:
		case POSIX_FADV_RANDOM:
		case POSIX_FADV_SEQUENTIAL:
		case POSIX_FADV_WILLNEED:
		case POSIX_FADV_NOREUSE:
		case POSIX_FADV_DONTNEED:
			/* no bad return value, but ignore advice */
			break;
		default:
			ret = -EINVAL;
		}
		goto out;
	}

	/* Careful about overflows. Len == 0 means "as much as possible" */
	endbyte = offset + len;
	if (!len || endbyte < len)
		endbyte = -1;
	else
		endbyte--;		/* inclusive */

	bdi = inode_to_bdi(mapping->host);

	switch (advice) {
	case POSIX_FADV_NORMAL:
		f.file->f_ra.ra_pages = bdi->ra_pages;
		spin_lock(&f.file->f_lock);
		f.file->f_mode &= ~FMODE_RANDOM;
		spin_unlock(&f.file->f_lock);
		break;
	case POSIX_FADV_RANDOM:
		spin_lock(&f.file->f_lock);
		f.file->f_mode |= FMODE_RANDOM;
		spin_unlock(&f.file->f_lock);
		break;
	case POSIX_FADV_SEQUENTIAL:
		f.file->f_ra.ra_pages = bdi->ra_pages * 2;
		spin_lock(&f.file->f_lock);
		f.file->f_mode &= ~FMODE_RANDOM;
		spin_unlock(&f.file->f_lock);
		break;
	case POSIX_FADV_WILLNEED:
		/* First and last PARTIAL page! */
		start_index = offset >> PAGE_CACHE_SHIFT;
		end_index = endbyte >> PAGE_CACHE_SHIFT;

		/* Careful about overflow on the "+1" */
		nrpages = end_index - start_index + 1;
		if (!nrpages)
			nrpages = ~0UL;

		/*
		 * Ignore return value because fadvise() shall return
		 * success even if filesystem can't retrieve a hint,
		 */
		force_page_cache_readahead(mapping, f.file, start_index,
					   nrpages);
		break;
	case POSIX_FADV_NOREUSE:
		break;
	case POSIX_FADV_DONTNEED:
		if (!bdi_write_congested(bdi))
			__filemap_fdatawrite_range(mapping, offset, endbyte,
						   WB_SYNC_NONE);

		/*
		 * First and last FULL page! Partial pages are deliberately
		 * preserved on the expectation that it is better to preserve
		 * needed memory than to discard unneeded memory.
		 */
		start_index = (offset+(PAGE_CACHE_SIZE-1)) >> PAGE_CACHE_SHIFT;
		end_index = (endbyte >> PAGE_CACHE_SHIFT);

		if (end_index >= start_index) {
			unsigned long count = invalidate_mapping_pages(mapping,
						start_index, end_index);

			/*
			 * If fewer pages were invalidated than expected then
			 * it is possible that some of the pages were on
			 * a per-cpu pagevec for a remote CPU. Drain all
			 * pagevecs and try again.
			 */
			if (count < (end_index - start_index + 1)) {
				lru_add_drain_all();
				invalidate_mapping_pages(mapping, start_index,
						end_index);
			}
		}
		break;
	default:
		ret = -EINVAL;
	}
out:
	fdput(f);
	return ret;
}

#ifdef __ARCH_WANT_SYS_FADVISE64

SYSCALL_DEFINE4(fadvise64, int, fd, loff_t, offset, size_t, len, int, advice)
{
	return sys_fadvise64_64(fd, offset, len, advice);
}

#endif
