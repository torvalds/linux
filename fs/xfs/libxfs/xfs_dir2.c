// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2000-2001,2005 Silicon Graphics, Inc.
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
#include "xfs_trans.h"
#include "xfs_bmap.h"
#include "xfs_dir2.h"
#include "xfs_dir2_priv.h"
#include "xfs_errortag.h"
#include "xfs_error.h"
#include "xfs_trace.h"
#include "xfs_health.h"
#include "xfs_bmap_btree.h"
#include "xfs_trans_space.h"
#include "xfs_parent.h"
#include "xfs_ag.h"

const struct xfs_name xfs_name_dotdot = {
	.name	= (const unsigned char *)"..",
	.len	= 2,
	.type	= XFS_DIR3_FT_DIR,
};

const struct xfs_name xfs_name_dot = {
	.name	= (const unsigned char *)".",
	.len	= 1,
	.type	= XFS_DIR3_FT_DIR,
};

/*
 * Convert inode mode to directory entry filetype
 */
unsigned char
xfs_mode_to_ftype(
	int		mode)
{
	switch (mode & S_IFMT) {
	case S_IFREG:
		return XFS_DIR3_FT_REG_FILE;
	case S_IFDIR:
		return XFS_DIR3_FT_DIR;
	case S_IFCHR:
		return XFS_DIR3_FT_CHRDEV;
	case S_IFBLK:
		return XFS_DIR3_FT_BLKDEV;
	case S_IFIFO:
		return XFS_DIR3_FT_FIFO;
	case S_IFSOCK:
		return XFS_DIR3_FT_SOCK;
	case S_IFLNK:
		return XFS_DIR3_FT_SYMLINK;
	default:
		return XFS_DIR3_FT_UNKNOWN;
	}
}

/*
 * ASCII case-insensitive (ie. A-Z) support for directories that was
 * used in IRIX.
 */
xfs_dahash_t
xfs_ascii_ci_hashname(
	const struct xfs_name	*name)
{
	xfs_dahash_t		hash;
	int			i;

	for (i = 0, hash = 0; i < name->len; i++)
		hash = xfs_ascii_ci_xfrm(name->name[i]) ^ rol32(hash, 7);

	return hash;
}

enum xfs_dacmp
xfs_ascii_ci_compname(
	struct xfs_da_args	*args,
	const unsigned char	*name,
	int			len)
{
	enum xfs_dacmp		result;
	int			i;

	if (args->namelen != len)
		return XFS_CMP_DIFFERENT;

	result = XFS_CMP_EXACT;
	for (i = 0; i < len; i++) {
		if (args->name[i] == name[i])
			continue;
		if (xfs_ascii_ci_xfrm(args->name[i]) !=
		    xfs_ascii_ci_xfrm(name[i]))
			return XFS_CMP_DIFFERENT;
		result = XFS_CMP_CASE;
	}

	return result;
}

int
xfs_da_mount(
	struct xfs_mount	*mp)
{
	struct xfs_da_geometry	*dageo;


	ASSERT(mp->m_sb.sb_versionnum & XFS_SB_VERSION_DIRV2BIT);
	ASSERT(xfs_dir2_dirblock_bytes(&mp->m_sb) <= XFS_MAX_BLOCKSIZE);

	mp->m_dir_geo = kzalloc(sizeof(struct xfs_da_geometry),
				GFP_KERNEL | __GFP_RETRY_MAYFAIL);
	mp->m_attr_geo = kzalloc(sizeof(struct xfs_da_geometry),
				GFP_KERNEL | __GFP_RETRY_MAYFAIL);
	if (!mp->m_dir_geo || !mp->m_attr_geo) {
		kfree(mp->m_dir_geo);
		kfree(mp->m_attr_geo);
		return -ENOMEM;
	}

	/* set up directory geometry */
	dageo = mp->m_dir_geo;
	dageo->blklog = mp->m_sb.sb_blocklog + mp->m_sb.sb_dirblklog;
	dageo->fsblog = mp->m_sb.sb_blocklog;
	dageo->blksize = xfs_dir2_dirblock_bytes(&mp->m_sb);
	dageo->fsbcount = 1 << mp->m_sb.sb_dirblklog;
	if (xfs_has_crc(mp)) {
		dageo->node_hdr_size = sizeof(struct xfs_da3_node_hdr);
		dageo->leaf_hdr_size = sizeof(struct xfs_dir3_leaf_hdr);
		dageo->free_hdr_size = sizeof(struct xfs_dir3_free_hdr);
		dageo->data_entry_offset =
				sizeof(struct xfs_dir3_data_hdr);
	} else {
		dageo->node_hdr_size = sizeof(struct xfs_da_node_hdr);
		dageo->leaf_hdr_size = sizeof(struct xfs_dir2_leaf_hdr);
		dageo->free_hdr_size = sizeof(struct xfs_dir2_free_hdr);
		dageo->data_entry_offset =
				sizeof(struct xfs_dir2_data_hdr);
	}
	dageo->leaf_max_ents = (dageo->blksize - dageo->leaf_hdr_size) /
			sizeof(struct xfs_dir2_leaf_entry);
	dageo->free_max_bests = (dageo->blksize - dageo->free_hdr_size) /
			sizeof(xfs_dir2_data_off_t);

	dageo->data_first_offset = dageo->data_entry_offset +
			xfs_dir2_data_entsize(mp, 1) +
			xfs_dir2_data_entsize(mp, 2);

	/*
	 * Now we've set up the block conversion variables, we can calculate the
	 * segment block constants using the geometry structure.
	 */
	dageo->datablk = xfs_dir2_byte_to_da(dageo, XFS_DIR2_DATA_OFFSET);
	dageo->leafblk = xfs_dir2_byte_to_da(dageo, XFS_DIR2_LEAF_OFFSET);
	dageo->freeblk = xfs_dir2_byte_to_da(dageo, XFS_DIR2_FREE_OFFSET);
	dageo->node_ents = (dageo->blksize - dageo->node_hdr_size) /
				(uint)sizeof(xfs_da_node_entry_t);
	dageo->max_extents = (XFS_DIR2_MAX_SPACES * XFS_DIR2_SPACE_SIZE) >>
					mp->m_sb.sb_blocklog;
	dageo->magicpct = (dageo->blksize * 37) / 100;

	/* set up attribute geometry - single fsb only */
	dageo = mp->m_attr_geo;
	dageo->blklog = mp->m_sb.sb_blocklog;
	dageo->fsblog = mp->m_sb.sb_blocklog;
	dageo->blksize = 1 << dageo->blklog;
	dageo->fsbcount = 1;
	dageo->node_hdr_size = mp->m_dir_geo->node_hdr_size;
	dageo->node_ents = (dageo->blksize - dageo->node_hdr_size) /
				(uint)sizeof(xfs_da_node_entry_t);

	if (xfs_has_large_extent_counts(mp))
		dageo->max_extents = XFS_MAX_EXTCNT_ATTR_FORK_LARGE;
	else
		dageo->max_extents = XFS_MAX_EXTCNT_ATTR_FORK_SMALL;

	dageo->magicpct = (dageo->blksize * 37) / 100;
	return 0;
}

void
xfs_da_unmount(
	struct xfs_mount	*mp)
{
	kfree(mp->m_dir_geo);
	kfree(mp->m_attr_geo);
}

/*
 * Return 1 if directory contains only "." and "..".
 */
int
xfs_dir_isempty(
	xfs_inode_t	*dp)
{
	xfs_dir2_sf_hdr_t	*sfp;

	ASSERT(S_ISDIR(VFS_I(dp)->i_mode));
	if (dp->i_disk_size == 0)	/* might happen during shutdown. */
		return 1;
	if (dp->i_disk_size > xfs_inode_data_fork_size(dp))
		return 0;
	sfp = dp->i_df.if_data;
	return !sfp->count;
}

/*
 * Validate a given inode number.
 */
int
xfs_dir_ino_validate(
	xfs_mount_t	*mp,
	xfs_ino_t	ino)
{
	bool		ino_ok = xfs_verify_dir_ino(mp, ino);

	if (XFS_IS_CORRUPT(mp, !ino_ok) ||
	    XFS_TEST_ERROR(false, mp, XFS_ERRTAG_DIR_INO_VALIDATE)) {
		xfs_warn(mp, "Invalid inode number 0x%Lx",
				(unsigned long long) ino);
		return -EFSCORRUPTED;
	}
	return 0;
}

/*
 * Initialize a directory with its "." and ".." entries.
 */
int
xfs_dir_init(
	xfs_trans_t	*tp,
	xfs_inode_t	*dp,
	xfs_inode_t	*pdp)
{
	struct xfs_da_args *args;
	int		error;

	ASSERT(S_ISDIR(VFS_I(dp)->i_mode));
	error = xfs_dir_ino_validate(tp->t_mountp, pdp->i_ino);
	if (error)
		return error;

	args = kzalloc(sizeof(*args), GFP_KERNEL | __GFP_NOFAIL);
	if (!args)
		return -ENOMEM;

	args->geo = dp->i_mount->m_dir_geo;
	args->dp = dp;
	args->trans = tp;
	args->owner = dp->i_ino;
	error = xfs_dir2_sf_create(args, pdp->i_ino);
	kfree(args);
	return error;
}

enum xfs_dir2_fmt
xfs_dir2_format(
	struct xfs_da_args	*args,
	int			*error)
{
	struct xfs_inode	*dp = args->dp;
	struct xfs_mount	*mp = dp->i_mount;
	struct xfs_da_geometry	*geo = mp->m_dir_geo;
	xfs_fileoff_t		eof;

	xfs_assert_ilocked(dp, XFS_ILOCK_SHARED | XFS_ILOCK_EXCL);

	*error = 0;
	if (dp->i_df.if_format == XFS_DINODE_FMT_LOCAL)
		return XFS_DIR2_FMT_SF;

	*error = xfs_bmap_last_offset(dp, &eof, XFS_DATA_FORK);
	if (*error)
		return XFS_DIR2_FMT_ERROR;

	if (eof == XFS_B_TO_FSB(mp, geo->blksize)) {
		if (XFS_IS_CORRUPT(mp, dp->i_disk_size != geo->blksize)) {
			xfs_da_mark_sick(args);
			*error = -EFSCORRUPTED;
			return XFS_DIR2_FMT_ERROR;
		}
		return XFS_DIR2_FMT_BLOCK;
	}
	if (eof == geo->leafblk + geo->fsbcount)
		return XFS_DIR2_FMT_LEAF;
	return XFS_DIR2_FMT_NODE;
}

int
xfs_dir_createname_args(
	struct xfs_da_args	*args)
{
	int			error;

	if (!args->inumber)
		args->op_flags |= XFS_DA_OP_JUSTCHECK;

	switch (xfs_dir2_format(args, &error)) {
	case XFS_DIR2_FMT_SF:
		return xfs_dir2_sf_addname(args);
	case XFS_DIR2_FMT_BLOCK:
		return xfs_dir2_block_addname(args);
	case XFS_DIR2_FMT_LEAF:
		return xfs_dir2_leaf_addname(args);
	case XFS_DIR2_FMT_NODE:
		return xfs_dir2_node_addname(args);
	default:
		return error;
	}
}

/*
 * Enter a name in a directory, or check for available space.
 * If inum is 0, only the available space test is performed.
 */
int
xfs_dir_createname(
	struct xfs_trans	*tp,
	struct xfs_inode	*dp,
	const struct xfs_name	*name,
	xfs_ino_t		inum,		/* new entry inode number */
	xfs_extlen_t		total)		/* bmap's total block count */
{
	struct xfs_da_args	*args;
	int			rval;

	ASSERT(S_ISDIR(VFS_I(dp)->i_mode));

	if (inum) {
		rval = xfs_dir_ino_validate(tp->t_mountp, inum);
		if (rval)
			return rval;
		XFS_STATS_INC(dp->i_mount, xs_dir_create);
	}

	args = kzalloc(sizeof(*args), GFP_KERNEL | __GFP_NOFAIL);
	if (!args)
		return -ENOMEM;

	args->geo = dp->i_mount->m_dir_geo;
	args->name = name->name;
	args->namelen = name->len;
	args->filetype = name->type;
	args->hashval = xfs_dir2_hashname(dp->i_mount, name);
	args->inumber = inum;
	args->dp = dp;
	args->total = total;
	args->whichfork = XFS_DATA_FORK;
	args->trans = tp;
	args->op_flags = XFS_DA_OP_ADDNAME | XFS_DA_OP_OKNOENT;
	args->owner = dp->i_ino;

	rval = xfs_dir_createname_args(args);
	kfree(args);
	return rval;
}

/*
 * If doing a CI lookup and case-insensitive match, dup actual name into
 * args.value. Return EEXIST for success (ie. name found) or an error.
 */
int
xfs_dir_cilookup_result(
	struct xfs_da_args *args,
	const unsigned char *name,
	int		len)
{
	if (args->cmpresult == XFS_CMP_DIFFERENT)
		return -ENOENT;
	if (args->cmpresult != XFS_CMP_CASE ||
					!(args->op_flags & XFS_DA_OP_CILOOKUP))
		return -EEXIST;

	args->value = kmalloc(len,
			GFP_KERNEL | __GFP_NOLOCKDEP | __GFP_RETRY_MAYFAIL);
	if (!args->value)
		return -ENOMEM;

	memcpy(args->value, name, len);
	args->valuelen = len;
	return -EEXIST;
}

int
xfs_dir_lookup_args(
	struct xfs_da_args	*args)
{
	int			error;

	switch (xfs_dir2_format(args, &error)) {
	case XFS_DIR2_FMT_SF:
		error = xfs_dir2_sf_lookup(args);
		break;
	case XFS_DIR2_FMT_BLOCK:
		error = xfs_dir2_block_lookup(args);
		break;
	case XFS_DIR2_FMT_LEAF:
		error = xfs_dir2_leaf_lookup(args);
		break;
	case XFS_DIR2_FMT_NODE:
		error = xfs_dir2_node_lookup(args);
		break;
	default:
		break;
	}

	if (error != -EEXIST)
		return error;
	return 0;
}

/*
 * Lookup a name in a directory, give back the inode number.
 * If ci_name is not NULL, returns the actual name in ci_name if it differs
 * to name, or ci_name->name is set to NULL for an exact match.
 */

int
xfs_dir_lookup(
	struct xfs_trans	*tp,
	struct xfs_inode	*dp,
	const struct xfs_name	*name,
	xfs_ino_t		*inum,	  /* out: inode number */
	struct xfs_name		*ci_name) /* out: actual name if CI match */
{
	struct xfs_da_args	*args;
	int			rval;
	int			lock_mode;

	ASSERT(S_ISDIR(VFS_I(dp)->i_mode));
	XFS_STATS_INC(dp->i_mount, xs_dir_lookup);

	args = kzalloc(sizeof(*args),
			GFP_KERNEL | __GFP_NOLOCKDEP | __GFP_NOFAIL);
	args->geo = dp->i_mount->m_dir_geo;
	args->name = name->name;
	args->namelen = name->len;
	args->filetype = name->type;
	args->hashval = xfs_dir2_hashname(dp->i_mount, name);
	args->dp = dp;
	args->whichfork = XFS_DATA_FORK;
	args->trans = tp;
	args->op_flags = XFS_DA_OP_OKNOENT;
	args->owner = dp->i_ino;
	if (ci_name)
		args->op_flags |= XFS_DA_OP_CILOOKUP;

	lock_mode = xfs_ilock_data_map_shared(dp);
	rval = xfs_dir_lookup_args(args);
	if (!rval) {
		*inum = args->inumber;
		if (ci_name) {
			ci_name->name = args->value;
			ci_name->len = args->valuelen;
		}
	}
	xfs_iunlock(dp, lock_mode);
	kfree(args);
	return rval;
}

int
xfs_dir_removename_args(
	struct xfs_da_args	*args)
{
	int			error;

	switch (xfs_dir2_format(args, &error)) {
	case XFS_DIR2_FMT_SF:
		return xfs_dir2_sf_removename(args);
	case XFS_DIR2_FMT_BLOCK:
		return xfs_dir2_block_removename(args);
	case XFS_DIR2_FMT_LEAF:
		return xfs_dir2_leaf_removename(args);
	case XFS_DIR2_FMT_NODE:
		return xfs_dir2_node_removename(args);
	default:
		return error;
	}
}

/*
 * Remove an entry from a directory.
 */
int
xfs_dir_removename(
	struct xfs_trans	*tp,
	struct xfs_inode	*dp,
	const struct xfs_name	*name,
	xfs_ino_t		ino,
	xfs_extlen_t		total)		/* bmap's total block count */
{
	struct xfs_da_args	*args;
	int			rval;

	ASSERT(S_ISDIR(VFS_I(dp)->i_mode));
	XFS_STATS_INC(dp->i_mount, xs_dir_remove);

	args = kzalloc(sizeof(*args), GFP_KERNEL | __GFP_NOFAIL);
	if (!args)
		return -ENOMEM;

	args->geo = dp->i_mount->m_dir_geo;
	args->name = name->name;
	args->namelen = name->len;
	args->filetype = name->type;
	args->hashval = xfs_dir2_hashname(dp->i_mount, name);
	args->inumber = ino;
	args->dp = dp;
	args->total = total;
	args->whichfork = XFS_DATA_FORK;
	args->trans = tp;
	args->owner = dp->i_ino;
	rval = xfs_dir_removename_args(args);
	kfree(args);
	return rval;
}

int
xfs_dir_replace_args(
	struct xfs_da_args	*args)
{
	int			error;

	switch (xfs_dir2_format(args, &error)) {
	case XFS_DIR2_FMT_SF:
		return xfs_dir2_sf_replace(args);
	case XFS_DIR2_FMT_BLOCK:
		return xfs_dir2_block_replace(args);
	case XFS_DIR2_FMT_LEAF:
		return xfs_dir2_leaf_replace(args);
	case XFS_DIR2_FMT_NODE:
		return xfs_dir2_node_replace(args);
	default:
		return error;
	}
}

/*
 * Replace the inode number of a directory entry.
 */
int
xfs_dir_replace(
	struct xfs_trans	*tp,
	struct xfs_inode	*dp,
	const struct xfs_name	*name,		/* name of entry to replace */
	xfs_ino_t		inum,		/* new inode number */
	xfs_extlen_t		total)		/* bmap's total block count */
{
	struct xfs_da_args	*args;
	int			rval;

	ASSERT(S_ISDIR(VFS_I(dp)->i_mode));

	rval = xfs_dir_ino_validate(tp->t_mountp, inum);
	if (rval)
		return rval;

	args = kzalloc(sizeof(*args), GFP_KERNEL | __GFP_NOFAIL);
	if (!args)
		return -ENOMEM;

	args->geo = dp->i_mount->m_dir_geo;
	args->name = name->name;
	args->namelen = name->len;
	args->filetype = name->type;
	args->hashval = xfs_dir2_hashname(dp->i_mount, name);
	args->inumber = inum;
	args->dp = dp;
	args->total = total;
	args->whichfork = XFS_DATA_FORK;
	args->trans = tp;
	args->owner = dp->i_ino;
	rval = xfs_dir_replace_args(args);
	kfree(args);
	return rval;
}

/*
 * See if this entry can be added to the directory without allocating space.
 */
int
xfs_dir_canenter(
	struct xfs_trans	*tp,
	struct xfs_inode	*dp,
	const struct xfs_name	*name)		/* name of entry to add */
{
	return xfs_dir_createname(tp, dp, name, 0, 0);
}

/*
 * Utility routines.
 */

/*
 * Add a block to the directory.
 *
 * This routine is for data and free blocks, not leaf/node blocks which are
 * handled by xfs_da_grow_inode.
 */
int
xfs_dir2_grow_inode(
	struct xfs_da_args	*args,
	int			space,	/* v2 dir's space XFS_DIR2_xxx_SPACE */
	xfs_dir2_db_t		*dbp)	/* out: block number added */
{
	struct xfs_inode	*dp = args->dp;
	struct xfs_mount	*mp = dp->i_mount;
	xfs_fileoff_t		bno;	/* directory offset of new block */
	int			count;	/* count of filesystem blocks */
	int			error;

	trace_xfs_dir2_grow_inode(args, space);

	/*
	 * Set lowest possible block in the space requested.
	 */
	bno = XFS_B_TO_FSBT(mp, space * XFS_DIR2_SPACE_SIZE);
	count = args->geo->fsbcount;

	error = xfs_da_grow_inode_int(args, &bno, count);
	if (error)
		return error;

	*dbp = xfs_dir2_da_to_db(args->geo, (xfs_dablk_t)bno);

	/*
	 * Update file's size if this is the data space and it grew.
	 */
	if (space == XFS_DIR2_DATA_SPACE) {
		xfs_fsize_t	size;		/* directory file (data) size */

		size = XFS_FSB_TO_B(mp, bno + count);
		if (size > dp->i_disk_size) {
			dp->i_disk_size = size;
			xfs_trans_log_inode(args->trans, dp, XFS_ILOG_CORE);
		}
	}
	return 0;
}

/*
 * Remove the given block from the directory.
 * This routine is used for data and free blocks, leaf/node are done
 * by xfs_da_shrink_inode.
 */
int
xfs_dir2_shrink_inode(
	struct xfs_da_args	*args,
	xfs_dir2_db_t		db,
	struct xfs_buf		*bp)
{
	xfs_fileoff_t		bno;		/* directory file offset */
	xfs_dablk_t		da;		/* directory file offset */
	int			done;		/* bunmap is finished */
	struct xfs_inode	*dp;
	int			error;
	struct xfs_mount	*mp;
	struct xfs_trans	*tp;

	trace_xfs_dir2_shrink_inode(args, db);

	dp = args->dp;
	mp = dp->i_mount;
	tp = args->trans;
	da = xfs_dir2_db_to_da(args->geo, db);

	/* Unmap the fsblock(s). */
	error = xfs_bunmapi(tp, dp, da, args->geo->fsbcount, 0, 0, &done);
	if (error) {
		/*
		 * ENOSPC actually can happen if we're in a removename with no
		 * space reservation, and the resulting block removal would
		 * cause a bmap btree split or conversion from extents to btree.
		 * This can only happen for un-fragmented directory blocks,
		 * since you need to be punching out the middle of an extent.
		 * In this case we need to leave the block in the file, and not
		 * binval it.  So the block has to be in a consistent empty
		 * state and appropriately logged.  We don't free up the buffer,
		 * the caller can tell it hasn't happened since it got an error
		 * back.
		 */
		return error;
	}
	ASSERT(done);
	/*
	 * Invalidate the buffer from the transaction.
	 */
	xfs_trans_binval(tp, bp);
	/*
	 * If it's not a data block, we're done.
	 */
	if (db >= xfs_dir2_byte_to_db(args->geo, XFS_DIR2_LEAF_OFFSET))
		return 0;
	/*
	 * If the block isn't the last one in the directory, we're done.
	 */
	if (dp->i_disk_size > xfs_dir2_db_off_to_byte(args->geo, db + 1, 0))
		return 0;
	bno = da;
	if ((error = xfs_bmap_last_before(tp, dp, &bno, XFS_DATA_FORK))) {
		/*
		 * This can't really happen unless there's kernel corruption.
		 */
		return error;
	}
	if (db == args->geo->datablk)
		ASSERT(bno == 0);
	else
		ASSERT(bno > 0);
	/*
	 * Set the size to the new last block.
	 */
	dp->i_disk_size = XFS_FSB_TO_B(mp, bno);
	xfs_trans_log_inode(tp, dp, XFS_ILOG_CORE);
	return 0;
}

/* Returns true if the directory entry name is valid. */
bool
xfs_dir2_namecheck(
	const void	*name,
	size_t		length)
{
	/*
	 * MAXNAMELEN includes the trailing null, but (name/length) leave it
	 * out, so use >= for the length check.
	 */
	if (length >= MAXNAMELEN)
		return false;

	/* There shouldn't be any slashes or nulls here */
	return !memchr(name, '/', length) && !memchr(name, 0, length);
}

xfs_dahash_t
xfs_dir2_hashname(
	struct xfs_mount	*mp,
	const struct xfs_name	*name)
{
	if (unlikely(xfs_has_asciici(mp)))
		return xfs_ascii_ci_hashname(name);
	return xfs_da_hashname(name->name, name->len);
}

enum xfs_dacmp
xfs_dir2_compname(
	struct xfs_da_args	*args,
	const unsigned char	*name,
	int			len)
{
	if (unlikely(xfs_has_asciici(args->dp->i_mount)))
		return xfs_ascii_ci_compname(args, name, len);
	return xfs_da_compname(args, name, len);
}

/*
 * Given a directory @dp, a newly allocated inode @ip, and a @name, link @ip
 * into @dp under the given @name.  If @ip is a directory, it will be
 * initialized.  Both inodes must have the ILOCK held and the transaction must
 * have sufficient blocks reserved.
 */
int
xfs_dir_create_child(
	struct xfs_trans	*tp,
	unsigned int		resblks,
	struct xfs_dir_update	*du)
{
	struct xfs_inode	*dp = du->dp;
	const struct xfs_name	*name = du->name;
	struct xfs_inode	*ip = du->ip;
	int			error;

	xfs_assert_ilocked(ip, XFS_ILOCK_EXCL);
	xfs_assert_ilocked(dp, XFS_ILOCK_EXCL);

	error = xfs_dir_createname(tp, dp, name, ip->i_ino, resblks);
	if (error) {
		ASSERT(error != -ENOSPC);
		return error;
	}

	xfs_trans_ichgtime(tp, dp, XFS_ICHGTIME_MOD | XFS_ICHGTIME_CHG);
	xfs_trans_log_inode(tp, dp, XFS_ILOG_CORE);

	if (S_ISDIR(VFS_I(ip)->i_mode)) {
		error = xfs_dir_init(tp, ip, dp);
		if (error)
			return error;

		xfs_bumplink(tp, dp);
	}

	/*
	 * If we have parent pointers, we need to add the attribute containing
	 * the parent information now.
	 */
	if (du->ppargs) {
		error = xfs_parent_addname(tp, du->ppargs, dp, name, ip);
		if (error)
			return error;
	}

	return 0;
}

/*
 * Given a directory @dp, an existing non-directory inode @ip, and a @name,
 * link @ip into @dp under the given @name.  Both inodes must have the ILOCK
 * held.
 */
int
xfs_dir_add_child(
	struct xfs_trans	*tp,
	unsigned int		resblks,
	struct xfs_dir_update	*du)
{
	struct xfs_inode	*dp = du->dp;
	const struct xfs_name	*name = du->name;
	struct xfs_inode	*ip = du->ip;
	struct xfs_mount	*mp = tp->t_mountp;
	int			error;

	xfs_assert_ilocked(ip, XFS_ILOCK_EXCL);
	xfs_assert_ilocked(dp, XFS_ILOCK_EXCL);
	ASSERT(!S_ISDIR(VFS_I(ip)->i_mode));

	if (!resblks) {
		error = xfs_dir_canenter(tp, dp, name);
		if (error)
			return error;
	}

	/*
	 * Handle initial link state of O_TMPFILE inode
	 */
	if (VFS_I(ip)->i_nlink == 0) {
		struct xfs_perag	*pag;

		pag = xfs_perag_get(mp, XFS_INO_TO_AGNO(mp, ip->i_ino));
		error = xfs_iunlink_remove(tp, pag, ip);
		xfs_perag_put(pag);
		if (error)
			return error;
	}

	error = xfs_dir_createname(tp, dp, name, ip->i_ino, resblks);
	if (error)
		return error;

	xfs_trans_ichgtime(tp, dp, XFS_ICHGTIME_MOD | XFS_ICHGTIME_CHG);
	xfs_trans_log_inode(tp, dp, XFS_ILOG_CORE);

	xfs_bumplink(tp, ip);

	/*
	 * If we have parent pointers, we now need to add the parent record to
	 * the attribute fork of the inode. If this is the initial parent
	 * attribute, we need to create it correctly, otherwise we can just add
	 * the parent to the inode.
	 */
	if (du->ppargs) {
		error = xfs_parent_addname(tp, du->ppargs, dp, name, ip);
		if (error)
			return error;
	}

	return 0;
}

/*
 * Given a directory @dp, a child @ip, and a @name, remove the (@name, @ip)
 * entry from the directory.  Both inodes must have the ILOCK held.
 */
int
xfs_dir_remove_child(
	struct xfs_trans	*tp,
	unsigned int		resblks,
	struct xfs_dir_update	*du)
{
	struct xfs_inode	*dp = du->dp;
	const struct xfs_name	*name = du->name;
	struct xfs_inode	*ip = du->ip;
	int			error;

	xfs_assert_ilocked(ip, XFS_ILOCK_EXCL);
	xfs_assert_ilocked(dp, XFS_ILOCK_EXCL);

	/*
	 * If we're removing a directory perform some additional validation.
	 */
	if (S_ISDIR(VFS_I(ip)->i_mode)) {
		ASSERT(VFS_I(ip)->i_nlink >= 2);
		if (VFS_I(ip)->i_nlink != 2)
			return -ENOTEMPTY;
		if (!xfs_dir_isempty(ip))
			return -ENOTEMPTY;

		/* Drop the link from ip's "..".  */
		error = xfs_droplink(tp, dp);
		if (error)
			return error;

		/* Drop the "." link from ip to self.  */
		error = xfs_droplink(tp, ip);
		if (error)
			return error;

		/*
		 * Point the unlinked child directory's ".." entry to the root
		 * directory to eliminate back-references to inodes that may
		 * get freed before the child directory is closed.  If the fs
		 * gets shrunk, this can lead to dirent inode validation errors.
		 */
		if (dp->i_ino != tp->t_mountp->m_sb.sb_rootino) {
			error = xfs_dir_replace(tp, ip, &xfs_name_dotdot,
					tp->t_mountp->m_sb.sb_rootino, 0);
			if (error)
				return error;
		}
	} else {
		/*
		 * When removing a non-directory we need to log the parent
		 * inode here.  For a directory this is done implicitly
		 * by the xfs_droplink call for the ".." entry.
		 */
		xfs_trans_log_inode(tp, dp, XFS_ILOG_CORE);
	}
	xfs_trans_ichgtime(tp, dp, XFS_ICHGTIME_MOD | XFS_ICHGTIME_CHG);

	/* Drop the link from dp to ip. */
	error = xfs_droplink(tp, ip);
	if (error)
		return error;

	error = xfs_dir_removename(tp, dp, name, ip->i_ino, resblks);
	if (error) {
		ASSERT(error != -ENOENT);
		return error;
	}

	/* Remove parent pointer. */
	if (du->ppargs) {
		error = xfs_parent_removename(tp, du->ppargs, dp, name, ip);
		if (error)
			return error;
	}

	return 0;
}
