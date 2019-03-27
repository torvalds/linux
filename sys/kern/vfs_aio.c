/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 1997 John S. Dyson.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. John S. Dyson's name may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * DISCLAIMER:  This code isn't warranted to do anything useful.  Anything
 * bad that happens because of using this software isn't the responsibility
 * of the author.  This software is distributed AS-IS.
 */

/*
 * This file contains support for the POSIX 1003.1B AIO/LIO facility.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/malloc.h>
#include <sys/bio.h>
#include <sys/buf.h>
#include <sys/capsicum.h>
#include <sys/eventhandler.h>
#include <sys/sysproto.h>
#include <sys/filedesc.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/kthread.h>
#include <sys/fcntl.h>
#include <sys/file.h>
#include <sys/limits.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/unistd.h>
#include <sys/posix4.h>
#include <sys/proc.h>
#include <sys/resourcevar.h>
#include <sys/signalvar.h>
#include <sys/syscallsubr.h>
#include <sys/protosw.h>
#include <sys/rwlock.h>
#include <sys/sema.h>
#include <sys/socket.h>
#include <sys/socketvar.h>
#include <sys/syscall.h>
#include <sys/sysent.h>
#include <sys/sysctl.h>
#include <sys/syslog.h>
#include <sys/sx.h>
#include <sys/taskqueue.h>
#include <sys/vnode.h>
#include <sys/conf.h>
#include <sys/event.h>
#include <sys/mount.h>
#include <geom/geom.h>

#include <machine/atomic.h>

#include <vm/vm.h>
#include <vm/vm_page.h>
#include <vm/vm_extern.h>
#include <vm/pmap.h>
#include <vm/vm_map.h>
#include <vm/vm_object.h>
#include <vm/uma.h>
#include <sys/aio.h>

/*
 * Counter for allocating reference ids to new jobs.  Wrapped to 1 on
 * overflow. (XXX will be removed soon.)
 */
static u_long jobrefid;

/*
 * Counter for aio_fsync.
 */
static uint64_t jobseqno;

#ifndef MAX_AIO_PER_PROC
#define MAX_AIO_PER_PROC	32
#endif

#ifndef MAX_AIO_QUEUE_PER_PROC
#define MAX_AIO_QUEUE_PER_PROC	256
#endif

#ifndef MAX_AIO_QUEUE
#define MAX_AIO_QUEUE		1024 /* Bigger than MAX_AIO_QUEUE_PER_PROC */
#endif

#ifndef MAX_BUF_AIO
#define MAX_BUF_AIO		16
#endif

FEATURE(aio, "Asynchronous I/O");
SYSCTL_DECL(_p1003_1b);

static MALLOC_DEFINE(M_LIO, "lio", "listio aio control block list");
static MALLOC_DEFINE(M_AIOS, "aios", "aio_suspend aio control block list");

static SYSCTL_NODE(_vfs, OID_AUTO, aio, CTLFLAG_RW, 0,
    "Async IO management");

static int enable_aio_unsafe = 0;
SYSCTL_INT(_vfs_aio, OID_AUTO, enable_unsafe, CTLFLAG_RW, &enable_aio_unsafe, 0,
    "Permit asynchronous IO on all file types, not just known-safe types");

static unsigned int unsafe_warningcnt = 1;
SYSCTL_UINT(_vfs_aio, OID_AUTO, unsafe_warningcnt, CTLFLAG_RW,
    &unsafe_warningcnt, 0,
    "Warnings that will be triggered upon failed IO requests on unsafe files");

static int max_aio_procs = MAX_AIO_PROCS;
SYSCTL_INT(_vfs_aio, OID_AUTO, max_aio_procs, CTLFLAG_RW, &max_aio_procs, 0,
    "Maximum number of kernel processes to use for handling async IO ");

static int num_aio_procs = 0;
SYSCTL_INT(_vfs_aio, OID_AUTO, num_aio_procs, CTLFLAG_RD, &num_aio_procs, 0,
    "Number of presently active kernel processes for async IO");

/*
 * The code will adjust the actual number of AIO processes towards this
 * number when it gets a chance.
 */
static int target_aio_procs = TARGET_AIO_PROCS;
SYSCTL_INT(_vfs_aio, OID_AUTO, target_aio_procs, CTLFLAG_RW, &target_aio_procs,
    0,
    "Preferred number of ready kernel processes for async IO");

static int max_queue_count = MAX_AIO_QUEUE;
SYSCTL_INT(_vfs_aio, OID_AUTO, max_aio_queue, CTLFLAG_RW, &max_queue_count, 0,
    "Maximum number of aio requests to queue, globally");

static int num_queue_count = 0;
SYSCTL_INT(_vfs_aio, OID_AUTO, num_queue_count, CTLFLAG_RD, &num_queue_count, 0,
    "Number of queued aio requests");

static int num_buf_aio = 0;
SYSCTL_INT(_vfs_aio, OID_AUTO, num_buf_aio, CTLFLAG_RD, &num_buf_aio, 0,
    "Number of aio requests presently handled by the buf subsystem");

static int num_unmapped_aio = 0;
SYSCTL_INT(_vfs_aio, OID_AUTO, num_unmapped_aio, CTLFLAG_RD, &num_unmapped_aio,
    0,
    "Number of aio requests presently handled by unmapped I/O buffers");

/* Number of async I/O processes in the process of being started */
/* XXX This should be local to aio_aqueue() */
static int num_aio_resv_start = 0;

static int aiod_lifetime;
SYSCTL_INT(_vfs_aio, OID_AUTO, aiod_lifetime, CTLFLAG_RW, &aiod_lifetime, 0,
    "Maximum lifetime for idle aiod");

static int max_aio_per_proc = MAX_AIO_PER_PROC;
SYSCTL_INT(_vfs_aio, OID_AUTO, max_aio_per_proc, CTLFLAG_RW, &max_aio_per_proc,
    0,
    "Maximum active aio requests per process");

static int max_aio_queue_per_proc = MAX_AIO_QUEUE_PER_PROC;
SYSCTL_INT(_vfs_aio, OID_AUTO, max_aio_queue_per_proc, CTLFLAG_RW,
    &max_aio_queue_per_proc, 0,
    "Maximum queued aio requests per process");

static int max_buf_aio = MAX_BUF_AIO;
SYSCTL_INT(_vfs_aio, OID_AUTO, max_buf_aio, CTLFLAG_RW, &max_buf_aio, 0,
    "Maximum buf aio requests per process");

/* 
 * Though redundant with vfs.aio.max_aio_queue_per_proc, POSIX requires
 * sysconf(3) to support AIO_LISTIO_MAX, and we implement that with
 * vfs.aio.aio_listio_max.
 */
SYSCTL_INT(_p1003_1b, CTL_P1003_1B_AIO_LISTIO_MAX, aio_listio_max,
    CTLFLAG_RD | CTLFLAG_CAPRD, &max_aio_queue_per_proc,
    0, "Maximum aio requests for a single lio_listio call");

#ifdef COMPAT_FREEBSD6
typedef struct oaiocb {
	int	aio_fildes;		/* File descriptor */
	off_t	aio_offset;		/* File offset for I/O */
	volatile void *aio_buf;         /* I/O buffer in process space */
	size_t	aio_nbytes;		/* Number of bytes for I/O */
	struct	osigevent aio_sigevent;	/* Signal to deliver */
	int	aio_lio_opcode;		/* LIO opcode */
	int	aio_reqprio;		/* Request priority -- ignored */
	struct	__aiocb_private	_aiocb_private;
} oaiocb_t;
#endif

/*
 * Below is a key of locks used to protect each member of struct kaiocb
 * aioliojob and kaioinfo and any backends.
 *
 * * - need not protected
 * a - locked by kaioinfo lock
 * b - locked by backend lock, the backend lock can be null in some cases,
 *     for example, BIO belongs to this type, in this case, proc lock is
 *     reused.
 * c - locked by aio_job_mtx, the lock for the generic file I/O backend.
 */

/*
 * If the routine that services an AIO request blocks while running in an
 * AIO kernel process it can starve other I/O requests.  BIO requests
 * queued via aio_qbio() complete asynchronously and do not use AIO kernel
 * processes at all.  Socket I/O requests use a separate pool of
 * kprocs and also force non-blocking I/O.  Other file I/O requests
 * use the generic fo_read/fo_write operations which can block.  The
 * fsync and mlock operations can also block while executing.  Ideally
 * none of these requests would block while executing.
 *
 * Note that the service routines cannot toggle O_NONBLOCK in the file
 * structure directly while handling a request due to races with
 * userland threads.
 */

/* jobflags */
#define	KAIOCB_QUEUEING		0x01
#define	KAIOCB_CANCELLED	0x02
#define	KAIOCB_CANCELLING	0x04
#define	KAIOCB_CHECKSYNC	0x08
#define	KAIOCB_CLEARED		0x10
#define	KAIOCB_FINISHED		0x20

/*
 * AIO process info
 */
#define AIOP_FREE	0x1			/* proc on free queue */

struct aioproc {
	int	aioprocflags;			/* (c) AIO proc flags */
	TAILQ_ENTRY(aioproc) list;		/* (c) list of processes */
	struct	proc *aioproc;			/* (*) the AIO proc */
};

/*
 * data-structure for lio signal management
 */
struct aioliojob {
	int	lioj_flags;			/* (a) listio flags */
	int	lioj_count;			/* (a) listio flags */
	int	lioj_finished_count;		/* (a) listio flags */
	struct	sigevent lioj_signal;		/* (a) signal on all I/O done */
	TAILQ_ENTRY(aioliojob) lioj_list;	/* (a) lio list */
	struct	knlist klist;			/* (a) list of knotes */
	ksiginfo_t lioj_ksi;			/* (a) Realtime signal info */
};

#define	LIOJ_SIGNAL		0x1	/* signal on all done (lio) */
#define	LIOJ_SIGNAL_POSTED	0x2	/* signal has been posted */
#define LIOJ_KEVENT_POSTED	0x4	/* kevent triggered */

/*
 * per process aio data structure
 */
struct kaioinfo {
	struct	mtx kaio_mtx;		/* the lock to protect this struct */
	int	kaio_flags;		/* (a) per process kaio flags */
	int	kaio_active_count;	/* (c) number of currently used AIOs */
	int	kaio_count;		/* (a) size of AIO queue */
	int	kaio_buffer_count;	/* (a) number of bio buffers */
	TAILQ_HEAD(,kaiocb) kaio_all;	/* (a) all AIOs in a process */
	TAILQ_HEAD(,kaiocb) kaio_done;	/* (a) done queue for process */
	TAILQ_HEAD(,aioliojob) kaio_liojoblist; /* (a) list of lio jobs */
	TAILQ_HEAD(,kaiocb) kaio_jobqueue;	/* (a) job queue for process */
	TAILQ_HEAD(,kaiocb) kaio_syncqueue;	/* (a) queue for aio_fsync */
	TAILQ_HEAD(,kaiocb) kaio_syncready;  /* (a) second q for aio_fsync */
	struct	task kaio_task;		/* (*) task to kick aio processes */
	struct	task kaio_sync_task;	/* (*) task to schedule fsync jobs */
};

#define AIO_LOCK(ki)		mtx_lock(&(ki)->kaio_mtx)
#define AIO_UNLOCK(ki)		mtx_unlock(&(ki)->kaio_mtx)
#define AIO_LOCK_ASSERT(ki, f)	mtx_assert(&(ki)->kaio_mtx, (f))
#define AIO_MTX(ki)		(&(ki)->kaio_mtx)

#define KAIO_RUNDOWN	0x1	/* process is being run down */
#define KAIO_WAKEUP	0x2	/* wakeup process when AIO completes */

/*
 * Operations used to interact with userland aio control blocks.
 * Different ABIs provide their own operations.
 */
struct aiocb_ops {
	int	(*copyin)(struct aiocb *ujob, struct aiocb *kjob);
	long	(*fetch_status)(struct aiocb *ujob);
	long	(*fetch_error)(struct aiocb *ujob);
	int	(*store_status)(struct aiocb *ujob, long status);
	int	(*store_error)(struct aiocb *ujob, long error);
	int	(*store_kernelinfo)(struct aiocb *ujob, long jobref);
	int	(*store_aiocb)(struct aiocb **ujobp, struct aiocb *ujob);
};

static TAILQ_HEAD(,aioproc) aio_freeproc;		/* (c) Idle daemons */
static struct sema aio_newproc_sem;
static struct mtx aio_job_mtx;
static TAILQ_HEAD(,kaiocb) aio_jobs;			/* (c) Async job list */
static struct unrhdr *aiod_unr;

void		aio_init_aioinfo(struct proc *p);
static int	aio_onceonly(void);
static int	aio_free_entry(struct kaiocb *job);
static void	aio_process_rw(struct kaiocb *job);
static void	aio_process_sync(struct kaiocb *job);
static void	aio_process_mlock(struct kaiocb *job);
static void	aio_schedule_fsync(void *context, int pending);
static int	aio_newproc(int *);
int		aio_aqueue(struct thread *td, struct aiocb *ujob,
		    struct aioliojob *lio, int type, struct aiocb_ops *ops);
static int	aio_queue_file(struct file *fp, struct kaiocb *job);
static void	aio_biowakeup(struct bio *bp);
static void	aio_proc_rundown(void *arg, struct proc *p);
static void	aio_proc_rundown_exec(void *arg, struct proc *p,
		    struct image_params *imgp);
static int	aio_qbio(struct proc *p, struct kaiocb *job);
static void	aio_daemon(void *param);
static void	aio_bio_done_notify(struct proc *userp, struct kaiocb *job);
static bool	aio_clear_cancel_function_locked(struct kaiocb *job);
static int	aio_kick(struct proc *userp);
static void	aio_kick_nowait(struct proc *userp);
static void	aio_kick_helper(void *context, int pending);
static int	filt_aioattach(struct knote *kn);
static void	filt_aiodetach(struct knote *kn);
static int	filt_aio(struct knote *kn, long hint);
static int	filt_lioattach(struct knote *kn);
static void	filt_liodetach(struct knote *kn);
static int	filt_lio(struct knote *kn, long hint);

/*
 * Zones for:
 * 	kaio	Per process async io info
 *	aiop	async io process data
 *	aiocb	async io jobs
 *	aiolio	list io jobs
 */
static uma_zone_t kaio_zone, aiop_zone, aiocb_zone, aiolio_zone;

/* kqueue filters for aio */
static struct filterops aio_filtops = {
	.f_isfd = 0,
	.f_attach = filt_aioattach,
	.f_detach = filt_aiodetach,
	.f_event = filt_aio,
};
static struct filterops lio_filtops = {
	.f_isfd = 0,
	.f_attach = filt_lioattach,
	.f_detach = filt_liodetach,
	.f_event = filt_lio
};

static eventhandler_tag exit_tag, exec_tag;

TASKQUEUE_DEFINE_THREAD(aiod_kick);

/*
 * Main operations function for use as a kernel module.
 */
static int
aio_modload(struct module *module, int cmd, void *arg)
{
	int error = 0;

	switch (cmd) {
	case MOD_LOAD:
		aio_onceonly();
		break;
	case MOD_SHUTDOWN:
		break;
	default:
		error = EOPNOTSUPP;
		break;
	}
	return (error);
}

static moduledata_t aio_mod = {
	"aio",
	&aio_modload,
	NULL
};

DECLARE_MODULE(aio, aio_mod, SI_SUB_VFS, SI_ORDER_ANY);
MODULE_VERSION(aio, 1);

/*
 * Startup initialization
 */
static int
aio_onceonly(void)
{

	exit_tag = EVENTHANDLER_REGISTER(process_exit, aio_proc_rundown, NULL,
	    EVENTHANDLER_PRI_ANY);
	exec_tag = EVENTHANDLER_REGISTER(process_exec, aio_proc_rundown_exec,
	    NULL, EVENTHANDLER_PRI_ANY);
	kqueue_add_filteropts(EVFILT_AIO, &aio_filtops);
	kqueue_add_filteropts(EVFILT_LIO, &lio_filtops);
	TAILQ_INIT(&aio_freeproc);
	sema_init(&aio_newproc_sem, 0, "aio_new_proc");
	mtx_init(&aio_job_mtx, "aio_job", NULL, MTX_DEF);
	TAILQ_INIT(&aio_jobs);
	aiod_unr = new_unrhdr(1, INT_MAX, NULL);
	kaio_zone = uma_zcreate("AIO", sizeof(struct kaioinfo), NULL, NULL,
	    NULL, NULL, UMA_ALIGN_PTR, UMA_ZONE_NOFREE);
	aiop_zone = uma_zcreate("AIOP", sizeof(struct aioproc), NULL,
	    NULL, NULL, NULL, UMA_ALIGN_PTR, UMA_ZONE_NOFREE);
	aiocb_zone = uma_zcreate("AIOCB", sizeof(struct kaiocb), NULL, NULL,
	    NULL, NULL, UMA_ALIGN_PTR, UMA_ZONE_NOFREE);
	aiolio_zone = uma_zcreate("AIOLIO", sizeof(struct aioliojob), NULL,
	    NULL, NULL, NULL, UMA_ALIGN_PTR, UMA_ZONE_NOFREE);
	aiod_lifetime = AIOD_LIFETIME_DEFAULT;
	jobrefid = 1;
	p31b_setcfg(CTL_P1003_1B_ASYNCHRONOUS_IO, _POSIX_ASYNCHRONOUS_IO);
	p31b_setcfg(CTL_P1003_1B_AIO_MAX, MAX_AIO_QUEUE);
	p31b_setcfg(CTL_P1003_1B_AIO_PRIO_DELTA_MAX, 0);

	return (0);
}

/*
 * Init the per-process aioinfo structure.  The aioinfo limits are set
 * per-process for user limit (resource) management.
 */
void
aio_init_aioinfo(struct proc *p)
{
	struct kaioinfo *ki;

	ki = uma_zalloc(kaio_zone, M_WAITOK);
	mtx_init(&ki->kaio_mtx, "aiomtx", NULL, MTX_DEF | MTX_NEW);
	ki->kaio_flags = 0;
	ki->kaio_active_count = 0;
	ki->kaio_count = 0;
	ki->kaio_buffer_count = 0;
	TAILQ_INIT(&ki->kaio_all);
	TAILQ_INIT(&ki->kaio_done);
	TAILQ_INIT(&ki->kaio_jobqueue);
	TAILQ_INIT(&ki->kaio_liojoblist);
	TAILQ_INIT(&ki->kaio_syncqueue);
	TAILQ_INIT(&ki->kaio_syncready);
	TASK_INIT(&ki->kaio_task, 0, aio_kick_helper, p);
	TASK_INIT(&ki->kaio_sync_task, 0, aio_schedule_fsync, ki);
	PROC_LOCK(p);
	if (p->p_aioinfo == NULL) {
		p->p_aioinfo = ki;
		PROC_UNLOCK(p);
	} else {
		PROC_UNLOCK(p);
		mtx_destroy(&ki->kaio_mtx);
		uma_zfree(kaio_zone, ki);
	}

	while (num_aio_procs < MIN(target_aio_procs, max_aio_procs))
		aio_newproc(NULL);
}

static int
aio_sendsig(struct proc *p, struct sigevent *sigev, ksiginfo_t *ksi)
{
	struct thread *td;
	int error;

	error = sigev_findtd(p, sigev, &td);
	if (error)
		return (error);
	if (!KSI_ONQ(ksi)) {
		ksiginfo_set_sigev(ksi, sigev);
		ksi->ksi_code = SI_ASYNCIO;
		ksi->ksi_flags |= KSI_EXT | KSI_INS;
		tdsendsignal(p, td, ksi->ksi_signo, ksi);
	}
	PROC_UNLOCK(p);
	return (error);
}

/*
 * Free a job entry.  Wait for completion if it is currently active, but don't
 * delay forever.  If we delay, we return a flag that says that we have to
 * restart the queue scan.
 */
static int
aio_free_entry(struct kaiocb *job)
{
	struct kaioinfo *ki;
	struct aioliojob *lj;
	struct proc *p;

	p = job->userproc;
	MPASS(curproc == p);
	ki = p->p_aioinfo;
	MPASS(ki != NULL);

	AIO_LOCK_ASSERT(ki, MA_OWNED);
	MPASS(job->jobflags & KAIOCB_FINISHED);

	atomic_subtract_int(&num_queue_count, 1);

	ki->kaio_count--;
	MPASS(ki->kaio_count >= 0);

	TAILQ_REMOVE(&ki->kaio_done, job, plist);
	TAILQ_REMOVE(&ki->kaio_all, job, allist);

	lj = job->lio;
	if (lj) {
		lj->lioj_count--;
		lj->lioj_finished_count--;

		if (lj->lioj_count == 0) {
			TAILQ_REMOVE(&ki->kaio_liojoblist, lj, lioj_list);
			/* lio is going away, we need to destroy any knotes */
			knlist_delete(&lj->klist, curthread, 1);
			PROC_LOCK(p);
			sigqueue_take(&lj->lioj_ksi);
			PROC_UNLOCK(p);
			uma_zfree(aiolio_zone, lj);
		}
	}

	/* job is going away, we need to destroy any knotes */
	knlist_delete(&job->klist, curthread, 1);
	PROC_LOCK(p);
	sigqueue_take(&job->ksi);
	PROC_UNLOCK(p);

	AIO_UNLOCK(ki);

	/*
	 * The thread argument here is used to find the owning process
	 * and is also passed to fo_close() which may pass it to various
	 * places such as devsw close() routines.  Because of that, we
	 * need a thread pointer from the process owning the job that is
	 * persistent and won't disappear out from under us or move to
	 * another process.
	 *
	 * Currently, all the callers of this function call it to remove
	 * a kaiocb from the current process' job list either via a
	 * syscall or due to the current process calling exit() or
	 * execve().  Thus, we know that p == curproc.  We also know that
	 * curthread can't exit since we are curthread.
	 *
	 * Therefore, we use curthread as the thread to pass to
	 * knlist_delete().  This does mean that it is possible for the
	 * thread pointer at close time to differ from the thread pointer
	 * at open time, but this is already true of file descriptors in
	 * a multithreaded process.
	 */
	if (job->fd_file)
		fdrop(job->fd_file, curthread);
	crfree(job->cred);
	uma_zfree(aiocb_zone, job);
	AIO_LOCK(ki);

	return (0);
}

static void
aio_proc_rundown_exec(void *arg, struct proc *p,
    struct image_params *imgp __unused)
{
   	aio_proc_rundown(arg, p);
}

static int
aio_cancel_job(struct proc *p, struct kaioinfo *ki, struct kaiocb *job)
{
	aio_cancel_fn_t *func;
	int cancelled;

	AIO_LOCK_ASSERT(ki, MA_OWNED);
	if (job->jobflags & (KAIOCB_CANCELLED | KAIOCB_FINISHED))
		return (0);
	MPASS((job->jobflags & KAIOCB_CANCELLING) == 0);
	job->jobflags |= KAIOCB_CANCELLED;

	func = job->cancel_fn;

	/*
	 * If there is no cancel routine, just leave the job marked as
	 * cancelled.  The job should be in active use by a caller who
	 * should complete it normally or when it fails to install a
	 * cancel routine.
	 */
	if (func == NULL)
		return (0);

	/*
	 * Set the CANCELLING flag so that aio_complete() will defer
	 * completions of this job.  This prevents the job from being
	 * freed out from under the cancel callback.  After the
	 * callback any deferred completion (whether from the callback
	 * or any other source) will be completed.
	 */
	job->jobflags |= KAIOCB_CANCELLING;
	AIO_UNLOCK(ki);
	func(job);
	AIO_LOCK(ki);
	job->jobflags &= ~KAIOCB_CANCELLING;
	if (job->jobflags & KAIOCB_FINISHED) {
		cancelled = job->uaiocb._aiocb_private.error == ECANCELED;
		TAILQ_REMOVE(&ki->kaio_jobqueue, job, plist);
		aio_bio_done_notify(p, job);
	} else {
		/*
		 * The cancel callback might have scheduled an
		 * operation to cancel this request, but it is
		 * only counted as cancelled if the request is
		 * cancelled when the callback returns.
		 */
		cancelled = 0;
	}
	return (cancelled);
}

/*
 * Rundown the jobs for a given process.
 */
static void
aio_proc_rundown(void *arg, struct proc *p)
{
	struct kaioinfo *ki;
	struct aioliojob *lj;
	struct kaiocb *job, *jobn;

	KASSERT(curthread->td_proc == p,
	    ("%s: called on non-curproc", __func__));
	ki = p->p_aioinfo;
	if (ki == NULL)
		return;

	AIO_LOCK(ki);
	ki->kaio_flags |= KAIO_RUNDOWN;

restart:

	/*
	 * Try to cancel all pending requests. This code simulates
	 * aio_cancel on all pending I/O requests.
	 */
	TAILQ_FOREACH_SAFE(job, &ki->kaio_jobqueue, plist, jobn) {
		aio_cancel_job(p, ki, job);
	}

	/* Wait for all running I/O to be finished */
	if (TAILQ_FIRST(&ki->kaio_jobqueue) || ki->kaio_active_count != 0) {
		ki->kaio_flags |= KAIO_WAKEUP;
		msleep(&p->p_aioinfo, AIO_MTX(ki), PRIBIO, "aioprn", hz);
		goto restart;
	}

	/* Free all completed I/O requests. */
	while ((job = TAILQ_FIRST(&ki->kaio_done)) != NULL)
		aio_free_entry(job);

	while ((lj = TAILQ_FIRST(&ki->kaio_liojoblist)) != NULL) {
		if (lj->lioj_count == 0) {
			TAILQ_REMOVE(&ki->kaio_liojoblist, lj, lioj_list);
			knlist_delete(&lj->klist, curthread, 1);
			PROC_LOCK(p);
			sigqueue_take(&lj->lioj_ksi);
			PROC_UNLOCK(p);
			uma_zfree(aiolio_zone, lj);
		} else {
			panic("LIO job not cleaned up: C:%d, FC:%d\n",
			    lj->lioj_count, lj->lioj_finished_count);
		}
	}
	AIO_UNLOCK(ki);
	taskqueue_drain(taskqueue_aiod_kick, &ki->kaio_task);
	taskqueue_drain(taskqueue_aiod_kick, &ki->kaio_sync_task);
	mtx_destroy(&ki->kaio_mtx);
	uma_zfree(kaio_zone, ki);
	p->p_aioinfo = NULL;
}

/*
 * Select a job to run (called by an AIO daemon).
 */
static struct kaiocb *
aio_selectjob(struct aioproc *aiop)
{
	struct kaiocb *job;
	struct kaioinfo *ki;
	struct proc *userp;

	mtx_assert(&aio_job_mtx, MA_OWNED);
restart:
	TAILQ_FOREACH(job, &aio_jobs, list) {
		userp = job->userproc;
		ki = userp->p_aioinfo;

		if (ki->kaio_active_count < max_aio_per_proc) {
			TAILQ_REMOVE(&aio_jobs, job, list);
			if (!aio_clear_cancel_function(job))
				goto restart;

			/* Account for currently active jobs. */
			ki->kaio_active_count++;
			break;
		}
	}
	return (job);
}

/*
 * Move all data to a permanent storage device.  This code
 * simulates the fsync syscall.
 */
static int
aio_fsync_vnode(struct thread *td, struct vnode *vp)
{
	struct mount *mp;
	int error;

	if ((error = vn_start_write(vp, &mp, V_WAIT | PCATCH)) != 0)
		goto drop;
	vn_lock(vp, LK_EXCLUSIVE | LK_RETRY);
	if (vp->v_object != NULL) {
		VM_OBJECT_WLOCK(vp->v_object);
		vm_object_page_clean(vp->v_object, 0, 0, 0);
		VM_OBJECT_WUNLOCK(vp->v_object);
	}
	error = VOP_FSYNC(vp, MNT_WAIT, td);

	VOP_UNLOCK(vp, 0);
	vn_finished_write(mp);
drop:
	return (error);
}

/*
 * The AIO processing activity for LIO_READ/LIO_WRITE.  This is the code that
 * does the I/O request for the non-bio version of the operations.  The normal
 * vn operations are used, and this code should work in all instances for every
 * type of file, including pipes, sockets, fifos, and regular files.
 *
 * XXX I don't think it works well for socket, pipe, and fifo.
 */
static void
aio_process_rw(struct kaiocb *job)
{
	struct ucred *td_savedcred;
	struct thread *td;
	struct aiocb *cb;
	struct file *fp;
	struct uio auio;
	struct iovec aiov;
	ssize_t cnt;
	long msgsnd_st, msgsnd_end;
	long msgrcv_st, msgrcv_end;
	long oublock_st, oublock_end;
	long inblock_st, inblock_end;
	int error;

	KASSERT(job->uaiocb.aio_lio_opcode == LIO_READ ||
	    job->uaiocb.aio_lio_opcode == LIO_WRITE,
	    ("%s: opcode %d", __func__, job->uaiocb.aio_lio_opcode));

	aio_switch_vmspace(job);
	td = curthread;
	td_savedcred = td->td_ucred;
	td->td_ucred = job->cred;
	cb = &job->uaiocb;
	fp = job->fd_file;

	aiov.iov_base = (void *)(uintptr_t)cb->aio_buf;
	aiov.iov_len = cb->aio_nbytes;

	auio.uio_iov = &aiov;
	auio.uio_iovcnt = 1;
	auio.uio_offset = cb->aio_offset;
	auio.uio_resid = cb->aio_nbytes;
	cnt = cb->aio_nbytes;
	auio.uio_segflg = UIO_USERSPACE;
	auio.uio_td = td;

	msgrcv_st = td->td_ru.ru_msgrcv;
	msgsnd_st = td->td_ru.ru_msgsnd;
	inblock_st = td->td_ru.ru_inblock;
	oublock_st = td->td_ru.ru_oublock;

	/*
	 * aio_aqueue() acquires a reference to the file that is
	 * released in aio_free_entry().
	 */
	if (cb->aio_lio_opcode == LIO_READ) {
		auio.uio_rw = UIO_READ;
		if (auio.uio_resid == 0)
			error = 0;
		else
			error = fo_read(fp, &auio, fp->f_cred, FOF_OFFSET, td);
	} else {
		if (fp->f_type == DTYPE_VNODE)
			bwillwrite();
		auio.uio_rw = UIO_WRITE;
		error = fo_write(fp, &auio, fp->f_cred, FOF_OFFSET, td);
	}
	msgrcv_end = td->td_ru.ru_msgrcv;
	msgsnd_end = td->td_ru.ru_msgsnd;
	inblock_end = td->td_ru.ru_inblock;
	oublock_end = td->td_ru.ru_oublock;

	job->msgrcv = msgrcv_end - msgrcv_st;
	job->msgsnd = msgsnd_end - msgsnd_st;
	job->inblock = inblock_end - inblock_st;
	job->outblock = oublock_end - oublock_st;

	if ((error) && (auio.uio_resid != cnt)) {
		if (error == ERESTART || error == EINTR || error == EWOULDBLOCK)
			error = 0;
		if ((error == EPIPE) && (cb->aio_lio_opcode == LIO_WRITE)) {
			PROC_LOCK(job->userproc);
			kern_psignal(job->userproc, SIGPIPE);
			PROC_UNLOCK(job->userproc);
		}
	}

	cnt -= auio.uio_resid;
	td->td_ucred = td_savedcred;
	if (error)
		aio_complete(job, -1, error);
	else
		aio_complete(job, cnt, 0);
}

static void
aio_process_sync(struct kaiocb *job)
{
	struct thread *td = curthread;
	struct ucred *td_savedcred = td->td_ucred;
	struct file *fp = job->fd_file;
	int error = 0;

	KASSERT(job->uaiocb.aio_lio_opcode == LIO_SYNC,
	    ("%s: opcode %d", __func__, job->uaiocb.aio_lio_opcode));

	td->td_ucred = job->cred;
	if (fp->f_vnode != NULL)
		error = aio_fsync_vnode(td, fp->f_vnode);
	td->td_ucred = td_savedcred;
	if (error)
		aio_complete(job, -1, error);
	else
		aio_complete(job, 0, 0);
}

static void
aio_process_mlock(struct kaiocb *job)
{
	struct aiocb *cb = &job->uaiocb;
	int error;

	KASSERT(job->uaiocb.aio_lio_opcode == LIO_MLOCK,
	    ("%s: opcode %d", __func__, job->uaiocb.aio_lio_opcode));

	aio_switch_vmspace(job);
	error = kern_mlock(job->userproc, job->cred,
	    __DEVOLATILE(uintptr_t, cb->aio_buf), cb->aio_nbytes);
	aio_complete(job, error != 0 ? -1 : 0, error);
}

static void
aio_bio_done_notify(struct proc *userp, struct kaiocb *job)
{
	struct aioliojob *lj;
	struct kaioinfo *ki;
	struct kaiocb *sjob, *sjobn;
	int lj_done;
	bool schedule_fsync;

	ki = userp->p_aioinfo;
	AIO_LOCK_ASSERT(ki, MA_OWNED);
	lj = job->lio;
	lj_done = 0;
	if (lj) {
		lj->lioj_finished_count++;
		if (lj->lioj_count == lj->lioj_finished_count)
			lj_done = 1;
	}
	TAILQ_INSERT_TAIL(&ki->kaio_done, job, plist);
	MPASS(job->jobflags & KAIOCB_FINISHED);

	if (ki->kaio_flags & KAIO_RUNDOWN)
		goto notification_done;

	if (job->uaiocb.aio_sigevent.sigev_notify == SIGEV_SIGNAL ||
	    job->uaiocb.aio_sigevent.sigev_notify == SIGEV_THREAD_ID)
		aio_sendsig(userp, &job->uaiocb.aio_sigevent, &job->ksi);

	KNOTE_LOCKED(&job->klist, 1);

	if (lj_done) {
		if (lj->lioj_signal.sigev_notify == SIGEV_KEVENT) {
			lj->lioj_flags |= LIOJ_KEVENT_POSTED;
			KNOTE_LOCKED(&lj->klist, 1);
		}
		if ((lj->lioj_flags & (LIOJ_SIGNAL|LIOJ_SIGNAL_POSTED))
		    == LIOJ_SIGNAL
		    && (lj->lioj_signal.sigev_notify == SIGEV_SIGNAL ||
		        lj->lioj_signal.sigev_notify == SIGEV_THREAD_ID)) {
			aio_sendsig(userp, &lj->lioj_signal, &lj->lioj_ksi);
			lj->lioj_flags |= LIOJ_SIGNAL_POSTED;
		}
	}

notification_done:
	if (job->jobflags & KAIOCB_CHECKSYNC) {
		schedule_fsync = false;
		TAILQ_FOREACH_SAFE(sjob, &ki->kaio_syncqueue, list, sjobn) {
			if (job->fd_file != sjob->fd_file ||
			    job->seqno >= sjob->seqno)
				continue;
			if (--sjob->pending > 0)
				continue;
			TAILQ_REMOVE(&ki->kaio_syncqueue, sjob, list);
			if (!aio_clear_cancel_function_locked(sjob))
				continue;
			TAILQ_INSERT_TAIL(&ki->kaio_syncready, sjob, list);
			schedule_fsync = true;
		}
		if (schedule_fsync)
			taskqueue_enqueue(taskqueue_aiod_kick,
			    &ki->kaio_sync_task);
	}
	if (ki->kaio_flags & KAIO_WAKEUP) {
		ki->kaio_flags &= ~KAIO_WAKEUP;
		wakeup(&userp->p_aioinfo);
	}
}

static void
aio_schedule_fsync(void *context, int pending)
{
	struct kaioinfo *ki;
	struct kaiocb *job;

	ki = context;
	AIO_LOCK(ki);
	while (!TAILQ_EMPTY(&ki->kaio_syncready)) {
		job = TAILQ_FIRST(&ki->kaio_syncready);
		TAILQ_REMOVE(&ki->kaio_syncready, job, list);
		AIO_UNLOCK(ki);
		aio_schedule(job, aio_process_sync);
		AIO_LOCK(ki);
	}
	AIO_UNLOCK(ki);
}

bool
aio_cancel_cleared(struct kaiocb *job)
{

	/*
	 * The caller should hold the same queue lock held when
	 * aio_clear_cancel_function() was called and set this flag
	 * ensuring this check sees an up-to-date value.  However,
	 * there is no way to assert that.
	 */
	return ((job->jobflags & KAIOCB_CLEARED) != 0);
}

static bool
aio_clear_cancel_function_locked(struct kaiocb *job)
{

	AIO_LOCK_ASSERT(job->userproc->p_aioinfo, MA_OWNED);
	MPASS(job->cancel_fn != NULL);
	if (job->jobflags & KAIOCB_CANCELLING) {
		job->jobflags |= KAIOCB_CLEARED;
		return (false);
	}
	job->cancel_fn = NULL;
	return (true);
}

bool
aio_clear_cancel_function(struct kaiocb *job)
{
	struct kaioinfo *ki;
	bool ret;

	ki = job->userproc->p_aioinfo;
	AIO_LOCK(ki);
	ret = aio_clear_cancel_function_locked(job);
	AIO_UNLOCK(ki);
	return (ret);
}

static bool
aio_set_cancel_function_locked(struct kaiocb *job, aio_cancel_fn_t *func)
{

	AIO_LOCK_ASSERT(job->userproc->p_aioinfo, MA_OWNED);
	if (job->jobflags & KAIOCB_CANCELLED)
		return (false);
	job->cancel_fn = func;
	return (true);
}

bool
aio_set_cancel_function(struct kaiocb *job, aio_cancel_fn_t *func)
{
	struct kaioinfo *ki;
	bool ret;

	ki = job->userproc->p_aioinfo;
	AIO_LOCK(ki);
	ret = aio_set_cancel_function_locked(job, func);
	AIO_UNLOCK(ki);
	return (ret);
}

void
aio_complete(struct kaiocb *job, long status, int error)
{
	struct kaioinfo *ki;
	struct proc *userp;

	job->uaiocb._aiocb_private.error = error;
	job->uaiocb._aiocb_private.status = status;

	userp = job->userproc;
	ki = userp->p_aioinfo;

	AIO_LOCK(ki);
	KASSERT(!(job->jobflags & KAIOCB_FINISHED),
	    ("duplicate aio_complete"));
	job->jobflags |= KAIOCB_FINISHED;
	if ((job->jobflags & (KAIOCB_QUEUEING | KAIOCB_CANCELLING)) == 0) {
		TAILQ_REMOVE(&ki->kaio_jobqueue, job, plist);
		aio_bio_done_notify(userp, job);
	}
	AIO_UNLOCK(ki);
}

void
aio_cancel(struct kaiocb *job)
{

	aio_complete(job, -1, ECANCELED);
}

void
aio_switch_vmspace(struct kaiocb *job)
{

	vmspace_switch_aio(job->userproc->p_vmspace);
}

/*
 * The AIO daemon, most of the actual work is done in aio_process_*,
 * but the setup (and address space mgmt) is done in this routine.
 */
static void
aio_daemon(void *_id)
{
	struct kaiocb *job;
	struct aioproc *aiop;
	struct kaioinfo *ki;
	struct proc *p;
	struct vmspace *myvm;
	struct thread *td = curthread;
	int id = (intptr_t)_id;

	/*
	 * Grab an extra reference on the daemon's vmspace so that it
	 * doesn't get freed by jobs that switch to a different
	 * vmspace.
	 */
	p = td->td_proc;
	myvm = vmspace_acquire_ref(p);

	KASSERT(p->p_textvp == NULL, ("kthread has a textvp"));

	/*
	 * Allocate and ready the aio control info.  There is one aiop structure
	 * per daemon.
	 */
	aiop = uma_zalloc(aiop_zone, M_WAITOK);
	aiop->aioproc = p;
	aiop->aioprocflags = 0;

	/*
	 * Wakeup parent process.  (Parent sleeps to keep from blasting away
	 * and creating too many daemons.)
	 */
	sema_post(&aio_newproc_sem);

	mtx_lock(&aio_job_mtx);
	for (;;) {
		/*
		 * Take daemon off of free queue
		 */
		if (aiop->aioprocflags & AIOP_FREE) {
			TAILQ_REMOVE(&aio_freeproc, aiop, list);
			aiop->aioprocflags &= ~AIOP_FREE;
		}

		/*
		 * Check for jobs.
		 */
		while ((job = aio_selectjob(aiop)) != NULL) {
			mtx_unlock(&aio_job_mtx);

			ki = job->userproc->p_aioinfo;
			job->handle_fn(job);

			mtx_lock(&aio_job_mtx);
			/* Decrement the active job count. */
			ki->kaio_active_count--;
		}

		/*
		 * Disconnect from user address space.
		 */
		if (p->p_vmspace != myvm) {
			mtx_unlock(&aio_job_mtx);
			vmspace_switch_aio(myvm);
			mtx_lock(&aio_job_mtx);
			/*
			 * We have to restart to avoid race, we only sleep if
			 * no job can be selected.
			 */
			continue;
		}

		mtx_assert(&aio_job_mtx, MA_OWNED);

		TAILQ_INSERT_HEAD(&aio_freeproc, aiop, list);
		aiop->aioprocflags |= AIOP_FREE;

		/*
		 * If daemon is inactive for a long time, allow it to exit,
		 * thereby freeing resources.
		 */
		if (msleep(p, &aio_job_mtx, PRIBIO, "aiordy",
		    aiod_lifetime) == EWOULDBLOCK && TAILQ_EMPTY(&aio_jobs) &&
		    (aiop->aioprocflags & AIOP_FREE) &&
		    num_aio_procs > target_aio_procs)
			break;
	}
	TAILQ_REMOVE(&aio_freeproc, aiop, list);
	num_aio_procs--;
	mtx_unlock(&aio_job_mtx);
	uma_zfree(aiop_zone, aiop);
	free_unr(aiod_unr, id);
	vmspace_free(myvm);

	KASSERT(p->p_vmspace == myvm,
	    ("AIOD: bad vmspace for exiting daemon"));
	KASSERT(myvm->vm_refcnt > 1,
	    ("AIOD: bad vm refcnt for exiting daemon: %d", myvm->vm_refcnt));
	kproc_exit(0);
}

/*
 * Create a new AIO daemon. This is mostly a kernel-thread fork routine. The
 * AIO daemon modifies its environment itself.
 */
static int
aio_newproc(int *start)
{
	int error;
	struct proc *p;
	int id;

	id = alloc_unr(aiod_unr);
	error = kproc_create(aio_daemon, (void *)(intptr_t)id, &p,
		RFNOWAIT, 0, "aiod%d", id);
	if (error == 0) {
		/*
		 * Wait until daemon is started.
		 */
		sema_wait(&aio_newproc_sem);
		mtx_lock(&aio_job_mtx);
		num_aio_procs++;
		if (start != NULL)
			(*start)--;
		mtx_unlock(&aio_job_mtx);
	} else {
		free_unr(aiod_unr, id);
	}
	return (error);
}

/*
 * Try the high-performance, low-overhead bio method for eligible
 * VCHR devices.  This method doesn't use an aio helper thread, and
 * thus has very low overhead.
 *
 * Assumes that the caller, aio_aqueue(), has incremented the file
 * structure's reference count, preventing its deallocation for the
 * duration of this call.
 */
static int
aio_qbio(struct proc *p, struct kaiocb *job)
{
	struct aiocb *cb;
	struct file *fp;
	struct bio *bp;
	struct buf *pbuf;
	struct vnode *vp;
	struct cdevsw *csw;
	struct cdev *dev;
	struct kaioinfo *ki;
	int error, ref, poff;
	vm_prot_t prot;

	cb = &job->uaiocb;
	fp = job->fd_file;

	if (!(cb->aio_lio_opcode == LIO_WRITE ||
	    cb->aio_lio_opcode == LIO_READ))
		return (-1);
	if (fp == NULL || fp->f_type != DTYPE_VNODE)
		return (-1);

	vp = fp->f_vnode;
	if (vp->v_type != VCHR)
		return (-1);
	if (vp->v_bufobj.bo_bsize == 0)
		return (-1);
	if (cb->aio_nbytes % vp->v_bufobj.bo_bsize)
		return (-1);

	ref = 0;
	csw = devvn_refthread(vp, &dev, &ref);
	if (csw == NULL)
		return (ENXIO);

	if ((csw->d_flags & D_DISK) == 0) {
		error = -1;
		goto unref;
	}
	if (cb->aio_nbytes > dev->si_iosize_max) {
		error = -1;
		goto unref;
	}

	ki = p->p_aioinfo;
	poff = (vm_offset_t)cb->aio_buf & PAGE_MASK;
	if ((dev->si_flags & SI_UNMAPPED) && unmapped_buf_allowed) {
		if (cb->aio_nbytes > MAXPHYS) {
			error = -1;
			goto unref;
		}

		pbuf = NULL;
	} else {
		if (cb->aio_nbytes > MAXPHYS - poff) {
			error = -1;
			goto unref;
		}
		if (ki->kaio_buffer_count >= max_buf_aio) {
			error = EAGAIN;
			goto unref;
		}

		job->pbuf = pbuf = uma_zalloc(pbuf_zone, M_WAITOK);
		BUF_KERNPROC(pbuf);
		AIO_LOCK(ki);
		ki->kaio_buffer_count++;
		AIO_UNLOCK(ki);
	}
	job->bp = bp = g_alloc_bio();

	bp->bio_length = cb->aio_nbytes;
	bp->bio_bcount = cb->aio_nbytes;
	bp->bio_done = aio_biowakeup;
	bp->bio_data = (void *)(uintptr_t)cb->aio_buf;
	bp->bio_offset = cb->aio_offset;
	bp->bio_cmd = cb->aio_lio_opcode == LIO_WRITE ? BIO_WRITE : BIO_READ;
	bp->bio_dev = dev;
	bp->bio_caller1 = (void *)job;

	prot = VM_PROT_READ;
	if (cb->aio_lio_opcode == LIO_READ)
		prot |= VM_PROT_WRITE;	/* Less backwards than it looks */
	job->npages = vm_fault_quick_hold_pages(&curproc->p_vmspace->vm_map,
	    (vm_offset_t)bp->bio_data, bp->bio_length, prot, job->pages,
	    nitems(job->pages));
	if (job->npages < 0) {
		error = EFAULT;
		goto doerror;
	}
	if (pbuf != NULL) {
		pmap_qenter((vm_offset_t)pbuf->b_data,
		    job->pages, job->npages);
		bp->bio_data = pbuf->b_data + poff;
		atomic_add_int(&num_buf_aio, 1);
	} else {
		bp->bio_ma = job->pages;
		bp->bio_ma_n = job->npages;
		bp->bio_ma_offset = poff;
		bp->bio_data = unmapped_buf;
		bp->bio_flags |= BIO_UNMAPPED;
		atomic_add_int(&num_unmapped_aio, 1);
	}

	/* Perform transfer. */
	csw->d_strategy(bp);
	dev_relthread(dev, ref);
	return (0);

doerror:
	if (pbuf != NULL) {
		AIO_LOCK(ki);
		ki->kaio_buffer_count--;
		AIO_UNLOCK(ki);
		uma_zfree(pbuf_zone, pbuf);
		job->pbuf = NULL;
	}
	g_destroy_bio(bp);
	job->bp = NULL;
unref:
	dev_relthread(dev, ref);
	return (error);
}

#ifdef COMPAT_FREEBSD6
static int
convert_old_sigevent(struct osigevent *osig, struct sigevent *nsig)
{

	/*
	 * Only SIGEV_NONE, SIGEV_SIGNAL, and SIGEV_KEVENT are
	 * supported by AIO with the old sigevent structure.
	 */
	nsig->sigev_notify = osig->sigev_notify;
	switch (nsig->sigev_notify) {
	case SIGEV_NONE:
		break;
	case SIGEV_SIGNAL:
		nsig->sigev_signo = osig->__sigev_u.__sigev_signo;
		break;
	case SIGEV_KEVENT:
		nsig->sigev_notify_kqueue =
		    osig->__sigev_u.__sigev_notify_kqueue;
		nsig->sigev_value.sival_ptr = osig->sigev_value.sival_ptr;
		break;
	default:
		return (EINVAL);
	}
	return (0);
}

static int
aiocb_copyin_old_sigevent(struct aiocb *ujob, struct aiocb *kjob)
{
	struct oaiocb *ojob;
	int error;

	bzero(kjob, sizeof(struct aiocb));
	error = copyin(ujob, kjob, sizeof(struct oaiocb));
	if (error)
		return (error);
	ojob = (struct oaiocb *)kjob;
	return (convert_old_sigevent(&ojob->aio_sigevent, &kjob->aio_sigevent));
}
#endif

static int
aiocb_copyin(struct aiocb *ujob, struct aiocb *kjob)
{

	return (copyin(ujob, kjob, sizeof(struct aiocb)));
}

static long
aiocb_fetch_status(struct aiocb *ujob)
{

	return (fuword(&ujob->_aiocb_private.status));
}

static long
aiocb_fetch_error(struct aiocb *ujob)
{

	return (fuword(&ujob->_aiocb_private.error));
}

static int
aiocb_store_status(struct aiocb *ujob, long status)
{

	return (suword(&ujob->_aiocb_private.status, status));
}

static int
aiocb_store_error(struct aiocb *ujob, long error)
{

	return (suword(&ujob->_aiocb_private.error, error));
}

static int
aiocb_store_kernelinfo(struct aiocb *ujob, long jobref)
{

	return (suword(&ujob->_aiocb_private.kernelinfo, jobref));
}

static int
aiocb_store_aiocb(struct aiocb **ujobp, struct aiocb *ujob)
{

	return (suword(ujobp, (long)ujob));
}

static struct aiocb_ops aiocb_ops = {
	.copyin = aiocb_copyin,
	.fetch_status = aiocb_fetch_status,
	.fetch_error = aiocb_fetch_error,
	.store_status = aiocb_store_status,
	.store_error = aiocb_store_error,
	.store_kernelinfo = aiocb_store_kernelinfo,
	.store_aiocb = aiocb_store_aiocb,
};

#ifdef COMPAT_FREEBSD6
static struct aiocb_ops aiocb_ops_osigevent = {
	.copyin = aiocb_copyin_old_sigevent,
	.fetch_status = aiocb_fetch_status,
	.fetch_error = aiocb_fetch_error,
	.store_status = aiocb_store_status,
	.store_error = aiocb_store_error,
	.store_kernelinfo = aiocb_store_kernelinfo,
	.store_aiocb = aiocb_store_aiocb,
};
#endif

/*
 * Queue a new AIO request.  Choosing either the threaded or direct bio VCHR
 * technique is done in this code.
 */
int
aio_aqueue(struct thread *td, struct aiocb *ujob, struct aioliojob *lj,
    int type, struct aiocb_ops *ops)
{
	struct proc *p = td->td_proc;
	struct file *fp;
	struct kaiocb *job;
	struct kaioinfo *ki;
	struct kevent kev;
	int opcode;
	int error;
	int fd, kqfd;
	int jid;
	u_short evflags;

	if (p->p_aioinfo == NULL)
		aio_init_aioinfo(p);

	ki = p->p_aioinfo;

	ops->store_status(ujob, -1);
	ops->store_error(ujob, 0);
	ops->store_kernelinfo(ujob, -1);

	if (num_queue_count >= max_queue_count ||
	    ki->kaio_count >= max_aio_queue_per_proc) {
		ops->store_error(ujob, EAGAIN);
		return (EAGAIN);
	}

	job = uma_zalloc(aiocb_zone, M_WAITOK | M_ZERO);
	knlist_init_mtx(&job->klist, AIO_MTX(ki));

	error = ops->copyin(ujob, &job->uaiocb);
	if (error) {
		ops->store_error(ujob, error);
		uma_zfree(aiocb_zone, job);
		return (error);
	}

	if (job->uaiocb.aio_nbytes > IOSIZE_MAX) {
		uma_zfree(aiocb_zone, job);
		return (EINVAL);
	}

	if (job->uaiocb.aio_sigevent.sigev_notify != SIGEV_KEVENT &&
	    job->uaiocb.aio_sigevent.sigev_notify != SIGEV_SIGNAL &&
	    job->uaiocb.aio_sigevent.sigev_notify != SIGEV_THREAD_ID &&
	    job->uaiocb.aio_sigevent.sigev_notify != SIGEV_NONE) {
		ops->store_error(ujob, EINVAL);
		uma_zfree(aiocb_zone, job);
		return (EINVAL);
	}

	if ((job->uaiocb.aio_sigevent.sigev_notify == SIGEV_SIGNAL ||
	     job->uaiocb.aio_sigevent.sigev_notify == SIGEV_THREAD_ID) &&
		!_SIG_VALID(job->uaiocb.aio_sigevent.sigev_signo)) {
		uma_zfree(aiocb_zone, job);
		return (EINVAL);
	}

	ksiginfo_init(&job->ksi);

	/* Save userspace address of the job info. */
	job->ujob = ujob;

	/* Get the opcode. */
	if (type != LIO_NOP)
		job->uaiocb.aio_lio_opcode = type;
	opcode = job->uaiocb.aio_lio_opcode;

	/*
	 * Validate the opcode and fetch the file object for the specified
	 * file descriptor.
	 *
	 * XXXRW: Moved the opcode validation up here so that we don't
	 * retrieve a file descriptor without knowing what the capabiltity
	 * should be.
	 */
	fd = job->uaiocb.aio_fildes;
	switch (opcode) {
	case LIO_WRITE:
		error = fget_write(td, fd, &cap_pwrite_rights, &fp);
		break;
	case LIO_READ:
		error = fget_read(td, fd, &cap_pread_rights, &fp);
		break;
	case LIO_SYNC:
		error = fget(td, fd, &cap_fsync_rights, &fp);
		break;
	case LIO_MLOCK:
		fp = NULL;
		break;
	case LIO_NOP:
		error = fget(td, fd, &cap_no_rights, &fp);
		break;
	default:
		error = EINVAL;
	}
	if (error) {
		uma_zfree(aiocb_zone, job);
		ops->store_error(ujob, error);
		return (error);
	}

	if (opcode == LIO_SYNC && fp->f_vnode == NULL) {
		error = EINVAL;
		goto aqueue_fail;
	}

	if ((opcode == LIO_READ || opcode == LIO_WRITE) &&
	    job->uaiocb.aio_offset < 0 &&
	    (fp->f_vnode == NULL || fp->f_vnode->v_type != VCHR)) {
		error = EINVAL;
		goto aqueue_fail;
	}

	job->fd_file = fp;

	mtx_lock(&aio_job_mtx);
	jid = jobrefid++;
	job->seqno = jobseqno++;
	mtx_unlock(&aio_job_mtx);
	error = ops->store_kernelinfo(ujob, jid);
	if (error) {
		error = EINVAL;
		goto aqueue_fail;
	}
	job->uaiocb._aiocb_private.kernelinfo = (void *)(intptr_t)jid;

	if (opcode == LIO_NOP) {
		fdrop(fp, td);
		uma_zfree(aiocb_zone, job);
		return (0);
	}

	if (job->uaiocb.aio_sigevent.sigev_notify != SIGEV_KEVENT)
		goto no_kqueue;
	evflags = job->uaiocb.aio_sigevent.sigev_notify_kevent_flags;
	if ((evflags & ~(EV_CLEAR | EV_DISPATCH | EV_ONESHOT)) != 0) {
		error = EINVAL;
		goto aqueue_fail;
	}
	kqfd = job->uaiocb.aio_sigevent.sigev_notify_kqueue;
	memset(&kev, 0, sizeof(kev));
	kev.ident = (uintptr_t)job->ujob;
	kev.filter = EVFILT_AIO;
	kev.flags = EV_ADD | EV_ENABLE | EV_FLAG1 | evflags;
	kev.data = (intptr_t)job;
	kev.udata = job->uaiocb.aio_sigevent.sigev_value.sival_ptr;
	error = kqfd_register(kqfd, &kev, td, M_WAITOK);
	if (error)
		goto aqueue_fail;

no_kqueue:

	ops->store_error(ujob, EINPROGRESS);
	job->uaiocb._aiocb_private.error = EINPROGRESS;
	job->userproc = p;
	job->cred = crhold(td->td_ucred);
	job->jobflags = KAIOCB_QUEUEING;
	job->lio = lj;

	if (opcode == LIO_MLOCK) {
		aio_schedule(job, aio_process_mlock);
		error = 0;
	} else if (fp->f_ops->fo_aio_queue == NULL)
		error = aio_queue_file(fp, job);
	else
		error = fo_aio_queue(fp, job);
	if (error)
		goto aqueue_fail;

	AIO_LOCK(ki);
	job->jobflags &= ~KAIOCB_QUEUEING;
	TAILQ_INSERT_TAIL(&ki->kaio_all, job, allist);
	ki->kaio_count++;
	if (lj)
		lj->lioj_count++;
	atomic_add_int(&num_queue_count, 1);
	if (job->jobflags & KAIOCB_FINISHED) {
		/*
		 * The queue callback completed the request synchronously.
		 * The bulk of the completion is deferred in that case
		 * until this point.
		 */
		aio_bio_done_notify(p, job);
	} else
		TAILQ_INSERT_TAIL(&ki->kaio_jobqueue, job, plist);
	AIO_UNLOCK(ki);
	return (0);

aqueue_fail:
	knlist_delete(&job->klist, curthread, 0);
	if (fp)
		fdrop(fp, td);
	uma_zfree(aiocb_zone, job);
	ops->store_error(ujob, error);
	return (error);
}

static void
aio_cancel_daemon_job(struct kaiocb *job)
{

	mtx_lock(&aio_job_mtx);
	if (!aio_cancel_cleared(job))
		TAILQ_REMOVE(&aio_jobs, job, list);
	mtx_unlock(&aio_job_mtx);
	aio_cancel(job);
}

void
aio_schedule(struct kaiocb *job, aio_handle_fn_t *func)
{

	mtx_lock(&aio_job_mtx);
	if (!aio_set_cancel_function(job, aio_cancel_daemon_job)) {
		mtx_unlock(&aio_job_mtx);
		aio_cancel(job);
		return;
	}
	job->handle_fn = func;
	TAILQ_INSERT_TAIL(&aio_jobs, job, list);
	aio_kick_nowait(job->userproc);
	mtx_unlock(&aio_job_mtx);
}

static void
aio_cancel_sync(struct kaiocb *job)
{
	struct kaioinfo *ki;

	ki = job->userproc->p_aioinfo;
	AIO_LOCK(ki);
	if (!aio_cancel_cleared(job))
		TAILQ_REMOVE(&ki->kaio_syncqueue, job, list);
	AIO_UNLOCK(ki);
	aio_cancel(job);
}

int
aio_queue_file(struct file *fp, struct kaiocb *job)
{
	struct kaioinfo *ki;
	struct kaiocb *job2;
	struct vnode *vp;
	struct mount *mp;
	int error;
	bool safe;

	ki = job->userproc->p_aioinfo;
	error = aio_qbio(job->userproc, job);
	if (error >= 0)
		return (error);
	safe = false;
	if (fp->f_type == DTYPE_VNODE) {
		vp = fp->f_vnode;
		if (vp->v_type == VREG || vp->v_type == VDIR) {
			mp = fp->f_vnode->v_mount;
			if (mp == NULL || (mp->mnt_flag & MNT_LOCAL) != 0)
				safe = true;
		}
	}
	if (!(safe || enable_aio_unsafe)) {
		counted_warning(&unsafe_warningcnt,
		    "is attempting to use unsafe AIO requests");
		return (EOPNOTSUPP);
	}

	switch (job->uaiocb.aio_lio_opcode) {
	case LIO_READ:
	case LIO_WRITE:
		aio_schedule(job, aio_process_rw);
		error = 0;
		break;
	case LIO_SYNC:
		AIO_LOCK(ki);
		TAILQ_FOREACH(job2, &ki->kaio_jobqueue, plist) {
			if (job2->fd_file == job->fd_file &&
			    job2->uaiocb.aio_lio_opcode != LIO_SYNC &&
			    job2->seqno < job->seqno) {
				job2->jobflags |= KAIOCB_CHECKSYNC;
				job->pending++;
			}
		}
		if (job->pending != 0) {
			if (!aio_set_cancel_function_locked(job,
				aio_cancel_sync)) {
				AIO_UNLOCK(ki);
				aio_cancel(job);
				return (0);
			}
			TAILQ_INSERT_TAIL(&ki->kaio_syncqueue, job, list);
			AIO_UNLOCK(ki);
			return (0);
		}
		AIO_UNLOCK(ki);
		aio_schedule(job, aio_process_sync);
		error = 0;
		break;
	default:
		error = EINVAL;
	}
	return (error);
}

static void
aio_kick_nowait(struct proc *userp)
{
	struct kaioinfo *ki = userp->p_aioinfo;
	struct aioproc *aiop;

	mtx_assert(&aio_job_mtx, MA_OWNED);
	if ((aiop = TAILQ_FIRST(&aio_freeproc)) != NULL) {
		TAILQ_REMOVE(&aio_freeproc, aiop, list);
		aiop->aioprocflags &= ~AIOP_FREE;
		wakeup(aiop->aioproc);
	} else if (num_aio_resv_start + num_aio_procs < max_aio_procs &&
	    ki->kaio_active_count + num_aio_resv_start < max_aio_per_proc) {
		taskqueue_enqueue(taskqueue_aiod_kick, &ki->kaio_task);
	}
}

static int
aio_kick(struct proc *userp)
{
	struct kaioinfo *ki = userp->p_aioinfo;
	struct aioproc *aiop;
	int error, ret = 0;

	mtx_assert(&aio_job_mtx, MA_OWNED);
retryproc:
	if ((aiop = TAILQ_FIRST(&aio_freeproc)) != NULL) {
		TAILQ_REMOVE(&aio_freeproc, aiop, list);
		aiop->aioprocflags &= ~AIOP_FREE;
		wakeup(aiop->aioproc);
	} else if (num_aio_resv_start + num_aio_procs < max_aio_procs &&
	    ki->kaio_active_count + num_aio_resv_start < max_aio_per_proc) {
		num_aio_resv_start++;
		mtx_unlock(&aio_job_mtx);
		error = aio_newproc(&num_aio_resv_start);
		mtx_lock(&aio_job_mtx);
		if (error) {
			num_aio_resv_start--;
			goto retryproc;
		}
	} else {
		ret = -1;
	}
	return (ret);
}

static void
aio_kick_helper(void *context, int pending)
{
	struct proc *userp = context;

	mtx_lock(&aio_job_mtx);
	while (--pending >= 0) {
		if (aio_kick(userp))
			break;
	}
	mtx_unlock(&aio_job_mtx);
}

/*
 * Support the aio_return system call, as a side-effect, kernel resources are
 * released.
 */
static int
kern_aio_return(struct thread *td, struct aiocb *ujob, struct aiocb_ops *ops)
{
	struct proc *p = td->td_proc;
	struct kaiocb *job;
	struct kaioinfo *ki;
	long status, error;

	ki = p->p_aioinfo;
	if (ki == NULL)
		return (EINVAL);
	AIO_LOCK(ki);
	TAILQ_FOREACH(job, &ki->kaio_done, plist) {
		if (job->ujob == ujob)
			break;
	}
	if (job != NULL) {
		MPASS(job->jobflags & KAIOCB_FINISHED);
		status = job->uaiocb._aiocb_private.status;
		error = job->uaiocb._aiocb_private.error;
		td->td_retval[0] = status;
		td->td_ru.ru_oublock += job->outblock;
		td->td_ru.ru_inblock += job->inblock;
		td->td_ru.ru_msgsnd += job->msgsnd;
		td->td_ru.ru_msgrcv += job->msgrcv;
		aio_free_entry(job);
		AIO_UNLOCK(ki);
		ops->store_error(ujob, error);
		ops->store_status(ujob, status);
	} else {
		error = EINVAL;
		AIO_UNLOCK(ki);
	}
	return (error);
}

int
sys_aio_return(struct thread *td, struct aio_return_args *uap)
{

	return (kern_aio_return(td, uap->aiocbp, &aiocb_ops));
}

/*
 * Allow a process to wakeup when any of the I/O requests are completed.
 */
static int
kern_aio_suspend(struct thread *td, int njoblist, struct aiocb **ujoblist,
    struct timespec *ts)
{
	struct proc *p = td->td_proc;
	struct timeval atv;
	struct kaioinfo *ki;
	struct kaiocb *firstjob, *job;
	int error, i, timo;

	timo = 0;
	if (ts) {
		if (ts->tv_nsec < 0 || ts->tv_nsec >= 1000000000)
			return (EINVAL);

		TIMESPEC_TO_TIMEVAL(&atv, ts);
		if (itimerfix(&atv))
			return (EINVAL);
		timo = tvtohz(&atv);
	}

	ki = p->p_aioinfo;
	if (ki == NULL)
		return (EAGAIN);

	if (njoblist == 0)
		return (0);

	AIO_LOCK(ki);
	for (;;) {
		firstjob = NULL;
		error = 0;
		TAILQ_FOREACH(job, &ki->kaio_all, allist) {
			for (i = 0; i < njoblist; i++) {
				if (job->ujob == ujoblist[i]) {
					if (firstjob == NULL)
						firstjob = job;
					if (job->jobflags & KAIOCB_FINISHED)
						goto RETURN;
				}
			}
		}
		/* All tasks were finished. */
		if (firstjob == NULL)
			break;

		ki->kaio_flags |= KAIO_WAKEUP;
		error = msleep(&p->p_aioinfo, AIO_MTX(ki), PRIBIO | PCATCH,
		    "aiospn", timo);
		if (error == ERESTART)
			error = EINTR;
		if (error)
			break;
	}
RETURN:
	AIO_UNLOCK(ki);
	return (error);
}

int
sys_aio_suspend(struct thread *td, struct aio_suspend_args *uap)
{
	struct timespec ts, *tsp;
	struct aiocb **ujoblist;
	int error;

	if (uap->nent < 0 || uap->nent > max_aio_queue_per_proc)
		return (EINVAL);

	if (uap->timeout) {
		/* Get timespec struct. */
		if ((error = copyin(uap->timeout, &ts, sizeof(ts))) != 0)
			return (error);
		tsp = &ts;
	} else
		tsp = NULL;

	ujoblist = malloc(uap->nent * sizeof(ujoblist[0]), M_AIOS, M_WAITOK);
	error = copyin(uap->aiocbp, ujoblist, uap->nent * sizeof(ujoblist[0]));
	if (error == 0)
		error = kern_aio_suspend(td, uap->nent, ujoblist, tsp);
	free(ujoblist, M_AIOS);
	return (error);
}

/*
 * aio_cancel cancels any non-bio aio operations not currently in progress.
 */
int
sys_aio_cancel(struct thread *td, struct aio_cancel_args *uap)
{
	struct proc *p = td->td_proc;
	struct kaioinfo *ki;
	struct kaiocb *job, *jobn;
	struct file *fp;
	int error;
	int cancelled = 0;
	int notcancelled = 0;
	struct vnode *vp;

	/* Lookup file object. */
	error = fget(td, uap->fd, &cap_no_rights, &fp);
	if (error)
		return (error);

	ki = p->p_aioinfo;
	if (ki == NULL)
		goto done;

	if (fp->f_type == DTYPE_VNODE) {
		vp = fp->f_vnode;
		if (vn_isdisk(vp, &error)) {
			fdrop(fp, td);
			td->td_retval[0] = AIO_NOTCANCELED;
			return (0);
		}
	}

	AIO_LOCK(ki);
	TAILQ_FOREACH_SAFE(job, &ki->kaio_jobqueue, plist, jobn) {
		if ((uap->fd == job->uaiocb.aio_fildes) &&
		    ((uap->aiocbp == NULL) ||
		     (uap->aiocbp == job->ujob))) {
			if (aio_cancel_job(p, ki, job)) {
				cancelled++;
			} else {
				notcancelled++;
			}
			if (uap->aiocbp != NULL)
				break;
		}
	}
	AIO_UNLOCK(ki);

done:
	fdrop(fp, td);

	if (uap->aiocbp != NULL) {
		if (cancelled) {
			td->td_retval[0] = AIO_CANCELED;
			return (0);
		}
	}

	if (notcancelled) {
		td->td_retval[0] = AIO_NOTCANCELED;
		return (0);
	}

	if (cancelled) {
		td->td_retval[0] = AIO_CANCELED;
		return (0);
	}

	td->td_retval[0] = AIO_ALLDONE;

	return (0);
}

/*
 * aio_error is implemented in the kernel level for compatibility purposes
 * only.  For a user mode async implementation, it would be best to do it in
 * a userland subroutine.
 */
static int
kern_aio_error(struct thread *td, struct aiocb *ujob, struct aiocb_ops *ops)
{
	struct proc *p = td->td_proc;
	struct kaiocb *job;
	struct kaioinfo *ki;
	int status;

	ki = p->p_aioinfo;
	if (ki == NULL) {
		td->td_retval[0] = EINVAL;
		return (0);
	}

	AIO_LOCK(ki);
	TAILQ_FOREACH(job, &ki->kaio_all, allist) {
		if (job->ujob == ujob) {
			if (job->jobflags & KAIOCB_FINISHED)
				td->td_retval[0] =
					job->uaiocb._aiocb_private.error;
			else
				td->td_retval[0] = EINPROGRESS;
			AIO_UNLOCK(ki);
			return (0);
		}
	}
	AIO_UNLOCK(ki);

	/*
	 * Hack for failure of aio_aqueue.
	 */
	status = ops->fetch_status(ujob);
	if (status == -1) {
		td->td_retval[0] = ops->fetch_error(ujob);
		return (0);
	}

	td->td_retval[0] = EINVAL;
	return (0);
}

int
sys_aio_error(struct thread *td, struct aio_error_args *uap)
{

	return (kern_aio_error(td, uap->aiocbp, &aiocb_ops));
}

/* syscall - asynchronous read from a file (REALTIME) */
#ifdef COMPAT_FREEBSD6
int
freebsd6_aio_read(struct thread *td, struct freebsd6_aio_read_args *uap)
{

	return (aio_aqueue(td, (struct aiocb *)uap->aiocbp, NULL, LIO_READ,
	    &aiocb_ops_osigevent));
}
#endif

int
sys_aio_read(struct thread *td, struct aio_read_args *uap)
{

	return (aio_aqueue(td, uap->aiocbp, NULL, LIO_READ, &aiocb_ops));
}

/* syscall - asynchronous write to a file (REALTIME) */
#ifdef COMPAT_FREEBSD6
int
freebsd6_aio_write(struct thread *td, struct freebsd6_aio_write_args *uap)
{

	return (aio_aqueue(td, (struct aiocb *)uap->aiocbp, NULL, LIO_WRITE,
	    &aiocb_ops_osigevent));
}
#endif

int
sys_aio_write(struct thread *td, struct aio_write_args *uap)
{

	return (aio_aqueue(td, uap->aiocbp, NULL, LIO_WRITE, &aiocb_ops));
}

int
sys_aio_mlock(struct thread *td, struct aio_mlock_args *uap)
{

	return (aio_aqueue(td, uap->aiocbp, NULL, LIO_MLOCK, &aiocb_ops));
}

static int
kern_lio_listio(struct thread *td, int mode, struct aiocb * const *uacb_list,
    struct aiocb **acb_list, int nent, struct sigevent *sig,
    struct aiocb_ops *ops)
{
	struct proc *p = td->td_proc;
	struct aiocb *job;
	struct kaioinfo *ki;
	struct aioliojob *lj;
	struct kevent kev;
	int error;
	int nagain, nerror;
	int i;

	if ((mode != LIO_NOWAIT) && (mode != LIO_WAIT))
		return (EINVAL);

	if (nent < 0 || nent > max_aio_queue_per_proc)
		return (EINVAL);

	if (p->p_aioinfo == NULL)
		aio_init_aioinfo(p);

	ki = p->p_aioinfo;

	lj = uma_zalloc(aiolio_zone, M_WAITOK);
	lj->lioj_flags = 0;
	lj->lioj_count = 0;
	lj->lioj_finished_count = 0;
	knlist_init_mtx(&lj->klist, AIO_MTX(ki));
	ksiginfo_init(&lj->lioj_ksi);

	/*
	 * Setup signal.
	 */
	if (sig && (mode == LIO_NOWAIT)) {
		bcopy(sig, &lj->lioj_signal, sizeof(lj->lioj_signal));
		if (lj->lioj_signal.sigev_notify == SIGEV_KEVENT) {
			/* Assume only new style KEVENT */
			memset(&kev, 0, sizeof(kev));
			kev.filter = EVFILT_LIO;
			kev.flags = EV_ADD | EV_ENABLE | EV_FLAG1;
			kev.ident = (uintptr_t)uacb_list; /* something unique */
			kev.data = (intptr_t)lj;
			/* pass user defined sigval data */
			kev.udata = lj->lioj_signal.sigev_value.sival_ptr;
			error = kqfd_register(
			    lj->lioj_signal.sigev_notify_kqueue, &kev, td,
			    M_WAITOK);
			if (error) {
				uma_zfree(aiolio_zone, lj);
				return (error);
			}
		} else if (lj->lioj_signal.sigev_notify == SIGEV_NONE) {
			;
		} else if (lj->lioj_signal.sigev_notify == SIGEV_SIGNAL ||
			   lj->lioj_signal.sigev_notify == SIGEV_THREAD_ID) {
				if (!_SIG_VALID(lj->lioj_signal.sigev_signo)) {
					uma_zfree(aiolio_zone, lj);
					return EINVAL;
				}
				lj->lioj_flags |= LIOJ_SIGNAL;
		} else {
			uma_zfree(aiolio_zone, lj);
			return EINVAL;
		}
	}

	AIO_LOCK(ki);
	TAILQ_INSERT_TAIL(&ki->kaio_liojoblist, lj, lioj_list);
	/*
	 * Add extra aiocb count to avoid the lio to be freed
	 * by other threads doing aio_waitcomplete or aio_return,
	 * and prevent event from being sent until we have queued
	 * all tasks.
	 */
	lj->lioj_count = 1;
	AIO_UNLOCK(ki);

	/*
	 * Get pointers to the list of I/O requests.
	 */
	nagain = 0;
	nerror = 0;
	for (i = 0; i < nent; i++) {
		job = acb_list[i];
		if (job != NULL) {
			error = aio_aqueue(td, job, lj, LIO_NOP, ops);
			if (error == EAGAIN)
				nagain++;
			else if (error != 0)
				nerror++;
		}
	}

	error = 0;
	AIO_LOCK(ki);
	if (mode == LIO_WAIT) {
		while (lj->lioj_count - 1 != lj->lioj_finished_count) {
			ki->kaio_flags |= KAIO_WAKEUP;
			error = msleep(&p->p_aioinfo, AIO_MTX(ki),
			    PRIBIO | PCATCH, "aiospn", 0);
			if (error == ERESTART)
				error = EINTR;
			if (error)
				break;
		}
	} else {
		if (lj->lioj_count - 1 == lj->lioj_finished_count) {
			if (lj->lioj_signal.sigev_notify == SIGEV_KEVENT) {
				lj->lioj_flags |= LIOJ_KEVENT_POSTED;
				KNOTE_LOCKED(&lj->klist, 1);
			}
			if ((lj->lioj_flags & (LIOJ_SIGNAL|LIOJ_SIGNAL_POSTED))
			    == LIOJ_SIGNAL
			    && (lj->lioj_signal.sigev_notify == SIGEV_SIGNAL ||
			    lj->lioj_signal.sigev_notify == SIGEV_THREAD_ID)) {
				aio_sendsig(p, &lj->lioj_signal,
					    &lj->lioj_ksi);
				lj->lioj_flags |= LIOJ_SIGNAL_POSTED;
			}
		}
	}
	lj->lioj_count--;
	if (lj->lioj_count == 0) {
		TAILQ_REMOVE(&ki->kaio_liojoblist, lj, lioj_list);
		knlist_delete(&lj->klist, curthread, 1);
		PROC_LOCK(p);
		sigqueue_take(&lj->lioj_ksi);
		PROC_UNLOCK(p);
		AIO_UNLOCK(ki);
		uma_zfree(aiolio_zone, lj);
	} else
		AIO_UNLOCK(ki);

	if (nerror)
		return (EIO);
	else if (nagain)
		return (EAGAIN);
	else
		return (error);
}

/* syscall - list directed I/O (REALTIME) */
#ifdef COMPAT_FREEBSD6
int
freebsd6_lio_listio(struct thread *td, struct freebsd6_lio_listio_args *uap)
{
	struct aiocb **acb_list;
	struct sigevent *sigp, sig;
	struct osigevent osig;
	int error, nent;

	if ((uap->mode != LIO_NOWAIT) && (uap->mode != LIO_WAIT))
		return (EINVAL);

	nent = uap->nent;
	if (nent < 0 || nent > max_aio_queue_per_proc)
		return (EINVAL);

	if (uap->sig && (uap->mode == LIO_NOWAIT)) {
		error = copyin(uap->sig, &osig, sizeof(osig));
		if (error)
			return (error);
		error = convert_old_sigevent(&osig, &sig);
		if (error)
			return (error);
		sigp = &sig;
	} else
		sigp = NULL;

	acb_list = malloc(sizeof(struct aiocb *) * nent, M_LIO, M_WAITOK);
	error = copyin(uap->acb_list, acb_list, nent * sizeof(acb_list[0]));
	if (error == 0)
		error = kern_lio_listio(td, uap->mode,
		    (struct aiocb * const *)uap->acb_list, acb_list, nent, sigp,
		    &aiocb_ops_osigevent);
	free(acb_list, M_LIO);
	return (error);
}
#endif

/* syscall - list directed I/O (REALTIME) */
int
sys_lio_listio(struct thread *td, struct lio_listio_args *uap)
{
	struct aiocb **acb_list;
	struct sigevent *sigp, sig;
	int error, nent;

	if ((uap->mode != LIO_NOWAIT) && (uap->mode != LIO_WAIT))
		return (EINVAL);

	nent = uap->nent;
	if (nent < 0 || nent > max_aio_queue_per_proc)
		return (EINVAL);

	if (uap->sig && (uap->mode == LIO_NOWAIT)) {
		error = copyin(uap->sig, &sig, sizeof(sig));
		if (error)
			return (error);
		sigp = &sig;
	} else
		sigp = NULL;

	acb_list = malloc(sizeof(struct aiocb *) * nent, M_LIO, M_WAITOK);
	error = copyin(uap->acb_list, acb_list, nent * sizeof(acb_list[0]));
	if (error == 0)
		error = kern_lio_listio(td, uap->mode, uap->acb_list, acb_list,
		    nent, sigp, &aiocb_ops);
	free(acb_list, M_LIO);
	return (error);
}

static void
aio_biowakeup(struct bio *bp)
{
	struct kaiocb *job = (struct kaiocb *)bp->bio_caller1;
	struct proc *userp;
	struct kaioinfo *ki;
	size_t nbytes;
	int error, nblks;

	/* Release mapping into kernel space. */
	userp = job->userproc;
	ki = userp->p_aioinfo;
	if (job->pbuf) {
		pmap_qremove((vm_offset_t)job->pbuf->b_data, job->npages);
		uma_zfree(pbuf_zone, job->pbuf);
		job->pbuf = NULL;
		atomic_subtract_int(&num_buf_aio, 1);
		AIO_LOCK(ki);
		ki->kaio_buffer_count--;
		AIO_UNLOCK(ki);
	} else
		atomic_subtract_int(&num_unmapped_aio, 1);
	vm_page_unhold_pages(job->pages, job->npages);

	bp = job->bp;
	job->bp = NULL;
	nbytes = job->uaiocb.aio_nbytes - bp->bio_resid;
	error = 0;
	if (bp->bio_flags & BIO_ERROR)
		error = bp->bio_error;
	nblks = btodb(nbytes);
	if (job->uaiocb.aio_lio_opcode == LIO_WRITE)
		job->outblock += nblks;
	else
		job->inblock += nblks;

	if (error)
		aio_complete(job, -1, error);
	else
		aio_complete(job, nbytes, 0);

	g_destroy_bio(bp);
}

/* syscall - wait for the next completion of an aio request */
static int
kern_aio_waitcomplete(struct thread *td, struct aiocb **ujobp,
    struct timespec *ts, struct aiocb_ops *ops)
{
	struct proc *p = td->td_proc;
	struct timeval atv;
	struct kaioinfo *ki;
	struct kaiocb *job;
	struct aiocb *ujob;
	long error, status;
	int timo;

	ops->store_aiocb(ujobp, NULL);

	if (ts == NULL) {
		timo = 0;
	} else if (ts->tv_sec == 0 && ts->tv_nsec == 0) {
		timo = -1;
	} else {
		if ((ts->tv_nsec < 0) || (ts->tv_nsec >= 1000000000))
			return (EINVAL);

		TIMESPEC_TO_TIMEVAL(&atv, ts);
		if (itimerfix(&atv))
			return (EINVAL);
		timo = tvtohz(&atv);
	}

	if (p->p_aioinfo == NULL)
		aio_init_aioinfo(p);
	ki = p->p_aioinfo;

	error = 0;
	job = NULL;
	AIO_LOCK(ki);
	while ((job = TAILQ_FIRST(&ki->kaio_done)) == NULL) {
		if (timo == -1) {
			error = EWOULDBLOCK;
			break;
		}
		ki->kaio_flags |= KAIO_WAKEUP;
		error = msleep(&p->p_aioinfo, AIO_MTX(ki), PRIBIO | PCATCH,
		    "aiowc", timo);
		if (timo && error == ERESTART)
			error = EINTR;
		if (error)
			break;
	}

	if (job != NULL) {
		MPASS(job->jobflags & KAIOCB_FINISHED);
		ujob = job->ujob;
		status = job->uaiocb._aiocb_private.status;
		error = job->uaiocb._aiocb_private.error;
		td->td_retval[0] = status;
		td->td_ru.ru_oublock += job->outblock;
		td->td_ru.ru_inblock += job->inblock;
		td->td_ru.ru_msgsnd += job->msgsnd;
		td->td_ru.ru_msgrcv += job->msgrcv;
		aio_free_entry(job);
		AIO_UNLOCK(ki);
		ops->store_aiocb(ujobp, ujob);
		ops->store_error(ujob, error);
		ops->store_status(ujob, status);
	} else
		AIO_UNLOCK(ki);

	return (error);
}

int
sys_aio_waitcomplete(struct thread *td, struct aio_waitcomplete_args *uap)
{
	struct timespec ts, *tsp;
	int error;

	if (uap->timeout) {
		/* Get timespec struct. */
		error = copyin(uap->timeout, &ts, sizeof(ts));
		if (error)
			return (error);
		tsp = &ts;
	} else
		tsp = NULL;

	return (kern_aio_waitcomplete(td, uap->aiocbp, tsp, &aiocb_ops));
}

static int
kern_aio_fsync(struct thread *td, int op, struct aiocb *ujob,
    struct aiocb_ops *ops)
{

	if (op != O_SYNC) /* XXX lack of O_DSYNC */
		return (EINVAL);
	return (aio_aqueue(td, ujob, NULL, LIO_SYNC, ops));
}

int
sys_aio_fsync(struct thread *td, struct aio_fsync_args *uap)
{

	return (kern_aio_fsync(td, uap->op, uap->aiocbp, &aiocb_ops));
}

/* kqueue attach function */
static int
filt_aioattach(struct knote *kn)
{
	struct kaiocb *job;

	job = (struct kaiocb *)(uintptr_t)kn->kn_sdata;

	/*
	 * The job pointer must be validated before using it, so
	 * registration is restricted to the kernel; the user cannot
	 * set EV_FLAG1.
	 */
	if ((kn->kn_flags & EV_FLAG1) == 0)
		return (EPERM);
	kn->kn_ptr.p_aio = job;
	kn->kn_flags &= ~EV_FLAG1;

	knlist_add(&job->klist, kn, 0);

	return (0);
}

/* kqueue detach function */
static void
filt_aiodetach(struct knote *kn)
{
	struct knlist *knl;

	knl = &kn->kn_ptr.p_aio->klist;
	knl->kl_lock(knl->kl_lockarg);
	if (!knlist_empty(knl))
		knlist_remove(knl, kn, 1);
	knl->kl_unlock(knl->kl_lockarg);
}

/* kqueue filter function */
/*ARGSUSED*/
static int
filt_aio(struct knote *kn, long hint)
{
	struct kaiocb *job = kn->kn_ptr.p_aio;

	kn->kn_data = job->uaiocb._aiocb_private.error;
	if (!(job->jobflags & KAIOCB_FINISHED))
		return (0);
	kn->kn_flags |= EV_EOF;
	return (1);
}

/* kqueue attach function */
static int
filt_lioattach(struct knote *kn)
{
	struct aioliojob *lj;

	lj = (struct aioliojob *)(uintptr_t)kn->kn_sdata;

	/*
	 * The aioliojob pointer must be validated before using it, so
	 * registration is restricted to the kernel; the user cannot
	 * set EV_FLAG1.
	 */
	if ((kn->kn_flags & EV_FLAG1) == 0)
		return (EPERM);
	kn->kn_ptr.p_lio = lj;
	kn->kn_flags &= ~EV_FLAG1;

	knlist_add(&lj->klist, kn, 0);

	return (0);
}

/* kqueue detach function */
static void
filt_liodetach(struct knote *kn)
{
	struct knlist *knl;

	knl = &kn->kn_ptr.p_lio->klist;
	knl->kl_lock(knl->kl_lockarg);
	if (!knlist_empty(knl))
		knlist_remove(knl, kn, 1);
	knl->kl_unlock(knl->kl_lockarg);
}

/* kqueue filter function */
/*ARGSUSED*/
static int
filt_lio(struct knote *kn, long hint)
{
	struct aioliojob * lj = kn->kn_ptr.p_lio;

	return (lj->lioj_flags & LIOJ_KEVENT_POSTED);
}

#ifdef COMPAT_FREEBSD32
#include <sys/mount.h>
#include <sys/socket.h>
#include <compat/freebsd32/freebsd32.h>
#include <compat/freebsd32/freebsd32_proto.h>
#include <compat/freebsd32/freebsd32_signal.h>
#include <compat/freebsd32/freebsd32_syscall.h>
#include <compat/freebsd32/freebsd32_util.h>

struct __aiocb_private32 {
	int32_t	status;
	int32_t	error;
	uint32_t kernelinfo;
};

#ifdef COMPAT_FREEBSD6
typedef struct oaiocb32 {
	int	aio_fildes;		/* File descriptor */
	uint64_t aio_offset __packed;	/* File offset for I/O */
	uint32_t aio_buf;		/* I/O buffer in process space */
	uint32_t aio_nbytes;		/* Number of bytes for I/O */
	struct	osigevent32 aio_sigevent; /* Signal to deliver */
	int	aio_lio_opcode;		/* LIO opcode */
	int	aio_reqprio;		/* Request priority -- ignored */
	struct	__aiocb_private32 _aiocb_private;
} oaiocb32_t;
#endif

typedef struct aiocb32 {
	int32_t	aio_fildes;		/* File descriptor */
	uint64_t aio_offset __packed;	/* File offset for I/O */
	uint32_t aio_buf;		/* I/O buffer in process space */
	uint32_t aio_nbytes;		/* Number of bytes for I/O */
	int	__spare__[2];
	uint32_t __spare2__;
	int	aio_lio_opcode;		/* LIO opcode */
	int	aio_reqprio;		/* Request priority -- ignored */
	struct	__aiocb_private32 _aiocb_private;
	struct	sigevent32 aio_sigevent;	/* Signal to deliver */
} aiocb32_t;

#ifdef COMPAT_FREEBSD6
static int
convert_old_sigevent32(struct osigevent32 *osig, struct sigevent *nsig)
{

	/*
	 * Only SIGEV_NONE, SIGEV_SIGNAL, and SIGEV_KEVENT are
	 * supported by AIO with the old sigevent structure.
	 */
	CP(*osig, *nsig, sigev_notify);
	switch (nsig->sigev_notify) {
	case SIGEV_NONE:
		break;
	case SIGEV_SIGNAL:
		nsig->sigev_signo = osig->__sigev_u.__sigev_signo;
		break;
	case SIGEV_KEVENT:
		nsig->sigev_notify_kqueue =
		    osig->__sigev_u.__sigev_notify_kqueue;
		PTRIN_CP(*osig, *nsig, sigev_value.sival_ptr);
		break;
	default:
		return (EINVAL);
	}
	return (0);
}

static int
aiocb32_copyin_old_sigevent(struct aiocb *ujob, struct aiocb *kjob)
{
	struct oaiocb32 job32;
	int error;

	bzero(kjob, sizeof(struct aiocb));
	error = copyin(ujob, &job32, sizeof(job32));
	if (error)
		return (error);

	CP(job32, *kjob, aio_fildes);
	CP(job32, *kjob, aio_offset);
	PTRIN_CP(job32, *kjob, aio_buf);
	CP(job32, *kjob, aio_nbytes);
	CP(job32, *kjob, aio_lio_opcode);
	CP(job32, *kjob, aio_reqprio);
	CP(job32, *kjob, _aiocb_private.status);
	CP(job32, *kjob, _aiocb_private.error);
	PTRIN_CP(job32, *kjob, _aiocb_private.kernelinfo);
	return (convert_old_sigevent32(&job32.aio_sigevent,
	    &kjob->aio_sigevent));
}
#endif

static int
aiocb32_copyin(struct aiocb *ujob, struct aiocb *kjob)
{
	struct aiocb32 job32;
	int error;

	error = copyin(ujob, &job32, sizeof(job32));
	if (error)
		return (error);
	CP(job32, *kjob, aio_fildes);
	CP(job32, *kjob, aio_offset);
	PTRIN_CP(job32, *kjob, aio_buf);
	CP(job32, *kjob, aio_nbytes);
	CP(job32, *kjob, aio_lio_opcode);
	CP(job32, *kjob, aio_reqprio);
	CP(job32, *kjob, _aiocb_private.status);
	CP(job32, *kjob, _aiocb_private.error);
	PTRIN_CP(job32, *kjob, _aiocb_private.kernelinfo);
	return (convert_sigevent32(&job32.aio_sigevent, &kjob->aio_sigevent));
}

static long
aiocb32_fetch_status(struct aiocb *ujob)
{
	struct aiocb32 *ujob32;

	ujob32 = (struct aiocb32 *)ujob;
	return (fuword32(&ujob32->_aiocb_private.status));
}

static long
aiocb32_fetch_error(struct aiocb *ujob)
{
	struct aiocb32 *ujob32;

	ujob32 = (struct aiocb32 *)ujob;
	return (fuword32(&ujob32->_aiocb_private.error));
}

static int
aiocb32_store_status(struct aiocb *ujob, long status)
{
	struct aiocb32 *ujob32;

	ujob32 = (struct aiocb32 *)ujob;
	return (suword32(&ujob32->_aiocb_private.status, status));
}

static int
aiocb32_store_error(struct aiocb *ujob, long error)
{
	struct aiocb32 *ujob32;

	ujob32 = (struct aiocb32 *)ujob;
	return (suword32(&ujob32->_aiocb_private.error, error));
}

static int
aiocb32_store_kernelinfo(struct aiocb *ujob, long jobref)
{
	struct aiocb32 *ujob32;

	ujob32 = (struct aiocb32 *)ujob;
	return (suword32(&ujob32->_aiocb_private.kernelinfo, jobref));
}

static int
aiocb32_store_aiocb(struct aiocb **ujobp, struct aiocb *ujob)
{

	return (suword32(ujobp, (long)ujob));
}

static struct aiocb_ops aiocb32_ops = {
	.copyin = aiocb32_copyin,
	.fetch_status = aiocb32_fetch_status,
	.fetch_error = aiocb32_fetch_error,
	.store_status = aiocb32_store_status,
	.store_error = aiocb32_store_error,
	.store_kernelinfo = aiocb32_store_kernelinfo,
	.store_aiocb = aiocb32_store_aiocb,
};

#ifdef COMPAT_FREEBSD6
static struct aiocb_ops aiocb32_ops_osigevent = {
	.copyin = aiocb32_copyin_old_sigevent,
	.fetch_status = aiocb32_fetch_status,
	.fetch_error = aiocb32_fetch_error,
	.store_status = aiocb32_store_status,
	.store_error = aiocb32_store_error,
	.store_kernelinfo = aiocb32_store_kernelinfo,
	.store_aiocb = aiocb32_store_aiocb,
};
#endif

int
freebsd32_aio_return(struct thread *td, struct freebsd32_aio_return_args *uap)
{

	return (kern_aio_return(td, (struct aiocb *)uap->aiocbp, &aiocb32_ops));
}

int
freebsd32_aio_suspend(struct thread *td, struct freebsd32_aio_suspend_args *uap)
{
	struct timespec32 ts32;
	struct timespec ts, *tsp;
	struct aiocb **ujoblist;
	uint32_t *ujoblist32;
	int error, i;

	if (uap->nent < 0 || uap->nent > max_aio_queue_per_proc)
		return (EINVAL);

	if (uap->timeout) {
		/* Get timespec struct. */
		if ((error = copyin(uap->timeout, &ts32, sizeof(ts32))) != 0)
			return (error);
		CP(ts32, ts, tv_sec);
		CP(ts32, ts, tv_nsec);
		tsp = &ts;
	} else
		tsp = NULL;

	ujoblist = malloc(uap->nent * sizeof(ujoblist[0]), M_AIOS, M_WAITOK);
	ujoblist32 = (uint32_t *)ujoblist;
	error = copyin(uap->aiocbp, ujoblist32, uap->nent *
	    sizeof(ujoblist32[0]));
	if (error == 0) {
		for (i = uap->nent - 1; i >= 0; i--)
			ujoblist[i] = PTRIN(ujoblist32[i]);

		error = kern_aio_suspend(td, uap->nent, ujoblist, tsp);
	}
	free(ujoblist, M_AIOS);
	return (error);
}

int
freebsd32_aio_error(struct thread *td, struct freebsd32_aio_error_args *uap)
{

	return (kern_aio_error(td, (struct aiocb *)uap->aiocbp, &aiocb32_ops));
}

#ifdef COMPAT_FREEBSD6
int
freebsd6_freebsd32_aio_read(struct thread *td,
    struct freebsd6_freebsd32_aio_read_args *uap)
{

	return (aio_aqueue(td, (struct aiocb *)uap->aiocbp, NULL, LIO_READ,
	    &aiocb32_ops_osigevent));
}
#endif

int
freebsd32_aio_read(struct thread *td, struct freebsd32_aio_read_args *uap)
{

	return (aio_aqueue(td, (struct aiocb *)uap->aiocbp, NULL, LIO_READ,
	    &aiocb32_ops));
}

#ifdef COMPAT_FREEBSD6
int
freebsd6_freebsd32_aio_write(struct thread *td,
    struct freebsd6_freebsd32_aio_write_args *uap)
{

	return (aio_aqueue(td, (struct aiocb *)uap->aiocbp, NULL, LIO_WRITE,
	    &aiocb32_ops_osigevent));
}
#endif

int
freebsd32_aio_write(struct thread *td, struct freebsd32_aio_write_args *uap)
{

	return (aio_aqueue(td, (struct aiocb *)uap->aiocbp, NULL, LIO_WRITE,
	    &aiocb32_ops));
}

int
freebsd32_aio_mlock(struct thread *td, struct freebsd32_aio_mlock_args *uap)
{

	return (aio_aqueue(td, (struct aiocb *)uap->aiocbp, NULL, LIO_MLOCK,
	    &aiocb32_ops));
}

int
freebsd32_aio_waitcomplete(struct thread *td,
    struct freebsd32_aio_waitcomplete_args *uap)
{
	struct timespec32 ts32;
	struct timespec ts, *tsp;
	int error;

	if (uap->timeout) {
		/* Get timespec struct. */
		error = copyin(uap->timeout, &ts32, sizeof(ts32));
		if (error)
			return (error);
		CP(ts32, ts, tv_sec);
		CP(ts32, ts, tv_nsec);
		tsp = &ts;
	} else
		tsp = NULL;

	return (kern_aio_waitcomplete(td, (struct aiocb **)uap->aiocbp, tsp,
	    &aiocb32_ops));
}

int
freebsd32_aio_fsync(struct thread *td, struct freebsd32_aio_fsync_args *uap)
{

	return (kern_aio_fsync(td, uap->op, (struct aiocb *)uap->aiocbp,
	    &aiocb32_ops));
}

#ifdef COMPAT_FREEBSD6
int
freebsd6_freebsd32_lio_listio(struct thread *td,
    struct freebsd6_freebsd32_lio_listio_args *uap)
{
	struct aiocb **acb_list;
	struct sigevent *sigp, sig;
	struct osigevent32 osig;
	uint32_t *acb_list32;
	int error, i, nent;

	if ((uap->mode != LIO_NOWAIT) && (uap->mode != LIO_WAIT))
		return (EINVAL);

	nent = uap->nent;
	if (nent < 0 || nent > max_aio_queue_per_proc)
		return (EINVAL);

	if (uap->sig && (uap->mode == LIO_NOWAIT)) {
		error = copyin(uap->sig, &osig, sizeof(osig));
		if (error)
			return (error);
		error = convert_old_sigevent32(&osig, &sig);
		if (error)
			return (error);
		sigp = &sig;
	} else
		sigp = NULL;

	acb_list32 = malloc(sizeof(uint32_t) * nent, M_LIO, M_WAITOK);
	error = copyin(uap->acb_list, acb_list32, nent * sizeof(uint32_t));
	if (error) {
		free(acb_list32, M_LIO);
		return (error);
	}
	acb_list = malloc(sizeof(struct aiocb *) * nent, M_LIO, M_WAITOK);
	for (i = 0; i < nent; i++)
		acb_list[i] = PTRIN(acb_list32[i]);
	free(acb_list32, M_LIO);

	error = kern_lio_listio(td, uap->mode,
	    (struct aiocb * const *)uap->acb_list, acb_list, nent, sigp,
	    &aiocb32_ops_osigevent);
	free(acb_list, M_LIO);
	return (error);
}
#endif

int
freebsd32_lio_listio(struct thread *td, struct freebsd32_lio_listio_args *uap)
{
	struct aiocb **acb_list;
	struct sigevent *sigp, sig;
	struct sigevent32 sig32;
	uint32_t *acb_list32;
	int error, i, nent;

	if ((uap->mode != LIO_NOWAIT) && (uap->mode != LIO_WAIT))
		return (EINVAL);

	nent = uap->nent;
	if (nent < 0 || nent > max_aio_queue_per_proc)
		return (EINVAL);

	if (uap->sig && (uap->mode == LIO_NOWAIT)) {
		error = copyin(uap->sig, &sig32, sizeof(sig32));
		if (error)
			return (error);
		error = convert_sigevent32(&sig32, &sig);
		if (error)
			return (error);
		sigp = &sig;
	} else
		sigp = NULL;

	acb_list32 = malloc(sizeof(uint32_t) * nent, M_LIO, M_WAITOK);
	error = copyin(uap->acb_list, acb_list32, nent * sizeof(uint32_t));
	if (error) {
		free(acb_list32, M_LIO);
		return (error);
	}
	acb_list = malloc(sizeof(struct aiocb *) * nent, M_LIO, M_WAITOK);
	for (i = 0; i < nent; i++)
		acb_list[i] = PTRIN(acb_list32[i]);
	free(acb_list32, M_LIO);

	error = kern_lio_listio(td, uap->mode,
	    (struct aiocb * const *)uap->acb_list, acb_list, nent, sigp,
	    &aiocb32_ops);
	free(acb_list, M_LIO);
	return (error);
}

#endif
