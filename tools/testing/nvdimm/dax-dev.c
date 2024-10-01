// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2016, Intel Corporation.
 */
#include "test/nfit_test.h"
#include <linux/mm.h>
#include "../../../drivers/dax/dax-private.h"

phys_addr_t dax_pgoff_to_phys(struct dev_dax *dev_dax, pgoff_t pgoff,
		unsigned long size)
{
	int i;

	for (i = 0; i < dev_dax->nr_range; i++) {
		struct dev_dax_range *dax_range = &dev_dax->ranges[i];
		struct range *range = &dax_range->range;
		unsigned long long pgoff_end;
		phys_addr_t addr;

		pgoff_end = dax_range->pgoff + PHYS_PFN(range_len(range)) - 1;
		if (pgoff < dax_range->pgoff || pgoff > pgoff_end)
			continue;
		addr = PFN_PHYS(pgoff - dax_range->pgoff) + range->start;
		if (addr + size - 1 <= range->end) {
			if (get_nfit_res(addr)) {
				struct page *page;

				if (dev_dax->region->align > PAGE_SIZE)
					return -1;

				page = vmalloc_to_page((void *)addr);
				return PFN_PHYS(page_to_pfn(page));
			}
			return addr;
		}
		break;
	}
	return -1;
}
