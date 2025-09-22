/*	$OpenBSD: hfsc.c,v 1.51 2025/07/07 02:28:50 jsg Exp $	*/

/*
 * Copyright (c) 2012-2013 Henning Brauer <henning@openbsd.org>
 * Copyright (c) 1997-1999 Carnegie Mellon University. All Rights Reserved.
 *
 * Permission to use, copy, modify, and distribute this software and
 * its documentation is hereby granted (including for commercial or
 * for-profit use), provided that both the copyright notice and this
 * permission notice appear in all copies of the software, derivative
 * works, or modified versions, and any portions thereof.
 *
 * THIS SOFTWARE IS EXPERIMENTAL AND IS KNOWN TO HAVE BUGS, SOME OF
 * WHICH MAY HAVE SERIOUS CONSEQUENCES.  CARNEGIE MELLON PROVIDES THIS
 * SOFTWARE IN ITS ``AS IS'' CONDITION, AND ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED.  IN NO EVENT SHALL CARNEGIE MELLON UNIVERSITY BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT
 * OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE
 * USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH
 * DAMAGE.
 *
 * Carnegie Mellon encourages (but does not require) users of this
 * software to return any improvements or extensions that they make,
 * and to grant Carnegie Mellon the rights to redistribute these
 * changes without encumbrance.
 */
/*
 * H-FSC is described in Proceedings of SIGCOMM'97,
 * "A Hierarchical Fair Service Curve Algorithm for Link-Sharing,
 * Real-Time and Priority Service"
 * by Ion Stoica, Hui Zhang, and T. S. Eugene Ng.
 *
 * Oleg Cherevko <olwi@aq.ml.com.ua> added the upperlimit for link-sharing.
 * when a class has an upperlimit, the fit-time is computed from the
 * upperlimit service curve.  the link-sharing scheduler does not schedule
 * a class whose fit-time exceeds the current time.
 */

#include <sys/param.h>
#include <sys/malloc.h>
#include <sys/pool.h>
#include <sys/mbuf.h>
#include <sys/systm.h>
#include <sys/errno.h>
#include <sys/queue.h>
#include <sys/kernel.h>
#include <sys/timeout.h>

#include <net/if.h>
#include <net/if_var.h>

#include <net/pfvar.h>
#include <net/hfsc.h>

/*
 * kernel internal service curve representation
 *	coordinates are given by 64 bit unsigned integers.
 *	x-axis: unit is clock count.  for the intel x86 architecture,
 *		the raw Pentium TSC (Timestamp Counter) value is used.
 *		virtual time is also calculated in this time scale.
 *	y-axis: unit is byte.
 *
 *	the service curve parameters are converted to the internal
 *	representation.
 *	the slope values are scaled to avoid overflow.
 *	the inverse slope values as well as the y-projection of the 1st
 *	segment are kept in order to avoid 64-bit divide operations
 *	that are expensive on 32-bit architectures.
 *
 *  note: Intel Pentium TSC never wraps around in several thousands of years.
 *	x-axis doesn't wrap around for 1089 years with 1GHz clock.
 *      y-axis doesn't wrap around for 4358 years with 1Gbps bandwidth.
 */

/* kernel internal representation of a service curve */
struct hfsc_internal_sc {
	u_int64_t	sm1;	/* scaled slope of the 1st segment */
	u_int64_t	ism1;	/* scaled inverse-slope of the 1st segment */
	u_int64_t	dx;	/* the x-projection of the 1st segment */
	u_int64_t	dy;	/* the y-projection of the 1st segment */
	u_int64_t	sm2;	/* scaled slope of the 2nd segment */
	u_int64_t	ism2;	/* scaled inverse-slope of the 2nd segment */
};

/* runtime service curve */
struct hfsc_runtime_sc {
	u_int64_t	x;	/* current starting position on x-axis */
	u_int64_t	y;	/* current starting position on x-axis */
	u_int64_t	sm1;	/* scaled slope of the 1st segment */
	u_int64_t	ism1;	/* scaled inverse-slope of the 1st segment */
	u_int64_t	dx;	/* the x-projection of the 1st segment */
	u_int64_t	dy;	/* the y-projection of the 1st segment */
	u_int64_t	sm2;	/* scaled slope of the 2nd segment */
	u_int64_t	ism2;	/* scaled inverse-slope of the 2nd segment */
};

struct hfsc_classq {
	struct mbuf_list q;	 /* Queue of packets */
	int		 qlimit; /* Queue limit */
};

/* for TAILQ based ellist and actlist implementation */
struct hfsc_class;
TAILQ_HEAD(hfsc_eligible, hfsc_class);
TAILQ_HEAD(hfsc_active, hfsc_class);
#define	hfsc_actlist_last(s)		TAILQ_LAST(s, hfsc_active)

struct hfsc_class {
	u_int		cl_id;		/* class id (just for debug) */
	u_int32_t	cl_handle;	/* class handle */
	int		cl_flags;	/* misc flags */

	struct hfsc_class *cl_parent;	/* parent class */
	struct hfsc_class *cl_siblings;	/* sibling classes */
	struct hfsc_class *cl_children;	/* child classes */

	struct hfsc_classq cl_q;	/* class queue structure */

	const struct pfq_ops *cl_qops;	/* queue manager */
	void		*cl_qdata;	/* queue manager data */
	void		*cl_cookie;	/* queue manager cookie */

	u_int64_t	cl_total;	/* total work in bytes */
	u_int64_t	cl_cumul;	/* cumulative work in bytes
					   done by real-time criteria */
	u_int64_t	cl_d;		/* deadline */
	u_int64_t	cl_e;		/* eligible time */
	u_int64_t	cl_vt;		/* virtual time */
	u_int64_t	cl_f;		/* time when this class will fit for
					   link-sharing, max(myf, cfmin) */
	u_int64_t	cl_myf;		/* my fit-time (as calculated from this
					   class's own upperlimit curve) */
	u_int64_t	cl_myfadj;	/* my fit-time adjustment
					   (to cancel history dependence) */
	u_int64_t	cl_cfmin;	/* earliest children's fit-time (used
					   with cl_myf to obtain cl_f) */
	u_int64_t	cl_cvtmin;	/* minimal virtual time among the
					   children fit for link-sharing
					   (monotonic within a period) */
	u_int64_t	cl_vtadj;	/* intra-period cumulative vt
					   adjustment */
	u_int64_t	cl_vtoff;	/* inter-period cumulative vt offset */
	u_int64_t	cl_cvtmax;	/* max child's vt in the last period */

	u_int64_t	cl_initvt;	/* init virtual time (for debugging) */

	struct hfsc_internal_sc *cl_rsc; /* internal real-time service curve */
	struct hfsc_internal_sc *cl_fsc; /* internal fair service curve */
	struct hfsc_internal_sc *cl_usc; /* internal upperlimit service curve */
	struct hfsc_runtime_sc   cl_deadline; /* deadline curve */
	struct hfsc_runtime_sc   cl_eligible; /* eligible curve */
	struct hfsc_runtime_sc   cl_virtual;  /* virtual curve */
	struct hfsc_runtime_sc   cl_ulimit;   /* upperlimit curve */

	u_int		cl_vtperiod;	/* vt period sequence no */
	u_int		cl_parentperiod;  /* parent's vt period seqno */
	int		cl_nactive;	/* number of active children */
	struct hfsc_active	cl_actc; /* active children list */

	TAILQ_ENTRY(hfsc_class) cl_actlist; /* active children list entry */
	TAILQ_ENTRY(hfsc_class) cl_ellist; /* eligible list entry */

	struct {
		struct hfsc_pktcntr xmit_cnt;
		struct hfsc_pktcntr drop_cnt;
		u_int period;
	} cl_stats;
};

/*
 * hfsc interface state
 */
struct hfsc_if {
	struct hfsc_if		*hif_next;	/* interface state list */
	struct hfsc_class	*hif_rootclass;		/* root class */
	struct hfsc_class	*hif_defaultclass;	/* default class */
	struct hfsc_class	**hif_class_tbl;

	u_int64_t		hif_microtime;	/* time at deq_begin */

	u_int	hif_allocated;			/* # of slots in hif_class_tbl */
	u_int	hif_classes;			/* # of classes in the tree */
	u_int	hif_classid;			/* class id sequence number */

	struct hfsc_eligible hif_eligible;	/* eligible list */
	struct timeout hif_defer;	/* for queues that weren't ready */
};

/*
 * function prototypes
 */
struct hfsc_class	*hfsc_class_create(struct hfsc_if *,
			    struct hfsc_sc *, struct hfsc_sc *,
			    struct hfsc_sc *, struct hfsc_class *, int,
			    int, int);
int			 hfsc_class_destroy(struct hfsc_if *,
			    struct hfsc_class *);
struct hfsc_class	*hfsc_nextclass(struct hfsc_class *);

void		 hfsc_cl_purge(struct hfsc_if *, struct hfsc_class *,
		     struct mbuf_list *);

void		 hfsc_update_sc(struct hfsc_if *, struct hfsc_class *, int);
void		 hfsc_deferred(void *);
void		 hfsc_update_cfmin(struct hfsc_class *);
void		 hfsc_set_active(struct hfsc_if *, struct hfsc_class *, int);
void		 hfsc_set_passive(struct hfsc_if *, struct hfsc_class *);
void		 hfsc_init_ed(struct hfsc_if *, struct hfsc_class *, int);
void		 hfsc_update_ed(struct hfsc_if *, struct hfsc_class *, int);
void		 hfsc_update_d(struct hfsc_class *, int);
void		 hfsc_init_vf(struct hfsc_class *, int);
void		 hfsc_update_vf(struct hfsc_class *, int, u_int64_t);
void		 hfsc_ellist_insert(struct hfsc_if *, struct hfsc_class *);
void		 hfsc_ellist_remove(struct hfsc_if *, struct hfsc_class *);
void		 hfsc_ellist_update(struct hfsc_if *, struct hfsc_class *);
struct hfsc_class	*hfsc_ellist_get_mindl(struct hfsc_if *, u_int64_t);
void		 hfsc_actlist_insert(struct hfsc_class *);
void		 hfsc_actlist_remove(struct hfsc_class *);
void		 hfsc_actlist_update(struct hfsc_class *);

struct hfsc_class	*hfsc_actlist_firstfit(struct hfsc_class *,
				    u_int64_t);

static __inline u_int64_t	seg_x2y(u_int64_t, u_int64_t);
static __inline u_int64_t	seg_y2x(u_int64_t, u_int64_t);
static __inline u_int64_t	m2sm(u_int);
static __inline u_int64_t	m2ism(u_int);
static __inline u_int64_t	d2dx(u_int);
static __inline u_int		sm2m(u_int64_t);
static __inline u_int		dx2d(u_int64_t);

void		hfsc_sc2isc(struct hfsc_sc *, struct hfsc_internal_sc *);
void		hfsc_rtsc_init(struct hfsc_runtime_sc *,
		    struct hfsc_internal_sc *, u_int64_t, u_int64_t);
u_int64_t	hfsc_rtsc_y2x(struct hfsc_runtime_sc *, u_int64_t);
u_int64_t	hfsc_rtsc_x2y(struct hfsc_runtime_sc *, u_int64_t);
void		hfsc_rtsc_min(struct hfsc_runtime_sc *,
		    struct hfsc_internal_sc *, u_int64_t, u_int64_t);

void		hfsc_getclstats(struct hfsc_class_stats *, struct hfsc_class *);
struct hfsc_class	*hfsc_clh2cph(struct hfsc_if *, u_int32_t);

#define	HFSC_FREQ		1000000000LL
#define	HFSC_CLK_PER_TICK	tick_nsec
#define	HFSC_HT_INFINITY	0xffffffffffffffffLL /* infinite time value */

#define hfsc_uptime()		nsecuptime()

struct pool	hfsc_class_pl, hfsc_internal_sc_pl;

/*
 * ifqueue glue.
 */

unsigned int	 hfsc_idx(unsigned int, const struct mbuf *);
struct mbuf	*hfsc_enq(struct ifqueue *, struct mbuf *);
struct mbuf	*hfsc_deq_begin(struct ifqueue *, void **);
void		 hfsc_deq_commit(struct ifqueue *, struct mbuf *, void *);
void		 hfsc_purge(struct ifqueue *, struct mbuf_list *);
void		*hfsc_alloc(unsigned int, void *);
void		 hfsc_free(unsigned int, void *);

const struct ifq_ops hfsc_ops = {
	hfsc_idx,
	hfsc_enq,
	hfsc_deq_begin,
	hfsc_deq_commit,
	hfsc_purge,
	hfsc_alloc,
	hfsc_free,
};

const struct ifq_ops * const ifq_hfsc_ops = &hfsc_ops;

/*
 * pf queue glue.
 */

void		*hfsc_pf_alloc(struct ifnet *);
int		 hfsc_pf_addqueue(void *, struct pf_queuespec *);
void		 hfsc_pf_free(void *);
int		 hfsc_pf_qstats(struct pf_queuespec *, void *, int *);
unsigned int	 hfsc_pf_qlength(void *);
struct mbuf *	 hfsc_pf_enqueue(void *, struct mbuf *);
struct mbuf *	 hfsc_pf_deq_begin(void *, void **, struct mbuf_list *);
void		 hfsc_pf_deq_commit(void *, struct mbuf *, void *);
void		 hfsc_pf_purge(void *, struct mbuf_list *);

const struct pfq_ops hfsc_pf_ops = {
	hfsc_pf_alloc,
	hfsc_pf_addqueue,
	hfsc_pf_free,
	hfsc_pf_qstats,
	hfsc_pf_qlength,
	hfsc_pf_enqueue,
	hfsc_pf_deq_begin,
	hfsc_pf_deq_commit,
	hfsc_pf_purge
};

const struct pfq_ops * const pfq_hfsc_ops = &hfsc_pf_ops;

/*
 * shortcuts for repeated use
 */
static inline unsigned int
hfsc_class_qlength(struct hfsc_class *cl)
{
	/* Only leaf classes have a queue */
	if (cl->cl_qops != NULL)
		return cl->cl_qops->pfq_qlength(cl->cl_qdata);
	return 0;
}

static inline struct mbuf *
hfsc_class_enqueue(struct hfsc_class *cl, struct mbuf *m)
{
	return cl->cl_qops->pfq_enqueue(cl->cl_qdata, m);
}

static inline struct mbuf *
hfsc_class_deq_begin(struct hfsc_class *cl, struct mbuf_list *ml)
{
	return cl->cl_qops->pfq_deq_begin(cl->cl_qdata, &cl->cl_cookie, ml);
}

static inline void
hfsc_class_deq_commit(struct hfsc_class *cl, struct mbuf *m)
{
	return cl->cl_qops->pfq_deq_commit(cl->cl_qdata, m, cl->cl_cookie);
}

static inline void
hfsc_class_purge(struct hfsc_class *cl, struct mbuf_list *ml)
{
	/* Only leaf classes have a queue */
	if (cl->cl_qops != NULL)
		return cl->cl_qops->pfq_purge(cl->cl_qdata, ml);
}

static inline u_int
hfsc_more_slots(u_int current)
{
	u_int want = current * 2;

	return (want > HFSC_MAX_CLASSES ? HFSC_MAX_CLASSES : want);
}

static void
hfsc_grow_class_tbl(struct hfsc_if *hif, u_int howmany)
{
	struct hfsc_class **newtbl, **old;
	size_t oldlen = sizeof(void *) * hif->hif_allocated;

	newtbl = mallocarray(howmany, sizeof(void *), M_DEVBUF,
	    M_WAITOK | M_ZERO);
	old = hif->hif_class_tbl;

	memcpy(newtbl, old, oldlen);
	hif->hif_class_tbl = newtbl;
	hif->hif_allocated = howmany;

	free(old, M_DEVBUF, oldlen);
}

void
hfsc_initialize(void)
{
	pool_init(&hfsc_class_pl, sizeof(struct hfsc_class), 0,
	    IPL_NONE, PR_WAITOK, "hfscclass", NULL);
	pool_init(&hfsc_internal_sc_pl, sizeof(struct hfsc_internal_sc), 0,
	    IPL_NONE, PR_WAITOK, "hfscintsc", NULL);
}

void *
hfsc_pf_alloc(struct ifnet *ifp)
{
	struct hfsc_if *hif;

	KASSERT(ifp != NULL);

	hif = malloc(sizeof(*hif), M_DEVBUF, M_WAITOK | M_ZERO);
	TAILQ_INIT(&hif->hif_eligible);
	hif->hif_class_tbl = mallocarray(HFSC_DEFAULT_CLASSES, sizeof(void *),
	    M_DEVBUF, M_WAITOK | M_ZERO);
	hif->hif_allocated = HFSC_DEFAULT_CLASSES;

	timeout_set(&hif->hif_defer, hfsc_deferred, ifp);

	return (hif);
}

int
hfsc_pf_addqueue(void *arg, struct pf_queuespec *q)
{
	struct hfsc_if *hif = arg;
	struct hfsc_class *cl, *parent, *np = NULL;
	struct hfsc_sc rtsc, lssc, ulsc;
	int error = 0;

	KASSERT(hif != NULL);
	KASSERT(q->qid != 0);

	/* Root queue must have non-zero linksharing parameters */
	if (q->linkshare.m1.absolute == 0 && q->linkshare.m2.absolute == 0 &&
	    q->parent_qid == 0)
		return (EINVAL);

	if (q->parent_qid == 0 && hif->hif_rootclass == NULL) {
		np = hfsc_class_create(hif, NULL, NULL, NULL, NULL,
		    0, 0, HFSC_ROOT_CLASS | q->qid);
		if (np == NULL)
			return (EINVAL);
		parent = np;
	} else if ((parent = hfsc_clh2cph(hif, q->parent_qid)) == NULL)
		return (EINVAL);

	if (hfsc_clh2cph(hif, q->qid) != NULL) {
		hfsc_class_destroy(hif, np);
		return (EBUSY);
	}

	rtsc.m1 = q->realtime.m1.absolute;
	rtsc.d  = q->realtime.d;
	rtsc.m2 = q->realtime.m2.absolute;
	lssc.m1 = q->linkshare.m1.absolute;
	lssc.d  = q->linkshare.d;
	lssc.m2 = q->linkshare.m2.absolute;
	ulsc.m1 = q->upperlimit.m1.absolute;
	ulsc.d  = q->upperlimit.d;
	ulsc.m2 = q->upperlimit.m2.absolute;

	if ((cl = hfsc_class_create(hif, &rtsc, &lssc, &ulsc,
	    parent, q->qlimit, q->flags, q->qid)) == NULL) {
		hfsc_class_destroy(hif, np);
		return (ENOMEM);
	}

	/* Attach a queue manager if specified */
	cl->cl_qops = pf_queue_manager(q);
	/* Realtime class cannot be used with an external queue manager */
	if (cl->cl_qops == NULL || cl->cl_rsc != NULL) {
		cl->cl_qops = pfq_hfsc_ops;
		cl->cl_qdata = &cl->cl_q;
	} else {
		cl->cl_qdata = cl->cl_qops->pfq_alloc(q->kif->pfik_ifp);
		if (cl->cl_qdata == NULL) {
			cl->cl_qops = NULL;
			hfsc_class_destroy(hif, cl);
			hfsc_class_destroy(hif, np);
			return (ENOMEM);
		}
		error = cl->cl_qops->pfq_addqueue(cl->cl_qdata, q);
		if (error) {
			cl->cl_qops->pfq_free(cl->cl_qdata);
			cl->cl_qops = NULL;
			hfsc_class_destroy(hif, cl);
			hfsc_class_destroy(hif, np);
			return (error);
		}
	}

	KASSERT(cl->cl_qops != NULL);
	KASSERT(cl->cl_qdata != NULL);

	return (0);
}

int
hfsc_pf_qstats(struct pf_queuespec *q, void *ubuf, int *nbytes)
{
	struct ifnet *ifp = q->kif->pfik_ifp;
	struct hfsc_if *hif;
	struct hfsc_class *cl;
	struct hfsc_class_stats stats;
	int error = 0;

	if (ifp == NULL)
		return (EBADF);

	if (*nbytes < sizeof(stats))
		return (EINVAL);

	hif = ifq_q_enter(&ifp->if_snd, ifq_hfsc_ops);
	if (hif == NULL)
		return (EBADF);

	if ((cl = hfsc_clh2cph(hif, q->qid)) == NULL) {
		ifq_q_leave(&ifp->if_snd, hif);
		return (EINVAL);
	}

	hfsc_getclstats(&stats, cl);
	ifq_q_leave(&ifp->if_snd, hif);

	if ((error = copyout((caddr_t)&stats, ubuf, sizeof(stats))) != 0)
		return (error);

	*nbytes = sizeof(stats);
	return (0);
}

void
hfsc_pf_free(void *arg)
{
	hfsc_free(0, arg);
}

unsigned int
hfsc_pf_qlength(void *arg)
{
	struct hfsc_classq *cq = arg;

	return ml_len(&cq->q);
}

struct mbuf *
hfsc_pf_enqueue(void *arg, struct mbuf *m)
{
	struct hfsc_classq *cq = arg;

	if (ml_len(&cq->q) >= cq->qlimit)
		return (m);

	ml_enqueue(&cq->q, m);
	return (NULL);
}

struct mbuf *
hfsc_pf_deq_begin(void *arg, void **cookiep, struct mbuf_list *free_ml)
{
	struct hfsc_classq *cq = arg;

	return MBUF_LIST_FIRST(&cq->q);
}

void
hfsc_pf_deq_commit(void *arg, struct mbuf *m, void *cookie)
{
	struct hfsc_classq *cq = arg;

	ml_dequeue(&cq->q);
}

void
hfsc_pf_purge(void *arg, struct mbuf_list *ml)
{
	struct hfsc_classq *cq = arg;

	ml_enlist(ml, &cq->q);
}

unsigned int
hfsc_idx(unsigned int nqueues, const struct mbuf *m)
{
	/*
	 * hfsc can only function on a single ifq and the stack understands
	 * this. when the first ifq on an interface is switched to hfsc,
	 * this gets used to map all mbufs to the first and only ifq that
	 * is set up for hfsc.
	 */
	return (0);
}

void *
hfsc_alloc(unsigned int idx, void *q)
{
	struct hfsc_if *hif = q;

	KASSERT(idx == 0); /* when hfsc is enabled we only use the first ifq */
	KASSERT(hif != NULL);
	return (hif);
}

void
hfsc_free(unsigned int idx, void *q)
{
	struct hfsc_if *hif = q;
	struct hfsc_class *cl;
	int i, restart;

	KERNEL_ASSERT_LOCKED();
	KASSERT(idx == 0); /* when hfsc is enabled we only use the first ifq */

	timeout_del(&hif->hif_defer);

	do {
		restart = 0;
		for (i = 0; i < hif->hif_allocated; i++) {
			cl = hif->hif_class_tbl[i];
			if (hfsc_class_destroy(hif, cl) == EBUSY)
				restart++;
		}
	} while (restart > 0);

	free(hif->hif_class_tbl, M_DEVBUF, hif->hif_allocated * sizeof(void *));
	free(hif, M_DEVBUF, sizeof(*hif));
}

void
hfsc_purge(struct ifqueue *ifq, struct mbuf_list *ml)
{
	struct hfsc_if		*hif = ifq->ifq_q;
	struct hfsc_class	*cl;

	for (cl = hif->hif_rootclass; cl != NULL; cl = hfsc_nextclass(cl))
		hfsc_cl_purge(hif, cl, ml);
}

struct hfsc_class *
hfsc_class_create(struct hfsc_if *hif, struct hfsc_sc *rsc,
    struct hfsc_sc *fsc, struct hfsc_sc *usc, struct hfsc_class *parent,
    int qlimit, int flags, int qid)
{
	struct hfsc_class *cl, *p;
	int i, s;

	if (qlimit == 0)
		qlimit = HFSC_DEFAULT_QLIMIT;

	if (hif->hif_classes >= hif->hif_allocated) {
		u_int newslots = hfsc_more_slots(hif->hif_allocated);

		if (newslots == hif->hif_allocated)
			return (NULL);
		hfsc_grow_class_tbl(hif, newslots);
	}

	cl = pool_get(&hfsc_class_pl, PR_WAITOK | PR_ZERO);
	TAILQ_INIT(&cl->cl_actc);

	ml_init(&cl->cl_q.q);
	cl->cl_q.qlimit = qlimit;
	cl->cl_flags = flags;

	if (rsc != NULL && (rsc->m1 != 0 || rsc->m2 != 0)) {
		cl->cl_rsc = pool_get(&hfsc_internal_sc_pl, PR_WAITOK);
		hfsc_sc2isc(rsc, cl->cl_rsc);
		hfsc_rtsc_init(&cl->cl_deadline, cl->cl_rsc, 0, 0);
		hfsc_rtsc_init(&cl->cl_eligible, cl->cl_rsc, 0, 0);
	}
	if (fsc != NULL && (fsc->m1 != 0 || fsc->m2 != 0)) {
		cl->cl_fsc = pool_get(&hfsc_internal_sc_pl, PR_WAITOK);
		hfsc_sc2isc(fsc, cl->cl_fsc);
		hfsc_rtsc_init(&cl->cl_virtual, cl->cl_fsc, 0, 0);
	}
	if (usc != NULL && (usc->m1 != 0 || usc->m2 != 0)) {
		cl->cl_usc = pool_get(&hfsc_internal_sc_pl, PR_WAITOK);
		hfsc_sc2isc(usc, cl->cl_usc);
		hfsc_rtsc_init(&cl->cl_ulimit, cl->cl_usc, 0, 0);
	}

	cl->cl_id = hif->hif_classid++;
	cl->cl_handle = qid;
	cl->cl_parent = parent;

	s = splnet();
	hif->hif_classes++;

	/*
	 * find a free slot in the class table.  if the slot matching
	 * the lower bits of qid is free, use this slot.  otherwise,
	 * use the first free slot.
	 */
	i = qid % hif->hif_allocated;
	if (hif->hif_class_tbl[i] == NULL)
		hif->hif_class_tbl[i] = cl;
	else {
		for (i = 0; i < hif->hif_allocated; i++)
			if (hif->hif_class_tbl[i] == NULL) {
				hif->hif_class_tbl[i] = cl;
				break;
			}
		if (i == hif->hif_allocated) {
			splx(s);
			goto err_ret;
		}
	}

	if (flags & HFSC_DEFAULTCLASS)
		hif->hif_defaultclass = cl;

	if (parent == NULL)
		hif->hif_rootclass = cl;
	else {
		/* add this class to the children list of the parent */
		if ((p = parent->cl_children) == NULL)
			parent->cl_children = cl;
		else {
			while (p->cl_siblings != NULL)
				p = p->cl_siblings;
			p->cl_siblings = cl;
		}
	}
	splx(s);

	return (cl);

err_ret:
	if (cl->cl_fsc != NULL)
		pool_put(&hfsc_internal_sc_pl, cl->cl_fsc);
	if (cl->cl_rsc != NULL)
		pool_put(&hfsc_internal_sc_pl, cl->cl_rsc);
	if (cl->cl_usc != NULL)
		pool_put(&hfsc_internal_sc_pl, cl->cl_usc);
	pool_put(&hfsc_class_pl, cl);
	return (NULL);
}

int
hfsc_class_destroy(struct hfsc_if *hif, struct hfsc_class *cl)
{
	int i, s;

	if (cl == NULL)
		return (0);

	if (cl->cl_children != NULL)
		return (EBUSY);

	s = splnet();
	KASSERT(hfsc_class_qlength(cl) == 0);

	if (cl->cl_parent != NULL) {
		struct hfsc_class *p = cl->cl_parent->cl_children;

		if (p == cl)
			cl->cl_parent->cl_children = cl->cl_siblings;
		else do {
			if (p->cl_siblings == cl) {
				p->cl_siblings = cl->cl_siblings;
				break;
			}
		} while ((p = p->cl_siblings) != NULL);
	}

	for (i = 0; i < hif->hif_allocated; i++)
		if (hif->hif_class_tbl[i] == cl) {
			hif->hif_class_tbl[i] = NULL;
			break;
		}

	hif->hif_classes--;
	splx(s);

	KASSERT(TAILQ_EMPTY(&cl->cl_actc));

	if (cl == hif->hif_rootclass)
		hif->hif_rootclass = NULL;
	if (cl == hif->hif_defaultclass)
		hif->hif_defaultclass = NULL;

	/* Free external queue manager resources */
	if (cl->cl_qops && cl->cl_qops != pfq_hfsc_ops)
		cl->cl_qops->pfq_free(cl->cl_qdata);

	if (cl->cl_usc != NULL)
		pool_put(&hfsc_internal_sc_pl, cl->cl_usc);
	if (cl->cl_fsc != NULL)
		pool_put(&hfsc_internal_sc_pl, cl->cl_fsc);
	if (cl->cl_rsc != NULL)
		pool_put(&hfsc_internal_sc_pl, cl->cl_rsc);
	pool_put(&hfsc_class_pl, cl);

	return (0);
}

/*
 * hfsc_nextclass returns the next class in the tree.
 *   usage:
 *	for (cl = hif->hif_rootclass; cl != NULL; cl = hfsc_nextclass(cl))
 *		do_something;
 */
struct hfsc_class *
hfsc_nextclass(struct hfsc_class *cl)
{
	if (cl->cl_children != NULL)
		cl = cl->cl_children;
	else if (cl->cl_siblings != NULL)
		cl = cl->cl_siblings;
	else {
		while ((cl = cl->cl_parent) != NULL)
			if (cl->cl_siblings) {
				cl = cl->cl_siblings;
				break;
			}
	}

	return (cl);
}

struct mbuf *
hfsc_enq(struct ifqueue *ifq, struct mbuf *m)
{
	struct hfsc_if *hif = ifq->ifq_q;
	struct hfsc_class *cl;
	struct mbuf *dm;

	if ((cl = hfsc_clh2cph(hif, m->m_pkthdr.pf.qid)) == NULL ||
	    cl->cl_children != NULL) {
		cl = hif->hif_defaultclass;
		if (cl == NULL)
			return (m);
	}

	dm = hfsc_class_enqueue(cl, m);

	/* successfully queued. */
	if (dm != m && hfsc_class_qlength(cl) == 1) {
		hfsc_set_active(hif, cl, m->m_pkthdr.len);
		if (!timeout_pending(&hif->hif_defer))
			timeout_add(&hif->hif_defer, 1);
	}

	/* drop occurred. */
	if (dm != NULL)
		PKTCNTR_INC(&cl->cl_stats.drop_cnt, dm->m_pkthdr.len);

	return (dm);
}

struct mbuf *
hfsc_deq_begin(struct ifqueue *ifq, void **cookiep)
{
	struct mbuf_list free_ml = MBUF_LIST_INITIALIZER();
	struct hfsc_if *hif = ifq->ifq_q;
	struct hfsc_class *cl, *tcl;
	struct mbuf *m;
	u_int64_t cur_time;

	cur_time = hfsc_uptime();

	/*
	 * if there are eligible classes, use real-time criteria.
	 * find the class with the minimum deadline among
	 * the eligible classes.
	 */
	cl = hfsc_ellist_get_mindl(hif, cur_time);
	if (cl == NULL) {
		/*
		 * use link-sharing criteria
		 * get the class with the minimum vt in the hierarchy
		 */
		cl = NULL;
		tcl = hif->hif_rootclass;

		while (tcl != NULL && tcl->cl_children != NULL) {
			tcl = hfsc_actlist_firstfit(tcl, cur_time);
			if (tcl == NULL)
				continue;

			/*
			 * update parent's cl_cvtmin.
			 * don't update if the new vt is smaller.
			 */
			if (tcl->cl_parent->cl_cvtmin < tcl->cl_vt)
				tcl->cl_parent->cl_cvtmin = tcl->cl_vt;

			cl = tcl;
		}
		/* XXX HRTIMER plan hfsc_deferred precisely here. */
		if (cl == NULL)
			return (NULL);
	}

	m = hfsc_class_deq_begin(cl, &free_ml);
	ifq_mfreeml(ifq, &free_ml);
	if (m == NULL) {
		hfsc_update_sc(hif, cl, 0);
		return (NULL);
	}

	hif->hif_microtime = cur_time;
	*cookiep = cl;
	return (m);
}

void
hfsc_deq_commit(struct ifqueue *ifq, struct mbuf *m, void *cookie)
{
	struct hfsc_if *hif = ifq->ifq_q;
	struct hfsc_class *cl = cookie;

	hfsc_class_deq_commit(cl, m);
	hfsc_update_sc(hif, cl, m->m_pkthdr.len);

	PKTCNTR_INC(&cl->cl_stats.xmit_cnt, m->m_pkthdr.len);
}

void
hfsc_update_sc(struct hfsc_if *hif, struct hfsc_class *cl, int len)
{
	int next_len, realtime = 0;
	u_int64_t cur_time = hif->hif_microtime;

	/* check if the class was scheduled by real-time criteria */
	if (cl->cl_rsc != NULL)
		realtime = (cl->cl_e <= cur_time);

	hfsc_update_vf(cl, len, cur_time);
	if (realtime)
		cl->cl_cumul += len;

	if (hfsc_class_qlength(cl) > 0) {
		/*
		 * Realtime queue needs to look into the future and make
		 * calculations based on that. This is the reason it can't
		 * be used with an external queue manager.
		 */
		if (cl->cl_rsc != NULL) {
			struct mbuf *m0;

			/* update ed */
			KASSERT(cl->cl_qops == pfq_hfsc_ops);
			m0 = MBUF_LIST_FIRST(&cl->cl_q.q);
			next_len = m0->m_pkthdr.len;

			if (realtime)
				hfsc_update_ed(hif, cl, next_len);
			else
				hfsc_update_d(cl, next_len);
		}
	} else {
		/* the class becomes passive */
		hfsc_set_passive(hif, cl);
	}
}

void
hfsc_deferred(void *arg)
{
	struct ifnet *ifp = arg;
	struct ifqueue *ifq = &ifp->if_snd;
	struct hfsc_if *hif;

	if (!HFSC_ENABLED(ifq))
		return;

	if (!ifq_empty(ifq))
		ifq_start(ifq);

	hif = ifq_q_enter(&ifp->if_snd, ifq_hfsc_ops);
	if (hif == NULL)
		return;
	/* XXX HRTIMER nearest virtual/fit time is likely less than 1/HZ. */
	timeout_add(&hif->hif_defer, 1);
	ifq_q_leave(&ifp->if_snd, hif);
}

void
hfsc_cl_purge(struct hfsc_if *hif, struct hfsc_class *cl, struct mbuf_list *ml)
{
	struct mbuf_list ml2 = MBUF_LIST_INITIALIZER();

	hfsc_class_purge(cl, &ml2);
	if (ml_empty(&ml2))
		return;

	ml_enlist(ml, &ml2);

	hfsc_update_vf(cl, 0, 0);	/* remove cl from the actlist */
	hfsc_set_passive(hif, cl);
}

void
hfsc_set_active(struct hfsc_if *hif, struct hfsc_class *cl, int len)
{
	if (cl->cl_rsc != NULL)
		hfsc_init_ed(hif, cl, len);
	if (cl->cl_fsc != NULL)
		hfsc_init_vf(cl, len);

	cl->cl_stats.period++;
}

void
hfsc_set_passive(struct hfsc_if *hif, struct hfsc_class *cl)
{
	if (cl->cl_rsc != NULL)
		hfsc_ellist_remove(hif, cl);

	/*
	 * actlist is handled in hfsc_update_vf() so that hfsc_update_vf(cl, 0,
	 * 0) needs to be called explicitly to remove a class from actlist
	 */
}

void
hfsc_init_ed(struct hfsc_if *hif, struct hfsc_class *cl, int next_len)
{
	u_int64_t cur_time;

	cur_time = hfsc_uptime();

	/* update the deadline curve */
	hfsc_rtsc_min(&cl->cl_deadline, cl->cl_rsc, cur_time, cl->cl_cumul);

	/*
	 * update the eligible curve.
	 * for concave, it is equal to the deadline curve.
	 * for convex, it is a linear curve with slope m2.
	 */
	cl->cl_eligible = cl->cl_deadline;
	if (cl->cl_rsc->sm1 <= cl->cl_rsc->sm2) {
		cl->cl_eligible.dx = 0;
		cl->cl_eligible.dy = 0;
	}

	/* compute e and d */
	cl->cl_e = hfsc_rtsc_y2x(&cl->cl_eligible, cl->cl_cumul);
	cl->cl_d = hfsc_rtsc_y2x(&cl->cl_deadline, cl->cl_cumul + next_len);

	hfsc_ellist_insert(hif, cl);
}

void
hfsc_update_ed(struct hfsc_if *hif, struct hfsc_class *cl, int next_len)
{
	cl->cl_e = hfsc_rtsc_y2x(&cl->cl_eligible, cl->cl_cumul);
	cl->cl_d = hfsc_rtsc_y2x(&cl->cl_deadline, cl->cl_cumul + next_len);

	hfsc_ellist_update(hif, cl);
}

void
hfsc_update_d(struct hfsc_class *cl, int next_len)
{
	cl->cl_d = hfsc_rtsc_y2x(&cl->cl_deadline, cl->cl_cumul + next_len);
}

void
hfsc_init_vf(struct hfsc_class *cl, int len)
{
	struct hfsc_class *max_cl, *p;
	u_int64_t vt, f, cur_time;
	int go_active;

	cur_time = 0;
	go_active = 1;
	for ( ; cl->cl_parent != NULL; cl = cl->cl_parent) {
		if (go_active && cl->cl_nactive++ == 0)
			go_active = 1;
		else
			go_active = 0;

		if (go_active) {
			max_cl = TAILQ_LAST(&cl->cl_parent->cl_actc,
			    hfsc_active);
			if (max_cl != NULL) {
				/*
				 * set vt to the average of the min and max
				 * classes.  if the parent's period didn't
				 * change, don't decrease vt of the class.
				 */
				vt = max_cl->cl_vt;
				if (cl->cl_parent->cl_cvtmin != 0)
					vt = (cl->cl_parent->cl_cvtmin + vt)/2;

				if (cl->cl_parent->cl_vtperiod !=
				    cl->cl_parentperiod || vt > cl->cl_vt)
					cl->cl_vt = vt;
			} else {
				/*
				 * first child for a new parent backlog period.
				 * add parent's cvtmax to vtoff of children
				 * to make a new vt (vtoff + vt) larger than
				 * the vt in the last period for all children.
				 */
				vt = cl->cl_parent->cl_cvtmax;
				for (p = cl->cl_parent->cl_children; p != NULL;
				     p = p->cl_siblings)
					p->cl_vtoff += vt;
				cl->cl_vt = 0;
				cl->cl_parent->cl_cvtmax = 0;
				cl->cl_parent->cl_cvtmin = 0;
			}
			cl->cl_initvt = cl->cl_vt;

			/* update the virtual curve */
			vt = cl->cl_vt + cl->cl_vtoff;
			hfsc_rtsc_min(&cl->cl_virtual, cl->cl_fsc, vt,
			    cl->cl_total);
			if (cl->cl_virtual.x == vt) {
				cl->cl_virtual.x -= cl->cl_vtoff;
				cl->cl_vtoff = 0;
			}
			cl->cl_vtadj = 0;

			cl->cl_vtperiod++;  /* increment vt period */
			cl->cl_parentperiod = cl->cl_parent->cl_vtperiod;
			if (cl->cl_parent->cl_nactive == 0)
				cl->cl_parentperiod++;
			cl->cl_f = 0;

			hfsc_actlist_insert(cl);

			if (cl->cl_usc != NULL) {
				/* class has upper limit curve */
				if (cur_time == 0)
					cur_time = hfsc_uptime();

				/* update the ulimit curve */
				hfsc_rtsc_min(&cl->cl_ulimit, cl->cl_usc, cur_time,
				    cl->cl_total);
				/* compute myf */
				cl->cl_myf = hfsc_rtsc_y2x(&cl->cl_ulimit,
				    cl->cl_total);
				cl->cl_myfadj = 0;
			}
		}

		if (cl->cl_myf > cl->cl_cfmin)
			f = cl->cl_myf;
		else
			f = cl->cl_cfmin;
		if (f != cl->cl_f) {
			cl->cl_f = f;
			hfsc_update_cfmin(cl->cl_parent);
		}
	}
}

void
hfsc_update_vf(struct hfsc_class *cl, int len, u_int64_t cur_time)
{
	u_int64_t f, myf_bound, delta;
	int go_passive = 0;

	if (hfsc_class_qlength(cl) == 0)
		go_passive = 1;

	for (; cl->cl_parent != NULL; cl = cl->cl_parent) {
		cl->cl_total += len;

		if (cl->cl_fsc == NULL || cl->cl_nactive == 0)
			continue;

		if (go_passive && --cl->cl_nactive == 0)
			go_passive = 1;
		else
			go_passive = 0;

		if (go_passive) {
			/* no more active child, going passive */

			/* update cvtmax of the parent class */
			if (cl->cl_vt > cl->cl_parent->cl_cvtmax)
				cl->cl_parent->cl_cvtmax = cl->cl_vt;

			/* remove this class from the vt list */
			hfsc_actlist_remove(cl);

			hfsc_update_cfmin(cl->cl_parent);

			continue;
		}

		/*
		 * update vt and f
		 */
		cl->cl_vt = hfsc_rtsc_y2x(&cl->cl_virtual, cl->cl_total)
		    - cl->cl_vtoff + cl->cl_vtadj;

		/*
		 * if vt of the class is smaller than cvtmin,
		 * the class was skipped in the past due to non-fit.
		 * if so, we need to adjust vtadj.
		 */
		if (cl->cl_vt < cl->cl_parent->cl_cvtmin) {
			cl->cl_vtadj += cl->cl_parent->cl_cvtmin - cl->cl_vt;
			cl->cl_vt = cl->cl_parent->cl_cvtmin;
		}

		/* update the vt list */
		hfsc_actlist_update(cl);

		if (cl->cl_usc != NULL) {
			cl->cl_myf = cl->cl_myfadj +
			    hfsc_rtsc_y2x(&cl->cl_ulimit, cl->cl_total);

			/*
			 * if myf lags behind by more than one clock tick
			 * from the current time, adjust myfadj to prevent
			 * a rate-limited class from going greedy.
			 * in a steady state under rate-limiting, myf
			 * fluctuates within one clock tick.
			 */
			myf_bound = cur_time - HFSC_CLK_PER_TICK;
			if (cl->cl_myf < myf_bound) {
				delta = cur_time - cl->cl_myf;
				cl->cl_myfadj += delta;
				cl->cl_myf += delta;
			}
		}

		/* cl_f is max(cl_myf, cl_cfmin) */
		if (cl->cl_myf > cl->cl_cfmin)
			f = cl->cl_myf;
		else
			f = cl->cl_cfmin;
		if (f != cl->cl_f) {
			cl->cl_f = f;
			hfsc_update_cfmin(cl->cl_parent);
		}
	}
}

void
hfsc_update_cfmin(struct hfsc_class *cl)
{
	struct hfsc_class *p;
	u_int64_t cfmin;

	if (TAILQ_EMPTY(&cl->cl_actc)) {
		cl->cl_cfmin = 0;
		return;
	}
	cfmin = HFSC_HT_INFINITY;
	TAILQ_FOREACH(p, &cl->cl_actc, cl_actlist) {
		if (p->cl_f == 0) {
			cl->cl_cfmin = 0;
			return;
		}
		if (p->cl_f < cfmin)
			cfmin = p->cl_f;
	}
	cl->cl_cfmin = cfmin;
}

/*
 * eligible list holds backlogged classes being sorted by their eligible times.
 * there is one eligible list per interface.
 */
void
hfsc_ellist_insert(struct hfsc_if *hif, struct hfsc_class *cl)
{
	struct hfsc_class *p;

	/* check the last entry first */
	if ((p = TAILQ_LAST(&hif->hif_eligible, hfsc_eligible)) == NULL ||
	    p->cl_e <= cl->cl_e) {
		TAILQ_INSERT_TAIL(&hif->hif_eligible, cl, cl_ellist);
		return;
	}

	TAILQ_FOREACH(p, &hif->hif_eligible, cl_ellist) {
		if (cl->cl_e < p->cl_e) {
			TAILQ_INSERT_BEFORE(p, cl, cl_ellist);
			return;
		}
	}
}

void
hfsc_ellist_remove(struct hfsc_if *hif, struct hfsc_class *cl)
{
	TAILQ_REMOVE(&hif->hif_eligible, cl, cl_ellist);
}

void
hfsc_ellist_update(struct hfsc_if *hif, struct hfsc_class *cl)
{
	struct hfsc_class *p, *last;

	/*
	 * the eligible time of a class increases monotonically.
	 * if the next entry has a larger eligible time, nothing to do.
	 */
	p = TAILQ_NEXT(cl, cl_ellist);
	if (p == NULL || cl->cl_e <= p->cl_e)
		return;

	/* check the last entry */
	last = TAILQ_LAST(&hif->hif_eligible, hfsc_eligible);
	if (last->cl_e <= cl->cl_e) {
		TAILQ_REMOVE(&hif->hif_eligible, cl, cl_ellist);
		TAILQ_INSERT_TAIL(&hif->hif_eligible, cl, cl_ellist);
		return;
	}

	/*
	 * the new position must be between the next entry
	 * and the last entry
	 */
	while ((p = TAILQ_NEXT(p, cl_ellist)) != NULL) {
		if (cl->cl_e < p->cl_e) {
			TAILQ_REMOVE(&hif->hif_eligible, cl, cl_ellist);
			TAILQ_INSERT_BEFORE(p, cl, cl_ellist);
			return;
		}
	}
}

/* find the class with the minimum deadline among the eligible classes */
struct hfsc_class *
hfsc_ellist_get_mindl(struct hfsc_if *hif, u_int64_t cur_time)
{
	struct hfsc_class *p, *cl = NULL;

	TAILQ_FOREACH(p, &hif->hif_eligible, cl_ellist) {
		if (p->cl_e > cur_time)
			break;
		if (cl == NULL || p->cl_d < cl->cl_d)
			cl = p;
	}
	return (cl);
}

/*
 * active children list holds backlogged child classes being sorted
 * by their virtual time.
 * each intermediate class has one active children list.
 */
void
hfsc_actlist_insert(struct hfsc_class *cl)
{
	struct hfsc_class *p;

	/* check the last entry first */
	if ((p = TAILQ_LAST(&cl->cl_parent->cl_actc, hfsc_active)) == NULL
	    || p->cl_vt <= cl->cl_vt) {
		TAILQ_INSERT_TAIL(&cl->cl_parent->cl_actc, cl, cl_actlist);
		return;
	}

	TAILQ_FOREACH(p, &cl->cl_parent->cl_actc, cl_actlist) {
		if (cl->cl_vt < p->cl_vt) {
			TAILQ_INSERT_BEFORE(p, cl, cl_actlist);
			return;
		}
	}
}

void
hfsc_actlist_remove(struct hfsc_class *cl)
{
	TAILQ_REMOVE(&cl->cl_parent->cl_actc, cl, cl_actlist);
}

void
hfsc_actlist_update(struct hfsc_class *cl)
{
	struct hfsc_class *p, *last;

	/*
	 * the virtual time of a class increases monotonically during its
	 * backlogged period.
	 * if the next entry has a larger virtual time, nothing to do.
	 */
	p = TAILQ_NEXT(cl, cl_actlist);
	if (p == NULL || cl->cl_vt < p->cl_vt)
		return;

	/* check the last entry */
	last = TAILQ_LAST(&cl->cl_parent->cl_actc, hfsc_active);
	if (last->cl_vt <= cl->cl_vt) {
		TAILQ_REMOVE(&cl->cl_parent->cl_actc, cl, cl_actlist);
		TAILQ_INSERT_TAIL(&cl->cl_parent->cl_actc, cl, cl_actlist);
		return;
	}

	/*
	 * the new position must be between the next entry
	 * and the last entry
	 */
	while ((p = TAILQ_NEXT(p, cl_actlist)) != NULL) {
		if (cl->cl_vt < p->cl_vt) {
			TAILQ_REMOVE(&cl->cl_parent->cl_actc, cl, cl_actlist);
			TAILQ_INSERT_BEFORE(p, cl, cl_actlist);
			return;
		}
	}
}

struct hfsc_class *
hfsc_actlist_firstfit(struct hfsc_class *cl, u_int64_t cur_time)
{
	struct hfsc_class *p;

	TAILQ_FOREACH(p, &cl->cl_actc, cl_actlist)
		if (p->cl_f <= cur_time)
			return (p);

	return (NULL);
}

/*
 * service curve support functions
 *
 *  external service curve parameters
 *	m: bits/sec
 *	d: msec
 *  internal service curve parameters
 *	sm: (bytes/tsc_interval) << SM_SHIFT
 *	ism: (tsc_count/byte) << ISM_SHIFT
 *	dx: tsc_count
 *
 * SM_SHIFT and ISM_SHIFT are scaled in order to keep effective digits.
 * we should be able to handle 100K-1Gbps linkspeed with 200Hz-1GHz CPU
 * speed.  SM_SHIFT and ISM_SHIFT are selected to have at least 3 effective
 * digits in decimal using the following table.
 *
 *  bits/sec    100Kbps     1Mbps     10Mbps     100Mbps    1Gbps
 *  ----------+-------------------------------------------------------
 *  bytes/nsec  12.5e-6    125e-6     1250e-6    12500e-6   125000e-6
 *  sm(500MHz)  25.0e-6    250e-6     2500e-6    25000e-6   250000e-6
 *  sm(200MHz)  62.5e-6    625e-6     6250e-6    62500e-6   625000e-6
 *
 *  nsec/byte   80000      8000       800        80         8
 *  ism(500MHz) 40000      4000       400        40         4
 *  ism(200MHz) 16000      1600       160        16         1.6
 */
#define	SM_SHIFT	24
#define	ISM_SHIFT	10

#define	SM_MASK		((1LL << SM_SHIFT) - 1)
#define	ISM_MASK	((1LL << ISM_SHIFT) - 1)

static __inline u_int64_t
seg_x2y(u_int64_t x, u_int64_t sm)
{
	u_int64_t y;

	/*
	 * compute
	 *	y = x * sm >> SM_SHIFT
	 * but divide it for the upper and lower bits to avoid overflow
	 */
	y = (x >> SM_SHIFT) * sm + (((x & SM_MASK) * sm) >> SM_SHIFT);
	return (y);
}

static __inline u_int64_t
seg_y2x(u_int64_t y, u_int64_t ism)
{
	u_int64_t x;

	if (y == 0)
		x = 0;
	else if (ism == HFSC_HT_INFINITY)
		x = HFSC_HT_INFINITY;
	else {
		x = (y >> ISM_SHIFT) * ism
		    + (((y & ISM_MASK) * ism) >> ISM_SHIFT);
	}
	return (x);
}

static __inline u_int64_t
m2sm(u_int m)
{
	u_int64_t sm;

	sm = ((u_int64_t)m << SM_SHIFT) / 8 / HFSC_FREQ;
	return (sm);
}

static __inline u_int64_t
m2ism(u_int m)
{
	u_int64_t ism;

	if (m == 0)
		ism = HFSC_HT_INFINITY;
	else
		ism = ((u_int64_t)HFSC_FREQ << ISM_SHIFT) * 8 / m;
	return (ism);
}

static __inline u_int64_t
d2dx(u_int d)
{
	u_int64_t dx;

	dx = ((u_int64_t)d * HFSC_FREQ) / 1000;
	return (dx);
}

static __inline u_int
sm2m(u_int64_t sm)
{
	u_int64_t m;

	m = (sm * 8 * HFSC_FREQ) >> SM_SHIFT;
	return ((u_int)m);
}

static __inline u_int
dx2d(u_int64_t dx)
{
	u_int64_t d;

	d = dx * 1000 / HFSC_FREQ;
	return ((u_int)d);
}

void
hfsc_sc2isc(struct hfsc_sc *sc, struct hfsc_internal_sc *isc)
{
	isc->sm1 = m2sm(sc->m1);
	isc->ism1 = m2ism(sc->m1);
	isc->dx = d2dx(sc->d);
	isc->dy = seg_x2y(isc->dx, isc->sm1);
	isc->sm2 = m2sm(sc->m2);
	isc->ism2 = m2ism(sc->m2);
}

/*
 * initialize the runtime service curve with the given internal
 * service curve starting at (x, y).
 */
void
hfsc_rtsc_init(struct hfsc_runtime_sc *rtsc, struct hfsc_internal_sc * isc,
    u_int64_t x, u_int64_t y)
{
	rtsc->x =	x;
	rtsc->y =	y;
	rtsc->sm1 =	isc->sm1;
	rtsc->ism1 =	isc->ism1;
	rtsc->dx =	isc->dx;
	rtsc->dy =	isc->dy;
	rtsc->sm2 =	isc->sm2;
	rtsc->ism2 =	isc->ism2;
}

/*
 * calculate the y-projection of the runtime service curve by the
 * given x-projection value
 */
u_int64_t
hfsc_rtsc_y2x(struct hfsc_runtime_sc *rtsc, u_int64_t y)
{
	u_int64_t x;

	if (y < rtsc->y)
		x = rtsc->x;
	else if (y <= rtsc->y + rtsc->dy) {
		/* x belongs to the 1st segment */
		if (rtsc->dy == 0)
			x = rtsc->x + rtsc->dx;
		else
			x = rtsc->x + seg_y2x(y - rtsc->y, rtsc->ism1);
	} else {
		/* x belongs to the 2nd segment */
		x = rtsc->x + rtsc->dx
		    + seg_y2x(y - rtsc->y - rtsc->dy, rtsc->ism2);
	}
	return (x);
}

u_int64_t
hfsc_rtsc_x2y(struct hfsc_runtime_sc *rtsc, u_int64_t x)
{
	u_int64_t y;

	if (x <= rtsc->x)
		y = rtsc->y;
	else if (x <= rtsc->x + rtsc->dx)
		/* y belongs to the 1st segment */
		y = rtsc->y + seg_x2y(x - rtsc->x, rtsc->sm1);
	else
		/* y belongs to the 2nd segment */
		y = rtsc->y + rtsc->dy
		    + seg_x2y(x - rtsc->x - rtsc->dx, rtsc->sm2);
	return (y);
}

/*
 * update the runtime service curve by taking the minimum of the current
 * runtime service curve and the service curve starting at (x, y).
 */
void
hfsc_rtsc_min(struct hfsc_runtime_sc *rtsc, struct hfsc_internal_sc *isc,
    u_int64_t x, u_int64_t y)
{
	u_int64_t y1, y2, dx, dy;

	if (isc->sm1 <= isc->sm2) {
		/* service curve is convex */
		y1 = hfsc_rtsc_x2y(rtsc, x);
		if (y1 < y)
			/* the current rtsc is smaller */
			return;
		rtsc->x = x;
		rtsc->y = y;
		return;
	}

	/*
	 * service curve is concave
	 * compute the two y values of the current rtsc
	 *	y1: at x
	 *	y2: at (x + dx)
	 */
	y1 = hfsc_rtsc_x2y(rtsc, x);
	if (y1 <= y) {
		/* rtsc is below isc, no change to rtsc */
		return;
	}

	y2 = hfsc_rtsc_x2y(rtsc, x + isc->dx);
	if (y2 >= y + isc->dy) {
		/* rtsc is above isc, replace rtsc by isc */
		rtsc->x = x;
		rtsc->y = y;
		rtsc->dx = isc->dx;
		rtsc->dy = isc->dy;
		return;
	}

	/*
	 * the two curves intersect
	 * compute the offsets (dx, dy) using the reverse
	 * function of seg_x2y()
	 *	seg_x2y(dx, sm1) == seg_x2y(dx, sm2) + (y1 - y)
	 */
	dx = ((y1 - y) << SM_SHIFT) / (isc->sm1 - isc->sm2);
	/*
	 * check if (x, y1) belongs to the 1st segment of rtsc.
	 * if so, add the offset.
	 */
	if (rtsc->x + rtsc->dx > x)
		dx += rtsc->x + rtsc->dx - x;
	dy = seg_x2y(dx, isc->sm1);

	rtsc->x = x;
	rtsc->y = y;
	rtsc->dx = dx;
	rtsc->dy = dy;
	return;
}

void
hfsc_getclstats(struct hfsc_class_stats *sp, struct hfsc_class *cl)
{
	sp->class_id = cl->cl_id;
	sp->class_handle = cl->cl_handle;

	if (cl->cl_rsc != NULL) {
		sp->rsc.m1 = sm2m(cl->cl_rsc->sm1);
		sp->rsc.d = dx2d(cl->cl_rsc->dx);
		sp->rsc.m2 = sm2m(cl->cl_rsc->sm2);
	} else {
		sp->rsc.m1 = 0;
		sp->rsc.d = 0;
		sp->rsc.m2 = 0;
	}
	if (cl->cl_fsc != NULL) {
		sp->fsc.m1 = sm2m(cl->cl_fsc->sm1);
		sp->fsc.d = dx2d(cl->cl_fsc->dx);
		sp->fsc.m2 = sm2m(cl->cl_fsc->sm2);
	} else {
		sp->fsc.m1 = 0;
		sp->fsc.d = 0;
		sp->fsc.m2 = 0;
	}
	if (cl->cl_usc != NULL) {
		sp->usc.m1 = sm2m(cl->cl_usc->sm1);
		sp->usc.d = dx2d(cl->cl_usc->dx);
		sp->usc.m2 = sm2m(cl->cl_usc->sm2);
	} else {
		sp->usc.m1 = 0;
		sp->usc.d = 0;
		sp->usc.m2 = 0;
	}

	sp->total = cl->cl_total;
	sp->cumul = cl->cl_cumul;

	sp->d = cl->cl_d;
	sp->e = cl->cl_e;
	sp->vt = cl->cl_vt;
	sp->f = cl->cl_f;

	sp->initvt = cl->cl_initvt;
	sp->vtperiod = cl->cl_vtperiod;
	sp->parentperiod = cl->cl_parentperiod;
	sp->nactive = cl->cl_nactive;
	sp->vtoff = cl->cl_vtoff;
	sp->cvtmax = cl->cl_cvtmax;
	sp->myf = cl->cl_myf;
	sp->cfmin = cl->cl_cfmin;
	sp->cvtmin = cl->cl_cvtmin;
	sp->myfadj = cl->cl_myfadj;
	sp->vtadj = cl->cl_vtadj;

	sp->cur_time = hfsc_uptime();
	sp->machclk_freq = HFSC_FREQ;

	sp->qlength = hfsc_class_qlength(cl);
	sp->qlimit = cl->cl_q.qlimit;
	sp->xmit_cnt = cl->cl_stats.xmit_cnt;
	sp->drop_cnt = cl->cl_stats.drop_cnt;
	sp->period = cl->cl_stats.period;

	sp->qtype = 0;
}

/* convert a class handle to the corresponding class pointer */
struct hfsc_class *
hfsc_clh2cph(struct hfsc_if *hif, u_int32_t chandle)
{
	int i;
	struct hfsc_class *cl;

	if (chandle == 0)
		return (NULL);
	/*
	 * first, try the slot corresponding to the lower bits of the handle.
	 * if it does not match, do the linear table search.
	 */
	i = chandle % hif->hif_allocated;
	if ((cl = hif->hif_class_tbl[i]) != NULL && cl->cl_handle == chandle)
		return (cl);
	for (i = 0; i < hif->hif_allocated; i++)
		if ((cl = hif->hif_class_tbl[i]) != NULL &&
		    cl->cl_handle == chandle)
			return (cl);
	return (NULL);
}
