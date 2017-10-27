/*
 * Copyright (c) 2016, Intel Corporation.
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
#include <linux/mm.h>
#include "../../../drivers/dax/dax-private.h"

phys_addr_t dax_pgoff_to_phys(struct dev_dax *dev_dax, pgoff_t pgoff,
		unsigned long size)
{
	struct resource *res;
	phys_addr_t addr;
	int i;

	for (i = 0; i < dev_dax->num_resources; i++) {
		res = &dev_dax->res[i];
		addr = pgoff * PAGE_SIZE + res->start;
		if (addr >= res->start && addr <= res->end)
			break;
		pgoff -= PHYS_PFN(resource_size(res));
	}

	if (i < dev_dax->num_resources) {
		res = &dev_dax->res[i];
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
