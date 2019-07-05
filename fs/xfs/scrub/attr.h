/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Copyright (C) 2019 Oracle.  All Rights Reserved.
 * Author: Darrick J. Wong <darrick.wong@oracle.com>
 */
#ifndef __XFS_SCRUB_ATTR_H__
#define __XFS_SCRUB_ATTR_H__

/*
 * Temporary storage for online scrub and repair of extended attributes.
 */
struct xchk_xattr_buf {
	/*
	 * Memory buffer -- either used for extracting attr values while
	 * walking the attributes; or for computing attr block bitmaps when
	 * checking the attribute tree.
	 *
	 * Each bitmap contains enough bits to track every byte in an attr
	 * block (rounded up to the size of an unsigned long).  The attr block
	 * used space bitmap starts at the beginning of the buffer; the free
	 * space bitmap follows immediately after; and we have a third buffer
	 * for storing intermediate bitmap results.
	 */
	uint8_t			buf[0];
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

/* A bitmap of free space computed by walking attr leaf block free info. */
static inline unsigned long *
xchk_xattr_freemap(
	struct xfs_scrub	*sc)
{
	return xchk_xattr_usedmap(sc) +
			BITS_TO_LONGS(sc->mp->m_attr_geo->blksize);
}

/* A bitmap used to hold temporary results. */
static inline unsigned long *
xchk_xattr_dstmap(
	struct xfs_scrub	*sc)
{
	return xchk_xattr_freemap(sc) +
			BITS_TO_LONGS(sc->mp->m_attr_geo->blksize);
}

#endif	/* __XFS_SCRUB_ATTR_H__ */
