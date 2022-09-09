// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2014-2016, Intel Corporation.
 */
#include "test/nfit_test.h"
#include <linux/blkdev.h>
#include <linux/dax.h>
#include <pmem.h>
#include <nd.h>

long __pmem_direct_access(struct pmem_device *pmem, pgoff_t pgoff,
		long nr_pages, enum dax_access_mode mode, void **kaddr,
		pfn_t *pfn)
{
	resource_size_t offset = PFN_PHYS(pgoff) + pmem->data_offset;

	if (unlikely(is_bad_pmem(&pmem->bb, PFN_PHYS(pgoff) / 512,
					PFN_PHYS(nr_pages))))
		return -EIO;

	/*
	 * Limit dax to a single page at a time given vmalloc()-backed
	 * in the nfit_test case.
	 */
	if (get_nfit_res(pmem->phys_addr + offset)) {
		struct page *page;

		if (kaddr)
			*kaddr = pmem->virt_addr + offset;
		page = vmalloc_to_page(pmem->virt_addr + offset);
		if (pfn)
			*pfn = page_to_pfn_t(page);
		pr_debug_ratelimited("%s: pmem: %p pgoff: %#lx pfn: %#lx\n",
				__func__, pmem, pgoff, page_to_pfn(page));

		return 1;
	}

	if (kaddr)
		*kaddr = pmem->virt_addr + offset;
	if (pfn)
		*pfn = phys_to_pfn_t(pmem->phys_addr + offset, pmem->pfn_flags);

	/*
	 * If badblocks are present, limit known good range to the
	 * requested range.
	 */
	if (unlikely(pmem->bb.count))
		return nr_pages;
	return PHYS_PFN(pmem->size - pmem->pfn_pad - offset);
}
