/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Copyright (C) 2019-2023 Oracle.  All Rights Reserved.
 * Author: Darrick J. Wong <djwong@kernel.org>
 */
#ifndef __XFS_SCRUB_ATTR_H__
#define __XFS_SCRUB_ATTR_H__

/*
 * Temporary storage for online scrub and repair of extended attributes.
 */
struct xchk_xattr_buf {
	/* Bitmap of used space in xattr leaf blocks. */
	unsigned long		*usedmap;

	/* Bitmap of free space in xattr leaf blocks. */
	unsigned long		*freemap;

	/* Size of @buf, in bytes. */
	size_t			sz;

	/*
	 * Memory buffer -- used for extracting attr values while walking the
	 * attributes.
	 */
	uint8_t			buf[];
};

/* A place to store attribute values. */
static inline uint8_t *
xchk_xattr_valuebuf(
	struct xfs_scrub	*sc)
{
	struct xchk_xattr_buf	*ab = sc->buf;

	return ab->buf;
}

#endif	/* __XFS_SCRUB_ATTR_H__ */
