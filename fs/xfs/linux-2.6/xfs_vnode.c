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

void
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

bhv_vnode_t *
vn_initialize(
	struct inode	*inode)
{
	bhv_vnode_t	*vp = vn_from_inode(inode);

	XFS_STATS_INC(vn_active);
	XFS_STATS_INC(vn_alloc);

	ASSERT(VN_CACHED(vp) == 0);

	return vp;
}

/*
 * Revalidate the Linux inode from the vattr.
 * Note: i_size _not_ updated; we must hold the inode
 * semaphore when doing that - callers responsibility.
 */
void
vn_revalidate_core(
	bhv_vnode_t	*vp,
	bhv_vattr_t	*vap)
{
	struct inode	*inode = vn_to_inode(vp);

	inode->i_mode	    = vap->va_mode;
	inode->i_nlink	    = vap->va_nlink;
	inode->i_uid	    = vap->va_uid;
	inode->i_gid	    = vap->va_gid;
	inode->i_blocks	    = vap->va_nblocks;
	inode->i_mtime	    = vap->va_mtime;
	inode->i_ctime	    = vap->va_ctime;
	if (vap->va_xflags & XFS_XFLAG_IMMUTABLE)
		inode->i_flags |= S_IMMUTABLE;
	else
		inode->i_flags &= ~S_IMMUTABLE;
	if (vap->va_xflags & XFS_XFLAG_APPEND)
		inode->i_flags |= S_APPEND;
	else
		inode->i_flags &= ~S_APPEND;
	if (vap->va_xflags & XFS_XFLAG_SYNC)
		inode->i_flags |= S_SYNC;
	else
		inode->i_flags &= ~S_SYNC;
	if (vap->va_xflags & XFS_XFLAG_NOATIME)
		inode->i_flags |= S_NOATIME;
	else
		inode->i_flags &= ~S_NOATIME;
}

/*
 * Revalidate the Linux inode from the vnode.
 */
int
__vn_revalidate(
	bhv_vnode_t	*vp,
	bhv_vattr_t	*vattr)
{
	int		error;

	vn_trace_entry(xfs_vtoi(vp), __FUNCTION__, (inst_t *)__return_address);
	vattr->va_mask = XFS_AT_STAT | XFS_AT_XFLAGS;
	error = xfs_getattr(xfs_vtoi(vp), vattr, 0);
	if (likely(!error)) {
		vn_revalidate_core(vp, vattr);
		xfs_iflags_clear(xfs_vtoi(vp), XFS_IMODIFIED);
	}
	return -error;
}

int
vn_revalidate(
	bhv_vnode_t	*vp)
{
	bhv_vattr_t	vattr;

	return __vn_revalidate(vp, &vattr);
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

#ifdef	XFS_VNODE_TRACE

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
vn_trace_entry(xfs_inode_t *ip, const char *func, inst_t *ra)
{
	KTRACE_ENTER(ip, VNODE_KTRACE_ENTRY, func, 0, ra);
}

void
vn_trace_exit(xfs_inode_t *ip, const char *func, inst_t *ra)
{
	KTRACE_ENTER(ip, VNODE_KTRACE_EXIT, func, 0, ra);
}

void
vn_trace_hold(xfs_inode_t *ip, char *file, int line, inst_t *ra)
{
	KTRACE_ENTER(ip, VNODE_KTRACE_HOLD, file, line, ra);
}

void
vn_trace_ref(xfs_inode_t *ip, char *file, int line, inst_t *ra)
{
	KTRACE_ENTER(ip, VNODE_KTRACE_REF, file, line, ra);
}

void
vn_trace_rele(xfs_inode_t *ip, char *file, int line, inst_t *ra)
{
	KTRACE_ENTER(ip, VNODE_KTRACE_RELE, file, line, ra);
}
#endif	/* XFS_VNODE_TRACE */
