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
		void __pmem **kaddr, pfn_t *pfn, long size)
{
	struct pmem_device *pmem = bdev->bd_queue->queuedata;
	resource_size_t offset = sector * 512 + pmem->data_offset;

	/* disable DAX for nfit_test pmem devices */
	if (get_nfit_res(pmem->phys_addr + offset)) {
		dev_info_once(pmem->bb.dev, "dax is disabled for nfit_test\n");
		return -EIO;
	}

	if (unlikely(is_bad_pmem(&pmem->bb, sector, size)))
		return -EIO;
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
