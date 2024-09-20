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
#include "xfs_ialloc.h"

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

#ifdef CONFIG_XFS_LIVE_HOOKS
/*
 * Use a static key here to reduce the overhead of directory live update hooks.
 * If the compiler supports jump labels, the static branch will be replaced by
 * a nop sled when there are no hook users.  Online fsck is currently the only
 * caller, so this is a reasonable tradeoff.
 *
 * Note: Patching the kernel code requires taking the cpu hotplug lock.  Other
 * parts of the kernel allocate memory with that lock held, which means that
 * XFS callers cannot hold any locks that might be used by memory reclaim or
 * writeback when calling the static_branch_{inc,dec} functions.
 */
DEFINE_STATIC_XFS_HOOK_SWITCH(xfs_dir_hooks_switch);

void
xfs_dir_hook_disable(void)
{
	xfs_hooks_switch_off(&xfs_dir_hooks_switch);
}

void
xfs_dir_hook_enable(void)
{
	xfs_hooks_switch_on(&xfs_dir_hooks_switch);
}

/* Call hooks for a directory update relating to a child dirent update. */
inline void
xfs_dir_update_hook(
	struct xfs_inode		*dp,
	struct xfs_inode		*ip,
	int				delta,
	const struct xfs_name		*name)
{
	if (xfs_hooks_switched_on(&xfs_dir_hooks_switch)) {
		struct xfs_dir_update_params	p = {
			.dp		= dp,
			.ip		= ip,
			.delta		= delta,
			.name		= name,
		};
		struct xfs_mount	*mp = ip->i_mount;

		xfs_hooks_call(&mp->m_dir_update_hooks, 0, &p);
	}
}

/* Call the specified function during a directory update. */
int
xfs_dir_hook_add(
	struct xfs_mount	*mp,
	struct xfs_dir_hook	*hook)
{
	return xfs_hooks_add(&mp->m_dir_update_hooks, &hook->dirent_hook);
}

/* Stop calling the specified function during a directory update. */
void
xfs_dir_hook_del(
	struct xfs_mount	*mp,
	struct xfs_dir_hook	*hook)
{
	xfs_hooks_del(&mp->m_dir_update_hooks, &hook->dirent_hook);
}

/* Configure directory update hook functions. */
void
xfs_dir_hook_setup(
	struct xfs_dir_hook	*hook,
	notifier_fn_t		mod_fn)
{
	xfs_hook_setup(&hook->dirent_hook, mod_fn);
}
#endif /* CONFIG_XFS_LIVE_HOOKS */

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

	xfs_dir_update_hook(dp, ip, 1, name);
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

	xfs_dir_update_hook(dp, ip, 1, name);
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

	xfs_dir_update_hook(dp, ip, -1, name);
	return 0;
}

/*
 * Exchange the entry (@name1, @ip1) in directory @dp1 with the entry (@name2,
 * @ip2) in directory @dp2, and update '..' @ip1 and @ip2's entries as needed.
 * @ip1 and @ip2 need not be of the same type.
 *
 * All inodes must have the ILOCK held, and both entries must already exist.
 */
int
xfs_dir_exchange_children(
	struct xfs_trans	*tp,
	struct xfs_dir_update	*du1,
	struct xfs_dir_update	*du2,
	unsigned int		spaceres)
{
	struct xfs_inode	*dp1 = du1->dp;
	const struct xfs_name	*name1 = du1->name;
	struct xfs_inode	*ip1 = du1->ip;
	struct xfs_inode	*dp2 = du2->dp;
	const struct xfs_name	*name2 = du2->name;
	struct xfs_inode	*ip2 = du2->ip;
	int			ip1_flags = 0;
	int			ip2_flags = 0;
	int			dp2_flags = 0;
	int			error;

	/* Swap inode number for dirent in first parent */
	error = xfs_dir_replace(tp, dp1, name1, ip2->i_ino, spaceres);
	if (error)
		return error;

	/* Swap inode number for dirent in second parent */
	error = xfs_dir_replace(tp, dp2, name2, ip1->i_ino, spaceres);
	if (error)
		return error;

	/*
	 * If we're renaming one or more directories across different parents,
	 * update the respective ".." entries (and link counts) to match the new
	 * parents.
	 */
	if (dp1 != dp2) {
		dp2_flags = XFS_ICHGTIME_MOD | XFS_ICHGTIME_CHG;

		if (S_ISDIR(VFS_I(ip2)->i_mode)) {
			error = xfs_dir_replace(tp, ip2, &xfs_name_dotdot,
						dp1->i_ino, spaceres);
			if (error)
				return error;

			/* transfer ip2 ".." reference to dp1 */
			if (!S_ISDIR(VFS_I(ip1)->i_mode)) {
				error = xfs_droplink(tp, dp2);
				if (error)
					return error;
				xfs_bumplink(tp, dp1);
			}

			/*
			 * Although ip1 isn't changed here, userspace needs
			 * to be warned about the change, so that applications
			 * relying on it (like backup ones), will properly
			 * notify the change
			 */
			ip1_flags |= XFS_ICHGTIME_CHG;
			ip2_flags |= XFS_ICHGTIME_MOD | XFS_ICHGTIME_CHG;
		}

		if (S_ISDIR(VFS_I(ip1)->i_mode)) {
			error = xfs_dir_replace(tp, ip1, &xfs_name_dotdot,
						dp2->i_ino, spaceres);
			if (error)
				return error;

			/* transfer ip1 ".." reference to dp2 */
			if (!S_ISDIR(VFS_I(ip2)->i_mode)) {
				error = xfs_droplink(tp, dp1);
				if (error)
					return error;
				xfs_bumplink(tp, dp2);
			}

			/*
			 * Although ip2 isn't changed here, userspace needs
			 * to be warned about the change, so that applications
			 * relying on it (like backup ones), will properly
			 * notify the change
			 */
			ip1_flags |= XFS_ICHGTIME_MOD | XFS_ICHGTIME_CHG;
			ip2_flags |= XFS_ICHGTIME_CHG;
		}
	}

	if (ip1_flags) {
		xfs_trans_ichgtime(tp, ip1, ip1_flags);
		xfs_trans_log_inode(tp, ip1, XFS_ILOG_CORE);
	}
	if (ip2_flags) {
		xfs_trans_ichgtime(tp, ip2, ip2_flags);
		xfs_trans_log_inode(tp, ip2, XFS_ILOG_CORE);
	}
	if (dp2_flags) {
		xfs_trans_ichgtime(tp, dp2, dp2_flags);
		xfs_trans_log_inode(tp, dp2, XFS_ILOG_CORE);
	}
	xfs_trans_ichgtime(tp, dp1, XFS_ICHGTIME_MOD | XFS_ICHGTIME_CHG);
	xfs_trans_log_inode(tp, dp1, XFS_ILOG_CORE);

	/* Schedule parent pointer replacements */
	if (du1->ppargs) {
		error = xfs_parent_replacename(tp, du1->ppargs, dp1, name1,
				dp2, name2, ip1);
		if (error)
			return error;
	}

	if (du2->ppargs) {
		error = xfs_parent_replacename(tp, du2->ppargs, dp2, name2,
				dp1, name1, ip2);
		if (error)
			return error;
	}

	/*
	 * Inform our hook clients that we've finished an exchange operation as
	 * follows: removed the source and target files from their directories;
	 * added the target to the source directory; and added the source to
	 * the target directory.  All inodes are locked, so it's ok to model a
	 * rename this way so long as we say we deleted entries before we add
	 * new ones.
	 */
	xfs_dir_update_hook(dp1, ip1, -1, name1);
	xfs_dir_update_hook(dp2, ip2, -1, name2);
	xfs_dir_update_hook(dp1, ip2, 1, name1);
	xfs_dir_update_hook(dp2, ip1, 1, name2);
	return 0;
}

/*
 * Given an entry (@src_name, @src_ip) in directory @src_dp, make the entry
 * @target_name in directory @target_dp point to @src_ip and remove the
 * original entry, cleaning up everything left behind.
 *
 * Cleanup involves dropping a link count on @target_ip, and either removing
 * the (@src_name, @src_ip) entry from @src_dp or simply replacing the entry
 * with (@src_name, @wip) if a whiteout inode @wip is supplied.
 *
 * All inodes must have the ILOCK held.  We assume that if @src_ip is a
 * directory then its '..' doesn't already point to @target_dp, and that @wip
 * is a freshly allocated whiteout.
 */
int
xfs_dir_rename_children(
	struct xfs_trans	*tp,
	struct xfs_dir_update	*du_src,
	struct xfs_dir_update	*du_tgt,
	unsigned int		spaceres,
	struct xfs_dir_update	*du_wip)
{
	struct xfs_mount	*mp = tp->t_mountp;
	struct xfs_inode	*src_dp = du_src->dp;
	const struct xfs_name	*src_name = du_src->name;
	struct xfs_inode	*src_ip = du_src->ip;
	struct xfs_inode	*target_dp = du_tgt->dp;
	const struct xfs_name	*target_name = du_tgt->name;
	struct xfs_inode	*target_ip = du_tgt->ip;
	bool			new_parent = (src_dp != target_dp);
	bool			src_is_directory;
	int			error;

	src_is_directory = S_ISDIR(VFS_I(src_ip)->i_mode);

	/*
	 * Check for expected errors before we dirty the transaction
	 * so we can return an error without a transaction abort.
	 */
	if (target_ip == NULL) {
		/*
		 * If there's no space reservation, check the entry will
		 * fit before actually inserting it.
		 */
		if (!spaceres) {
			error = xfs_dir_canenter(tp, target_dp, target_name);
			if (error)
				return error;
		}
	} else {
		/*
		 * If target exists and it's a directory, check that whether
		 * it can be destroyed.
		 */
		if (S_ISDIR(VFS_I(target_ip)->i_mode) &&
		    (!xfs_dir_isempty(target_ip) ||
		     (VFS_I(target_ip)->i_nlink > 2)))
			return -EEXIST;
	}

	/*
	 * Directory entry creation below may acquire the AGF. Remove
	 * the whiteout from the unlinked list first to preserve correct
	 * AGI/AGF locking order. This dirties the transaction so failures
	 * after this point will abort and log recovery will clean up the
	 * mess.
	 *
	 * For whiteouts, we need to bump the link count on the whiteout
	 * inode. After this point, we have a real link, clear the tmpfile
	 * state flag from the inode so it doesn't accidentally get misused
	 * in future.
	 */
	if (du_wip->ip) {
		struct xfs_perag	*pag;

		ASSERT(VFS_I(du_wip->ip)->i_nlink == 0);

		pag = xfs_perag_get(mp, XFS_INO_TO_AGNO(mp, du_wip->ip->i_ino));
		error = xfs_iunlink_remove(tp, pag, du_wip->ip);
		xfs_perag_put(pag);
		if (error)
			return error;

		xfs_bumplink(tp, du_wip->ip);
	}

	/*
	 * Set up the target.
	 */
	if (target_ip == NULL) {
		/*
		 * If target does not exist and the rename crosses
		 * directories, adjust the target directory link count
		 * to account for the ".." reference from the new entry.
		 */
		error = xfs_dir_createname(tp, target_dp, target_name,
					   src_ip->i_ino, spaceres);
		if (error)
			return error;

		xfs_trans_ichgtime(tp, target_dp,
					XFS_ICHGTIME_MOD | XFS_ICHGTIME_CHG);

		if (new_parent && src_is_directory) {
			xfs_bumplink(tp, target_dp);
		}
	} else { /* target_ip != NULL */
		/*
		 * Link the source inode under the target name.
		 * If the source inode is a directory and we are moving
		 * it across directories, its ".." entry will be
		 * inconsistent until we replace that down below.
		 *
		 * In case there is already an entry with the same
		 * name at the destination directory, remove it first.
		 */
		error = xfs_dir_replace(tp, target_dp, target_name,
					src_ip->i_ino, spaceres);
		if (error)
			return error;

		xfs_trans_ichgtime(tp, target_dp,
					XFS_ICHGTIME_MOD | XFS_ICHGTIME_CHG);

		/*
		 * Decrement the link count on the target since the target
		 * dir no longer points to it.
		 */
		error = xfs_droplink(tp, target_ip);
		if (error)
			return error;

		if (src_is_directory) {
			/*
			 * Drop the link from the old "." entry.
			 */
			error = xfs_droplink(tp, target_ip);
			if (error)
				return error;
		}
	} /* target_ip != NULL */

	/*
	 * Remove the source.
	 */
	if (new_parent && src_is_directory) {
		/*
		 * Rewrite the ".." entry to point to the new
		 * directory.
		 */
		error = xfs_dir_replace(tp, src_ip, &xfs_name_dotdot,
					target_dp->i_ino, spaceres);
		ASSERT(error != -EEXIST);
		if (error)
			return error;
	}

	/*
	 * We always want to hit the ctime on the source inode.
	 *
	 * This isn't strictly required by the standards since the source
	 * inode isn't really being changed, but old unix file systems did
	 * it and some incremental backup programs won't work without it.
	 */
	xfs_trans_ichgtime(tp, src_ip, XFS_ICHGTIME_CHG);
	xfs_trans_log_inode(tp, src_ip, XFS_ILOG_CORE);

	/*
	 * Adjust the link count on src_dp.  This is necessary when
	 * renaming a directory, either within one parent when
	 * the target existed, or across two parent directories.
	 */
	if (src_is_directory && (new_parent || target_ip != NULL)) {

		/*
		 * Decrement link count on src_directory since the
		 * entry that's moved no longer points to it.
		 */
		error = xfs_droplink(tp, src_dp);
		if (error)
			return error;
	}

	/*
	 * For whiteouts, we only need to update the source dirent with the
	 * inode number of the whiteout inode rather than removing it
	 * altogether.
	 */
	if (du_wip->ip)
		error = xfs_dir_replace(tp, src_dp, src_name, du_wip->ip->i_ino,
					spaceres);
	else
		error = xfs_dir_removename(tp, src_dp, src_name, src_ip->i_ino,
					   spaceres);
	if (error)
		return error;

	xfs_trans_ichgtime(tp, src_dp, XFS_ICHGTIME_MOD | XFS_ICHGTIME_CHG);
	xfs_trans_log_inode(tp, src_dp, XFS_ILOG_CORE);
	if (new_parent)
		xfs_trans_log_inode(tp, target_dp, XFS_ILOG_CORE);

	/* Schedule parent pointer updates. */
	if (du_wip->ppargs) {
		error = xfs_parent_addname(tp, du_wip->ppargs, src_dp,
				src_name, du_wip->ip);
		if (error)
			return error;
	}

	if (du_src->ppargs) {
		error = xfs_parent_replacename(tp, du_src->ppargs, src_dp,
				src_name, target_dp, target_name, src_ip);
		if (error)
			return error;
	}

	if (du_tgt->ppargs) {
		error = xfs_parent_removename(tp, du_tgt->ppargs, target_dp,
				target_name, target_ip);
		if (error)
			return error;
	}

	/*
	 * Inform our hook clients that we've finished a rename operation as
	 * follows: removed the source and target files from their directories;
	 * that we've added the source to the target directory; and finally
	 * that we've added the whiteout, if there was one.  All inodes are
	 * locked, so it's ok to model a rename this way so long as we say we
	 * deleted entries before we add new ones.
	 */
	if (target_ip)
		xfs_dir_update_hook(target_dp, target_ip, -1, target_name);
	xfs_dir_update_hook(src_dp, src_ip, -1, src_name);
	xfs_dir_update_hook(target_dp, src_ip, 1, target_name);
	if (du_wip->ip)
		xfs_dir_update_hook(src_dp, du_wip->ip, 1, src_name);
	return 0;
}
