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

#include <linux/slab.h>
#include <linux/module.h>
#include <net/9p/9p.h>
#include <net/9p/client.h>
#include <linux/scatterlist.h>
#include "trans_common.h"

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

/**
 * p9_nr_pages - Return number of pages needed to accommodate the payload.
 */
int p9_nr_pages(char *data, int len)
{
	unsigned long start_page, end_page;
	start_page =  (unsigned long)data >> PAGE_SHIFT;
	end_page = ((unsigned long)data + len + PAGE_SIZE - 1) >> PAGE_SHIFT;
	return end_page - start_page;
}
EXPORT_SYMBOL(p9_nr_pages);

/**
 * payload_gup - Translates user buffer into kernel pages and
 * pins them either for read/write through get_user_pages_fast().
 * @req: Request to be sent to server.
 * @pdata_off: data offset into the first page after translation (gup).
 * @pdata_len: Total length of the IO. gup may not return requested # of pages.
 * @nr_pages: number of pages to accommodate the payload
 * @rw: Indicates if the pages are for read or write.
 */

int p9_payload_gup(char *data, int *nr_pages, struct page **pages, int write)
{
	int nr_mapped_pages;

	nr_mapped_pages = get_user_pages_fast((unsigned long)data,
					      *nr_pages, write, pages);
	if (nr_mapped_pages <= 0)
		return nr_mapped_pages;

	*nr_pages = nr_mapped_pages;
	return 0;
}
EXPORT_SYMBOL(p9_payload_gup);
