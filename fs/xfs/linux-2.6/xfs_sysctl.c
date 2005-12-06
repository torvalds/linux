/*
 * Copyright (c) 2001-2005 Silicon Graphics, Inc.
 * All Rights Reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it would be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write the Free Software Foundation,
 * Inc.,  51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */
#include "xfs.h"
#include <linux/sysctl.h>
#include <linux/proc_fs.h>

static struct ctl_table_header *xfs_table_header;

#ifdef CONFIG_PROC_FS
STATIC int
xfs_stats_clear_proc_handler(
	ctl_table	*ctl,
	int		write,
	struct file	*filp,
	void		__user *buffer,
	size_t		*lenp,
	loff_t		*ppos)
{
	int		c, ret, *valp = ctl->data;
	__uint32_t	vn_active;

	ret = proc_dointvec_minmax(ctl, write, filp, buffer, lenp, ppos);

	if (!ret && write && *valp) {
		printk("XFS Clearing xfsstats\n");
		for (c = 0; c < NR_CPUS; c++) {
			if (!cpu_possible(c)) continue;
			preempt_disable();
			/* save vn_active, it's a universal truth! */
			vn_active = per_cpu(xfsstats, c).vn_active;
			memset(&per_cpu(xfsstats, c), 0,
			       sizeof(struct xfsstats));
			per_cpu(xfsstats, c).vn_active = vn_active;
			preempt_enable();
		}
		xfs_stats_clear = 0;
	}

	return ret;
}
#endif /* CONFIG_PROC_FS */

STATIC ctl_table xfs_table[] = {
	{XFS_RESTRICT_CHOWN, "restrict_chown", &xfs_params.restrict_chown.val,
	sizeof(int), 0644, NULL, &proc_dointvec_minmax,
	&sysctl_intvec, NULL,
	&xfs_params.restrict_chown.min, &xfs_params.restrict_chown.max},

	{XFS_SGID_INHERIT, "irix_sgid_inherit", &xfs_params.sgid_inherit.val,
	sizeof(int), 0644, NULL, &proc_dointvec_minmax,
	&sysctl_intvec, NULL,
	&xfs_params.sgid_inherit.min, &xfs_params.sgid_inherit.max},

	{XFS_SYMLINK_MODE, "irix_symlink_mode", &xfs_params.symlink_mode.val,
	sizeof(int), 0644, NULL, &proc_dointvec_minmax,
	&sysctl_intvec, NULL,
	&xfs_params.symlink_mode.min, &xfs_params.symlink_mode.max},

	{XFS_PANIC_MASK, "panic_mask", &xfs_params.panic_mask.val,
	sizeof(int), 0644, NULL, &proc_dointvec_minmax,
	&sysctl_intvec, NULL,
	&xfs_params.panic_mask.min, &xfs_params.panic_mask.max},

	{XFS_ERRLEVEL, "error_level", &xfs_params.error_level.val,
	sizeof(int), 0644, NULL, &proc_dointvec_minmax,
	&sysctl_intvec, NULL,
	&xfs_params.error_level.min, &xfs_params.error_level.max},

	{XFS_SYNCD_TIMER, "xfssyncd_centisecs", &xfs_params.syncd_timer.val,
	sizeof(int), 0644, NULL, &proc_dointvec_minmax,
	&sysctl_intvec, NULL,
	&xfs_params.syncd_timer.min, &xfs_params.syncd_timer.max},

	{XFS_INHERIT_SYNC, "inherit_sync", &xfs_params.inherit_sync.val,
	sizeof(int), 0644, NULL, &proc_dointvec_minmax,
	&sysctl_intvec, NULL,
	&xfs_params.inherit_sync.min, &xfs_params.inherit_sync.max},

	{XFS_INHERIT_NODUMP, "inherit_nodump", &xfs_params.inherit_nodump.val,
	sizeof(int), 0644, NULL, &proc_dointvec_minmax,
	&sysctl_intvec, NULL,
	&xfs_params.inherit_nodump.min, &xfs_params.inherit_nodump.max},

	{XFS_INHERIT_NOATIME, "inherit_noatime", &xfs_params.inherit_noatim.val,
	sizeof(int), 0644, NULL, &proc_dointvec_minmax,
	&sysctl_intvec, NULL,
	&xfs_params.inherit_noatim.min, &xfs_params.inherit_noatim.max},

	{XFS_BUF_TIMER, "xfsbufd_centisecs", &xfs_params.xfs_buf_timer.val,
	sizeof(int), 0644, NULL, &proc_dointvec_minmax,
	&sysctl_intvec, NULL,
	&xfs_params.xfs_buf_timer.min, &xfs_params.xfs_buf_timer.max},

	{XFS_BUF_AGE, "age_buffer_centisecs", &xfs_params.xfs_buf_age.val,
	sizeof(int), 0644, NULL, &proc_dointvec_minmax,
	&sysctl_intvec, NULL,
	&xfs_params.xfs_buf_age.min, &xfs_params.xfs_buf_age.max},

	{XFS_INHERIT_NOSYM, "inherit_nosymlinks", &xfs_params.inherit_nosym.val,
	sizeof(int), 0644, NULL, &proc_dointvec_minmax,
	&sysctl_intvec, NULL,
	&xfs_params.inherit_nosym.min, &xfs_params.inherit_nosym.max},

	{XFS_ROTORSTEP, "rotorstep", &xfs_params.rotorstep.val,
	sizeof(int), 0644, NULL, &proc_dointvec_minmax,
	&sysctl_intvec, NULL,
	&xfs_params.rotorstep.min, &xfs_params.rotorstep.max},

	/* please keep this the last entry */
#ifdef CONFIG_PROC_FS
	{XFS_STATS_CLEAR, "stats_clear", &xfs_params.stats_clear.val,
	sizeof(int), 0644, NULL, &xfs_stats_clear_proc_handler,
	&sysctl_intvec, NULL,
	&xfs_params.stats_clear.min, &xfs_params.stats_clear.max},
#endif /* CONFIG_PROC_FS */

	{0}
};

STATIC ctl_table xfs_dir_table[] = {
	{FS_XFS, "xfs", NULL, 0, 0555, xfs_table},
	{0}
};

STATIC ctl_table xfs_root_table[] = {
	{CTL_FS, "fs",  NULL, 0, 0555, xfs_dir_table},
	{0}
};

void
xfs_sysctl_register(void)
{
	xfs_table_header = register_sysctl_table(xfs_root_table, 1);
}

void
xfs_sysctl_unregister(void)
{
	if (xfs_table_header)
		unregister_sysctl_table(xfs_table_header);
}
