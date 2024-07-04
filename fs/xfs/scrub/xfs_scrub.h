// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2017-2023 Oracle.  All Rights Reserved.
 * Author: Darrick J. Wong <djwong@kernel.org>
 */
#ifndef __XFS_SCRUB_H__
#define __XFS_SCRUB_H__

#ifndef CONFIG_XFS_ONLINE_SCRUB
# define xfs_ioc_scrub_metadata(f, a)	(-ENOTTY)
# define xfs_ioc_scrubv_metadata(f, a)	(-ENOTTY)
#else
int xfs_ioc_scrub_metadata(struct file *file, void __user *arg);
int xfs_ioc_scrubv_metadata(struct file *file, void __user *arg);
#endif /* CONFIG_XFS_ONLINE_SCRUB */

#endif	/* __XFS_SCRUB_H__ */
