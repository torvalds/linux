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
	struct resource *res = &dev_dax->region->res;
	phys_addr_t addr;

	addr = pgoff * PAGE_SIZE + res->start;
	if (addr >= res->start && addr <= res->end) {
		if (addr + size - 1 <= res->end) {
			if (get_nfit_res(addr)) {
				struct page *page;

				if (dev_dax->region->align > PAGE_SIZE)
					return -1;

				page = vmalloc_to_page((void *)addr);
				return PFN_PHYS(page_to_pfn(page));
			} else
				return addr;
		}
	}
	return -1;
}
