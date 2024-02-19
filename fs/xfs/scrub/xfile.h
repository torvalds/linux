/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Copyright (C) 2018-2023 Oracle.  All Rights Reserved.
 * Author: Darrick J. Wong <djwong@kernel.org>
 */
#ifndef __XFS_SCRUB_XFILE_H__
#define __XFS_SCRUB_XFILE_H__

struct xfile_page {
	struct page		*page;
	void			*fsdata;
	loff_t			pos;
};

static inline bool xfile_page_cached(const struct xfile_page *xfpage)
{
	return xfpage->page != NULL;
}

static inline pgoff_t xfile_page_index(const struct xfile_page *xfpage)
{
	return xfpage->page->index;
}

struct xfile {
	struct file		*file;
};

int xfile_create(const char *description, loff_t isize, struct xfile **xfilep);
void xfile_destroy(struct xfile *xf);

int xfile_load(struct xfile *xf, void *buf, size_t count, loff_t pos);
int xfile_store(struct xfile *xf, const void *buf, size_t count,
		loff_t pos);

loff_t xfile_seek_data(struct xfile *xf, loff_t pos);

int xfile_get_page(struct xfile *xf, loff_t offset, unsigned int len,
		struct xfile_page *xbuf);
int xfile_put_page(struct xfile *xf, struct xfile_page *xbuf);

#endif /* __XFS_SCRUB_XFILE_H__ */
