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
#include "xfs_vnodeops.h"
#include "xfs_bmap_btree.h"
#include "xfs_inode.h"

/*
 * And this gunk is needed for xfs_mount.h"
 */
#include "xfs_log.h"
#include "xfs_trans.h"
#include "xfs_sb.h"
#include "xfs_dmapi.h"
#include "xfs_inum.h"
#include "xfs_ag.h"
#include "xfs_mount.h"


#ifdef	XFS_INODE_TRACE

#define KTRACE_ENTER(ip, vk, s, line, ra)			\
	ktrace_enter(	(ip)->i_trace,				\
/*  0 */		(void *)(__psint_t)(vk),		\
/*  1 */		(void *)(s),				\
/*  2 */		(void *)(__psint_t) line,		\
/*  3 */		(void *)(__psint_t)atomic_read(&VFS_I(ip)->i_count), \
/*  4 */		(void *)(ra),				\
/*  5 */		NULL,					\
/*  6 */		(void *)(__psint_t)current_cpu(),	\
/*  7 */		(void *)(__psint_t)current_pid(),	\
/*  8 */		(void *)__return_address,		\
/*  9 */		NULL, NULL, NULL, NULL, NULL, NULL, NULL)

/*
 * Vnode tracing code.
 */
void
_xfs_itrace_entry(xfs_inode_t *ip, const char *func, inst_t *ra)
{
	KTRACE_ENTER(ip, INODE_KTRACE_ENTRY, func, 0, ra);
}

void
_xfs_itrace_exit(xfs_inode_t *ip, const char *func, inst_t *ra)
{
	KTRACE_ENTER(ip, INODE_KTRACE_EXIT, func, 0, ra);
}

void
xfs_itrace_hold(xfs_inode_t *ip, char *file, int line, inst_t *ra)
{
	KTRACE_ENTER(ip, INODE_KTRACE_HOLD, file, line, ra);
}

void
_xfs_itrace_ref(xfs_inode_t *ip, char *file, int line, inst_t *ra)
{
	KTRACE_ENTER(ip, INODE_KTRACE_REF, file, line, ra);
}

void
xfs_itrace_rele(xfs_inode_t *ip, char *file, int line, inst_t *ra)
{
	KTRACE_ENTER(ip, INODE_KTRACE_RELE, file, line, ra);
}
#endif	/* XFS_INODE_TRACE */
