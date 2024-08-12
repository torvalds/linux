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
#include "xfs_attr_item.h"
#include "xfs_health.h"

struct kmem_cache		*xfs_parent_args_cache;

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

/*
 * Initialize the parent pointer arguments structure.  Caller must have zeroed
 * the contents of @args.  @tp is only required for updates.
 */
static void
xfs_parent_da_args_init(
	struct xfs_da_args	*args,
	struct xfs_trans	*tp,
	struct xfs_parent_rec	*rec,
	struct xfs_inode	*child,
	xfs_ino_t		owner,
	const struct xfs_name	*parent_name)
{
	args->geo = child->i_mount->m_attr_geo;
	args->whichfork = XFS_ATTR_FORK;
	args->attr_filter = XFS_ATTR_PARENT;
	args->op_flags = XFS_DA_OP_LOGGED | XFS_DA_OP_OKNOENT;
	args->trans = tp;
	args->dp = child;
	args->owner = owner;
	args->name = parent_name->name;
	args->namelen = parent_name->len;
	args->value = rec;
	args->valuelen = sizeof(struct xfs_parent_rec);
	xfs_attr_sethash(args);
}

/* Make sure the incore state is ready for a parent pointer query/update. */
static inline int
xfs_parent_iread_extents(
	struct xfs_trans	*tp,
	struct xfs_inode	*child)
{
	/* Parent pointers require that the attr fork must exist. */
	if (XFS_IS_CORRUPT(child->i_mount, !xfs_inode_has_attr_fork(child))) {
		xfs_inode_mark_sick(child, XFS_SICK_INO_PARENT);
		return -EFSCORRUPTED;
	}

	return xfs_iread_extents(tp, child, XFS_ATTR_FORK);
}

/* Add a parent pointer to reflect a dirent addition. */
int
xfs_parent_addname(
	struct xfs_trans	*tp,
	struct xfs_parent_args	*ppargs,
	struct xfs_inode	*dp,
	const struct xfs_name	*parent_name,
	struct xfs_inode	*child)
{
	int			error;

	error = xfs_parent_iread_extents(tp, child);
	if (error)
		return error;

	xfs_inode_to_parent_rec(&ppargs->rec, dp);
	xfs_parent_da_args_init(&ppargs->args, tp, &ppargs->rec, child,
			child->i_ino, parent_name);
	xfs_attr_defer_add(&ppargs->args, XFS_ATTR_DEFER_SET);
	return 0;
}

/* Remove a parent pointer to reflect a dirent removal. */
int
xfs_parent_removename(
	struct xfs_trans	*tp,
	struct xfs_parent_args	*ppargs,
	struct xfs_inode	*dp,
	const struct xfs_name	*parent_name,
	struct xfs_inode	*child)
{
	int			error;

	error = xfs_parent_iread_extents(tp, child);
	if (error)
		return error;

	xfs_inode_to_parent_rec(&ppargs->rec, dp);
	xfs_parent_da_args_init(&ppargs->args, tp, &ppargs->rec, child,
			child->i_ino, parent_name);
	xfs_attr_defer_add(&ppargs->args, XFS_ATTR_DEFER_REMOVE);
	return 0;
}

/* Replace one parent pointer with another to reflect a rename. */
int
xfs_parent_replacename(
	struct xfs_trans	*tp,
	struct xfs_parent_args	*ppargs,
	struct xfs_inode	*old_dp,
	const struct xfs_name	*old_name,
	struct xfs_inode	*new_dp,
	const struct xfs_name	*new_name,
	struct xfs_inode	*child)
{
	int			error;

	error = xfs_parent_iread_extents(tp, child);
	if (error)
		return error;

	xfs_inode_to_parent_rec(&ppargs->rec, old_dp);
	xfs_parent_da_args_init(&ppargs->args, tp, &ppargs->rec, child,
			child->i_ino, old_name);

	xfs_inode_to_parent_rec(&ppargs->new_rec, new_dp);
	ppargs->args.new_name = new_name->name;
	ppargs->args.new_namelen = new_name->len;
	ppargs->args.new_value = &ppargs->new_rec;
	ppargs->args.new_valuelen = sizeof(struct xfs_parent_rec);
	xfs_attr_defer_add(&ppargs->args, XFS_ATTR_DEFER_REPLACE);
	return 0;
}

/*
 * Extract parent pointer information from any parent pointer xattr into
 * @parent_ino/gen.  The last two parameters can be NULL pointers.
 *
 * Returns 0 if this is not a parent pointer xattr at all; or -EFSCORRUPTED for
 * garbage.
 */
int
xfs_parent_from_attr(
	struct xfs_mount	*mp,
	unsigned int		attr_flags,
	const unsigned char	*name,
	unsigned int		namelen,
	const void		*value,
	unsigned int		valuelen,
	xfs_ino_t		*parent_ino,
	uint32_t		*parent_gen)
{
	const struct xfs_parent_rec	*rec = value;

	ASSERT(attr_flags & XFS_ATTR_PARENT);

	if (!xfs_parent_namecheck(attr_flags, name, namelen))
		return -EFSCORRUPTED;
	if (!xfs_parent_valuecheck(mp, value, valuelen))
		return -EFSCORRUPTED;

	if (parent_ino)
		*parent_ino = be64_to_cpu(rec->p_ino);
	if (parent_gen)
		*parent_gen = be32_to_cpu(rec->p_gen);
	return 0;
}

/*
 * Look up a parent pointer record (@parent_name -> @pptr) of @ip.
 *
 * Caller must hold at least ILOCK_SHARED.  The scratchpad need not be
 * initialized.
 *
 * Returns 0 if the pointer is found, -ENOATTR if there is no match, or a
 * negative errno.
 */
int
xfs_parent_lookup(
	struct xfs_trans		*tp,
	struct xfs_inode		*ip,
	const struct xfs_name		*parent_name,
	struct xfs_parent_rec		*pptr,
	struct xfs_da_args		*scratch)
{
	memset(scratch, 0, sizeof(struct xfs_da_args));
	xfs_parent_da_args_init(scratch, tp, pptr, ip, ip->i_ino, parent_name);
	return xfs_attr_get_ilocked(scratch);
}

/* Sanity-check a parent pointer before we try to perform repairs. */
static inline bool
xfs_parent_sanity_check(
	struct xfs_mount		*mp,
	const struct xfs_name		*parent_name,
	const struct xfs_parent_rec	*pptr)
{
	if (!xfs_parent_namecheck(XFS_ATTR_PARENT, parent_name->name,
				parent_name->len))
		return false;

	if (!xfs_parent_valuecheck(mp, pptr, sizeof(*pptr)))
		return false;

	return true;
}


/*
 * Attach the parent pointer (@parent_name -> @pptr) to @ip immediately.
 * Caller must not have a transaction or hold the ILOCK.  This is for
 * specialized repair functions only.  The scratchpad need not be initialized.
 */
int
xfs_parent_set(
	struct xfs_inode	*ip,
	xfs_ino_t		owner,
	const struct xfs_name	*parent_name,
	struct xfs_parent_rec	*pptr,
	struct xfs_da_args	*scratch)
{
	if (!xfs_parent_sanity_check(ip->i_mount, parent_name, pptr)) {
		ASSERT(0);
		return -EFSCORRUPTED;
	}

	memset(scratch, 0, sizeof(struct xfs_da_args));
	xfs_parent_da_args_init(scratch, NULL, pptr, ip, owner, parent_name);
	return xfs_attr_set(scratch, XFS_ATTRUPDATE_CREATE, false);
}

/*
 * Remove the parent pointer (@parent_name -> @pptr) from @ip immediately.
 * Caller must not have a transaction or hold the ILOCK.  This is for
 * specialized repair functions only.  The scratchpad need not be initialized.
 */
int
xfs_parent_unset(
	struct xfs_inode		*ip,
	xfs_ino_t			owner,
	const struct xfs_name		*parent_name,
	struct xfs_parent_rec		*pptr,
	struct xfs_da_args		*scratch)
{
	if (!xfs_parent_sanity_check(ip->i_mount, parent_name, pptr)) {
		ASSERT(0);
		return -EFSCORRUPTED;
	}

	memset(scratch, 0, sizeof(struct xfs_da_args));
	xfs_parent_da_args_init(scratch, NULL, pptr, ip, owner, parent_name);
	return xfs_attr_set(scratch, XFS_ATTRUPDATE_REMOVE, false);
}
