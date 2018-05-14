// SPDX-License-Identifier: GPL-2.0 OR MIT

/*
 * Xen para-virtual sound device
 *
 * Copyright (C) 2016-2018 EPAM Systems Inc.
 *
 * Author: Oleksandr Andrushchenko <oleksandr_andrushchenko@epam.com>
 */

#include <linux/kernel.h>
#include <xen/xen.h>
#include <xen/xenbus.h>

#include "xen_snd_front_shbuf.h"

grant_ref_t xen_snd_front_shbuf_get_dir_start(struct xen_snd_front_shbuf *buf)
{
	if (!buf->grefs)
		return GRANT_INVALID_REF;

	return buf->grefs[0];
}

void xen_snd_front_shbuf_clear(struct xen_snd_front_shbuf *buf)
{
	memset(buf, 0, sizeof(*buf));
}

void xen_snd_front_shbuf_free(struct xen_snd_front_shbuf *buf)
{
	int i;

	if (buf->grefs) {
		for (i = 0; i < buf->num_grefs; i++)
			if (buf->grefs[i] != GRANT_INVALID_REF)
				gnttab_end_foreign_access(buf->grefs[i],
							  0, 0UL);
		kfree(buf->grefs);
	}
	kfree(buf->directory);
	free_pages_exact(buf->buffer, buf->buffer_sz);
	xen_snd_front_shbuf_clear(buf);
}

/*
 * number of grant references a page can hold with respect to the
 * xensnd_page_directory header
 */
#define XENSND_NUM_GREFS_PER_PAGE ((XEN_PAGE_SIZE - \
		offsetof(struct xensnd_page_directory, gref)) / \
		sizeof(grant_ref_t))

static void fill_page_dir(struct xen_snd_front_shbuf *buf,
			  int num_pages_dir)
{
	struct xensnd_page_directory *page_dir;
	unsigned char *ptr;
	int i, cur_gref, grefs_left, to_copy;

	ptr = buf->directory;
	grefs_left = buf->num_grefs - num_pages_dir;
	/*
	 * skip grant references at the beginning, they are for pages granted
	 * for the page directory itself
	 */
	cur_gref = num_pages_dir;
	for (i = 0; i < num_pages_dir; i++) {
		page_dir = (struct xensnd_page_directory *)ptr;
		if (grefs_left <= XENSND_NUM_GREFS_PER_PAGE) {
			to_copy = grefs_left;
			page_dir->gref_dir_next_page = GRANT_INVALID_REF;
		} else {
			to_copy = XENSND_NUM_GREFS_PER_PAGE;
			page_dir->gref_dir_next_page = buf->grefs[i + 1];
		}

		memcpy(&page_dir->gref, &buf->grefs[cur_gref],
		       to_copy * sizeof(grant_ref_t));

		ptr += XEN_PAGE_SIZE;
		grefs_left -= to_copy;
		cur_gref += to_copy;
	}
}

static int grant_references(struct xenbus_device *xb_dev,
			    struct xen_snd_front_shbuf *buf,
			    int num_pages_dir, int num_pages_buffer,
			    int num_grefs)
{
	grant_ref_t priv_gref_head;
	unsigned long frame;
	int ret, i, j, cur_ref;
	int otherend_id;

	ret = gnttab_alloc_grant_references(num_grefs, &priv_gref_head);
	if (ret)
		return ret;

	buf->num_grefs = num_grefs;
	otherend_id = xb_dev->otherend_id;
	j = 0;

	for (i = 0; i < num_pages_dir; i++) {
		cur_ref = gnttab_claim_grant_reference(&priv_gref_head);
		if (cur_ref < 0) {
			ret = cur_ref;
			goto fail;
		}

		frame = xen_page_to_gfn(virt_to_page(buf->directory +
						     XEN_PAGE_SIZE * i));
		gnttab_grant_foreign_access_ref(cur_ref, otherend_id, frame, 0);
		buf->grefs[j++] = cur_ref;
	}

	for (i = 0; i < num_pages_buffer; i++) {
		cur_ref = gnttab_claim_grant_reference(&priv_gref_head);
		if (cur_ref < 0) {
			ret = cur_ref;
			goto fail;
		}

		frame = xen_page_to_gfn(virt_to_page(buf->buffer +
						     XEN_PAGE_SIZE * i));
		gnttab_grant_foreign_access_ref(cur_ref, otherend_id, frame, 0);
		buf->grefs[j++] = cur_ref;
	}

	gnttab_free_grant_references(priv_gref_head);
	fill_page_dir(buf, num_pages_dir);
	return 0;

fail:
	gnttab_free_grant_references(priv_gref_head);
	return ret;
}

static int alloc_int_buffers(struct xen_snd_front_shbuf *buf,
			     int num_pages_dir, int num_pages_buffer,
			     int num_grefs)
{
	buf->grefs = kcalloc(num_grefs, sizeof(*buf->grefs), GFP_KERNEL);
	if (!buf->grefs)
		return -ENOMEM;

	buf->directory = kcalloc(num_pages_dir, XEN_PAGE_SIZE, GFP_KERNEL);
	if (!buf->directory)
		goto fail;

	buf->buffer_sz = num_pages_buffer * XEN_PAGE_SIZE;
	buf->buffer = alloc_pages_exact(buf->buffer_sz, GFP_KERNEL);
	if (!buf->buffer)
		goto fail;

	return 0;

fail:
	kfree(buf->grefs);
	buf->grefs = NULL;
	kfree(buf->directory);
	buf->directory = NULL;
	return -ENOMEM;
}

int xen_snd_front_shbuf_alloc(struct xenbus_device *xb_dev,
			      struct xen_snd_front_shbuf *buf,
			      unsigned int buffer_sz)
{
	int num_pages_buffer, num_pages_dir, num_grefs;
	int ret;

	xen_snd_front_shbuf_clear(buf);

	num_pages_buffer = DIV_ROUND_UP(buffer_sz, XEN_PAGE_SIZE);
	/* number of pages the page directory consumes itself */
	num_pages_dir = DIV_ROUND_UP(num_pages_buffer,
				     XENSND_NUM_GREFS_PER_PAGE);
	num_grefs = num_pages_buffer + num_pages_dir;

	ret = alloc_int_buffers(buf, num_pages_dir,
				num_pages_buffer, num_grefs);
	if (ret < 0)
		return ret;

	ret = grant_references(xb_dev, buf, num_pages_dir, num_pages_buffer,
			       num_grefs);
	if (ret < 0)
		return ret;

	fill_page_dir(buf, num_pages_dir);
	return 0;
}
