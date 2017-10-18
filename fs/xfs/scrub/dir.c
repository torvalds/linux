/*
 * Copyright (C) 2017 Oracle.  All Rights Reserved.
 *
 * Author: Darrick J. Wong <darrick.wong@oracle.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it would be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write the Free Software Foundation,
 * Inc.,  51 Franklin St, Fifth Floor, Boston, MA  02110-1301, USA.
 */
#include "xfs.h"
#include "xfs_fs.h"
#include "xfs_shared.h"
#include "xfs_format.h"
#include "xfs_trans_resv.h"
#include "xfs_mount.h"
#include "xfs_defer.h"
#include "xfs_btree.h"
#include "xfs_bit.h"
#include "xfs_log_format.h"
#include "xfs_trans.h"
#include "xfs_sb.h"
#include "xfs_inode.h"
#include "xfs_icache.h"
#include "xfs_itable.h"
#include "xfs_da_format.h"
#include "xfs_da_btree.h"
#include "xfs_dir2.h"
#include "xfs_dir2_priv.h"
#include "xfs_ialloc.h"
#include "scrub/xfs_scrub.h"
#include "scrub/scrub.h"
#include "scrub/common.h"
#include "scrub/trace.h"
#include "scrub/dabtree.h"

/* Set us up to scrub directories. */
int
xfs_scrub_setup_directory(
	struct xfs_scrub_context	*sc,
	struct xfs_inode		*ip)
{
	return xfs_scrub_setup_inode_contents(sc, ip, 0);
}

/* Directories */

/* Scrub a directory entry. */

struct xfs_scrub_dir_ctx {
	/* VFS fill-directory iterator */
	struct dir_context		dir_iter;

	struct xfs_scrub_context	*sc;
};

/* Check that an inode's mode matches a given DT_ type. */
STATIC int
xfs_scrub_dir_check_ftype(
	struct xfs_scrub_dir_ctx	*sdc,
	xfs_fileoff_t			offset,
	xfs_ino_t			inum,
	int				dtype)
{
	struct xfs_mount		*mp = sdc->sc->mp;
	struct xfs_inode		*ip;
	int				ino_dtype;
	int				error = 0;

	if (!xfs_sb_version_hasftype(&mp->m_sb)) {
		if (dtype != DT_UNKNOWN && dtype != DT_DIR)
			xfs_scrub_fblock_set_corrupt(sdc->sc, XFS_DATA_FORK,
					offset);
		goto out;
	}

	/*
	 * Grab the inode pointed to by the dirent.  We release the
	 * inode before we cancel the scrub transaction.  Since we're
	 * don't know a priori that releasing the inode won't trigger
	 * eofblocks cleanup (which allocates what would be a nested
	 * transaction), we can't use DONTCACHE here because DONTCACHE
	 * inodes can trigger immediate inactive cleanup of the inode.
	 */
	error = xfs_iget(mp, sdc->sc->tp, inum, 0, 0, &ip);
	if (!xfs_scrub_fblock_process_error(sdc->sc, XFS_DATA_FORK, offset,
			&error))
		goto out;

	/* Convert mode to the DT_* values that dir_emit uses. */
	ino_dtype = xfs_dir3_get_dtype(mp,
			xfs_mode_to_ftype(VFS_I(ip)->i_mode));
	if (ino_dtype != dtype)
		xfs_scrub_fblock_set_corrupt(sdc->sc, XFS_DATA_FORK, offset);
	iput(VFS_I(ip));
out:
	return error;
}

/*
 * Scrub a single directory entry.
 *
 * We use the VFS directory iterator (i.e. readdir) to call this
 * function for every directory entry in a directory.  Once we're here,
 * we check the inode number to make sure it's sane, then we check that
 * we can look up this filename.  Finally, we check the ftype.
 */
STATIC int
xfs_scrub_dir_actor(
	struct dir_context		*dir_iter,
	const char			*name,
	int				namelen,
	loff_t				pos,
	u64				ino,
	unsigned			type)
{
	struct xfs_mount		*mp;
	struct xfs_inode		*ip;
	struct xfs_scrub_dir_ctx	*sdc;
	struct xfs_name			xname;
	xfs_ino_t			lookup_ino;
	xfs_dablk_t			offset;
	int				error = 0;

	sdc = container_of(dir_iter, struct xfs_scrub_dir_ctx, dir_iter);
	ip = sdc->sc->ip;
	mp = ip->i_mount;
	offset = xfs_dir2_db_to_da(mp->m_dir_geo,
			xfs_dir2_dataptr_to_db(mp->m_dir_geo, pos));

	/* Does this inode number make sense? */
	if (!xfs_verify_dir_ino(mp, ino)) {
		xfs_scrub_fblock_set_corrupt(sdc->sc, XFS_DATA_FORK, offset);
		goto out;
	}

	if (!strncmp(".", name, namelen)) {
		/* If this is "." then check that the inum matches the dir. */
		if (xfs_sb_version_hasftype(&mp->m_sb) && type != DT_DIR)
			xfs_scrub_fblock_set_corrupt(sdc->sc, XFS_DATA_FORK,
					offset);
		if (ino != ip->i_ino)
			xfs_scrub_fblock_set_corrupt(sdc->sc, XFS_DATA_FORK,
					offset);
	} else if (!strncmp("..", name, namelen)) {
		/*
		 * If this is ".." in the root inode, check that the inum
		 * matches this dir.
		 */
		if (xfs_sb_version_hasftype(&mp->m_sb) && type != DT_DIR)
			xfs_scrub_fblock_set_corrupt(sdc->sc, XFS_DATA_FORK,
					offset);
		if (ip->i_ino == mp->m_sb.sb_rootino && ino != ip->i_ino)
			xfs_scrub_fblock_set_corrupt(sdc->sc, XFS_DATA_FORK,
					offset);
	}

	/* Verify that we can look up this name by hash. */
	xname.name = name;
	xname.len = namelen;
	xname.type = XFS_DIR3_FT_UNKNOWN;

	error = xfs_dir_lookup(sdc->sc->tp, ip, &xname, &lookup_ino, NULL);
	if (!xfs_scrub_fblock_process_error(sdc->sc, XFS_DATA_FORK, offset,
			&error))
		goto fail_xref;
	if (lookup_ino != ino) {
		xfs_scrub_fblock_set_corrupt(sdc->sc, XFS_DATA_FORK, offset);
		goto out;
	}

	/* Verify the file type.  This function absorbs error codes. */
	error = xfs_scrub_dir_check_ftype(sdc, offset, lookup_ino, type);
	if (error)
		goto out;
out:
	return error;
fail_xref:
	return error;
}

/* Scrub a directory btree record. */
STATIC int
xfs_scrub_dir_rec(
	struct xfs_scrub_da_btree	*ds,
	int				level,
	void				*rec)
{
	struct xfs_mount		*mp = ds->state->mp;
	struct xfs_dir2_leaf_entry	*ent = rec;
	struct xfs_inode		*dp = ds->dargs.dp;
	struct xfs_dir2_data_entry	*dent;
	struct xfs_buf			*bp;
	xfs_ino_t			ino;
	xfs_dablk_t			rec_bno;
	xfs_dir2_db_t			db;
	xfs_dir2_data_aoff_t		off;
	xfs_dir2_dataptr_t		ptr;
	xfs_dahash_t			calc_hash;
	xfs_dahash_t			hash;
	unsigned int			tag;
	int				error;

	/* Check the hash of the entry. */
	error = xfs_scrub_da_btree_hash(ds, level, &ent->hashval);
	if (error)
		goto out;

	/* Valid hash pointer? */
	ptr = be32_to_cpu(ent->address);
	if (ptr == 0)
		return 0;

	/* Find the directory entry's location. */
	db = xfs_dir2_dataptr_to_db(mp->m_dir_geo, ptr);
	off = xfs_dir2_dataptr_to_off(mp->m_dir_geo, ptr);
	rec_bno = xfs_dir2_db_to_da(mp->m_dir_geo, db);

	if (rec_bno >= mp->m_dir_geo->leafblk) {
		xfs_scrub_da_set_corrupt(ds, level);
		goto out;
	}
	error = xfs_dir3_data_read(ds->dargs.trans, dp, rec_bno, -2, &bp);
	if (!xfs_scrub_fblock_process_error(ds->sc, XFS_DATA_FORK, rec_bno,
			&error))
		goto out;
	if (!bp) {
		xfs_scrub_fblock_set_corrupt(ds->sc, XFS_DATA_FORK, rec_bno);
		goto out;
	}

	/* Retrieve the entry, sanity check it, and compare hashes. */
	dent = (struct xfs_dir2_data_entry *)(((char *)bp->b_addr) + off);
	ino = be64_to_cpu(dent->inumber);
	hash = be32_to_cpu(ent->hashval);
	tag = be16_to_cpup(dp->d_ops->data_entry_tag_p(dent));
	if (!xfs_verify_dir_ino(mp, ino) || tag != off)
		xfs_scrub_fblock_set_corrupt(ds->sc, XFS_DATA_FORK, rec_bno);
	if (dent->namelen == 0) {
		xfs_scrub_fblock_set_corrupt(ds->sc, XFS_DATA_FORK, rec_bno);
		goto out_relse;
	}
	calc_hash = xfs_da_hashname(dent->name, dent->namelen);
	if (calc_hash != hash)
		xfs_scrub_fblock_set_corrupt(ds->sc, XFS_DATA_FORK, rec_bno);

out_relse:
	xfs_trans_brelse(ds->dargs.trans, bp);
out:
	return error;
}

/* Scrub a whole directory. */
int
xfs_scrub_directory(
	struct xfs_scrub_context	*sc)
{
	struct xfs_scrub_dir_ctx	sdc = {
		.dir_iter.actor = xfs_scrub_dir_actor,
		.dir_iter.pos = 0,
		.sc = sc,
	};
	size_t				bufsize;
	loff_t				oldpos;
	int				error;

	if (!S_ISDIR(VFS_I(sc->ip)->i_mode))
		return -ENOENT;

	/* Plausible size? */
	if (sc->ip->i_d.di_size < xfs_dir2_sf_hdr_size(0)) {
		xfs_scrub_ino_set_corrupt(sc, sc->ip->i_ino, NULL);
		goto out;
	}

	/* Check directory tree structure */
	error = xfs_scrub_da_btree(sc, XFS_DATA_FORK, xfs_scrub_dir_rec);
	if (error)
		return error;

	if (sc->sm->sm_flags & XFS_SCRUB_OFLAG_CORRUPT)
		return error;

	/*
	 * Check that every dirent we see can also be looked up by hash.
	 * Userspace usually asks for a 32k buffer, so we will too.
	 */
	bufsize = (size_t)min_t(loff_t, XFS_READDIR_BUFSIZE,
			sc->ip->i_d.di_size);

	/*
	 * Look up every name in this directory by hash.
	 *
	 * Use the xfs_readdir function to call xfs_scrub_dir_actor on
	 * every directory entry in this directory.  In _actor, we check
	 * the name, inode number, and ftype (if applicable) of the
	 * entry.  xfs_readdir uses the VFS filldir functions to provide
	 * iteration context.
	 *
	 * The VFS grabs a read or write lock via i_rwsem before it reads
	 * or writes to a directory.  If we've gotten this far we've
	 * already obtained IOLOCK_EXCL, which (since 4.10) is the same as
	 * getting a write lock on i_rwsem.  Therefore, it is safe for us
	 * to drop the ILOCK here in order to reuse the _readdir and
	 * _dir_lookup routines, which do their own ILOCK locking.
	 */
	oldpos = 0;
	sc->ilock_flags &= ~XFS_ILOCK_EXCL;
	xfs_iunlock(sc->ip, XFS_ILOCK_EXCL);
	while (true) {
		error = xfs_readdir(sc->tp, sc->ip, &sdc.dir_iter, bufsize);
		if (!xfs_scrub_fblock_process_error(sc, XFS_DATA_FORK, 0,
				&error))
			goto out;
		if (oldpos == sdc.dir_iter.pos)
			break;
		oldpos = sdc.dir_iter.pos;
	}

out:
	return error;
}
