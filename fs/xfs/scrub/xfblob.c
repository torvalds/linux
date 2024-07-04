// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (c) 2021-2024 Oracle.  All Rights Reserved.
 * Author: Darrick J. Wong <djwong@kernel.org>
 */
#include "xfs.h"
#include "xfs_fs.h"
#include "xfs_shared.h"
#include "xfs_format.h"
#include "scrub/scrub.h"
#include "scrub/xfile.h"
#include "scrub/xfarray.h"
#include "scrub/xfblob.h"

/*
 * XFS Blob Storage
 * ================
 * Stores and retrieves blobs using an xfile.  Objects are appended to the file
 * and the offset is returned as a magic cookie for retrieval.
 */

#define XB_KEY_MAGIC	0xABAADDAD
struct xb_key {
	uint32_t		xb_magic;  /* XB_KEY_MAGIC */
	uint32_t		xb_size;   /* size of the blob, in bytes */
	loff_t			xb_offset; /* byte offset of this key */
	/* blob comes after here */
} __packed;

/* Initialize a blob storage object. */
int
xfblob_create(
	const char		*description,
	struct xfblob		**blobp)
{
	struct xfblob		*blob;
	struct xfile		*xfile;
	int			error;

	error = xfile_create(description, 0, &xfile);
	if (error)
		return error;

	blob = kmalloc(sizeof(struct xfblob), XCHK_GFP_FLAGS);
	if (!blob) {
		error = -ENOMEM;
		goto out_xfile;
	}

	blob->xfile = xfile;
	blob->last_offset = PAGE_SIZE;

	*blobp = blob;
	return 0;

out_xfile:
	xfile_destroy(xfile);
	return error;
}

/* Destroy a blob storage object. */
void
xfblob_destroy(
	struct xfblob	*blob)
{
	xfile_destroy(blob->xfile);
	kfree(blob);
}

/* Retrieve a blob. */
int
xfblob_load(
	struct xfblob	*blob,
	xfblob_cookie	cookie,
	void		*ptr,
	uint32_t	size)
{
	struct xb_key	key;
	int		error;

	error = xfile_load(blob->xfile, &key, sizeof(key), cookie);
	if (error)
		return error;

	if (key.xb_magic != XB_KEY_MAGIC || key.xb_offset != cookie) {
		ASSERT(0);
		return -ENODATA;
	}
	if (size < key.xb_size) {
		ASSERT(0);
		return -EFBIG;
	}

	return xfile_load(blob->xfile, ptr, key.xb_size,
			cookie + sizeof(key));
}

/* Store a blob. */
int
xfblob_store(
	struct xfblob	*blob,
	xfblob_cookie	*cookie,
	const void	*ptr,
	uint32_t	size)
{
	struct xb_key	key = {
		.xb_offset = blob->last_offset,
		.xb_magic = XB_KEY_MAGIC,
		.xb_size = size,
	};
	loff_t		pos = blob->last_offset;
	int		error;

	error = xfile_store(blob->xfile, &key, sizeof(key), pos);
	if (error)
		return error;

	pos += sizeof(key);
	error = xfile_store(blob->xfile, ptr, size, pos);
	if (error)
		goto out_err;

	*cookie = blob->last_offset;
	blob->last_offset += sizeof(key) + size;
	return 0;
out_err:
	xfile_discard(blob->xfile, blob->last_offset, sizeof(key));
	return error;
}

/* Free a blob. */
int
xfblob_free(
	struct xfblob	*blob,
	xfblob_cookie	cookie)
{
	struct xb_key	key;
	int		error;

	error = xfile_load(blob->xfile, &key, sizeof(key), cookie);
	if (error)
		return error;

	if (key.xb_magic != XB_KEY_MAGIC || key.xb_offset != cookie) {
		ASSERT(0);
		return -ENODATA;
	}

	xfile_discard(blob->xfile, cookie, sizeof(key) + key.xb_size);
	return 0;
}

/* How many bytes is this blob storage object consuming? */
unsigned long long
xfblob_bytes(
	struct xfblob		*blob)
{
	return xfile_bytes(blob->xfile);
}

/* Drop all the blobs. */
void
xfblob_truncate(
	struct xfblob	*blob)
{
	xfile_discard(blob->xfile, PAGE_SIZE, MAX_LFS_FILESIZE - PAGE_SIZE);
	blob->last_offset = PAGE_SIZE;
}
