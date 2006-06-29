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
#include "xfs_bit.h"
#include "xfs_log.h"
#include "xfs_inum.h"
#include "xfs_trans.h"
#include "xfs_sb.h"
#include "xfs_ag.h"
#include "xfs_dir2.h"
#include "xfs_dmapi.h"
#include "xfs_mount.h"
#include "xfs_da_btree.h"
#include "xfs_bmap_btree.h"
#include "xfs_alloc_btree.h"
#include "xfs_dir2_sf.h"
#include "xfs_attr_sf.h"
#include "xfs_dinode.h"
#include "xfs_inode.h"
#include "xfs_inode_item.h"
#include "xfs_bmap.h"
#include "xfs_dir2_data.h"
#include "xfs_dir2_leaf.h"
#include "xfs_dir2_block.h"
#include "xfs_dir2_node.h"
#include "xfs_dir2_trace.h"
#include "xfs_error.h"

static int	xfs_dir2_put_dirent64_direct(xfs_dir2_put_args_t *pa);
static int	xfs_dir2_put_dirent64_uio(xfs_dir2_put_args_t *pa);

void
xfs_dir_mount(
	xfs_mount_t	*mp)
{
	ASSERT(XFS_SB_VERSION_HASDIRV2(&mp->m_sb));
	ASSERT((1 << (mp->m_sb.sb_blocklog + mp->m_sb.sb_dirblklog)) <=
	       XFS_MAX_BLOCKSIZE);
	mp->m_dirblksize = 1 << (mp->m_sb.sb_blocklog + mp->m_sb.sb_dirblklog);
	mp->m_dirblkfsbs = 1 << mp->m_sb.sb_dirblklog;
	mp->m_dirdatablk = XFS_DIR2_DB_TO_DA(mp, XFS_DIR2_DATA_FIRSTDB(mp));
	mp->m_dirleafblk = XFS_DIR2_DB_TO_DA(mp, XFS_DIR2_LEAF_FIRSTDB(mp));
	mp->m_dirfreeblk = XFS_DIR2_DB_TO_DA(mp, XFS_DIR2_FREE_FIRSTDB(mp));
	mp->m_attr_node_ents =
		(mp->m_sb.sb_blocksize - (uint)sizeof(xfs_da_node_hdr_t)) /
		(uint)sizeof(xfs_da_node_entry_t);
	mp->m_dir_node_ents =
		(mp->m_dirblksize - (uint)sizeof(xfs_da_node_hdr_t)) /
		(uint)sizeof(xfs_da_node_entry_t);
	mp->m_dir_magicpct = (mp->m_dirblksize * 37) / 100;
}

/*
 * Return 1 if directory contains only "." and "..".
 */
int
xfs_dir_isempty(
	xfs_inode_t	*dp)
{
	xfs_dir2_sf_t	*sfp;

	ASSERT((dp->i_d.di_mode & S_IFMT) == S_IFDIR);
	if (dp->i_d.di_size == 0)	/* might happen during shutdown. */
		return 1;
	if (dp->i_d.di_size > XFS_IFORK_DSIZE(dp))
		return 0;
	sfp = (xfs_dir2_sf_t *)dp->i_df.if_u1.if_data;
	return !sfp->hdr.count;
}

/*
 * Validate a given inode number.
 */
int
xfs_dir_ino_validate(
	xfs_mount_t	*mp,
	xfs_ino_t	ino)
{
	xfs_agblock_t	agblkno;
	xfs_agino_t	agino;
	xfs_agnumber_t	agno;
	int		ino_ok;
	int		ioff;

	agno = XFS_INO_TO_AGNO(mp, ino);
	agblkno = XFS_INO_TO_AGBNO(mp, ino);
	ioff = XFS_INO_TO_OFFSET(mp, ino);
	agino = XFS_OFFBNO_TO_AGINO(mp, agblkno, ioff);
	ino_ok =
		agno < mp->m_sb.sb_agcount &&
		agblkno < mp->m_sb.sb_agblocks &&
		agblkno != 0 &&
		ioff < (1 << mp->m_sb.sb_inopblog) &&
		XFS_AGINO_TO_INO(mp, agno, agino) == ino;
	if (unlikely(XFS_TEST_ERROR(!ino_ok, mp, XFS_ERRTAG_DIR_INO_VALIDATE,
			XFS_RANDOM_DIR_INO_VALIDATE))) {
		xfs_fs_cmn_err(CE_WARN, mp, "Invalid inode number 0x%Lx",
				(unsigned long long) ino);
		XFS_ERROR_REPORT("xfs_dir_ino_validate", XFS_ERRLEVEL_LOW, mp);
		return XFS_ERROR(EFSCORRUPTED);
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
	xfs_da_args_t	args;
	int		error;

	memset((char *)&args, 0, sizeof(args));
	args.dp = dp;
	args.trans = tp;
	ASSERT((dp->i_d.di_mode & S_IFMT) == S_IFDIR);
	if ((error = xfs_dir_ino_validate(tp->t_mountp, pdp->i_ino)))
		return error;
	return xfs_dir2_sf_create(&args, pdp->i_ino);
}

/*
  Enter a name in a directory.
 */
int
xfs_dir_createname(
	xfs_trans_t		*tp,
	xfs_inode_t		*dp,
	char			*name,
	int			namelen,
	xfs_ino_t		inum,		/* new entry inode number */
	xfs_fsblock_t		*first,		/* bmap's firstblock */
	xfs_bmap_free_t		*flist,		/* bmap's freeblock list */
	xfs_extlen_t		total)		/* bmap's total block count */
{
	xfs_da_args_t		args;
	int			rval;
	int			v;		/* type-checking value */

	ASSERT((dp->i_d.di_mode & S_IFMT) == S_IFDIR);
	if ((rval = xfs_dir_ino_validate(tp->t_mountp, inum)))
		return rval;
	XFS_STATS_INC(xs_dir_create);

	args.name = name;
	args.namelen = namelen;
	args.hashval = xfs_da_hashname(name, namelen);
	args.inumber = inum;
	args.dp = dp;
	args.firstblock = first;
	args.flist = flist;
	args.total = total;
	args.whichfork = XFS_DATA_FORK;
	args.trans = tp;
	args.justcheck = 0;
	args.addname = args.oknoent = 1;

	if (dp->i_d.di_format == XFS_DINODE_FMT_LOCAL)
		rval = xfs_dir2_sf_addname(&args);
	else if ((rval = xfs_dir2_isblock(tp, dp, &v)))
		return rval;
	else if (v)
		rval = xfs_dir2_block_addname(&args);
	else if ((rval = xfs_dir2_isleaf(tp, dp, &v)))
		return rval;
	else if (v)
		rval = xfs_dir2_leaf_addname(&args);
	else
		rval = xfs_dir2_node_addname(&args);
	return rval;
}

/*
 * Lookup a name in a directory, give back the inode number.
 */
int
xfs_dir_lookup(
	xfs_trans_t	*tp,
	xfs_inode_t	*dp,
	char		*name,
	int		namelen,
	xfs_ino_t	*inum)		/* out: inode number */
{
	xfs_da_args_t	args;
	int		rval;
	int		v;		/* type-checking value */

	ASSERT((dp->i_d.di_mode & S_IFMT) == S_IFDIR);
	XFS_STATS_INC(xs_dir_lookup);

	args.name = name;
	args.namelen = namelen;
	args.hashval = xfs_da_hashname(name, namelen);
	args.inumber = 0;
	args.dp = dp;
	args.firstblock = NULL;
	args.flist = NULL;
	args.total = 0;
	args.whichfork = XFS_DATA_FORK;
	args.trans = tp;
	args.justcheck = args.addname = 0;
	args.oknoent = 1;

	if (dp->i_d.di_format == XFS_DINODE_FMT_LOCAL)
		rval = xfs_dir2_sf_lookup(&args);
	else if ((rval = xfs_dir2_isblock(tp, dp, &v)))
		return rval;
	else if (v)
		rval = xfs_dir2_block_lookup(&args);
	else if ((rval = xfs_dir2_isleaf(tp, dp, &v)))
		return rval;
	else if (v)
		rval = xfs_dir2_leaf_lookup(&args);
	else
		rval = xfs_dir2_node_lookup(&args);
	if (rval == EEXIST)
		rval = 0;
	if (rval == 0)
		*inum = args.inumber;
	return rval;
}

/*
 * Remove an entry from a directory.
 */
int
xfs_dir_removename(
	xfs_trans_t	*tp,
	xfs_inode_t	*dp,
	char		*name,
	int		namelen,
	xfs_ino_t	ino,
	xfs_fsblock_t	*first,		/* bmap's firstblock */
	xfs_bmap_free_t	*flist,		/* bmap's freeblock list */
	xfs_extlen_t	total)		/* bmap's total block count */
{
	xfs_da_args_t	args;
	int		rval;
	int		v;		/* type-checking value */

	ASSERT((dp->i_d.di_mode & S_IFMT) == S_IFDIR);
	XFS_STATS_INC(xs_dir_remove);

	args.name = name;
	args.namelen = namelen;
	args.hashval = xfs_da_hashname(name, namelen);
	args.inumber = ino;
	args.dp = dp;
	args.firstblock = first;
	args.flist = flist;
	args.total = total;
	args.whichfork = XFS_DATA_FORK;
	args.trans = tp;
	args.justcheck = args.addname = args.oknoent = 0;

	if (dp->i_d.di_format == XFS_DINODE_FMT_LOCAL)
		rval = xfs_dir2_sf_removename(&args);
	else if ((rval = xfs_dir2_isblock(tp, dp, &v)))
		return rval;
	else if (v)
		rval = xfs_dir2_block_removename(&args);
	else if ((rval = xfs_dir2_isleaf(tp, dp, &v)))
		return rval;
	else if (v)
		rval = xfs_dir2_leaf_removename(&args);
	else
		rval = xfs_dir2_node_removename(&args);
	return rval;
}

/*
 * Read a directory.
 */
int
xfs_dir_getdents(
	xfs_trans_t	*tp,
	xfs_inode_t	*dp,
	uio_t		*uio,		/* caller's buffer control */
	int		*eofp)		/* out: eof reached */
{
	int		alignment;	/* alignment required for ABI */
	xfs_dirent_t	*dbp;		/* malloc'ed buffer */
	xfs_dir2_put_t	put;		/* entry formatting routine */
	int		rval;		/* return value */
	int		v;		/* type-checking value */

	ASSERT((dp->i_d.di_mode & S_IFMT) == S_IFDIR);
	XFS_STATS_INC(xs_dir_getdents);
	/*
	 * If our caller has given us a single contiguous aligned memory buffer,
	 * just work directly within that buffer.  If it's in user memory,
	 * lock it down first.
	 */
	alignment = sizeof(xfs_off_t) - 1;
	if ((uio->uio_iovcnt == 1) &&
	    (((__psint_t)uio->uio_iov[0].iov_base & alignment) == 0) &&
	    ((uio->uio_iov[0].iov_len & alignment) == 0)) {
		dbp = NULL;
		put = xfs_dir2_put_dirent64_direct;
	} else {
		dbp = kmem_alloc(sizeof(*dbp) + MAXNAMELEN, KM_SLEEP);
		put = xfs_dir2_put_dirent64_uio;
	}

	*eofp = 0;
	if (dp->i_d.di_format == XFS_DINODE_FMT_LOCAL)
		rval = xfs_dir2_sf_getdents(dp, uio, eofp, dbp, put);
	else if ((rval = xfs_dir2_isblock(tp, dp, &v)))
		;
	else if (v)
		rval = xfs_dir2_block_getdents(tp, dp, uio, eofp, dbp, put);
	else
		rval = xfs_dir2_leaf_getdents(tp, dp, uio, eofp, dbp, put);
	if (dbp != NULL)
		kmem_free(dbp, sizeof(*dbp) + MAXNAMELEN);
	return rval;
}

/*
 * Replace the inode number of a directory entry.
 */
int
xfs_dir_replace(
	xfs_trans_t	*tp,
	xfs_inode_t	*dp,
	char		*name,		/* name of entry to replace */
	int		namelen,
	xfs_ino_t	inum,		/* new inode number */
	xfs_fsblock_t	*first,		/* bmap's firstblock */
	xfs_bmap_free_t	*flist,		/* bmap's freeblock list */
	xfs_extlen_t	total)		/* bmap's total block count */
{
	xfs_da_args_t	args;
	int		rval;
	int		v;		/* type-checking value */

	ASSERT((dp->i_d.di_mode & S_IFMT) == S_IFDIR);

	if ((rval = xfs_dir_ino_validate(tp->t_mountp, inum)))
		return rval;

	args.name = name;
	args.namelen = namelen;
	args.hashval = xfs_da_hashname(name, namelen);
	args.inumber = inum;
	args.dp = dp;
	args.firstblock = first;
	args.flist = flist;
	args.total = total;
	args.whichfork = XFS_DATA_FORK;
	args.trans = tp;
	args.justcheck = args.addname = args.oknoent = 0;

	if (dp->i_d.di_format == XFS_DINODE_FMT_LOCAL)
		rval = xfs_dir2_sf_replace(&args);
	else if ((rval = xfs_dir2_isblock(tp, dp, &v)))
		return rval;
	else if (v)
		rval = xfs_dir2_block_replace(&args);
	else if ((rval = xfs_dir2_isleaf(tp, dp, &v)))
		return rval;
	else if (v)
		rval = xfs_dir2_leaf_replace(&args);
	else
		rval = xfs_dir2_node_replace(&args);
	return rval;
}

/*
 * See if this entry can be added to the directory without allocating space.
 */
int
xfs_dir_canenter(
	xfs_trans_t	*tp,
	xfs_inode_t	*dp,
	char		*name,		/* name of entry to add */
	int		namelen)
{
	xfs_da_args_t	args;
	int		rval;
	int		v;		/* type-checking value */

	ASSERT((dp->i_d.di_mode & S_IFMT) == S_IFDIR);

	args.name = name;
	args.namelen = namelen;
	args.hashval = xfs_da_hashname(name, namelen);
	args.inumber = 0;
	args.dp = dp;
	args.firstblock = NULL;
	args.flist = NULL;
	args.total = 0;
	args.whichfork = XFS_DATA_FORK;
	args.trans = tp;
	args.justcheck = args.addname = args.oknoent = 1;

	if (dp->i_d.di_format == XFS_DINODE_FMT_LOCAL)
		rval = xfs_dir2_sf_addname(&args);
	else if ((rval = xfs_dir2_isblock(tp, dp, &v)))
		return rval;
	else if (v)
		rval = xfs_dir2_block_addname(&args);
	else if ((rval = xfs_dir2_isleaf(tp, dp, &v)))
		return rval;
	else if (v)
		rval = xfs_dir2_leaf_addname(&args);
	else
		rval = xfs_dir2_node_addname(&args);
	return rval;
}

/*
 * Utility routines.
 */

/*
 * Add a block to the directory.
 * This routine is for data and free blocks, not leaf/node blocks
 * which are handled by xfs_da_grow_inode.
 */
int
xfs_dir2_grow_inode(
	xfs_da_args_t	*args,
	int		space,		/* v2 dir's space XFS_DIR2_xxx_SPACE */
	xfs_dir2_db_t	*dbp)		/* out: block number added */
{
	xfs_fileoff_t	bno;		/* directory offset of new block */
	int		count;		/* count of filesystem blocks */
	xfs_inode_t	*dp;		/* incore directory inode */
	int		error;
	int		got;		/* blocks actually mapped */
	int		i;
	xfs_bmbt_irec_t	map;		/* single structure for bmap */
	int		mapi;		/* mapping index */
	xfs_bmbt_irec_t	*mapp;		/* bmap mapping structure(s) */
	xfs_mount_t	*mp;
	int		nmap;		/* number of bmap entries */
	xfs_trans_t	*tp;

	xfs_dir2_trace_args_s("grow_inode", args, space);
	dp = args->dp;
	tp = args->trans;
	mp = dp->i_mount;
	/*
	 * Set lowest possible block in the space requested.
	 */
	bno = XFS_B_TO_FSBT(mp, space * XFS_DIR2_SPACE_SIZE);
	count = mp->m_dirblkfsbs;
	/*
	 * Find the first hole for our block.
	 */
	if ((error = xfs_bmap_first_unused(tp, dp, count, &bno, XFS_DATA_FORK)))
		return error;
	nmap = 1;
	ASSERT(args->firstblock != NULL);
	/*
	 * Try mapping the new block contiguously (one extent).
	 */
	if ((error = xfs_bmapi(tp, dp, bno, count,
			XFS_BMAPI_WRITE|XFS_BMAPI_METADATA|XFS_BMAPI_CONTIG,
			args->firstblock, args->total, &map, &nmap,
			args->flist, NULL)))
		return error;
	ASSERT(nmap <= 1);
	if (nmap == 1) {
		mapp = &map;
		mapi = 1;
	}
	/*
	 * Didn't work and this is a multiple-fsb directory block.
	 * Try again with contiguous flag turned on.
	 */
	else if (nmap == 0 && count > 1) {
		xfs_fileoff_t	b;	/* current file offset */

		/*
		 * Space for maximum number of mappings.
		 */
		mapp = kmem_alloc(sizeof(*mapp) * count, KM_SLEEP);
		/*
		 * Iterate until we get to the end of our block.
		 */
		for (b = bno, mapi = 0; b < bno + count; ) {
			int	c;	/* current fsb count */

			/*
			 * Can't map more than MAX_NMAP at once.
			 */
			nmap = MIN(XFS_BMAP_MAX_NMAP, count);
			c = (int)(bno + count - b);
			if ((error = xfs_bmapi(tp, dp, b, c,
					XFS_BMAPI_WRITE|XFS_BMAPI_METADATA,
					args->firstblock, args->total,
					&mapp[mapi], &nmap, args->flist,
					NULL))) {
				kmem_free(mapp, sizeof(*mapp) * count);
				return error;
			}
			if (nmap < 1)
				break;
			/*
			 * Add this bunch into our table, go to the next offset.
			 */
			mapi += nmap;
			b = mapp[mapi - 1].br_startoff +
			    mapp[mapi - 1].br_blockcount;
		}
	}
	/*
	 * Didn't work.
	 */
	else {
		mapi = 0;
		mapp = NULL;
	}
	/*
	 * See how many fsb's we got.
	 */
	for (i = 0, got = 0; i < mapi; i++)
		got += mapp[i].br_blockcount;
	/*
	 * Didn't get enough fsb's, or the first/last block's are wrong.
	 */
	if (got != count || mapp[0].br_startoff != bno ||
	    mapp[mapi - 1].br_startoff + mapp[mapi - 1].br_blockcount !=
	    bno + count) {
		if (mapp != &map)
			kmem_free(mapp, sizeof(*mapp) * count);
		return XFS_ERROR(ENOSPC);
	}
	/*
	 * Done with the temporary mapping table.
	 */
	if (mapp != &map)
		kmem_free(mapp, sizeof(*mapp) * count);
	*dbp = XFS_DIR2_DA_TO_DB(mp, (xfs_dablk_t)bno);
	/*
	 * Update file's size if this is the data space and it grew.
	 */
	if (space == XFS_DIR2_DATA_SPACE) {
		xfs_fsize_t	size;		/* directory file (data) size */

		size = XFS_FSB_TO_B(mp, bno + count);
		if (size > dp->i_d.di_size) {
			dp->i_d.di_size = size;
			xfs_trans_log_inode(tp, dp, XFS_ILOG_CORE);
		}
	}
	return 0;
}

/*
 * See if the directory is a single-block form directory.
 */
int
xfs_dir2_isblock(
	xfs_trans_t	*tp,
	xfs_inode_t	*dp,
	int		*vp)		/* out: 1 is block, 0 is not block */
{
	xfs_fileoff_t	last;		/* last file offset */
	xfs_mount_t	*mp;
	int		rval;

	mp = dp->i_mount;
	if ((rval = xfs_bmap_last_offset(tp, dp, &last, XFS_DATA_FORK)))
		return rval;
	rval = XFS_FSB_TO_B(mp, last) == mp->m_dirblksize;
	ASSERT(rval == 0 || dp->i_d.di_size == mp->m_dirblksize);
	*vp = rval;
	return 0;
}

/*
 * See if the directory is a single-leaf form directory.
 */
int
xfs_dir2_isleaf(
	xfs_trans_t	*tp,
	xfs_inode_t	*dp,
	int		*vp)		/* out: 1 is leaf, 0 is not leaf */
{
	xfs_fileoff_t	last;		/* last file offset */
	xfs_mount_t	*mp;
	int		rval;

	mp = dp->i_mount;
	if ((rval = xfs_bmap_last_offset(tp, dp, &last, XFS_DATA_FORK)))
		return rval;
	*vp = last == mp->m_dirleafblk + (1 << mp->m_sb.sb_dirblklog);
	return 0;
}

/*
 * Getdents put routine for 64-bit ABI, direct form.
 */
static int
xfs_dir2_put_dirent64_direct(
	xfs_dir2_put_args_t	*pa)
{
	xfs_dirent_t		*idbp;		/* dirent pointer */
	iovec_t			*iovp;		/* io vector */
	int			namelen;	/* entry name length */
	int			reclen;		/* entry total length */
	uio_t			*uio;		/* I/O control */

	namelen = pa->namelen;
	reclen = DIRENTSIZE(namelen);
	uio = pa->uio;
	/*
	 * Won't fit in the remaining space.
	 */
	if (reclen > uio->uio_resid) {
		pa->done = 0;
		return 0;
	}
	iovp = uio->uio_iov;
	idbp = (xfs_dirent_t *)iovp->iov_base;
	iovp->iov_base = (char *)idbp + reclen;
	iovp->iov_len -= reclen;
	uio->uio_resid -= reclen;
	idbp->d_reclen = reclen;
	idbp->d_ino = pa->ino;
	idbp->d_off = pa->cook;
	idbp->d_name[namelen] = '\0';
	pa->done = 1;
	memcpy(idbp->d_name, pa->name, namelen);
	return 0;
}

/*
 * Getdents put routine for 64-bit ABI, uio form.
 */
static int
xfs_dir2_put_dirent64_uio(
	xfs_dir2_put_args_t	*pa)
{
	xfs_dirent_t		*idbp;		/* dirent pointer */
	int			namelen;	/* entry name length */
	int			reclen;		/* entry total length */
	int			rval;		/* return value */
	uio_t			*uio;		/* I/O control */

	namelen = pa->namelen;
	reclen = DIRENTSIZE(namelen);
	uio = pa->uio;
	/*
	 * Won't fit in the remaining space.
	 */
	if (reclen > uio->uio_resid) {
		pa->done = 0;
		return 0;
	}
	idbp = pa->dbp;
	idbp->d_reclen = reclen;
	idbp->d_ino = pa->ino;
	idbp->d_off = pa->cook;
	idbp->d_name[namelen] = '\0';
	memcpy(idbp->d_name, pa->name, namelen);
	rval = uio_read((caddr_t)idbp, reclen, uio);
	pa->done = (rval == 0);
	return rval;
}

/*
 * Remove the given block from the directory.
 * This routine is used for data and free blocks, leaf/node are done
 * by xfs_da_shrink_inode.
 */
int
xfs_dir2_shrink_inode(
	xfs_da_args_t	*args,
	xfs_dir2_db_t	db,
	xfs_dabuf_t	*bp)
{
	xfs_fileoff_t	bno;		/* directory file offset */
	xfs_dablk_t	da;		/* directory file offset */
	int		done;		/* bunmap is finished */
	xfs_inode_t	*dp;
	int		error;
	xfs_mount_t	*mp;
	xfs_trans_t	*tp;

	xfs_dir2_trace_args_db("shrink_inode", args, db, bp);
	dp = args->dp;
	mp = dp->i_mount;
	tp = args->trans;
	da = XFS_DIR2_DB_TO_DA(mp, db);
	/*
	 * Unmap the fsblock(s).
	 */
	if ((error = xfs_bunmapi(tp, dp, da, mp->m_dirblkfsbs,
			XFS_BMAPI_METADATA, 0, args->firstblock, args->flist,
			NULL, &done))) {
		/*
		 * ENOSPC actually can happen if we're in a removename with
		 * no space reservation, and the resulting block removal
		 * would cause a bmap btree split or conversion from extents
		 * to btree.  This can only happen for un-fragmented
		 * directory blocks, since you need to be punching out
		 * the middle of an extent.
		 * In this case we need to leave the block in the file,
		 * and not binval it.
		 * So the block has to be in a consistent empty state
		 * and appropriately logged.
		 * We don't free up the buffer, the caller can tell it
		 * hasn't happened since it got an error back.
		 */
		return error;
	}
	ASSERT(done);
	/*
	 * Invalidate the buffer from the transaction.
	 */
	xfs_da_binval(tp, bp);
	/*
	 * If it's not a data block, we're done.
	 */
	if (db >= XFS_DIR2_LEAF_FIRSTDB(mp))
		return 0;
	/*
	 * If the block isn't the last one in the directory, we're done.
	 */
	if (dp->i_d.di_size > XFS_DIR2_DB_OFF_TO_BYTE(mp, db + 1, 0))
		return 0;
	bno = da;
	if ((error = xfs_bmap_last_before(tp, dp, &bno, XFS_DATA_FORK))) {
		/*
		 * This can't really happen unless there's kernel corruption.
		 */
		return error;
	}
	if (db == mp->m_dirdatablk)
		ASSERT(bno == 0);
	else
		ASSERT(bno > 0);
	/*
	 * Set the size to the new last block.
	 */
	dp->i_d.di_size = XFS_FSB_TO_B(mp, bno);
	xfs_trans_log_inode(tp, dp, XFS_ILOG_CORE);
	return 0;
}
