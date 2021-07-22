// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2001-2005 Silicon Graphics, Inc.
 * All Rights Reserved.
 */
#include "xfs.h"
#include "xfs_error.h"

static struct ctl_table_header *xfs_table_header;

#ifdef CONFIG_PROC_FS
STATIC int
xfs_stats_clear_proc_handler(
	struct ctl_table	*ctl,
	int			write,
	void			*buffer,
	size_t			*lenp,
	loff_t			*ppos)
{
	int		ret, *valp = ctl->data;

	ret = proc_dointvec_minmax(ctl, write, buffer, lenp, ppos);

	if (!ret && write && *valp) {
		xfs_stats_clearall(xfsstats.xs_stats);
		xfs_stats_clear = 0;
	}

	return ret;
}

STATIC int
xfs_panic_mask_proc_handler(
	struct ctl_table	*ctl,
	int			write,
	void			*buffer,
	size_t			*lenp,
	loff_t			*ppos)
{
	int		ret, *valp = ctl->data;

	ret = proc_dointvec_minmax(ctl, write, buffer, lenp, ppos);
	if (!ret && write) {
		xfs_panic_mask = *valp;
#ifdef DEBUG
		xfs_panic_mask |= (XFS_PTAG_SHUTDOWN_CORRUPT | XFS_PTAG_LOGRES);
#endif
	}
	return ret;
}
#endif /* CONFIG_PROC_FS */

STATIC int
xfs_deprecated_dointvec_minmax(
	struct ctl_table	*ctl,
	int			write,
	void			*buffer,
	size_t			*lenp,
	loff_t			*ppos)
{
	if (write) {
		printk_ratelimited(KERN_WARNING
				"XFS: %s sysctl option is deprecated.\n",
				ctl->procname);
	}
	return proc_dointvec_minmax(ctl, write, buffer, lenp, ppos);
}

static struct ctl_table xfs_table[] = {
	{
		.procname	= "irix_sgid_inherit",
		.data		= &xfs_params.sgid_inherit.val,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= xfs_deprecated_dointvec_minmax,
		.extra1		= &xfs_params.sgid_inherit.min,
		.extra2		= &xfs_params.sgid_inherit.max
	},
	{
		.procname	= "irix_symlink_mode",
		.data		= &xfs_params.symlink_mode.val,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= xfs_deprecated_dointvec_minmax,
		.extra1		= &xfs_params.symlink_mode.min,
		.extra2		= &xfs_params.symlink_mode.max
	},
	{
		.procname	= "panic_mask",
		.data		= &xfs_params.panic_mask.val,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= xfs_panic_mask_proc_handler,
		.extra1		= &xfs_params.panic_mask.min,
		.extra2		= &xfs_params.panic_mask.max
	},

	{
		.procname	= "error_level",
		.data		= &xfs_params.error_level.val,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec_minmax,
		.extra1		= &xfs_params.error_level.min,
		.extra2		= &xfs_params.error_level.max
	},
	{
		.procname	= "xfssyncd_centisecs",
		.data		= &xfs_params.syncd_timer.val,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec_minmax,
		.extra1		= &xfs_params.syncd_timer.min,
		.extra2		= &xfs_params.syncd_timer.max
	},
	{
		.procname	= "inherit_sync",
		.data		= &xfs_params.inherit_sync.val,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec_minmax,
		.extra1		= &xfs_params.inherit_sync.min,
		.extra2		= &xfs_params.inherit_sync.max
	},
	{
		.procname	= "inherit_nodump",
		.data		= &xfs_params.inherit_nodump.val,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec_minmax,
		.extra1		= &xfs_params.inherit_nodump.min,
		.extra2		= &xfs_params.inherit_nodump.max
	},
	{
		.procname	= "inherit_noatime",
		.data		= &xfs_params.inherit_noatim.val,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec_minmax,
		.extra1		= &xfs_params.inherit_noatim.min,
		.extra2		= &xfs_params.inherit_noatim.max
	},
	{
		.procname	= "inherit_nosymlinks",
		.data		= &xfs_params.inherit_nosym.val,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec_minmax,
		.extra1		= &xfs_params.inherit_nosym.min,
		.extra2		= &xfs_params.inherit_nosym.max
	},
	{
		.procname	= "rotorstep",
		.data		= &xfs_params.rotorstep.val,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec_minmax,
		.extra1		= &xfs_params.rotorstep.min,
		.extra2		= &xfs_params.rotorstep.max
	},
	{
		.procname	= "inherit_nodefrag",
		.data		= &xfs_params.inherit_nodfrg.val,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec_minmax,
		.extra1		= &xfs_params.inherit_nodfrg.min,
		.extra2		= &xfs_params.inherit_nodfrg.max
	},
	{
		.procname	= "filestream_centisecs",
		.data		= &xfs_params.fstrm_timer.val,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec_minmax,
		.extra1		= &xfs_params.fstrm_timer.min,
		.extra2		= &xfs_params.fstrm_timer.max,
	},
	{
		.procname	= "speculative_prealloc_lifetime",
		.data		= &xfs_params.blockgc_timer.val,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec_minmax,
		.extra1		= &xfs_params.blockgc_timer.min,
		.extra2		= &xfs_params.blockgc_timer.max,
	},
	{
		.procname	= "speculative_cow_prealloc_lifetime",
		.data		= &xfs_params.blockgc_timer.val,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= xfs_deprecated_dointvec_minmax,
		.extra1		= &xfs_params.blockgc_timer.min,
		.extra2		= &xfs_params.blockgc_timer.max,
	},
	/* please keep this the last entry */
#ifdef CONFIG_PROC_FS
	{
		.procname	= "stats_clear",
		.data		= &xfs_params.stats_clear.val,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= xfs_stats_clear_proc_handler,
		.extra1		= &xfs_params.stats_clear.min,
		.extra2		= &xfs_params.stats_clear.max
	},
#endif /* CONFIG_PROC_FS */

	{}
};

static struct ctl_table xfs_dir_table[] = {
	{
		.procname	= "xfs",
		.mode		= 0555,
		.child		= xfs_table
	},
	{}
};

static struct ctl_table xfs_root_table[] = {
	{
		.procname	= "fs",
		.mode		= 0555,
		.child		= xfs_dir_table
	},
	{}
};

int
xfs_sysctl_register(void)
{
	xfs_table_header = register_sysctl_table(xfs_root_table);
	if (!xfs_table_header)
		return -ENOMEM;
	return 0;
}

void
xfs_sysctl_unregister(void)
{
	unregister_sysctl_table(xfs_table_header);
}
