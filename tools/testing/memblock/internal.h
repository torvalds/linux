/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef _MM_INTERNAL_H
#define _MM_INTERNAL_H

struct page {};

void memblock_free_pages(struct page *page, unsigned long pfn,
			 unsigned int order)
{
}

#endif
