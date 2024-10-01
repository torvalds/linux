// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2023 Oracle.  All Rights Reserved.
 * Author: Darrick J. Wong <djwong@kernel.org>
 */
#ifndef __XFS_SCRUB_STATS_H__
#define __XFS_SCRUB_STATS_H__

struct xchk_stats_run {
	u64			scrub_ns;
	u64			repair_ns;
	unsigned int		retries;
	bool			repair_attempted;
	bool			repair_succeeded;
};

#ifdef CONFIG_XFS_ONLINE_SCRUB_STATS
struct xchk_stats;

int __init xchk_global_stats_setup(struct dentry *parent);
void xchk_global_stats_teardown(void);

int xchk_mount_stats_alloc(struct xfs_mount *mp);
void xchk_mount_stats_free(struct xfs_mount *mp);

void xchk_stats_register(struct xchk_stats *cs, struct dentry *parent);
void xchk_stats_unregister(struct xchk_stats *cs);

void xchk_stats_merge(struct xfs_mount *mp, const struct xfs_scrub_metadata *sm,
		const struct xchk_stats_run *run);

static inline u64 xchk_stats_now(void) { return ktime_get_ns(); }
static inline u64 xchk_stats_elapsed_ns(u64 since)
{
	u64 now = xchk_stats_now();

	/*
	 * If the system doesn't have a high enough resolution clock, charge at
	 * least one nanosecond so that our stats don't report instantaneous
	 * runtimes.
	 */
	if (now == since)
		return 1;

	return now - since;
}
#else
# define xchk_global_stats_setup(parent)	(0)
# define xchk_global_stats_teardown()		((void)0)
# define xchk_mount_stats_alloc(mp)		(0)
# define xchk_mount_stats_free(mp)		((void)0)
# define xchk_stats_register(cs, parent)	((void)0)
# define xchk_stats_unregister(cs)		((void)0)
# define xchk_stats_now()			(0)
# define xchk_stats_elapsed_ns(x)		(0 * (x))
# define xchk_stats_merge(mp, sm, run)		((void)0)
#endif /* CONFIG_XFS_ONLINE_SCRUB_STATS */

#endif /* __XFS_SCRUB_STATS_H__ */
