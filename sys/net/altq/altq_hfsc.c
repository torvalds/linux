/*-
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
 *
 * $KAME: altq_hfsc.c,v 1.24 2003/12/05 05:40:46 kjc Exp $
 * $FreeBSD$
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

#include "opt_altq.h"
#include "opt_inet.h"
#include "opt_inet6.h"

#ifdef ALTQ_HFSC  /* hfsc is enabled by ALTQ_HFSC option in opt_altq.h */

#include <sys/param.h>
#include <sys/malloc.h>
#include <sys/mbuf.h>
#include <sys/socket.h>
#include <sys/systm.h>
#include <sys/errno.h>
#include <sys/queue.h>
#if 1 /* ALTQ3_COMPAT */
#include <sys/sockio.h>
#include <sys/proc.h>
#include <sys/kernel.h>
#endif /* ALTQ3_COMPAT */

#include <net/if.h>
#include <net/if_var.h>
#include <netinet/in.h>

#include <netpfil/pf/pf.h>
#include <netpfil/pf/pf_altq.h>
#include <netpfil/pf/pf_mtag.h>
#include <net/altq/altq.h>
#include <net/altq/altq_hfsc.h>

/*
 * function prototypes
 */
static int			 hfsc_clear_interface(struct hfsc_if *);
static int			 hfsc_request(struct ifaltq *, int, void *);
static void			 hfsc_purge(struct hfsc_if *);
static struct hfsc_class	*hfsc_class_create(struct hfsc_if *,
    struct service_curve *, struct service_curve *, struct service_curve *,
    struct hfsc_class *, int, int, int);
static int			 hfsc_class_destroy(struct hfsc_class *);
static struct hfsc_class	*hfsc_nextclass(struct hfsc_class *);
static int			 hfsc_enqueue(struct ifaltq *, struct mbuf *,
				    struct altq_pktattr *);
static struct mbuf		*hfsc_dequeue(struct ifaltq *, int);

static int		 hfsc_addq(struct hfsc_class *, struct mbuf *);
static struct mbuf	*hfsc_getq(struct hfsc_class *);
static struct mbuf	*hfsc_pollq(struct hfsc_class *);
static void		 hfsc_purgeq(struct hfsc_class *);

static void		 update_cfmin(struct hfsc_class *);
static void		 set_active(struct hfsc_class *, int);
static void		 set_passive(struct hfsc_class *);

static void		 init_ed(struct hfsc_class *, int);
static void		 update_ed(struct hfsc_class *, int);
static void		 update_d(struct hfsc_class *, int);
static void		 init_vf(struct hfsc_class *, int);
static void		 update_vf(struct hfsc_class *, int, u_int64_t);
static void		 ellist_insert(struct hfsc_class *);
static void		 ellist_remove(struct hfsc_class *);
static void		 ellist_update(struct hfsc_class *);
struct hfsc_class	*hfsc_get_mindl(struct hfsc_if *, u_int64_t);
static void		 actlist_insert(struct hfsc_class *);
static void		 actlist_remove(struct hfsc_class *);
static void		 actlist_update(struct hfsc_class *);

static struct hfsc_class	*actlist_firstfit(struct hfsc_class *,
				    u_int64_t);

static __inline u_int64_t	seg_x2y(u_int64_t, u_int64_t);
static __inline u_int64_t	seg_y2x(u_int64_t, u_int64_t);
static __inline u_int64_t	m2sm(u_int64_t);
static __inline u_int64_t	m2ism(u_int64_t);
static __inline u_int64_t	d2dx(u_int);
static u_int64_t		sm2m(u_int64_t);
static u_int			dx2d(u_int64_t);

static void		sc2isc(struct service_curve *, struct internal_sc *);
static void		rtsc_init(struct runtime_sc *, struct internal_sc *,
			    u_int64_t, u_int64_t);
static u_int64_t	rtsc_y2x(struct runtime_sc *, u_int64_t);
static u_int64_t	rtsc_x2y(struct runtime_sc *, u_int64_t);
static void		rtsc_min(struct runtime_sc *, struct internal_sc *,
			    u_int64_t, u_int64_t);

static void			 get_class_stats_v0(struct hfsc_classstats_v0 *,
				    struct hfsc_class *);
static void			 get_class_stats_v1(struct hfsc_classstats_v1 *,
				    struct hfsc_class *);
static struct hfsc_class	*clh_to_clp(struct hfsc_if *, u_int32_t);



/*
 * macros
 */
#define	is_a_parent_class(cl)	((cl)->cl_children != NULL)

#define	HT_INFINITY	0xffffffffffffffffULL	/* infinite time value */


int
hfsc_pfattach(struct pf_altq *a)
{
	struct ifnet *ifp;
	int s, error;

	if ((ifp = ifunit(a->ifname)) == NULL || a->altq_disc == NULL)
		return (EINVAL);
	s = splnet();
	error = altq_attach(&ifp->if_snd, ALTQT_HFSC, a->altq_disc,
	    hfsc_enqueue, hfsc_dequeue, hfsc_request, NULL, NULL);
	splx(s);
	return (error);
}

int
hfsc_add_altq(struct ifnet *ifp, struct pf_altq *a)
{
	struct hfsc_if *hif;

	if (ifp == NULL)
		return (EINVAL);
	if (!ALTQ_IS_READY(&ifp->if_snd))
		return (ENODEV);

	hif = malloc(sizeof(struct hfsc_if), M_DEVBUF, M_NOWAIT | M_ZERO);
	if (hif == NULL)
		return (ENOMEM);

	TAILQ_INIT(&hif->hif_eligible);
	hif->hif_ifq = &ifp->if_snd;

	/* keep the state in pf_altq */
	a->altq_disc = hif;

	return (0);
}

int
hfsc_remove_altq(struct pf_altq *a)
{
	struct hfsc_if *hif;

	if ((hif = a->altq_disc) == NULL)
		return (EINVAL);
	a->altq_disc = NULL;

	(void)hfsc_clear_interface(hif);
	(void)hfsc_class_destroy(hif->hif_rootclass);

	free(hif, M_DEVBUF);

	return (0);
}

int
hfsc_add_queue(struct pf_altq *a)
{
	struct hfsc_if *hif;
	struct hfsc_class *cl, *parent;
	struct hfsc_opts_v1 *opts;
	struct service_curve rtsc, lssc, ulsc;

	if ((hif = a->altq_disc) == NULL)
		return (EINVAL);

	opts = &a->pq_u.hfsc_opts;

	if (a->parent_qid == HFSC_NULLCLASS_HANDLE &&
	    hif->hif_rootclass == NULL)
		parent = NULL;
	else if ((parent = clh_to_clp(hif, a->parent_qid)) == NULL)
		return (EINVAL);

	if (a->qid == 0)
		return (EINVAL);

	if (clh_to_clp(hif, a->qid) != NULL)
		return (EBUSY);

	rtsc.m1 = opts->rtsc_m1;
	rtsc.d  = opts->rtsc_d;
	rtsc.m2 = opts->rtsc_m2;
	lssc.m1 = opts->lssc_m1;
	lssc.d  = opts->lssc_d;
	lssc.m2 = opts->lssc_m2;
	ulsc.m1 = opts->ulsc_m1;
	ulsc.d  = opts->ulsc_d;
	ulsc.m2 = opts->ulsc_m2;

	cl = hfsc_class_create(hif, &rtsc, &lssc, &ulsc,
	    parent, a->qlimit, opts->flags, a->qid);
	if (cl == NULL)
		return (ENOMEM);

	return (0);
}

int
hfsc_remove_queue(struct pf_altq *a)
{
	struct hfsc_if *hif;
	struct hfsc_class *cl;

	if ((hif = a->altq_disc) == NULL)
		return (EINVAL);

	if ((cl = clh_to_clp(hif, a->qid)) == NULL)
		return (EINVAL);

	return (hfsc_class_destroy(cl));
}

int
hfsc_getqstats(struct pf_altq *a, void *ubuf, int *nbytes, int version)
{
	struct hfsc_if *hif;
	struct hfsc_class *cl;
	union {
		struct hfsc_classstats_v0 v0;
		struct hfsc_classstats_v1 v1;
	} stats;
	size_t stats_size;
	int error = 0;

	if ((hif = altq_lookup(a->ifname, ALTQT_HFSC)) == NULL)
		return (EBADF);

	if ((cl = clh_to_clp(hif, a->qid)) == NULL)
		return (EINVAL);

	if (version > HFSC_STATS_VERSION)
		return (EINVAL);

	memset(&stats, 0, sizeof(stats));
	switch (version) {
	case 0:
		get_class_stats_v0(&stats.v0, cl);
		stats_size = sizeof(struct hfsc_classstats_v0);
		break;
	case 1:
		get_class_stats_v1(&stats.v1, cl);
		stats_size = sizeof(struct hfsc_classstats_v1);
		break;
	}		

	if (*nbytes < stats_size)
		return (EINVAL);

	if ((error = copyout((caddr_t)&stats, ubuf, stats_size)) != 0)
		return (error);
	*nbytes = stats_size;
	return (0);
}

/*
 * bring the interface back to the initial state by discarding
 * all the filters and classes except the root class.
 */
static int
hfsc_clear_interface(struct hfsc_if *hif)
{
	struct hfsc_class	*cl;


	/* clear out the classes */
	while (hif->hif_rootclass != NULL &&
	    (cl = hif->hif_rootclass->cl_children) != NULL) {
		/*
		 * remove the first leaf class found in the hierarchy
		 * then start over
		 */
		for (; cl != NULL; cl = hfsc_nextclass(cl)) {
			if (!is_a_parent_class(cl)) {
				(void)hfsc_class_destroy(cl);
				break;
			}
		}
	}

	return (0);
}

static int
hfsc_request(struct ifaltq *ifq, int req, void *arg)
{
	struct hfsc_if	*hif = (struct hfsc_if *)ifq->altq_disc;

	IFQ_LOCK_ASSERT(ifq);

	switch (req) {
	case ALTRQ_PURGE:
		hfsc_purge(hif);
		break;
	}
	return (0);
}

/* discard all the queued packets on the interface */
static void
hfsc_purge(struct hfsc_if *hif)
{
	struct hfsc_class *cl;

	for (cl = hif->hif_rootclass; cl != NULL; cl = hfsc_nextclass(cl))
		if (!qempty(cl->cl_q))
			hfsc_purgeq(cl);
	if (ALTQ_IS_ENABLED(hif->hif_ifq))
		hif->hif_ifq->ifq_len = 0;
}

struct hfsc_class *
hfsc_class_create(struct hfsc_if *hif, struct service_curve *rsc,
    struct service_curve *fsc, struct service_curve *usc,
    struct hfsc_class *parent, int qlimit, int flags, int qid)
{
	struct hfsc_class *cl, *p;
	int i, s;

	if (hif->hif_classes >= HFSC_MAX_CLASSES)
		return (NULL);

#ifndef ALTQ_RED
	if (flags & HFCF_RED) {
#ifdef ALTQ_DEBUG
		printf("hfsc_class_create: RED not configured for HFSC!\n");
#endif
		return (NULL);
	}
#endif
#ifndef ALTQ_CODEL
	if (flags & HFCF_CODEL) {
#ifdef ALTQ_DEBUG
		printf("hfsc_class_create: CODEL not configured for HFSC!\n");
#endif
		return (NULL);
	}
#endif

	cl = malloc(sizeof(struct hfsc_class), M_DEVBUF, M_NOWAIT | M_ZERO);
	if (cl == NULL)
		return (NULL);

	cl->cl_q = malloc(sizeof(class_queue_t), M_DEVBUF, M_NOWAIT | M_ZERO);
	if (cl->cl_q == NULL)
		goto err_ret;

	TAILQ_INIT(&cl->cl_actc);

	if (qlimit == 0)
		qlimit = 50;  /* use default */
	qlimit(cl->cl_q) = qlimit;
	qtype(cl->cl_q) = Q_DROPTAIL;
	qlen(cl->cl_q) = 0;
	qsize(cl->cl_q) = 0;
	cl->cl_flags = flags;
#ifdef ALTQ_RED
	if (flags & (HFCF_RED|HFCF_RIO)) {
		int red_flags, red_pkttime;
		u_int m2;

		m2 = 0;
		if (rsc != NULL && rsc->m2 > m2)
			m2 = rsc->m2;
		if (fsc != NULL && fsc->m2 > m2)
			m2 = fsc->m2;
		if (usc != NULL && usc->m2 > m2)
			m2 = usc->m2;

		red_flags = 0;
		if (flags & HFCF_ECN)
			red_flags |= REDF_ECN;
#ifdef ALTQ_RIO
		if (flags & HFCF_CLEARDSCP)
			red_flags |= RIOF_CLEARDSCP;
#endif
		if (m2 < 8)
			red_pkttime = 1000 * 1000 * 1000; /* 1 sec */
		else
			red_pkttime = (int64_t)hif->hif_ifq->altq_ifp->if_mtu
				* 1000 * 1000 * 1000 / (m2 / 8);
		if (flags & HFCF_RED) {
			cl->cl_red = red_alloc(0, 0,
			    qlimit(cl->cl_q) * 10/100,
			    qlimit(cl->cl_q) * 30/100,
			    red_flags, red_pkttime);
			if (cl->cl_red != NULL)
				qtype(cl->cl_q) = Q_RED;
		}
#ifdef ALTQ_RIO
		else {
			cl->cl_red = (red_t *)rio_alloc(0, NULL,
			    red_flags, red_pkttime);
			if (cl->cl_red != NULL)
				qtype(cl->cl_q) = Q_RIO;
		}
#endif
	}
#endif /* ALTQ_RED */
#ifdef ALTQ_CODEL
	if (flags & HFCF_CODEL) {
		cl->cl_codel = codel_alloc(5, 100, 0);
		if (cl->cl_codel != NULL)
			qtype(cl->cl_q) = Q_CODEL;
	}
#endif

	if (rsc != NULL && (rsc->m1 != 0 || rsc->m2 != 0)) {
		cl->cl_rsc = malloc(sizeof(struct internal_sc),
		    M_DEVBUF, M_NOWAIT);
		if (cl->cl_rsc == NULL)
			goto err_ret;
		sc2isc(rsc, cl->cl_rsc);
		rtsc_init(&cl->cl_deadline, cl->cl_rsc, 0, 0);
		rtsc_init(&cl->cl_eligible, cl->cl_rsc, 0, 0);
	}
	if (fsc != NULL && (fsc->m1 != 0 || fsc->m2 != 0)) {
		cl->cl_fsc = malloc(sizeof(struct internal_sc),
		    M_DEVBUF, M_NOWAIT);
		if (cl->cl_fsc == NULL)
			goto err_ret;
		sc2isc(fsc, cl->cl_fsc);
		rtsc_init(&cl->cl_virtual, cl->cl_fsc, 0, 0);
	}
	if (usc != NULL && (usc->m1 != 0 || usc->m2 != 0)) {
		cl->cl_usc = malloc(sizeof(struct internal_sc),
		    M_DEVBUF, M_NOWAIT);
		if (cl->cl_usc == NULL)
			goto err_ret;
		sc2isc(usc, cl->cl_usc);
		rtsc_init(&cl->cl_ulimit, cl->cl_usc, 0, 0);
	}

	cl->cl_id = hif->hif_classid++;
	cl->cl_handle = qid;
	cl->cl_hif = hif;
	cl->cl_parent = parent;

	s = splnet();
	IFQ_LOCK(hif->hif_ifq);
	hif->hif_classes++;

	/*
	 * find a free slot in the class table.  if the slot matching
	 * the lower bits of qid is free, use this slot.  otherwise,
	 * use the first free slot.
	 */
	i = qid % HFSC_MAX_CLASSES;
	if (hif->hif_class_tbl[i] == NULL)
		hif->hif_class_tbl[i] = cl;
	else {
		for (i = 0; i < HFSC_MAX_CLASSES; i++)
			if (hif->hif_class_tbl[i] == NULL) {
				hif->hif_class_tbl[i] = cl;
				break;
			}
		if (i == HFSC_MAX_CLASSES) {
			IFQ_UNLOCK(hif->hif_ifq);
			splx(s);
			goto err_ret;
		}
	}
	cl->cl_slot = i;

	if (flags & HFCF_DEFAULTCLASS)
		hif->hif_defaultclass = cl;

	if (parent == NULL) {
		/* this is root class */
		hif->hif_rootclass = cl;
	} else {
		/* add this class to the children list of the parent */
		if ((p = parent->cl_children) == NULL)
			parent->cl_children = cl;
		else {
			while (p->cl_siblings != NULL)
				p = p->cl_siblings;
			p->cl_siblings = cl;
		}
	}
	IFQ_UNLOCK(hif->hif_ifq);
	splx(s);

	return (cl);

 err_ret:
	if (cl->cl_red != NULL) {
#ifdef ALTQ_RIO
		if (q_is_rio(cl->cl_q))
			rio_destroy((rio_t *)cl->cl_red);
#endif
#ifdef ALTQ_RED
		if (q_is_red(cl->cl_q))
			red_destroy(cl->cl_red);
#endif
#ifdef ALTQ_CODEL
		if (q_is_codel(cl->cl_q))
			codel_destroy(cl->cl_codel);
#endif
	}
	if (cl->cl_fsc != NULL)
		free(cl->cl_fsc, M_DEVBUF);
	if (cl->cl_rsc != NULL)
		free(cl->cl_rsc, M_DEVBUF);
	if (cl->cl_usc != NULL)
		free(cl->cl_usc, M_DEVBUF);
	if (cl->cl_q != NULL)
		free(cl->cl_q, M_DEVBUF);
	free(cl, M_DEVBUF);
	return (NULL);
}

static int
hfsc_class_destroy(struct hfsc_class *cl)
{
	int s;

	if (cl == NULL)
		return (0);

	if (is_a_parent_class(cl))
		return (EBUSY);

	s = splnet();
	IFQ_LOCK(cl->cl_hif->hif_ifq);


	if (!qempty(cl->cl_q))
		hfsc_purgeq(cl);

	if (cl->cl_parent == NULL) {
		/* this is root class */
	} else {
		struct hfsc_class *p = cl->cl_parent->cl_children;

		if (p == cl)
			cl->cl_parent->cl_children = cl->cl_siblings;
		else do {
			if (p->cl_siblings == cl) {
				p->cl_siblings = cl->cl_siblings;
				break;
			}
		} while ((p = p->cl_siblings) != NULL);
		ASSERT(p != NULL);
	}

	cl->cl_hif->hif_class_tbl[cl->cl_slot] = NULL;
	cl->cl_hif->hif_classes--;
	IFQ_UNLOCK(cl->cl_hif->hif_ifq);
	splx(s);

	if (cl->cl_red != NULL) {
#ifdef ALTQ_RIO
		if (q_is_rio(cl->cl_q))
			rio_destroy((rio_t *)cl->cl_red);
#endif
#ifdef ALTQ_RED
		if (q_is_red(cl->cl_q))
			red_destroy(cl->cl_red);
#endif
#ifdef ALTQ_CODEL
		if (q_is_codel(cl->cl_q))
			codel_destroy(cl->cl_codel);
#endif
	}

	IFQ_LOCK(cl->cl_hif->hif_ifq);
	if (cl == cl->cl_hif->hif_rootclass)
		cl->cl_hif->hif_rootclass = NULL;
	if (cl == cl->cl_hif->hif_defaultclass)
		cl->cl_hif->hif_defaultclass = NULL;
	IFQ_UNLOCK(cl->cl_hif->hif_ifq);

	if (cl->cl_usc != NULL)
		free(cl->cl_usc, M_DEVBUF);
	if (cl->cl_fsc != NULL)
		free(cl->cl_fsc, M_DEVBUF);
	if (cl->cl_rsc != NULL)
		free(cl->cl_rsc, M_DEVBUF);
	free(cl->cl_q, M_DEVBUF);
	free(cl, M_DEVBUF);

	return (0);
}

/*
 * hfsc_nextclass returns the next class in the tree.
 *   usage:
 *	for (cl = hif->hif_rootclass; cl != NULL; cl = hfsc_nextclass(cl))
 *		do_something;
 */
static struct hfsc_class *
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

/*
 * hfsc_enqueue is an enqueue function to be registered to
 * (*altq_enqueue) in struct ifaltq.
 */
static int
hfsc_enqueue(struct ifaltq *ifq, struct mbuf *m, struct altq_pktattr *pktattr)
{
	struct hfsc_if	*hif = (struct hfsc_if *)ifq->altq_disc;
	struct hfsc_class *cl;
	struct pf_mtag *t;
	int len;

	IFQ_LOCK_ASSERT(ifq);

	/* grab class set by classifier */
	if ((m->m_flags & M_PKTHDR) == 0) {
		/* should not happen */
		printf("altq: packet for %s does not have pkthdr\n",
		    ifq->altq_ifp->if_xname);
		m_freem(m);
		return (ENOBUFS);
	}
	cl = NULL;
	if ((t = pf_find_mtag(m)) != NULL)
		cl = clh_to_clp(hif, t->qid);
	if (cl == NULL || is_a_parent_class(cl)) {
		cl = hif->hif_defaultclass;
		if (cl == NULL) {
			m_freem(m);
			return (ENOBUFS);
		}
	}
	cl->cl_pktattr = NULL;
	len = m_pktlen(m);
	if (hfsc_addq(cl, m) != 0) {
		/* drop occurred.  mbuf was freed in hfsc_addq. */
		PKTCNTR_ADD(&cl->cl_stats.drop_cnt, len);
		return (ENOBUFS);
	}
	IFQ_INC_LEN(ifq);
	cl->cl_hif->hif_packets++;

	/* successfully queued. */
	if (qlen(cl->cl_q) == 1)
		set_active(cl, m_pktlen(m));

	return (0);
}

/*
 * hfsc_dequeue is a dequeue function to be registered to
 * (*altq_dequeue) in struct ifaltq.
 *
 * note: ALTDQ_POLL returns the next packet without removing the packet
 *	from the queue.  ALTDQ_REMOVE is a normal dequeue operation.
 *	ALTDQ_REMOVE must return the same packet if called immediately
 *	after ALTDQ_POLL.
 */
static struct mbuf *
hfsc_dequeue(struct ifaltq *ifq, int op)
{
	struct hfsc_if	*hif = (struct hfsc_if *)ifq->altq_disc;
	struct hfsc_class *cl;
	struct mbuf *m;
	int len, next_len;
	int realtime = 0;
	u_int64_t cur_time;

	IFQ_LOCK_ASSERT(ifq);

	if (hif->hif_packets == 0)
		/* no packet in the tree */
		return (NULL);

	cur_time = read_machclk();

	if (op == ALTDQ_REMOVE && hif->hif_pollcache != NULL) {

		cl = hif->hif_pollcache;
		hif->hif_pollcache = NULL;
		/* check if the class was scheduled by real-time criteria */
		if (cl->cl_rsc != NULL)
			realtime = (cl->cl_e <= cur_time);
	} else {
		/*
		 * if there are eligible classes, use real-time criteria.
		 * find the class with the minimum deadline among
		 * the eligible classes.
		 */
		if ((cl = hfsc_get_mindl(hif, cur_time))
		    != NULL) {
			realtime = 1;
		} else {
#ifdef ALTQ_DEBUG
			int fits = 0;
#endif
			/*
			 * use link-sharing criteria
			 * get the class with the minimum vt in the hierarchy
			 */
			cl = hif->hif_rootclass;
			while (is_a_parent_class(cl)) {

				cl = actlist_firstfit(cl, cur_time);
				if (cl == NULL) {
#ifdef ALTQ_DEBUG
					if (fits > 0)
						printf("%d fit but none found\n",fits);
#endif
					return (NULL);
				}
				/*
				 * update parent's cl_cvtmin.
				 * don't update if the new vt is smaller.
				 */
				if (cl->cl_parent->cl_cvtmin < cl->cl_vt)
					cl->cl_parent->cl_cvtmin = cl->cl_vt;
#ifdef ALTQ_DEBUG
				fits++;
#endif
			}
		}

		if (op == ALTDQ_POLL) {
			hif->hif_pollcache = cl;
			m = hfsc_pollq(cl);
			return (m);
		}
	}

	m = hfsc_getq(cl);
	if (m == NULL)
		panic("hfsc_dequeue:");
	len = m_pktlen(m);
	cl->cl_hif->hif_packets--;
	IFQ_DEC_LEN(ifq);
	PKTCNTR_ADD(&cl->cl_stats.xmit_cnt, len);

	update_vf(cl, len, cur_time);
	if (realtime)
		cl->cl_cumul += len;

	if (!qempty(cl->cl_q)) {
		if (cl->cl_rsc != NULL) {
			/* update ed */
			next_len = m_pktlen(qhead(cl->cl_q));

			if (realtime)
				update_ed(cl, next_len);
			else
				update_d(cl, next_len);
		}
	} else {
		/* the class becomes passive */
		set_passive(cl);
	}

	return (m);
}

static int
hfsc_addq(struct hfsc_class *cl, struct mbuf *m)
{

#ifdef ALTQ_RIO
	if (q_is_rio(cl->cl_q))
		return rio_addq((rio_t *)cl->cl_red, cl->cl_q,
				m, cl->cl_pktattr);
#endif
#ifdef ALTQ_RED
	if (q_is_red(cl->cl_q))
		return red_addq(cl->cl_red, cl->cl_q, m, cl->cl_pktattr);
#endif
#ifdef ALTQ_CODEL
	if (q_is_codel(cl->cl_q))
		return codel_addq(cl->cl_codel, cl->cl_q, m);
#endif
	if (qlen(cl->cl_q) >= qlimit(cl->cl_q)) {
		m_freem(m);
		return (-1);
	}

	if (cl->cl_flags & HFCF_CLEARDSCP)
		write_dsfield(m, cl->cl_pktattr, 0);

	_addq(cl->cl_q, m);

	return (0);
}

static struct mbuf *
hfsc_getq(struct hfsc_class *cl)
{
#ifdef ALTQ_RIO
	if (q_is_rio(cl->cl_q))
		return rio_getq((rio_t *)cl->cl_red, cl->cl_q);
#endif
#ifdef ALTQ_RED
	if (q_is_red(cl->cl_q))
		return red_getq(cl->cl_red, cl->cl_q);
#endif
#ifdef ALTQ_CODEL
	if (q_is_codel(cl->cl_q))
		return codel_getq(cl->cl_codel, cl->cl_q);
#endif
	return _getq(cl->cl_q);
}

static struct mbuf *
hfsc_pollq(struct hfsc_class *cl)
{
	return qhead(cl->cl_q);
}

static void
hfsc_purgeq(struct hfsc_class *cl)
{
	struct mbuf *m;

	if (qempty(cl->cl_q))
		return;

	while ((m = _getq(cl->cl_q)) != NULL) {
		PKTCNTR_ADD(&cl->cl_stats.drop_cnt, m_pktlen(m));
		m_freem(m);
		cl->cl_hif->hif_packets--;
		IFQ_DEC_LEN(cl->cl_hif->hif_ifq);
	}
	ASSERT(qlen(cl->cl_q) == 0);

	update_vf(cl, 0, 0);	/* remove cl from the actlist */
	set_passive(cl);
}

static void
set_active(struct hfsc_class *cl, int len)
{
	if (cl->cl_rsc != NULL)
		init_ed(cl, len);
	if (cl->cl_fsc != NULL)
		init_vf(cl, len);

	cl->cl_stats.period++;
}

static void
set_passive(struct hfsc_class *cl)
{
	if (cl->cl_rsc != NULL)
		ellist_remove(cl);

	/*
	 * actlist is now handled in update_vf() so that update_vf(cl, 0, 0)
	 * needs to be called explicitly to remove a class from actlist
	 */
}

static void
init_ed(struct hfsc_class *cl, int next_len)
{
	u_int64_t cur_time;

	cur_time = read_machclk();

	/* update the deadline curve */
	rtsc_min(&cl->cl_deadline, cl->cl_rsc, cur_time, cl->cl_cumul);

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
	cl->cl_e = rtsc_y2x(&cl->cl_eligible, cl->cl_cumul);
	cl->cl_d = rtsc_y2x(&cl->cl_deadline, cl->cl_cumul + next_len);

	ellist_insert(cl);
}

static void
update_ed(struct hfsc_class *cl, int next_len)
{
	cl->cl_e = rtsc_y2x(&cl->cl_eligible, cl->cl_cumul);
	cl->cl_d = rtsc_y2x(&cl->cl_deadline, cl->cl_cumul + next_len);

	ellist_update(cl);
}

static void
update_d(struct hfsc_class *cl, int next_len)
{
	cl->cl_d = rtsc_y2x(&cl->cl_deadline, cl->cl_cumul + next_len);
}

static void
init_vf(struct hfsc_class *cl, int len)
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
			max_cl = TAILQ_LAST(&cl->cl_parent->cl_actc, acthead);
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
			rtsc_min(&cl->cl_virtual, cl->cl_fsc, vt, cl->cl_total);
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

			actlist_insert(cl);

			if (cl->cl_usc != NULL) {
				/* class has upper limit curve */
				if (cur_time == 0)
					cur_time = read_machclk();

				/* update the ulimit curve */
				rtsc_min(&cl->cl_ulimit, cl->cl_usc, cur_time,
				    cl->cl_total);
				/* compute myf */
				cl->cl_myf = rtsc_y2x(&cl->cl_ulimit,
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
			update_cfmin(cl->cl_parent);
		}
	}
}

static void
update_vf(struct hfsc_class *cl, int len, u_int64_t cur_time)
{
	u_int64_t f, myf_bound, delta;
	int go_passive;

	go_passive = qempty(cl->cl_q);

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
			actlist_remove(cl);

			update_cfmin(cl->cl_parent);

			continue;
		}

		/*
		 * update vt and f
		 */
		cl->cl_vt = rtsc_y2x(&cl->cl_virtual, cl->cl_total)
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
		actlist_update(cl);

		if (cl->cl_usc != NULL) {
			cl->cl_myf = cl->cl_myfadj
			    + rtsc_y2x(&cl->cl_ulimit, cl->cl_total);

			/*
			 * if myf lags behind by more than one clock tick
			 * from the current time, adjust myfadj to prevent
			 * a rate-limited class from going greedy.
			 * in a steady state under rate-limiting, myf
			 * fluctuates within one clock tick.
			 */
			myf_bound = cur_time - machclk_per_tick;
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
			update_cfmin(cl->cl_parent);
		}
	}
}

static void
update_cfmin(struct hfsc_class *cl)
{
	struct hfsc_class *p;
	u_int64_t cfmin;

	if (TAILQ_EMPTY(&cl->cl_actc)) {
		cl->cl_cfmin = 0;
		return;
	}
	cfmin = HT_INFINITY;
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
 * TAILQ based ellist and actlist implementation
 * (ion wanted to make a calendar queue based implementation)
 */
/*
 * eligible list holds backlogged classes being sorted by their eligible times.
 * there is one eligible list per interface.
 */

static void
ellist_insert(struct hfsc_class *cl)
{
	struct hfsc_if	*hif = cl->cl_hif;
	struct hfsc_class *p;

	/* check the last entry first */
	if ((p = TAILQ_LAST(&hif->hif_eligible, elighead)) == NULL ||
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
	ASSERT(0); /* should not reach here */
}

static void
ellist_remove(struct hfsc_class *cl)
{
	struct hfsc_if	*hif = cl->cl_hif;

	TAILQ_REMOVE(&hif->hif_eligible, cl, cl_ellist);
}

static void
ellist_update(struct hfsc_class *cl)
{
	struct hfsc_if	*hif = cl->cl_hif;
	struct hfsc_class *p, *last;

	/*
	 * the eligible time of a class increases monotonically.
	 * if the next entry has a larger eligible time, nothing to do.
	 */
	p = TAILQ_NEXT(cl, cl_ellist);
	if (p == NULL || cl->cl_e <= p->cl_e)
		return;

	/* check the last entry */
	last = TAILQ_LAST(&hif->hif_eligible, elighead);
	ASSERT(last != NULL);
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
	ASSERT(0); /* should not reach here */
}

/* find the class with the minimum deadline among the eligible classes */
struct hfsc_class *
hfsc_get_mindl(struct hfsc_if *hif, u_int64_t cur_time)
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

static void
actlist_insert(struct hfsc_class *cl)
{
	struct hfsc_class *p;

	/* check the last entry first */
	if ((p = TAILQ_LAST(&cl->cl_parent->cl_actc, acthead)) == NULL
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
	ASSERT(0); /* should not reach here */
}

static void
actlist_remove(struct hfsc_class *cl)
{
	TAILQ_REMOVE(&cl->cl_parent->cl_actc, cl, cl_actlist);
}

static void
actlist_update(struct hfsc_class *cl)
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
	last = TAILQ_LAST(&cl->cl_parent->cl_actc, acthead);
	ASSERT(last != NULL);
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
	ASSERT(0); /* should not reach here */
}

static struct hfsc_class *
actlist_firstfit(struct hfsc_class *cl, u_int64_t cur_time)
{
	struct hfsc_class *p;

	TAILQ_FOREACH(p, &cl->cl_actc, cl_actlist) {
		if (p->cl_f <= cur_time)
			return (p);
	}
	return (NULL);
}

/*
 * service curve support functions
 *
 *  external service curve parameters
 *	m: bits/sec
 *	d: msec
 *  internal service curve parameters
 *	sm: (bytes/machclk tick) << SM_SHIFT
 *	ism: (machclk ticks/byte) << ISM_SHIFT
 *	dx: machclk ticks
 *
 * SM_SHIFT and ISM_SHIFT are scaled in order to keep effective digits.  we
 * should be able to handle 100K-100Gbps linkspeed with 256 MHz machclk
 * frequency and at least 3 effective digits in decimal.
 *
 */
#define	SM_SHIFT	24
#define	ISM_SHIFT	14

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
	else if (ism == HT_INFINITY)
		x = HT_INFINITY;
	else {
		x = (y >> ISM_SHIFT) * ism
		    + (((y & ISM_MASK) * ism) >> ISM_SHIFT);
	}
	return (x);
}

static __inline u_int64_t
m2sm(u_int64_t m)
{
	u_int64_t sm;

	sm = (m << SM_SHIFT) / 8 / machclk_freq;
	return (sm);
}

static __inline u_int64_t
m2ism(u_int64_t m)
{
	u_int64_t ism;

	if (m == 0)
		ism = HT_INFINITY;
	else
		ism = ((u_int64_t)machclk_freq << ISM_SHIFT) * 8 / m;
	return (ism);
}

static __inline u_int64_t
d2dx(u_int d)
{
	u_int64_t dx;

	dx = ((u_int64_t)d * machclk_freq) / 1000;
	return (dx);
}

static u_int64_t
sm2m(u_int64_t sm)
{
	u_int64_t m;

	m = (sm * 8 * machclk_freq) >> SM_SHIFT;
	return (m);
}

static u_int
dx2d(u_int64_t dx)
{
	u_int64_t d;

	d = dx * 1000 / machclk_freq;
	return ((u_int)d);
}

static void
sc2isc(struct service_curve *sc, struct internal_sc *isc)
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
static void
rtsc_init(struct runtime_sc *rtsc, struct internal_sc * isc, u_int64_t x,
    u_int64_t y)
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
static u_int64_t
rtsc_y2x(struct runtime_sc *rtsc, u_int64_t y)
{
	u_int64_t	x;

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

static u_int64_t
rtsc_x2y(struct runtime_sc *rtsc, u_int64_t x)
{
	u_int64_t	y;

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
static void
rtsc_min(struct runtime_sc *rtsc, struct internal_sc *isc, u_int64_t x,
    u_int64_t y)
{
	u_int64_t	y1, y2, dx, dy;

	if (isc->sm1 <= isc->sm2) {
		/* service curve is convex */
		y1 = rtsc_x2y(rtsc, x);
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
	y1 = rtsc_x2y(rtsc, x);
	if (y1 <= y) {
		/* rtsc is below isc, no change to rtsc */
		return;
	}

	y2 = rtsc_x2y(rtsc, x + isc->dx);
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

static void
get_class_stats_v0(struct hfsc_classstats_v0 *sp, struct hfsc_class *cl)
{
	sp->class_id = cl->cl_id;
	sp->class_handle = cl->cl_handle;

#define SATU32(x)	(u_int32_t)uqmin((x), UINT_MAX)

	if (cl->cl_rsc != NULL) {
		sp->rsc.m1 = SATU32(sm2m(cl->cl_rsc->sm1));
		sp->rsc.d = dx2d(cl->cl_rsc->dx);
		sp->rsc.m2 = SATU32(sm2m(cl->cl_rsc->sm2));
	} else {
		sp->rsc.m1 = 0;
		sp->rsc.d = 0;
		sp->rsc.m2 = 0;
	}
	if (cl->cl_fsc != NULL) {
		sp->fsc.m1 = SATU32(sm2m(cl->cl_fsc->sm1));
		sp->fsc.d = dx2d(cl->cl_fsc->dx);
		sp->fsc.m2 = SATU32(sm2m(cl->cl_fsc->sm2));
	} else {
		sp->fsc.m1 = 0;
		sp->fsc.d = 0;
		sp->fsc.m2 = 0;
	}
	if (cl->cl_usc != NULL) {
		sp->usc.m1 = SATU32(sm2m(cl->cl_usc->sm1));
		sp->usc.d = dx2d(cl->cl_usc->dx);
		sp->usc.m2 = SATU32(sm2m(cl->cl_usc->sm2));
	} else {
		sp->usc.m1 = 0;
		sp->usc.d = 0;
		sp->usc.m2 = 0;
	}

#undef SATU32
	
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

	sp->cur_time = read_machclk();
	sp->machclk_freq = machclk_freq;

	sp->qlength = qlen(cl->cl_q);
	sp->qlimit = qlimit(cl->cl_q);
	sp->xmit_cnt = cl->cl_stats.xmit_cnt;
	sp->drop_cnt = cl->cl_stats.drop_cnt;
	sp->period = cl->cl_stats.period;

	sp->qtype = qtype(cl->cl_q);
#ifdef ALTQ_RED
	if (q_is_red(cl->cl_q))
		red_getstats(cl->cl_red, &sp->red[0]);
#endif
#ifdef ALTQ_RIO
	if (q_is_rio(cl->cl_q))
		rio_getstats((rio_t *)cl->cl_red, &sp->red[0]);
#endif
#ifdef ALTQ_CODEL
	if (q_is_codel(cl->cl_q))
		codel_getstats(cl->cl_codel, &sp->codel);
#endif
}

static void
get_class_stats_v1(struct hfsc_classstats_v1 *sp, struct hfsc_class *cl)
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

	sp->cur_time = read_machclk();
	sp->machclk_freq = machclk_freq;

	sp->qlength = qlen(cl->cl_q);
	sp->qlimit = qlimit(cl->cl_q);
	sp->xmit_cnt = cl->cl_stats.xmit_cnt;
	sp->drop_cnt = cl->cl_stats.drop_cnt;
	sp->period = cl->cl_stats.period;

	sp->qtype = qtype(cl->cl_q);
#ifdef ALTQ_RED
	if (q_is_red(cl->cl_q))
		red_getstats(cl->cl_red, &sp->red[0]);
#endif
#ifdef ALTQ_RIO
	if (q_is_rio(cl->cl_q))
		rio_getstats((rio_t *)cl->cl_red, &sp->red[0]);
#endif
#ifdef ALTQ_CODEL
	if (q_is_codel(cl->cl_q))
		codel_getstats(cl->cl_codel, &sp->codel);
#endif
}

/* convert a class handle to the corresponding class pointer */
static struct hfsc_class *
clh_to_clp(struct hfsc_if *hif, u_int32_t chandle)
{
	int i;
	struct hfsc_class *cl;

	if (chandle == 0)
		return (NULL);
	/*
	 * first, try optimistically the slot matching the lower bits of
	 * the handle.  if it fails, do the linear table search.
	 */
	i = chandle % HFSC_MAX_CLASSES;
	if ((cl = hif->hif_class_tbl[i]) != NULL && cl->cl_handle == chandle)
		return (cl);
	for (i = 0; i < HFSC_MAX_CLASSES; i++)
		if ((cl = hif->hif_class_tbl[i]) != NULL &&
		    cl->cl_handle == chandle)
			return (cl);
	return (NULL);
}


#endif /* ALTQ_HFSC */
