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
 *  @*private: PDU's private page of struct trans_rpage_info
 */
void
p9_release_req_pages(struct trans_rpage_info *rpinfo)
{
	int i = 0;

	while (rpinfo->rp_data[i] && rpinfo->rp_nr_pages--) {
		put_page(rpinfo->rp_data[i]);
		i++;
	}
}
EXPORT_SYMBOL(p9_release_req_pages);

/**
 * p9_nr_pages - Return number of pages needed to accommodate the payload.
 */
int
p9_nr_pages(struct p9_req_t *req)
{
	unsigned long start_page, end_page;
	start_page =  (unsigned long)req->tc->pubuf >> PAGE_SHIFT;
	end_page = ((unsigned long)req->tc->pubuf + req->tc->pbuf_size +
			PAGE_SIZE - 1) >> PAGE_SHIFT;
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
int
p9_payload_gup(struct p9_req_t *req, size_t *pdata_off, int *pdata_len,
		int nr_pages, u8 rw)
{
	uint32_t first_page_bytes = 0;
	int32_t pdata_mapped_pages;
	struct trans_rpage_info  *rpinfo;

	*pdata_off = (__force size_t)req->tc->pubuf & (PAGE_SIZE-1);

	if (*pdata_off)
		first_page_bytes = min(((size_t)PAGE_SIZE - *pdata_off),
				       req->tc->pbuf_size);

	rpinfo = req->tc->private;
	pdata_mapped_pages = get_user_pages_fast((unsigned long)req->tc->pubuf,
			nr_pages, rw, &rpinfo->rp_data[0]);
	if (pdata_mapped_pages <= 0)
		return pdata_mapped_pages;

	rpinfo->rp_nr_pages = pdata_mapped_pages;
	if (*pdata_off) {
		*pdata_len = first_page_bytes;
		*pdata_len += min((req->tc->pbuf_size - *pdata_len),
				((size_t)pdata_mapped_pages - 1) << PAGE_SHIFT);
	} else {
		*pdata_len = min(req->tc->pbuf_size,
				(size_t)pdata_mapped_pages << PAGE_SHIFT);
	}
	return 0;
}
EXPORT_SYMBOL(p9_payload_gup);
