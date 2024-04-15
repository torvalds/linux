/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Copyright (c) 2021-2024 Oracle.  All Rights Reserved.
 * Author: Darrick J. Wong <djwong@kernel.org>
 */
#ifndef __XFS_SCRUB_XFBLOB_H__
#define __XFS_SCRUB_XFBLOB_H__

struct xfblob {
	struct xfile	*xfile;
	loff_t		last_offset;
};

typedef loff_t		xfblob_cookie;

int xfblob_create(const char *descr, struct xfblob **blobp);
void xfblob_destroy(struct xfblob *blob);
int xfblob_load(struct xfblob *blob, xfblob_cookie cookie, void *ptr,
		uint32_t size);
int xfblob_store(struct xfblob *blob, xfblob_cookie *cookie, const void *ptr,
		uint32_t size);
int xfblob_free(struct xfblob *blob, xfblob_cookie cookie);

#endif /* __XFS_SCRUB_XFBLOB_H__ */
