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
	/* Bitmap of free space in xattr leaf blocks. */
	unsigned long		*freemap;

	/* Size of @buf, in bytes. */
	size_t			sz;

	/*
	 * Memory buffer -- either used for extracting attr values while
	 * walking the attributes; or for computing attr block bitmaps when
	 * checking the attribute tree.
	 *
	 * Each bitmap contains enough bits to track every byte in an attr
	 * block (rounded up to the size of an unsigned long).  The attr block
	 * used space bitmap starts at the beginning of the buffer.
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

/* A bitmap of space usage computed by walking an attr leaf block. */
static inline unsigned long *
xchk_xattr_usedmap(
	struct xfs_scrub	*sc)
{
	struct xchk_xattr_buf	*ab = sc->buf;

	return (unsigned long *)ab->buf;
}

#endif	/* __XFS_SCRUB_ATTR_H__ */
