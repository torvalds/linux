// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (c) 2021-2024 Oracle.  All Rights Reserved.
 * Author: Darrick J. Wong <djwong@kernel.org>
 */
#ifndef __XFS_SCRUB_ORPHANAGE_H__
#define __XFS_SCRUB_ORPHANAGE_H__

#ifdef CONFIG_XFS_ONLINE_REPAIR
int xrep_orphanage_create(struct xfs_scrub *sc);

/*
 * If we're doing a repair, ensure that the orphanage exists and attach it to
 * the scrub context.
 */
static inline int
xrep_orphanage_try_create(
	struct xfs_scrub	*sc)
{
	int			error;

	ASSERT(sc->sm->sm_flags & XFS_SCRUB_IFLAG_REPAIR);

	error = xrep_orphanage_create(sc);
	switch (error) {
	case 0:
	case -ENOENT:
	case -ENOTDIR:
	case -ENOSPC:
		/*
		 * If the orphanage can't be found or isn't a directory, we'll
		 * keep going, but we won't be able to attach the file to the
		 * orphanage if we can't find the parent.
		 */
		return 0;
	}

	return error;
}

int xrep_orphanage_iolock_two(struct xfs_scrub *sc);

void xrep_orphanage_ilock(struct xfs_scrub *sc, unsigned int ilock_flags);
bool xrep_orphanage_ilock_nowait(struct xfs_scrub *sc,
		unsigned int ilock_flags);
void xrep_orphanage_iunlock(struct xfs_scrub *sc, unsigned int ilock_flags);

void xrep_orphanage_rele(struct xfs_scrub *sc);

/* Information about a request to add a file to the orphanage. */
struct xrep_adoption {
	struct xfs_scrub	*sc;

	/* Name used for the adoption. */
	struct xfs_name		*xname;

	/* Parent pointer context tracking */
	struct xfs_parent_args	ppargs;

	/* Block reservations for orphanage and child (if directory). */
	unsigned int		orphanage_blkres;
	unsigned int		child_blkres;

	/*
	 * Does the caller want us to bump the child link count?  This is not
	 * needed when reattaching files that have become disconnected but have
	 * nlink > 1.  It is necessary when changing the directory tree
	 * structure.
	 */
	bool			bump_child_nlink:1;
};

bool xrep_orphanage_can_adopt(struct xfs_scrub *sc);

int xrep_adoption_trans_alloc(struct xfs_scrub *sc,
		struct xrep_adoption *adopt);
int xrep_adoption_compute_name(struct xrep_adoption *adopt,
		struct xfs_name *xname);
int xrep_adoption_move(struct xrep_adoption *adopt);
int xrep_adoption_trans_roll(struct xrep_adoption *adopt);
#else
struct xrep_adoption { /* empty */ };
# define xrep_orphanage_rele(sc)	((void)0)
#endif /* CONFIG_XFS_ONLINE_REPAIR */

#endif /* __XFS_SCRUB_ORPHANAGE_H__ */
