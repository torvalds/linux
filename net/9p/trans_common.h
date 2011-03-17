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

/* TRUE if it is user context */
#define P9_IS_USER_CONTEXT (!segment_eq(get_fs(), KERNEL_DS))

/**
 * struct trans_rpage_info - To store mapped page information in PDU.
 * @rp_alloc:Set if this structure is allocd, not a reuse unused space in pdu.
 * @rp_nr_pages: Number of mapped pages
 * @rp_data: Array of page pointers
 */
struct trans_rpage_info {
	u8 rp_alloc;
	int rp_nr_pages;
	struct page *rp_data[0];
};

void p9_release_req_pages(struct trans_rpage_info *);
int p9_payload_gup(struct p9_req_t *, size_t *, int *, int, u8);
int p9_nr_pages(struct p9_req_t *);
