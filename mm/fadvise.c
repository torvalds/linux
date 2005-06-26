/*
 * mm/fadvise.c
 *
 * Copyright (C) 2002, Linus Torvalds
 *
 * 11Jan2003	akpm@digeo.com
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
#include <linux/syscalls.h>

#include <asm/unistd.h>

/*
 * POSIX_FADV_WILLNEED could set PG_Referenced, and POSIX_FADV_NOREUSE could
 * deactivate the pages and clear PG_Referenced.
 */
asmlinkage long sys_fadvise64_64(int fd, loff_t offset, loff_t len, int advice)
{
	struct file *file = fget(fd);
	struct address_space *mapping;
	struct backing_dev_info *bdi;
	loff_t endbyte;
	pgoff_t start_index;
	pgoff_t end_index;
	unsigned long nrpages;
	int ret = 0;

	if (!file)
		return -EBADF;

	mapping = file->f_mapping;
	if (!mapping || len < 0) {
		ret = -EINVAL;
		goto out;
	}

	if (mapping->a_ops->get_xip_page)
		/* no bad return value, but ignore advice */
		goto out;

	/* Careful about overflows. Len == 0 means "as much as possible" */
	endbyte = offset + len;
	if (!len || endbyte < len)
		endbyte = -1;

	bdi = mapping->backing_dev_info;

	switch (advice) {
	case POSIX_FADV_NORMAL:
		file->f_ra.ra_pages = bdi->ra_pages;
		break;
	case POSIX_FADV_RANDOM:
		file->f_ra.ra_pages = 0;
		break;
	case POSIX_FADV_SEQUENTIAL:
		file->f_ra.ra_pages = bdi->ra_pages * 2;
		break;
	case POSIX_FADV_WILLNEED:
	case POSIX_FADV_NOREUSE:
		if (!mapping->a_ops->readpage) {
			ret = -EINVAL;
			break;
		}

		/* First and last PARTIAL page! */
		start_index = offset >> PAGE_CACHE_SHIFT;
		end_index = (endbyte-1) >> PAGE_CACHE_SHIFT;

		/* Careful about overflow on the "+1" */
		nrpages = end_index - start_index + 1;
		if (!nrpages)
			nrpages = ~0UL;
		
		ret = force_page_cache_readahead(mapping, file,
				start_index,
				max_sane_readahead(nrpages));
		if (ret > 0)
			ret = 0;
		break;
	case POSIX_FADV_DONTNEED:
		if (!bdi_write_congested(mapping->backing_dev_info))
			filemap_flush(mapping);

		/* First and last FULL page! */
		start_index = (offset + (PAGE_CACHE_SIZE-1)) >> PAGE_CACHE_SHIFT;
		end_index = (endbyte >> PAGE_CACHE_SHIFT);

		if (end_index > start_index)
			invalidate_mapping_pages(mapping, start_index, end_index-1);
		break;
	default:
		ret = -EINVAL;
	}
out:
	fput(file);
	return ret;
}

#ifdef __ARCH_WANT_SYS_FADVISE64

asmlinkage long sys_fadvise64(int fd, loff_t offset, size_t len, int advice)
{
	return sys_fadvise64_64(fd, offset, len, advice);
}

#endif
