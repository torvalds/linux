// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (c) 2021-2024 Oracle.  All Rights Reserved.
 * Author: Darrick J. Wong <djwong@kernel.org>
 */
#ifndef __XFS_SCRUB_TEMPFILE_H__
#define __XFS_SCRUB_TEMPFILE_H__

#ifdef CONFIG_XFS_ONLINE_REPAIR
int xrep_tempfile_create(struct xfs_scrub *sc, uint16_t mode);
void xrep_tempfile_rele(struct xfs_scrub *sc);

int xrep_tempfile_adjust_directory_tree(struct xfs_scrub *sc);

bool xrep_tempfile_iolock_nowait(struct xfs_scrub *sc);
int xrep_tempfile_iolock_polled(struct xfs_scrub *sc);
void xrep_tempfile_iounlock(struct xfs_scrub *sc);

void xrep_tempfile_ilock(struct xfs_scrub *sc);
bool xrep_tempfile_ilock_nowait(struct xfs_scrub *sc);
void xrep_tempfile_iunlock(struct xfs_scrub *sc);
void xrep_tempfile_iunlock_both(struct xfs_scrub *sc);
void xrep_tempfile_ilock_both(struct xfs_scrub *sc);

int xrep_tempfile_prealloc(struct xfs_scrub *sc, xfs_fileoff_t off,
		xfs_filblks_t len);

enum xfs_blft;

typedef int (*xrep_tempfile_copyin_fn)(struct xfs_scrub *sc,
		struct xfs_buf *bp, void *data);

int xrep_tempfile_copyin(struct xfs_scrub *sc, xfs_fileoff_t off,
		xfs_filblks_t len, xrep_tempfile_copyin_fn fn, void *data);

int xrep_tempfile_set_isize(struct xfs_scrub *sc, unsigned long long isize);

int xrep_tempfile_roll_trans(struct xfs_scrub *sc);
void xrep_tempfile_copyout_local(struct xfs_scrub *sc, int whichfork);
bool xrep_is_tempfile(const struct xfs_inode *ip);
#else
static inline void xrep_tempfile_iolock_both(struct xfs_scrub *sc)
{
	xchk_ilock(sc, XFS_IOLOCK_EXCL);
}
# define xrep_is_tempfile(ip)		(false)
# define xrep_tempfile_adjust_directory_tree(sc)	(0)
# define xrep_tempfile_rele(sc)
#endif /* CONFIG_XFS_ONLINE_REPAIR */

#endif /* __XFS_SCRUB_TEMPFILE_H__ */
