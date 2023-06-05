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
	/* Bitmap of used space in xattr leaf blocks and shortform forks. */
	unsigned long		*usedmap;

	/* Bitmap of free space in xattr leaf blocks. */
	unsigned long		*freemap;

	/* Memory buffer used to extract xattr values. */
	void			*value;
	size_t			value_sz;
};

#endif	/* __XFS_SCRUB_ATTR_H__ */
