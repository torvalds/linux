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
 * or the like.  Any license provided herein, whether implied or
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


uint64_t vn_generation;		/* vnode generation number */
DEFINE_SPINLOCK(vnumber_lock);

/*
 * Dedicated vnode inactive/reclaim sync semaphores.
 * Prime number of hash buckets since address is used as the key.
 */
#define NVSYNC                  37
#define vptosync(v)             (&vsync[((unsigned long)v) % NVSYNC])
sv_t vsync[NVSYNC];

/*
 * Translate stat(2) file types to vnode types and vice versa.
 * Aware of numeric order of S_IFMT and vnode type values.
 */
enum vtype iftovt_tab[] = {
	VNON, VFIFO, VCHR, VNON, VDIR, VNON, VBLK, VNON,
	VREG, VNON, VLNK, VNON, VSOCK, VNON, VNON, VNON
};

u_short vttoif_tab[] = {
	0, S_IFREG, S_IFDIR, S_IFBLK, S_IFCHR, S_IFLNK, S_IFIFO, 0, S_IFSOCK
};


void
vn_init(void)
{
	register sv_t *svp;
	register int i;

	for (svp = vsync, i = 0; i < NVSYNC; i++, svp++)
		init_sv(svp, SV_DEFAULT, "vsy", i);
}

/*
 * Clean a vnode of filesystem-specific data and prepare it for reuse.
 */
STATIC int
vn_reclaim(
	struct vnode	*vp)
{
	int		error;

	XFS_STATS_INC(vn_reclaim);
	vn_trace_entry(vp, "vn_reclaim", (inst_t *)__return_address);

	/*
	 * Only make the VOP_RECLAIM call if there are behaviors
	 * to call.
	 */
	if (vp->v_fbhv) {
		VOP_RECLAIM(vp, error);
		if (error)
			return -error;
	}
	ASSERT(vp->v_fbhv == NULL);

	VN_LOCK(vp);
	vp->v_flag &= (VRECLM|VWAIT);
	VN_UNLOCK(vp, 0);

	vp->v_type = VNON;
	vp->v_fbhv = NULL;

#ifdef XFS_VNODE_TRACE
	ktrace_free(vp->v_trace);
	vp->v_trace = NULL;
#endif

	return 0;
}

STATIC void
vn_wakeup(
	struct vnode	*vp)
{
	VN_LOCK(vp);
	if (vp->v_flag & VWAIT)
		sv_broadcast(vptosync(vp));
	vp->v_flag &= ~(VRECLM|VWAIT|VMODIFIED);
	VN_UNLOCK(vp, 0);
}

int
vn_wait(
	struct vnode	*vp)
{
	VN_LOCK(vp);
	if (vp->v_flag & (VINACT | VRECLM)) {
		vp->v_flag |= VWAIT;
		sv_wait(vptosync(vp), PINOD, &vp->v_lock, 0);
		return 1;
	}
	VN_UNLOCK(vp, 0);
	return 0;
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

#ifdef	XFS_VNODE_TRACE
	vp->v_trace = ktrace_alloc(VNODE_TRACE_SIZE, KM_SLEEP);
	printk("Allocated VNODE_TRACE at 0x%p\n", vp->v_trace);
#endif	/* XFS_VNODE_TRACE */

	vn_trace_exit(vp, "vn_initialize", (inst_t *)__return_address);
	return vp;
}

/*
 * Get a reference on a vnode.
 */
vnode_t *
vn_get(
	struct vnode	*vp,
	vmap_t		*vmap)
{
	struct inode	*inode;

	XFS_STATS_INC(vn_get);
	inode = LINVFS_GET_IP(vp);
	if (inode->i_state & I_FREEING)
		return NULL;

	inode = ilookup(vmap->v_vfsp->vfs_super, vmap->v_ino);
	if (!inode)	/* Inode not present */
		return NULL;

	vn_trace_exit(vp, "vn_get", (inst_t *)__return_address);

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

	inode->i_mode	    = VTTOIF(vap->va_type) | vap->va_mode;
	inode->i_nlink	    = vap->va_nlink;
	inode->i_uid	    = vap->va_uid;
	inode->i_gid	    = vap->va_gid;
	inode->i_blocks	    = vap->va_nblocks;
	inode->i_mtime	    = vap->va_mtime;
	inode->i_ctime	    = vap->va_ctime;
	inode->i_atime	    = vap->va_atime;
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
 * purge a vnode from the cache
 * At this point the vnode is guaranteed to have no references (vn_count == 0)
 * The caller has to make sure that there are no ways someone could
 * get a handle (via vn_get) on the vnode (usually done via a mount/vfs lock).
 */
void
vn_purge(
	struct vnode	*vp,
	vmap_t		*vmap)
{
	vn_trace_entry(vp, "vn_purge", (inst_t *)__return_address);

again:
	/*
	 * Check whether vp has already been reclaimed since our caller
	 * sampled its version while holding a filesystem cache lock that
	 * its VOP_RECLAIM function acquires.
	 */
	VN_LOCK(vp);
	if (vp->v_number != vmap->v_number) {
		VN_UNLOCK(vp, 0);
		return;
	}

	/*
	 * If vp is being reclaimed or inactivated, wait until it is inert,
	 * then proceed.  Can't assume that vnode is actually reclaimed
	 * just because the reclaimed flag is asserted -- a vn_alloc
	 * reclaim can fail.
	 */
	if (vp->v_flag & (VINACT | VRECLM)) {
		ASSERT(vn_count(vp) == 0);
		vp->v_flag |= VWAIT;
		sv_wait(vptosync(vp), PINOD, &vp->v_lock, 0);
		goto again;
	}

	/*
	 * Another process could have raced in and gotten this vnode...
	 */
	if (vn_count(vp) > 0) {
		VN_UNLOCK(vp, 0);
		return;
	}

	XFS_STATS_DEC(vn_active);
	vp->v_flag |= VRECLM;
	VN_UNLOCK(vp, 0);

	/*
	 * Call VOP_RECLAIM and clean vp. The FSYNC_INVAL flag tells
	 * vp's filesystem to flush and invalidate all cached resources.
	 * When vn_reclaim returns, vp should have no private data,
	 * either in a system cache or attached to v_data.
	 */
	if (vn_reclaim(vp) != 0)
		panic("vn_purge: cannot reclaim");

	/*
	 * Wakeup anyone waiting for vp to be reclaimed.
	 */
	vn_wakeup(vp);
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

/*
 *  Call VOP_INACTIVE on last reference.
 */
void
vn_rele(
	struct vnode	*vp)
{
	int		vcnt;
	int		cache;

	XFS_STATS_INC(vn_rele);

	VN_LOCK(vp);

	vn_trace_entry(vp, "vn_rele", (inst_t *)__return_address);
	vcnt = vn_count(vp);

	/*
	 * Since we always get called from put_inode we know
	 * that i_count won't be decremented after we
	 * return.
	 */
	if (!vcnt) {
		/*
		 * As soon as we turn this on, noone can find us in vn_get
		 * until we turn off VINACT or VRECLM
		 */
		vp->v_flag |= VINACT;
		VN_UNLOCK(vp, 0);

		/*
		 * Do not make the VOP_INACTIVE call if there
		 * are no behaviors attached to the vnode to call.
		 */
		if (vp->v_fbhv)
			VOP_INACTIVE(vp, NULL, cache);

		VN_LOCK(vp);
		if (vp->v_flag & VWAIT)
			sv_broadcast(vptosync(vp));

		vp->v_flag &= ~(VINACT|VWAIT|VRECLM|VMODIFIED);
	}

	VN_UNLOCK(vp, 0);

	vn_trace_exit(vp, "vn_rele", (inst_t *)__return_address);
}

/*
 * Finish the removal of a vnode.
 */
void
vn_remove(
	struct vnode	*vp)
{
	vmap_t		vmap;

	/* Make sure we don't do this to the same vnode twice */
	if (!(vp->v_fbhv))
		return;

	XFS_STATS_INC(vn_remove);
	vn_trace_exit(vp, "vn_remove", (inst_t *)__return_address);

	/*
	 * After the following purge the vnode
	 * will no longer exist.
	 */
	VMAP(vp, vmap);
	vn_purge(vp, &vmap);
}


#ifdef	XFS_VNODE_TRACE

#define KTRACE_ENTER(vp, vk, s, line, ra)			\
	ktrace_enter(	(vp)->v_trace,				\
/*  0 */		(void *)(__psint_t)(vk),		\
/*  1 */		(void *)(s),				\
/*  2 */		(void *)(__psint_t) line,		\
/*  3 */		(void *)(vn_count(vp)), \
/*  4 */		(void *)(ra),				\
/*  5 */		(void *)(__psunsigned_t)(vp)->v_flag,	\
/*  6 */		(void *)(__psint_t)current_cpu(),	\
/*  7 */		(void *)(__psint_t)current_pid(),	\
/*  8 */		(void *)__return_address,		\
/*  9 */		0, 0, 0, 0, 0, 0, 0)

/*
 * Vnode tracing code.
 */
void
vn_trace_entry(vnode_t *vp, char *func, inst_t *ra)
{
	KTRACE_ENTER(vp, VNODE_KTRACE_ENTRY, func, 0, ra);
}

void
vn_trace_exit(vnode_t *vp, char *func, inst_t *ra)
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
