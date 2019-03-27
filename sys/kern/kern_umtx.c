/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2015, 2016 The FreeBSD Foundation
 * Copyright (c) 2004, David Xu <davidxu@freebsd.org>
 * Copyright (c) 2002, Jeffrey Roberson <jeff@freebsd.org>
 * All rights reserved.
 *
 * Portions of this software were developed by Konstantin Belousov
 * under sponsorship from the FreeBSD Foundation.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice unmodified, this list of conditions, and the following
 *    disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include "opt_umtx_profiling.h"

#include <sys/param.h>
#include <sys/kernel.h>
#include <sys/fcntl.h>
#include <sys/file.h>
#include <sys/filedesc.h>
#include <sys/limits.h>
#include <sys/lock.h>
#include <sys/malloc.h>
#include <sys/mman.h>
#include <sys/mutex.h>
#include <sys/priv.h>
#include <sys/proc.h>
#include <sys/resource.h>
#include <sys/resourcevar.h>
#include <sys/rwlock.h>
#include <sys/sbuf.h>
#include <sys/sched.h>
#include <sys/smp.h>
#include <sys/sysctl.h>
#include <sys/sysent.h>
#include <sys/systm.h>
#include <sys/sysproto.h>
#include <sys/syscallsubr.h>
#include <sys/taskqueue.h>
#include <sys/time.h>
#include <sys/eventhandler.h>
#include <sys/umtx.h>

#include <security/mac/mac_framework.h>

#include <vm/vm.h>
#include <vm/vm_param.h>
#include <vm/pmap.h>
#include <vm/vm_map.h>
#include <vm/vm_object.h>

#include <machine/atomic.h>
#include <machine/cpu.h>

#ifdef COMPAT_FREEBSD32
#include <compat/freebsd32/freebsd32_proto.h>
#endif

#define _UMUTEX_TRY		1
#define _UMUTEX_WAIT		2

#ifdef UMTX_PROFILING
#define	UPROF_PERC_BIGGER(w, f, sw, sf)					\
	(((w) > (sw)) || ((w) == (sw) && (f) > (sf)))
#endif

/* Priority inheritance mutex info. */
struct umtx_pi {
	/* Owner thread */
	struct thread		*pi_owner;

	/* Reference count */
	int			pi_refcount;

 	/* List entry to link umtx holding by thread */
	TAILQ_ENTRY(umtx_pi)	pi_link;

	/* List entry in hash */
	TAILQ_ENTRY(umtx_pi)	pi_hashlink;

	/* List for waiters */
	TAILQ_HEAD(,umtx_q)	pi_blocked;

	/* Identify a userland lock object */
	struct umtx_key		pi_key;
};

/* A userland synchronous object user. */
struct umtx_q {
	/* Linked list for the hash. */
	TAILQ_ENTRY(umtx_q)	uq_link;

	/* Umtx key. */
	struct umtx_key		uq_key;

	/* Umtx flags. */
	int			uq_flags;
#define UQF_UMTXQ	0x0001

	/* The thread waits on. */
	struct thread		*uq_thread;

	/*
	 * Blocked on PI mutex. read can use chain lock
	 * or umtx_lock, write must have both chain lock and
	 * umtx_lock being hold.
	 */
	struct umtx_pi		*uq_pi_blocked;

	/* On blocked list */
	TAILQ_ENTRY(umtx_q)	uq_lockq;

	/* Thread contending with us */
	TAILQ_HEAD(,umtx_pi)	uq_pi_contested;

	/* Inherited priority from PP mutex */
	u_char			uq_inherited_pri;
	
	/* Spare queue ready to be reused */
	struct umtxq_queue	*uq_spare_queue;

	/* The queue we on */
	struct umtxq_queue	*uq_cur_queue;
};

TAILQ_HEAD(umtxq_head, umtx_q);

/* Per-key wait-queue */
struct umtxq_queue {
	struct umtxq_head	head;
	struct umtx_key		key;
	LIST_ENTRY(umtxq_queue)	link;
	int			length;
};

LIST_HEAD(umtxq_list, umtxq_queue);

/* Userland lock object's wait-queue chain */
struct umtxq_chain {
	/* Lock for this chain. */
	struct mtx		uc_lock;

	/* List of sleep queues. */
	struct umtxq_list	uc_queue[2];
#define UMTX_SHARED_QUEUE	0
#define UMTX_EXCLUSIVE_QUEUE	1

	LIST_HEAD(, umtxq_queue) uc_spare_queue;

	/* Busy flag */
	char			uc_busy;

	/* Chain lock waiters */
	int			uc_waiters;

	/* All PI in the list */
	TAILQ_HEAD(,umtx_pi)	uc_pi_list;

#ifdef UMTX_PROFILING
	u_int 			length;
	u_int			max_length;
#endif
};

#define	UMTXQ_LOCKED_ASSERT(uc)		mtx_assert(&(uc)->uc_lock, MA_OWNED)

/*
 * Don't propagate time-sharing priority, there is a security reason,
 * a user can simply introduce PI-mutex, let thread A lock the mutex,
 * and let another thread B block on the mutex, because B is
 * sleeping, its priority will be boosted, this causes A's priority to
 * be boosted via priority propagating too and will never be lowered even
 * if it is using 100%CPU, this is unfair to other processes.
 */

#define UPRI(td)	(((td)->td_user_pri >= PRI_MIN_TIMESHARE &&\
			  (td)->td_user_pri <= PRI_MAX_TIMESHARE) ?\
			 PRI_MAX_TIMESHARE : (td)->td_user_pri)

#define	GOLDEN_RATIO_PRIME	2654404609U
#ifndef	UMTX_CHAINS
#define	UMTX_CHAINS		512
#endif
#define	UMTX_SHIFTS		(__WORD_BIT - 9)

#define	GET_SHARE(flags)	\
    (((flags) & USYNC_PROCESS_SHARED) == 0 ? THREAD_SHARE : PROCESS_SHARE)

#define BUSY_SPINS		200

struct abs_timeout {
	int clockid;
	bool is_abs_real;	/* TIMER_ABSTIME && CLOCK_REALTIME* */
	struct timespec cur;
	struct timespec end;
};

#ifdef COMPAT_FREEBSD32
struct umutex32 {
	volatile __lwpid_t	m_owner;	/* Owner of the mutex */
	__uint32_t		m_flags;	/* Flags of the mutex */
	__uint32_t		m_ceilings[2];	/* Priority protect ceiling */
	__uint32_t		m_rb_lnk;	/* Robust linkage */
	__uint32_t		m_pad;
	__uint32_t		m_spare[2];
};

_Static_assert(sizeof(struct umutex) == sizeof(struct umutex32), "umutex32");
_Static_assert(__offsetof(struct umutex, m_spare[0]) ==
    __offsetof(struct umutex32, m_spare[0]), "m_spare32");
#endif

int umtx_shm_vnobj_persistent = 0;
SYSCTL_INT(_kern_ipc, OID_AUTO, umtx_vnode_persistent, CTLFLAG_RWTUN,
    &umtx_shm_vnobj_persistent, 0,
    "False forces destruction of umtx attached to file, on last close");
static int umtx_max_rb = 1000;
SYSCTL_INT(_kern_ipc, OID_AUTO, umtx_max_robust, CTLFLAG_RWTUN,
    &umtx_max_rb, 0,
    "");

static uma_zone_t		umtx_pi_zone;
static struct umtxq_chain	umtxq_chains[2][UMTX_CHAINS];
static MALLOC_DEFINE(M_UMTX, "umtx", "UMTX queue memory");
static int			umtx_pi_allocated;

static SYSCTL_NODE(_debug, OID_AUTO, umtx, CTLFLAG_RW, 0, "umtx debug");
SYSCTL_INT(_debug_umtx, OID_AUTO, umtx_pi_allocated, CTLFLAG_RD,
    &umtx_pi_allocated, 0, "Allocated umtx_pi");
static int umtx_verbose_rb = 1;
SYSCTL_INT(_debug_umtx, OID_AUTO, robust_faults_verbose, CTLFLAG_RWTUN,
    &umtx_verbose_rb, 0,
    "");

#ifdef UMTX_PROFILING
static long max_length;
SYSCTL_LONG(_debug_umtx, OID_AUTO, max_length, CTLFLAG_RD, &max_length, 0, "max_length");
static SYSCTL_NODE(_debug_umtx, OID_AUTO, chains, CTLFLAG_RD, 0, "umtx chain stats");
#endif

static void abs_timeout_update(struct abs_timeout *timo);

static void umtx_shm_init(void);
static void umtxq_sysinit(void *);
static void umtxq_hash(struct umtx_key *key);
static struct umtxq_chain *umtxq_getchain(struct umtx_key *key);
static void umtxq_lock(struct umtx_key *key);
static void umtxq_unlock(struct umtx_key *key);
static void umtxq_busy(struct umtx_key *key);
static void umtxq_unbusy(struct umtx_key *key);
static void umtxq_insert_queue(struct umtx_q *uq, int q);
static void umtxq_remove_queue(struct umtx_q *uq, int q);
static int umtxq_sleep(struct umtx_q *uq, const char *wmesg, struct abs_timeout *);
static int umtxq_count(struct umtx_key *key);
static struct umtx_pi *umtx_pi_alloc(int);
static void umtx_pi_free(struct umtx_pi *pi);
static int do_unlock_pp(struct thread *td, struct umutex *m, uint32_t flags,
    bool rb);
static void umtx_thread_cleanup(struct thread *td);
static void umtx_exec_hook(void *arg __unused, struct proc *p __unused,
    struct image_params *imgp __unused);
SYSINIT(umtx, SI_SUB_EVENTHANDLER+1, SI_ORDER_MIDDLE, umtxq_sysinit, NULL);

#define umtxq_signal(key, nwake)	umtxq_signal_queue((key), (nwake), UMTX_SHARED_QUEUE)
#define umtxq_insert(uq)	umtxq_insert_queue((uq), UMTX_SHARED_QUEUE)
#define umtxq_remove(uq)	umtxq_remove_queue((uq), UMTX_SHARED_QUEUE)

static struct mtx umtx_lock;

#ifdef UMTX_PROFILING
static void
umtx_init_profiling(void) 
{
	struct sysctl_oid *chain_oid;
	char chain_name[10];
	int i;

	for (i = 0; i < UMTX_CHAINS; ++i) {
		snprintf(chain_name, sizeof(chain_name), "%d", i);
		chain_oid = SYSCTL_ADD_NODE(NULL, 
		    SYSCTL_STATIC_CHILDREN(_debug_umtx_chains), OID_AUTO, 
		    chain_name, CTLFLAG_RD, NULL, "umtx hash stats");
		SYSCTL_ADD_INT(NULL, SYSCTL_CHILDREN(chain_oid), OID_AUTO,
		    "max_length0", CTLFLAG_RD, &umtxq_chains[0][i].max_length, 0, NULL);
		SYSCTL_ADD_INT(NULL, SYSCTL_CHILDREN(chain_oid), OID_AUTO,
		    "max_length1", CTLFLAG_RD, &umtxq_chains[1][i].max_length, 0, NULL);
	}
}

static int
sysctl_debug_umtx_chains_peaks(SYSCTL_HANDLER_ARGS)
{
	char buf[512];
	struct sbuf sb;
	struct umtxq_chain *uc;
	u_int fract, i, j, tot, whole;
	u_int sf0, sf1, sf2, sf3, sf4;
	u_int si0, si1, si2, si3, si4;
	u_int sw0, sw1, sw2, sw3, sw4;

	sbuf_new(&sb, buf, sizeof(buf), SBUF_FIXEDLEN);
	for (i = 0; i < 2; i++) {
		tot = 0;
		for (j = 0; j < UMTX_CHAINS; ++j) {
			uc = &umtxq_chains[i][j];
			mtx_lock(&uc->uc_lock);
			tot += uc->max_length;
			mtx_unlock(&uc->uc_lock);
		}
		if (tot == 0)
			sbuf_printf(&sb, "%u) Empty ", i);
		else {
			sf0 = sf1 = sf2 = sf3 = sf4 = 0;
			si0 = si1 = si2 = si3 = si4 = 0;
			sw0 = sw1 = sw2 = sw3 = sw4 = 0;
			for (j = 0; j < UMTX_CHAINS; j++) {
				uc = &umtxq_chains[i][j];
				mtx_lock(&uc->uc_lock);
				whole = uc->max_length * 100;
				mtx_unlock(&uc->uc_lock);
				fract = (whole % tot) * 100;
				if (UPROF_PERC_BIGGER(whole, fract, sw0, sf0)) {
					sf0 = fract;
					si0 = j;
					sw0 = whole;
				} else if (UPROF_PERC_BIGGER(whole, fract, sw1,
				    sf1)) {
					sf1 = fract;
					si1 = j;
					sw1 = whole;
				} else if (UPROF_PERC_BIGGER(whole, fract, sw2,
				    sf2)) {
					sf2 = fract;
					si2 = j;
					sw2 = whole;
				} else if (UPROF_PERC_BIGGER(whole, fract, sw3,
				    sf3)) {
					sf3 = fract;
					si3 = j;
					sw3 = whole;
				} else if (UPROF_PERC_BIGGER(whole, fract, sw4,
				    sf4)) {
					sf4 = fract;
					si4 = j;
					sw4 = whole;
				}
			}
			sbuf_printf(&sb, "queue %u:\n", i);
			sbuf_printf(&sb, "1st: %u.%u%% idx: %u\n", sw0 / tot,
			    sf0 / tot, si0);
			sbuf_printf(&sb, "2nd: %u.%u%% idx: %u\n", sw1 / tot,
			    sf1 / tot, si1);
			sbuf_printf(&sb, "3rd: %u.%u%% idx: %u\n", sw2 / tot,
			    sf2 / tot, si2);
			sbuf_printf(&sb, "4th: %u.%u%% idx: %u\n", sw3 / tot,
			    sf3 / tot, si3);
			sbuf_printf(&sb, "5th: %u.%u%% idx: %u\n", sw4 / tot,
			    sf4 / tot, si4);
		}
	}
	sbuf_trim(&sb);
	sbuf_finish(&sb);
	sysctl_handle_string(oidp, sbuf_data(&sb), sbuf_len(&sb), req);
	sbuf_delete(&sb);
	return (0);
}

static int
sysctl_debug_umtx_chains_clear(SYSCTL_HANDLER_ARGS)
{
	struct umtxq_chain *uc;
	u_int i, j;
	int clear, error;

	clear = 0;
	error = sysctl_handle_int(oidp, &clear, 0, req);
	if (error != 0 || req->newptr == NULL)
		return (error);

	if (clear != 0) {
		for (i = 0; i < 2; ++i) {
			for (j = 0; j < UMTX_CHAINS; ++j) {
				uc = &umtxq_chains[i][j];
				mtx_lock(&uc->uc_lock);
				uc->length = 0;
				uc->max_length = 0;	
				mtx_unlock(&uc->uc_lock);
			}
		}
	}
	return (0);
}

SYSCTL_PROC(_debug_umtx_chains, OID_AUTO, clear,
    CTLTYPE_INT | CTLFLAG_RW | CTLFLAG_MPSAFE, 0, 0,
    sysctl_debug_umtx_chains_clear, "I", "Clear umtx chains statistics");
SYSCTL_PROC(_debug_umtx_chains, OID_AUTO, peaks,
    CTLTYPE_STRING | CTLFLAG_RD | CTLFLAG_MPSAFE, 0, 0,
    sysctl_debug_umtx_chains_peaks, "A", "Highest peaks in chains max length");
#endif

static void
umtxq_sysinit(void *arg __unused)
{
	int i, j;

	umtx_pi_zone = uma_zcreate("umtx pi", sizeof(struct umtx_pi),
		NULL, NULL, NULL, NULL, UMA_ALIGN_PTR, 0);
	for (i = 0; i < 2; ++i) {
		for (j = 0; j < UMTX_CHAINS; ++j) {
			mtx_init(&umtxq_chains[i][j].uc_lock, "umtxql", NULL,
				 MTX_DEF | MTX_DUPOK);
			LIST_INIT(&umtxq_chains[i][j].uc_queue[0]);
			LIST_INIT(&umtxq_chains[i][j].uc_queue[1]);
			LIST_INIT(&umtxq_chains[i][j].uc_spare_queue);
			TAILQ_INIT(&umtxq_chains[i][j].uc_pi_list);
			umtxq_chains[i][j].uc_busy = 0;
			umtxq_chains[i][j].uc_waiters = 0;
#ifdef UMTX_PROFILING
			umtxq_chains[i][j].length = 0;
			umtxq_chains[i][j].max_length = 0;	
#endif
		}
	}
#ifdef UMTX_PROFILING
	umtx_init_profiling();
#endif
	mtx_init(&umtx_lock, "umtx lock", NULL, MTX_DEF);
	EVENTHANDLER_REGISTER(process_exec, umtx_exec_hook, NULL,
	    EVENTHANDLER_PRI_ANY);
	umtx_shm_init();
}

struct umtx_q *
umtxq_alloc(void)
{
	struct umtx_q *uq;

	uq = malloc(sizeof(struct umtx_q), M_UMTX, M_WAITOK | M_ZERO);
	uq->uq_spare_queue = malloc(sizeof(struct umtxq_queue), M_UMTX,
	    M_WAITOK | M_ZERO);
	TAILQ_INIT(&uq->uq_spare_queue->head);
	TAILQ_INIT(&uq->uq_pi_contested);
	uq->uq_inherited_pri = PRI_MAX;
	return (uq);
}

void
umtxq_free(struct umtx_q *uq)
{

	MPASS(uq->uq_spare_queue != NULL);
	free(uq->uq_spare_queue, M_UMTX);
	free(uq, M_UMTX);
}

static inline void
umtxq_hash(struct umtx_key *key)
{
	unsigned n;

	n = (uintptr_t)key->info.both.a + key->info.both.b;
	key->hash = ((n * GOLDEN_RATIO_PRIME) >> UMTX_SHIFTS) % UMTX_CHAINS;
}

static inline struct umtxq_chain *
umtxq_getchain(struct umtx_key *key)
{

	if (key->type <= TYPE_SEM)
		return (&umtxq_chains[1][key->hash]);
	return (&umtxq_chains[0][key->hash]);
}

/*
 * Lock a chain.
 */
static inline void
umtxq_lock(struct umtx_key *key)
{
	struct umtxq_chain *uc;

	uc = umtxq_getchain(key);
	mtx_lock(&uc->uc_lock);
}

/*
 * Unlock a chain.
 */
static inline void
umtxq_unlock(struct umtx_key *key)
{
	struct umtxq_chain *uc;

	uc = umtxq_getchain(key);
	mtx_unlock(&uc->uc_lock);
}

/*
 * Set chain to busy state when following operation
 * may be blocked (kernel mutex can not be used).
 */
static inline void
umtxq_busy(struct umtx_key *key)
{
	struct umtxq_chain *uc;

	uc = umtxq_getchain(key);
	mtx_assert(&uc->uc_lock, MA_OWNED);
	if (uc->uc_busy) {
#ifdef SMP
		if (smp_cpus > 1) {
			int count = BUSY_SPINS;
			if (count > 0) {
				umtxq_unlock(key);
				while (uc->uc_busy && --count > 0)
					cpu_spinwait();
				umtxq_lock(key);
			}
		}
#endif
		while (uc->uc_busy) {
			uc->uc_waiters++;
			msleep(uc, &uc->uc_lock, 0, "umtxqb", 0);
			uc->uc_waiters--;
		}
	}
	uc->uc_busy = 1;
}

/*
 * Unbusy a chain.
 */
static inline void
umtxq_unbusy(struct umtx_key *key)
{
	struct umtxq_chain *uc;

	uc = umtxq_getchain(key);
	mtx_assert(&uc->uc_lock, MA_OWNED);
	KASSERT(uc->uc_busy != 0, ("not busy"));
	uc->uc_busy = 0;
	if (uc->uc_waiters)
		wakeup_one(uc);
}

static inline void
umtxq_unbusy_unlocked(struct umtx_key *key)
{

	umtxq_lock(key);
	umtxq_unbusy(key);
	umtxq_unlock(key);
}

static struct umtxq_queue *
umtxq_queue_lookup(struct umtx_key *key, int q)
{
	struct umtxq_queue *uh;
	struct umtxq_chain *uc;

	uc = umtxq_getchain(key);
	UMTXQ_LOCKED_ASSERT(uc);
	LIST_FOREACH(uh, &uc->uc_queue[q], link) {
		if (umtx_key_match(&uh->key, key))
			return (uh);
	}

	return (NULL);
}

static inline void
umtxq_insert_queue(struct umtx_q *uq, int q)
{
	struct umtxq_queue *uh;
	struct umtxq_chain *uc;

	uc = umtxq_getchain(&uq->uq_key);
	UMTXQ_LOCKED_ASSERT(uc);
	KASSERT((uq->uq_flags & UQF_UMTXQ) == 0, ("umtx_q is already on queue"));
	uh = umtxq_queue_lookup(&uq->uq_key, q);
	if (uh != NULL) {
		LIST_INSERT_HEAD(&uc->uc_spare_queue, uq->uq_spare_queue, link);
	} else {
		uh = uq->uq_spare_queue;
		uh->key = uq->uq_key;
		LIST_INSERT_HEAD(&uc->uc_queue[q], uh, link);
#ifdef UMTX_PROFILING
		uc->length++;
		if (uc->length > uc->max_length) {
			uc->max_length = uc->length;
			if (uc->max_length > max_length)
				max_length = uc->max_length;	
		}
#endif
	}
	uq->uq_spare_queue = NULL;

	TAILQ_INSERT_TAIL(&uh->head, uq, uq_link);
	uh->length++;
	uq->uq_flags |= UQF_UMTXQ;
	uq->uq_cur_queue = uh;
	return;
}

static inline void
umtxq_remove_queue(struct umtx_q *uq, int q)
{
	struct umtxq_chain *uc;
	struct umtxq_queue *uh;

	uc = umtxq_getchain(&uq->uq_key);
	UMTXQ_LOCKED_ASSERT(uc);
	if (uq->uq_flags & UQF_UMTXQ) {
		uh = uq->uq_cur_queue;
		TAILQ_REMOVE(&uh->head, uq, uq_link);
		uh->length--;
		uq->uq_flags &= ~UQF_UMTXQ;
		if (TAILQ_EMPTY(&uh->head)) {
			KASSERT(uh->length == 0,
			    ("inconsistent umtxq_queue length"));
#ifdef UMTX_PROFILING
			uc->length--;
#endif
			LIST_REMOVE(uh, link);
		} else {
			uh = LIST_FIRST(&uc->uc_spare_queue);
			KASSERT(uh != NULL, ("uc_spare_queue is empty"));
			LIST_REMOVE(uh, link);
		}
		uq->uq_spare_queue = uh;
		uq->uq_cur_queue = NULL;
	}
}

/*
 * Check if there are multiple waiters
 */
static int
umtxq_count(struct umtx_key *key)
{
	struct umtxq_queue *uh;

	UMTXQ_LOCKED_ASSERT(umtxq_getchain(key));
	uh = umtxq_queue_lookup(key, UMTX_SHARED_QUEUE);
	if (uh != NULL)
		return (uh->length);
	return (0);
}

/*
 * Check if there are multiple PI waiters and returns first
 * waiter.
 */
static int
umtxq_count_pi(struct umtx_key *key, struct umtx_q **first)
{
	struct umtxq_queue *uh;

	*first = NULL;
	UMTXQ_LOCKED_ASSERT(umtxq_getchain(key));
	uh = umtxq_queue_lookup(key, UMTX_SHARED_QUEUE);
	if (uh != NULL) {
		*first = TAILQ_FIRST(&uh->head);
		return (uh->length);
	}
	return (0);
}

static int
umtxq_check_susp(struct thread *td)
{
	struct proc *p;
	int error;

	/*
	 * The check for TDF_NEEDSUSPCHK is racy, but it is enough to
	 * eventually break the lockstep loop.
	 */
	if ((td->td_flags & TDF_NEEDSUSPCHK) == 0)
		return (0);
	error = 0;
	p = td->td_proc;
	PROC_LOCK(p);
	if (P_SHOULDSTOP(p) ||
	    ((p->p_flag & P_TRACED) && (td->td_dbgflags & TDB_SUSPEND))) {
		if (p->p_flag & P_SINGLE_EXIT)
			error = EINTR;
		else
			error = ERESTART;
	}
	PROC_UNLOCK(p);
	return (error);
}

/*
 * Wake up threads waiting on an userland object.
 */

static int
umtxq_signal_queue(struct umtx_key *key, int n_wake, int q)
{
	struct umtxq_queue *uh;
	struct umtx_q *uq;
	int ret;

	ret = 0;
	UMTXQ_LOCKED_ASSERT(umtxq_getchain(key));
	uh = umtxq_queue_lookup(key, q);
	if (uh != NULL) {
		while ((uq = TAILQ_FIRST(&uh->head)) != NULL) {
			umtxq_remove_queue(uq, q);
			wakeup(uq);
			if (++ret >= n_wake)
				return (ret);
		}
	}
	return (ret);
}


/*
 * Wake up specified thread.
 */
static inline void
umtxq_signal_thread(struct umtx_q *uq)
{

	UMTXQ_LOCKED_ASSERT(umtxq_getchain(&uq->uq_key));
	umtxq_remove(uq);
	wakeup(uq);
}

static inline int 
tstohz(const struct timespec *tsp)
{
	struct timeval tv;

	TIMESPEC_TO_TIMEVAL(&tv, tsp);
	return tvtohz(&tv);
}

static void
abs_timeout_init(struct abs_timeout *timo, int clockid, int absolute,
	const struct timespec *timeout)
{

	timo->clockid = clockid;
	if (!absolute) {
		timo->is_abs_real = false;
		abs_timeout_update(timo);
		timespecadd(&timo->cur, timeout, &timo->end);
	} else {
		timo->end = *timeout;
		timo->is_abs_real = clockid == CLOCK_REALTIME ||
		    clockid == CLOCK_REALTIME_FAST ||
		    clockid == CLOCK_REALTIME_PRECISE;
		/*
		 * If is_abs_real, umtxq_sleep will read the clock
		 * after setting td_rtcgen; otherwise, read it here.
		 */
		if (!timo->is_abs_real) {
			abs_timeout_update(timo);
		}
	}
}

static void
abs_timeout_init2(struct abs_timeout *timo, const struct _umtx_time *umtxtime)
{

	abs_timeout_init(timo, umtxtime->_clockid,
	    (umtxtime->_flags & UMTX_ABSTIME) != 0, &umtxtime->_timeout);
}

static inline void
abs_timeout_update(struct abs_timeout *timo)
{

	kern_clock_gettime(curthread, timo->clockid, &timo->cur);
}

static int
abs_timeout_gethz(struct abs_timeout *timo)
{
	struct timespec tts;

	if (timespeccmp(&timo->end, &timo->cur, <=))
		return (-1); 
	timespecsub(&timo->end, &timo->cur, &tts);
	return (tstohz(&tts));
}

static uint32_t
umtx_unlock_val(uint32_t flags, bool rb)
{

	if (rb)
		return (UMUTEX_RB_OWNERDEAD);
	else if ((flags & UMUTEX_NONCONSISTENT) != 0)
		return (UMUTEX_RB_NOTRECOV);
	else
		return (UMUTEX_UNOWNED);

}

/*
 * Put thread into sleep state, before sleeping, check if
 * thread was removed from umtx queue.
 */
static inline int
umtxq_sleep(struct umtx_q *uq, const char *wmesg, struct abs_timeout *abstime)
{
	struct umtxq_chain *uc;
	int error, timo;

	if (abstime != NULL && abstime->is_abs_real) {
		curthread->td_rtcgen = atomic_load_acq_int(&rtc_generation);
		abs_timeout_update(abstime);
	}

	uc = umtxq_getchain(&uq->uq_key);
	UMTXQ_LOCKED_ASSERT(uc);
	for (;;) {
		if (!(uq->uq_flags & UQF_UMTXQ)) {
			error = 0;
			break;
		}
		if (abstime != NULL) {
			timo = abs_timeout_gethz(abstime);
			if (timo < 0) {
				error = ETIMEDOUT;
				break;
			}
		} else
			timo = 0;
		error = msleep(uq, &uc->uc_lock, PCATCH | PDROP, wmesg, timo);
		if (error == EINTR || error == ERESTART) {
			umtxq_lock(&uq->uq_key);
			break;
		}
		if (abstime != NULL) {
			if (abstime->is_abs_real)
				curthread->td_rtcgen =
				    atomic_load_acq_int(&rtc_generation);
			abs_timeout_update(abstime);
		}
		umtxq_lock(&uq->uq_key);
	}

	curthread->td_rtcgen = 0;
	return (error);
}

/*
 * Convert userspace address into unique logical address.
 */
int
umtx_key_get(const void *addr, int type, int share, struct umtx_key *key)
{
	struct thread *td = curthread;
	vm_map_t map;
	vm_map_entry_t entry;
	vm_pindex_t pindex;
	vm_prot_t prot;
	boolean_t wired;

	key->type = type;
	if (share == THREAD_SHARE) {
		key->shared = 0;
		key->info.private.vs = td->td_proc->p_vmspace;
		key->info.private.addr = (uintptr_t)addr;
	} else {
		MPASS(share == PROCESS_SHARE || share == AUTO_SHARE);
		map = &td->td_proc->p_vmspace->vm_map;
		if (vm_map_lookup(&map, (vm_offset_t)addr, VM_PROT_WRITE,
		    &entry, &key->info.shared.object, &pindex, &prot,
		    &wired) != KERN_SUCCESS) {
			return (EFAULT);
		}

		if ((share == PROCESS_SHARE) ||
		    (share == AUTO_SHARE &&
		     VM_INHERIT_SHARE == entry->inheritance)) {
			key->shared = 1;
			key->info.shared.offset = (vm_offset_t)addr -
			    entry->start + entry->offset;
			vm_object_reference(key->info.shared.object);
		} else {
			key->shared = 0;
			key->info.private.vs = td->td_proc->p_vmspace;
			key->info.private.addr = (uintptr_t)addr;
		}
		vm_map_lookup_done(map, entry);
	}

	umtxq_hash(key);
	return (0);
}

/*
 * Release key.
 */
void
umtx_key_release(struct umtx_key *key)
{
	if (key->shared)
		vm_object_deallocate(key->info.shared.object);
}

/*
 * Fetch and compare value, sleep on the address if value is not changed.
 */
static int
do_wait(struct thread *td, void *addr, u_long id,
    struct _umtx_time *timeout, int compat32, int is_private)
{
	struct abs_timeout timo;
	struct umtx_q *uq;
	u_long tmp;
	uint32_t tmp32;
	int error = 0;

	uq = td->td_umtxq;
	if ((error = umtx_key_get(addr, TYPE_SIMPLE_WAIT,
		is_private ? THREAD_SHARE : AUTO_SHARE, &uq->uq_key)) != 0)
		return (error);

	if (timeout != NULL)
		abs_timeout_init2(&timo, timeout);

	umtxq_lock(&uq->uq_key);
	umtxq_insert(uq);
	umtxq_unlock(&uq->uq_key);
	if (compat32 == 0) {
		error = fueword(addr, &tmp);
		if (error != 0)
			error = EFAULT;
	} else {
		error = fueword32(addr, &tmp32);
		if (error == 0)
			tmp = tmp32;
		else
			error = EFAULT;
	}
	umtxq_lock(&uq->uq_key);
	if (error == 0) {
		if (tmp == id)
			error = umtxq_sleep(uq, "uwait", timeout == NULL ?
			    NULL : &timo);
		if ((uq->uq_flags & UQF_UMTXQ) == 0)
			error = 0;
		else
			umtxq_remove(uq);
	} else if ((uq->uq_flags & UQF_UMTXQ) != 0) {
		umtxq_remove(uq);
	}
	umtxq_unlock(&uq->uq_key);
	umtx_key_release(&uq->uq_key);
	if (error == ERESTART)
		error = EINTR;
	return (error);
}

/*
 * Wake up threads sleeping on the specified address.
 */
int
kern_umtx_wake(struct thread *td, void *uaddr, int n_wake, int is_private)
{
	struct umtx_key key;
	int ret;
	
	if ((ret = umtx_key_get(uaddr, TYPE_SIMPLE_WAIT,
	    is_private ? THREAD_SHARE : AUTO_SHARE, &key)) != 0)
		return (ret);
	umtxq_lock(&key);
	umtxq_signal(&key, n_wake);
	umtxq_unlock(&key);
	umtx_key_release(&key);
	return (0);
}

/*
 * Lock PTHREAD_PRIO_NONE protocol POSIX mutex.
 */
static int
do_lock_normal(struct thread *td, struct umutex *m, uint32_t flags,
    struct _umtx_time *timeout, int mode)
{
	struct abs_timeout timo;
	struct umtx_q *uq;
	uint32_t owner, old, id;
	int error, rv;

	id = td->td_tid;
	uq = td->td_umtxq;
	error = 0;
	if (timeout != NULL)
		abs_timeout_init2(&timo, timeout);

	/*
	 * Care must be exercised when dealing with umtx structure. It
	 * can fault on any access.
	 */
	for (;;) {
		rv = fueword32(&m->m_owner, &owner);
		if (rv == -1)
			return (EFAULT);
		if (mode == _UMUTEX_WAIT) {
			if (owner == UMUTEX_UNOWNED ||
			    owner == UMUTEX_CONTESTED ||
			    owner == UMUTEX_RB_OWNERDEAD ||
			    owner == UMUTEX_RB_NOTRECOV)
				return (0);
		} else {
			/*
			 * Robust mutex terminated.  Kernel duty is to
			 * return EOWNERDEAD to the userspace.  The
			 * umutex.m_flags UMUTEX_NONCONSISTENT is set
			 * by the common userspace code.
			 */
			if (owner == UMUTEX_RB_OWNERDEAD) {
				rv = casueword32(&m->m_owner,
				    UMUTEX_RB_OWNERDEAD, &owner,
				    id | UMUTEX_CONTESTED);
				if (rv == -1)
					return (EFAULT);
				if (owner == UMUTEX_RB_OWNERDEAD)
					return (EOWNERDEAD); /* success */
				rv = umtxq_check_susp(td);
				if (rv != 0)
					return (rv);
				continue;
			}
			if (owner == UMUTEX_RB_NOTRECOV)
				return (ENOTRECOVERABLE);


			/*
			 * Try the uncontested case.  This should be
			 * done in userland.
			 */
			rv = casueword32(&m->m_owner, UMUTEX_UNOWNED,
			    &owner, id);
			/* The address was invalid. */
			if (rv == -1)
				return (EFAULT);

			/* The acquire succeeded. */
			if (owner == UMUTEX_UNOWNED)
				return (0);

			/*
			 * If no one owns it but it is contested try
			 * to acquire it.
			 */
			if (owner == UMUTEX_CONTESTED) {
				rv = casueword32(&m->m_owner,
				    UMUTEX_CONTESTED, &owner,
				    id | UMUTEX_CONTESTED);
				/* The address was invalid. */
				if (rv == -1)
					return (EFAULT);

				if (owner == UMUTEX_CONTESTED)
					return (0);

				rv = umtxq_check_susp(td);
				if (rv != 0)
					return (rv);

				/*
				 * If this failed the lock has
				 * changed, restart.
				 */
				continue;
			}
		}

		if (mode == _UMUTEX_TRY)
			return (EBUSY);

		/*
		 * If we caught a signal, we have retried and now
		 * exit immediately.
		 */
		if (error != 0)
			return (error);

		if ((error = umtx_key_get(m, TYPE_NORMAL_UMUTEX,
		    GET_SHARE(flags), &uq->uq_key)) != 0)
			return (error);

		umtxq_lock(&uq->uq_key);
		umtxq_busy(&uq->uq_key);
		umtxq_insert(uq);
		umtxq_unlock(&uq->uq_key);

		/*
		 * Set the contested bit so that a release in user space
		 * knows to use the system call for unlock.  If this fails
		 * either some one else has acquired the lock or it has been
		 * released.
		 */
		rv = casueword32(&m->m_owner, owner, &old,
		    owner | UMUTEX_CONTESTED);

		/* The address was invalid. */
		if (rv == -1) {
			umtxq_lock(&uq->uq_key);
			umtxq_remove(uq);
			umtxq_unbusy(&uq->uq_key);
			umtxq_unlock(&uq->uq_key);
			umtx_key_release(&uq->uq_key);
			return (EFAULT);
		}

		/*
		 * We set the contested bit, sleep. Otherwise the lock changed
		 * and we need to retry or we lost a race to the thread
		 * unlocking the umtx.
		 */
		umtxq_lock(&uq->uq_key);
		umtxq_unbusy(&uq->uq_key);
		if (old == owner)
			error = umtxq_sleep(uq, "umtxn", timeout == NULL ?
			    NULL : &timo);
		umtxq_remove(uq);
		umtxq_unlock(&uq->uq_key);
		umtx_key_release(&uq->uq_key);

		if (error == 0)
			error = umtxq_check_susp(td);
	}

	return (0);
}

/*
 * Unlock PTHREAD_PRIO_NONE protocol POSIX mutex.
 */
static int
do_unlock_normal(struct thread *td, struct umutex *m, uint32_t flags, bool rb)
{
	struct umtx_key key;
	uint32_t owner, old, id, newlock;
	int error, count;

	id = td->td_tid;
	/*
	 * Make sure we own this mtx.
	 */
	error = fueword32(&m->m_owner, &owner);
	if (error == -1)
		return (EFAULT);

	if ((owner & ~UMUTEX_CONTESTED) != id)
		return (EPERM);

	newlock = umtx_unlock_val(flags, rb);
	if ((owner & UMUTEX_CONTESTED) == 0) {
		error = casueword32(&m->m_owner, owner, &old, newlock);
		if (error == -1)
			return (EFAULT);
		if (old == owner)
			return (0);
		owner = old;
	}

	/* We should only ever be in here for contested locks */
	if ((error = umtx_key_get(m, TYPE_NORMAL_UMUTEX, GET_SHARE(flags),
	    &key)) != 0)
		return (error);

	umtxq_lock(&key);
	umtxq_busy(&key);
	count = umtxq_count(&key);
	umtxq_unlock(&key);

	/*
	 * When unlocking the umtx, it must be marked as unowned if
	 * there is zero or one thread only waiting for it.
	 * Otherwise, it must be marked as contested.
	 */
	if (count > 1)
		newlock |= UMUTEX_CONTESTED;
	error = casueword32(&m->m_owner, owner, &old, newlock);
	umtxq_lock(&key);
	umtxq_signal(&key, 1);
	umtxq_unbusy(&key);
	umtxq_unlock(&key);
	umtx_key_release(&key);
	if (error == -1)
		return (EFAULT);
	if (old != owner)
		return (EINVAL);
	return (0);
}

/*
 * Check if the mutex is available and wake up a waiter,
 * only for simple mutex.
 */
static int
do_wake_umutex(struct thread *td, struct umutex *m)
{
	struct umtx_key key;
	uint32_t owner;
	uint32_t flags;
	int error;
	int count;

	error = fueword32(&m->m_owner, &owner);
	if (error == -1)
		return (EFAULT);

	if ((owner & ~UMUTEX_CONTESTED) != 0 && owner != UMUTEX_RB_OWNERDEAD &&
	    owner != UMUTEX_RB_NOTRECOV)
		return (0);

	error = fueword32(&m->m_flags, &flags);
	if (error == -1)
		return (EFAULT);

	/* We should only ever be in here for contested locks */
	if ((error = umtx_key_get(m, TYPE_NORMAL_UMUTEX, GET_SHARE(flags),
	    &key)) != 0)
		return (error);

	umtxq_lock(&key);
	umtxq_busy(&key);
	count = umtxq_count(&key);
	umtxq_unlock(&key);

	if (count <= 1 && owner != UMUTEX_RB_OWNERDEAD &&
	    owner != UMUTEX_RB_NOTRECOV) {
		error = casueword32(&m->m_owner, UMUTEX_CONTESTED, &owner,
		    UMUTEX_UNOWNED);
		if (error == -1)
			error = EFAULT;
	}

	umtxq_lock(&key);
	if (error == 0 && count != 0 && ((owner & ~UMUTEX_CONTESTED) == 0 ||
	    owner == UMUTEX_RB_OWNERDEAD || owner == UMUTEX_RB_NOTRECOV))
		umtxq_signal(&key, 1);
	umtxq_unbusy(&key);
	umtxq_unlock(&key);
	umtx_key_release(&key);
	return (error);
}

/*
 * Check if the mutex has waiters and tries to fix contention bit.
 */
static int
do_wake2_umutex(struct thread *td, struct umutex *m, uint32_t flags)
{
	struct umtx_key key;
	uint32_t owner, old;
	int type;
	int error;
	int count;

	switch (flags & (UMUTEX_PRIO_INHERIT | UMUTEX_PRIO_PROTECT |
	    UMUTEX_ROBUST)) {
	case 0:
	case UMUTEX_ROBUST:
		type = TYPE_NORMAL_UMUTEX;
		break;
	case UMUTEX_PRIO_INHERIT:
		type = TYPE_PI_UMUTEX;
		break;
	case (UMUTEX_PRIO_INHERIT | UMUTEX_ROBUST):
		type = TYPE_PI_ROBUST_UMUTEX;
		break;
	case UMUTEX_PRIO_PROTECT:
		type = TYPE_PP_UMUTEX;
		break;
	case (UMUTEX_PRIO_PROTECT | UMUTEX_ROBUST):
		type = TYPE_PP_ROBUST_UMUTEX;
		break;
	default:
		return (EINVAL);
	}
	if ((error = umtx_key_get(m, type, GET_SHARE(flags), &key)) != 0)
		return (error);

	owner = 0;
	umtxq_lock(&key);
	umtxq_busy(&key);
	count = umtxq_count(&key);
	umtxq_unlock(&key);
	/*
	 * Only repair contention bit if there is a waiter, this means the mutex
	 * is still being referenced by userland code, otherwise don't update
	 * any memory.
	 */
	if (count > 1) {
		error = fueword32(&m->m_owner, &owner);
		if (error == -1)
			error = EFAULT;
		while (error == 0 && (owner & UMUTEX_CONTESTED) == 0) {
			error = casueword32(&m->m_owner, owner, &old,
			    owner | UMUTEX_CONTESTED);
			if (error == -1) {
				error = EFAULT;
				break;
			}
			if (old == owner)
				break;
			owner = old;
			error = umtxq_check_susp(td);
			if (error != 0)
				break;
		}
	} else if (count == 1) {
		error = fueword32(&m->m_owner, &owner);
		if (error == -1)
			error = EFAULT;
		while (error == 0 && (owner & ~UMUTEX_CONTESTED) != 0 &&
		    (owner & UMUTEX_CONTESTED) == 0) {
			error = casueword32(&m->m_owner, owner, &old,
			    owner | UMUTEX_CONTESTED);
			if (error == -1) {
				error = EFAULT;
				break;
			}
			if (old == owner)
				break;
			owner = old;
			error = umtxq_check_susp(td);
			if (error != 0)
				break;
		}
	}
	umtxq_lock(&key);
	if (error == EFAULT) {
		umtxq_signal(&key, INT_MAX);
	} else if (count != 0 && ((owner & ~UMUTEX_CONTESTED) == 0 ||
	    owner == UMUTEX_RB_OWNERDEAD || owner == UMUTEX_RB_NOTRECOV))
		umtxq_signal(&key, 1);
	umtxq_unbusy(&key);
	umtxq_unlock(&key);
	umtx_key_release(&key);
	return (error);
}

static inline struct umtx_pi *
umtx_pi_alloc(int flags)
{
	struct umtx_pi *pi;

	pi = uma_zalloc(umtx_pi_zone, M_ZERO | flags);
	TAILQ_INIT(&pi->pi_blocked);
	atomic_add_int(&umtx_pi_allocated, 1);
	return (pi);
}

static inline void
umtx_pi_free(struct umtx_pi *pi)
{
	uma_zfree(umtx_pi_zone, pi);
	atomic_add_int(&umtx_pi_allocated, -1);
}

/*
 * Adjust the thread's position on a pi_state after its priority has been
 * changed.
 */
static int
umtx_pi_adjust_thread(struct umtx_pi *pi, struct thread *td)
{
	struct umtx_q *uq, *uq1, *uq2;
	struct thread *td1;

	mtx_assert(&umtx_lock, MA_OWNED);
	if (pi == NULL)
		return (0);

	uq = td->td_umtxq;

	/*
	 * Check if the thread needs to be moved on the blocked chain.
	 * It needs to be moved if either its priority is lower than
	 * the previous thread or higher than the next thread.
	 */
	uq1 = TAILQ_PREV(uq, umtxq_head, uq_lockq);
	uq2 = TAILQ_NEXT(uq, uq_lockq);
	if ((uq1 != NULL && UPRI(td) < UPRI(uq1->uq_thread)) ||
	    (uq2 != NULL && UPRI(td) > UPRI(uq2->uq_thread))) {
		/*
		 * Remove thread from blocked chain and determine where
		 * it should be moved to.
		 */
		TAILQ_REMOVE(&pi->pi_blocked, uq, uq_lockq);
		TAILQ_FOREACH(uq1, &pi->pi_blocked, uq_lockq) {
			td1 = uq1->uq_thread;
			MPASS(td1->td_proc->p_magic == P_MAGIC);
			if (UPRI(td1) > UPRI(td))
				break;
		}

		if (uq1 == NULL)
			TAILQ_INSERT_TAIL(&pi->pi_blocked, uq, uq_lockq);
		else
			TAILQ_INSERT_BEFORE(uq1, uq, uq_lockq);
	}
	return (1);
}

static struct umtx_pi *
umtx_pi_next(struct umtx_pi *pi)
{
	struct umtx_q *uq_owner;

	if (pi->pi_owner == NULL)
		return (NULL);
	uq_owner = pi->pi_owner->td_umtxq;
	if (uq_owner == NULL)
		return (NULL);
	return (uq_owner->uq_pi_blocked);
}

/*
 * Floyd's Cycle-Finding Algorithm.
 */
static bool
umtx_pi_check_loop(struct umtx_pi *pi)
{
	struct umtx_pi *pi1;	/* fast iterator */

	mtx_assert(&umtx_lock, MA_OWNED);
	if (pi == NULL)
		return (false);
	pi1 = pi;
	for (;;) {
		pi = umtx_pi_next(pi);
		if (pi == NULL)
			break;
		pi1 = umtx_pi_next(pi1);
		if (pi1 == NULL)
			break;
		pi1 = umtx_pi_next(pi1);
		if (pi1 == NULL)
			break;
		if (pi == pi1)
			return (true);
	}
	return (false);
}

/*
 * Propagate priority when a thread is blocked on POSIX
 * PI mutex.
 */ 
static void
umtx_propagate_priority(struct thread *td)
{
	struct umtx_q *uq;
	struct umtx_pi *pi;
	int pri;

	mtx_assert(&umtx_lock, MA_OWNED);
	pri = UPRI(td);
	uq = td->td_umtxq;
	pi = uq->uq_pi_blocked;
	if (pi == NULL)
		return;
	if (umtx_pi_check_loop(pi))
		return;

	for (;;) {
		td = pi->pi_owner;
		if (td == NULL || td == curthread)
			return;

		MPASS(td->td_proc != NULL);
		MPASS(td->td_proc->p_magic == P_MAGIC);

		thread_lock(td);
		if (td->td_lend_user_pri > pri)
			sched_lend_user_prio(td, pri);
		else {
			thread_unlock(td);
			break;
		}
		thread_unlock(td);

		/*
		 * Pick up the lock that td is blocked on.
		 */
		uq = td->td_umtxq;
		pi = uq->uq_pi_blocked;
		if (pi == NULL)
			break;
		/* Resort td on the list if needed. */
		umtx_pi_adjust_thread(pi, td);
	}
}

/*
 * Unpropagate priority for a PI mutex when a thread blocked on
 * it is interrupted by signal or resumed by others.
 */
static void
umtx_repropagate_priority(struct umtx_pi *pi)
{
	struct umtx_q *uq, *uq_owner;
	struct umtx_pi *pi2;
	int pri;

	mtx_assert(&umtx_lock, MA_OWNED);

	if (umtx_pi_check_loop(pi))
		return;
	while (pi != NULL && pi->pi_owner != NULL) {
		pri = PRI_MAX;
		uq_owner = pi->pi_owner->td_umtxq;

		TAILQ_FOREACH(pi2, &uq_owner->uq_pi_contested, pi_link) {
			uq = TAILQ_FIRST(&pi2->pi_blocked);
			if (uq != NULL) {
				if (pri > UPRI(uq->uq_thread))
					pri = UPRI(uq->uq_thread);
			}
		}

		if (pri > uq_owner->uq_inherited_pri)
			pri = uq_owner->uq_inherited_pri;
		thread_lock(pi->pi_owner);
		sched_lend_user_prio(pi->pi_owner, pri);
		thread_unlock(pi->pi_owner);
		if ((pi = uq_owner->uq_pi_blocked) != NULL)
			umtx_pi_adjust_thread(pi, uq_owner->uq_thread);
	}
}

/*
 * Insert a PI mutex into owned list.
 */
static void
umtx_pi_setowner(struct umtx_pi *pi, struct thread *owner)
{
	struct umtx_q *uq_owner;

	uq_owner = owner->td_umtxq;
	mtx_assert(&umtx_lock, MA_OWNED);
	MPASS(pi->pi_owner == NULL);
	pi->pi_owner = owner;
	TAILQ_INSERT_TAIL(&uq_owner->uq_pi_contested, pi, pi_link);
}


/*
 * Disown a PI mutex, and remove it from the owned list.
 */
static void
umtx_pi_disown(struct umtx_pi *pi)
{

	mtx_assert(&umtx_lock, MA_OWNED);
	TAILQ_REMOVE(&pi->pi_owner->td_umtxq->uq_pi_contested, pi, pi_link);
	pi->pi_owner = NULL;
}

/*
 * Claim ownership of a PI mutex.
 */
static int
umtx_pi_claim(struct umtx_pi *pi, struct thread *owner)
{
	struct umtx_q *uq;
	int pri;

	mtx_lock(&umtx_lock);
	if (pi->pi_owner == owner) {
		mtx_unlock(&umtx_lock);
		return (0);
	}

	if (pi->pi_owner != NULL) {
		/*
		 * userland may have already messed the mutex, sigh.
		 */
		mtx_unlock(&umtx_lock);
		return (EPERM);
	}
	umtx_pi_setowner(pi, owner);
	uq = TAILQ_FIRST(&pi->pi_blocked);
	if (uq != NULL) {
		pri = UPRI(uq->uq_thread);
		thread_lock(owner);
		if (pri < UPRI(owner))
			sched_lend_user_prio(owner, pri);
		thread_unlock(owner);
	}
	mtx_unlock(&umtx_lock);
	return (0);
}

/*
 * Adjust a thread's order position in its blocked PI mutex,
 * this may result new priority propagating process.
 */
void
umtx_pi_adjust(struct thread *td, u_char oldpri)
{
	struct umtx_q *uq;
	struct umtx_pi *pi;

	uq = td->td_umtxq;
	mtx_lock(&umtx_lock);
	/*
	 * Pick up the lock that td is blocked on.
	 */
	pi = uq->uq_pi_blocked;
	if (pi != NULL) {
		umtx_pi_adjust_thread(pi, td);
		umtx_repropagate_priority(pi);
	}
	mtx_unlock(&umtx_lock);
}

/*
 * Sleep on a PI mutex.
 */
static int
umtxq_sleep_pi(struct umtx_q *uq, struct umtx_pi *pi, uint32_t owner,
    const char *wmesg, struct abs_timeout *timo, bool shared)
{
	struct thread *td, *td1;
	struct umtx_q *uq1;
	int error, pri;
#ifdef INVARIANTS
	struct umtxq_chain *uc;

	uc = umtxq_getchain(&pi->pi_key);
#endif
	error = 0;
	td = uq->uq_thread;
	KASSERT(td == curthread, ("inconsistent uq_thread"));
	UMTXQ_LOCKED_ASSERT(umtxq_getchain(&uq->uq_key));
	KASSERT(uc->uc_busy != 0, ("umtx chain is not busy"));
	umtxq_insert(uq);
	mtx_lock(&umtx_lock);
	if (pi->pi_owner == NULL) {
		mtx_unlock(&umtx_lock);
		td1 = tdfind(owner, shared ? -1 : td->td_proc->p_pid);
		mtx_lock(&umtx_lock);
		if (td1 != NULL) {
			if (pi->pi_owner == NULL)
				umtx_pi_setowner(pi, td1);
			PROC_UNLOCK(td1->td_proc);
		}
	}

	TAILQ_FOREACH(uq1, &pi->pi_blocked, uq_lockq) {
		pri = UPRI(uq1->uq_thread);
		if (pri > UPRI(td))
			break;
	}

	if (uq1 != NULL)
		TAILQ_INSERT_BEFORE(uq1, uq, uq_lockq);
	else
		TAILQ_INSERT_TAIL(&pi->pi_blocked, uq, uq_lockq);

	uq->uq_pi_blocked = pi;
	thread_lock(td);
	td->td_flags |= TDF_UPIBLOCKED;
	thread_unlock(td);
	umtx_propagate_priority(td);
	mtx_unlock(&umtx_lock);
	umtxq_unbusy(&uq->uq_key);

	error = umtxq_sleep(uq, wmesg, timo);
	umtxq_remove(uq);

	mtx_lock(&umtx_lock);
	uq->uq_pi_blocked = NULL;
	thread_lock(td);
	td->td_flags &= ~TDF_UPIBLOCKED;
	thread_unlock(td);
	TAILQ_REMOVE(&pi->pi_blocked, uq, uq_lockq);
	umtx_repropagate_priority(pi);
	mtx_unlock(&umtx_lock);
	umtxq_unlock(&uq->uq_key);

	return (error);
}

/*
 * Add reference count for a PI mutex.
 */
static void
umtx_pi_ref(struct umtx_pi *pi)
{

	UMTXQ_LOCKED_ASSERT(umtxq_getchain(&pi->pi_key));
	pi->pi_refcount++;
}

/*
 * Decrease reference count for a PI mutex, if the counter
 * is decreased to zero, its memory space is freed.
 */ 
static void
umtx_pi_unref(struct umtx_pi *pi)
{
	struct umtxq_chain *uc;

	uc = umtxq_getchain(&pi->pi_key);
	UMTXQ_LOCKED_ASSERT(uc);
	KASSERT(pi->pi_refcount > 0, ("invalid reference count"));
	if (--pi->pi_refcount == 0) {
		mtx_lock(&umtx_lock);
		if (pi->pi_owner != NULL)
			umtx_pi_disown(pi);
		KASSERT(TAILQ_EMPTY(&pi->pi_blocked),
			("blocked queue not empty"));
		mtx_unlock(&umtx_lock);
		TAILQ_REMOVE(&uc->uc_pi_list, pi, pi_hashlink);
		umtx_pi_free(pi);
	}
}

/*
 * Find a PI mutex in hash table.
 */
static struct umtx_pi *
umtx_pi_lookup(struct umtx_key *key)
{
	struct umtxq_chain *uc;
	struct umtx_pi *pi;

	uc = umtxq_getchain(key);
	UMTXQ_LOCKED_ASSERT(uc);

	TAILQ_FOREACH(pi, &uc->uc_pi_list, pi_hashlink) {
		if (umtx_key_match(&pi->pi_key, key)) {
			return (pi);
		}
	}
	return (NULL);
}

/*
 * Insert a PI mutex into hash table.
 */
static inline void
umtx_pi_insert(struct umtx_pi *pi)
{
	struct umtxq_chain *uc;

	uc = umtxq_getchain(&pi->pi_key);
	UMTXQ_LOCKED_ASSERT(uc);
	TAILQ_INSERT_TAIL(&uc->uc_pi_list, pi, pi_hashlink);
}

/*
 * Lock a PI mutex.
 */
static int
do_lock_pi(struct thread *td, struct umutex *m, uint32_t flags,
    struct _umtx_time *timeout, int try)
{
	struct abs_timeout timo;
	struct umtx_q *uq;
	struct umtx_pi *pi, *new_pi;
	uint32_t id, old_owner, owner, old;
	int error, rv;

	id = td->td_tid;
	uq = td->td_umtxq;

	if ((error = umtx_key_get(m, (flags & UMUTEX_ROBUST) != 0 ?
	    TYPE_PI_ROBUST_UMUTEX : TYPE_PI_UMUTEX, GET_SHARE(flags),
	    &uq->uq_key)) != 0)
		return (error);

	if (timeout != NULL)
		abs_timeout_init2(&timo, timeout);

	umtxq_lock(&uq->uq_key);
	pi = umtx_pi_lookup(&uq->uq_key);
	if (pi == NULL) {
		new_pi = umtx_pi_alloc(M_NOWAIT);
		if (new_pi == NULL) {
			umtxq_unlock(&uq->uq_key);
			new_pi = umtx_pi_alloc(M_WAITOK);
			umtxq_lock(&uq->uq_key);
			pi = umtx_pi_lookup(&uq->uq_key);
			if (pi != NULL) {
				umtx_pi_free(new_pi);
				new_pi = NULL;
			}
		}
		if (new_pi != NULL) {
			new_pi->pi_key = uq->uq_key;
			umtx_pi_insert(new_pi);
			pi = new_pi;
		}
	}
	umtx_pi_ref(pi);
	umtxq_unlock(&uq->uq_key);

	/*
	 * Care must be exercised when dealing with umtx structure.  It
	 * can fault on any access.
	 */
	for (;;) {
		/*
		 * Try the uncontested case.  This should be done in userland.
		 */
		rv = casueword32(&m->m_owner, UMUTEX_UNOWNED, &owner, id);
		/* The address was invalid. */
		if (rv == -1) {
			error = EFAULT;
			break;
		}

		/* The acquire succeeded. */
		if (owner == UMUTEX_UNOWNED) {
			error = 0;
			break;
		}

		if (owner == UMUTEX_RB_NOTRECOV) {
			error = ENOTRECOVERABLE;
			break;
		}

		/* If no one owns it but it is contested try to acquire it. */
		if (owner == UMUTEX_CONTESTED || owner == UMUTEX_RB_OWNERDEAD) {
			old_owner = owner;
			rv = casueword32(&m->m_owner, owner, &owner,
			    id | UMUTEX_CONTESTED);
			/* The address was invalid. */
			if (rv == -1) {
				error = EFAULT;
				break;
			}

			if (owner == old_owner) {
				umtxq_lock(&uq->uq_key);
				umtxq_busy(&uq->uq_key);
				error = umtx_pi_claim(pi, td);
				umtxq_unbusy(&uq->uq_key);
				umtxq_unlock(&uq->uq_key);
				if (error != 0) {
					/*
					 * Since we're going to return an
					 * error, restore the m_owner to its
					 * previous, unowned state to avoid
					 * compounding the problem.
					 */
					(void)casuword32(&m->m_owner,
					    id | UMUTEX_CONTESTED,
					    old_owner);
				}
				if (error == 0 &&
				    old_owner == UMUTEX_RB_OWNERDEAD)
					error = EOWNERDEAD;
				break;
			}

			error = umtxq_check_susp(td);
			if (error != 0)
				break;

			/* If this failed the lock has changed, restart. */
			continue;
		}

		if ((owner & ~UMUTEX_CONTESTED) == id) {
			error = EDEADLK;
			break;
		}

		if (try != 0) {
			error = EBUSY;
			break;
		}

		/*
		 * If we caught a signal, we have retried and now
		 * exit immediately.
		 */
		if (error != 0)
			break;
			
		umtxq_lock(&uq->uq_key);
		umtxq_busy(&uq->uq_key);
		umtxq_unlock(&uq->uq_key);

		/*
		 * Set the contested bit so that a release in user space
		 * knows to use the system call for unlock.  If this fails
		 * either some one else has acquired the lock or it has been
		 * released.
		 */
		rv = casueword32(&m->m_owner, owner, &old, owner |
		    UMUTEX_CONTESTED);

		/* The address was invalid. */
		if (rv == -1) {
			umtxq_unbusy_unlocked(&uq->uq_key);
			error = EFAULT;
			break;
		}

		umtxq_lock(&uq->uq_key);
		/*
		 * We set the contested bit, sleep. Otherwise the lock changed
		 * and we need to retry or we lost a race to the thread
		 * unlocking the umtx.  Note that the UMUTEX_RB_OWNERDEAD
		 * value for owner is impossible there.
		 */
		if (old == owner) {
			error = umtxq_sleep_pi(uq, pi,
			    owner & ~UMUTEX_CONTESTED,
			    "umtxpi", timeout == NULL ? NULL : &timo,
			    (flags & USYNC_PROCESS_SHARED) != 0);
			if (error != 0)
				continue;
		} else {
			umtxq_unbusy(&uq->uq_key);
			umtxq_unlock(&uq->uq_key);
		}

		error = umtxq_check_susp(td);
		if (error != 0)
			break;
	}

	umtxq_lock(&uq->uq_key);
	umtx_pi_unref(pi);
	umtxq_unlock(&uq->uq_key);

	umtx_key_release(&uq->uq_key);
	return (error);
}

/*
 * Unlock a PI mutex.
 */
static int
do_unlock_pi(struct thread *td, struct umutex *m, uint32_t flags, bool rb)
{
	struct umtx_key key;
	struct umtx_q *uq_first, *uq_first2, *uq_me;
	struct umtx_pi *pi, *pi2;
	uint32_t id, new_owner, old, owner;
	int count, error, pri;

	id = td->td_tid;
	/*
	 * Make sure we own this mtx.
	 */
	error = fueword32(&m->m_owner, &owner);
	if (error == -1)
		return (EFAULT);

	if ((owner & ~UMUTEX_CONTESTED) != id)
		return (EPERM);

	new_owner = umtx_unlock_val(flags, rb);

	/* This should be done in userland */
	if ((owner & UMUTEX_CONTESTED) == 0) {
		error = casueword32(&m->m_owner, owner, &old, new_owner);
		if (error == -1)
			return (EFAULT);
		if (old == owner)
			return (0);
		owner = old;
	}

	/* We should only ever be in here for contested locks */
	if ((error = umtx_key_get(m, (flags & UMUTEX_ROBUST) != 0 ?
	    TYPE_PI_ROBUST_UMUTEX : TYPE_PI_UMUTEX, GET_SHARE(flags),
	    &key)) != 0)
		return (error);

	umtxq_lock(&key);
	umtxq_busy(&key);
	count = umtxq_count_pi(&key, &uq_first);
	if (uq_first != NULL) {
		mtx_lock(&umtx_lock);
		pi = uq_first->uq_pi_blocked;
		KASSERT(pi != NULL, ("pi == NULL?"));
		if (pi->pi_owner != td && !(rb && pi->pi_owner == NULL)) {
			mtx_unlock(&umtx_lock);
			umtxq_unbusy(&key);
			umtxq_unlock(&key);
			umtx_key_release(&key);
			/* userland messed the mutex */
			return (EPERM);
		}
		uq_me = td->td_umtxq;
		if (pi->pi_owner == td)
			umtx_pi_disown(pi);
		/* get highest priority thread which is still sleeping. */
		uq_first = TAILQ_FIRST(&pi->pi_blocked);
		while (uq_first != NULL && 
		    (uq_first->uq_flags & UQF_UMTXQ) == 0) {
			uq_first = TAILQ_NEXT(uq_first, uq_lockq);
		}
		pri = PRI_MAX;
		TAILQ_FOREACH(pi2, &uq_me->uq_pi_contested, pi_link) {
			uq_first2 = TAILQ_FIRST(&pi2->pi_blocked);
			if (uq_first2 != NULL) {
				if (pri > UPRI(uq_first2->uq_thread))
					pri = UPRI(uq_first2->uq_thread);
			}
		}
		thread_lock(td);
		sched_lend_user_prio(td, pri);
		thread_unlock(td);
		mtx_unlock(&umtx_lock);
		if (uq_first)
			umtxq_signal_thread(uq_first);
	} else {
		pi = umtx_pi_lookup(&key);
		/*
		 * A umtx_pi can exist if a signal or timeout removed the
		 * last waiter from the umtxq, but there is still
		 * a thread in do_lock_pi() holding the umtx_pi.
		 */
		if (pi != NULL) {
			/*
			 * The umtx_pi can be unowned, such as when a thread
			 * has just entered do_lock_pi(), allocated the
			 * umtx_pi, and unlocked the umtxq.
			 * If the current thread owns it, it must disown it.
			 */
			mtx_lock(&umtx_lock);
			if (pi->pi_owner == td)
				umtx_pi_disown(pi);
			mtx_unlock(&umtx_lock);
		}
	}
	umtxq_unlock(&key);

	/*
	 * When unlocking the umtx, it must be marked as unowned if
	 * there is zero or one thread only waiting for it.
	 * Otherwise, it must be marked as contested.
	 */

	if (count > 1)
		new_owner |= UMUTEX_CONTESTED;
	error = casueword32(&m->m_owner, owner, &old, new_owner);

	umtxq_unbusy_unlocked(&key);
	umtx_key_release(&key);
	if (error == -1)
		return (EFAULT);
	if (old != owner)
		return (EINVAL);
	return (0);
}

/*
 * Lock a PP mutex.
 */
static int
do_lock_pp(struct thread *td, struct umutex *m, uint32_t flags,
    struct _umtx_time *timeout, int try)
{
	struct abs_timeout timo;
	struct umtx_q *uq, *uq2;
	struct umtx_pi *pi;
	uint32_t ceiling;
	uint32_t owner, id;
	int error, pri, old_inherited_pri, su, rv;

	id = td->td_tid;
	uq = td->td_umtxq;
	if ((error = umtx_key_get(m, (flags & UMUTEX_ROBUST) != 0 ?
	    TYPE_PP_ROBUST_UMUTEX : TYPE_PP_UMUTEX, GET_SHARE(flags),
	    &uq->uq_key)) != 0)
		return (error);

	if (timeout != NULL)
		abs_timeout_init2(&timo, timeout);

	su = (priv_check(td, PRIV_SCHED_RTPRIO) == 0);
	for (;;) {
		old_inherited_pri = uq->uq_inherited_pri;
		umtxq_lock(&uq->uq_key);
		umtxq_busy(&uq->uq_key);
		umtxq_unlock(&uq->uq_key);

		rv = fueword32(&m->m_ceilings[0], &ceiling);
		if (rv == -1) {
			error = EFAULT;
			goto out;
		}
		ceiling = RTP_PRIO_MAX - ceiling;
		if (ceiling > RTP_PRIO_MAX) {
			error = EINVAL;
			goto out;
		}

		mtx_lock(&umtx_lock);
		if (UPRI(td) < PRI_MIN_REALTIME + ceiling) {
			mtx_unlock(&umtx_lock);
			error = EINVAL;
			goto out;
		}
		if (su && PRI_MIN_REALTIME + ceiling < uq->uq_inherited_pri) {
			uq->uq_inherited_pri = PRI_MIN_REALTIME + ceiling;
			thread_lock(td);
			if (uq->uq_inherited_pri < UPRI(td))
				sched_lend_user_prio(td, uq->uq_inherited_pri);
			thread_unlock(td);
		}
		mtx_unlock(&umtx_lock);

		rv = casueword32(&m->m_owner, UMUTEX_CONTESTED, &owner,
		    id | UMUTEX_CONTESTED);
		/* The address was invalid. */
		if (rv == -1) {
			error = EFAULT;
			break;
		}

		if (owner == UMUTEX_CONTESTED) {
			error = 0;
			break;
		} else if (owner == UMUTEX_RB_OWNERDEAD) {
			rv = casueword32(&m->m_owner, UMUTEX_RB_OWNERDEAD,
			    &owner, id | UMUTEX_CONTESTED);
			if (rv == -1) {
				error = EFAULT;
				break;
			}
			if (owner == UMUTEX_RB_OWNERDEAD) {
				error = EOWNERDEAD; /* success */
				break;
			}
			error = 0;
		} else if (owner == UMUTEX_RB_NOTRECOV) {
			error = ENOTRECOVERABLE;
			break;
		}

		if (try != 0) {
			error = EBUSY;
			break;
		}

		/*
		 * If we caught a signal, we have retried and now
		 * exit immediately.
		 */
		if (error != 0)
			break;

		umtxq_lock(&uq->uq_key);
		umtxq_insert(uq);
		umtxq_unbusy(&uq->uq_key);
		error = umtxq_sleep(uq, "umtxpp", timeout == NULL ?
		    NULL : &timo);
		umtxq_remove(uq);
		umtxq_unlock(&uq->uq_key);

		mtx_lock(&umtx_lock);
		uq->uq_inherited_pri = old_inherited_pri;
		pri = PRI_MAX;
		TAILQ_FOREACH(pi, &uq->uq_pi_contested, pi_link) {
			uq2 = TAILQ_FIRST(&pi->pi_blocked);
			if (uq2 != NULL) {
				if (pri > UPRI(uq2->uq_thread))
					pri = UPRI(uq2->uq_thread);
			}
		}
		if (pri > uq->uq_inherited_pri)
			pri = uq->uq_inherited_pri;
		thread_lock(td);
		sched_lend_user_prio(td, pri);
		thread_unlock(td);
		mtx_unlock(&umtx_lock);
	}

	if (error != 0 && error != EOWNERDEAD) {
		mtx_lock(&umtx_lock);
		uq->uq_inherited_pri = old_inherited_pri;
		pri = PRI_MAX;
		TAILQ_FOREACH(pi, &uq->uq_pi_contested, pi_link) {
			uq2 = TAILQ_FIRST(&pi->pi_blocked);
			if (uq2 != NULL) {
				if (pri > UPRI(uq2->uq_thread))
					pri = UPRI(uq2->uq_thread);
			}
		}
		if (pri > uq->uq_inherited_pri)
			pri = uq->uq_inherited_pri;
		thread_lock(td);
		sched_lend_user_prio(td, pri);
		thread_unlock(td);
		mtx_unlock(&umtx_lock);
	}

out:
	umtxq_unbusy_unlocked(&uq->uq_key);
	umtx_key_release(&uq->uq_key);
	return (error);
}

/*
 * Unlock a PP mutex.
 */
static int
do_unlock_pp(struct thread *td, struct umutex *m, uint32_t flags, bool rb)
{
	struct umtx_key key;
	struct umtx_q *uq, *uq2;
	struct umtx_pi *pi;
	uint32_t id, owner, rceiling;
	int error, pri, new_inherited_pri, su;

	id = td->td_tid;
	uq = td->td_umtxq;
	su = (priv_check(td, PRIV_SCHED_RTPRIO) == 0);

	/*
	 * Make sure we own this mtx.
	 */
	error = fueword32(&m->m_owner, &owner);
	if (error == -1)
		return (EFAULT);

	if ((owner & ~UMUTEX_CONTESTED) != id)
		return (EPERM);

	error = copyin(&m->m_ceilings[1], &rceiling, sizeof(uint32_t));
	if (error != 0)
		return (error);

	if (rceiling == -1)
		new_inherited_pri = PRI_MAX;
	else {
		rceiling = RTP_PRIO_MAX - rceiling;
		if (rceiling > RTP_PRIO_MAX)
			return (EINVAL);
		new_inherited_pri = PRI_MIN_REALTIME + rceiling;
	}

	if ((error = umtx_key_get(m, (flags & UMUTEX_ROBUST) != 0 ?
	    TYPE_PP_ROBUST_UMUTEX : TYPE_PP_UMUTEX, GET_SHARE(flags),
	    &key)) != 0)
		return (error);
	umtxq_lock(&key);
	umtxq_busy(&key);
	umtxq_unlock(&key);
	/*
	 * For priority protected mutex, always set unlocked state
	 * to UMUTEX_CONTESTED, so that userland always enters kernel
	 * to lock the mutex, it is necessary because thread priority
	 * has to be adjusted for such mutex.
	 */
	error = suword32(&m->m_owner, umtx_unlock_val(flags, rb) |
	    UMUTEX_CONTESTED);

	umtxq_lock(&key);
	if (error == 0)
		umtxq_signal(&key, 1);
	umtxq_unbusy(&key);
	umtxq_unlock(&key);

	if (error == -1)
		error = EFAULT;
	else {
		mtx_lock(&umtx_lock);
		if (su != 0)
			uq->uq_inherited_pri = new_inherited_pri;
		pri = PRI_MAX;
		TAILQ_FOREACH(pi, &uq->uq_pi_contested, pi_link) {
			uq2 = TAILQ_FIRST(&pi->pi_blocked);
			if (uq2 != NULL) {
				if (pri > UPRI(uq2->uq_thread))
					pri = UPRI(uq2->uq_thread);
			}
		}
		if (pri > uq->uq_inherited_pri)
			pri = uq->uq_inherited_pri;
		thread_lock(td);
		sched_lend_user_prio(td, pri);
		thread_unlock(td);
		mtx_unlock(&umtx_lock);
	}
	umtx_key_release(&key);
	return (error);
}

static int
do_set_ceiling(struct thread *td, struct umutex *m, uint32_t ceiling,
    uint32_t *old_ceiling)
{
	struct umtx_q *uq;
	uint32_t flags, id, owner, save_ceiling;
	int error, rv, rv1;

	error = fueword32(&m->m_flags, &flags);
	if (error == -1)
		return (EFAULT);
	if ((flags & UMUTEX_PRIO_PROTECT) == 0)
		return (EINVAL);
	if (ceiling > RTP_PRIO_MAX)
		return (EINVAL);
	id = td->td_tid;
	uq = td->td_umtxq;
	if ((error = umtx_key_get(m, (flags & UMUTEX_ROBUST) != 0 ?
	    TYPE_PP_ROBUST_UMUTEX : TYPE_PP_UMUTEX, GET_SHARE(flags),
	    &uq->uq_key)) != 0)
		return (error);
	for (;;) {
		umtxq_lock(&uq->uq_key);
		umtxq_busy(&uq->uq_key);
		umtxq_unlock(&uq->uq_key);

		rv = fueword32(&m->m_ceilings[0], &save_ceiling);
		if (rv == -1) {
			error = EFAULT;
			break;
		}

		rv = casueword32(&m->m_owner, UMUTEX_CONTESTED, &owner,
		    id | UMUTEX_CONTESTED);
		if (rv == -1) {
			error = EFAULT;
			break;
		}

		if (owner == UMUTEX_CONTESTED) {
			rv = suword32(&m->m_ceilings[0], ceiling);
			rv1 = suword32(&m->m_owner, UMUTEX_CONTESTED);
			error = (rv == 0 && rv1 == 0) ? 0: EFAULT;
			break;
		}

		if ((owner & ~UMUTEX_CONTESTED) == id) {
			rv = suword32(&m->m_ceilings[0], ceiling);
			error = rv == 0 ? 0 : EFAULT;
			break;
		}

		if (owner == UMUTEX_RB_OWNERDEAD) {
			error = EOWNERDEAD;
			break;
		} else if (owner == UMUTEX_RB_NOTRECOV) {
			error = ENOTRECOVERABLE;
			break;
		}

		/*
		 * If we caught a signal, we have retried and now
		 * exit immediately.
		 */
		if (error != 0)
			break;

		/*
		 * We set the contested bit, sleep. Otherwise the lock changed
		 * and we need to retry or we lost a race to the thread
		 * unlocking the umtx.
		 */
		umtxq_lock(&uq->uq_key);
		umtxq_insert(uq);
		umtxq_unbusy(&uq->uq_key);
		error = umtxq_sleep(uq, "umtxpp", NULL);
		umtxq_remove(uq);
		umtxq_unlock(&uq->uq_key);
	}
	umtxq_lock(&uq->uq_key);
	if (error == 0)
		umtxq_signal(&uq->uq_key, INT_MAX);
	umtxq_unbusy(&uq->uq_key);
	umtxq_unlock(&uq->uq_key);
	umtx_key_release(&uq->uq_key);
	if (error == 0 && old_ceiling != NULL) {
		rv = suword32(old_ceiling, save_ceiling);
		error = rv == 0 ? 0 : EFAULT;
	}
	return (error);
}

/*
 * Lock a userland POSIX mutex.
 */
static int
do_lock_umutex(struct thread *td, struct umutex *m,
    struct _umtx_time *timeout, int mode)
{
	uint32_t flags;
	int error;

	error = fueword32(&m->m_flags, &flags);
	if (error == -1)
		return (EFAULT);

	switch (flags & (UMUTEX_PRIO_INHERIT | UMUTEX_PRIO_PROTECT)) {
	case 0:
		error = do_lock_normal(td, m, flags, timeout, mode);
		break;
	case UMUTEX_PRIO_INHERIT:
		error = do_lock_pi(td, m, flags, timeout, mode);
		break;
	case UMUTEX_PRIO_PROTECT:
		error = do_lock_pp(td, m, flags, timeout, mode);
		break;
	default:
		return (EINVAL);
	}
	if (timeout == NULL) {
		if (error == EINTR && mode != _UMUTEX_WAIT)
			error = ERESTART;
	} else {
		/* Timed-locking is not restarted. */
		if (error == ERESTART)
			error = EINTR;
	}
	return (error);
}

/*
 * Unlock a userland POSIX mutex.
 */
static int
do_unlock_umutex(struct thread *td, struct umutex *m, bool rb)
{
	uint32_t flags;
	int error;

	error = fueword32(&m->m_flags, &flags);
	if (error == -1)
		return (EFAULT);

	switch (flags & (UMUTEX_PRIO_INHERIT | UMUTEX_PRIO_PROTECT)) {
	case 0:
		return (do_unlock_normal(td, m, flags, rb));
	case UMUTEX_PRIO_INHERIT:
		return (do_unlock_pi(td, m, flags, rb));
	case UMUTEX_PRIO_PROTECT:
		return (do_unlock_pp(td, m, flags, rb));
	}

	return (EINVAL);
}

static int
do_cv_wait(struct thread *td, struct ucond *cv, struct umutex *m,
    struct timespec *timeout, u_long wflags)
{
	struct abs_timeout timo;
	struct umtx_q *uq;
	uint32_t flags, clockid, hasw;
	int error;

	uq = td->td_umtxq;
	error = fueword32(&cv->c_flags, &flags);
	if (error == -1)
		return (EFAULT);
	error = umtx_key_get(cv, TYPE_CV, GET_SHARE(flags), &uq->uq_key);
	if (error != 0)
		return (error);

	if ((wflags & CVWAIT_CLOCKID) != 0) {
		error = fueword32(&cv->c_clockid, &clockid);
		if (error == -1) {
			umtx_key_release(&uq->uq_key);
			return (EFAULT);
		}
		if (clockid < CLOCK_REALTIME ||
		    clockid >= CLOCK_THREAD_CPUTIME_ID) {
			/* hmm, only HW clock id will work. */
			umtx_key_release(&uq->uq_key);
			return (EINVAL);
		}
	} else {
		clockid = CLOCK_REALTIME;
	}

	umtxq_lock(&uq->uq_key);
	umtxq_busy(&uq->uq_key);
	umtxq_insert(uq);
	umtxq_unlock(&uq->uq_key);

	/*
	 * Set c_has_waiters to 1 before releasing user mutex, also
	 * don't modify cache line when unnecessary.
	 */
	error = fueword32(&cv->c_has_waiters, &hasw);
	if (error == 0 && hasw == 0)
		suword32(&cv->c_has_waiters, 1);

	umtxq_unbusy_unlocked(&uq->uq_key);

	error = do_unlock_umutex(td, m, false);

	if (timeout != NULL)
		abs_timeout_init(&timo, clockid, (wflags & CVWAIT_ABSTIME) != 0,
		    timeout);
	
	umtxq_lock(&uq->uq_key);
	if (error == 0) {
		error = umtxq_sleep(uq, "ucond", timeout == NULL ?
		    NULL : &timo);
	}

	if ((uq->uq_flags & UQF_UMTXQ) == 0)
		error = 0;
	else {
		/*
		 * This must be timeout,interrupted by signal or
		 * surprious wakeup, clear c_has_waiter flag when
		 * necessary.
		 */
		umtxq_busy(&uq->uq_key);
		if ((uq->uq_flags & UQF_UMTXQ) != 0) {
			int oldlen = uq->uq_cur_queue->length;
			umtxq_remove(uq);
			if (oldlen == 1) {
				umtxq_unlock(&uq->uq_key);
				suword32(&cv->c_has_waiters, 0);
				umtxq_lock(&uq->uq_key);
			}
		}
		umtxq_unbusy(&uq->uq_key);
		if (error == ERESTART)
			error = EINTR;
	}

	umtxq_unlock(&uq->uq_key);
	umtx_key_release(&uq->uq_key);
	return (error);
}

/*
 * Signal a userland condition variable.
 */
static int
do_cv_signal(struct thread *td, struct ucond *cv)
{
	struct umtx_key key;
	int error, cnt, nwake;
	uint32_t flags;

	error = fueword32(&cv->c_flags, &flags);
	if (error == -1)
		return (EFAULT);
	if ((error = umtx_key_get(cv, TYPE_CV, GET_SHARE(flags), &key)) != 0)
		return (error);	
	umtxq_lock(&key);
	umtxq_busy(&key);
	cnt = umtxq_count(&key);
	nwake = umtxq_signal(&key, 1);
	if (cnt <= nwake) {
		umtxq_unlock(&key);
		error = suword32(&cv->c_has_waiters, 0);
		if (error == -1)
			error = EFAULT;
		umtxq_lock(&key);
	}
	umtxq_unbusy(&key);
	umtxq_unlock(&key);
	umtx_key_release(&key);
	return (error);
}

static int
do_cv_broadcast(struct thread *td, struct ucond *cv)
{
	struct umtx_key key;
	int error;
	uint32_t flags;

	error = fueword32(&cv->c_flags, &flags);
	if (error == -1)
		return (EFAULT);
	if ((error = umtx_key_get(cv, TYPE_CV, GET_SHARE(flags), &key)) != 0)
		return (error);	

	umtxq_lock(&key);
	umtxq_busy(&key);
	umtxq_signal(&key, INT_MAX);
	umtxq_unlock(&key);

	error = suword32(&cv->c_has_waiters, 0);
	if (error == -1)
		error = EFAULT;

	umtxq_unbusy_unlocked(&key);

	umtx_key_release(&key);
	return (error);
}

static int
do_rw_rdlock(struct thread *td, struct urwlock *rwlock, long fflag, struct _umtx_time *timeout)
{
	struct abs_timeout timo;
	struct umtx_q *uq;
	uint32_t flags, wrflags;
	int32_t state, oldstate;
	int32_t blocked_readers;
	int error, error1, rv;

	uq = td->td_umtxq;
	error = fueword32(&rwlock->rw_flags, &flags);
	if (error == -1)
		return (EFAULT);
	error = umtx_key_get(rwlock, TYPE_RWLOCK, GET_SHARE(flags), &uq->uq_key);
	if (error != 0)
		return (error);

	if (timeout != NULL)
		abs_timeout_init2(&timo, timeout);

	wrflags = URWLOCK_WRITE_OWNER;
	if (!(fflag & URWLOCK_PREFER_READER) && !(flags & URWLOCK_PREFER_READER))
		wrflags |= URWLOCK_WRITE_WAITERS;

	for (;;) {
		rv = fueword32(&rwlock->rw_state, &state);
		if (rv == -1) {
			umtx_key_release(&uq->uq_key);
			return (EFAULT);
		}

		/* try to lock it */
		while (!(state & wrflags)) {
			if (__predict_false(URWLOCK_READER_COUNT(state) == URWLOCK_MAX_READERS)) {
				umtx_key_release(&uq->uq_key);
				return (EAGAIN);
			}
			rv = casueword32(&rwlock->rw_state, state,
			    &oldstate, state + 1);
			if (rv == -1) {
				umtx_key_release(&uq->uq_key);
				return (EFAULT);
			}
			if (oldstate == state) {
				umtx_key_release(&uq->uq_key);
				return (0);
			}
			error = umtxq_check_susp(td);
			if (error != 0)
				break;
			state = oldstate;
		}

		if (error)
			break;

		/* grab monitor lock */
		umtxq_lock(&uq->uq_key);
		umtxq_busy(&uq->uq_key);
		umtxq_unlock(&uq->uq_key);

		/*
		 * re-read the state, in case it changed between the try-lock above
		 * and the check below
		 */
		rv = fueword32(&rwlock->rw_state, &state);
		if (rv == -1)
			error = EFAULT;

		/* set read contention bit */
		while (error == 0 && (state & wrflags) &&
		    !(state & URWLOCK_READ_WAITERS)) {
			rv = casueword32(&rwlock->rw_state, state,
			    &oldstate, state | URWLOCK_READ_WAITERS);
			if (rv == -1) {
				error = EFAULT;
				break;
			}
			if (oldstate == state)
				goto sleep;
			state = oldstate;
			error = umtxq_check_susp(td);
			if (error != 0)
				break;
		}
		if (error != 0) {
			umtxq_unbusy_unlocked(&uq->uq_key);
			break;
		}

		/* state is changed while setting flags, restart */
		if (!(state & wrflags)) {
			umtxq_unbusy_unlocked(&uq->uq_key);
			error = umtxq_check_susp(td);
			if (error != 0)
				break;
			continue;
		}

sleep:
		/* contention bit is set, before sleeping, increase read waiter count */
		rv = fueword32(&rwlock->rw_blocked_readers,
		    &blocked_readers);
		if (rv == -1) {
			umtxq_unbusy_unlocked(&uq->uq_key);
			error = EFAULT;
			break;
		}
		suword32(&rwlock->rw_blocked_readers, blocked_readers+1);

		while (state & wrflags) {
			umtxq_lock(&uq->uq_key);
			umtxq_insert(uq);
			umtxq_unbusy(&uq->uq_key);

			error = umtxq_sleep(uq, "urdlck", timeout == NULL ?
			    NULL : &timo);

			umtxq_busy(&uq->uq_key);
			umtxq_remove(uq);
			umtxq_unlock(&uq->uq_key);
			if (error)
				break;
			rv = fueword32(&rwlock->rw_state, &state);
			if (rv == -1) {
				error = EFAULT;
				break;
			}
		}

		/* decrease read waiter count, and may clear read contention bit */
		rv = fueword32(&rwlock->rw_blocked_readers,
		    &blocked_readers);
		if (rv == -1) {
			umtxq_unbusy_unlocked(&uq->uq_key);
			error = EFAULT;
			break;
		}
		suword32(&rwlock->rw_blocked_readers, blocked_readers-1);
		if (blocked_readers == 1) {
			rv = fueword32(&rwlock->rw_state, &state);
			if (rv == -1) {
				umtxq_unbusy_unlocked(&uq->uq_key);
				error = EFAULT;
				break;
			}
			for (;;) {
				rv = casueword32(&rwlock->rw_state, state,
				    &oldstate, state & ~URWLOCK_READ_WAITERS);
				if (rv == -1) {
					error = EFAULT;
					break;
				}
				if (oldstate == state)
					break;
				state = oldstate;
				error1 = umtxq_check_susp(td);
				if (error1 != 0) {
					if (error == 0)
						error = error1;
					break;
				}
			}
		}

		umtxq_unbusy_unlocked(&uq->uq_key);
		if (error != 0)
			break;
	}
	umtx_key_release(&uq->uq_key);
	if (error == ERESTART)
		error = EINTR;
	return (error);
}

static int
do_rw_wrlock(struct thread *td, struct urwlock *rwlock, struct _umtx_time *timeout)
{
	struct abs_timeout timo;
	struct umtx_q *uq;
	uint32_t flags;
	int32_t state, oldstate;
	int32_t blocked_writers;
	int32_t blocked_readers;
	int error, error1, rv;

	uq = td->td_umtxq;
	error = fueword32(&rwlock->rw_flags, &flags);
	if (error == -1)
		return (EFAULT);
	error = umtx_key_get(rwlock, TYPE_RWLOCK, GET_SHARE(flags), &uq->uq_key);
	if (error != 0)
		return (error);

	if (timeout != NULL)
		abs_timeout_init2(&timo, timeout);

	blocked_readers = 0;
	for (;;) {
		rv = fueword32(&rwlock->rw_state, &state);
		if (rv == -1) {
			umtx_key_release(&uq->uq_key);
			return (EFAULT);
		}
		while (!(state & URWLOCK_WRITE_OWNER) && URWLOCK_READER_COUNT(state) == 0) {
			rv = casueword32(&rwlock->rw_state, state,
			    &oldstate, state | URWLOCK_WRITE_OWNER);
			if (rv == -1) {
				umtx_key_release(&uq->uq_key);
				return (EFAULT);
			}
			if (oldstate == state) {
				umtx_key_release(&uq->uq_key);
				return (0);
			}
			state = oldstate;
			error = umtxq_check_susp(td);
			if (error != 0)
				break;
		}

		if (error) {
			if (!(state & (URWLOCK_WRITE_OWNER|URWLOCK_WRITE_WAITERS)) &&
			    blocked_readers != 0) {
				umtxq_lock(&uq->uq_key);
				umtxq_busy(&uq->uq_key);
				umtxq_signal_queue(&uq->uq_key, INT_MAX, UMTX_SHARED_QUEUE);
				umtxq_unbusy(&uq->uq_key);
				umtxq_unlock(&uq->uq_key);
			}

			break;
		}

		/* grab monitor lock */
		umtxq_lock(&uq->uq_key);
		umtxq_busy(&uq->uq_key);
		umtxq_unlock(&uq->uq_key);

		/*
		 * re-read the state, in case it changed between the try-lock above
		 * and the check below
		 */
		rv = fueword32(&rwlock->rw_state, &state);
		if (rv == -1)
			error = EFAULT;

		while (error == 0 && ((state & URWLOCK_WRITE_OWNER) ||
		    URWLOCK_READER_COUNT(state) != 0) &&
		    (state & URWLOCK_WRITE_WAITERS) == 0) {
			rv = casueword32(&rwlock->rw_state, state,
			    &oldstate, state | URWLOCK_WRITE_WAITERS);
			if (rv == -1) {
				error = EFAULT;
				break;
			}
			if (oldstate == state)
				goto sleep;
			state = oldstate;
			error = umtxq_check_susp(td);
			if (error != 0)
				break;
		}
		if (error != 0) {
			umtxq_unbusy_unlocked(&uq->uq_key);
			break;
		}

		if (!(state & URWLOCK_WRITE_OWNER) && URWLOCK_READER_COUNT(state) == 0) {
			umtxq_unbusy_unlocked(&uq->uq_key);
			error = umtxq_check_susp(td);
			if (error != 0)
				break;
			continue;
		}
sleep:
		rv = fueword32(&rwlock->rw_blocked_writers,
		    &blocked_writers);
		if (rv == -1) {
			umtxq_unbusy_unlocked(&uq->uq_key);
			error = EFAULT;
			break;
		}
		suword32(&rwlock->rw_blocked_writers, blocked_writers+1);

		while ((state & URWLOCK_WRITE_OWNER) || URWLOCK_READER_COUNT(state) != 0) {
			umtxq_lock(&uq->uq_key);
			umtxq_insert_queue(uq, UMTX_EXCLUSIVE_QUEUE);
			umtxq_unbusy(&uq->uq_key);

			error = umtxq_sleep(uq, "uwrlck", timeout == NULL ?
			    NULL : &timo);

			umtxq_busy(&uq->uq_key);
			umtxq_remove_queue(uq, UMTX_EXCLUSIVE_QUEUE);
			umtxq_unlock(&uq->uq_key);
			if (error)
				break;
			rv = fueword32(&rwlock->rw_state, &state);
			if (rv == -1) {
				error = EFAULT;
				break;
			}
		}

		rv = fueword32(&rwlock->rw_blocked_writers,
		    &blocked_writers);
		if (rv == -1) {
			umtxq_unbusy_unlocked(&uq->uq_key);
			error = EFAULT;
			break;
		}
		suword32(&rwlock->rw_blocked_writers, blocked_writers-1);
		if (blocked_writers == 1) {
			rv = fueword32(&rwlock->rw_state, &state);
			if (rv == -1) {
				umtxq_unbusy_unlocked(&uq->uq_key);
				error = EFAULT;
				break;
			}
			for (;;) {
				rv = casueword32(&rwlock->rw_state, state,
				    &oldstate, state & ~URWLOCK_WRITE_WAITERS);
				if (rv == -1) {
					error = EFAULT;
					break;
				}
				if (oldstate == state)
					break;
				state = oldstate;
				error1 = umtxq_check_susp(td);
				/*
				 * We are leaving the URWLOCK_WRITE_WAITERS
				 * behind, but this should not harm the
				 * correctness.
				 */
				if (error1 != 0) {
					if (error == 0)
						error = error1;
					break;
				}
			}
			rv = fueword32(&rwlock->rw_blocked_readers,
			    &blocked_readers);
			if (rv == -1) {
				umtxq_unbusy_unlocked(&uq->uq_key);
				error = EFAULT;
				break;
			}
		} else
			blocked_readers = 0;

		umtxq_unbusy_unlocked(&uq->uq_key);
	}

	umtx_key_release(&uq->uq_key);
	if (error == ERESTART)
		error = EINTR;
	return (error);
}

static int
do_rw_unlock(struct thread *td, struct urwlock *rwlock)
{
	struct umtx_q *uq;
	uint32_t flags;
	int32_t state, oldstate;
	int error, rv, q, count;

	uq = td->td_umtxq;
	error = fueword32(&rwlock->rw_flags, &flags);
	if (error == -1)
		return (EFAULT);
	error = umtx_key_get(rwlock, TYPE_RWLOCK, GET_SHARE(flags), &uq->uq_key);
	if (error != 0)
		return (error);

	error = fueword32(&rwlock->rw_state, &state);
	if (error == -1) {
		error = EFAULT;
		goto out;
	}
	if (state & URWLOCK_WRITE_OWNER) {
		for (;;) {
			rv = casueword32(&rwlock->rw_state, state, 
			    &oldstate, state & ~URWLOCK_WRITE_OWNER);
			if (rv == -1) {
				error = EFAULT;
				goto out;
			}
			if (oldstate != state) {
				state = oldstate;
				if (!(oldstate & URWLOCK_WRITE_OWNER)) {
					error = EPERM;
					goto out;
				}
				error = umtxq_check_susp(td);
				if (error != 0)
					goto out;
			} else
				break;
		}
	} else if (URWLOCK_READER_COUNT(state) != 0) {
		for (;;) {
			rv = casueword32(&rwlock->rw_state, state,
			    &oldstate, state - 1);
			if (rv == -1) {
				error = EFAULT;
				goto out;
			}
			if (oldstate != state) {
				state = oldstate;
				if (URWLOCK_READER_COUNT(oldstate) == 0) {
					error = EPERM;
					goto out;
				}
				error = umtxq_check_susp(td);
				if (error != 0)
					goto out;
			} else
				break;
		}
	} else {
		error = EPERM;
		goto out;
	}

	count = 0;

	if (!(flags & URWLOCK_PREFER_READER)) {
		if (state & URWLOCK_WRITE_WAITERS) {
			count = 1;
			q = UMTX_EXCLUSIVE_QUEUE;
		} else if (state & URWLOCK_READ_WAITERS) {
			count = INT_MAX;
			q = UMTX_SHARED_QUEUE;
		}
	} else {
		if (state & URWLOCK_READ_WAITERS) {
			count = INT_MAX;
			q = UMTX_SHARED_QUEUE;
		} else if (state & URWLOCK_WRITE_WAITERS) {
			count = 1;
			q = UMTX_EXCLUSIVE_QUEUE;
		}
	}

	if (count) {
		umtxq_lock(&uq->uq_key);
		umtxq_busy(&uq->uq_key);
		umtxq_signal_queue(&uq->uq_key, count, q);
		umtxq_unbusy(&uq->uq_key);
		umtxq_unlock(&uq->uq_key);
	}
out:
	umtx_key_release(&uq->uq_key);
	return (error);
}

#if defined(COMPAT_FREEBSD9) || defined(COMPAT_FREEBSD10)
static int
do_sem_wait(struct thread *td, struct _usem *sem, struct _umtx_time *timeout)
{
	struct abs_timeout timo;
	struct umtx_q *uq;
	uint32_t flags, count, count1;
	int error, rv;

	uq = td->td_umtxq;
	error = fueword32(&sem->_flags, &flags);
	if (error == -1)
		return (EFAULT);
	error = umtx_key_get(sem, TYPE_SEM, GET_SHARE(flags), &uq->uq_key);
	if (error != 0)
		return (error);

	if (timeout != NULL)
		abs_timeout_init2(&timo, timeout);

	umtxq_lock(&uq->uq_key);
	umtxq_busy(&uq->uq_key);
	umtxq_insert(uq);
	umtxq_unlock(&uq->uq_key);
	rv = casueword32(&sem->_has_waiters, 0, &count1, 1);
	if (rv == 0)
		rv = fueword32(&sem->_count, &count);
	if (rv == -1 || count != 0) {
		umtxq_lock(&uq->uq_key);
		umtxq_unbusy(&uq->uq_key);
		umtxq_remove(uq);
		umtxq_unlock(&uq->uq_key);
		umtx_key_release(&uq->uq_key);
		return (rv == -1 ? EFAULT : 0);
	}
	umtxq_lock(&uq->uq_key);
	umtxq_unbusy(&uq->uq_key);

	error = umtxq_sleep(uq, "usem", timeout == NULL ? NULL : &timo);

	if ((uq->uq_flags & UQF_UMTXQ) == 0)
		error = 0;
	else {
		umtxq_remove(uq);
		/* A relative timeout cannot be restarted. */
		if (error == ERESTART && timeout != NULL &&
		    (timeout->_flags & UMTX_ABSTIME) == 0)
			error = EINTR;
	}
	umtxq_unlock(&uq->uq_key);
	umtx_key_release(&uq->uq_key);
	return (error);
}

/*
 * Signal a userland semaphore.
 */
static int
do_sem_wake(struct thread *td, struct _usem *sem)
{
	struct umtx_key key;
	int error, cnt;
	uint32_t flags;

	error = fueword32(&sem->_flags, &flags);
	if (error == -1)
		return (EFAULT);
	if ((error = umtx_key_get(sem, TYPE_SEM, GET_SHARE(flags), &key)) != 0)
		return (error);	
	umtxq_lock(&key);
	umtxq_busy(&key);
	cnt = umtxq_count(&key);
	if (cnt > 0) {
		/*
		 * Check if count is greater than 0, this means the memory is
		 * still being referenced by user code, so we can safely
		 * update _has_waiters flag.
		 */
		if (cnt == 1) {
			umtxq_unlock(&key);
			error = suword32(&sem->_has_waiters, 0);
			umtxq_lock(&key);
			if (error == -1)
				error = EFAULT;
		}
		umtxq_signal(&key, 1);
	}
	umtxq_unbusy(&key);
	umtxq_unlock(&key);
	umtx_key_release(&key);
	return (error);
}
#endif

static int
do_sem2_wait(struct thread *td, struct _usem2 *sem, struct _umtx_time *timeout)
{
	struct abs_timeout timo;
	struct umtx_q *uq;
	uint32_t count, flags;
	int error, rv;

	uq = td->td_umtxq;
	flags = fuword32(&sem->_flags);
	error = umtx_key_get(sem, TYPE_SEM, GET_SHARE(flags), &uq->uq_key);
	if (error != 0)
		return (error);

	if (timeout != NULL)
		abs_timeout_init2(&timo, timeout);

	umtxq_lock(&uq->uq_key);
	umtxq_busy(&uq->uq_key);
	umtxq_insert(uq);
	umtxq_unlock(&uq->uq_key);
	rv = fueword32(&sem->_count, &count);
	if (rv == -1) {
		umtxq_lock(&uq->uq_key);
		umtxq_unbusy(&uq->uq_key);
		umtxq_remove(uq);
		umtxq_unlock(&uq->uq_key);
		umtx_key_release(&uq->uq_key);
		return (EFAULT);
	}
	for (;;) {
		if (USEM_COUNT(count) != 0) {
			umtxq_lock(&uq->uq_key);
			umtxq_unbusy(&uq->uq_key);
			umtxq_remove(uq);
			umtxq_unlock(&uq->uq_key);
			umtx_key_release(&uq->uq_key);
			return (0);
		}
		if (count == USEM_HAS_WAITERS)
			break;
		rv = casueword32(&sem->_count, 0, &count, USEM_HAS_WAITERS);
		if (rv == -1) {
			umtxq_lock(&uq->uq_key);
			umtxq_unbusy(&uq->uq_key);
			umtxq_remove(uq);
			umtxq_unlock(&uq->uq_key);
			umtx_key_release(&uq->uq_key);
			return (EFAULT);
		}
		if (count == 0)
			break;
	}
	umtxq_lock(&uq->uq_key);
	umtxq_unbusy(&uq->uq_key);

	error = umtxq_sleep(uq, "usem", timeout == NULL ? NULL : &timo);

	if ((uq->uq_flags & UQF_UMTXQ) == 0)
		error = 0;
	else {
		umtxq_remove(uq);
		if (timeout != NULL && (timeout->_flags & UMTX_ABSTIME) == 0) {
			/* A relative timeout cannot be restarted. */
			if (error == ERESTART)
				error = EINTR;
			if (error == EINTR) {
				abs_timeout_update(&timo);
				timespecsub(&timo.end, &timo.cur,
				    &timeout->_timeout);
			}
		}
	}
	umtxq_unlock(&uq->uq_key);
	umtx_key_release(&uq->uq_key);
	return (error);
}

/*
 * Signal a userland semaphore.
 */
static int
do_sem2_wake(struct thread *td, struct _usem2 *sem)
{
	struct umtx_key key;
	int error, cnt, rv;
	uint32_t count, flags;

	rv = fueword32(&sem->_flags, &flags);
	if (rv == -1)
		return (EFAULT);
	if ((error = umtx_key_get(sem, TYPE_SEM, GET_SHARE(flags), &key)) != 0)
		return (error);	
	umtxq_lock(&key);
	umtxq_busy(&key);
	cnt = umtxq_count(&key);
	if (cnt > 0) {
		/*
		 * If this was the last sleeping thread, clear the waiters
		 * flag in _count.
		 */
		if (cnt == 1) {
			umtxq_unlock(&key);
			rv = fueword32(&sem->_count, &count);
			while (rv != -1 && count & USEM_HAS_WAITERS)
				rv = casueword32(&sem->_count, count, &count,
				    count & ~USEM_HAS_WAITERS);
			if (rv == -1)
				error = EFAULT;
			umtxq_lock(&key);
		}

		umtxq_signal(&key, 1);
	}
	umtxq_unbusy(&key);
	umtxq_unlock(&key);
	umtx_key_release(&key);
	return (error);
}

inline int
umtx_copyin_timeout(const void *addr, struct timespec *tsp)
{
	int error;

	error = copyin(addr, tsp, sizeof(struct timespec));
	if (error == 0) {
		if (tsp->tv_sec < 0 ||
		    tsp->tv_nsec >= 1000000000 ||
		    tsp->tv_nsec < 0)
			error = EINVAL;
	}
	return (error);
}

static inline int
umtx_copyin_umtx_time(const void *addr, size_t size, struct _umtx_time *tp)
{
	int error;
	
	if (size <= sizeof(struct timespec)) {
		tp->_clockid = CLOCK_REALTIME;
		tp->_flags = 0;
		error = copyin(addr, &tp->_timeout, sizeof(struct timespec));
	} else 
		error = copyin(addr, tp, sizeof(struct _umtx_time));
	if (error != 0)
		return (error);
	if (tp->_timeout.tv_sec < 0 ||
	    tp->_timeout.tv_nsec >= 1000000000 || tp->_timeout.tv_nsec < 0)
		return (EINVAL);
	return (0);
}

static int
__umtx_op_unimpl(struct thread *td, struct _umtx_op_args *uap)
{

	return (EOPNOTSUPP);
}

static int
__umtx_op_wait(struct thread *td, struct _umtx_op_args *uap)
{
	struct _umtx_time timeout, *tm_p;
	int error;

	if (uap->uaddr2 == NULL)
		tm_p = NULL;
	else {
		error = umtx_copyin_umtx_time(
		    uap->uaddr2, (size_t)uap->uaddr1, &timeout);
		if (error != 0)
			return (error);
		tm_p = &timeout;
	}
	return (do_wait(td, uap->obj, uap->val, tm_p, 0, 0));
}

static int
__umtx_op_wait_uint(struct thread *td, struct _umtx_op_args *uap)
{
	struct _umtx_time timeout, *tm_p;
	int error;

	if (uap->uaddr2 == NULL)
		tm_p = NULL;
	else {
		error = umtx_copyin_umtx_time(
		    uap->uaddr2, (size_t)uap->uaddr1, &timeout);
		if (error != 0)
			return (error);
		tm_p = &timeout;
	}
	return (do_wait(td, uap->obj, uap->val, tm_p, 1, 0));
}

static int
__umtx_op_wait_uint_private(struct thread *td, struct _umtx_op_args *uap)
{
	struct _umtx_time *tm_p, timeout;
	int error;

	if (uap->uaddr2 == NULL)
		tm_p = NULL;
	else {
		error = umtx_copyin_umtx_time(
		    uap->uaddr2, (size_t)uap->uaddr1, &timeout);
		if (error != 0)
			return (error);
		tm_p = &timeout;
	}
	return (do_wait(td, uap->obj, uap->val, tm_p, 1, 1));
}

static int
__umtx_op_wake(struct thread *td, struct _umtx_op_args *uap)
{

	return (kern_umtx_wake(td, uap->obj, uap->val, 0));
}

#define BATCH_SIZE	128
static int
__umtx_op_nwake_private(struct thread *td, struct _umtx_op_args *uap)
{
	char *uaddrs[BATCH_SIZE], **upp;
	int count, error, i, pos, tocopy;

	upp = (char **)uap->obj;
	error = 0;
	for (count = uap->val, pos = 0; count > 0; count -= tocopy,
	    pos += tocopy) {
		tocopy = MIN(count, BATCH_SIZE);
		error = copyin(upp + pos, uaddrs, tocopy * sizeof(char *));
		if (error != 0)
			break;
		for (i = 0; i < tocopy; ++i)
			kern_umtx_wake(td, uaddrs[i], INT_MAX, 1);
		maybe_yield();
	}
	return (error);
}

static int
__umtx_op_wake_private(struct thread *td, struct _umtx_op_args *uap)
{

	return (kern_umtx_wake(td, uap->obj, uap->val, 1));
}

static int
__umtx_op_lock_umutex(struct thread *td, struct _umtx_op_args *uap)
{
	struct _umtx_time *tm_p, timeout;
	int error;

	/* Allow a null timespec (wait forever). */
	if (uap->uaddr2 == NULL)
		tm_p = NULL;
	else {
		error = umtx_copyin_umtx_time(
		    uap->uaddr2, (size_t)uap->uaddr1, &timeout);
		if (error != 0)
			return (error);
		tm_p = &timeout;
	}
	return (do_lock_umutex(td, uap->obj, tm_p, 0));
}

static int
__umtx_op_trylock_umutex(struct thread *td, struct _umtx_op_args *uap)
{

	return (do_lock_umutex(td, uap->obj, NULL, _UMUTEX_TRY));
}

static int
__umtx_op_wait_umutex(struct thread *td, struct _umtx_op_args *uap)
{
	struct _umtx_time *tm_p, timeout;
	int error;

	/* Allow a null timespec (wait forever). */
	if (uap->uaddr2 == NULL)
		tm_p = NULL;
	else {
		error = umtx_copyin_umtx_time(
		    uap->uaddr2, (size_t)uap->uaddr1, &timeout);
		if (error != 0)
			return (error);
		tm_p = &timeout;
	}
	return (do_lock_umutex(td, uap->obj, tm_p, _UMUTEX_WAIT));
}

static int
__umtx_op_wake_umutex(struct thread *td, struct _umtx_op_args *uap)
{

	return (do_wake_umutex(td, uap->obj));
}

static int
__umtx_op_unlock_umutex(struct thread *td, struct _umtx_op_args *uap)
{

	return (do_unlock_umutex(td, uap->obj, false));
}

static int
__umtx_op_set_ceiling(struct thread *td, struct _umtx_op_args *uap)
{

	return (do_set_ceiling(td, uap->obj, uap->val, uap->uaddr1));
}

static int
__umtx_op_cv_wait(struct thread *td, struct _umtx_op_args *uap)
{
	struct timespec *ts, timeout;
	int error;

	/* Allow a null timespec (wait forever). */
	if (uap->uaddr2 == NULL)
		ts = NULL;
	else {
		error = umtx_copyin_timeout(uap->uaddr2, &timeout);
		if (error != 0)
			return (error);
		ts = &timeout;
	}
	return (do_cv_wait(td, uap->obj, uap->uaddr1, ts, uap->val));
}

static int
__umtx_op_cv_signal(struct thread *td, struct _umtx_op_args *uap)
{

	return (do_cv_signal(td, uap->obj));
}

static int
__umtx_op_cv_broadcast(struct thread *td, struct _umtx_op_args *uap)
{

	return (do_cv_broadcast(td, uap->obj));
}

static int
__umtx_op_rw_rdlock(struct thread *td, struct _umtx_op_args *uap)
{
	struct _umtx_time timeout;
	int error;

	/* Allow a null timespec (wait forever). */
	if (uap->uaddr2 == NULL) {
		error = do_rw_rdlock(td, uap->obj, uap->val, 0);
	} else {
		error = umtx_copyin_umtx_time(uap->uaddr2,
		   (size_t)uap->uaddr1, &timeout);
		if (error != 0)
			return (error);
		error = do_rw_rdlock(td, uap->obj, uap->val, &timeout);
	}
	return (error);
}

static int
__umtx_op_rw_wrlock(struct thread *td, struct _umtx_op_args *uap)
{
	struct _umtx_time timeout;
	int error;

	/* Allow a null timespec (wait forever). */
	if (uap->uaddr2 == NULL) {
		error = do_rw_wrlock(td, uap->obj, 0);
	} else {
		error = umtx_copyin_umtx_time(uap->uaddr2, 
		   (size_t)uap->uaddr1, &timeout);
		if (error != 0)
			return (error);

		error = do_rw_wrlock(td, uap->obj, &timeout);
	}
	return (error);
}

static int
__umtx_op_rw_unlock(struct thread *td, struct _umtx_op_args *uap)
{

	return (do_rw_unlock(td, uap->obj));
}

#if defined(COMPAT_FREEBSD9) || defined(COMPAT_FREEBSD10)
static int
__umtx_op_sem_wait(struct thread *td, struct _umtx_op_args *uap)
{
	struct _umtx_time *tm_p, timeout;
	int error;

	/* Allow a null timespec (wait forever). */
	if (uap->uaddr2 == NULL)
		tm_p = NULL;
	else {
		error = umtx_copyin_umtx_time(
		    uap->uaddr2, (size_t)uap->uaddr1, &timeout);
		if (error != 0)
			return (error);
		tm_p = &timeout;
	}
	return (do_sem_wait(td, uap->obj, tm_p));
}

static int
__umtx_op_sem_wake(struct thread *td, struct _umtx_op_args *uap)
{

	return (do_sem_wake(td, uap->obj));
}
#endif

static int
__umtx_op_wake2_umutex(struct thread *td, struct _umtx_op_args *uap)
{

	return (do_wake2_umutex(td, uap->obj, uap->val));
}

static int
__umtx_op_sem2_wait(struct thread *td, struct _umtx_op_args *uap)
{
	struct _umtx_time *tm_p, timeout;
	size_t uasize;
	int error;

	/* Allow a null timespec (wait forever). */
	if (uap->uaddr2 == NULL) {
		uasize = 0;
		tm_p = NULL;
	} else {
		uasize = (size_t)uap->uaddr1;
		error = umtx_copyin_umtx_time(uap->uaddr2, uasize, &timeout);
		if (error != 0)
			return (error);
		tm_p = &timeout;
	}
	error = do_sem2_wait(td, uap->obj, tm_p);
	if (error == EINTR && uap->uaddr2 != NULL &&
	    (timeout._flags & UMTX_ABSTIME) == 0 &&
	    uasize >= sizeof(struct _umtx_time) + sizeof(struct timespec)) {
		error = copyout(&timeout._timeout,
		    (struct _umtx_time *)uap->uaddr2 + 1,
		    sizeof(struct timespec));
		if (error == 0) {
			error = EINTR;
		}
	}

	return (error);
}

static int
__umtx_op_sem2_wake(struct thread *td, struct _umtx_op_args *uap)
{

	return (do_sem2_wake(td, uap->obj));
}

#define	USHM_OBJ_UMTX(o)						\
    ((struct umtx_shm_obj_list *)(&(o)->umtx_data))

#define	USHMF_REG_LINKED	0x0001
#define	USHMF_OBJ_LINKED	0x0002
struct umtx_shm_reg {
	TAILQ_ENTRY(umtx_shm_reg) ushm_reg_link;
	LIST_ENTRY(umtx_shm_reg) ushm_obj_link;
	struct umtx_key		ushm_key;
	struct ucred		*ushm_cred;
	struct shmfd		*ushm_obj;
	u_int			ushm_refcnt;
	u_int			ushm_flags;
};

LIST_HEAD(umtx_shm_obj_list, umtx_shm_reg);
TAILQ_HEAD(umtx_shm_reg_head, umtx_shm_reg);

static uma_zone_t umtx_shm_reg_zone;
static struct umtx_shm_reg_head umtx_shm_registry[UMTX_CHAINS];
static struct mtx umtx_shm_lock;
static struct umtx_shm_reg_head umtx_shm_reg_delfree =
    TAILQ_HEAD_INITIALIZER(umtx_shm_reg_delfree);

static void umtx_shm_free_reg(struct umtx_shm_reg *reg);

static void
umtx_shm_reg_delfree_tq(void *context __unused, int pending __unused)
{
	struct umtx_shm_reg_head d;
	struct umtx_shm_reg *reg, *reg1;

	TAILQ_INIT(&d);
	mtx_lock(&umtx_shm_lock);
	TAILQ_CONCAT(&d, &umtx_shm_reg_delfree, ushm_reg_link);
	mtx_unlock(&umtx_shm_lock);
	TAILQ_FOREACH_SAFE(reg, &d, ushm_reg_link, reg1) {
		TAILQ_REMOVE(&d, reg, ushm_reg_link);
		umtx_shm_free_reg(reg);
	}
}

static struct task umtx_shm_reg_delfree_task =
    TASK_INITIALIZER(0, umtx_shm_reg_delfree_tq, NULL);

static struct umtx_shm_reg *
umtx_shm_find_reg_locked(const struct umtx_key *key)
{
	struct umtx_shm_reg *reg;
	struct umtx_shm_reg_head *reg_head;

	KASSERT(key->shared, ("umtx_p_find_rg: private key"));
	mtx_assert(&umtx_shm_lock, MA_OWNED);
	reg_head = &umtx_shm_registry[key->hash];
	TAILQ_FOREACH(reg, reg_head, ushm_reg_link) {
		KASSERT(reg->ushm_key.shared,
		    ("non-shared key on reg %p %d", reg, reg->ushm_key.shared));
		if (reg->ushm_key.info.shared.object ==
		    key->info.shared.object &&
		    reg->ushm_key.info.shared.offset ==
		    key->info.shared.offset) {
			KASSERT(reg->ushm_key.type == TYPE_SHM, ("TYPE_USHM"));
			KASSERT(reg->ushm_refcnt > 0,
			    ("reg %p refcnt 0 onlist", reg));
			KASSERT((reg->ushm_flags & USHMF_REG_LINKED) != 0,
			    ("reg %p not linked", reg));
			reg->ushm_refcnt++;
			return (reg);
		}
	}
	return (NULL);
}

static struct umtx_shm_reg *
umtx_shm_find_reg(const struct umtx_key *key)
{
	struct umtx_shm_reg *reg;

	mtx_lock(&umtx_shm_lock);
	reg = umtx_shm_find_reg_locked(key);
	mtx_unlock(&umtx_shm_lock);
	return (reg);
}

static void
umtx_shm_free_reg(struct umtx_shm_reg *reg)
{

	chgumtxcnt(reg->ushm_cred->cr_ruidinfo, -1, 0);
	crfree(reg->ushm_cred);
	shm_drop(reg->ushm_obj);
	uma_zfree(umtx_shm_reg_zone, reg);
}

static bool
umtx_shm_unref_reg_locked(struct umtx_shm_reg *reg, bool force)
{
	bool res;

	mtx_assert(&umtx_shm_lock, MA_OWNED);
	KASSERT(reg->ushm_refcnt > 0, ("ushm_reg %p refcnt 0", reg));
	reg->ushm_refcnt--;
	res = reg->ushm_refcnt == 0;
	if (res || force) {
		if ((reg->ushm_flags & USHMF_REG_LINKED) != 0) {
			TAILQ_REMOVE(&umtx_shm_registry[reg->ushm_key.hash],
			    reg, ushm_reg_link);
			reg->ushm_flags &= ~USHMF_REG_LINKED;
		}
		if ((reg->ushm_flags & USHMF_OBJ_LINKED) != 0) {
			LIST_REMOVE(reg, ushm_obj_link);
			reg->ushm_flags &= ~USHMF_OBJ_LINKED;
		}
	}
	return (res);
}

static void
umtx_shm_unref_reg(struct umtx_shm_reg *reg, bool force)
{
	vm_object_t object;
	bool dofree;

	if (force) {
		object = reg->ushm_obj->shm_object;
		VM_OBJECT_WLOCK(object);
		object->flags |= OBJ_UMTXDEAD;
		VM_OBJECT_WUNLOCK(object);
	}
	mtx_lock(&umtx_shm_lock);
	dofree = umtx_shm_unref_reg_locked(reg, force);
	mtx_unlock(&umtx_shm_lock);
	if (dofree)
		umtx_shm_free_reg(reg);
}

void
umtx_shm_object_init(vm_object_t object)
{

	LIST_INIT(USHM_OBJ_UMTX(object));
}

void
umtx_shm_object_terminated(vm_object_t object)
{
	struct umtx_shm_reg *reg, *reg1;
	bool dofree;

	if (LIST_EMPTY(USHM_OBJ_UMTX(object)))
		return;

	dofree = false;
	mtx_lock(&umtx_shm_lock);
	LIST_FOREACH_SAFE(reg, USHM_OBJ_UMTX(object), ushm_obj_link, reg1) {
		if (umtx_shm_unref_reg_locked(reg, true)) {
			TAILQ_INSERT_TAIL(&umtx_shm_reg_delfree, reg,
			    ushm_reg_link);
			dofree = true;
		}
	}
	mtx_unlock(&umtx_shm_lock);
	if (dofree)
		taskqueue_enqueue(taskqueue_thread, &umtx_shm_reg_delfree_task);
}

static int
umtx_shm_create_reg(struct thread *td, const struct umtx_key *key,
    struct umtx_shm_reg **res)
{
	struct umtx_shm_reg *reg, *reg1;
	struct ucred *cred;
	int error;

	reg = umtx_shm_find_reg(key);
	if (reg != NULL) {
		*res = reg;
		return (0);
	}
	cred = td->td_ucred;
	if (!chgumtxcnt(cred->cr_ruidinfo, 1, lim_cur(td, RLIMIT_UMTXP)))
		return (ENOMEM);
	reg = uma_zalloc(umtx_shm_reg_zone, M_WAITOK | M_ZERO);
	reg->ushm_refcnt = 1;
	bcopy(key, &reg->ushm_key, sizeof(*key));
	reg->ushm_obj = shm_alloc(td->td_ucred, O_RDWR);
	reg->ushm_cred = crhold(cred);
	error = shm_dotruncate(reg->ushm_obj, PAGE_SIZE);
	if (error != 0) {
		umtx_shm_free_reg(reg);
		return (error);
	}
	mtx_lock(&umtx_shm_lock);
	reg1 = umtx_shm_find_reg_locked(key);
	if (reg1 != NULL) {
		mtx_unlock(&umtx_shm_lock);
		umtx_shm_free_reg(reg);
		*res = reg1;
		return (0);
	}
	reg->ushm_refcnt++;
	TAILQ_INSERT_TAIL(&umtx_shm_registry[key->hash], reg, ushm_reg_link);
	LIST_INSERT_HEAD(USHM_OBJ_UMTX(key->info.shared.object), reg,
	    ushm_obj_link);
	reg->ushm_flags = USHMF_REG_LINKED | USHMF_OBJ_LINKED;
	mtx_unlock(&umtx_shm_lock);
	*res = reg;
	return (0);
}

static int
umtx_shm_alive(struct thread *td, void *addr)
{
	vm_map_t map;
	vm_map_entry_t entry;
	vm_object_t object;
	vm_pindex_t pindex;
	vm_prot_t prot;
	int res, ret;
	boolean_t wired;

	map = &td->td_proc->p_vmspace->vm_map;
	res = vm_map_lookup(&map, (uintptr_t)addr, VM_PROT_READ, &entry,
	    &object, &pindex, &prot, &wired);
	if (res != KERN_SUCCESS)
		return (EFAULT);
	if (object == NULL)
		ret = EINVAL;
	else
		ret = (object->flags & OBJ_UMTXDEAD) != 0 ? ENOTTY : 0;
	vm_map_lookup_done(map, entry);
	return (ret);
}

static void
umtx_shm_init(void)
{
	int i;

	umtx_shm_reg_zone = uma_zcreate("umtx_shm", sizeof(struct umtx_shm_reg),
	    NULL, NULL, NULL, NULL, UMA_ALIGN_PTR, 0);
	mtx_init(&umtx_shm_lock, "umtxshm", NULL, MTX_DEF);
	for (i = 0; i < nitems(umtx_shm_registry); i++)
		TAILQ_INIT(&umtx_shm_registry[i]);
}

static int
umtx_shm(struct thread *td, void *addr, u_int flags)
{
	struct umtx_key key;
	struct umtx_shm_reg *reg;
	struct file *fp;
	int error, fd;

	if (__bitcount(flags & (UMTX_SHM_CREAT | UMTX_SHM_LOOKUP |
	    UMTX_SHM_DESTROY| UMTX_SHM_ALIVE)) != 1)
		return (EINVAL);
	if ((flags & UMTX_SHM_ALIVE) != 0)
		return (umtx_shm_alive(td, addr));
	error = umtx_key_get(addr, TYPE_SHM, PROCESS_SHARE, &key);
	if (error != 0)
		return (error);
	KASSERT(key.shared == 1, ("non-shared key"));
	if ((flags & UMTX_SHM_CREAT) != 0) {
		error = umtx_shm_create_reg(td, &key, &reg);
	} else {
		reg = umtx_shm_find_reg(&key);
		if (reg == NULL)
			error = ESRCH;
	}
	umtx_key_release(&key);
	if (error != 0)
		return (error);
	KASSERT(reg != NULL, ("no reg"));
	if ((flags & UMTX_SHM_DESTROY) != 0) {
		umtx_shm_unref_reg(reg, true);
	} else {
#if 0
#ifdef MAC
		error = mac_posixshm_check_open(td->td_ucred,
		    reg->ushm_obj, FFLAGS(O_RDWR));
		if (error == 0)
#endif
			error = shm_access(reg->ushm_obj, td->td_ucred,
			    FFLAGS(O_RDWR));
		if (error == 0)
#endif
			error = falloc_caps(td, &fp, &fd, O_CLOEXEC, NULL);
		if (error == 0) {
			shm_hold(reg->ushm_obj);
			finit(fp, FFLAGS(O_RDWR), DTYPE_SHM, reg->ushm_obj,
			    &shm_ops);
			td->td_retval[0] = fd;
			fdrop(fp, td);
		}
	}
	umtx_shm_unref_reg(reg, false);
	return (error);
}

static int
__umtx_op_shm(struct thread *td, struct _umtx_op_args *uap)
{

	return (umtx_shm(td, uap->uaddr1, uap->val));
}

static int
umtx_robust_lists(struct thread *td, struct umtx_robust_lists_params *rbp)
{

	td->td_rb_list = rbp->robust_list_offset;
	td->td_rbp_list = rbp->robust_priv_list_offset;
	td->td_rb_inact = rbp->robust_inact_offset;
	return (0);
}

static int
__umtx_op_robust_lists(struct thread *td, struct _umtx_op_args *uap)
{
	struct umtx_robust_lists_params rb;
	int error;

	if (uap->val > sizeof(rb))
		return (EINVAL);
	bzero(&rb, sizeof(rb));
	error = copyin(uap->uaddr1, &rb, uap->val);
	if (error != 0)
		return (error);
	return (umtx_robust_lists(td, &rb));
}

typedef int (*_umtx_op_func)(struct thread *td, struct _umtx_op_args *uap);

static const _umtx_op_func op_table[] = {
	[UMTX_OP_RESERVED0]	= __umtx_op_unimpl,
	[UMTX_OP_RESERVED1]	= __umtx_op_unimpl,
	[UMTX_OP_WAIT]		= __umtx_op_wait,
	[UMTX_OP_WAKE]		= __umtx_op_wake,
	[UMTX_OP_MUTEX_TRYLOCK]	= __umtx_op_trylock_umutex,
	[UMTX_OP_MUTEX_LOCK]	= __umtx_op_lock_umutex,
	[UMTX_OP_MUTEX_UNLOCK]	= __umtx_op_unlock_umutex,
	[UMTX_OP_SET_CEILING]	= __umtx_op_set_ceiling,
	[UMTX_OP_CV_WAIT]	= __umtx_op_cv_wait,
	[UMTX_OP_CV_SIGNAL]	= __umtx_op_cv_signal,
	[UMTX_OP_CV_BROADCAST]	= __umtx_op_cv_broadcast,
	[UMTX_OP_WAIT_UINT]	= __umtx_op_wait_uint,
	[UMTX_OP_RW_RDLOCK]	= __umtx_op_rw_rdlock,
	[UMTX_OP_RW_WRLOCK]	= __umtx_op_rw_wrlock,
	[UMTX_OP_RW_UNLOCK]	= __umtx_op_rw_unlock,
	[UMTX_OP_WAIT_UINT_PRIVATE] = __umtx_op_wait_uint_private,
	[UMTX_OP_WAKE_PRIVATE]	= __umtx_op_wake_private,
	[UMTX_OP_MUTEX_WAIT]	= __umtx_op_wait_umutex,
	[UMTX_OP_MUTEX_WAKE]	= __umtx_op_wake_umutex,
#if defined(COMPAT_FREEBSD9) || defined(COMPAT_FREEBSD10)
	[UMTX_OP_SEM_WAIT]	= __umtx_op_sem_wait,
	[UMTX_OP_SEM_WAKE]	= __umtx_op_sem_wake,
#else
	[UMTX_OP_SEM_WAIT]	= __umtx_op_unimpl,
	[UMTX_OP_SEM_WAKE]	= __umtx_op_unimpl,
#endif
	[UMTX_OP_NWAKE_PRIVATE]	= __umtx_op_nwake_private,
	[UMTX_OP_MUTEX_WAKE2]	= __umtx_op_wake2_umutex,
	[UMTX_OP_SEM2_WAIT]	= __umtx_op_sem2_wait,
	[UMTX_OP_SEM2_WAKE]	= __umtx_op_sem2_wake,
	[UMTX_OP_SHM]		= __umtx_op_shm,
	[UMTX_OP_ROBUST_LISTS]	= __umtx_op_robust_lists,
};

int
sys__umtx_op(struct thread *td, struct _umtx_op_args *uap)
{

	if ((unsigned)uap->op < nitems(op_table))
		return (*op_table[uap->op])(td, uap);
	return (EINVAL);
}

#ifdef COMPAT_FREEBSD32

struct timespec32 {
	int32_t tv_sec;
	int32_t tv_nsec;
};

struct umtx_time32 {
	struct	timespec32	timeout;
	uint32_t		flags;
	uint32_t		clockid;
};

static inline int
umtx_copyin_timeout32(void *addr, struct timespec *tsp)
{
	struct timespec32 ts32;
	int error;

	error = copyin(addr, &ts32, sizeof(struct timespec32));
	if (error == 0) {
		if (ts32.tv_sec < 0 ||
		    ts32.tv_nsec >= 1000000000 ||
		    ts32.tv_nsec < 0)
			error = EINVAL;
		else {
			tsp->tv_sec = ts32.tv_sec;
			tsp->tv_nsec = ts32.tv_nsec;
		}
	}
	return (error);
}

static inline int
umtx_copyin_umtx_time32(const void *addr, size_t size, struct _umtx_time *tp)
{
	struct umtx_time32 t32;
	int error;
	
	t32.clockid = CLOCK_REALTIME;
	t32.flags   = 0;
	if (size <= sizeof(struct timespec32))
		error = copyin(addr, &t32.timeout, sizeof(struct timespec32));
	else 
		error = copyin(addr, &t32, sizeof(struct umtx_time32));
	if (error != 0)
		return (error);
	if (t32.timeout.tv_sec < 0 ||
	    t32.timeout.tv_nsec >= 1000000000 || t32.timeout.tv_nsec < 0)
		return (EINVAL);
	tp->_timeout.tv_sec = t32.timeout.tv_sec;
	tp->_timeout.tv_nsec = t32.timeout.tv_nsec;
	tp->_flags = t32.flags;
	tp->_clockid = t32.clockid;
	return (0);
}

static int
__umtx_op_wait_compat32(struct thread *td, struct _umtx_op_args *uap)
{
	struct _umtx_time *tm_p, timeout;
	int error;

	if (uap->uaddr2 == NULL)
		tm_p = NULL;
	else {
		error = umtx_copyin_umtx_time32(uap->uaddr2,
			(size_t)uap->uaddr1, &timeout);
		if (error != 0)
			return (error);
		tm_p = &timeout;
	}
	return (do_wait(td, uap->obj, uap->val, tm_p, 1, 0));
}

static int
__umtx_op_lock_umutex_compat32(struct thread *td, struct _umtx_op_args *uap)
{
	struct _umtx_time *tm_p, timeout;
	int error;

	/* Allow a null timespec (wait forever). */
	if (uap->uaddr2 == NULL)
		tm_p = NULL;
	else {
		error = umtx_copyin_umtx_time32(uap->uaddr2,
			    (size_t)uap->uaddr1, &timeout);
		if (error != 0)
			return (error);
		tm_p = &timeout;
	}
	return (do_lock_umutex(td, uap->obj, tm_p, 0));
}

static int
__umtx_op_wait_umutex_compat32(struct thread *td, struct _umtx_op_args *uap)
{
	struct _umtx_time *tm_p, timeout;
	int error;

	/* Allow a null timespec (wait forever). */
	if (uap->uaddr2 == NULL)
		tm_p = NULL;
	else {
		error = umtx_copyin_umtx_time32(uap->uaddr2, 
		    (size_t)uap->uaddr1, &timeout);
		if (error != 0)
			return (error);
		tm_p = &timeout;
	}
	return (do_lock_umutex(td, uap->obj, tm_p, _UMUTEX_WAIT));
}

static int
__umtx_op_cv_wait_compat32(struct thread *td, struct _umtx_op_args *uap)
{
	struct timespec *ts, timeout;
	int error;

	/* Allow a null timespec (wait forever). */
	if (uap->uaddr2 == NULL)
		ts = NULL;
	else {
		error = umtx_copyin_timeout32(uap->uaddr2, &timeout);
		if (error != 0)
			return (error);
		ts = &timeout;
	}
	return (do_cv_wait(td, uap->obj, uap->uaddr1, ts, uap->val));
}

static int
__umtx_op_rw_rdlock_compat32(struct thread *td, struct _umtx_op_args *uap)
{
	struct _umtx_time timeout;
	int error;

	/* Allow a null timespec (wait forever). */
	if (uap->uaddr2 == NULL) {
		error = do_rw_rdlock(td, uap->obj, uap->val, 0);
	} else {
		error = umtx_copyin_umtx_time32(uap->uaddr2,
		    (size_t)uap->uaddr1, &timeout);
		if (error != 0)
			return (error);
		error = do_rw_rdlock(td, uap->obj, uap->val, &timeout);
	}
	return (error);
}

static int
__umtx_op_rw_wrlock_compat32(struct thread *td, struct _umtx_op_args *uap)
{
	struct _umtx_time timeout;
	int error;

	/* Allow a null timespec (wait forever). */
	if (uap->uaddr2 == NULL) {
		error = do_rw_wrlock(td, uap->obj, 0);
	} else {
		error = umtx_copyin_umtx_time32(uap->uaddr2,
		    (size_t)uap->uaddr1, &timeout);
		if (error != 0)
			return (error);
		error = do_rw_wrlock(td, uap->obj, &timeout);
	}
	return (error);
}

static int
__umtx_op_wait_uint_private_compat32(struct thread *td, struct _umtx_op_args *uap)
{
	struct _umtx_time *tm_p, timeout;
	int error;

	if (uap->uaddr2 == NULL)
		tm_p = NULL;
	else {
		error = umtx_copyin_umtx_time32(
		    uap->uaddr2, (size_t)uap->uaddr1,&timeout);
		if (error != 0)
			return (error);
		tm_p = &timeout;
	}
	return (do_wait(td, uap->obj, uap->val, tm_p, 1, 1));
}

#if defined(COMPAT_FREEBSD9) || defined(COMPAT_FREEBSD10)
static int
__umtx_op_sem_wait_compat32(struct thread *td, struct _umtx_op_args *uap)
{
	struct _umtx_time *tm_p, timeout;
	int error;

	/* Allow a null timespec (wait forever). */
	if (uap->uaddr2 == NULL)
		tm_p = NULL;
	else {
		error = umtx_copyin_umtx_time32(uap->uaddr2,
		    (size_t)uap->uaddr1, &timeout);
		if (error != 0)
			return (error);
		tm_p = &timeout;
	}
	return (do_sem_wait(td, uap->obj, tm_p));
}
#endif

static int
__umtx_op_sem2_wait_compat32(struct thread *td, struct _umtx_op_args *uap)
{
	struct _umtx_time *tm_p, timeout;
	size_t uasize;
	int error;

	/* Allow a null timespec (wait forever). */
	if (uap->uaddr2 == NULL) {
		uasize = 0;
		tm_p = NULL;
	} else {
		uasize = (size_t)uap->uaddr1;
		error = umtx_copyin_umtx_time32(uap->uaddr2, uasize, &timeout);
		if (error != 0)
			return (error);
		tm_p = &timeout;
	}
	error = do_sem2_wait(td, uap->obj, tm_p);
	if (error == EINTR && uap->uaddr2 != NULL &&
	    (timeout._flags & UMTX_ABSTIME) == 0 &&
	    uasize >= sizeof(struct umtx_time32) + sizeof(struct timespec32)) {
		struct timespec32 remain32 = {
			.tv_sec = timeout._timeout.tv_sec,
			.tv_nsec = timeout._timeout.tv_nsec
		};
		error = copyout(&remain32,
		    (struct umtx_time32 *)uap->uaddr2 + 1,
		    sizeof(struct timespec32));
		if (error == 0) {
			error = EINTR;
		}
	}

	return (error);
}

static int
__umtx_op_nwake_private32(struct thread *td, struct _umtx_op_args *uap)
{
	uint32_t uaddrs[BATCH_SIZE], **upp;
	int count, error, i, pos, tocopy;

	upp = (uint32_t **)uap->obj;
	error = 0;
	for (count = uap->val, pos = 0; count > 0; count -= tocopy,
	    pos += tocopy) {
		tocopy = MIN(count, BATCH_SIZE);
		error = copyin(upp + pos, uaddrs, tocopy * sizeof(uint32_t));
		if (error != 0)
			break;
		for (i = 0; i < tocopy; ++i)
			kern_umtx_wake(td, (void *)(intptr_t)uaddrs[i],
			    INT_MAX, 1);
		maybe_yield();
	}
	return (error);
}

struct umtx_robust_lists_params_compat32 {
	uint32_t	robust_list_offset;
	uint32_t	robust_priv_list_offset;
	uint32_t	robust_inact_offset;
};

static int
__umtx_op_robust_lists_compat32(struct thread *td, struct _umtx_op_args *uap)
{
	struct umtx_robust_lists_params rb;
	struct umtx_robust_lists_params_compat32 rb32;
	int error;

	if (uap->val > sizeof(rb32))
		return (EINVAL);
	bzero(&rb, sizeof(rb));
	bzero(&rb32, sizeof(rb32));
	error = copyin(uap->uaddr1, &rb32, uap->val);
	if (error != 0)
		return (error);
	rb.robust_list_offset = rb32.robust_list_offset;
	rb.robust_priv_list_offset = rb32.robust_priv_list_offset;
	rb.robust_inact_offset = rb32.robust_inact_offset;
	return (umtx_robust_lists(td, &rb));
}

static const _umtx_op_func op_table_compat32[] = {
	[UMTX_OP_RESERVED0]	= __umtx_op_unimpl,
	[UMTX_OP_RESERVED1]	= __umtx_op_unimpl,
	[UMTX_OP_WAIT]		= __umtx_op_wait_compat32,
	[UMTX_OP_WAKE]		= __umtx_op_wake,
	[UMTX_OP_MUTEX_TRYLOCK]	= __umtx_op_trylock_umutex,
	[UMTX_OP_MUTEX_LOCK]	= __umtx_op_lock_umutex_compat32,
	[UMTX_OP_MUTEX_UNLOCK]	= __umtx_op_unlock_umutex,
	[UMTX_OP_SET_CEILING]	= __umtx_op_set_ceiling,
	[UMTX_OP_CV_WAIT]	= __umtx_op_cv_wait_compat32,
	[UMTX_OP_CV_SIGNAL]	= __umtx_op_cv_signal,
	[UMTX_OP_CV_BROADCAST]	= __umtx_op_cv_broadcast,
	[UMTX_OP_WAIT_UINT]	= __umtx_op_wait_compat32,
	[UMTX_OP_RW_RDLOCK]	= __umtx_op_rw_rdlock_compat32,
	[UMTX_OP_RW_WRLOCK]	= __umtx_op_rw_wrlock_compat32,
	[UMTX_OP_RW_UNLOCK]	= __umtx_op_rw_unlock,
	[UMTX_OP_WAIT_UINT_PRIVATE] = __umtx_op_wait_uint_private_compat32,
	[UMTX_OP_WAKE_PRIVATE]	= __umtx_op_wake_private,
	[UMTX_OP_MUTEX_WAIT]	= __umtx_op_wait_umutex_compat32,
	[UMTX_OP_MUTEX_WAKE]	= __umtx_op_wake_umutex,
#if defined(COMPAT_FREEBSD9) || defined(COMPAT_FREEBSD10)
	[UMTX_OP_SEM_WAIT]	= __umtx_op_sem_wait_compat32,
	[UMTX_OP_SEM_WAKE]	= __umtx_op_sem_wake,
#else
	[UMTX_OP_SEM_WAIT]	= __umtx_op_unimpl,
	[UMTX_OP_SEM_WAKE]	= __umtx_op_unimpl,
#endif
	[UMTX_OP_NWAKE_PRIVATE]	= __umtx_op_nwake_private32,
	[UMTX_OP_MUTEX_WAKE2]	= __umtx_op_wake2_umutex,
	[UMTX_OP_SEM2_WAIT]	= __umtx_op_sem2_wait_compat32,
	[UMTX_OP_SEM2_WAKE]	= __umtx_op_sem2_wake,
	[UMTX_OP_SHM]		= __umtx_op_shm,
	[UMTX_OP_ROBUST_LISTS]	= __umtx_op_robust_lists_compat32,
};

int
freebsd32__umtx_op(struct thread *td, struct freebsd32__umtx_op_args *uap)
{

	if ((unsigned)uap->op < nitems(op_table_compat32)) {
		return (*op_table_compat32[uap->op])(td,
		    (struct _umtx_op_args *)uap);
	}
	return (EINVAL);
}
#endif

void
umtx_thread_init(struct thread *td)
{

	td->td_umtxq = umtxq_alloc();
	td->td_umtxq->uq_thread = td;
}

void
umtx_thread_fini(struct thread *td)
{

	umtxq_free(td->td_umtxq);
}

/*
 * It will be called when new thread is created, e.g fork().
 */
void
umtx_thread_alloc(struct thread *td)
{
	struct umtx_q *uq;

	uq = td->td_umtxq;
	uq->uq_inherited_pri = PRI_MAX;

	KASSERT(uq->uq_flags == 0, ("uq_flags != 0"));
	KASSERT(uq->uq_thread == td, ("uq_thread != td"));
	KASSERT(uq->uq_pi_blocked == NULL, ("uq_pi_blocked != NULL"));
	KASSERT(TAILQ_EMPTY(&uq->uq_pi_contested), ("uq_pi_contested is not empty"));
}

/*
 * exec() hook.
 *
 * Clear robust lists for all process' threads, not delaying the
 * cleanup to thread_exit hook, since the relevant address space is
 * destroyed right now.
 */
static void
umtx_exec_hook(void *arg __unused, struct proc *p,
    struct image_params *imgp __unused)
{
	struct thread *td;

	KASSERT(p == curproc, ("need curproc"));
	PROC_LOCK(p);
	KASSERT((p->p_flag & P_HADTHREADS) == 0 ||
	    (p->p_flag & P_STOPPED_SINGLE) != 0,
	    ("curproc must be single-threaded"));
	FOREACH_THREAD_IN_PROC(p, td) {
		KASSERT(td == curthread ||
		    ((td->td_flags & TDF_BOUNDARY) != 0 && TD_IS_SUSPENDED(td)),
		    ("running thread %p %p", p, td));
		PROC_UNLOCK(p);
		umtx_thread_cleanup(td);
		PROC_LOCK(p);
		td->td_rb_list = td->td_rbp_list = td->td_rb_inact = 0;
	}
	PROC_UNLOCK(p);
}

/*
 * thread_exit() hook.
 */
void
umtx_thread_exit(struct thread *td)
{

	umtx_thread_cleanup(td);
}

static int
umtx_read_uptr(struct thread *td, uintptr_t ptr, uintptr_t *res)
{
	u_long res1;
#ifdef COMPAT_FREEBSD32
	uint32_t res32;
#endif
	int error;

#ifdef COMPAT_FREEBSD32
	if (SV_PROC_FLAG(td->td_proc, SV_ILP32)) {
		error = fueword32((void *)ptr, &res32);
		if (error == 0)
			res1 = res32;
	} else
#endif
	{
		error = fueword((void *)ptr, &res1);
	}
	if (error == 0)
		*res = res1;
	else
		error = EFAULT;
	return (error);
}

static void
umtx_read_rb_list(struct thread *td, struct umutex *m, uintptr_t *rb_list)
{
#ifdef COMPAT_FREEBSD32
	struct umutex32 m32;

	if (SV_PROC_FLAG(td->td_proc, SV_ILP32)) {
		memcpy(&m32, m, sizeof(m32));
		*rb_list = m32.m_rb_lnk;
	} else
#endif
		*rb_list = m->m_rb_lnk;
}

static int
umtx_handle_rb(struct thread *td, uintptr_t rbp, uintptr_t *rb_list, bool inact)
{
	struct umutex m;
	int error;

	KASSERT(td->td_proc == curproc, ("need current vmspace"));
	error = copyin((void *)rbp, &m, sizeof(m));
	if (error != 0)
		return (error);
	if (rb_list != NULL)
		umtx_read_rb_list(td, &m, rb_list);
	if ((m.m_flags & UMUTEX_ROBUST) == 0)
		return (EINVAL);
	if ((m.m_owner & ~UMUTEX_CONTESTED) != td->td_tid)
		/* inact is cleared after unlock, allow the inconsistency */
		return (inact ? 0 : EINVAL);
	return (do_unlock_umutex(td, (struct umutex *)rbp, true));
}

static void
umtx_cleanup_rb_list(struct thread *td, uintptr_t rb_list, uintptr_t *rb_inact,
    const char *name)
{
	int error, i;
	uintptr_t rbp;
	bool inact;

	if (rb_list == 0)
		return;
	error = umtx_read_uptr(td, rb_list, &rbp);
	for (i = 0; error == 0 && rbp != 0 && i < umtx_max_rb; i++) {
		if (rbp == *rb_inact) {
			inact = true;
			*rb_inact = 0;
		} else
			inact = false;
		error = umtx_handle_rb(td, rbp, &rbp, inact);
	}
	if (i == umtx_max_rb && umtx_verbose_rb) {
		uprintf("comm %s pid %d: reached umtx %smax rb %d\n",
		    td->td_proc->p_comm, td->td_proc->p_pid, name, umtx_max_rb);
	}
	if (error != 0 && umtx_verbose_rb) {
		uprintf("comm %s pid %d: handling %srb error %d\n",
		    td->td_proc->p_comm, td->td_proc->p_pid, name, error);
	}
}

/*
 * Clean up umtx data.
 */
static void
umtx_thread_cleanup(struct thread *td)
{
	struct umtx_q *uq;
	struct umtx_pi *pi;
	uintptr_t rb_inact;

	/*
	 * Disown pi mutexes.
	 */
	uq = td->td_umtxq;
	if (uq != NULL) {
		mtx_lock(&umtx_lock);
		uq->uq_inherited_pri = PRI_MAX;
		while ((pi = TAILQ_FIRST(&uq->uq_pi_contested)) != NULL) {
			pi->pi_owner = NULL;
			TAILQ_REMOVE(&uq->uq_pi_contested, pi, pi_link);
		}
		mtx_unlock(&umtx_lock);
		thread_lock(td);
		sched_lend_user_prio(td, PRI_MAX);
		thread_unlock(td);
	}

	/*
	 * Handle terminated robust mutexes.  Must be done after
	 * robust pi disown, otherwise unlock could see unowned
	 * entries.
	 */
	rb_inact = td->td_rb_inact;
	if (rb_inact != 0)
		(void)umtx_read_uptr(td, rb_inact, &rb_inact);
	umtx_cleanup_rb_list(td, td->td_rb_list, &rb_inact, "");
	umtx_cleanup_rb_list(td, td->td_rbp_list, &rb_inact, "priv ");
	if (rb_inact != 0)
		(void)umtx_handle_rb(td, rb_inact, NULL, true);
}
