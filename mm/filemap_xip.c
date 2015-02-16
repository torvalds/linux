/*
 *	linux/mm/filemap_xip.c
 *
 * Copyright (C) 2005 IBM Corporation
 * Author: Carsten Otte <cotte@de.ibm.com>
 *
 * derived from linux/mm/filemap.c - Copyright (C) Linus Torvalds
 *
 */

#include <linux/fs.h>
#include <linux/backing-dev.h>
#include <linux/pagemap.h>
#include <linux/export.h>
#include <linux/uio.h>
#include <linux/rmap.h>
#include <linux/mmu_notifier.h>
#include <linux/sched.h>
#include <linux/seqlock.h>
#include <linux/mutex.h>
#include <linux/gfp.h>
#include <asm/tlbflush.h>
#include <asm/io.h>

/*
 * truncate a page used for execute in place
 * functionality is analog to block_truncate_page but does use get_xip_mem
 * to get the page instead of page cache
 */
int
xip_truncate_page(struct address_space *mapping, loff_t from)
{
	pgoff_t index = from >> PAGE_CACHE_SHIFT;
	unsigned offset = from & (PAGE_CACHE_SIZE-1);
	unsigned blocksize;
	unsigned length;
	void *xip_mem;
	unsigned long xip_pfn;
	int err;

	BUG_ON(!mapping->a_ops->get_xip_mem);

	blocksize = 1 << mapping->host->i_blkbits;
	length = offset & (blocksize - 1);

	/* Block boundary? Nothing to do */
	if (!length)
		return 0;

	length = blocksize - length;

	err = mapping->a_ops->get_xip_mem(mapping, index, 0,
						&xip_mem, &xip_pfn);
	if (unlikely(err)) {
		if (err == -ENODATA)
			/* Hole? No need to truncate */
			return 0;
		else
			return err;
	}
	memset(xip_mem + offset, 0, length);
	return 0;
}
EXPORT_SYMBOL_GPL(xip_truncate_page);
