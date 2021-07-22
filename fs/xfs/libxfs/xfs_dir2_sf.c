// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2000-2003,2005 Silicon Graphics, Inc.
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
#include "xfs_dir2.h"
#include "xfs_dir2_priv.h"
#include "xfs_trace.h"

/*
 * Prototypes for internal functions.
 */
static void xfs_dir2_sf_addname_easy(xfs_da_args_t *args,
				     xfs_dir2_sf_entry_t *sfep,
				     xfs_dir2_data_aoff_t offset,
				     int new_isize);
static void xfs_dir2_sf_addname_hard(xfs_da_args_t *args, int objchange,
				     int new_isize);
static int xfs_dir2_sf_addname_pick(xfs_da_args_t *args, int objchange,
				    xfs_dir2_sf_entry_t **sfepp,
				    xfs_dir2_data_aoff_t *offsetp);
#ifdef DEBUG
static void xfs_dir2_sf_check(xfs_da_args_t *args);
#else
#define	xfs_dir2_sf_check(args)
#endif /* DEBUG */

static void xfs_dir2_sf_toino4(xfs_da_args_t *args);
static void xfs_dir2_sf_toino8(xfs_da_args_t *args);

int
xfs_dir2_sf_entsize(
	struct xfs_mount	*mp,
	struct xfs_dir2_sf_hdr	*hdr,
	int			len)
{
	int			count = len;

	count += sizeof(struct xfs_dir2_sf_entry);	/* namelen + offset */
	count += hdr->i8count ? XFS_INO64_SIZE : XFS_INO32_SIZE; /* ino # */

	if (xfs_sb_version_hasftype(&mp->m_sb))
		count += sizeof(uint8_t);
	return count;
}

struct xfs_dir2_sf_entry *
xfs_dir2_sf_nextentry(
	struct xfs_mount	*mp,
	struct xfs_dir2_sf_hdr	*hdr,
	struct xfs_dir2_sf_entry *sfep)
{
	return (void *)sfep + xfs_dir2_sf_entsize(mp, hdr, sfep->namelen);
}

/*
 * In short-form directory entries the inode numbers are stored at variable
 * offset behind the entry name. If the entry stores a filetype value, then it
 * sits between the name and the inode number.  The actual inode numbers can
 * come in two formats as well, either 4 bytes or 8 bytes wide.
 */
xfs_ino_t
xfs_dir2_sf_get_ino(
	struct xfs_mount		*mp,
	struct xfs_dir2_sf_hdr		*hdr,
	struct xfs_dir2_sf_entry	*sfep)
{
	uint8_t				*from = sfep->name + sfep->namelen;

	if (xfs_sb_version_hasftype(&mp->m_sb))
		from++;

	if (!hdr->i8count)
		return get_unaligned_be32(from);
	return get_unaligned_be64(from) & XFS_MAXINUMBER;
}

void
xfs_dir2_sf_put_ino(
	struct xfs_mount		*mp,
	struct xfs_dir2_sf_hdr		*hdr,
	struct xfs_dir2_sf_entry	*sfep,
	xfs_ino_t			ino)
{
	uint8_t				*to = sfep->name + sfep->namelen;

	ASSERT(ino <= XFS_MAXINUMBER);

	if (xfs_sb_version_hasftype(&mp->m_sb))
		to++;

	if (hdr->i8count)
		put_unaligned_be64(ino, to);
	else
		put_unaligned_be32(ino, to);
}

xfs_ino_t
xfs_dir2_sf_get_parent_ino(
	struct xfs_dir2_sf_hdr	*hdr)
{
	if (!hdr->i8count)
		return get_unaligned_be32(hdr->parent);
	return get_unaligned_be64(hdr->parent) & XFS_MAXINUMBER;
}

void
xfs_dir2_sf_put_parent_ino(
	struct xfs_dir2_sf_hdr		*hdr,
	xfs_ino_t			ino)
{
	ASSERT(ino <= XFS_MAXINUMBER);

	if (hdr->i8count)
		put_unaligned_be64(ino, hdr->parent);
	else
		put_unaligned_be32(ino, hdr->parent);
}

/*
 * The file type field is stored at the end of the name for filetype enabled
 * shortform directories, or not at all otherwise.
 */
uint8_t
xfs_dir2_sf_get_ftype(
	struct xfs_mount		*mp,
	struct xfs_dir2_sf_entry	*sfep)
{
	if (xfs_sb_version_hasftype(&mp->m_sb)) {
		uint8_t			ftype = sfep->name[sfep->namelen];

		if (ftype < XFS_DIR3_FT_MAX)
			return ftype;
	}

	return XFS_DIR3_FT_UNKNOWN;
}

void
xfs_dir2_sf_put_ftype(
	struct xfs_mount	*mp,
	struct xfs_dir2_sf_entry *sfep,
	uint8_t			ftype)
{
	ASSERT(ftype < XFS_DIR3_FT_MAX);

	if (xfs_sb_version_hasftype(&mp->m_sb))
		sfep->name[sfep->namelen] = ftype;
}

/*
 * Given a block directory (dp/block), calculate its size as a shortform (sf)
 * directory and a header for the sf directory, if it will fit it the
 * space currently present in the inode.  If it won't fit, the output
 * size is too big (but not accurate).
 */
int						/* size for sf form */
xfs_dir2_block_sfsize(
	xfs_inode_t		*dp,		/* incore inode pointer */
	xfs_dir2_data_hdr_t	*hdr,		/* block directory data */
	xfs_dir2_sf_hdr_t	*sfhp)		/* output: header for sf form */
{
	xfs_dir2_dataptr_t	addr;		/* data entry address */
	xfs_dir2_leaf_entry_t	*blp;		/* leaf area of the block */
	xfs_dir2_block_tail_t	*btp;		/* tail area of the block */
	int			count;		/* shortform entry count */
	xfs_dir2_data_entry_t	*dep;		/* data entry in the block */
	int			i;		/* block entry index */
	int			i8count;	/* count of big-inode entries */
	int			isdot;		/* entry is "." */
	int			isdotdot;	/* entry is ".." */
	xfs_mount_t		*mp;		/* mount structure pointer */
	int			namelen;	/* total name bytes */
	xfs_ino_t		parent = 0;	/* parent inode number */
	int			size=0;		/* total computed size */
	int			has_ftype;
	struct xfs_da_geometry	*geo;

	mp = dp->i_mount;
	geo = mp->m_dir_geo;

	/*
	 * if there is a filetype field, add the extra byte to the namelen
	 * for each entry that we see.
	 */
	has_ftype = xfs_sb_version_hasftype(&mp->m_sb) ? 1 : 0;

	count = i8count = namelen = 0;
	btp = xfs_dir2_block_tail_p(geo, hdr);
	blp = xfs_dir2_block_leaf_p(btp);

	/*
	 * Iterate over the block's data entries by using the leaf pointers.
	 */
	for (i = 0; i < be32_to_cpu(btp->count); i++) {
		if ((addr = be32_to_cpu(blp[i].address)) == XFS_DIR2_NULL_DATAPTR)
			continue;
		/*
		 * Calculate the pointer to the entry at hand.
		 */
		dep = (xfs_dir2_data_entry_t *)((char *)hdr +
				xfs_dir2_dataptr_to_off(geo, addr));
		/*
		 * Detect . and .., so we can special-case them.
		 * . is not included in sf directories.
		 * .. is included by just the parent inode number.
		 */
		isdot = dep->namelen == 1 && dep->name[0] == '.';
		isdotdot =
			dep->namelen == 2 &&
			dep->name[0] == '.' && dep->name[1] == '.';

		if (!isdot)
			i8count += be64_to_cpu(dep->inumber) > XFS_DIR2_MAX_SHORT_INUM;

		/* take into account the file type field */
		if (!isdot && !isdotdot) {
			count++;
			namelen += dep->namelen + has_ftype;
		} else if (isdotdot)
			parent = be64_to_cpu(dep->inumber);
		/*
		 * Calculate the new size, see if we should give up yet.
		 */
		size = xfs_dir2_sf_hdr_size(i8count) +	/* header */
		       count * 3 * sizeof(u8) +		/* namelen + offset */
		       namelen +			/* name */
		       (i8count ?			/* inumber */
				count * XFS_INO64_SIZE :
				count * XFS_INO32_SIZE);
		if (size > XFS_IFORK_DSIZE(dp))
			return size;		/* size value is a failure */
	}
	/*
	 * Create the output header, if it worked.
	 */
	sfhp->count = count;
	sfhp->i8count = i8count;
	xfs_dir2_sf_put_parent_ino(sfhp, parent);
	return size;
}

/*
 * Convert a block format directory to shortform.
 * Caller has already checked that it will fit, and built us a header.
 */
int						/* error */
xfs_dir2_block_to_sf(
	struct xfs_da_args	*args,		/* operation arguments */
	struct xfs_buf		*bp,
	int			size,		/* shortform directory size */
	struct xfs_dir2_sf_hdr	*sfhp)		/* shortform directory hdr */
{
	struct xfs_inode	*dp = args->dp;
	struct xfs_mount	*mp = dp->i_mount;
	int			error;		/* error return value */
	int			logflags;	/* inode logging flags */
	struct xfs_dir2_sf_entry *sfep;		/* shortform entry */
	struct xfs_dir2_sf_hdr	*sfp;		/* shortform directory header */
	unsigned int		offset = args->geo->data_entry_offset;
	unsigned int		end;

	trace_xfs_dir2_block_to_sf(args);

	/*
	 * Allocate a temporary destination buffer the size of the inode to
	 * format the data into.  Once we have formatted the data, we can free
	 * the block and copy the formatted data into the inode literal area.
	 */
	sfp = kmem_alloc(mp->m_sb.sb_inodesize, 0);
	memcpy(sfp, sfhp, xfs_dir2_sf_hdr_size(sfhp->i8count));

	/*
	 * Loop over the active and unused entries.  Stop when we reach the
	 * leaf/tail portion of the block.
	 */
	end = xfs_dir3_data_end_offset(args->geo, bp->b_addr);
	sfep = xfs_dir2_sf_firstentry(sfp);
	while (offset < end) {
		struct xfs_dir2_data_unused	*dup = bp->b_addr + offset;
		struct xfs_dir2_data_entry	*dep = bp->b_addr + offset;

		/*
		 * If it's unused, just skip over it.
		 */
		if (be16_to_cpu(dup->freetag) == XFS_DIR2_DATA_FREE_TAG) {
			offset += be16_to_cpu(dup->length);
			continue;
		}

		/*
		 * Skip .
		 */
		if (dep->namelen == 1 && dep->name[0] == '.')
			ASSERT(be64_to_cpu(dep->inumber) == dp->i_ino);
		/*
		 * Skip .., but make sure the inode number is right.
		 */
		else if (dep->namelen == 2 &&
			 dep->name[0] == '.' && dep->name[1] == '.')
			ASSERT(be64_to_cpu(dep->inumber) ==
			       xfs_dir2_sf_get_parent_ino(sfp));
		/*
		 * Normal entry, copy it into shortform.
		 */
		else {
			sfep->namelen = dep->namelen;
			xfs_dir2_sf_put_offset(sfep, offset);
			memcpy(sfep->name, dep->name, dep->namelen);
			xfs_dir2_sf_put_ino(mp, sfp, sfep,
					      be64_to_cpu(dep->inumber));
			xfs_dir2_sf_put_ftype(mp, sfep,
					xfs_dir2_data_get_ftype(mp, dep));

			sfep = xfs_dir2_sf_nextentry(mp, sfp, sfep);
		}
		offset += xfs_dir2_data_entsize(mp, dep->namelen);
	}
	ASSERT((char *)sfep - (char *)sfp == size);

	/* now we are done with the block, we can shrink the inode */
	logflags = XFS_ILOG_CORE;
	error = xfs_dir2_shrink_inode(args, args->geo->datablk, bp);
	if (error) {
		ASSERT(error != -ENOSPC);
		goto out;
	}

	/*
	 * The buffer is now unconditionally gone, whether
	 * xfs_dir2_shrink_inode worked or not.
	 *
	 * Convert the inode to local format and copy the data in.
	 */
	ASSERT(dp->i_df.if_bytes == 0);
	xfs_init_local_fork(dp, XFS_DATA_FORK, sfp, size);
	dp->i_df.if_format = XFS_DINODE_FMT_LOCAL;
	dp->i_disk_size = size;

	logflags |= XFS_ILOG_DDATA;
	xfs_dir2_sf_check(args);
out:
	xfs_trans_log_inode(args->trans, dp, logflags);
	kmem_free(sfp);
	return error;
}

/*
 * Add a name to a shortform directory.
 * There are two algorithms, "easy" and "hard" which we decide on
 * before changing anything.
 * Convert to block form if necessary, if the new entry won't fit.
 */
int						/* error */
xfs_dir2_sf_addname(
	xfs_da_args_t		*args)		/* operation arguments */
{
	xfs_inode_t		*dp;		/* incore directory inode */
	int			error;		/* error return value */
	int			incr_isize;	/* total change in size */
	int			new_isize;	/* size after adding name */
	int			objchange;	/* changing to 8-byte inodes */
	xfs_dir2_data_aoff_t	offset = 0;	/* offset for new entry */
	int			pick;		/* which algorithm to use */
	xfs_dir2_sf_hdr_t	*sfp;		/* shortform structure */
	xfs_dir2_sf_entry_t	*sfep = NULL;	/* shortform entry */

	trace_xfs_dir2_sf_addname(args);

	ASSERT(xfs_dir2_sf_lookup(args) == -ENOENT);
	dp = args->dp;
	ASSERT(dp->i_df.if_format == XFS_DINODE_FMT_LOCAL);
	ASSERT(dp->i_disk_size >= offsetof(struct xfs_dir2_sf_hdr, parent));
	ASSERT(dp->i_df.if_bytes == dp->i_disk_size);
	ASSERT(dp->i_df.if_u1.if_data != NULL);
	sfp = (xfs_dir2_sf_hdr_t *)dp->i_df.if_u1.if_data;
	ASSERT(dp->i_disk_size >= xfs_dir2_sf_hdr_size(sfp->i8count));
	/*
	 * Compute entry (and change in) size.
	 */
	incr_isize = xfs_dir2_sf_entsize(dp->i_mount, sfp, args->namelen);
	objchange = 0;

	/*
	 * Do we have to change to 8 byte inodes?
	 */
	if (args->inumber > XFS_DIR2_MAX_SHORT_INUM && sfp->i8count == 0) {
		/*
		 * Yes, adjust the inode size.  old count + (parent + new)
		 */
		incr_isize += (sfp->count + 2) * XFS_INO64_DIFF;
		objchange = 1;
	}

	new_isize = (int)dp->i_disk_size + incr_isize;
	/*
	 * Won't fit as shortform any more (due to size),
	 * or the pick routine says it won't (due to offset values).
	 */
	if (new_isize > XFS_IFORK_DSIZE(dp) ||
	    (pick =
	     xfs_dir2_sf_addname_pick(args, objchange, &sfep, &offset)) == 0) {
		/*
		 * Just checking or no space reservation, it doesn't fit.
		 */
		if ((args->op_flags & XFS_DA_OP_JUSTCHECK) || args->total == 0)
			return -ENOSPC;
		/*
		 * Convert to block form then add the name.
		 */
		error = xfs_dir2_sf_to_block(args);
		if (error)
			return error;
		return xfs_dir2_block_addname(args);
	}
	/*
	 * Just checking, it fits.
	 */
	if (args->op_flags & XFS_DA_OP_JUSTCHECK)
		return 0;
	/*
	 * Do it the easy way - just add it at the end.
	 */
	if (pick == 1)
		xfs_dir2_sf_addname_easy(args, sfep, offset, new_isize);
	/*
	 * Do it the hard way - look for a place to insert the new entry.
	 * Convert to 8 byte inode numbers first if necessary.
	 */
	else {
		ASSERT(pick == 2);
		if (objchange)
			xfs_dir2_sf_toino8(args);
		xfs_dir2_sf_addname_hard(args, objchange, new_isize);
	}
	xfs_trans_log_inode(args->trans, dp, XFS_ILOG_CORE | XFS_ILOG_DDATA);
	return 0;
}

/*
 * Add the new entry the "easy" way.
 * This is copying the old directory and adding the new entry at the end.
 * Since it's sorted by "offset" we need room after the last offset
 * that's already there, and then room to convert to a block directory.
 * This is already checked by the pick routine.
 */
static void
xfs_dir2_sf_addname_easy(
	xfs_da_args_t		*args,		/* operation arguments */
	xfs_dir2_sf_entry_t	*sfep,		/* pointer to new entry */
	xfs_dir2_data_aoff_t	offset,		/* offset to use for new ent */
	int			new_isize)	/* new directory size */
{
	struct xfs_inode	*dp = args->dp;
	struct xfs_mount	*mp = dp->i_mount;
	int			byteoff;	/* byte offset in sf dir */
	xfs_dir2_sf_hdr_t	*sfp;		/* shortform structure */

	sfp = (xfs_dir2_sf_hdr_t *)dp->i_df.if_u1.if_data;
	byteoff = (int)((char *)sfep - (char *)sfp);
	/*
	 * Grow the in-inode space.
	 */
	xfs_idata_realloc(dp, xfs_dir2_sf_entsize(mp, sfp, args->namelen),
			  XFS_DATA_FORK);
	/*
	 * Need to set up again due to realloc of the inode data.
	 */
	sfp = (xfs_dir2_sf_hdr_t *)dp->i_df.if_u1.if_data;
	sfep = (xfs_dir2_sf_entry_t *)((char *)sfp + byteoff);
	/*
	 * Fill in the new entry.
	 */
	sfep->namelen = args->namelen;
	xfs_dir2_sf_put_offset(sfep, offset);
	memcpy(sfep->name, args->name, sfep->namelen);
	xfs_dir2_sf_put_ino(mp, sfp, sfep, args->inumber);
	xfs_dir2_sf_put_ftype(mp, sfep, args->filetype);

	/*
	 * Update the header and inode.
	 */
	sfp->count++;
	if (args->inumber > XFS_DIR2_MAX_SHORT_INUM)
		sfp->i8count++;
	dp->i_disk_size = new_isize;
	xfs_dir2_sf_check(args);
}

/*
 * Add the new entry the "hard" way.
 * The caller has already converted to 8 byte inode numbers if necessary,
 * in which case we need to leave the i8count at 1.
 * Find a hole that the new entry will fit into, and copy
 * the first part of the entries, the new entry, and the last part of
 * the entries.
 */
/* ARGSUSED */
static void
xfs_dir2_sf_addname_hard(
	xfs_da_args_t		*args,		/* operation arguments */
	int			objchange,	/* changing inode number size */
	int			new_isize)	/* new directory size */
{
	struct xfs_inode	*dp = args->dp;
	struct xfs_mount	*mp = dp->i_mount;
	int			add_datasize;	/* data size need for new ent */
	char			*buf;		/* buffer for old */
	int			eof;		/* reached end of old dir */
	int			nbytes;		/* temp for byte copies */
	xfs_dir2_data_aoff_t	new_offset;	/* next offset value */
	xfs_dir2_data_aoff_t	offset;		/* current offset value */
	int			old_isize;	/* previous size */
	xfs_dir2_sf_entry_t	*oldsfep;	/* entry in original dir */
	xfs_dir2_sf_hdr_t	*oldsfp;	/* original shortform dir */
	xfs_dir2_sf_entry_t	*sfep;		/* entry in new dir */
	xfs_dir2_sf_hdr_t	*sfp;		/* new shortform dir */

	/*
	 * Copy the old directory to the stack buffer.
	 */
	sfp = (xfs_dir2_sf_hdr_t *)dp->i_df.if_u1.if_data;
	old_isize = (int)dp->i_disk_size;
	buf = kmem_alloc(old_isize, 0);
	oldsfp = (xfs_dir2_sf_hdr_t *)buf;
	memcpy(oldsfp, sfp, old_isize);
	/*
	 * Loop over the old directory finding the place we're going
	 * to insert the new entry.
	 * If it's going to end up at the end then oldsfep will point there.
	 */
	for (offset = args->geo->data_first_offset,
	      oldsfep = xfs_dir2_sf_firstentry(oldsfp),
	      add_datasize = xfs_dir2_data_entsize(mp, args->namelen),
	      eof = (char *)oldsfep == &buf[old_isize];
	     !eof;
	     offset = new_offset + xfs_dir2_data_entsize(mp, oldsfep->namelen),
	      oldsfep = xfs_dir2_sf_nextentry(mp, oldsfp, oldsfep),
	      eof = (char *)oldsfep == &buf[old_isize]) {
		new_offset = xfs_dir2_sf_get_offset(oldsfep);
		if (offset + add_datasize <= new_offset)
			break;
	}
	/*
	 * Get rid of the old directory, then allocate space for
	 * the new one.  We do this so xfs_idata_realloc won't copy
	 * the data.
	 */
	xfs_idata_realloc(dp, -old_isize, XFS_DATA_FORK);
	xfs_idata_realloc(dp, new_isize, XFS_DATA_FORK);
	/*
	 * Reset the pointer since the buffer was reallocated.
	 */
	sfp = (xfs_dir2_sf_hdr_t *)dp->i_df.if_u1.if_data;
	/*
	 * Copy the first part of the directory, including the header.
	 */
	nbytes = (int)((char *)oldsfep - (char *)oldsfp);
	memcpy(sfp, oldsfp, nbytes);
	sfep = (xfs_dir2_sf_entry_t *)((char *)sfp + nbytes);
	/*
	 * Fill in the new entry, and update the header counts.
	 */
	sfep->namelen = args->namelen;
	xfs_dir2_sf_put_offset(sfep, offset);
	memcpy(sfep->name, args->name, sfep->namelen);
	xfs_dir2_sf_put_ino(mp, sfp, sfep, args->inumber);
	xfs_dir2_sf_put_ftype(mp, sfep, args->filetype);
	sfp->count++;
	if (args->inumber > XFS_DIR2_MAX_SHORT_INUM && !objchange)
		sfp->i8count++;
	/*
	 * If there's more left to copy, do that.
	 */
	if (!eof) {
		sfep = xfs_dir2_sf_nextentry(mp, sfp, sfep);
		memcpy(sfep, oldsfep, old_isize - nbytes);
	}
	kmem_free(buf);
	dp->i_disk_size = new_isize;
	xfs_dir2_sf_check(args);
}

/*
 * Decide if the new entry will fit at all.
 * If it will fit, pick between adding the new entry to the end (easy)
 * or somewhere else (hard).
 * Return 0 (won't fit), 1 (easy), 2 (hard).
 */
/*ARGSUSED*/
static int					/* pick result */
xfs_dir2_sf_addname_pick(
	xfs_da_args_t		*args,		/* operation arguments */
	int			objchange,	/* inode # size changes */
	xfs_dir2_sf_entry_t	**sfepp,	/* out(1): new entry ptr */
	xfs_dir2_data_aoff_t	*offsetp)	/* out(1): new offset */
{
	struct xfs_inode	*dp = args->dp;
	struct xfs_mount	*mp = dp->i_mount;
	int			holefit;	/* found hole it will fit in */
	int			i;		/* entry number */
	xfs_dir2_data_aoff_t	offset;		/* data block offset */
	xfs_dir2_sf_entry_t	*sfep;		/* shortform entry */
	xfs_dir2_sf_hdr_t	*sfp;		/* shortform structure */
	int			size;		/* entry's data size */
	int			used;		/* data bytes used */

	sfp = (xfs_dir2_sf_hdr_t *)dp->i_df.if_u1.if_data;
	size = xfs_dir2_data_entsize(mp, args->namelen);
	offset = args->geo->data_first_offset;
	sfep = xfs_dir2_sf_firstentry(sfp);
	holefit = 0;
	/*
	 * Loop over sf entries.
	 * Keep track of data offset and whether we've seen a place
	 * to insert the new entry.
	 */
	for (i = 0; i < sfp->count; i++) {
		if (!holefit)
			holefit = offset + size <= xfs_dir2_sf_get_offset(sfep);
		offset = xfs_dir2_sf_get_offset(sfep) +
			 xfs_dir2_data_entsize(mp, sfep->namelen);
		sfep = xfs_dir2_sf_nextentry(mp, sfp, sfep);
	}
	/*
	 * Calculate data bytes used excluding the new entry, if this
	 * was a data block (block form directory).
	 */
	used = offset +
	       (sfp->count + 3) * (uint)sizeof(xfs_dir2_leaf_entry_t) +
	       (uint)sizeof(xfs_dir2_block_tail_t);
	/*
	 * If it won't fit in a block form then we can't insert it,
	 * we'll go back, convert to block, then try the insert and convert
	 * to leaf.
	 */
	if (used + (holefit ? 0 : size) > args->geo->blksize)
		return 0;
	/*
	 * If changing the inode number size, do it the hard way.
	 */
	if (objchange)
		return 2;
	/*
	 * If it won't fit at the end then do it the hard way (use the hole).
	 */
	if (used + size > args->geo->blksize)
		return 2;
	/*
	 * Do it the easy way.
	 */
	*sfepp = sfep;
	*offsetp = offset;
	return 1;
}

#ifdef DEBUG
/*
 * Check consistency of shortform directory, assert if bad.
 */
static void
xfs_dir2_sf_check(
	xfs_da_args_t		*args)		/* operation arguments */
{
	struct xfs_inode	*dp = args->dp;
	struct xfs_mount	*mp = dp->i_mount;
	int			i;		/* entry number */
	int			i8count;	/* number of big inode#s */
	xfs_ino_t		ino;		/* entry inode number */
	int			offset;		/* data offset */
	xfs_dir2_sf_entry_t	*sfep;		/* shortform dir entry */
	xfs_dir2_sf_hdr_t	*sfp;		/* shortform structure */

	sfp = (xfs_dir2_sf_hdr_t *)dp->i_df.if_u1.if_data;
	offset = args->geo->data_first_offset;
	ino = xfs_dir2_sf_get_parent_ino(sfp);
	i8count = ino > XFS_DIR2_MAX_SHORT_INUM;

	for (i = 0, sfep = xfs_dir2_sf_firstentry(sfp);
	     i < sfp->count;
	     i++, sfep = xfs_dir2_sf_nextentry(mp, sfp, sfep)) {
		ASSERT(xfs_dir2_sf_get_offset(sfep) >= offset);
		ino = xfs_dir2_sf_get_ino(mp, sfp, sfep);
		i8count += ino > XFS_DIR2_MAX_SHORT_INUM;
		offset =
			xfs_dir2_sf_get_offset(sfep) +
			xfs_dir2_data_entsize(mp, sfep->namelen);
		ASSERT(xfs_dir2_sf_get_ftype(mp, sfep) < XFS_DIR3_FT_MAX);
	}
	ASSERT(i8count == sfp->i8count);
	ASSERT((char *)sfep - (char *)sfp == dp->i_disk_size);
	ASSERT(offset +
	       (sfp->count + 2) * (uint)sizeof(xfs_dir2_leaf_entry_t) +
	       (uint)sizeof(xfs_dir2_block_tail_t) <= args->geo->blksize);
}
#endif	/* DEBUG */

/* Verify the consistency of an inline directory. */
xfs_failaddr_t
xfs_dir2_sf_verify(
	struct xfs_inode		*ip)
{
	struct xfs_mount		*mp = ip->i_mount;
	struct xfs_ifork		*ifp = XFS_IFORK_PTR(ip, XFS_DATA_FORK);
	struct xfs_dir2_sf_hdr		*sfp;
	struct xfs_dir2_sf_entry	*sfep;
	struct xfs_dir2_sf_entry	*next_sfep;
	char				*endp;
	xfs_ino_t			ino;
	int				i;
	int				i8count;
	int				offset;
	int64_t				size;
	int				error;
	uint8_t				filetype;

	ASSERT(ifp->if_format == XFS_DINODE_FMT_LOCAL);

	sfp = (struct xfs_dir2_sf_hdr *)ifp->if_u1.if_data;
	size = ifp->if_bytes;

	/*
	 * Give up if the directory is way too short.
	 */
	if (size <= offsetof(struct xfs_dir2_sf_hdr, parent) ||
	    size < xfs_dir2_sf_hdr_size(sfp->i8count))
		return __this_address;

	endp = (char *)sfp + size;

	/* Check .. entry */
	ino = xfs_dir2_sf_get_parent_ino(sfp);
	i8count = ino > XFS_DIR2_MAX_SHORT_INUM;
	error = xfs_dir_ino_validate(mp, ino);
	if (error)
		return __this_address;
	offset = mp->m_dir_geo->data_first_offset;

	/* Check all reported entries */
	sfep = xfs_dir2_sf_firstentry(sfp);
	for (i = 0; i < sfp->count; i++) {
		/*
		 * struct xfs_dir2_sf_entry has a variable length.
		 * Check the fixed-offset parts of the structure are
		 * within the data buffer.
		 */
		if (((char *)sfep + sizeof(*sfep)) >= endp)
			return __this_address;

		/* Don't allow names with known bad length. */
		if (sfep->namelen == 0)
			return __this_address;

		/*
		 * Check that the variable-length part of the structure is
		 * within the data buffer.  The next entry starts after the
		 * name component, so nextentry is an acceptable test.
		 */
		next_sfep = xfs_dir2_sf_nextentry(mp, sfp, sfep);
		if (endp < (char *)next_sfep)
			return __this_address;

		/* Check that the offsets always increase. */
		if (xfs_dir2_sf_get_offset(sfep) < offset)
			return __this_address;

		/* Check the inode number. */
		ino = xfs_dir2_sf_get_ino(mp, sfp, sfep);
		i8count += ino > XFS_DIR2_MAX_SHORT_INUM;
		error = xfs_dir_ino_validate(mp, ino);
		if (error)
			return __this_address;

		/* Check the file type. */
		filetype = xfs_dir2_sf_get_ftype(mp, sfep);
		if (filetype >= XFS_DIR3_FT_MAX)
			return __this_address;

		offset = xfs_dir2_sf_get_offset(sfep) +
				xfs_dir2_data_entsize(mp, sfep->namelen);

		sfep = next_sfep;
	}
	if (i8count != sfp->i8count)
		return __this_address;
	if ((void *)sfep != (void *)endp)
		return __this_address;

	/* Make sure this whole thing ought to be in local format. */
	if (offset + (sfp->count + 2) * (uint)sizeof(xfs_dir2_leaf_entry_t) +
	    (uint)sizeof(xfs_dir2_block_tail_t) > mp->m_dir_geo->blksize)
		return __this_address;

	return NULL;
}

/*
 * Create a new (shortform) directory.
 */
int					/* error, always 0 */
xfs_dir2_sf_create(
	xfs_da_args_t	*args,		/* operation arguments */
	xfs_ino_t	pino)		/* parent inode number */
{
	xfs_inode_t	*dp;		/* incore directory inode */
	int		i8count;	/* parent inode is an 8-byte number */
	xfs_dir2_sf_hdr_t *sfp;		/* shortform structure */
	int		size;		/* directory size */

	trace_xfs_dir2_sf_create(args);

	dp = args->dp;

	ASSERT(dp != NULL);
	ASSERT(dp->i_disk_size == 0);
	/*
	 * If it's currently a zero-length extent file,
	 * convert it to local format.
	 */
	if (dp->i_df.if_format == XFS_DINODE_FMT_EXTENTS) {
		dp->i_df.if_format = XFS_DINODE_FMT_LOCAL;
		xfs_trans_log_inode(args->trans, dp, XFS_ILOG_CORE);
	}
	ASSERT(dp->i_df.if_format == XFS_DINODE_FMT_LOCAL);
	ASSERT(dp->i_df.if_bytes == 0);
	i8count = pino > XFS_DIR2_MAX_SHORT_INUM;
	size = xfs_dir2_sf_hdr_size(i8count);
	/*
	 * Make a buffer for the data.
	 */
	xfs_idata_realloc(dp, size, XFS_DATA_FORK);
	/*
	 * Fill in the header,
	 */
	sfp = (xfs_dir2_sf_hdr_t *)dp->i_df.if_u1.if_data;
	sfp->i8count = i8count;
	/*
	 * Now can put in the inode number, since i8count is set.
	 */
	xfs_dir2_sf_put_parent_ino(sfp, pino);
	sfp->count = 0;
	dp->i_disk_size = size;
	xfs_dir2_sf_check(args);
	xfs_trans_log_inode(args->trans, dp, XFS_ILOG_CORE | XFS_ILOG_DDATA);
	return 0;
}

/*
 * Lookup an entry in a shortform directory.
 * Returns EEXIST if found, ENOENT if not found.
 */
int						/* error */
xfs_dir2_sf_lookup(
	xfs_da_args_t		*args)		/* operation arguments */
{
	struct xfs_inode	*dp = args->dp;
	struct xfs_mount	*mp = dp->i_mount;
	int			i;		/* entry index */
	int			error;
	xfs_dir2_sf_entry_t	*sfep;		/* shortform directory entry */
	xfs_dir2_sf_hdr_t	*sfp;		/* shortform structure */
	enum xfs_dacmp		cmp;		/* comparison result */
	xfs_dir2_sf_entry_t	*ci_sfep;	/* case-insens. entry */

	trace_xfs_dir2_sf_lookup(args);

	xfs_dir2_sf_check(args);

	ASSERT(dp->i_df.if_format == XFS_DINODE_FMT_LOCAL);
	ASSERT(dp->i_disk_size >= offsetof(struct xfs_dir2_sf_hdr, parent));
	ASSERT(dp->i_df.if_bytes == dp->i_disk_size);
	ASSERT(dp->i_df.if_u1.if_data != NULL);
	sfp = (xfs_dir2_sf_hdr_t *)dp->i_df.if_u1.if_data;
	ASSERT(dp->i_disk_size >= xfs_dir2_sf_hdr_size(sfp->i8count));
	/*
	 * Special case for .
	 */
	if (args->namelen == 1 && args->name[0] == '.') {
		args->inumber = dp->i_ino;
		args->cmpresult = XFS_CMP_EXACT;
		args->filetype = XFS_DIR3_FT_DIR;
		return -EEXIST;
	}
	/*
	 * Special case for ..
	 */
	if (args->namelen == 2 &&
	    args->name[0] == '.' && args->name[1] == '.') {
		args->inumber = xfs_dir2_sf_get_parent_ino(sfp);
		args->cmpresult = XFS_CMP_EXACT;
		args->filetype = XFS_DIR3_FT_DIR;
		return -EEXIST;
	}
	/*
	 * Loop over all the entries trying to match ours.
	 */
	ci_sfep = NULL;
	for (i = 0, sfep = xfs_dir2_sf_firstentry(sfp); i < sfp->count;
	     i++, sfep = xfs_dir2_sf_nextentry(mp, sfp, sfep)) {
		/*
		 * Compare name and if it's an exact match, return the inode
		 * number. If it's the first case-insensitive match, store the
		 * inode number and continue looking for an exact match.
		 */
		cmp = xfs_dir2_compname(args, sfep->name, sfep->namelen);
		if (cmp != XFS_CMP_DIFFERENT && cmp != args->cmpresult) {
			args->cmpresult = cmp;
			args->inumber = xfs_dir2_sf_get_ino(mp, sfp, sfep);
			args->filetype = xfs_dir2_sf_get_ftype(mp, sfep);
			if (cmp == XFS_CMP_EXACT)
				return -EEXIST;
			ci_sfep = sfep;
		}
	}
	ASSERT(args->op_flags & XFS_DA_OP_OKNOENT);
	/*
	 * Here, we can only be doing a lookup (not a rename or replace).
	 * If a case-insensitive match was not found, return -ENOENT.
	 */
	if (!ci_sfep)
		return -ENOENT;
	/* otherwise process the CI match as required by the caller */
	error = xfs_dir_cilookup_result(args, ci_sfep->name, ci_sfep->namelen);
	return error;
}

/*
 * Remove an entry from a shortform directory.
 */
int						/* error */
xfs_dir2_sf_removename(
	xfs_da_args_t		*args)
{
	struct xfs_inode	*dp = args->dp;
	struct xfs_mount	*mp = dp->i_mount;
	int			byteoff;	/* offset of removed entry */
	int			entsize;	/* this entry's size */
	int			i;		/* shortform entry index */
	int			newsize;	/* new inode size */
	int			oldsize;	/* old inode size */
	xfs_dir2_sf_entry_t	*sfep;		/* shortform directory entry */
	xfs_dir2_sf_hdr_t	*sfp;		/* shortform structure */

	trace_xfs_dir2_sf_removename(args);

	ASSERT(dp->i_df.if_format == XFS_DINODE_FMT_LOCAL);
	oldsize = (int)dp->i_disk_size;
	ASSERT(oldsize >= offsetof(struct xfs_dir2_sf_hdr, parent));
	ASSERT(dp->i_df.if_bytes == oldsize);
	ASSERT(dp->i_df.if_u1.if_data != NULL);
	sfp = (xfs_dir2_sf_hdr_t *)dp->i_df.if_u1.if_data;
	ASSERT(oldsize >= xfs_dir2_sf_hdr_size(sfp->i8count));
	/*
	 * Loop over the old directory entries.
	 * Find the one we're deleting.
	 */
	for (i = 0, sfep = xfs_dir2_sf_firstentry(sfp); i < sfp->count;
	     i++, sfep = xfs_dir2_sf_nextentry(mp, sfp, sfep)) {
		if (xfs_da_compname(args, sfep->name, sfep->namelen) ==
								XFS_CMP_EXACT) {
			ASSERT(xfs_dir2_sf_get_ino(mp, sfp, sfep) ==
			       args->inumber);
			break;
		}
	}
	/*
	 * Didn't find it.
	 */
	if (i == sfp->count)
		return -ENOENT;
	/*
	 * Calculate sizes.
	 */
	byteoff = (int)((char *)sfep - (char *)sfp);
	entsize = xfs_dir2_sf_entsize(mp, sfp, args->namelen);
	newsize = oldsize - entsize;
	/*
	 * Copy the part if any after the removed entry, sliding it down.
	 */
	if (byteoff + entsize < oldsize)
		memmove((char *)sfp + byteoff, (char *)sfp + byteoff + entsize,
			oldsize - (byteoff + entsize));
	/*
	 * Fix up the header and file size.
	 */
	sfp->count--;
	dp->i_disk_size = newsize;
	/*
	 * Reallocate, making it smaller.
	 */
	xfs_idata_realloc(dp, newsize - oldsize, XFS_DATA_FORK);
	sfp = (xfs_dir2_sf_hdr_t *)dp->i_df.if_u1.if_data;
	/*
	 * Are we changing inode number size?
	 */
	if (args->inumber > XFS_DIR2_MAX_SHORT_INUM) {
		if (sfp->i8count == 1)
			xfs_dir2_sf_toino4(args);
		else
			sfp->i8count--;
	}
	xfs_dir2_sf_check(args);
	xfs_trans_log_inode(args->trans, dp, XFS_ILOG_CORE | XFS_ILOG_DDATA);
	return 0;
}

/*
 * Check whether the sf dir replace operation need more blocks.
 */
static bool
xfs_dir2_sf_replace_needblock(
	struct xfs_inode	*dp,
	xfs_ino_t		inum)
{
	int			newsize;
	struct xfs_dir2_sf_hdr	*sfp;

	if (dp->i_df.if_format != XFS_DINODE_FMT_LOCAL)
		return false;

	sfp = (struct xfs_dir2_sf_hdr *)dp->i_df.if_u1.if_data;
	newsize = dp->i_df.if_bytes + (sfp->count + 1) * XFS_INO64_DIFF;

	return inum > XFS_DIR2_MAX_SHORT_INUM &&
	       sfp->i8count == 0 && newsize > XFS_IFORK_DSIZE(dp);
}

/*
 * Replace the inode number of an entry in a shortform directory.
 */
int						/* error */
xfs_dir2_sf_replace(
	xfs_da_args_t		*args)		/* operation arguments */
{
	struct xfs_inode	*dp = args->dp;
	struct xfs_mount	*mp = dp->i_mount;
	int			i;		/* entry index */
	xfs_ino_t		ino=0;		/* entry old inode number */
	int			i8elevated;	/* sf_toino8 set i8count=1 */
	xfs_dir2_sf_entry_t	*sfep;		/* shortform directory entry */
	xfs_dir2_sf_hdr_t	*sfp;		/* shortform structure */

	trace_xfs_dir2_sf_replace(args);

	ASSERT(dp->i_df.if_format == XFS_DINODE_FMT_LOCAL);
	ASSERT(dp->i_disk_size >= offsetof(struct xfs_dir2_sf_hdr, parent));
	ASSERT(dp->i_df.if_bytes == dp->i_disk_size);
	ASSERT(dp->i_df.if_u1.if_data != NULL);
	sfp = (xfs_dir2_sf_hdr_t *)dp->i_df.if_u1.if_data;
	ASSERT(dp->i_disk_size >= xfs_dir2_sf_hdr_size(sfp->i8count));

	/*
	 * New inode number is large, and need to convert to 8-byte inodes.
	 */
	if (args->inumber > XFS_DIR2_MAX_SHORT_INUM && sfp->i8count == 0) {
		int	error;			/* error return value */

		/*
		 * Won't fit as shortform, convert to block then do replace.
		 */
		if (xfs_dir2_sf_replace_needblock(dp, args->inumber)) {
			error = xfs_dir2_sf_to_block(args);
			if (error)
				return error;
			return xfs_dir2_block_replace(args);
		}
		/*
		 * Still fits, convert to 8-byte now.
		 */
		xfs_dir2_sf_toino8(args);
		i8elevated = 1;
		sfp = (xfs_dir2_sf_hdr_t *)dp->i_df.if_u1.if_data;
	} else
		i8elevated = 0;

	ASSERT(args->namelen != 1 || args->name[0] != '.');
	/*
	 * Replace ..'s entry.
	 */
	if (args->namelen == 2 &&
	    args->name[0] == '.' && args->name[1] == '.') {
		ino = xfs_dir2_sf_get_parent_ino(sfp);
		ASSERT(args->inumber != ino);
		xfs_dir2_sf_put_parent_ino(sfp, args->inumber);
	}
	/*
	 * Normal entry, look for the name.
	 */
	else {
		for (i = 0, sfep = xfs_dir2_sf_firstentry(sfp); i < sfp->count;
		     i++, sfep = xfs_dir2_sf_nextentry(mp, sfp, sfep)) {
			if (xfs_da_compname(args, sfep->name, sfep->namelen) ==
								XFS_CMP_EXACT) {
				ino = xfs_dir2_sf_get_ino(mp, sfp, sfep);
				ASSERT(args->inumber != ino);
				xfs_dir2_sf_put_ino(mp, sfp, sfep,
						args->inumber);
				xfs_dir2_sf_put_ftype(mp, sfep, args->filetype);
				break;
			}
		}
		/*
		 * Didn't find it.
		 */
		if (i == sfp->count) {
			ASSERT(args->op_flags & XFS_DA_OP_OKNOENT);
			if (i8elevated)
				xfs_dir2_sf_toino4(args);
			return -ENOENT;
		}
	}
	/*
	 * See if the old number was large, the new number is small.
	 */
	if (ino > XFS_DIR2_MAX_SHORT_INUM &&
	    args->inumber <= XFS_DIR2_MAX_SHORT_INUM) {
		/*
		 * And the old count was one, so need to convert to small.
		 */
		if (sfp->i8count == 1)
			xfs_dir2_sf_toino4(args);
		else
			sfp->i8count--;
	}
	/*
	 * See if the old number was small, the new number is large.
	 */
	if (ino <= XFS_DIR2_MAX_SHORT_INUM &&
	    args->inumber > XFS_DIR2_MAX_SHORT_INUM) {
		/*
		 * add to the i8count unless we just converted to 8-byte
		 * inodes (which does an implied i8count = 1)
		 */
		ASSERT(sfp->i8count != 0);
		if (!i8elevated)
			sfp->i8count++;
	}
	xfs_dir2_sf_check(args);
	xfs_trans_log_inode(args->trans, dp, XFS_ILOG_DDATA);
	return 0;
}

/*
 * Convert from 8-byte inode numbers to 4-byte inode numbers.
 * The last 8-byte inode number is gone, but the count is still 1.
 */
static void
xfs_dir2_sf_toino4(
	xfs_da_args_t		*args)		/* operation arguments */
{
	struct xfs_inode	*dp = args->dp;
	struct xfs_mount	*mp = dp->i_mount;
	char			*buf;		/* old dir's buffer */
	int			i;		/* entry index */
	int			newsize;	/* new inode size */
	xfs_dir2_sf_entry_t	*oldsfep;	/* old sf entry */
	xfs_dir2_sf_hdr_t	*oldsfp;	/* old sf directory */
	int			oldsize;	/* old inode size */
	xfs_dir2_sf_entry_t	*sfep;		/* new sf entry */
	xfs_dir2_sf_hdr_t	*sfp;		/* new sf directory */

	trace_xfs_dir2_sf_toino4(args);

	/*
	 * Copy the old directory to the buffer.
	 * Then nuke it from the inode, and add the new buffer to the inode.
	 * Don't want xfs_idata_realloc copying the data here.
	 */
	oldsize = dp->i_df.if_bytes;
	buf = kmem_alloc(oldsize, 0);
	oldsfp = (xfs_dir2_sf_hdr_t *)dp->i_df.if_u1.if_data;
	ASSERT(oldsfp->i8count == 1);
	memcpy(buf, oldsfp, oldsize);
	/*
	 * Compute the new inode size.
	 */
	newsize = oldsize - (oldsfp->count + 1) * XFS_INO64_DIFF;
	xfs_idata_realloc(dp, -oldsize, XFS_DATA_FORK);
	xfs_idata_realloc(dp, newsize, XFS_DATA_FORK);
	/*
	 * Reset our pointers, the data has moved.
	 */
	oldsfp = (xfs_dir2_sf_hdr_t *)buf;
	sfp = (xfs_dir2_sf_hdr_t *)dp->i_df.if_u1.if_data;
	/*
	 * Fill in the new header.
	 */
	sfp->count = oldsfp->count;
	sfp->i8count = 0;
	xfs_dir2_sf_put_parent_ino(sfp, xfs_dir2_sf_get_parent_ino(oldsfp));
	/*
	 * Copy the entries field by field.
	 */
	for (i = 0, sfep = xfs_dir2_sf_firstentry(sfp),
		    oldsfep = xfs_dir2_sf_firstentry(oldsfp);
	     i < sfp->count;
	     i++, sfep = xfs_dir2_sf_nextentry(mp, sfp, sfep),
		  oldsfep = xfs_dir2_sf_nextentry(mp, oldsfp, oldsfep)) {
		sfep->namelen = oldsfep->namelen;
		memcpy(sfep->offset, oldsfep->offset, sizeof(sfep->offset));
		memcpy(sfep->name, oldsfep->name, sfep->namelen);
		xfs_dir2_sf_put_ino(mp, sfp, sfep,
				xfs_dir2_sf_get_ino(mp, oldsfp, oldsfep));
		xfs_dir2_sf_put_ftype(mp, sfep,
				xfs_dir2_sf_get_ftype(mp, oldsfep));
	}
	/*
	 * Clean up the inode.
	 */
	kmem_free(buf);
	dp->i_disk_size = newsize;
	xfs_trans_log_inode(args->trans, dp, XFS_ILOG_CORE | XFS_ILOG_DDATA);
}

/*
 * Convert existing entries from 4-byte inode numbers to 8-byte inode numbers.
 * The new entry w/ an 8-byte inode number is not there yet; we leave with
 * i8count set to 1, but no corresponding 8-byte entry.
 */
static void
xfs_dir2_sf_toino8(
	xfs_da_args_t		*args)		/* operation arguments */
{
	struct xfs_inode	*dp = args->dp;
	struct xfs_mount	*mp = dp->i_mount;
	char			*buf;		/* old dir's buffer */
	int			i;		/* entry index */
	int			newsize;	/* new inode size */
	xfs_dir2_sf_entry_t	*oldsfep;	/* old sf entry */
	xfs_dir2_sf_hdr_t	*oldsfp;	/* old sf directory */
	int			oldsize;	/* old inode size */
	xfs_dir2_sf_entry_t	*sfep;		/* new sf entry */
	xfs_dir2_sf_hdr_t	*sfp;		/* new sf directory */

	trace_xfs_dir2_sf_toino8(args);

	/*
	 * Copy the old directory to the buffer.
	 * Then nuke it from the inode, and add the new buffer to the inode.
	 * Don't want xfs_idata_realloc copying the data here.
	 */
	oldsize = dp->i_df.if_bytes;
	buf = kmem_alloc(oldsize, 0);
	oldsfp = (xfs_dir2_sf_hdr_t *)dp->i_df.if_u1.if_data;
	ASSERT(oldsfp->i8count == 0);
	memcpy(buf, oldsfp, oldsize);
	/*
	 * Compute the new inode size (nb: entry count + 1 for parent)
	 */
	newsize = oldsize + (oldsfp->count + 1) * XFS_INO64_DIFF;
	xfs_idata_realloc(dp, -oldsize, XFS_DATA_FORK);
	xfs_idata_realloc(dp, newsize, XFS_DATA_FORK);
	/*
	 * Reset our pointers, the data has moved.
	 */
	oldsfp = (xfs_dir2_sf_hdr_t *)buf;
	sfp = (xfs_dir2_sf_hdr_t *)dp->i_df.if_u1.if_data;
	/*
	 * Fill in the new header.
	 */
	sfp->count = oldsfp->count;
	sfp->i8count = 1;
	xfs_dir2_sf_put_parent_ino(sfp, xfs_dir2_sf_get_parent_ino(oldsfp));
	/*
	 * Copy the entries field by field.
	 */
	for (i = 0, sfep = xfs_dir2_sf_firstentry(sfp),
		    oldsfep = xfs_dir2_sf_firstentry(oldsfp);
	     i < sfp->count;
	     i++, sfep = xfs_dir2_sf_nextentry(mp, sfp, sfep),
		  oldsfep = xfs_dir2_sf_nextentry(mp, oldsfp, oldsfep)) {
		sfep->namelen = oldsfep->namelen;
		memcpy(sfep->offset, oldsfep->offset, sizeof(sfep->offset));
		memcpy(sfep->name, oldsfep->name, sfep->namelen);
		xfs_dir2_sf_put_ino(mp, sfp, sfep,
				xfs_dir2_sf_get_ino(mp, oldsfp, oldsfep));
		xfs_dir2_sf_put_ftype(mp, sfep,
				xfs_dir2_sf_get_ftype(mp, oldsfep));
	}
	/*
	 * Clean up the inode.
	 */
	kmem_free(buf);
	dp->i_disk_size = newsize;
	xfs_trans_log_inode(args->trans, dp, XFS_ILOG_CORE | XFS_ILOG_DDATA);
}
