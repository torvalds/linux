/*
 * Copyright (c) 2000-2003 Silicon Graphics, Inc.
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
#include "xfs_fs.h"
#include "xfs_bit.h"
#include "xfs_log.h"
#include "xfs_inum.h"
#include "xfs_trans.h"
#include "xfs_sb.h"
#include "xfs_ag.h"
#include "xfs_dir2.h"
#include "xfs_alloc.h"
#include "xfs_dmapi.h"
#include "xfs_quota.h"
#include "xfs_mount.h"
#include "xfs_bmap_btree.h"
#include "xfs_alloc_btree.h"
#include "xfs_ialloc_btree.h"
#include "xfs_dir2_sf.h"
#include "xfs_attr_sf.h"
#include "xfs_dinode.h"
#include "xfs_inode.h"
#include "xfs_ialloc.h"
#include "xfs_itable.h"
#include "xfs_bmap.h"
#include "xfs_btree.h"
#include "xfs_rtalloc.h"
#include "xfs_error.h"
#include "xfs_rw.h"
#include "xfs_attr.h"
#include "xfs_buf_item.h"
#include "xfs_qm.h"

struct xqmstats xqmstats;

static int xqm_proc_show(struct seq_file *m, void *v)
{
	/* maximum; incore; ratio free to inuse; freelist */
	seq_printf(m, "%d\t%d\t%d\t%u\n",
			ndquot,
			xfs_Gqm? atomic_read(&xfs_Gqm->qm_totaldquots) : 0,
			xfs_Gqm? xfs_Gqm->qm_dqfree_ratio : 0,
			xfs_Gqm? xfs_Gqm->qm_dqfreelist.qh_nelems : 0);
	return 0;
}

static int xqm_proc_open(struct inode *inode, struct file *file)
{
	return single_open(file, xqm_proc_show, NULL);
}

static const struct file_operations xqm_proc_fops = {
	.owner		= THIS_MODULE,
	.open		= xqm_proc_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};

static int xqmstat_proc_show(struct seq_file *m, void *v)
{
	/* quota performance statistics */
	seq_printf(m, "qm %u %u %u %u %u %u %u %u\n",
			xqmstats.xs_qm_dqreclaims,
			xqmstats.xs_qm_dqreclaim_misses,
			xqmstats.xs_qm_dquot_dups,
			xqmstats.xs_qm_dqcachemisses,
			xqmstats.xs_qm_dqcachehits,
			xqmstats.xs_qm_dqwants,
			xqmstats.xs_qm_dqshake_reclaims,
			xqmstats.xs_qm_dqinact_reclaims);
	return 0;
}

static int xqmstat_proc_open(struct inode *inode, struct file *file)
{
	return single_open(file, xqmstat_proc_show, NULL);
}

static const struct file_operations xqmstat_proc_fops = {
	.owner		= THIS_MODULE,
	.open		= xqmstat_proc_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};

void
xfs_qm_init_procfs(void)
{
	proc_create("fs/xfs/xqmstat", 0, NULL, &xqmstat_proc_fops);
	proc_create("fs/xfs/xqm", 0, NULL, &xqm_proc_fops);
}

void
xfs_qm_cleanup_procfs(void)
{
	remove_proc_entry("fs/xfs/xqm", NULL);
	remove_proc_entry("fs/xfs/xqmstat", NULL);
}
