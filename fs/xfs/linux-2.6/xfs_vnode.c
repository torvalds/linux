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


/*
 * Dedicated vnode inactive/reclaim sync semaphores.
 * Prime number of hash buckets since address is used as the key.
 */
#define NVSYNC                  37
#define vptosync(v)             (&vsync[((unsigned long)v) % NVSYNC])
static wait_queue_head_t vsync[NVSYNC];

void __init
vn_init(void)
{
	int i;

	for (i = 0; i < NVSYNC; i++)
		init_waitqueue_head(&vsync[i]);
}

void
vn_iowait(
	xfs_inode_t	*ip)
{
	wait_queue_head_t *wq = vptosync(ip);

	wait_event(*wq, (atomic_read(&ip->i_iocount) == 0));
}

void
vn_iowake(
	xfs_inode_t	*ip)
{
	if (atomic_dec_and_test(&ip->i_iocount))
		wake_up(vptosync(ip));
}

/*
 * Volume managers supporting multiple paths can send back ENODEV when the
 * final path disappears.  In this case continuing to fill the page cache
 * with dirty data which cannot be written out is evil, so prevent that.
 */
void
vn_ioerror(
	xfs_inode_t	*ip,
	int		error,
	char		*f,
	int		l)
{
	if (unlikely(error == -ENODEV))
		xfs_do_force_shutdown(ip->i_mount, SHUTDOWN_DEVICE_REQ, f, l);
}

/*
 * Revalidate the Linux inode from the XFS inode.
 * Note: i_size _not_ updated; we must hold the inode
 * semaphore when doing that - callers responsibility.
 */
int
vn_revalidate(
	bhv_vnode_t		*vp)
{
	struct inode		*inode = vn_to_inode(vp);
	struct xfs_inode	*ip = XFS_I(inode);
	struct xfs_mount	*mp = ip->i_mount;
	unsigned long		xflags;

	xfs_itrace_entry(ip);

	if (XFS_FORCED_SHUTDOWN(mp))
		return -EIO;

	xfs_ilock(ip, XFS_ILOCK_SHARED);
	inode->i_mode	    = ip->i_d.di_mode;
	inode->i_uid	    = ip->i_d.di_uid;
	inode->i_gid	    = ip->i_d.di_gid;
	inode->i_mtime.tv_sec = ip->i_d.di_mtime.t_sec;
	inode->i_mtime.tv_nsec = ip->i_d.di_mtime.t_nsec;
	inode->i_ctime.tv_sec = ip->i_d.di_ctime.t_sec;
	inode->i_ctime.tv_nsec = ip->i_d.di_ctime.t_nsec;

	xflags = xfs_ip2xflags(ip);
	if (xflags & XFS_XFLAG_IMMUTABLE)
		inode->i_flags |= S_IMMUTABLE;
	else
		inode->i_flags &= ~S_IMMUTABLE;
	if (xflags & XFS_XFLAG_APPEND)
		inode->i_flags |= S_APPEND;
	else
		inode->i_flags &= ~S_APPEND;
	if (xflags & XFS_XFLAG_SYNC)
		inode->i_flags |= S_SYNC;
	else
		inode->i_flags &= ~S_SYNC;
	if (xflags & XFS_XFLAG_NOATIME)
		inode->i_flags |= S_NOATIME;
	else
		inode->i_flags &= ~S_NOATIME;
	xfs_iunlock(ip, XFS_ILOCK_SHARED);

	xfs_iflags_clear(ip, XFS_IMODIFIED);
	return 0;
}

/*
 * Add a reference to a referenced vnode.
 */
bhv_vnode_t *
vn_hold(
	bhv_vnode_t	*vp)
{
	struct inode	*inode;

	XFS_STATS_INC(vn_hold);

	inode = igrab(vn_to_inode(vp));
	ASSERT(inode);

	return vp;
}

#ifdef	XFS_INODE_TRACE

/*
 * Reference count of Linux inode if present, -1 if the xfs_inode
 * has no associated Linux inode.
 */
static inline int xfs_icount(struct xfs_inode *ip)
{
	bhv_vnode_t *vp = XFS_ITOV_NULL(ip);

	if (vp)
		return vn_count(vp);
	return -1;
}

#define KTRACE_ENTER(ip, vk, s, line, ra)			\
	ktrace_enter(	(ip)->i_trace,				\
/*  0 */		(void *)(__psint_t)(vk),		\
/*  1 */		(void *)(s),				\
/*  2 */		(void *)(__psint_t) line,		\
/*  3 */		(void *)(__psint_t)xfs_icount(ip),	\
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
