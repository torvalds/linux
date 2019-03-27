/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2010 Luigi Rizzo, Riccardo Panicucci, Universita` di Pisa
 * All rights reserved
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
 * internal dummynet APIs.
 *
 * $FreeBSD$
 */

#ifndef _IP_DN_PRIVATE_H
#define _IP_DN_PRIVATE_H

/* debugging support
 * use ND() to remove debugging, D() to print a line,
 * DX(level, ...) to print above a certain level
 * If you redefine D() you are expected to redefine all.
 */
#ifndef D
#define ND(fmt, ...) do {} while (0)
#define D1(fmt, ...) do {} while (0)
#define D(fmt, ...) printf("%-10s " fmt "\n",      \
        __FUNCTION__, ## __VA_ARGS__)
#define DX(lev, fmt, ...) do {              \
        if (dn_cfg.debug > lev) D(fmt, ## __VA_ARGS__); } while (0)
#endif

MALLOC_DECLARE(M_DUMMYNET);

#ifndef __linux__
#define div64(a, b)  ((int64_t)(a) / (int64_t)(b))
#endif

#define DN_LOCK_INIT() do {				\
	mtx_init(&dn_cfg.uh_mtx, "dn_uh", NULL, MTX_DEF);	\
	mtx_init(&dn_cfg.bh_mtx, "dn_bh", NULL, MTX_DEF);	\
	} while (0)
#define DN_LOCK_DESTROY() do {				\
	mtx_destroy(&dn_cfg.uh_mtx);			\
	mtx_destroy(&dn_cfg.bh_mtx);			\
	} while (0)
#if 0 /* not used yet */
#define DN_UH_RLOCK()		mtx_lock(&dn_cfg.uh_mtx)
#define DN_UH_RUNLOCK()		mtx_unlock(&dn_cfg.uh_mtx)
#define DN_UH_WLOCK()		mtx_lock(&dn_cfg.uh_mtx)
#define DN_UH_WUNLOCK()		mtx_unlock(&dn_cfg.uh_mtx)
#define DN_UH_LOCK_ASSERT()	mtx_assert(&dn_cfg.uh_mtx, MA_OWNED)
#endif

#define DN_BH_RLOCK()		mtx_lock(&dn_cfg.uh_mtx)
#define DN_BH_RUNLOCK()		mtx_unlock(&dn_cfg.uh_mtx)
#define DN_BH_WLOCK()		mtx_lock(&dn_cfg.uh_mtx)
#define DN_BH_WUNLOCK()		mtx_unlock(&dn_cfg.uh_mtx)
#define DN_BH_LOCK_ASSERT()	mtx_assert(&dn_cfg.uh_mtx, MA_OWNED)

SLIST_HEAD(dn_schk_head, dn_schk);
SLIST_HEAD(dn_sch_inst_head, dn_sch_inst);
SLIST_HEAD(dn_fsk_head, dn_fsk);
SLIST_HEAD(dn_queue_head, dn_queue);
SLIST_HEAD(dn_alg_head, dn_alg);

#ifdef NEW_AQM
SLIST_HEAD(dn_aqm_head, dn_aqm); /* for new AQMs */
#endif

struct mq {	/* a basic queue of packets*/
        struct mbuf *head, *tail;
	int count;
};

static inline void
set_oid(struct dn_id *o, int type, int len)
{
        o->type = type;
        o->len = len;
        o->subtype = 0;
}

/*
 * configuration and global data for a dummynet instance
 *
 * When a configuration is modified from userland, 'id' is incremented
 * so we can use the value to check for stale pointers.
 */
struct dn_parms {
	uint32_t	id;		/* configuration version */

	/* defaults (sysctl-accessible) */
	int	red_lookup_depth;
	int	red_avg_pkt_size;
	int	red_max_pkt_size;
	int	hash_size;
	int	max_hash_size;
	long	byte_limit;		/* max queue sizes */
	long	slot_limit;

	int	io_fast;
	int	debug;

	/* timekeeping */
	struct timeval prev_t;		/* last time dummynet_tick ran */
	struct dn_heap	evheap;		/* scheduled events */

	/* counters of objects -- used for reporting space */
	int	schk_count;
	int	si_count;
	int	fsk_count;
	int	queue_count;

	/* ticks and other stuff */
	uint64_t	curr_time;
	/* flowsets and schedulers are in hash tables, with 'hash_size'
	 * buckets. fshash is looked up at every packet arrival
	 * so better be generous if we expect many entries.
	 */
	struct dn_ht	*fshash;
	struct dn_ht	*schedhash;
	/* list of flowsets without a scheduler -- use sch_chain */
	struct dn_fsk_head	fsu;	/* list of unlinked flowsets */
	struct dn_alg_head	schedlist;	/* list of algorithms */
#ifdef NEW_AQM
	struct dn_aqm_head	aqmlist;	/* list of AQMs */
#endif

	/* Store the fs/sch to scan when draining. The value is the
	 * bucket number of the hash table. Expire can be disabled
	 * with net.inet.ip.dummynet.expire=0, or it happens every
	 * expire ticks.
	 **/
	int drain_fs;
	int drain_sch;
	uint32_t expire;
	uint32_t expire_cycle;	/* tick count */

	int init_done;

	/* if the upper half is busy doing something long,
	 * can set the busy flag and we will enqueue packets in
	 * a queue for later processing.
	 */
	int	busy;
	struct	mq	pending;

#ifdef _KERNEL
	/*
	 * This file is normally used in the kernel, unless we do
	 * some userland tests, in which case we do not need a mtx.
	 * uh_mtx arbitrates between system calls and also
	 * protects fshash, schedhash and fsunlinked.
	 * These structures are readonly for the lower half.
	 * bh_mtx protects all other structures which may be
	 * modified upon packet arrivals
	 */
#if defined( __linux__ ) || defined( _WIN32 )
	spinlock_t uh_mtx;
	spinlock_t bh_mtx;
#else
	struct mtx uh_mtx;
	struct mtx bh_mtx;
#endif

#endif /* _KERNEL */
};

/*
 * Delay line, contains all packets on output from a link.
 * Every scheduler instance has one.
 */
struct delay_line {
	struct dn_id oid;
	struct dn_sch_inst *si;
	struct mq mq;
};

/*
 * The kernel side of a flowset. It is linked in a hash table
 * of flowsets, and in a list of children of their parent scheduler.
 * qht is either the queue or (if HAVE_MASK) a hash table queues.
 * Note that the mask to use is the (flow_mask|sched_mask), which
 * changes as we attach/detach schedulers. So we store it here.
 *
 * XXX If we want to add scheduler-specific parameters, we need to
 * put them in external storage because the scheduler may not be
 * available when the fsk is created.
 */
struct dn_fsk { /* kernel side of a flowset */
	struct dn_fs fs;
	SLIST_ENTRY(dn_fsk) fsk_next;	/* hash chain for fshash */

	struct ipfw_flow_id fsk_mask;

	/* qht is a hash table of queues, or just a single queue
	 * a bit in fs.flags tells us which one
	 */
	struct dn_ht	*qht;
	struct dn_schk *sched;		/* Sched we are linked to */
	SLIST_ENTRY(dn_fsk) sch_chain;	/* list of fsk attached to sched */

	/* bucket index used by drain routine to drain queues for this
	 * flowset
	 */
	int drain_bucket;
	/* Parameter realted to RED / GRED */
	/* original values are in dn_fs*/
	int w_q ;		/* queue weight (scaled) */
	int max_th ;		/* maximum threshold for queue (scaled) */
	int min_th ;		/* minimum threshold for queue (scaled) */
	int max_p ;		/* maximum value for p_b (scaled) */

	u_int c_1 ;		/* max_p/(max_th-min_th) (scaled) */
	u_int c_2 ;		/* max_p*min_th/(max_th-min_th) (scaled) */
	u_int c_3 ;		/* for GRED, (1-max_p)/max_th (scaled) */
	u_int c_4 ;		/* for GRED, 1 - 2*max_p (scaled) */
	u_int * w_q_lookup ;	/* lookup table for computing (1-w_q)^t */
	u_int lookup_depth ;	/* depth of lookup table */
	int lookup_step ;	/* granularity inside the lookup table */
	int lookup_weight ;	/* equal to (1-w_q)^t / (1-w_q)^(t+1) */
	int avg_pkt_size ;	/* medium packet size */
	int max_pkt_size ;	/* max packet size */
#ifdef NEW_AQM
	struct dn_aqm *aqmfp;	/* Pointer to AQM functions */
	void *aqmcfg;	/* configuration parameters for AQM */
#endif
};

/*
 * A queue is created as a child of a flowset unless it belongs to
 * a !MULTIQUEUE scheduler. It is normally in a hash table in the
 * flowset. fs always points to the parent flowset.
 * si normally points to the sch_inst, unless the flowset has been
 * detached from the scheduler -- in this case si == NULL and we
 * should not enqueue.
 */
struct dn_queue {
	struct dn_flow ni;	/* oid, flow_id, stats */
	struct mq mq;	/* packets queue */
	struct dn_sch_inst *_si;	/* owner scheduler instance */
	SLIST_ENTRY(dn_queue) q_next; /* hash chain list for qht */
	struct dn_fsk *fs;		/* parent flowset. */

	/* RED parameters */
	int avg;		/* average queue length est. (scaled) */
	int count;		/* arrivals since last RED drop */
	int random;		/* random value (scaled) */
	uint64_t q_time;	/* start of queue idle time */
#ifdef NEW_AQM
	void *aqm_status;	/* per-queue status variables*/
#endif

};

/*
 * The kernel side of a scheduler. Contains the userland config,
 * a link, pointer to extra config arguments from command line,
 * kernel flags, and a pointer to the scheduler methods.
 * It is stored in a hash table, and holds a list of all
 * flowsets and scheduler instances.
 * XXX sch must be at the beginning, see schk_hash().
 */
struct dn_schk {
	struct dn_sch sch;
	struct dn_alg *fp;	/* Pointer to scheduler functions */
	struct dn_link link;	/* The link, embedded */
	struct dn_profile *profile; /* delay profile, if any */
	struct dn_id *cfg;	/* extra config arguments */

	SLIST_ENTRY(dn_schk) schk_next;  /* hash chain for schedhash */

	struct dn_fsk_head fsk_list;  /* all fsk linked to me */
	struct dn_fsk *fs;	/* Flowset for !MULTIQUEUE */

	/* bucket index used by the drain routine to drain the scheduler
	 * instance for this flowset.
	 */
	int drain_bucket;

	/* Hash table of all instances (through sch.sched_mask)
	 * or single instance if no mask. Always valid.
	 */
	struct dn_ht	*siht;
};


/*
 * Scheduler instance.
 * Contains variables and all queues relative to a this instance.
 * This struct is created a runtime.
 */
struct dn_sch_inst {
	struct dn_flow	ni;	/* oid, flowid and stats */
	SLIST_ENTRY(dn_sch_inst) si_next; /* hash chain for siht */
	struct delay_line dline;
	struct dn_schk *sched;	/* the template */
	int		kflags;	/* DN_ACTIVE */

	int64_t	credit;		/* bits I can transmit (more or less). */
	uint64_t sched_time;	/* time link was scheduled in ready_heap */
	uint64_t idle_time;	/* start of scheduler instance idle time */

	/* q_count is the number of queues that this instance is using.
	 * The counter is incremented or decremented when
	 * a reference from the queue is created or deleted.
	 * It is used to make sure that a scheduler instance can be safely
	 * deleted by the drain routine. See notes below.
	 */
	int q_count;

};

/*
 * NOTE about object drain.
 * The system will automatically (XXX check when) drain queues and
 * scheduler instances when they are idle.
 * A queue is idle when it has no packets; an instance is idle when
 * it is not in the evheap heap, and the corresponding delay line is empty.
 * A queue can be safely deleted when it is idle because of the scheduler
 * function xxx_free_queue() will remove any references to it.
 * An instance can be only deleted when no queues reference it. To be sure
 * of that, a counter (q_count) stores the number of queues that are pointing
 * to the instance.
 *
 * XXX
 * Order of scan:
 * - take all flowset in a bucket for the flowset hash table
 * - take all queues in a bucket for the flowset
 * - increment the queue bucket
 * - scan next flowset bucket
 * Nothing is done if a bucket contains no entries.
 *
 * The same schema is used for sceduler instances
 */


/* kernel-side flags. Linux has DN_DELETE in fcntl.h
 */
enum {
	/* 1 and 2 are reserved for the SCAN flags */
	DN_DESTROY	= 0x0004, /* destroy */
	DN_DELETE_FS	= 0x0008, /* destroy flowset */
	DN_DETACH	= 0x0010,
	DN_ACTIVE	= 0x0020, /* object is in evheap */
	DN_F_DLINE	= 0x0040, /* object is a delay line */
	DN_DEL_SAFE	= 0x0080, /* delete a queue only if no longer needed
				   * by scheduler */
	DN_QHT_IS_Q	= 0x0100, /* in flowset, qht is a single queue */
};

/*
 * Packets processed by dummynet have an mbuf tag associated with
 * them that carries their dummynet state.
 * Outside dummynet, only the 'rule' field is relevant, and it must
 * be at the beginning of the structure.
 */
struct dn_pkt_tag {
	struct ipfw_rule_ref rule;	/* matching rule	*/

	/* second part, dummynet specific */
	int dn_dir;		/* action when packet comes out.*/
				/* see ip_fw_private.h		*/
	uint64_t output_time;	/* when the pkt is due for delivery*/
	struct ifnet *ifp;	/* interface, for ip_output	*/
	struct _ip6dn_args ip6opt;	/* XXX ipv6 options	*/
	uint16_t iphdr_off;	/* IP header offset for mtodo()	*/
};

/*
 * Possible values for dn_dir. XXXGL: this needs to be reviewed
 * and converted to same values ip_fw_args.flags use.
 */
enum {
	DIR_OUT =	0,
	DIR_IN =	1,
	DIR_FWD =	2,
	DIR_DROP =	3,
	PROTO_LAYER2 =	0x4, /* set for layer 2 */
	PROTO_IPV4 =	0x08,
	PROTO_IPV6 =	0x10,
	PROTO_IFB =	0x0c, /* layer2 + ifbridge */
};

extern struct dn_parms dn_cfg;
//VNET_DECLARE(struct dn_parms, _base_dn_cfg);
//#define dn_cfg	VNET(_base_dn_cfg)

int dummynet_io(struct mbuf **, struct ip_fw_args *);
void dummynet_task(void *context, int pending);
void dn_reschedule(void);
struct dn_pkt_tag * dn_tag_get(struct mbuf *m);

struct dn_queue *ipdn_q_find(struct dn_fsk *, struct dn_sch_inst *,
        struct ipfw_flow_id *);
struct dn_sch_inst *ipdn_si_find(struct dn_schk *, struct ipfw_flow_id *);

/*
 * copy_range is a template for requests for ranges of pipes/queues/scheds.
 * The number of ranges is variable and can be derived by o.len.
 * As a default, we use a small number of entries so that the struct
 * fits easily on the stack and is sufficient for most common requests.
 */
#define DEFAULT_RANGES	5
struct copy_range {
        struct dn_id o;
        uint32_t	r[ 2 * DEFAULT_RANGES ];
};

struct copy_args {
	char **start;
	char *end;
	int flags;
	int type;
	struct copy_range *extra;	/* extra filtering */
};

struct sockopt;
int ip_dummynet_compat(struct sockopt *sopt);
int dummynet_get(struct sockopt *sopt, void **compat);
int dn_c_copy_q (void *_ni, void *arg);
int dn_c_copy_pipe(struct dn_schk *s, struct copy_args *a, int nq);
int dn_c_copy_fs(struct dn_fsk *f, struct copy_args *a, int nq);
int dn_compat_copy_queue(struct copy_args *a, void *_o);
int dn_compat_copy_pipe(struct copy_args *a, void *_o);
int copy_data_helper_compat(void *_o, void *_arg);
int dn_compat_calc_size(void);
int do_config(void *p, int l);

/* function to drain idle object */
void dn_drain_scheduler(void);
void dn_drain_queue(void);

#ifdef NEW_AQM
int ecn_mark(struct mbuf* m);

/* moved from ip_dn_io.c to here to be available for AQMs modules*/
static inline void
mq_append(struct mq *q, struct mbuf *m)
{
#ifdef USERSPACE
	// buffers from netmap need to be copied
	// XXX note that the routine is not expected to fail
	ND("append %p to %p", m, q);
	if (m->m_flags & M_STACK) {
		struct mbuf *m_new;
		void *p;
		int l, ofs;

		ofs = m->m_data - m->__m_extbuf;
		// XXX allocate
		MGETHDR(m_new, M_NOWAIT, MT_DATA);
		ND("*** WARNING, volatile buf %p ext %p %d dofs %d m_new %p",
			m, m->__m_extbuf, m->__m_extlen, ofs, m_new);
		p = m_new->__m_extbuf;	/* new pointer */
		l = m_new->__m_extlen;	/* new len */
		if (l <= m->__m_extlen) {
			panic("extlen too large");
		}

		*m_new = *m;	// copy
		m_new->m_flags &= ~M_STACK;
		m_new->__m_extbuf = p; // point to new buffer
		_pkt_copy(m->__m_extbuf, p, m->__m_extlen);
		m_new->m_data = p + ofs;
		m = m_new;
	}
#endif /* USERSPACE */
	if (q->head == NULL)
		q->head = m;
	else
		q->tail->m_nextpkt = m;
	q->count++;
	q->tail = m;
	m->m_nextpkt = NULL;
}
#endif /* NEW_AQM */

#endif /* _IP_DN_PRIVATE_H */
