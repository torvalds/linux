/*
 * Copyright (c) 2006 Oracle.  All rights reserved.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * OpenIB.org BSD license below:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and/or other materials
 *        provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 */
#include <linux/highmem.h>
#include <linux/gfp.h>
#include <linux/cpu.h>
#include <linux/export.h>

#include "rds.h"

struct rds_page_remainder {
	struct page	*r_page;
	unsigned long	r_offset;
};

static
DEFINE_PER_CPU_SHARED_ALIGNED(struct rds_page_remainder, rds_page_remainders);

/**
 * rds_page_remainder_alloc - build up regions of a message.
 *
 * @scat: Scatter list for message
 * @bytes: the number of bytes needed.
 * @gfp: the waiting behaviour of the allocation
 *
 * @gfp is always ored with __GFP_HIGHMEM.  Callers must be prepared to
 * kmap the pages, etc.
 *
 * If @bytes is at least a full page then this just returns a page from
 * alloc_page().
 *
 * If @bytes is a partial page then this stores the unused region of the
 * page in a per-cpu structure.  Future partial-page allocations may be
 * satisfied from that cached region.  This lets us waste less memory on
 * small allocations with minimal complexity.  It works because the transmit
 * path passes read-only page regions down to devices.  They hold a page
 * reference until they are done with the region.
 */
int rds_page_remainder_alloc(struct scatterlist *scat, unsigned long bytes,
			     gfp_t gfp)
{
	struct rds_page_remainder *rem;
	unsigned long flags;
	struct page *page;
	int ret;

	gfp |= __GFP_HIGHMEM;

	/* jump straight to allocation if we're trying for a huge page */
	if (bytes >= PAGE_SIZE) {
		page = alloc_page(gfp);
		if (!page) {
			ret = -ENOMEM;
		} else {
			sg_set_page(scat, page, PAGE_SIZE, 0);
			ret = 0;
		}
		goto out;
	}

	rem = &per_cpu(rds_page_remainders, get_cpu());
	local_irq_save(flags);

	while (1) {
		/* avoid a tiny region getting stuck by tossing it */
		if (rem->r_page && bytes > (PAGE_SIZE - rem->r_offset)) {
			rds_stats_inc(s_page_remainder_miss);
			__free_page(rem->r_page);
			rem->r_page = NULL;
		}

		/* hand out a fragment from the cached page */
		if (rem->r_page && bytes <= (PAGE_SIZE - rem->r_offset)) {
			sg_set_page(scat, rem->r_page, bytes, rem->r_offset);
			get_page(sg_page(scat));

			if (rem->r_offset != 0)
				rds_stats_inc(s_page_remainder_hit);

			rem->r_offset += ALIGN(bytes, 8);
			if (rem->r_offset >= PAGE_SIZE) {
				__free_page(rem->r_page);
				rem->r_page = NULL;
			}
			ret = 0;
			break;
		}

		/* alloc if there is nothing for us to use */
		local_irq_restore(flags);
		put_cpu();

		page = alloc_page(gfp);

		rem = &per_cpu(rds_page_remainders, get_cpu());
		local_irq_save(flags);

		if (!page) {
			ret = -ENOMEM;
			break;
		}

		/* did someone race to fill the remainder before us? */
		if (rem->r_page) {
			__free_page(page);
			continue;
		}

		/* otherwise install our page and loop around to alloc */
		rem->r_page = page;
		rem->r_offset = 0;
	}

	local_irq_restore(flags);
	put_cpu();
out:
	rdsdebug("bytes %lu ret %d %p %u %u\n", bytes, ret,
		 ret ? NULL : sg_page(scat), ret ? 0 : scat->offset,
		 ret ? 0 : scat->length);
	return ret;
}
EXPORT_SYMBOL_GPL(rds_page_remainder_alloc);

void rds_page_exit(void)
{
	unsigned int cpu;

	for_each_possible_cpu(cpu) {
		struct rds_page_remainder *rem;

		rem = &per_cpu(rds_page_remainders, cpu);
		rdsdebug("cpu %u\n", cpu);

		if (rem->r_page)
			__free_page(rem->r_page);
		rem->r_page = NULL;
	}
}
