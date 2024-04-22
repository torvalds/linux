/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Copyright (c) 2020-2024 Oracle.  All Rights Reserved.
 * Author: Darrick J. Wong <djwong@kernel.org>
 */
#ifndef __XFS_SCRUB_FINDPARENT_H__
#define __XFS_SCRUB_FINDPARENT_H__

struct xrep_parent_scan_info {
	struct xfs_scrub	*sc;

	/* Inode scan cursor. */
	struct xchk_iscan	iscan;

	/* Hook to capture directory entry updates. */
	struct xfs_dir_hook	dhook;

	/* Lock protecting parent_ino. */
	struct mutex		lock;

	/* Parent inode that we've found. */
	xfs_ino_t		parent_ino;

	bool			lookup_parent;
};

int __xrep_findparent_scan_start(struct xfs_scrub *sc,
		struct xrep_parent_scan_info *pscan,
		notifier_fn_t custom_fn);
static inline int xrep_findparent_scan_start(struct xfs_scrub *sc,
		struct xrep_parent_scan_info *pscan)
{
	return __xrep_findparent_scan_start(sc, pscan, NULL);
}
int xrep_findparent_scan(struct xrep_parent_scan_info *pscan);
void xrep_findparent_scan_teardown(struct xrep_parent_scan_info *pscan);

static inline void
xrep_findparent_scan_found(
	struct xrep_parent_scan_info	*pscan,
	xfs_ino_t			ino)
{
	mutex_lock(&pscan->lock);
	pscan->parent_ino = ino;
	mutex_unlock(&pscan->lock);
}

void xrep_findparent_scan_finish_early(struct xrep_parent_scan_info *pscan,
		xfs_ino_t ino);

int xrep_findparent_confirm(struct xfs_scrub *sc, xfs_ino_t *parent_ino);

xfs_ino_t xrep_findparent_self_reference(struct xfs_scrub *sc);
xfs_ino_t xrep_findparent_from_dcache(struct xfs_scrub *sc);

#endif /* __XFS_SCRUB_FINDPARENT_H__ */
