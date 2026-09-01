/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef __MM_MM_INIT_H
#define __MM_MM_INIT_H

void memblock_free_pages(unsigned long pfn, unsigned int order)
{
}

static inline void accept_memory(phys_addr_t start, unsigned long size)
{
}

unsigned long free_reserved_area(void *start, void *end, int poison, const char *s);
void free_reserved_page(struct page *page);

static inline bool deferred_pages_enabled(void)
{
	return false;
}

static inline void init_deferred_page(unsigned long pfn, int nid)
{
}
#endif /* __MM_MM_INIT_H */
