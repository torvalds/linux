/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Copyright (c) 2023-2024 Oracle.  All Rights Reserved.
 * Author: Darrick J. Wong <djwong@kernel.org>
 */
#ifndef __XFS_SCRUB_DIRTREE_H__
#define __XFS_SCRUB_DIRTREE_H__

/*
 * Each of these represents one parent pointer path step in a chain going
 * up towards the directory tree root.  These are stored inside an xfarray.
 */
struct xchk_dirpath_step {
	/* Directory entry name associated with this parent link. */
	xfblob_cookie		name_cookie;
	unsigned int		name_len;

	/* Handle of the parent directory. */
	struct xfs_parent_rec	pptr_rec;
};

enum xchk_dirpath_outcome {
	XCHK_DIRPATH_SCANNING = 0,	/* still being put together */
	XCHK_DIRPATH_DELETE,		/* delete this path */
	XCHK_DIRPATH_CORRUPT,		/* corruption detected in path */
	XCHK_DIRPATH_LOOP,		/* cycle detected further up */
	XCHK_DIRPATH_STALE,		/* path is stale */
	XCHK_DIRPATH_OK,		/* path reaches the root */
};

/*
 * Each of these represents one parent pointer path out of the directory being
 * scanned.  These exist in-core, and hopefully there aren't more than a
 * handful of them.
 */
struct xchk_dirpath {
	struct list_head	list;

	/* Index of the first step in this path. */
	xfarray_idx_t		first_step;

	/* Index of the second step in this path. */
	xfarray_idx_t		second_step;

	/* Inodes seen while walking this path. */
	struct xino_bitmap	seen_inodes;

	/* Number of steps in this path. */
	unsigned int		nr_steps;

	/* Which path is this? */
	unsigned int		path_nr;

	/* What did we conclude from following this path? */
	enum xchk_dirpath_outcome outcome;
};

struct xchk_dirtree_outcomes {
	/* Number of XCHK_DIRPATH_DELETE */
	unsigned int		bad;

	/* Number of XCHK_DIRPATH_CORRUPT or XCHK_DIRPATH_LOOP */
	unsigned int		suspect;

	/* Number of XCHK_DIRPATH_OK */
	unsigned int		good;
};

struct xchk_dirtree {
	struct xfs_scrub	*sc;

	/* Root inode that we're looking for. */
	xfs_ino_t		root_ino;

	/* Scratch buffer for scanning pptr xattrs */
	struct xfs_parent_rec	pptr_rec;
	struct xfs_da_args	pptr_args;

	/* Name buffer */
	struct xfs_name		xname;
	char			namebuf[MAXNAMELEN];

	/* lock for everything below here */
	struct mutex		lock;

	/*
	 * All path steps observed during this scan.  Each of the path
	 * steps for a particular pathwalk are recorded in sequential
	 * order in the xfarray.  A pathwalk ends either with a step
	 * pointing to the root directory (success) or pointing to NULLFSINO
	 * (loop detected, empty dir detected, etc).
	 */
	struct xfarray		*path_steps;

	/* All names observed during this scan. */
	struct xfblob		*path_names;

	/* All paths being tracked by this scanner. */
	struct list_head	path_list;

	/* Number of paths in path_list. */
	unsigned int		nr_paths;

	/* Number of parents found by a pptr scan. */
	unsigned int		parents_found;

	/* Have the path data been invalidated by a concurrent update? */
	bool			stale:1;
};

#define xchk_dirtree_for_each_path_safe(dl, path, n) \
	list_for_each_entry_safe((path), (n), &(dl)->path_list, list)

#define xchk_dirtree_for_each_path(dl, path) \
	list_for_each_entry((path), &(dl)->path_list, list)

static inline bool
xchk_dirtree_parentless(const struct xchk_dirtree *dl)
{
	struct xfs_scrub	*sc = dl->sc;

	if (sc->ip == sc->mp->m_rootip)
		return true;
	if (VFS_I(sc->ip)->i_nlink == 0)
		return true;
	return false;
}

#endif /* __XFS_SCRUB_DIRTREE_H__ */
