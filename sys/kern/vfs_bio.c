/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2004 Poul-Henning Kamp
 * Copyright (c) 1994,1997 John S. Dyson
 * Copyright (c) 2013 The FreeBSD Foundation
 * All rights reserved.
 *
 * Portions of this software were developed by Konstantin Belousov
 * under sponsorship from the FreeBSD Foundation.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

/*
 * this file contains a new buffer I/O scheme implementing a coherent
 * VM object and buffer cache scheme.  Pains have been taken to make
 * sure that the performance degradation associated with schemes such
 * as this is not realized.
 *
 * Author:  John S. Dyson
 * Significant help during the development and debugging phases
 * had been provided by David Greenman, also of the FreeBSD core team.
 *
 * see man buf(9) for more info.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/bio.h>
#include <sys/bitset.h>
#include <sys/conf.h>
#include <sys/counter.h>
#include <sys/buf.h>
#include <sys/devicestat.h>
#include <sys/eventhandler.h>
#include <sys/fail.h>
#include <sys/limits.h>
#include <sys/lock.h>
#include <sys/malloc.h>
#include <sys/mount.h>
#include <sys/mutex.h>
#include <sys/kernel.h>
#include <sys/kthread.h>
#include <sys/proc.h>
#include <sys/racct.h>
#include <sys/resourcevar.h>
#include <sys/rwlock.h>
#include <sys/smp.h>
#include <sys/sysctl.h>
#include <sys/sysproto.h>
#include <sys/vmem.h>
#include <sys/vmmeter.h>
#include <sys/vnode.h>
#include <sys/watchdog.h>
#include <geom/geom.h>
#include <vm/vm.h>
#include <vm/vm_param.h>
#include <vm/vm_kern.h>
#include <vm/vm_object.h>
#include <vm/vm_page.h>
#include <vm/vm_pageout.h>
#include <vm/vm_pager.h>
#include <vm/vm_extern.h>
#include <vm/vm_map.h>
#include <vm/swap_pager.h>

static MALLOC_DEFINE(M_BIOBUF, "biobuf", "BIO buffer");

struct	bio_ops bioops;		/* I/O operation notification */

struct	buf_ops buf_ops_bio = {
	.bop_name	=	"buf_ops_bio",
	.bop_write	=	bufwrite,
	.bop_strategy	=	bufstrategy,
	.bop_sync	=	bufsync,
	.bop_bdflush	=	bufbdflush,
};

struct bufqueue {
	struct mtx_padalign	bq_lock;
	TAILQ_HEAD(, buf)	bq_queue;
	uint8_t			bq_index;
	uint16_t		bq_subqueue;
	int			bq_len;
} __aligned(CACHE_LINE_SIZE);

#define	BQ_LOCKPTR(bq)		(&(bq)->bq_lock)
#define	BQ_LOCK(bq)		mtx_lock(BQ_LOCKPTR((bq)))
#define	BQ_UNLOCK(bq)		mtx_unlock(BQ_LOCKPTR((bq)))
#define	BQ_ASSERT_LOCKED(bq)	mtx_assert(BQ_LOCKPTR((bq)), MA_OWNED)

struct bufdomain {
	struct bufqueue	bd_subq[MAXCPU + 1]; /* Per-cpu sub queues + global */
	struct bufqueue bd_dirtyq;
	struct bufqueue	*bd_cleanq;
	struct mtx_padalign bd_run_lock;
	/* Constants */
	long		bd_maxbufspace;
	long		bd_hibufspace;
	long 		bd_lobufspace;
	long 		bd_bufspacethresh;
	int		bd_hifreebuffers;
	int		bd_lofreebuffers;
	int		bd_hidirtybuffers;
	int		bd_lodirtybuffers;
	int		bd_dirtybufthresh;
	int		bd_lim;
	/* atomics */
	int		bd_wanted;
	int __aligned(CACHE_LINE_SIZE)	bd_numdirtybuffers;
	int __aligned(CACHE_LINE_SIZE)	bd_running;
	long __aligned(CACHE_LINE_SIZE) bd_bufspace;
	int __aligned(CACHE_LINE_SIZE)	bd_freebuffers;
} __aligned(CACHE_LINE_SIZE);

#define	BD_LOCKPTR(bd)		(&(bd)->bd_cleanq->bq_lock)
#define	BD_LOCK(bd)		mtx_lock(BD_LOCKPTR((bd)))
#define	BD_UNLOCK(bd)		mtx_unlock(BD_LOCKPTR((bd)))
#define	BD_ASSERT_LOCKED(bd)	mtx_assert(BD_LOCKPTR((bd)), MA_OWNED)
#define	BD_RUN_LOCKPTR(bd)	(&(bd)->bd_run_lock)
#define	BD_RUN_LOCK(bd)		mtx_lock(BD_RUN_LOCKPTR((bd)))
#define	BD_RUN_UNLOCK(bd)	mtx_unlock(BD_RUN_LOCKPTR((bd)))
#define	BD_DOMAIN(bd)		(bd - bdomain)

static struct buf *buf;		/* buffer header pool */
extern struct buf *swbuf;	/* Swap buffer header pool. */
caddr_t unmapped_buf;

/* Used below and for softdep flushing threads in ufs/ffs/ffs_softdep.c */
struct proc *bufdaemonproc;

static int inmem(struct vnode *vp, daddr_t blkno);
static void vm_hold_free_pages(struct buf *bp, int newbsize);
static void vm_hold_load_pages(struct buf *bp, vm_offset_t from,
		vm_offset_t to);
static void vfs_page_set_valid(struct buf *bp, vm_ooffset_t off, vm_page_t m);
static void vfs_page_set_validclean(struct buf *bp, vm_ooffset_t off,
		vm_page_t m);
static void vfs_clean_pages_dirty_buf(struct buf *bp);
static void vfs_setdirty_locked_object(struct buf *bp);
static void vfs_vmio_invalidate(struct buf *bp);
static void vfs_vmio_truncate(struct buf *bp, int npages);
static void vfs_vmio_extend(struct buf *bp, int npages, int size);
static int vfs_bio_clcheck(struct vnode *vp, int size,
		daddr_t lblkno, daddr_t blkno);
static void breada(struct vnode *, daddr_t *, int *, int, struct ucred *, int,
		void (*)(struct buf *));
static int buf_flush(struct vnode *vp, struct bufdomain *, int);
static int flushbufqueues(struct vnode *, struct bufdomain *, int, int);
static void buf_daemon(void);
static __inline void bd_wakeup(void);
static int sysctl_runningspace(SYSCTL_HANDLER_ARGS);
static void bufkva_reclaim(vmem_t *, int);
static void bufkva_free(struct buf *);
static int buf_import(void *, void **, int, int, int);
static void buf_release(void *, void **, int);
static void maxbcachebuf_adjust(void);
static inline struct bufdomain *bufdomain(struct buf *);
static void bq_remove(struct bufqueue *bq, struct buf *bp);
static void bq_insert(struct bufqueue *bq, struct buf *bp, bool unlock);
static int buf_recycle(struct bufdomain *, bool kva);
static void bq_init(struct bufqueue *bq, int qindex, int cpu,
	    const char *lockname);
static void bd_init(struct bufdomain *bd);
static int bd_flushall(struct bufdomain *bd);
static int sysctl_bufdomain_long(SYSCTL_HANDLER_ARGS);
static int sysctl_bufdomain_int(SYSCTL_HANDLER_ARGS);

static int sysctl_bufspace(SYSCTL_HANDLER_ARGS);
int vmiodirenable = TRUE;
SYSCTL_INT(_vfs, OID_AUTO, vmiodirenable, CTLFLAG_RW, &vmiodirenable, 0,
    "Use the VM system for directory writes");
long runningbufspace;
SYSCTL_LONG(_vfs, OID_AUTO, runningbufspace, CTLFLAG_RD, &runningbufspace, 0,
    "Amount of presently outstanding async buffer io");
SYSCTL_PROC(_vfs, OID_AUTO, bufspace, CTLTYPE_LONG|CTLFLAG_MPSAFE|CTLFLAG_RD,
    NULL, 0, sysctl_bufspace, "L", "Physical memory used for buffers");
static counter_u64_t bufkvaspace;
SYSCTL_COUNTER_U64(_vfs, OID_AUTO, bufkvaspace, CTLFLAG_RD, &bufkvaspace,
    "Kernel virtual memory used for buffers");
static long maxbufspace;
SYSCTL_PROC(_vfs, OID_AUTO, maxbufspace,
    CTLTYPE_LONG|CTLFLAG_MPSAFE|CTLFLAG_RW, &maxbufspace,
    __offsetof(struct bufdomain, bd_maxbufspace), sysctl_bufdomain_long, "L",
    "Maximum allowed value of bufspace (including metadata)");
static long bufmallocspace;
SYSCTL_LONG(_vfs, OID_AUTO, bufmallocspace, CTLFLAG_RD, &bufmallocspace, 0,
    "Amount of malloced memory for buffers");
static long maxbufmallocspace;
SYSCTL_LONG(_vfs, OID_AUTO, maxmallocbufspace, CTLFLAG_RW, &maxbufmallocspace,
    0, "Maximum amount of malloced memory for buffers");
static long lobufspace;
SYSCTL_PROC(_vfs, OID_AUTO, lobufspace,
    CTLTYPE_LONG|CTLFLAG_MPSAFE|CTLFLAG_RW, &lobufspace,
    __offsetof(struct bufdomain, bd_lobufspace), sysctl_bufdomain_long, "L",
    "Minimum amount of buffers we want to have");
long hibufspace;
SYSCTL_PROC(_vfs, OID_AUTO, hibufspace,
    CTLTYPE_LONG|CTLFLAG_MPSAFE|CTLFLAG_RW, &hibufspace,
    __offsetof(struct bufdomain, bd_hibufspace), sysctl_bufdomain_long, "L",
    "Maximum allowed value of bufspace (excluding metadata)");
long bufspacethresh;
SYSCTL_PROC(_vfs, OID_AUTO, bufspacethresh,
    CTLTYPE_LONG|CTLFLAG_MPSAFE|CTLFLAG_RW, &bufspacethresh,
    __offsetof(struct bufdomain, bd_bufspacethresh), sysctl_bufdomain_long, "L",
    "Bufspace consumed before waking the daemon to free some");
static counter_u64_t buffreekvacnt;
SYSCTL_COUNTER_U64(_vfs, OID_AUTO, buffreekvacnt, CTLFLAG_RW, &buffreekvacnt,
    "Number of times we have freed the KVA space from some buffer");
static counter_u64_t bufdefragcnt;
SYSCTL_COUNTER_U64(_vfs, OID_AUTO, bufdefragcnt, CTLFLAG_RW, &bufdefragcnt,
    "Number of times we have had to repeat buffer allocation to defragment");
static long lorunningspace;
SYSCTL_PROC(_vfs, OID_AUTO, lorunningspace, CTLTYPE_LONG | CTLFLAG_MPSAFE |
    CTLFLAG_RW, &lorunningspace, 0, sysctl_runningspace, "L",
    "Minimum preferred space used for in-progress I/O");
static long hirunningspace;
SYSCTL_PROC(_vfs, OID_AUTO, hirunningspace, CTLTYPE_LONG | CTLFLAG_MPSAFE |
    CTLFLAG_RW, &hirunningspace, 0, sysctl_runningspace, "L",
    "Maximum amount of space to use for in-progress I/O");
int dirtybufferflushes;
SYSCTL_INT(_vfs, OID_AUTO, dirtybufferflushes, CTLFLAG_RW, &dirtybufferflushes,
    0, "Number of bdwrite to bawrite conversions to limit dirty buffers");
int bdwriteskip;
SYSCTL_INT(_vfs, OID_AUTO, bdwriteskip, CTLFLAG_RW, &bdwriteskip,
    0, "Number of buffers supplied to bdwrite with snapshot deadlock risk");
int altbufferflushes;
SYSCTL_INT(_vfs, OID_AUTO, altbufferflushes, CTLFLAG_RW, &altbufferflushes,
    0, "Number of fsync flushes to limit dirty buffers");
static int recursiveflushes;
SYSCTL_INT(_vfs, OID_AUTO, recursiveflushes, CTLFLAG_RW, &recursiveflushes,
    0, "Number of flushes skipped due to being recursive");
static int sysctl_numdirtybuffers(SYSCTL_HANDLER_ARGS);
SYSCTL_PROC(_vfs, OID_AUTO, numdirtybuffers,
    CTLTYPE_INT|CTLFLAG_MPSAFE|CTLFLAG_RD, NULL, 0, sysctl_numdirtybuffers, "I",
    "Number of buffers that are dirty (has unwritten changes) at the moment");
static int lodirtybuffers;
SYSCTL_PROC(_vfs, OID_AUTO, lodirtybuffers,
    CTLTYPE_INT|CTLFLAG_MPSAFE|CTLFLAG_RW, &lodirtybuffers,
    __offsetof(struct bufdomain, bd_lodirtybuffers), sysctl_bufdomain_int, "I",
    "How many buffers we want to have free before bufdaemon can sleep");
static int hidirtybuffers;
SYSCTL_PROC(_vfs, OID_AUTO, hidirtybuffers,
    CTLTYPE_INT|CTLFLAG_MPSAFE|CTLFLAG_RW, &hidirtybuffers,
    __offsetof(struct bufdomain, bd_hidirtybuffers), sysctl_bufdomain_int, "I",
    "When the number of dirty buffers is considered severe");
int dirtybufthresh;
SYSCTL_PROC(_vfs, OID_AUTO, dirtybufthresh,
    CTLTYPE_INT|CTLFLAG_MPSAFE|CTLFLAG_RW, &dirtybufthresh,
    __offsetof(struct bufdomain, bd_dirtybufthresh), sysctl_bufdomain_int, "I",
    "Number of bdwrite to bawrite conversions to clear dirty buffers");
static int numfreebuffers;
SYSCTL_INT(_vfs, OID_AUTO, numfreebuffers, CTLFLAG_RD, &numfreebuffers, 0,
    "Number of free buffers");
static int lofreebuffers;
SYSCTL_PROC(_vfs, OID_AUTO, lofreebuffers,
    CTLTYPE_INT|CTLFLAG_MPSAFE|CTLFLAG_RW, &lofreebuffers,
    __offsetof(struct bufdomain, bd_lofreebuffers), sysctl_bufdomain_int, "I",
   "Target number of free buffers");
static int hifreebuffers;
SYSCTL_PROC(_vfs, OID_AUTO, hifreebuffers,
    CTLTYPE_INT|CTLFLAG_MPSAFE|CTLFLAG_RW, &hifreebuffers,
    __offsetof(struct bufdomain, bd_hifreebuffers), sysctl_bufdomain_int, "I",
   "Threshold for clean buffer recycling");
static counter_u64_t getnewbufcalls;
SYSCTL_COUNTER_U64(_vfs, OID_AUTO, getnewbufcalls, CTLFLAG_RD,
   &getnewbufcalls, "Number of calls to getnewbuf");
static counter_u64_t getnewbufrestarts;
SYSCTL_COUNTER_U64(_vfs, OID_AUTO, getnewbufrestarts, CTLFLAG_RD,
    &getnewbufrestarts,
    "Number of times getnewbuf has had to restart a buffer acquisition");
static counter_u64_t mappingrestarts;
SYSCTL_COUNTER_U64(_vfs, OID_AUTO, mappingrestarts, CTLFLAG_RD,
    &mappingrestarts,
    "Number of times getblk has had to restart a buffer mapping for "
    "unmapped buffer");
static counter_u64_t numbufallocfails;
SYSCTL_COUNTER_U64(_vfs, OID_AUTO, numbufallocfails, CTLFLAG_RW,
    &numbufallocfails, "Number of times buffer allocations failed");
static int flushbufqtarget = 100;
SYSCTL_INT(_vfs, OID_AUTO, flushbufqtarget, CTLFLAG_RW, &flushbufqtarget, 0,
    "Amount of work to do in flushbufqueues when helping bufdaemon");
static counter_u64_t notbufdflushes;
SYSCTL_COUNTER_U64(_vfs, OID_AUTO, notbufdflushes, CTLFLAG_RD, &notbufdflushes,
    "Number of dirty buffer flushes done by the bufdaemon helpers");
static long barrierwrites;
SYSCTL_LONG(_vfs, OID_AUTO, barrierwrites, CTLFLAG_RW, &barrierwrites, 0,
    "Number of barrier writes");
SYSCTL_INT(_vfs, OID_AUTO, unmapped_buf_allowed, CTLFLAG_RD,
    &unmapped_buf_allowed, 0,
    "Permit the use of the unmapped i/o");
int maxbcachebuf = MAXBCACHEBUF;
SYSCTL_INT(_vfs, OID_AUTO, maxbcachebuf, CTLFLAG_RDTUN, &maxbcachebuf, 0,
    "Maximum size of a buffer cache block");

/*
 * This lock synchronizes access to bd_request.
 */
static struct mtx_padalign __exclusive_cache_line bdlock;

/*
 * This lock protects the runningbufreq and synchronizes runningbufwakeup and
 * waitrunningbufspace().
 */
static struct mtx_padalign __exclusive_cache_line rbreqlock;

/*
 * Lock that protects bdirtywait.
 */
static struct mtx_padalign __exclusive_cache_line bdirtylock;

/*
 * Wakeup point for bufdaemon, as well as indicator of whether it is already
 * active.  Set to 1 when the bufdaemon is already "on" the queue, 0 when it
 * is idling.
 */
static int bd_request;

/*
 * Request for the buf daemon to write more buffers than is indicated by
 * lodirtybuf.  This may be necessary to push out excess dependencies or
 * defragment the address space where a simple count of the number of dirty
 * buffers is insufficient to characterize the demand for flushing them.
 */
static int bd_speedupreq;

/*
 * Synchronization (sleep/wakeup) variable for active buffer space requests.
 * Set when wait starts, cleared prior to wakeup().
 * Used in runningbufwakeup() and waitrunningbufspace().
 */
static int runningbufreq;

/*
 * Synchronization for bwillwrite() waiters.
 */
static int bdirtywait;

/*
 * Definitions for the buffer free lists.
 */
#define QUEUE_NONE	0	/* on no queue */
#define QUEUE_EMPTY	1	/* empty buffer headers */
#define QUEUE_DIRTY	2	/* B_DELWRI buffers */
#define QUEUE_CLEAN	3	/* non-B_DELWRI buffers */
#define QUEUE_SENTINEL	4	/* not an queue index, but mark for sentinel */

/* Maximum number of buffer domains. */
#define	BUF_DOMAINS	8

struct bufdomainset bdlodirty;		/* Domains > lodirty */
struct bufdomainset bdhidirty;		/* Domains > hidirty */

/* Configured number of clean queues. */
static int __read_mostly buf_domains;

BITSET_DEFINE(bufdomainset, BUF_DOMAINS);
struct bufdomain __exclusive_cache_line bdomain[BUF_DOMAINS];
struct bufqueue __exclusive_cache_line bqempty;

/*
 * per-cpu empty buffer cache.
 */
uma_zone_t buf_zone;

/*
 * Single global constant for BUF_WMESG, to avoid getting multiple references.
 * buf_wmesg is referred from macros.
 */
const char *buf_wmesg = BUF_WMESG;

static int
sysctl_runningspace(SYSCTL_HANDLER_ARGS)
{
	long value;
	int error;

	value = *(long *)arg1;
	error = sysctl_handle_long(oidp, &value, 0, req);
	if (error != 0 || req->newptr == NULL)
		return (error);
	mtx_lock(&rbreqlock);
	if (arg1 == &hirunningspace) {
		if (value < lorunningspace)
			error = EINVAL;
		else
			hirunningspace = value;
	} else {
		KASSERT(arg1 == &lorunningspace,
		    ("%s: unknown arg1", __func__));
		if (value > hirunningspace)
			error = EINVAL;
		else
			lorunningspace = value;
	}
	mtx_unlock(&rbreqlock);
	return (error);
}

static int
sysctl_bufdomain_int(SYSCTL_HANDLER_ARGS)
{
	int error;
	int value;
	int i;

	value = *(int *)arg1;
	error = sysctl_handle_int(oidp, &value, 0, req);
	if (error != 0 || req->newptr == NULL)
		return (error);
	*(int *)arg1 = value;
	for (i = 0; i < buf_domains; i++)
		*(int *)(uintptr_t)(((uintptr_t)&bdomain[i]) + arg2) =
		    value / buf_domains;

	return (error);
}

static int
sysctl_bufdomain_long(SYSCTL_HANDLER_ARGS)
{
	long value;
	int error;
	int i;

	value = *(long *)arg1;
	error = sysctl_handle_long(oidp, &value, 0, req);
	if (error != 0 || req->newptr == NULL)
		return (error);
	*(long *)arg1 = value;
	for (i = 0; i < buf_domains; i++)
		*(long *)(uintptr_t)(((uintptr_t)&bdomain[i]) + arg2) =
		    value / buf_domains;

	return (error);
}

#if defined(COMPAT_FREEBSD4) || defined(COMPAT_FREEBSD5) || \
    defined(COMPAT_FREEBSD6) || defined(COMPAT_FREEBSD7)
static int
sysctl_bufspace(SYSCTL_HANDLER_ARGS)
{
	long lvalue;
	int ivalue;
	int i;

	lvalue = 0;
	for (i = 0; i < buf_domains; i++)
		lvalue += bdomain[i].bd_bufspace;
	if (sizeof(int) == sizeof(long) || req->oldlen >= sizeof(long))
		return (sysctl_handle_long(oidp, &lvalue, 0, req));
	if (lvalue > INT_MAX)
		/* On overflow, still write out a long to trigger ENOMEM. */
		return (sysctl_handle_long(oidp, &lvalue, 0, req));
	ivalue = lvalue;
	return (sysctl_handle_int(oidp, &ivalue, 0, req));
}
#else
static int
sysctl_bufspace(SYSCTL_HANDLER_ARGS)
{
	long lvalue;
	int i;

	lvalue = 0;
	for (i = 0; i < buf_domains; i++)
		lvalue += bdomain[i].bd_bufspace;
	return (sysctl_handle_long(oidp, &lvalue, 0, req));
}
#endif

static int
sysctl_numdirtybuffers(SYSCTL_HANDLER_ARGS)
{
	int value;
	int i;

	value = 0;
	for (i = 0; i < buf_domains; i++)
		value += bdomain[i].bd_numdirtybuffers;
	return (sysctl_handle_int(oidp, &value, 0, req));
}

/*
 *	bdirtywakeup:
 *
 *	Wakeup any bwillwrite() waiters.
 */
static void
bdirtywakeup(void)
{
	mtx_lock(&bdirtylock);
	if (bdirtywait) {
		bdirtywait = 0;
		wakeup(&bdirtywait);
	}
	mtx_unlock(&bdirtylock);
}

/*
 *	bd_clear:
 *
 *	Clear a domain from the appropriate bitsets when dirtybuffers
 *	is decremented.
 */
static void
bd_clear(struct bufdomain *bd)
{

	mtx_lock(&bdirtylock);
	if (bd->bd_numdirtybuffers <= bd->bd_lodirtybuffers)
		BIT_CLR(BUF_DOMAINS, BD_DOMAIN(bd), &bdlodirty);
	if (bd->bd_numdirtybuffers <= bd->bd_hidirtybuffers)
		BIT_CLR(BUF_DOMAINS, BD_DOMAIN(bd), &bdhidirty);
	mtx_unlock(&bdirtylock);
}

/*
 *	bd_set:
 *
 *	Set a domain in the appropriate bitsets when dirtybuffers
 *	is incremented.
 */
static void
bd_set(struct bufdomain *bd)
{

	mtx_lock(&bdirtylock);
	if (bd->bd_numdirtybuffers > bd->bd_lodirtybuffers)
		BIT_SET(BUF_DOMAINS, BD_DOMAIN(bd), &bdlodirty);
	if (bd->bd_numdirtybuffers > bd->bd_hidirtybuffers)
		BIT_SET(BUF_DOMAINS, BD_DOMAIN(bd), &bdhidirty);
	mtx_unlock(&bdirtylock);
}

/*
 *	bdirtysub:
 *
 *	Decrement the numdirtybuffers count by one and wakeup any
 *	threads blocked in bwillwrite().
 */
static void
bdirtysub(struct buf *bp)
{
	struct bufdomain *bd;
	int num;

	bd = bufdomain(bp);
	num = atomic_fetchadd_int(&bd->bd_numdirtybuffers, -1);
	if (num == (bd->bd_lodirtybuffers + bd->bd_hidirtybuffers) / 2)
		bdirtywakeup();
	if (num == bd->bd_lodirtybuffers || num == bd->bd_hidirtybuffers)
		bd_clear(bd);
}

/*
 *	bdirtyadd:
 *
 *	Increment the numdirtybuffers count by one and wakeup the buf 
 *	daemon if needed.
 */
static void
bdirtyadd(struct buf *bp)
{
	struct bufdomain *bd;
	int num;

	/*
	 * Only do the wakeup once as we cross the boundary.  The
	 * buf daemon will keep running until the condition clears.
	 */
	bd = bufdomain(bp);
	num = atomic_fetchadd_int(&bd->bd_numdirtybuffers, 1);
	if (num == (bd->bd_lodirtybuffers + bd->bd_hidirtybuffers) / 2)
		bd_wakeup();
	if (num == bd->bd_lodirtybuffers || num == bd->bd_hidirtybuffers)
		bd_set(bd);
}

/*
 *	bufspace_daemon_wakeup:
 *
 *	Wakeup the daemons responsible for freeing clean bufs.
 */
static void
bufspace_daemon_wakeup(struct bufdomain *bd)
{

	/*
	 * avoid the lock if the daemon is running.
	 */
	if (atomic_fetchadd_int(&bd->bd_running, 1) == 0) {
		BD_RUN_LOCK(bd);
		atomic_store_int(&bd->bd_running, 1);
		wakeup(&bd->bd_running);
		BD_RUN_UNLOCK(bd);
	}
}

/*
 *	bufspace_daemon_wait:
 *
 *	Sleep until the domain falls below a limit or one second passes.
 */
static void
bufspace_daemon_wait(struct bufdomain *bd)
{
	/*
	 * Re-check our limits and sleep.  bd_running must be
	 * cleared prior to checking the limits to avoid missed
	 * wakeups.  The waker will adjust one of bufspace or
	 * freebuffers prior to checking bd_running.
	 */
	BD_RUN_LOCK(bd);
	atomic_store_int(&bd->bd_running, 0);
	if (bd->bd_bufspace < bd->bd_bufspacethresh &&
	    bd->bd_freebuffers > bd->bd_lofreebuffers) {
		msleep(&bd->bd_running, BD_RUN_LOCKPTR(bd), PRIBIO|PDROP,
		    "-", hz);
	} else {
		/* Avoid spurious wakeups while running. */
		atomic_store_int(&bd->bd_running, 1);
		BD_RUN_UNLOCK(bd);
	}
}

/*
 *	bufspace_adjust:
 *
 *	Adjust the reported bufspace for a KVA managed buffer, possibly
 * 	waking any waiters.
 */
static void
bufspace_adjust(struct buf *bp, int bufsize)
{
	struct bufdomain *bd;
	long space;
	int diff;

	KASSERT((bp->b_flags & B_MALLOC) == 0,
	    ("bufspace_adjust: malloc buf %p", bp));
	bd = bufdomain(bp);
	diff = bufsize - bp->b_bufsize;
	if (diff < 0) {
		atomic_subtract_long(&bd->bd_bufspace, -diff);
	} else if (diff > 0) {
		space = atomic_fetchadd_long(&bd->bd_bufspace, diff);
		/* Wake up the daemon on the transition. */
		if (space < bd->bd_bufspacethresh &&
		    space + diff >= bd->bd_bufspacethresh)
			bufspace_daemon_wakeup(bd);
	}
	bp->b_bufsize = bufsize;
}

/*
 *	bufspace_reserve:
 *
 *	Reserve bufspace before calling allocbuf().  metadata has a
 *	different space limit than data.
 */
static int
bufspace_reserve(struct bufdomain *bd, int size, bool metadata)
{
	long limit, new;
	long space;

	if (metadata)
		limit = bd->bd_maxbufspace;
	else
		limit = bd->bd_hibufspace;
	space = atomic_fetchadd_long(&bd->bd_bufspace, size);
	new = space + size;
	if (new > limit) {
		atomic_subtract_long(&bd->bd_bufspace, size);
		return (ENOSPC);
	}

	/* Wake up the daemon on the transition. */
	if (space < bd->bd_bufspacethresh && new >= bd->bd_bufspacethresh)
		bufspace_daemon_wakeup(bd);

	return (0);
}

/*
 *	bufspace_release:
 *
 *	Release reserved bufspace after bufspace_adjust() has consumed it.
 */
static void
bufspace_release(struct bufdomain *bd, int size)
{

	atomic_subtract_long(&bd->bd_bufspace, size);
}

/*
 *	bufspace_wait:
 *
 *	Wait for bufspace, acting as the buf daemon if a locked vnode is
 *	supplied.  bd_wanted must be set prior to polling for space.  The
 *	operation must be re-tried on return.
 */
static void
bufspace_wait(struct bufdomain *bd, struct vnode *vp, int gbflags,
    int slpflag, int slptimeo)
{
	struct thread *td;
	int error, fl, norunbuf;

	if ((gbflags & GB_NOWAIT_BD) != 0)
		return;

	td = curthread;
	BD_LOCK(bd);
	while (bd->bd_wanted) {
		if (vp != NULL && vp->v_type != VCHR &&
		    (td->td_pflags & TDP_BUFNEED) == 0) {
			BD_UNLOCK(bd);
			/*
			 * getblk() is called with a vnode locked, and
			 * some majority of the dirty buffers may as
			 * well belong to the vnode.  Flushing the
			 * buffers there would make a progress that
			 * cannot be achieved by the buf_daemon, that
			 * cannot lock the vnode.
			 */
			norunbuf = ~(TDP_BUFNEED | TDP_NORUNNINGBUF) |
			    (td->td_pflags & TDP_NORUNNINGBUF);

			/*
			 * Play bufdaemon.  The getnewbuf() function
			 * may be called while the thread owns lock
			 * for another dirty buffer for the same
			 * vnode, which makes it impossible to use
			 * VOP_FSYNC() there, due to the buffer lock
			 * recursion.
			 */
			td->td_pflags |= TDP_BUFNEED | TDP_NORUNNINGBUF;
			fl = buf_flush(vp, bd, flushbufqtarget);
			td->td_pflags &= norunbuf;
			BD_LOCK(bd);
			if (fl != 0)
				continue;
			if (bd->bd_wanted == 0)
				break;
		}
		error = msleep(&bd->bd_wanted, BD_LOCKPTR(bd),
		    (PRIBIO + 4) | slpflag, "newbuf", slptimeo);
		if (error != 0)
			break;
	}
	BD_UNLOCK(bd);
}


/*
 *	bufspace_daemon:
 *
 *	buffer space management daemon.  Tries to maintain some marginal
 *	amount of free buffer space so that requesting processes neither
 *	block nor work to reclaim buffers.
 */
static void
bufspace_daemon(void *arg)
{
	struct bufdomain *bd;

	EVENTHANDLER_REGISTER(shutdown_pre_sync, kthread_shutdown, curthread,
	    SHUTDOWN_PRI_LAST + 100);

	bd = arg;
	for (;;) {
		kthread_suspend_check();

		/*
		 * Free buffers from the clean queue until we meet our
		 * targets.
		 *
		 * Theory of operation:  The buffer cache is most efficient
		 * when some free buffer headers and space are always
		 * available to getnewbuf().  This daemon attempts to prevent
		 * the excessive blocking and synchronization associated
		 * with shortfall.  It goes through three phases according
		 * demand:
		 *
		 * 1)	The daemon wakes up voluntarily once per-second
		 *	during idle periods when the counters are below
		 *	the wakeup thresholds (bufspacethresh, lofreebuffers).
		 *
		 * 2)	The daemon wakes up as we cross the thresholds
		 *	ahead of any potential blocking.  This may bounce
		 *	slightly according to the rate of consumption and
		 *	release.
		 *
		 * 3)	The daemon and consumers are starved for working
		 *	clean buffers.  This is the 'bufspace' sleep below
		 *	which will inefficiently trade bufs with bqrelse
		 *	until we return to condition 2.
		 */
		while (bd->bd_bufspace > bd->bd_lobufspace ||
		    bd->bd_freebuffers < bd->bd_hifreebuffers) {
			if (buf_recycle(bd, false) != 0) {
				if (bd_flushall(bd))
					continue;
				/*
				 * Speedup dirty if we've run out of clean
				 * buffers.  This is possible in particular
				 * because softdep may held many bufs locked
				 * pending writes to other bufs which are
				 * marked for delayed write, exhausting
				 * clean space until they are written.
				 */
				bd_speedup();
				BD_LOCK(bd);
				if (bd->bd_wanted) {
					msleep(&bd->bd_wanted, BD_LOCKPTR(bd),
					    PRIBIO|PDROP, "bufspace", hz/10);
				} else
					BD_UNLOCK(bd);
			}
			maybe_yield();
		}
		bufspace_daemon_wait(bd);
	}
}

/*
 *	bufmallocadjust:
 *
 *	Adjust the reported bufspace for a malloc managed buffer, possibly
 *	waking any waiters.
 */
static void
bufmallocadjust(struct buf *bp, int bufsize)
{
	int diff;

	KASSERT((bp->b_flags & B_MALLOC) != 0,
	    ("bufmallocadjust: non-malloc buf %p", bp));
	diff = bufsize - bp->b_bufsize;
	if (diff < 0)
		atomic_subtract_long(&bufmallocspace, -diff);
	else
		atomic_add_long(&bufmallocspace, diff);
	bp->b_bufsize = bufsize;
}

/*
 *	runningwakeup:
 *
 *	Wake up processes that are waiting on asynchronous writes to fall
 *	below lorunningspace.
 */
static void
runningwakeup(void)
{

	mtx_lock(&rbreqlock);
	if (runningbufreq) {
		runningbufreq = 0;
		wakeup(&runningbufreq);
	}
	mtx_unlock(&rbreqlock);
}

/*
 *	runningbufwakeup:
 *
 *	Decrement the outstanding write count according.
 */
void
runningbufwakeup(struct buf *bp)
{
	long space, bspace;

	bspace = bp->b_runningbufspace;
	if (bspace == 0)
		return;
	space = atomic_fetchadd_long(&runningbufspace, -bspace);
	KASSERT(space >= bspace, ("runningbufspace underflow %ld %ld",
	    space, bspace));
	bp->b_runningbufspace = 0;
	/*
	 * Only acquire the lock and wakeup on the transition from exceeding
	 * the threshold to falling below it.
	 */
	if (space < lorunningspace)
		return;
	if (space - bspace > lorunningspace)
		return;
	runningwakeup();
}

/*
 *	waitrunningbufspace()
 *
 *	runningbufspace is a measure of the amount of I/O currently
 *	running.  This routine is used in async-write situations to
 *	prevent creating huge backups of pending writes to a device.
 *	Only asynchronous writes are governed by this function.
 *
 *	This does NOT turn an async write into a sync write.  It waits  
 *	for earlier writes to complete and generally returns before the
 *	caller's write has reached the device.
 */
void
waitrunningbufspace(void)
{

	mtx_lock(&rbreqlock);
	while (runningbufspace > hirunningspace) {
		runningbufreq = 1;
		msleep(&runningbufreq, &rbreqlock, PVM, "wdrain", 0);
	}
	mtx_unlock(&rbreqlock);
}


/*
 *	vfs_buf_test_cache:
 *
 *	Called when a buffer is extended.  This function clears the B_CACHE
 *	bit if the newly extended portion of the buffer does not contain
 *	valid data.
 */
static __inline void
vfs_buf_test_cache(struct buf *bp, vm_ooffset_t foff, vm_offset_t off,
    vm_offset_t size, vm_page_t m)
{

	VM_OBJECT_ASSERT_LOCKED(m->object);
	if (bp->b_flags & B_CACHE) {
		int base = (foff + off) & PAGE_MASK;
		if (vm_page_is_valid(m, base, size) == 0)
			bp->b_flags &= ~B_CACHE;
	}
}

/* Wake up the buffer daemon if necessary */
static void
bd_wakeup(void)
{

	mtx_lock(&bdlock);
	if (bd_request == 0) {
		bd_request = 1;
		wakeup(&bd_request);
	}
	mtx_unlock(&bdlock);
}

/*
 * Adjust the maxbcachbuf tunable.
 */
static void
maxbcachebuf_adjust(void)
{
	int i;

	/*
	 * maxbcachebuf must be a power of 2 >= MAXBSIZE.
	 */
	i = 2;
	while (i * 2 <= maxbcachebuf)
		i *= 2;
	maxbcachebuf = i;
	if (maxbcachebuf < MAXBSIZE)
		maxbcachebuf = MAXBSIZE;
	if (maxbcachebuf > MAXPHYS)
		maxbcachebuf = MAXPHYS;
	if (bootverbose != 0 && maxbcachebuf != MAXBCACHEBUF)
		printf("maxbcachebuf=%d\n", maxbcachebuf);
}

/*
 * bd_speedup - speedup the buffer cache flushing code
 */
void
bd_speedup(void)
{
	int needwake;

	mtx_lock(&bdlock);
	needwake = 0;
	if (bd_speedupreq == 0 || bd_request == 0)
		needwake = 1;
	bd_speedupreq = 1;
	bd_request = 1;
	if (needwake)
		wakeup(&bd_request);
	mtx_unlock(&bdlock);
}

#ifdef __i386__
#define	TRANSIENT_DENOM	5
#else
#define	TRANSIENT_DENOM 10
#endif

/*
 * Calculating buffer cache scaling values and reserve space for buffer
 * headers.  This is called during low level kernel initialization and
 * may be called more then once.  We CANNOT write to the memory area
 * being reserved at this time.
 */
caddr_t
kern_vfs_bio_buffer_alloc(caddr_t v, long physmem_est)
{
	int tuned_nbuf;
	long maxbuf, maxbuf_sz, buf_sz,	biotmap_sz;

	/*
	 * physmem_est is in pages.  Convert it to kilobytes (assumes
	 * PAGE_SIZE is >= 1K)
	 */
	physmem_est = physmem_est * (PAGE_SIZE / 1024);

	maxbcachebuf_adjust();
	/*
	 * The nominal buffer size (and minimum KVA allocation) is BKVASIZE.
	 * For the first 64MB of ram nominally allocate sufficient buffers to
	 * cover 1/4 of our ram.  Beyond the first 64MB allocate additional
	 * buffers to cover 1/10 of our ram over 64MB.  When auto-sizing
	 * the buffer cache we limit the eventual kva reservation to
	 * maxbcache bytes.
	 *
	 * factor represents the 1/4 x ram conversion.
	 */
	if (nbuf == 0) {
		int factor = 4 * BKVASIZE / 1024;

		nbuf = 50;
		if (physmem_est > 4096)
			nbuf += min((physmem_est - 4096) / factor,
			    65536 / factor);
		if (physmem_est > 65536)
			nbuf += min((physmem_est - 65536) * 2 / (factor * 5),
			    32 * 1024 * 1024 / (factor * 5));

		if (maxbcache && nbuf > maxbcache / BKVASIZE)
			nbuf = maxbcache / BKVASIZE;
		tuned_nbuf = 1;
	} else
		tuned_nbuf = 0;

	/* XXX Avoid unsigned long overflows later on with maxbufspace. */
	maxbuf = (LONG_MAX / 3) / BKVASIZE;
	if (nbuf > maxbuf) {
		if (!tuned_nbuf)
			printf("Warning: nbufs lowered from %d to %ld\n", nbuf,
			    maxbuf);
		nbuf = maxbuf;
	}

	/*
	 * Ideal allocation size for the transient bio submap is 10%
	 * of the maximal space buffer map.  This roughly corresponds
	 * to the amount of the buffer mapped for typical UFS load.
	 *
	 * Clip the buffer map to reserve space for the transient
	 * BIOs, if its extent is bigger than 90% (80% on i386) of the
	 * maximum buffer map extent on the platform.
	 *
	 * The fall-back to the maxbuf in case of maxbcache unset,
	 * allows to not trim the buffer KVA for the architectures
	 * with ample KVA space.
	 */
	if (bio_transient_maxcnt == 0 && unmapped_buf_allowed) {
		maxbuf_sz = maxbcache != 0 ? maxbcache : maxbuf * BKVASIZE;
		buf_sz = (long)nbuf * BKVASIZE;
		if (buf_sz < maxbuf_sz / TRANSIENT_DENOM *
		    (TRANSIENT_DENOM - 1)) {
			/*
			 * There is more KVA than memory.  Do not
			 * adjust buffer map size, and assign the rest
			 * of maxbuf to transient map.
			 */
			biotmap_sz = maxbuf_sz - buf_sz;
		} else {
			/*
			 * Buffer map spans all KVA we could afford on
			 * this platform.  Give 10% (20% on i386) of
			 * the buffer map to the transient bio map.
			 */
			biotmap_sz = buf_sz / TRANSIENT_DENOM;
			buf_sz -= biotmap_sz;
		}
		if (biotmap_sz / INT_MAX > MAXPHYS)
			bio_transient_maxcnt = INT_MAX;
		else
			bio_transient_maxcnt = biotmap_sz / MAXPHYS;
		/*
		 * Artificially limit to 1024 simultaneous in-flight I/Os
		 * using the transient mapping.
		 */
		if (bio_transient_maxcnt > 1024)
			bio_transient_maxcnt = 1024;
		if (tuned_nbuf)
			nbuf = buf_sz / BKVASIZE;
	}

	if (nswbuf == 0) {
		nswbuf = min(nbuf / 4, 256);
		if (nswbuf < NSWBUF_MIN)
			nswbuf = NSWBUF_MIN;
	}

	/*
	 * Reserve space for the buffer cache buffers
	 */
	buf = (void *)v;
	v = (caddr_t)(buf + nbuf);

	return(v);
}

/* Initialize the buffer subsystem.  Called before use of any buffers. */
void
bufinit(void)
{
	struct buf *bp;
	int i;

	KASSERT(maxbcachebuf >= MAXBSIZE,
	    ("maxbcachebuf (%d) must be >= MAXBSIZE (%d)\n", maxbcachebuf,
	    MAXBSIZE));
	bq_init(&bqempty, QUEUE_EMPTY, -1, "bufq empty lock");
	mtx_init(&rbreqlock, "runningbufspace lock", NULL, MTX_DEF);
	mtx_init(&bdlock, "buffer daemon lock", NULL, MTX_DEF);
	mtx_init(&bdirtylock, "dirty buf lock", NULL, MTX_DEF);

	unmapped_buf = (caddr_t)kva_alloc(MAXPHYS);

	/* finally, initialize each buffer header and stick on empty q */
	for (i = 0; i < nbuf; i++) {
		bp = &buf[i];
		bzero(bp, sizeof *bp);
		bp->b_flags = B_INVAL;
		bp->b_rcred = NOCRED;
		bp->b_wcred = NOCRED;
		bp->b_qindex = QUEUE_NONE;
		bp->b_domain = -1;
		bp->b_subqueue = mp_maxid + 1;
		bp->b_xflags = 0;
		bp->b_data = bp->b_kvabase = unmapped_buf;
		LIST_INIT(&bp->b_dep);
		BUF_LOCKINIT(bp);
		bq_insert(&bqempty, bp, false);
	}

	/*
	 * maxbufspace is the absolute maximum amount of buffer space we are 
	 * allowed to reserve in KVM and in real terms.  The absolute maximum
	 * is nominally used by metadata.  hibufspace is the nominal maximum
	 * used by most other requests.  The differential is required to 
	 * ensure that metadata deadlocks don't occur.
	 *
	 * maxbufspace is based on BKVASIZE.  Allocating buffers larger then
	 * this may result in KVM fragmentation which is not handled optimally
	 * by the system. XXX This is less true with vmem.  We could use
	 * PAGE_SIZE.
	 */
	maxbufspace = (long)nbuf * BKVASIZE;
	hibufspace = lmax(3 * maxbufspace / 4, maxbufspace - maxbcachebuf * 10);
	lobufspace = (hibufspace / 20) * 19; /* 95% */
	bufspacethresh = lobufspace + (hibufspace - lobufspace) / 2;

	/*
	 * Note: The 16 MiB upper limit for hirunningspace was chosen
	 * arbitrarily and may need further tuning. It corresponds to
	 * 128 outstanding write IO requests (if IO size is 128 KiB),
	 * which fits with many RAID controllers' tagged queuing limits.
	 * The lower 1 MiB limit is the historical upper limit for
	 * hirunningspace.
	 */
	hirunningspace = lmax(lmin(roundup(hibufspace / 64, maxbcachebuf),
	    16 * 1024 * 1024), 1024 * 1024);
	lorunningspace = roundup((hirunningspace * 2) / 3, maxbcachebuf);

	/*
	 * Limit the amount of malloc memory since it is wired permanently into
	 * the kernel space.  Even though this is accounted for in the buffer
	 * allocation, we don't want the malloced region to grow uncontrolled.
	 * The malloc scheme improves memory utilization significantly on
	 * average (small) directories.
	 */
	maxbufmallocspace = hibufspace / 20;

	/*
	 * Reduce the chance of a deadlock occurring by limiting the number
	 * of delayed-write dirty buffers we allow to stack up.
	 */
	hidirtybuffers = nbuf / 4 + 20;
	dirtybufthresh = hidirtybuffers * 9 / 10;
	/*
	 * To support extreme low-memory systems, make sure hidirtybuffers
	 * cannot eat up all available buffer space.  This occurs when our
	 * minimum cannot be met.  We try to size hidirtybuffers to 3/4 our
	 * buffer space assuming BKVASIZE'd buffers.
	 */
	while ((long)hidirtybuffers * BKVASIZE > 3 * hibufspace / 4) {
		hidirtybuffers >>= 1;
	}
	lodirtybuffers = hidirtybuffers / 2;

	/*
	 * lofreebuffers should be sufficient to avoid stalling waiting on
	 * buf headers under heavy utilization.  The bufs in per-cpu caches
	 * are counted as free but will be unavailable to threads executing
	 * on other cpus.
	 *
	 * hifreebuffers is the free target for the bufspace daemon.  This
	 * should be set appropriately to limit work per-iteration.
	 */
	lofreebuffers = MIN((nbuf / 25) + (20 * mp_ncpus), 128 * mp_ncpus);
	hifreebuffers = (3 * lofreebuffers) / 2;
	numfreebuffers = nbuf;

	/* Setup the kva and free list allocators. */
	vmem_set_reclaim(buffer_arena, bufkva_reclaim);
	buf_zone = uma_zcache_create("buf free cache", sizeof(struct buf),
	    NULL, NULL, NULL, NULL, buf_import, buf_release, NULL, 0);

	/*
	 * Size the clean queue according to the amount of buffer space.
	 * One queue per-256mb up to the max.  More queues gives better
	 * concurrency but less accurate LRU.
	 */
	buf_domains = MIN(howmany(maxbufspace, 256*1024*1024), BUF_DOMAINS);
	for (i = 0 ; i < buf_domains; i++) {
		struct bufdomain *bd;

		bd = &bdomain[i];
		bd_init(bd);
		bd->bd_freebuffers = nbuf / buf_domains;
		bd->bd_hifreebuffers = hifreebuffers / buf_domains;
		bd->bd_lofreebuffers = lofreebuffers / buf_domains;
		bd->bd_bufspace = 0;
		bd->bd_maxbufspace = maxbufspace / buf_domains;
		bd->bd_hibufspace = hibufspace / buf_domains;
		bd->bd_lobufspace = lobufspace / buf_domains;
		bd->bd_bufspacethresh = bufspacethresh / buf_domains;
		bd->bd_numdirtybuffers = 0;
		bd->bd_hidirtybuffers = hidirtybuffers / buf_domains;
		bd->bd_lodirtybuffers = lodirtybuffers / buf_domains;
		bd->bd_dirtybufthresh = dirtybufthresh / buf_domains;
		/* Don't allow more than 2% of bufs in the per-cpu caches. */
		bd->bd_lim = nbuf / buf_domains / 50 / mp_ncpus;
	}
	getnewbufcalls = counter_u64_alloc(M_WAITOK);
	getnewbufrestarts = counter_u64_alloc(M_WAITOK);
	mappingrestarts = counter_u64_alloc(M_WAITOK);
	numbufallocfails = counter_u64_alloc(M_WAITOK);
	notbufdflushes = counter_u64_alloc(M_WAITOK);
	buffreekvacnt = counter_u64_alloc(M_WAITOK);
	bufdefragcnt = counter_u64_alloc(M_WAITOK);
	bufkvaspace = counter_u64_alloc(M_WAITOK);
}

#ifdef INVARIANTS
static inline void
vfs_buf_check_mapped(struct buf *bp)
{

	KASSERT(bp->b_kvabase != unmapped_buf,
	    ("mapped buf: b_kvabase was not updated %p", bp));
	KASSERT(bp->b_data != unmapped_buf,
	    ("mapped buf: b_data was not updated %p", bp));
	KASSERT(bp->b_data < unmapped_buf || bp->b_data >= unmapped_buf +
	    MAXPHYS, ("b_data + b_offset unmapped %p", bp));
}

static inline void
vfs_buf_check_unmapped(struct buf *bp)
{

	KASSERT(bp->b_data == unmapped_buf,
	    ("unmapped buf: corrupted b_data %p", bp));
}

#define	BUF_CHECK_MAPPED(bp) vfs_buf_check_mapped(bp)
#define	BUF_CHECK_UNMAPPED(bp) vfs_buf_check_unmapped(bp)
#else
#define	BUF_CHECK_MAPPED(bp) do {} while (0)
#define	BUF_CHECK_UNMAPPED(bp) do {} while (0)
#endif

static int
isbufbusy(struct buf *bp)
{
	if (((bp->b_flags & B_INVAL) == 0 && BUF_ISLOCKED(bp)) ||
	    ((bp->b_flags & (B_DELWRI | B_INVAL)) == B_DELWRI))
		return (1);
	return (0);
}

/*
 * Shutdown the system cleanly to prepare for reboot, halt, or power off.
 */
void
bufshutdown(int show_busybufs)
{
	static int first_buf_printf = 1;
	struct buf *bp;
	int iter, nbusy, pbusy;
#ifndef PREEMPTION
	int subiter;
#endif

	/* 
	 * Sync filesystems for shutdown
	 */
	wdog_kern_pat(WD_LASTVAL);
	sys_sync(curthread, NULL);

	/*
	 * With soft updates, some buffers that are
	 * written will be remarked as dirty until other
	 * buffers are written.
	 */
	for (iter = pbusy = 0; iter < 20; iter++) {
		nbusy = 0;
		for (bp = &buf[nbuf]; --bp >= buf; )
			if (isbufbusy(bp))
				nbusy++;
		if (nbusy == 0) {
			if (first_buf_printf)
				printf("All buffers synced.");
			break;
		}
		if (first_buf_printf) {
			printf("Syncing disks, buffers remaining... ");
			first_buf_printf = 0;
		}
		printf("%d ", nbusy);
		if (nbusy < pbusy)
			iter = 0;
		pbusy = nbusy;

		wdog_kern_pat(WD_LASTVAL);
		sys_sync(curthread, NULL);

#ifdef PREEMPTION
		/*
		 * Spin for a while to allow interrupt threads to run.
		 */
		DELAY(50000 * iter);
#else
		/*
		 * Context switch several times to allow interrupt
		 * threads to run.
		 */
		for (subiter = 0; subiter < 50 * iter; subiter++) {
			thread_lock(curthread);
			mi_switch(SW_VOL, NULL);
			thread_unlock(curthread);
			DELAY(1000);
		}
#endif
	}
	printf("\n");
	/*
	 * Count only busy local buffers to prevent forcing 
	 * a fsck if we're just a client of a wedged NFS server
	 */
	nbusy = 0;
	for (bp = &buf[nbuf]; --bp >= buf; ) {
		if (isbufbusy(bp)) {
#if 0
/* XXX: This is bogus.  We should probably have a BO_REMOTE flag instead */
			if (bp->b_dev == NULL) {
				TAILQ_REMOVE(&mountlist,
				    bp->b_vp->v_mount, mnt_list);
				continue;
			}
#endif
			nbusy++;
			if (show_busybufs > 0) {
				printf(
	    "%d: buf:%p, vnode:%p, flags:%0x, blkno:%jd, lblkno:%jd, buflock:",
				    nbusy, bp, bp->b_vp, bp->b_flags,
				    (intmax_t)bp->b_blkno,
				    (intmax_t)bp->b_lblkno);
				BUF_LOCKPRINTINFO(bp);
				if (show_busybufs > 1)
					vn_printf(bp->b_vp,
					    "vnode content: ");
			}
		}
	}
	if (nbusy) {
		/*
		 * Failed to sync all blocks. Indicate this and don't
		 * unmount filesystems (thus forcing an fsck on reboot).
		 */
		printf("Giving up on %d buffers\n", nbusy);
		DELAY(5000000);	/* 5 seconds */
	} else {
		if (!first_buf_printf)
			printf("Final sync complete\n");
		/*
		 * Unmount filesystems
		 */
		if (panicstr == NULL)
			vfs_unmountall();
	}
	swapoff_all();
	DELAY(100000);		/* wait for console output to finish */
}

static void
bpmap_qenter(struct buf *bp)
{

	BUF_CHECK_MAPPED(bp);

	/*
	 * bp->b_data is relative to bp->b_offset, but
	 * bp->b_offset may be offset into the first page.
	 */
	bp->b_data = (caddr_t)trunc_page((vm_offset_t)bp->b_data);
	pmap_qenter((vm_offset_t)bp->b_data, bp->b_pages, bp->b_npages);
	bp->b_data = (caddr_t)((vm_offset_t)bp->b_data |
	    (vm_offset_t)(bp->b_offset & PAGE_MASK));
}

static inline struct bufdomain *
bufdomain(struct buf *bp)
{

	return (&bdomain[bp->b_domain]);
}

static struct bufqueue *
bufqueue(struct buf *bp)
{

	switch (bp->b_qindex) {
	case QUEUE_NONE:
		/* FALLTHROUGH */
	case QUEUE_SENTINEL:
		return (NULL);
	case QUEUE_EMPTY:
		return (&bqempty);
	case QUEUE_DIRTY:
		return (&bufdomain(bp)->bd_dirtyq);
	case QUEUE_CLEAN:
		return (&bufdomain(bp)->bd_subq[bp->b_subqueue]);
	default:
		break;
	}
	panic("bufqueue(%p): Unhandled type %d\n", bp, bp->b_qindex);
}

/*
 * Return the locked bufqueue that bp is a member of.
 */
static struct bufqueue *
bufqueue_acquire(struct buf *bp)
{
	struct bufqueue *bq, *nbq;

	/*
	 * bp can be pushed from a per-cpu queue to the
	 * cleanq while we're waiting on the lock.  Retry
	 * if the queues don't match.
	 */
	bq = bufqueue(bp);
	BQ_LOCK(bq);
	for (;;) {
		nbq = bufqueue(bp);
		if (bq == nbq)
			break;
		BQ_UNLOCK(bq);
		BQ_LOCK(nbq);
		bq = nbq;
	}
	return (bq);
}

/*
 *	binsfree:
 *
 *	Insert the buffer into the appropriate free list.  Requires a
 *	locked buffer on entry and buffer is unlocked before return.
 */
static void
binsfree(struct buf *bp, int qindex)
{
	struct bufdomain *bd;
	struct bufqueue *bq;

	KASSERT(qindex == QUEUE_CLEAN || qindex == QUEUE_DIRTY,
	    ("binsfree: Invalid qindex %d", qindex));
	BUF_ASSERT_XLOCKED(bp);

	/*
	 * Handle delayed bremfree() processing.
	 */
	if (bp->b_flags & B_REMFREE) {
		if (bp->b_qindex == qindex) {
			bp->b_flags |= B_REUSE;
			bp->b_flags &= ~B_REMFREE;
			BUF_UNLOCK(bp);
			return;
		}
		bq = bufqueue_acquire(bp);
		bq_remove(bq, bp);
		BQ_UNLOCK(bq);
	}
	bd = bufdomain(bp);
	if (qindex == QUEUE_CLEAN) {
		if (bd->bd_lim != 0)
			bq = &bd->bd_subq[PCPU_GET(cpuid)];
		else
			bq = bd->bd_cleanq;
	} else
		bq = &bd->bd_dirtyq;
	bq_insert(bq, bp, true);
}

/*
 * buf_free:
 *
 *	Free a buffer to the buf zone once it no longer has valid contents.
 */
static void
buf_free(struct buf *bp)
{

	if (bp->b_flags & B_REMFREE)
		bremfreef(bp);
	if (bp->b_vflags & BV_BKGRDINPROG)
		panic("losing buffer 1");
	if (bp->b_rcred != NOCRED) {
		crfree(bp->b_rcred);
		bp->b_rcred = NOCRED;
	}
	if (bp->b_wcred != NOCRED) {
		crfree(bp->b_wcred);
		bp->b_wcred = NOCRED;
	}
	if (!LIST_EMPTY(&bp->b_dep))
		buf_deallocate(bp);
	bufkva_free(bp);
	atomic_add_int(&bufdomain(bp)->bd_freebuffers, 1);
	BUF_UNLOCK(bp);
	uma_zfree(buf_zone, bp);
}

/*
 * buf_import:
 *
 *	Import bufs into the uma cache from the buf list.  The system still
 *	expects a static array of bufs and much of the synchronization
 *	around bufs assumes type stable storage.  As a result, UMA is used
 *	only as a per-cpu cache of bufs still maintained on a global list.
 */
static int
buf_import(void *arg, void **store, int cnt, int domain, int flags)
{
	struct buf *bp;
	int i;

	BQ_LOCK(&bqempty);
	for (i = 0; i < cnt; i++) {
		bp = TAILQ_FIRST(&bqempty.bq_queue);
		if (bp == NULL)
			break;
		bq_remove(&bqempty, bp);
		store[i] = bp;
	}
	BQ_UNLOCK(&bqempty);

	return (i);
}

/*
 * buf_release:
 *
 *	Release bufs from the uma cache back to the buffer queues.
 */
static void
buf_release(void *arg, void **store, int cnt)
{
	struct bufqueue *bq;
	struct buf *bp;
        int i;

	bq = &bqempty;
	BQ_LOCK(bq);
        for (i = 0; i < cnt; i++) {
		bp = store[i];
		/* Inline bq_insert() to batch locking. */
		TAILQ_INSERT_TAIL(&bq->bq_queue, bp, b_freelist);
		bp->b_flags &= ~(B_AGE | B_REUSE);
		bq->bq_len++;
		bp->b_qindex = bq->bq_index;
	}
	BQ_UNLOCK(bq);
}

/*
 * buf_alloc:
 *
 *	Allocate an empty buffer header.
 */
static struct buf *
buf_alloc(struct bufdomain *bd)
{
	struct buf *bp;
	int freebufs;

	/*
	 * We can only run out of bufs in the buf zone if the average buf
	 * is less than BKVASIZE.  In this case the actual wait/block will
	 * come from buf_reycle() failing to flush one of these small bufs.
	 */
	bp = NULL;
	freebufs = atomic_fetchadd_int(&bd->bd_freebuffers, -1);
	if (freebufs > 0)
		bp = uma_zalloc(buf_zone, M_NOWAIT);
	if (bp == NULL) {
		atomic_add_int(&bd->bd_freebuffers, 1);
		bufspace_daemon_wakeup(bd);
		counter_u64_add(numbufallocfails, 1);
		return (NULL);
	}
	/*
	 * Wake-up the bufspace daemon on transition below threshold.
	 */
	if (freebufs == bd->bd_lofreebuffers)
		bufspace_daemon_wakeup(bd);

	if (BUF_LOCK(bp, LK_EXCLUSIVE | LK_NOWAIT, NULL) != 0)
		panic("getnewbuf_empty: Locked buf %p on free queue.", bp);
	
	KASSERT(bp->b_vp == NULL,
	    ("bp: %p still has vnode %p.", bp, bp->b_vp));
	KASSERT((bp->b_flags & (B_DELWRI | B_NOREUSE)) == 0,
	    ("invalid buffer %p flags %#x", bp, bp->b_flags));
	KASSERT((bp->b_xflags & (BX_VNCLEAN|BX_VNDIRTY)) == 0,
	    ("bp: %p still on a buffer list. xflags %X", bp, bp->b_xflags));
	KASSERT(bp->b_npages == 0,
	    ("bp: %p still has %d vm pages\n", bp, bp->b_npages));
	KASSERT(bp->b_kvasize == 0, ("bp: %p still has kva\n", bp));
	KASSERT(bp->b_bufsize == 0, ("bp: %p still has bufspace\n", bp));

	bp->b_domain = BD_DOMAIN(bd);
	bp->b_flags = 0;
	bp->b_ioflags = 0;
	bp->b_xflags = 0;
	bp->b_vflags = 0;
	bp->b_vp = NULL;
	bp->b_blkno = bp->b_lblkno = 0;
	bp->b_offset = NOOFFSET;
	bp->b_iodone = 0;
	bp->b_error = 0;
	bp->b_resid = 0;
	bp->b_bcount = 0;
	bp->b_npages = 0;
	bp->b_dirtyoff = bp->b_dirtyend = 0;
	bp->b_bufobj = NULL;
	bp->b_data = bp->b_kvabase = unmapped_buf;
	bp->b_fsprivate1 = NULL;
	bp->b_fsprivate2 = NULL;
	bp->b_fsprivate3 = NULL;
	LIST_INIT(&bp->b_dep);

	return (bp);
}

/*
 *	buf_recycle:
 *
 *	Free a buffer from the given bufqueue.  kva controls whether the
 *	freed buf must own some kva resources.  This is used for
 *	defragmenting.
 */
static int
buf_recycle(struct bufdomain *bd, bool kva)
{
	struct bufqueue *bq;
	struct buf *bp, *nbp;

	if (kva)
		counter_u64_add(bufdefragcnt, 1);
	nbp = NULL;
	bq = bd->bd_cleanq;
	BQ_LOCK(bq);
	KASSERT(BQ_LOCKPTR(bq) == BD_LOCKPTR(bd),
	    ("buf_recycle: Locks don't match"));
	nbp = TAILQ_FIRST(&bq->bq_queue);

	/*
	 * Run scan, possibly freeing data and/or kva mappings on the fly
	 * depending.
	 */
	while ((bp = nbp) != NULL) {
		/*
		 * Calculate next bp (we can only use it if we do not
		 * release the bqlock).
		 */
		nbp = TAILQ_NEXT(bp, b_freelist);

		/*
		 * If we are defragging then we need a buffer with 
		 * some kva to reclaim.
		 */
		if (kva && bp->b_kvasize == 0)
			continue;

		if (BUF_LOCK(bp, LK_EXCLUSIVE | LK_NOWAIT, NULL) != 0)
			continue;

		/*
		 * Implement a second chance algorithm for frequently
		 * accessed buffers.
		 */
		if ((bp->b_flags & B_REUSE) != 0) {
			TAILQ_REMOVE(&bq->bq_queue, bp, b_freelist);
			TAILQ_INSERT_TAIL(&bq->bq_queue, bp, b_freelist);
			bp->b_flags &= ~B_REUSE;
			BUF_UNLOCK(bp);
			continue;
		}

		/*
		 * Skip buffers with background writes in progress.
		 */
		if ((bp->b_vflags & BV_BKGRDINPROG) != 0) {
			BUF_UNLOCK(bp);
			continue;
		}

		KASSERT(bp->b_qindex == QUEUE_CLEAN,
		    ("buf_recycle: inconsistent queue %d bp %p",
		    bp->b_qindex, bp));
		KASSERT(bp->b_domain == BD_DOMAIN(bd),
		    ("getnewbuf: queue domain %d doesn't match request %d",
		    bp->b_domain, (int)BD_DOMAIN(bd)));
		/*
		 * NOTE:  nbp is now entirely invalid.  We can only restart
		 * the scan from this point on.
		 */
		bq_remove(bq, bp);
		BQ_UNLOCK(bq);

		/*
		 * Requeue the background write buffer with error and
		 * restart the scan.
		 */
		if ((bp->b_vflags & BV_BKGRDERR) != 0) {
			bqrelse(bp);
			BQ_LOCK(bq);
			nbp = TAILQ_FIRST(&bq->bq_queue);
			continue;
		}
		bp->b_flags |= B_INVAL;
		brelse(bp);
		return (0);
	}
	bd->bd_wanted = 1;
	BQ_UNLOCK(bq);

	return (ENOBUFS);
}

/*
 *	bremfree:
 *
 *	Mark the buffer for removal from the appropriate free list.
 *	
 */
void
bremfree(struct buf *bp)
{

	CTR3(KTR_BUF, "bremfree(%p) vp %p flags %X", bp, bp->b_vp, bp->b_flags);
	KASSERT((bp->b_flags & B_REMFREE) == 0,
	    ("bremfree: buffer %p already marked for delayed removal.", bp));
	KASSERT(bp->b_qindex != QUEUE_NONE,
	    ("bremfree: buffer %p not on a queue.", bp));
	BUF_ASSERT_XLOCKED(bp);

	bp->b_flags |= B_REMFREE;
}

/*
 *	bremfreef:
 *
 *	Force an immediate removal from a free list.  Used only in nfs when
 *	it abuses the b_freelist pointer.
 */
void
bremfreef(struct buf *bp)
{
	struct bufqueue *bq;

	bq = bufqueue_acquire(bp);
	bq_remove(bq, bp);
	BQ_UNLOCK(bq);
}

static void
bq_init(struct bufqueue *bq, int qindex, int subqueue, const char *lockname)
{

	mtx_init(&bq->bq_lock, lockname, NULL, MTX_DEF);
	TAILQ_INIT(&bq->bq_queue);
	bq->bq_len = 0;
	bq->bq_index = qindex;
	bq->bq_subqueue = subqueue;
}

static void
bd_init(struct bufdomain *bd)
{
	int i;

	bd->bd_cleanq = &bd->bd_subq[mp_maxid + 1];
	bq_init(bd->bd_cleanq, QUEUE_CLEAN, mp_maxid + 1, "bufq clean lock");
	bq_init(&bd->bd_dirtyq, QUEUE_DIRTY, -1, "bufq dirty lock");
	for (i = 0; i <= mp_maxid; i++)
		bq_init(&bd->bd_subq[i], QUEUE_CLEAN, i,
		    "bufq clean subqueue lock");
	mtx_init(&bd->bd_run_lock, "bufspace daemon run lock", NULL, MTX_DEF);
}

/*
 *	bq_remove:
 *
 *	Removes a buffer from the free list, must be called with the
 *	correct qlock held.
 */
static void
bq_remove(struct bufqueue *bq, struct buf *bp)
{

	CTR3(KTR_BUF, "bq_remove(%p) vp %p flags %X",
	    bp, bp->b_vp, bp->b_flags);
	KASSERT(bp->b_qindex != QUEUE_NONE,
	    ("bq_remove: buffer %p not on a queue.", bp));
	KASSERT(bufqueue(bp) == bq,
	    ("bq_remove: Remove buffer %p from wrong queue.", bp));

	BQ_ASSERT_LOCKED(bq);
	if (bp->b_qindex != QUEUE_EMPTY) {
		BUF_ASSERT_XLOCKED(bp);
	}
	KASSERT(bq->bq_len >= 1,
	    ("queue %d underflow", bp->b_qindex));
	TAILQ_REMOVE(&bq->bq_queue, bp, b_freelist);
	bq->bq_len--;
	bp->b_qindex = QUEUE_NONE;
	bp->b_flags &= ~(B_REMFREE | B_REUSE);
}

static void
bd_flush(struct bufdomain *bd, struct bufqueue *bq)
{
	struct buf *bp;

	BQ_ASSERT_LOCKED(bq);
	if (bq != bd->bd_cleanq) {
		BD_LOCK(bd);
		while ((bp = TAILQ_FIRST(&bq->bq_queue)) != NULL) {
			TAILQ_REMOVE(&bq->bq_queue, bp, b_freelist);
			TAILQ_INSERT_TAIL(&bd->bd_cleanq->bq_queue, bp,
			    b_freelist);
			bp->b_subqueue = bd->bd_cleanq->bq_subqueue;
		}
		bd->bd_cleanq->bq_len += bq->bq_len;
		bq->bq_len = 0;
	}
	if (bd->bd_wanted) {
		bd->bd_wanted = 0;
		wakeup(&bd->bd_wanted);
	}
	if (bq != bd->bd_cleanq)
		BD_UNLOCK(bd);
}

static int
bd_flushall(struct bufdomain *bd)
{
	struct bufqueue *bq;
	int flushed;
	int i;

	if (bd->bd_lim == 0)
		return (0);
	flushed = 0;
	for (i = 0; i <= mp_maxid; i++) {
		bq = &bd->bd_subq[i];
		if (bq->bq_len == 0)
			continue;
		BQ_LOCK(bq);
		bd_flush(bd, bq);
		BQ_UNLOCK(bq);
		flushed++;
	}

	return (flushed);
}

static void
bq_insert(struct bufqueue *bq, struct buf *bp, bool unlock)
{
	struct bufdomain *bd;

	if (bp->b_qindex != QUEUE_NONE)
		panic("bq_insert: free buffer %p onto another queue?", bp);

	bd = bufdomain(bp);
	if (bp->b_flags & B_AGE) {
		/* Place this buf directly on the real queue. */
		if (bq->bq_index == QUEUE_CLEAN)
			bq = bd->bd_cleanq;
		BQ_LOCK(bq);
		TAILQ_INSERT_HEAD(&bq->bq_queue, bp, b_freelist);
	} else {
		BQ_LOCK(bq);
		TAILQ_INSERT_TAIL(&bq->bq_queue, bp, b_freelist);
	}
	bp->b_flags &= ~(B_AGE | B_REUSE);
	bq->bq_len++;
	bp->b_qindex = bq->bq_index;
	bp->b_subqueue = bq->bq_subqueue;

	/*
	 * Unlock before we notify so that we don't wakeup a waiter that
	 * fails a trylock on the buf and sleeps again.
	 */
	if (unlock)
		BUF_UNLOCK(bp);

	if (bp->b_qindex == QUEUE_CLEAN) {
		/*
		 * Flush the per-cpu queue and notify any waiters.
		 */
		if (bd->bd_wanted || (bq != bd->bd_cleanq &&
		    bq->bq_len >= bd->bd_lim))
			bd_flush(bd, bq);
	}
	BQ_UNLOCK(bq);
}

/*
 *	bufkva_free:
 *
 *	Free the kva allocation for a buffer.
 *
 */
static void
bufkva_free(struct buf *bp)
{

#ifdef INVARIANTS
	if (bp->b_kvasize == 0) {
		KASSERT(bp->b_kvabase == unmapped_buf &&
		    bp->b_data == unmapped_buf,
		    ("Leaked KVA space on %p", bp));
	} else if (buf_mapped(bp))
		BUF_CHECK_MAPPED(bp);
	else
		BUF_CHECK_UNMAPPED(bp);
#endif
	if (bp->b_kvasize == 0)
		return;

	vmem_free(buffer_arena, (vm_offset_t)bp->b_kvabase, bp->b_kvasize);
	counter_u64_add(bufkvaspace, -bp->b_kvasize);
	counter_u64_add(buffreekvacnt, 1);
	bp->b_data = bp->b_kvabase = unmapped_buf;
	bp->b_kvasize = 0;
}

/*
 *	bufkva_alloc:
 *
 *	Allocate the buffer KVA and set b_kvasize and b_kvabase.
 */
static int
bufkva_alloc(struct buf *bp, int maxsize, int gbflags)
{
	vm_offset_t addr;
	int error;

	KASSERT((gbflags & GB_UNMAPPED) == 0 || (gbflags & GB_KVAALLOC) != 0,
	    ("Invalid gbflags 0x%x in %s", gbflags, __func__));

	bufkva_free(bp);

	addr = 0;
	error = vmem_alloc(buffer_arena, maxsize, M_BESTFIT | M_NOWAIT, &addr);
	if (error != 0) {
		/*
		 * Buffer map is too fragmented.  Request the caller
		 * to defragment the map.
		 */
		return (error);
	}
	bp->b_kvabase = (caddr_t)addr;
	bp->b_kvasize = maxsize;
	counter_u64_add(bufkvaspace, bp->b_kvasize);
	if ((gbflags & GB_UNMAPPED) != 0) {
		bp->b_data = unmapped_buf;
		BUF_CHECK_UNMAPPED(bp);
	} else {
		bp->b_data = bp->b_kvabase;
		BUF_CHECK_MAPPED(bp);
	}
	return (0);
}

/*
 *	bufkva_reclaim:
 *
 *	Reclaim buffer kva by freeing buffers holding kva.  This is a vmem
 *	callback that fires to avoid returning failure.
 */
static void
bufkva_reclaim(vmem_t *vmem, int flags)
{
	bool done;
	int q;
	int i;

	done = false;
	for (i = 0; i < 5; i++) {
		for (q = 0; q < buf_domains; q++)
			if (buf_recycle(&bdomain[q], true) != 0)
				done = true;
		if (done)
			break;
	}
	return;
}

/*
 * Attempt to initiate asynchronous I/O on read-ahead blocks.  We must
 * clear BIO_ERROR and B_INVAL prior to initiating I/O . If B_CACHE is set,
 * the buffer is valid and we do not have to do anything.
 */
static void
breada(struct vnode * vp, daddr_t * rablkno, int * rabsize, int cnt,
    struct ucred * cred, int flags, void (*ckhashfunc)(struct buf *))
{
	struct buf *rabp;
	int i;

	for (i = 0; i < cnt; i++, rablkno++, rabsize++) {
		if (inmem(vp, *rablkno))
			continue;
		rabp = getblk(vp, *rablkno, *rabsize, 0, 0, 0);
		if ((rabp->b_flags & B_CACHE) != 0) {
			brelse(rabp);
			continue;
		}
		if (!TD_IS_IDLETHREAD(curthread)) {
#ifdef RACCT
			if (racct_enable) {
				PROC_LOCK(curproc);
				racct_add_buf(curproc, rabp, 0);
				PROC_UNLOCK(curproc);
			}
#endif /* RACCT */
			curthread->td_ru.ru_inblock++;
		}
		rabp->b_flags |= B_ASYNC;
		rabp->b_flags &= ~B_INVAL;
		if ((flags & GB_CKHASH) != 0) {
			rabp->b_flags |= B_CKHASH;
			rabp->b_ckhashcalc = ckhashfunc;
		}
		rabp->b_ioflags &= ~BIO_ERROR;
		rabp->b_iocmd = BIO_READ;
		if (rabp->b_rcred == NOCRED && cred != NOCRED)
			rabp->b_rcred = crhold(cred);
		vfs_busy_pages(rabp, 0);
		BUF_KERNPROC(rabp);
		rabp->b_iooffset = dbtob(rabp->b_blkno);
		bstrategy(rabp);
	}
}

/*
 * Entry point for bread() and breadn() via #defines in sys/buf.h.
 *
 * Get a buffer with the specified data.  Look in the cache first.  We
 * must clear BIO_ERROR and B_INVAL prior to initiating I/O.  If B_CACHE
 * is set, the buffer is valid and we do not have to do anything, see
 * getblk(). Also starts asynchronous I/O on read-ahead blocks.
 *
 * Always return a NULL buffer pointer (in bpp) when returning an error.
 */
int
breadn_flags(struct vnode *vp, daddr_t blkno, int size, daddr_t *rablkno,
    int *rabsize, int cnt, struct ucred *cred, int flags,
    void (*ckhashfunc)(struct buf *), struct buf **bpp)
{
	struct buf *bp;
	struct thread *td;
	int error, readwait, rv;

	CTR3(KTR_BUF, "breadn(%p, %jd, %d)", vp, blkno, size);
	td = curthread;
	/*
	 * Can only return NULL if GB_LOCK_NOWAIT or GB_SPARSE flags
	 * are specified.
	 */
	error = getblkx(vp, blkno, size, 0, 0, flags, &bp);
	if (error != 0) {
		*bpp = NULL;
		return (error);
	}
	flags &= ~GB_NOSPARSE;
	*bpp = bp;

	/*
	 * If not found in cache, do some I/O
	 */
	readwait = 0;
	if ((bp->b_flags & B_CACHE) == 0) {
		if (!TD_IS_IDLETHREAD(td)) {
#ifdef RACCT
			if (racct_enable) {
				PROC_LOCK(td->td_proc);
				racct_add_buf(td->td_proc, bp, 0);
				PROC_UNLOCK(td->td_proc);
			}
#endif /* RACCT */
			td->td_ru.ru_inblock++;
		}
		bp->b_iocmd = BIO_READ;
		bp->b_flags &= ~B_INVAL;
		if ((flags & GB_CKHASH) != 0) {
			bp->b_flags |= B_CKHASH;
			bp->b_ckhashcalc = ckhashfunc;
		}
		bp->b_ioflags &= ~BIO_ERROR;
		if (bp->b_rcred == NOCRED && cred != NOCRED)
			bp->b_rcred = crhold(cred);
		vfs_busy_pages(bp, 0);
		bp->b_iooffset = dbtob(bp->b_blkno);
		bstrategy(bp);
		++readwait;
	}

	/*
	 * Attempt to initiate asynchronous I/O on read-ahead blocks.
	 */
	breada(vp, rablkno, rabsize, cnt, cred, flags, ckhashfunc);

	rv = 0;
	if (readwait) {
		rv = bufwait(bp);
		if (rv != 0) {
			brelse(bp);
			*bpp = NULL;
		}
	}
	return (rv);
}

/*
 * Write, release buffer on completion.  (Done by iodone
 * if async).  Do not bother writing anything if the buffer
 * is invalid.
 *
 * Note that we set B_CACHE here, indicating that buffer is
 * fully valid and thus cacheable.  This is true even of NFS
 * now so we set it generally.  This could be set either here 
 * or in biodone() since the I/O is synchronous.  We put it
 * here.
 */
int
bufwrite(struct buf *bp)
{
	int oldflags;
	struct vnode *vp;
	long space;
	int vp_md;

	CTR3(KTR_BUF, "bufwrite(%p) vp %p flags %X", bp, bp->b_vp, bp->b_flags);
	if ((bp->b_bufobj->bo_flag & BO_DEAD) != 0) {
		bp->b_flags |= B_INVAL | B_RELBUF;
		bp->b_flags &= ~B_CACHE;
		brelse(bp);
		return (ENXIO);
	}
	if (bp->b_flags & B_INVAL) {
		brelse(bp);
		return (0);
	}

	if (bp->b_flags & B_BARRIER)
		atomic_add_long(&barrierwrites, 1);

	oldflags = bp->b_flags;

	BUF_ASSERT_HELD(bp);

	KASSERT(!(bp->b_vflags & BV_BKGRDINPROG),
	    ("FFS background buffer should not get here %p", bp));

	vp = bp->b_vp;
	if (vp)
		vp_md = vp->v_vflag & VV_MD;
	else
		vp_md = 0;

	/*
	 * Mark the buffer clean.  Increment the bufobj write count
	 * before bundirty() call, to prevent other thread from seeing
	 * empty dirty list and zero counter for writes in progress,
	 * falsely indicating that the bufobj is clean.
	 */
	bufobj_wref(bp->b_bufobj);
	bundirty(bp);

	bp->b_flags &= ~B_DONE;
	bp->b_ioflags &= ~BIO_ERROR;
	bp->b_flags |= B_CACHE;
	bp->b_iocmd = BIO_WRITE;

	vfs_busy_pages(bp, 1);

	/*
	 * Normal bwrites pipeline writes
	 */
	bp->b_runningbufspace = bp->b_bufsize;
	space = atomic_fetchadd_long(&runningbufspace, bp->b_runningbufspace);

	if (!TD_IS_IDLETHREAD(curthread)) {
#ifdef RACCT
		if (racct_enable) {
			PROC_LOCK(curproc);
			racct_add_buf(curproc, bp, 1);
			PROC_UNLOCK(curproc);
		}
#endif /* RACCT */
		curthread->td_ru.ru_oublock++;
	}
	if (oldflags & B_ASYNC)
		BUF_KERNPROC(bp);
	bp->b_iooffset = dbtob(bp->b_blkno);
	buf_track(bp, __func__);
	bstrategy(bp);

	if ((oldflags & B_ASYNC) == 0) {
		int rtval = bufwait(bp);
		brelse(bp);
		return (rtval);
	} else if (space > hirunningspace) {
		/*
		 * don't allow the async write to saturate the I/O
		 * system.  We will not deadlock here because
		 * we are blocking waiting for I/O that is already in-progress
		 * to complete. We do not block here if it is the update
		 * or syncer daemon trying to clean up as that can lead
		 * to deadlock.
		 */
		if ((curthread->td_pflags & TDP_NORUNNINGBUF) == 0 && !vp_md)
			waitrunningbufspace();
	}

	return (0);
}

void
bufbdflush(struct bufobj *bo, struct buf *bp)
{
	struct buf *nbp;

	if (bo->bo_dirty.bv_cnt > dirtybufthresh + 10) {
		(void) VOP_FSYNC(bp->b_vp, MNT_NOWAIT, curthread);
		altbufferflushes++;
	} else if (bo->bo_dirty.bv_cnt > dirtybufthresh) {
		BO_LOCK(bo);
		/*
		 * Try to find a buffer to flush.
		 */
		TAILQ_FOREACH(nbp, &bo->bo_dirty.bv_hd, b_bobufs) {
			if ((nbp->b_vflags & BV_BKGRDINPROG) ||
			    BUF_LOCK(nbp,
				     LK_EXCLUSIVE | LK_NOWAIT, NULL))
				continue;
			if (bp == nbp)
				panic("bdwrite: found ourselves");
			BO_UNLOCK(bo);
			/* Don't countdeps with the bo lock held. */
			if (buf_countdeps(nbp, 0)) {
				BO_LOCK(bo);
				BUF_UNLOCK(nbp);
				continue;
			}
			if (nbp->b_flags & B_CLUSTEROK) {
				vfs_bio_awrite(nbp);
			} else {
				bremfree(nbp);
				bawrite(nbp);
			}
			dirtybufferflushes++;
			break;
		}
		if (nbp == NULL)
			BO_UNLOCK(bo);
	}
}

/*
 * Delayed write. (Buffer is marked dirty).  Do not bother writing
 * anything if the buffer is marked invalid.
 *
 * Note that since the buffer must be completely valid, we can safely
 * set B_CACHE.  In fact, we have to set B_CACHE here rather then in
 * biodone() in order to prevent getblk from writing the buffer
 * out synchronously.
 */
void
bdwrite(struct buf *bp)
{
	struct thread *td = curthread;
	struct vnode *vp;
	struct bufobj *bo;

	CTR3(KTR_BUF, "bdwrite(%p) vp %p flags %X", bp, bp->b_vp, bp->b_flags);
	KASSERT(bp->b_bufobj != NULL, ("No b_bufobj %p", bp));
	KASSERT((bp->b_flags & B_BARRIER) == 0,
	    ("Barrier request in delayed write %p", bp));
	BUF_ASSERT_HELD(bp);

	if (bp->b_flags & B_INVAL) {
		brelse(bp);
		return;
	}

	/*
	 * If we have too many dirty buffers, don't create any more.
	 * If we are wildly over our limit, then force a complete
	 * cleanup. Otherwise, just keep the situation from getting
	 * out of control. Note that we have to avoid a recursive
	 * disaster and not try to clean up after our own cleanup!
	 */
	vp = bp->b_vp;
	bo = bp->b_bufobj;
	if ((td->td_pflags & (TDP_COWINPROGRESS|TDP_INBDFLUSH)) == 0) {
		td->td_pflags |= TDP_INBDFLUSH;
		BO_BDFLUSH(bo, bp);
		td->td_pflags &= ~TDP_INBDFLUSH;
	} else
		recursiveflushes++;

	bdirty(bp);
	/*
	 * Set B_CACHE, indicating that the buffer is fully valid.  This is
	 * true even of NFS now.
	 */
	bp->b_flags |= B_CACHE;

	/*
	 * This bmap keeps the system from needing to do the bmap later,
	 * perhaps when the system is attempting to do a sync.  Since it
	 * is likely that the indirect block -- or whatever other datastructure
	 * that the filesystem needs is still in memory now, it is a good
	 * thing to do this.  Note also, that if the pageout daemon is
	 * requesting a sync -- there might not be enough memory to do
	 * the bmap then...  So, this is important to do.
	 */
	if (vp->v_type != VCHR && bp->b_lblkno == bp->b_blkno) {
		VOP_BMAP(vp, bp->b_lblkno, NULL, &bp->b_blkno, NULL, NULL);
	}

	buf_track(bp, __func__);

	/*
	 * Set the *dirty* buffer range based upon the VM system dirty
	 * pages.
	 *
	 * Mark the buffer pages as clean.  We need to do this here to
	 * satisfy the vnode_pager and the pageout daemon, so that it
	 * thinks that the pages have been "cleaned".  Note that since
	 * the pages are in a delayed write buffer -- the VFS layer
	 * "will" see that the pages get written out on the next sync,
	 * or perhaps the cluster will be completed.
	 */
	vfs_clean_pages_dirty_buf(bp);
	bqrelse(bp);

	/*
	 * note: we cannot initiate I/O from a bdwrite even if we wanted to,
	 * due to the softdep code.
	 */
}

/*
 *	bdirty:
 *
 *	Turn buffer into delayed write request.  We must clear BIO_READ and
 *	B_RELBUF, and we must set B_DELWRI.  We reassign the buffer to 
 *	itself to properly update it in the dirty/clean lists.  We mark it
 *	B_DONE to ensure that any asynchronization of the buffer properly
 *	clears B_DONE ( else a panic will occur later ).  
 *
 *	bdirty() is kinda like bdwrite() - we have to clear B_INVAL which
 *	might have been set pre-getblk().  Unlike bwrite/bdwrite, bdirty()
 *	should only be called if the buffer is known-good.
 *
 *	Since the buffer is not on a queue, we do not update the numfreebuffers
 *	count.
 *
 *	The buffer must be on QUEUE_NONE.
 */
void
bdirty(struct buf *bp)
{

	CTR3(KTR_BUF, "bdirty(%p) vp %p flags %X",
	    bp, bp->b_vp, bp->b_flags);
	KASSERT(bp->b_bufobj != NULL, ("No b_bufobj %p", bp));
	KASSERT(bp->b_flags & B_REMFREE || bp->b_qindex == QUEUE_NONE,
	    ("bdirty: buffer %p still on queue %d", bp, bp->b_qindex));
	BUF_ASSERT_HELD(bp);
	bp->b_flags &= ~(B_RELBUF);
	bp->b_iocmd = BIO_WRITE;

	if ((bp->b_flags & B_DELWRI) == 0) {
		bp->b_flags |= /* XXX B_DONE | */ B_DELWRI;
		reassignbuf(bp);
		bdirtyadd(bp);
	}
}

/*
 *	bundirty:
 *
 *	Clear B_DELWRI for buffer.
 *
 *	Since the buffer is not on a queue, we do not update the numfreebuffers
 *	count.
 *	
 *	The buffer must be on QUEUE_NONE.
 */

void
bundirty(struct buf *bp)
{

	CTR3(KTR_BUF, "bundirty(%p) vp %p flags %X", bp, bp->b_vp, bp->b_flags);
	KASSERT(bp->b_bufobj != NULL, ("No b_bufobj %p", bp));
	KASSERT(bp->b_flags & B_REMFREE || bp->b_qindex == QUEUE_NONE,
	    ("bundirty: buffer %p still on queue %d", bp, bp->b_qindex));
	BUF_ASSERT_HELD(bp);

	if (bp->b_flags & B_DELWRI) {
		bp->b_flags &= ~B_DELWRI;
		reassignbuf(bp);
		bdirtysub(bp);
	}
	/*
	 * Since it is now being written, we can clear its deferred write flag.
	 */
	bp->b_flags &= ~B_DEFERRED;
}

/*
 *	bawrite:
 *
 *	Asynchronous write.  Start output on a buffer, but do not wait for
 *	it to complete.  The buffer is released when the output completes.
 *
 *	bwrite() ( or the VOP routine anyway ) is responsible for handling 
 *	B_INVAL buffers.  Not us.
 */
void
bawrite(struct buf *bp)
{

	bp->b_flags |= B_ASYNC;
	(void) bwrite(bp);
}

/*
 *	babarrierwrite:
 *
 *	Asynchronous barrier write.  Start output on a buffer, but do not
 *	wait for it to complete.  Place a write barrier after this write so
 *	that this buffer and all buffers written before it are committed to
 *	the disk before any buffers written after this write are committed
 *	to the disk.  The buffer is released when the output completes.
 */
void
babarrierwrite(struct buf *bp)
{

	bp->b_flags |= B_ASYNC | B_BARRIER;
	(void) bwrite(bp);
}

/*
 *	bbarrierwrite:
 *
 *	Synchronous barrier write.  Start output on a buffer and wait for
 *	it to complete.  Place a write barrier after this write so that
 *	this buffer and all buffers written before it are committed to 
 *	the disk before any buffers written after this write are committed
 *	to the disk.  The buffer is released when the output completes.
 */
int
bbarrierwrite(struct buf *bp)
{

	bp->b_flags |= B_BARRIER;
	return (bwrite(bp));
}

/*
 *	bwillwrite:
 *
 *	Called prior to the locking of any vnodes when we are expecting to
 *	write.  We do not want to starve the buffer cache with too many
 *	dirty buffers so we block here.  By blocking prior to the locking
 *	of any vnodes we attempt to avoid the situation where a locked vnode
 *	prevents the various system daemons from flushing related buffers.
 */
void
bwillwrite(void)
{

	if (buf_dirty_count_severe()) {
		mtx_lock(&bdirtylock);
		while (buf_dirty_count_severe()) {
			bdirtywait = 1;
			msleep(&bdirtywait, &bdirtylock, (PRIBIO + 4),
			    "flswai", 0);
		}
		mtx_unlock(&bdirtylock);
	}
}

/*
 * Return true if we have too many dirty buffers.
 */
int
buf_dirty_count_severe(void)
{

	return (!BIT_EMPTY(BUF_DOMAINS, &bdhidirty));
}

/*
 *	brelse:
 *
 *	Release a busy buffer and, if requested, free its resources.  The
 *	buffer will be stashed in the appropriate bufqueue[] allowing it
 *	to be accessed later as a cache entity or reused for other purposes.
 */
void
brelse(struct buf *bp)
{
	struct mount *v_mnt;
	int qindex;

	/*
	 * Many functions erroneously call brelse with a NULL bp under rare
	 * error conditions. Simply return when called with a NULL bp.
	 */
	if (bp == NULL)
		return;
	CTR3(KTR_BUF, "brelse(%p) vp %p flags %X",
	    bp, bp->b_vp, bp->b_flags);
	KASSERT(!(bp->b_flags & (B_CLUSTER|B_PAGING)),
	    ("brelse: inappropriate B_PAGING or B_CLUSTER bp %p", bp));
	KASSERT((bp->b_flags & B_VMIO) != 0 || (bp->b_flags & B_NOREUSE) == 0,
	    ("brelse: non-VMIO buffer marked NOREUSE"));

	if (BUF_LOCKRECURSED(bp)) {
		/*
		 * Do not process, in particular, do not handle the
		 * B_INVAL/B_RELBUF and do not release to free list.
		 */
		BUF_UNLOCK(bp);
		return;
	}

	if (bp->b_flags & B_MANAGED) {
		bqrelse(bp);
		return;
	}

	if ((bp->b_vflags & (BV_BKGRDINPROG | BV_BKGRDERR)) == BV_BKGRDERR) {
		BO_LOCK(bp->b_bufobj);
		bp->b_vflags &= ~BV_BKGRDERR;
		BO_UNLOCK(bp->b_bufobj);
		bdirty(bp);
	}
	if (bp->b_iocmd == BIO_WRITE && (bp->b_ioflags & BIO_ERROR) &&
	    (bp->b_error != ENXIO || !LIST_EMPTY(&bp->b_dep)) &&
	    !(bp->b_flags & B_INVAL)) {
		/*
		 * Failed write, redirty.  All errors except ENXIO (which
		 * means the device is gone) are treated as being
		 * transient.
		 *
		 * XXX Treating EIO as transient is not correct; the
		 * contract with the local storage device drivers is that
		 * they will only return EIO once the I/O is no longer
		 * retriable.  Network I/O also respects this through the
		 * guarantees of TCP and/or the internal retries of NFS.
		 * ENOMEM might be transient, but we also have no way of
		 * knowing when its ok to retry/reschedule.  In general,
		 * this entire case should be made obsolete through better
		 * error handling/recovery and resource scheduling.
		 *
		 * Do this also for buffers that failed with ENXIO, but have
		 * non-empty dependencies - the soft updates code might need
		 * to access the buffer to untangle them.
		 *
		 * Must clear BIO_ERROR to prevent pages from being scrapped.
		 */
		bp->b_ioflags &= ~BIO_ERROR;
		bdirty(bp);
	} else if ((bp->b_flags & (B_NOCACHE | B_INVAL)) ||
	    (bp->b_ioflags & BIO_ERROR) || (bp->b_bufsize <= 0)) {
		/*
		 * Either a failed read I/O, or we were asked to free or not
		 * cache the buffer, or we failed to write to a device that's
		 * no longer present.
		 */
		bp->b_flags |= B_INVAL;
		if (!LIST_EMPTY(&bp->b_dep))
			buf_deallocate(bp);
		if (bp->b_flags & B_DELWRI)
			bdirtysub(bp);
		bp->b_flags &= ~(B_DELWRI | B_CACHE);
		if ((bp->b_flags & B_VMIO) == 0) {
			allocbuf(bp, 0);
			if (bp->b_vp)
				brelvp(bp);
		}
	}

	/*
	 * We must clear B_RELBUF if B_DELWRI is set.  If vfs_vmio_truncate() 
	 * is called with B_DELWRI set, the underlying pages may wind up
	 * getting freed causing a previous write (bdwrite()) to get 'lost'
	 * because pages associated with a B_DELWRI bp are marked clean.
	 * 
	 * We still allow the B_INVAL case to call vfs_vmio_truncate(), even
	 * if B_DELWRI is set.
	 */
	if (bp->b_flags & B_DELWRI)
		bp->b_flags &= ~B_RELBUF;

	/*
	 * VMIO buffer rundown.  It is not very necessary to keep a VMIO buffer
	 * constituted, not even NFS buffers now.  Two flags effect this.  If
	 * B_INVAL, the struct buf is invalidated but the VM object is kept
	 * around ( i.e. so it is trivial to reconstitute the buffer later ).
	 *
	 * If BIO_ERROR or B_NOCACHE is set, pages in the VM object will be
	 * invalidated.  BIO_ERROR cannot be set for a failed write unless the
	 * buffer is also B_INVAL because it hits the re-dirtying code above.
	 *
	 * Normally we can do this whether a buffer is B_DELWRI or not.  If
	 * the buffer is an NFS buffer, it is tracking piecemeal writes or
	 * the commit state and we cannot afford to lose the buffer. If the
	 * buffer has a background write in progress, we need to keep it
	 * around to prevent it from being reconstituted and starting a second
	 * background write.
	 */

	v_mnt = bp->b_vp != NULL ? bp->b_vp->v_mount : NULL;

	if ((bp->b_flags & B_VMIO) && (bp->b_flags & B_NOCACHE ||
	    (bp->b_ioflags & BIO_ERROR && bp->b_iocmd == BIO_READ)) &&
	    (v_mnt == NULL || (v_mnt->mnt_vfc->vfc_flags & VFCF_NETWORK) == 0 ||
	    vn_isdisk(bp->b_vp, NULL) || (bp->b_flags & B_DELWRI) == 0)) {
		vfs_vmio_invalidate(bp);
		allocbuf(bp, 0);
	}

	if ((bp->b_flags & (B_INVAL | B_RELBUF)) != 0 ||
	    (bp->b_flags & (B_DELWRI | B_NOREUSE)) == B_NOREUSE) {
		allocbuf(bp, 0);
		bp->b_flags &= ~B_NOREUSE;
		if (bp->b_vp != NULL)
			brelvp(bp);
	}
			
	/*
	 * If the buffer has junk contents signal it and eventually
	 * clean up B_DELWRI and diassociate the vnode so that gbincore()
	 * doesn't find it.
	 */
	if (bp->b_bufsize == 0 || (bp->b_ioflags & BIO_ERROR) != 0 ||
	    (bp->b_flags & (B_INVAL | B_NOCACHE | B_RELBUF)) != 0)
		bp->b_flags |= B_INVAL;
	if (bp->b_flags & B_INVAL) {
		if (bp->b_flags & B_DELWRI)
			bundirty(bp);
		if (bp->b_vp)
			brelvp(bp);
	}

	buf_track(bp, __func__);

	/* buffers with no memory */
	if (bp->b_bufsize == 0) {
		buf_free(bp);
		return;
	}
	/* buffers with junk contents */
	if (bp->b_flags & (B_INVAL | B_NOCACHE | B_RELBUF) ||
	    (bp->b_ioflags & BIO_ERROR)) {
		bp->b_xflags &= ~(BX_BKGRDWRITE | BX_ALTDATA);
		if (bp->b_vflags & BV_BKGRDINPROG)
			panic("losing buffer 2");
		qindex = QUEUE_CLEAN;
		bp->b_flags |= B_AGE;
	/* remaining buffers */
	} else if (bp->b_flags & B_DELWRI)
		qindex = QUEUE_DIRTY;
	else
		qindex = QUEUE_CLEAN;

	if ((bp->b_flags & B_DELWRI) == 0 && (bp->b_xflags & BX_VNDIRTY))
		panic("brelse: not dirty");

	bp->b_flags &= ~(B_ASYNC | B_NOCACHE | B_RELBUF | B_DIRECT);
	/* binsfree unlocks bp. */
	binsfree(bp, qindex);
}

/*
 * Release a buffer back to the appropriate queue but do not try to free
 * it.  The buffer is expected to be used again soon.
 *
 * bqrelse() is used by bdwrite() to requeue a delayed write, and used by
 * biodone() to requeue an async I/O on completion.  It is also used when
 * known good buffers need to be requeued but we think we may need the data
 * again soon.
 *
 * XXX we should be able to leave the B_RELBUF hint set on completion.
 */
void
bqrelse(struct buf *bp)
{
	int qindex;

	CTR3(KTR_BUF, "bqrelse(%p) vp %p flags %X", bp, bp->b_vp, bp->b_flags);
	KASSERT(!(bp->b_flags & (B_CLUSTER|B_PAGING)),
	    ("bqrelse: inappropriate B_PAGING or B_CLUSTER bp %p", bp));

	qindex = QUEUE_NONE;
	if (BUF_LOCKRECURSED(bp)) {
		/* do not release to free list */
		BUF_UNLOCK(bp);
		return;
	}
	bp->b_flags &= ~(B_ASYNC | B_NOCACHE | B_AGE | B_RELBUF);

	if (bp->b_flags & B_MANAGED) {
		if (bp->b_flags & B_REMFREE)
			bremfreef(bp);
		goto out;
	}

	/* buffers with stale but valid contents */
	if ((bp->b_flags & B_DELWRI) != 0 || (bp->b_vflags & (BV_BKGRDINPROG |
	    BV_BKGRDERR)) == BV_BKGRDERR) {
		BO_LOCK(bp->b_bufobj);
		bp->b_vflags &= ~BV_BKGRDERR;
		BO_UNLOCK(bp->b_bufobj);
		qindex = QUEUE_DIRTY;
	} else {
		if ((bp->b_flags & B_DELWRI) == 0 &&
		    (bp->b_xflags & BX_VNDIRTY))
			panic("bqrelse: not dirty");
		if ((bp->b_flags & B_NOREUSE) != 0) {
			brelse(bp);
			return;
		}
		qindex = QUEUE_CLEAN;
	}
	buf_track(bp, __func__);
	/* binsfree unlocks bp. */
	binsfree(bp, qindex);
	return;

out:
	buf_track(bp, __func__);
	/* unlock */
	BUF_UNLOCK(bp);
}

/*
 * Complete I/O to a VMIO backed page.  Validate the pages as appropriate,
 * restore bogus pages.
 */
static void
vfs_vmio_iodone(struct buf *bp)
{
	vm_ooffset_t foff;
	vm_page_t m;
	vm_object_t obj;
	struct vnode *vp __unused;
	int i, iosize, resid;
	bool bogus;

	obj = bp->b_bufobj->bo_object;
	KASSERT(obj->paging_in_progress >= bp->b_npages,
	    ("vfs_vmio_iodone: paging in progress(%d) < b_npages(%d)",
	    obj->paging_in_progress, bp->b_npages));

	vp = bp->b_vp;
	KASSERT(vp->v_holdcnt > 0,
	    ("vfs_vmio_iodone: vnode %p has zero hold count", vp));
	KASSERT(vp->v_object != NULL,
	    ("vfs_vmio_iodone: vnode %p has no vm_object", vp));

	foff = bp->b_offset;
	KASSERT(bp->b_offset != NOOFFSET,
	    ("vfs_vmio_iodone: bp %p has no buffer offset", bp));

	bogus = false;
	iosize = bp->b_bcount - bp->b_resid;
	VM_OBJECT_WLOCK(obj);
	for (i = 0; i < bp->b_npages; i++) {
		resid = ((foff + PAGE_SIZE) & ~(off_t)PAGE_MASK) - foff;
		if (resid > iosize)
			resid = iosize;

		/*
		 * cleanup bogus pages, restoring the originals
		 */
		m = bp->b_pages[i];
		if (m == bogus_page) {
			bogus = true;
			m = vm_page_lookup(obj, OFF_TO_IDX(foff));
			if (m == NULL)
				panic("biodone: page disappeared!");
			bp->b_pages[i] = m;
		} else if ((bp->b_iocmd == BIO_READ) && resid > 0) {
			/*
			 * In the write case, the valid and clean bits are
			 * already changed correctly ( see bdwrite() ), so we 
			 * only need to do this here in the read case.
			 */
			KASSERT((m->dirty & vm_page_bits(foff & PAGE_MASK,
			    resid)) == 0, ("vfs_vmio_iodone: page %p "
			    "has unexpected dirty bits", m));
			vfs_page_set_valid(bp, foff, m);
		}
		KASSERT(OFF_TO_IDX(foff) == m->pindex,
		    ("vfs_vmio_iodone: foff(%jd)/pindex(%ju) mismatch",
		    (intmax_t)foff, (uintmax_t)m->pindex));

		vm_page_sunbusy(m);
		foff = (foff + PAGE_SIZE) & ~(off_t)PAGE_MASK;
		iosize -= resid;
	}
	vm_object_pip_wakeupn(obj, bp->b_npages);
	VM_OBJECT_WUNLOCK(obj);
	if (bogus && buf_mapped(bp)) {
		BUF_CHECK_MAPPED(bp);
		pmap_qenter(trunc_page((vm_offset_t)bp->b_data),
		    bp->b_pages, bp->b_npages);
	}
}

/*
 * Unwire a page held by a buf and either free it or update the page queues to
 * reflect its recent use.
 */
static void
vfs_vmio_unwire(struct buf *bp, vm_page_t m)
{
	bool freed;

	vm_page_lock(m);
	if (vm_page_unwire_noq(m)) {
		if ((bp->b_flags & B_DIRECT) != 0)
			freed = vm_page_try_to_free(m);
		else
			freed = false;
		if (!freed) {
			/*
			 * Use a racy check of the valid bits to determine
			 * whether we can accelerate reclamation of the page.
			 * The valid bits will be stable unless the page is
			 * being mapped or is referenced by multiple buffers,
			 * and in those cases we expect races to be rare.  At
			 * worst we will either accelerate reclamation of a
			 * valid page and violate LRU, or unnecessarily defer
			 * reclamation of an invalid page.
			 *
			 * The B_NOREUSE flag marks data that is not expected to
			 * be reused, so accelerate reclamation in that case
			 * too.  Otherwise, maintain LRU.
			 */
			if (m->valid == 0 || (bp->b_flags & B_NOREUSE) != 0)
				vm_page_deactivate_noreuse(m);
			else if (vm_page_active(m))
				vm_page_reference(m);
			else
				vm_page_deactivate(m);
		}
	}
	vm_page_unlock(m);
}

/*
 * Perform page invalidation when a buffer is released.  The fully invalid
 * pages will be reclaimed later in vfs_vmio_truncate().
 */
static void
vfs_vmio_invalidate(struct buf *bp)
{
	vm_object_t obj;
	vm_page_t m;
	int i, resid, poffset, presid;

	if (buf_mapped(bp)) {
		BUF_CHECK_MAPPED(bp);
		pmap_qremove(trunc_page((vm_offset_t)bp->b_data), bp->b_npages);
	} else
		BUF_CHECK_UNMAPPED(bp);
	/*
	 * Get the base offset and length of the buffer.  Note that 
	 * in the VMIO case if the buffer block size is not
	 * page-aligned then b_data pointer may not be page-aligned.
	 * But our b_pages[] array *IS* page aligned.
	 *
	 * block sizes less then DEV_BSIZE (usually 512) are not 
	 * supported due to the page granularity bits (m->valid,
	 * m->dirty, etc...). 
	 *
	 * See man buf(9) for more information
	 */
	obj = bp->b_bufobj->bo_object;
	resid = bp->b_bufsize;
	poffset = bp->b_offset & PAGE_MASK;
	VM_OBJECT_WLOCK(obj);
	for (i = 0; i < bp->b_npages; i++) {
		m = bp->b_pages[i];
		if (m == bogus_page)
			panic("vfs_vmio_invalidate: Unexpected bogus page.");
		bp->b_pages[i] = NULL;

		presid = resid > (PAGE_SIZE - poffset) ?
		    (PAGE_SIZE - poffset) : resid;
		KASSERT(presid >= 0, ("brelse: extra page"));
		while (vm_page_xbusied(m)) {
			vm_page_lock(m);
			VM_OBJECT_WUNLOCK(obj);
			vm_page_busy_sleep(m, "mbncsh", true);
			VM_OBJECT_WLOCK(obj);
		}
		if (pmap_page_wired_mappings(m) == 0)
			vm_page_set_invalid(m, poffset, presid);
		vfs_vmio_unwire(bp, m);
		resid -= presid;
		poffset = 0;
	}
	VM_OBJECT_WUNLOCK(obj);
	bp->b_npages = 0;
}

/*
 * Page-granular truncation of an existing VMIO buffer.
 */
static void
vfs_vmio_truncate(struct buf *bp, int desiredpages)
{
	vm_object_t obj;
	vm_page_t m;
	int i;

	if (bp->b_npages == desiredpages)
		return;

	if (buf_mapped(bp)) {
		BUF_CHECK_MAPPED(bp);
		pmap_qremove((vm_offset_t)trunc_page((vm_offset_t)bp->b_data) +
		    (desiredpages << PAGE_SHIFT), bp->b_npages - desiredpages);
	} else
		BUF_CHECK_UNMAPPED(bp);

	/*
	 * The object lock is needed only if we will attempt to free pages.
	 */
	obj = (bp->b_flags & B_DIRECT) != 0 ? bp->b_bufobj->bo_object : NULL;
	if (obj != NULL)
		VM_OBJECT_WLOCK(obj);
	for (i = desiredpages; i < bp->b_npages; i++) {
		m = bp->b_pages[i];
		KASSERT(m != bogus_page, ("allocbuf: bogus page found"));
		bp->b_pages[i] = NULL;
		vfs_vmio_unwire(bp, m);
	}
	if (obj != NULL)
		VM_OBJECT_WUNLOCK(obj);
	bp->b_npages = desiredpages;
}

/*
 * Byte granular extension of VMIO buffers.
 */
static void
vfs_vmio_extend(struct buf *bp, int desiredpages, int size)
{
	/*
	 * We are growing the buffer, possibly in a 
	 * byte-granular fashion.
	 */
	vm_object_t obj;
	vm_offset_t toff;
	vm_offset_t tinc;
	vm_page_t m;

	/*
	 * Step 1, bring in the VM pages from the object, allocating
	 * them if necessary.  We must clear B_CACHE if these pages
	 * are not valid for the range covered by the buffer.
	 */
	obj = bp->b_bufobj->bo_object;
	VM_OBJECT_WLOCK(obj);
	if (bp->b_npages < desiredpages) {
		/*
		 * We must allocate system pages since blocking
		 * here could interfere with paging I/O, no
		 * matter which process we are.
		 *
		 * Only exclusive busy can be tested here.
		 * Blocking on shared busy might lead to
		 * deadlocks once allocbuf() is called after
		 * pages are vfs_busy_pages().
		 */
		(void)vm_page_grab_pages(obj,
		    OFF_TO_IDX(bp->b_offset) + bp->b_npages,
		    VM_ALLOC_SYSTEM | VM_ALLOC_IGN_SBUSY |
		    VM_ALLOC_NOBUSY | VM_ALLOC_WIRED,
		    &bp->b_pages[bp->b_npages], desiredpages - bp->b_npages);
		bp->b_npages = desiredpages;
	}

	/*
	 * Step 2.  We've loaded the pages into the buffer,
	 * we have to figure out if we can still have B_CACHE
	 * set.  Note that B_CACHE is set according to the
	 * byte-granular range ( bcount and size ), not the
	 * aligned range ( newbsize ).
	 *
	 * The VM test is against m->valid, which is DEV_BSIZE
	 * aligned.  Needless to say, the validity of the data
	 * needs to also be DEV_BSIZE aligned.  Note that this
	 * fails with NFS if the server or some other client
	 * extends the file's EOF.  If our buffer is resized, 
	 * B_CACHE may remain set! XXX
	 */
	toff = bp->b_bcount;
	tinc = PAGE_SIZE - ((bp->b_offset + toff) & PAGE_MASK);
	while ((bp->b_flags & B_CACHE) && toff < size) {
		vm_pindex_t pi;

		if (tinc > (size - toff))
			tinc = size - toff;
		pi = ((bp->b_offset & PAGE_MASK) + toff) >> PAGE_SHIFT;
		m = bp->b_pages[pi];
		vfs_buf_test_cache(bp, bp->b_offset, toff, tinc, m);
		toff += tinc;
		tinc = PAGE_SIZE;
	}
	VM_OBJECT_WUNLOCK(obj);

	/*
	 * Step 3, fixup the KVA pmap.
	 */
	if (buf_mapped(bp))
		bpmap_qenter(bp);
	else
		BUF_CHECK_UNMAPPED(bp);
}

/*
 * Check to see if a block at a particular lbn is available for a clustered
 * write.
 */
static int
vfs_bio_clcheck(struct vnode *vp, int size, daddr_t lblkno, daddr_t blkno)
{
	struct buf *bpa;
	int match;

	match = 0;

	/* If the buf isn't in core skip it */
	if ((bpa = gbincore(&vp->v_bufobj, lblkno)) == NULL)
		return (0);

	/* If the buf is busy we don't want to wait for it */
	if (BUF_LOCK(bpa, LK_EXCLUSIVE | LK_NOWAIT, NULL) != 0)
		return (0);

	/* Only cluster with valid clusterable delayed write buffers */
	if ((bpa->b_flags & (B_DELWRI | B_CLUSTEROK | B_INVAL)) !=
	    (B_DELWRI | B_CLUSTEROK))
		goto done;

	if (bpa->b_bufsize != size)
		goto done;

	/*
	 * Check to see if it is in the expected place on disk and that the
	 * block has been mapped.
	 */
	if ((bpa->b_blkno != bpa->b_lblkno) && (bpa->b_blkno == blkno))
		match = 1;
done:
	BUF_UNLOCK(bpa);
	return (match);
}

/*
 *	vfs_bio_awrite:
 *
 *	Implement clustered async writes for clearing out B_DELWRI buffers.
 *	This is much better then the old way of writing only one buffer at
 *	a time.  Note that we may not be presented with the buffers in the 
 *	correct order, so we search for the cluster in both directions.
 */
int
vfs_bio_awrite(struct buf *bp)
{
	struct bufobj *bo;
	int i;
	int j;
	daddr_t lblkno = bp->b_lblkno;
	struct vnode *vp = bp->b_vp;
	int ncl;
	int nwritten;
	int size;
	int maxcl;
	int gbflags;

	bo = &vp->v_bufobj;
	gbflags = (bp->b_data == unmapped_buf) ? GB_UNMAPPED : 0;
	/*
	 * right now we support clustered writing only to regular files.  If
	 * we find a clusterable block we could be in the middle of a cluster
	 * rather then at the beginning.
	 */
	if ((vp->v_type == VREG) && 
	    (vp->v_mount != 0) && /* Only on nodes that have the size info */
	    (bp->b_flags & (B_CLUSTEROK | B_INVAL)) == B_CLUSTEROK) {

		size = vp->v_mount->mnt_stat.f_iosize;
		maxcl = MAXPHYS / size;

		BO_RLOCK(bo);
		for (i = 1; i < maxcl; i++)
			if (vfs_bio_clcheck(vp, size, lblkno + i,
			    bp->b_blkno + ((i * size) >> DEV_BSHIFT)) == 0)
				break;

		for (j = 1; i + j <= maxcl && j <= lblkno; j++) 
			if (vfs_bio_clcheck(vp, size, lblkno - j,
			    bp->b_blkno - ((j * size) >> DEV_BSHIFT)) == 0)
				break;
		BO_RUNLOCK(bo);
		--j;
		ncl = i + j;
		/*
		 * this is a possible cluster write
		 */
		if (ncl != 1) {
			BUF_UNLOCK(bp);
			nwritten = cluster_wbuild(vp, size, lblkno - j, ncl,
			    gbflags);
			return (nwritten);
		}
	}
	bremfree(bp);
	bp->b_flags |= B_ASYNC;
	/*
	 * default (old) behavior, writing out only one block
	 *
	 * XXX returns b_bufsize instead of b_bcount for nwritten?
	 */
	nwritten = bp->b_bufsize;
	(void) bwrite(bp);

	return (nwritten);
}

/*
 *	getnewbuf_kva:
 *
 *	Allocate KVA for an empty buf header according to gbflags.
 */
static int
getnewbuf_kva(struct buf *bp, int gbflags, int maxsize)
{

	if ((gbflags & (GB_UNMAPPED | GB_KVAALLOC)) != GB_UNMAPPED) {
		/*
		 * In order to keep fragmentation sane we only allocate kva
		 * in BKVASIZE chunks.  XXX with vmem we can do page size.
		 */
		maxsize = (maxsize + BKVAMASK) & ~BKVAMASK;

		if (maxsize != bp->b_kvasize &&
		    bufkva_alloc(bp, maxsize, gbflags))
			return (ENOSPC);
	}
	return (0);
}

/*
 *	getnewbuf:
 *
 *	Find and initialize a new buffer header, freeing up existing buffers
 *	in the bufqueues as necessary.  The new buffer is returned locked.
 *
 *	We block if:
 *		We have insufficient buffer headers
 *		We have insufficient buffer space
 *		buffer_arena is too fragmented ( space reservation fails )
 *		If we have to flush dirty buffers ( but we try to avoid this )
 *
 *	The caller is responsible for releasing the reserved bufspace after
 *	allocbuf() is called.
 */
static struct buf *
getnewbuf(struct vnode *vp, int slpflag, int slptimeo, int maxsize, int gbflags)
{
	struct bufdomain *bd;
	struct buf *bp;
	bool metadata, reserved;

	bp = NULL;
	KASSERT((gbflags & (GB_UNMAPPED | GB_KVAALLOC)) != GB_KVAALLOC,
	    ("GB_KVAALLOC only makes sense with GB_UNMAPPED"));
	if (!unmapped_buf_allowed)
		gbflags &= ~(GB_UNMAPPED | GB_KVAALLOC);

	if (vp == NULL || (vp->v_vflag & (VV_MD | VV_SYSTEM)) != 0 ||
	    vp->v_type == VCHR)
		metadata = true;
	else
		metadata = false;
	if (vp == NULL)
		bd = &bdomain[0];
	else
		bd = &bdomain[vp->v_bufobj.bo_domain];

	counter_u64_add(getnewbufcalls, 1);
	reserved = false;
	do {
		if (reserved == false &&
		    bufspace_reserve(bd, maxsize, metadata) != 0) {
			counter_u64_add(getnewbufrestarts, 1);
			continue;
		}
		reserved = true;
		if ((bp = buf_alloc(bd)) == NULL) {
			counter_u64_add(getnewbufrestarts, 1);
			continue;
		}
		if (getnewbuf_kva(bp, gbflags, maxsize) == 0)
			return (bp);
		break;
	} while (buf_recycle(bd, false) == 0);

	if (reserved)
		bufspace_release(bd, maxsize);
	if (bp != NULL) {
		bp->b_flags |= B_INVAL;
		brelse(bp);
	}
	bufspace_wait(bd, vp, gbflags, slpflag, slptimeo);

	return (NULL);
}

/*
 *	buf_daemon:
 *
 *	buffer flushing daemon.  Buffers are normally flushed by the
 *	update daemon but if it cannot keep up this process starts to
 *	take the load in an attempt to prevent getnewbuf() from blocking.
 */
static struct kproc_desc buf_kp = {
	"bufdaemon",
	buf_daemon,
	&bufdaemonproc
};
SYSINIT(bufdaemon, SI_SUB_KTHREAD_BUF, SI_ORDER_FIRST, kproc_start, &buf_kp);

static int
buf_flush(struct vnode *vp, struct bufdomain *bd, int target)
{
	int flushed;

	flushed = flushbufqueues(vp, bd, target, 0);
	if (flushed == 0) {
		/*
		 * Could not find any buffers without rollback
		 * dependencies, so just write the first one
		 * in the hopes of eventually making progress.
		 */
		if (vp != NULL && target > 2)
			target /= 2;
		flushbufqueues(vp, bd, target, 1);
	}
	return (flushed);
}

static void
buf_daemon()
{
	struct bufdomain *bd;
	int speedupreq;
	int lodirty;
	int i;

	/*
	 * This process needs to be suspended prior to shutdown sync.
	 */
	EVENTHANDLER_REGISTER(shutdown_pre_sync, kthread_shutdown, curthread,
	    SHUTDOWN_PRI_LAST + 100);

	/*
	 * Start the buf clean daemons as children threads.
	 */
	for (i = 0 ; i < buf_domains; i++) {
		int error;

		error = kthread_add((void (*)(void *))bufspace_daemon,
		    &bdomain[i], curproc, NULL, 0, 0, "bufspacedaemon-%d", i);
		if (error)
			panic("error %d spawning bufspace daemon", error);
	}

	/*
	 * This process is allowed to take the buffer cache to the limit
	 */
	curthread->td_pflags |= TDP_NORUNNINGBUF | TDP_BUFNEED;
	mtx_lock(&bdlock);
	for (;;) {
		bd_request = 0;
		mtx_unlock(&bdlock);

		kthread_suspend_check();

		/*
		 * Save speedupreq for this pass and reset to capture new
		 * requests.
		 */
		speedupreq = bd_speedupreq;
		bd_speedupreq = 0;

		/*
		 * Flush each domain sequentially according to its level and
		 * the speedup request.
		 */
		for (i = 0; i < buf_domains; i++) {
			bd = &bdomain[i];
			if (speedupreq)
				lodirty = bd->bd_numdirtybuffers / 2;
			else
				lodirty = bd->bd_lodirtybuffers;
			while (bd->bd_numdirtybuffers > lodirty) {
				if (buf_flush(NULL, bd,
				    bd->bd_numdirtybuffers - lodirty) == 0)
					break;
				kern_yield(PRI_USER);
			}
		}

		/*
		 * Only clear bd_request if we have reached our low water
		 * mark.  The buf_daemon normally waits 1 second and
		 * then incrementally flushes any dirty buffers that have
		 * built up, within reason.
		 *
		 * If we were unable to hit our low water mark and couldn't
		 * find any flushable buffers, we sleep for a short period
		 * to avoid endless loops on unlockable buffers.
		 */
		mtx_lock(&bdlock);
		if (!BIT_EMPTY(BUF_DOMAINS, &bdlodirty)) {
			/*
			 * We reached our low water mark, reset the
			 * request and sleep until we are needed again.
			 * The sleep is just so the suspend code works.
			 */
			bd_request = 0;
			/*
			 * Do an extra wakeup in case dirty threshold
			 * changed via sysctl and the explicit transition
			 * out of shortfall was missed.
			 */
			bdirtywakeup();
			if (runningbufspace <= lorunningspace)
				runningwakeup();
			msleep(&bd_request, &bdlock, PVM, "psleep", hz);
		} else {
			/*
			 * We couldn't find any flushable dirty buffers but
			 * still have too many dirty buffers, we
			 * have to sleep and try again.  (rare)
			 */
			msleep(&bd_request, &bdlock, PVM, "qsleep", hz / 10);
		}
	}
}

/*
 *	flushbufqueues:
 *
 *	Try to flush a buffer in the dirty queue.  We must be careful to
 *	free up B_INVAL buffers instead of write them, which NFS is 
 *	particularly sensitive to.
 */
static int flushwithdeps = 0;
SYSCTL_INT(_vfs, OID_AUTO, flushwithdeps, CTLFLAG_RW, &flushwithdeps,
    0, "Number of buffers flushed with dependecies that require rollbacks");

static int
flushbufqueues(struct vnode *lvp, struct bufdomain *bd, int target,
    int flushdeps)
{
	struct bufqueue *bq;
	struct buf *sentinel;
	struct vnode *vp;
	struct mount *mp;
	struct buf *bp;
	int hasdeps;
	int flushed;
	int error;
	bool unlock;

	flushed = 0;
	bq = &bd->bd_dirtyq;
	bp = NULL;
	sentinel = malloc(sizeof(struct buf), M_TEMP, M_WAITOK | M_ZERO);
	sentinel->b_qindex = QUEUE_SENTINEL;
	BQ_LOCK(bq);
	TAILQ_INSERT_HEAD(&bq->bq_queue, sentinel, b_freelist);
	BQ_UNLOCK(bq);
	while (flushed != target) {
		maybe_yield();
		BQ_LOCK(bq);
		bp = TAILQ_NEXT(sentinel, b_freelist);
		if (bp != NULL) {
			TAILQ_REMOVE(&bq->bq_queue, sentinel, b_freelist);
			TAILQ_INSERT_AFTER(&bq->bq_queue, bp, sentinel,
			    b_freelist);
		} else {
			BQ_UNLOCK(bq);
			break;
		}
		/*
		 * Skip sentinels inserted by other invocations of the
		 * flushbufqueues(), taking care to not reorder them.
		 *
		 * Only flush the buffers that belong to the
		 * vnode locked by the curthread.
		 */
		if (bp->b_qindex == QUEUE_SENTINEL || (lvp != NULL &&
		    bp->b_vp != lvp)) {
			BQ_UNLOCK(bq);
			continue;
		}
		error = BUF_LOCK(bp, LK_EXCLUSIVE | LK_NOWAIT, NULL);
		BQ_UNLOCK(bq);
		if (error != 0)
			continue;

		/*
		 * BKGRDINPROG can only be set with the buf and bufobj
		 * locks both held.  We tolerate a race to clear it here.
		 */
		if ((bp->b_vflags & BV_BKGRDINPROG) != 0 ||
		    (bp->b_flags & B_DELWRI) == 0) {
			BUF_UNLOCK(bp);
			continue;
		}
		if (bp->b_flags & B_INVAL) {
			bremfreef(bp);
			brelse(bp);
			flushed++;
			continue;
		}

		if (!LIST_EMPTY(&bp->b_dep) && buf_countdeps(bp, 0)) {
			if (flushdeps == 0) {
				BUF_UNLOCK(bp);
				continue;
			}
			hasdeps = 1;
		} else
			hasdeps = 0;
		/*
		 * We must hold the lock on a vnode before writing
		 * one of its buffers. Otherwise we may confuse, or
		 * in the case of a snapshot vnode, deadlock the
		 * system.
		 *
		 * The lock order here is the reverse of the normal
		 * of vnode followed by buf lock.  This is ok because
		 * the NOWAIT will prevent deadlock.
		 */
		vp = bp->b_vp;
		if (vn_start_write(vp, &mp, V_NOWAIT) != 0) {
			BUF_UNLOCK(bp);
			continue;
		}
		if (lvp == NULL) {
			unlock = true;
			error = vn_lock(vp, LK_EXCLUSIVE | LK_NOWAIT);
		} else {
			ASSERT_VOP_LOCKED(vp, "getbuf");
			unlock = false;
			error = VOP_ISLOCKED(vp) == LK_EXCLUSIVE ? 0 :
			    vn_lock(vp, LK_TRYUPGRADE);
		}
		if (error == 0) {
			CTR3(KTR_BUF, "flushbufqueue(%p) vp %p flags %X",
			    bp, bp->b_vp, bp->b_flags);
			if (curproc == bufdaemonproc) {
				vfs_bio_awrite(bp);
			} else {
				bremfree(bp);
				bwrite(bp);
				counter_u64_add(notbufdflushes, 1);
			}
			vn_finished_write(mp);
			if (unlock)
				VOP_UNLOCK(vp, 0);
			flushwithdeps += hasdeps;
			flushed++;

			/*
			 * Sleeping on runningbufspace while holding
			 * vnode lock leads to deadlock.
			 */
			if (curproc == bufdaemonproc &&
			    runningbufspace > hirunningspace)
				waitrunningbufspace();
			continue;
		}
		vn_finished_write(mp);
		BUF_UNLOCK(bp);
	}
	BQ_LOCK(bq);
	TAILQ_REMOVE(&bq->bq_queue, sentinel, b_freelist);
	BQ_UNLOCK(bq);
	free(sentinel, M_TEMP);
	return (flushed);
}

/*
 * Check to see if a block is currently memory resident.
 */
struct buf *
incore(struct bufobj *bo, daddr_t blkno)
{
	struct buf *bp;

	BO_RLOCK(bo);
	bp = gbincore(bo, blkno);
	BO_RUNLOCK(bo);
	return (bp);
}

/*
 * Returns true if no I/O is needed to access the
 * associated VM object.  This is like incore except
 * it also hunts around in the VM system for the data.
 */

static int
inmem(struct vnode * vp, daddr_t blkno)
{
	vm_object_t obj;
	vm_offset_t toff, tinc, size;
	vm_page_t m;
	vm_ooffset_t off;

	ASSERT_VOP_LOCKED(vp, "inmem");

	if (incore(&vp->v_bufobj, blkno))
		return 1;
	if (vp->v_mount == NULL)
		return 0;
	obj = vp->v_object;
	if (obj == NULL)
		return (0);

	size = PAGE_SIZE;
	if (size > vp->v_mount->mnt_stat.f_iosize)
		size = vp->v_mount->mnt_stat.f_iosize;
	off = (vm_ooffset_t)blkno * (vm_ooffset_t)vp->v_mount->mnt_stat.f_iosize;

	VM_OBJECT_RLOCK(obj);
	for (toff = 0; toff < vp->v_mount->mnt_stat.f_iosize; toff += tinc) {
		m = vm_page_lookup(obj, OFF_TO_IDX(off + toff));
		if (!m)
			goto notinmem;
		tinc = size;
		if (tinc > PAGE_SIZE - ((toff + off) & PAGE_MASK))
			tinc = PAGE_SIZE - ((toff + off) & PAGE_MASK);
		if (vm_page_is_valid(m,
		    (vm_offset_t) ((toff + off) & PAGE_MASK), tinc) == 0)
			goto notinmem;
	}
	VM_OBJECT_RUNLOCK(obj);
	return 1;

notinmem:
	VM_OBJECT_RUNLOCK(obj);
	return (0);
}

/*
 * Set the dirty range for a buffer based on the status of the dirty
 * bits in the pages comprising the buffer.  The range is limited
 * to the size of the buffer.
 *
 * Tell the VM system that the pages associated with this buffer
 * are clean.  This is used for delayed writes where the data is
 * going to go to disk eventually without additional VM intevention.
 *
 * Note that while we only really need to clean through to b_bcount, we
 * just go ahead and clean through to b_bufsize.
 */
static void
vfs_clean_pages_dirty_buf(struct buf *bp)
{
	vm_ooffset_t foff, noff, eoff;
	vm_page_t m;
	int i;

	if ((bp->b_flags & B_VMIO) == 0 || bp->b_bufsize == 0)
		return;

	foff = bp->b_offset;
	KASSERT(bp->b_offset != NOOFFSET,
	    ("vfs_clean_pages_dirty_buf: no buffer offset"));

	VM_OBJECT_WLOCK(bp->b_bufobj->bo_object);
	vfs_drain_busy_pages(bp);
	vfs_setdirty_locked_object(bp);
	for (i = 0; i < bp->b_npages; i++) {
		noff = (foff + PAGE_SIZE) & ~(off_t)PAGE_MASK;
		eoff = noff;
		if (eoff > bp->b_offset + bp->b_bufsize)
			eoff = bp->b_offset + bp->b_bufsize;
		m = bp->b_pages[i];
		vfs_page_set_validclean(bp, foff, m);
		/* vm_page_clear_dirty(m, foff & PAGE_MASK, eoff - foff); */
		foff = noff;
	}
	VM_OBJECT_WUNLOCK(bp->b_bufobj->bo_object);
}

static void
vfs_setdirty_locked_object(struct buf *bp)
{
	vm_object_t object;
	int i;

	object = bp->b_bufobj->bo_object;
	VM_OBJECT_ASSERT_WLOCKED(object);

	/*
	 * We qualify the scan for modified pages on whether the
	 * object has been flushed yet.
	 */
	if ((object->flags & OBJ_MIGHTBEDIRTY) != 0) {
		vm_offset_t boffset;
		vm_offset_t eoffset;

		/*
		 * test the pages to see if they have been modified directly
		 * by users through the VM system.
		 */
		for (i = 0; i < bp->b_npages; i++)
			vm_page_test_dirty(bp->b_pages[i]);

		/*
		 * Calculate the encompassing dirty range, boffset and eoffset,
		 * (eoffset - boffset) bytes.
		 */

		for (i = 0; i < bp->b_npages; i++) {
			if (bp->b_pages[i]->dirty)
				break;
		}
		boffset = (i << PAGE_SHIFT) - (bp->b_offset & PAGE_MASK);

		for (i = bp->b_npages - 1; i >= 0; --i) {
			if (bp->b_pages[i]->dirty) {
				break;
			}
		}
		eoffset = ((i + 1) << PAGE_SHIFT) - (bp->b_offset & PAGE_MASK);

		/*
		 * Fit it to the buffer.
		 */

		if (eoffset > bp->b_bcount)
			eoffset = bp->b_bcount;

		/*
		 * If we have a good dirty range, merge with the existing
		 * dirty range.
		 */

		if (boffset < eoffset) {
			if (bp->b_dirtyoff > boffset)
				bp->b_dirtyoff = boffset;
			if (bp->b_dirtyend < eoffset)
				bp->b_dirtyend = eoffset;
		}
	}
}

/*
 * Allocate the KVA mapping for an existing buffer.
 * If an unmapped buffer is provided but a mapped buffer is requested, take
 * also care to properly setup mappings between pages and KVA.
 */
static void
bp_unmapped_get_kva(struct buf *bp, daddr_t blkno, int size, int gbflags)
{
	int bsize, maxsize, need_mapping, need_kva;
	off_t offset;

	need_mapping = bp->b_data == unmapped_buf &&
	    (gbflags & GB_UNMAPPED) == 0;
	need_kva = bp->b_kvabase == unmapped_buf &&
	    bp->b_data == unmapped_buf &&
	    (gbflags & GB_KVAALLOC) != 0;
	if (!need_mapping && !need_kva)
		return;

	BUF_CHECK_UNMAPPED(bp);

	if (need_mapping && bp->b_kvabase != unmapped_buf) {
		/*
		 * Buffer is not mapped, but the KVA was already
		 * reserved at the time of the instantiation.  Use the
		 * allocated space.
		 */
		goto has_addr;
	}

	/*
	 * Calculate the amount of the address space we would reserve
	 * if the buffer was mapped.
	 */
	bsize = vn_isdisk(bp->b_vp, NULL) ? DEV_BSIZE : bp->b_bufobj->bo_bsize;
	KASSERT(bsize != 0, ("bsize == 0, check bo->bo_bsize"));
	offset = blkno * bsize;
	maxsize = size + (offset & PAGE_MASK);
	maxsize = imax(maxsize, bsize);

	while (bufkva_alloc(bp, maxsize, gbflags) != 0) {
		if ((gbflags & GB_NOWAIT_BD) != 0) {
			/*
			 * XXXKIB: defragmentation cannot
			 * succeed, not sure what else to do.
			 */
			panic("GB_NOWAIT_BD and GB_UNMAPPED %p", bp);
		}
		counter_u64_add(mappingrestarts, 1);
		bufspace_wait(bufdomain(bp), bp->b_vp, gbflags, 0, 0);
	}
has_addr:
	if (need_mapping) {
		/* b_offset is handled by bpmap_qenter. */
		bp->b_data = bp->b_kvabase;
		BUF_CHECK_MAPPED(bp);
		bpmap_qenter(bp);
	}
}

struct buf *
getblk(struct vnode *vp, daddr_t blkno, int size, int slpflag, int slptimeo,
    int flags)
{
	struct buf *bp;
	int error;

	error = getblkx(vp, blkno, size, slpflag, slptimeo, flags, &bp);
	if (error != 0)
		return (NULL);
	return (bp);
}

/*
 *	getblkx:
 *
 *	Get a block given a specified block and offset into a file/device.
 *	The buffers B_DONE bit will be cleared on return, making it almost
 * 	ready for an I/O initiation.  B_INVAL may or may not be set on 
 *	return.  The caller should clear B_INVAL prior to initiating a
 *	READ.
 *
 *	For a non-VMIO buffer, B_CACHE is set to the opposite of B_INVAL for
 *	an existing buffer.
 *
 *	For a VMIO buffer, B_CACHE is modified according to the backing VM.
 *	If getblk()ing a previously 0-sized invalid buffer, B_CACHE is set
 *	and then cleared based on the backing VM.  If the previous buffer is
 *	non-0-sized but invalid, B_CACHE will be cleared.
 *
 *	If getblk() must create a new buffer, the new buffer is returned with
 *	both B_INVAL and B_CACHE clear unless it is a VMIO buffer, in which
 *	case it is returned with B_INVAL clear and B_CACHE set based on the
 *	backing VM.
 *
 *	getblk() also forces a bwrite() for any B_DELWRI buffer whos
 *	B_CACHE bit is clear.
 *	
 *	What this means, basically, is that the caller should use B_CACHE to
 *	determine whether the buffer is fully valid or not and should clear
 *	B_INVAL prior to issuing a read.  If the caller intends to validate
 *	the buffer by loading its data area with something, the caller needs
 *	to clear B_INVAL.  If the caller does this without issuing an I/O, 
 *	the caller should set B_CACHE ( as an optimization ), else the caller
 *	should issue the I/O and biodone() will set B_CACHE if the I/O was
 *	a write attempt or if it was a successful read.  If the caller 
 *	intends to issue a READ, the caller must clear B_INVAL and BIO_ERROR
 *	prior to issuing the READ.  biodone() will *not* clear B_INVAL.
 */
int
getblkx(struct vnode *vp, daddr_t blkno, int size, int slpflag, int slptimeo,
    int flags, struct buf **bpp)
{
	struct buf *bp;
	struct bufobj *bo;
	daddr_t d_blkno;
	int bsize, error, maxsize, vmio;
	off_t offset;

	CTR3(KTR_BUF, "getblk(%p, %ld, %d)", vp, (long)blkno, size);
	KASSERT((flags & (GB_UNMAPPED | GB_KVAALLOC)) != GB_KVAALLOC,
	    ("GB_KVAALLOC only makes sense with GB_UNMAPPED"));
	ASSERT_VOP_LOCKED(vp, "getblk");
	if (size > maxbcachebuf)
		panic("getblk: size(%d) > maxbcachebuf(%d)\n", size,
		    maxbcachebuf);
	if (!unmapped_buf_allowed)
		flags &= ~(GB_UNMAPPED | GB_KVAALLOC);

	bo = &vp->v_bufobj;
	d_blkno = blkno;
loop:
	BO_RLOCK(bo);
	bp = gbincore(bo, blkno);
	if (bp != NULL) {
		int lockflags;
		/*
		 * Buffer is in-core.  If the buffer is not busy nor managed,
		 * it must be on a queue.
		 */
		lockflags = LK_EXCLUSIVE | LK_SLEEPFAIL | LK_INTERLOCK;

		if ((flags & GB_LOCK_NOWAIT) != 0)
			lockflags |= LK_NOWAIT;

		error = BUF_TIMELOCK(bp, lockflags,
		    BO_LOCKPTR(bo), "getblk", slpflag, slptimeo);

		/*
		 * If we slept and got the lock we have to restart in case
		 * the buffer changed identities.
		 */
		if (error == ENOLCK)
			goto loop;
		/* We timed out or were interrupted. */
		else if (error != 0)
			return (error);
		/* If recursed, assume caller knows the rules. */
		else if (BUF_LOCKRECURSED(bp))
			goto end;

		/*
		 * The buffer is locked.  B_CACHE is cleared if the buffer is 
		 * invalid.  Otherwise, for a non-VMIO buffer, B_CACHE is set
		 * and for a VMIO buffer B_CACHE is adjusted according to the
		 * backing VM cache.
		 */
		if (bp->b_flags & B_INVAL)
			bp->b_flags &= ~B_CACHE;
		else if ((bp->b_flags & (B_VMIO | B_INVAL)) == 0)
			bp->b_flags |= B_CACHE;
		if (bp->b_flags & B_MANAGED)
			MPASS(bp->b_qindex == QUEUE_NONE);
		else
			bremfree(bp);

		/*
		 * check for size inconsistencies for non-VMIO case.
		 */
		if (bp->b_bcount != size) {
			if ((bp->b_flags & B_VMIO) == 0 ||
			    (size > bp->b_kvasize)) {
				if (bp->b_flags & B_DELWRI) {
					bp->b_flags |= B_NOCACHE;
					bwrite(bp);
				} else {
					if (LIST_EMPTY(&bp->b_dep)) {
						bp->b_flags |= B_RELBUF;
						brelse(bp);
					} else {
						bp->b_flags |= B_NOCACHE;
						bwrite(bp);
					}
				}
				goto loop;
			}
		}

		/*
		 * Handle the case of unmapped buffer which should
		 * become mapped, or the buffer for which KVA
		 * reservation is requested.
		 */
		bp_unmapped_get_kva(bp, blkno, size, flags);

		/*
		 * If the size is inconsistent in the VMIO case, we can resize
		 * the buffer.  This might lead to B_CACHE getting set or
		 * cleared.  If the size has not changed, B_CACHE remains
		 * unchanged from its previous state.
		 */
		allocbuf(bp, size);

		KASSERT(bp->b_offset != NOOFFSET, 
		    ("getblk: no buffer offset"));

		/*
		 * A buffer with B_DELWRI set and B_CACHE clear must
		 * be committed before we can return the buffer in
		 * order to prevent the caller from issuing a read
		 * ( due to B_CACHE not being set ) and overwriting
		 * it.
		 *
		 * Most callers, including NFS and FFS, need this to
		 * operate properly either because they assume they
		 * can issue a read if B_CACHE is not set, or because
		 * ( for example ) an uncached B_DELWRI might loop due 
		 * to softupdates re-dirtying the buffer.  In the latter
		 * case, B_CACHE is set after the first write completes,
		 * preventing further loops.
		 * NOTE!  b*write() sets B_CACHE.  If we cleared B_CACHE
		 * above while extending the buffer, we cannot allow the
		 * buffer to remain with B_CACHE set after the write
		 * completes or it will represent a corrupt state.  To
		 * deal with this we set B_NOCACHE to scrap the buffer
		 * after the write.
		 *
		 * We might be able to do something fancy, like setting
		 * B_CACHE in bwrite() except if B_DELWRI is already set,
		 * so the below call doesn't set B_CACHE, but that gets real
		 * confusing.  This is much easier.
		 */

		if ((bp->b_flags & (B_CACHE|B_DELWRI)) == B_DELWRI) {
			bp->b_flags |= B_NOCACHE;
			bwrite(bp);
			goto loop;
		}
		bp->b_flags &= ~B_DONE;
	} else {
		/*
		 * Buffer is not in-core, create new buffer.  The buffer
		 * returned by getnewbuf() is locked.  Note that the returned
		 * buffer is also considered valid (not marked B_INVAL).
		 */
		BO_RUNLOCK(bo);
		/*
		 * If the user does not want us to create the buffer, bail out
		 * here.
		 */
		if (flags & GB_NOCREAT)
			return (EEXIST);
		if (bdomain[bo->bo_domain].bd_freebuffers == 0 &&
		    TD_IS_IDLETHREAD(curthread))
			return (EBUSY);

		bsize = vn_isdisk(vp, NULL) ? DEV_BSIZE : bo->bo_bsize;
		KASSERT(bsize != 0, ("bsize == 0, check bo->bo_bsize"));
		offset = blkno * bsize;
		vmio = vp->v_object != NULL;
		if (vmio) {
			maxsize = size + (offset & PAGE_MASK);
		} else {
			maxsize = size;
			/* Do not allow non-VMIO notmapped buffers. */
			flags &= ~(GB_UNMAPPED | GB_KVAALLOC);
		}
		maxsize = imax(maxsize, bsize);
		if ((flags & GB_NOSPARSE) != 0 && vmio &&
		    !vn_isdisk(vp, NULL)) {
			error = VOP_BMAP(vp, blkno, NULL, &d_blkno, 0, 0);
			KASSERT(error != EOPNOTSUPP,
			    ("GB_NOSPARSE from fs not supporting bmap, vp %p",
			    vp));
			if (error != 0)
				return (error);
			if (d_blkno == -1)
				return (EJUSTRETURN);
		}

		bp = getnewbuf(vp, slpflag, slptimeo, maxsize, flags);
		if (bp == NULL) {
			if (slpflag || slptimeo)
				return (ETIMEDOUT);
			/*
			 * XXX This is here until the sleep path is diagnosed
			 * enough to work under very low memory conditions.
			 *
			 * There's an issue on low memory, 4BSD+non-preempt
			 * systems (eg MIPS routers with 32MB RAM) where buffer
			 * exhaustion occurs without sleeping for buffer
			 * reclaimation.  This just sticks in a loop and
			 * constantly attempts to allocate a buffer, which
			 * hits exhaustion and tries to wakeup bufdaemon.
			 * This never happens because we never yield.
			 *
			 * The real solution is to identify and fix these cases
			 * so we aren't effectively busy-waiting in a loop
			 * until the reclaimation path has cycles to run.
			 */
			kern_yield(PRI_USER);
			goto loop;
		}

		/*
		 * This code is used to make sure that a buffer is not
		 * created while the getnewbuf routine is blocked.
		 * This can be a problem whether the vnode is locked or not.
		 * If the buffer is created out from under us, we have to
		 * throw away the one we just created.
		 *
		 * Note: this must occur before we associate the buffer
		 * with the vp especially considering limitations in
		 * the splay tree implementation when dealing with duplicate
		 * lblkno's.
		 */
		BO_LOCK(bo);
		if (gbincore(bo, blkno)) {
			BO_UNLOCK(bo);
			bp->b_flags |= B_INVAL;
			bufspace_release(bufdomain(bp), maxsize);
			brelse(bp);
			goto loop;
		}

		/*
		 * Insert the buffer into the hash, so that it can
		 * be found by incore.
		 */
		bp->b_lblkno = blkno;
		bp->b_blkno = d_blkno;
		bp->b_offset = offset;
		bgetvp(vp, bp);
		BO_UNLOCK(bo);

		/*
		 * set B_VMIO bit.  allocbuf() the buffer bigger.  Since the
		 * buffer size starts out as 0, B_CACHE will be set by
		 * allocbuf() for the VMIO case prior to it testing the
		 * backing store for validity.
		 */

		if (vmio) {
			bp->b_flags |= B_VMIO;
			KASSERT(vp->v_object == bp->b_bufobj->bo_object,
			    ("ARGH! different b_bufobj->bo_object %p %p %p\n",
			    bp, vp->v_object, bp->b_bufobj->bo_object));
		} else {
			bp->b_flags &= ~B_VMIO;
			KASSERT(bp->b_bufobj->bo_object == NULL,
			    ("ARGH! has b_bufobj->bo_object %p %p\n",
			    bp, bp->b_bufobj->bo_object));
			BUF_CHECK_MAPPED(bp);
		}

		allocbuf(bp, size);
		bufspace_release(bufdomain(bp), maxsize);
		bp->b_flags &= ~B_DONE;
	}
	CTR4(KTR_BUF, "getblk(%p, %ld, %d) = %p", vp, (long)blkno, size, bp);
	BUF_ASSERT_HELD(bp);
end:
	buf_track(bp, __func__);
	KASSERT(bp->b_bufobj == bo,
	    ("bp %p wrong b_bufobj %p should be %p", bp, bp->b_bufobj, bo));
	*bpp = bp;
	return (0);
}

/*
 * Get an empty, disassociated buffer of given size.  The buffer is initially
 * set to B_INVAL.
 */
struct buf *
geteblk(int size, int flags)
{
	struct buf *bp;
	int maxsize;

	maxsize = (size + BKVAMASK) & ~BKVAMASK;
	while ((bp = getnewbuf(NULL, 0, 0, maxsize, flags)) == NULL) {
		if ((flags & GB_NOWAIT_BD) &&
		    (curthread->td_pflags & TDP_BUFNEED) != 0)
			return (NULL);
	}
	allocbuf(bp, size);
	bufspace_release(bufdomain(bp), maxsize);
	bp->b_flags |= B_INVAL;	/* b_dep cleared by getnewbuf() */
	BUF_ASSERT_HELD(bp);
	return (bp);
}

/*
 * Truncate the backing store for a non-vmio buffer.
 */
static void
vfs_nonvmio_truncate(struct buf *bp, int newbsize)
{

	if (bp->b_flags & B_MALLOC) {
		/*
		 * malloced buffers are not shrunk
		 */
		if (newbsize == 0) {
			bufmallocadjust(bp, 0);
			free(bp->b_data, M_BIOBUF);
			bp->b_data = bp->b_kvabase;
			bp->b_flags &= ~B_MALLOC;
		}
		return;
	}
	vm_hold_free_pages(bp, newbsize);
	bufspace_adjust(bp, newbsize);
}

/*
 * Extend the backing for a non-VMIO buffer.
 */
static void
vfs_nonvmio_extend(struct buf *bp, int newbsize)
{
	caddr_t origbuf;
	int origbufsize;

	/*
	 * We only use malloced memory on the first allocation.
	 * and revert to page-allocated memory when the buffer
	 * grows.
	 *
	 * There is a potential smp race here that could lead
	 * to bufmallocspace slightly passing the max.  It
	 * is probably extremely rare and not worth worrying
	 * over.
	 */
	if (bp->b_bufsize == 0 && newbsize <= PAGE_SIZE/2 &&
	    bufmallocspace < maxbufmallocspace) {
		bp->b_data = malloc(newbsize, M_BIOBUF, M_WAITOK);
		bp->b_flags |= B_MALLOC;
		bufmallocadjust(bp, newbsize);
		return;
	}

	/*
	 * If the buffer is growing on its other-than-first
	 * allocation then we revert to the page-allocation
	 * scheme.
	 */
	origbuf = NULL;
	origbufsize = 0;
	if (bp->b_flags & B_MALLOC) {
		origbuf = bp->b_data;
		origbufsize = bp->b_bufsize;
		bp->b_data = bp->b_kvabase;
		bufmallocadjust(bp, 0);
		bp->b_flags &= ~B_MALLOC;
		newbsize = round_page(newbsize);
	}
	vm_hold_load_pages(bp, (vm_offset_t) bp->b_data + bp->b_bufsize,
	    (vm_offset_t) bp->b_data + newbsize);
	if (origbuf != NULL) {
		bcopy(origbuf, bp->b_data, origbufsize);
		free(origbuf, M_BIOBUF);
	}
	bufspace_adjust(bp, newbsize);
}

/*
 * This code constitutes the buffer memory from either anonymous system
 * memory (in the case of non-VMIO operations) or from an associated
 * VM object (in the case of VMIO operations).  This code is able to
 * resize a buffer up or down.
 *
 * Note that this code is tricky, and has many complications to resolve
 * deadlock or inconsistent data situations.  Tread lightly!!! 
 * There are B_CACHE and B_DELWRI interactions that must be dealt with by 
 * the caller.  Calling this code willy nilly can result in the loss of data.
 *
 * allocbuf() only adjusts B_CACHE for VMIO buffers.  getblk() deals with
 * B_CACHE for the non-VMIO case.
 */
int
allocbuf(struct buf *bp, int size)
{
	int newbsize;

	BUF_ASSERT_HELD(bp);

	if (bp->b_bcount == size)
		return (1);

	if (bp->b_kvasize != 0 && bp->b_kvasize < size)
		panic("allocbuf: buffer too small");

	newbsize = roundup2(size, DEV_BSIZE);
	if ((bp->b_flags & B_VMIO) == 0) {
		if ((bp->b_flags & B_MALLOC) == 0)
			newbsize = round_page(newbsize);
		/*
		 * Just get anonymous memory from the kernel.  Don't
		 * mess with B_CACHE.
		 */
		if (newbsize < bp->b_bufsize)
			vfs_nonvmio_truncate(bp, newbsize);
		else if (newbsize > bp->b_bufsize)
			vfs_nonvmio_extend(bp, newbsize);
	} else {
		int desiredpages;

		desiredpages = (size == 0) ? 0 :
		    num_pages((bp->b_offset & PAGE_MASK) + newbsize);

		if (bp->b_flags & B_MALLOC)
			panic("allocbuf: VMIO buffer can't be malloced");
		/*
		 * Set B_CACHE initially if buffer is 0 length or will become
		 * 0-length.
		 */
		if (size == 0 || bp->b_bufsize == 0)
			bp->b_flags |= B_CACHE;

		if (newbsize < bp->b_bufsize)
			vfs_vmio_truncate(bp, desiredpages);
		/* XXX This looks as if it should be newbsize > b_bufsize */
		else if (size > bp->b_bcount)
			vfs_vmio_extend(bp, desiredpages, size);
		bufspace_adjust(bp, newbsize);
	}
	bp->b_bcount = size;		/* requested buffer size. */
	return (1);
}

extern int inflight_transient_maps;

static struct bio_queue nondump_bios;

void
biodone(struct bio *bp)
{
	struct mtx *mtxp;
	void (*done)(struct bio *);
	vm_offset_t start, end;

	biotrack(bp, __func__);

	/*
	 * Avoid completing I/O when dumping after a panic since that may
	 * result in a deadlock in the filesystem or pager code.  Note that
	 * this doesn't affect dumps that were started manually since we aim
	 * to keep the system usable after it has been resumed.
	 */
	if (__predict_false(dumping && SCHEDULER_STOPPED())) {
		TAILQ_INSERT_HEAD(&nondump_bios, bp, bio_queue);
		return;
	}
	if ((bp->bio_flags & BIO_TRANSIENT_MAPPING) != 0) {
		bp->bio_flags &= ~BIO_TRANSIENT_MAPPING;
		bp->bio_flags |= BIO_UNMAPPED;
		start = trunc_page((vm_offset_t)bp->bio_data);
		end = round_page((vm_offset_t)bp->bio_data + bp->bio_length);
		bp->bio_data = unmapped_buf;
		pmap_qremove(start, atop(end - start));
		vmem_free(transient_arena, start, end - start);
		atomic_add_int(&inflight_transient_maps, -1);
	}
	done = bp->bio_done;
	if (done == NULL) {
		mtxp = mtx_pool_find(mtxpool_sleep, bp);
		mtx_lock(mtxp);
		bp->bio_flags |= BIO_DONE;
		wakeup(bp);
		mtx_unlock(mtxp);
	} else
		done(bp);
}

/*
 * Wait for a BIO to finish.
 */
int
biowait(struct bio *bp, const char *wchan)
{
	struct mtx *mtxp;

	mtxp = mtx_pool_find(mtxpool_sleep, bp);
	mtx_lock(mtxp);
	while ((bp->bio_flags & BIO_DONE) == 0)
		msleep(bp, mtxp, PRIBIO, wchan, 0);
	mtx_unlock(mtxp);
	if (bp->bio_error != 0)
		return (bp->bio_error);
	if (!(bp->bio_flags & BIO_ERROR))
		return (0);
	return (EIO);
}

void
biofinish(struct bio *bp, struct devstat *stat, int error)
{
	
	if (error) {
		bp->bio_error = error;
		bp->bio_flags |= BIO_ERROR;
	}
	if (stat != NULL)
		devstat_end_transaction_bio(stat, bp);
	biodone(bp);
}

#if defined(BUF_TRACKING) || defined(FULL_BUF_TRACKING)
void
biotrack_buf(struct bio *bp, const char *location)
{

	buf_track(bp->bio_track_bp, location);
}
#endif

/*
 *	bufwait:
 *
 *	Wait for buffer I/O completion, returning error status.  The buffer
 *	is left locked and B_DONE on return.  B_EINTR is converted into an EINTR
 *	error and cleared.
 */
int
bufwait(struct buf *bp)
{
	if (bp->b_iocmd == BIO_READ)
		bwait(bp, PRIBIO, "biord");
	else
		bwait(bp, PRIBIO, "biowr");
	if (bp->b_flags & B_EINTR) {
		bp->b_flags &= ~B_EINTR;
		return (EINTR);
	}
	if (bp->b_ioflags & BIO_ERROR) {
		return (bp->b_error ? bp->b_error : EIO);
	} else {
		return (0);
	}
}

/*
 *	bufdone:
 *
 *	Finish I/O on a buffer, optionally calling a completion function.
 *	This is usually called from an interrupt so process blocking is
 *	not allowed.
 *
 *	biodone is also responsible for setting B_CACHE in a B_VMIO bp.
 *	In a non-VMIO bp, B_CACHE will be set on the next getblk() 
 *	assuming B_INVAL is clear.
 *
 *	For the VMIO case, we set B_CACHE if the op was a read and no
 *	read error occurred, or if the op was a write.  B_CACHE is never
 *	set if the buffer is invalid or otherwise uncacheable.
 *
 *	biodone does not mess with B_INVAL, allowing the I/O routine or the
 *	initiator to leave B_INVAL set to brelse the buffer out of existence
 *	in the biodone routine.
 */
void
bufdone(struct buf *bp)
{
	struct bufobj *dropobj;
	void    (*biodone)(struct buf *);

	buf_track(bp, __func__);
	CTR3(KTR_BUF, "bufdone(%p) vp %p flags %X", bp, bp->b_vp, bp->b_flags);
	dropobj = NULL;

	KASSERT(!(bp->b_flags & B_DONE), ("biodone: bp %p already done", bp));
	BUF_ASSERT_HELD(bp);

	runningbufwakeup(bp);
	if (bp->b_iocmd == BIO_WRITE)
		dropobj = bp->b_bufobj;
	/* call optional completion function if requested */
	if (bp->b_iodone != NULL) {
		biodone = bp->b_iodone;
		bp->b_iodone = NULL;
		(*biodone) (bp);
		if (dropobj)
			bufobj_wdrop(dropobj);
		return;
	}
	if (bp->b_flags & B_VMIO) {
		/*
		 * Set B_CACHE if the op was a normal read and no error
		 * occurred.  B_CACHE is set for writes in the b*write()
		 * routines.
		 */
		if (bp->b_iocmd == BIO_READ &&
		    !(bp->b_flags & (B_INVAL|B_NOCACHE)) &&
		    !(bp->b_ioflags & BIO_ERROR))
			bp->b_flags |= B_CACHE;
		vfs_vmio_iodone(bp);
	}
	if (!LIST_EMPTY(&bp->b_dep))
		buf_complete(bp);
	if ((bp->b_flags & B_CKHASH) != 0) {
		KASSERT(bp->b_iocmd == BIO_READ,
		    ("bufdone: b_iocmd %d not BIO_READ", bp->b_iocmd));
		KASSERT(buf_mapped(bp), ("bufdone: bp %p not mapped", bp));
		(*bp->b_ckhashcalc)(bp);
	}
	/*
	 * For asynchronous completions, release the buffer now. The brelse
	 * will do a wakeup there if necessary - so no need to do a wakeup
	 * here in the async case. The sync case always needs to do a wakeup.
	 */
	if (bp->b_flags & B_ASYNC) {
		if ((bp->b_flags & (B_NOCACHE | B_INVAL | B_RELBUF)) ||
		    (bp->b_ioflags & BIO_ERROR))
			brelse(bp);
		else
			bqrelse(bp);
	} else
		bdone(bp);
	if (dropobj)
		bufobj_wdrop(dropobj);
}

/*
 * This routine is called in lieu of iodone in the case of
 * incomplete I/O.  This keeps the busy status for pages
 * consistent.
 */
void
vfs_unbusy_pages(struct buf *bp)
{
	int i;
	vm_object_t obj;
	vm_page_t m;

	runningbufwakeup(bp);
	if (!(bp->b_flags & B_VMIO))
		return;

	obj = bp->b_bufobj->bo_object;
	VM_OBJECT_WLOCK(obj);
	for (i = 0; i < bp->b_npages; i++) {
		m = bp->b_pages[i];
		if (m == bogus_page) {
			m = vm_page_lookup(obj, OFF_TO_IDX(bp->b_offset) + i);
			if (!m)
				panic("vfs_unbusy_pages: page missing\n");
			bp->b_pages[i] = m;
			if (buf_mapped(bp)) {
				BUF_CHECK_MAPPED(bp);
				pmap_qenter(trunc_page((vm_offset_t)bp->b_data),
				    bp->b_pages, bp->b_npages);
			} else
				BUF_CHECK_UNMAPPED(bp);
		}
		vm_page_sunbusy(m);
	}
	vm_object_pip_wakeupn(obj, bp->b_npages);
	VM_OBJECT_WUNLOCK(obj);
}

/*
 * vfs_page_set_valid:
 *
 *	Set the valid bits in a page based on the supplied offset.   The
 *	range is restricted to the buffer's size.
 *
 *	This routine is typically called after a read completes.
 */
static void
vfs_page_set_valid(struct buf *bp, vm_ooffset_t off, vm_page_t m)
{
	vm_ooffset_t eoff;

	/*
	 * Compute the end offset, eoff, such that [off, eoff) does not span a
	 * page boundary and eoff is not greater than the end of the buffer.
	 * The end of the buffer, in this case, is our file EOF, not the
	 * allocation size of the buffer.
	 */
	eoff = (off + PAGE_SIZE) & ~(vm_ooffset_t)PAGE_MASK;
	if (eoff > bp->b_offset + bp->b_bcount)
		eoff = bp->b_offset + bp->b_bcount;

	/*
	 * Set valid range.  This is typically the entire buffer and thus the
	 * entire page.
	 */
	if (eoff > off)
		vm_page_set_valid_range(m, off & PAGE_MASK, eoff - off);
}

/*
 * vfs_page_set_validclean:
 *
 *	Set the valid bits and clear the dirty bits in a page based on the
 *	supplied offset.   The range is restricted to the buffer's size.
 */
static void
vfs_page_set_validclean(struct buf *bp, vm_ooffset_t off, vm_page_t m)
{
	vm_ooffset_t soff, eoff;

	/*
	 * Start and end offsets in buffer.  eoff - soff may not cross a
	 * page boundary or cross the end of the buffer.  The end of the
	 * buffer, in this case, is our file EOF, not the allocation size
	 * of the buffer.
	 */
	soff = off;
	eoff = (off + PAGE_SIZE) & ~(off_t)PAGE_MASK;
	if (eoff > bp->b_offset + bp->b_bcount)
		eoff = bp->b_offset + bp->b_bcount;

	/*
	 * Set valid range.  This is typically the entire buffer and thus the
	 * entire page.
	 */
	if (eoff > soff) {
		vm_page_set_validclean(
		    m,
		   (vm_offset_t) (soff & PAGE_MASK),
		   (vm_offset_t) (eoff - soff)
		);
	}
}

/*
 * Ensure that all buffer pages are not exclusive busied.  If any page is
 * exclusive busy, drain it.
 */
void
vfs_drain_busy_pages(struct buf *bp)
{
	vm_page_t m;
	int i, last_busied;

	VM_OBJECT_ASSERT_WLOCKED(bp->b_bufobj->bo_object);
	last_busied = 0;
	for (i = 0; i < bp->b_npages; i++) {
		m = bp->b_pages[i];
		if (vm_page_xbusied(m)) {
			for (; last_busied < i; last_busied++)
				vm_page_sbusy(bp->b_pages[last_busied]);
			while (vm_page_xbusied(m)) {
				vm_page_lock(m);
				VM_OBJECT_WUNLOCK(bp->b_bufobj->bo_object);
				vm_page_busy_sleep(m, "vbpage", true);
				VM_OBJECT_WLOCK(bp->b_bufobj->bo_object);
			}
		}
	}
	for (i = 0; i < last_busied; i++)
		vm_page_sunbusy(bp->b_pages[i]);
}

/*
 * This routine is called before a device strategy routine.
 * It is used to tell the VM system that paging I/O is in
 * progress, and treat the pages associated with the buffer
 * almost as being exclusive busy.  Also the object paging_in_progress
 * flag is handled to make sure that the object doesn't become
 * inconsistent.
 *
 * Since I/O has not been initiated yet, certain buffer flags
 * such as BIO_ERROR or B_INVAL may be in an inconsistent state
 * and should be ignored.
 */
void
vfs_busy_pages(struct buf *bp, int clear_modify)
{
	vm_object_t obj;
	vm_ooffset_t foff;
	vm_page_t m;
	int i;
	bool bogus;

	if (!(bp->b_flags & B_VMIO))
		return;

	obj = bp->b_bufobj->bo_object;
	foff = bp->b_offset;
	KASSERT(bp->b_offset != NOOFFSET,
	    ("vfs_busy_pages: no buffer offset"));
	VM_OBJECT_WLOCK(obj);
	vfs_drain_busy_pages(bp);
	if (bp->b_bufsize != 0)
		vfs_setdirty_locked_object(bp);
	bogus = false;
	for (i = 0; i < bp->b_npages; i++) {
		m = bp->b_pages[i];

		if ((bp->b_flags & B_CLUSTER) == 0) {
			vm_object_pip_add(obj, 1);
			vm_page_sbusy(m);
		}
		/*
		 * When readying a buffer for a read ( i.e
		 * clear_modify == 0 ), it is important to do
		 * bogus_page replacement for valid pages in 
		 * partially instantiated buffers.  Partially 
		 * instantiated buffers can, in turn, occur when
		 * reconstituting a buffer from its VM backing store
		 * base.  We only have to do this if B_CACHE is
		 * clear ( which causes the I/O to occur in the
		 * first place ).  The replacement prevents the read
		 * I/O from overwriting potentially dirty VM-backed
		 * pages.  XXX bogus page replacement is, uh, bogus.
		 * It may not work properly with small-block devices.
		 * We need to find a better way.
		 */
		if (clear_modify) {
			pmap_remove_write(m);
			vfs_page_set_validclean(bp, foff, m);
		} else if (m->valid == VM_PAGE_BITS_ALL &&
		    (bp->b_flags & B_CACHE) == 0) {
			bp->b_pages[i] = bogus_page;
			bogus = true;
		}
		foff = (foff + PAGE_SIZE) & ~(off_t)PAGE_MASK;
	}
	VM_OBJECT_WUNLOCK(obj);
	if (bogus && buf_mapped(bp)) {
		BUF_CHECK_MAPPED(bp);
		pmap_qenter(trunc_page((vm_offset_t)bp->b_data),
		    bp->b_pages, bp->b_npages);
	}
}

/*
 *	vfs_bio_set_valid:
 *
 *	Set the range within the buffer to valid.  The range is
 *	relative to the beginning of the buffer, b_offset.  Note that
 *	b_offset itself may be offset from the beginning of the first
 *	page.
 */
void   
vfs_bio_set_valid(struct buf *bp, int base, int size)
{
	int i, n;
	vm_page_t m;

	if (!(bp->b_flags & B_VMIO))
		return;

	/*
	 * Fixup base to be relative to beginning of first page.
	 * Set initial n to be the maximum number of bytes in the
	 * first page that can be validated.
	 */
	base += (bp->b_offset & PAGE_MASK);
	n = PAGE_SIZE - (base & PAGE_MASK);

	VM_OBJECT_WLOCK(bp->b_bufobj->bo_object);
	for (i = base / PAGE_SIZE; size > 0 && i < bp->b_npages; ++i) {
		m = bp->b_pages[i];
		if (n > size)
			n = size;
		vm_page_set_valid_range(m, base & PAGE_MASK, n);
		base += n;
		size -= n;
		n = PAGE_SIZE;
	}
	VM_OBJECT_WUNLOCK(bp->b_bufobj->bo_object);
}

/*
 *	vfs_bio_clrbuf:
 *
 *	If the specified buffer is a non-VMIO buffer, clear the entire
 *	buffer.  If the specified buffer is a VMIO buffer, clear and
 *	validate only the previously invalid portions of the buffer.
 *	This routine essentially fakes an I/O, so we need to clear
 *	BIO_ERROR and B_INVAL.
 *
 *	Note that while we only theoretically need to clear through b_bcount,
 *	we go ahead and clear through b_bufsize.
 */
void
vfs_bio_clrbuf(struct buf *bp) 
{
	int i, j, mask, sa, ea, slide;

	if ((bp->b_flags & (B_VMIO | B_MALLOC)) != B_VMIO) {
		clrbuf(bp);
		return;
	}
	bp->b_flags &= ~B_INVAL;
	bp->b_ioflags &= ~BIO_ERROR;
	VM_OBJECT_WLOCK(bp->b_bufobj->bo_object);
	if ((bp->b_npages == 1) && (bp->b_bufsize < PAGE_SIZE) &&
	    (bp->b_offset & PAGE_MASK) == 0) {
		if (bp->b_pages[0] == bogus_page)
			goto unlock;
		mask = (1 << (bp->b_bufsize / DEV_BSIZE)) - 1;
		VM_OBJECT_ASSERT_WLOCKED(bp->b_pages[0]->object);
		if ((bp->b_pages[0]->valid & mask) == mask)
			goto unlock;
		if ((bp->b_pages[0]->valid & mask) == 0) {
			pmap_zero_page_area(bp->b_pages[0], 0, bp->b_bufsize);
			bp->b_pages[0]->valid |= mask;
			goto unlock;
		}
	}
	sa = bp->b_offset & PAGE_MASK;
	slide = 0;
	for (i = 0; i < bp->b_npages; i++, sa = 0) {
		slide = imin(slide + PAGE_SIZE, bp->b_offset + bp->b_bufsize);
		ea = slide & PAGE_MASK;
		if (ea == 0)
			ea = PAGE_SIZE;
		if (bp->b_pages[i] == bogus_page)
			continue;
		j = sa / DEV_BSIZE;
		mask = ((1 << ((ea - sa) / DEV_BSIZE)) - 1) << j;
		VM_OBJECT_ASSERT_WLOCKED(bp->b_pages[i]->object);
		if ((bp->b_pages[i]->valid & mask) == mask)
			continue;
		if ((bp->b_pages[i]->valid & mask) == 0)
			pmap_zero_page_area(bp->b_pages[i], sa, ea - sa);
		else {
			for (; sa < ea; sa += DEV_BSIZE, j++) {
				if ((bp->b_pages[i]->valid & (1 << j)) == 0) {
					pmap_zero_page_area(bp->b_pages[i],
					    sa, DEV_BSIZE);
				}
			}
		}
		bp->b_pages[i]->valid |= mask;
	}
unlock:
	VM_OBJECT_WUNLOCK(bp->b_bufobj->bo_object);
	bp->b_resid = 0;
}

void
vfs_bio_bzero_buf(struct buf *bp, int base, int size)
{
	vm_page_t m;
	int i, n;

	if (buf_mapped(bp)) {
		BUF_CHECK_MAPPED(bp);
		bzero(bp->b_data + base, size);
	} else {
		BUF_CHECK_UNMAPPED(bp);
		n = PAGE_SIZE - (base & PAGE_MASK);
		for (i = base / PAGE_SIZE; size > 0 && i < bp->b_npages; ++i) {
			m = bp->b_pages[i];
			if (n > size)
				n = size;
			pmap_zero_page_area(m, base & PAGE_MASK, n);
			base += n;
			size -= n;
			n = PAGE_SIZE;
		}
	}
}

/*
 * Update buffer flags based on I/O request parameters, optionally releasing the
 * buffer.  If it's VMIO or direct I/O, the buffer pages are released to the VM,
 * where they may be placed on a page queue (VMIO) or freed immediately (direct
 * I/O).  Otherwise the buffer is released to the cache.
 */
static void
b_io_dismiss(struct buf *bp, int ioflag, bool release)
{

	KASSERT((ioflag & IO_NOREUSE) == 0 || (ioflag & IO_VMIO) != 0,
	    ("buf %p non-VMIO noreuse", bp));

	if ((ioflag & IO_DIRECT) != 0)
		bp->b_flags |= B_DIRECT;
	if ((ioflag & IO_EXT) != 0)
		bp->b_xflags |= BX_ALTDATA;
	if ((ioflag & (IO_VMIO | IO_DIRECT)) != 0 && LIST_EMPTY(&bp->b_dep)) {
		bp->b_flags |= B_RELBUF;
		if ((ioflag & IO_NOREUSE) != 0)
			bp->b_flags |= B_NOREUSE;
		if (release)
			brelse(bp);
	} else if (release)
		bqrelse(bp);
}

void
vfs_bio_brelse(struct buf *bp, int ioflag)
{

	b_io_dismiss(bp, ioflag, true);
}

void
vfs_bio_set_flags(struct buf *bp, int ioflag)
{

	b_io_dismiss(bp, ioflag, false);
}

/*
 * vm_hold_load_pages and vm_hold_free_pages get pages into
 * a buffers address space.  The pages are anonymous and are
 * not associated with a file object.
 */
static void
vm_hold_load_pages(struct buf *bp, vm_offset_t from, vm_offset_t to)
{
	vm_offset_t pg;
	vm_page_t p;
	int index;

	BUF_CHECK_MAPPED(bp);

	to = round_page(to);
	from = round_page(from);
	index = (from - trunc_page((vm_offset_t)bp->b_data)) >> PAGE_SHIFT;

	for (pg = from; pg < to; pg += PAGE_SIZE, index++) {
		/*
		 * note: must allocate system pages since blocking here
		 * could interfere with paging I/O, no matter which
		 * process we are.
		 */
		p = vm_page_alloc(NULL, 0, VM_ALLOC_SYSTEM | VM_ALLOC_NOOBJ |
		    VM_ALLOC_WIRED | VM_ALLOC_COUNT((to - pg) >> PAGE_SHIFT) |
		    VM_ALLOC_WAITOK);
		pmap_qenter(pg, &p, 1);
		bp->b_pages[index] = p;
	}
	bp->b_npages = index;
}

/* Return pages associated with this buf to the vm system */
static void
vm_hold_free_pages(struct buf *bp, int newbsize)
{
	vm_offset_t from;
	vm_page_t p;
	int index, newnpages;

	BUF_CHECK_MAPPED(bp);

	from = round_page((vm_offset_t)bp->b_data + newbsize);
	newnpages = (from - trunc_page((vm_offset_t)bp->b_data)) >> PAGE_SHIFT;
	if (bp->b_npages > newnpages)
		pmap_qremove(from, bp->b_npages - newnpages);
	for (index = newnpages; index < bp->b_npages; index++) {
		p = bp->b_pages[index];
		bp->b_pages[index] = NULL;
		p->wire_count--;
		vm_page_free(p);
	}
	vm_wire_sub(bp->b_npages - newnpages);
	bp->b_npages = newnpages;
}

/*
 * Map an IO request into kernel virtual address space.
 *
 * All requests are (re)mapped into kernel VA space.
 * Notice that we use b_bufsize for the size of the buffer
 * to be mapped.  b_bcount might be modified by the driver.
 *
 * Note that even if the caller determines that the address space should
 * be valid, a race or a smaller-file mapped into a larger space may
 * actually cause vmapbuf() to fail, so all callers of vmapbuf() MUST
 * check the return value.
 *
 * This function only works with pager buffers.
 */
int
vmapbuf(struct buf *bp, int mapbuf)
{
	vm_prot_t prot;
	int pidx;

	if (bp->b_bufsize < 0)
		return (-1);
	prot = VM_PROT_READ;
	if (bp->b_iocmd == BIO_READ)
		prot |= VM_PROT_WRITE;	/* Less backwards than it looks */
	if ((pidx = vm_fault_quick_hold_pages(&curproc->p_vmspace->vm_map,
	    (vm_offset_t)bp->b_data, bp->b_bufsize, prot, bp->b_pages,
	    btoc(MAXPHYS))) < 0)
		return (-1);
	bp->b_npages = pidx;
	bp->b_offset = ((vm_offset_t)bp->b_data) & PAGE_MASK;
	if (mapbuf || !unmapped_buf_allowed) {
		pmap_qenter((vm_offset_t)bp->b_kvabase, bp->b_pages, pidx);
		bp->b_data = bp->b_kvabase + bp->b_offset;
	} else
		bp->b_data = unmapped_buf;
	return(0);
}

/*
 * Free the io map PTEs associated with this IO operation.
 * We also invalidate the TLB entries and restore the original b_addr.
 *
 * This function only works with pager buffers.
 */
void
vunmapbuf(struct buf *bp)
{
	int npages;

	npages = bp->b_npages;
	if (buf_mapped(bp))
		pmap_qremove(trunc_page((vm_offset_t)bp->b_data), npages);
	vm_page_unhold_pages(bp->b_pages, npages);

	bp->b_data = unmapped_buf;
}

void
bdone(struct buf *bp)
{
	struct mtx *mtxp;

	mtxp = mtx_pool_find(mtxpool_sleep, bp);
	mtx_lock(mtxp);
	bp->b_flags |= B_DONE;
	wakeup(bp);
	mtx_unlock(mtxp);
}

void
bwait(struct buf *bp, u_char pri, const char *wchan)
{
	struct mtx *mtxp;

	mtxp = mtx_pool_find(mtxpool_sleep, bp);
	mtx_lock(mtxp);
	while ((bp->b_flags & B_DONE) == 0)
		msleep(bp, mtxp, pri, wchan, 0);
	mtx_unlock(mtxp);
}

int
bufsync(struct bufobj *bo, int waitfor)
{

	return (VOP_FSYNC(bo2vnode(bo), waitfor, curthread));
}

void
bufstrategy(struct bufobj *bo, struct buf *bp)
{
	int i __unused;
	struct vnode *vp;

	vp = bp->b_vp;
	KASSERT(vp == bo->bo_private, ("Inconsistent vnode bufstrategy"));
	KASSERT(vp->v_type != VCHR && vp->v_type != VBLK,
	    ("Wrong vnode in bufstrategy(bp=%p, vp=%p)", bp, vp));
	i = VOP_STRATEGY(vp, bp);
	KASSERT(i == 0, ("VOP_STRATEGY failed bp=%p vp=%p", bp, bp->b_vp));
}

/*
 * Initialize a struct bufobj before use.  Memory is assumed zero filled.
 */
void
bufobj_init(struct bufobj *bo, void *private)
{
	static volatile int bufobj_cleanq;

        bo->bo_domain =
            atomic_fetchadd_int(&bufobj_cleanq, 1) % buf_domains;
        rw_init(BO_LOCKPTR(bo), "bufobj interlock");
        bo->bo_private = private;
        TAILQ_INIT(&bo->bo_clean.bv_hd);
        TAILQ_INIT(&bo->bo_dirty.bv_hd);
}

void
bufobj_wrefl(struct bufobj *bo)
{

	KASSERT(bo != NULL, ("NULL bo in bufobj_wref"));
	ASSERT_BO_WLOCKED(bo);
	bo->bo_numoutput++;
}

void
bufobj_wref(struct bufobj *bo)
{

	KASSERT(bo != NULL, ("NULL bo in bufobj_wref"));
	BO_LOCK(bo);
	bo->bo_numoutput++;
	BO_UNLOCK(bo);
}

void
bufobj_wdrop(struct bufobj *bo)
{

	KASSERT(bo != NULL, ("NULL bo in bufobj_wdrop"));
	BO_LOCK(bo);
	KASSERT(bo->bo_numoutput > 0, ("bufobj_wdrop non-positive count"));
	if ((--bo->bo_numoutput == 0) && (bo->bo_flag & BO_WWAIT)) {
		bo->bo_flag &= ~BO_WWAIT;
		wakeup(&bo->bo_numoutput);
	}
	BO_UNLOCK(bo);
}

int
bufobj_wwait(struct bufobj *bo, int slpflag, int timeo)
{
	int error;

	KASSERT(bo != NULL, ("NULL bo in bufobj_wwait"));
	ASSERT_BO_WLOCKED(bo);
	error = 0;
	while (bo->bo_numoutput) {
		bo->bo_flag |= BO_WWAIT;
		error = msleep(&bo->bo_numoutput, BO_LOCKPTR(bo),
		    slpflag | (PRIBIO + 1), "bo_wwait", timeo);
		if (error)
			break;
	}
	return (error);
}

/*
 * Set bio_data or bio_ma for struct bio from the struct buf.
 */
void
bdata2bio(struct buf *bp, struct bio *bip)
{

	if (!buf_mapped(bp)) {
		KASSERT(unmapped_buf_allowed, ("unmapped"));
		bip->bio_ma = bp->b_pages;
		bip->bio_ma_n = bp->b_npages;
		bip->bio_data = unmapped_buf;
		bip->bio_ma_offset = (vm_offset_t)bp->b_offset & PAGE_MASK;
		bip->bio_flags |= BIO_UNMAPPED;
		KASSERT(round_page(bip->bio_ma_offset + bip->bio_length) /
		    PAGE_SIZE == bp->b_npages,
		    ("Buffer %p too short: %d %lld %d", bp, bip->bio_ma_offset,
		    (long long)bip->bio_length, bip->bio_ma_n));
	} else {
		bip->bio_data = bp->b_data;
		bip->bio_ma = NULL;
	}
}

/*
 * The MIPS pmap code currently doesn't handle aliased pages.
 * The VIPT caches may not handle page aliasing themselves, leading
 * to data corruption.
 *
 * As such, this code makes a system extremely unhappy if said
 * system doesn't support unaliasing the above situation in hardware.
 * Some "recent" systems (eg some mips24k/mips74k cores) don't enable
 * this feature at build time, so it has to be handled in software.
 *
 * Once the MIPS pmap/cache code grows to support this function on
 * earlier chips, it should be flipped back off.
 */
#ifdef	__mips__
static int buf_pager_relbuf = 1;
#else
static int buf_pager_relbuf = 0;
#endif
SYSCTL_INT(_vfs, OID_AUTO, buf_pager_relbuf, CTLFLAG_RWTUN,
    &buf_pager_relbuf, 0,
    "Make buffer pager release buffers after reading");

/*
 * The buffer pager.  It uses buffer reads to validate pages.
 *
 * In contrast to the generic local pager from vm/vnode_pager.c, this
 * pager correctly and easily handles volumes where the underlying
 * device block size is greater than the machine page size.  The
 * buffer cache transparently extends the requested page run to be
 * aligned at the block boundary, and does the necessary bogus page
 * replacements in the addends to avoid obliterating already valid
 * pages.
 *
 * The only non-trivial issue is that the exclusive busy state for
 * pages, which is assumed by the vm_pager_getpages() interface, is
 * incompatible with the VMIO buffer cache's desire to share-busy the
 * pages.  This function performs a trivial downgrade of the pages'
 * state before reading buffers, and a less trivial upgrade from the
 * shared-busy to excl-busy state after the read.
 */
int
vfs_bio_getpages(struct vnode *vp, vm_page_t *ma, int count,
    int *rbehind, int *rahead, vbg_get_lblkno_t get_lblkno,
    vbg_get_blksize_t get_blksize)
{
	vm_page_t m;
	vm_object_t object;
	struct buf *bp;
	struct mount *mp;
	daddr_t lbn, lbnp;
	vm_ooffset_t la, lb, poff, poffe;
	long bsize;
	int bo_bs, br_flags, error, i, pgsin, pgsin_a, pgsin_b;
	bool redo, lpart;

	object = vp->v_object;
	mp = vp->v_mount;
	error = 0;
	la = IDX_TO_OFF(ma[count - 1]->pindex);
	if (la >= object->un_pager.vnp.vnp_size)
		return (VM_PAGER_BAD);

	/*
	 * Change the meaning of la from where the last requested page starts
	 * to where it ends, because that's the end of the requested region
	 * and the start of the potential read-ahead region.
	 */
	la += PAGE_SIZE;
	lpart = la > object->un_pager.vnp.vnp_size;
	bo_bs = get_blksize(vp, get_lblkno(vp, IDX_TO_OFF(ma[0]->pindex)));

	/*
	 * Calculate read-ahead, behind and total pages.
	 */
	pgsin = count;
	lb = IDX_TO_OFF(ma[0]->pindex);
	pgsin_b = OFF_TO_IDX(lb - rounddown2(lb, bo_bs));
	pgsin += pgsin_b;
	if (rbehind != NULL)
		*rbehind = pgsin_b;
	pgsin_a = OFF_TO_IDX(roundup2(la, bo_bs) - la);
	if (la + IDX_TO_OFF(pgsin_a) >= object->un_pager.vnp.vnp_size)
		pgsin_a = OFF_TO_IDX(roundup2(object->un_pager.vnp.vnp_size,
		    PAGE_SIZE) - la);
	pgsin += pgsin_a;
	if (rahead != NULL)
		*rahead = pgsin_a;
	VM_CNT_INC(v_vnodein);
	VM_CNT_ADD(v_vnodepgsin, pgsin);

	br_flags = (mp != NULL && (mp->mnt_kern_flag & MNTK_UNMAPPED_BUFS)
	    != 0) ? GB_UNMAPPED : 0;
	VM_OBJECT_WLOCK(object);
again:
	for (i = 0; i < count; i++)
		vm_page_busy_downgrade(ma[i]);
	VM_OBJECT_WUNLOCK(object);

	lbnp = -1;
	for (i = 0; i < count; i++) {
		m = ma[i];

		/*
		 * Pages are shared busy and the object lock is not
		 * owned, which together allow for the pages'
		 * invalidation.  The racy test for validity avoids
		 * useless creation of the buffer for the most typical
		 * case when invalidation is not used in redo or for
		 * parallel read.  The shared->excl upgrade loop at
		 * the end of the function catches the race in a
		 * reliable way (protected by the object lock).
		 */
		if (m->valid == VM_PAGE_BITS_ALL)
			continue;

		poff = IDX_TO_OFF(m->pindex);
		poffe = MIN(poff + PAGE_SIZE, object->un_pager.vnp.vnp_size);
		for (; poff < poffe; poff += bsize) {
			lbn = get_lblkno(vp, poff);
			if (lbn == lbnp)
				goto next_page;
			lbnp = lbn;

			bsize = get_blksize(vp, lbn);
			error = bread_gb(vp, lbn, bsize, curthread->td_ucred,
			    br_flags, &bp);
			if (error != 0)
				goto end_pages;
			if (LIST_EMPTY(&bp->b_dep)) {
				/*
				 * Invalidation clears m->valid, but
				 * may leave B_CACHE flag if the
				 * buffer existed at the invalidation
				 * time.  In this case, recycle the
				 * buffer to do real read on next
				 * bread() after redo.
				 *
				 * Otherwise B_RELBUF is not strictly
				 * necessary, enable to reduce buf
				 * cache pressure.
				 */
				if (buf_pager_relbuf ||
				    m->valid != VM_PAGE_BITS_ALL)
					bp->b_flags |= B_RELBUF;

				bp->b_flags &= ~B_NOCACHE;
				brelse(bp);
			} else {
				bqrelse(bp);
			}
		}
		KASSERT(1 /* racy, enable for debugging */ ||
		    m->valid == VM_PAGE_BITS_ALL || i == count - 1,
		    ("buf %d %p invalid", i, m));
		if (i == count - 1 && lpart) {
			VM_OBJECT_WLOCK(object);
			if (m->valid != 0 &&
			    m->valid != VM_PAGE_BITS_ALL)
				vm_page_zero_invalid(m, TRUE);
			VM_OBJECT_WUNLOCK(object);
		}
next_page:;
	}
end_pages:

	VM_OBJECT_WLOCK(object);
	redo = false;
	for (i = 0; i < count; i++) {
		vm_page_sunbusy(ma[i]);
		ma[i] = vm_page_grab(object, ma[i]->pindex, VM_ALLOC_NORMAL);

		/*
		 * Since the pages were only sbusy while neither the
		 * buffer nor the object lock was held by us, or
		 * reallocated while vm_page_grab() slept for busy
		 * relinguish, they could have been invalidated.
		 * Recheck the valid bits and re-read as needed.
		 *
		 * Note that the last page is made fully valid in the
		 * read loop, and partial validity for the page at
		 * index count - 1 could mean that the page was
		 * invalidated or removed, so we must restart for
		 * safety as well.
		 */
		if (ma[i]->valid != VM_PAGE_BITS_ALL)
			redo = true;
	}
	if (redo && error == 0)
		goto again;
	VM_OBJECT_WUNLOCK(object);
	return (error != 0 ? VM_PAGER_ERROR : VM_PAGER_OK);
}

#include "opt_ddb.h"
#ifdef DDB
#include <ddb/ddb.h>

/* DDB command to show buffer data */
DB_SHOW_COMMAND(buffer, db_show_buffer)
{
	/* get args */
	struct buf *bp = (struct buf *)addr;
#ifdef FULL_BUF_TRACKING
	uint32_t i, j;
#endif

	if (!have_addr) {
		db_printf("usage: show buffer <addr>\n");
		return;
	}

	db_printf("buf at %p\n", bp);
	db_printf("b_flags = 0x%b, b_xflags=0x%b\n",
	    (u_int)bp->b_flags, PRINT_BUF_FLAGS,
	    (u_int)bp->b_xflags, PRINT_BUF_XFLAGS);
	db_printf("b_vflags=0x%b b_ioflags0x%b\n",
	    (u_int)bp->b_vflags, PRINT_BUF_VFLAGS,
	    (u_int)bp->b_ioflags, PRINT_BIO_FLAGS);
	db_printf(
	    "b_error = %d, b_bufsize = %ld, b_bcount = %ld, b_resid = %ld\n"
	    "b_bufobj = (%p), b_data = %p\n, b_blkno = %jd, b_lblkno = %jd, "
	    "b_vp = %p, b_dep = %p\n",
	    bp->b_error, bp->b_bufsize, bp->b_bcount, bp->b_resid,
	    bp->b_bufobj, bp->b_data, (intmax_t)bp->b_blkno,
	    (intmax_t)bp->b_lblkno, bp->b_vp, bp->b_dep.lh_first);
	db_printf("b_kvabase = %p, b_kvasize = %d\n",
	    bp->b_kvabase, bp->b_kvasize);
	if (bp->b_npages) {
		int i;
		db_printf("b_npages = %d, pages(OBJ, IDX, PA): ", bp->b_npages);
		for (i = 0; i < bp->b_npages; i++) {
			vm_page_t m;
			m = bp->b_pages[i];
			if (m != NULL)
				db_printf("(%p, 0x%lx, 0x%lx)", m->object,
				    (u_long)m->pindex,
				    (u_long)VM_PAGE_TO_PHYS(m));
			else
				db_printf("( ??? )");
			if ((i + 1) < bp->b_npages)
				db_printf(",");
		}
		db_printf("\n");
	}
	BUF_LOCKPRINTINFO(bp);
#if defined(FULL_BUF_TRACKING)
	db_printf("b_io_tracking: b_io_tcnt = %u\n", bp->b_io_tcnt);

	i = bp->b_io_tcnt % BUF_TRACKING_SIZE;
	for (j = 1; j <= BUF_TRACKING_SIZE; j++) {
		if (bp->b_io_tracking[BUF_TRACKING_ENTRY(i - j)] == NULL)
			continue;
		db_printf(" %2u: %s\n", j,
		    bp->b_io_tracking[BUF_TRACKING_ENTRY(i - j)]);
	}
#elif defined(BUF_TRACKING)
	db_printf("b_io_tracking: %s\n", bp->b_io_tracking);
#endif
	db_printf(" ");
}

DB_SHOW_COMMAND(bufqueues, bufqueues)
{
	struct bufdomain *bd;
	struct buf *bp;
	long total;
	int i, j, cnt;

	db_printf("bqempty: %d\n", bqempty.bq_len);

	for (i = 0; i < buf_domains; i++) {
		bd = &bdomain[i];
		db_printf("Buf domain %d\n", i);
		db_printf("\tfreebufs\t%d\n", bd->bd_freebuffers);
		db_printf("\tlofreebufs\t%d\n", bd->bd_lofreebuffers);
		db_printf("\thifreebufs\t%d\n", bd->bd_hifreebuffers);
		db_printf("\n");
		db_printf("\tbufspace\t%ld\n", bd->bd_bufspace);
		db_printf("\tmaxbufspace\t%ld\n", bd->bd_maxbufspace);
		db_printf("\thibufspace\t%ld\n", bd->bd_hibufspace);
		db_printf("\tlobufspace\t%ld\n", bd->bd_lobufspace);
		db_printf("\tbufspacethresh\t%ld\n", bd->bd_bufspacethresh);
		db_printf("\n");
		db_printf("\tnumdirtybuffers\t%d\n", bd->bd_numdirtybuffers);
		db_printf("\tlodirtybuffers\t%d\n", bd->bd_lodirtybuffers);
		db_printf("\thidirtybuffers\t%d\n", bd->bd_hidirtybuffers);
		db_printf("\tdirtybufthresh\t%d\n", bd->bd_dirtybufthresh);
		db_printf("\n");
		total = 0;
		TAILQ_FOREACH(bp, &bd->bd_cleanq->bq_queue, b_freelist)
			total += bp->b_bufsize;
		db_printf("\tcleanq count\t%d (%ld)\n",
		    bd->bd_cleanq->bq_len, total);
		total = 0;
		TAILQ_FOREACH(bp, &bd->bd_dirtyq.bq_queue, b_freelist)
			total += bp->b_bufsize;
		db_printf("\tdirtyq count\t%d (%ld)\n",
		    bd->bd_dirtyq.bq_len, total);
		db_printf("\twakeup\t\t%d\n", bd->bd_wanted);
		db_printf("\tlim\t\t%d\n", bd->bd_lim);
		db_printf("\tCPU ");
		for (j = 0; j <= mp_maxid; j++)
			db_printf("%d, ", bd->bd_subq[j].bq_len);
		db_printf("\n");
		cnt = 0;
		total = 0;
		for (j = 0; j < nbuf; j++)
			if (buf[j].b_domain == i && BUF_ISLOCKED(&buf[j])) {
				cnt++;
				total += buf[j].b_bufsize;
			}
		db_printf("\tLocked buffers: %d space %ld\n", cnt, total);
		cnt = 0;
		total = 0;
		for (j = 0; j < nbuf; j++)
			if (buf[j].b_domain == i) {
				cnt++;
				total += buf[j].b_bufsize;
			}
		db_printf("\tTotal buffers: %d space %ld\n", cnt, total);
	}
}

DB_SHOW_COMMAND(lockedbufs, lockedbufs)
{
	struct buf *bp;
	int i;

	for (i = 0; i < nbuf; i++) {
		bp = &buf[i];
		if (BUF_ISLOCKED(bp)) {
			db_show_buffer((uintptr_t)bp, 1, 0, NULL);
			db_printf("\n");
			if (db_pager_quit)
				break;
		}
	}
}

DB_SHOW_COMMAND(vnodebufs, db_show_vnodebufs)
{
	struct vnode *vp;
	struct buf *bp;

	if (!have_addr) {
		db_printf("usage: show vnodebufs <addr>\n");
		return;
	}
	vp = (struct vnode *)addr;
	db_printf("Clean buffers:\n");
	TAILQ_FOREACH(bp, &vp->v_bufobj.bo_clean.bv_hd, b_bobufs) {
		db_show_buffer((uintptr_t)bp, 1, 0, NULL);
		db_printf("\n");
	}
	db_printf("Dirty buffers:\n");
	TAILQ_FOREACH(bp, &vp->v_bufobj.bo_dirty.bv_hd, b_bobufs) {
		db_show_buffer((uintptr_t)bp, 1, 0, NULL);
		db_printf("\n");
	}
}

DB_COMMAND(countfreebufs, db_coundfreebufs)
{
	struct buf *bp;
	int i, used = 0, nfree = 0;

	if (have_addr) {
		db_printf("usage: countfreebufs\n");
		return;
	}

	for (i = 0; i < nbuf; i++) {
		bp = &buf[i];
		if (bp->b_qindex == QUEUE_EMPTY)
			nfree++;
		else
			used++;
	}

	db_printf("Counted %d free, %d used (%d tot)\n", nfree, used,
	    nfree + used);
	db_printf("numfreebuffers is %d\n", numfreebuffers);
}
#endif /* DDB */
