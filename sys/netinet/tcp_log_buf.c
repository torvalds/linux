/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2016-2018 Netflix, Inc.
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

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/kernel.h>
#include <sys/lock.h>
#include <sys/malloc.h>
#include <sys/mutex.h>
#include <sys/queue.h>
#include <sys/refcount.h>
#include <sys/rwlock.h>
#include <sys/socket.h>
#include <sys/socketvar.h>
#include <sys/sysctl.h>
#include <sys/tree.h>
#include <sys/counter.h>

#include <dev/tcp_log/tcp_log_dev.h>

#include <net/if.h>
#include <net/if_var.h>
#include <net/vnet.h>

#include <netinet/in.h>
#include <netinet/in_pcb.h>
#include <netinet/in_var.h>
#include <netinet/tcp_var.h>
#include <netinet/tcp_log_buf.h>

/* Default expiry time */
#define	TCP_LOG_EXPIRE_TIME	((sbintime_t)60 * SBT_1S)

/* Max interval at which to run the expiry timer */
#define	TCP_LOG_EXPIRE_INTVL	((sbintime_t)5 * SBT_1S)

bool	tcp_log_verbose;
static uma_zone_t tcp_log_bucket_zone, tcp_log_node_zone, tcp_log_zone;
static int	tcp_log_session_limit = TCP_LOG_BUF_DEFAULT_SESSION_LIMIT;
static uint32_t	tcp_log_version = TCP_LOG_BUF_VER;
RB_HEAD(tcp_log_id_tree, tcp_log_id_bucket);
static struct tcp_log_id_tree tcp_log_id_head;
static STAILQ_HEAD(, tcp_log_id_node) tcp_log_expireq_head =
    STAILQ_HEAD_INITIALIZER(tcp_log_expireq_head);
static struct mtx tcp_log_expireq_mtx;
static struct callout tcp_log_expireq_callout;
static u_long tcp_log_auto_ratio = 0;
static volatile u_long tcp_log_auto_ratio_cur = 0;
static uint32_t tcp_log_auto_mode = TCP_LOG_STATE_TAIL;
static bool tcp_log_auto_all = false;

RB_PROTOTYPE_STATIC(tcp_log_id_tree, tcp_log_id_bucket, tlb_rb, tcp_log_id_cmp)

SYSCTL_NODE(_net_inet_tcp, OID_AUTO, bb, CTLFLAG_RW, 0, "TCP Black Box controls");

SYSCTL_BOOL(_net_inet_tcp_bb, OID_AUTO, log_verbose, CTLFLAG_RW, &tcp_log_verbose,
    0, "Force verbose logging for TCP traces");

SYSCTL_INT(_net_inet_tcp_bb, OID_AUTO, log_session_limit,
    CTLFLAG_RW, &tcp_log_session_limit, 0,
    "Maximum number of events maintained for each TCP session");

SYSCTL_UMA_MAX(_net_inet_tcp_bb, OID_AUTO, log_global_limit, CTLFLAG_RW,
    &tcp_log_zone, "Maximum number of events maintained for all TCP sessions");

SYSCTL_UMA_CUR(_net_inet_tcp_bb, OID_AUTO, log_global_entries, CTLFLAG_RD,
    &tcp_log_zone, "Current number of events maintained for all TCP sessions");

SYSCTL_UMA_MAX(_net_inet_tcp_bb, OID_AUTO, log_id_limit, CTLFLAG_RW,
    &tcp_log_bucket_zone, "Maximum number of log IDs");

SYSCTL_UMA_CUR(_net_inet_tcp_bb, OID_AUTO, log_id_entries, CTLFLAG_RD,
    &tcp_log_bucket_zone, "Current number of log IDs");

SYSCTL_UMA_MAX(_net_inet_tcp_bb, OID_AUTO, log_id_tcpcb_limit, CTLFLAG_RW,
    &tcp_log_node_zone, "Maximum number of tcpcbs with log IDs");

SYSCTL_UMA_CUR(_net_inet_tcp_bb, OID_AUTO, log_id_tcpcb_entries, CTLFLAG_RD,
    &tcp_log_node_zone, "Current number of tcpcbs with log IDs");

SYSCTL_U32(_net_inet_tcp_bb, OID_AUTO, log_version, CTLFLAG_RD, &tcp_log_version,
    0, "Version of log formats exported");

SYSCTL_ULONG(_net_inet_tcp_bb, OID_AUTO, log_auto_ratio, CTLFLAG_RW,
    &tcp_log_auto_ratio, 0, "Do auto capturing for 1 out of N sessions");

SYSCTL_U32(_net_inet_tcp_bb, OID_AUTO, log_auto_mode, CTLFLAG_RW,
    &tcp_log_auto_mode, TCP_LOG_STATE_HEAD_AUTO,
    "Logging mode for auto-selected sessions (default is TCP_LOG_STATE_HEAD_AUTO)");

SYSCTL_BOOL(_net_inet_tcp_bb, OID_AUTO, log_auto_all, CTLFLAG_RW,
    &tcp_log_auto_all, false,
    "Auto-select from all sessions (rather than just those with IDs)");

#ifdef TCPLOG_DEBUG_COUNTERS
counter_u64_t tcp_log_queued;
counter_u64_t tcp_log_que_fail1;
counter_u64_t tcp_log_que_fail2;
counter_u64_t tcp_log_que_fail3;
counter_u64_t tcp_log_que_fail4;
counter_u64_t tcp_log_que_fail5;
counter_u64_t tcp_log_que_copyout;
counter_u64_t tcp_log_que_read;
counter_u64_t tcp_log_que_freed;

SYSCTL_COUNTER_U64(_net_inet_tcp_bb, OID_AUTO, queued, CTLFLAG_RD,
    &tcp_log_queued, "Number of entries queued");
SYSCTL_COUNTER_U64(_net_inet_tcp_bb, OID_AUTO, fail1, CTLFLAG_RD,
    &tcp_log_que_fail1, "Number of entries queued but fail 1");
SYSCTL_COUNTER_U64(_net_inet_tcp_bb, OID_AUTO, fail2, CTLFLAG_RD,
    &tcp_log_que_fail2, "Number of entries queued but fail 2");
SYSCTL_COUNTER_U64(_net_inet_tcp_bb, OID_AUTO, fail3, CTLFLAG_RD,
    &tcp_log_que_fail3, "Number of entries queued but fail 3");
SYSCTL_COUNTER_U64(_net_inet_tcp_bb, OID_AUTO, fail4, CTLFLAG_RD,
    &tcp_log_que_fail4, "Number of entries queued but fail 4");
SYSCTL_COUNTER_U64(_net_inet_tcp_bb, OID_AUTO, fail5, CTLFLAG_RD,
    &tcp_log_que_fail5, "Number of entries queued but fail 4");
SYSCTL_COUNTER_U64(_net_inet_tcp_bb, OID_AUTO, copyout, CTLFLAG_RD,
    &tcp_log_que_copyout, "Number of entries copied out");
SYSCTL_COUNTER_U64(_net_inet_tcp_bb, OID_AUTO, read, CTLFLAG_RD,
    &tcp_log_que_read, "Number of entries read from the queue");
SYSCTL_COUNTER_U64(_net_inet_tcp_bb, OID_AUTO, freed, CTLFLAG_RD,
    &tcp_log_que_freed, "Number of entries freed after reading");
#endif

#ifdef INVARIANTS
#define	TCPLOG_DEBUG_RINGBUF
#endif

struct tcp_log_mem
{
	STAILQ_ENTRY(tcp_log_mem) tlm_queue;
	struct tcp_log_buffer	tlm_buf;
	struct tcp_log_verbose	tlm_v;
#ifdef TCPLOG_DEBUG_RINGBUF
	volatile int		tlm_refcnt;
#endif
};

/* 60 bytes for the header, + 16 bytes for padding */
static uint8_t	zerobuf[76];

/*
 * Lock order:
 * 1. TCPID_TREE
 * 2. TCPID_BUCKET
 * 3. INP
 *
 * Rules:
 * A. You need a lock on the Tree to add/remove buckets.
 * B. You need a lock on the bucket to add/remove nodes from the bucket.
 * C. To change information in a node, you need the INP lock if the tln_closed
 *    field is false. Otherwise, you need the bucket lock. (Note that the
 *    tln_closed field can change at any point, so you need to recheck the
 *    entry after acquiring the INP lock.)
 * D. To remove a node from the bucket, you must have that entry locked,
 *    according to the criteria of Rule C. Also, the node must not be on
 *    the expiry queue.
 * E. The exception to C is the expiry queue fields, which are locked by
 *    the TCPLOG_EXPIREQ lock.
 *
 * Buckets have a reference count. Each node is a reference. Further,
 * other callers may add reference counts to keep a bucket from disappearing.
 * You can add a reference as long as you own a lock sufficient to keep the
 * bucket from disappearing. For example, a common use is:
 *   a. Have a locked INP, but need to lock the TCPID_BUCKET.
 *   b. Add a refcount on the bucket. (Safe because the INP lock prevents
 *      the TCPID_BUCKET from going away.)
 *   c. Drop the INP lock.
 *   d. Acquire a lock on the TCPID_BUCKET.
 *   e. Acquire a lock on the INP.
 *   f. Drop the refcount on the bucket.
 *      (At this point, the bucket may disappear.)
 *
 * Expire queue lock:
 * You can acquire this with either the bucket or INP lock. Don't reverse it.
 * When the expire code has committed to freeing a node, it resets the expiry
 * time to SBT_MAX. That is the signal to everyone else that they should
 * leave that node alone.
 */
static struct rwlock tcp_id_tree_lock;
#define	TCPID_TREE_WLOCK()		rw_wlock(&tcp_id_tree_lock)
#define	TCPID_TREE_RLOCK()		rw_rlock(&tcp_id_tree_lock)
#define	TCPID_TREE_UPGRADE()		rw_try_upgrade(&tcp_id_tree_lock)
#define	TCPID_TREE_WUNLOCK()		rw_wunlock(&tcp_id_tree_lock)
#define	TCPID_TREE_RUNLOCK()		rw_runlock(&tcp_id_tree_lock)
#define	TCPID_TREE_WLOCK_ASSERT()	rw_assert(&tcp_id_tree_lock, RA_WLOCKED)
#define	TCPID_TREE_RLOCK_ASSERT()	rw_assert(&tcp_id_tree_lock, RA_RLOCKED)
#define	TCPID_TREE_UNLOCK_ASSERT()	rw_assert(&tcp_id_tree_lock, RA_UNLOCKED)

#define	TCPID_BUCKET_LOCK_INIT(tlb)	mtx_init(&((tlb)->tlb_mtx), "tcp log id bucket", NULL, MTX_DEF)
#define	TCPID_BUCKET_LOCK_DESTROY(tlb)	mtx_destroy(&((tlb)->tlb_mtx))
#define	TCPID_BUCKET_LOCK(tlb)		mtx_lock(&((tlb)->tlb_mtx))
#define	TCPID_BUCKET_UNLOCK(tlb)	mtx_unlock(&((tlb)->tlb_mtx))
#define	TCPID_BUCKET_LOCK_ASSERT(tlb)	mtx_assert(&((tlb)->tlb_mtx), MA_OWNED)
#define	TCPID_BUCKET_UNLOCK_ASSERT(tlb) mtx_assert(&((tlb)->tlb_mtx), MA_NOTOWNED)

#define	TCPID_BUCKET_REF(tlb)		refcount_acquire(&((tlb)->tlb_refcnt))
#define	TCPID_BUCKET_UNREF(tlb)		refcount_release(&((tlb)->tlb_refcnt))

#define	TCPLOG_EXPIREQ_LOCK()		mtx_lock(&tcp_log_expireq_mtx)
#define	TCPLOG_EXPIREQ_UNLOCK()		mtx_unlock(&tcp_log_expireq_mtx)

SLIST_HEAD(tcp_log_id_head, tcp_log_id_node);

struct tcp_log_id_bucket
{
	/*
	 * tlb_id must be first. This lets us use strcmp on
	 * (struct tcp_log_id_bucket *) and (char *) interchangeably.
	 */
	char				tlb_id[TCP_LOG_ID_LEN];
	RB_ENTRY(tcp_log_id_bucket)	tlb_rb;
	struct tcp_log_id_head		tlb_head;
	struct mtx			tlb_mtx;
	volatile u_int			tlb_refcnt;
};

struct tcp_log_id_node
{
	SLIST_ENTRY(tcp_log_id_node) tln_list;
	STAILQ_ENTRY(tcp_log_id_node) tln_expireq; /* Locked by the expireq lock */
	sbintime_t		tln_expiretime;	/* Locked by the expireq lock */

	/*
	 * If INP is NULL, that means the connection has closed. We've
	 * saved the connection endpoint information and the log entries
	 * in the tln_ie and tln_entries members. We've also saved a pointer
	 * to the enclosing bucket here. If INP is not NULL, the information is
	 * in the PCB and not here.
	 */
	struct inpcb		*tln_inp;
	struct tcpcb		*tln_tp;
	struct tcp_log_id_bucket *tln_bucket;
	struct in_endpoints	tln_ie;
	struct tcp_log_stailq	tln_entries;
	int			tln_count;
	volatile int		tln_closed;
	uint8_t			tln_af;
};

enum tree_lock_state {
	TREE_UNLOCKED = 0,
	TREE_RLOCKED,
	TREE_WLOCKED,
};

/* Do we want to select this session for auto-logging? */
static __inline bool
tcp_log_selectauto(void)
{

	/*
	 * If we are doing auto-capturing, figure out whether we will capture
	 * this session.
	 */
	if (tcp_log_auto_ratio &&
	    (atomic_fetchadd_long(&tcp_log_auto_ratio_cur, 1) %
	    tcp_log_auto_ratio) == 0)
		return (true);
	return (false);
}

static __inline int
tcp_log_id_cmp(struct tcp_log_id_bucket *a, struct tcp_log_id_bucket *b)
{
	KASSERT(a != NULL, ("tcp_log_id_cmp: argument a is unexpectedly NULL"));
	KASSERT(b != NULL, ("tcp_log_id_cmp: argument b is unexpectedly NULL"));
	return strncmp(a->tlb_id, b->tlb_id, TCP_LOG_ID_LEN);
}

RB_GENERATE_STATIC(tcp_log_id_tree, tcp_log_id_bucket, tlb_rb, tcp_log_id_cmp)

static __inline void
tcp_log_id_validate_tree_lock(int tree_locked)
{

#ifdef INVARIANTS
	switch (tree_locked) {
	case TREE_WLOCKED:
		TCPID_TREE_WLOCK_ASSERT();
		break;
	case TREE_RLOCKED:
		TCPID_TREE_RLOCK_ASSERT();
		break;
	case TREE_UNLOCKED:
		TCPID_TREE_UNLOCK_ASSERT();
		break;
	default:
		kassert_panic("%s:%d: unknown tree lock state", __func__,
		    __LINE__);
	}
#endif
}

static __inline void
tcp_log_remove_bucket(struct tcp_log_id_bucket *tlb)
{

	TCPID_TREE_WLOCK_ASSERT();
	KASSERT(SLIST_EMPTY(&tlb->tlb_head),
	    ("%s: Attempt to remove non-empty bucket", __func__));
	if (RB_REMOVE(tcp_log_id_tree, &tcp_log_id_head, tlb) == NULL) {
#ifdef INVARIANTS
		kassert_panic("%s:%d: error removing element from tree",
			    __func__, __LINE__);
#endif
	}
	TCPID_BUCKET_LOCK_DESTROY(tlb);
	uma_zfree(tcp_log_bucket_zone, tlb);
}

/*
 * Call with a referenced and locked bucket.
 * Will return true if the bucket was freed; otherwise, false.
 * tlb: The bucket to unreference.
 * tree_locked: A pointer to the state of the tree lock. If the tree lock
 *    state changes, the function will update it.
 * inp: If not NULL and the function needs to drop the inp lock to relock the
 *    tree, it will do so. (The caller must ensure inp will not become invalid,
 *    probably by holding a reference to it.)
 */
static bool
tcp_log_unref_bucket(struct tcp_log_id_bucket *tlb, int *tree_locked,
    struct inpcb *inp)
{

	KASSERT(tlb != NULL, ("%s: called with NULL tlb", __func__));
	KASSERT(tree_locked != NULL, ("%s: called with NULL tree_locked",
	    __func__));

	tcp_log_id_validate_tree_lock(*tree_locked);

	/*
	 * Did we hold the last reference on the tlb? If so, we may need
	 * to free it. (Note that we can realistically only execute the
	 * loop twice: once without a write lock and once with a write
	 * lock.)
	 */
	while (TCPID_BUCKET_UNREF(tlb)) {
		/*
		 * We need a write lock on the tree to free this.
		 * If we can upgrade the tree lock, this is "easy". If we
		 * can't upgrade the tree lock, we need to do this the
		 * "hard" way: unwind all our locks and relock everything.
		 * In the meantime, anything could have changed. We even
		 * need to validate that we still need to free the bucket.
		 */
		if (*tree_locked == TREE_RLOCKED && TCPID_TREE_UPGRADE())
			*tree_locked = TREE_WLOCKED;
		else if (*tree_locked != TREE_WLOCKED) {
			TCPID_BUCKET_REF(tlb);
			if (inp != NULL)
				INP_WUNLOCK(inp);
			TCPID_BUCKET_UNLOCK(tlb);
			if (*tree_locked == TREE_RLOCKED)
				TCPID_TREE_RUNLOCK();
			TCPID_TREE_WLOCK();
			*tree_locked = TREE_WLOCKED;
			TCPID_BUCKET_LOCK(tlb);
			if (inp != NULL)
				INP_WLOCK(inp);
			continue;
		}

		/*
		 * We have an empty bucket and a write lock on the tree.
		 * Remove the empty bucket.
		 */
		tcp_log_remove_bucket(tlb);
		return (true);
	}
	return (false);
}

/*
 * Call with a locked bucket. This function will release the lock on the
 * bucket before returning.
 *
 * The caller is responsible for freeing the tp->t_lin/tln node!
 *
 * Note: one of tp or both tlb and tln must be supplied.
 *
 * inp: A pointer to the inp. If the function needs to drop the inp lock to
 *    acquire the tree write lock, it will do so. (The caller must ensure inp
 *    will not become invalid, probably by holding a reference to it.)
 * tp: A pointer to the tcpcb. (optional; if specified, tlb and tln are ignored)
 * tlb: A pointer to the bucket. (optional; ignored if tp is specified)
 * tln: A pointer to the node. (optional; ignored if tp is specified)
 * tree_locked: A pointer to the state of the tree lock. If the tree lock
 *    state changes, the function will update it.
 *
 * Will return true if the INP lock was reacquired; otherwise, false.
 */
static bool
tcp_log_remove_id_node(struct inpcb *inp, struct tcpcb *tp,
    struct tcp_log_id_bucket *tlb, struct tcp_log_id_node *tln,
    int *tree_locked)
{
	int orig_tree_locked;

	KASSERT(tp != NULL || (tlb != NULL && tln != NULL),
	    ("%s: called with tp=%p, tlb=%p, tln=%p", __func__,
	    tp, tlb, tln));
	KASSERT(tree_locked != NULL, ("%s: called with NULL tree_locked",
	    __func__));

	if (tp != NULL) {
		tlb = tp->t_lib;
		tln = tp->t_lin;
		KASSERT(tlb != NULL, ("%s: unexpectedly NULL tlb", __func__));
		KASSERT(tln != NULL, ("%s: unexpectedly NULL tln", __func__));
	}

	tcp_log_id_validate_tree_lock(*tree_locked);
	TCPID_BUCKET_LOCK_ASSERT(tlb);

	/*
	 * Remove the node, clear the log bucket and node from the TCPCB, and
	 * decrement the bucket refcount. In the process, if this is the
	 * last reference, the bucket will be freed.
	 */
	SLIST_REMOVE(&tlb->tlb_head, tln, tcp_log_id_node, tln_list);
	if (tp != NULL) {
		tp->t_lib = NULL;
		tp->t_lin = NULL;
	}
	orig_tree_locked = *tree_locked;
	if (!tcp_log_unref_bucket(tlb, tree_locked, inp))
		TCPID_BUCKET_UNLOCK(tlb);
	return (*tree_locked != orig_tree_locked);
}

#define	RECHECK_INP_CLEAN(cleanup)	do {			\
	if (inp->inp_flags & (INP_TIMEWAIT | INP_DROPPED)) {	\
		rv = ECONNRESET;				\
		cleanup;					\
		goto done;					\
	}							\
	tp = intotcpcb(inp);					\
} while (0)

#define	RECHECK_INP()	RECHECK_INP_CLEAN(/* noop */)

static void
tcp_log_grow_tlb(char *tlb_id, struct tcpcb *tp)
{

	INP_WLOCK_ASSERT(tp->t_inpcb);

#ifdef NETFLIX
	if (V_tcp_perconn_stats_enable == 2 && tp->t_stats == NULL)
		(void)tcp_stats_sample_rollthedice(tp, tlb_id, strlen(tlb_id));
#endif
}

/*
 * Set the TCP log ID for a TCPCB.
 * Called with INPCB locked. Returns with it unlocked.
 */
int
tcp_log_set_id(struct tcpcb *tp, char *id)
{
	struct tcp_log_id_bucket *tlb, *tmp_tlb;
	struct tcp_log_id_node *tln;
	struct inpcb *inp;
	int tree_locked, rv;
	bool bucket_locked;

	tlb = NULL;
	tln = NULL;
	inp = tp->t_inpcb;
	tree_locked = TREE_UNLOCKED;
	bucket_locked = false;

restart:
	INP_WLOCK_ASSERT(inp);

	/* See if the ID is unchanged. */
	if ((tp->t_lib != NULL && !strcmp(tp->t_lib->tlb_id, id)) ||
	    (tp->t_lib == NULL && *id == 0)) {
		rv = 0;
		goto done;
	}

	/*
	 * If the TCPCB had a previous ID, we need to extricate it from
	 * the previous list.
	 *
	 * Drop the TCPCB lock and lock the tree and the bucket.
	 * Because this is called in the socket context, we (theoretically)
	 * don't need to worry about the INPCB completely going away
	 * while we are gone.
	 */
	if (tp->t_lib != NULL) {
		tlb = tp->t_lib;
		TCPID_BUCKET_REF(tlb);
		INP_WUNLOCK(inp);

		if (tree_locked == TREE_UNLOCKED) {
			TCPID_TREE_RLOCK();
			tree_locked = TREE_RLOCKED;
		}
		TCPID_BUCKET_LOCK(tlb);
		bucket_locked = true;
		INP_WLOCK(inp);

		/*
		 * Unreference the bucket. If our bucket went away, it is no
		 * longer locked or valid.
		 */
		if (tcp_log_unref_bucket(tlb, &tree_locked, inp)) {
			bucket_locked = false;
			tlb = NULL;
		}

		/* Validate the INP. */
		RECHECK_INP();

		/*
		 * Evaluate whether the bucket changed while we were unlocked.
		 *
		 * Possible scenarios here:
		 * 1. Bucket is unchanged and the same one we started with.
		 * 2. The TCPCB no longer has a bucket and our bucket was
		 *    freed.
		 * 3. The TCPCB has a new bucket, whether ours was freed.
		 * 4. The TCPCB no longer has a bucket and our bucket was
		 *    not freed.
		 *
		 * In cases 2-4, we will start over. In case 1, we will
		 * proceed here to remove the bucket.
		 */
		if (tlb == NULL || tp->t_lib != tlb) {
			KASSERT(bucket_locked || tlb == NULL,
			    ("%s: bucket_locked (%d) and tlb (%p) are "
			    "inconsistent", __func__, bucket_locked, tlb));
			
			if (bucket_locked) {
				TCPID_BUCKET_UNLOCK(tlb);
				bucket_locked = false;
				tlb = NULL;
			}
			goto restart;
		}

		/*
		 * Store the (struct tcp_log_id_node) for reuse. Then, remove
		 * it from the bucket. In the process, we may end up relocking.
		 * If so, we need to validate that the INP is still valid, and
		 * the TCPCB entries match we expect.
		 *
		 * We will clear tlb and change the bucket_locked state just
		 * before calling tcp_log_remove_id_node(), since that function
		 * will unlock the bucket.
		 */
		if (tln != NULL)
			uma_zfree(tcp_log_node_zone, tln);
		tln = tp->t_lin;
		tlb = NULL;
		bucket_locked = false;
		if (tcp_log_remove_id_node(inp, tp, NULL, NULL, &tree_locked)) {
			RECHECK_INP();

			/*
			 * If the TCPCB moved to a new bucket while we had
			 * dropped the lock, restart.
			 */
			if (tp->t_lib != NULL || tp->t_lin != NULL)
				goto restart;
		}

		/*
		 * Yay! We successfully removed the TCPCB from its old
		 * bucket. Phew!
		 *
		 * On to bigger and better things...
		 */
	}

	/* At this point, the TCPCB should not be in any bucket. */
	KASSERT(tp->t_lib == NULL, ("%s: tp->t_lib is not NULL", __func__));

	/*
	 * If the new ID is not empty, we need to now assign this TCPCB to a
	 * new bucket.
	 */
	if (*id) {
		/* Get a new tln, if we don't already have one to reuse. */
		if (tln == NULL) {
			tln = uma_zalloc(tcp_log_node_zone, M_NOWAIT | M_ZERO);
			if (tln == NULL) {
				rv = ENOBUFS;
				goto done;
			}
			tln->tln_inp = inp;
			tln->tln_tp = tp;
		}

		/*
		 * Drop the INP lock for a bit. We don't need it, and dropping
		 * it prevents lock order reversals.
		 */
		INP_WUNLOCK(inp);

		/* Make sure we have at least a read lock on the tree. */
		tcp_log_id_validate_tree_lock(tree_locked);
		if (tree_locked == TREE_UNLOCKED) {
			TCPID_TREE_RLOCK();
			tree_locked = TREE_RLOCKED;
		}

refind:
		/*
		 * Remember that we constructed (struct tcp_log_id_node) so
		 * we can safely cast the id to it for the purposes of finding.
		 */
		KASSERT(tlb == NULL, ("%s:%d tlb unexpectedly non-NULL", 
		    __func__, __LINE__));
		tmp_tlb = RB_FIND(tcp_log_id_tree, &tcp_log_id_head,
		    (struct tcp_log_id_bucket *) id);

		/*
		 * If we didn't find a matching bucket, we need to add a new
		 * one. This requires a write lock. But, of course, we will
		 * need to recheck some things when we re-acquire the lock.
		 */
		if (tmp_tlb == NULL && tree_locked != TREE_WLOCKED) {
			tree_locked = TREE_WLOCKED;
			if (!TCPID_TREE_UPGRADE()) {
				TCPID_TREE_RUNLOCK();
				TCPID_TREE_WLOCK();

				/*
				 * The tree may have changed while we were
				 * unlocked.
				 */
				goto refind;
			}
		}

		/* If we need to add a new bucket, do it now. */
		if (tmp_tlb == NULL) {
			/* Allocate new bucket. */
			tlb = uma_zalloc(tcp_log_bucket_zone, M_NOWAIT);
			if (tlb == NULL) {
				rv = ENOBUFS;
				goto done_noinp;
			}

			/*
			 * Copy the ID to the bucket.
			 * NB: Don't use strlcpy() unless you are sure
			 * we've always validated NULL termination.
			 *
			 * TODO: When I'm done writing this, see if we
			 * we have correctly validated NULL termination and
			 * can use strlcpy(). :-)
			 */
			strncpy(tlb->tlb_id, id, TCP_LOG_ID_LEN - 1);
			tlb->tlb_id[TCP_LOG_ID_LEN - 1] = '\0';

			/*
			 * Take the refcount for the first node and go ahead
			 * and lock this. Note that we zero the tlb_mtx
			 * structure, since 0xdeadc0de flips the right bits
			 * for the code to think that this mutex has already
			 * been initialized. :-(
			 */
			SLIST_INIT(&tlb->tlb_head);
			refcount_init(&tlb->tlb_refcnt, 1);
			memset(&tlb->tlb_mtx, 0, sizeof(struct mtx));
			TCPID_BUCKET_LOCK_INIT(tlb);
			TCPID_BUCKET_LOCK(tlb);
			bucket_locked = true;

#define	FREE_NEW_TLB()	do {				\
	TCPID_BUCKET_LOCK_DESTROY(tlb);			\
	uma_zfree(tcp_log_bucket_zone, tlb);		\
	bucket_locked = false;				\
	tlb = NULL;					\
} while (0)
			/*
			 * Relock the INP and make sure we are still
			 * unassigned.
			 */
			INP_WLOCK(inp);
			RECHECK_INP_CLEAN(FREE_NEW_TLB());
			if (tp->t_lib != NULL) {
				FREE_NEW_TLB();
				goto restart;
			}

			/* Add the new bucket to the tree. */
			tmp_tlb = RB_INSERT(tcp_log_id_tree, &tcp_log_id_head,
			    tlb);
			KASSERT(tmp_tlb == NULL,
			    ("%s: Unexpected conflicting bucket (%p) while "
			    "adding new bucket (%p)", __func__, tmp_tlb, tlb));

			/*
			 * If we found a conflicting bucket, free the new
			 * one we made and fall through to use the existing
			 * bucket.
			 */
			if (tmp_tlb != NULL) {
				FREE_NEW_TLB();
				INP_WUNLOCK(inp);
			}
#undef	FREE_NEW_TLB
		}

		/* If we found an existing bucket, use it. */
		if (tmp_tlb != NULL) {
			tlb = tmp_tlb;
			TCPID_BUCKET_LOCK(tlb);
			bucket_locked = true;

			/*
			 * Relock the INP and make sure we are still
			 * unassigned.
			 */
			INP_UNLOCK_ASSERT(inp);
			INP_WLOCK(inp);
			RECHECK_INP();
			if (tp->t_lib != NULL) {
				TCPID_BUCKET_UNLOCK(tlb);
				tlb = NULL;
				goto restart;
			}

			/* Take a reference on the bucket. */
			TCPID_BUCKET_REF(tlb);
		}

		tcp_log_grow_tlb(tlb->tlb_id, tp);

		/* Add the new node to the list. */
		SLIST_INSERT_HEAD(&tlb->tlb_head, tln, tln_list);
		tp->t_lib = tlb;
		tp->t_lin = tln;
		tln = NULL;
	}

	rv = 0;

done:
	/* Unlock things, as needed, and return. */
	INP_WUNLOCK(inp);
done_noinp:
	INP_UNLOCK_ASSERT(inp);
	if (bucket_locked) {
		TCPID_BUCKET_LOCK_ASSERT(tlb);
		TCPID_BUCKET_UNLOCK(tlb);
	} else if (tlb != NULL)
		TCPID_BUCKET_UNLOCK_ASSERT(tlb);
	if (tree_locked == TREE_WLOCKED) {
		TCPID_TREE_WLOCK_ASSERT();
		TCPID_TREE_WUNLOCK();
	} else if (tree_locked == TREE_RLOCKED) {
		TCPID_TREE_RLOCK_ASSERT();
		TCPID_TREE_RUNLOCK();
	} else
		TCPID_TREE_UNLOCK_ASSERT();
	if (tln != NULL)
		uma_zfree(tcp_log_node_zone, tln);
	return (rv);
}

/*
 * Get the TCP log ID for a TCPCB.
 * Called with INPCB locked.
 * 'buf' must point to a buffer that is at least TCP_LOG_ID_LEN bytes long.
 * Returns number of bytes copied.
 */
size_t
tcp_log_get_id(struct tcpcb *tp, char *buf)
{
	size_t len;

	INP_LOCK_ASSERT(tp->t_inpcb);
	if (tp->t_lib != NULL) {
		len = strlcpy(buf, tp->t_lib->tlb_id, TCP_LOG_ID_LEN);
		KASSERT(len < TCP_LOG_ID_LEN,
		    ("%s:%d: tp->t_lib->tlb_id too long (%zu)",
		    __func__, __LINE__, len));
	} else {
		*buf = '\0';
		len = 0;
	}
	return (len);
}

/*
 * Get number of connections with the same log ID.
 * Log ID is taken from given TCPCB.
 * Called with INPCB locked.
 */
u_int
tcp_log_get_id_cnt(struct tcpcb *tp)
{

	INP_WLOCK_ASSERT(tp->t_inpcb);
	return ((tp->t_lib == NULL) ? 0 : tp->t_lib->tlb_refcnt);
}

#ifdef TCPLOG_DEBUG_RINGBUF
/*
 * Functions/macros to increment/decrement reference count for a log
 * entry. This should catch when we do a double-free/double-remove or
 * a double-add.
 */
static inline void
_tcp_log_entry_refcnt_add(struct tcp_log_mem *log_entry, const char *func,
    int line)
{
	int refcnt;

	refcnt = atomic_fetchadd_int(&log_entry->tlm_refcnt, 1);
	if (refcnt != 0)
		panic("%s:%d: log_entry(%p)->tlm_refcnt is %d (expected 0)",
		    func, line, log_entry, refcnt);
}
#define	tcp_log_entry_refcnt_add(l)	\
    _tcp_log_entry_refcnt_add((l), __func__, __LINE__)

static inline void
_tcp_log_entry_refcnt_rem(struct tcp_log_mem *log_entry, const char *func,
    int line)
{
	int refcnt;

	refcnt = atomic_fetchadd_int(&log_entry->tlm_refcnt, -1);
	if (refcnt != 1)
		panic("%s:%d: log_entry(%p)->tlm_refcnt is %d (expected 1)",
		    func, line, log_entry, refcnt);
}
#define	tcp_log_entry_refcnt_rem(l)	\
    _tcp_log_entry_refcnt_rem((l), __func__, __LINE__)

#else /* !TCPLOG_DEBUG_RINGBUF */

#define	tcp_log_entry_refcnt_add(l)
#define	tcp_log_entry_refcnt_rem(l)

#endif

/*
 * Cleanup after removing a log entry, but only decrement the count if we
 * are running INVARIANTS.
 */
static inline void
tcp_log_free_log_common(struct tcp_log_mem *log_entry, int *count __unused)
{

	uma_zfree(tcp_log_zone, log_entry);
#ifdef INVARIANTS
	(*count)--;
	KASSERT(*count >= 0,
	    ("%s: count unexpectedly negative", __func__));
#endif
}

static void
tcp_log_free_entries(struct tcp_log_stailq *head, int *count)
{
	struct tcp_log_mem *log_entry;

	/* Free the entries. */
	while ((log_entry = STAILQ_FIRST(head)) != NULL) {
		STAILQ_REMOVE_HEAD(head, tlm_queue);
		tcp_log_entry_refcnt_rem(log_entry);
		tcp_log_free_log_common(log_entry, count);
	}
}

/* Cleanup after removing a log entry. */
static inline void
tcp_log_remove_log_cleanup(struct tcpcb *tp, struct tcp_log_mem *log_entry)
{
	uma_zfree(tcp_log_zone, log_entry);
	tp->t_lognum--;
	KASSERT(tp->t_lognum >= 0,
	    ("%s: tp->t_lognum unexpectedly negative", __func__));
}

/* Remove a log entry from the head of a list. */
static inline void
tcp_log_remove_log_head(struct tcpcb *tp, struct tcp_log_mem *log_entry)
{

	KASSERT(log_entry == STAILQ_FIRST(&tp->t_logs),
	    ("%s: attempt to remove non-HEAD log entry", __func__));
	STAILQ_REMOVE_HEAD(&tp->t_logs, tlm_queue);
	tcp_log_entry_refcnt_rem(log_entry);
	tcp_log_remove_log_cleanup(tp, log_entry);
}

#ifdef TCPLOG_DEBUG_RINGBUF
/*
 * Initialize the log entry's reference count, which we want to
 * survive allocations.
 */
static int
tcp_log_zone_init(void *mem, int size, int flags __unused)
{
	struct tcp_log_mem *tlm;

	KASSERT(size >= sizeof(struct tcp_log_mem),
	    ("%s: unexpectedly short (%d) allocation", __func__, size));
	tlm = (struct tcp_log_mem *)mem;
	tlm->tlm_refcnt = 0;
	return (0);
}

/*
 * Double check that the refcnt is zero on allocation and return.
 */
static int
tcp_log_zone_ctor(void *mem, int size, void *args __unused, int flags __unused)
{
	struct tcp_log_mem *tlm;

	KASSERT(size >= sizeof(struct tcp_log_mem),
	    ("%s: unexpectedly short (%d) allocation", __func__, size));
	tlm = (struct tcp_log_mem *)mem;
	if (tlm->tlm_refcnt != 0)
		panic("%s:%d: tlm(%p)->tlm_refcnt is %d (expected 0)",
		    __func__, __LINE__, tlm, tlm->tlm_refcnt);
	return (0);
}

static void
tcp_log_zone_dtor(void *mem, int size, void *args __unused)
{
	struct tcp_log_mem *tlm;

	KASSERT(size >= sizeof(struct tcp_log_mem),
	    ("%s: unexpectedly short (%d) allocation", __func__, size));
	tlm = (struct tcp_log_mem *)mem;
	if (tlm->tlm_refcnt != 0)
		panic("%s:%d: tlm(%p)->tlm_refcnt is %d (expected 0)",
		    __func__, __LINE__, tlm, tlm->tlm_refcnt);
}
#endif /* TCPLOG_DEBUG_RINGBUF */

/* Do global initialization. */
void
tcp_log_init(void)
{

	tcp_log_zone = uma_zcreate("tcp_log", sizeof(struct tcp_log_mem),
#ifdef TCPLOG_DEBUG_RINGBUF
	    tcp_log_zone_ctor, tcp_log_zone_dtor, tcp_log_zone_init,
#else
	    NULL, NULL, NULL,
#endif
	    NULL, UMA_ALIGN_PTR, 0);
	(void)uma_zone_set_max(tcp_log_zone, TCP_LOG_BUF_DEFAULT_GLOBAL_LIMIT);
	tcp_log_bucket_zone = uma_zcreate("tcp_log_bucket",
	    sizeof(struct tcp_log_id_bucket), NULL, NULL, NULL, NULL,
	    UMA_ALIGN_PTR, 0);
	tcp_log_node_zone = uma_zcreate("tcp_log_node",
	    sizeof(struct tcp_log_id_node), NULL, NULL, NULL, NULL,
	    UMA_ALIGN_PTR, 0);
#ifdef TCPLOG_DEBUG_COUNTERS
	tcp_log_queued = counter_u64_alloc(M_WAITOK);
	tcp_log_que_fail1 = counter_u64_alloc(M_WAITOK);
	tcp_log_que_fail2 = counter_u64_alloc(M_WAITOK);
	tcp_log_que_fail3 = counter_u64_alloc(M_WAITOK);
	tcp_log_que_fail4 = counter_u64_alloc(M_WAITOK);
	tcp_log_que_fail5 = counter_u64_alloc(M_WAITOK);
	tcp_log_que_copyout = counter_u64_alloc(M_WAITOK);
	tcp_log_que_read = counter_u64_alloc(M_WAITOK);
	tcp_log_que_freed = counter_u64_alloc(M_WAITOK);
#endif

	rw_init_flags(&tcp_id_tree_lock, "TCP ID tree", RW_NEW);
	mtx_init(&tcp_log_expireq_mtx, "TCP log expireq", NULL, MTX_DEF);
	callout_init(&tcp_log_expireq_callout, 1);
}

/* Do per-TCPCB initialization. */
void
tcp_log_tcpcbinit(struct tcpcb *tp)
{

	/* A new TCPCB should start out zero-initialized. */
	STAILQ_INIT(&tp->t_logs);

	/*
	 * If we are doing auto-capturing, figure out whether we will capture
	 * this session.
	 */
	if (tcp_log_selectauto()) {
		tp->t_logstate = tcp_log_auto_mode;
		tp->t_flags2 |= TF2_LOG_AUTO;
	}
}


/* Remove entries */
static void
tcp_log_expire(void *unused __unused)
{
	struct tcp_log_id_bucket *tlb;
	struct tcp_log_id_node *tln;
	sbintime_t expiry_limit;
	int tree_locked;

	TCPLOG_EXPIREQ_LOCK();
	if (callout_pending(&tcp_log_expireq_callout)) {
		/* Callout was reset. */
		TCPLOG_EXPIREQ_UNLOCK();
		return;
	}

	/*
	 * Process entries until we reach one that expires too far in the
	 * future. Look one second in the future.
	 */
	expiry_limit = getsbinuptime() + SBT_1S;
	tree_locked = TREE_UNLOCKED;

	while ((tln = STAILQ_FIRST(&tcp_log_expireq_head)) != NULL &&
	    tln->tln_expiretime <= expiry_limit) {
		if (!callout_active(&tcp_log_expireq_callout)) {
			/*
			 * Callout was stopped. I guess we should
			 * just quit at this point.
			 */
			TCPLOG_EXPIREQ_UNLOCK();
			return;
		}

		/*
		 * Remove the node from the head of the list and unlock
		 * the list. Change the expiry time to SBT_MAX as a signal
		 * to other threads that we now own this.
		 */
		STAILQ_REMOVE_HEAD(&tcp_log_expireq_head, tln_expireq);
		tln->tln_expiretime = SBT_MAX;
		TCPLOG_EXPIREQ_UNLOCK();

		/*
		 * Remove the node from the bucket.
		 */
		tlb = tln->tln_bucket;
		TCPID_BUCKET_LOCK(tlb);
		if (tcp_log_remove_id_node(NULL, NULL, tlb, tln, &tree_locked)) {
			tcp_log_id_validate_tree_lock(tree_locked);
			if (tree_locked == TREE_WLOCKED)
				TCPID_TREE_WUNLOCK();
			else
				TCPID_TREE_RUNLOCK();
			tree_locked = TREE_UNLOCKED;
		}

		/* Drop the INP reference. */
		INP_WLOCK(tln->tln_inp);
		if (!in_pcbrele_wlocked(tln->tln_inp))
			INP_WUNLOCK(tln->tln_inp);

		/* Free the log records. */
		tcp_log_free_entries(&tln->tln_entries, &tln->tln_count);

		/* Free the node. */
		uma_zfree(tcp_log_node_zone, tln);

		/* Relock the expiry queue. */
		TCPLOG_EXPIREQ_LOCK();
	}

	/*
	 * We've expired all the entries we can. Do we need to reschedule
	 * ourselves?
	 */
	callout_deactivate(&tcp_log_expireq_callout);
	if (tln != NULL) {
		/*
		 * Get max(now + TCP_LOG_EXPIRE_INTVL, tln->tln_expiretime) and
		 * set the next callout to that. (This helps ensure we generally
		 * run the callout no more often than desired.)
		 */
		expiry_limit = getsbinuptime() + TCP_LOG_EXPIRE_INTVL;
		if (expiry_limit < tln->tln_expiretime)
			expiry_limit = tln->tln_expiretime;
		callout_reset_sbt(&tcp_log_expireq_callout, expiry_limit,
		    SBT_1S, tcp_log_expire, NULL, C_ABSOLUTE);
	}

	/* We're done. */
	TCPLOG_EXPIREQ_UNLOCK();
	return;
}

/*
 * Move log data from the TCPCB to a new node. This will reset the TCPCB log
 * entries and log count; however, it will not touch other things from the
 * TCPCB (e.g. t_lin, t_lib).
 *
 * NOTE: Must hold a lock on the INP.
 */
static void
tcp_log_move_tp_to_node(struct tcpcb *tp, struct tcp_log_id_node *tln)
{

	INP_WLOCK_ASSERT(tp->t_inpcb);

	tln->tln_ie = tp->t_inpcb->inp_inc.inc_ie;
	if (tp->t_inpcb->inp_inc.inc_flags & INC_ISIPV6)
		tln->tln_af = AF_INET6;
	else
		tln->tln_af = AF_INET;
	tln->tln_entries = tp->t_logs;
	tln->tln_count = tp->t_lognum;
	tln->tln_bucket = tp->t_lib;

	/* Clear information from the PCB. */
	STAILQ_INIT(&tp->t_logs);
	tp->t_lognum = 0;
}

/* Do per-TCPCB cleanup */
void
tcp_log_tcpcbfini(struct tcpcb *tp)
{
	struct tcp_log_id_node *tln, *tln_first;
	struct tcp_log_mem *log_entry;
	sbintime_t callouttime;

	INP_WLOCK_ASSERT(tp->t_inpcb);

	/*
	 * If we were gathering packets to be automatically dumped, try to do
	 * it now. If this succeeds, the log information in the TCPCB will be
	 * cleared. Otherwise, we'll handle the log information as we do
	 * for other states.
	 */
	switch(tp->t_logstate) {
	case TCP_LOG_STATE_HEAD_AUTO:
		(void)tcp_log_dump_tp_logbuf(tp, "auto-dumped from head",
		    M_NOWAIT, false);
		break;
	case TCP_LOG_STATE_TAIL_AUTO:
		(void)tcp_log_dump_tp_logbuf(tp, "auto-dumped from tail",
		    M_NOWAIT, false);
		break;
	case TCP_LOG_STATE_CONTINUAL:
		(void)tcp_log_dump_tp_logbuf(tp, "auto-dumped from continual",
		    M_NOWAIT, false);
		break;
	}

	/*
	 * There are two ways we could keep logs: per-socket or per-ID. If
	 * we are tracking logs with an ID, then the logs survive the
	 * destruction of the TCPCB.
	 * 
	 * If the TCPCB is associated with an ID node, move the logs from the
	 * TCPCB to the ID node. In theory, this is safe, for reasons which I
	 * will now explain for my own benefit when I next need to figure out
	 * this code. :-)
	 *
	 * We own the INP lock. Therefore, no one else can change the contents
	 * of this node (Rule C). Further, no one can remove this node from
	 * the bucket while we hold the lock (Rule D). Basically, no one can
	 * mess with this node. That leaves two states in which we could be:
	 * 
	 * 1. Another thread is currently waiting to acquire the INP lock, with
	 *    plans to do something with this node. When we drop the INP lock,
	 *    they will have a chance to do that. They will recheck the
	 *    tln_closed field (see note to Rule C) and then acquire the
	 *    bucket lock before proceeding further.
	 *
	 * 2. Another thread will try to acquire a lock at some point in the
	 *    future. If they try to acquire a lock before we set the
	 *    tln_closed field, they will follow state #1. If they try to
	 *    acquire a lock after we set the tln_closed field, they will be
	 *    able to make changes to the node, at will, following Rule C.
	 *
	 * Therefore, we currently own this node and can make any changes
	 * we want. But, as soon as we set the tln_closed field to true, we
	 * have effectively dropped our lock on the node. (For this reason, we
	 * also need to make sure our writes are ordered correctly. An atomic
	 * operation with "release" semantics should be sufficient.)
	 */

	if (tp->t_lin != NULL) {
		/* Copy the relevant information to the log entry. */
		tln = tp->t_lin;
		KASSERT(tln->tln_inp == tp->t_inpcb,
		    ("%s: Mismatched inp (tln->tln_inp=%p, tp->t_inpcb=%p)",
		    __func__, tln->tln_inp, tp->t_inpcb));
		tcp_log_move_tp_to_node(tp, tln);

		/* Clear information from the PCB. */
		tp->t_lin = NULL;
		tp->t_lib = NULL;

		/*
		 * Take a reference on the INP. This ensures that the INP
		 * remains valid while the node is on the expiry queue. This
		 * ensures the INP is valid for other threads that may be
		 * racing to lock this node when we move it to the expire
		 * queue.
		 */
		in_pcbref(tp->t_inpcb);

		/*
		 * Store the entry on the expiry list. The exact behavior
		 * depends on whether we have entries to keep. If so, we
		 * put the entry at the tail of the list and expire in
		 * TCP_LOG_EXPIRE_TIME. Otherwise, we expire "now" and put
		 * the entry at the head of the list. (Handling the cleanup
		 * via the expiry timer lets us avoid locking messy-ness here.)
		 */
		tln->tln_expiretime = getsbinuptime();
		TCPLOG_EXPIREQ_LOCK();
		if (tln->tln_count) {
			tln->tln_expiretime += TCP_LOG_EXPIRE_TIME;
			if (STAILQ_EMPTY(&tcp_log_expireq_head) &&
			    !callout_active(&tcp_log_expireq_callout)) {
				/*
				 * We are adding the first entry and a callout
				 * is not currently scheduled; therefore, we
				 * need to schedule one.
				 */
				callout_reset_sbt(&tcp_log_expireq_callout,
				    tln->tln_expiretime, SBT_1S, tcp_log_expire,
				    NULL, C_ABSOLUTE);
			}
			STAILQ_INSERT_TAIL(&tcp_log_expireq_head, tln,
			    tln_expireq);
		} else {
			callouttime = tln->tln_expiretime +
			    TCP_LOG_EXPIRE_INTVL;
			tln_first = STAILQ_FIRST(&tcp_log_expireq_head);

			if ((tln_first == NULL ||
			    callouttime < tln_first->tln_expiretime) &&
			    (callout_pending(&tcp_log_expireq_callout) ||
			    !callout_active(&tcp_log_expireq_callout))) {
				/*
				 * The list is empty, or we want to run the
				 * expire code before the first entry's timer
				 * fires. Also, we are in a case where a callout
				 * is not actively running. We want to reset
				 * the callout to occur sooner.
				 */
				callout_reset_sbt(&tcp_log_expireq_callout,
				    callouttime, SBT_1S, tcp_log_expire, NULL,
				    C_ABSOLUTE);
			}

			/*
			 * Insert to the head, or just after the head, as
			 * appropriate. (This might result in small
			 * mis-orderings as a bunch of "expire now" entries
			 * gather at the start of the list, but that should
			 * not produce big problems, since the expire timer
			 * will walk through all of them.)
			 */
			if (tln_first == NULL ||
			    tln->tln_expiretime < tln_first->tln_expiretime)
				STAILQ_INSERT_HEAD(&tcp_log_expireq_head, tln,
				    tln_expireq);
			else
				STAILQ_INSERT_AFTER(&tcp_log_expireq_head,
				    tln_first, tln, tln_expireq);
		}
		TCPLOG_EXPIREQ_UNLOCK();

		/*
		 * We are done messing with the tln. After this point, we
		 * can't touch it. (Note that the "release" semantics should
		 * be included with the TCPLOG_EXPIREQ_UNLOCK() call above.
		 * Therefore, they should be unnecessary here. However, it
		 * seems like a good idea to include them anyway, since we
		 * really are releasing a lock here.)
		 */
		atomic_store_rel_int(&tln->tln_closed, 1);
	} else {
		/* Remove log entries. */
		while ((log_entry = STAILQ_FIRST(&tp->t_logs)) != NULL)
			tcp_log_remove_log_head(tp, log_entry);
		KASSERT(tp->t_lognum == 0,
		    ("%s: After freeing entries, tp->t_lognum=%d (expected 0)",
			__func__, tp->t_lognum));
	}

	/*
	 * Change the log state to off (just in case anything tries to sneak
	 * in a last-minute log).
	 */
	tp->t_logstate = TCP_LOG_STATE_OFF;
}

/*
 * This logs an event for a TCP socket. Normally, this is called via
 * TCP_LOG_EVENT or TCP_LOG_EVENT_VERBOSE. See the documentation for
 * TCP_LOG_EVENT().
 */

struct tcp_log_buffer *
tcp_log_event_(struct tcpcb *tp, struct tcphdr *th, struct sockbuf *rxbuf,
    struct sockbuf *txbuf, uint8_t eventid, int errornum, uint32_t len,
    union tcp_log_stackspecific *stackinfo, int th_hostorder,
    const char *output_caller, const char *func, int line, const struct timeval *itv)
{
	struct tcp_log_mem *log_entry;
	struct tcp_log_buffer *log_buf;
	int attempt_count = 0;
	struct tcp_log_verbose *log_verbose;
	uint32_t logsn;

	KASSERT((func == NULL && line == 0) || (func != NULL && line > 0),
	    ("%s called with inconsistent func (%p) and line (%d) arguments",
		__func__, func, line));

	INP_WLOCK_ASSERT(tp->t_inpcb);

	KASSERT(tp->t_logstate == TCP_LOG_STATE_HEAD ||
	    tp->t_logstate == TCP_LOG_STATE_TAIL ||
	    tp->t_logstate == TCP_LOG_STATE_CONTINUAL ||
	    tp->t_logstate == TCP_LOG_STATE_HEAD_AUTO ||
	    tp->t_logstate == TCP_LOG_STATE_TAIL_AUTO,
	    ("%s called with unexpected tp->t_logstate (%d)", __func__,
		tp->t_logstate));

	/*
	 * Get the serial number. We do this early so it will
	 * increment even if we end up skipping the log entry for some
	 * reason.
	 */
	logsn = tp->t_logsn++;

	/*
	 * Can we get a new log entry? If so, increment the lognum counter
	 * here.
	 */
retry:
	if (tp->t_lognum < tcp_log_session_limit) {
		if ((log_entry = uma_zalloc(tcp_log_zone, M_NOWAIT)) != NULL)
			tp->t_lognum++;
	} else
		log_entry = NULL;

	/* Do we need to try to reuse? */
	if (log_entry == NULL) {
		/*
		 * Sacrifice auto-logged sessions without a log ID if
		 * tcp_log_auto_all is false. (If they don't have a log
		 * ID by now, it is probable that either they won't get one
		 * or we are resource-constrained.)
		 */
		if (tp->t_lib == NULL && (tp->t_flags2 & TF2_LOG_AUTO) &&
		    !tcp_log_auto_all) {
			if (tcp_log_state_change(tp, TCP_LOG_STATE_CLEAR)) {
#ifdef INVARIANTS
				panic("%s:%d: tcp_log_state_change() failed "
				    "to set tp %p to TCP_LOG_STATE_CLEAR",
				    __func__, __LINE__, tp);
#endif
				tp->t_logstate = TCP_LOG_STATE_OFF;
			}
			return (NULL);
		}
		/*
		 * If we are in TCP_LOG_STATE_HEAD_AUTO state, try to dump
		 * the buffers. If successful, deactivate tracing. Otherwise,
		 * leave it active so we will retry.
		 */
		if (tp->t_logstate == TCP_LOG_STATE_HEAD_AUTO &&
		    !tcp_log_dump_tp_logbuf(tp, "auto-dumped from head",
		    M_NOWAIT, false)) {
			tp->t_logstate = TCP_LOG_STATE_OFF;
			return(NULL);
		} else if ((tp->t_logstate == TCP_LOG_STATE_CONTINUAL) &&
		    !tcp_log_dump_tp_logbuf(tp, "auto-dumped from continual",
		    M_NOWAIT, false)) {
			if (attempt_count == 0) {
				attempt_count++;
				goto retry;
			}
#ifdef TCPLOG_DEBUG_COUNTERS
			counter_u64_add(tcp_log_que_fail4, 1);
#endif
			return(NULL);
		} else if (tp->t_logstate == TCP_LOG_STATE_HEAD_AUTO)
			return(NULL);

		/* If in HEAD state, just deactivate the tracing and return. */
		if (tp->t_logstate == TCP_LOG_STATE_HEAD) {
			tp->t_logstate = TCP_LOG_STATE_OFF;
			return(NULL);
		}

		/*
		 * Get a buffer to reuse. If that fails, just give up.
		 * (We can't log anything without a buffer in which to
		 * put it.)
		 *
		 * Note that we don't change the t_lognum counter
		 * here. Because we are re-using the buffer, the total
		 * number won't change.
		 */
		if ((log_entry = STAILQ_FIRST(&tp->t_logs)) == NULL)
			return(NULL);
		STAILQ_REMOVE_HEAD(&tp->t_logs, tlm_queue);
		tcp_log_entry_refcnt_rem(log_entry);
	}

	KASSERT(log_entry != NULL,
	    ("%s: log_entry unexpectedly NULL", __func__));

	/* Extract the log buffer and verbose buffer pointers. */
	log_buf = &log_entry->tlm_buf;
	log_verbose = &log_entry->tlm_v;

	/* Basic entries. */
	if (itv == NULL)
		getmicrouptime(&log_buf->tlb_tv);
	else
		memcpy(&log_buf->tlb_tv, itv, sizeof(struct timeval));
	log_buf->tlb_ticks = ticks;
	log_buf->tlb_sn = logsn;
	log_buf->tlb_stackid = tp->t_fb->tfb_id;
	log_buf->tlb_eventid = eventid;
	log_buf->tlb_eventflags = 0;
	log_buf->tlb_errno = errornum;

	/* Socket buffers */
	if (rxbuf != NULL) {
		log_buf->tlb_eventflags |= TLB_FLAG_RXBUF;
		log_buf->tlb_rxbuf.tls_sb_acc = rxbuf->sb_acc;
		log_buf->tlb_rxbuf.tls_sb_ccc = rxbuf->sb_ccc;
		log_buf->tlb_rxbuf.tls_sb_spare = 0;
	}
	if (txbuf != NULL) {
		log_buf->tlb_eventflags |= TLB_FLAG_TXBUF;
		log_buf->tlb_txbuf.tls_sb_acc = txbuf->sb_acc;
		log_buf->tlb_txbuf.tls_sb_ccc = txbuf->sb_ccc;
		log_buf->tlb_txbuf.tls_sb_spare = 0;
	}
	/* Copy values from tp to the log entry. */
#define	COPY_STAT(f)	log_buf->tlb_ ## f = tp->f
#define	COPY_STAT_T(f)	log_buf->tlb_ ## f = tp->t_ ## f
	COPY_STAT_T(state);
	COPY_STAT_T(starttime);
	COPY_STAT(iss);
	COPY_STAT_T(flags);
	COPY_STAT(snd_una);
	COPY_STAT(snd_max);
	COPY_STAT(snd_cwnd);
	COPY_STAT(snd_nxt);
	COPY_STAT(snd_recover);
	COPY_STAT(snd_wnd);
	COPY_STAT(snd_ssthresh);
	COPY_STAT_T(srtt);
	COPY_STAT_T(rttvar);
	COPY_STAT(rcv_up);
	COPY_STAT(rcv_adv);
	COPY_STAT(rcv_nxt);
	COPY_STAT(sack_newdata);
	COPY_STAT(rcv_wnd);
	COPY_STAT_T(dupacks);
	COPY_STAT_T(segqlen);
	COPY_STAT(snd_numholes);
	COPY_STAT(snd_scale);
	COPY_STAT(rcv_scale);
#undef COPY_STAT
#undef COPY_STAT_T
	log_buf->tlb_flex1 = 0;
	log_buf->tlb_flex2 = 0;
	/* Copy stack-specific info. */
	if (stackinfo != NULL) {
		memcpy(&log_buf->tlb_stackinfo, stackinfo,
		    sizeof(log_buf->tlb_stackinfo));
		log_buf->tlb_eventflags |= TLB_FLAG_STACKINFO;
	}

	/* The packet */
	log_buf->tlb_len = len;
	if (th) {
		int optlen;

		log_buf->tlb_eventflags |= TLB_FLAG_HDR;
		log_buf->tlb_th = *th;
		if (th_hostorder)
			tcp_fields_to_net(&log_buf->tlb_th);
		optlen = (th->th_off << 2) - sizeof (struct tcphdr);
		if (optlen > 0)
			memcpy(log_buf->tlb_opts, th + 1, optlen);
	}

	/* Verbose information */
	if (func != NULL) {
		log_buf->tlb_eventflags |= TLB_FLAG_VERBOSE;
		if (output_caller != NULL)
			strlcpy(log_verbose->tlv_snd_frm, output_caller,
			    TCP_FUNC_LEN);
		else
			*log_verbose->tlv_snd_frm = 0;
		strlcpy(log_verbose->tlv_trace_func, func, TCP_FUNC_LEN);
		log_verbose->tlv_trace_line = line;
	}

	/* Insert the new log at the tail. */
	STAILQ_INSERT_TAIL(&tp->t_logs, log_entry, tlm_queue);
	tcp_log_entry_refcnt_add(log_entry);
	return (log_buf);
}

/*
 * Change the logging state for a TCPCB. Returns 0 on success or an
 * error code on failure.
 */
int
tcp_log_state_change(struct tcpcb *tp, int state)
{
	struct tcp_log_mem *log_entry;

	INP_WLOCK_ASSERT(tp->t_inpcb);
	switch(state) {
	case TCP_LOG_STATE_CLEAR:
		while ((log_entry = STAILQ_FIRST(&tp->t_logs)) != NULL)
			tcp_log_remove_log_head(tp, log_entry);
		/* Fall through */

	case TCP_LOG_STATE_OFF:
		tp->t_logstate = TCP_LOG_STATE_OFF;
		break;

	case TCP_LOG_STATE_TAIL:
	case TCP_LOG_STATE_HEAD:
	case TCP_LOG_STATE_CONTINUAL:
	case TCP_LOG_STATE_HEAD_AUTO:
	case TCP_LOG_STATE_TAIL_AUTO:
		tp->t_logstate = state;
		break;

	default:
		return (EINVAL);
	}

	tp->t_flags2 &= ~(TF2_LOG_AUTO);

	return (0);
}

/* If tcp_drain() is called, flush half the log entries. */
void
tcp_log_drain(struct tcpcb *tp)
{
	struct tcp_log_mem *log_entry, *next;
	int target, skip;

	INP_WLOCK_ASSERT(tp->t_inpcb);
	if ((target = tp->t_lognum / 2) == 0)
		return;

	/*
	 * If we are logging the "head" packets, we want to discard
	 * from the tail of the queue. Otherwise, we want to discard
	 * from the head.
	 */
	if (tp->t_logstate == TCP_LOG_STATE_HEAD ||
	    tp->t_logstate == TCP_LOG_STATE_HEAD_AUTO) {
		skip = tp->t_lognum - target;
		STAILQ_FOREACH(log_entry, &tp->t_logs, tlm_queue)
			if (!--skip)
				break;
		KASSERT(log_entry != NULL,
		    ("%s: skipped through all entries!", __func__));
		if (log_entry == NULL)
			return;
		while ((next = STAILQ_NEXT(log_entry, tlm_queue)) != NULL) {
			STAILQ_REMOVE_AFTER(&tp->t_logs, log_entry, tlm_queue);
			tcp_log_entry_refcnt_rem(next);
			tcp_log_remove_log_cleanup(tp, next);
#ifdef INVARIANTS
			target--;
#endif
		}
		KASSERT(target == 0,
		    ("%s: After removing from tail, target was %d", __func__,
			target));
	} else if (tp->t_logstate == TCP_LOG_STATE_CONTINUAL) {
		(void)tcp_log_dump_tp_logbuf(tp, "auto-dumped from continual",
		    M_NOWAIT, false);
	} else {
		while ((log_entry = STAILQ_FIRST(&tp->t_logs)) != NULL &&
		    target--)
			tcp_log_remove_log_head(tp, log_entry);
		KASSERT(target <= 0,
		    ("%s: After removing from head, target was %d", __func__,
			target));
		KASSERT(tp->t_lognum > 0,
		    ("%s: After removing from head, tp->t_lognum was %d",
			__func__, target));
		KASSERT(log_entry != NULL,
		    ("%s: After removing from head, the tailq was empty",
			__func__));
	}
}

static inline int
tcp_log_copyout(struct sockopt *sopt, void *src, void *dst, size_t len)
{

	if (sopt->sopt_td != NULL)
		return (copyout(src, dst, len));
	bcopy(src, dst, len);
	return (0);
}

static int
tcp_log_logs_to_buf(struct sockopt *sopt, struct tcp_log_stailq *log_tailqp,
    struct tcp_log_buffer **end, int count)
{
	struct tcp_log_buffer *out_entry;
	struct tcp_log_mem *log_entry;
	size_t entrysize;
	int error;
#ifdef INVARIANTS
	int orig_count = count;
#endif

	/* Copy the data out. */
	error = 0;
	out_entry = (struct tcp_log_buffer *) sopt->sopt_val;
	STAILQ_FOREACH(log_entry, log_tailqp, tlm_queue) {
		count--;
		KASSERT(count >= 0,
		    ("%s:%d: Exceeded expected count (%d) processing list %p",
		    __func__, __LINE__, orig_count, log_tailqp));

#ifdef TCPLOG_DEBUG_COUNTERS
		counter_u64_add(tcp_log_que_copyout, 1);
#endif

		/*
		 * Skip copying out the header if it isn't present.
		 * Instead, copy out zeros (to ensure we don't leak info).
		 * TODO: Make sure we truly do zero everything we don't
		 * explicitly set.
		 */
		if (log_entry->tlm_buf.tlb_eventflags & TLB_FLAG_HDR)
			entrysize = sizeof(struct tcp_log_buffer);
		else
			entrysize = offsetof(struct tcp_log_buffer, tlb_th);
		error = tcp_log_copyout(sopt, &log_entry->tlm_buf, out_entry,
		    entrysize);
		if (error)
			break;
		if (!(log_entry->tlm_buf.tlb_eventflags & TLB_FLAG_HDR)) {
			error = tcp_log_copyout(sopt, zerobuf,
			    ((uint8_t *)out_entry) + entrysize,
			    sizeof(struct tcp_log_buffer) - entrysize);
		}

		/*
		 * Copy out the verbose bit, if needed. Either way,
		 * increment the output pointer the correct amount.
		 */
		if (log_entry->tlm_buf.tlb_eventflags & TLB_FLAG_VERBOSE) {
			error = tcp_log_copyout(sopt, &log_entry->tlm_v,
			    out_entry->tlb_verbose,
			    sizeof(struct tcp_log_verbose));
			if (error)
				break;
			out_entry = (struct tcp_log_buffer *)
			    (((uint8_t *) (out_entry + 1)) +
			    sizeof(struct tcp_log_verbose));
		} else
			out_entry++;
	}
	*end = out_entry;
	KASSERT(error || count == 0,
	    ("%s:%d: Less than expected count (%d) processing list %p"
	    " (%d remain)", __func__, __LINE__, orig_count,
	    log_tailqp, count));

	return (error);
}

/*
 * Copy out the buffer. Note that we do incremental copying, so
 * sooptcopyout() won't work. However, the goal is to produce the same
 * end result as if we copied in the entire user buffer, updated it,
 * and then used sooptcopyout() to copy it out.
 *
 * NOTE: This should be called with a write lock on the PCB; however,
 * the function will drop it after it extracts the data from the TCPCB.
 */
int
tcp_log_getlogbuf(struct sockopt *sopt, struct tcpcb *tp)
{
	struct tcp_log_stailq log_tailq;
	struct tcp_log_mem *log_entry, *log_next;
	struct tcp_log_buffer *out_entry;
	struct inpcb *inp;
	size_t outsize, entrysize;
	int error, outnum;

	INP_WLOCK_ASSERT(tp->t_inpcb);
	inp = tp->t_inpcb;

	/*
	 * Determine which log entries will fit in the buffer. As an
	 * optimization, skip this if all the entries will clearly fit
	 * in the buffer. (However, get an exact size if we are using
	 * INVARIANTS.)
	 */
#ifndef INVARIANTS
	if (sopt->sopt_valsize / (sizeof(struct tcp_log_buffer) +
	    sizeof(struct tcp_log_verbose)) >= tp->t_lognum) {
		log_entry = STAILQ_LAST(&tp->t_logs, tcp_log_mem, tlm_queue);
		log_next = NULL;
		outsize = 0;
		outnum = tp->t_lognum;
	} else {
#endif
		outsize = outnum = 0;
		log_entry = NULL;
		STAILQ_FOREACH(log_next, &tp->t_logs, tlm_queue) {
			entrysize = sizeof(struct tcp_log_buffer);
			if (log_next->tlm_buf.tlb_eventflags &
			    TLB_FLAG_VERBOSE)
				entrysize += sizeof(struct tcp_log_verbose);
			if ((sopt->sopt_valsize - outsize) < entrysize)
				break;
			outsize += entrysize;
			outnum++;
			log_entry = log_next;
		}
		KASSERT(outsize <= sopt->sopt_valsize,
		    ("%s: calculated output size (%zu) greater than available"
			"space (%zu)", __func__, outsize, sopt->sopt_valsize));
#ifndef INVARIANTS
	}
#endif

	/*
	 * Copy traditional sooptcopyout() behavior: if sopt->sopt_val
	 * is NULL, silently skip the copy. However, in this case, we
	 * will leave the list alone and return. Functionally, this
	 * gives userspace a way to poll for an approximate buffer
	 * size they will need to get the log entries.
	 */
	if (sopt->sopt_val == NULL) {
		INP_WUNLOCK(inp);
		if (outsize == 0) {
			outsize = outnum * (sizeof(struct tcp_log_buffer) +
			    sizeof(struct tcp_log_verbose));
		}
		if (sopt->sopt_valsize > outsize)
			sopt->sopt_valsize = outsize;
		return (0);
	}

	/*
	 * Break apart the list. We'll save the ones we want to copy
	 * out locally and remove them from the TCPCB list. We can
	 * then drop the INPCB lock while we do the copyout.
	 *
	 * There are roughly three cases:
	 * 1. There was nothing to copy out. That's easy: drop the
	 * lock and return.
	 * 2. We are copying out the entire list. Again, that's easy:
	 * move the whole list.
	 * 3. We are copying out a partial list. That's harder. We
	 * need to update the list book-keeping entries.
	 */
	if (log_entry != NULL && log_next == NULL) {
		/* Move entire list. */
		KASSERT(outnum == tp->t_lognum,
		    ("%s:%d: outnum (%d) should match tp->t_lognum (%d)",
			__func__, __LINE__, outnum, tp->t_lognum));
		log_tailq = tp->t_logs;
		tp->t_lognum = 0;
		STAILQ_INIT(&tp->t_logs);
	} else if (log_entry != NULL) {
		/* Move partial list. */
		KASSERT(outnum < tp->t_lognum,
		    ("%s:%d: outnum (%d) not less than tp->t_lognum (%d)",
			__func__, __LINE__, outnum, tp->t_lognum));
		STAILQ_FIRST(&log_tailq) = STAILQ_FIRST(&tp->t_logs);
		STAILQ_FIRST(&tp->t_logs) = STAILQ_NEXT(log_entry, tlm_queue);
		KASSERT(STAILQ_NEXT(log_entry, tlm_queue) != NULL,
		    ("%s:%d: tp->t_logs is unexpectedly shorter than expected"
		    "(tp: %p, log_tailq: %p, outnum: %d, tp->t_lognum: %d)",
		    __func__, __LINE__, tp, &log_tailq, outnum, tp->t_lognum));
		STAILQ_NEXT(log_entry, tlm_queue) = NULL;
		log_tailq.stqh_last = &STAILQ_NEXT(log_entry, tlm_queue);
		tp->t_lognum -= outnum;
	} else
		STAILQ_INIT(&log_tailq);

	/* Drop the PCB lock. */
	INP_WUNLOCK(inp);

	/* Copy the data out. */
	error = tcp_log_logs_to_buf(sopt, &log_tailq, &out_entry, outnum);

	if (error) {
		/* Restore list */
		INP_WLOCK(inp);
		if ((inp->inp_flags & (INP_TIMEWAIT | INP_DROPPED)) == 0) {
			tp = intotcpcb(inp);

			/* Merge the two lists. */
			STAILQ_CONCAT(&log_tailq, &tp->t_logs);
			tp->t_logs = log_tailq;
			tp->t_lognum += outnum;
		}
		INP_WUNLOCK(inp);
	} else {
		/* Sanity check entries */
		KASSERT(((caddr_t)out_entry - (caddr_t)sopt->sopt_val)  ==
		    outsize, ("%s: Actual output size (%zu) != "
			"calculated output size (%zu)", __func__,
			(size_t)((caddr_t)out_entry - (caddr_t)sopt->sopt_val),
			outsize));

		/* Free the entries we just copied out. */
		STAILQ_FOREACH_SAFE(log_entry, &log_tailq, tlm_queue, log_next) {
			tcp_log_entry_refcnt_rem(log_entry);
			uma_zfree(tcp_log_zone, log_entry);
		}
	}

	sopt->sopt_valsize = (size_t)((caddr_t)out_entry -
	    (caddr_t)sopt->sopt_val);
	return (error);
}

static void
tcp_log_free_queue(struct tcp_log_dev_queue *param)
{
	struct tcp_log_dev_log_queue *entry;

	KASSERT(param != NULL, ("%s: called with NULL param", __func__));
	if (param == NULL)
		return;

	entry = (struct tcp_log_dev_log_queue *)param;

	/* Free the entries. */
	tcp_log_free_entries(&entry->tldl_entries, &entry->tldl_count);

	/* Free the buffer, if it is allocated. */
	if (entry->tldl_common.tldq_buf != NULL)
		free(entry->tldl_common.tldq_buf, M_TCPLOGDEV);

	/* Free the queue entry. */
	free(entry, M_TCPLOGDEV);
}

static struct tcp_log_common_header *
tcp_log_expandlogbuf(struct tcp_log_dev_queue *param)
{
	struct tcp_log_dev_log_queue *entry;
	struct tcp_log_header *hdr;
	uint8_t *end;
	struct sockopt sopt;
	int error;

	entry = (struct tcp_log_dev_log_queue *)param;

	/* Take a worst-case guess at space needs. */
	sopt.sopt_valsize = sizeof(struct tcp_log_header) +
	    entry->tldl_count * (sizeof(struct tcp_log_buffer) +
	    sizeof(struct tcp_log_verbose));
	hdr = malloc(sopt.sopt_valsize, M_TCPLOGDEV, M_NOWAIT);
	if (hdr == NULL) {
#ifdef TCPLOG_DEBUG_COUNTERS
		counter_u64_add(tcp_log_que_fail5, entry->tldl_count);
#endif
		return (NULL);
	}
	sopt.sopt_val = hdr + 1;
	sopt.sopt_valsize -= sizeof(struct tcp_log_header);
	sopt.sopt_td = NULL;
	
	error = tcp_log_logs_to_buf(&sopt, &entry->tldl_entries,
	    (struct tcp_log_buffer **)&end, entry->tldl_count);
	if (error) {
		free(hdr, M_TCPLOGDEV);
		return (NULL);
	}

	/* Free the entries. */
	tcp_log_free_entries(&entry->tldl_entries, &entry->tldl_count);
	entry->tldl_count = 0;

	memset(hdr, 0, sizeof(struct tcp_log_header));
	hdr->tlh_version = TCP_LOG_BUF_VER;
	hdr->tlh_type = TCP_LOG_DEV_TYPE_BBR;
	hdr->tlh_length = end - (uint8_t *)hdr;
	hdr->tlh_ie = entry->tldl_ie;
	hdr->tlh_af = entry->tldl_af;
	getboottime(&hdr->tlh_offset);
	strlcpy(hdr->tlh_id, entry->tldl_id, TCP_LOG_ID_LEN);
	strlcpy(hdr->tlh_reason, entry->tldl_reason, TCP_LOG_REASON_LEN);
	return ((struct tcp_log_common_header *)hdr);
}

/*
 * Queue the tcpcb's log buffer for transmission via the log buffer facility.
 *
 * NOTE: This should be called with a write lock on the PCB.
 *
 * how should be M_WAITOK or M_NOWAIT. If M_WAITOK, the function will drop
 * and reacquire the INP lock if it needs to do so.
 *
 * If force is false, this will only dump auto-logged sessions if
 * tcp_log_auto_all is true or if there is a log ID defined for the session.
 */
int
tcp_log_dump_tp_logbuf(struct tcpcb *tp, char *reason, int how, bool force)
{
	struct tcp_log_dev_log_queue *entry;
	struct inpcb *inp;
#ifdef TCPLOG_DEBUG_COUNTERS
	int num_entries;
#endif

	inp = tp->t_inpcb;
	INP_WLOCK_ASSERT(inp);

	/* If there are no log entries, there is nothing to do. */
	if (tp->t_lognum == 0)
		return (0);

	/* Check for a log ID. */
	if (tp->t_lib == NULL && (tp->t_flags2 & TF2_LOG_AUTO) &&
	    !tcp_log_auto_all && !force) {
		struct tcp_log_mem *log_entry;

		/*
		 * We needed a log ID and none was found. Free the log entries
		 * and return success. Also, cancel further logging. If the
		 * session doesn't have a log ID by now, we'll assume it isn't
		 * going to get one.
		 */
		while ((log_entry = STAILQ_FIRST(&tp->t_logs)) != NULL)
			tcp_log_remove_log_head(tp, log_entry);
		KASSERT(tp->t_lognum == 0,
		    ("%s: After freeing entries, tp->t_lognum=%d (expected 0)",
			__func__, tp->t_lognum));
		tp->t_logstate = TCP_LOG_STATE_OFF;
		return (0);
	}

	/*
	 * Allocate memory. If we must wait, we'll need to drop the locks
	 * and reacquire them (and do all the related business that goes
	 * along with that).
	 */
	entry = malloc(sizeof(struct tcp_log_dev_log_queue), M_TCPLOGDEV,
	    M_NOWAIT);
	if (entry == NULL && (how & M_NOWAIT)) {
#ifdef TCPLOG_DEBUG_COUNTERS
		counter_u64_add(tcp_log_que_fail3, 1);
#endif
		return (ENOBUFS);
	}
	if (entry == NULL) {
		INP_WUNLOCK(inp);
		entry = malloc(sizeof(struct tcp_log_dev_log_queue),
		    M_TCPLOGDEV, M_WAITOK);
		INP_WLOCK(inp);
		/*
		 * Note that this check is slightly overly-restrictive in
		 * that the TCB can survive either of these events.
		 * However, there is currently not a good way to ensure
		 * that is the case. So, if we hit this M_WAIT path, we
		 * may end up dropping some entries. That seems like a
		 * small price to pay for safety.
		 */
		if (inp->inp_flags & (INP_TIMEWAIT | INP_DROPPED)) {
			free(entry, M_TCPLOGDEV);
#ifdef TCPLOG_DEBUG_COUNTERS
			counter_u64_add(tcp_log_que_fail2, 1);
#endif
			return (ECONNRESET);
		}
		tp = intotcpcb(inp);
		if (tp->t_lognum == 0) {
			free(entry, M_TCPLOGDEV);
			return (0);
		}
	}

	/* Fill in the unique parts of the queue entry. */
	if (tp->t_lib != NULL)
		strlcpy(entry->tldl_id, tp->t_lib->tlb_id, TCP_LOG_ID_LEN);
	else
		strlcpy(entry->tldl_id, "UNKNOWN", TCP_LOG_ID_LEN);
	if (reason != NULL)
		strlcpy(entry->tldl_reason, reason, TCP_LOG_REASON_LEN);
	else
		strlcpy(entry->tldl_reason, "UNKNOWN", TCP_LOG_ID_LEN);
	entry->tldl_ie = inp->inp_inc.inc_ie;
	if (inp->inp_inc.inc_flags & INC_ISIPV6)
		entry->tldl_af = AF_INET6;
	else
		entry->tldl_af = AF_INET;
	entry->tldl_entries = tp->t_logs;
	entry->tldl_count = tp->t_lognum;

	/* Fill in the common parts of the queue entry. */
	entry->tldl_common.tldq_buf = NULL;
	entry->tldl_common.tldq_xform = tcp_log_expandlogbuf;
	entry->tldl_common.tldq_dtor = tcp_log_free_queue;

	/* Clear the log data from the TCPCB. */
#ifdef TCPLOG_DEBUG_COUNTERS
	num_entries = tp->t_lognum;
#endif
	tp->t_lognum = 0;
	STAILQ_INIT(&tp->t_logs);

	/* Add the entry. If no one is listening, free the entry. */
	if (tcp_log_dev_add_log((struct tcp_log_dev_queue *)entry)) {
		tcp_log_free_queue((struct tcp_log_dev_queue *)entry);
#ifdef TCPLOG_DEBUG_COUNTERS
		counter_u64_add(tcp_log_que_fail1, num_entries);
	} else {
		counter_u64_add(tcp_log_queued, num_entries);
#endif
	}
	return (0);
}

/*
 * Queue the log_id_node's log buffers for transmission via the log buffer
 * facility.
 *
 * NOTE: This should be called with the bucket locked and referenced.
 *
 * how should be M_WAITOK or M_NOWAIT. If M_WAITOK, the function will drop
 * and reacquire the bucket lock if it needs to do so. (The caller must
 * ensure that the tln is no longer on any lists so no one else will mess
 * with this while the lock is dropped!)
 */
static int
tcp_log_dump_node_logbuf(struct tcp_log_id_node *tln, char *reason, int how)
{
	struct tcp_log_dev_log_queue *entry;
	struct tcp_log_id_bucket *tlb;

	tlb = tln->tln_bucket;
	TCPID_BUCKET_LOCK_ASSERT(tlb);
	KASSERT(tlb->tlb_refcnt > 0,
	    ("%s:%d: Called with unreferenced bucket (tln=%p, tlb=%p)",
	    __func__, __LINE__, tln, tlb));
	KASSERT(tln->tln_closed,
	    ("%s:%d: Called for node with tln_closed==false (tln=%p)",
	    __func__, __LINE__, tln));

	/* If there are no log entries, there is nothing to do. */
	if (tln->tln_count == 0)
		return (0);

	/*
	 * Allocate memory. If we must wait, we'll need to drop the locks
	 * and reacquire them (and do all the related business that goes
	 * along with that).
	 */
	entry = malloc(sizeof(struct tcp_log_dev_log_queue), M_TCPLOGDEV,
	    M_NOWAIT);
	if (entry == NULL && (how & M_NOWAIT))
		return (ENOBUFS);
	if (entry == NULL) {
		TCPID_BUCKET_UNLOCK(tlb);
		entry = malloc(sizeof(struct tcp_log_dev_log_queue),
		    M_TCPLOGDEV, M_WAITOK);
		TCPID_BUCKET_LOCK(tlb);
	}

	/* Fill in the common parts of the queue entry.. */
	entry->tldl_common.tldq_buf = NULL;
	entry->tldl_common.tldq_xform = tcp_log_expandlogbuf;
	entry->tldl_common.tldq_dtor = tcp_log_free_queue;

	/* Fill in the unique parts of the queue entry. */
	strlcpy(entry->tldl_id, tlb->tlb_id, TCP_LOG_ID_LEN);
	if (reason != NULL)
		strlcpy(entry->tldl_reason, reason, TCP_LOG_REASON_LEN);
	else
		strlcpy(entry->tldl_reason, "UNKNOWN", TCP_LOG_ID_LEN);
	entry->tldl_ie = tln->tln_ie;
	entry->tldl_entries = tln->tln_entries;
	entry->tldl_count = tln->tln_count;
	entry->tldl_af = tln->tln_af;

	/* Add the entry. If no one is listening, free the entry. */
	if (tcp_log_dev_add_log((struct tcp_log_dev_queue *)entry))
		tcp_log_free_queue((struct tcp_log_dev_queue *)entry);

	return (0);
}


/*
 * Queue the log buffers for all sessions in a bucket for transmissions via
 * the log buffer facility.
 *
 * NOTE: This should be called with a locked bucket; however, the function
 * will drop the lock.
 */
#define	LOCAL_SAVE	10
static void
tcp_log_dumpbucketlogs(struct tcp_log_id_bucket *tlb, char *reason)
{
	struct tcp_log_id_node local_entries[LOCAL_SAVE];
	struct inpcb *inp;
	struct tcpcb *tp;
	struct tcp_log_id_node *cur_tln, *prev_tln, *tmp_tln;
	int i, num_local_entries, tree_locked;
	bool expireq_locked;

	TCPID_BUCKET_LOCK_ASSERT(tlb);

	/*
	 * Take a reference on the bucket to keep it from disappearing until
	 * we are done.
	 */
	TCPID_BUCKET_REF(tlb);

	/*
	 * We'll try to create these without dropping locks. However, we
	 * might very well need to drop locks to get memory. If that's the
	 * case, we'll save up to 10 on the stack, and sacrifice the rest.
	 * (Otherwise, we need to worry about finding our place again in a
	 * potentially changed list. It just doesn't seem worth the trouble
	 * to do that.
	 */
	expireq_locked = false;
	num_local_entries = 0;
	prev_tln = NULL;
	tree_locked = TREE_UNLOCKED;
	SLIST_FOREACH_SAFE(cur_tln, &tlb->tlb_head, tln_list, tmp_tln) {
		/*
		 * If this isn't associated with a TCPCB, we can pull it off
		 * the list now. We need to be careful that the expire timer
		 * hasn't already taken ownership (tln_expiretime == SBT_MAX).
		 * If so, we let the expire timer code free the data. 
		 */
		if (cur_tln->tln_closed) {
no_inp:
			/*
			 * Get the expireq lock so we can get a consistent
			 * read of tln_expiretime and so we can remove this
			 * from the expireq.
			 */
			if (!expireq_locked) {
				TCPLOG_EXPIREQ_LOCK();
				expireq_locked = true;
			}

			/*
			 * We ignore entries with tln_expiretime == SBT_MAX.
			 * The expire timer code already owns those.
			 */
			KASSERT(cur_tln->tln_expiretime > (sbintime_t) 0,
			    ("%s:%d: node on the expire queue without positive "
			    "expire time", __func__, __LINE__));
			if (cur_tln->tln_expiretime == SBT_MAX) {
				prev_tln = cur_tln;
				continue;
			}

			/* Remove the entry from the expireq. */
			STAILQ_REMOVE(&tcp_log_expireq_head, cur_tln,
			    tcp_log_id_node, tln_expireq);

			/* Remove the entry from the bucket. */
			if (prev_tln != NULL)
				SLIST_REMOVE_AFTER(prev_tln, tln_list);
			else
				SLIST_REMOVE_HEAD(&tlb->tlb_head, tln_list);

			/*
			 * Drop the INP and bucket reference counts. Due to
			 * lock-ordering rules, we need to drop the expire
			 * queue lock.
			 */
			TCPLOG_EXPIREQ_UNLOCK();
			expireq_locked = false;

			/* Drop the INP reference. */
			INP_WLOCK(cur_tln->tln_inp);
			if (!in_pcbrele_wlocked(cur_tln->tln_inp))
				INP_WUNLOCK(cur_tln->tln_inp);

			if (tcp_log_unref_bucket(tlb, &tree_locked, NULL)) {
#ifdef INVARIANTS
				panic("%s: Bucket refcount unexpectedly 0.",
				    __func__);
#endif
				/*
				 * Recover as best we can: free the entry we
				 * own.
				 */
				tcp_log_free_entries(&cur_tln->tln_entries,
				    &cur_tln->tln_count);
				uma_zfree(tcp_log_node_zone, cur_tln);
				goto done;
			}

			if (tcp_log_dump_node_logbuf(cur_tln, reason,
			    M_NOWAIT)) {
				/*
				 * If we have sapce, save the entries locally.
				 * Otherwise, free them.
				 */
				if (num_local_entries < LOCAL_SAVE) {
					local_entries[num_local_entries] =
					    *cur_tln;
					num_local_entries++;
				} else {
					tcp_log_free_entries(
					    &cur_tln->tln_entries,
					    &cur_tln->tln_count);
				}
			}

			/* No matter what, we are done with the node now. */
			uma_zfree(tcp_log_node_zone, cur_tln);

			/*
			 * Because we removed this entry from the list, prev_tln
			 * (which tracks the previous entry still on the tlb
			 * list) remains unchanged.
			 */
			continue;
		}

		/*
		 * If we get to this point, the session data is still held in
		 * the TCPCB. So, we need to pull the data out of that.
		 *
		 * We will need to drop the expireq lock so we can lock the INP.
		 * We can then try to extract the data the "easy" way. If that
		 * fails, we'll save the log entries for later.
		 */
		if (expireq_locked) {
			TCPLOG_EXPIREQ_UNLOCK();
			expireq_locked = false;
		}

		/* Lock the INP and then re-check the state. */
		inp = cur_tln->tln_inp;
		INP_WLOCK(inp);
		/*
		 * If we caught this while it was transitioning, the data
		 * might have moved from the TCPCB to the tln (signified by
		 * setting tln_closed to true. If so, treat this like an
		 * inactive connection.
		 */
		if (cur_tln->tln_closed) {
			/*
			 * It looks like we may have caught this connection
			 * while it was transitioning from active to inactive.
			 * Treat this like an inactive connection.
			 */
			INP_WUNLOCK(inp);
			goto no_inp;
		}

		/*
		 * Try to dump the data from the tp without dropping the lock.
		 * If this fails, try to save off the data locally.
		 */
		tp = cur_tln->tln_tp;
		if (tcp_log_dump_tp_logbuf(tp, reason, M_NOWAIT, true) &&
		    num_local_entries < LOCAL_SAVE) {
			tcp_log_move_tp_to_node(tp,
			    &local_entries[num_local_entries]);
			local_entries[num_local_entries].tln_closed = 1;
			KASSERT(local_entries[num_local_entries].tln_bucket ==
			    tlb, ("%s: %d: bucket mismatch for node %p",
			    __func__, __LINE__, cur_tln));
			num_local_entries++;
		}

		INP_WUNLOCK(inp);

		/*
		 * We are goint to leave the current tln on the list. It will
		 * become the previous tln.
		 */
		prev_tln = cur_tln;
	}

	/* Drop our locks, if any. */
	KASSERT(tree_locked == TREE_UNLOCKED,
	    ("%s: %d: tree unexpectedly locked", __func__, __LINE__));
	switch (tree_locked) {
	case TREE_WLOCKED:
		TCPID_TREE_WUNLOCK();
		tree_locked = TREE_UNLOCKED;
		break;
	case TREE_RLOCKED:
		TCPID_TREE_RUNLOCK();
		tree_locked = TREE_UNLOCKED;
		break;
	}
	if (expireq_locked) {
		TCPLOG_EXPIREQ_UNLOCK();
		expireq_locked = false;
	}

	/*
	 * Try again for any saved entries. tcp_log_dump_node_logbuf() is
	 * guaranteed to free the log entries within the node. And, since
	 * the node itself is on our stack, we don't need to free it.
	 */
	for (i = 0; i < num_local_entries; i++)
		tcp_log_dump_node_logbuf(&local_entries[i], reason, M_WAITOK);

	/* Drop our reference. */
	if (!tcp_log_unref_bucket(tlb, &tree_locked, NULL))
		TCPID_BUCKET_UNLOCK(tlb);

done:
	/* Drop our locks, if any. */
	switch (tree_locked) {
	case TREE_WLOCKED:
		TCPID_TREE_WUNLOCK();
		break;
	case TREE_RLOCKED:
		TCPID_TREE_RUNLOCK();
		break;
	}
	if (expireq_locked)
		TCPLOG_EXPIREQ_UNLOCK();
}
#undef	LOCAL_SAVE


/*
 * Queue the log buffers for all sessions in a bucket for transmissions via
 * the log buffer facility.
 *
 * NOTE: This should be called with a locked INP; however, the function
 * will drop the lock.
 */
void
tcp_log_dump_tp_bucket_logbufs(struct tcpcb *tp, char *reason)
{
	struct tcp_log_id_bucket *tlb;
	int tree_locked;

	/* Figure out our bucket and lock it. */
	INP_WLOCK_ASSERT(tp->t_inpcb);
	tlb = tp->t_lib;
	if (tlb == NULL) {
		/*
		 * No bucket; treat this like a request to dump a single
		 * session's traces.
		 */
		(void)tcp_log_dump_tp_logbuf(tp, reason, M_WAITOK, true);
		INP_WUNLOCK(tp->t_inpcb);
		return;
	}
	TCPID_BUCKET_REF(tlb);
	INP_WUNLOCK(tp->t_inpcb);
	TCPID_BUCKET_LOCK(tlb);

	/* If we are the last reference, we have nothing more to do here. */
	tree_locked = TREE_UNLOCKED;
	if (tcp_log_unref_bucket(tlb, &tree_locked, NULL)) {
		switch (tree_locked) {
		case TREE_WLOCKED:
			TCPID_TREE_WUNLOCK();
			break;
		case TREE_RLOCKED:
			TCPID_TREE_RUNLOCK();
			break;
		}
		return;
	}

	/* Turn this over to tcp_log_dumpbucketlogs() to finish the work. */ 
	tcp_log_dumpbucketlogs(tlb, reason);
}

/*
 * Mark the end of a flow with the current stack. A stack can add
 * stack-specific info to this trace event by overriding this
 * function (see bbr_log_flowend() for example).
 */
void
tcp_log_flowend(struct tcpcb *tp)
{
	if (tp->t_logstate != TCP_LOG_STATE_OFF) {
		struct socket *so = tp->t_inpcb->inp_socket;
		TCP_LOG_EVENT(tp, NULL, &so->so_rcv, &so->so_snd,
				TCP_LOG_FLOWEND, 0, 0, NULL, false);
	}
}

