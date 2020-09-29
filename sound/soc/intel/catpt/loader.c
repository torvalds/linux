// SPDX-License-Identifier: GPL-2.0-only
//
// Copyright(c) 2020 Intel Corporation. All rights reserved.
//
// Author: Cezary Rojewski <cezary.rojewski@intel.com>
//

#include <linux/dma-mapping.h>
#include <linux/slab.h>
#include "core.h"

void catpt_sram_init(struct resource *sram, u32 start, u32 size)
{
	sram->start = start;
	sram->end = start + size - 1;
}

void catpt_sram_free(struct resource *sram)
{
	struct resource *res, *save;

	for (res = sram->child; res;) {
		save = res->sibling;
		release_resource(res);
		kfree(res);
		res = save;
	}
}

struct resource *
catpt_request_region(struct resource *root, resource_size_t size)
{
	struct resource *res = root->child;
	resource_size_t addr = root->start;

	for (;;) {
		if (res->start - addr >= size)
			break;
		addr = res->end + 1;
		res = res->sibling;
		if (!res)
			return NULL;
	}

	return __request_region(root, addr, size, NULL, 0);
}
