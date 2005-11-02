/*
 * Copyright (c) 2000-2001,2005 Silicon Graphics, Inc.
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
#include "xfs_alloc_btree.h"
#include "xfs_ialloc_btree.h"
#include "xfs_alloc.h"
#include "xfs_btree.h"
#include "xfs_dir_sf.h"
#include "xfs_dir2_sf.h"
#include "xfs_attr_sf.h"
#include "xfs_dinode.h"
#include "xfs_inode.h"
#include "xfs_bmap.h"
#include "xfs_dir_leaf.h"
#include "xfs_error.h"

/*
 * xfs_dir.c
 *
 * Provide the external interfaces to manage directories.
 */

/*========================================================================
 * Function prototypes for the kernel.
 *========================================================================*/

/*
 * Functions for the dirops interfaces.
 */
static void	xfs_dir_mount(struct xfs_mount *mp);

static int	xfs_dir_isempty(struct xfs_inode *dp);

static int	xfs_dir_init(struct xfs_trans *trans,
			     struct xfs_inode *dir,
			     struct xfs_inode *parent_dir);

static int	xfs_dir_createname(struct xfs_trans *trans,
				   struct xfs_inode *dp,
				   char *name_string,
				   int name_len,
				   xfs_ino_t inode_number,
				   xfs_fsblock_t *firstblock,
				   xfs_bmap_free_t *flist,
				   xfs_extlen_t total);

static int	xfs_dir_lookup(struct xfs_trans *tp,
			       struct xfs_inode *dp,
			       char *name_string,
			       int name_length,
			       xfs_ino_t *inode_number);

static int	xfs_dir_removename(struct xfs_trans *trans,
				   struct xfs_inode *dp,
				   char *name_string,
				   int name_length,
				   xfs_ino_t ino,
				   xfs_fsblock_t *firstblock,
				   xfs_bmap_free_t *flist,
				   xfs_extlen_t total);

static int	xfs_dir_getdents(struct xfs_trans *tp,
				 struct xfs_inode *dp,
				 struct uio *uiop,
				 int *eofp);

static int	xfs_dir_replace(struct xfs_trans *tp,
				struct xfs_inode *dp,
				char *name_string,
				int name_length,
				xfs_ino_t inode_number,
				xfs_fsblock_t *firstblock,
				xfs_bmap_free_t *flist,
				xfs_extlen_t total);

static int	xfs_dir_canenter(struct xfs_trans *tp,
				 struct xfs_inode *dp,
				 char *name_string,
				 int name_length);

static int	xfs_dir_shortform_validate_ondisk(xfs_mount_t *mp,
						  xfs_dinode_t *dip);

xfs_dirops_t xfsv1_dirops = {
	.xd_mount			= xfs_dir_mount,
	.xd_isempty			= xfs_dir_isempty,
	.xd_init			= xfs_dir_init,
	.xd_createname			= xfs_dir_createname,
	.xd_lookup			= xfs_dir_lookup,
	.xd_removename			= xfs_dir_removename,
	.xd_getdents			= xfs_dir_getdents,
	.xd_replace			= xfs_dir_replace,
	.xd_canenter			= xfs_dir_canenter,
	.xd_shortform_validate_ondisk	= xfs_dir_shortform_validate_ondisk,
	.xd_shortform_to_single		= xfs_dir_shortform_to_leaf,
};

/*
 * Internal routines when dirsize == XFS_LBSIZE(mp).
 */
STATIC int xfs_dir_leaf_lookup(xfs_da_args_t *args);
STATIC int xfs_dir_leaf_removename(xfs_da_args_t *args, int *number_entries,
						 int *total_namebytes);
STATIC int xfs_dir_leaf_getdents(xfs_trans_t *trans, xfs_inode_t *dp,
					     uio_t *uio, int *eofp,
					     xfs_dirent_t *dbp,
					     xfs_dir_put_t put);
STATIC int xfs_dir_leaf_replace(xfs_da_args_t *args);

/*
 * Internal routines when dirsize > XFS_LBSIZE(mp).
 */
STATIC int xfs_dir_node_addname(xfs_da_args_t *args);
STATIC int xfs_dir_node_lookup(xfs_da_args_t *args);
STATIC int xfs_dir_node_removename(xfs_da_args_t *args);
STATIC int xfs_dir_node_getdents(xfs_trans_t *trans, xfs_inode_t *dp,
					     uio_t *uio, int *eofp,
					     xfs_dirent_t *dbp,
					     xfs_dir_put_t put);
STATIC int xfs_dir_node_replace(xfs_da_args_t *args);

#if defined(XFS_DIR_TRACE)
ktrace_t *xfs_dir_trace_buf;
#endif


/*========================================================================
 * Overall external interface routines.
 *========================================================================*/

xfs_dahash_t	xfs_dir_hash_dot, xfs_dir_hash_dotdot;

/*
 * One-time startup routine called from xfs_init().
 */
void
xfs_dir_startup(void)
{
	xfs_dir_hash_dot = xfs_da_hashname(".", 1);
	xfs_dir_hash_dotdot = xfs_da_hashname("..", 2);
}

/*
 * Initialize directory-related fields in the mount structure.
 */
static void
xfs_dir_mount(xfs_mount_t *mp)
{
	uint shortcount, leafcount, count;

	mp->m_dirversion = 1;
	if (mp->m_flags & XFS_MOUNT_COMPAT_ATTR) {
		shortcount = (mp->m_attroffset -
				(uint)sizeof(xfs_dir_sf_hdr_t)) /
				 (uint)sizeof(xfs_dir_sf_entry_t);
		leafcount = (XFS_LBSIZE(mp) -
				(uint)sizeof(xfs_dir_leaf_hdr_t)) /
				 ((uint)sizeof(xfs_dir_leaf_entry_t) +
				  (uint)sizeof(xfs_dir_leaf_name_t));
	} else {
		shortcount = (XFS_BMDR_SPACE_CALC(MINABTPTRS) -
			      (uint)sizeof(xfs_dir_sf_hdr_t)) /
			       (uint)sizeof(xfs_dir_sf_entry_t);
		leafcount = (XFS_LBSIZE(mp) -
			    (uint)sizeof(xfs_dir_leaf_hdr_t)) /
			     ((uint)sizeof(xfs_dir_leaf_entry_t) +
			      (uint)sizeof(xfs_dir_leaf_name_t));
	}
	count = shortcount > leafcount ? shortcount : leafcount;
	mp->m_dircook_elog = xfs_da_log2_roundup(count + 1);
	ASSERT(mp->m_dircook_elog <= mp->m_sb.sb_blocklog);
	mp->m_dir_node_ents = mp->m_attr_node_ents =
		(XFS_LBSIZE(mp) - (uint)sizeof(xfs_da_node_hdr_t)) /
		(uint)sizeof(xfs_da_node_entry_t);
	mp->m_dir_magicpct = (XFS_LBSIZE(mp) * 37) / 100;
	mp->m_dirblksize = mp->m_sb.sb_blocksize;
	mp->m_dirblkfsbs = 1;
}

/*
 * Return 1 if directory contains only "." and "..".
 */
static int
xfs_dir_isempty(xfs_inode_t *dp)
{
	xfs_dir_sf_hdr_t *hdr;

	ASSERT((dp->i_d.di_mode & S_IFMT) == S_IFDIR);
	if (dp->i_d.di_size == 0)
		return(1);
	if (dp->i_d.di_size > XFS_IFORK_DSIZE(dp))
		return(0);
	hdr = (xfs_dir_sf_hdr_t *)dp->i_df.if_u1.if_data;
	return(hdr->count == 0);
}

/*
 * Initialize a directory with its "." and ".." entries.
 */
static int
xfs_dir_init(xfs_trans_t *trans, xfs_inode_t *dir, xfs_inode_t *parent_dir)
{
	xfs_da_args_t args;
	int error;

	memset((char *)&args, 0, sizeof(args));
	args.dp = dir;
	args.trans = trans;

	ASSERT((dir->i_d.di_mode & S_IFMT) == S_IFDIR);
	if ((error = xfs_dir_ino_validate(trans->t_mountp, parent_dir->i_ino)))
		return error;

	return(xfs_dir_shortform_create(&args, parent_dir->i_ino));
}

/*
 * Generic handler routine to add a name to a directory.
 * Transitions directory from shortform to Btree as necessary.
 */
static int							/* error */
xfs_dir_createname(xfs_trans_t *trans, xfs_inode_t *dp, char *name,
		   int namelen, xfs_ino_t inum, xfs_fsblock_t *firstblock,
		   xfs_bmap_free_t *flist, xfs_extlen_t total)
{
	xfs_da_args_t args;
	int retval, newsize, done;

	ASSERT((dp->i_d.di_mode & S_IFMT) == S_IFDIR);

	if ((retval = xfs_dir_ino_validate(trans->t_mountp, inum)))
		return (retval);

	XFS_STATS_INC(xs_dir_create);
	/*
	 * Fill in the arg structure for this request.
	 */
	args.name = name;
	args.namelen = namelen;
	args.hashval = xfs_da_hashname(name, namelen);
	args.inumber = inum;
	args.dp = dp;
	args.firstblock = firstblock;
	args.flist = flist;
	args.total = total;
	args.whichfork = XFS_DATA_FORK;
	args.trans = trans;
	args.justcheck = 0;
	args.addname = args.oknoent = 1;

	/*
	 * Decide on what work routines to call based on the inode size.
	 */
	done = 0;
	if (dp->i_d.di_format == XFS_DINODE_FMT_LOCAL) {
		newsize = XFS_DIR_SF_ENTSIZE_BYNAME(args.namelen);
		if ((dp->i_d.di_size + newsize) <= XFS_IFORK_DSIZE(dp)) {
			retval = xfs_dir_shortform_addname(&args);
			done = 1;
		} else {
			if (total == 0)
				return XFS_ERROR(ENOSPC);
			retval = xfs_dir_shortform_to_leaf(&args);
			done = retval != 0;
		}
	}
	if (!done && xfs_bmap_one_block(dp, XFS_DATA_FORK)) {
		retval = xfs_dir_leaf_addname(&args);
		done = retval != ENOSPC;
		if (!done) {
			if (total == 0)
				return XFS_ERROR(ENOSPC);
			retval = xfs_dir_leaf_to_node(&args);
			done = retval != 0;
		}
	}
	if (!done) {
		retval = xfs_dir_node_addname(&args);
	}
	return(retval);
}

/*
 * Generic handler routine to check if a name can be added to a directory,
 * without adding any blocks to the directory.
 */
static int							/* error */
xfs_dir_canenter(xfs_trans_t *trans, xfs_inode_t *dp, char *name, int namelen)
{
	xfs_da_args_t args;
	int retval, newsize;

	ASSERT((dp->i_d.di_mode & S_IFMT) == S_IFDIR);
	/*
	 * Fill in the arg structure for this request.
	 */
	args.name = name;
	args.namelen = namelen;
	args.hashval = xfs_da_hashname(name, namelen);
	args.inumber = 0;
	args.dp = dp;
	args.firstblock = NULL;
	args.flist = NULL;
	args.total = 0;
	args.whichfork = XFS_DATA_FORK;
	args.trans = trans;
	args.justcheck = args.addname = args.oknoent = 1;

	/*
	 * Decide on what work routines to call based on the inode size.
	 */
	if (dp->i_d.di_format == XFS_DINODE_FMT_LOCAL) {
		newsize = XFS_DIR_SF_ENTSIZE_BYNAME(args.namelen);
		if ((dp->i_d.di_size + newsize) <= XFS_IFORK_DSIZE(dp))
			retval = 0;
		else
			retval = XFS_ERROR(ENOSPC);
	} else if (xfs_bmap_one_block(dp, XFS_DATA_FORK)) {
		retval = xfs_dir_leaf_addname(&args);
	} else {
		retval = xfs_dir_node_addname(&args);
	}
	return(retval);
}

/*
 * Generic handler routine to remove a name from a directory.
 * Transitions directory from Btree to shortform as necessary.
 */
static int							/* error */
xfs_dir_removename(xfs_trans_t *trans, xfs_inode_t *dp, char *name,
		   int namelen, xfs_ino_t ino, xfs_fsblock_t *firstblock,
		   xfs_bmap_free_t *flist, xfs_extlen_t total)
{
	xfs_da_args_t args;
	int count, totallen, newsize, retval;

	ASSERT((dp->i_d.di_mode & S_IFMT) == S_IFDIR);
	XFS_STATS_INC(xs_dir_remove);
	/*
	 * Fill in the arg structure for this request.
	 */
	args.name = name;
	args.namelen = namelen;
	args.hashval = xfs_da_hashname(name, namelen);
	args.inumber = ino;
	args.dp = dp;
	args.firstblock = firstblock;
	args.flist = flist;
	args.total = total;
	args.whichfork = XFS_DATA_FORK;
	args.trans = trans;
	args.justcheck = args.addname = args.oknoent = 0;

	/*
	 * Decide on what work routines to call based on the inode size.
	 */
	if (dp->i_d.di_format == XFS_DINODE_FMT_LOCAL) {
		retval = xfs_dir_shortform_removename(&args);
	} else if (xfs_bmap_one_block(dp, XFS_DATA_FORK)) {
		retval = xfs_dir_leaf_removename(&args, &count, &totallen);
		if (retval == 0) {
			newsize = XFS_DIR_SF_ALLFIT(count, totallen);
			if (newsize <= XFS_IFORK_DSIZE(dp)) {
				retval = xfs_dir_leaf_to_shortform(&args);
			}
		}
	} else {
		retval = xfs_dir_node_removename(&args);
	}
	return(retval);
}

static int							/* error */
xfs_dir_lookup(xfs_trans_t *trans, xfs_inode_t *dp, char *name, int namelen,
				   xfs_ino_t *inum)
{
	xfs_da_args_t args;
	int retval;

	ASSERT((dp->i_d.di_mode & S_IFMT) == S_IFDIR);

	XFS_STATS_INC(xs_dir_lookup);
	/*
	 * Fill in the arg structure for this request.
	 */
	args.name = name;
	args.namelen = namelen;
	args.hashval = xfs_da_hashname(name, namelen);
	args.inumber = 0;
	args.dp = dp;
	args.firstblock = NULL;
	args.flist = NULL;
	args.total = 0;
	args.whichfork = XFS_DATA_FORK;
	args.trans = trans;
	args.justcheck = args.addname = 0;
	args.oknoent = 1;

	/*
	 * Decide on what work routines to call based on the inode size.
	 */
	if (dp->i_d.di_format == XFS_DINODE_FMT_LOCAL) {
		retval = xfs_dir_shortform_lookup(&args);
	} else if (xfs_bmap_one_block(dp, XFS_DATA_FORK)) {
		retval = xfs_dir_leaf_lookup(&args);
	} else {
		retval = xfs_dir_node_lookup(&args);
	}
	if (retval == EEXIST)
		retval = 0;
	*inum = args.inumber;
	return(retval);
}

/*
 * Implement readdir.
 */
static int							/* error */
xfs_dir_getdents(xfs_trans_t *trans, xfs_inode_t *dp, uio_t *uio, int *eofp)
{
	xfs_dirent_t *dbp;
	int  alignment, retval;
	xfs_dir_put_t put;

	XFS_STATS_INC(xs_dir_getdents);
	ASSERT((dp->i_d.di_mode & S_IFMT) == S_IFDIR);

	/*
	 * If our caller has given us a single contiguous memory buffer,
	 * just work directly within that buffer.  If it's in user memory,
	 * lock it down first.
	 */
	alignment = sizeof(xfs_off_t) - 1;
	if ((uio->uio_iovcnt == 1) &&
	    (((__psint_t)uio->uio_iov[0].iov_base & alignment) == 0) &&
	    ((uio->uio_iov[0].iov_len & alignment) == 0)) {
		dbp = NULL;
		put = xfs_dir_put_dirent64_direct;
	} else {
		dbp = kmem_alloc(sizeof(*dbp) + MAXNAMELEN, KM_SLEEP);
		put = xfs_dir_put_dirent64_uio;
	}

	/*
	 * Decide on what work routines to call based on the inode size.
	 */
	*eofp = 0;

	if (dp->i_d.di_format == XFS_DINODE_FMT_LOCAL) {
		retval = xfs_dir_shortform_getdents(dp, uio, eofp, dbp, put);
	} else if (xfs_bmap_one_block(dp, XFS_DATA_FORK)) {
		retval = xfs_dir_leaf_getdents(trans, dp, uio, eofp, dbp, put);
	} else {
		retval = xfs_dir_node_getdents(trans, dp, uio, eofp, dbp, put);
	}
	if (dbp != NULL)
		kmem_free(dbp, sizeof(*dbp) + MAXNAMELEN);

	return(retval);
}

static int							/* error */
xfs_dir_replace(xfs_trans_t *trans, xfs_inode_t *dp, char *name, int namelen,
				    xfs_ino_t inum, xfs_fsblock_t *firstblock,
				    xfs_bmap_free_t *flist, xfs_extlen_t total)
{
	xfs_da_args_t args;
	int retval;

	ASSERT((dp->i_d.di_mode & S_IFMT) == S_IFDIR);

	if ((retval = xfs_dir_ino_validate(trans->t_mountp, inum)))
		return retval;

	/*
	 * Fill in the arg structure for this request.
	 */
	args.name = name;
	args.namelen = namelen;
	args.hashval = xfs_da_hashname(name, namelen);
	args.inumber = inum;
	args.dp = dp;
	args.firstblock = firstblock;
	args.flist = flist;
	args.total = total;
	args.whichfork = XFS_DATA_FORK;
	args.trans = trans;
	args.justcheck = args.addname = args.oknoent = 0;

	/*
	 * Decide on what work routines to call based on the inode size.
	 */
	if (dp->i_d.di_format == XFS_DINODE_FMT_LOCAL) {
		retval = xfs_dir_shortform_replace(&args);
	} else if (xfs_bmap_one_block(dp, XFS_DATA_FORK)) {
		retval = xfs_dir_leaf_replace(&args);
	} else {
		retval = xfs_dir_node_replace(&args);
	}

	return(retval);
}

static int
xfs_dir_shortform_validate_ondisk(xfs_mount_t *mp, xfs_dinode_t *dp)
{
	xfs_ino_t		ino;
	int			namelen_sum;
	int			count;
	xfs_dir_shortform_t	*sf;
	xfs_dir_sf_entry_t	*sfe;
	int			i;



	if ((INT_GET(dp->di_core.di_mode, ARCH_CONVERT) & S_IFMT) != S_IFDIR) {
		return 0;
	}
	if (INT_GET(dp->di_core.di_format, ARCH_CONVERT) != XFS_DINODE_FMT_LOCAL) {
		return 0;
	}
	if (INT_GET(dp->di_core.di_size, ARCH_CONVERT) < sizeof(sf->hdr)) {
		xfs_fs_cmn_err(CE_WARN, mp, "Invalid shortform size: dp 0x%p",
			dp);
		return 1;
	}
	sf = (xfs_dir_shortform_t *)(&dp->di_u.di_dirsf);
	ino = XFS_GET_DIR_INO8(sf->hdr.parent);
	if (xfs_dir_ino_validate(mp, ino))
		return 1;

	count =	sf->hdr.count;
	if ((count < 0) || ((count * 10) > XFS_LITINO(mp))) {
		xfs_fs_cmn_err(CE_WARN, mp,
			"Invalid shortform count: dp 0x%p", dp);
		return(1);
	}

	if (count == 0) {
		return 0;
	}

	namelen_sum = 0;
	sfe = &sf->list[0];
	for (i = sf->hdr.count - 1; i >= 0; i--) {
		ino = XFS_GET_DIR_INO8(sfe->inumber);
		xfs_dir_ino_validate(mp, ino);
		if (sfe->namelen >= XFS_LITINO(mp)) {
			xfs_fs_cmn_err(CE_WARN, mp,
				"Invalid shortform namelen: dp 0x%p", dp);
			return 1;
		}
		namelen_sum += sfe->namelen;
		sfe = XFS_DIR_SF_NEXTENTRY(sfe);
	}
	if (namelen_sum >= XFS_LITINO(mp)) {
		xfs_fs_cmn_err(CE_WARN, mp,
			"Invalid shortform namelen: dp 0x%p", dp);
		return 1;
	}

	return 0;
}

/*========================================================================
 * External routines when dirsize == XFS_LBSIZE(dp->i_mount).
 *========================================================================*/

/*
 * Add a name to the leaf directory structure
 * This is the external routine.
 */
int
xfs_dir_leaf_addname(xfs_da_args_t *args)
{
	int index, retval;
	xfs_dabuf_t *bp;

	retval = xfs_da_read_buf(args->trans, args->dp, 0, -1, &bp,
					      XFS_DATA_FORK);
	if (retval)
		return(retval);
	ASSERT(bp != NULL);

	retval = xfs_dir_leaf_lookup_int(bp, args, &index);
	if (retval == ENOENT)
		retval = xfs_dir_leaf_add(bp, args, index);
	xfs_da_buf_done(bp);
	return(retval);
}

/*
 * Remove a name from the leaf directory structure
 * This is the external routine.
 */
STATIC int
xfs_dir_leaf_removename(xfs_da_args_t *args, int *count, int *totallen)
{
	xfs_dir_leafblock_t *leaf;
	int index, retval;
	xfs_dabuf_t *bp;

	retval = xfs_da_read_buf(args->trans, args->dp, 0, -1, &bp,
					      XFS_DATA_FORK);
	if (retval)
		return(retval);
	ASSERT(bp != NULL);
	leaf = bp->data;
	ASSERT(INT_GET(leaf->hdr.info.magic, ARCH_CONVERT) == XFS_DIR_LEAF_MAGIC);
	retval = xfs_dir_leaf_lookup_int(bp, args, &index);
	if (retval == EEXIST) {
		(void)xfs_dir_leaf_remove(args->trans, bp, index);
		*count = INT_GET(leaf->hdr.count, ARCH_CONVERT);
		*totallen = INT_GET(leaf->hdr.namebytes, ARCH_CONVERT);
		retval = 0;
	}
	xfs_da_buf_done(bp);
	return(retval);
}

/*
 * Look up a name in a leaf directory structure.
 * This is the external routine.
 */
STATIC int
xfs_dir_leaf_lookup(xfs_da_args_t *args)
{
	int index, retval;
	xfs_dabuf_t *bp;

	retval = xfs_da_read_buf(args->trans, args->dp, 0, -1, &bp,
					      XFS_DATA_FORK);
	if (retval)
		return(retval);
	ASSERT(bp != NULL);
	retval = xfs_dir_leaf_lookup_int(bp, args, &index);
	xfs_da_brelse(args->trans, bp);
	return(retval);
}

/*
 * Copy out directory entries for getdents(), for leaf directories.
 */
STATIC int
xfs_dir_leaf_getdents(xfs_trans_t *trans, xfs_inode_t *dp, uio_t *uio,
				  int *eofp, xfs_dirent_t *dbp, xfs_dir_put_t put)
{
	xfs_dabuf_t *bp;
	int retval, eob;

	retval = xfs_da_read_buf(dp->i_transp, dp, 0, -1, &bp, XFS_DATA_FORK);
	if (retval)
		return(retval);
	ASSERT(bp != NULL);
	retval = xfs_dir_leaf_getdents_int(bp, dp, 0, uio, &eob, dbp, put, -1);
	xfs_da_brelse(trans, bp);
	*eofp = (eob == 0);
	return(retval);
}

/*
 * Look up a name in a leaf directory structure, replace the inode number.
 * This is the external routine.
 */
STATIC int
xfs_dir_leaf_replace(xfs_da_args_t *args)
{
	int index, retval;
	xfs_dabuf_t *bp;
	xfs_ino_t inum;
	xfs_dir_leafblock_t *leaf;
	xfs_dir_leaf_entry_t *entry;
	xfs_dir_leaf_name_t *namest;

	inum = args->inumber;
	retval = xfs_da_read_buf(args->trans, args->dp, 0, -1, &bp,
					      XFS_DATA_FORK);
	if (retval)
		return(retval);
	ASSERT(bp != NULL);
	retval = xfs_dir_leaf_lookup_int(bp, args, &index);
	if (retval == EEXIST) {
		leaf = bp->data;
		entry = &leaf->entries[index];
		namest = XFS_DIR_LEAF_NAMESTRUCT(leaf, INT_GET(entry->nameidx, ARCH_CONVERT));
		/* XXX - replace assert? */
		XFS_DIR_SF_PUT_DIRINO(&inum, &namest->inumber);
		xfs_da_log_buf(args->trans, bp,
		    XFS_DA_LOGRANGE(leaf, namest, sizeof(namest->inumber)));
		xfs_da_buf_done(bp);
		retval = 0;
	} else
		xfs_da_brelse(args->trans, bp);
	return(retval);
}


/*========================================================================
 * External routines when dirsize > XFS_LBSIZE(mp).
 *========================================================================*/

/*
 * Add a name to a Btree-format directory.
 *
 * This will involve walking down the Btree, and may involve splitting
 * leaf nodes and even splitting intermediate nodes up to and including
 * the root node (a special case of an intermediate node).
 */
STATIC int
xfs_dir_node_addname(xfs_da_args_t *args)
{
	xfs_da_state_t *state;
	xfs_da_state_blk_t *blk;
	int retval, error;

	/*
	 * Fill in bucket of arguments/results/context to carry around.
	 */
	state = xfs_da_state_alloc();
	state->args = args;
	state->mp = args->dp->i_mount;
	state->blocksize = state->mp->m_sb.sb_blocksize;
	state->node_ents = state->mp->m_dir_node_ents;

	/*
	 * Search to see if name already exists, and get back a pointer
	 * to where it should go.
	 */
	error = xfs_da_node_lookup_int(state, &retval);
	if (error)
		retval = error;
	if (retval != ENOENT)
		goto error;
	blk = &state->path.blk[ state->path.active-1 ];
	ASSERT(blk->magic == XFS_DIR_LEAF_MAGIC);
	retval = xfs_dir_leaf_add(blk->bp, args, blk->index);
	if (retval == 0) {
		/*
		 * Addition succeeded, update Btree hashvals.
		 */
		if (!args->justcheck)
			xfs_da_fixhashpath(state, &state->path);
	} else {
		/*
		 * Addition failed, split as many Btree elements as required.
		 */
		if (args->total == 0) {
			ASSERT(retval == ENOSPC);
			goto error;
		}
		retval = xfs_da_split(state);
	}
error:
	xfs_da_state_free(state);

	return(retval);
}

/*
 * Remove a name from a B-tree directory.
 *
 * This will involve walking down the Btree, and may involve joining
 * leaf nodes and even joining intermediate nodes up to and including
 * the root node (a special case of an intermediate node).
 */
STATIC int
xfs_dir_node_removename(xfs_da_args_t *args)
{
	xfs_da_state_t *state;
	xfs_da_state_blk_t *blk;
	int retval, error;

	state = xfs_da_state_alloc();
	state->args = args;
	state->mp = args->dp->i_mount;
	state->blocksize = state->mp->m_sb.sb_blocksize;
	state->node_ents = state->mp->m_dir_node_ents;

	/*
	 * Search to see if name exists, and get back a pointer to it.
	 */
	error = xfs_da_node_lookup_int(state, &retval);
	if (error)
		retval = error;
	if (retval != EEXIST) {
		xfs_da_state_free(state);
		return(retval);
	}

	/*
	 * Remove the name and update the hashvals in the tree.
	 */
	blk = &state->path.blk[ state->path.active-1 ];
	ASSERT(blk->magic == XFS_DIR_LEAF_MAGIC);
	retval = xfs_dir_leaf_remove(args->trans, blk->bp, blk->index);
	xfs_da_fixhashpath(state, &state->path);

	/*
	 * Check to see if the tree needs to be collapsed.
	 */
	error = 0;
	if (retval) {
		error = xfs_da_join(state);
	}

	xfs_da_state_free(state);
	if (error)
		return(error);
	return(0);
}

/*
 * Look up a filename in a int directory.
 * Use an internal routine to actually do all the work.
 */
STATIC int
xfs_dir_node_lookup(xfs_da_args_t *args)
{
	xfs_da_state_t *state;
	int retval, error, i;

	state = xfs_da_state_alloc();
	state->args = args;
	state->mp = args->dp->i_mount;
	state->blocksize = state->mp->m_sb.sb_blocksize;
	state->node_ents = state->mp->m_dir_node_ents;

	/*
	 * Search to see if name exists,
	 * and get back a pointer to it.
	 */
	error = xfs_da_node_lookup_int(state, &retval);
	if (error) {
		retval = error;
	}

	/*
	 * If not in a transaction, we have to release all the buffers.
	 */
	for (i = 0; i < state->path.active; i++) {
		xfs_da_brelse(args->trans, state->path.blk[i].bp);
		state->path.blk[i].bp = NULL;
	}

	xfs_da_state_free(state);
	return(retval);
}

STATIC int
xfs_dir_node_getdents(xfs_trans_t *trans, xfs_inode_t *dp, uio_t *uio,
				  int *eofp, xfs_dirent_t *dbp, xfs_dir_put_t put)
{
	xfs_da_intnode_t *node;
	xfs_da_node_entry_t *btree;
	xfs_dir_leafblock_t *leaf = NULL;
	xfs_dablk_t bno, nextbno;
	xfs_dahash_t cookhash;
	xfs_mount_t *mp;
	int error, eob, i;
	xfs_dabuf_t *bp;
	xfs_daddr_t nextda;

	/*
	 * Pick up our context.
	 */
	mp = dp->i_mount;
	bp = NULL;
	bno = XFS_DA_COOKIE_BNO(mp, uio->uio_offset);
	cookhash = XFS_DA_COOKIE_HASH(mp, uio->uio_offset);

	xfs_dir_trace_g_du("node: start", dp, uio);

	/*
	 * Re-find our place, even if we're confused about what our place is.
	 *
	 * First we check the block number from the magic cookie, it is a
	 * cache of where we ended last time.  If we find a leaf block, and
	 * the starting hashval in that block is less than our desired
	 * hashval, then we run with it.
	 */
	if (bno > 0) {
		error = xfs_da_read_buf(trans, dp, bno, -2, &bp, XFS_DATA_FORK);
		if ((error != 0) && (error != EFSCORRUPTED))
			return(error);
		if (bp)
			leaf = bp->data;
		if (bp && INT_GET(leaf->hdr.info.magic, ARCH_CONVERT) != XFS_DIR_LEAF_MAGIC) {
			xfs_dir_trace_g_dub("node: block not a leaf",
						   dp, uio, bno);
			xfs_da_brelse(trans, bp);
			bp = NULL;
		}
		if (bp && INT_GET(leaf->entries[0].hashval, ARCH_CONVERT) > cookhash) {
			xfs_dir_trace_g_dub("node: leaf hash too large",
						   dp, uio, bno);
			xfs_da_brelse(trans, bp);
			bp = NULL;
		}
		if (bp &&
		    cookhash > INT_GET(leaf->entries[INT_GET(leaf->hdr.count, ARCH_CONVERT) - 1].hashval, ARCH_CONVERT)) {
			xfs_dir_trace_g_dub("node: leaf hash too small",
						   dp, uio, bno);
			xfs_da_brelse(trans, bp);
			bp = NULL;
		}
	}

	/*
	 * If we did not find a leaf block from the blockno in the cookie,
	 * or we there was no blockno in the cookie (eg: first time thru),
	 * the we start at the top of the Btree and re-find our hashval.
	 */
	if (bp == NULL) {
		xfs_dir_trace_g_du("node: start at root" , dp, uio);
		bno = 0;
		for (;;) {
			error = xfs_da_read_buf(trans, dp, bno, -1, &bp,
						       XFS_DATA_FORK);
			if (error)
				return(error);
			if (bp == NULL)
				return(XFS_ERROR(EFSCORRUPTED));
			node = bp->data;
			if (INT_GET(node->hdr.info.magic, ARCH_CONVERT) != XFS_DA_NODE_MAGIC)
				break;
			btree = &node->btree[0];
			xfs_dir_trace_g_dun("node: node detail", dp, uio, node);
			for (i = 0; i < INT_GET(node->hdr.count, ARCH_CONVERT); btree++, i++) {
				if (INT_GET(btree->hashval, ARCH_CONVERT) >= cookhash) {
					bno = INT_GET(btree->before, ARCH_CONVERT);
					break;
				}
			}
			if (i == INT_GET(node->hdr.count, ARCH_CONVERT)) {
				xfs_da_brelse(trans, bp);
				xfs_dir_trace_g_du("node: hash beyond EOF",
							  dp, uio);
				uio->uio_offset = XFS_DA_MAKE_COOKIE(mp, 0, 0,
							     XFS_DA_MAXHASH);
				*eofp = 1;
				return(0);
			}
			xfs_dir_trace_g_dub("node: going to block",
						   dp, uio, bno);
			xfs_da_brelse(trans, bp);
		}
	}
	ASSERT(cookhash != XFS_DA_MAXHASH);

	/*
	 * We've dropped down to the (first) leaf block that contains the
	 * hashval we are interested in.  Continue rolling upward thru the
	 * leaf blocks until we fill up our buffer.
	 */
	for (;;) {
		leaf = bp->data;
		if (unlikely(INT_GET(leaf->hdr.info.magic, ARCH_CONVERT) != XFS_DIR_LEAF_MAGIC)) {
			xfs_dir_trace_g_dul("node: not a leaf", dp, uio, leaf);
			xfs_da_brelse(trans, bp);
			XFS_CORRUPTION_ERROR("xfs_dir_node_getdents(1)",
					     XFS_ERRLEVEL_LOW, mp, leaf);
			return XFS_ERROR(EFSCORRUPTED);
		}
		xfs_dir_trace_g_dul("node: leaf detail", dp, uio, leaf);
		if ((nextbno = INT_GET(leaf->hdr.info.forw, ARCH_CONVERT))) {
			nextda = xfs_da_reada_buf(trans, dp, nextbno,
						  XFS_DATA_FORK);
		} else
			nextda = -1;
		error = xfs_dir_leaf_getdents_int(bp, dp, bno, uio, &eob, dbp,
						  put, nextda);
		xfs_da_brelse(trans, bp);
		bno = nextbno;
		if (eob) {
			xfs_dir_trace_g_dub("node: E-O-B", dp, uio, bno);
			*eofp = 0;
			return(error);
		}
		if (bno == 0)
			break;
		error = xfs_da_read_buf(trans, dp, bno, nextda, &bp,
					XFS_DATA_FORK);
		if (error)
			return(error);
		if (unlikely(bp == NULL)) {
			XFS_ERROR_REPORT("xfs_dir_node_getdents(2)",
					 XFS_ERRLEVEL_LOW, mp);
			return(XFS_ERROR(EFSCORRUPTED));
		}
	}
	*eofp = 1;
	xfs_dir_trace_g_du("node: E-O-F", dp, uio);
	return(0);
}

/*
 * Look up a filename in an int directory, replace the inode number.
 * Use an internal routine to actually do the lookup.
 */
STATIC int
xfs_dir_node_replace(xfs_da_args_t *args)
{
	xfs_da_state_t *state;
	xfs_da_state_blk_t *blk;
	xfs_dir_leafblock_t *leaf;
	xfs_dir_leaf_entry_t *entry;
	xfs_dir_leaf_name_t *namest;
	xfs_ino_t inum;
	int retval, error, i;
	xfs_dabuf_t *bp;

	state = xfs_da_state_alloc();
	state->args = args;
	state->mp = args->dp->i_mount;
	state->blocksize = state->mp->m_sb.sb_blocksize;
	state->node_ents = state->mp->m_dir_node_ents;
	inum = args->inumber;

	/*
	 * Search to see if name exists,
	 * and get back a pointer to it.
	 */
	error = xfs_da_node_lookup_int(state, &retval);
	if (error) {
		retval = error;
	}

	if (retval == EEXIST) {
		blk = &state->path.blk[state->path.active - 1];
		ASSERT(blk->magic == XFS_DIR_LEAF_MAGIC);
		bp = blk->bp;
		leaf = bp->data;
		entry = &leaf->entries[blk->index];
		namest = XFS_DIR_LEAF_NAMESTRUCT(leaf, INT_GET(entry->nameidx, ARCH_CONVERT));
		/* XXX - replace assert ? */
		XFS_DIR_SF_PUT_DIRINO(&inum, &namest->inumber);
		xfs_da_log_buf(args->trans, bp,
		    XFS_DA_LOGRANGE(leaf, namest, sizeof(namest->inumber)));
		xfs_da_buf_done(bp);
		blk->bp = NULL;
		retval = 0;
	} else {
		i = state->path.active - 1;
		xfs_da_brelse(args->trans, state->path.blk[i].bp);
		state->path.blk[i].bp = NULL;
	}
	for (i = 0; i < state->path.active - 1; i++) {
		xfs_da_brelse(args->trans, state->path.blk[i].bp);
		state->path.blk[i].bp = NULL;
	}

	xfs_da_state_free(state);
	return(retval);
}

#if defined(XFS_DIR_TRACE)
/*
 * Add a trace buffer entry for an inode and a uio.
 */
void
xfs_dir_trace_g_du(char *where, xfs_inode_t *dp, uio_t *uio)
{
	xfs_dir_trace_enter(XFS_DIR_KTRACE_G_DU, where,
		     (void *)dp, (void *)dp->i_mount,
		     (void *)((unsigned long)(uio->uio_offset >> 32)),
		     (void *)((unsigned long)(uio->uio_offset & 0xFFFFFFFF)),
		     (void *)(unsigned long)uio->uio_resid,
		     NULL, NULL, NULL, NULL, NULL, NULL, NULL);
}

/*
 * Add a trace buffer entry for an inode and a uio.
 */
void
xfs_dir_trace_g_dub(char *where, xfs_inode_t *dp, uio_t *uio, xfs_dablk_t bno)
{
	xfs_dir_trace_enter(XFS_DIR_KTRACE_G_DUB, where,
		     (void *)dp, (void *)dp->i_mount,
		     (void *)((unsigned long)(uio->uio_offset >> 32)),
		     (void *)((unsigned long)(uio->uio_offset & 0xFFFFFFFF)),
		     (void *)(unsigned long)uio->uio_resid,
		     (void *)(unsigned long)bno,
		     NULL, NULL, NULL, NULL, NULL, NULL);
}

/*
 * Add a trace buffer entry for an inode and a uio.
 */
void
xfs_dir_trace_g_dun(char *where, xfs_inode_t *dp, uio_t *uio,
			xfs_da_intnode_t *node)
{
	int	last = INT_GET(node->hdr.count, ARCH_CONVERT) - 1;

	xfs_dir_trace_enter(XFS_DIR_KTRACE_G_DUN, where,
		     (void *)dp, (void *)dp->i_mount,
		     (void *)((unsigned long)(uio->uio_offset >> 32)),
		     (void *)((unsigned long)(uio->uio_offset & 0xFFFFFFFF)),
		     (void *)(unsigned long)uio->uio_resid,
		     (void *)(unsigned long)
			INT_GET(node->hdr.info.forw, ARCH_CONVERT),
		     (void *)(unsigned long)
			INT_GET(node->hdr.count, ARCH_CONVERT),
		     (void *)(unsigned long)
			INT_GET(node->btree[0].hashval, ARCH_CONVERT),
		     (void *)(unsigned long)
			INT_GET(node->btree[last].hashval, ARCH_CONVERT),
		     NULL, NULL, NULL);
}

/*
 * Add a trace buffer entry for an inode and a uio.
 */
void
xfs_dir_trace_g_dul(char *where, xfs_inode_t *dp, uio_t *uio,
			xfs_dir_leafblock_t *leaf)
{
	int	last = INT_GET(leaf->hdr.count, ARCH_CONVERT) - 1;

	xfs_dir_trace_enter(XFS_DIR_KTRACE_G_DUL, where,
		     (void *)dp, (void *)dp->i_mount,
		     (void *)((unsigned long)(uio->uio_offset >> 32)),
		     (void *)((unsigned long)(uio->uio_offset & 0xFFFFFFFF)),
		     (void *)(unsigned long)uio->uio_resid,
		     (void *)(unsigned long)
			INT_GET(leaf->hdr.info.forw, ARCH_CONVERT),
		     (void *)(unsigned long)
			INT_GET(leaf->hdr.count, ARCH_CONVERT),
		     (void *)(unsigned long)
			INT_GET(leaf->entries[0].hashval, ARCH_CONVERT),
		     (void *)(unsigned long)
			INT_GET(leaf->entries[last].hashval, ARCH_CONVERT),
		     NULL, NULL, NULL);
}

/*
 * Add a trace buffer entry for an inode and a uio.
 */
void
xfs_dir_trace_g_due(char *where, xfs_inode_t *dp, uio_t *uio,
			xfs_dir_leaf_entry_t *entry)
{
	xfs_dir_trace_enter(XFS_DIR_KTRACE_G_DUE, where,
		     (void *)dp, (void *)dp->i_mount,
		     (void *)((unsigned long)(uio->uio_offset >> 32)),
		     (void *)((unsigned long)(uio->uio_offset & 0xFFFFFFFF)),
		     (void *)(unsigned long)uio->uio_resid,
		     (void *)(unsigned long)
			INT_GET(entry->hashval, ARCH_CONVERT),
		     NULL, NULL, NULL, NULL, NULL, NULL);
}

/*
 * Add a trace buffer entry for an inode and a uio.
 */
void
xfs_dir_trace_g_duc(char *where, xfs_inode_t *dp, uio_t *uio, xfs_off_t cookie)
{
	xfs_dir_trace_enter(XFS_DIR_KTRACE_G_DUC, where,
		     (void *)dp, (void *)dp->i_mount,
		     (void *)((unsigned long)(uio->uio_offset >> 32)),
		     (void *)((unsigned long)(uio->uio_offset & 0xFFFFFFFF)),
		     (void *)(unsigned long)uio->uio_resid,
		     (void *)((unsigned long)(cookie >> 32)),
		     (void *)((unsigned long)(cookie & 0xFFFFFFFF)),
		     NULL, NULL, NULL, NULL, NULL);
}

/*
 * Add a trace buffer entry for the arguments given to the routine,
 * generic form.
 */
void
xfs_dir_trace_enter(int type, char *where,
			void * a0, void * a1,
			void * a2, void * a3,
			void * a4, void * a5,
			void * a6, void * a7,
			void * a8, void * a9,
			void * a10, void * a11)
{
	ASSERT(xfs_dir_trace_buf);
	ktrace_enter(xfs_dir_trace_buf, (void *)(unsigned long)type,
					(void *)where,
					(void *)a0, (void *)a1, (void *)a2,
					(void *)a3, (void *)a4, (void *)a5,
					(void *)a6, (void *)a7, (void *)a8,
					(void *)a9, (void *)a10, (void *)a11,
					NULL, NULL);
}
#endif	/* XFS_DIR_TRACE */
