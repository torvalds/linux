// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (c) 2020-2024 Oracle.  All Rights Reserved.
 * Author: Darrick J. Wong <djwong@kernel.org>
 */
#include "xfs.h"
#include "xfs_fs.h"
#include "xfs_shared.h"
#include "xfs_format.h"
#include "xfs_trans_resv.h"
#include "xfs_mount.h"
#include "xfs_defer.h"
#include "xfs_bit.h"
#include "xfs_log_format.h"
#include "xfs_trans.h"
#include "xfs_sb.h"
#include "xfs_inode.h"
#include "xfs_icache.h"
#include "xfs_da_format.h"
#include "xfs_da_btree.h"
#include "xfs_dir2.h"
#include "xfs_dir2_priv.h"
#include "xfs_bmap.h"
#include "xfs_quota.h"
#include "xfs_bmap_btree.h"
#include "xfs_trans_space.h"
#include "xfs_bmap_util.h"
#include "xfs_exchmaps.h"
#include "xfs_exchrange.h"
#include "xfs_ag.h"
#include "xfs_parent.h"
#include "scrub/xfs_scrub.h"
#include "scrub/scrub.h"
#include "scrub/common.h"
#include "scrub/trace.h"
#include "scrub/repair.h"
#include "scrub/tempfile.h"
#include "scrub/tempexch.h"
#include "scrub/xfile.h"
#include "scrub/xfarray.h"
#include "scrub/xfblob.h"
#include "scrub/iscan.h"
#include "scrub/readdir.h"
#include "scrub/reap.h"
#include "scrub/findparent.h"
#include "scrub/orphanage.h"
#include "scrub/listxattr.h"

/*
 * Directory Repair
 * ================
 *
 * We repair directories by reading the directory data blocks looking for
 * directory entries that look salvageable (name passes verifiers, entry points
 * to a valid allocated inode, etc).  Each entry worth salvaging is stashed in
 * memory, and the stashed entries are periodically replayed into a temporary
 * directory to constrain memory use.  Batching the construction of the
 * temporary directory in this fashion reduces lock cycling of the directory
 * being repaired and the temporary directory, and will later become important
 * for parent pointer scanning.
 *
 * If parent pointers are enabled on this filesystem, we instead reconstruct
 * the directory by visiting each parent pointer of each file in the filesystem
 * and translating the relevant parent pointer records into dirents.  In this
 * case, it is advantageous to stash all directory entries created from parent
 * pointers for a single child file before replaying them into the temporary
 * directory.  To save memory, the live filesystem scan reuses the findparent
 * fields.  Directory repair chooses either parent pointer scanning or
 * directory entry salvaging, but not both.
 *
 * Directory entries added to the temporary directory do not elevate the link
 * counts of the inodes found.  When salvaging completes, the remaining stashed
 * entries are replayed to the temporary directory.  An atomic mapping exchange
 * is used to commit the new directory blocks to the directory being repaired.
 * This will disrupt readdir cursors.
 *
 * Locking Issues
 * --------------
 *
 * If /a, /a/b, and /c are all directories, the VFS does not take i_rwsem on
 * /a/b for a "mv /a/b /c/" operation.  This means that only b's ILOCK protects
 * b's dotdot update.  This is in contrast to every other dotdot update (link,
 * remove, mkdir).  If the repair code drops the ILOCK, it must either
 * revalidate the dotdot entry or use dirent hooks to capture updates from
 * other threads.
 */

/* Create a dirent in the tempdir. */
#define XREP_DIRENT_ADD		(1)

/* Remove a dirent from the tempdir. */
#define XREP_DIRENT_REMOVE	(2)

/* Directory entry to be restored in the new directory. */
struct xrep_dirent {
	/* Cookie for retrieval of the dirent name. */
	xfblob_cookie		name_cookie;

	/* Target inode number. */
	xfs_ino_t		ino;

	/* Length of the dirent name. */
	uint8_t			namelen;

	/* File type of the dirent. */
	uint8_t			ftype;

	/* XREP_DIRENT_{ADD,REMOVE} */
	uint8_t			action;
};

/*
 * Stash up to 8 pages of recovered dirent data in dir_entries and dir_names
 * before we write them to the temp dir.
 */
#define XREP_DIR_MAX_STASH_BYTES	(PAGE_SIZE * 8)

struct xrep_dir {
	struct xfs_scrub	*sc;

	/* Fixed-size array of xrep_dirent structures. */
	struct xfarray		*dir_entries;

	/* Blobs containing directory entry names. */
	struct xfblob		*dir_names;

	/* Information for exchanging data forks at the end. */
	struct xrep_tempexch	tx;

	/* Preallocated args struct for performing dir operations */
	struct xfs_da_args	args;

	/*
	 * Information used to scan the filesystem to find the inumber of the
	 * dotdot entry for this directory.  For directory salvaging when
	 * parent pointers are not enabled, we use the findparent_* functions
	 * on this object and access only the parent_ino field directly.
	 *
	 * When parent pointers are enabled, however, the pptr scanner uses the
	 * iscan, hooks, lock, and parent_ino fields of this object directly.
	 * @pscan.lock coordinates access to dir_entries, dir_names,
	 * parent_ino, subdirs, dirents, and args.  This reduces the memory
	 * requirements of this structure.
	 */
	struct xrep_parent_scan_info pscan;

	/*
	 * Context information for attaching this directory to the lost+found
	 * if this directory does not have a parent.
	 */
	struct xrep_adoption	adoption;

	/* How many subdirectories did we find? */
	uint64_t		subdirs;

	/* How many dirents did we find? */
	unsigned int		dirents;

	/* Should we move this directory to the orphanage? */
	bool			needs_adoption;

	/* Directory entry name, plus the trailing null. */
	struct xfs_name		xname;
	unsigned char		namebuf[MAXNAMELEN];
};

/* Tear down all the incore stuff we created. */
static void
xrep_dir_teardown(
	struct xfs_scrub	*sc)
{
	struct xrep_dir		*rd = sc->buf;

	xrep_findparent_scan_teardown(&rd->pscan);
	xfblob_destroy(rd->dir_names);
	xfarray_destroy(rd->dir_entries);
}

/* Set up for a directory repair. */
int
xrep_setup_directory(
	struct xfs_scrub	*sc)
{
	struct xrep_dir		*rd;
	int			error;

	xchk_fsgates_enable(sc, XCHK_FSGATES_DIRENTS);

	error = xrep_orphanage_try_create(sc);
	if (error)
		return error;

	error = xrep_tempfile_create(sc, S_IFDIR);
	if (error)
		return error;

	rd = kvzalloc(sizeof(struct xrep_dir), XCHK_GFP_FLAGS);
	if (!rd)
		return -ENOMEM;
	rd->sc = sc;
	rd->xname.name = rd->namebuf;
	sc->buf = rd;

	return 0;
}

/*
 * Look up the dotdot entry and confirm that it's really the parent.
 * Returns NULLFSINO if we don't know what to do.
 */
static inline xfs_ino_t
xrep_dir_lookup_parent(
	struct xrep_dir		*rd)
{
	struct xfs_scrub	*sc = rd->sc;
	xfs_ino_t		ino;
	int			error;

	error = xfs_dir_lookup(sc->tp, sc->ip, &xfs_name_dotdot, &ino, NULL);
	if (error)
		return NULLFSINO;
	if (!xfs_verify_dir_ino(sc->mp, ino))
		return NULLFSINO;

	error = xrep_findparent_confirm(sc, &ino);
	if (error)
		return NULLFSINO;

	return ino;
}

/*
 * Look up '..' in the dentry cache and confirm that it's really the parent.
 * Returns NULLFSINO if the dcache misses or if the hit is implausible.
 */
static inline xfs_ino_t
xrep_dir_dcache_parent(
	struct xrep_dir		*rd)
{
	struct xfs_scrub	*sc = rd->sc;
	xfs_ino_t		parent_ino;
	int			error;

	parent_ino = xrep_findparent_from_dcache(sc);
	if (parent_ino == NULLFSINO)
		return parent_ino;

	error = xrep_findparent_confirm(sc, &parent_ino);
	if (error)
		return NULLFSINO;

	return parent_ino;
}

/* Try to find the parent of the directory being repaired. */
STATIC int
xrep_dir_find_parent(
	struct xrep_dir		*rd)
{
	xfs_ino_t		ino;

	ino = xrep_findparent_self_reference(rd->sc);
	if (ino != NULLFSINO) {
		xrep_findparent_scan_finish_early(&rd->pscan, ino);
		return 0;
	}

	ino = xrep_dir_dcache_parent(rd);
	if (ino != NULLFSINO) {
		xrep_findparent_scan_finish_early(&rd->pscan, ino);
		return 0;
	}

	ino = xrep_dir_lookup_parent(rd);
	if (ino != NULLFSINO) {
		xrep_findparent_scan_finish_early(&rd->pscan, ino);
		return 0;
	}

	/*
	 * A full filesystem scan is the last resort.  On a busy filesystem,
	 * the scan can fail with -EBUSY if we cannot grab IOLOCKs.  That means
	 * that we don't know what who the parent is, so we should return to
	 * userspace.
	 */
	return xrep_findparent_scan(&rd->pscan);
}

/*
 * Decide if we want to salvage this entry.  We don't bother with oversized
 * names or the dot entry.
 */
STATIC int
xrep_dir_want_salvage(
	struct xrep_dir		*rd,
	const char		*name,
	int			namelen,
	xfs_ino_t		ino)
{
	struct xfs_mount	*mp = rd->sc->mp;

	/* No pointers to ourselves or to garbage. */
	if (ino == rd->sc->ip->i_ino)
		return false;
	if (!xfs_verify_dir_ino(mp, ino))
		return false;

	/* No weird looking names or dot entries. */
	if (namelen >= MAXNAMELEN || namelen <= 0)
		return false;
	if (namelen == 1 && name[0] == '.')
		return false;
	if (!xfs_dir2_namecheck(name, namelen))
		return false;

	return true;
}

/*
 * Remember that we want to create a dirent in the tempdir.  These stashed
 * actions will be replayed later.
 */
STATIC int
xrep_dir_stash_createname(
	struct xrep_dir		*rd,
	const struct xfs_name	*name,
	xfs_ino_t		ino)
{
	struct xrep_dirent	dirent = {
		.action		= XREP_DIRENT_ADD,
		.ino		= ino,
		.namelen	= name->len,
		.ftype		= name->type,
	};
	int			error;

	trace_xrep_dir_stash_createname(rd->sc->tempip, name, ino);

	error = xfblob_storename(rd->dir_names, &dirent.name_cookie, name);
	if (error)
		return error;

	return xfarray_append(rd->dir_entries, &dirent);
}

/*
 * Remember that we want to remove a dirent from the tempdir.  These stashed
 * actions will be replayed later.
 */
STATIC int
xrep_dir_stash_removename(
	struct xrep_dir		*rd,
	const struct xfs_name	*name,
	xfs_ino_t		ino)
{
	struct xrep_dirent	dirent = {
		.action		= XREP_DIRENT_REMOVE,
		.ino		= ino,
		.namelen	= name->len,
		.ftype		= name->type,
	};
	int			error;

	trace_xrep_dir_stash_removename(rd->sc->tempip, name, ino);

	error = xfblob_storename(rd->dir_names, &dirent.name_cookie, name);
	if (error)
		return error;

	return xfarray_append(rd->dir_entries, &dirent);
}

/* Allocate an in-core record to hold entries while we rebuild the dir data. */
STATIC int
xrep_dir_salvage_entry(
	struct xrep_dir		*rd,
	unsigned char		*name,
	unsigned int		namelen,
	xfs_ino_t		ino)
{
	struct xfs_name		xname = {
		.name		= name,
	};
	struct xfs_scrub	*sc = rd->sc;
	struct xfs_inode	*ip;
	unsigned int		i = 0;
	int			error = 0;

	if (xchk_should_terminate(sc, &error))
		return error;

	/*
	 * Truncate the name to the first character that would trip namecheck.
	 * If we no longer have a name after that, ignore this entry.
	 */
	while (i < namelen && name[i] != 0 && name[i] != '/')
		i++;
	if (i == 0)
		return 0;
	xname.len = i;

	/* Ignore '..' entries; we already picked the new parent. */
	if (xname.len == 2 && name[0] == '.' && name[1] == '.') {
		trace_xrep_dir_salvaged_parent(sc->ip, ino);
		return 0;
	}

	trace_xrep_dir_salvage_entry(sc->ip, &xname, ino);

	/*
	 * Compute the ftype or dump the entry if we can't.  We don't lock the
	 * inode because inodes can't change type while we have a reference.
	 */
	error = xchk_iget(sc, ino, &ip);
	if (error)
		return 0;

	xname.type = xfs_mode_to_ftype(VFS_I(ip)->i_mode);
	xchk_irele(sc, ip);

	return xrep_dir_stash_createname(rd, &xname, ino);
}

/* Record a shortform directory entry for later reinsertion. */
STATIC int
xrep_dir_salvage_sf_entry(
	struct xrep_dir			*rd,
	struct xfs_dir2_sf_hdr		*sfp,
	struct xfs_dir2_sf_entry	*sfep)
{
	xfs_ino_t			ino;

	ino = xfs_dir2_sf_get_ino(rd->sc->mp, sfp, sfep);
	if (!xrep_dir_want_salvage(rd, sfep->name, sfep->namelen, ino))
		return 0;

	return xrep_dir_salvage_entry(rd, sfep->name, sfep->namelen, ino);
}

/* Record a regular directory entry for later reinsertion. */
STATIC int
xrep_dir_salvage_data_entry(
	struct xrep_dir			*rd,
	struct xfs_dir2_data_entry	*dep)
{
	xfs_ino_t			ino;

	ino = be64_to_cpu(dep->inumber);
	if (!xrep_dir_want_salvage(rd, dep->name, dep->namelen, ino))
		return 0;

	return xrep_dir_salvage_entry(rd, dep->name, dep->namelen, ino);
}

/* Try to recover block/data format directory entries. */
STATIC int
xrep_dir_recover_data(
	struct xrep_dir		*rd,
	struct xfs_buf		*bp)
{
	struct xfs_da_geometry	*geo = rd->sc->mp->m_dir_geo;
	unsigned int		offset;
	unsigned int		end;
	int			error = 0;

	/*
	 * Loop over the data portion of the block.
	 * Each object is a real entry (dep) or an unused one (dup).
	 */
	offset = geo->data_entry_offset;
	end = min_t(unsigned int, BBTOB(bp->b_length),
			xfs_dir3_data_end_offset(geo, bp->b_addr));

	while (offset < end) {
		struct xfs_dir2_data_unused	*dup = bp->b_addr + offset;
		struct xfs_dir2_data_entry	*dep = bp->b_addr + offset;

		if (xchk_should_terminate(rd->sc, &error))
			return error;

		/* Skip unused entries. */
		if (be16_to_cpu(dup->freetag) == XFS_DIR2_DATA_FREE_TAG) {
			offset += be16_to_cpu(dup->length);
			continue;
		}

		/* Don't walk off the end of the block. */
		offset += xfs_dir2_data_entsize(rd->sc->mp, dep->namelen);
		if (offset > end)
			break;

		/* Ok, let's save this entry. */
		error = xrep_dir_salvage_data_entry(rd, dep);
		if (error)
			return error;

	}

	return 0;
}

/* Try to recover shortform directory entries. */
STATIC int
xrep_dir_recover_sf(
	struct xrep_dir			*rd)
{
	struct xfs_dir2_sf_hdr		*hdr;
	struct xfs_dir2_sf_entry	*sfep;
	struct xfs_dir2_sf_entry	*next;
	struct xfs_ifork		*ifp;
	xfs_ino_t			ino;
	unsigned char			*end;
	int				error = 0;

	ifp = xfs_ifork_ptr(rd->sc->ip, XFS_DATA_FORK);
	hdr = ifp->if_data;
	end = (unsigned char *)ifp->if_data + ifp->if_bytes;

	ino = xfs_dir2_sf_get_parent_ino(hdr);
	trace_xrep_dir_salvaged_parent(rd->sc->ip, ino);

	sfep = xfs_dir2_sf_firstentry(hdr);
	while ((unsigned char *)sfep < end) {
		if (xchk_should_terminate(rd->sc, &error))
			return error;

		next = xfs_dir2_sf_nextentry(rd->sc->mp, hdr, sfep);
		if ((unsigned char *)next > end)
			break;

		/* Ok, let's save this entry. */
		error = xrep_dir_salvage_sf_entry(rd, hdr, sfep);
		if (error)
			return error;

		sfep = next;
	}

	return 0;
}

/*
 * Try to figure out the format of this directory from the data fork mappings
 * and the directory size.  If we can be reasonably sure of format, we can be
 * more aggressive in salvaging directory entries.  On return, @magic_guess
 * will be set to DIR3_BLOCK_MAGIC if we think this is a "block format"
 * directory; DIR3_DATA_MAGIC if we think this is a "data format" directory,
 * and 0 if we can't tell.
 */
STATIC void
xrep_dir_guess_format(
	struct xrep_dir		*rd,
	__be32			*magic_guess)
{
	struct xfs_inode	*dp = rd->sc->ip;
	struct xfs_mount	*mp = rd->sc->mp;
	struct xfs_da_geometry	*geo = mp->m_dir_geo;
	xfs_fileoff_t		last;
	int			error;

	ASSERT(xfs_has_crc(mp));

	*magic_guess = 0;

	/*
	 * If there's a single directory block and the directory size is
	 * exactly one block, this has to be a single block format directory.
	 */
	error = xfs_bmap_last_offset(dp, &last, XFS_DATA_FORK);
	if (!error && XFS_FSB_TO_B(mp, last) == geo->blksize &&
	    dp->i_disk_size == geo->blksize) {
		*magic_guess = cpu_to_be32(XFS_DIR3_BLOCK_MAGIC);
		return;
	}

	/*
	 * If the last extent before the leaf offset matches the directory
	 * size and the directory size is larger than 1 block, this is a
	 * data format directory.
	 */
	last = geo->leafblk;
	error = xfs_bmap_last_before(rd->sc->tp, dp, &last, XFS_DATA_FORK);
	if (!error &&
	    XFS_FSB_TO_B(mp, last) > geo->blksize &&
	    XFS_FSB_TO_B(mp, last) == dp->i_disk_size) {
		*magic_guess = cpu_to_be32(XFS_DIR3_DATA_MAGIC);
		return;
	}
}

/* Recover directory entries from a specific directory block. */
STATIC int
xrep_dir_recover_dirblock(
	struct xrep_dir		*rd,
	__be32			magic_guess,
	xfs_dablk_t		dabno)
{
	struct xfs_dir2_data_hdr *hdr;
	struct xfs_buf		*bp;
	__be32			oldmagic;
	int			error;

	/*
	 * Try to read buffer.  We invalidate them in the next step so we don't
	 * bother to set a buffer type or ops.
	 */
	error = xfs_da_read_buf(rd->sc->tp, rd->sc->ip, dabno,
			XFS_DABUF_MAP_HOLE_OK, &bp, XFS_DATA_FORK, NULL);
	if (error || !bp)
		return error;

	hdr = bp->b_addr;
	oldmagic = hdr->magic;

	trace_xrep_dir_recover_dirblock(rd->sc->ip, dabno,
			be32_to_cpu(hdr->magic), be32_to_cpu(magic_guess));

	/*
	 * If we're sure of the block's format, proceed with the salvage
	 * operation using the specified magic number.
	 */
	if (magic_guess) {
		hdr->magic = magic_guess;
		goto recover;
	}

	/*
	 * If we couldn't guess what type of directory this is, then we will
	 * only salvage entries from directory blocks that match the magic
	 * number and pass verifiers.
	 */
	switch (hdr->magic) {
	case cpu_to_be32(XFS_DIR2_BLOCK_MAGIC):
	case cpu_to_be32(XFS_DIR3_BLOCK_MAGIC):
		if (!xrep_buf_verify_struct(bp, &xfs_dir3_block_buf_ops))
			goto out;
		if (xfs_dir3_block_header_check(bp, rd->sc->ip->i_ino) != NULL)
			goto out;
		break;
	case cpu_to_be32(XFS_DIR2_DATA_MAGIC):
	case cpu_to_be32(XFS_DIR3_DATA_MAGIC):
		if (!xrep_buf_verify_struct(bp, &xfs_dir3_data_buf_ops))
			goto out;
		if (xfs_dir3_data_header_check(bp, rd->sc->ip->i_ino) != NULL)
			goto out;
		break;
	default:
		goto out;
	}

recover:
	error = xrep_dir_recover_data(rd, bp);

out:
	hdr->magic = oldmagic;
	xfs_trans_brelse(rd->sc->tp, bp);
	return error;
}

static inline void
xrep_dir_init_args(
	struct xrep_dir		*rd,
	struct xfs_inode	*dp,
	const struct xfs_name	*name)
{
	memset(&rd->args, 0, sizeof(struct xfs_da_args));
	rd->args.geo = rd->sc->mp->m_dir_geo;
	rd->args.whichfork = XFS_DATA_FORK;
	rd->args.owner = rd->sc->ip->i_ino;
	rd->args.trans = rd->sc->tp;
	rd->args.dp = dp;
	if (!name)
		return;
	rd->args.name = name->name;
	rd->args.namelen = name->len;
	rd->args.filetype = name->type;
	rd->args.hashval = xfs_dir2_hashname(rd->sc->mp, name);
}

/* Replay a stashed createname into the temporary directory. */
STATIC int
xrep_dir_replay_createname(
	struct xrep_dir		*rd,
	const struct xfs_name	*name,
	xfs_ino_t		inum,
	xfs_extlen_t		total)
{
	struct xfs_scrub	*sc = rd->sc;
	struct xfs_inode	*dp = rd->sc->tempip;
	int			error;

	ASSERT(S_ISDIR(VFS_I(dp)->i_mode));

	error = xfs_dir_ino_validate(sc->mp, inum);
	if (error)
		return error;

	trace_xrep_dir_replay_createname(dp, name, inum);

	xrep_dir_init_args(rd, dp, name);
	rd->args.inumber = inum;
	rd->args.total = total;
	rd->args.op_flags = XFS_DA_OP_ADDNAME | XFS_DA_OP_OKNOENT;
	return xfs_dir_createname_args(&rd->args);
}

/* Replay a stashed removename onto the temporary directory. */
STATIC int
xrep_dir_replay_removename(
	struct xrep_dir		*rd,
	const struct xfs_name	*name,
	xfs_extlen_t		total)
{
	struct xfs_inode	*dp = rd->args.dp;

	ASSERT(S_ISDIR(VFS_I(dp)->i_mode));

	xrep_dir_init_args(rd, dp, name);
	rd->args.op_flags = 0;
	rd->args.total = total;

	trace_xrep_dir_replay_removename(dp, name, 0);
	return xfs_dir_removename_args(&rd->args);
}

/*
 * Add this stashed incore directory entry to the temporary directory.
 * The caller must hold the tempdir's IOLOCK, must not hold any ILOCKs, and
 * must not be in transaction context.
 */
STATIC int
xrep_dir_replay_update(
	struct xrep_dir			*rd,
	const struct xfs_name		*xname,
	const struct xrep_dirent	*dirent)
{
	struct xfs_mount		*mp = rd->sc->mp;
#ifdef DEBUG
	xfs_ino_t			ino;
#endif
	uint				resblks;
	int				error;

	resblks = xfs_link_space_res(mp, xname->len);
	error = xchk_trans_alloc(rd->sc, resblks);
	if (error)
		return error;

	/* Lock the temporary directory and join it to the transaction */
	xrep_tempfile_ilock(rd->sc);
	xfs_trans_ijoin(rd->sc->tp, rd->sc->tempip, 0);

	switch (dirent->action) {
	case XREP_DIRENT_ADD:
		/*
		 * Create a replacement dirent in the temporary directory.
		 * Note that _createname doesn't check for existing entries.
		 * There shouldn't be any in the temporary dir, but we'll
		 * verify this in debug mode.
		 */
#ifdef DEBUG
		error = xchk_dir_lookup(rd->sc, rd->sc->tempip, xname, &ino);
		if (error != -ENOENT) {
			ASSERT(error != -ENOENT);
			goto out_cancel;
		}
#endif

		error = xrep_dir_replay_createname(rd, xname, dirent->ino,
				resblks);
		if (error)
			goto out_cancel;

		if (xname->type == XFS_DIR3_FT_DIR)
			rd->subdirs++;
		rd->dirents++;
		break;
	case XREP_DIRENT_REMOVE:
		/*
		 * Remove a dirent from the temporary directory.  Note that
		 * _removename doesn't check the inode target of the exist
		 * entry.  There should be a perfect match in the temporary
		 * dir, but we'll verify this in debug mode.
		 */
#ifdef DEBUG
		error = xchk_dir_lookup(rd->sc, rd->sc->tempip, xname, &ino);
		if (error) {
			ASSERT(error != 0);
			goto out_cancel;
		}
		if (ino != dirent->ino) {
			ASSERT(ino == dirent->ino);
			error = -EIO;
			goto out_cancel;
		}
#endif

		error = xrep_dir_replay_removename(rd, xname, resblks);
		if (error)
			goto out_cancel;

		if (xname->type == XFS_DIR3_FT_DIR)
			rd->subdirs--;
		rd->dirents--;
		break;
	default:
		ASSERT(0);
		error = -EIO;
		goto out_cancel;
	}

	/* Commit and unlock. */
	error = xrep_trans_commit(rd->sc);
	if (error)
		return error;

	xrep_tempfile_iunlock(rd->sc);
	return 0;
out_cancel:
	xchk_trans_cancel(rd->sc);
	xrep_tempfile_iunlock(rd->sc);
	return error;
}

/*
 * Flush stashed incore dirent updates that have been recorded by the scanner.
 * This is done to reduce the memory requirements of the directory rebuild,
 * since directories can contain up to 32GB of directory data.
 *
 * Caller must not hold transactions or ILOCKs.  Caller must hold the tempdir
 * IOLOCK.
 */
STATIC int
xrep_dir_replay_updates(
	struct xrep_dir		*rd)
{
	xfarray_idx_t		array_cur;
	int			error;

	/* Add all the salvaged dirents to the temporary directory. */
	mutex_lock(&rd->pscan.lock);
	foreach_xfarray_idx(rd->dir_entries, array_cur) {
		struct xrep_dirent	dirent;

		error = xfarray_load(rd->dir_entries, array_cur, &dirent);
		if (error)
			goto out_unlock;

		error = xfblob_loadname(rd->dir_names, dirent.name_cookie,
				&rd->xname, dirent.namelen);
		if (error)
			goto out_unlock;
		rd->xname.type = dirent.ftype;
		mutex_unlock(&rd->pscan.lock);

		error = xrep_dir_replay_update(rd, &rd->xname, &dirent);
		if (error)
			return error;
		mutex_lock(&rd->pscan.lock);
	}

	/* Empty out both arrays now that we've added the entries. */
	xfarray_truncate(rd->dir_entries);
	xfblob_truncate(rd->dir_names);
	mutex_unlock(&rd->pscan.lock);
	return 0;
out_unlock:
	mutex_unlock(&rd->pscan.lock);
	return error;
}

/*
 * Periodically flush stashed directory entries to the temporary dir.  This
 * is done to reduce the memory requirements of the directory rebuild, since
 * directories can contain up to 32GB of directory data.
 */
STATIC int
xrep_dir_flush_stashed(
	struct xrep_dir		*rd)
{
	int			error;

	/*
	 * Entering this function, the scrub context has a reference to the
	 * inode being repaired, the temporary file, and a scrub transaction
	 * that we use during dirent salvaging to avoid livelocking if there
	 * are cycles in the directory structures.  We hold ILOCK_EXCL on both
	 * the inode being repaired and the temporary file, though they are
	 * not ijoined to the scrub transaction.
	 *
	 * To constrain kernel memory use, we occasionally write salvaged
	 * dirents from the xfarray and xfblob structures into the temporary
	 * directory in preparation for exchanging the directory structures at
	 * the end.  Updating the temporary file requires a transaction, so we
	 * commit the scrub transaction and drop the two ILOCKs so that
	 * we can allocate whatever transaction we want.
	 *
	 * We still hold IOLOCK_EXCL on the inode being repaired, which
	 * prevents anyone from accessing the damaged directory data while we
	 * repair it.
	 */
	error = xrep_trans_commit(rd->sc);
	if (error)
		return error;
	xchk_iunlock(rd->sc, XFS_ILOCK_EXCL);

	/*
	 * Take the IOLOCK of the temporary file while we modify dirents.  This
	 * isn't strictly required because the temporary file is never revealed
	 * to userspace, but we follow the same locking rules.  We still hold
	 * sc->ip's IOLOCK.
	 */
	error = xrep_tempfile_iolock_polled(rd->sc);
	if (error)
		return error;

	/* Write to the tempdir all the updates that we've stashed. */
	error = xrep_dir_replay_updates(rd);
	xrep_tempfile_iounlock(rd->sc);
	if (error)
		return error;

	/*
	 * Recreate the salvage transaction and relock the dir we're salvaging.
	 */
	error = xchk_trans_alloc(rd->sc, 0);
	if (error)
		return error;
	xchk_ilock(rd->sc, XFS_ILOCK_EXCL);
	return 0;
}

/* Decide if we've stashed too much dirent data in memory. */
static inline bool
xrep_dir_want_flush_stashed(
	struct xrep_dir		*rd)
{
	unsigned long long	bytes;

	bytes = xfarray_bytes(rd->dir_entries) + xfblob_bytes(rd->dir_names);
	return bytes > XREP_DIR_MAX_STASH_BYTES;
}

/* Extract as many directory entries as we can. */
STATIC int
xrep_dir_recover(
	struct xrep_dir		*rd)
{
	struct xfs_bmbt_irec	got;
	struct xfs_scrub	*sc = rd->sc;
	struct xfs_da_geometry	*geo = sc->mp->m_dir_geo;
	xfs_fileoff_t		offset;
	xfs_dablk_t		dabno;
	__be32			magic_guess;
	int			nmap;
	int			error;

	xrep_dir_guess_format(rd, &magic_guess);

	/* Iterate each directory data block in the data fork. */
	for (offset = 0;
	     offset < geo->leafblk;
	     offset = got.br_startoff + got.br_blockcount) {
		nmap = 1;
		error = xfs_bmapi_read(sc->ip, offset, geo->leafblk - offset,
				&got, &nmap, 0);
		if (error)
			return error;
		if (nmap != 1)
			return -EFSCORRUPTED;
		if (!xfs_bmap_is_written_extent(&got))
			continue;

		for (dabno = round_up(got.br_startoff, geo->fsbcount);
		     dabno < got.br_startoff + got.br_blockcount;
		     dabno += geo->fsbcount) {
			if (xchk_should_terminate(rd->sc, &error))
				return error;

			error = xrep_dir_recover_dirblock(rd,
					magic_guess, dabno);
			if (error)
				return error;

			/* Flush dirents to constrain memory usage. */
			if (xrep_dir_want_flush_stashed(rd)) {
				error = xrep_dir_flush_stashed(rd);
				if (error)
					return error;
			}
		}
	}

	return 0;
}

/*
 * Find all the directory entries for this inode by scraping them out of the
 * directory leaf blocks by hand, and flushing them into the temp dir.
 */
STATIC int
xrep_dir_find_entries(
	struct xrep_dir		*rd)
{
	struct xfs_inode	*dp = rd->sc->ip;
	int			error;

	/*
	 * Salvage directory entries from the old directory, and write them to
	 * the temporary directory.
	 */
	if (dp->i_df.if_format == XFS_DINODE_FMT_LOCAL) {
		error = xrep_dir_recover_sf(rd);
	} else {
		error = xfs_iread_extents(rd->sc->tp, dp, XFS_DATA_FORK);
		if (error)
			return error;

		error = xrep_dir_recover(rd);
	}
	if (error)
		return error;

	return xrep_dir_flush_stashed(rd);
}

/* Scan all files in the filesystem for dirents. */
STATIC int
xrep_dir_salvage_entries(
	struct xrep_dir		*rd)
{
	struct xfs_scrub	*sc = rd->sc;
	int			error;

	/*
	 * Drop the ILOCK on this directory so that we can scan for this
	 * directory's parent.  Figure out who is going to be the parent of
	 * this directory, then retake the ILOCK so that we can salvage
	 * directory entries.
	 */
	xchk_iunlock(sc, XFS_ILOCK_EXCL);
	error = xrep_dir_find_parent(rd);
	xchk_ilock(sc, XFS_ILOCK_EXCL);
	if (error)
		return error;

	/*
	 * Collect directory entries by parsing raw leaf blocks to salvage
	 * whatever we can.  When we're done, free the staging memory before
	 * exchanging the directories to reduce memory usage.
	 */
	error = xrep_dir_find_entries(rd);
	if (error)
		return error;

	/*
	 * Cancel the repair transaction and drop the ILOCK so that we can
	 * (later) use the atomic mapping exchange functions to compute the
	 * correct block reservations and re-lock the inodes.
	 *
	 * We still hold IOLOCK_EXCL (aka i_rwsem) which will prevent directory
	 * modifications, but there's nothing to prevent userspace from reading
	 * the directory until we're ready for the exchange operation.  Reads
	 * will return -EIO without shutting down the fs, so we're ok with
	 * that.
	 *
	 * The VFS can change dotdot on us, but the findparent scan will keep
	 * our incore parent inode up to date.  See the note on locking issues
	 * for more details.
	 */
	error = xrep_trans_commit(sc);
	if (error)
		return error;

	xchk_iunlock(sc, XFS_ILOCK_EXCL);
	return 0;
}


/*
 * Examine a parent pointer of a file.  If it leads us back to the directory
 * that we're rebuilding, create an incore dirent from the parent pointer and
 * stash it.
 */
STATIC int
xrep_dir_scan_pptr(
	struct xfs_scrub		*sc,
	struct xfs_inode		*ip,
	unsigned int			attr_flags,
	const unsigned char		*name,
	unsigned int			namelen,
	const void			*value,
	unsigned int			valuelen,
	void				*priv)
{
	struct xfs_name			xname = {
		.name			= name,
		.len			= namelen,
		.type			= xfs_mode_to_ftype(VFS_I(ip)->i_mode),
	};
	xfs_ino_t			parent_ino;
	uint32_t			parent_gen;
	struct xrep_dir			*rd = priv;
	int				error;

	if (!(attr_flags & XFS_ATTR_PARENT))
		return 0;

	/*
	 * Ignore parent pointers that point back to a different dir, list the
	 * wrong generation number, or are invalid.
	 */
	error = xfs_parent_from_attr(sc->mp, attr_flags, name, namelen, value,
			valuelen, &parent_ino, &parent_gen);
	if (error)
		return error;

	if (parent_ino != sc->ip->i_ino ||
	    parent_gen != VFS_I(sc->ip)->i_generation)
		return 0;

	mutex_lock(&rd->pscan.lock);
	error = xrep_dir_stash_createname(rd, &xname, ip->i_ino);
	mutex_unlock(&rd->pscan.lock);
	return error;
}

/*
 * If this child dirent points to the directory being repaired, remember that
 * fact so that we can reset the dotdot entry if necessary.
 */
STATIC int
xrep_dir_scan_dirent(
	struct xfs_scrub	*sc,
	struct xfs_inode	*dp,
	xfs_dir2_dataptr_t	dapos,
	const struct xfs_name	*name,
	xfs_ino_t		ino,
	void			*priv)
{
	struct xrep_dir		*rd = priv;

	/* Dirent doesn't point to this directory. */
	if (ino != rd->sc->ip->i_ino)
		return 0;

	/* Ignore garbage inum. */
	if (!xfs_verify_dir_ino(rd->sc->mp, ino))
		return 0;

	/* No weird looking names. */
	if (name->len >= MAXNAMELEN || name->len <= 0)
		return 0;

	/* Don't pick up dot or dotdot entries; we only want child dirents. */
	if (xfs_dir2_samename(name, &xfs_name_dotdot) ||
	    xfs_dir2_samename(name, &xfs_name_dot))
		return 0;

	trace_xrep_dir_stash_createname(sc->tempip, &xfs_name_dotdot,
			dp->i_ino);

	xrep_findparent_scan_found(&rd->pscan, dp->i_ino);
	return 0;
}

/*
 * Decide if we want to look for child dirents or parent pointers in this file.
 * Skip the dir being repaired and any files being used to stage repairs.
 */
static inline bool
xrep_dir_want_scan(
	struct xrep_dir		*rd,
	const struct xfs_inode	*ip)
{
	return ip != rd->sc->ip && !xrep_is_tempfile(ip);
}

/*
 * Take ILOCK on a file that we want to scan.
 *
 * Select ILOCK_EXCL if the file is a directory with an unloaded data bmbt or
 * has an unloaded attr bmbt.  Otherwise, take ILOCK_SHARED.
 */
static inline unsigned int
xrep_dir_scan_ilock(
	struct xrep_dir		*rd,
	struct xfs_inode	*ip)
{
	uint			lock_mode = XFS_ILOCK_SHARED;

	/* Need to take the shared ILOCK to advance the iscan cursor. */
	if (!xrep_dir_want_scan(rd, ip))
		goto lock;

	if (S_ISDIR(VFS_I(ip)->i_mode) && xfs_need_iread_extents(&ip->i_df)) {
		lock_mode = XFS_ILOCK_EXCL;
		goto lock;
	}

	if (xfs_inode_has_attr_fork(ip) && xfs_need_iread_extents(&ip->i_af))
		lock_mode = XFS_ILOCK_EXCL;

lock:
	xfs_ilock(ip, lock_mode);
	return lock_mode;
}

/*
 * Scan this file for relevant child dirents or parent pointers that point to
 * the directory we're rebuilding.
 */
STATIC int
xrep_dir_scan_file(
	struct xrep_dir		*rd,
	struct xfs_inode	*ip)
{
	unsigned int		lock_mode;
	int			error = 0;

	lock_mode = xrep_dir_scan_ilock(rd, ip);

	if (!xrep_dir_want_scan(rd, ip))
		goto scan_done;

	/*
	 * If the extended attributes look as though they has been zapped by
	 * the inode record repair code, we cannot scan for parent pointers.
	 */
	if (xchk_pptr_looks_zapped(ip)) {
		error = -EBUSY;
		goto scan_done;
	}

	error = xchk_xattr_walk(rd->sc, ip, xrep_dir_scan_pptr, NULL, rd);
	if (error)
		goto scan_done;

	if (S_ISDIR(VFS_I(ip)->i_mode)) {
		/*
		 * If the directory looks as though it has been zapped by the
		 * inode record repair code, we cannot scan for child dirents.
		 */
		if (xchk_dir_looks_zapped(ip)) {
			error = -EBUSY;
			goto scan_done;
		}

		error = xchk_dir_walk(rd->sc, ip, xrep_dir_scan_dirent, rd);
		if (error)
			goto scan_done;
	}

scan_done:
	xchk_iscan_mark_visited(&rd->pscan.iscan, ip);
	xfs_iunlock(ip, lock_mode);
	return error;
}

/*
 * Scan all files in the filesystem for parent pointers that we can turn into
 * replacement dirents, and a dirent that we can use to set the dotdot pointer.
 */
STATIC int
xrep_dir_scan_dirtree(
	struct xrep_dir		*rd)
{
	struct xfs_scrub	*sc = rd->sc;
	struct xfs_inode	*ip;
	int			error;

	/* Roots of directory trees are their own parents. */
	if (sc->ip == sc->mp->m_rootip)
		xrep_findparent_scan_found(&rd->pscan, sc->ip->i_ino);

	/*
	 * Filesystem scans are time consuming.  Drop the directory ILOCK and
	 * all other resources for the duration of the scan and hope for the
	 * best.  The live update hooks will keep our scan information up to
	 * date even though we've dropped the locks.
	 */
	xchk_trans_cancel(sc);
	if (sc->ilock_flags & (XFS_ILOCK_SHARED | XFS_ILOCK_EXCL))
		xchk_iunlock(sc, sc->ilock_flags & (XFS_ILOCK_SHARED |
						    XFS_ILOCK_EXCL));
	error = xchk_trans_alloc_empty(sc);
	if (error)
		return error;

	while ((error = xchk_iscan_iter(&rd->pscan.iscan, &ip)) == 1) {
		bool		flush;

		error = xrep_dir_scan_file(rd, ip);
		xchk_irele(sc, ip);
		if (error)
			break;

		/* Flush stashed dirent updates to constrain memory usage. */
		mutex_lock(&rd->pscan.lock);
		flush = xrep_dir_want_flush_stashed(rd);
		mutex_unlock(&rd->pscan.lock);
		if (flush) {
			xchk_trans_cancel(sc);

			error = xrep_tempfile_iolock_polled(sc);
			if (error)
				break;

			error = xrep_dir_replay_updates(rd);
			xrep_tempfile_iounlock(sc);
			if (error)
				break;

			error = xchk_trans_alloc_empty(sc);
			if (error)
				break;
		}

		if (xchk_should_terminate(sc, &error))
			break;
	}
	xchk_iscan_iter_finish(&rd->pscan.iscan);
	if (error) {
		/*
		 * If we couldn't grab an inode that was busy with a state
		 * change, change the error code so that we exit to userspace
		 * as quickly as possible.
		 */
		if (error == -EBUSY)
			return -ECANCELED;
		return error;
	}

	/*
	 * Cancel the empty transaction so that we can (later) use the atomic
	 * file mapping exchange functions to lock files and commit the new
	 * directory.
	 */
	xchk_trans_cancel(rd->sc);
	return 0;
}

/*
 * Capture dirent updates being made by other threads which are relevant to the
 * directory being repaired.
 */
STATIC int
xrep_dir_live_update(
	struct notifier_block		*nb,
	unsigned long			action,
	void				*data)
{
	struct xfs_dir_update_params	*p = data;
	struct xrep_dir			*rd;
	struct xfs_scrub		*sc;
	int				error = 0;

	rd = container_of(nb, struct xrep_dir, pscan.dhook.dirent_hook.nb);
	sc = rd->sc;

	/*
	 * This thread updated a child dirent in the directory that we're
	 * rebuilding.  Stash the update for replay against the temporary
	 * directory.
	 */
	if (p->dp->i_ino == sc->ip->i_ino &&
	    xchk_iscan_want_live_update(&rd->pscan.iscan, p->ip->i_ino)) {
		mutex_lock(&rd->pscan.lock);
		if (p->delta > 0)
			error = xrep_dir_stash_createname(rd, p->name,
					p->ip->i_ino);
		else
			error = xrep_dir_stash_removename(rd, p->name,
					p->ip->i_ino);
		mutex_unlock(&rd->pscan.lock);
		if (error)
			goto out_abort;
	}

	/*
	 * This thread updated another directory's child dirent that points to
	 * the directory that we're rebuilding, so remember the new dotdot
	 * target.
	 */
	if (p->ip->i_ino == sc->ip->i_ino &&
	    xchk_iscan_want_live_update(&rd->pscan.iscan, p->dp->i_ino)) {
		if (p->delta > 0) {
			trace_xrep_dir_stash_createname(sc->tempip,
					&xfs_name_dotdot,
					p->dp->i_ino);

			xrep_findparent_scan_found(&rd->pscan, p->dp->i_ino);
		} else {
			trace_xrep_dir_stash_removename(sc->tempip,
					&xfs_name_dotdot,
					rd->pscan.parent_ino);

			xrep_findparent_scan_found(&rd->pscan, NULLFSINO);
		}
	}

	return NOTIFY_DONE;
out_abort:
	xchk_iscan_abort(&rd->pscan.iscan);
	return NOTIFY_DONE;
}

/*
 * Free all the directory blocks and reset the data fork.  The caller must
 * join the inode to the transaction.  This function returns with the inode
 * joined to a clean scrub transaction.
 */
STATIC int
xrep_dir_reset_fork(
	struct xrep_dir		*rd,
	xfs_ino_t		parent_ino)
{
	struct xfs_scrub	*sc = rd->sc;
	struct xfs_ifork	*ifp = xfs_ifork_ptr(sc->tempip, XFS_DATA_FORK);
	int			error;

	/* Unmap all the directory buffers. */
	if (xfs_ifork_has_extents(ifp)) {
		error = xrep_reap_ifork(sc, sc->tempip, XFS_DATA_FORK);
		if (error)
			return error;
	}

	trace_xrep_dir_reset_fork(sc->tempip, parent_ino);

	/* Reset the data fork to an empty data fork. */
	xfs_idestroy_fork(ifp);
	ifp->if_bytes = 0;
	sc->tempip->i_disk_size = 0;

	/* Reinitialize the short form directory. */
	xrep_dir_init_args(rd, sc->tempip, NULL);
	return xfs_dir2_sf_create(&rd->args, parent_ino);
}

/*
 * Prepare both inodes' directory forks for exchanging mappings.  Promote the
 * tempfile from short format to leaf format, and if the file being repaired
 * has a short format data fork, turn it into an empty extent list.
 */
STATIC int
xrep_dir_swap_prep(
	struct xfs_scrub	*sc,
	bool			temp_local,
	bool			ip_local)
{
	int			error;

	/*
	 * If the tempfile's directory is in shortform format, convert that to
	 * a single leaf extent so that we can use the atomic mapping exchange.
	 */
	if (temp_local) {
		struct xfs_da_args	args = {
			.dp		= sc->tempip,
			.geo		= sc->mp->m_dir_geo,
			.whichfork	= XFS_DATA_FORK,
			.trans		= sc->tp,
			.total		= 1,
			.owner		= sc->ip->i_ino,
		};

		error = xfs_dir2_sf_to_block(&args);
		if (error)
			return error;

		/*
		 * Roll the deferred log items to get us back to a clean
		 * transaction.
		 */
		error = xfs_defer_finish(&sc->tp);
		if (error)
			return error;
	}

	/*
	 * If the file being repaired had a shortform data fork, convert that
	 * to an empty extent list in preparation for the atomic mapping
	 * exchange.
	 */
	if (ip_local) {
		struct xfs_ifork	*ifp;

		ifp = xfs_ifork_ptr(sc->ip, XFS_DATA_FORK);
		xfs_idestroy_fork(ifp);
		ifp->if_format = XFS_DINODE_FMT_EXTENTS;
		ifp->if_nextents = 0;
		ifp->if_bytes = 0;
		ifp->if_data = NULL;
		ifp->if_height = 0;

		xfs_trans_log_inode(sc->tp, sc->ip,
				XFS_ILOG_CORE | XFS_ILOG_DDATA);
	}

	return 0;
}

/*
 * Replace the inode number of a directory entry.
 */
static int
xrep_dir_replace(
	struct xrep_dir		*rd,
	struct xfs_inode	*dp,
	const struct xfs_name	*name,
	xfs_ino_t		inum,
	xfs_extlen_t		total)
{
	struct xfs_scrub	*sc = rd->sc;
	int			error;

	ASSERT(S_ISDIR(VFS_I(dp)->i_mode));

	error = xfs_dir_ino_validate(sc->mp, inum);
	if (error)
		return error;

	xrep_dir_init_args(rd, dp, name);
	rd->args.inumber = inum;
	rd->args.total = total;
	return xfs_dir_replace_args(&rd->args);
}

/*
 * Reset the link count of this directory and adjust the unlinked list pointers
 * as needed.
 */
STATIC int
xrep_dir_set_nlink(
	struct xrep_dir		*rd)
{
	struct xfs_scrub	*sc = rd->sc;
	struct xfs_inode	*dp = sc->ip;
	struct xfs_perag	*pag;
	unsigned int		new_nlink = min_t(unsigned long long,
						  rd->subdirs + 2,
						  XFS_NLINK_PINNED);
	int			error;

	/*
	 * The directory is not on the incore unlinked list, which means that
	 * it needs to be reachable via the directory tree.  Update the nlink
	 * with our observed link count.  If the directory has no parent, it
	 * will be moved to the orphanage.
	 */
	if (!xfs_inode_on_unlinked_list(dp))
		goto reset_nlink;

	/*
	 * The directory is on the unlinked list and we did not find any
	 * dirents.  Set the link count to zero and let the directory
	 * inactivate when the last reference drops.
	 */
	if (rd->dirents == 0) {
		rd->needs_adoption = false;
		new_nlink = 0;
		goto reset_nlink;
	}

	/*
	 * The directory is on the unlinked list and we found dirents.  This
	 * directory needs to be reachable via the directory tree.  Remove the
	 * dir from the unlinked list and update nlink with the observed link
	 * count.  If the directory has no parent, it will be moved to the
	 * orphanage.
	 */
	pag = xfs_perag_get(sc->mp, XFS_INO_TO_AGNO(sc->mp, dp->i_ino));
	if (!pag) {
		ASSERT(0);
		return -EFSCORRUPTED;
	}

	error = xfs_iunlink_remove(sc->tp, pag, dp);
	xfs_perag_put(pag);
	if (error)
		return error;

reset_nlink:
	if (VFS_I(dp)->i_nlink != new_nlink)
		set_nlink(VFS_I(dp), new_nlink);
	return 0;
}

/*
 * Finish replaying stashed dirent updates, allocate a transaction for
 * exchanging data fork mappings, and take the ILOCKs of both directories
 * before we commit the new directory structure.
 */
STATIC int
xrep_dir_finalize_tempdir(
	struct xrep_dir		*rd)
{
	struct xfs_scrub	*sc = rd->sc;
	int			error;

	if (!xfs_has_parent(sc->mp))
		return xrep_tempexch_trans_alloc(sc, XFS_DATA_FORK, &rd->tx);

	/*
	 * Repair relies on the ILOCK to quiesce all possible dirent updates.
	 * Replay all queued dirent updates into the tempdir before exchanging
	 * the contents, even if that means dropping the ILOCKs and the
	 * transaction.
	 */
	do {
		error = xrep_dir_replay_updates(rd);
		if (error)
			return error;

		error = xrep_tempexch_trans_alloc(sc, XFS_DATA_FORK, &rd->tx);
		if (error)
			return error;

		if (xfarray_length(rd->dir_entries) == 0)
			break;

		xchk_trans_cancel(sc);
		xrep_tempfile_iunlock_both(sc);
	} while (!xchk_should_terminate(sc, &error));
	return error;
}

/* Exchange the temporary directory's data fork with the one being repaired. */
STATIC int
xrep_dir_swap(
	struct xrep_dir		*rd)
{
	struct xfs_scrub	*sc = rd->sc;
	bool			ip_local, temp_local;
	int			error = 0;

	/*
	 * If we never found the parent for this directory, temporarily assign
	 * the root dir as the parent; we'll move this to the orphanage after
	 * exchanging the dir contents.  We hold the ILOCK of the dir being
	 * repaired, so we're not worried about racy updates of dotdot.
	 */
	ASSERT(sc->ilock_flags & XFS_ILOCK_EXCL);
	if (rd->pscan.parent_ino == NULLFSINO) {
		rd->needs_adoption = true;
		rd->pscan.parent_ino = rd->sc->mp->m_sb.sb_rootino;
	}

	/*
	 * Reset the temporary directory's '..' entry to point to the parent
	 * that we found.  The temporary directory was created with the root
	 * directory as the parent, so we can skip this if repairing a
	 * subdirectory of the root.
	 *
	 * It's also possible that this replacement could also expand a sf
	 * tempdir into block format.
	 */
	if (rd->pscan.parent_ino != sc->mp->m_rootip->i_ino) {
		error = xrep_dir_replace(rd, rd->sc->tempip, &xfs_name_dotdot,
				rd->pscan.parent_ino, rd->tx.req.resblks);
		if (error)
			return error;
	}

	/*
	 * Changing the dot and dotdot entries could have changed the shape of
	 * the directory, so we recompute these.
	 */
	ip_local = sc->ip->i_df.if_format == XFS_DINODE_FMT_LOCAL;
	temp_local = sc->tempip->i_df.if_format == XFS_DINODE_FMT_LOCAL;

	/*
	 * If the both files have a local format data fork and the rebuilt
	 * directory data would fit in the repaired file's data fork, copy
	 * the contents from the tempfile and update the directory link count.
	 * We're done now.
	 */
	if (ip_local && temp_local &&
	    sc->tempip->i_disk_size <= xfs_inode_data_fork_size(sc->ip)) {
		xrep_tempfile_copyout_local(sc, XFS_DATA_FORK);
		return xrep_dir_set_nlink(rd);
	}

	/*
	 * Clean the transaction before we start working on exchanging
	 * directory contents.
	 */
	error = xrep_tempfile_roll_trans(rd->sc);
	if (error)
		return error;

	/* Otherwise, make sure both data forks are in block-mapping mode. */
	error = xrep_dir_swap_prep(sc, temp_local, ip_local);
	if (error)
		return error;

	/*
	 * Set nlink of the directory in the same transaction sequence that
	 * (atomically) commits the new directory data.
	 */
	error = xrep_dir_set_nlink(rd);
	if (error)
		return error;

	return xrep_tempexch_contents(sc, &rd->tx);
}

/*
 * Exchange the new directory contents (which we created in the tempfile) with
 * the directory being repaired.
 */
STATIC int
xrep_dir_rebuild_tree(
	struct xrep_dir		*rd)
{
	struct xfs_scrub	*sc = rd->sc;
	int			error;

	trace_xrep_dir_rebuild_tree(sc->ip, rd->pscan.parent_ino);

	/*
	 * Take the IOLOCK on the temporary file so that we can run dir
	 * operations with the same locks held as we would for a normal file.
	 * We still hold sc->ip's IOLOCK.
	 */
	error = xrep_tempfile_iolock_polled(rd->sc);
	if (error)
		return error;

	/*
	 * Allocate transaction, lock inodes, and make sure that we've replayed
	 * all the stashed dirent updates to the tempdir.  After this point,
	 * we're ready to exchange data fork mappings.
	 */
	error = xrep_dir_finalize_tempdir(rd);
	if (error)
		return error;

	if (xchk_iscan_aborted(&rd->pscan.iscan))
		return -ECANCELED;

	/*
	 * Exchange the tempdir's data fork with the file being repaired.  This
	 * recreates the transaction and re-takes the ILOCK in the scrub
	 * context.
	 */
	error = xrep_dir_swap(rd);
	if (error)
		return error;

	/*
	 * Release the old directory blocks and reset the data fork of the temp
	 * directory to an empty shortform directory because inactivation does
	 * nothing for directories.
	 */
	error = xrep_dir_reset_fork(rd, sc->mp->m_rootip->i_ino);
	if (error)
		return error;

	/*
	 * Roll to get a transaction without any inodes joined to it.  Then we
	 * can drop the tempfile's ILOCK and IOLOCK before doing more work on
	 * the scrub target directory.
	 */
	error = xfs_trans_roll(&sc->tp);
	if (error)
		return error;

	xrep_tempfile_iunlock(sc);
	xrep_tempfile_iounlock(sc);
	return 0;
}

/* Set up the filesystem scan so we can regenerate directory entries. */
STATIC int
xrep_dir_setup_scan(
	struct xrep_dir		*rd)
{
	struct xfs_scrub	*sc = rd->sc;
	char			*descr;
	int			error;

	/* Set up some staging memory for salvaging dirents. */
	descr = xchk_xfile_ino_descr(sc, "directory entries");
	error = xfarray_create(descr, 0, sizeof(struct xrep_dirent),
			&rd->dir_entries);
	kfree(descr);
	if (error)
		return error;

	descr = xchk_xfile_ino_descr(sc, "directory entry names");
	error = xfblob_create(descr, &rd->dir_names);
	kfree(descr);
	if (error)
		goto out_xfarray;

	if (xfs_has_parent(sc->mp))
		error = __xrep_findparent_scan_start(sc, &rd->pscan,
				xrep_dir_live_update);
	else
		error = xrep_findparent_scan_start(sc, &rd->pscan);
	if (error)
		goto out_xfblob;

	return 0;

out_xfblob:
	xfblob_destroy(rd->dir_names);
	rd->dir_names = NULL;
out_xfarray:
	xfarray_destroy(rd->dir_entries);
	rd->dir_entries = NULL;
	return error;
}

/*
 * Move the current file to the orphanage.
 *
 * Caller must hold IOLOCK_EXCL on @sc->ip, and no other inode locks.  Upon
 * successful return, the scrub transaction will have enough extra reservation
 * to make the move; it will hold IOLOCK_EXCL and ILOCK_EXCL of @sc->ip and the
 * orphanage; and both inodes will be ijoined.
 */
STATIC int
xrep_dir_move_to_orphanage(
	struct xrep_dir		*rd)
{
	struct xfs_scrub	*sc = rd->sc;
	xfs_ino_t		orig_parent, new_parent;
	int			error;

	/*
	 * We are about to drop the ILOCK on sc->ip to lock the orphanage and
	 * prepare for the adoption.  Therefore, look up the old dotdot entry
	 * for sc->ip so that we can compare it after we re-lock sc->ip.
	 */
	error = xchk_dir_lookup(sc, sc->ip, &xfs_name_dotdot, &orig_parent);
	if (error)
		return error;

	/*
	 * Drop the ILOCK on the scrub target and commit the transaction.
	 * Adoption computes its own resource requirements and gathers the
	 * necessary components.
	 */
	error = xrep_trans_commit(sc);
	if (error)
		return error;
	xchk_iunlock(sc, XFS_ILOCK_EXCL);

	/* If we can take the orphanage's iolock then we're ready to move. */
	if (!xrep_orphanage_ilock_nowait(sc, XFS_IOLOCK_EXCL)) {
		xchk_iunlock(sc, sc->ilock_flags);
		error = xrep_orphanage_iolock_two(sc);
		if (error)
			return error;
	}

	/* Grab transaction and ILOCK the two files. */
	error = xrep_adoption_trans_alloc(sc, &rd->adoption);
	if (error)
		return error;

	error = xrep_adoption_compute_name(&rd->adoption, &rd->xname);
	if (error)
		return error;

	/*
	 * Now that we've reacquired the ILOCK on sc->ip, look up the dotdot
	 * entry again.  If the parent changed or the child was unlinked while
	 * the child directory was unlocked, we don't need to move the child to
	 * the orphanage after all.
	 */
	error = xchk_dir_lookup(sc, sc->ip, &xfs_name_dotdot, &new_parent);
	if (error)
		return error;

	/*
	 * Attach to the orphanage if we still have a linked directory and it
	 * hasn't been moved.
	 */
	if (orig_parent == new_parent && VFS_I(sc->ip)->i_nlink > 0) {
		error = xrep_adoption_move(&rd->adoption);
		if (error)
			return error;
	}

	/*
	 * Launder the scrub transaction so we can drop the orphanage ILOCK
	 * and IOLOCK.  Return holding the scrub target's ILOCK and IOLOCK.
	 */
	error = xrep_adoption_trans_roll(&rd->adoption);
	if (error)
		return error;

	xrep_orphanage_iunlock(sc, XFS_ILOCK_EXCL);
	xrep_orphanage_iunlock(sc, XFS_IOLOCK_EXCL);
	return 0;
}

/*
 * Repair the directory metadata.
 *
 * XXX: Directory entry buffers can be multiple fsblocks in size.  The buffer
 * cache in XFS can't handle aliased multiblock buffers, so this might
 * misbehave if the directory blocks are crosslinked with other filesystem
 * metadata.
 *
 * XXX: Is it necessary to check the dcache for this directory to make sure
 * that we always recreate every cached entry?
 */
int
xrep_directory(
	struct xfs_scrub	*sc)
{
	struct xrep_dir		*rd = sc->buf;
	int			error;

	/* The rmapbt is required to reap the old data fork. */
	if (!xfs_has_rmapbt(sc->mp))
		return -EOPNOTSUPP;
	/* We require atomic file exchange range to rebuild anything. */
	if (!xfs_has_exchange_range(sc->mp))
		return -EOPNOTSUPP;

	error = xrep_dir_setup_scan(rd);
	if (error)
		return error;

	if (xfs_has_parent(sc->mp))
		error = xrep_dir_scan_dirtree(rd);
	else
		error = xrep_dir_salvage_entries(rd);
	if (error)
		goto out_teardown;

	/* Last chance to abort before we start committing fixes. */
	if (xchk_should_terminate(sc, &error))
		goto out_teardown;

	error = xrep_dir_rebuild_tree(rd);
	if (error)
		goto out_teardown;

	if (rd->needs_adoption) {
		if (!xrep_orphanage_can_adopt(rd->sc))
			error = -EFSCORRUPTED;
		else
			error = xrep_dir_move_to_orphanage(rd);
		if (error)
			goto out_teardown;
	}

out_teardown:
	xrep_dir_teardown(sc);
	return error;
}
