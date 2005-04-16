/*
 * Copyright (c) 2000-2003 Silicon Graphics, Inc.  All Rights Reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it would be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *
 * Further, this software is distributed without any warranty that it is
 * free of the rightful claim of any third person regarding infringement
 * or the like.	 Any license provided herein, whether implied or
 * otherwise, applies only to this software file.  Patent licenses, if
 * any, provided herein do not apply to combinations of this program with
 * other software, or any other product whatsoever.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write the Free Software Foundation, Inc., 59
 * Temple Place - Suite 330, Boston MA 02111-1307, USA.
 *
 * Contact information: Silicon Graphics, Inc., 1600 Amphitheatre Pkwy,
 * Mountain View, CA  94043, or:
 *
 * http://www.sgi.com
 *
 * For further information regarding this notice, see:
 *
 * http://oss.sgi.com/projects/GenInfo/SGIGPLNoticeExplan/
 */

#include "xfs.h"
#include "xfs_fs.h"
#include "xfs_inum.h"
#include "xfs_log.h"
#include "xfs_trans.h"
#include "xfs_sb.h"
#include "xfs_dir.h"
#include "xfs_dir2.h"
#include "xfs_alloc.h"
#include "xfs_dmapi.h"
#include "xfs_quota.h"
#include "xfs_mount.h"
#include "xfs_alloc_btree.h"
#include "xfs_bmap_btree.h"
#include "xfs_ialloc_btree.h"
#include "xfs_btree.h"
#include "xfs_ialloc.h"
#include "xfs_attr_sf.h"
#include "xfs_dir_sf.h"
#include "xfs_dir2_sf.h"
#include "xfs_dinode.h"
#include "xfs_inode.h"
#include "xfs_bmap.h"
#include "xfs_bit.h"
#include "xfs_rtalloc.h"
#include "xfs_error.h"
#include "xfs_itable.h"
#include "xfs_rw.h"
#include "xfs_acl.h"
#include "xfs_cap.h"
#include "xfs_mac.h"
#include "xfs_attr.h"
#include "xfs_buf_item.h"

#include "xfs_qm.h"

struct xqmstats xqmstats;

STATIC int
xfs_qm_read_xfsquota(
	char		*buffer,
	char		**start,
	off_t		offset,
	int		count,
	int		*eof,
	void		*data)
{
	int		len;

	/* maximum; incore; ratio free to inuse; freelist */
	len = sprintf(buffer, "%d\t%d\t%d\t%u\n",
			ndquot,
			xfs_Gqm? atomic_read(&xfs_Gqm->qm_totaldquots) : 0,
			xfs_Gqm? xfs_Gqm->qm_dqfree_ratio : 0,
			xfs_Gqm? xfs_Gqm->qm_dqfreelist.qh_nelems : 0);

	if (offset >= len) {
		*start = buffer;
		*eof = 1;
		return 0;
	}
	*start = buffer + offset;
	if ((len -= offset) > count)
		return count;
	*eof = 1;

	return len;
}

STATIC int
xfs_qm_read_stats(
	char		*buffer,
	char		**start,
	off_t		offset,
	int		count,
	int		*eof,
	void		*data)
{
	int		len;

	/* quota performance statistics */
	len = sprintf(buffer, "qm %u %u %u %u %u %u %u %u\n",
			xqmstats.xs_qm_dqreclaims,
			xqmstats.xs_qm_dqreclaim_misses,
			xqmstats.xs_qm_dquot_dups,
			xqmstats.xs_qm_dqcachemisses,
			xqmstats.xs_qm_dqcachehits,
			xqmstats.xs_qm_dqwants,
			xqmstats.xs_qm_dqshake_reclaims,
			xqmstats.xs_qm_dqinact_reclaims);

	if (offset >= len) {
		*start = buffer;
		*eof = 1;
		return 0;
	}
	*start = buffer + offset;
	if ((len -= offset) > count)
		return count;
	*eof = 1;

	return len;
}

void
xfs_qm_init_procfs(void)
{
	create_proc_read_entry("fs/xfs/xqmstat", 0, NULL, xfs_qm_read_stats, NULL);
	create_proc_read_entry("fs/xfs/xqm", 0, NULL, xfs_qm_read_xfsquota, NULL);
}

void
xfs_qm_cleanup_procfs(void)
{
	remove_proc_entry("fs/xfs/xqm", NULL);
	remove_proc_entry("fs/xfs/xqmstat", NULL);
}
