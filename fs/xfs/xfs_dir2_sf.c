/*
 * Copyright (c) 2000-2003,2005 Silicon Graphics, Inc.
 * All Rights Reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it would be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write the Free Software Foundation,
 * Inc.,  51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */
#include "xfs.h"
#include "xfs_fs.h"
#include "xfs_types.h"
#include "xfs_log.h"
#include "xfs_inum.h"
#include "xfs_trans.h"
#include "xfs_sb.h"
#include "xfs_dir.h"
#include "xfs_dir2.h"
#include "xfs_dmapi.h"
#include "xfs_mount.h"
#include "xfs_da_btree.h"
#include "xfs_bmap_btree.h"
#include "xfs_dir_sf.h"
#include "xfs_dir2_sf.h"
#include "xfs_attr_sf.h"
#include "xfs_dinode.h"
#include "xfs_inode.h"
#include "xfs_inode_item.h"
#include "xfs_dir_leaf.h"
#include "xfs_error.h"
#include "xfs_dir2_data.h"
#include "xfs_dir2_leaf.h"
#include "xfs_dir2_block.h"
#include "xfs_dir2_trace.h"

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
#if XFS_BIG_INUMS
static void xfs_dir2_sf_toino4(xfs_da_args_t *args);
static void xfs_dir2_sf_toino8(xfs_da_args_t *args);
#endif /* XFS_BIG_INUMS */

/*
 * Given a block directory (dp/block), calculate its size as a shortform (sf)
 * directory and a header for the sf directory, if it will fit it the
 * space currently present in the inode.  If it won't fit, the output
 * size is too big (but not accurate).
 */
int						/* size for sf form */
xfs_dir2_block_sfsize(
	xfs_inode_t		*dp,		/* incore inode pointer */
	xfs_dir2_block_t	*block,		/* block directory data */
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

	mp = dp->i_mount;

	count = i8count = namelen = 0;
	btp = XFS_DIR2_BLOCK_TAIL_P(mp, block);
	blp = XFS_DIR2_BLOCK_LEAF_P(btp);

	/*
	 * Iterate over the block's data entries by using the leaf pointers.
	 */
	for (i = 0; i < be32_to_cpu(btp->count); i++) {
		if ((addr = be32_to_cpu(blp[i].address)) == XFS_DIR2_NULL_DATAPTR)
			continue;
		/*
		 * Calculate the pointer to the entry at hand.
		 */
		dep = (xfs_dir2_data_entry_t *)
		      ((char *)block + XFS_DIR2_DATAPTR_TO_OFF(mp, addr));
		/*
		 * Detect . and .., so we can special-case them.
		 * . is not included in sf directories.
		 * .. is included by just the parent inode number.
		 */
		isdot = dep->namelen == 1 && dep->name[0] == '.';
		isdotdot =
			dep->namelen == 2 &&
			dep->name[0] == '.' && dep->name[1] == '.';
#if XFS_BIG_INUMS
		if (!isdot)
			i8count += be64_to_cpu(dep->inumber) > XFS_DIR2_MAX_SHORT_INUM;
#endif
		if (!isdot && !isdotdot) {
			count++;
			namelen += dep->namelen;
		} else if (isdotdot)
			parent = be64_to_cpu(dep->inumber);
		/*
		 * Calculate the new size, see if we should give up yet.
		 */
		size = XFS_DIR2_SF_HDR_SIZE(i8count) +		/* header */
		       count +					/* namelen */
		       count * (uint)sizeof(xfs_dir2_sf_off_t) + /* offset */
		       namelen +				/* name */
		       (i8count ?				/* inumber */
				(uint)sizeof(xfs_dir2_ino8_t) * count :
				(uint)sizeof(xfs_dir2_ino4_t) * count);
		if (size > XFS_IFORK_DSIZE(dp))
			return size;		/* size value is a failure */
	}
	/*
	 * Create the output header, if it worked.
	 */
	sfhp->count = count;
	sfhp->i8count = i8count;
	XFS_DIR2_SF_PUT_INUMBER((xfs_dir2_sf_t *)sfhp, &parent, &sfhp->parent);
	return size;
}

/*
 * Convert a block format directory to shortform.
 * Caller has already checked that it will fit, and built us a header.
 */
int						/* error */
xfs_dir2_block_to_sf(
	xfs_da_args_t		*args,		/* operation arguments */
	xfs_dabuf_t		*bp,		/* block buffer */
	int			size,		/* shortform directory size */
	xfs_dir2_sf_hdr_t	*sfhp)		/* shortform directory hdr */
{
	xfs_dir2_block_t	*block;		/* block structure */
	xfs_dir2_block_tail_t	*btp;		/* block tail pointer */
	xfs_dir2_data_entry_t	*dep;		/* data entry pointer */
	xfs_inode_t		*dp;		/* incore directory inode */
	xfs_dir2_data_unused_t	*dup;		/* unused data pointer */
	char			*endptr;	/* end of data entries */
	int			error;		/* error return value */
	int			logflags;	/* inode logging flags */
	xfs_mount_t		*mp;		/* filesystem mount point */
	char			*ptr;		/* current data pointer */
	xfs_dir2_sf_entry_t	*sfep;		/* shortform entry */
	xfs_dir2_sf_t		*sfp;		/* shortform structure */
	xfs_ino_t               temp;

	xfs_dir2_trace_args_sb("block_to_sf", args, size, bp);
	dp = args->dp;
	mp = dp->i_mount;

	/*
	 * Make a copy of the block data, so we can shrink the inode
	 * and add local data.
	 */
	block = kmem_alloc(mp->m_dirblksize, KM_SLEEP);
	memcpy(block, bp->data, mp->m_dirblksize);
	logflags = XFS_ILOG_CORE;
	if ((error = xfs_dir2_shrink_inode(args, mp->m_dirdatablk, bp))) {
		ASSERT(error != ENOSPC);
		goto out;
	}
	/*
	 * The buffer is now unconditionally gone, whether
	 * xfs_dir2_shrink_inode worked or not.
	 *
	 * Convert the inode to local format.
	 */
	dp->i_df.if_flags &= ~XFS_IFEXTENTS;
	dp->i_df.if_flags |= XFS_IFINLINE;
	dp->i_d.di_format = XFS_DINODE_FMT_LOCAL;
	ASSERT(dp->i_df.if_bytes == 0);
	xfs_idata_realloc(dp, size, XFS_DATA_FORK);
	logflags |= XFS_ILOG_DDATA;
	/*
	 * Copy the header into the newly allocate local space.
	 */
	sfp = (xfs_dir2_sf_t *)dp->i_df.if_u1.if_data;
	memcpy(sfp, sfhp, XFS_DIR2_SF_HDR_SIZE(sfhp->i8count));
	dp->i_d.di_size = size;
	/*
	 * Set up to loop over the block's entries.
	 */
	btp = XFS_DIR2_BLOCK_TAIL_P(mp, block);
	ptr = (char *)block->u;
	endptr = (char *)XFS_DIR2_BLOCK_LEAF_P(btp);
	sfep = XFS_DIR2_SF_FIRSTENTRY(sfp);
	/*
	 * Loop over the active and unused entries.
	 * Stop when we reach the leaf/tail portion of the block.
	 */
	while (ptr < endptr) {
		/*
		 * If it's unused, just skip over it.
		 */
		dup = (xfs_dir2_data_unused_t *)ptr;
		if (be16_to_cpu(dup->freetag) == XFS_DIR2_DATA_FREE_TAG) {
			ptr += be16_to_cpu(dup->length);
			continue;
		}
		dep = (xfs_dir2_data_entry_t *)ptr;
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
			       XFS_DIR2_SF_GET_INUMBER(sfp, &sfp->hdr.parent));
		/*
		 * Normal entry, copy it into shortform.
		 */
		else {
			sfep->namelen = dep->namelen;
			XFS_DIR2_SF_PUT_OFFSET(sfep,
				(xfs_dir2_data_aoff_t)
				((char *)dep - (char *)block));
			memcpy(sfep->name, dep->name, dep->namelen);
			temp = be64_to_cpu(dep->inumber);
			XFS_DIR2_SF_PUT_INUMBER(sfp, &temp,
				XFS_DIR2_SF_INUMBERP(sfep));
			sfep = XFS_DIR2_SF_NEXTENTRY(sfp, sfep);
		}
		ptr += XFS_DIR2_DATA_ENTSIZE(dep->namelen);
	}
	ASSERT((char *)sfep - (char *)sfp == size);
	xfs_dir2_sf_check(args);
out:
	xfs_trans_log_inode(args->trans, dp, logflags);
	kmem_free(block, mp->m_dirblksize);
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
	int			add_entsize;	/* size of the new entry */
	xfs_inode_t		*dp;		/* incore directory inode */
	int			error;		/* error return value */
	int			incr_isize;	/* total change in size */
	int			new_isize;	/* di_size after adding name */
	int			objchange;	/* changing to 8-byte inodes */
	xfs_dir2_data_aoff_t	offset = 0;	/* offset for new entry */
	int			old_isize;	/* di_size before adding name */
	int			pick;		/* which algorithm to use */
	xfs_dir2_sf_t		*sfp;		/* shortform structure */
	xfs_dir2_sf_entry_t	*sfep = NULL;	/* shortform entry */

	xfs_dir2_trace_args("sf_addname", args);
	ASSERT(xfs_dir2_sf_lookup(args) == ENOENT);
	dp = args->dp;
	ASSERT(dp->i_df.if_flags & XFS_IFINLINE);
	/*
	 * Make sure the shortform value has some of its header.
	 */
	if (dp->i_d.di_size < offsetof(xfs_dir2_sf_hdr_t, parent)) {
		ASSERT(XFS_FORCED_SHUTDOWN(dp->i_mount));
		return XFS_ERROR(EIO);
	}
	ASSERT(dp->i_df.if_bytes == dp->i_d.di_size);
	ASSERT(dp->i_df.if_u1.if_data != NULL);
	sfp = (xfs_dir2_sf_t *)dp->i_df.if_u1.if_data;
	ASSERT(dp->i_d.di_size >= XFS_DIR2_SF_HDR_SIZE(sfp->hdr.i8count));
	/*
	 * Compute entry (and change in) size.
	 */
	add_entsize = XFS_DIR2_SF_ENTSIZE_BYNAME(sfp, args->namelen);
	incr_isize = add_entsize;
	objchange = 0;
#if XFS_BIG_INUMS
	/*
	 * Do we have to change to 8 byte inodes?
	 */
	if (args->inumber > XFS_DIR2_MAX_SHORT_INUM && sfp->hdr.i8count == 0) {
		/*
		 * Yes, adjust the entry size and the total size.
		 */
		add_entsize +=
			(uint)sizeof(xfs_dir2_ino8_t) -
			(uint)sizeof(xfs_dir2_ino4_t);
		incr_isize +=
			(sfp->hdr.count + 2) *
			((uint)sizeof(xfs_dir2_ino8_t) -
			 (uint)sizeof(xfs_dir2_ino4_t));
		objchange = 1;
	}
#endif
	old_isize = (int)dp->i_d.di_size;
	new_isize = old_isize + incr_isize;
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
		if (args->justcheck || args->total == 0)
			return XFS_ERROR(ENOSPC);
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
	if (args->justcheck)
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
#if XFS_BIG_INUMS
		if (objchange)
			xfs_dir2_sf_toino8(args);
#endif
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
	int			byteoff;	/* byte offset in sf dir */
	xfs_inode_t		*dp;		/* incore directory inode */
	xfs_dir2_sf_t		*sfp;		/* shortform structure */

	dp = args->dp;

	sfp = (xfs_dir2_sf_t *)dp->i_df.if_u1.if_data;
	byteoff = (int)((char *)sfep - (char *)sfp);
	/*
	 * Grow the in-inode space.
	 */
	xfs_idata_realloc(dp, XFS_DIR2_SF_ENTSIZE_BYNAME(sfp, args->namelen),
		XFS_DATA_FORK);
	/*
	 * Need to set up again due to realloc of the inode data.
	 */
	sfp = (xfs_dir2_sf_t *)dp->i_df.if_u1.if_data;
	sfep = (xfs_dir2_sf_entry_t *)((char *)sfp + byteoff);
	/*
	 * Fill in the new entry.
	 */
	sfep->namelen = args->namelen;
	XFS_DIR2_SF_PUT_OFFSET(sfep, offset);
	memcpy(sfep->name, args->name, sfep->namelen);
	XFS_DIR2_SF_PUT_INUMBER(sfp, &args->inumber,
		XFS_DIR2_SF_INUMBERP(sfep));
	/*
	 * Update the header and inode.
	 */
	sfp->hdr.count++;
#if XFS_BIG_INUMS
	if (args->inumber > XFS_DIR2_MAX_SHORT_INUM)
		sfp->hdr.i8count++;
#endif
	dp->i_d.di_size = new_isize;
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
	int			add_datasize;	/* data size need for new ent */
	char			*buf;		/* buffer for old */
	xfs_inode_t		*dp;		/* incore directory inode */
	int			eof;		/* reached end of old dir */
	int			nbytes;		/* temp for byte copies */
	xfs_dir2_data_aoff_t	new_offset;	/* next offset value */
	xfs_dir2_data_aoff_t	offset;		/* current offset value */
	int			old_isize;	/* previous di_size */
	xfs_dir2_sf_entry_t	*oldsfep;	/* entry in original dir */
	xfs_dir2_sf_t		*oldsfp;	/* original shortform dir */
	xfs_dir2_sf_entry_t	*sfep;		/* entry in new dir */
	xfs_dir2_sf_t		*sfp;		/* new shortform dir */

	/*
	 * Copy the old directory to the stack buffer.
	 */
	dp = args->dp;

	sfp = (xfs_dir2_sf_t *)dp->i_df.if_u1.if_data;
	old_isize = (int)dp->i_d.di_size;
	buf = kmem_alloc(old_isize, KM_SLEEP);
	oldsfp = (xfs_dir2_sf_t *)buf;
	memcpy(oldsfp, sfp, old_isize);
	/*
	 * Loop over the old directory finding the place we're going
	 * to insert the new entry.
	 * If it's going to end up at the end then oldsfep will point there.
	 */
	for (offset = XFS_DIR2_DATA_FIRST_OFFSET,
	      oldsfep = XFS_DIR2_SF_FIRSTENTRY(oldsfp),
	      add_datasize = XFS_DIR2_DATA_ENTSIZE(args->namelen),
	      eof = (char *)oldsfep == &buf[old_isize];
	     !eof;
	     offset = new_offset + XFS_DIR2_DATA_ENTSIZE(oldsfep->namelen),
	      oldsfep = XFS_DIR2_SF_NEXTENTRY(oldsfp, oldsfep),
	      eof = (char *)oldsfep == &buf[old_isize]) {
		new_offset = XFS_DIR2_SF_GET_OFFSET(oldsfep);
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
	sfp = (xfs_dir2_sf_t *)dp->i_df.if_u1.if_data;
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
	XFS_DIR2_SF_PUT_OFFSET(sfep, offset);
	memcpy(sfep->name, args->name, sfep->namelen);
	XFS_DIR2_SF_PUT_INUMBER(sfp, &args->inumber,
		XFS_DIR2_SF_INUMBERP(sfep));
	sfp->hdr.count++;
#if XFS_BIG_INUMS
	if (args->inumber > XFS_DIR2_MAX_SHORT_INUM && !objchange)
		sfp->hdr.i8count++;
#endif
	/*
	 * If there's more left to copy, do that.
	 */
	if (!eof) {
		sfep = XFS_DIR2_SF_NEXTENTRY(sfp, sfep);
		memcpy(sfep, oldsfep, old_isize - nbytes);
	}
	kmem_free(buf, old_isize);
	dp->i_d.di_size = new_isize;
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
	xfs_inode_t		*dp;		/* incore directory inode */
	int			holefit;	/* found hole it will fit in */
	int			i;		/* entry number */
	xfs_mount_t		*mp;		/* filesystem mount point */
	xfs_dir2_data_aoff_t	offset;		/* data block offset */
	xfs_dir2_sf_entry_t	*sfep;		/* shortform entry */
	xfs_dir2_sf_t		*sfp;		/* shortform structure */
	int			size;		/* entry's data size */
	int			used;		/* data bytes used */

	dp = args->dp;
	mp = dp->i_mount;

	sfp = (xfs_dir2_sf_t *)dp->i_df.if_u1.if_data;
	size = XFS_DIR2_DATA_ENTSIZE(args->namelen);
	offset = XFS_DIR2_DATA_FIRST_OFFSET;
	sfep = XFS_DIR2_SF_FIRSTENTRY(sfp);
	holefit = 0;
	/*
	 * Loop over sf entries.
	 * Keep track of data offset and whether we've seen a place
	 * to insert the new entry.
	 */
	for (i = 0; i < sfp->hdr.count; i++) {
		if (!holefit)
			holefit = offset + size <= XFS_DIR2_SF_GET_OFFSET(sfep);
		offset = XFS_DIR2_SF_GET_OFFSET(sfep) +
			 XFS_DIR2_DATA_ENTSIZE(sfep->namelen);
		sfep = XFS_DIR2_SF_NEXTENTRY(sfp, sfep);
	}
	/*
	 * Calculate data bytes used excluding the new entry, if this
	 * was a data block (block form directory).
	 */
	used = offset +
	       (sfp->hdr.count + 3) * (uint)sizeof(xfs_dir2_leaf_entry_t) +
	       (uint)sizeof(xfs_dir2_block_tail_t);
	/*
	 * If it won't fit in a block form then we can't insert it,
	 * we'll go back, convert to block, then try the insert and convert
	 * to leaf.
	 */
	if (used + (holefit ? 0 : size) > mp->m_dirblksize)
		return 0;
	/*
	 * If changing the inode number size, do it the hard way.
	 */
#if XFS_BIG_INUMS
	if (objchange) {
		return 2;
	}
#else
	ASSERT(objchange == 0);
#endif
	/*
	 * If it won't fit at the end then do it the hard way (use the hole).
	 */
	if (used + size > mp->m_dirblksize)
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
	xfs_inode_t		*dp;		/* incore directory inode */
	int			i;		/* entry number */
	int			i8count;	/* number of big inode#s */
	xfs_ino_t		ino;		/* entry inode number */
	int			offset;		/* data offset */
	xfs_dir2_sf_entry_t	*sfep;		/* shortform dir entry */
	xfs_dir2_sf_t		*sfp;		/* shortform structure */

	dp = args->dp;

	sfp = (xfs_dir2_sf_t *)dp->i_df.if_u1.if_data;
	offset = XFS_DIR2_DATA_FIRST_OFFSET;
	ino = XFS_DIR2_SF_GET_INUMBER(sfp, &sfp->hdr.parent);
	i8count = ino > XFS_DIR2_MAX_SHORT_INUM;

	for (i = 0, sfep = XFS_DIR2_SF_FIRSTENTRY(sfp);
	     i < sfp->hdr.count;
	     i++, sfep = XFS_DIR2_SF_NEXTENTRY(sfp, sfep)) {
		ASSERT(XFS_DIR2_SF_GET_OFFSET(sfep) >= offset);
		ino = XFS_DIR2_SF_GET_INUMBER(sfp, XFS_DIR2_SF_INUMBERP(sfep));
		i8count += ino > XFS_DIR2_MAX_SHORT_INUM;
		offset =
			XFS_DIR2_SF_GET_OFFSET(sfep) +
			XFS_DIR2_DATA_ENTSIZE(sfep->namelen);
	}
	ASSERT(i8count == sfp->hdr.i8count);
	ASSERT(XFS_BIG_INUMS || i8count == 0);
	ASSERT((char *)sfep - (char *)sfp == dp->i_d.di_size);
	ASSERT(offset +
	       (sfp->hdr.count + 2) * (uint)sizeof(xfs_dir2_leaf_entry_t) +
	       (uint)sizeof(xfs_dir2_block_tail_t) <=
	       dp->i_mount->m_dirblksize);
}
#endif	/* DEBUG */

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
	xfs_dir2_sf_t	*sfp;		/* shortform structure */
	int		size;		/* directory size */

	xfs_dir2_trace_args_i("sf_create", args, pino);
	dp = args->dp;

	ASSERT(dp != NULL);
	ASSERT(dp->i_d.di_size == 0);
	/*
	 * If it's currently a zero-length extent file,
	 * convert it to local format.
	 */
	if (dp->i_d.di_format == XFS_DINODE_FMT_EXTENTS) {
		dp->i_df.if_flags &= ~XFS_IFEXTENTS;	/* just in case */
		dp->i_d.di_format = XFS_DINODE_FMT_LOCAL;
		xfs_trans_log_inode(args->trans, dp, XFS_ILOG_CORE);
		dp->i_df.if_flags |= XFS_IFINLINE;
	}
	ASSERT(dp->i_df.if_flags & XFS_IFINLINE);
	ASSERT(dp->i_df.if_bytes == 0);
	i8count = pino > XFS_DIR2_MAX_SHORT_INUM;
	size = XFS_DIR2_SF_HDR_SIZE(i8count);
	/*
	 * Make a buffer for the data.
	 */
	xfs_idata_realloc(dp, size, XFS_DATA_FORK);
	/*
	 * Fill in the header,
	 */
	sfp = (xfs_dir2_sf_t *)dp->i_df.if_u1.if_data;
	sfp->hdr.i8count = i8count;
	/*
	 * Now can put in the inode number, since i8count is set.
	 */
	XFS_DIR2_SF_PUT_INUMBER(sfp, &pino, &sfp->hdr.parent);
	sfp->hdr.count = 0;
	dp->i_d.di_size = size;
	xfs_dir2_sf_check(args);
	xfs_trans_log_inode(args->trans, dp, XFS_ILOG_CORE | XFS_ILOG_DDATA);
	return 0;
}

int						/* error */
xfs_dir2_sf_getdents(
	xfs_inode_t		*dp,		/* incore directory inode */
	uio_t			*uio,		/* caller's buffer control */
	int			*eofp,		/* eof reached? (out) */
	xfs_dirent_t		*dbp,		/* caller's buffer */
	xfs_dir2_put_t		put)		/* abi's formatting function */
{
	int			error;		/* error return value */
	int			i;		/* shortform entry number */
	xfs_mount_t		*mp;		/* filesystem mount point */
	xfs_dir2_dataptr_t	off;		/* current entry's offset */
	xfs_dir2_put_args_t	p;		/* arg package for put rtn */
	xfs_dir2_sf_entry_t	*sfep;		/* shortform directory entry */
	xfs_dir2_sf_t		*sfp;		/* shortform structure */
	xfs_off_t			dir_offset;

	mp = dp->i_mount;

	ASSERT(dp->i_df.if_flags & XFS_IFINLINE);
	/*
	 * Give up if the directory is way too short.
	 */
	if (dp->i_d.di_size < offsetof(xfs_dir2_sf_hdr_t, parent)) {
		ASSERT(XFS_FORCED_SHUTDOWN(mp));
		return XFS_ERROR(EIO);
	}

	dir_offset = uio->uio_offset;

	ASSERT(dp->i_df.if_bytes == dp->i_d.di_size);
	ASSERT(dp->i_df.if_u1.if_data != NULL);

	sfp = (xfs_dir2_sf_t *)dp->i_df.if_u1.if_data;

	ASSERT(dp->i_d.di_size >= XFS_DIR2_SF_HDR_SIZE(sfp->hdr.i8count));

	/*
	 * If the block number in the offset is out of range, we're done.
	 */
	if (XFS_DIR2_DATAPTR_TO_DB(mp, dir_offset) > mp->m_dirdatablk) {
		*eofp = 1;
		return 0;
	}

	/*
	 * Set up putargs structure.
	 */
	p.dbp = dbp;
	p.put = put;
	p.uio = uio;
	/*
	 * Put . entry unless we're starting past it.
	 */
	if (dir_offset <=
		    XFS_DIR2_DB_OFF_TO_DATAPTR(mp, mp->m_dirdatablk,
					       XFS_DIR2_DATA_DOT_OFFSET)) {
		p.cook = XFS_DIR2_DB_OFF_TO_DATAPTR(mp, 0,
						XFS_DIR2_DATA_DOTDOT_OFFSET);
		p.ino = dp->i_ino;
#if XFS_BIG_INUMS
		p.ino += mp->m_inoadd;
#endif
		p.name = ".";
		p.namelen = 1;

		error = p.put(&p);

		if (!p.done) {
			uio->uio_offset =
				XFS_DIR2_DB_OFF_TO_DATAPTR(mp, mp->m_dirdatablk,
						XFS_DIR2_DATA_DOT_OFFSET);
			return error;
		}
	}

	/*
	 * Put .. entry unless we're starting past it.
	 */
	if (dir_offset <=
		    XFS_DIR2_DB_OFF_TO_DATAPTR(mp, mp->m_dirdatablk,
					       XFS_DIR2_DATA_DOTDOT_OFFSET)) {
		p.cook = XFS_DIR2_DB_OFF_TO_DATAPTR(mp, mp->m_dirdatablk,
						XFS_DIR2_DATA_FIRST_OFFSET);
		p.ino = XFS_DIR2_SF_GET_INUMBER(sfp, &sfp->hdr.parent);
#if XFS_BIG_INUMS
		p.ino += mp->m_inoadd;
#endif
		p.name = "..";
		p.namelen = 2;

		error = p.put(&p);

		if (!p.done) {
			uio->uio_offset =
				XFS_DIR2_DB_OFF_TO_DATAPTR(mp, mp->m_dirdatablk,
					XFS_DIR2_DATA_DOTDOT_OFFSET);
			return error;
		}
	}

	/*
	 * Loop while there are more entries and put'ing works.
	 */
	for (i = 0, sfep = XFS_DIR2_SF_FIRSTENTRY(sfp);
		     i < sfp->hdr.count;
			     i++, sfep = XFS_DIR2_SF_NEXTENTRY(sfp, sfep)) {

		off = XFS_DIR2_DB_OFF_TO_DATAPTR(mp, mp->m_dirdatablk,
				XFS_DIR2_SF_GET_OFFSET(sfep));

		if (dir_offset > off)
			continue;

		p.namelen = sfep->namelen;

		p.cook = XFS_DIR2_DB_OFF_TO_DATAPTR(mp, mp->m_dirdatablk,
			XFS_DIR2_SF_GET_OFFSET(sfep) +
			XFS_DIR2_DATA_ENTSIZE(p.namelen));

		p.ino = XFS_DIR2_SF_GET_INUMBER(sfp, XFS_DIR2_SF_INUMBERP(sfep));
#if XFS_BIG_INUMS
		p.ino += mp->m_inoadd;
#endif
		p.name = (char *)sfep->name;

		error = p.put(&p);

		if (!p.done) {
			uio->uio_offset = off;
			return error;
		}
	}

	/*
	 * They all fit.
	 */
	*eofp = 1;

	uio->uio_offset =
		XFS_DIR2_DB_OFF_TO_DATAPTR(mp, mp->m_dirdatablk + 1, 0);

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
	xfs_inode_t		*dp;		/* incore directory inode */
	int			i;		/* entry index */
	xfs_dir2_sf_entry_t	*sfep;		/* shortform directory entry */
	xfs_dir2_sf_t		*sfp;		/* shortform structure */

	xfs_dir2_trace_args("sf_lookup", args);
	xfs_dir2_sf_check(args);
	dp = args->dp;

	ASSERT(dp->i_df.if_flags & XFS_IFINLINE);
	/*
	 * Bail out if the directory is way too short.
	 */
	if (dp->i_d.di_size < offsetof(xfs_dir2_sf_hdr_t, parent)) {
		ASSERT(XFS_FORCED_SHUTDOWN(dp->i_mount));
		return XFS_ERROR(EIO);
	}
	ASSERT(dp->i_df.if_bytes == dp->i_d.di_size);
	ASSERT(dp->i_df.if_u1.if_data != NULL);
	sfp = (xfs_dir2_sf_t *)dp->i_df.if_u1.if_data;
	ASSERT(dp->i_d.di_size >= XFS_DIR2_SF_HDR_SIZE(sfp->hdr.i8count));
	/*
	 * Special case for .
	 */
	if (args->namelen == 1 && args->name[0] == '.') {
		args->inumber = dp->i_ino;
		return XFS_ERROR(EEXIST);
	}
	/*
	 * Special case for ..
	 */
	if (args->namelen == 2 &&
	    args->name[0] == '.' && args->name[1] == '.') {
		args->inumber = XFS_DIR2_SF_GET_INUMBER(sfp, &sfp->hdr.parent);
		return XFS_ERROR(EEXIST);
	}
	/*
	 * Loop over all the entries trying to match ours.
	 */
	for (i = 0, sfep = XFS_DIR2_SF_FIRSTENTRY(sfp);
	     i < sfp->hdr.count;
	     i++, sfep = XFS_DIR2_SF_NEXTENTRY(sfp, sfep)) {
		if (sfep->namelen == args->namelen &&
		    sfep->name[0] == args->name[0] &&
		    memcmp(args->name, sfep->name, args->namelen) == 0) {
			args->inumber =
				XFS_DIR2_SF_GET_INUMBER(sfp,
					XFS_DIR2_SF_INUMBERP(sfep));
			return XFS_ERROR(EEXIST);
		}
	}
	/*
	 * Didn't find it.
	 */
	ASSERT(args->oknoent);
	return XFS_ERROR(ENOENT);
}

/*
 * Remove an entry from a shortform directory.
 */
int						/* error */
xfs_dir2_sf_removename(
	xfs_da_args_t		*args)
{
	int			byteoff;	/* offset of removed entry */
	xfs_inode_t		*dp;		/* incore directory inode */
	int			entsize;	/* this entry's size */
	int			i;		/* shortform entry index */
	int			newsize;	/* new inode size */
	int			oldsize;	/* old inode size */
	xfs_dir2_sf_entry_t	*sfep;		/* shortform directory entry */
	xfs_dir2_sf_t		*sfp;		/* shortform structure */

	xfs_dir2_trace_args("sf_removename", args);
	dp = args->dp;

	ASSERT(dp->i_df.if_flags & XFS_IFINLINE);
	oldsize = (int)dp->i_d.di_size;
	/*
	 * Bail out if the directory is way too short.
	 */
	if (oldsize < offsetof(xfs_dir2_sf_hdr_t, parent)) {
		ASSERT(XFS_FORCED_SHUTDOWN(dp->i_mount));
		return XFS_ERROR(EIO);
	}
	ASSERT(dp->i_df.if_bytes == oldsize);
	ASSERT(dp->i_df.if_u1.if_data != NULL);
	sfp = (xfs_dir2_sf_t *)dp->i_df.if_u1.if_data;
	ASSERT(oldsize >= XFS_DIR2_SF_HDR_SIZE(sfp->hdr.i8count));
	/*
	 * Loop over the old directory entries.
	 * Find the one we're deleting.
	 */
	for (i = 0, sfep = XFS_DIR2_SF_FIRSTENTRY(sfp);
	     i < sfp->hdr.count;
	     i++, sfep = XFS_DIR2_SF_NEXTENTRY(sfp, sfep)) {
		if (sfep->namelen == args->namelen &&
		    sfep->name[0] == args->name[0] &&
		    memcmp(sfep->name, args->name, args->namelen) == 0) {
			ASSERT(XFS_DIR2_SF_GET_INUMBER(sfp,
					XFS_DIR2_SF_INUMBERP(sfep)) ==
				args->inumber);
			break;
		}
	}
	/*
	 * Didn't find it.
	 */
	if (i == sfp->hdr.count) {
		return XFS_ERROR(ENOENT);
	}
	/*
	 * Calculate sizes.
	 */
	byteoff = (int)((char *)sfep - (char *)sfp);
	entsize = XFS_DIR2_SF_ENTSIZE_BYNAME(sfp, args->namelen);
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
	sfp->hdr.count--;
	dp->i_d.di_size = newsize;
	/*
	 * Reallocate, making it smaller.
	 */
	xfs_idata_realloc(dp, newsize - oldsize, XFS_DATA_FORK);
	sfp = (xfs_dir2_sf_t *)dp->i_df.if_u1.if_data;
#if XFS_BIG_INUMS
	/*
	 * Are we changing inode number size?
	 */
	if (args->inumber > XFS_DIR2_MAX_SHORT_INUM) {
		if (sfp->hdr.i8count == 1)
			xfs_dir2_sf_toino4(args);
		else
			sfp->hdr.i8count--;
	}
#endif
	xfs_dir2_sf_check(args);
	xfs_trans_log_inode(args->trans, dp, XFS_ILOG_CORE | XFS_ILOG_DDATA);
	return 0;
}

/*
 * Replace the inode number of an entry in a shortform directory.
 */
int						/* error */
xfs_dir2_sf_replace(
	xfs_da_args_t		*args)		/* operation arguments */
{
	xfs_inode_t		*dp;		/* incore directory inode */
	int			i;		/* entry index */
#if XFS_BIG_INUMS || defined(DEBUG)
	xfs_ino_t		ino=0;		/* entry old inode number */
#endif
#if XFS_BIG_INUMS
	int			i8elevated;	/* sf_toino8 set i8count=1 */
#endif
	xfs_dir2_sf_entry_t	*sfep;		/* shortform directory entry */
	xfs_dir2_sf_t		*sfp;		/* shortform structure */

	xfs_dir2_trace_args("sf_replace", args);
	dp = args->dp;

	ASSERT(dp->i_df.if_flags & XFS_IFINLINE);
	/*
	 * Bail out if the shortform directory is way too small.
	 */
	if (dp->i_d.di_size < offsetof(xfs_dir2_sf_hdr_t, parent)) {
		ASSERT(XFS_FORCED_SHUTDOWN(dp->i_mount));
		return XFS_ERROR(EIO);
	}
	ASSERT(dp->i_df.if_bytes == dp->i_d.di_size);
	ASSERT(dp->i_df.if_u1.if_data != NULL);
	sfp = (xfs_dir2_sf_t *)dp->i_df.if_u1.if_data;
	ASSERT(dp->i_d.di_size >= XFS_DIR2_SF_HDR_SIZE(sfp->hdr.i8count));
#if XFS_BIG_INUMS
	/*
	 * New inode number is large, and need to convert to 8-byte inodes.
	 */
	if (args->inumber > XFS_DIR2_MAX_SHORT_INUM && sfp->hdr.i8count == 0) {
		int	error;			/* error return value */
		int	newsize;		/* new inode size */

		newsize =
			dp->i_df.if_bytes +
			(sfp->hdr.count + 1) *
			((uint)sizeof(xfs_dir2_ino8_t) -
			 (uint)sizeof(xfs_dir2_ino4_t));
		/*
		 * Won't fit as shortform, convert to block then do replace.
		 */
		if (newsize > XFS_IFORK_DSIZE(dp)) {
			error = xfs_dir2_sf_to_block(args);
			if (error) {
				return error;
			}
			return xfs_dir2_block_replace(args);
		}
		/*
		 * Still fits, convert to 8-byte now.
		 */
		xfs_dir2_sf_toino8(args);
		i8elevated = 1;
		sfp = (xfs_dir2_sf_t *)dp->i_df.if_u1.if_data;
	} else
		i8elevated = 0;
#endif
	ASSERT(args->namelen != 1 || args->name[0] != '.');
	/*
	 * Replace ..'s entry.
	 */
	if (args->namelen == 2 &&
	    args->name[0] == '.' && args->name[1] == '.') {
#if XFS_BIG_INUMS || defined(DEBUG)
		ino = XFS_DIR2_SF_GET_INUMBER(sfp, &sfp->hdr.parent);
		ASSERT(args->inumber != ino);
#endif
		XFS_DIR2_SF_PUT_INUMBER(sfp, &args->inumber, &sfp->hdr.parent);
	}
	/*
	 * Normal entry, look for the name.
	 */
	else {
		for (i = 0, sfep = XFS_DIR2_SF_FIRSTENTRY(sfp);
		     i < sfp->hdr.count;
		     i++, sfep = XFS_DIR2_SF_NEXTENTRY(sfp, sfep)) {
			if (sfep->namelen == args->namelen &&
			    sfep->name[0] == args->name[0] &&
			    memcmp(args->name, sfep->name, args->namelen) == 0) {
#if XFS_BIG_INUMS || defined(DEBUG)
				ino = XFS_DIR2_SF_GET_INUMBER(sfp,
					XFS_DIR2_SF_INUMBERP(sfep));
				ASSERT(args->inumber != ino);
#endif
				XFS_DIR2_SF_PUT_INUMBER(sfp, &args->inumber,
					XFS_DIR2_SF_INUMBERP(sfep));
				break;
			}
		}
		/*
		 * Didn't find it.
		 */
		if (i == sfp->hdr.count) {
			ASSERT(args->oknoent);
#if XFS_BIG_INUMS
			if (i8elevated)
				xfs_dir2_sf_toino4(args);
#endif
			return XFS_ERROR(ENOENT);
		}
	}
#if XFS_BIG_INUMS
	/*
	 * See if the old number was large, the new number is small.
	 */
	if (ino > XFS_DIR2_MAX_SHORT_INUM &&
	    args->inumber <= XFS_DIR2_MAX_SHORT_INUM) {
		/*
		 * And the old count was one, so need to convert to small.
		 */
		if (sfp->hdr.i8count == 1)
			xfs_dir2_sf_toino4(args);
		else
			sfp->hdr.i8count--;
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
		ASSERT(sfp->hdr.i8count != 0);
		if (!i8elevated)
			sfp->hdr.i8count++;
	}
#endif
	xfs_dir2_sf_check(args);
	xfs_trans_log_inode(args->trans, dp, XFS_ILOG_DDATA);
	return 0;
}

#if XFS_BIG_INUMS
/*
 * Convert from 8-byte inode numbers to 4-byte inode numbers.
 * The last 8-byte inode number is gone, but the count is still 1.
 */
static void
xfs_dir2_sf_toino4(
	xfs_da_args_t		*args)		/* operation arguments */
{
	char			*buf;		/* old dir's buffer */
	xfs_inode_t		*dp;		/* incore directory inode */
	int			i;		/* entry index */
	xfs_ino_t		ino;		/* entry inode number */
	int			newsize;	/* new inode size */
	xfs_dir2_sf_entry_t	*oldsfep;	/* old sf entry */
	xfs_dir2_sf_t		*oldsfp;	/* old sf directory */
	int			oldsize;	/* old inode size */
	xfs_dir2_sf_entry_t	*sfep;		/* new sf entry */
	xfs_dir2_sf_t		*sfp;		/* new sf directory */

	xfs_dir2_trace_args("sf_toino4", args);
	dp = args->dp;

	/*
	 * Copy the old directory to the buffer.
	 * Then nuke it from the inode, and add the new buffer to the inode.
	 * Don't want xfs_idata_realloc copying the data here.
	 */
	oldsize = dp->i_df.if_bytes;
	buf = kmem_alloc(oldsize, KM_SLEEP);
	oldsfp = (xfs_dir2_sf_t *)dp->i_df.if_u1.if_data;
	ASSERT(oldsfp->hdr.i8count == 1);
	memcpy(buf, oldsfp, oldsize);
	/*
	 * Compute the new inode size.
	 */
	newsize =
		oldsize -
		(oldsfp->hdr.count + 1) *
		((uint)sizeof(xfs_dir2_ino8_t) - (uint)sizeof(xfs_dir2_ino4_t));
	xfs_idata_realloc(dp, -oldsize, XFS_DATA_FORK);
	xfs_idata_realloc(dp, newsize, XFS_DATA_FORK);
	/*
	 * Reset our pointers, the data has moved.
	 */
	oldsfp = (xfs_dir2_sf_t *)buf;
	sfp = (xfs_dir2_sf_t *)dp->i_df.if_u1.if_data;
	/*
	 * Fill in the new header.
	 */
	sfp->hdr.count = oldsfp->hdr.count;
	sfp->hdr.i8count = 0;
	ino = XFS_DIR2_SF_GET_INUMBER(oldsfp, &oldsfp->hdr.parent);
	XFS_DIR2_SF_PUT_INUMBER(sfp, &ino, &sfp->hdr.parent);
	/*
	 * Copy the entries field by field.
	 */
	for (i = 0, sfep = XFS_DIR2_SF_FIRSTENTRY(sfp),
		    oldsfep = XFS_DIR2_SF_FIRSTENTRY(oldsfp);
	     i < sfp->hdr.count;
	     i++, sfep = XFS_DIR2_SF_NEXTENTRY(sfp, sfep),
		  oldsfep = XFS_DIR2_SF_NEXTENTRY(oldsfp, oldsfep)) {
		sfep->namelen = oldsfep->namelen;
		sfep->offset = oldsfep->offset;
		memcpy(sfep->name, oldsfep->name, sfep->namelen);
		ino = XFS_DIR2_SF_GET_INUMBER(oldsfp,
			XFS_DIR2_SF_INUMBERP(oldsfep));
		XFS_DIR2_SF_PUT_INUMBER(sfp, &ino, XFS_DIR2_SF_INUMBERP(sfep));
	}
	/*
	 * Clean up the inode.
	 */
	kmem_free(buf, oldsize);
	dp->i_d.di_size = newsize;
	xfs_trans_log_inode(args->trans, dp, XFS_ILOG_CORE | XFS_ILOG_DDATA);
}

/*
 * Convert from 4-byte inode numbers to 8-byte inode numbers.
 * The new 8-byte inode number is not there yet, we leave with the
 * count 1 but no corresponding entry.
 */
static void
xfs_dir2_sf_toino8(
	xfs_da_args_t		*args)		/* operation arguments */
{
	char			*buf;		/* old dir's buffer */
	xfs_inode_t		*dp;		/* incore directory inode */
	int			i;		/* entry index */
	xfs_ino_t		ino;		/* entry inode number */
	int			newsize;	/* new inode size */
	xfs_dir2_sf_entry_t	*oldsfep;	/* old sf entry */
	xfs_dir2_sf_t		*oldsfp;	/* old sf directory */
	int			oldsize;	/* old inode size */
	xfs_dir2_sf_entry_t	*sfep;		/* new sf entry */
	xfs_dir2_sf_t		*sfp;		/* new sf directory */

	xfs_dir2_trace_args("sf_toino8", args);
	dp = args->dp;

	/*
	 * Copy the old directory to the buffer.
	 * Then nuke it from the inode, and add the new buffer to the inode.
	 * Don't want xfs_idata_realloc copying the data here.
	 */
	oldsize = dp->i_df.if_bytes;
	buf = kmem_alloc(oldsize, KM_SLEEP);
	oldsfp = (xfs_dir2_sf_t *)dp->i_df.if_u1.if_data;
	ASSERT(oldsfp->hdr.i8count == 0);
	memcpy(buf, oldsfp, oldsize);
	/*
	 * Compute the new inode size.
	 */
	newsize =
		oldsize +
		(oldsfp->hdr.count + 1) *
		((uint)sizeof(xfs_dir2_ino8_t) - (uint)sizeof(xfs_dir2_ino4_t));
	xfs_idata_realloc(dp, -oldsize, XFS_DATA_FORK);
	xfs_idata_realloc(dp, newsize, XFS_DATA_FORK);
	/*
	 * Reset our pointers, the data has moved.
	 */
	oldsfp = (xfs_dir2_sf_t *)buf;
	sfp = (xfs_dir2_sf_t *)dp->i_df.if_u1.if_data;
	/*
	 * Fill in the new header.
	 */
	sfp->hdr.count = oldsfp->hdr.count;
	sfp->hdr.i8count = 1;
	ino = XFS_DIR2_SF_GET_INUMBER(oldsfp, &oldsfp->hdr.parent);
	XFS_DIR2_SF_PUT_INUMBER(sfp, &ino, &sfp->hdr.parent);
	/*
	 * Copy the entries field by field.
	 */
	for (i = 0, sfep = XFS_DIR2_SF_FIRSTENTRY(sfp),
		    oldsfep = XFS_DIR2_SF_FIRSTENTRY(oldsfp);
	     i < sfp->hdr.count;
	     i++, sfep = XFS_DIR2_SF_NEXTENTRY(sfp, sfep),
		  oldsfep = XFS_DIR2_SF_NEXTENTRY(oldsfp, oldsfep)) {
		sfep->namelen = oldsfep->namelen;
		sfep->offset = oldsfep->offset;
		memcpy(sfep->name, oldsfep->name, sfep->namelen);
		ino = XFS_DIR2_SF_GET_INUMBER(oldsfp,
			XFS_DIR2_SF_INUMBERP(oldsfep));
		XFS_DIR2_SF_PUT_INUMBER(sfp, &ino, XFS_DIR2_SF_INUMBERP(sfep));
	}
	/*
	 * Clean up the inode.
	 */
	kmem_free(buf, oldsize);
	dp->i_d.di_size = newsize;
	xfs_trans_log_inode(args->trans, dp, XFS_ILOG_CORE | XFS_ILOG_DDATA);
}
#endif	/* XFS_BIG_INUMS */
