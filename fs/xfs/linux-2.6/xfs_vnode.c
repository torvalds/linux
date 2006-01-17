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

uint64_t vn_generation;		/* vnode generation number */
DEFINE_SPINLOCK(vnumber_lock);

/*
 * Dedicated vnode inactive/reclaim sync semaphores.
 * Prime number of hash buckets since address is used as the key.
 */
#define NVSYNC                  37
#define vptosync(v)             (&vsync[((unsigned long)v) % NVSYNC])
STATIC wait_queue_head_t vsync[NVSYNC];

void
vn_init(void)
{
	int i;

	for (i = 0; i < NVSYNC; i++)
		init_waitqueue_head(&vsync[i]);
}

void
vn_iowait(
	struct vnode	*vp)
{
	wait_queue_head_t *wq = vptosync(vp);

	wait_event(*wq, (atomic_read(&vp->v_iocount) == 0));
}

void
vn_iowake(
	struct vnode	*vp)
{
	if (atomic_dec_and_test(&vp->v_iocount))
		wake_up(vptosync(vp));
}

struct vnode *
vn_initialize(
	struct inode	*inode)
{
	struct vnode	*vp = LINVFS_GET_VP(inode);

	XFS_STATS_INC(vn_active);
	XFS_STATS_INC(vn_alloc);

	vp->v_flag = VMODIFIED;
	spinlock_init(&vp->v_lock, "v_lock");

	spin_lock(&vnumber_lock);
	if (!++vn_generation)	/* v_number shouldn't be zero */
		vn_generation++;
	vp->v_number = vn_generation;
	spin_unlock(&vnumber_lock);

	ASSERT(VN_CACHED(vp) == 0);

	/* Initialize the first behavior and the behavior chain head. */
	vn_bhv_head_init(VN_BHV_HEAD(vp), "vnode");

	atomic_set(&vp->v_iocount, 0);

#ifdef	XFS_VNODE_TRACE
	vp->v_trace = ktrace_alloc(VNODE_TRACE_SIZE, KM_SLEEP);
#endif	/* XFS_VNODE_TRACE */

	vn_trace_exit(vp, "vn_initialize", (inst_t *)__return_address);
	return vp;
}

/*
 * Revalidate the Linux inode from the vattr.
 * Note: i_size _not_ updated; we must hold the inode
 * semaphore when doing that - callers responsibility.
 */
void
vn_revalidate_core(
	struct vnode	*vp,
	vattr_t		*vap)
{
	struct inode	*inode = LINVFS_GET_IP(vp);

	inode->i_mode	    = vap->va_mode;
	inode->i_nlink	    = vap->va_nlink;
	inode->i_uid	    = vap->va_uid;
	inode->i_gid	    = vap->va_gid;
	inode->i_blocks	    = vap->va_nblocks;
	inode->i_mtime	    = vap->va_mtime;
	inode->i_ctime	    = vap->va_ctime;
	inode->i_blksize    = vap->va_blocksize;
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
vn_revalidate(
	struct vnode	*vp)
{
	vattr_t		va;
	int		error;

	vn_trace_entry(vp, "vn_revalidate", (inst_t *)__return_address);
	ASSERT(vp->v_fbhv != NULL);

	va.va_mask = XFS_AT_STAT|XFS_AT_XFLAGS;
	VOP_GETATTR(vp, &va, 0, NULL, error);
	if (!error) {
		vn_revalidate_core(vp, &va);
		VUNMODIFY(vp);
	}
	return -error;
}

/*
 * Add a reference to a referenced vnode.
 */
struct vnode *
vn_hold(
	struct vnode	*vp)
{
	struct inode	*inode;

	XFS_STATS_INC(vn_hold);

	VN_LOCK(vp);
	inode = igrab(LINVFS_GET_IP(vp));
	ASSERT(inode);
	VN_UNLOCK(vp, 0);

	return vp;
}

#ifdef	XFS_VNODE_TRACE

#define KTRACE_ENTER(vp, vk, s, line, ra)			\
	ktrace_enter(	(vp)->v_trace,				\
/*  0 */		(void *)(__psint_t)(vk),		\
/*  1 */		(void *)(s),				\
/*  2 */		(void *)(__psint_t) line,		\
/*  3 */		(void *)(__psint_t)(vn_count(vp)),	\
/*  4 */		(void *)(ra),				\
/*  5 */		(void *)(__psunsigned_t)(vp)->v_flag,	\
/*  6 */		(void *)(__psint_t)current_cpu(),	\
/*  7 */		(void *)(__psint_t)current_pid(),	\
/*  8 */		(void *)__return_address,		\
/*  9 */		NULL, NULL, NULL, NULL, NULL, NULL, NULL)

/*
 * Vnode tracing code.
 */
void
vn_trace_entry(vnode_t *vp, const char *func, inst_t *ra)
{
	KTRACE_ENTER(vp, VNODE_KTRACE_ENTRY, func, 0, ra);
}

void
vn_trace_exit(vnode_t *vp, const char *func, inst_t *ra)
{
	KTRACE_ENTER(vp, VNODE_KTRACE_EXIT, func, 0, ra);
}

void
vn_trace_hold(vnode_t *vp, char *file, int line, inst_t *ra)
{
	KTRACE_ENTER(vp, VNODE_KTRACE_HOLD, file, line, ra);
}

void
vn_trace_ref(vnode_t *vp, char *file, int line, inst_t *ra)
{
	KTRACE_ENTER(vp, VNODE_KTRACE_REF, file, line, ra);
}

void
vn_trace_rele(vnode_t *vp, char *file, int line, inst_t *ra)
{
	KTRACE_ENTER(vp, VNODE_KTRACE_RELE, file, line, ra);
}
#endif	/* XFS_VNODE_TRACE */
