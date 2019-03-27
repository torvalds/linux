/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2005-2007 Joseph Koshy
 * Copyright (c) 2007 The FreeBSD Foundation
 * Copyright (c) 2018 Matthew Macy
 * All rights reserved.
 *
 * Portions of this software were developed by A. Joseph Koshy under
 * sponsorship from the FreeBSD Foundation and Google, Inc.
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
 *
 */

/*
 * Logging code for hwpmc(4)
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/capsicum.h>
#include <sys/domainset.h>
#include <sys/file.h>
#include <sys/kernel.h>
#include <sys/kthread.h>
#include <sys/lock.h>
#include <sys/module.h>
#include <sys/mutex.h>
#include <sys/pmc.h>
#include <sys/pmckern.h>
#include <sys/pmclog.h>
#include <sys/proc.h>
#include <sys/sched.h>
#include <sys/signalvar.h>
#include <sys/smp.h>
#include <sys/syscallsubr.h>
#include <sys/sysctl.h>
#include <sys/systm.h>
#include <sys/uio.h>
#include <sys/unistd.h>
#include <sys/vnode.h>

#if defined(__i386__) || defined(__amd64__)
#include <machine/clock.h>
#endif

#define curdomain PCPU_GET(domain)

/*
 * Sysctl tunables
 */

SYSCTL_DECL(_kern_hwpmc);

/*
 * kern.hwpmc.logbuffersize -- size of the per-cpu owner buffers.
 */

static int pmclog_buffer_size = PMC_LOG_BUFFER_SIZE;
#if (__FreeBSD_version < 1100000)
TUNABLE_INT(PMC_SYSCTL_NAME_PREFIX "logbuffersize", &pmclog_buffer_size);
#endif
SYSCTL_INT(_kern_hwpmc, OID_AUTO, logbuffersize, CTLFLAG_RDTUN,
    &pmclog_buffer_size, 0, "size of log buffers in kilobytes");

/*
 * kern.hwpmc.nbuffer -- number of global log buffers
 */

static int pmc_nlogbuffers_pcpu = PMC_NLOGBUFFERS_PCPU;
#if (__FreeBSD_version < 1100000)
TUNABLE_INT(PMC_SYSCTL_NAME_PREFIX "nbuffers", &pmc_nlogbuffers_pcpu);
#endif
SYSCTL_INT(_kern_hwpmc, OID_AUTO, nbuffers_pcpu, CTLFLAG_RDTUN,
    &pmc_nlogbuffers_pcpu, 0, "number of log buffers per cpu");

/*
 * Global log buffer list and associated spin lock.
 */

static struct mtx pmc_kthread_mtx;	/* sleep lock */

#define	PMCLOG_INIT_BUFFER_DESCRIPTOR(D, buf, domain) do {						\
		(D)->plb_fence = ((char *) (buf)) +	1024*pmclog_buffer_size;			\
		(D)->plb_base  = (D)->plb_ptr = ((char *) (buf));				\
		(D)->plb_domain = domain; \
	} while (0)

#define	PMCLOG_RESET_BUFFER_DESCRIPTOR(D) do {			\
		(D)->plb_ptr  = (D)->plb_base; \
	} while (0)

/*
 * Log file record constructors.
 */
#define	_PMCLOG_TO_HEADER(T,L)						\
	((PMCLOG_HEADER_MAGIC << 24) |					\
	 (PMCLOG_TYPE_ ## T << 16)   |					\
	 ((L) & 0xFFFF))

/* reserve LEN bytes of space and initialize the entry header */
#define	_PMCLOG_RESERVE_SAFE(PO,TYPE,LEN,ACTION, TSC) do {	\
		uint32_t *_le;						\
		int _len = roundup((LEN), sizeof(uint32_t));	\
		struct pmclog_header *ph;							\
		if ((_le = pmclog_reserve((PO), _len)) == NULL) {	\
			ACTION;											\
		}													\
		ph = (struct pmclog_header *)_le;					\
		ph->pl_header =_PMCLOG_TO_HEADER(TYPE,_len);	\
		ph->pl_tsc = (TSC);									\
		_le += sizeof(*ph)/4	/* skip over timestamp */

/* reserve LEN bytes of space and initialize the entry header */
#define	_PMCLOG_RESERVE(PO,TYPE,LEN,ACTION) do {			\
		uint32_t *_le;						\
		int _len = roundup((LEN), sizeof(uint32_t));	\
		uint64_t tsc;										\
		struct pmclog_header *ph;							\
		tsc = pmc_rdtsc();									\
		spinlock_enter();									\
		if ((_le = pmclog_reserve((PO), _len)) == NULL) {	\
			spinlock_exit();								\
			ACTION;											\
		}												\
		ph = (struct pmclog_header *)_le;					\
		ph->pl_header =_PMCLOG_TO_HEADER(TYPE,_len);	\
		ph->pl_tsc = tsc;									\
		_le += sizeof(*ph)/4	/* skip over timestamp */



#define	PMCLOG_RESERVE_SAFE(P,T,L,TSC)		_PMCLOG_RESERVE_SAFE(P,T,L,return,TSC)
#define	PMCLOG_RESERVE(P,T,L)		_PMCLOG_RESERVE(P,T,L,return)
#define	PMCLOG_RESERVE_WITH_ERROR(P,T,L) _PMCLOG_RESERVE(P,T,L,		\
	error=ENOMEM;goto error)

#define	PMCLOG_EMIT32(V)	do { *_le++ = (V); } while (0)
#define	PMCLOG_EMIT64(V)	do { 					\
		*_le++ = (uint32_t) ((V) & 0xFFFFFFFF);			\
		*_le++ = (uint32_t) (((V) >> 32) & 0xFFFFFFFF);		\
	} while (0)


/* Emit a string.  Caution: does NOT update _le, so needs to be last */
#define	PMCLOG_EMITSTRING(S,L)	do { bcopy((S), _le, (L)); } while (0)
#define	PMCLOG_EMITNULLSTRING(L) do { bzero(_le, (L)); } while (0)

#define	PMCLOG_DESPATCH_SAFE(PO)						\
	    pmclog_release((PO));						\
	} while (0)

#define	PMCLOG_DESPATCH_SCHED_LOCK(PO)						\
	     pmclog_release_flags((PO), 0);							\
	} while (0)

#define	PMCLOG_DESPATCH(PO)							\
	    pmclog_release((PO));						\
		spinlock_exit();							\
	} while (0)

#define	PMCLOG_DESPATCH_SYNC(PO)						\
	    pmclog_schedule_io((PO), 1);						\
		spinlock_exit();								\
		} while (0)


#define TSDELTA 4
/*
 * Assertions about the log file format.
 */
CTASSERT(sizeof(struct pmclog_callchain) == 7*4 + TSDELTA +
    PMC_CALLCHAIN_DEPTH_MAX*sizeof(uintfptr_t));
CTASSERT(sizeof(struct pmclog_closelog) == 3*4 + TSDELTA);
CTASSERT(sizeof(struct pmclog_dropnotify) == 3*4 + TSDELTA);
CTASSERT(sizeof(struct pmclog_map_in) == PATH_MAX + TSDELTA +
    5*4 + sizeof(uintfptr_t));
CTASSERT(offsetof(struct pmclog_map_in,pl_pathname) ==
    5*4 + TSDELTA + sizeof(uintfptr_t));
CTASSERT(sizeof(struct pmclog_map_out) == 5*4 + 2*sizeof(uintfptr_t) + TSDELTA);
CTASSERT(sizeof(struct pmclog_pmcallocate) == 9*4 + TSDELTA);
CTASSERT(sizeof(struct pmclog_pmcattach) == 5*4 + PATH_MAX + TSDELTA);
CTASSERT(offsetof(struct pmclog_pmcattach,pl_pathname) == 5*4 + TSDELTA);
CTASSERT(sizeof(struct pmclog_pmcdetach) == 5*4 + TSDELTA);
CTASSERT(sizeof(struct pmclog_proccsw) == 7*4 + 8 + TSDELTA);
CTASSERT(sizeof(struct pmclog_procexec) == 5*4 + PATH_MAX +
    sizeof(uintfptr_t) + TSDELTA);
CTASSERT(offsetof(struct pmclog_procexec,pl_pathname) == 5*4 + TSDELTA +
    sizeof(uintfptr_t));
CTASSERT(sizeof(struct pmclog_procexit) == 5*4 + 8 + TSDELTA);
CTASSERT(sizeof(struct pmclog_procfork) == 5*4 + TSDELTA);
CTASSERT(sizeof(struct pmclog_sysexit) == 6*4);
CTASSERT(sizeof(struct pmclog_userdata) == 6*4);

/*
 * Log buffer structure
 */

struct pmclog_buffer {
	TAILQ_ENTRY(pmclog_buffer) plb_next;
	char 		*plb_base;
	char		*plb_ptr;
	char 		*plb_fence;
	uint16_t	 plb_domain;
} __aligned(CACHE_LINE_SIZE);

/*
 * Prototypes
 */

static int pmclog_get_buffer(struct pmc_owner *po);
static void pmclog_loop(void *arg);
static void pmclog_release(struct pmc_owner *po);
static uint32_t *pmclog_reserve(struct pmc_owner *po, int length);
static void pmclog_schedule_io(struct pmc_owner *po, int wakeup);
static void pmclog_schedule_all(struct pmc_owner *po);
static void pmclog_stop_kthread(struct pmc_owner *po);

/*
 * Helper functions
 */

static inline void
pmc_plb_rele_unlocked(struct pmclog_buffer *plb)
{
	TAILQ_INSERT_HEAD(&pmc_dom_hdrs[plb->plb_domain]->pdbh_head, plb, plb_next);
}

static inline void
pmc_plb_rele(struct pmclog_buffer *plb)
{
	mtx_lock_spin(&pmc_dom_hdrs[plb->plb_domain]->pdbh_mtx);
	pmc_plb_rele_unlocked(plb);
	mtx_unlock_spin(&pmc_dom_hdrs[plb->plb_domain]->pdbh_mtx);
}

/*
 * Get a log buffer
 */
static int
pmclog_get_buffer(struct pmc_owner *po)
{
	struct pmclog_buffer *plb;
	int domain;

	KASSERT(po->po_curbuf[curcpu] == NULL,
	    ("[pmclog,%d] po=%p current buffer still valid", __LINE__, po));

	domain = curdomain;
	MPASS(pmc_dom_hdrs[domain]);
	mtx_lock_spin(&pmc_dom_hdrs[domain]->pdbh_mtx);
	if ((plb = TAILQ_FIRST(&pmc_dom_hdrs[domain]->pdbh_head)) != NULL)
		TAILQ_REMOVE(&pmc_dom_hdrs[domain]->pdbh_head, plb, plb_next);
	mtx_unlock_spin(&pmc_dom_hdrs[domain]->pdbh_mtx);

	PMCDBG2(LOG,GTB,1, "po=%p plb=%p", po, plb);

#ifdef	HWPMC_DEBUG
	if (plb)
		KASSERT(plb->plb_ptr == plb->plb_base &&
		    plb->plb_base < plb->plb_fence,
		    ("[pmclog,%d] po=%p buffer invariants: ptr=%p "
		    "base=%p fence=%p", __LINE__, po, plb->plb_ptr,
		    plb->plb_base, plb->plb_fence));
#endif

	po->po_curbuf[curcpu] = plb;

	/* update stats */
	counter_u64_add(pmc_stats.pm_buffer_requests, 1);
	if (plb == NULL)
		counter_u64_add(pmc_stats.pm_buffer_requests_failed, 1);

	return (plb ? 0 : ENOMEM);
}

struct pmclog_proc_init_args {
	struct proc *kthr;
	struct pmc_owner *po;
	bool exit;
	bool acted;
};

int
pmclog_proc_create(struct thread *td, void **handlep)
{
	struct pmclog_proc_init_args *ia;
	int error;

	ia = malloc(sizeof(*ia), M_TEMP, M_WAITOK | M_ZERO);
	error = kproc_create(pmclog_loop, ia, &ia->kthr,
	    RFHIGHPID, 0, "hwpmc: proc(%d)", td->td_proc->p_pid);
	if (error == 0)
		*handlep = ia;
	return (error);
}

void
pmclog_proc_ignite(void *handle, struct pmc_owner *po)
{
	struct pmclog_proc_init_args *ia;

	ia = handle;
	mtx_lock(&pmc_kthread_mtx);
	MPASS(!ia->acted);
	MPASS(ia->po == NULL);
	MPASS(!ia->exit);
	MPASS(ia->kthr != NULL);
	if (po == NULL) {
		ia->exit = true;
	} else {
		ia->po = po;
		KASSERT(po->po_kthread == NULL,
		    ("[pmclog,%d] po=%p kthread (%p) already present",
		    __LINE__, po, po->po_kthread));
		po->po_kthread = ia->kthr;
	}
	wakeup(ia);
	while (!ia->acted)
		msleep(ia, &pmc_kthread_mtx, PWAIT, "pmclogw", 0);
	mtx_unlock(&pmc_kthread_mtx);
	free(ia, M_TEMP);
}

/*
 * Log handler loop.
 *
 * This function is executed by each pmc owner's helper thread.
 */
static void
pmclog_loop(void *arg)
{
	struct pmclog_proc_init_args *ia;
	struct pmc_owner *po;
	struct pmclog_buffer *lb;
	struct proc *p;
	struct ucred *ownercred;
	struct ucred *mycred;
	struct thread *td;
	sigset_t unb;
	struct uio auio;
	struct iovec aiov;
	size_t nbytes;
	int error;

	td = curthread;

	SIGEMPTYSET(unb);
	SIGADDSET(unb, SIGHUP);
	(void)kern_sigprocmask(td, SIG_UNBLOCK, &unb, NULL, 0);

	ia = arg;
	MPASS(ia->kthr == curproc);
	MPASS(!ia->acted);
	mtx_lock(&pmc_kthread_mtx);
	while (ia->po == NULL && !ia->exit)
		msleep(ia, &pmc_kthread_mtx, PWAIT, "pmclogi", 0);
	if (ia->exit) {
		ia->acted = true;
		wakeup(ia);
		mtx_unlock(&pmc_kthread_mtx);
		kproc_exit(0);
	}
	MPASS(ia->po != NULL);
	po = ia->po;
	ia->acted = true;
	wakeup(ia);
	mtx_unlock(&pmc_kthread_mtx);
	ia = NULL;

	p = po->po_owner;
	mycred = td->td_ucred;

	PROC_LOCK(p);
	ownercred = crhold(p->p_ucred);
	PROC_UNLOCK(p);

	PMCDBG2(LOG,INI,1, "po=%p kt=%p", po, po->po_kthread);
	KASSERT(po->po_kthread == curthread->td_proc,
	    ("[pmclog,%d] proc mismatch po=%p po/kt=%p curproc=%p", __LINE__,
		po, po->po_kthread, curthread->td_proc));

	lb = NULL;


	/*
	 * Loop waiting for I/O requests to be added to the owner
	 * struct's queue.  The loop is exited when the log file
	 * is deconfigured.
	 */

	mtx_lock(&pmc_kthread_mtx);

	for (;;) {

		/* check if we've been asked to exit */
		if ((po->po_flags & PMC_PO_OWNS_LOGFILE) == 0)
			break;

		if (lb == NULL) { /* look for a fresh buffer to write */
			mtx_lock_spin(&po->po_mtx);
			if ((lb = TAILQ_FIRST(&po->po_logbuffers)) == NULL) {
				mtx_unlock_spin(&po->po_mtx);

				/* No more buffers and shutdown required. */
				if (po->po_flags & PMC_PO_SHUTDOWN)
					break;

				(void) msleep(po, &pmc_kthread_mtx, PWAIT,
				    "pmcloop", 250);
				continue;
			}

			TAILQ_REMOVE(&po->po_logbuffers, lb, plb_next);
			mtx_unlock_spin(&po->po_mtx);
		}

		mtx_unlock(&pmc_kthread_mtx);

		/* process the request */
		PMCDBG3(LOG,WRI,2, "po=%p base=%p ptr=%p", po,
		    lb->plb_base, lb->plb_ptr);
		/* change our thread's credentials before issuing the I/O */

		aiov.iov_base = lb->plb_base;
		aiov.iov_len  = nbytes = lb->plb_ptr - lb->plb_base;

		auio.uio_iov    = &aiov;
		auio.uio_iovcnt = 1;
		auio.uio_offset = -1;
		auio.uio_resid  = nbytes;
		auio.uio_rw     = UIO_WRITE;
		auio.uio_segflg = UIO_SYSSPACE;
		auio.uio_td     = td;

		/* switch thread credentials -- see kern_ktrace.c */
		td->td_ucred = ownercred;
		error = fo_write(po->po_file, &auio, ownercred, 0, td);
		td->td_ucred = mycred;

		if (error) {
			/* XXX some errors are recoverable */
			/* send a SIGIO to the owner and exit */
			PROC_LOCK(p);
			kern_psignal(p, SIGIO);
			PROC_UNLOCK(p);

			mtx_lock(&pmc_kthread_mtx);

			po->po_error = error; /* save for flush log */

			PMCDBG2(LOG,WRI,2, "po=%p error=%d", po, error);

			break;
		}

		mtx_lock(&pmc_kthread_mtx);

		/* put the used buffer back into the global pool */
		PMCLOG_RESET_BUFFER_DESCRIPTOR(lb);

		pmc_plb_rele(lb);
		lb = NULL;
	}

	wakeup_one(po->po_kthread);
	po->po_kthread = NULL;

	mtx_unlock(&pmc_kthread_mtx);

	/* return the current I/O buffer to the global pool */
	if (lb) {
		PMCLOG_RESET_BUFFER_DESCRIPTOR(lb);

		pmc_plb_rele(lb);
	}

	/*
	 * Exit this thread, signalling the waiter
	 */

	crfree(ownercred);

	kproc_exit(0);
}

/*
 * Release and log entry and schedule an I/O if needed.
 */

static void
pmclog_release_flags(struct pmc_owner *po, int wakeup)
{
	struct pmclog_buffer *plb;

	plb = po->po_curbuf[curcpu];
	KASSERT(plb->plb_ptr >= plb->plb_base,
	    ("[pmclog,%d] buffer invariants po=%p ptr=%p base=%p", __LINE__,
		po, plb->plb_ptr, plb->plb_base));
	KASSERT(plb->plb_ptr <= plb->plb_fence,
	    ("[pmclog,%d] buffer invariants po=%p ptr=%p fenc=%p", __LINE__,
		po, plb->plb_ptr, plb->plb_fence));

	/* schedule an I/O if we've filled a buffer */
	if (plb->plb_ptr >= plb->plb_fence)
		pmclog_schedule_io(po, wakeup);

	PMCDBG1(LOG,REL,1, "po=%p", po);
}

static void
pmclog_release(struct pmc_owner *po)
{

	pmclog_release_flags(po, 1);
}


/*
 * Attempt to reserve 'length' bytes of space in an owner's log
 * buffer.  The function returns a pointer to 'length' bytes of space
 * if there was enough space or returns NULL if no space was
 * available.  Non-null returns do so with the po mutex locked.  The
 * caller must invoke pmclog_release() on the pmc owner structure
 * when done.
 */

static uint32_t *
pmclog_reserve(struct pmc_owner *po, int length)
{
	uintptr_t newptr, oldptr;
	struct pmclog_buffer *plb, **pplb;

	PMCDBG2(LOG,ALL,1, "po=%p len=%d", po, length);

	KASSERT(length % sizeof(uint32_t) == 0,
	    ("[pmclog,%d] length not a multiple of word size", __LINE__));

	/* No more data when shutdown in progress. */
	if (po->po_flags & PMC_PO_SHUTDOWN)
		return (NULL);

	pplb = &po->po_curbuf[curcpu];
	if (*pplb == NULL && pmclog_get_buffer(po) != 0)
		goto fail;

	KASSERT(*pplb != NULL,
	    ("[pmclog,%d] po=%p no current buffer", __LINE__, po));

	plb = *pplb;
	KASSERT(plb->plb_ptr >= plb->plb_base &&
	    plb->plb_ptr <= plb->plb_fence,
	    ("[pmclog,%d] po=%p buffer invariants: ptr=%p base=%p fence=%p",
		__LINE__, po, plb->plb_ptr, plb->plb_base,
		plb->plb_fence));

	oldptr = (uintptr_t) plb->plb_ptr;
	newptr = oldptr + length;

	KASSERT(oldptr != (uintptr_t) NULL,
	    ("[pmclog,%d] po=%p Null log buffer pointer", __LINE__, po));

	/*
	 * If we have space in the current buffer, return a pointer to
	 * available space with the PO structure locked.
	 */
	if (newptr <= (uintptr_t) plb->plb_fence) {
		plb->plb_ptr = (char *) newptr;
		goto done;
	}

	/*
	 * Otherwise, schedule the current buffer for output and get a
	 * fresh buffer.
	 */
	pmclog_schedule_io(po, 0);

	if (pmclog_get_buffer(po) != 0)
		goto fail;

	plb = *pplb;
	KASSERT(plb != NULL,
	    ("[pmclog,%d] po=%p no current buffer", __LINE__, po));

	KASSERT(plb->plb_ptr != NULL,
	    ("[pmclog,%d] null return from pmc_get_log_buffer", __LINE__));

	KASSERT(plb->plb_ptr == plb->plb_base &&
	    plb->plb_ptr <= plb->plb_fence,
	    ("[pmclog,%d] po=%p buffer invariants: ptr=%p base=%p fence=%p",
		__LINE__, po, plb->plb_ptr, plb->plb_base,
		plb->plb_fence));

	oldptr = (uintptr_t) plb->plb_ptr;

 done:
	return ((uint32_t *) oldptr);
 fail:
	return (NULL);
}

/*
 * Schedule an I/O.
 *
 * Transfer the current buffer to the helper kthread.
 */

static void
pmclog_schedule_io(struct pmc_owner *po, int wakeup)
{
	struct pmclog_buffer *plb;

	plb = po->po_curbuf[curcpu];
	po->po_curbuf[curcpu] = NULL;
	KASSERT(plb != NULL,
	    ("[pmclog,%d] schedule_io with null buffer po=%p", __LINE__, po));
	KASSERT(plb->plb_ptr >= plb->plb_base,
	    ("[pmclog,%d] buffer invariants po=%p ptr=%p base=%p", __LINE__,
		po, plb->plb_ptr, plb->plb_base));
	KASSERT(plb->plb_ptr <= plb->plb_fence,
	    ("[pmclog,%d] buffer invariants po=%p ptr=%p fenc=%p", __LINE__,
		po, plb->plb_ptr, plb->plb_fence));

	PMCDBG1(LOG,SIO, 1, "po=%p", po);

	/*
	 * Add the current buffer to the tail of the buffer list and
	 * wakeup the helper.
	 */
	mtx_lock_spin(&po->po_mtx);
	TAILQ_INSERT_TAIL(&po->po_logbuffers, plb, plb_next);
	mtx_unlock_spin(&po->po_mtx);
	if (wakeup)
		wakeup_one(po);
}

/*
 * Stop the helper kthread.
 */

static void
pmclog_stop_kthread(struct pmc_owner *po)
{

	mtx_lock(&pmc_kthread_mtx);
	po->po_flags &= ~PMC_PO_OWNS_LOGFILE;
	if (po->po_kthread != NULL) {
		PROC_LOCK(po->po_kthread);
		kern_psignal(po->po_kthread, SIGHUP);
		PROC_UNLOCK(po->po_kthread);
	}
	wakeup_one(po);
	while (po->po_kthread)
		msleep(po->po_kthread, &pmc_kthread_mtx, PPAUSE, "pmckstp", 0);
	mtx_unlock(&pmc_kthread_mtx);
}

/*
 * Public functions
 */

/*
 * Configure a log file for pmc owner 'po'.
 *
 * Parameter 'logfd' is a file handle referencing an open file in the
 * owner process.  This file needs to have been opened for writing.
 */

int
pmclog_configure_log(struct pmc_mdep *md, struct pmc_owner *po, int logfd)
{
	struct proc *p;
	struct timespec ts;
	uint64_t tsc;
	int error;

	sx_assert(&pmc_sx, SA_XLOCKED);
	PMCDBG2(LOG,CFG,1, "config po=%p logfd=%d", po, logfd);

	p = po->po_owner;

	/* return EBUSY if a log file was already present */
	if (po->po_flags & PMC_PO_OWNS_LOGFILE)
		return (EBUSY);

	KASSERT(po->po_file == NULL,
	    ("[pmclog,%d] po=%p file (%p) already present", __LINE__, po,
		po->po_file));

	/* get a reference to the file state */
	error = fget_write(curthread, logfd, &cap_write_rights, &po->po_file);
	if (error)
		goto error;

	/* mark process as owning a log file */
	po->po_flags |= PMC_PO_OWNS_LOGFILE;

	/* mark process as using HWPMCs */
	PROC_LOCK(p);
	p->p_flag |= P_HWPMC;
	PROC_UNLOCK(p);
	nanotime(&ts);
	tsc = pmc_rdtsc();
	/* create a log initialization entry */
	PMCLOG_RESERVE_WITH_ERROR(po, INITIALIZE,
	    sizeof(struct pmclog_initialize));
	PMCLOG_EMIT32(PMC_VERSION);
	PMCLOG_EMIT32(md->pmd_cputype);
#if defined(__i386__) || defined(__amd64__)
	PMCLOG_EMIT64(tsc_freq);
#else
	/* other architectures will need to fill this in */
	PMCLOG_EMIT32(0);
	PMCLOG_EMIT32(0);
#endif
	memcpy(_le, &ts, sizeof(ts));
	_le += sizeof(ts)/4;
	PMCLOG_EMITSTRING(pmc_cpuid, PMC_CPUID_LEN);
	PMCLOG_DESPATCH_SYNC(po);

	return (0);

 error:
	KASSERT(po->po_kthread == NULL, ("[pmclog,%d] po=%p kthread not "
	    "stopped", __LINE__, po));

	if (po->po_file)
		(void) fdrop(po->po_file, curthread);
	po->po_file  = NULL;	/* clear file and error state */
	po->po_error = 0;
	po->po_flags &= ~PMC_PO_OWNS_LOGFILE;

	return (error);
}


/*
 * De-configure a log file.  This will throw away any buffers queued
 * for this owner process.
 */

int
pmclog_deconfigure_log(struct pmc_owner *po)
{
	int error;
	struct pmclog_buffer *lb;

	PMCDBG1(LOG,CFG,1, "de-config po=%p", po);

	if ((po->po_flags & PMC_PO_OWNS_LOGFILE) == 0)
		return (EINVAL);

	KASSERT(po->po_sscount == 0,
	    ("[pmclog,%d] po=%p still owning SS PMCs", __LINE__, po));
	KASSERT(po->po_file != NULL,
	    ("[pmclog,%d] po=%p no log file", __LINE__, po));

	/* stop the kthread, this will reset the 'OWNS_LOGFILE' flag */
	pmclog_stop_kthread(po);

	KASSERT(po->po_kthread == NULL,
	    ("[pmclog,%d] po=%p kthread not stopped", __LINE__, po));

	/* return all queued log buffers to the global pool */
	while ((lb = TAILQ_FIRST(&po->po_logbuffers)) != NULL) {
		TAILQ_REMOVE(&po->po_logbuffers, lb, plb_next);
		PMCLOG_RESET_BUFFER_DESCRIPTOR(lb);
		pmc_plb_rele(lb);
	}
	for (int i = 0; i < mp_ncpus; i++) {
		thread_lock(curthread);
		sched_bind(curthread, i);
		thread_unlock(curthread);
		/* return the 'current' buffer to the global pool */
		if ((lb = po->po_curbuf[curcpu]) != NULL) {
			PMCLOG_RESET_BUFFER_DESCRIPTOR(lb);
			pmc_plb_rele(lb);
		}
	}
	thread_lock(curthread);
	sched_unbind(curthread);
	thread_unlock(curthread);

	/* drop a reference to the fd */
	if (po->po_file != NULL) {
		error = fdrop(po->po_file, curthread);
		po->po_file = NULL;
	} else
		error = 0;
	po->po_error = 0;

	return (error);
}

/*
 * Flush a process' log buffer.
 */

int
pmclog_flush(struct pmc_owner *po, int force)
{
	int error;

	PMCDBG1(LOG,FLS,1, "po=%p", po);

	/*
	 * If there is a pending error recorded by the logger thread,
	 * return that.
	 */
	if (po->po_error)
		return (po->po_error);

	error = 0;

	/*
	 * Check that we do have an active log file.
	 */
	mtx_lock(&pmc_kthread_mtx);
	if ((po->po_flags & PMC_PO_OWNS_LOGFILE) == 0) {
		error = EINVAL;
		goto error;
	}

	pmclog_schedule_all(po);
 error:
	mtx_unlock(&pmc_kthread_mtx);

	return (error);
}

static void
pmclog_schedule_one_cond(struct pmc_owner *po)
{
	struct pmclog_buffer *plb;
	int cpu;

	spinlock_enter();
	cpu = curcpu;
	/* tell hardclock not to run again */
	if (PMC_CPU_HAS_SAMPLES(cpu))
		PMC_CALL_HOOK_UNLOCKED(curthread, PMC_FN_DO_SAMPLES, NULL);

	plb = po->po_curbuf[cpu];
	if (plb && plb->plb_ptr != plb->plb_base)
		pmclog_schedule_io(po, 1);
	spinlock_exit();
}

static void
pmclog_schedule_all(struct pmc_owner *po)
{
	/*
	 * Schedule the current buffer if any and not empty.
	 */
	for (int i = 0; i < mp_ncpus; i++) {
		thread_lock(curthread);
		sched_bind(curthread, i);
		thread_unlock(curthread);
		pmclog_schedule_one_cond(po);
	}
	thread_lock(curthread);
	sched_unbind(curthread);
	thread_unlock(curthread);
}

int
pmclog_close(struct pmc_owner *po)
{

	PMCDBG1(LOG,CLO,1, "po=%p", po);

	pmclog_process_closelog(po);

	mtx_lock(&pmc_kthread_mtx);
	/*
	 * Initiate shutdown: no new data queued,
	 * thread will close file on last block.
	 */
	po->po_flags |= PMC_PO_SHUTDOWN;
	/* give time for all to see */
	DELAY(50);
	
	/*
	 * Schedule the current buffer.
	 */
	pmclog_schedule_all(po);
	wakeup_one(po);

	mtx_unlock(&pmc_kthread_mtx);

	return (0);
}

void
pmclog_process_callchain(struct pmc *pm, struct pmc_sample *ps)
{
	int n, recordlen;
	uint32_t flags;
	struct pmc_owner *po;

	PMCDBG3(LOG,SAM,1,"pm=%p pid=%d n=%d", pm, ps->ps_pid,
	    ps->ps_nsamples);

	recordlen = offsetof(struct pmclog_callchain, pl_pc) +
	    ps->ps_nsamples * sizeof(uintfptr_t);
	po = pm->pm_owner;
	flags = PMC_CALLCHAIN_TO_CPUFLAGS(ps->ps_cpu,ps->ps_flags);
	PMCLOG_RESERVE_SAFE(po, CALLCHAIN, recordlen, ps->ps_tsc);
	PMCLOG_EMIT32(ps->ps_pid);
	PMCLOG_EMIT32(ps->ps_tid);
	PMCLOG_EMIT32(pm->pm_id);
	PMCLOG_EMIT32(flags);
	for (n = 0; n < ps->ps_nsamples; n++)
		PMCLOG_EMITADDR(ps->ps_pc[n]);
	PMCLOG_DESPATCH_SAFE(po);
}

void
pmclog_process_closelog(struct pmc_owner *po)
{
	PMCLOG_RESERVE(po,CLOSELOG,sizeof(struct pmclog_closelog));
	PMCLOG_DESPATCH_SYNC(po);
}

void
pmclog_process_dropnotify(struct pmc_owner *po)
{
	PMCLOG_RESERVE(po,DROPNOTIFY,sizeof(struct pmclog_dropnotify));
	PMCLOG_DESPATCH(po);
}

void
pmclog_process_map_in(struct pmc_owner *po, pid_t pid, uintfptr_t start,
    const char *path)
{
	int pathlen, recordlen;

	KASSERT(path != NULL, ("[pmclog,%d] map-in, null path", __LINE__));

	pathlen = strlen(path) + 1;	/* #bytes for path name */
	recordlen = offsetof(struct pmclog_map_in, pl_pathname) +
	    pathlen;

	PMCLOG_RESERVE(po, MAP_IN, recordlen);
	PMCLOG_EMIT32(pid);
	PMCLOG_EMIT32(0);
	PMCLOG_EMITADDR(start);
	PMCLOG_EMITSTRING(path,pathlen);
	PMCLOG_DESPATCH_SYNC(po);
}

void
pmclog_process_map_out(struct pmc_owner *po, pid_t pid, uintfptr_t start,
    uintfptr_t end)
{
	KASSERT(start <= end, ("[pmclog,%d] start > end", __LINE__));

	PMCLOG_RESERVE(po, MAP_OUT, sizeof(struct pmclog_map_out));
	PMCLOG_EMIT32(pid);
	PMCLOG_EMIT32(0);
	PMCLOG_EMITADDR(start);
	PMCLOG_EMITADDR(end);
	PMCLOG_DESPATCH(po);
}

void
pmclog_process_pmcallocate(struct pmc *pm)
{
	struct pmc_owner *po;
	struct pmc_soft *ps;

	po = pm->pm_owner;

	PMCDBG1(LOG,ALL,1, "pm=%p", pm);

	if (PMC_TO_CLASS(pm) == PMC_CLASS_SOFT) {
		PMCLOG_RESERVE(po, PMCALLOCATEDYN,
		    sizeof(struct pmclog_pmcallocatedyn));
		PMCLOG_EMIT32(pm->pm_id);
		PMCLOG_EMIT32(pm->pm_event);
		PMCLOG_EMIT32(pm->pm_flags);
		PMCLOG_EMIT32(0);
		PMCLOG_EMIT64(pm->pm_sc.pm_reloadcount);
		ps = pmc_soft_ev_acquire(pm->pm_event);
		if (ps != NULL)
			PMCLOG_EMITSTRING(ps->ps_ev.pm_ev_name,PMC_NAME_MAX);
		else
			PMCLOG_EMITNULLSTRING(PMC_NAME_MAX);
		pmc_soft_ev_release(ps);
		PMCLOG_DESPATCH_SYNC(po);
	} else {
		PMCLOG_RESERVE(po, PMCALLOCATE,
		    sizeof(struct pmclog_pmcallocate));
		PMCLOG_EMIT32(pm->pm_id);
		PMCLOG_EMIT32(pm->pm_event);
		PMCLOG_EMIT32(pm->pm_flags);
		PMCLOG_EMIT32(0);
		PMCLOG_EMIT64(pm->pm_sc.pm_reloadcount);
		PMCLOG_DESPATCH_SYNC(po);
	}
}

void
pmclog_process_pmcattach(struct pmc *pm, pid_t pid, char *path)
{
	int pathlen, recordlen;
	struct pmc_owner *po;

	PMCDBG2(LOG,ATT,1,"pm=%p pid=%d", pm, pid);

	po = pm->pm_owner;

	pathlen = strlen(path) + 1;	/* #bytes for the string */
	recordlen = offsetof(struct pmclog_pmcattach, pl_pathname) + pathlen;

	PMCLOG_RESERVE(po, PMCATTACH, recordlen);
	PMCLOG_EMIT32(pm->pm_id);
	PMCLOG_EMIT32(pid);
	PMCLOG_EMITSTRING(path, pathlen);
	PMCLOG_DESPATCH_SYNC(po);
}

void
pmclog_process_pmcdetach(struct pmc *pm, pid_t pid)
{
	struct pmc_owner *po;

	PMCDBG2(LOG,ATT,1,"!pm=%p pid=%d", pm, pid);

	po = pm->pm_owner;

	PMCLOG_RESERVE(po, PMCDETACH, sizeof(struct pmclog_pmcdetach));
	PMCLOG_EMIT32(pm->pm_id);
	PMCLOG_EMIT32(pid);
	PMCLOG_DESPATCH_SYNC(po);
}

void
pmclog_process_proccreate(struct pmc_owner *po, struct proc *p, int sync)
{
	if (sync) {
		PMCLOG_RESERVE(po, PROC_CREATE, sizeof(struct pmclog_proccreate));
		PMCLOG_EMIT32(p->p_pid);
		PMCLOG_EMIT32(p->p_flag);
		PMCLOG_EMITSTRING(p->p_comm, MAXCOMLEN+1);
		PMCLOG_DESPATCH_SYNC(po);
	} else {
		PMCLOG_RESERVE(po, PROC_CREATE, sizeof(struct pmclog_proccreate));
		PMCLOG_EMIT32(p->p_pid);
		PMCLOG_EMIT32(p->p_flag);
		PMCLOG_EMITSTRING(p->p_comm, MAXCOMLEN+1);
		PMCLOG_DESPATCH(po);
	}
}

/*
 * Log a context switch event to the log file.
 */

void
pmclog_process_proccsw(struct pmc *pm, struct pmc_process *pp, pmc_value_t v, struct thread *td)
{
	struct pmc_owner *po;

	KASSERT(pm->pm_flags & PMC_F_LOG_PROCCSW,
	    ("[pmclog,%d] log-process-csw called gratuitously", __LINE__));

	PMCDBG3(LOG,SWO,1,"pm=%p pid=%d v=%jx", pm, pp->pp_proc->p_pid,
	    v);

	po = pm->pm_owner;

	PMCLOG_RESERVE_SAFE(po, PROCCSW, sizeof(struct pmclog_proccsw), pmc_rdtsc());
	PMCLOG_EMIT64(v);
	PMCLOG_EMIT32(pm->pm_id);
	PMCLOG_EMIT32(pp->pp_proc->p_pid);
	PMCLOG_EMIT32(td->td_tid);
	PMCLOG_EMIT32(0);
	PMCLOG_DESPATCH_SCHED_LOCK(po);
}

void
pmclog_process_procexec(struct pmc_owner *po, pmc_id_t pmid, pid_t pid,
    uintfptr_t startaddr, char *path)
{
	int pathlen, recordlen;

	PMCDBG3(LOG,EXC,1,"po=%p pid=%d path=\"%s\"", po, pid, path);

	pathlen   = strlen(path) + 1;	/* #bytes for the path */
	recordlen = offsetof(struct pmclog_procexec, pl_pathname) + pathlen;
	PMCLOG_RESERVE(po, PROCEXEC, recordlen);
	PMCLOG_EMIT32(pid);
	PMCLOG_EMIT32(pmid);
	PMCLOG_EMITADDR(startaddr);
	PMCLOG_EMITSTRING(path,pathlen);
	PMCLOG_DESPATCH_SYNC(po);
}

/*
 * Log a process exit event (and accumulated pmc value) to the log file.
 */

void
pmclog_process_procexit(struct pmc *pm, struct pmc_process *pp)
{
	int ri;
	struct pmc_owner *po;

	ri = PMC_TO_ROWINDEX(pm);
	PMCDBG3(LOG,EXT,1,"pm=%p pid=%d v=%jx", pm, pp->pp_proc->p_pid,
	    pp->pp_pmcs[ri].pp_pmcval);

	po = pm->pm_owner;

	PMCLOG_RESERVE(po, PROCEXIT, sizeof(struct pmclog_procexit));
	PMCLOG_EMIT32(pm->pm_id);
	PMCLOG_EMIT32(pp->pp_proc->p_pid);
	PMCLOG_EMIT64(pp->pp_pmcs[ri].pp_pmcval);
	PMCLOG_DESPATCH(po);
}

/*
 * Log a fork event.
 */

void
pmclog_process_procfork(struct pmc_owner *po, pid_t oldpid, pid_t newpid)
{
	PMCLOG_RESERVE(po, PROCFORK, sizeof(struct pmclog_procfork));
	PMCLOG_EMIT32(oldpid);
	PMCLOG_EMIT32(newpid);
	PMCLOG_DESPATCH(po);
}

/*
 * Log a process exit event of the form suitable for system-wide PMCs.
 */

void
pmclog_process_sysexit(struct pmc_owner *po, pid_t pid)
{
	PMCLOG_RESERVE(po, SYSEXIT, sizeof(struct pmclog_sysexit));
	PMCLOG_EMIT32(pid);
	PMCLOG_DESPATCH(po);
}

void
pmclog_process_threadcreate(struct pmc_owner *po, struct thread *td, int sync)
{
	struct proc *p;

	p = td->td_proc;
	if (sync) {
		PMCLOG_RESERVE(po, THR_CREATE, sizeof(struct pmclog_threadcreate));
		PMCLOG_EMIT32(td->td_tid);
		PMCLOG_EMIT32(p->p_pid);
		PMCLOG_EMIT32(p->p_flag);
		PMCLOG_EMIT32(0);
		PMCLOG_EMITSTRING(td->td_name, MAXCOMLEN+1);
		PMCLOG_DESPATCH_SYNC(po);
	} else {
		PMCLOG_RESERVE(po, THR_CREATE, sizeof(struct pmclog_threadcreate));
		PMCLOG_EMIT32(td->td_tid);
		PMCLOG_EMIT32(p->p_pid);
		PMCLOG_EMIT32(p->p_flag);
		PMCLOG_EMIT32(0);
		PMCLOG_EMITSTRING(td->td_name, MAXCOMLEN+1);
		PMCLOG_DESPATCH(po);
	}
}

void
pmclog_process_threadexit(struct pmc_owner *po, struct thread *td)
{

	PMCLOG_RESERVE(po, THR_EXIT, sizeof(struct pmclog_threadexit));
	PMCLOG_EMIT32(td->td_tid);
	PMCLOG_DESPATCH(po);
}

/*
 * Write a user log entry.
 */

int
pmclog_process_userlog(struct pmc_owner *po, struct pmc_op_writelog *wl)
{
	int error;

	PMCDBG2(LOG,WRI,1, "writelog po=%p ud=0x%x", po, wl->pm_userdata);

	error = 0;

	PMCLOG_RESERVE_WITH_ERROR(po, USERDATA,
	    sizeof(struct pmclog_userdata));
	PMCLOG_EMIT32(wl->pm_userdata);
	PMCLOG_DESPATCH(po);

 error:
	return (error);
}

/*
 * Initialization.
 *
 * Create a pool of log buffers and initialize mutexes.
 */

void
pmclog_initialize()
{
	struct pmclog_buffer *plb;
	int domain, ncpus, total;

	if (pmclog_buffer_size <= 0 || pmclog_buffer_size > 16*1024) {
		(void) printf("hwpmc: tunable logbuffersize=%d must be "
					  "greater than zero and less than or equal to 16MB.\n",
					  pmclog_buffer_size);
		pmclog_buffer_size = PMC_LOG_BUFFER_SIZE;
	}

	if (pmc_nlogbuffers_pcpu <= 0) {
		(void) printf("hwpmc: tunable nlogbuffers=%d must be greater "
					  "than zero.\n", pmc_nlogbuffers_pcpu);
		pmc_nlogbuffers_pcpu = PMC_NLOGBUFFERS_PCPU;
	}
	if (pmc_nlogbuffers_pcpu*pmclog_buffer_size > 32*1024) {
		(void) printf("hwpmc: memory allocated pcpu must be less than 32MB (is %dK).\n",
					  pmc_nlogbuffers_pcpu*pmclog_buffer_size);
		pmc_nlogbuffers_pcpu = PMC_NLOGBUFFERS_PCPU;
		pmclog_buffer_size = PMC_LOG_BUFFER_SIZE;
	}
	for (domain = 0; domain < vm_ndomains; domain++) {
		ncpus = pmc_dom_hdrs[domain]->pdbh_ncpus;
		total = ncpus * pmc_nlogbuffers_pcpu;

		plb = malloc_domainset(sizeof(struct pmclog_buffer) * total,
		    M_PMC, DOMAINSET_PREF(domain), M_WAITOK | M_ZERO);
		pmc_dom_hdrs[domain]->pdbh_plbs = plb;
		for (; total > 0; total--, plb++) {
			void *buf;

			buf = malloc_domainset(1024 * pmclog_buffer_size, M_PMC,
			    DOMAINSET_PREF(domain), M_WAITOK | M_ZERO);
			PMCLOG_INIT_BUFFER_DESCRIPTOR(plb, buf, domain);
			pmc_plb_rele_unlocked(plb);
		}
	}
	mtx_init(&pmc_kthread_mtx, "pmc-kthread", "pmc-sleep", MTX_DEF);
}

/*
 * Shutdown logging.
 *
 * Destroy mutexes and release memory back the to free pool.
 */

void
pmclog_shutdown()
{
	struct pmclog_buffer *plb;
	int domain;

	mtx_destroy(&pmc_kthread_mtx);

	for (domain = 0; domain < vm_ndomains; domain++) {
		while ((plb = TAILQ_FIRST(&pmc_dom_hdrs[domain]->pdbh_head)) != NULL) {
			TAILQ_REMOVE(&pmc_dom_hdrs[domain]->pdbh_head, plb, plb_next);
			free(plb->plb_base, M_PMC);
		}
		free(pmc_dom_hdrs[domain]->pdbh_plbs, M_PMC);
	}
}
