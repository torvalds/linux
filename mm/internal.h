/* internal.h: mm/ internal definitions
 *
 * Copyright (C) 2004 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

static inline void set_page_refs(struct page *page, int order)
{
#ifdef CONFIG_MMU
	set_page_count(page, 1);
#else
	int i;

	/*
	 * We need to reference all the pages for this order, otherwise if
	 * anyone accesses one of the pages with (get/put) it will be freed.
	 * - eg: access_process_vm()
	 */
	for (i = 0; i < (1 << order); i++)
		set_page_count(page + i, 1);
#endif /* CONFIG_MMU */
}

extern void fastcall __init __free_pages_bootmem(struct page *page,
						unsigned int order);
