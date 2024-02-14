/* SPDX-License-Identifier: GPL-2.0 OR MIT */

/*
 * Xen frontend/backend page directory based shared buffer
 * helper module.
 *
 * Copyright (C) 2018 EPAM Systems Inc.
 *
 * Author: Oleksandr Andrushchenko <oleksandr_andrushchenko@epam.com>
 */

#ifndef __XEN_FRONT_PGDIR_SHBUF_H_
#define __XEN_FRONT_PGDIR_SHBUF_H_

#include <linux/kernel.h>

#include <xen/grant_table.h>

struct xen_front_pgdir_shbuf_ops;

struct xen_front_pgdir_shbuf {
	/*
	 * Number of references granted for the backend use:
	 *
	 *  - for frontend allocated/imported buffers this holds the number
	 *    of grant references for the page directory and the pages
	 *    of the buffer
	 *
	 *  - for the buffer provided by the backend this only holds the number
	 *    of grant references for the page directory itself as grant
	 *    references for the buffer will be provided by the backend.
	 */
	int num_grefs;
	grant_ref_t *grefs;
	/* Page directory backing storage. */
	u8 *directory;

	/*
	 * Number of pages for the shared buffer itself (excluding the page
	 * directory).
	 */
	int num_pages;
	/*
	 * Backing storage of the shared buffer: these are the pages being
	 * shared.
	 */
	struct page **pages;

	struct xenbus_device *xb_dev;

	/* These are the ops used internally depending on be_alloc mode. */
	const struct xen_front_pgdir_shbuf_ops *ops;

	/* Xen map handles for the buffer allocated by the backend. */
	grant_handle_t *backend_map_handles;
};

struct xen_front_pgdir_shbuf_cfg {
	struct xenbus_device *xb_dev;

	/* Number of pages of the buffer backing storage. */
	int num_pages;
	/* Pages of the buffer to be shared. */
	struct page **pages;

	/*
	 * This is allocated outside because there are use-cases when
	 * the buffer structure is allocated as a part of a bigger one.
	 */
	struct xen_front_pgdir_shbuf *pgdir;
	/*
	 * Mode of grant reference sharing: if set then backend will share
	 * grant references to the buffer with the frontend.
	 */
	int be_alloc;
};

int xen_front_pgdir_shbuf_alloc(struct xen_front_pgdir_shbuf_cfg *cfg);

grant_ref_t
xen_front_pgdir_shbuf_get_dir_start(struct xen_front_pgdir_shbuf *buf);

int xen_front_pgdir_shbuf_map(struct xen_front_pgdir_shbuf *buf);

int xen_front_pgdir_shbuf_unmap(struct xen_front_pgdir_shbuf *buf);

void xen_front_pgdir_shbuf_free(struct xen_front_pgdir_shbuf *buf);

#endif /* __XEN_FRONT_PGDIR_SHBUF_H_ */
