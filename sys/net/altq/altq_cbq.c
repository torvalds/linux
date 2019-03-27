/*-
 * Copyright (c) Sun Microsystems, Inc. 1993-1998 All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *      This product includes software developed by the SMCC Technology
 *      Development Group at Sun Microsystems, Inc.
 *
 * 4. The name of the Sun Microsystems, Inc nor may not be used to endorse or
 *      promote products derived from this software without specific prior
 *      written permission.
 *
 * SUN MICROSYSTEMS DOES NOT CLAIM MERCHANTABILITY OF THIS SOFTWARE OR THE
 * SUITABILITY OF THIS SOFTWARE FOR ANY PARTICULAR PURPOSE.  The software is
 * provided "as is" without express or implied warranty of any kind.
 *
 * These notices must be retained in any copies of any part of this software.
 *
 * $KAME: altq_cbq.c,v 1.19 2003/09/17 14:23:25 kjc Exp $
 * $FreeBSD$
 */

#include "opt_altq.h"
#include "opt_inet.h"
#include "opt_inet6.h"
#ifdef ALTQ_CBQ	/* cbq is enabled by ALTQ_CBQ option in opt_altq.h */

#include <sys/param.h>
#include <sys/malloc.h>
#include <sys/mbuf.h>
#include <sys/socket.h>
#include <sys/systm.h>
#include <sys/proc.h>
#include <sys/errno.h>
#include <sys/time.h>

#include <net/if.h>
#include <net/if_var.h>
#include <netinet/in.h>

#include <netpfil/pf/pf.h>
#include <netpfil/pf/pf_altq.h>
#include <netpfil/pf/pf_mtag.h>
#include <net/altq/altq.h>
#include <net/altq/altq_cbq.h>


/*
 * Forward Declarations.
 */
static int		 cbq_class_destroy(cbq_state_t *, struct rm_class *);
static struct rm_class  *clh_to_clp(cbq_state_t *, u_int32_t);
static int		 cbq_clear_interface(cbq_state_t *);
static int		 cbq_request(struct ifaltq *, int, void *);
static int		 cbq_enqueue(struct ifaltq *, struct mbuf *,
			     struct altq_pktattr *);
static struct mbuf	*cbq_dequeue(struct ifaltq *, int);
static void		 cbqrestart(struct ifaltq *);
static void		 get_class_stats(class_stats_t *, struct rm_class *);
static void		 cbq_purge(cbq_state_t *);

/*
 * int
 * cbq_class_destroy(cbq_mod_state_t *, struct rm_class *) - This
 *	function destroys a given traffic class.  Before destroying
 *	the class, all traffic for that class is released.
 */
static int
cbq_class_destroy(cbq_state_t *cbqp, struct rm_class *cl)
{
	int	i;

	/* delete the class */
	rmc_delete_class(&cbqp->ifnp, cl);

	/*
	 * free the class handle
	 */
	for (i = 0; i < CBQ_MAX_CLASSES; i++)
		if (cbqp->cbq_class_tbl[i] == cl)
			cbqp->cbq_class_tbl[i] = NULL;

	if (cl == cbqp->ifnp.root_)
		cbqp->ifnp.root_ = NULL;
	if (cl == cbqp->ifnp.default_)
		cbqp->ifnp.default_ = NULL;
	return (0);
}

/* convert class handle to class pointer */
static struct rm_class *
clh_to_clp(cbq_state_t *cbqp, u_int32_t chandle)
{
	int i;
	struct rm_class *cl;

	if (chandle == 0)
		return (NULL);
	/*
	 * first, try optimistically the slot matching the lower bits of
	 * the handle.  if it fails, do the linear table search.
	 */
	i = chandle % CBQ_MAX_CLASSES;
	if ((cl = cbqp->cbq_class_tbl[i]) != NULL &&
	    cl->stats_.handle == chandle)
		return (cl);
	for (i = 0; i < CBQ_MAX_CLASSES; i++)
		if ((cl = cbqp->cbq_class_tbl[i]) != NULL &&
		    cl->stats_.handle == chandle)
			return (cl);
	return (NULL);
}

static int
cbq_clear_interface(cbq_state_t *cbqp)
{
	int		 again, i;
	struct rm_class	*cl;

#ifdef ALTQ3_CLFIER_COMPAT
	/* free the filters for this interface */
	acc_discard_filters(&cbqp->cbq_classifier, NULL, 1);
#endif

	/* clear out the classes now */
	do {
		again = 0;
		for (i = 0; i < CBQ_MAX_CLASSES; i++) {
			if ((cl = cbqp->cbq_class_tbl[i]) != NULL) {
				if (is_a_parent_class(cl))
					again++;
				else {
					cbq_class_destroy(cbqp, cl);
					cbqp->cbq_class_tbl[i] = NULL;
					if (cl == cbqp->ifnp.root_)
						cbqp->ifnp.root_ = NULL;
					if (cl == cbqp->ifnp.default_)
						cbqp->ifnp.default_ = NULL;
				}
			}
		}
	} while (again);

	return (0);
}

static int
cbq_request(struct ifaltq *ifq, int req, void *arg)
{
	cbq_state_t	*cbqp = (cbq_state_t *)ifq->altq_disc;

	IFQ_LOCK_ASSERT(ifq);

	switch (req) {
	case ALTRQ_PURGE:
		cbq_purge(cbqp);
		break;
	}
	return (0);
}

/* copy the stats info in rm_class to class_states_t */
static void
get_class_stats(class_stats_t *statsp, struct rm_class *cl)
{
	statsp->xmit_cnt	= cl->stats_.xmit_cnt;
	statsp->drop_cnt	= cl->stats_.drop_cnt;
	statsp->over		= cl->stats_.over;
	statsp->borrows		= cl->stats_.borrows;
	statsp->overactions	= cl->stats_.overactions;
	statsp->delays		= cl->stats_.delays;

	statsp->depth		= cl->depth_;
	statsp->priority	= cl->pri_;
	statsp->maxidle		= cl->maxidle_;
	statsp->minidle		= cl->minidle_;
	statsp->offtime		= cl->offtime_;
	statsp->qmax		= qlimit(cl->q_);
	statsp->ns_per_byte	= cl->ns_per_byte_;
	statsp->wrr_allot	= cl->w_allotment_;
	statsp->qcnt		= qlen(cl->q_);
	statsp->avgidle		= cl->avgidle_;

	statsp->qtype		= qtype(cl->q_);
#ifdef ALTQ_RED
	if (q_is_red(cl->q_))
		red_getstats(cl->red_, &statsp->red[0]);
#endif
#ifdef ALTQ_RIO
	if (q_is_rio(cl->q_))
		rio_getstats((rio_t *)cl->red_, &statsp->red[0]);
#endif
#ifdef ALTQ_CODEL
	if (q_is_codel(cl->q_))
		codel_getstats(cl->codel_, &statsp->codel);
#endif
}

int
cbq_pfattach(struct pf_altq *a)
{
	struct ifnet	*ifp;
	int		 s, error;

	if ((ifp = ifunit(a->ifname)) == NULL || a->altq_disc == NULL)
		return (EINVAL);
	s = splnet();
	error = altq_attach(&ifp->if_snd, ALTQT_CBQ, a->altq_disc,
	    cbq_enqueue, cbq_dequeue, cbq_request, NULL, NULL);
	splx(s);
	return (error);
}

int
cbq_add_altq(struct ifnet *ifp, struct pf_altq *a)
{
	cbq_state_t	*cbqp;

	if (ifp == NULL)
		return (EINVAL);
	if (!ALTQ_IS_READY(&ifp->if_snd))
		return (ENODEV);

	/* allocate and initialize cbq_state_t */
	cbqp = malloc(sizeof(cbq_state_t), M_DEVBUF, M_NOWAIT | M_ZERO);
	if (cbqp == NULL)
		return (ENOMEM);
	CALLOUT_INIT(&cbqp->cbq_callout);
	cbqp->cbq_qlen = 0;
	cbqp->ifnp.ifq_ = &ifp->if_snd;	    /* keep the ifq */

	/* keep the state in pf_altq */
	a->altq_disc = cbqp;

	return (0);
}

int
cbq_remove_altq(struct pf_altq *a)
{
	cbq_state_t	*cbqp;

	if ((cbqp = a->altq_disc) == NULL)
		return (EINVAL);
	a->altq_disc = NULL;

	cbq_clear_interface(cbqp);

	if (cbqp->ifnp.default_)
		cbq_class_destroy(cbqp, cbqp->ifnp.default_);
	if (cbqp->ifnp.root_)
		cbq_class_destroy(cbqp, cbqp->ifnp.root_);

	/* deallocate cbq_state_t */
	free(cbqp, M_DEVBUF);

	return (0);
}

int
cbq_add_queue(struct pf_altq *a)
{
	struct rm_class	*borrow, *parent;
	cbq_state_t	*cbqp;
	struct rm_class	*cl;
	struct cbq_opts	*opts;
	int		i;

	if ((cbqp = a->altq_disc) == NULL)
		return (EINVAL);
	if (a->qid == 0)
		return (EINVAL);

	/*
	 * find a free slot in the class table.  if the slot matching
	 * the lower bits of qid is free, use this slot.  otherwise,
	 * use the first free slot.
	 */
	i = a->qid % CBQ_MAX_CLASSES;
	if (cbqp->cbq_class_tbl[i] != NULL) {
		for (i = 0; i < CBQ_MAX_CLASSES; i++)
			if (cbqp->cbq_class_tbl[i] == NULL)
				break;
		if (i == CBQ_MAX_CLASSES)
			return (EINVAL);
	}

	opts = &a->pq_u.cbq_opts;
	/* check parameters */
	if (a->priority >= CBQ_MAXPRI)
		return (EINVAL);

	/* Get pointers to parent and borrow classes.  */
	parent = clh_to_clp(cbqp, a->parent_qid);
	if (opts->flags & CBQCLF_BORROW)
		borrow = parent;
	else
		borrow = NULL;

	/*
	 * A class must borrow from it's parent or it can not
	 * borrow at all.  Hence, borrow can be null.
	 */
	if (parent == NULL && (opts->flags & CBQCLF_ROOTCLASS) == 0) {
		printf("cbq_add_queue: no parent class!\n");
		return (EINVAL);
	}

	if ((borrow != parent)  && (borrow != NULL)) {
		printf("cbq_add_class: borrow class != parent\n");
		return (EINVAL);
	}

	/*
	 * check parameters
	 */
	switch (opts->flags & CBQCLF_CLASSMASK) {
	case CBQCLF_ROOTCLASS:
		if (parent != NULL)
			return (EINVAL);
		if (cbqp->ifnp.root_)
			return (EINVAL);
		break;
	case CBQCLF_DEFCLASS:
		if (cbqp->ifnp.default_)
			return (EINVAL);
		break;
	case 0:
		if (a->qid == 0)
			return (EINVAL);
		break;
	default:
		/* more than two flags bits set */
		return (EINVAL);
	}

	/*
	 * create a class.  if this is a root class, initialize the
	 * interface.
	 */
	if ((opts->flags & CBQCLF_CLASSMASK) == CBQCLF_ROOTCLASS) {
		rmc_init(cbqp->ifnp.ifq_, &cbqp->ifnp, opts->ns_per_byte,
		    cbqrestart, a->qlimit, RM_MAXQUEUED,
		    opts->maxidle, opts->minidle, opts->offtime,
		    opts->flags);
		cl = cbqp->ifnp.root_;
	} else {
		cl = rmc_newclass(a->priority,
				  &cbqp->ifnp, opts->ns_per_byte,
				  rmc_delay_action, a->qlimit, parent, borrow,
				  opts->maxidle, opts->minidle, opts->offtime,
				  opts->pktsize, opts->flags);
	}
	if (cl == NULL)
		return (ENOMEM);

	/* return handle to user space. */
	cl->stats_.handle = a->qid;
	cl->stats_.depth = cl->depth_;

	/* save the allocated class */
	cbqp->cbq_class_tbl[i] = cl;

	if ((opts->flags & CBQCLF_CLASSMASK) == CBQCLF_DEFCLASS)
		cbqp->ifnp.default_ = cl;

	return (0);
}

int
cbq_remove_queue(struct pf_altq *a)
{
	struct rm_class	*cl;
	cbq_state_t	*cbqp;
	int		i;

	if ((cbqp = a->altq_disc) == NULL)
		return (EINVAL);

	if ((cl = clh_to_clp(cbqp, a->qid)) == NULL)
		return (EINVAL);

	/* if we are a parent class, then return an error. */
	if (is_a_parent_class(cl))
		return (EINVAL);

	/* delete the class */
	rmc_delete_class(&cbqp->ifnp, cl);

	/*
	 * free the class handle
	 */
	for (i = 0; i < CBQ_MAX_CLASSES; i++)
		if (cbqp->cbq_class_tbl[i] == cl) {
			cbqp->cbq_class_tbl[i] = NULL;
			if (cl == cbqp->ifnp.root_)
				cbqp->ifnp.root_ = NULL;
			if (cl == cbqp->ifnp.default_)
				cbqp->ifnp.default_ = NULL;
			break;
		}

	return (0);
}

int
cbq_getqstats(struct pf_altq *a, void *ubuf, int *nbytes, int version)
{
	cbq_state_t	*cbqp;
	struct rm_class	*cl;
	class_stats_t	 stats;
	int		 error = 0;

	if ((cbqp = altq_lookup(a->ifname, ALTQT_CBQ)) == NULL)
		return (EBADF);

	if ((cl = clh_to_clp(cbqp, a->qid)) == NULL)
		return (EINVAL);

	if (*nbytes < sizeof(stats))
		return (EINVAL);

	get_class_stats(&stats, cl);

	if ((error = copyout((caddr_t)&stats, ubuf, sizeof(stats))) != 0)
		return (error);
	*nbytes = sizeof(stats);
	return (0);
}

/*
 * int
 * cbq_enqueue(struct ifaltq *ifq, struct mbuf *m, struct altq_pktattr *pattr)
 *		- Queue data packets.
 *
 *	cbq_enqueue is set to ifp->if_altqenqueue and called by an upper
 *	layer (e.g. ether_output).  cbq_enqueue queues the given packet
 *	to the cbq, then invokes the driver's start routine.
 *
 *	Assumptions:	called in splimp
 *	Returns:	0 if the queueing is successful.
 *			ENOBUFS if a packet dropping occurred as a result of
 *			the queueing.
 */

static int
cbq_enqueue(struct ifaltq *ifq, struct mbuf *m, struct altq_pktattr *pktattr)
{
	cbq_state_t	*cbqp = (cbq_state_t *)ifq->altq_disc;
	struct rm_class	*cl;
	struct pf_mtag	*t;
	int		 len;

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
		cl = clh_to_clp(cbqp, t->qid);
	if (cl == NULL) {
		cl = cbqp->ifnp.default_;
		if (cl == NULL) {
			m_freem(m);
			return (ENOBUFS);
		}
	}
	cl->pktattr_ = NULL;
	len = m_pktlen(m);
	if (rmc_queue_packet(cl, m) != 0) {
		/* drop occurred.  some mbuf was freed in rmc_queue_packet. */
		PKTCNTR_ADD(&cl->stats_.drop_cnt, len);
		return (ENOBUFS);
	}

	/* successfully queued. */
	++cbqp->cbq_qlen;
	IFQ_INC_LEN(ifq);
	return (0);
}

static struct mbuf *
cbq_dequeue(struct ifaltq *ifq, int op)
{
	cbq_state_t	*cbqp = (cbq_state_t *)ifq->altq_disc;
	struct mbuf	*m;

	IFQ_LOCK_ASSERT(ifq);

	m = rmc_dequeue_next(&cbqp->ifnp, op);

	if (m && op == ALTDQ_REMOVE) {
		--cbqp->cbq_qlen;  /* decrement # of packets in cbq */
		IFQ_DEC_LEN(ifq);

		/* Update the class. */
		rmc_update_class_util(&cbqp->ifnp);
	}
	return (m);
}

/*
 * void
 * cbqrestart(queue_t *) - Restart sending of data.
 * called from rmc_restart in splimp via timeout after waking up
 * a suspended class.
 *	Returns:	NONE
 */

static void
cbqrestart(struct ifaltq *ifq)
{
	cbq_state_t	*cbqp;
	struct ifnet	*ifp;

	IFQ_LOCK_ASSERT(ifq);

	if (!ALTQ_IS_ENABLED(ifq))
		/* cbq must have been detached */
		return;

	if ((cbqp = (cbq_state_t *)ifq->altq_disc) == NULL)
		/* should not happen */
		return;

	ifp = ifq->altq_ifp;
	if (ifp->if_start &&
	    cbqp->cbq_qlen > 0 && (ifp->if_drv_flags & IFF_DRV_OACTIVE) == 0) {
	    	IFQ_UNLOCK(ifq);
		(*ifp->if_start)(ifp);
		IFQ_LOCK(ifq);
	}
}

static void cbq_purge(cbq_state_t *cbqp)
{
	struct rm_class	*cl;
	int		 i;

	for (i = 0; i < CBQ_MAX_CLASSES; i++)
		if ((cl = cbqp->cbq_class_tbl[i]) != NULL)
			rmc_dropall(cl);
	if (ALTQ_IS_ENABLED(cbqp->ifnp.ifq_))
		cbqp->ifnp.ifq_->ifq_len = 0;
}

#endif /* ALTQ_CBQ */
