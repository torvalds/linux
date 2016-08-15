/*
 * Copyright (c) 2014-2016, Intel Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 */
#include "test/nfit_test.h"
#include <linux/blkdev.h>
#include <pmem.h>
#include <nd.h>

long pmem_direct_access(struct block_device *bdev, sector_t sector,
		void **kaddr, pfn_t *pfn, long size)
{
	struct pmem_device *pmem = bdev->bd_queue->queuedata;
	resource_size_t offset = sector * 512 + pmem->data_offset;

	if (unlikely(is_bad_pmem(&pmem->bb, sector, size)))
		return -EIO;

	/*
	 * Limit dax to a single page at a time given vmalloc()-backed
	 * in the nfit_test case.
	 */
	if (get_nfit_res(pmem->phys_addr + offset)) {
		struct page *page;

		*kaddr = pmem->virt_addr + offset;
		page = vmalloc_to_page(pmem->virt_addr + offset);
		*pfn = page_to_pfn_t(page);
		dev_dbg_ratelimited(disk_to_dev(bdev->bd_disk)->parent,
				"%s: sector: %#llx pfn: %#lx\n", __func__,
				(unsigned long long) sector, page_to_pfn(page));

		return PAGE_SIZE;
	}

	*kaddr = pmem->virt_addr + offset;
	*pfn = phys_to_pfn_t(pmem->phys_addr + offset, pmem->pfn_flags);

	/*
	 * If badblocks are present, limit known good range to the
	 * requested range.
	 */
	if (unlikely(pmem->bb.count))
		return size;
	return pmem->size - pmem->pfn_pad - offset;
}
