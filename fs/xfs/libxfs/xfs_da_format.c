// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2000,2002,2005 Silicon Graphics, Inc.
 * Copyright (c) 2013 Red Hat, Inc.
 * All Rights Reserved.
 */
#include "xfs.h"
#include "xfs_fs.h"
#include "xfs_shared.h"
#include "xfs_format.h"
#include "xfs_log_format.h"
#include "xfs_trans_resv.h"
#include "xfs_mount.h"
#include "xfs_inode.h"
#include "xfs_dir2.h"
#include "xfs_dir2_priv.h"

/*
 * Directory data block operations
 */

/*
 * For special situations, the dirent size ends up fixed because we always know
 * what the size of the entry is. That's true for the "." and "..", and
 * therefore we know that they are a fixed size and hence their offsets are
 * constant, as is the first entry.
 *
 * Hence, this calculation is written as a macro to be able to be calculated at
 * compile time and so certain offsets can be calculated directly in the
 * structure initaliser via the macro. There are two macros - one for dirents
 * with ftype and without so there are no unresolvable conditionals in the
 * calculations. We also use round_up() as XFS_DIR2_DATA_ALIGN is always a power
 * of 2 and the compiler doesn't reject it (unlike roundup()).
 */
#define XFS_DIR2_DATA_ENTSIZE(n)					\
	round_up((offsetof(struct xfs_dir2_data_entry, name[0]) + (n) +	\
		 sizeof(xfs_dir2_data_off_t)), XFS_DIR2_DATA_ALIGN)

#define XFS_DIR3_DATA_ENTSIZE(n)					\
	round_up((offsetof(struct xfs_dir2_data_entry, name[0]) + (n) +	\
		 sizeof(xfs_dir2_data_off_t) + sizeof(uint8_t)),	\
		XFS_DIR2_DATA_ALIGN)

int
xfs_dir2_data_entsize(
	struct xfs_mount	*mp,
	int			n)
{
	if (xfs_sb_version_hasftype(&mp->m_sb))
		return XFS_DIR3_DATA_ENTSIZE(n);
	else
		return XFS_DIR2_DATA_ENTSIZE(n);
}

static uint8_t
xfs_dir2_data_get_ftype(
	struct xfs_dir2_data_entry *dep)
{
	return XFS_DIR3_FT_UNKNOWN;
}

static void
xfs_dir2_data_put_ftype(
	struct xfs_dir2_data_entry *dep,
	uint8_t			ftype)
{
	ASSERT(ftype < XFS_DIR3_FT_MAX);
}

static uint8_t
xfs_dir3_data_get_ftype(
	struct xfs_dir2_data_entry *dep)
{
	uint8_t		ftype = dep->name[dep->namelen];

	if (ftype >= XFS_DIR3_FT_MAX)
		return XFS_DIR3_FT_UNKNOWN;
	return ftype;
}

static void
xfs_dir3_data_put_ftype(
	struct xfs_dir2_data_entry *dep,
	uint8_t			type)
{
	ASSERT(type < XFS_DIR3_FT_MAX);
	ASSERT(dep->namelen != 0);

	dep->name[dep->namelen] = type;
}

/*
 * Pointer to an entry's tag word.
 */
static __be16 *
xfs_dir2_data_entry_tag_p(
	struct xfs_dir2_data_entry *dep)
{
	return (__be16 *)((char *)dep +
		XFS_DIR2_DATA_ENTSIZE(dep->namelen) - sizeof(__be16));
}

static __be16 *
xfs_dir3_data_entry_tag_p(
	struct xfs_dir2_data_entry *dep)
{
	return (__be16 *)((char *)dep +
		XFS_DIR3_DATA_ENTSIZE(dep->namelen) - sizeof(__be16));
}

static struct xfs_dir2_data_free *
xfs_dir2_data_bestfree_p(struct xfs_dir2_data_hdr *hdr)
{
	return hdr->bestfree;
}

static struct xfs_dir2_data_free *
xfs_dir3_data_bestfree_p(struct xfs_dir2_data_hdr *hdr)
{
	return ((struct xfs_dir3_data_hdr *)hdr)->best_free;
}

static const struct xfs_dir_ops xfs_dir2_ops = {
	.data_get_ftype = xfs_dir2_data_get_ftype,
	.data_put_ftype = xfs_dir2_data_put_ftype,
	.data_entry_tag_p = xfs_dir2_data_entry_tag_p,
	.data_bestfree_p = xfs_dir2_data_bestfree_p,

	.data_first_offset =  sizeof(struct xfs_dir2_data_hdr) +
				XFS_DIR2_DATA_ENTSIZE(1) +
				XFS_DIR2_DATA_ENTSIZE(2),
	.data_entry_offset = sizeof(struct xfs_dir2_data_hdr),
};

static const struct xfs_dir_ops xfs_dir2_ftype_ops = {
	.data_get_ftype = xfs_dir3_data_get_ftype,
	.data_put_ftype = xfs_dir3_data_put_ftype,
	.data_entry_tag_p = xfs_dir3_data_entry_tag_p,
	.data_bestfree_p = xfs_dir2_data_bestfree_p,

	.data_first_offset =  sizeof(struct xfs_dir2_data_hdr) +
				XFS_DIR3_DATA_ENTSIZE(1) +
				XFS_DIR3_DATA_ENTSIZE(2),
	.data_entry_offset = sizeof(struct xfs_dir2_data_hdr),
};

static const struct xfs_dir_ops xfs_dir3_ops = {
	.data_get_ftype = xfs_dir3_data_get_ftype,
	.data_put_ftype = xfs_dir3_data_put_ftype,
	.data_entry_tag_p = xfs_dir3_data_entry_tag_p,
	.data_bestfree_p = xfs_dir3_data_bestfree_p,

	.data_first_offset =  sizeof(struct xfs_dir3_data_hdr) +
				XFS_DIR3_DATA_ENTSIZE(1) +
				XFS_DIR3_DATA_ENTSIZE(2),
	.data_entry_offset = sizeof(struct xfs_dir3_data_hdr),
};

/*
 * Return the ops structure according to the current config.  If we are passed
 * an inode, then that overrides the default config we use which is based on
 * feature bits.
 */
const struct xfs_dir_ops *
xfs_dir_get_ops(
	struct xfs_mount	*mp,
	struct xfs_inode	*dp)
{
	if (dp)
		return dp->d_ops;
	if (mp->m_dir_inode_ops)
		return mp->m_dir_inode_ops;
	if (xfs_sb_version_hascrc(&mp->m_sb))
		return &xfs_dir3_ops;
	if (xfs_sb_version_hasftype(&mp->m_sb))
		return &xfs_dir2_ftype_ops;
	return &xfs_dir2_ops;
}
