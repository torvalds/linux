/*
 * Copyright IBM Corporation, 2010
 * Author Venkateswararao Jujjuri <jvrao@linux.vnet.ibm.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of version 2.1 of the GNU Lesser General Public License
 * as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it would be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *
 */

#include <linux/mm.h>
#include <linux/module.h>

/**
 *  p9_release_req_pages - Release pages after the transaction.
 */
void p9_release_pages(struct page **pages, int nr_pages)
{
	int i;

	for (i = 0; i < nr_pages; i++)
		if (pages[i])
			put_page(pages[i]);
}
EXPORT_SYMBOL(p9_release_pages);
