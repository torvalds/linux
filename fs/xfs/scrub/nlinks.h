/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Copyright (c) 2021-2024 Oracle.  All Rights Reserved.
 * Author: Darrick J. Wong <djwong@kernel.org>
 */
#ifndef __XFS_SCRUB_NLINKS_H__
#define __XFS_SCRUB_NLINKS_H__

/* Live link count control structure. */
struct xchk_nlink_ctrs {
	struct xfs_scrub	*sc;

	/* Shadow link count data and its mutex. */
	struct xfarray		*nlinks;
	struct mutex		lock;

	/*
	 * The collection step uses a separate iscan context from the compare
	 * step because the collection iscan coordinates live updates to the
	 * observation data while this scanner is running.  The compare iscan
	 * is secondary and can be reinitialized as needed.
	 */
	struct xchk_iscan	collect_iscan;
	struct xchk_iscan	compare_iscan;

	/*
	 * Hook into directory updates so that we can receive live updates
	 * from other writer threads.
	 */
	struct xfs_dir_hook	dhook;

	/* Orphanage reparenting request. */
	struct xrep_adoption	adoption;

	/* Directory entry name, plus the trailing null. */
	struct xfs_name		xname;
	char			namebuf[MAXNAMELEN];
};

/*
 * In-core link counts for a given inode in the filesystem.
 *
 * For an empty rootdir, the directory entries and the field to which they are
 * accounted are as follows:
 *
 * Root directory:
 *
 * . points to self		(root.child)
 * .. points to self		(root.parent)
 * f1 points to a child file	(f1.parent)
 * d1 points to a child dir	(d1.parent, root.child)
 *
 * Subdirectory d1:
 *
 * . points to self		(d1.child)
 * .. points to root dir	(root.backref)
 * f2 points to child file	(f2.parent)
 * f3 points to root.f1		(f1.parent)
 *
 * root.nlink == 3 (root.dot, root.dotdot, root.d1)
 * d1.nlink == 2 (root.d1, d1.dot)
 * f1.nlink == 2 (root.f1, d1.f3)
 * f2.nlink == 1 (d1.f2)
 */
struct xchk_nlink {
	/* Count of forward links from parent directories to this file. */
	xfs_nlink_t		parents;

	/*
	 * Count of back links to this parent directory from child
	 * subdirectories.
	 */
	xfs_nlink_t		backrefs;

	/*
	 * Count of forward links from this directory to all child files and
	 * the number of dot entries.  Should be zero for non-directories.
	 */
	xfs_nlink_t		children;

	/* Record state flags */
	unsigned int		flags;
};

/*
 * This incore link count has been written at least once.  We never want to
 * store an xchk_nlink that looks uninitialized.
 */
#define XCHK_NLINK_WRITTEN		(1U << 0)

/* Already checked this link count record. */
#define XCHK_NLINK_COMPARE_SCANNED	(1U << 1)

/* Already made a repair with this link count record. */
#define XREP_NLINK_DIRTY		(1U << 2)

/* Compute total link count, using large enough variables to detect overflow. */
static inline uint64_t
xchk_nlink_total(struct xfs_inode *ip, const struct xchk_nlink *live)
{
	uint64_t	ret = live->parents;

	/* Add one link count for the dot entry of any linked directory. */
	if (ip && S_ISDIR(VFS_I(ip)->i_mode) && VFS_I(ip)->i_nlink)
		ret++;
	return ret + live->children;
}

#endif /* __XFS_SCRUB_NLINKS_H__ */
