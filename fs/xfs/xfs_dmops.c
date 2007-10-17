/*
 * Copyright (c) 2000-2003,2005 Silicon Graphics, Inc.
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
#include "xfs_types.h"
#include "xfs_log.h"
#include "xfs_trans.h"
#include "xfs_sb.h"
#include "xfs_dmapi.h"
#include "xfs_inum.h"
#include "xfs_ag.h"
#include "xfs_mount.h"
#include "xfs_clnt.h"


static struct xfs_dmops xfs_dmcore_stub = {
	.xfs_send_data		= (xfs_send_data_t)fs_nosys,
	.xfs_send_mmap		= (xfs_send_mmap_t)fs_noerr,
	.xfs_send_destroy	= (xfs_send_destroy_t)fs_nosys,
	.xfs_send_namesp	= (xfs_send_namesp_t)fs_nosys,
	.xfs_send_mount		= (xfs_send_mount_t)fs_nosys,
	.xfs_send_unmount	= (xfs_send_unmount_t)fs_noerr,
};

int
xfs_dmops_get(struct xfs_mount *mp, struct xfs_mount_args *args)
{
	if (args->flags & XFSMNT_DMAPI) {
		struct xfs_dmops *ops;

		ops = symbol_get(xfs_dmcore_xfs);
		if (!ops) {
			request_module("xfs_dmapi");
			ops = symbol_get(xfs_dmcore_xfs);
		}

		if (!ops) {
			cmn_err(CE_WARN, "XFS: no dmapi support available.");
			return EINVAL;
		}
		mp->m_dm_ops = ops;
	} else {
		mp->m_dm_ops = &xfs_dmcore_stub;
	}

	return 0;
}

void
xfs_dmops_put(struct xfs_mount *mp)
{
	if (mp->m_dm_ops != &xfs_dmcore_stub)
		symbol_put(xfs_dmcore_xfs);
}
