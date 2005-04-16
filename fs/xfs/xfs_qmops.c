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

#include "xfs_macros.h"
#include "xfs_types.h"
#include "xfs_inum.h"
#include "xfs_log.h"
#include "xfs_trans.h"
#include "xfs_sb.h"
#include "xfs_ag.h"
#include "xfs_dir.h"
#include "xfs_dir2.h"
#include "xfs_dmapi.h"
#include "xfs_mount.h"


STATIC struct xfs_dquot *
xfs_dqvopchown_default(
	struct xfs_trans	*tp,
	struct xfs_inode	*ip,
	struct xfs_dquot	**dqp,
	struct xfs_dquot	*dq)
{
	return NULL;
}

xfs_qmops_t	xfs_qmcore_stub = {
	.xfs_qminit		= (xfs_qminit_t) fs_noerr,
	.xfs_qmdone		= (xfs_qmdone_t) fs_noerr,
	.xfs_qmmount		= (xfs_qmmount_t) fs_noerr,
	.xfs_qmunmount		= (xfs_qmunmount_t) fs_noerr,
	.xfs_dqrele		= (xfs_dqrele_t) fs_noerr,
	.xfs_dqattach		= (xfs_dqattach_t) fs_noerr,
	.xfs_dqdetach		= (xfs_dqdetach_t) fs_noerr,
	.xfs_dqpurgeall		= (xfs_dqpurgeall_t) fs_noerr,
	.xfs_dqvopalloc		= (xfs_dqvopalloc_t) fs_noerr,
	.xfs_dqvopcreate	= (xfs_dqvopcreate_t) fs_noerr,
	.xfs_dqvoprename	= (xfs_dqvoprename_t) fs_noerr,
	.xfs_dqvopchown		= xfs_dqvopchown_default,
	.xfs_dqvopchownresv	= (xfs_dqvopchownresv_t) fs_noerr,
};
