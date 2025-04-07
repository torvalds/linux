/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef _MM_INTERNAL_H
#define _MM_INTERNAL_H

/*
 * Enable memblock_dbg() messages
 */
#ifdef MEMBLOCK_DEBUG
static int memblock_debug = 1;
#endif

#define pr_warn_ratelimited(fmt, ...)    printf(fmt, ##__VA_ARGS__)

bool mirrored_kernelcore = false;

struct page {};

void memblock_free_pages(struct page *page, unsigned long pfn,
			 unsigned int order)
{
}

static inline void accept_memory(phys_addr_t start, unsigned long size)
{
}

static inline unsigned long free_reserved_area(void *start, void *end,
					       int poison, const char *s)
{
	return 0;
}

#endif
