/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1989, 1993
 *	The Regents of the University of California.  All rights reserved.
 * (c) UNIX System Laboratories, Inc.
 * All or some portions of this file are derived from material licensed
 * to the University of California by American Telephone and Telegraph
 * Co. or Unix System Laboratories, Inc. and are reproduced herein with
 * the permission of UNIX System Laboratories, Inc.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *	@(#)vfs_subr.c	8.31 (Berkeley) 5/26/95
 */

/*
 * External virtual filesystem routines
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include "opt_ddb.h"
#include "opt_watchdog.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/bio.h>
#include <sys/buf.h>
#include <sys/condvar.h>
#include <sys/conf.h>
#include <sys/counter.h>
#include <sys/dirent.h>
#include <sys/event.h>
#include <sys/eventhandler.h>
#include <sys/extattr.h>
#include <sys/file.h>
#include <sys/fcntl.h>
#include <sys/jail.h>
#include <sys/kdb.h>
#include <sys/kernel.h>
#include <sys/kthread.h>
#include <sys/lockf.h>
#include <sys/malloc.h>
#include <sys/mount.h>
#include <sys/namei.h>
#include <sys/pctrie.h>
#include <sys/priv.h>
#include <sys/reboot.h>
#include <sys/refcount.h>
#include <sys/rwlock.h>
#include <sys/sched.h>
#include <sys/sleepqueue.h>
#include <sys/smp.h>
#include <sys/stat.h>
#include <sys/sysctl.h>
#include <sys/syslog.h>
#include <sys/vmmeter.h>
#include <sys/vnode.h>
#include <sys/watchdog.h>

#include <machine/stdarg.h>

#include <security/mac/mac_framework.h>

#include <vm/vm.h>
#include <vm/vm_object.h>
#include <vm/vm_extern.h>
#include <vm/pmap.h>
#include <vm/vm_map.h>
#include <vm/vm_page.h>
#include <vm/vm_kern.h>
#include <vm/uma.h>

#ifdef DDB
#include <ddb/ddb.h>
#endif

static void	delmntque(struct vnode *vp);
static int	flushbuflist(struct bufv *bufv, int flags, struct bufobj *bo,
		    int slpflag, int slptimeo);
static void	syncer_shutdown(void *arg, int howto);
static int	vtryrecycle(struct vnode *vp);
static void	v_init_counters(struct vnode *);
static void	v_incr_usecount(struct vnode *);
static void	v_incr_usecount_locked(struct vnode *);
static void	v_incr_devcount(struct vnode *);
static void	v_decr_devcount(struct vnode *);
static void	vgonel(struct vnode *);
static void	vfs_knllock(void *arg);
static void	vfs_knlunlock(void *arg);
static void	vfs_knl_assert_locked(void *arg);
static void	vfs_knl_assert_unlocked(void *arg);
static void	vnlru_return_batches(struct vfsops *mnt_op);
static void	destroy_vpollinfo(struct vpollinfo *vi);

/*
 * These fences are intended for cases where some synchronization is
 * needed between access of v_iflags and lockless vnode refcount (v_holdcnt
 * and v_usecount) updates.  Access to v_iflags is generally synchronized
 * by the interlock, but we have some internal assertions that check vnode
 * flags without acquiring the lock.  Thus, these fences are INVARIANTS-only
 * for now.
 */
#ifdef INVARIANTS
#define	VNODE_REFCOUNT_FENCE_ACQ()	atomic_thread_fence_acq()
#define	VNODE_REFCOUNT_FENCE_REL()	atomic_thread_fence_rel()
#else
#define	VNODE_REFCOUNT_FENCE_ACQ()
#define	VNODE_REFCOUNT_FENCE_REL()
#endif

/*
 * Number of vnodes in existence.  Increased whenever getnewvnode()
 * allocates a new vnode, decreased in vdropl() for VI_DOOMED vnode.
 */
static unsigned long	numvnodes;

SYSCTL_ULONG(_vfs, OID_AUTO, numvnodes, CTLFLAG_RD, &numvnodes, 0,
    "Number of vnodes in existence");

static counter_u64_t vnodes_created;
SYSCTL_COUNTER_U64(_vfs, OID_AUTO, vnodes_created, CTLFLAG_RD, &vnodes_created,
    "Number of vnodes created by getnewvnode");

static u_long mnt_free_list_batch = 128;
SYSCTL_ULONG(_vfs, OID_AUTO, mnt_free_list_batch, CTLFLAG_RW,
    &mnt_free_list_batch, 0, "Limit of vnodes held on mnt's free list");

/*
 * Conversion tables for conversion from vnode types to inode formats
 * and back.
 */
enum vtype iftovt_tab[16] = {
	VNON, VFIFO, VCHR, VNON, VDIR, VNON, VBLK, VNON,
	VREG, VNON, VLNK, VNON, VSOCK, VNON, VNON, VNON
};
int vttoif_tab[10] = {
	0, S_IFREG, S_IFDIR, S_IFBLK, S_IFCHR, S_IFLNK,
	S_IFSOCK, S_IFIFO, S_IFMT, S_IFMT
};

/*
 * List of vnodes that are ready for recycling.
 */
static TAILQ_HEAD(freelst, vnode) vnode_free_list;

/*
 * "Free" vnode target.  Free vnodes are rarely completely free, but are
 * just ones that are cheap to recycle.  Usually they are for files which
 * have been stat'd but not read; these usually have inode and namecache
 * data attached to them.  This target is the preferred minimum size of a
 * sub-cache consisting mostly of such files. The system balances the size
 * of this sub-cache with its complement to try to prevent either from
 * thrashing while the other is relatively inactive.  The targets express
 * a preference for the best balance.
 *
 * "Above" this target there are 2 further targets (watermarks) related
 * to recyling of free vnodes.  In the best-operating case, the cache is
 * exactly full, the free list has size between vlowat and vhiwat above the
 * free target, and recycling from it and normal use maintains this state.
 * Sometimes the free list is below vlowat or even empty, but this state
 * is even better for immediate use provided the cache is not full.
 * Otherwise, vnlru_proc() runs to reclaim enough vnodes (usually non-free
 * ones) to reach one of these states.  The watermarks are currently hard-
 * coded as 4% and 9% of the available space higher.  These and the default
 * of 25% for wantfreevnodes are too large if the memory size is large.
 * E.g., 9% of 75% of MAXVNODES is more than 566000 vnodes to reclaim
 * whenever vnlru_proc() becomes active.
 */
static u_long wantfreevnodes;
SYSCTL_ULONG(_vfs, OID_AUTO, wantfreevnodes, CTLFLAG_RW,
    &wantfreevnodes, 0, "Target for minimum number of \"free\" vnodes");
static u_long freevnodes;
SYSCTL_ULONG(_vfs, OID_AUTO, freevnodes, CTLFLAG_RD,
    &freevnodes, 0, "Number of \"free\" vnodes");

static counter_u64_t recycles_count;
SYSCTL_COUNTER_U64(_vfs, OID_AUTO, recycles, CTLFLAG_RD, &recycles_count,
    "Number of vnodes recycled to meet vnode cache targets");

/*
 * Various variables used for debugging the new implementation of
 * reassignbuf().
 * XXX these are probably of (very) limited utility now.
 */
static int reassignbufcalls;
SYSCTL_INT(_vfs, OID_AUTO, reassignbufcalls, CTLFLAG_RW, &reassignbufcalls, 0,
    "Number of calls to reassignbuf");

static counter_u64_t free_owe_inact;
SYSCTL_COUNTER_U64(_vfs, OID_AUTO, free_owe_inact, CTLFLAG_RD, &free_owe_inact,
    "Number of times free vnodes kept on active list due to VFS "
    "owing inactivation");

/* To keep more than one thread at a time from running vfs_getnewfsid */
static struct mtx mntid_mtx;

/*
 * Lock for any access to the following:
 *	vnode_free_list
 *	numvnodes
 *	freevnodes
 */
static struct mtx vnode_free_list_mtx;

/* Publicly exported FS */
struct nfs_public nfs_pub;

static uma_zone_t buf_trie_zone;

/* Zone for allocation of new vnodes - used exclusively by getnewvnode() */
static uma_zone_t vnode_zone;
static uma_zone_t vnodepoll_zone;

/*
 * The workitem queue.
 *
 * It is useful to delay writes of file data and filesystem metadata
 * for tens of seconds so that quickly created and deleted files need
 * not waste disk bandwidth being created and removed. To realize this,
 * we append vnodes to a "workitem" queue. When running with a soft
 * updates implementation, most pending metadata dependencies should
 * not wait for more than a few seconds. Thus, mounted on block devices
 * are delayed only about a half the time that file data is delayed.
 * Similarly, directory updates are more critical, so are only delayed
 * about a third the time that file data is delayed. Thus, there are
 * SYNCER_MAXDELAY queues that are processed round-robin at a rate of
 * one each second (driven off the filesystem syncer process). The
 * syncer_delayno variable indicates the next queue that is to be processed.
 * Items that need to be processed soon are placed in this queue:
 *
 *	syncer_workitem_pending[syncer_delayno]
 *
 * A delay of fifteen seconds is done by placing the request fifteen
 * entries later in the queue:
 *
 *	syncer_workitem_pending[(syncer_delayno + 15) & syncer_mask]
 *
 */
static int syncer_delayno;
static long syncer_mask;
LIST_HEAD(synclist, bufobj);
static struct synclist *syncer_workitem_pending;
/*
 * The sync_mtx protects:
 *	bo->bo_synclist
 *	sync_vnode_count
 *	syncer_delayno
 *	syncer_state
 *	syncer_workitem_pending
 *	syncer_worklist_len
 *	rushjob
 */
static struct mtx sync_mtx;
static struct cv sync_wakeup;

#define SYNCER_MAXDELAY		32
static int syncer_maxdelay = SYNCER_MAXDELAY;	/* maximum delay time */
static int syncdelay = 30;		/* max time to delay syncing data */
static int filedelay = 30;		/* time to delay syncing files */
SYSCTL_INT(_kern, OID_AUTO, filedelay, CTLFLAG_RW, &filedelay, 0,
    "Time to delay syncing files (in seconds)");
static int dirdelay = 29;		/* time to delay syncing directories */
SYSCTL_INT(_kern, OID_AUTO, dirdelay, CTLFLAG_RW, &dirdelay, 0,
    "Time to delay syncing directories (in seconds)");
static int metadelay = 28;		/* time to delay syncing metadata */
SYSCTL_INT(_kern, OID_AUTO, metadelay, CTLFLAG_RW, &metadelay, 0,
    "Time to delay syncing metadata (in seconds)");
static int rushjob;		/* number of slots to run ASAP */
static int stat_rush_requests;	/* number of times I/O speeded up */
SYSCTL_INT(_debug, OID_AUTO, rush_requests, CTLFLAG_RW, &stat_rush_requests, 0,
    "Number of times I/O speeded up (rush requests)");

/*
 * When shutting down the syncer, run it at four times normal speed.
 */
#define SYNCER_SHUTDOWN_SPEEDUP		4
static int sync_vnode_count;
static int syncer_worklist_len;
static enum { SYNCER_RUNNING, SYNCER_SHUTTING_DOWN, SYNCER_FINAL_DELAY }
    syncer_state;

/* Target for maximum number of vnodes. */
int desiredvnodes;
static int gapvnodes;		/* gap between wanted and desired */
static int vhiwat;		/* enough extras after expansion */
static int vlowat;		/* minimal extras before expansion */
static int vstir;		/* nonzero to stir non-free vnodes */
static volatile int vsmalltrigger = 8;	/* pref to keep if > this many pages */

static int
sysctl_update_desiredvnodes(SYSCTL_HANDLER_ARGS)
{
	int error, old_desiredvnodes;

	old_desiredvnodes = desiredvnodes;
	if ((error = sysctl_handle_int(oidp, arg1, arg2, req)) != 0)
		return (error);
	if (old_desiredvnodes != desiredvnodes) {
		wantfreevnodes = desiredvnodes / 4;
		/* XXX locking seems to be incomplete. */
		vfs_hash_changesize(desiredvnodes);
		cache_changesize(desiredvnodes);
	}
	return (0);
}

SYSCTL_PROC(_kern, KERN_MAXVNODES, maxvnodes,
    CTLTYPE_INT | CTLFLAG_MPSAFE | CTLFLAG_RW, &desiredvnodes, 0,
    sysctl_update_desiredvnodes, "I", "Target for maximum number of vnodes");
SYSCTL_ULONG(_kern, OID_AUTO, minvnodes, CTLFLAG_RW,
    &wantfreevnodes, 0, "Old name for vfs.wantfreevnodes (legacy)");
static int vnlru_nowhere;
SYSCTL_INT(_debug, OID_AUTO, vnlru_nowhere, CTLFLAG_RW,
    &vnlru_nowhere, 0, "Number of times the vnlru process ran without success");

/* Shift count for (uintptr_t)vp to initialize vp->v_hash. */
static int vnsz2log;

/*
 * Support for the bufobj clean & dirty pctrie.
 */
static void *
buf_trie_alloc(struct pctrie *ptree)
{

	return uma_zalloc(buf_trie_zone, M_NOWAIT);
}

static void
buf_trie_free(struct pctrie *ptree, void *node)
{

	uma_zfree(buf_trie_zone, node);
}
PCTRIE_DEFINE(BUF, buf, b_lblkno, buf_trie_alloc, buf_trie_free);

/*
 * Initialize the vnode management data structures.
 *
 * Reevaluate the following cap on the number of vnodes after the physical
 * memory size exceeds 512GB.  In the limit, as the physical memory size
 * grows, the ratio of the memory size in KB to vnodes approaches 64:1.
 */
#ifndef	MAXVNODES_MAX
#define	MAXVNODES_MAX	(512 * 1024 * 1024 / 64)	/* 8M */
#endif

/*
 * Initialize a vnode as it first enters the zone.
 */
static int
vnode_init(void *mem, int size, int flags)
{
	struct vnode *vp;

	vp = mem;
	bzero(vp, size);
	/*
	 * Setup locks.
	 */
	vp->v_vnlock = &vp->v_lock;
	mtx_init(&vp->v_interlock, "vnode interlock", NULL, MTX_DEF);
	/*
	 * By default, don't allow shared locks unless filesystems opt-in.
	 */
	lockinit(vp->v_vnlock, PVFS, "vnode", VLKTIMEOUT,
	    LK_NOSHARE | LK_IS_VNODE);
	/*
	 * Initialize bufobj.
	 */
	bufobj_init(&vp->v_bufobj, vp);
	/*
	 * Initialize namecache.
	 */
	LIST_INIT(&vp->v_cache_src);
	TAILQ_INIT(&vp->v_cache_dst);
	/*
	 * Initialize rangelocks.
	 */
	rangelock_init(&vp->v_rl);
	return (0);
}

/*
 * Free a vnode when it is cleared from the zone.
 */
static void
vnode_fini(void *mem, int size)
{
	struct vnode *vp;
	struct bufobj *bo;

	vp = mem;
	rangelock_destroy(&vp->v_rl);
	lockdestroy(vp->v_vnlock);
	mtx_destroy(&vp->v_interlock);
	bo = &vp->v_bufobj;
	rw_destroy(BO_LOCKPTR(bo));
}

/*
 * Provide the size of NFS nclnode and NFS fh for calculation of the
 * vnode memory consumption.  The size is specified directly to
 * eliminate dependency on NFS-private header.
 *
 * Other filesystems may use bigger or smaller (like UFS and ZFS)
 * private inode data, but the NFS-based estimation is ample enough.
 * Still, we care about differences in the size between 64- and 32-bit
 * platforms.
 *
 * Namecache structure size is heuristically
 * sizeof(struct namecache_ts) + CACHE_PATH_CUTOFF + 1.
 */
#ifdef _LP64
#define	NFS_NCLNODE_SZ	(528 + 64)
#define	NC_SZ		148
#else
#define	NFS_NCLNODE_SZ	(360 + 32)
#define	NC_SZ		92
#endif

static void
vntblinit(void *dummy __unused)
{
	u_int i;
	int physvnodes, virtvnodes;

	/*
	 * Desiredvnodes is a function of the physical memory size and the
	 * kernel's heap size.  Generally speaking, it scales with the
	 * physical memory size.  The ratio of desiredvnodes to the physical
	 * memory size is 1:16 until desiredvnodes exceeds 98,304.
	 * Thereafter, the
	 * marginal ratio of desiredvnodes to the physical memory size is
	 * 1:64.  However, desiredvnodes is limited by the kernel's heap
	 * size.  The memory required by desiredvnodes vnodes and vm objects
	 * must not exceed 1/10th of the kernel's heap size.
	 */
	physvnodes = maxproc + pgtok(vm_cnt.v_page_count) / 64 +
	    3 * min(98304 * 16, pgtok(vm_cnt.v_page_count)) / 64;
	virtvnodes = vm_kmem_size / (10 * (sizeof(struct vm_object) +
	    sizeof(struct vnode) + NC_SZ * ncsizefactor + NFS_NCLNODE_SZ));
	desiredvnodes = min(physvnodes, virtvnodes);
	if (desiredvnodes > MAXVNODES_MAX) {
		if (bootverbose)
			printf("Reducing kern.maxvnodes %d -> %d\n",
			    desiredvnodes, MAXVNODES_MAX);
		desiredvnodes = MAXVNODES_MAX;
	}
	wantfreevnodes = desiredvnodes / 4;
	mtx_init(&mntid_mtx, "mntid", NULL, MTX_DEF);
	TAILQ_INIT(&vnode_free_list);
	mtx_init(&vnode_free_list_mtx, "vnode_free_list", NULL, MTX_DEF);
	vnode_zone = uma_zcreate("VNODE", sizeof (struct vnode), NULL, NULL,
	    vnode_init, vnode_fini, UMA_ALIGN_PTR, 0);
	vnodepoll_zone = uma_zcreate("VNODEPOLL", sizeof (struct vpollinfo),
	    NULL, NULL, NULL, NULL, UMA_ALIGN_PTR, 0);
	/*
	 * Preallocate enough nodes to support one-per buf so that
	 * we can not fail an insert.  reassignbuf() callers can not
	 * tolerate the insertion failure.
	 */
	buf_trie_zone = uma_zcreate("BUF TRIE", pctrie_node_size(),
	    NULL, NULL, pctrie_zone_init, NULL, UMA_ALIGN_PTR, 
	    UMA_ZONE_NOFREE | UMA_ZONE_VM);
	uma_prealloc(buf_trie_zone, nbuf);

	vnodes_created = counter_u64_alloc(M_WAITOK);
	recycles_count = counter_u64_alloc(M_WAITOK);
	free_owe_inact = counter_u64_alloc(M_WAITOK);

	/*
	 * Initialize the filesystem syncer.
	 */
	syncer_workitem_pending = hashinit(syncer_maxdelay, M_VNODE,
	    &syncer_mask);
	syncer_maxdelay = syncer_mask + 1;
	mtx_init(&sync_mtx, "Syncer mtx", NULL, MTX_DEF);
	cv_init(&sync_wakeup, "syncer");
	for (i = 1; i <= sizeof(struct vnode); i <<= 1)
		vnsz2log++;
	vnsz2log--;
}
SYSINIT(vfs, SI_SUB_VFS, SI_ORDER_FIRST, vntblinit, NULL);


/*
 * Mark a mount point as busy. Used to synchronize access and to delay
 * unmounting. Eventually, mountlist_mtx is not released on failure.
 *
 * vfs_busy() is a custom lock, it can block the caller.
 * vfs_busy() only sleeps if the unmount is active on the mount point.
 * For a mountpoint mp, vfs_busy-enforced lock is before lock of any
 * vnode belonging to mp.
 *
 * Lookup uses vfs_busy() to traverse mount points.
 * root fs			var fs
 * / vnode lock		A	/ vnode lock (/var)		D
 * /var vnode lock	B	/log vnode lock(/var/log)	E
 * vfs_busy lock	C	vfs_busy lock			F
 *
 * Within each file system, the lock order is C->A->B and F->D->E.
 *
 * When traversing across mounts, the system follows that lock order:
 *
 *        C->A->B
 *              |
 *              +->F->D->E
 *
 * The lookup() process for namei("/var") illustrates the process:
 *  VOP_LOOKUP() obtains B while A is held
 *  vfs_busy() obtains a shared lock on F while A and B are held
 *  vput() releases lock on B
 *  vput() releases lock on A
 *  VFS_ROOT() obtains lock on D while shared lock on F is held
 *  vfs_unbusy() releases shared lock on F
 *  vn_lock() obtains lock on deadfs vnode vp_crossmp instead of A.
 *    Attempt to lock A (instead of vp_crossmp) while D is held would
 *    violate the global order, causing deadlocks.
 *
 * dounmount() locks B while F is drained.
 */
int
vfs_busy(struct mount *mp, int flags)
{

	MPASS((flags & ~MBF_MASK) == 0);
	CTR3(KTR_VFS, "%s: mp %p with flags %d", __func__, mp, flags);

	MNT_ILOCK(mp);
	MNT_REF(mp);
	/*
	 * If mount point is currently being unmounted, sleep until the
	 * mount point fate is decided.  If thread doing the unmounting fails,
	 * it will clear MNTK_UNMOUNT flag before waking us up, indicating
	 * that this mount point has survived the unmount attempt and vfs_busy
	 * should retry.  Otherwise the unmounter thread will set MNTK_REFEXPIRE
	 * flag in addition to MNTK_UNMOUNT, indicating that mount point is
	 * about to be really destroyed.  vfs_busy needs to release its
	 * reference on the mount point in this case and return with ENOENT,
	 * telling the caller that mount mount it tried to busy is no longer
	 * valid.
	 */
	while (mp->mnt_kern_flag & MNTK_UNMOUNT) {
		if (flags & MBF_NOWAIT || mp->mnt_kern_flag & MNTK_REFEXPIRE) {
			MNT_REL(mp);
			MNT_IUNLOCK(mp);
			CTR1(KTR_VFS, "%s: failed busying before sleeping",
			    __func__);
			return (ENOENT);
		}
		if (flags & MBF_MNTLSTLOCK)
			mtx_unlock(&mountlist_mtx);
		mp->mnt_kern_flag |= MNTK_MWAIT;
		msleep(mp, MNT_MTX(mp), PVFS | PDROP, "vfs_busy", 0);
		if (flags & MBF_MNTLSTLOCK)
			mtx_lock(&mountlist_mtx);
		MNT_ILOCK(mp);
	}
	if (flags & MBF_MNTLSTLOCK)
		mtx_unlock(&mountlist_mtx);
	mp->mnt_lockref++;
	MNT_IUNLOCK(mp);
	return (0);
}

/*
 * Free a busy filesystem.
 */
void
vfs_unbusy(struct mount *mp)
{

	CTR2(KTR_VFS, "%s: mp %p", __func__, mp);
	MNT_ILOCK(mp);
	MNT_REL(mp);
	KASSERT(mp->mnt_lockref > 0, ("negative mnt_lockref"));
	mp->mnt_lockref--;
	if (mp->mnt_lockref == 0 && (mp->mnt_kern_flag & MNTK_DRAINING) != 0) {
		MPASS(mp->mnt_kern_flag & MNTK_UNMOUNT);
		CTR1(KTR_VFS, "%s: waking up waiters", __func__);
		mp->mnt_kern_flag &= ~MNTK_DRAINING;
		wakeup(&mp->mnt_lockref);
	}
	MNT_IUNLOCK(mp);
}

/*
 * Lookup a mount point by filesystem identifier.
 */
struct mount *
vfs_getvfs(fsid_t *fsid)
{
	struct mount *mp;

	CTR2(KTR_VFS, "%s: fsid %p", __func__, fsid);
	mtx_lock(&mountlist_mtx);
	TAILQ_FOREACH(mp, &mountlist, mnt_list) {
		if (mp->mnt_stat.f_fsid.val[0] == fsid->val[0] &&
		    mp->mnt_stat.f_fsid.val[1] == fsid->val[1]) {
			vfs_ref(mp);
			mtx_unlock(&mountlist_mtx);
			return (mp);
		}
	}
	mtx_unlock(&mountlist_mtx);
	CTR2(KTR_VFS, "%s: lookup failed for %p id", __func__, fsid);
	return ((struct mount *) 0);
}

/*
 * Lookup a mount point by filesystem identifier, busying it before
 * returning.
 *
 * To avoid congestion on mountlist_mtx, implement simple direct-mapped
 * cache for popular filesystem identifiers.  The cache is lockess, using
 * the fact that struct mount's are never freed.  In worst case we may
 * get pointer to unmounted or even different filesystem, so we have to
 * check what we got, and go slow way if so.
 */
struct mount *
vfs_busyfs(fsid_t *fsid)
{
#define	FSID_CACHE_SIZE	256
	typedef struct mount * volatile vmp_t;
	static vmp_t cache[FSID_CACHE_SIZE];
	struct mount *mp;
	int error;
	uint32_t hash;

	CTR2(KTR_VFS, "%s: fsid %p", __func__, fsid);
	hash = fsid->val[0] ^ fsid->val[1];
	hash = (hash >> 16 ^ hash) & (FSID_CACHE_SIZE - 1);
	mp = cache[hash];
	if (mp == NULL ||
	    mp->mnt_stat.f_fsid.val[0] != fsid->val[0] ||
	    mp->mnt_stat.f_fsid.val[1] != fsid->val[1])
		goto slow;
	if (vfs_busy(mp, 0) != 0) {
		cache[hash] = NULL;
		goto slow;
	}
	if (mp->mnt_stat.f_fsid.val[0] == fsid->val[0] &&
	    mp->mnt_stat.f_fsid.val[1] == fsid->val[1])
		return (mp);
	else
	    vfs_unbusy(mp);

slow:
	mtx_lock(&mountlist_mtx);
	TAILQ_FOREACH(mp, &mountlist, mnt_list) {
		if (mp->mnt_stat.f_fsid.val[0] == fsid->val[0] &&
		    mp->mnt_stat.f_fsid.val[1] == fsid->val[1]) {
			error = vfs_busy(mp, MBF_MNTLSTLOCK);
			if (error) {
				cache[hash] = NULL;
				mtx_unlock(&mountlist_mtx);
				return (NULL);
			}
			cache[hash] = mp;
			return (mp);
		}
	}
	CTR2(KTR_VFS, "%s: lookup failed for %p id", __func__, fsid);
	mtx_unlock(&mountlist_mtx);
	return ((struct mount *) 0);
}

/*
 * Check if a user can access privileged mount options.
 */
int
vfs_suser(struct mount *mp, struct thread *td)
{
	int error;

	if (jailed(td->td_ucred)) {
		/*
		 * If the jail of the calling thread lacks permission for
		 * this type of file system, deny immediately.
		 */
		if (!prison_allow(td->td_ucred, mp->mnt_vfc->vfc_prison_flag))
			return (EPERM);

		/*
		 * If the file system was mounted outside the jail of the
		 * calling thread, deny immediately.
		 */
		if (prison_check(td->td_ucred, mp->mnt_cred) != 0)
			return (EPERM);
	}

	/*
	 * If file system supports delegated administration, we don't check
	 * for the PRIV_VFS_MOUNT_OWNER privilege - it will be better verified
	 * by the file system itself.
	 * If this is not the user that did original mount, we check for
	 * the PRIV_VFS_MOUNT_OWNER privilege.
	 */
	if (!(mp->mnt_vfc->vfc_flags & VFCF_DELEGADMIN) &&
	    mp->mnt_cred->cr_uid != td->td_ucred->cr_uid) {
		if ((error = priv_check(td, PRIV_VFS_MOUNT_OWNER)) != 0)
			return (error);
	}
	return (0);
}

/*
 * Get a new unique fsid.  Try to make its val[0] unique, since this value
 * will be used to create fake device numbers for stat().  Also try (but
 * not so hard) make its val[0] unique mod 2^16, since some emulators only
 * support 16-bit device numbers.  We end up with unique val[0]'s for the
 * first 2^16 calls and unique val[0]'s mod 2^16 for the first 2^8 calls.
 *
 * Keep in mind that several mounts may be running in parallel.  Starting
 * the search one past where the previous search terminated is both a
 * micro-optimization and a defense against returning the same fsid to
 * different mounts.
 */
void
vfs_getnewfsid(struct mount *mp)
{
	static uint16_t mntid_base;
	struct mount *nmp;
	fsid_t tfsid;
	int mtype;

	CTR2(KTR_VFS, "%s: mp %p", __func__, mp);
	mtx_lock(&mntid_mtx);
	mtype = mp->mnt_vfc->vfc_typenum;
	tfsid.val[1] = mtype;
	mtype = (mtype & 0xFF) << 24;
	for (;;) {
		tfsid.val[0] = makedev(255,
		    mtype | ((mntid_base & 0xFF00) << 8) | (mntid_base & 0xFF));
		mntid_base++;
		if ((nmp = vfs_getvfs(&tfsid)) == NULL)
			break;
		vfs_rel(nmp);
	}
	mp->mnt_stat.f_fsid.val[0] = tfsid.val[0];
	mp->mnt_stat.f_fsid.val[1] = tfsid.val[1];
	mtx_unlock(&mntid_mtx);
}

/*
 * Knob to control the precision of file timestamps:
 *
 *   0 = seconds only; nanoseconds zeroed.
 *   1 = seconds and nanoseconds, accurate within 1/HZ.
 *   2 = seconds and nanoseconds, truncated to microseconds.
 * >=3 = seconds and nanoseconds, maximum precision.
 */
enum { TSP_SEC, TSP_HZ, TSP_USEC, TSP_NSEC };

static int timestamp_precision = TSP_USEC;
SYSCTL_INT(_vfs, OID_AUTO, timestamp_precision, CTLFLAG_RW,
    &timestamp_precision, 0, "File timestamp precision (0: seconds, "
    "1: sec + ns accurate to 1/HZ, 2: sec + ns truncated to us, "
    "3+: sec + ns (max. precision))");

/*
 * Get a current timestamp.
 */
void
vfs_timestamp(struct timespec *tsp)
{
	struct timeval tv;

	switch (timestamp_precision) {
	case TSP_SEC:
		tsp->tv_sec = time_second;
		tsp->tv_nsec = 0;
		break;
	case TSP_HZ:
		getnanotime(tsp);
		break;
	case TSP_USEC:
		microtime(&tv);
		TIMEVAL_TO_TIMESPEC(&tv, tsp);
		break;
	case TSP_NSEC:
	default:
		nanotime(tsp);
		break;
	}
}

/*
 * Set vnode attributes to VNOVAL
 */
void
vattr_null(struct vattr *vap)
{

	vap->va_type = VNON;
	vap->va_size = VNOVAL;
	vap->va_bytes = VNOVAL;
	vap->va_mode = VNOVAL;
	vap->va_nlink = VNOVAL;
	vap->va_uid = VNOVAL;
	vap->va_gid = VNOVAL;
	vap->va_fsid = VNOVAL;
	vap->va_fileid = VNOVAL;
	vap->va_blocksize = VNOVAL;
	vap->va_rdev = VNOVAL;
	vap->va_atime.tv_sec = VNOVAL;
	vap->va_atime.tv_nsec = VNOVAL;
	vap->va_mtime.tv_sec = VNOVAL;
	vap->va_mtime.tv_nsec = VNOVAL;
	vap->va_ctime.tv_sec = VNOVAL;
	vap->va_ctime.tv_nsec = VNOVAL;
	vap->va_birthtime.tv_sec = VNOVAL;
	vap->va_birthtime.tv_nsec = VNOVAL;
	vap->va_flags = VNOVAL;
	vap->va_gen = VNOVAL;
	vap->va_vaflags = 0;
}

/*
 * This routine is called when we have too many vnodes.  It attempts
 * to free <count> vnodes and will potentially free vnodes that still
 * have VM backing store (VM backing store is typically the cause
 * of a vnode blowout so we want to do this).  Therefore, this operation
 * is not considered cheap.
 *
 * A number of conditions may prevent a vnode from being reclaimed.
 * the buffer cache may have references on the vnode, a directory
 * vnode may still have references due to the namei cache representing
 * underlying files, or the vnode may be in active use.   It is not
 * desirable to reuse such vnodes.  These conditions may cause the
 * number of vnodes to reach some minimum value regardless of what
 * you set kern.maxvnodes to.  Do not set kern.maxvnodes too low.
 */
static int
vlrureclaim(struct mount *mp, int reclaim_nc_src, int trigger)
{
	struct vnode *vp;
	int count, done, target;

	done = 0;
	vn_start_write(NULL, &mp, V_WAIT);
	MNT_ILOCK(mp);
	count = mp->mnt_nvnodelistsize;
	target = count * (int64_t)gapvnodes / imax(desiredvnodes, 1);
	target = target / 10 + 1;
	while (count != 0 && done < target) {
		vp = TAILQ_FIRST(&mp->mnt_nvnodelist);
		while (vp != NULL && vp->v_type == VMARKER)
			vp = TAILQ_NEXT(vp, v_nmntvnodes);
		if (vp == NULL)
			break;
		/*
		 * XXX LRU is completely broken for non-free vnodes.  First
		 * by calling here in mountpoint order, then by moving
		 * unselected vnodes to the end here, and most grossly by
		 * removing the vlruvp() function that was supposed to
		 * maintain the order.  (This function was born broken
		 * since syncer problems prevented it doing anything.)  The
		 * order is closer to LRC (C = Created).
		 *
		 * LRU reclaiming of vnodes seems to have last worked in
		 * FreeBSD-3 where LRU wasn't mentioned under any spelling.
		 * Then there was no hold count, and inactive vnodes were
		 * simply put on the free list in LRU order.  The separate
		 * lists also break LRU.  We prefer to reclaim from the
		 * free list for technical reasons.  This tends to thrash
		 * the free list to keep very unrecently used held vnodes.
		 * The problem is mitigated by keeping the free list large.
		 */
		TAILQ_REMOVE(&mp->mnt_nvnodelist, vp, v_nmntvnodes);
		TAILQ_INSERT_TAIL(&mp->mnt_nvnodelist, vp, v_nmntvnodes);
		--count;
		if (!VI_TRYLOCK(vp))
			goto next_iter;
		/*
		 * If it's been deconstructed already, it's still
		 * referenced, or it exceeds the trigger, skip it.
		 * Also skip free vnodes.  We are trying to make space
		 * to expand the free list, not reduce it.
		 */
		if (vp->v_usecount ||
		    (!reclaim_nc_src && !LIST_EMPTY(&vp->v_cache_src)) ||
		    ((vp->v_iflag & VI_FREE) != 0) ||
		    (vp->v_iflag & VI_DOOMED) != 0 || (vp->v_object != NULL &&
		    vp->v_object->resident_page_count > trigger)) {
			VI_UNLOCK(vp);
			goto next_iter;
		}
		MNT_IUNLOCK(mp);
		vholdl(vp);
		if (VOP_LOCK(vp, LK_INTERLOCK|LK_EXCLUSIVE|LK_NOWAIT)) {
			vdrop(vp);
			goto next_iter_mntunlocked;
		}
		VI_LOCK(vp);
		/*
		 * v_usecount may have been bumped after VOP_LOCK() dropped
		 * the vnode interlock and before it was locked again.
		 *
		 * It is not necessary to recheck VI_DOOMED because it can
		 * only be set by another thread that holds both the vnode
		 * lock and vnode interlock.  If another thread has the
		 * vnode lock before we get to VOP_LOCK() and obtains the
		 * vnode interlock after VOP_LOCK() drops the vnode
		 * interlock, the other thread will be unable to drop the
		 * vnode lock before our VOP_LOCK() call fails.
		 */
		if (vp->v_usecount ||
		    (!reclaim_nc_src && !LIST_EMPTY(&vp->v_cache_src)) ||
		    (vp->v_iflag & VI_FREE) != 0 ||
		    (vp->v_object != NULL &&
		    vp->v_object->resident_page_count > trigger)) {
			VOP_UNLOCK(vp, LK_INTERLOCK);
			vdrop(vp);
			goto next_iter_mntunlocked;
		}
		KASSERT((vp->v_iflag & VI_DOOMED) == 0,
		    ("VI_DOOMED unexpectedly detected in vlrureclaim()"));
		counter_u64_add(recycles_count, 1);
		vgonel(vp);
		VOP_UNLOCK(vp, 0);
		vdropl(vp);
		done++;
next_iter_mntunlocked:
		if (!should_yield())
			goto relock_mnt;
		goto yield;
next_iter:
		if (!should_yield())
			continue;
		MNT_IUNLOCK(mp);
yield:
		kern_yield(PRI_USER);
relock_mnt:
		MNT_ILOCK(mp);
	}
	MNT_IUNLOCK(mp);
	vn_finished_write(mp);
	return done;
}

static int max_vnlru_free = 10000; /* limit on vnode free requests per call */
SYSCTL_INT(_debug, OID_AUTO, max_vnlru_free, CTLFLAG_RW, &max_vnlru_free,
    0,
    "limit on vnode free requests per call to the vnlru_free routine");

/*
 * Attempt to reduce the free list by the requested amount.
 */
static void
vnlru_free_locked(int count, struct vfsops *mnt_op)
{
	struct vnode *vp;
	struct mount *mp;
	bool tried_batches;

	tried_batches = false;
	mtx_assert(&vnode_free_list_mtx, MA_OWNED);
	if (count > max_vnlru_free)
		count = max_vnlru_free;
	for (; count > 0; count--) {
		vp = TAILQ_FIRST(&vnode_free_list);
		/*
		 * The list can be modified while the free_list_mtx
		 * has been dropped and vp could be NULL here.
		 */
		if (vp == NULL) {
			if (tried_batches)
				break;
			mtx_unlock(&vnode_free_list_mtx);
			vnlru_return_batches(mnt_op);
			tried_batches = true;
			mtx_lock(&vnode_free_list_mtx);
			continue;
		}

		VNASSERT(vp->v_op != NULL, vp,
		    ("vnlru_free: vnode already reclaimed."));
		KASSERT((vp->v_iflag & VI_FREE) != 0,
		    ("Removing vnode not on freelist"));
		KASSERT((vp->v_iflag & VI_ACTIVE) == 0,
		    ("Mangling active vnode"));
		TAILQ_REMOVE(&vnode_free_list, vp, v_actfreelist);

		/*
		 * Don't recycle if our vnode is from different type
		 * of mount point.  Note that mp is type-safe, the
		 * check does not reach unmapped address even if
		 * vnode is reclaimed.
		 * Don't recycle if we can't get the interlock without
		 * blocking.
		 */
		if ((mnt_op != NULL && (mp = vp->v_mount) != NULL &&
		    mp->mnt_op != mnt_op) || !VI_TRYLOCK(vp)) {
			TAILQ_INSERT_TAIL(&vnode_free_list, vp, v_actfreelist);
			continue;
		}
		VNASSERT((vp->v_iflag & VI_FREE) != 0 && vp->v_holdcnt == 0,
		    vp, ("vp inconsistent on freelist"));

		/*
		 * The clear of VI_FREE prevents activation of the
		 * vnode.  There is no sense in putting the vnode on
		 * the mount point active list, only to remove it
		 * later during recycling.  Inline the relevant part
		 * of vholdl(), to avoid triggering assertions or
		 * activating.
		 */
		freevnodes--;
		vp->v_iflag &= ~VI_FREE;
		VNODE_REFCOUNT_FENCE_REL();
		refcount_acquire(&vp->v_holdcnt);

		mtx_unlock(&vnode_free_list_mtx);
		VI_UNLOCK(vp);
		vtryrecycle(vp);
		/*
		 * If the recycled succeeded this vdrop will actually free
		 * the vnode.  If not it will simply place it back on
		 * the free list.
		 */
		vdrop(vp);
		mtx_lock(&vnode_free_list_mtx);
	}
}

void
vnlru_free(int count, struct vfsops *mnt_op)
{

	mtx_lock(&vnode_free_list_mtx);
	vnlru_free_locked(count, mnt_op);
	mtx_unlock(&vnode_free_list_mtx);
}


/* XXX some names and initialization are bad for limits and watermarks. */
static int
vspace(void)
{
	int space;

	gapvnodes = imax(desiredvnodes - wantfreevnodes, 100);
	vhiwat = gapvnodes / 11; /* 9% -- just under the 10% in vlrureclaim() */
	vlowat = vhiwat / 2;
	if (numvnodes > desiredvnodes)
		return (0);
	space = desiredvnodes - numvnodes;
	if (freevnodes > wantfreevnodes)
		space += freevnodes - wantfreevnodes;
	return (space);
}

static void
vnlru_return_batch_locked(struct mount *mp)
{
	struct vnode *vp;

	mtx_assert(&mp->mnt_listmtx, MA_OWNED);

	if (mp->mnt_tmpfreevnodelistsize == 0)
		return;

	TAILQ_FOREACH(vp, &mp->mnt_tmpfreevnodelist, v_actfreelist) {
		VNASSERT((vp->v_mflag & VMP_TMPMNTFREELIST) != 0, vp,
		    ("vnode without VMP_TMPMNTFREELIST on mnt_tmpfreevnodelist"));
		vp->v_mflag &= ~VMP_TMPMNTFREELIST;
	}
	mtx_lock(&vnode_free_list_mtx);
	TAILQ_CONCAT(&vnode_free_list, &mp->mnt_tmpfreevnodelist, v_actfreelist);
	freevnodes += mp->mnt_tmpfreevnodelistsize;
	mtx_unlock(&vnode_free_list_mtx);
	mp->mnt_tmpfreevnodelistsize = 0;
}

static void
vnlru_return_batch(struct mount *mp)
{

	mtx_lock(&mp->mnt_listmtx);
	vnlru_return_batch_locked(mp);
	mtx_unlock(&mp->mnt_listmtx);
}

static void
vnlru_return_batches(struct vfsops *mnt_op)
{
	struct mount *mp, *nmp;
	bool need_unbusy;

	mtx_lock(&mountlist_mtx);
	for (mp = TAILQ_FIRST(&mountlist); mp != NULL; mp = nmp) {
		need_unbusy = false;
		if (mnt_op != NULL && mp->mnt_op != mnt_op)
			goto next;
		if (mp->mnt_tmpfreevnodelistsize == 0)
			goto next;
		if (vfs_busy(mp, MBF_NOWAIT | MBF_MNTLSTLOCK) == 0) {
			vnlru_return_batch(mp);
			need_unbusy = true;
			mtx_lock(&mountlist_mtx);
		}
next:
		nmp = TAILQ_NEXT(mp, mnt_list);
		if (need_unbusy)
			vfs_unbusy(mp);
	}
	mtx_unlock(&mountlist_mtx);
}

/*
 * Attempt to recycle vnodes in a context that is always safe to block.
 * Calling vlrurecycle() from the bowels of filesystem code has some
 * interesting deadlock problems.
 */
static struct proc *vnlruproc;
static int vnlruproc_sig;

static void
vnlru_proc(void)
{
	struct mount *mp, *nmp;
	unsigned long onumvnodes;
	int done, force, reclaim_nc_src, trigger, usevnodes;

	EVENTHANDLER_REGISTER(shutdown_pre_sync, kproc_shutdown, vnlruproc,
	    SHUTDOWN_PRI_FIRST);

	force = 0;
	for (;;) {
		kproc_suspend_check(vnlruproc);
		mtx_lock(&vnode_free_list_mtx);
		/*
		 * If numvnodes is too large (due to desiredvnodes being
		 * adjusted using its sysctl, or emergency growth), first
		 * try to reduce it by discarding from the free list.
		 */
		if (numvnodes > desiredvnodes)
			vnlru_free_locked(numvnodes - desiredvnodes, NULL);
		/*
		 * Sleep if the vnode cache is in a good state.  This is
		 * when it is not over-full and has space for about a 4%
		 * or 9% expansion (by growing its size or inexcessively
		 * reducing its free list).  Otherwise, try to reclaim
		 * space for a 10% expansion.
		 */
		if (vstir && force == 0) {
			force = 1;
			vstir = 0;
		}
		if (vspace() >= vlowat && force == 0) {
			vnlruproc_sig = 0;
			wakeup(&vnlruproc_sig);
			msleep(vnlruproc, &vnode_free_list_mtx,
			    PVFS|PDROP, "vlruwt", hz);
			continue;
		}
		mtx_unlock(&vnode_free_list_mtx);
		done = 0;
		onumvnodes = numvnodes;
		/*
		 * Calculate parameters for recycling.  These are the same
		 * throughout the loop to give some semblance of fairness.
		 * The trigger point is to avoid recycling vnodes with lots
		 * of resident pages.  We aren't trying to free memory; we
		 * are trying to recycle or at least free vnodes.
		 */
		if (numvnodes <= desiredvnodes)
			usevnodes = numvnodes - freevnodes;
		else
			usevnodes = numvnodes;
		if (usevnodes <= 0)
			usevnodes = 1;
		/*
		 * The trigger value is is chosen to give a conservatively
		 * large value to ensure that it alone doesn't prevent
		 * making progress.  The value can easily be so large that
		 * it is effectively infinite in some congested and
		 * misconfigured cases, and this is necessary.  Normally
		 * it is about 8 to 100 (pages), which is quite large.
		 */
		trigger = vm_cnt.v_page_count * 2 / usevnodes;
		if (force < 2)
			trigger = vsmalltrigger;
		reclaim_nc_src = force >= 3;
		mtx_lock(&mountlist_mtx);
		for (mp = TAILQ_FIRST(&mountlist); mp != NULL; mp = nmp) {
			if (vfs_busy(mp, MBF_NOWAIT | MBF_MNTLSTLOCK)) {
				nmp = TAILQ_NEXT(mp, mnt_list);
				continue;
			}
			done += vlrureclaim(mp, reclaim_nc_src, trigger);
			mtx_lock(&mountlist_mtx);
			nmp = TAILQ_NEXT(mp, mnt_list);
			vfs_unbusy(mp);
		}
		mtx_unlock(&mountlist_mtx);
		if (onumvnodes > desiredvnodes && numvnodes <= desiredvnodes)
			uma_reclaim();
		if (done == 0) {
			if (force == 0 || force == 1) {
				force = 2;
				continue;
			}
			if (force == 2) {
				force = 3;
				continue;
			}
			force = 0;
			vnlru_nowhere++;
			tsleep(vnlruproc, PPAUSE, "vlrup", hz * 3);
		} else
			kern_yield(PRI_USER);
		/*
		 * After becoming active to expand above low water, keep
		 * active until above high water.
		 */
		force = vspace() < vhiwat;
	}
}

static struct kproc_desc vnlru_kp = {
	"vnlru",
	vnlru_proc,
	&vnlruproc
};
SYSINIT(vnlru, SI_SUB_KTHREAD_UPDATE, SI_ORDER_FIRST, kproc_start,
    &vnlru_kp);
 
/*
 * Routines having to do with the management of the vnode table.
 */

/*
 * Try to recycle a freed vnode.  We abort if anyone picks up a reference
 * before we actually vgone().  This function must be called with the vnode
 * held to prevent the vnode from being returned to the free list midway
 * through vgone().
 */
static int
vtryrecycle(struct vnode *vp)
{
	struct mount *vnmp;

	CTR2(KTR_VFS, "%s: vp %p", __func__, vp);
	VNASSERT(vp->v_holdcnt, vp,
	    ("vtryrecycle: Recycling vp %p without a reference.", vp));
	/*
	 * This vnode may found and locked via some other list, if so we
	 * can't recycle it yet.
	 */
	if (VOP_LOCK(vp, LK_EXCLUSIVE | LK_NOWAIT) != 0) {
		CTR2(KTR_VFS,
		    "%s: impossible to recycle, vp %p lock is already held",
		    __func__, vp);
		return (EWOULDBLOCK);
	}
	/*
	 * Don't recycle if its filesystem is being suspended.
	 */
	if (vn_start_write(vp, &vnmp, V_NOWAIT) != 0) {
		VOP_UNLOCK(vp, 0);
		CTR2(KTR_VFS,
		    "%s: impossible to recycle, cannot start the write for %p",
		    __func__, vp);
		return (EBUSY);
	}
	/*
	 * If we got this far, we need to acquire the interlock and see if
	 * anyone picked up this vnode from another list.  If not, we will
	 * mark it with DOOMED via vgonel() so that anyone who does find it
	 * will skip over it.
	 */
	VI_LOCK(vp);
	if (vp->v_usecount) {
		VOP_UNLOCK(vp, LK_INTERLOCK);
		vn_finished_write(vnmp);
		CTR2(KTR_VFS,
		    "%s: impossible to recycle, %p is already referenced",
		    __func__, vp);
		return (EBUSY);
	}
	if ((vp->v_iflag & VI_DOOMED) == 0) {
		counter_u64_add(recycles_count, 1);
		vgonel(vp);
	}
	VOP_UNLOCK(vp, LK_INTERLOCK);
	vn_finished_write(vnmp);
	return (0);
}

static void
vcheckspace(void)
{

	if (vspace() < vlowat && vnlruproc_sig == 0) {
		vnlruproc_sig = 1;
		wakeup(vnlruproc);
	}
}

/*
 * Wait if necessary for space for a new vnode.
 */
static int
getnewvnode_wait(int suspended)
{

	mtx_assert(&vnode_free_list_mtx, MA_OWNED);
	if (numvnodes >= desiredvnodes) {
		if (suspended) {
			/*
			 * The file system is being suspended.  We cannot
			 * risk a deadlock here, so allow allocation of
			 * another vnode even if this would give too many.
			 */
			return (0);
		}
		if (vnlruproc_sig == 0) {
			vnlruproc_sig = 1;	/* avoid unnecessary wakeups */
			wakeup(vnlruproc);
		}
		msleep(&vnlruproc_sig, &vnode_free_list_mtx, PVFS,
		    "vlruwk", hz);
	}
	/* Post-adjust like the pre-adjust in getnewvnode(). */
	if (numvnodes + 1 > desiredvnodes && freevnodes > 1)
		vnlru_free_locked(1, NULL);
	return (numvnodes >= desiredvnodes ? ENFILE : 0);
}

/*
 * This hack is fragile, and probably not needed any more now that the
 * watermark handling works.
 */
void
getnewvnode_reserve(u_int count)
{
	struct thread *td;

	/* Pre-adjust like the pre-adjust in getnewvnode(), with any count. */
	/* XXX no longer so quick, but this part is not racy. */
	mtx_lock(&vnode_free_list_mtx);
	if (numvnodes + count > desiredvnodes && freevnodes > wantfreevnodes)
		vnlru_free_locked(ulmin(numvnodes + count - desiredvnodes,
		    freevnodes - wantfreevnodes), NULL);
	mtx_unlock(&vnode_free_list_mtx);

	td = curthread;
	/* First try to be quick and racy. */
	if (atomic_fetchadd_long(&numvnodes, count) + count <= desiredvnodes) {
		td->td_vp_reserv += count;
		vcheckspace();	/* XXX no longer so quick, but more racy */
		return;
	} else
		atomic_subtract_long(&numvnodes, count);

	mtx_lock(&vnode_free_list_mtx);
	while (count > 0) {
		if (getnewvnode_wait(0) == 0) {
			count--;
			td->td_vp_reserv++;
			atomic_add_long(&numvnodes, 1);
		}
	}
	vcheckspace();
	mtx_unlock(&vnode_free_list_mtx);
}

/*
 * This hack is fragile, especially if desiredvnodes or wantvnodes are
 * misconfgured or changed significantly.  Reducing desiredvnodes below
 * the reserved amount should cause bizarre behaviour like reducing it
 * below the number of active vnodes -- the system will try to reduce
 * numvnodes to match, but should fail, so the subtraction below should
 * not overflow.
 */
void
getnewvnode_drop_reserve(void)
{
	struct thread *td;

	td = curthread;
	atomic_subtract_long(&numvnodes, td->td_vp_reserv);
	td->td_vp_reserv = 0;
}

/*
 * Return the next vnode from the free list.
 */
int
getnewvnode(const char *tag, struct mount *mp, struct vop_vector *vops,
    struct vnode **vpp)
{
	struct vnode *vp;
	struct thread *td;
	struct lock_object *lo;
	static int cyclecount;
	int error __unused;

	CTR3(KTR_VFS, "%s: mp %p with tag %s", __func__, mp, tag);
	vp = NULL;
	td = curthread;
	if (td->td_vp_reserv > 0) {
		td->td_vp_reserv -= 1;
		goto alloc;
	}
	mtx_lock(&vnode_free_list_mtx);
	if (numvnodes < desiredvnodes)
		cyclecount = 0;
	else if (cyclecount++ >= freevnodes) {
		cyclecount = 0;
		vstir = 1;
	}
	/*
	 * Grow the vnode cache if it will not be above its target max
	 * after growing.  Otherwise, if the free list is nonempty, try
	 * to reclaim 1 item from it before growing the cache (possibly
	 * above its target max if the reclamation failed or is delayed).
	 * Otherwise, wait for some space.  In all cases, schedule
	 * vnlru_proc() if we are getting short of space.  The watermarks
	 * should be chosen so that we never wait or even reclaim from
	 * the free list to below its target minimum.
	 */
	if (numvnodes + 1 <= desiredvnodes)
		;
	else if (freevnodes > 0)
		vnlru_free_locked(1, NULL);
	else {
		error = getnewvnode_wait(mp != NULL && (mp->mnt_kern_flag &
		    MNTK_SUSPEND));
#if 0	/* XXX Not all VFS_VGET/ffs_vget callers check returns. */
		if (error != 0) {
			mtx_unlock(&vnode_free_list_mtx);
			return (error);
		}
#endif
	}
	vcheckspace();
	atomic_add_long(&numvnodes, 1);
	mtx_unlock(&vnode_free_list_mtx);
alloc:
	counter_u64_add(vnodes_created, 1);
	vp = (struct vnode *) uma_zalloc(vnode_zone, M_WAITOK);
	/*
	 * Locks are given the generic name "vnode" when created.
	 * Follow the historic practice of using the filesystem
	 * name when they allocated, e.g., "zfs", "ufs", "nfs, etc.
	 *
	 * Locks live in a witness group keyed on their name. Thus,
	 * when a lock is renamed, it must also move from the witness
	 * group of its old name to the witness group of its new name.
	 *
	 * The change only needs to be made when the vnode moves
	 * from one filesystem type to another. We ensure that each
	 * filesystem use a single static name pointer for its tag so
	 * that we can compare pointers rather than doing a strcmp().
	 */
	lo = &vp->v_vnlock->lock_object;
	if (lo->lo_name != tag) {
		lo->lo_name = tag;
		WITNESS_DESTROY(lo);
		WITNESS_INIT(lo, tag);
	}
	/*
	 * By default, don't allow shared locks unless filesystems opt-in.
	 */
	vp->v_vnlock->lock_object.lo_flags |= LK_NOSHARE;
	/*
	 * Finalize various vnode identity bits.
	 */
	KASSERT(vp->v_object == NULL, ("stale v_object %p", vp));
	KASSERT(vp->v_lockf == NULL, ("stale v_lockf %p", vp));
	KASSERT(vp->v_pollinfo == NULL, ("stale v_pollinfo %p", vp));
	vp->v_type = VNON;
	vp->v_tag = tag;
	vp->v_op = vops;
	v_init_counters(vp);
	vp->v_bufobj.bo_ops = &buf_ops_bio;
#ifdef DIAGNOSTIC
	if (mp == NULL && vops != &dead_vnodeops)
		printf("NULL mp in getnewvnode(9), tag %s\n", tag);
#endif
#ifdef MAC
	mac_vnode_init(vp);
	if (mp != NULL && (mp->mnt_flag & MNT_MULTILABEL) == 0)
		mac_vnode_associate_singlelabel(mp, vp);
#endif
	if (mp != NULL) {
		vp->v_bufobj.bo_bsize = mp->mnt_stat.f_iosize;
		if ((mp->mnt_kern_flag & MNTK_NOKNOTE) != 0)
			vp->v_vflag |= VV_NOKNOTE;
	}

	/*
	 * For the filesystems which do not use vfs_hash_insert(),
	 * still initialize v_hash to have vfs_hash_index() useful.
	 * E.g., nullfs uses vfs_hash_index() on the lower vnode for
	 * its own hashing.
	 */
	vp->v_hash = (uintptr_t)vp >> vnsz2log;

	*vpp = vp;
	return (0);
}

/*
 * Delete from old mount point vnode list, if on one.
 */
static void
delmntque(struct vnode *vp)
{
	struct mount *mp;
	int active;

	mp = vp->v_mount;
	if (mp == NULL)
		return;
	MNT_ILOCK(mp);
	VI_LOCK(vp);
	KASSERT(mp->mnt_activevnodelistsize <= mp->mnt_nvnodelistsize,
	    ("Active vnode list size %d > Vnode list size %d",
	     mp->mnt_activevnodelistsize, mp->mnt_nvnodelistsize));
	active = vp->v_iflag & VI_ACTIVE;
	vp->v_iflag &= ~VI_ACTIVE;
	if (active) {
		mtx_lock(&mp->mnt_listmtx);
		TAILQ_REMOVE(&mp->mnt_activevnodelist, vp, v_actfreelist);
		mp->mnt_activevnodelistsize--;
		mtx_unlock(&mp->mnt_listmtx);
	}
	vp->v_mount = NULL;
	VI_UNLOCK(vp);
	VNASSERT(mp->mnt_nvnodelistsize > 0, vp,
		("bad mount point vnode list size"));
	TAILQ_REMOVE(&mp->mnt_nvnodelist, vp, v_nmntvnodes);
	mp->mnt_nvnodelistsize--;
	MNT_REL(mp);
	MNT_IUNLOCK(mp);
}

static void
insmntque_stddtr(struct vnode *vp, void *dtr_arg)
{

	vp->v_data = NULL;
	vp->v_op = &dead_vnodeops;
	vgone(vp);
	vput(vp);
}

/*
 * Insert into list of vnodes for the new mount point, if available.
 */
int
insmntque1(struct vnode *vp, struct mount *mp,
	void (*dtr)(struct vnode *, void *), void *dtr_arg)
{

	KASSERT(vp->v_mount == NULL,
		("insmntque: vnode already on per mount vnode list"));
	VNASSERT(mp != NULL, vp, ("Don't call insmntque(foo, NULL)"));
	ASSERT_VOP_ELOCKED(vp, "insmntque: non-locked vp");

	/*
	 * We acquire the vnode interlock early to ensure that the
	 * vnode cannot be recycled by another process releasing a
	 * holdcnt on it before we get it on both the vnode list
	 * and the active vnode list. The mount mutex protects only
	 * manipulation of the vnode list and the vnode freelist
	 * mutex protects only manipulation of the active vnode list.
	 * Hence the need to hold the vnode interlock throughout.
	 */
	MNT_ILOCK(mp);
	VI_LOCK(vp);
	if (((mp->mnt_kern_flag & MNTK_NOINSMNTQ) != 0 &&
	    ((mp->mnt_kern_flag & MNTK_UNMOUNTF) != 0 ||
	    mp->mnt_nvnodelistsize == 0)) &&
	    (vp->v_vflag & VV_FORCEINSMQ) == 0) {
		VI_UNLOCK(vp);
		MNT_IUNLOCK(mp);
		if (dtr != NULL)
			dtr(vp, dtr_arg);
		return (EBUSY);
	}
	vp->v_mount = mp;
	MNT_REF(mp);
	TAILQ_INSERT_TAIL(&mp->mnt_nvnodelist, vp, v_nmntvnodes);
	VNASSERT(mp->mnt_nvnodelistsize >= 0, vp,
		("neg mount point vnode list size"));
	mp->mnt_nvnodelistsize++;
	KASSERT((vp->v_iflag & VI_ACTIVE) == 0,
	    ("Activating already active vnode"));
	vp->v_iflag |= VI_ACTIVE;
	mtx_lock(&mp->mnt_listmtx);
	TAILQ_INSERT_HEAD(&mp->mnt_activevnodelist, vp, v_actfreelist);
	mp->mnt_activevnodelistsize++;
	mtx_unlock(&mp->mnt_listmtx);
	VI_UNLOCK(vp);
	MNT_IUNLOCK(mp);
	return (0);
}

int
insmntque(struct vnode *vp, struct mount *mp)
{

	return (insmntque1(vp, mp, insmntque_stddtr, NULL));
}

/*
 * Flush out and invalidate all buffers associated with a bufobj
 * Called with the underlying object locked.
 */
int
bufobj_invalbuf(struct bufobj *bo, int flags, int slpflag, int slptimeo)
{
	int error;

	BO_LOCK(bo);
	if (flags & V_SAVE) {
		error = bufobj_wwait(bo, slpflag, slptimeo);
		if (error) {
			BO_UNLOCK(bo);
			return (error);
		}
		if (bo->bo_dirty.bv_cnt > 0) {
			BO_UNLOCK(bo);
			if ((error = BO_SYNC(bo, MNT_WAIT)) != 0)
				return (error);
			/*
			 * XXX We could save a lock/unlock if this was only
			 * enabled under INVARIANTS
			 */
			BO_LOCK(bo);
			if (bo->bo_numoutput > 0 || bo->bo_dirty.bv_cnt > 0)
				panic("vinvalbuf: dirty bufs");
		}
	}
	/*
	 * If you alter this loop please notice that interlock is dropped and
	 * reacquired in flushbuflist.  Special care is needed to ensure that
	 * no race conditions occur from this.
	 */
	do {
		error = flushbuflist(&bo->bo_clean,
		    flags, bo, slpflag, slptimeo);
		if (error == 0 && !(flags & V_CLEANONLY))
			error = flushbuflist(&bo->bo_dirty,
			    flags, bo, slpflag, slptimeo);
		if (error != 0 && error != EAGAIN) {
			BO_UNLOCK(bo);
			return (error);
		}
	} while (error != 0);

	/*
	 * Wait for I/O to complete.  XXX needs cleaning up.  The vnode can
	 * have write I/O in-progress but if there is a VM object then the
	 * VM object can also have read-I/O in-progress.
	 */
	do {
		bufobj_wwait(bo, 0, 0);
		if ((flags & V_VMIO) == 0) {
			BO_UNLOCK(bo);
			if (bo->bo_object != NULL) {
				VM_OBJECT_WLOCK(bo->bo_object);
				vm_object_pip_wait(bo->bo_object, "bovlbx");
				VM_OBJECT_WUNLOCK(bo->bo_object);
			}
			BO_LOCK(bo);
		}
	} while (bo->bo_numoutput > 0);
	BO_UNLOCK(bo);

	/*
	 * Destroy the copy in the VM cache, too.
	 */
	if (bo->bo_object != NULL &&
	    (flags & (V_ALT | V_NORMAL | V_CLEANONLY | V_VMIO)) == 0) {
		VM_OBJECT_WLOCK(bo->bo_object);
		vm_object_page_remove(bo->bo_object, 0, 0, (flags & V_SAVE) ?
		    OBJPR_CLEANONLY : 0);
		VM_OBJECT_WUNLOCK(bo->bo_object);
	}

#ifdef INVARIANTS
	BO_LOCK(bo);
	if ((flags & (V_ALT | V_NORMAL | V_CLEANONLY | V_VMIO |
	    V_ALLOWCLEAN)) == 0 && (bo->bo_dirty.bv_cnt > 0 ||
	    bo->bo_clean.bv_cnt > 0))
		panic("vinvalbuf: flush failed");
	if ((flags & (V_ALT | V_NORMAL | V_CLEANONLY | V_VMIO)) == 0 &&
	    bo->bo_dirty.bv_cnt > 0)
		panic("vinvalbuf: flush dirty failed");
	BO_UNLOCK(bo);
#endif
	return (0);
}

/*
 * Flush out and invalidate all buffers associated with a vnode.
 * Called with the underlying object locked.
 */
int
vinvalbuf(struct vnode *vp, int flags, int slpflag, int slptimeo)
{

	CTR3(KTR_VFS, "%s: vp %p with flags %d", __func__, vp, flags);
	ASSERT_VOP_LOCKED(vp, "vinvalbuf");
	if (vp->v_object != NULL && vp->v_object->handle != vp)
		return (0);
	return (bufobj_invalbuf(&vp->v_bufobj, flags, slpflag, slptimeo));
}

/*
 * Flush out buffers on the specified list.
 *
 */
static int
flushbuflist(struct bufv *bufv, int flags, struct bufobj *bo, int slpflag,
    int slptimeo)
{
	struct buf *bp, *nbp;
	int retval, error;
	daddr_t lblkno;
	b_xflags_t xflags;

	ASSERT_BO_WLOCKED(bo);

	retval = 0;
	TAILQ_FOREACH_SAFE(bp, &bufv->bv_hd, b_bobufs, nbp) {
		/*
		 * If we are flushing both V_NORMAL and V_ALT buffers then
		 * do not skip any buffers. If we are flushing only V_NORMAL
		 * buffers then skip buffers marked as BX_ALTDATA. If we are
		 * flushing only V_ALT buffers then skip buffers not marked
		 * as BX_ALTDATA.
		 */
		if (((flags & (V_NORMAL | V_ALT)) != (V_NORMAL | V_ALT)) &&
		   (((flags & V_NORMAL) && (bp->b_xflags & BX_ALTDATA) != 0) ||
		    ((flags & V_ALT) && (bp->b_xflags & BX_ALTDATA) == 0))) {
			continue;
		}
		if (nbp != NULL) {
			lblkno = nbp->b_lblkno;
			xflags = nbp->b_xflags & (BX_VNDIRTY | BX_VNCLEAN);
		}
		retval = EAGAIN;
		error = BUF_TIMELOCK(bp,
		    LK_EXCLUSIVE | LK_SLEEPFAIL | LK_INTERLOCK, BO_LOCKPTR(bo),
		    "flushbuf", slpflag, slptimeo);
		if (error) {
			BO_LOCK(bo);
			return (error != ENOLCK ? error : EAGAIN);
		}
		KASSERT(bp->b_bufobj == bo,
		    ("bp %p wrong b_bufobj %p should be %p",
		    bp, bp->b_bufobj, bo));
		/*
		 * XXX Since there are no node locks for NFS, I
		 * believe there is a slight chance that a delayed
		 * write will occur while sleeping just above, so
		 * check for it.
		 */
		if (((bp->b_flags & (B_DELWRI | B_INVAL)) == B_DELWRI) &&
		    (flags & V_SAVE)) {
			bremfree(bp);
			bp->b_flags |= B_ASYNC;
			bwrite(bp);
			BO_LOCK(bo);
			return (EAGAIN);	/* XXX: why not loop ? */
		}
		bremfree(bp);
		bp->b_flags |= (B_INVAL | B_RELBUF);
		bp->b_flags &= ~B_ASYNC;
		brelse(bp);
		BO_LOCK(bo);
		if (nbp == NULL)
			break;
		nbp = gbincore(bo, lblkno);
		if (nbp == NULL || (nbp->b_xflags & (BX_VNDIRTY | BX_VNCLEAN))
		    != xflags)
			break;			/* nbp invalid */
	}
	return (retval);
}

int
bnoreuselist(struct bufv *bufv, struct bufobj *bo, daddr_t startn, daddr_t endn)
{
	struct buf *bp;
	int error;
	daddr_t lblkno;

	ASSERT_BO_LOCKED(bo);

	for (lblkno = startn;;) {
again:
		bp = BUF_PCTRIE_LOOKUP_GE(&bufv->bv_root, lblkno);
		if (bp == NULL || bp->b_lblkno >= endn ||
		    bp->b_lblkno < startn)
			break;
		error = BUF_TIMELOCK(bp, LK_EXCLUSIVE | LK_SLEEPFAIL |
		    LK_INTERLOCK, BO_LOCKPTR(bo), "brlsfl", 0, 0);
		if (error != 0) {
			BO_RLOCK(bo);
			if (error == ENOLCK)
				goto again;
			return (error);
		}
		KASSERT(bp->b_bufobj == bo,
		    ("bp %p wrong b_bufobj %p should be %p",
		    bp, bp->b_bufobj, bo));
		lblkno = bp->b_lblkno + 1;
		if ((bp->b_flags & B_MANAGED) == 0)
			bremfree(bp);
		bp->b_flags |= B_RELBUF;
		/*
		 * In the VMIO case, use the B_NOREUSE flag to hint that the
		 * pages backing each buffer in the range are unlikely to be
		 * reused.  Dirty buffers will have the hint applied once
		 * they've been written.
		 */
		if ((bp->b_flags & B_VMIO) != 0)
			bp->b_flags |= B_NOREUSE;
		brelse(bp);
		BO_RLOCK(bo);
	}
	return (0);
}

/*
 * Truncate a file's buffer and pages to a specified length.  This
 * is in lieu of the old vinvalbuf mechanism, which performed unneeded
 * sync activity.
 */
int
vtruncbuf(struct vnode *vp, struct ucred *cred, off_t length, int blksize)
{
	struct buf *bp, *nbp;
	int anyfreed;
	daddr_t trunclbn;
	struct bufobj *bo;

	CTR5(KTR_VFS, "%s: vp %p with cred %p and block %d:%ju", __func__,
	    vp, cred, blksize, (uintmax_t)length);

	/*
	 * Round up to the *next* lbn.
	 */
	trunclbn = howmany(length, blksize);

	ASSERT_VOP_LOCKED(vp, "vtruncbuf");
restart:
	bo = &vp->v_bufobj;
	BO_LOCK(bo);
	anyfreed = 1;
	for (;anyfreed;) {
		anyfreed = 0;
		TAILQ_FOREACH_SAFE(bp, &bo->bo_clean.bv_hd, b_bobufs, nbp) {
			if (bp->b_lblkno < trunclbn)
				continue;
			if (BUF_LOCK(bp,
			    LK_EXCLUSIVE | LK_SLEEPFAIL | LK_INTERLOCK,
			    BO_LOCKPTR(bo)) == ENOLCK)
				goto restart;

			bremfree(bp);
			bp->b_flags |= (B_INVAL | B_RELBUF);
			bp->b_flags &= ~B_ASYNC;
			brelse(bp);
			anyfreed = 1;

			BO_LOCK(bo);
			if (nbp != NULL &&
			    (((nbp->b_xflags & BX_VNCLEAN) == 0) ||
			    (nbp->b_vp != vp) ||
			    (nbp->b_flags & B_DELWRI))) {
				BO_UNLOCK(bo);
				goto restart;
			}
		}

		TAILQ_FOREACH_SAFE(bp, &bo->bo_dirty.bv_hd, b_bobufs, nbp) {
			if (bp->b_lblkno < trunclbn)
				continue;
			if (BUF_LOCK(bp,
			    LK_EXCLUSIVE | LK_SLEEPFAIL | LK_INTERLOCK,
			    BO_LOCKPTR(bo)) == ENOLCK)
				goto restart;
			bremfree(bp);
			bp->b_flags |= (B_INVAL | B_RELBUF);
			bp->b_flags &= ~B_ASYNC;
			brelse(bp);
			anyfreed = 1;

			BO_LOCK(bo);
			if (nbp != NULL &&
			    (((nbp->b_xflags & BX_VNDIRTY) == 0) ||
			    (nbp->b_vp != vp) ||
			    (nbp->b_flags & B_DELWRI) == 0)) {
				BO_UNLOCK(bo);
				goto restart;
			}
		}
	}

	if (length > 0) {
restartsync:
		TAILQ_FOREACH_SAFE(bp, &bo->bo_dirty.bv_hd, b_bobufs, nbp) {
			if (bp->b_lblkno > 0)
				continue;
			/*
			 * Since we hold the vnode lock this should only
			 * fail if we're racing with the buf daemon.
			 */
			if (BUF_LOCK(bp,
			    LK_EXCLUSIVE | LK_SLEEPFAIL | LK_INTERLOCK,
			    BO_LOCKPTR(bo)) == ENOLCK) {
				goto restart;
			}
			VNASSERT((bp->b_flags & B_DELWRI), vp,
			    ("buf(%p) on dirty queue without DELWRI", bp));

			bremfree(bp);
			bawrite(bp);
			BO_LOCK(bo);
			goto restartsync;
		}
	}

	bufobj_wwait(bo, 0, 0);
	BO_UNLOCK(bo);
	vnode_pager_setsize(vp, length);

	return (0);
}

static void
buf_vlist_remove(struct buf *bp)
{
	struct bufv *bv;

	KASSERT(bp->b_bufobj != NULL, ("No b_bufobj %p", bp));
	ASSERT_BO_WLOCKED(bp->b_bufobj);
	KASSERT((bp->b_xflags & (BX_VNDIRTY|BX_VNCLEAN)) !=
	    (BX_VNDIRTY|BX_VNCLEAN),
	    ("buf_vlist_remove: Buf %p is on two lists", bp));
	if (bp->b_xflags & BX_VNDIRTY)
		bv = &bp->b_bufobj->bo_dirty;
	else
		bv = &bp->b_bufobj->bo_clean;
	BUF_PCTRIE_REMOVE(&bv->bv_root, bp->b_lblkno);
	TAILQ_REMOVE(&bv->bv_hd, bp, b_bobufs);
	bv->bv_cnt--;
	bp->b_xflags &= ~(BX_VNDIRTY | BX_VNCLEAN);
}

/*
 * Add the buffer to the sorted clean or dirty block list.
 *
 * NOTE: xflags is passed as a constant, optimizing this inline function!
 */
static void
buf_vlist_add(struct buf *bp, struct bufobj *bo, b_xflags_t xflags)
{
	struct bufv *bv;
	struct buf *n;
	int error;

	ASSERT_BO_WLOCKED(bo);
	KASSERT((xflags & BX_VNDIRTY) == 0 || (bo->bo_flag & BO_DEAD) == 0,
	    ("dead bo %p", bo));
	KASSERT((bp->b_xflags & (BX_VNDIRTY|BX_VNCLEAN)) == 0,
	    ("buf_vlist_add: Buf %p has existing xflags %d", bp, bp->b_xflags));
	bp->b_xflags |= xflags;
	if (xflags & BX_VNDIRTY)
		bv = &bo->bo_dirty;
	else
		bv = &bo->bo_clean;

	/*
	 * Keep the list ordered.  Optimize empty list insertion.  Assume
	 * we tend to grow at the tail so lookup_le should usually be cheaper
	 * than _ge. 
	 */
	if (bv->bv_cnt == 0 ||
	    bp->b_lblkno > TAILQ_LAST(&bv->bv_hd, buflists)->b_lblkno)
		TAILQ_INSERT_TAIL(&bv->bv_hd, bp, b_bobufs);
	else if ((n = BUF_PCTRIE_LOOKUP_LE(&bv->bv_root, bp->b_lblkno)) == NULL)
		TAILQ_INSERT_HEAD(&bv->bv_hd, bp, b_bobufs);
	else
		TAILQ_INSERT_AFTER(&bv->bv_hd, n, bp, b_bobufs);
	error = BUF_PCTRIE_INSERT(&bv->bv_root, bp);
	if (error)
		panic("buf_vlist_add:  Preallocated nodes insufficient.");
	bv->bv_cnt++;
}

/*
 * Look up a buffer using the buffer tries.
 */
struct buf *
gbincore(struct bufobj *bo, daddr_t lblkno)
{
	struct buf *bp;

	ASSERT_BO_LOCKED(bo);
	bp = BUF_PCTRIE_LOOKUP(&bo->bo_clean.bv_root, lblkno);
	if (bp != NULL)
		return (bp);
	return BUF_PCTRIE_LOOKUP(&bo->bo_dirty.bv_root, lblkno);
}

/*
 * Associate a buffer with a vnode.
 */
void
bgetvp(struct vnode *vp, struct buf *bp)
{
	struct bufobj *bo;

	bo = &vp->v_bufobj;
	ASSERT_BO_WLOCKED(bo);
	VNASSERT(bp->b_vp == NULL, bp->b_vp, ("bgetvp: not free"));

	CTR3(KTR_BUF, "bgetvp(%p) vp %p flags %X", bp, vp, bp->b_flags);
	VNASSERT((bp->b_xflags & (BX_VNDIRTY|BX_VNCLEAN)) == 0, vp,
	    ("bgetvp: bp already attached! %p", bp));

	vhold(vp);
	bp->b_vp = vp;
	bp->b_bufobj = bo;
	/*
	 * Insert onto list for new vnode.
	 */
	buf_vlist_add(bp, bo, BX_VNCLEAN);
}

/*
 * Disassociate a buffer from a vnode.
 */
void
brelvp(struct buf *bp)
{
	struct bufobj *bo;
	struct vnode *vp;

	CTR3(KTR_BUF, "brelvp(%p) vp %p flags %X", bp, bp->b_vp, bp->b_flags);
	KASSERT(bp->b_vp != NULL, ("brelvp: NULL"));

	/*
	 * Delete from old vnode list, if on one.
	 */
	vp = bp->b_vp;		/* XXX */
	bo = bp->b_bufobj;
	BO_LOCK(bo);
	if (bp->b_xflags & (BX_VNDIRTY | BX_VNCLEAN))
		buf_vlist_remove(bp);
	else
		panic("brelvp: Buffer %p not on queue.", bp);
	if ((bo->bo_flag & BO_ONWORKLST) && bo->bo_dirty.bv_cnt == 0) {
		bo->bo_flag &= ~BO_ONWORKLST;
		mtx_lock(&sync_mtx);
		LIST_REMOVE(bo, bo_synclist);
		syncer_worklist_len--;
		mtx_unlock(&sync_mtx);
	}
	bp->b_vp = NULL;
	bp->b_bufobj = NULL;
	BO_UNLOCK(bo);
	vdrop(vp);
}

/*
 * Add an item to the syncer work queue.
 */
static void
vn_syncer_add_to_worklist(struct bufobj *bo, int delay)
{
	int slot;

	ASSERT_BO_WLOCKED(bo);

	mtx_lock(&sync_mtx);
	if (bo->bo_flag & BO_ONWORKLST)
		LIST_REMOVE(bo, bo_synclist);
	else {
		bo->bo_flag |= BO_ONWORKLST;
		syncer_worklist_len++;
	}

	if (delay > syncer_maxdelay - 2)
		delay = syncer_maxdelay - 2;
	slot = (syncer_delayno + delay) & syncer_mask;

	LIST_INSERT_HEAD(&syncer_workitem_pending[slot], bo, bo_synclist);
	mtx_unlock(&sync_mtx);
}

static int
sysctl_vfs_worklist_len(SYSCTL_HANDLER_ARGS)
{
	int error, len;

	mtx_lock(&sync_mtx);
	len = syncer_worklist_len - sync_vnode_count;
	mtx_unlock(&sync_mtx);
	error = SYSCTL_OUT(req, &len, sizeof(len));
	return (error);
}

SYSCTL_PROC(_vfs, OID_AUTO, worklist_len, CTLTYPE_INT | CTLFLAG_RD, NULL, 0,
    sysctl_vfs_worklist_len, "I", "Syncer thread worklist length");

static struct proc *updateproc;
static void sched_sync(void);
static struct kproc_desc up_kp = {
	"syncer",
	sched_sync,
	&updateproc
};
SYSINIT(syncer, SI_SUB_KTHREAD_UPDATE, SI_ORDER_FIRST, kproc_start, &up_kp);

static int
sync_vnode(struct synclist *slp, struct bufobj **bo, struct thread *td)
{
	struct vnode *vp;
	struct mount *mp;

	*bo = LIST_FIRST(slp);
	if (*bo == NULL)
		return (0);
	vp = bo2vnode(*bo);
	if (VOP_ISLOCKED(vp) != 0 || VI_TRYLOCK(vp) == 0)
		return (1);
	/*
	 * We use vhold in case the vnode does not
	 * successfully sync.  vhold prevents the vnode from
	 * going away when we unlock the sync_mtx so that
	 * we can acquire the vnode interlock.
	 */
	vholdl(vp);
	mtx_unlock(&sync_mtx);
	VI_UNLOCK(vp);
	if (vn_start_write(vp, &mp, V_NOWAIT) != 0) {
		vdrop(vp);
		mtx_lock(&sync_mtx);
		return (*bo == LIST_FIRST(slp));
	}
	vn_lock(vp, LK_EXCLUSIVE | LK_RETRY);
	(void) VOP_FSYNC(vp, MNT_LAZY, td);
	VOP_UNLOCK(vp, 0);
	vn_finished_write(mp);
	BO_LOCK(*bo);
	if (((*bo)->bo_flag & BO_ONWORKLST) != 0) {
		/*
		 * Put us back on the worklist.  The worklist
		 * routine will remove us from our current
		 * position and then add us back in at a later
		 * position.
		 */
		vn_syncer_add_to_worklist(*bo, syncdelay);
	}
	BO_UNLOCK(*bo);
	vdrop(vp);
	mtx_lock(&sync_mtx);
	return (0);
}

static int first_printf = 1;

/*
 * System filesystem synchronizer daemon.
 */
static void
sched_sync(void)
{
	struct synclist *next, *slp;
	struct bufobj *bo;
	long starttime;
	struct thread *td = curthread;
	int last_work_seen;
	int net_worklist_len;
	int syncer_final_iter;
	int error;

	last_work_seen = 0;
	syncer_final_iter = 0;
	syncer_state = SYNCER_RUNNING;
	starttime = time_uptime;
	td->td_pflags |= TDP_NORUNNINGBUF;

	EVENTHANDLER_REGISTER(shutdown_pre_sync, syncer_shutdown, td->td_proc,
	    SHUTDOWN_PRI_LAST);

	mtx_lock(&sync_mtx);
	for (;;) {
		if (syncer_state == SYNCER_FINAL_DELAY &&
		    syncer_final_iter == 0) {
			mtx_unlock(&sync_mtx);
			kproc_suspend_check(td->td_proc);
			mtx_lock(&sync_mtx);
		}
		net_worklist_len = syncer_worklist_len - sync_vnode_count;
		if (syncer_state != SYNCER_RUNNING &&
		    starttime != time_uptime) {
			if (first_printf) {
				printf("\nSyncing disks, vnodes remaining... ");
				first_printf = 0;
			}
			printf("%d ", net_worklist_len);
		}
		starttime = time_uptime;

		/*
		 * Push files whose dirty time has expired.  Be careful
		 * of interrupt race on slp queue.
		 *
		 * Skip over empty worklist slots when shutting down.
		 */
		do {
			slp = &syncer_workitem_pending[syncer_delayno];
			syncer_delayno += 1;
			if (syncer_delayno == syncer_maxdelay)
				syncer_delayno = 0;
			next = &syncer_workitem_pending[syncer_delayno];
			/*
			 * If the worklist has wrapped since the
			 * it was emptied of all but syncer vnodes,
			 * switch to the FINAL_DELAY state and run
			 * for one more second.
			 */
			if (syncer_state == SYNCER_SHUTTING_DOWN &&
			    net_worklist_len == 0 &&
			    last_work_seen == syncer_delayno) {
				syncer_state = SYNCER_FINAL_DELAY;
				syncer_final_iter = SYNCER_SHUTDOWN_SPEEDUP;
			}
		} while (syncer_state != SYNCER_RUNNING && LIST_EMPTY(slp) &&
		    syncer_worklist_len > 0);

		/*
		 * Keep track of the last time there was anything
		 * on the worklist other than syncer vnodes.
		 * Return to the SHUTTING_DOWN state if any
		 * new work appears.
		 */
		if (net_worklist_len > 0 || syncer_state == SYNCER_RUNNING)
			last_work_seen = syncer_delayno;
		if (net_worklist_len > 0 && syncer_state == SYNCER_FINAL_DELAY)
			syncer_state = SYNCER_SHUTTING_DOWN;
		while (!LIST_EMPTY(slp)) {
			error = sync_vnode(slp, &bo, td);
			if (error == 1) {
				LIST_REMOVE(bo, bo_synclist);
				LIST_INSERT_HEAD(next, bo, bo_synclist);
				continue;
			}

			if (first_printf == 0) {
				/*
				 * Drop the sync mutex, because some watchdog
				 * drivers need to sleep while patting
				 */
				mtx_unlock(&sync_mtx);
				wdog_kern_pat(WD_LASTVAL);
				mtx_lock(&sync_mtx);
			}

		}
		if (syncer_state == SYNCER_FINAL_DELAY && syncer_final_iter > 0)
			syncer_final_iter--;
		/*
		 * The variable rushjob allows the kernel to speed up the
		 * processing of the filesystem syncer process. A rushjob
		 * value of N tells the filesystem syncer to process the next
		 * N seconds worth of work on its queue ASAP. Currently rushjob
		 * is used by the soft update code to speed up the filesystem
		 * syncer process when the incore state is getting so far
		 * ahead of the disk that the kernel memory pool is being
		 * threatened with exhaustion.
		 */
		if (rushjob > 0) {
			rushjob -= 1;
			continue;
		}
		/*
		 * Just sleep for a short period of time between
		 * iterations when shutting down to allow some I/O
		 * to happen.
		 *
		 * If it has taken us less than a second to process the
		 * current work, then wait. Otherwise start right over
		 * again. We can still lose time if any single round
		 * takes more than two seconds, but it does not really
		 * matter as we are just trying to generally pace the
		 * filesystem activity.
		 */
		if (syncer_state != SYNCER_RUNNING ||
		    time_uptime == starttime) {
			thread_lock(td);
			sched_prio(td, PPAUSE);
			thread_unlock(td);
		}
		if (syncer_state != SYNCER_RUNNING)
			cv_timedwait(&sync_wakeup, &sync_mtx,
			    hz / SYNCER_SHUTDOWN_SPEEDUP);
		else if (time_uptime == starttime)
			cv_timedwait(&sync_wakeup, &sync_mtx, hz);
	}
}

/*
 * Request the syncer daemon to speed up its work.
 * We never push it to speed up more than half of its
 * normal turn time, otherwise it could take over the cpu.
 */
int
speedup_syncer(void)
{
	int ret = 0;

	mtx_lock(&sync_mtx);
	if (rushjob < syncdelay / 2) {
		rushjob += 1;
		stat_rush_requests += 1;
		ret = 1;
	}
	mtx_unlock(&sync_mtx);
	cv_broadcast(&sync_wakeup);
	return (ret);
}

/*
 * Tell the syncer to speed up its work and run though its work
 * list several times, then tell it to shut down.
 */
static void
syncer_shutdown(void *arg, int howto)
{

	if (howto & RB_NOSYNC)
		return;
	mtx_lock(&sync_mtx);
	syncer_state = SYNCER_SHUTTING_DOWN;
	rushjob = 0;
	mtx_unlock(&sync_mtx);
	cv_broadcast(&sync_wakeup);
	kproc_shutdown(arg, howto);
}

void
syncer_suspend(void)
{

	syncer_shutdown(updateproc, 0);
}

void
syncer_resume(void)
{

	mtx_lock(&sync_mtx);
	first_printf = 1;
	syncer_state = SYNCER_RUNNING;
	mtx_unlock(&sync_mtx);
	cv_broadcast(&sync_wakeup);
	kproc_resume(updateproc);
}

/*
 * Reassign a buffer from one vnode to another.
 * Used to assign file specific control information
 * (indirect blocks) to the vnode to which they belong.
 */
void
reassignbuf(struct buf *bp)
{
	struct vnode *vp;
	struct bufobj *bo;
	int delay;
#ifdef INVARIANTS
	struct bufv *bv;
#endif

	vp = bp->b_vp;
	bo = bp->b_bufobj;
	++reassignbufcalls;

	CTR3(KTR_BUF, "reassignbuf(%p) vp %p flags %X",
	    bp, bp->b_vp, bp->b_flags);
	/*
	 * B_PAGING flagged buffers cannot be reassigned because their vp
	 * is not fully linked in.
	 */
	if (bp->b_flags & B_PAGING)
		panic("cannot reassign paging buffer");

	/*
	 * Delete from old vnode list, if on one.
	 */
	BO_LOCK(bo);
	if (bp->b_xflags & (BX_VNDIRTY | BX_VNCLEAN))
		buf_vlist_remove(bp);
	else
		panic("reassignbuf: Buffer %p not on queue.", bp);
	/*
	 * If dirty, put on list of dirty buffers; otherwise insert onto list
	 * of clean buffers.
	 */
	if (bp->b_flags & B_DELWRI) {
		if ((bo->bo_flag & BO_ONWORKLST) == 0) {
			switch (vp->v_type) {
			case VDIR:
				delay = dirdelay;
				break;
			case VCHR:
				delay = metadelay;
				break;
			default:
				delay = filedelay;
			}
			vn_syncer_add_to_worklist(bo, delay);
		}
		buf_vlist_add(bp, bo, BX_VNDIRTY);
	} else {
		buf_vlist_add(bp, bo, BX_VNCLEAN);

		if ((bo->bo_flag & BO_ONWORKLST) && bo->bo_dirty.bv_cnt == 0) {
			mtx_lock(&sync_mtx);
			LIST_REMOVE(bo, bo_synclist);
			syncer_worklist_len--;
			mtx_unlock(&sync_mtx);
			bo->bo_flag &= ~BO_ONWORKLST;
		}
	}
#ifdef INVARIANTS
	bv = &bo->bo_clean;
	bp = TAILQ_FIRST(&bv->bv_hd);
	KASSERT(bp == NULL || bp->b_bufobj == bo,
	    ("bp %p wrong b_bufobj %p should be %p", bp, bp->b_bufobj, bo));
	bp = TAILQ_LAST(&bv->bv_hd, buflists);
	KASSERT(bp == NULL || bp->b_bufobj == bo,
	    ("bp %p wrong b_bufobj %p should be %p", bp, bp->b_bufobj, bo));
	bv = &bo->bo_dirty;
	bp = TAILQ_FIRST(&bv->bv_hd);
	KASSERT(bp == NULL || bp->b_bufobj == bo,
	    ("bp %p wrong b_bufobj %p should be %p", bp, bp->b_bufobj, bo));
	bp = TAILQ_LAST(&bv->bv_hd, buflists);
	KASSERT(bp == NULL || bp->b_bufobj == bo,
	    ("bp %p wrong b_bufobj %p should be %p", bp, bp->b_bufobj, bo));
#endif
	BO_UNLOCK(bo);
}

static void
v_init_counters(struct vnode *vp)
{

	VNASSERT(vp->v_type == VNON && vp->v_data == NULL && vp->v_iflag == 0,
	    vp, ("%s called for an initialized vnode", __FUNCTION__));
	ASSERT_VI_UNLOCKED(vp, __FUNCTION__);

	refcount_init(&vp->v_holdcnt, 1);
	refcount_init(&vp->v_usecount, 1);
}

static void
v_incr_usecount_locked(struct vnode *vp)
{

	ASSERT_VI_LOCKED(vp, __func__);
	if ((vp->v_iflag & VI_OWEINACT) != 0) {
		VNASSERT(vp->v_usecount == 0, vp,
		    ("vnode with usecount and VI_OWEINACT set"));
		vp->v_iflag &= ~VI_OWEINACT;
	}
	refcount_acquire(&vp->v_usecount);
	v_incr_devcount(vp);
}

/*
 * Increment the use count on the vnode, taking care to reference
 * the driver's usecount if this is a chardev.
 */
static void
v_incr_usecount(struct vnode *vp)
{

	ASSERT_VI_UNLOCKED(vp, __func__);
	CTR2(KTR_VFS, "%s: vp %p", __func__, vp);

	if (vp->v_type != VCHR &&
	    refcount_acquire_if_not_zero(&vp->v_usecount)) {
		VNODE_REFCOUNT_FENCE_ACQ();
		VNASSERT((vp->v_iflag & VI_OWEINACT) == 0, vp,
		    ("vnode with usecount and VI_OWEINACT set"));
	} else {
		VI_LOCK(vp);
		v_incr_usecount_locked(vp);
		VI_UNLOCK(vp);
	}
}

/*
 * Increment si_usecount of the associated device, if any.
 */
static void
v_incr_devcount(struct vnode *vp)
{

	ASSERT_VI_LOCKED(vp, __FUNCTION__);
	if (vp->v_type == VCHR && vp->v_rdev != NULL) {
		dev_lock();
		vp->v_rdev->si_usecount++;
		dev_unlock();
	}
}

/*
 * Decrement si_usecount of the associated device, if any.
 */
static void
v_decr_devcount(struct vnode *vp)
{

	ASSERT_VI_LOCKED(vp, __FUNCTION__);
	if (vp->v_type == VCHR && vp->v_rdev != NULL) {
		dev_lock();
		vp->v_rdev->si_usecount--;
		dev_unlock();
	}
}

/*
 * Grab a particular vnode from the free list, increment its
 * reference count and lock it.  VI_DOOMED is set if the vnode
 * is being destroyed.  Only callers who specify LK_RETRY will
 * see doomed vnodes.  If inactive processing was delayed in
 * vput try to do it here.
 *
 * Notes on lockless counter manipulation:
 * _vhold, vputx and other routines make various decisions based
 * on either holdcnt or usecount being 0. As long as either counter
 * is not transitioning 0->1 nor 1->0, the manipulation can be done
 * with atomic operations. Otherwise the interlock is taken covering
 * both the atomic and additional actions.
 */
int
vget(struct vnode *vp, int flags, struct thread *td)
{
	int error, oweinact;

	VNASSERT((flags & LK_TYPE_MASK) != 0, vp,
	    ("vget: invalid lock operation"));

	if ((flags & LK_INTERLOCK) != 0)
		ASSERT_VI_LOCKED(vp, __func__);
	else
		ASSERT_VI_UNLOCKED(vp, __func__);
	if ((flags & LK_VNHELD) != 0)
		VNASSERT((vp->v_holdcnt > 0), vp,
		    ("vget: LK_VNHELD passed but vnode not held"));

	CTR3(KTR_VFS, "%s: vp %p with flags %d", __func__, vp, flags);

	if ((flags & LK_VNHELD) == 0)
		_vhold(vp, (flags & LK_INTERLOCK) != 0);

	if ((error = vn_lock(vp, flags)) != 0) {
		vdrop(vp);
		CTR2(KTR_VFS, "%s: impossible to lock vnode %p", __func__,
		    vp);
		return (error);
	}
	if (vp->v_iflag & VI_DOOMED && (flags & LK_RETRY) == 0)
		panic("vget: vn_lock failed to return ENOENT\n");
	/*
	 * We don't guarantee that any particular close will
	 * trigger inactive processing so just make a best effort
	 * here at preventing a reference to a removed file.  If
	 * we don't succeed no harm is done.
	 *
	 * Upgrade our holdcnt to a usecount.
	 */
	if (vp->v_type == VCHR ||
	    !refcount_acquire_if_not_zero(&vp->v_usecount)) {
		VI_LOCK(vp);
		if ((vp->v_iflag & VI_OWEINACT) == 0) {
			oweinact = 0;
		} else {
			oweinact = 1;
			vp->v_iflag &= ~VI_OWEINACT;
			VNODE_REFCOUNT_FENCE_REL();
		}
		refcount_acquire(&vp->v_usecount);
		v_incr_devcount(vp);
		if (oweinact && VOP_ISLOCKED(vp) == LK_EXCLUSIVE &&
		    (flags & LK_NOWAIT) == 0)
			vinactive(vp, td);
		VI_UNLOCK(vp);
	}
	return (0);
}

/*
 * Increase the reference (use) and hold count of a vnode.
 * This will also remove the vnode from the free list if it is presently free.
 */
void
vref(struct vnode *vp)
{

	CTR2(KTR_VFS, "%s: vp %p", __func__, vp);
	_vhold(vp, false);
	v_incr_usecount(vp);
}

void
vrefl(struct vnode *vp)
{

	ASSERT_VI_LOCKED(vp, __func__);
	CTR2(KTR_VFS, "%s: vp %p", __func__, vp);
	_vhold(vp, true);
	v_incr_usecount_locked(vp);
}

void
vrefact(struct vnode *vp)
{

	CTR2(KTR_VFS, "%s: vp %p", __func__, vp);
	if (__predict_false(vp->v_type == VCHR)) {
		VNASSERT(vp->v_holdcnt > 0 && vp->v_usecount > 0, vp,
		    ("%s: wrong ref counts", __func__));
		vref(vp);
		return;
	}
#ifdef INVARIANTS
	int old = atomic_fetchadd_int(&vp->v_holdcnt, 1);
	VNASSERT(old > 0, vp, ("%s: wrong hold count", __func__));
	old = atomic_fetchadd_int(&vp->v_usecount, 1);
	VNASSERT(old > 0, vp, ("%s: wrong use count", __func__));
#else
	refcount_acquire(&vp->v_holdcnt);
	refcount_acquire(&vp->v_usecount);
#endif
}

/*
 * Return reference count of a vnode.
 *
 * The results of this call are only guaranteed when some mechanism is used to
 * stop other processes from gaining references to the vnode.  This may be the
 * case if the caller holds the only reference.  This is also useful when stale
 * data is acceptable as race conditions may be accounted for by some other
 * means.
 */
int
vrefcnt(struct vnode *vp)
{

	return (vp->v_usecount);
}

#define	VPUTX_VRELE	1
#define	VPUTX_VPUT	2
#define	VPUTX_VUNREF	3

/*
 * Decrement the use and hold counts for a vnode.
 *
 * See an explanation near vget() as to why atomic operation is safe.
 */
static void
vputx(struct vnode *vp, int func)
{
	int error;

	KASSERT(vp != NULL, ("vputx: null vp"));
	if (func == VPUTX_VUNREF)
		ASSERT_VOP_LOCKED(vp, "vunref");
	else if (func == VPUTX_VPUT)
		ASSERT_VOP_LOCKED(vp, "vput");
	else
		KASSERT(func == VPUTX_VRELE, ("vputx: wrong func"));
	ASSERT_VI_UNLOCKED(vp, __func__);
	CTR2(KTR_VFS, "%s: vp %p", __func__, vp);

	if (vp->v_type != VCHR &&
	    refcount_release_if_not_last(&vp->v_usecount)) {
		if (func == VPUTX_VPUT)
			VOP_UNLOCK(vp, 0);
		vdrop(vp);
		return;
	}

	VI_LOCK(vp);

	/*
	 * We want to hold the vnode until the inactive finishes to
	 * prevent vgone() races.  We drop the use count here and the
	 * hold count below when we're done.
	 */
	if (!refcount_release(&vp->v_usecount) ||
	    (vp->v_iflag & VI_DOINGINACT)) {
		if (func == VPUTX_VPUT)
			VOP_UNLOCK(vp, 0);
		v_decr_devcount(vp);
		vdropl(vp);
		return;
	}

	v_decr_devcount(vp);

	error = 0;

	if (vp->v_usecount != 0) {
		vn_printf(vp, "vputx: usecount not zero for vnode ");
		panic("vputx: usecount not zero");
	}

	CTR2(KTR_VFS, "%s: return vnode %p to the freelist", __func__, vp);

	/*
	 * We must call VOP_INACTIVE with the node locked. Mark
	 * as VI_DOINGINACT to avoid recursion.
	 */
	vp->v_iflag |= VI_OWEINACT;
	switch (func) {
	case VPUTX_VRELE:
		error = vn_lock(vp, LK_EXCLUSIVE | LK_INTERLOCK);
		VI_LOCK(vp);
		break;
	case VPUTX_VPUT:
		if (VOP_ISLOCKED(vp) != LK_EXCLUSIVE) {
			error = VOP_LOCK(vp, LK_UPGRADE | LK_INTERLOCK |
			    LK_NOWAIT);
			VI_LOCK(vp);
		}
		break;
	case VPUTX_VUNREF:
		if (VOP_ISLOCKED(vp) != LK_EXCLUSIVE) {
			error = VOP_LOCK(vp, LK_TRYUPGRADE | LK_INTERLOCK);
			VI_LOCK(vp);
		}
		break;
	}
	VNASSERT(vp->v_usecount == 0 || (vp->v_iflag & VI_OWEINACT) == 0, vp,
	    ("vnode with usecount and VI_OWEINACT set"));
	if (error == 0) {
		if (vp->v_iflag & VI_OWEINACT)
			vinactive(vp, curthread);
		if (func != VPUTX_VUNREF)
			VOP_UNLOCK(vp, 0);
	}
	vdropl(vp);
}

/*
 * Vnode put/release.
 * If count drops to zero, call inactive routine and return to freelist.
 */
void
vrele(struct vnode *vp)
{

	vputx(vp, VPUTX_VRELE);
}

/*
 * Release an already locked vnode.  This give the same effects as
 * unlock+vrele(), but takes less time and avoids releasing and
 * re-aquiring the lock (as vrele() acquires the lock internally.)
 */
void
vput(struct vnode *vp)
{

	vputx(vp, VPUTX_VPUT);
}

/*
 * Release an exclusively locked vnode. Do not unlock the vnode lock.
 */
void
vunref(struct vnode *vp)
{

	vputx(vp, VPUTX_VUNREF);
}

/*
 * Increase the hold count and activate if this is the first reference.
 */
void
_vhold(struct vnode *vp, bool locked)
{
	struct mount *mp;

	if (locked)
		ASSERT_VI_LOCKED(vp, __func__);
	else
		ASSERT_VI_UNLOCKED(vp, __func__);
	CTR2(KTR_VFS, "%s: vp %p", __func__, vp);
	if (!locked) {
		if (refcount_acquire_if_not_zero(&vp->v_holdcnt)) {
			VNODE_REFCOUNT_FENCE_ACQ();
			VNASSERT((vp->v_iflag & VI_FREE) == 0, vp,
			    ("_vhold: vnode with holdcnt is free"));
			return;
		}
		VI_LOCK(vp);
	}
	if ((vp->v_iflag & VI_FREE) == 0) {
		refcount_acquire(&vp->v_holdcnt);
		if (!locked)
			VI_UNLOCK(vp);
		return;
	}
	VNASSERT(vp->v_holdcnt == 0, vp,
	    ("%s: wrong hold count", __func__));
	VNASSERT(vp->v_op != NULL, vp,
	    ("%s: vnode already reclaimed.", __func__));
	/*
	 * Remove a vnode from the free list, mark it as in use,
	 * and put it on the active list.
	 */
	VNASSERT(vp->v_mount != NULL, vp,
	    ("_vhold: vnode not on per mount vnode list"));
	mp = vp->v_mount;
	mtx_lock(&mp->mnt_listmtx);
	if ((vp->v_mflag & VMP_TMPMNTFREELIST) != 0) {
		TAILQ_REMOVE(&mp->mnt_tmpfreevnodelist, vp, v_actfreelist);
		mp->mnt_tmpfreevnodelistsize--;
		vp->v_mflag &= ~VMP_TMPMNTFREELIST;
	} else {
		mtx_lock(&vnode_free_list_mtx);
		TAILQ_REMOVE(&vnode_free_list, vp, v_actfreelist);
		freevnodes--;
		mtx_unlock(&vnode_free_list_mtx);
	}
	KASSERT((vp->v_iflag & VI_ACTIVE) == 0,
	    ("Activating already active vnode"));
	vp->v_iflag &= ~VI_FREE;
	vp->v_iflag |= VI_ACTIVE;
	TAILQ_INSERT_HEAD(&mp->mnt_activevnodelist, vp, v_actfreelist);
	mp->mnt_activevnodelistsize++;
	mtx_unlock(&mp->mnt_listmtx);
	refcount_acquire(&vp->v_holdcnt);
	if (!locked)
		VI_UNLOCK(vp);
}

/*
 * Drop the hold count of the vnode.  If this is the last reference to
 * the vnode we place it on the free list unless it has been vgone'd
 * (marked VI_DOOMED) in which case we will free it.
 *
 * Because the vnode vm object keeps a hold reference on the vnode if
 * there is at least one resident non-cached page, the vnode cannot
 * leave the active list without the page cleanup done.
 */
void
_vdrop(struct vnode *vp, bool locked)
{
	struct bufobj *bo;
	struct mount *mp;
	int active;

	if (locked)
		ASSERT_VI_LOCKED(vp, __func__);
	else
		ASSERT_VI_UNLOCKED(vp, __func__);
	CTR2(KTR_VFS, "%s: vp %p", __func__, vp);
	if ((int)vp->v_holdcnt <= 0)
		panic("vdrop: holdcnt %d", vp->v_holdcnt);
	if (!locked) {
		if (refcount_release_if_not_last(&vp->v_holdcnt))
			return;
		VI_LOCK(vp);
	}
	if (refcount_release(&vp->v_holdcnt) == 0) {
		VI_UNLOCK(vp);
		return;
	}
	if ((vp->v_iflag & VI_DOOMED) == 0) {
		/*
		 * Mark a vnode as free: remove it from its active list
		 * and put it up for recycling on the freelist.
		 */
		VNASSERT(vp->v_op != NULL, vp,
		    ("vdropl: vnode already reclaimed."));
		VNASSERT((vp->v_iflag & VI_FREE) == 0, vp,
		    ("vnode already free"));
		VNASSERT(vp->v_holdcnt == 0, vp,
		    ("vdropl: freeing when we shouldn't"));
		active = vp->v_iflag & VI_ACTIVE;
		if ((vp->v_iflag & VI_OWEINACT) == 0) {
			vp->v_iflag &= ~VI_ACTIVE;
			mp = vp->v_mount;
			if (mp != NULL) {
				mtx_lock(&mp->mnt_listmtx);
				if (active) {
					TAILQ_REMOVE(&mp->mnt_activevnodelist,
					    vp, v_actfreelist);
					mp->mnt_activevnodelistsize--;
				}
				TAILQ_INSERT_TAIL(&mp->mnt_tmpfreevnodelist,
				    vp, v_actfreelist);
				mp->mnt_tmpfreevnodelistsize++;
				vp->v_iflag |= VI_FREE;
				vp->v_mflag |= VMP_TMPMNTFREELIST;
				VI_UNLOCK(vp);
				if (mp->mnt_tmpfreevnodelistsize >=
				    mnt_free_list_batch)
					vnlru_return_batch_locked(mp);
				mtx_unlock(&mp->mnt_listmtx);
			} else {
				VNASSERT(active == 0, vp,
				    ("vdropl: active vnode not on per mount "
				    "vnode list"));
				mtx_lock(&vnode_free_list_mtx);
				TAILQ_INSERT_TAIL(&vnode_free_list, vp,
				    v_actfreelist);
				freevnodes++;
				vp->v_iflag |= VI_FREE;
				VI_UNLOCK(vp);
				mtx_unlock(&vnode_free_list_mtx);
			}
		} else {
			VI_UNLOCK(vp);
			counter_u64_add(free_owe_inact, 1);
		}
		return;
	}
	/*
	 * The vnode has been marked for destruction, so free it.
	 *
	 * The vnode will be returned to the zone where it will
	 * normally remain until it is needed for another vnode. We
	 * need to cleanup (or verify that the cleanup has already
	 * been done) any residual data left from its current use
	 * so as not to contaminate the freshly allocated vnode.
	 */
	CTR2(KTR_VFS, "%s: destroying the vnode %p", __func__, vp);
	atomic_subtract_long(&numvnodes, 1);
	bo = &vp->v_bufobj;
	VNASSERT((vp->v_iflag & VI_FREE) == 0, vp,
	    ("cleaned vnode still on the free list."));
	VNASSERT(vp->v_data == NULL, vp, ("cleaned vnode isn't"));
	VNASSERT(vp->v_holdcnt == 0, vp, ("Non-zero hold count"));
	VNASSERT(vp->v_usecount == 0, vp, ("Non-zero use count"));
	VNASSERT(vp->v_writecount == 0, vp, ("Non-zero write count"));
	VNASSERT(bo->bo_numoutput == 0, vp, ("Clean vnode has pending I/O's"));
	VNASSERT(bo->bo_clean.bv_cnt == 0, vp, ("cleanbufcnt not 0"));
	VNASSERT(pctrie_is_empty(&bo->bo_clean.bv_root), vp,
	    ("clean blk trie not empty"));
	VNASSERT(bo->bo_dirty.bv_cnt == 0, vp, ("dirtybufcnt not 0"));
	VNASSERT(pctrie_is_empty(&bo->bo_dirty.bv_root), vp,
	    ("dirty blk trie not empty"));
	VNASSERT(TAILQ_EMPTY(&vp->v_cache_dst), vp, ("vp has namecache dst"));
	VNASSERT(LIST_EMPTY(&vp->v_cache_src), vp, ("vp has namecache src"));
	VNASSERT(vp->v_cache_dd == NULL, vp, ("vp has namecache for .."));
	VNASSERT(TAILQ_EMPTY(&vp->v_rl.rl_waiters), vp,
	    ("Dangling rangelock waiters"));
	VI_UNLOCK(vp);
#ifdef MAC
	mac_vnode_destroy(vp);
#endif
	if (vp->v_pollinfo != NULL) {
		destroy_vpollinfo(vp->v_pollinfo);
		vp->v_pollinfo = NULL;
	}
#ifdef INVARIANTS
	/* XXX Elsewhere we detect an already freed vnode via NULL v_op. */
	vp->v_op = NULL;
#endif
	vp->v_mountedhere = NULL;
	vp->v_unpcb = NULL;
	vp->v_rdev = NULL;
	vp->v_fifoinfo = NULL;
	vp->v_lasta = vp->v_clen = vp->v_cstart = vp->v_lastw = 0;
	vp->v_iflag = 0;
	vp->v_vflag = 0;
	bo->bo_flag = 0;
	uma_zfree(vnode_zone, vp);
}

/*
 * Call VOP_INACTIVE on the vnode and manage the DOINGINACT and OWEINACT
 * flags.  DOINGINACT prevents us from recursing in calls to vinactive.
 * OWEINACT tracks whether a vnode missed a call to inactive due to a
 * failed lock upgrade.
 */
void
vinactive(struct vnode *vp, struct thread *td)
{
	struct vm_object *obj;

	ASSERT_VOP_ELOCKED(vp, "vinactive");
	ASSERT_VI_LOCKED(vp, "vinactive");
	VNASSERT((vp->v_iflag & VI_DOINGINACT) == 0, vp,
	    ("vinactive: recursed on VI_DOINGINACT"));
	CTR2(KTR_VFS, "%s: vp %p", __func__, vp);
	vp->v_iflag |= VI_DOINGINACT;
	vp->v_iflag &= ~VI_OWEINACT;
	VI_UNLOCK(vp);
	/*
	 * Before moving off the active list, we must be sure that any
	 * modified pages are converted into the vnode's dirty
	 * buffers, since these will no longer be checked once the
	 * vnode is on the inactive list.
	 *
	 * The write-out of the dirty pages is asynchronous.  At the
	 * point that VOP_INACTIVE() is called, there could still be
	 * pending I/O and dirty pages in the object.
	 */
	if ((obj = vp->v_object) != NULL && (vp->v_vflag & VV_NOSYNC) == 0 &&
	    (obj->flags & OBJ_MIGHTBEDIRTY) != 0) {
		VM_OBJECT_WLOCK(obj);
		vm_object_page_clean(obj, 0, 0, 0);
		VM_OBJECT_WUNLOCK(obj);
	}
	VOP_INACTIVE(vp, td);
	VI_LOCK(vp);
	VNASSERT(vp->v_iflag & VI_DOINGINACT, vp,
	    ("vinactive: lost VI_DOINGINACT"));
	vp->v_iflag &= ~VI_DOINGINACT;
}

/*
 * Remove any vnodes in the vnode table belonging to mount point mp.
 *
 * If FORCECLOSE is not specified, there should not be any active ones,
 * return error if any are found (nb: this is a user error, not a
 * system error). If FORCECLOSE is specified, detach any active vnodes
 * that are found.
 *
 * If WRITECLOSE is set, only flush out regular file vnodes open for
 * writing.
 *
 * SKIPSYSTEM causes any vnodes marked VV_SYSTEM to be skipped.
 *
 * `rootrefs' specifies the base reference count for the root vnode
 * of this filesystem. The root vnode is considered busy if its
 * v_usecount exceeds this value. On a successful return, vflush(, td)
 * will call vrele() on the root vnode exactly rootrefs times.
 * If the SKIPSYSTEM or WRITECLOSE flags are specified, rootrefs must
 * be zero.
 */
#ifdef DIAGNOSTIC
static int busyprt = 0;		/* print out busy vnodes */
SYSCTL_INT(_debug, OID_AUTO, busyprt, CTLFLAG_RW, &busyprt, 0, "Print out busy vnodes");
#endif

int
vflush(struct mount *mp, int rootrefs, int flags, struct thread *td)
{
	struct vnode *vp, *mvp, *rootvp = NULL;
	struct vattr vattr;
	int busy = 0, error;

	CTR4(KTR_VFS, "%s: mp %p with rootrefs %d and flags %d", __func__, mp,
	    rootrefs, flags);
	if (rootrefs > 0) {
		KASSERT((flags & (SKIPSYSTEM | WRITECLOSE)) == 0,
		    ("vflush: bad args"));
		/*
		 * Get the filesystem root vnode. We can vput() it
		 * immediately, since with rootrefs > 0, it won't go away.
		 */
		if ((error = VFS_ROOT(mp, LK_EXCLUSIVE, &rootvp)) != 0) {
			CTR2(KTR_VFS, "%s: vfs_root lookup failed with %d",
			    __func__, error);
			return (error);
		}
		vput(rootvp);
	}
loop:
	MNT_VNODE_FOREACH_ALL(vp, mp, mvp) {
		vholdl(vp);
		error = vn_lock(vp, LK_INTERLOCK | LK_EXCLUSIVE);
		if (error) {
			vdrop(vp);
			MNT_VNODE_FOREACH_ALL_ABORT(mp, mvp);
			goto loop;
		}
		/*
		 * Skip over a vnodes marked VV_SYSTEM.
		 */
		if ((flags & SKIPSYSTEM) && (vp->v_vflag & VV_SYSTEM)) {
			VOP_UNLOCK(vp, 0);
			vdrop(vp);
			continue;
		}
		/*
		 * If WRITECLOSE is set, flush out unlinked but still open
		 * files (even if open only for reading) and regular file
		 * vnodes open for writing.
		 */
		if (flags & WRITECLOSE) {
			if (vp->v_object != NULL) {
				VM_OBJECT_WLOCK(vp->v_object);
				vm_object_page_clean(vp->v_object, 0, 0, 0);
				VM_OBJECT_WUNLOCK(vp->v_object);
			}
			error = VOP_FSYNC(vp, MNT_WAIT, td);
			if (error != 0) {
				VOP_UNLOCK(vp, 0);
				vdrop(vp);
				MNT_VNODE_FOREACH_ALL_ABORT(mp, mvp);
				return (error);
			}
			error = VOP_GETATTR(vp, &vattr, td->td_ucred);
			VI_LOCK(vp);

			if ((vp->v_type == VNON ||
			    (error == 0 && vattr.va_nlink > 0)) &&
			    (vp->v_writecount == 0 || vp->v_type != VREG)) {
				VOP_UNLOCK(vp, 0);
				vdropl(vp);
				continue;
			}
		} else
			VI_LOCK(vp);
		/*
		 * With v_usecount == 0, all we need to do is clear out the
		 * vnode data structures and we are done.
		 *
		 * If FORCECLOSE is set, forcibly close the vnode.
		 */
		if (vp->v_usecount == 0 || (flags & FORCECLOSE)) {
			vgonel(vp);
		} else {
			busy++;
#ifdef DIAGNOSTIC
			if (busyprt)
				vn_printf(vp, "vflush: busy vnode ");
#endif
		}
		VOP_UNLOCK(vp, 0);
		vdropl(vp);
	}
	if (rootrefs > 0 && (flags & FORCECLOSE) == 0) {
		/*
		 * If just the root vnode is busy, and if its refcount
		 * is equal to `rootrefs', then go ahead and kill it.
		 */
		VI_LOCK(rootvp);
		KASSERT(busy > 0, ("vflush: not busy"));
		VNASSERT(rootvp->v_usecount >= rootrefs, rootvp,
		    ("vflush: usecount %d < rootrefs %d",
		     rootvp->v_usecount, rootrefs));
		if (busy == 1 && rootvp->v_usecount == rootrefs) {
			VOP_LOCK(rootvp, LK_EXCLUSIVE|LK_INTERLOCK);
			vgone(rootvp);
			VOP_UNLOCK(rootvp, 0);
			busy = 0;
		} else
			VI_UNLOCK(rootvp);
	}
	if (busy) {
		CTR2(KTR_VFS, "%s: failing as %d vnodes are busy", __func__,
		    busy);
		return (EBUSY);
	}
	for (; rootrefs > 0; rootrefs--)
		vrele(rootvp);
	return (0);
}

/*
 * Recycle an unused vnode to the front of the free list.
 */
int
vrecycle(struct vnode *vp)
{
	int recycled;

	VI_LOCK(vp);
	recycled = vrecyclel(vp);
	VI_UNLOCK(vp);
	return (recycled);
}

/*
 * vrecycle, with the vp interlock held.
 */
int
vrecyclel(struct vnode *vp)
{
	int recycled;

	ASSERT_VOP_ELOCKED(vp, __func__);
	ASSERT_VI_LOCKED(vp, __func__);
	CTR2(KTR_VFS, "%s: vp %p", __func__, vp);
	recycled = 0;
	if (vp->v_usecount == 0) {
		recycled = 1;
		vgonel(vp);
	}
	return (recycled);
}

/*
 * Eliminate all activity associated with a vnode
 * in preparation for reuse.
 */
void
vgone(struct vnode *vp)
{
	VI_LOCK(vp);
	vgonel(vp);
	VI_UNLOCK(vp);
}

static void
notify_lowervp_vfs_dummy(struct mount *mp __unused,
    struct vnode *lowervp __unused)
{
}

/*
 * Notify upper mounts about reclaimed or unlinked vnode.
 */
void
vfs_notify_upper(struct vnode *vp, int event)
{
	static struct vfsops vgonel_vfsops = {
		.vfs_reclaim_lowervp = notify_lowervp_vfs_dummy,
		.vfs_unlink_lowervp = notify_lowervp_vfs_dummy,
	};
	struct mount *mp, *ump, *mmp;

	mp = vp->v_mount;
	if (mp == NULL)
		return;

	MNT_ILOCK(mp);
	if (TAILQ_EMPTY(&mp->mnt_uppers))
		goto unlock;
	MNT_IUNLOCK(mp);
	mmp = malloc(sizeof(struct mount), M_TEMP, M_WAITOK | M_ZERO);
	mmp->mnt_op = &vgonel_vfsops;
	mmp->mnt_kern_flag |= MNTK_MARKER;
	MNT_ILOCK(mp);
	mp->mnt_kern_flag |= MNTK_VGONE_UPPER;
	for (ump = TAILQ_FIRST(&mp->mnt_uppers); ump != NULL;) {
		if ((ump->mnt_kern_flag & MNTK_MARKER) != 0) {
			ump = TAILQ_NEXT(ump, mnt_upper_link);
			continue;
		}
		TAILQ_INSERT_AFTER(&mp->mnt_uppers, ump, mmp, mnt_upper_link);
		MNT_IUNLOCK(mp);
		switch (event) {
		case VFS_NOTIFY_UPPER_RECLAIM:
			VFS_RECLAIM_LOWERVP(ump, vp);
			break;
		case VFS_NOTIFY_UPPER_UNLINK:
			VFS_UNLINK_LOWERVP(ump, vp);
			break;
		default:
			KASSERT(0, ("invalid event %d", event));
			break;
		}
		MNT_ILOCK(mp);
		ump = TAILQ_NEXT(mmp, mnt_upper_link);
		TAILQ_REMOVE(&mp->mnt_uppers, mmp, mnt_upper_link);
	}
	free(mmp, M_TEMP);
	mp->mnt_kern_flag &= ~MNTK_VGONE_UPPER;
	if ((mp->mnt_kern_flag & MNTK_VGONE_WAITER) != 0) {
		mp->mnt_kern_flag &= ~MNTK_VGONE_WAITER;
		wakeup(&mp->mnt_uppers);
	}
unlock:
	MNT_IUNLOCK(mp);
}

/*
 * vgone, with the vp interlock held.
 */
static void
vgonel(struct vnode *vp)
{
	struct thread *td;
	int oweinact;
	int active;
	struct mount *mp;

	ASSERT_VOP_ELOCKED(vp, "vgonel");
	ASSERT_VI_LOCKED(vp, "vgonel");
	VNASSERT(vp->v_holdcnt, vp,
	    ("vgonel: vp %p has no reference.", vp));
	CTR2(KTR_VFS, "%s: vp %p", __func__, vp);
	td = curthread;

	/*
	 * Don't vgonel if we're already doomed.
	 */
	if (vp->v_iflag & VI_DOOMED)
		return;
	vp->v_iflag |= VI_DOOMED;

	/*
	 * Check to see if the vnode is in use.  If so, we have to call
	 * VOP_CLOSE() and VOP_INACTIVE().
	 */
	active = vp->v_usecount;
	oweinact = (vp->v_iflag & VI_OWEINACT);
	VI_UNLOCK(vp);
	vfs_notify_upper(vp, VFS_NOTIFY_UPPER_RECLAIM);

	/*
	 * If purging an active vnode, it must be closed and
	 * deactivated before being reclaimed.
	 */
	if (active)
		VOP_CLOSE(vp, FNONBLOCK, NOCRED, td);
	if (oweinact || active) {
		VI_LOCK(vp);
		if ((vp->v_iflag & VI_DOINGINACT) == 0)
			vinactive(vp, td);
		VI_UNLOCK(vp);
	}
	if (vp->v_type == VSOCK)
		vfs_unp_reclaim(vp);

	/*
	 * Clean out any buffers associated with the vnode.
	 * If the flush fails, just toss the buffers.
	 */
	mp = NULL;
	if (!TAILQ_EMPTY(&vp->v_bufobj.bo_dirty.bv_hd))
		(void) vn_start_secondary_write(vp, &mp, V_WAIT);
	if (vinvalbuf(vp, V_SAVE, 0, 0) != 0) {
		while (vinvalbuf(vp, 0, 0, 0) != 0)
			;
	}

	BO_LOCK(&vp->v_bufobj);
	KASSERT(TAILQ_EMPTY(&vp->v_bufobj.bo_dirty.bv_hd) &&
	    vp->v_bufobj.bo_dirty.bv_cnt == 0 &&
	    TAILQ_EMPTY(&vp->v_bufobj.bo_clean.bv_hd) &&
	    vp->v_bufobj.bo_clean.bv_cnt == 0,
	    ("vp %p bufobj not invalidated", vp));

	/*
	 * For VMIO bufobj, BO_DEAD is set in vm_object_terminate()
	 * after the object's page queue is flushed.
	 */
	if (vp->v_bufobj.bo_object == NULL)
		vp->v_bufobj.bo_flag |= BO_DEAD;
	BO_UNLOCK(&vp->v_bufobj);

	/*
	 * Reclaim the vnode.
	 */
	if (VOP_RECLAIM(vp, td))
		panic("vgone: cannot reclaim");
	if (mp != NULL)
		vn_finished_secondary_write(mp);
	VNASSERT(vp->v_object == NULL, vp,
	    ("vop_reclaim left v_object vp=%p, tag=%s", vp, vp->v_tag));
	/*
	 * Clear the advisory locks and wake up waiting threads.
	 */
	(void)VOP_ADVLOCKPURGE(vp);
	vp->v_lockf = NULL;
	/*
	 * Delete from old mount point vnode list.
	 */
	delmntque(vp);
	cache_purge(vp);
	/*
	 * Done with purge, reset to the standard lock and invalidate
	 * the vnode.
	 */
	VI_LOCK(vp);
	vp->v_vnlock = &vp->v_lock;
	vp->v_op = &dead_vnodeops;
	vp->v_tag = "none";
	vp->v_type = VBAD;
}

/*
 * Calculate the total number of references to a special device.
 */
int
vcount(struct vnode *vp)
{
	int count;

	dev_lock();
	count = vp->v_rdev->si_usecount;
	dev_unlock();
	return (count);
}

/*
 * Same as above, but using the struct cdev *as argument
 */
int
count_dev(struct cdev *dev)
{
	int count;

	dev_lock();
	count = dev->si_usecount;
	dev_unlock();
	return(count);
}

/*
 * Print out a description of a vnode.
 */
static char *typename[] =
{"VNON", "VREG", "VDIR", "VBLK", "VCHR", "VLNK", "VSOCK", "VFIFO", "VBAD",
 "VMARKER"};

void
vn_printf(struct vnode *vp, const char *fmt, ...)
{
	va_list ap;
	char buf[256], buf2[16];
	u_long flags;

	va_start(ap, fmt);
	vprintf(fmt, ap);
	va_end(ap);
	printf("%p: ", (void *)vp);
	printf("tag %s, type %s\n", vp->v_tag, typename[vp->v_type]);
	printf("    usecount %d, writecount %d, refcount %d",
	    vp->v_usecount, vp->v_writecount, vp->v_holdcnt);
	switch (vp->v_type) {
	case VDIR:
		printf(" mountedhere %p\n", vp->v_mountedhere);
		break;
	case VCHR:
		printf(" rdev %p\n", vp->v_rdev);
		break;
	case VSOCK:
		printf(" socket %p\n", vp->v_unpcb);
		break;
	case VFIFO:
		printf(" fifoinfo %p\n", vp->v_fifoinfo);
		break;
	default:
		printf("\n");
		break;
	}
	buf[0] = '\0';
	buf[1] = '\0';
	if (vp->v_vflag & VV_ROOT)
		strlcat(buf, "|VV_ROOT", sizeof(buf));
	if (vp->v_vflag & VV_ISTTY)
		strlcat(buf, "|VV_ISTTY", sizeof(buf));
	if (vp->v_vflag & VV_NOSYNC)
		strlcat(buf, "|VV_NOSYNC", sizeof(buf));
	if (vp->v_vflag & VV_ETERNALDEV)
		strlcat(buf, "|VV_ETERNALDEV", sizeof(buf));
	if (vp->v_vflag & VV_CACHEDLABEL)
		strlcat(buf, "|VV_CACHEDLABEL", sizeof(buf));
	if (vp->v_vflag & VV_TEXT)
		strlcat(buf, "|VV_TEXT", sizeof(buf));
	if (vp->v_vflag & VV_COPYONWRITE)
		strlcat(buf, "|VV_COPYONWRITE", sizeof(buf));
	if (vp->v_vflag & VV_SYSTEM)
		strlcat(buf, "|VV_SYSTEM", sizeof(buf));
	if (vp->v_vflag & VV_PROCDEP)
		strlcat(buf, "|VV_PROCDEP", sizeof(buf));
	if (vp->v_vflag & VV_NOKNOTE)
		strlcat(buf, "|VV_NOKNOTE", sizeof(buf));
	if (vp->v_vflag & VV_DELETED)
		strlcat(buf, "|VV_DELETED", sizeof(buf));
	if (vp->v_vflag & VV_MD)
		strlcat(buf, "|VV_MD", sizeof(buf));
	if (vp->v_vflag & VV_FORCEINSMQ)
		strlcat(buf, "|VV_FORCEINSMQ", sizeof(buf));
	flags = vp->v_vflag & ~(VV_ROOT | VV_ISTTY | VV_NOSYNC | VV_ETERNALDEV |
	    VV_CACHEDLABEL | VV_TEXT | VV_COPYONWRITE | VV_SYSTEM | VV_PROCDEP |
	    VV_NOKNOTE | VV_DELETED | VV_MD | VV_FORCEINSMQ);
	if (flags != 0) {
		snprintf(buf2, sizeof(buf2), "|VV(0x%lx)", flags);
		strlcat(buf, buf2, sizeof(buf));
	}
	if (vp->v_iflag & VI_MOUNT)
		strlcat(buf, "|VI_MOUNT", sizeof(buf));
	if (vp->v_iflag & VI_DOOMED)
		strlcat(buf, "|VI_DOOMED", sizeof(buf));
	if (vp->v_iflag & VI_FREE)
		strlcat(buf, "|VI_FREE", sizeof(buf));
	if (vp->v_iflag & VI_ACTIVE)
		strlcat(buf, "|VI_ACTIVE", sizeof(buf));
	if (vp->v_iflag & VI_DOINGINACT)
		strlcat(buf, "|VI_DOINGINACT", sizeof(buf));
	if (vp->v_iflag & VI_OWEINACT)
		strlcat(buf, "|VI_OWEINACT", sizeof(buf));
	flags = vp->v_iflag & ~(VI_MOUNT | VI_DOOMED | VI_FREE |
	    VI_ACTIVE | VI_DOINGINACT | VI_OWEINACT);
	if (flags != 0) {
		snprintf(buf2, sizeof(buf2), "|VI(0x%lx)", flags);
		strlcat(buf, buf2, sizeof(buf));
	}
	printf("    flags (%s)\n", buf + 1);
	if (mtx_owned(VI_MTX(vp)))
		printf(" VI_LOCKed");
	if (vp->v_object != NULL)
		printf("    v_object %p ref %d pages %d "
		    "cleanbuf %d dirtybuf %d\n",
		    vp->v_object, vp->v_object->ref_count,
		    vp->v_object->resident_page_count,
		    vp->v_bufobj.bo_clean.bv_cnt,
		    vp->v_bufobj.bo_dirty.bv_cnt);
	printf("    ");
	lockmgr_printinfo(vp->v_vnlock);
	if (vp->v_data != NULL)
		VOP_PRINT(vp);
}

#ifdef DDB
/*
 * List all of the locked vnodes in the system.
 * Called when debugging the kernel.
 */
DB_SHOW_COMMAND(lockedvnods, lockedvnodes)
{
	struct mount *mp;
	struct vnode *vp;

	/*
	 * Note: because this is DDB, we can't obey the locking semantics
	 * for these structures, which means we could catch an inconsistent
	 * state and dereference a nasty pointer.  Not much to be done
	 * about that.
	 */
	db_printf("Locked vnodes\n");
	TAILQ_FOREACH(mp, &mountlist, mnt_list) {
		TAILQ_FOREACH(vp, &mp->mnt_nvnodelist, v_nmntvnodes) {
			if (vp->v_type != VMARKER && VOP_ISLOCKED(vp))
				vn_printf(vp, "vnode ");
		}
	}
}

/*
 * Show details about the given vnode.
 */
DB_SHOW_COMMAND(vnode, db_show_vnode)
{
	struct vnode *vp;

	if (!have_addr)
		return;
	vp = (struct vnode *)addr;
	vn_printf(vp, "vnode ");
}

/*
 * Show details about the given mount point.
 */
DB_SHOW_COMMAND(mount, db_show_mount)
{
	struct mount *mp;
	struct vfsopt *opt;
	struct statfs *sp;
	struct vnode *vp;
	char buf[512];
	uint64_t mflags;
	u_int flags;

	if (!have_addr) {
		/* No address given, print short info about all mount points. */
		TAILQ_FOREACH(mp, &mountlist, mnt_list) {
			db_printf("%p %s on %s (%s)\n", mp,
			    mp->mnt_stat.f_mntfromname,
			    mp->mnt_stat.f_mntonname,
			    mp->mnt_stat.f_fstypename);
			if (db_pager_quit)
				break;
		}
		db_printf("\nMore info: show mount <addr>\n");
		return;
	}

	mp = (struct mount *)addr;
	db_printf("%p %s on %s (%s)\n", mp, mp->mnt_stat.f_mntfromname,
	    mp->mnt_stat.f_mntonname, mp->mnt_stat.f_fstypename);

	buf[0] = '\0';
	mflags = mp->mnt_flag;
#define	MNT_FLAG(flag)	do {						\
	if (mflags & (flag)) {						\
		if (buf[0] != '\0')					\
			strlcat(buf, ", ", sizeof(buf));		\
		strlcat(buf, (#flag) + 4, sizeof(buf));			\
		mflags &= ~(flag);					\
	}								\
} while (0)
	MNT_FLAG(MNT_RDONLY);
	MNT_FLAG(MNT_SYNCHRONOUS);
	MNT_FLAG(MNT_NOEXEC);
	MNT_FLAG(MNT_NOSUID);
	MNT_FLAG(MNT_NFS4ACLS);
	MNT_FLAG(MNT_UNION);
	MNT_FLAG(MNT_ASYNC);
	MNT_FLAG(MNT_SUIDDIR);
	MNT_FLAG(MNT_SOFTDEP);
	MNT_FLAG(MNT_NOSYMFOLLOW);
	MNT_FLAG(MNT_GJOURNAL);
	MNT_FLAG(MNT_MULTILABEL);
	MNT_FLAG(MNT_ACLS);
	MNT_FLAG(MNT_NOATIME);
	MNT_FLAG(MNT_NOCLUSTERR);
	MNT_FLAG(MNT_NOCLUSTERW);
	MNT_FLAG(MNT_SUJ);
	MNT_FLAG(MNT_EXRDONLY);
	MNT_FLAG(MNT_EXPORTED);
	MNT_FLAG(MNT_DEFEXPORTED);
	MNT_FLAG(MNT_EXPORTANON);
	MNT_FLAG(MNT_EXKERB);
	MNT_FLAG(MNT_EXPUBLIC);
	MNT_FLAG(MNT_LOCAL);
	MNT_FLAG(MNT_QUOTA);
	MNT_FLAG(MNT_ROOTFS);
	MNT_FLAG(MNT_USER);
	MNT_FLAG(MNT_IGNORE);
	MNT_FLAG(MNT_UPDATE);
	MNT_FLAG(MNT_DELEXPORT);
	MNT_FLAG(MNT_RELOAD);
	MNT_FLAG(MNT_FORCE);
	MNT_FLAG(MNT_SNAPSHOT);
	MNT_FLAG(MNT_BYFSID);
#undef MNT_FLAG
	if (mflags != 0) {
		if (buf[0] != '\0')
			strlcat(buf, ", ", sizeof(buf));
		snprintf(buf + strlen(buf), sizeof(buf) - strlen(buf),
		    "0x%016jx", mflags);
	}
	db_printf("    mnt_flag = %s\n", buf);

	buf[0] = '\0';
	flags = mp->mnt_kern_flag;
#define	MNT_KERN_FLAG(flag)	do {					\
	if (flags & (flag)) {						\
		if (buf[0] != '\0')					\
			strlcat(buf, ", ", sizeof(buf));		\
		strlcat(buf, (#flag) + 5, sizeof(buf));			\
		flags &= ~(flag);					\
	}								\
} while (0)
	MNT_KERN_FLAG(MNTK_UNMOUNTF);
	MNT_KERN_FLAG(MNTK_ASYNC);
	MNT_KERN_FLAG(MNTK_SOFTDEP);
	MNT_KERN_FLAG(MNTK_NOINSMNTQ);
	MNT_KERN_FLAG(MNTK_DRAINING);
	MNT_KERN_FLAG(MNTK_REFEXPIRE);
	MNT_KERN_FLAG(MNTK_EXTENDED_SHARED);
	MNT_KERN_FLAG(MNTK_SHARED_WRITES);
	MNT_KERN_FLAG(MNTK_NO_IOPF);
	MNT_KERN_FLAG(MNTK_VGONE_UPPER);
	MNT_KERN_FLAG(MNTK_VGONE_WAITER);
	MNT_KERN_FLAG(MNTK_LOOKUP_EXCL_DOTDOT);
	MNT_KERN_FLAG(MNTK_MARKER);
	MNT_KERN_FLAG(MNTK_USES_BCACHE);
	MNT_KERN_FLAG(MNTK_NOASYNC);
	MNT_KERN_FLAG(MNTK_UNMOUNT);
	MNT_KERN_FLAG(MNTK_MWAIT);
	MNT_KERN_FLAG(MNTK_SUSPEND);
	MNT_KERN_FLAG(MNTK_SUSPEND2);
	MNT_KERN_FLAG(MNTK_SUSPENDED);
	MNT_KERN_FLAG(MNTK_LOOKUP_SHARED);
	MNT_KERN_FLAG(MNTK_NOKNOTE);
#undef MNT_KERN_FLAG
	if (flags != 0) {
		if (buf[0] != '\0')
			strlcat(buf, ", ", sizeof(buf));
		snprintf(buf + strlen(buf), sizeof(buf) - strlen(buf),
		    "0x%08x", flags);
	}
	db_printf("    mnt_kern_flag = %s\n", buf);

	db_printf("    mnt_opt = ");
	opt = TAILQ_FIRST(mp->mnt_opt);
	if (opt != NULL) {
		db_printf("%s", opt->name);
		opt = TAILQ_NEXT(opt, link);
		while (opt != NULL) {
			db_printf(", %s", opt->name);
			opt = TAILQ_NEXT(opt, link);
		}
	}
	db_printf("\n");

	sp = &mp->mnt_stat;
	db_printf("    mnt_stat = { version=%u type=%u flags=0x%016jx "
	    "bsize=%ju iosize=%ju blocks=%ju bfree=%ju bavail=%jd files=%ju "
	    "ffree=%jd syncwrites=%ju asyncwrites=%ju syncreads=%ju "
	    "asyncreads=%ju namemax=%u owner=%u fsid=[%d, %d] }\n",
	    (u_int)sp->f_version, (u_int)sp->f_type, (uintmax_t)sp->f_flags,
	    (uintmax_t)sp->f_bsize, (uintmax_t)sp->f_iosize,
	    (uintmax_t)sp->f_blocks, (uintmax_t)sp->f_bfree,
	    (intmax_t)sp->f_bavail, (uintmax_t)sp->f_files,
	    (intmax_t)sp->f_ffree, (uintmax_t)sp->f_syncwrites,
	    (uintmax_t)sp->f_asyncwrites, (uintmax_t)sp->f_syncreads,
	    (uintmax_t)sp->f_asyncreads, (u_int)sp->f_namemax,
	    (u_int)sp->f_owner, (int)sp->f_fsid.val[0], (int)sp->f_fsid.val[1]);

	db_printf("    mnt_cred = { uid=%u ruid=%u",
	    (u_int)mp->mnt_cred->cr_uid, (u_int)mp->mnt_cred->cr_ruid);
	if (jailed(mp->mnt_cred))
		db_printf(", jail=%d", mp->mnt_cred->cr_prison->pr_id);
	db_printf(" }\n");
	db_printf("    mnt_ref = %d\n", mp->mnt_ref);
	db_printf("    mnt_gen = %d\n", mp->mnt_gen);
	db_printf("    mnt_nvnodelistsize = %d\n", mp->mnt_nvnodelistsize);
	db_printf("    mnt_activevnodelistsize = %d\n",
	    mp->mnt_activevnodelistsize);
	db_printf("    mnt_writeopcount = %d\n", mp->mnt_writeopcount);
	db_printf("    mnt_maxsymlinklen = %d\n", mp->mnt_maxsymlinklen);
	db_printf("    mnt_iosize_max = %d\n", mp->mnt_iosize_max);
	db_printf("    mnt_hashseed = %u\n", mp->mnt_hashseed);
	db_printf("    mnt_lockref = %d\n", mp->mnt_lockref);
	db_printf("    mnt_secondary_writes = %d\n", mp->mnt_secondary_writes);
	db_printf("    mnt_secondary_accwrites = %d\n",
	    mp->mnt_secondary_accwrites);
	db_printf("    mnt_gjprovider = %s\n",
	    mp->mnt_gjprovider != NULL ? mp->mnt_gjprovider : "NULL");

	db_printf("\n\nList of active vnodes\n");
	TAILQ_FOREACH(vp, &mp->mnt_activevnodelist, v_actfreelist) {
		if (vp->v_type != VMARKER) {
			vn_printf(vp, "vnode ");
			if (db_pager_quit)
				break;
		}
	}
	db_printf("\n\nList of inactive vnodes\n");
	TAILQ_FOREACH(vp, &mp->mnt_nvnodelist, v_nmntvnodes) {
		if (vp->v_type != VMARKER && (vp->v_iflag & VI_ACTIVE) == 0) {
			vn_printf(vp, "vnode ");
			if (db_pager_quit)
				break;
		}
	}
}
#endif	/* DDB */

/*
 * Fill in a struct xvfsconf based on a struct vfsconf.
 */
static int
vfsconf2x(struct sysctl_req *req, struct vfsconf *vfsp)
{
	struct xvfsconf xvfsp;

	bzero(&xvfsp, sizeof(xvfsp));
	strcpy(xvfsp.vfc_name, vfsp->vfc_name);
	xvfsp.vfc_typenum = vfsp->vfc_typenum;
	xvfsp.vfc_refcount = vfsp->vfc_refcount;
	xvfsp.vfc_flags = vfsp->vfc_flags;
	/*
	 * These are unused in userland, we keep them
	 * to not break binary compatibility.
	 */
	xvfsp.vfc_vfsops = NULL;
	xvfsp.vfc_next = NULL;
	return (SYSCTL_OUT(req, &xvfsp, sizeof(xvfsp)));
}

#ifdef COMPAT_FREEBSD32
struct xvfsconf32 {
	uint32_t	vfc_vfsops;
	char		vfc_name[MFSNAMELEN];
	int32_t		vfc_typenum;
	int32_t		vfc_refcount;
	int32_t		vfc_flags;
	uint32_t	vfc_next;
};

static int
vfsconf2x32(struct sysctl_req *req, struct vfsconf *vfsp)
{
	struct xvfsconf32 xvfsp;

	bzero(&xvfsp, sizeof(xvfsp));
	strcpy(xvfsp.vfc_name, vfsp->vfc_name);
	xvfsp.vfc_typenum = vfsp->vfc_typenum;
	xvfsp.vfc_refcount = vfsp->vfc_refcount;
	xvfsp.vfc_flags = vfsp->vfc_flags;
	return (SYSCTL_OUT(req, &xvfsp, sizeof(xvfsp)));
}
#endif

/*
 * Top level filesystem related information gathering.
 */
static int
sysctl_vfs_conflist(SYSCTL_HANDLER_ARGS)
{
	struct vfsconf *vfsp;
	int error;

	error = 0;
	vfsconf_slock();
	TAILQ_FOREACH(vfsp, &vfsconf, vfc_list) {
#ifdef COMPAT_FREEBSD32
		if (req->flags & SCTL_MASK32)
			error = vfsconf2x32(req, vfsp);
		else
#endif
			error = vfsconf2x(req, vfsp);
		if (error)
			break;
	}
	vfsconf_sunlock();
	return (error);
}

SYSCTL_PROC(_vfs, OID_AUTO, conflist, CTLTYPE_OPAQUE | CTLFLAG_RD |
    CTLFLAG_MPSAFE, NULL, 0, sysctl_vfs_conflist,
    "S,xvfsconf", "List of all configured filesystems");

#ifndef BURN_BRIDGES
static int	sysctl_ovfs_conf(SYSCTL_HANDLER_ARGS);

static int
vfs_sysctl(SYSCTL_HANDLER_ARGS)
{
	int *name = (int *)arg1 - 1;	/* XXX */
	u_int namelen = arg2 + 1;	/* XXX */
	struct vfsconf *vfsp;

	log(LOG_WARNING, "userland calling deprecated sysctl, "
	    "please rebuild world\n");

#if 1 || defined(COMPAT_PRELITE2)
	/* Resolve ambiguity between VFS_VFSCONF and VFS_GENERIC. */
	if (namelen == 1)
		return (sysctl_ovfs_conf(oidp, arg1, arg2, req));
#endif

	switch (name[1]) {
	case VFS_MAXTYPENUM:
		if (namelen != 2)
			return (ENOTDIR);
		return (SYSCTL_OUT(req, &maxvfsconf, sizeof(int)));
	case VFS_CONF:
		if (namelen != 3)
			return (ENOTDIR);	/* overloaded */
		vfsconf_slock();
		TAILQ_FOREACH(vfsp, &vfsconf, vfc_list) {
			if (vfsp->vfc_typenum == name[2])
				break;
		}
		vfsconf_sunlock();
		if (vfsp == NULL)
			return (EOPNOTSUPP);
#ifdef COMPAT_FREEBSD32
		if (req->flags & SCTL_MASK32)
			return (vfsconf2x32(req, vfsp));
		else
#endif
			return (vfsconf2x(req, vfsp));
	}
	return (EOPNOTSUPP);
}

static SYSCTL_NODE(_vfs, VFS_GENERIC, generic, CTLFLAG_RD | CTLFLAG_SKIP |
    CTLFLAG_MPSAFE, vfs_sysctl,
    "Generic filesystem");

#if 1 || defined(COMPAT_PRELITE2)

static int
sysctl_ovfs_conf(SYSCTL_HANDLER_ARGS)
{
	int error;
	struct vfsconf *vfsp;
	struct ovfsconf ovfs;

	vfsconf_slock();
	TAILQ_FOREACH(vfsp, &vfsconf, vfc_list) {
		bzero(&ovfs, sizeof(ovfs));
		ovfs.vfc_vfsops = vfsp->vfc_vfsops;	/* XXX used as flag */
		strcpy(ovfs.vfc_name, vfsp->vfc_name);
		ovfs.vfc_index = vfsp->vfc_typenum;
		ovfs.vfc_refcount = vfsp->vfc_refcount;
		ovfs.vfc_flags = vfsp->vfc_flags;
		error = SYSCTL_OUT(req, &ovfs, sizeof ovfs);
		if (error != 0) {
			vfsconf_sunlock();
			return (error);
		}
	}
	vfsconf_sunlock();
	return (0);
}

#endif /* 1 || COMPAT_PRELITE2 */
#endif /* !BURN_BRIDGES */

#define KINFO_VNODESLOP		10
#ifdef notyet
/*
 * Dump vnode list (via sysctl).
 */
/* ARGSUSED */
static int
sysctl_vnode(SYSCTL_HANDLER_ARGS)
{
	struct xvnode *xvn;
	struct mount *mp;
	struct vnode *vp;
	int error, len, n;

	/*
	 * Stale numvnodes access is not fatal here.
	 */
	req->lock = 0;
	len = (numvnodes + KINFO_VNODESLOP) * sizeof *xvn;
	if (!req->oldptr)
		/* Make an estimate */
		return (SYSCTL_OUT(req, 0, len));

	error = sysctl_wire_old_buffer(req, 0);
	if (error != 0)
		return (error);
	xvn = malloc(len, M_TEMP, M_ZERO | M_WAITOK);
	n = 0;
	mtx_lock(&mountlist_mtx);
	TAILQ_FOREACH(mp, &mountlist, mnt_list) {
		if (vfs_busy(mp, MBF_NOWAIT | MBF_MNTLSTLOCK))
			continue;
		MNT_ILOCK(mp);
		TAILQ_FOREACH(vp, &mp->mnt_nvnodelist, v_nmntvnodes) {
			if (n == len)
				break;
			vref(vp);
			xvn[n].xv_size = sizeof *xvn;
			xvn[n].xv_vnode = vp;
			xvn[n].xv_id = 0;	/* XXX compat */
#define XV_COPY(field) xvn[n].xv_##field = vp->v_##field
			XV_COPY(usecount);
			XV_COPY(writecount);
			XV_COPY(holdcnt);
			XV_COPY(mount);
			XV_COPY(numoutput);
			XV_COPY(type);
#undef XV_COPY
			xvn[n].xv_flag = vp->v_vflag;

			switch (vp->v_type) {
			case VREG:
			case VDIR:
			case VLNK:
				break;
			case VBLK:
			case VCHR:
				if (vp->v_rdev == NULL) {
					vrele(vp);
					continue;
				}
				xvn[n].xv_dev = dev2udev(vp->v_rdev);
				break;
			case VSOCK:
				xvn[n].xv_socket = vp->v_socket;
				break;
			case VFIFO:
				xvn[n].xv_fifo = vp->v_fifoinfo;
				break;
			case VNON:
			case VBAD:
			default:
				/* shouldn't happen? */
				vrele(vp);
				continue;
			}
			vrele(vp);
			++n;
		}
		MNT_IUNLOCK(mp);
		mtx_lock(&mountlist_mtx);
		vfs_unbusy(mp);
		if (n == len)
			break;
	}
	mtx_unlock(&mountlist_mtx);

	error = SYSCTL_OUT(req, xvn, n * sizeof *xvn);
	free(xvn, M_TEMP);
	return (error);
}

SYSCTL_PROC(_kern, KERN_VNODE, vnode, CTLTYPE_OPAQUE | CTLFLAG_RD |
    CTLFLAG_MPSAFE, 0, 0, sysctl_vnode, "S,xvnode",
    "");
#endif

static void
unmount_or_warn(struct mount *mp)
{
	int error;

	error = dounmount(mp, MNT_FORCE, curthread);
	if (error != 0) {
		printf("unmount of %s failed (", mp->mnt_stat.f_mntonname);
		if (error == EBUSY)
			printf("BUSY)\n");
		else
			printf("%d)\n", error);
	}
}

/*
 * Unmount all filesystems. The list is traversed in reverse order
 * of mounting to avoid dependencies.
 */
void
vfs_unmountall(void)
{
	struct mount *mp, *tmp;

	CTR1(KTR_VFS, "%s: unmounting all filesystems", __func__);

	/*
	 * Since this only runs when rebooting, it is not interlocked.
	 */
	TAILQ_FOREACH_REVERSE_SAFE(mp, &mountlist, mntlist, mnt_list, tmp) {
		vfs_ref(mp);

		/*
		 * Forcibly unmounting "/dev" before "/" would prevent clean
		 * unmount of the latter.
		 */
		if (mp == rootdevmp)
			continue;

		unmount_or_warn(mp);
	}

	if (rootdevmp != NULL)
		unmount_or_warn(rootdevmp);
}

/*
 * perform msync on all vnodes under a mount point
 * the mount point must be locked.
 */
void
vfs_msync(struct mount *mp, int flags)
{
	struct vnode *vp, *mvp;
	struct vm_object *obj;

	CTR2(KTR_VFS, "%s: mp %p", __func__, mp);

	vnlru_return_batch(mp);

	MNT_VNODE_FOREACH_ACTIVE(vp, mp, mvp) {
		obj = vp->v_object;
		if (obj != NULL && (obj->flags & OBJ_MIGHTBEDIRTY) != 0 &&
		    (flags == MNT_WAIT || VOP_ISLOCKED(vp) == 0)) {
			if (!vget(vp,
			    LK_EXCLUSIVE | LK_RETRY | LK_INTERLOCK,
			    curthread)) {
				if (vp->v_vflag & VV_NOSYNC) {	/* unlinked */
					vput(vp);
					continue;
				}

				obj = vp->v_object;
				if (obj != NULL) {
					VM_OBJECT_WLOCK(obj);
					vm_object_page_clean(obj, 0, 0,
					    flags == MNT_WAIT ?
					    OBJPC_SYNC : OBJPC_NOSYNC);
					VM_OBJECT_WUNLOCK(obj);
				}
				vput(vp);
			}
		} else
			VI_UNLOCK(vp);
	}
}

static void
destroy_vpollinfo_free(struct vpollinfo *vi)
{

	knlist_destroy(&vi->vpi_selinfo.si_note);
	mtx_destroy(&vi->vpi_lock);
	uma_zfree(vnodepoll_zone, vi);
}

static void
destroy_vpollinfo(struct vpollinfo *vi)
{

	knlist_clear(&vi->vpi_selinfo.si_note, 1);
	seldrain(&vi->vpi_selinfo);
	destroy_vpollinfo_free(vi);
}

/*
 * Initialize per-vnode helper structure to hold poll-related state.
 */
void
v_addpollinfo(struct vnode *vp)
{
	struct vpollinfo *vi;

	if (vp->v_pollinfo != NULL)
		return;
	vi = uma_zalloc(vnodepoll_zone, M_WAITOK | M_ZERO);
	mtx_init(&vi->vpi_lock, "vnode pollinfo", NULL, MTX_DEF);
	knlist_init(&vi->vpi_selinfo.si_note, vp, vfs_knllock,
	    vfs_knlunlock, vfs_knl_assert_locked, vfs_knl_assert_unlocked);
	VI_LOCK(vp);
	if (vp->v_pollinfo != NULL) {
		VI_UNLOCK(vp);
		destroy_vpollinfo_free(vi);
		return;
	}
	vp->v_pollinfo = vi;
	VI_UNLOCK(vp);
}

/*
 * Record a process's interest in events which might happen to
 * a vnode.  Because poll uses the historic select-style interface
 * internally, this routine serves as both the ``check for any
 * pending events'' and the ``record my interest in future events''
 * functions.  (These are done together, while the lock is held,
 * to avoid race conditions.)
 */
int
vn_pollrecord(struct vnode *vp, struct thread *td, int events)
{

	v_addpollinfo(vp);
	mtx_lock(&vp->v_pollinfo->vpi_lock);
	if (vp->v_pollinfo->vpi_revents & events) {
		/*
		 * This leaves events we are not interested
		 * in available for the other process which
		 * which presumably had requested them
		 * (otherwise they would never have been
		 * recorded).
		 */
		events &= vp->v_pollinfo->vpi_revents;
		vp->v_pollinfo->vpi_revents &= ~events;

		mtx_unlock(&vp->v_pollinfo->vpi_lock);
		return (events);
	}
	vp->v_pollinfo->vpi_events |= events;
	selrecord(td, &vp->v_pollinfo->vpi_selinfo);
	mtx_unlock(&vp->v_pollinfo->vpi_lock);
	return (0);
}

/*
 * Routine to create and manage a filesystem syncer vnode.
 */
#define sync_close ((int (*)(struct  vop_close_args *))nullop)
static int	sync_fsync(struct  vop_fsync_args *);
static int	sync_inactive(struct  vop_inactive_args *);
static int	sync_reclaim(struct  vop_reclaim_args *);

static struct vop_vector sync_vnodeops = {
	.vop_bypass =	VOP_EOPNOTSUPP,
	.vop_close =	sync_close,		/* close */
	.vop_fsync =	sync_fsync,		/* fsync */
	.vop_inactive =	sync_inactive,	/* inactive */
	.vop_reclaim =	sync_reclaim,	/* reclaim */
	.vop_lock1 =	vop_stdlock,	/* lock */
	.vop_unlock =	vop_stdunlock,	/* unlock */
	.vop_islocked =	vop_stdislocked,	/* islocked */
};

/*
 * Create a new filesystem syncer vnode for the specified mount point.
 */
void
vfs_allocate_syncvnode(struct mount *mp)
{
	struct vnode *vp;
	struct bufobj *bo;
	static long start, incr, next;
	int error;

	/* Allocate a new vnode */
	error = getnewvnode("syncer", mp, &sync_vnodeops, &vp);
	if (error != 0)
		panic("vfs_allocate_syncvnode: getnewvnode() failed");
	vp->v_type = VNON;
	vn_lock(vp, LK_EXCLUSIVE | LK_RETRY);
	vp->v_vflag |= VV_FORCEINSMQ;
	error = insmntque(vp, mp);
	if (error != 0)
		panic("vfs_allocate_syncvnode: insmntque() failed");
	vp->v_vflag &= ~VV_FORCEINSMQ;
	VOP_UNLOCK(vp, 0);
	/*
	 * Place the vnode onto the syncer worklist. We attempt to
	 * scatter them about on the list so that they will go off
	 * at evenly distributed times even if all the filesystems
	 * are mounted at once.
	 */
	next += incr;
	if (next == 0 || next > syncer_maxdelay) {
		start /= 2;
		incr /= 2;
		if (start == 0) {
			start = syncer_maxdelay / 2;
			incr = syncer_maxdelay;
		}
		next = start;
	}
	bo = &vp->v_bufobj;
	BO_LOCK(bo);
	vn_syncer_add_to_worklist(bo, syncdelay > 0 ? next % syncdelay : 0);
	/* XXX - vn_syncer_add_to_worklist() also grabs and drops sync_mtx. */
	mtx_lock(&sync_mtx);
	sync_vnode_count++;
	if (mp->mnt_syncer == NULL) {
		mp->mnt_syncer = vp;
		vp = NULL;
	}
	mtx_unlock(&sync_mtx);
	BO_UNLOCK(bo);
	if (vp != NULL) {
		vn_lock(vp, LK_EXCLUSIVE | LK_RETRY);
		vgone(vp);
		vput(vp);
	}
}

void
vfs_deallocate_syncvnode(struct mount *mp)
{
	struct vnode *vp;

	mtx_lock(&sync_mtx);
	vp = mp->mnt_syncer;
	if (vp != NULL)
		mp->mnt_syncer = NULL;
	mtx_unlock(&sync_mtx);
	if (vp != NULL)
		vrele(vp);
}

/*
 * Do a lazy sync of the filesystem.
 */
static int
sync_fsync(struct vop_fsync_args *ap)
{
	struct vnode *syncvp = ap->a_vp;
	struct mount *mp = syncvp->v_mount;
	int error, save;
	struct bufobj *bo;

	/*
	 * We only need to do something if this is a lazy evaluation.
	 */
	if (ap->a_waitfor != MNT_LAZY)
		return (0);

	/*
	 * Move ourselves to the back of the sync list.
	 */
	bo = &syncvp->v_bufobj;
	BO_LOCK(bo);
	vn_syncer_add_to_worklist(bo, syncdelay);
	BO_UNLOCK(bo);

	/*
	 * Walk the list of vnodes pushing all that are dirty and
	 * not already on the sync list.
	 */
	if (vfs_busy(mp, MBF_NOWAIT) != 0)
		return (0);
	if (vn_start_write(NULL, &mp, V_NOWAIT) != 0) {
		vfs_unbusy(mp);
		return (0);
	}
	save = curthread_pflags_set(TDP_SYNCIO);
	vfs_msync(mp, MNT_NOWAIT);
	error = VFS_SYNC(mp, MNT_LAZY);
	curthread_pflags_restore(save);
	vn_finished_write(mp);
	vfs_unbusy(mp);
	return (error);
}

/*
 * The syncer vnode is no referenced.
 */
static int
sync_inactive(struct vop_inactive_args *ap)
{

	vgone(ap->a_vp);
	return (0);
}

/*
 * The syncer vnode is no longer needed and is being decommissioned.
 *
 * Modifications to the worklist must be protected by sync_mtx.
 */
static int
sync_reclaim(struct vop_reclaim_args *ap)
{
	struct vnode *vp = ap->a_vp;
	struct bufobj *bo;

	bo = &vp->v_bufobj;
	BO_LOCK(bo);
	mtx_lock(&sync_mtx);
	if (vp->v_mount->mnt_syncer == vp)
		vp->v_mount->mnt_syncer = NULL;
	if (bo->bo_flag & BO_ONWORKLST) {
		LIST_REMOVE(bo, bo_synclist);
		syncer_worklist_len--;
		sync_vnode_count--;
		bo->bo_flag &= ~BO_ONWORKLST;
	}
	mtx_unlock(&sync_mtx);
	BO_UNLOCK(bo);

	return (0);
}

/*
 * Check if vnode represents a disk device
 */
int
vn_isdisk(struct vnode *vp, int *errp)
{
	int error;

	if (vp->v_type != VCHR) {
		error = ENOTBLK;
		goto out;
	}
	error = 0;
	dev_lock();
	if (vp->v_rdev == NULL)
		error = ENXIO;
	else if (vp->v_rdev->si_devsw == NULL)
		error = ENXIO;
	else if (!(vp->v_rdev->si_devsw->d_flags & D_DISK))
		error = ENOTBLK;
	dev_unlock();
out:
	if (errp != NULL)
		*errp = error;
	return (error == 0);
}

/*
 * Common filesystem object access control check routine.  Accepts a
 * vnode's type, "mode", uid and gid, requested access mode, credentials,
 * and optional call-by-reference privused argument allowing vaccess()
 * to indicate to the caller whether privilege was used to satisfy the
 * request (obsoleted).  Returns 0 on success, or an errno on failure.
 */
int
vaccess(enum vtype type, mode_t file_mode, uid_t file_uid, gid_t file_gid,
    accmode_t accmode, struct ucred *cred, int *privused)
{
	accmode_t dac_granted;
	accmode_t priv_granted;

	KASSERT((accmode & ~(VEXEC | VWRITE | VREAD | VADMIN | VAPPEND)) == 0,
	    ("invalid bit in accmode"));
	KASSERT((accmode & VAPPEND) == 0 || (accmode & VWRITE),
	    ("VAPPEND without VWRITE"));

	/*
	 * Look for a normal, non-privileged way to access the file/directory
	 * as requested.  If it exists, go with that.
	 */

	if (privused != NULL)
		*privused = 0;

	dac_granted = 0;

	/* Check the owner. */
	if (cred->cr_uid == file_uid) {
		dac_granted |= VADMIN;
		if (file_mode & S_IXUSR)
			dac_granted |= VEXEC;
		if (file_mode & S_IRUSR)
			dac_granted |= VREAD;
		if (file_mode & S_IWUSR)
			dac_granted |= (VWRITE | VAPPEND);

		if ((accmode & dac_granted) == accmode)
			return (0);

		goto privcheck;
	}

	/* Otherwise, check the groups (first match) */
	if (groupmember(file_gid, cred)) {
		if (file_mode & S_IXGRP)
			dac_granted |= VEXEC;
		if (file_mode & S_IRGRP)
			dac_granted |= VREAD;
		if (file_mode & S_IWGRP)
			dac_granted |= (VWRITE | VAPPEND);

		if ((accmode & dac_granted) == accmode)
			return (0);

		goto privcheck;
	}

	/* Otherwise, check everyone else. */
	if (file_mode & S_IXOTH)
		dac_granted |= VEXEC;
	if (file_mode & S_IROTH)
		dac_granted |= VREAD;
	if (file_mode & S_IWOTH)
		dac_granted |= (VWRITE | VAPPEND);
	if ((accmode & dac_granted) == accmode)
		return (0);

privcheck:
	/*
	 * Build a privilege mask to determine if the set of privileges
	 * satisfies the requirements when combined with the granted mask
	 * from above.  For each privilege, if the privilege is required,
	 * bitwise or the request type onto the priv_granted mask.
	 */
	priv_granted = 0;

	if (type == VDIR) {
		/*
		 * For directories, use PRIV_VFS_LOOKUP to satisfy VEXEC
		 * requests, instead of PRIV_VFS_EXEC.
		 */
		if ((accmode & VEXEC) && ((dac_granted & VEXEC) == 0) &&
		    !priv_check_cred(cred, PRIV_VFS_LOOKUP))
			priv_granted |= VEXEC;
	} else {
		/*
		 * Ensure that at least one execute bit is on. Otherwise,
		 * a privileged user will always succeed, and we don't want
		 * this to happen unless the file really is executable.
		 */
		if ((accmode & VEXEC) && ((dac_granted & VEXEC) == 0) &&
		    (file_mode & (S_IXUSR | S_IXGRP | S_IXOTH)) != 0 &&
		    !priv_check_cred(cred, PRIV_VFS_EXEC))
			priv_granted |= VEXEC;
	}

	if ((accmode & VREAD) && ((dac_granted & VREAD) == 0) &&
	    !priv_check_cred(cred, PRIV_VFS_READ))
		priv_granted |= VREAD;

	if ((accmode & VWRITE) && ((dac_granted & VWRITE) == 0) &&
	    !priv_check_cred(cred, PRIV_VFS_WRITE))
		priv_granted |= (VWRITE | VAPPEND);

	if ((accmode & VADMIN) && ((dac_granted & VADMIN) == 0) &&
	    !priv_check_cred(cred, PRIV_VFS_ADMIN))
		priv_granted |= VADMIN;

	if ((accmode & (priv_granted | dac_granted)) == accmode) {
		/* XXX audit: privilege used */
		if (privused != NULL)
			*privused = 1;
		return (0);
	}

	return ((accmode & VADMIN) ? EPERM : EACCES);
}

/*
 * Credential check based on process requesting service, and per-attribute
 * permissions.
 */
int
extattr_check_cred(struct vnode *vp, int attrnamespace, struct ucred *cred,
    struct thread *td, accmode_t accmode)
{

	/*
	 * Kernel-invoked always succeeds.
	 */
	if (cred == NOCRED)
		return (0);

	/*
	 * Do not allow privileged processes in jail to directly manipulate
	 * system attributes.
	 */
	switch (attrnamespace) {
	case EXTATTR_NAMESPACE_SYSTEM:
		/* Potentially should be: return (EPERM); */
		return (priv_check_cred(cred, PRIV_VFS_EXTATTR_SYSTEM));
	case EXTATTR_NAMESPACE_USER:
		return (VOP_ACCESS(vp, accmode, cred, td));
	default:
		return (EPERM);
	}
}

#ifdef DEBUG_VFS_LOCKS
/*
 * This only exists to suppress warnings from unlocked specfs accesses.  It is
 * no longer ok to have an unlocked VFS.
 */
#define	IGNORE_LOCK(vp) (panicstr != NULL || (vp) == NULL ||		\
	(vp)->v_type == VCHR ||	(vp)->v_type == VBAD)

int vfs_badlock_ddb = 1;	/* Drop into debugger on violation. */
SYSCTL_INT(_debug, OID_AUTO, vfs_badlock_ddb, CTLFLAG_RW, &vfs_badlock_ddb, 0,
    "Drop into debugger on lock violation");

int vfs_badlock_mutex = 1;	/* Check for interlock across VOPs. */
SYSCTL_INT(_debug, OID_AUTO, vfs_badlock_mutex, CTLFLAG_RW, &vfs_badlock_mutex,
    0, "Check for interlock across VOPs");

int vfs_badlock_print = 1;	/* Print lock violations. */
SYSCTL_INT(_debug, OID_AUTO, vfs_badlock_print, CTLFLAG_RW, &vfs_badlock_print,
    0, "Print lock violations");

int vfs_badlock_vnode = 1;	/* Print vnode details on lock violations. */
SYSCTL_INT(_debug, OID_AUTO, vfs_badlock_vnode, CTLFLAG_RW, &vfs_badlock_vnode,
    0, "Print vnode details on lock violations");

#ifdef KDB
int vfs_badlock_backtrace = 1;	/* Print backtrace at lock violations. */
SYSCTL_INT(_debug, OID_AUTO, vfs_badlock_backtrace, CTLFLAG_RW,
    &vfs_badlock_backtrace, 0, "Print backtrace at lock violations");
#endif

static void
vfs_badlock(const char *msg, const char *str, struct vnode *vp)
{

#ifdef KDB
	if (vfs_badlock_backtrace)
		kdb_backtrace();
#endif
	if (vfs_badlock_vnode)
		vn_printf(vp, "vnode ");
	if (vfs_badlock_print)
		printf("%s: %p %s\n", str, (void *)vp, msg);
	if (vfs_badlock_ddb)
		kdb_enter(KDB_WHY_VFSLOCK, "lock violation");
}

void
assert_vi_locked(struct vnode *vp, const char *str)
{

	if (vfs_badlock_mutex && !mtx_owned(VI_MTX(vp)))
		vfs_badlock("interlock is not locked but should be", str, vp);
}

void
assert_vi_unlocked(struct vnode *vp, const char *str)
{

	if (vfs_badlock_mutex && mtx_owned(VI_MTX(vp)))
		vfs_badlock("interlock is locked but should not be", str, vp);
}

void
assert_vop_locked(struct vnode *vp, const char *str)
{
	int locked;

	if (!IGNORE_LOCK(vp)) {
		locked = VOP_ISLOCKED(vp);
		if (locked == 0 || locked == LK_EXCLOTHER)
			vfs_badlock("is not locked but should be", str, vp);
	}
}

void
assert_vop_unlocked(struct vnode *vp, const char *str)
{

	if (!IGNORE_LOCK(vp) && VOP_ISLOCKED(vp) == LK_EXCLUSIVE)
		vfs_badlock("is locked but should not be", str, vp);
}

void
assert_vop_elocked(struct vnode *vp, const char *str)
{

	if (!IGNORE_LOCK(vp) && VOP_ISLOCKED(vp) != LK_EXCLUSIVE)
		vfs_badlock("is not exclusive locked but should be", str, vp);
}
#endif /* DEBUG_VFS_LOCKS */

void
vop_rename_fail(struct vop_rename_args *ap)
{

	if (ap->a_tvp != NULL)
		vput(ap->a_tvp);
	if (ap->a_tdvp == ap->a_tvp)
		vrele(ap->a_tdvp);
	else
		vput(ap->a_tdvp);
	vrele(ap->a_fdvp);
	vrele(ap->a_fvp);
}

void
vop_rename_pre(void *ap)
{
	struct vop_rename_args *a = ap;

#ifdef DEBUG_VFS_LOCKS
	if (a->a_tvp)
		ASSERT_VI_UNLOCKED(a->a_tvp, "VOP_RENAME");
	ASSERT_VI_UNLOCKED(a->a_tdvp, "VOP_RENAME");
	ASSERT_VI_UNLOCKED(a->a_fvp, "VOP_RENAME");
	ASSERT_VI_UNLOCKED(a->a_fdvp, "VOP_RENAME");

	/* Check the source (from). */
	if (a->a_tdvp->v_vnlock != a->a_fdvp->v_vnlock &&
	    (a->a_tvp == NULL || a->a_tvp->v_vnlock != a->a_fdvp->v_vnlock))
		ASSERT_VOP_UNLOCKED(a->a_fdvp, "vop_rename: fdvp locked");
	if (a->a_tvp == NULL || a->a_tvp->v_vnlock != a->a_fvp->v_vnlock)
		ASSERT_VOP_UNLOCKED(a->a_fvp, "vop_rename: fvp locked");

	/* Check the target. */
	if (a->a_tvp)
		ASSERT_VOP_LOCKED(a->a_tvp, "vop_rename: tvp not locked");
	ASSERT_VOP_LOCKED(a->a_tdvp, "vop_rename: tdvp not locked");
#endif
	if (a->a_tdvp != a->a_fdvp)
		vhold(a->a_fdvp);
	if (a->a_tvp != a->a_fvp)
		vhold(a->a_fvp);
	vhold(a->a_tdvp);
	if (a->a_tvp)
		vhold(a->a_tvp);
}

#ifdef DEBUG_VFS_LOCKS
void
vop_strategy_pre(void *ap)
{
	struct vop_strategy_args *a;
	struct buf *bp;

	a = ap;
	bp = a->a_bp;

	/*
	 * Cluster ops lock their component buffers but not the IO container.
	 */
	if ((bp->b_flags & B_CLUSTER) != 0)
		return;

	if (panicstr == NULL && !BUF_ISLOCKED(bp)) {
		if (vfs_badlock_print)
			printf(
			    "VOP_STRATEGY: bp is not locked but should be\n");
		if (vfs_badlock_ddb)
			kdb_enter(KDB_WHY_VFSLOCK, "lock violation");
	}
}

void
vop_lock_pre(void *ap)
{
	struct vop_lock1_args *a = ap;

	if ((a->a_flags & LK_INTERLOCK) == 0)
		ASSERT_VI_UNLOCKED(a->a_vp, "VOP_LOCK");
	else
		ASSERT_VI_LOCKED(a->a_vp, "VOP_LOCK");
}

void
vop_lock_post(void *ap, int rc)
{
	struct vop_lock1_args *a = ap;

	ASSERT_VI_UNLOCKED(a->a_vp, "VOP_LOCK");
	if (rc == 0 && (a->a_flags & LK_EXCLOTHER) == 0)
		ASSERT_VOP_LOCKED(a->a_vp, "VOP_LOCK");
}

void
vop_unlock_pre(void *ap)
{
	struct vop_unlock_args *a = ap;

	if (a->a_flags & LK_INTERLOCK)
		ASSERT_VI_LOCKED(a->a_vp, "VOP_UNLOCK");
	ASSERT_VOP_LOCKED(a->a_vp, "VOP_UNLOCK");
}

void
vop_unlock_post(void *ap, int rc)
{
	struct vop_unlock_args *a = ap;

	if (a->a_flags & LK_INTERLOCK)
		ASSERT_VI_UNLOCKED(a->a_vp, "VOP_UNLOCK");
}
#endif

void
vop_create_post(void *ap, int rc)
{
	struct vop_create_args *a = ap;

	if (!rc)
		VFS_KNOTE_LOCKED(a->a_dvp, NOTE_WRITE);
}

void
vop_deleteextattr_post(void *ap, int rc)
{
	struct vop_deleteextattr_args *a = ap;

	if (!rc)
		VFS_KNOTE_LOCKED(a->a_vp, NOTE_ATTRIB);
}

void
vop_link_post(void *ap, int rc)
{
	struct vop_link_args *a = ap;

	if (!rc) {
		VFS_KNOTE_LOCKED(a->a_vp, NOTE_LINK);
		VFS_KNOTE_LOCKED(a->a_tdvp, NOTE_WRITE);
	}
}

void
vop_mkdir_post(void *ap, int rc)
{
	struct vop_mkdir_args *a = ap;

	if (!rc)
		VFS_KNOTE_LOCKED(a->a_dvp, NOTE_WRITE | NOTE_LINK);
}

void
vop_mknod_post(void *ap, int rc)
{
	struct vop_mknod_args *a = ap;

	if (!rc)
		VFS_KNOTE_LOCKED(a->a_dvp, NOTE_WRITE);
}

void
vop_reclaim_post(void *ap, int rc)
{
	struct vop_reclaim_args *a = ap;

	if (!rc)
		VFS_KNOTE_LOCKED(a->a_vp, NOTE_REVOKE);
}

void
vop_remove_post(void *ap, int rc)
{
	struct vop_remove_args *a = ap;

	if (!rc) {
		VFS_KNOTE_LOCKED(a->a_dvp, NOTE_WRITE);
		VFS_KNOTE_LOCKED(a->a_vp, NOTE_DELETE);
	}
}

void
vop_rename_post(void *ap, int rc)
{
	struct vop_rename_args *a = ap;
	long hint;

	if (!rc) {
		hint = NOTE_WRITE;
		if (a->a_fdvp == a->a_tdvp) {
			if (a->a_tvp != NULL && a->a_tvp->v_type == VDIR)
				hint |= NOTE_LINK;
			VFS_KNOTE_UNLOCKED(a->a_fdvp, hint);
			VFS_KNOTE_UNLOCKED(a->a_tdvp, hint);
		} else {
			hint |= NOTE_EXTEND;
			if (a->a_fvp->v_type == VDIR)
				hint |= NOTE_LINK;
			VFS_KNOTE_UNLOCKED(a->a_fdvp, hint);

			if (a->a_fvp->v_type == VDIR && a->a_tvp != NULL &&
			    a->a_tvp->v_type == VDIR)
				hint &= ~NOTE_LINK;
			VFS_KNOTE_UNLOCKED(a->a_tdvp, hint);
		}

		VFS_KNOTE_UNLOCKED(a->a_fvp, NOTE_RENAME);
		if (a->a_tvp)
			VFS_KNOTE_UNLOCKED(a->a_tvp, NOTE_DELETE);
	}
	if (a->a_tdvp != a->a_fdvp)
		vdrop(a->a_fdvp);
	if (a->a_tvp != a->a_fvp)
		vdrop(a->a_fvp);
	vdrop(a->a_tdvp);
	if (a->a_tvp)
		vdrop(a->a_tvp);
}

void
vop_rmdir_post(void *ap, int rc)
{
	struct vop_rmdir_args *a = ap;

	if (!rc) {
		VFS_KNOTE_LOCKED(a->a_dvp, NOTE_WRITE | NOTE_LINK);
		VFS_KNOTE_LOCKED(a->a_vp, NOTE_DELETE);
	}
}

void
vop_setattr_post(void *ap, int rc)
{
	struct vop_setattr_args *a = ap;

	if (!rc)
		VFS_KNOTE_LOCKED(a->a_vp, NOTE_ATTRIB);
}

void
vop_setextattr_post(void *ap, int rc)
{
	struct vop_setextattr_args *a = ap;

	if (!rc)
		VFS_KNOTE_LOCKED(a->a_vp, NOTE_ATTRIB);
}

void
vop_symlink_post(void *ap, int rc)
{
	struct vop_symlink_args *a = ap;

	if (!rc)
		VFS_KNOTE_LOCKED(a->a_dvp, NOTE_WRITE);
}

void
vop_open_post(void *ap, int rc)
{
	struct vop_open_args *a = ap;

	if (!rc)
		VFS_KNOTE_LOCKED(a->a_vp, NOTE_OPEN);
}

void
vop_close_post(void *ap, int rc)
{
	struct vop_close_args *a = ap;

	if (!rc && (a->a_cred != NOCRED || /* filter out revokes */
	    (a->a_vp->v_iflag & VI_DOOMED) == 0)) {
		VFS_KNOTE_LOCKED(a->a_vp, (a->a_fflag & FWRITE) != 0 ?
		    NOTE_CLOSE_WRITE : NOTE_CLOSE);
	}
}

void
vop_read_post(void *ap, int rc)
{
	struct vop_read_args *a = ap;

	if (!rc)
		VFS_KNOTE_LOCKED(a->a_vp, NOTE_READ);
}

void
vop_readdir_post(void *ap, int rc)
{
	struct vop_readdir_args *a = ap;

	if (!rc)
		VFS_KNOTE_LOCKED(a->a_vp, NOTE_READ);
}

static struct knlist fs_knlist;

static void
vfs_event_init(void *arg)
{
	knlist_init_mtx(&fs_knlist, NULL);
}
/* XXX - correct order? */
SYSINIT(vfs_knlist, SI_SUB_VFS, SI_ORDER_ANY, vfs_event_init, NULL);

void
vfs_event_signal(fsid_t *fsid, uint32_t event, intptr_t data __unused)
{

	KNOTE_UNLOCKED(&fs_knlist, event);
}

static int	filt_fsattach(struct knote *kn);
static void	filt_fsdetach(struct knote *kn);
static int	filt_fsevent(struct knote *kn, long hint);

struct filterops fs_filtops = {
	.f_isfd = 0,
	.f_attach = filt_fsattach,
	.f_detach = filt_fsdetach,
	.f_event = filt_fsevent
};

static int
filt_fsattach(struct knote *kn)
{

	kn->kn_flags |= EV_CLEAR;
	knlist_add(&fs_knlist, kn, 0);
	return (0);
}

static void
filt_fsdetach(struct knote *kn)
{

	knlist_remove(&fs_knlist, kn, 0);
}

static int
filt_fsevent(struct knote *kn, long hint)
{

	kn->kn_fflags |= hint;
	return (kn->kn_fflags != 0);
}

static int
sysctl_vfs_ctl(SYSCTL_HANDLER_ARGS)
{
	struct vfsidctl vc;
	int error;
	struct mount *mp;

	error = SYSCTL_IN(req, &vc, sizeof(vc));
	if (error)
		return (error);
	if (vc.vc_vers != VFS_CTL_VERS1)
		return (EINVAL);
	mp = vfs_getvfs(&vc.vc_fsid);
	if (mp == NULL)
		return (ENOENT);
	/* ensure that a specific sysctl goes to the right filesystem. */
	if (strcmp(vc.vc_fstypename, "*") != 0 &&
	    strcmp(vc.vc_fstypename, mp->mnt_vfc->vfc_name) != 0) {
		vfs_rel(mp);
		return (EINVAL);
	}
	VCTLTOREQ(&vc, req);
	error = VFS_SYSCTL(mp, vc.vc_op, req);
	vfs_rel(mp);
	return (error);
}

SYSCTL_PROC(_vfs, OID_AUTO, ctl, CTLTYPE_OPAQUE | CTLFLAG_WR,
    NULL, 0, sysctl_vfs_ctl, "",
    "Sysctl by fsid");

/*
 * Function to initialize a va_filerev field sensibly.
 * XXX: Wouldn't a random number make a lot more sense ??
 */
u_quad_t
init_va_filerev(void)
{
	struct bintime bt;

	getbinuptime(&bt);
	return (((u_quad_t)bt.sec << 32LL) | (bt.frac >> 32LL));
}

static int	filt_vfsread(struct knote *kn, long hint);
static int	filt_vfswrite(struct knote *kn, long hint);
static int	filt_vfsvnode(struct knote *kn, long hint);
static void	filt_vfsdetach(struct knote *kn);
static struct filterops vfsread_filtops = {
	.f_isfd = 1,
	.f_detach = filt_vfsdetach,
	.f_event = filt_vfsread
};
static struct filterops vfswrite_filtops = {
	.f_isfd = 1,
	.f_detach = filt_vfsdetach,
	.f_event = filt_vfswrite
};
static struct filterops vfsvnode_filtops = {
	.f_isfd = 1,
	.f_detach = filt_vfsdetach,
	.f_event = filt_vfsvnode
};

static void
vfs_knllock(void *arg)
{
	struct vnode *vp = arg;

	vn_lock(vp, LK_EXCLUSIVE | LK_RETRY);
}

static void
vfs_knlunlock(void *arg)
{
	struct vnode *vp = arg;

	VOP_UNLOCK(vp, 0);
}

static void
vfs_knl_assert_locked(void *arg)
{
#ifdef DEBUG_VFS_LOCKS
	struct vnode *vp = arg;

	ASSERT_VOP_LOCKED(vp, "vfs_knl_assert_locked");
#endif
}

static void
vfs_knl_assert_unlocked(void *arg)
{
#ifdef DEBUG_VFS_LOCKS
	struct vnode *vp = arg;

	ASSERT_VOP_UNLOCKED(vp, "vfs_knl_assert_unlocked");
#endif
}

int
vfs_kqfilter(struct vop_kqfilter_args *ap)
{
	struct vnode *vp = ap->a_vp;
	struct knote *kn = ap->a_kn;
	struct knlist *knl;

	switch (kn->kn_filter) {
	case EVFILT_READ:
		kn->kn_fop = &vfsread_filtops;
		break;
	case EVFILT_WRITE:
		kn->kn_fop = &vfswrite_filtops;
		break;
	case EVFILT_VNODE:
		kn->kn_fop = &vfsvnode_filtops;
		break;
	default:
		return (EINVAL);
	}

	kn->kn_hook = (caddr_t)vp;

	v_addpollinfo(vp);
	if (vp->v_pollinfo == NULL)
		return (ENOMEM);
	knl = &vp->v_pollinfo->vpi_selinfo.si_note;
	vhold(vp);
	knlist_add(knl, kn, 0);

	return (0);
}

/*
 * Detach knote from vnode
 */
static void
filt_vfsdetach(struct knote *kn)
{
	struct vnode *vp = (struct vnode *)kn->kn_hook;

	KASSERT(vp->v_pollinfo != NULL, ("Missing v_pollinfo"));
	knlist_remove(&vp->v_pollinfo->vpi_selinfo.si_note, kn, 0);
	vdrop(vp);
}

/*ARGSUSED*/
static int
filt_vfsread(struct knote *kn, long hint)
{
	struct vnode *vp = (struct vnode *)kn->kn_hook;
	struct vattr va;
	int res;

	/*
	 * filesystem is gone, so set the EOF flag and schedule
	 * the knote for deletion.
	 */
	if (hint == NOTE_REVOKE || (hint == 0 && vp->v_type == VBAD)) {
		VI_LOCK(vp);
		kn->kn_flags |= (EV_EOF | EV_ONESHOT);
		VI_UNLOCK(vp);
		return (1);
	}

	if (VOP_GETATTR(vp, &va, curthread->td_ucred))
		return (0);

	VI_LOCK(vp);
	kn->kn_data = va.va_size - kn->kn_fp->f_offset;
	res = (kn->kn_sfflags & NOTE_FILE_POLL) != 0 || kn->kn_data != 0;
	VI_UNLOCK(vp);
	return (res);
}

/*ARGSUSED*/
static int
filt_vfswrite(struct knote *kn, long hint)
{
	struct vnode *vp = (struct vnode *)kn->kn_hook;

	VI_LOCK(vp);

	/*
	 * filesystem is gone, so set the EOF flag and schedule
	 * the knote for deletion.
	 */
	if (hint == NOTE_REVOKE || (hint == 0 && vp->v_type == VBAD))
		kn->kn_flags |= (EV_EOF | EV_ONESHOT);

	kn->kn_data = 0;
	VI_UNLOCK(vp);
	return (1);
}

static int
filt_vfsvnode(struct knote *kn, long hint)
{
	struct vnode *vp = (struct vnode *)kn->kn_hook;
	int res;

	VI_LOCK(vp);
	if (kn->kn_sfflags & hint)
		kn->kn_fflags |= hint;
	if (hint == NOTE_REVOKE || (hint == 0 && vp->v_type == VBAD)) {
		kn->kn_flags |= EV_EOF;
		VI_UNLOCK(vp);
		return (1);
	}
	res = (kn->kn_fflags != 0);
	VI_UNLOCK(vp);
	return (res);
}

int
vfs_read_dirent(struct vop_readdir_args *ap, struct dirent *dp, off_t off)
{
	int error;

	if (dp->d_reclen > ap->a_uio->uio_resid)
		return (ENAMETOOLONG);
	error = uiomove(dp, dp->d_reclen, ap->a_uio);
	if (error) {
		if (ap->a_ncookies != NULL) {
			if (ap->a_cookies != NULL)
				free(ap->a_cookies, M_TEMP);
			ap->a_cookies = NULL;
			*ap->a_ncookies = 0;
		}
		return (error);
	}
	if (ap->a_ncookies == NULL)
		return (0);

	KASSERT(ap->a_cookies,
	    ("NULL ap->a_cookies value with non-NULL ap->a_ncookies!"));

	*ap->a_cookies = realloc(*ap->a_cookies,
	    (*ap->a_ncookies + 1) * sizeof(u_long), M_TEMP, M_WAITOK | M_ZERO);
	(*ap->a_cookies)[*ap->a_ncookies] = off;
	*ap->a_ncookies += 1;
	return (0);
}

/*
 * Mark for update the access time of the file if the filesystem
 * supports VOP_MARKATIME.  This functionality is used by execve and
 * mmap, so we want to avoid the I/O implied by directly setting
 * va_atime for the sake of efficiency.
 */
void
vfs_mark_atime(struct vnode *vp, struct ucred *cred)
{
	struct mount *mp;

	mp = vp->v_mount;
	ASSERT_VOP_LOCKED(vp, "vfs_mark_atime");
	if (mp != NULL && (mp->mnt_flag & (MNT_NOATIME | MNT_RDONLY)) == 0)
		(void)VOP_MARKATIME(vp);
}

/*
 * The purpose of this routine is to remove granularity from accmode_t,
 * reducing it into standard unix access bits - VEXEC, VREAD, VWRITE,
 * VADMIN and VAPPEND.
 *
 * If it returns 0, the caller is supposed to continue with the usual
 * access checks using 'accmode' as modified by this routine.  If it
 * returns nonzero value, the caller is supposed to return that value
 * as errno.
 *
 * Note that after this routine runs, accmode may be zero.
 */
int
vfs_unixify_accmode(accmode_t *accmode)
{
	/*
	 * There is no way to specify explicit "deny" rule using
	 * file mode or POSIX.1e ACLs.
	 */
	if (*accmode & VEXPLICIT_DENY) {
		*accmode = 0;
		return (0);
	}

	/*
	 * None of these can be translated into usual access bits.
	 * Also, the common case for NFSv4 ACLs is to not contain
	 * either of these bits. Caller should check for VWRITE
	 * on the containing directory instead.
	 */
	if (*accmode & (VDELETE_CHILD | VDELETE))
		return (EPERM);

	if (*accmode & VADMIN_PERMS) {
		*accmode &= ~VADMIN_PERMS;
		*accmode |= VADMIN;
	}

	/*
	 * There is no way to deny VREAD_ATTRIBUTES, VREAD_ACL
	 * or VSYNCHRONIZE using file mode or POSIX.1e ACL.
	 */
	*accmode &= ~(VSTAT_PERMS | VSYNCHRONIZE);

	return (0);
}

/*
 * These are helper functions for filesystems to traverse all
 * their vnodes.  See MNT_VNODE_FOREACH_ALL() in sys/mount.h.
 *
 * This interface replaces MNT_VNODE_FOREACH.
 */

MALLOC_DEFINE(M_VNODE_MARKER, "vnodemarker", "vnode marker");

struct vnode *
__mnt_vnode_next_all(struct vnode **mvp, struct mount *mp)
{
	struct vnode *vp;

	if (should_yield())
		kern_yield(PRI_USER);
	MNT_ILOCK(mp);
	KASSERT((*mvp)->v_mount == mp, ("marker vnode mount list mismatch"));
	for (vp = TAILQ_NEXT(*mvp, v_nmntvnodes); vp != NULL;
	    vp = TAILQ_NEXT(vp, v_nmntvnodes)) {
		/* Allow a racy peek at VI_DOOMED to save a lock acquisition. */
		if (vp->v_type == VMARKER || (vp->v_iflag & VI_DOOMED) != 0)
			continue;
		VI_LOCK(vp);
		if ((vp->v_iflag & VI_DOOMED) != 0) {
			VI_UNLOCK(vp);
			continue;
		}
		break;
	}
	if (vp == NULL) {
		__mnt_vnode_markerfree_all(mvp, mp);
		/* MNT_IUNLOCK(mp); -- done in above function */
		mtx_assert(MNT_MTX(mp), MA_NOTOWNED);
		return (NULL);
	}
	TAILQ_REMOVE(&mp->mnt_nvnodelist, *mvp, v_nmntvnodes);
	TAILQ_INSERT_AFTER(&mp->mnt_nvnodelist, vp, *mvp, v_nmntvnodes);
	MNT_IUNLOCK(mp);
	return (vp);
}

struct vnode *
__mnt_vnode_first_all(struct vnode **mvp, struct mount *mp)
{
	struct vnode *vp;

	*mvp = malloc(sizeof(struct vnode), M_VNODE_MARKER, M_WAITOK | M_ZERO);
	MNT_ILOCK(mp);
	MNT_REF(mp);
	(*mvp)->v_mount = mp;
	(*mvp)->v_type = VMARKER;

	TAILQ_FOREACH(vp, &mp->mnt_nvnodelist, v_nmntvnodes) {
		/* Allow a racy peek at VI_DOOMED to save a lock acquisition. */
		if (vp->v_type == VMARKER || (vp->v_iflag & VI_DOOMED) != 0)
			continue;
		VI_LOCK(vp);
		if ((vp->v_iflag & VI_DOOMED) != 0) {
			VI_UNLOCK(vp);
			continue;
		}
		break;
	}
	if (vp == NULL) {
		MNT_REL(mp);
		MNT_IUNLOCK(mp);
		free(*mvp, M_VNODE_MARKER);
		*mvp = NULL;
		return (NULL);
	}
	TAILQ_INSERT_AFTER(&mp->mnt_nvnodelist, vp, *mvp, v_nmntvnodes);
	MNT_IUNLOCK(mp);
	return (vp);
}

void
__mnt_vnode_markerfree_all(struct vnode **mvp, struct mount *mp)
{

	if (*mvp == NULL) {
		MNT_IUNLOCK(mp);
		return;
	}

	mtx_assert(MNT_MTX(mp), MA_OWNED);

	KASSERT((*mvp)->v_mount == mp, ("marker vnode mount list mismatch"));
	TAILQ_REMOVE(&mp->mnt_nvnodelist, *mvp, v_nmntvnodes);
	MNT_REL(mp);
	MNT_IUNLOCK(mp);
	free(*mvp, M_VNODE_MARKER);
	*mvp = NULL;
}

/*
 * These are helper functions for filesystems to traverse their
 * active vnodes.  See MNT_VNODE_FOREACH_ACTIVE() in sys/mount.h
 */
static void
mnt_vnode_markerfree_active(struct vnode **mvp, struct mount *mp)
{

	KASSERT((*mvp)->v_mount == mp, ("marker vnode mount list mismatch"));

	MNT_ILOCK(mp);
	MNT_REL(mp);
	MNT_IUNLOCK(mp);
	free(*mvp, M_VNODE_MARKER);
	*mvp = NULL;
}

/*
 * Relock the mp mount vnode list lock with the vp vnode interlock in the
 * conventional lock order during mnt_vnode_next_active iteration.
 *
 * On entry, the mount vnode list lock is held and the vnode interlock is not.
 * The list lock is dropped and reacquired.  On success, both locks are held.
 * On failure, the mount vnode list lock is held but the vnode interlock is
 * not, and the procedure may have yielded.
 */
static bool
mnt_vnode_next_active_relock(struct vnode *mvp, struct mount *mp,
    struct vnode *vp)
{
	const struct vnode *tmp;
	bool held, ret;

	VNASSERT(mvp->v_mount == mp && mvp->v_type == VMARKER &&
	    TAILQ_NEXT(mvp, v_actfreelist) != NULL, mvp,
	    ("%s: bad marker", __func__));
	VNASSERT(vp->v_mount == mp && vp->v_type != VMARKER, vp,
	    ("%s: inappropriate vnode", __func__));
	ASSERT_VI_UNLOCKED(vp, __func__);
	mtx_assert(&mp->mnt_listmtx, MA_OWNED);

	ret = false;

	TAILQ_REMOVE(&mp->mnt_activevnodelist, mvp, v_actfreelist);
	TAILQ_INSERT_BEFORE(vp, mvp, v_actfreelist);

	/*
	 * Use a hold to prevent vp from disappearing while the mount vnode
	 * list lock is dropped and reacquired.  Normally a hold would be
	 * acquired with vhold(), but that might try to acquire the vnode
	 * interlock, which would be a LOR with the mount vnode list lock.
	 */
	held = refcount_acquire_if_not_zero(&vp->v_holdcnt);
	mtx_unlock(&mp->mnt_listmtx);
	if (!held)
		goto abort;
	VI_LOCK(vp);
	if (!refcount_release_if_not_last(&vp->v_holdcnt)) {
		vdropl(vp);
		goto abort;
	}
	mtx_lock(&mp->mnt_listmtx);

	/*
	 * Determine whether the vnode is still the next one after the marker,
	 * excepting any other markers.  If the vnode has not been doomed by
	 * vgone() then the hold should have ensured that it remained on the
	 * active list.  If it has been doomed but is still on the active list,
	 * don't abort, but rather skip over it (avoid spinning on doomed
	 * vnodes).
	 */
	tmp = mvp;
	do {
		tmp = TAILQ_NEXT(tmp, v_actfreelist);
	} while (tmp != NULL && tmp->v_type == VMARKER);
	if (tmp != vp) {
		mtx_unlock(&mp->mnt_listmtx);
		VI_UNLOCK(vp);
		goto abort;
	}

	ret = true;
	goto out;
abort:
	maybe_yield();
	mtx_lock(&mp->mnt_listmtx);
out:
	if (ret)
		ASSERT_VI_LOCKED(vp, __func__);
	else
		ASSERT_VI_UNLOCKED(vp, __func__);
	mtx_assert(&mp->mnt_listmtx, MA_OWNED);
	return (ret);
}

static struct vnode *
mnt_vnode_next_active(struct vnode **mvp, struct mount *mp)
{
	struct vnode *vp, *nvp;

	mtx_assert(&mp->mnt_listmtx, MA_OWNED);
	KASSERT((*mvp)->v_mount == mp, ("marker vnode mount list mismatch"));
restart:
	vp = TAILQ_NEXT(*mvp, v_actfreelist);
	while (vp != NULL) {
		if (vp->v_type == VMARKER) {
			vp = TAILQ_NEXT(vp, v_actfreelist);
			continue;
		}
		/*
		 * Try-lock because this is the wrong lock order.  If that does
		 * not succeed, drop the mount vnode list lock and try to
		 * reacquire it and the vnode interlock in the right order.
		 */
		if (!VI_TRYLOCK(vp) &&
		    !mnt_vnode_next_active_relock(*mvp, mp, vp))
			goto restart;
		KASSERT(vp->v_type != VMARKER, ("locked marker %p", vp));
		KASSERT(vp->v_mount == mp || vp->v_mount == NULL,
		    ("alien vnode on the active list %p %p", vp, mp));
		if (vp->v_mount == mp && (vp->v_iflag & VI_DOOMED) == 0)
			break;
		nvp = TAILQ_NEXT(vp, v_actfreelist);
		VI_UNLOCK(vp);
		vp = nvp;
	}
	TAILQ_REMOVE(&mp->mnt_activevnodelist, *mvp, v_actfreelist);

	/* Check if we are done */
	if (vp == NULL) {
		mtx_unlock(&mp->mnt_listmtx);
		mnt_vnode_markerfree_active(mvp, mp);
		return (NULL);
	}
	TAILQ_INSERT_AFTER(&mp->mnt_activevnodelist, vp, *mvp, v_actfreelist);
	mtx_unlock(&mp->mnt_listmtx);
	ASSERT_VI_LOCKED(vp, "active iter");
	KASSERT((vp->v_iflag & VI_ACTIVE) != 0, ("Non-active vp %p", vp));
	return (vp);
}

struct vnode *
__mnt_vnode_next_active(struct vnode **mvp, struct mount *mp)
{

	if (should_yield())
		kern_yield(PRI_USER);
	mtx_lock(&mp->mnt_listmtx);
	return (mnt_vnode_next_active(mvp, mp));
}

struct vnode *
__mnt_vnode_first_active(struct vnode **mvp, struct mount *mp)
{
	struct vnode *vp;

	*mvp = malloc(sizeof(struct vnode), M_VNODE_MARKER, M_WAITOK | M_ZERO);
	MNT_ILOCK(mp);
	MNT_REF(mp);
	MNT_IUNLOCK(mp);
	(*mvp)->v_type = VMARKER;
	(*mvp)->v_mount = mp;

	mtx_lock(&mp->mnt_listmtx);
	vp = TAILQ_FIRST(&mp->mnt_activevnodelist);
	if (vp == NULL) {
		mtx_unlock(&mp->mnt_listmtx);
		mnt_vnode_markerfree_active(mvp, mp);
		return (NULL);
	}
	TAILQ_INSERT_BEFORE(vp, *mvp, v_actfreelist);
	return (mnt_vnode_next_active(mvp, mp));
}

void
__mnt_vnode_markerfree_active(struct vnode **mvp, struct mount *mp)
{

	if (*mvp == NULL)
		return;

	mtx_lock(&mp->mnt_listmtx);
	TAILQ_REMOVE(&mp->mnt_activevnodelist, *mvp, v_actfreelist);
	mtx_unlock(&mp->mnt_listmtx);
	mnt_vnode_markerfree_active(mvp, mp);
}
