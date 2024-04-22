// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2022-2024 Oracle.
 * All rights reserved.
 */
#include "xfs.h"
#include "xfs_fs.h"
#include "xfs_format.h"
#include "xfs_da_format.h"
#include "xfs_log_format.h"
#include "xfs_shared.h"
#include "xfs_trans_resv.h"
#include "xfs_mount.h"
#include "xfs_bmap_btree.h"
#include "xfs_inode.h"
#include "xfs_error.h"
#include "xfs_trace.h"
#include "xfs_trans.h"
#include "xfs_da_btree.h"
#include "xfs_attr.h"
#include "xfs_dir2.h"
#include "xfs_dir2_priv.h"
#include "xfs_attr_sf.h"
#include "xfs_bmap.h"
#include "xfs_defer.h"
#include "xfs_log.h"
#include "xfs_xattr.h"
#include "xfs_parent.h"
#include "xfs_trans_space.h"

/*
 * Parent pointer attribute handling.
 *
 * Because the attribute name is a filename component, it will never be longer
 * than 255 bytes and must not contain nulls or slashes.  These are roughly the
 * same constraints that apply to attribute names.
 *
 * The attribute value must always be a struct xfs_parent_rec.  This means the
 * attribute will never be in remote format because 12 bytes is nowhere near
 * xfs_attr_leaf_entsize_local_max() (~75% of block size).
 *
 * Creating a new parent attribute will always create a new attribute - there
 * should never, ever be an existing attribute in the tree for a new inode.
 * ENOSPC behavior is problematic - creating the inode without the parent
 * pointer is effectively a corruption, so we allow parent attribute creation
 * to dip into the reserve block pool to avoid unexpected ENOSPC errors from
 * occurring.
 */

/* Return true if parent pointer attr name is valid. */
bool
xfs_parent_namecheck(
	unsigned int			attr_flags,
	const void			*name,
	size_t				length)
{
	/*
	 * Parent pointers always use logged operations, so there should never
	 * be incomplete xattrs.
	 */
	if (attr_flags & XFS_ATTR_INCOMPLETE)
		return false;

	return xfs_dir2_namecheck(name, length);
}

/* Return true if parent pointer attr value is valid. */
bool
xfs_parent_valuecheck(
	struct xfs_mount		*mp,
	const void			*value,
	size_t				valuelen)
{
	const struct xfs_parent_rec	*rec = value;

	if (!xfs_has_parent(mp))
		return false;

	/* The xattr value must be a parent record. */
	if (valuelen != sizeof(struct xfs_parent_rec))
		return false;

	/* The parent record must be local. */
	if (value == NULL)
		return false;

	/* The parent inumber must be valid. */
	if (!xfs_verify_dir_ino(mp, be64_to_cpu(rec->p_ino)))
		return false;

	return true;
}

/* Compute the attribute name hash for a parent pointer. */
xfs_dahash_t
xfs_parent_hashval(
	struct xfs_mount		*mp,
	const uint8_t			*name,
	int				namelen,
	xfs_ino_t			parent_ino)
{
	struct xfs_name			xname = {
		.name			= name,
		.len			= namelen,
	};

	/*
	 * Use the same dirent name hash as would be used on the directory, but
	 * mix in the parent inode number to avoid collisions on hardlinked
	 * files with identical names but different parents.
	 */
	return xfs_dir2_hashname(mp, &xname) ^
		upper_32_bits(parent_ino) ^ lower_32_bits(parent_ino);
}

/* Compute the attribute name hash from the xattr components. */
xfs_dahash_t
xfs_parent_hashattr(
	struct xfs_mount		*mp,
	const uint8_t			*name,
	int				namelen,
	const void			*value,
	int				valuelen)
{
	const struct xfs_parent_rec	*rec = value;

	/* Requires a local attr value in xfs_parent_rec format */
	if (valuelen != sizeof(struct xfs_parent_rec)) {
		ASSERT(valuelen == sizeof(struct xfs_parent_rec));
		return 0;
	}

	if (!value) {
		ASSERT(value != NULL);
		return 0;
	}

	return xfs_parent_hashval(mp, name, namelen, be64_to_cpu(rec->p_ino));
}
