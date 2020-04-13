// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2000-2003,2005 Silicon Graphics, Inc.
 * All Rights Reserved.
 */
#include "xfs.h"

struct xstats xfsstats;

static int counter_val(struct xfsstats __percpu *stats, int idx)
{
	int val = 0, cpu;

	for_each_possible_cpu(cpu)
		val += *(((__u32 *)per_cpu_ptr(stats, cpu) + idx));
	return val;
}

int xfs_stats_format(struct xfsstats __percpu *stats, char *buf)
{
	int		i, j;
	int		len = 0;
	uint64_t	xs_xstrat_bytes = 0;
	uint64_t	xs_write_bytes = 0;
	uint64_t	xs_read_bytes = 0;

	static const struct xstats_entry {
		char	*desc;
		int	endpoint;
	} xstats[] = {
		{ "extent_alloc",	xfsstats_offset(xs_abt_lookup)	},
		{ "abt",		xfsstats_offset(xs_blk_mapr)	},
		{ "blk_map",		xfsstats_offset(xs_bmbt_lookup)	},
		{ "bmbt",		xfsstats_offset(xs_dir_lookup)	},
		{ "dir",		xfsstats_offset(xs_trans_sync)	},
		{ "trans",		xfsstats_offset(xs_ig_attempts)	},
		{ "ig",			xfsstats_offset(xs_log_writes)	},
		{ "log",		xfsstats_offset(xs_try_logspace)},
		{ "push_ail",		xfsstats_offset(xs_xstrat_quick)},
		{ "xstrat",		xfsstats_offset(xs_write_calls)	},
		{ "rw",			xfsstats_offset(xs_attr_get)	},
		{ "attr",		xfsstats_offset(xs_iflush_count)},
		{ "icluster",		xfsstats_offset(vn_active)	},
		{ "vnodes",		xfsstats_offset(xb_get)		},
		{ "buf",		xfsstats_offset(xs_abtb_2)	},
		{ "abtb2",		xfsstats_offset(xs_abtc_2)	},
		{ "abtc2",		xfsstats_offset(xs_bmbt_2)	},
		{ "bmbt2",		xfsstats_offset(xs_ibt_2)	},
		{ "ibt2",		xfsstats_offset(xs_fibt_2)	},
		{ "fibt2",		xfsstats_offset(xs_rmap_2)	},
		{ "rmapbt",		xfsstats_offset(xs_refcbt_2)	},
		{ "refcntbt",		xfsstats_offset(xs_qm_dqreclaims)},
		/* we print both series of quota information together */
		{ "qm",			xfsstats_offset(xs_xstrat_bytes)},
	};

	/* Loop over all stats groups */

	for (i = j = 0; i < ARRAY_SIZE(xstats); i++) {
		len += scnprintf(buf + len, PATH_MAX - len, "%s",
				xstats[i].desc);
		/* inner loop does each group */
		for (; j < xstats[i].endpoint; j++)
			len += scnprintf(buf + len, PATH_MAX - len, " %u",
					counter_val(stats, j));
		len += scnprintf(buf + len, PATH_MAX - len, "\n");
	}
	/* extra precision counters */
	for_each_possible_cpu(i) {
		xs_xstrat_bytes += per_cpu_ptr(stats, i)->s.xs_xstrat_bytes;
		xs_write_bytes += per_cpu_ptr(stats, i)->s.xs_write_bytes;
		xs_read_bytes += per_cpu_ptr(stats, i)->s.xs_read_bytes;
	}

	len += scnprintf(buf + len, PATH_MAX-len, "xpc %Lu %Lu %Lu\n",
			xs_xstrat_bytes, xs_write_bytes, xs_read_bytes);
	len += scnprintf(buf + len, PATH_MAX-len, "debug %u\n",
#if defined(DEBUG)
		1);
#else
		0);
#endif

	return len;
}

void xfs_stats_clearall(struct xfsstats __percpu *stats)
{
	int		c;
	uint32_t	vn_active;

	xfs_notice(NULL, "Clearing xfsstats");
	for_each_possible_cpu(c) {
		preempt_disable();
		/* save vn_active, it's a universal truth! */
		vn_active = per_cpu_ptr(stats, c)->s.vn_active;
		memset(per_cpu_ptr(stats, c), 0, sizeof(*stats));
		per_cpu_ptr(stats, c)->s.vn_active = vn_active;
		preempt_enable();
	}
}

#ifdef CONFIG_PROC_FS
/* legacy quota interfaces */
#ifdef CONFIG_XFS_QUOTA

#define XFSSTAT_START_XQMSTAT xfsstats_offset(xs_qm_dqreclaims)
#define XFSSTAT_END_XQMSTAT xfsstats_offset(xs_qm_dquot)

static int xqm_proc_show(struct seq_file *m, void *v)
{
	/* maximum; incore; ratio free to inuse; freelist */
	seq_printf(m, "%d\t%d\t%d\t%u\n",
		   0, counter_val(xfsstats.xs_stats, XFSSTAT_END_XQMSTAT),
		   0, counter_val(xfsstats.xs_stats, XFSSTAT_END_XQMSTAT + 1));
	return 0;
}

/* legacy quota stats interface no 2 */
static int xqmstat_proc_show(struct seq_file *m, void *v)
{
	int j;

	seq_printf(m, "qm");
	for (j = XFSSTAT_START_XQMSTAT; j < XFSSTAT_END_XQMSTAT; j++)
		seq_printf(m, " %u", counter_val(xfsstats.xs_stats, j));
	seq_putc(m, '\n');
	return 0;
}
#endif /* CONFIG_XFS_QUOTA */

int
xfs_init_procfs(void)
{
	if (!proc_mkdir("fs/xfs", NULL))
		return -ENOMEM;

	if (!proc_symlink("fs/xfs/stat", NULL,
			  "/sys/fs/xfs/stats/stats"))
		goto out;

#ifdef CONFIG_XFS_QUOTA
	if (!proc_create_single("fs/xfs/xqmstat", 0, NULL, xqmstat_proc_show))
		goto out;
	if (!proc_create_single("fs/xfs/xqm", 0, NULL, xqm_proc_show))
		goto out;
#endif
	return 0;

out:
	remove_proc_subtree("fs/xfs", NULL);
	return -ENOMEM;
}

void
xfs_cleanup_procfs(void)
{
	remove_proc_subtree("fs/xfs", NULL);
}
#endif /* CONFIG_PROC_FS */
