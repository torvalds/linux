/*-
 * Copyright (c) 1991-1997 Regents of the University of California.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *      This product includes software developed by the Network Research
 *      Group at Lawrence Berkeley Laboratory.
 * 4. Neither the name of the University nor of the Laboratory may be used
 *    to endorse or promote products derived from this software without
 *    specific prior written permission.
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
 * LBL code modified by speer@eng.sun.com, May 1977.
 * For questions and/or comments, please send mail to cbq@ee.lbl.gov
 *
 * @(#)rm_class.c  1.48     97/12/05 SMI
 * $KAME: altq_rmclass.c,v 1.19 2005/04/13 03:44:25 suz Exp $
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
#include <sys/errno.h>
#include <sys/time.h>

#include <net/if.h>
#include <net/if_var.h>

#include <net/altq/if_altq.h>
#include <net/altq/altq.h>
#include <net/altq/altq_codel.h>
#include <net/altq/altq_rmclass.h>
#include <net/altq/altq_rmclass_debug.h>
#include <net/altq/altq_red.h>
#include <net/altq/altq_rio.h>

/*
 * Local Macros
 */

#define	reset_cutoff(ifd)	{ ifd->cutoff_ = RM_MAXDEPTH; }

/*
 * Local routines.
 */

static int	rmc_satisfied(struct rm_class *, struct timeval *);
static void	rmc_wrr_set_weights(struct rm_ifdat *);
static void	rmc_depth_compute(struct rm_class *);
static void	rmc_depth_recompute(rm_class_t *);

static mbuf_t	*_rmc_wrr_dequeue_next(struct rm_ifdat *, int);
static mbuf_t	*_rmc_prr_dequeue_next(struct rm_ifdat *, int);

static int	_rmc_addq(rm_class_t *, mbuf_t *);
static void	_rmc_dropq(rm_class_t *);
static mbuf_t	*_rmc_getq(rm_class_t *);
static mbuf_t	*_rmc_pollq(rm_class_t *);

static int	rmc_under_limit(struct rm_class *, struct timeval *);
static void	rmc_tl_satisfied(struct rm_ifdat *, struct timeval *);
static void	rmc_drop_action(struct rm_class *);
static void	rmc_restart(struct rm_class *);
static void	rmc_root_overlimit(struct rm_class *, struct rm_class *);

#define	BORROW_OFFTIME
/*
 * BORROW_OFFTIME (experimental):
 * borrow the offtime of the class borrowing from.
 * the reason is that when its own offtime is set, the class is unable
 * to borrow much, especially when cutoff is taking effect.
 * but when the borrowed class is overloaded (advidle is close to minidle),
 * use the borrowing class's offtime to avoid overload.
 */
#define	ADJUST_CUTOFF
/*
 * ADJUST_CUTOFF (experimental):
 * if no underlimit class is found due to cutoff, increase cutoff and
 * retry the scheduling loop.
 * also, don't invoke delay_actions while cutoff is taking effect,
 * since a sleeping class won't have a chance to be scheduled in the
 * next loop.
 *
 * now heuristics for setting the top-level variable (cutoff_) becomes:
 *	1. if a packet arrives for a not-overlimit class, set cutoff
 *	   to the depth of the class.
 *	2. if cutoff is i, and a packet arrives for an overlimit class
 *	   with an underlimit ancestor at a lower level than i (say j),
 *	   then set cutoff to j.
 *	3. at scheduling a packet, if there is no underlimit class
 *	   due to the current cutoff level, increase cutoff by 1 and
 *	   then try to schedule again.
 */

/*
 * rm_class_t *
 * rmc_newclass(...) - Create a new resource management class at priority
 * 'pri' on the interface given by 'ifd'.
 *
 * nsecPerByte  is the data rate of the interface in nanoseconds/byte.
 *              E.g., 800 for a 10Mb/s ethernet.  If the class gets less
 *              than 100% of the bandwidth, this number should be the
 *              'effective' rate for the class.  Let f be the
 *              bandwidth fraction allocated to this class, and let
 *              nsPerByte be the data rate of the output link in
 *              nanoseconds/byte.  Then nsecPerByte is set to
 *              nsPerByte / f.  E.g., 1600 (= 800 / .5)
 *              for a class that gets 50% of an ethernet's bandwidth.
 *
 * action       the routine to call when the class is over limit.
 *
 * maxq         max allowable queue size for class (in packets).
 *
 * parent       parent class pointer.
 *
 * borrow       class to borrow from (should be either 'parent' or null).
 *
 * maxidle      max value allowed for class 'idle' time estimate (this
 *              parameter determines how large an initial burst of packets
 *              can be before overlimit action is invoked.
 *
 * offtime      how long 'delay' action will delay when class goes over
 *              limit (this parameter determines the steady-state burst
 *              size when a class is running over its limit).
 *
 * Maxidle and offtime have to be computed from the following:  If the
 * average packet size is s, the bandwidth fraction allocated to this
 * class is f, we want to allow b packet bursts, and the gain of the
 * averaging filter is g (= 1 - 2^(-RM_FILTER_GAIN)), then:
 *
 *   ptime = s * nsPerByte * (1 - f) / f
 *   maxidle = ptime * (1 - g^b) / g^b
 *   minidle = -ptime * (1 / (f - 1))
 *   offtime = ptime * (1 + 1/(1 - g) * (1 - g^(b - 1)) / g^(b - 1)
 *
 * Operationally, it's convenient to specify maxidle & offtime in units
 * independent of the link bandwidth so the maxidle & offtime passed to
 * this routine are the above values multiplied by 8*f/(1000*nsPerByte).
 * (The constant factor is a scale factor needed to make the parameters
 * integers.  This scaling also means that the 'unscaled' values of
 * maxidle*nsecPerByte/8 and offtime*nsecPerByte/8 will be in microseconds,
 * not nanoseconds.)  Also note that the 'idle' filter computation keeps
 * an estimate scaled upward by 2^RM_FILTER_GAIN so the passed value of
 * maxidle also must be scaled upward by this value.  Thus, the passed
 * values for maxidle and offtime can be computed as follows:
 *
 * maxidle = maxidle * 2^RM_FILTER_GAIN * 8 / (1000 * nsecPerByte)
 * offtime = offtime * 8 / (1000 * nsecPerByte)
 *
 * When USE_HRTIME is employed, then maxidle and offtime become:
 * 	maxidle = maxilde * (8.0 / nsecPerByte);
 * 	offtime = offtime * (8.0 / nsecPerByte);
 */
struct rm_class *
rmc_newclass(int pri, struct rm_ifdat *ifd, u_int nsecPerByte,
    void (*action)(rm_class_t *, rm_class_t *), int maxq,
    struct rm_class *parent, struct rm_class *borrow, u_int maxidle,
    int minidle, u_int offtime, int pktsize, int flags)
{
	struct rm_class	*cl;
	struct rm_class	*peer;
	int		 s;

	if (pri >= RM_MAXPRIO)
		return (NULL);
#ifndef ALTQ_RED
	if (flags & RMCF_RED) {
#ifdef ALTQ_DEBUG
		printf("rmc_newclass: RED not configured for CBQ!\n");
#endif
		return (NULL);
	}
#endif
#ifndef ALTQ_RIO
	if (flags & RMCF_RIO) {
#ifdef ALTQ_DEBUG
		printf("rmc_newclass: RIO not configured for CBQ!\n");
#endif
		return (NULL);
	}
#endif
#ifndef ALTQ_CODEL
	if (flags & RMCF_CODEL) {
#ifdef ALTQ_DEBUG
		printf("rmc_newclass: CODEL not configured for CBQ!\n");
#endif
		return (NULL);
	}
#endif

	cl = malloc(sizeof(struct rm_class), M_DEVBUF, M_NOWAIT | M_ZERO);
	if (cl == NULL)
		return (NULL);
	CALLOUT_INIT(&cl->callout_);
	cl->q_ = malloc(sizeof(class_queue_t), M_DEVBUF, M_NOWAIT | M_ZERO);
	if (cl->q_ == NULL) {
		free(cl, M_DEVBUF);
		return (NULL);
	}

	/*
	 * Class initialization.
	 */
	cl->children_ = NULL;
	cl->parent_ = parent;
	cl->borrow_ = borrow;
	cl->leaf_ = 1;
	cl->ifdat_ = ifd;
	cl->pri_ = pri;
	cl->allotment_ = RM_NS_PER_SEC / nsecPerByte; /* Bytes per sec */
	cl->depth_ = 0;
	cl->qthresh_ = 0;
	cl->ns_per_byte_ = nsecPerByte;

	qlimit(cl->q_) = maxq;
	qtype(cl->q_) = Q_DROPHEAD;
	qlen(cl->q_) = 0;
	cl->flags_ = flags;

#if 1 /* minidle is also scaled in ALTQ */
	cl->minidle_ = (minidle * (int)nsecPerByte) / 8;
	if (cl->minidle_ > 0)
		cl->minidle_ = 0;
#else
	cl->minidle_ = minidle;
#endif
	cl->maxidle_ = (maxidle * nsecPerByte) / 8;
	if (cl->maxidle_ == 0)
		cl->maxidle_ = 1;
#if 1 /* offtime is also scaled in ALTQ */
	cl->avgidle_ = cl->maxidle_;
	cl->offtime_ = ((offtime * nsecPerByte) / 8) >> RM_FILTER_GAIN;
	if (cl->offtime_ == 0)
		cl->offtime_ = 1;
#else
	cl->avgidle_ = 0;
	cl->offtime_ = (offtime * nsecPerByte) / 8;
#endif
	cl->overlimit = action;

#ifdef ALTQ_RED
	if (flags & (RMCF_RED|RMCF_RIO)) {
		int red_flags, red_pkttime;

		red_flags = 0;
		if (flags & RMCF_ECN)
			red_flags |= REDF_ECN;
		if (flags & RMCF_FLOWVALVE)
			red_flags |= REDF_FLOWVALVE;
#ifdef ALTQ_RIO
		if (flags & RMCF_CLEARDSCP)
			red_flags |= RIOF_CLEARDSCP;
#endif
		red_pkttime = nsecPerByte * pktsize  / 1000;

		if (flags & RMCF_RED) {
			cl->red_ = red_alloc(0, 0,
			    qlimit(cl->q_) * 10/100,
			    qlimit(cl->q_) * 30/100,
			    red_flags, red_pkttime);
			if (cl->red_ != NULL)
				qtype(cl->q_) = Q_RED;
		}
#ifdef ALTQ_RIO
		else {
			cl->red_ = (red_t *)rio_alloc(0, NULL,
						      red_flags, red_pkttime);
			if (cl->red_ != NULL)
				qtype(cl->q_) = Q_RIO;
		}
#endif
	}
#endif /* ALTQ_RED */
#ifdef ALTQ_CODEL
	if (flags & RMCF_CODEL) {
		cl->codel_ = codel_alloc(5, 100, 0);
		if (cl->codel_ != NULL)
			qtype(cl->q_) = Q_CODEL;
	}
#endif

	/*
	 * put the class into the class tree
	 */
	s = splnet();
	IFQ_LOCK(ifd->ifq_);
	if ((peer = ifd->active_[pri]) != NULL) {
		/* find the last class at this pri */
		cl->peer_ = peer;
		while (peer->peer_ != ifd->active_[pri])
			peer = peer->peer_;
		peer->peer_ = cl;
	} else {
		ifd->active_[pri] = cl;
		cl->peer_ = cl;
	}

	if (cl->parent_) {
		cl->next_ = parent->children_;
		parent->children_ = cl;
		parent->leaf_ = 0;
	}

	/*
	 * Compute the depth of this class and its ancestors in the class
	 * hierarchy.
	 */
	rmc_depth_compute(cl);

	/*
	 * If CBQ's WRR is enabled, then initialize the class WRR state.
	 */
	if (ifd->wrr_) {
		ifd->num_[pri]++;
		ifd->alloc_[pri] += cl->allotment_;
		rmc_wrr_set_weights(ifd);
	}
	IFQ_UNLOCK(ifd->ifq_);
	splx(s);
	return (cl);
}

int
rmc_modclass(struct rm_class *cl, u_int nsecPerByte, int maxq, u_int maxidle,
    int minidle, u_int offtime, int pktsize)
{
	struct rm_ifdat	*ifd;
	u_int		 old_allotment;
	int		 s;

	ifd = cl->ifdat_;
	old_allotment = cl->allotment_;

	s = splnet();
	IFQ_LOCK(ifd->ifq_);
	cl->allotment_ = RM_NS_PER_SEC / nsecPerByte; /* Bytes per sec */
	cl->qthresh_ = 0;
	cl->ns_per_byte_ = nsecPerByte;

	qlimit(cl->q_) = maxq;

#if 1 /* minidle is also scaled in ALTQ */
	cl->minidle_ = (minidle * nsecPerByte) / 8;
	if (cl->minidle_ > 0)
		cl->minidle_ = 0;
#else
	cl->minidle_ = minidle;
#endif
	cl->maxidle_ = (maxidle * nsecPerByte) / 8;
	if (cl->maxidle_ == 0)
		cl->maxidle_ = 1;
#if 1 /* offtime is also scaled in ALTQ */
	cl->avgidle_ = cl->maxidle_;
	cl->offtime_ = ((offtime * nsecPerByte) / 8) >> RM_FILTER_GAIN;
	if (cl->offtime_ == 0)
		cl->offtime_ = 1;
#else
	cl->avgidle_ = 0;
	cl->offtime_ = (offtime * nsecPerByte) / 8;
#endif

	/*
	 * If CBQ's WRR is enabled, then initialize the class WRR state.
	 */
	if (ifd->wrr_) {
		ifd->alloc_[cl->pri_] += cl->allotment_ - old_allotment;
		rmc_wrr_set_weights(ifd);
	}
	IFQ_UNLOCK(ifd->ifq_);
	splx(s);
	return (0);
}

/*
 * static void
 * rmc_wrr_set_weights(struct rm_ifdat *ifdat) - This function computes
 *	the appropriate run robin weights for the CBQ weighted round robin
 *	algorithm.
 *
 *	Returns: NONE
 */

static void
rmc_wrr_set_weights(struct rm_ifdat *ifd)
{
	int		i;
	struct rm_class	*cl, *clh;

	for (i = 0; i < RM_MAXPRIO; i++) {
		/*
		 * This is inverted from that of the simulator to
		 * maintain precision.
		 */
		if (ifd->num_[i] == 0)
			ifd->M_[i] = 0;
		else
			ifd->M_[i] = ifd->alloc_[i] /
				(ifd->num_[i] * ifd->maxpkt_);
		/*
		 * Compute the weighted allotment for each class.
		 * This takes the expensive div instruction out
		 * of the main loop for the wrr scheduling path.
		 * These only get recomputed when a class comes or
		 * goes.
		 */
		if (ifd->active_[i] != NULL) {
			clh = cl = ifd->active_[i];
			do {
				/* safe-guard for slow link or alloc_ == 0 */
				if (ifd->M_[i] == 0)
					cl->w_allotment_ = 0;
				else
					cl->w_allotment_ = cl->allotment_ /
						ifd->M_[i];
				cl = cl->peer_;
			} while ((cl != NULL) && (cl != clh));
		}
	}
}

int
rmc_get_weight(struct rm_ifdat *ifd, int pri)
{
	if ((pri >= 0) && (pri < RM_MAXPRIO))
		return (ifd->M_[pri]);
	else
		return (0);
}

/*
 * static void
 * rmc_depth_compute(struct rm_class *cl) - This function computes the
 *	appropriate depth of class 'cl' and its ancestors.
 *
 *	Returns:	NONE
 */

static void
rmc_depth_compute(struct rm_class *cl)
{
	rm_class_t	*t = cl, *p;

	/*
	 * Recompute the depth for the branch of the tree.
	 */
	while (t != NULL) {
		p = t->parent_;
		if (p && (t->depth_ >= p->depth_)) {
			p->depth_ = t->depth_ + 1;
			t = p;
		} else
			t = NULL;
	}
}

/*
 * static void
 * rmc_depth_recompute(struct rm_class *cl) - This function re-computes
 *	the depth of the tree after a class has been deleted.
 *
 *	Returns:	NONE
 */

static void
rmc_depth_recompute(rm_class_t *cl)
{
#if 1 /* ALTQ */
	rm_class_t	*p, *t;

	p = cl;
	while (p != NULL) {
		if ((t = p->children_) == NULL) {
			p->depth_ = 0;
		} else {
			int cdepth = 0;

			while (t != NULL) {
				if (t->depth_ > cdepth)
					cdepth = t->depth_;
				t = t->next_;
			}

			if (p->depth_ == cdepth + 1)
				/* no change to this parent */
				return;

			p->depth_ = cdepth + 1;
		}

		p = p->parent_;
	}
#else
	rm_class_t	*t;

	if (cl->depth_ >= 1) {
		if (cl->children_ == NULL) {
			cl->depth_ = 0;
		} else if ((t = cl->children_) != NULL) {
			while (t != NULL) {
				if (t->children_ != NULL)
					rmc_depth_recompute(t);
				t = t->next_;
			}
		} else
			rmc_depth_compute(cl);
	}
#endif
}

/*
 * void
 * rmc_delete_class(struct rm_ifdat *ifdat, struct rm_class *cl) - This
 *	function deletes a class from the link-sharing structure and frees
 *	all resources associated with the class.
 *
 *	Returns: NONE
 */

void
rmc_delete_class(struct rm_ifdat *ifd, struct rm_class *cl)
{
	struct rm_class	*p, *head, *previous;
	int		 s;

	ASSERT(cl->children_ == NULL);

	if (cl->sleeping_)
		CALLOUT_STOP(&cl->callout_);

	s = splnet();
	IFQ_LOCK(ifd->ifq_);
	/*
	 * Free packets in the packet queue.
	 * XXX - this may not be a desired behavior.  Packets should be
	 *		re-queued.
	 */
	rmc_dropall(cl);

	/*
	 * If the class has a parent, then remove the class from the
	 * class from the parent's children chain.
	 */
	if (cl->parent_ != NULL) {
		head = cl->parent_->children_;
		p = previous = head;
		if (head->next_ == NULL) {
			ASSERT(head == cl);
			cl->parent_->children_ = NULL;
			cl->parent_->leaf_ = 1;
		} else while (p != NULL) {
			if (p == cl) {
				if (cl == head)
					cl->parent_->children_ = cl->next_;
				else
					previous->next_ = cl->next_;
				cl->next_ = NULL;
				p = NULL;
			} else {
				previous = p;
				p = p->next_;
			}
		}
	}

	/*
	 * Delete class from class priority peer list.
	 */
	if ((p = ifd->active_[cl->pri_]) != NULL) {
		/*
		 * If there is more than one member of this priority
		 * level, then look for class(cl) in the priority level.
		 */
		if (p != p->peer_) {
			while (p->peer_ != cl)
				p = p->peer_;
			p->peer_ = cl->peer_;

			if (ifd->active_[cl->pri_] == cl)
				ifd->active_[cl->pri_] = cl->peer_;
		} else {
			ASSERT(p == cl);
			ifd->active_[cl->pri_] = NULL;
		}
	}

	/*
	 * Recompute the WRR weights.
	 */
	if (ifd->wrr_) {
		ifd->alloc_[cl->pri_] -= cl->allotment_;
		ifd->num_[cl->pri_]--;
		rmc_wrr_set_weights(ifd);
	}

	/*
	 * Re-compute the depth of the tree.
	 */
#if 1 /* ALTQ */
	rmc_depth_recompute(cl->parent_);
#else
	rmc_depth_recompute(ifd->root_);
#endif

	IFQ_UNLOCK(ifd->ifq_);
	splx(s);

	/*
	 * Free the class structure.
	 */
	if (cl->red_ != NULL) {
#ifdef ALTQ_RIO
		if (q_is_rio(cl->q_))
			rio_destroy((rio_t *)cl->red_);
#endif
#ifdef ALTQ_RED
		if (q_is_red(cl->q_))
			red_destroy(cl->red_);
#endif
#ifdef ALTQ_CODEL
		if (q_is_codel(cl->q_))
			codel_destroy(cl->codel_);
#endif
	}
	free(cl->q_, M_DEVBUF);
	free(cl, M_DEVBUF);
}


/*
 * void
 * rmc_init(...) - Initialize the resource management data structures
 *	associated with the output portion of interface 'ifp'.  'ifd' is
 *	where the structures will be built (for backwards compatibility, the
 *	structures aren't kept in the ifnet struct).  'nsecPerByte'
 *	gives the link speed (inverse of bandwidth) in nanoseconds/byte.
 *	'restart' is the driver-specific routine that the generic 'delay
 *	until under limit' action will call to restart output.  `maxq'
 *	is the queue size of the 'link' & 'default' classes.  'maxqueued'
 *	is the maximum number of packets that the resource management
 *	code will allow to be queued 'downstream' (this is typically 1).
 *
 *	Returns:	NONE
 */

void
rmc_init(struct ifaltq *ifq, struct rm_ifdat *ifd, u_int nsecPerByte,
    void (*restart)(struct ifaltq *), int maxq, int maxqueued, u_int maxidle,
    int minidle, u_int offtime, int flags)
{
	int		i, mtu;

	/*
	 * Initialize the CBQ tracing/debug facility.
	 */
	CBQTRACEINIT();

	bzero((char *)ifd, sizeof (*ifd));
	mtu = ifq->altq_ifp->if_mtu;
	ifd->ifq_ = ifq;
	ifd->restart = restart;
	ifd->maxqueued_ = maxqueued;
	ifd->ns_per_byte_ = nsecPerByte;
	ifd->maxpkt_ = mtu;
	ifd->wrr_ = (flags & RMCF_WRR) ? 1 : 0;
	ifd->efficient_ = (flags & RMCF_EFFICIENT) ? 1 : 0;
#if 1
	ifd->maxiftime_ = mtu * nsecPerByte / 1000 * 16;
	if (mtu * nsecPerByte > 10 * 1000000)
		ifd->maxiftime_ /= 4;
#endif

	reset_cutoff(ifd);
	CBQTRACE(rmc_init, 'INIT', ifd->cutoff_);

	/*
	 * Initialize the CBQ's WRR state.
	 */
	for (i = 0; i < RM_MAXPRIO; i++) {
		ifd->alloc_[i] = 0;
		ifd->M_[i] = 0;
		ifd->num_[i] = 0;
		ifd->na_[i] = 0;
		ifd->active_[i] = NULL;
	}

	/*
	 * Initialize current packet state.
	 */
	ifd->qi_ = 0;
	ifd->qo_ = 0;
	for (i = 0; i < RM_MAXQUEUED; i++) {
		ifd->class_[i] = NULL;
		ifd->curlen_[i] = 0;
		ifd->borrowed_[i] = NULL;
	}

	/*
	 * Create the root class of the link-sharing structure.
	 */
	if ((ifd->root_ = rmc_newclass(0, ifd,
				       nsecPerByte,
				       rmc_root_overlimit, maxq, 0, 0,
				       maxidle, minidle, offtime,
				       0, 0)) == NULL) {
		printf("rmc_init: root class not allocated\n");
		return ;
	}
	ifd->root_->depth_ = 0;
}

/*
 * void
 * rmc_queue_packet(struct rm_class *cl, mbuf_t *m) - Add packet given by
 *	mbuf 'm' to queue for resource class 'cl'.  This routine is called
 *	by a driver's if_output routine.  This routine must be called with
 *	output packet completion interrupts locked out (to avoid racing with
 *	rmc_dequeue_next).
 *
 *	Returns:	0 on successful queueing
 *			-1 when packet drop occurs
 */
int
rmc_queue_packet(struct rm_class *cl, mbuf_t *m)
{
	struct timeval	 now;
	struct rm_ifdat *ifd = cl->ifdat_;
	int		 cpri = cl->pri_;
	int		 is_empty = qempty(cl->q_);

	RM_GETTIME(now);
	if (ifd->cutoff_ > 0) {
		if (TV_LT(&cl->undertime_, &now)) {
			if (ifd->cutoff_ > cl->depth_)
				ifd->cutoff_ = cl->depth_;
			CBQTRACE(rmc_queue_packet, 'ffoc', cl->depth_);
		}
#if 1 /* ALTQ */
		else {
			/*
			 * the class is overlimit. if the class has
			 * underlimit ancestors, set cutoff to the lowest
			 * depth among them.
			 */
			struct rm_class *borrow = cl->borrow_;

			while (borrow != NULL &&
			       borrow->depth_ < ifd->cutoff_) {
				if (TV_LT(&borrow->undertime_, &now)) {
					ifd->cutoff_ = borrow->depth_;
					CBQTRACE(rmc_queue_packet, 'ffob', ifd->cutoff_);
					break;
				}
				borrow = borrow->borrow_;
			}
		}
#else /* !ALTQ */
		else if ((ifd->cutoff_ > 1) && cl->borrow_) {
			if (TV_LT(&cl->borrow_->undertime_, &now)) {
				ifd->cutoff_ = cl->borrow_->depth_;
				CBQTRACE(rmc_queue_packet, 'ffob',
					 cl->borrow_->depth_);
			}
		}
#endif /* !ALTQ */
	}

	if (_rmc_addq(cl, m) < 0)
		/* failed */
		return (-1);

	if (is_empty) {
		CBQTRACE(rmc_queue_packet, 'ytpe', cl->stats_.handle);
		ifd->na_[cpri]++;
	}

	if (qlen(cl->q_) > qlimit(cl->q_)) {
		/* note: qlimit can be set to 0 or 1 */
		rmc_drop_action(cl);
		return (-1);
	}
	return (0);
}

/*
 * void
 * rmc_tl_satisfied(struct rm_ifdat *ifd, struct timeval *now) - Check all
 *	classes to see if there are satified.
 */

static void
rmc_tl_satisfied(struct rm_ifdat *ifd, struct timeval *now)
{
	int		 i;
	rm_class_t	*p, *bp;

	for (i = RM_MAXPRIO - 1; i >= 0; i--) {
		if ((bp = ifd->active_[i]) != NULL) {
			p = bp;
			do {
				if (!rmc_satisfied(p, now)) {
					ifd->cutoff_ = p->depth_;
					return;
				}
				p = p->peer_;
			} while (p != bp);
		}
	}

	reset_cutoff(ifd);
}

/*
 * rmc_satisfied - Return 1 of the class is satisfied.  O, otherwise.
 */

static int
rmc_satisfied(struct rm_class *cl, struct timeval *now)
{
	rm_class_t	*p;

	if (cl == NULL)
		return (1);
	if (TV_LT(now, &cl->undertime_))
		return (1);
	if (cl->depth_ == 0) {
		if (!cl->sleeping_ && (qlen(cl->q_) > cl->qthresh_))
			return (0);
		else
			return (1);
	}
	if (cl->children_ != NULL) {
		p = cl->children_;
		while (p != NULL) {
			if (!rmc_satisfied(p, now))
				return (0);
			p = p->next_;
		}
	}

	return (1);
}

/*
 * Return 1 if class 'cl' is under limit or can borrow from a parent,
 * 0 if overlimit.  As a side-effect, this routine will invoke the
 * class overlimit action if the class if overlimit.
 */

static int
rmc_under_limit(struct rm_class *cl, struct timeval *now)
{
	rm_class_t	*p = cl;
	rm_class_t	*top;
	struct rm_ifdat	*ifd = cl->ifdat_;

	ifd->borrowed_[ifd->qi_] = NULL;
	/*
	 * If cl is the root class, then always return that it is
	 * underlimit.  Otherwise, check to see if the class is underlimit.
	 */
	if (cl->parent_ == NULL)
		return (1);

	if (cl->sleeping_) {
		if (TV_LT(now, &cl->undertime_))
			return (0);

		CALLOUT_STOP(&cl->callout_);
		cl->sleeping_ = 0;
		cl->undertime_.tv_sec = 0;
		return (1);
	}

	top = NULL;
	while (cl->undertime_.tv_sec && TV_LT(now, &cl->undertime_)) {
		if (((cl = cl->borrow_) == NULL) ||
		    (cl->depth_ > ifd->cutoff_)) {
#ifdef ADJUST_CUTOFF
			if (cl != NULL)
				/* cutoff is taking effect, just
				   return false without calling
				   the delay action. */
				return (0);
#endif
#ifdef BORROW_OFFTIME
			/*
			 * check if the class can borrow offtime too.
			 * borrow offtime from the top of the borrow
			 * chain if the top class is not overloaded.
			 */
			if (cl != NULL) {
				/* cutoff is taking effect, use this class as top. */
				top = cl;
				CBQTRACE(rmc_under_limit, 'ffou', ifd->cutoff_);
			}
			if (top != NULL && top->avgidle_ == top->minidle_)
				top = NULL;
			p->overtime_ = *now;
			(p->overlimit)(p, top);
#else
			p->overtime_ = *now;
			(p->overlimit)(p, NULL);
#endif
			return (0);
		}
		top = cl;
	}

	if (cl != p)
		ifd->borrowed_[ifd->qi_] = cl;
	return (1);
}

/*
 * _rmc_wrr_dequeue_next() - This is scheduler for WRR as opposed to
 *	Packet-by-packet round robin.
 *
 * The heart of the weighted round-robin scheduler, which decides which
 * class next gets to send a packet.  Highest priority first, then
 * weighted round-robin within priorites.
 *
 * Each able-to-send class gets to send until its byte allocation is
 * exhausted.  Thus, the active pointer is only changed after a class has
 * exhausted its allocation.
 *
 * If the scheduler finds no class that is underlimit or able to borrow,
 * then the first class found that had a nonzero queue and is allowed to
 * borrow gets to send.
 */

static mbuf_t *
_rmc_wrr_dequeue_next(struct rm_ifdat *ifd, int op)
{
	struct rm_class	*cl = NULL, *first = NULL;
	u_int		 deficit;
	int		 cpri;
	mbuf_t		*m;
	struct timeval	 now;

	RM_GETTIME(now);

	/*
	 * if the driver polls the top of the queue and then removes
	 * the polled packet, we must return the same packet.
	 */
	if (op == ALTDQ_REMOVE && ifd->pollcache_) {
		cl = ifd->pollcache_;
		cpri = cl->pri_;
		if (ifd->efficient_) {
			/* check if this class is overlimit */
			if (cl->undertime_.tv_sec != 0 &&
			    rmc_under_limit(cl, &now) == 0)
				first = cl;
		}
		ifd->pollcache_ = NULL;
		goto _wrr_out;
	}
	else {
		/* mode == ALTDQ_POLL || pollcache == NULL */
		ifd->pollcache_ = NULL;
		ifd->borrowed_[ifd->qi_] = NULL;
	}
#ifdef ADJUST_CUTOFF
 _again:
#endif
	for (cpri = RM_MAXPRIO - 1; cpri >= 0; cpri--) {
		if (ifd->na_[cpri] == 0)
			continue;
		deficit = 0;
		/*
		 * Loop through twice for a priority level, if some class
		 * was unable to send a packet the first round because
		 * of the weighted round-robin mechanism.
		 * During the second loop at this level, deficit==2.
		 * (This second loop is not needed if for every class,
		 * "M[cl->pri_])" times "cl->allotment" is greater than
		 * the byte size for the largest packet in the class.)
		 */
 _wrr_loop:
		cl = ifd->active_[cpri];
		ASSERT(cl != NULL);
		do {
			if ((deficit < 2) && (cl->bytes_alloc_ <= 0))
				cl->bytes_alloc_ += cl->w_allotment_;
			if (!qempty(cl->q_)) {
				if ((cl->undertime_.tv_sec == 0) ||
				    rmc_under_limit(cl, &now)) {
					if (cl->bytes_alloc_ > 0 || deficit > 1)
						goto _wrr_out;

					/* underlimit but no alloc */
					deficit = 1;
#if 1
					ifd->borrowed_[ifd->qi_] = NULL;
#endif
				}
				else if (first == NULL && cl->borrow_ != NULL)
					first = cl; /* borrowing candidate */
			}

			cl->bytes_alloc_ = 0;
			cl = cl->peer_;
		} while (cl != ifd->active_[cpri]);

		if (deficit == 1) {
			/* first loop found an underlimit class with deficit */
			/* Loop on same priority level, with new deficit.  */
			deficit = 2;
			goto _wrr_loop;
		}
	}

#ifdef ADJUST_CUTOFF
	/*
	 * no underlimit class found.  if cutoff is taking effect,
	 * increase cutoff and try again.
	 */
	if (first != NULL && ifd->cutoff_ < ifd->root_->depth_) {
		ifd->cutoff_++;
		CBQTRACE(_rmc_wrr_dequeue_next, 'ojda', ifd->cutoff_);
		goto _again;
	}
#endif /* ADJUST_CUTOFF */
	/*
	 * If LINK_EFFICIENCY is turned on, then the first overlimit
	 * class we encounter will send a packet if all the classes
	 * of the link-sharing structure are overlimit.
	 */
	reset_cutoff(ifd);
	CBQTRACE(_rmc_wrr_dequeue_next, 'otsr', ifd->cutoff_);

	if (!ifd->efficient_ || first == NULL)
		return (NULL);

	cl = first;
	cpri = cl->pri_;
#if 0	/* too time-consuming for nothing */
	if (cl->sleeping_)
		CALLOUT_STOP(&cl->callout_);
	cl->sleeping_ = 0;
	cl->undertime_.tv_sec = 0;
#endif
	ifd->borrowed_[ifd->qi_] = cl->borrow_;
	ifd->cutoff_ = cl->borrow_->depth_;

	/*
	 * Deque the packet and do the book keeping...
	 */
 _wrr_out:
	if (op == ALTDQ_REMOVE) {
		m = _rmc_getq(cl);
		if (m == NULL)
			panic("_rmc_wrr_dequeue_next");
		if (qempty(cl->q_))
			ifd->na_[cpri]--;

		/*
		 * Update class statistics and link data.
		 */
		if (cl->bytes_alloc_ > 0)
			cl->bytes_alloc_ -= m_pktlen(m);

		if ((cl->bytes_alloc_ <= 0) || first == cl)
			ifd->active_[cl->pri_] = cl->peer_;
		else
			ifd->active_[cl->pri_] = cl;

		ifd->class_[ifd->qi_] = cl;
		ifd->curlen_[ifd->qi_] = m_pktlen(m);
		ifd->now_[ifd->qi_] = now;
		ifd->qi_ = (ifd->qi_ + 1) % ifd->maxqueued_;
		ifd->queued_++;
	} else {
		/* mode == ALTDQ_PPOLL */
		m = _rmc_pollq(cl);
		ifd->pollcache_ = cl;
	}
	return (m);
}

/*
 * Dequeue & return next packet from the highest priority class that
 * has a packet to send & has enough allocation to send it.  This
 * routine is called by a driver whenever it needs a new packet to
 * output.
 */
static mbuf_t *
_rmc_prr_dequeue_next(struct rm_ifdat *ifd, int op)
{
	mbuf_t		*m;
	int		 cpri;
	struct rm_class	*cl, *first = NULL;
	struct timeval	 now;

	RM_GETTIME(now);

	/*
	 * if the driver polls the top of the queue and then removes
	 * the polled packet, we must return the same packet.
	 */
	if (op == ALTDQ_REMOVE && ifd->pollcache_) {
		cl = ifd->pollcache_;
		cpri = cl->pri_;
		ifd->pollcache_ = NULL;
		goto _prr_out;
	} else {
		/* mode == ALTDQ_POLL || pollcache == NULL */
		ifd->pollcache_ = NULL;
		ifd->borrowed_[ifd->qi_] = NULL;
	}
#ifdef ADJUST_CUTOFF
 _again:
#endif
	for (cpri = RM_MAXPRIO - 1; cpri >= 0; cpri--) {
		if (ifd->na_[cpri] == 0)
			continue;
		cl = ifd->active_[cpri];
		ASSERT(cl != NULL);
		do {
			if (!qempty(cl->q_)) {
				if ((cl->undertime_.tv_sec == 0) ||
				    rmc_under_limit(cl, &now))
					goto _prr_out;
				if (first == NULL && cl->borrow_ != NULL)
					first = cl;
			}
			cl = cl->peer_;
		} while (cl != ifd->active_[cpri]);
	}

#ifdef ADJUST_CUTOFF
	/*
	 * no underlimit class found.  if cutoff is taking effect, increase
	 * cutoff and try again.
	 */
	if (first != NULL && ifd->cutoff_ < ifd->root_->depth_) {
		ifd->cutoff_++;
		goto _again;
	}
#endif /* ADJUST_CUTOFF */
	/*
	 * If LINK_EFFICIENCY is turned on, then the first overlimit
	 * class we encounter will send a packet if all the classes
	 * of the link-sharing structure are overlimit.
	 */
	reset_cutoff(ifd);
	if (!ifd->efficient_ || first == NULL)
		return (NULL);

	cl = first;
	cpri = cl->pri_;
#if 0	/* too time-consuming for nothing */
	if (cl->sleeping_)
		CALLOUT_STOP(&cl->callout_);
	cl->sleeping_ = 0;
	cl->undertime_.tv_sec = 0;
#endif
	ifd->borrowed_[ifd->qi_] = cl->borrow_;
	ifd->cutoff_ = cl->borrow_->depth_;

	/*
	 * Deque the packet and do the book keeping...
	 */
 _prr_out:
	if (op == ALTDQ_REMOVE) {
		m = _rmc_getq(cl);
		if (m == NULL)
			panic("_rmc_prr_dequeue_next");
		if (qempty(cl->q_))
			ifd->na_[cpri]--;

		ifd->active_[cpri] = cl->peer_;

		ifd->class_[ifd->qi_] = cl;
		ifd->curlen_[ifd->qi_] = m_pktlen(m);
		ifd->now_[ifd->qi_] = now;
		ifd->qi_ = (ifd->qi_ + 1) % ifd->maxqueued_;
		ifd->queued_++;
	} else {
		/* mode == ALTDQ_POLL */
		m = _rmc_pollq(cl);
		ifd->pollcache_ = cl;
	}
	return (m);
}

/*
 * mbuf_t *
 * rmc_dequeue_next(struct rm_ifdat *ifd, struct timeval *now) - this function
 *	is invoked by the packet driver to get the next packet to be
 *	dequeued and output on the link.  If WRR is enabled, then the
 *	WRR dequeue next routine will determine the next packet to sent.
 *	Otherwise, packet-by-packet round robin is invoked.
 *
 *	Returns:	NULL, if a packet is not available or if all
 *			classes are overlimit.
 *
 *			Otherwise, Pointer to the next packet.
 */

mbuf_t *
rmc_dequeue_next(struct rm_ifdat *ifd, int mode)
{
	if (ifd->queued_ >= ifd->maxqueued_)
		return (NULL);
	else if (ifd->wrr_)
		return (_rmc_wrr_dequeue_next(ifd, mode));
	else
		return (_rmc_prr_dequeue_next(ifd, mode));
}

/*
 * Update the utilization estimate for the packet that just completed.
 * The packet's class & the parent(s) of that class all get their
 * estimators updated.  This routine is called by the driver's output-
 * packet-completion interrupt service routine.
 */

/*
 * a macro to approximate "divide by 1000" that gives 0.000999,
 * if a value has enough effective digits.
 * (on pentium, mul takes 9 cycles but div takes 46!)
 */
#define	NSEC_TO_USEC(t)	(((t) >> 10) + ((t) >> 16) + ((t) >> 17))
void
rmc_update_class_util(struct rm_ifdat *ifd)
{
	int		 idle, avgidle, pktlen;
	int		 pkt_time, tidle;
	rm_class_t	*cl, *borrowed;
	rm_class_t	*borrows;
	struct timeval	*nowp;

	/*
	 * Get the most recent completed class.
	 */
	if ((cl = ifd->class_[ifd->qo_]) == NULL)
		return;

	pktlen = ifd->curlen_[ifd->qo_];
	borrowed = ifd->borrowed_[ifd->qo_];
	borrows = borrowed;

	PKTCNTR_ADD(&cl->stats_.xmit_cnt, pktlen);

	/*
	 * Run estimator on class and its ancestors.
	 */
	/*
	 * rm_update_class_util is designed to be called when the
	 * transfer is completed from a xmit complete interrupt,
	 * but most drivers don't implement an upcall for that.
	 * so, just use estimated completion time.
	 * as a result, ifd->qi_ and ifd->qo_ are always synced.
	 */
	nowp = &ifd->now_[ifd->qo_];
	/* get pkt_time (for link) in usec */
#if 1  /* use approximation */
	pkt_time = ifd->curlen_[ifd->qo_] * ifd->ns_per_byte_;
	pkt_time = NSEC_TO_USEC(pkt_time);
#else
	pkt_time = ifd->curlen_[ifd->qo_] * ifd->ns_per_byte_ / 1000;
#endif
#if 1 /* ALTQ4PPP */
	if (TV_LT(nowp, &ifd->ifnow_)) {
		int iftime;

		/*
		 * make sure the estimated completion time does not go
		 * too far.  it can happen when the link layer supports
		 * data compression or the interface speed is set to
		 * a much lower value.
		 */
		TV_DELTA(&ifd->ifnow_, nowp, iftime);
		if (iftime+pkt_time < ifd->maxiftime_) {
			TV_ADD_DELTA(&ifd->ifnow_, pkt_time, &ifd->ifnow_);
		} else {
			TV_ADD_DELTA(nowp, ifd->maxiftime_, &ifd->ifnow_);
		}
	} else {
		TV_ADD_DELTA(nowp, pkt_time, &ifd->ifnow_);
	}
#else
	if (TV_LT(nowp, &ifd->ifnow_)) {
		TV_ADD_DELTA(&ifd->ifnow_, pkt_time, &ifd->ifnow_);
	} else {
		TV_ADD_DELTA(nowp, pkt_time, &ifd->ifnow_);
	}
#endif

	while (cl != NULL) {
		TV_DELTA(&ifd->ifnow_, &cl->last_, idle);
		if (idle >= 2000000)
			/*
			 * this class is idle enough, reset avgidle.
			 * (TV_DELTA returns 2000000 us when delta is large.)
			 */
			cl->avgidle_ = cl->maxidle_;

		/* get pkt_time (for class) in usec */
#if 1  /* use approximation */
		pkt_time = pktlen * cl->ns_per_byte_;
		pkt_time = NSEC_TO_USEC(pkt_time);
#else
		pkt_time = pktlen * cl->ns_per_byte_ / 1000;
#endif
		idle -= pkt_time;

		avgidle = cl->avgidle_;
		avgidle += idle - (avgidle >> RM_FILTER_GAIN);
		cl->avgidle_ = avgidle;

		/* Are we overlimit ? */
		if (avgidle <= 0) {
			CBQTRACE(rmc_update_class_util, 'milo', cl->stats_.handle);
#if 1 /* ALTQ */
			/*
			 * need some lower bound for avgidle, otherwise
			 * a borrowing class gets unbounded penalty.
			 */
			if (avgidle < cl->minidle_)
				avgidle = cl->avgidle_ = cl->minidle_;
#endif
			/* set next idle to make avgidle 0 */
			tidle = pkt_time +
				(((1 - RM_POWER) * avgidle) >> RM_FILTER_GAIN);
			TV_ADD_DELTA(nowp, tidle, &cl->undertime_);
			++cl->stats_.over;
		} else {
			cl->avgidle_ =
			    (avgidle > cl->maxidle_) ? cl->maxidle_ : avgidle;
			cl->undertime_.tv_sec = 0;
			if (cl->sleeping_) {
				CALLOUT_STOP(&cl->callout_);
				cl->sleeping_ = 0;
			}
		}

		if (borrows != NULL) {
			if (borrows != cl)
				++cl->stats_.borrows;
			else
				borrows = NULL;
		}
		cl->last_ = ifd->ifnow_;
		cl->last_pkttime_ = pkt_time;

#if 1
		if (cl->parent_ == NULL) {
			/* take stats of root class */
			PKTCNTR_ADD(&cl->stats_.xmit_cnt, pktlen);
		}
#endif

		cl = cl->parent_;
	}

	/*
	 * Check to see if cutoff needs to set to a new level.
	 */
	cl = ifd->class_[ifd->qo_];
	if (borrowed && (ifd->cutoff_ >= borrowed->depth_)) {
#if 1 /* ALTQ */
		if ((qlen(cl->q_) <= 0) || TV_LT(nowp, &borrowed->undertime_)) {
			rmc_tl_satisfied(ifd, nowp);
			CBQTRACE(rmc_update_class_util, 'broe', ifd->cutoff_);
		} else {
			ifd->cutoff_ = borrowed->depth_;
			CBQTRACE(rmc_update_class_util, 'ffob', borrowed->depth_);
		}
#else /* !ALTQ */
		if ((qlen(cl->q_) <= 1) || TV_LT(&now, &borrowed->undertime_)) {
			reset_cutoff(ifd);
#ifdef notdef
			rmc_tl_satisfied(ifd, &now);
#endif
			CBQTRACE(rmc_update_class_util, 'broe', ifd->cutoff_);
		} else {
			ifd->cutoff_ = borrowed->depth_;
			CBQTRACE(rmc_update_class_util, 'ffob', borrowed->depth_);
		}
#endif /* !ALTQ */
	}

	/*
	 * Release class slot
	 */
	ifd->borrowed_[ifd->qo_] = NULL;
	ifd->class_[ifd->qo_] = NULL;
	ifd->qo_ = (ifd->qo_ + 1) % ifd->maxqueued_;
	ifd->queued_--;
}

/*
 * void
 * rmc_drop_action(struct rm_class *cl) - Generic (not protocol-specific)
 *	over-limit action routines.  These get invoked by rmc_under_limit()
 *	if a class with packets to send if over its bandwidth limit & can't
 *	borrow from a parent class.
 *
 *	Returns: NONE
 */

static void
rmc_drop_action(struct rm_class *cl)
{
	struct rm_ifdat	*ifd = cl->ifdat_;

	ASSERT(qlen(cl->q_) > 0);
	_rmc_dropq(cl);
	if (qempty(cl->q_))
		ifd->na_[cl->pri_]--;
}

void rmc_dropall(struct rm_class *cl)
{
	struct rm_ifdat	*ifd = cl->ifdat_;

	if (!qempty(cl->q_)) {
		_flushq(cl->q_);

		ifd->na_[cl->pri_]--;
	}
}

#if (__FreeBSD_version > 300000)
/* hzto() is removed from FreeBSD-3.0 */
static int hzto(struct timeval *);

static int
hzto(tv)
	struct timeval *tv;
{
	struct timeval t2;

	getmicrotime(&t2);
	t2.tv_sec = tv->tv_sec - t2.tv_sec;
	t2.tv_usec = tv->tv_usec - t2.tv_usec;
	return (tvtohz(&t2));
}
#endif /* __FreeBSD_version > 300000 */

/*
 * void
 * rmc_delay_action(struct rm_class *cl) - This function is the generic CBQ
 *	delay action routine.  It is invoked via rmc_under_limit when the
 *	packet is discoverd to be overlimit.
 *
 *	If the delay action is result of borrow class being overlimit, then
 *	delay for the offtime of the borrowing class that is overlimit.
 *
 *	Returns: NONE
 */

void
rmc_delay_action(struct rm_class *cl, struct rm_class *borrow)
{
	int	delay, t, extradelay;

	cl->stats_.overactions++;
	TV_DELTA(&cl->undertime_, &cl->overtime_, delay);
#ifndef BORROW_OFFTIME
	delay += cl->offtime_;
#endif

	if (!cl->sleeping_) {
		CBQTRACE(rmc_delay_action, 'yled', cl->stats_.handle);
#ifdef BORROW_OFFTIME
		if (borrow != NULL)
			extradelay = borrow->offtime_;
		else
#endif
			extradelay = cl->offtime_;

#ifdef ALTQ
		/*
		 * XXX recalculate suspend time:
		 * current undertime is (tidle + pkt_time) calculated
		 * from the last transmission.
		 *	tidle: time required to bring avgidle back to 0
		 *	pkt_time: target waiting time for this class
		 * we need to replace pkt_time by offtime
		 */
		extradelay -= cl->last_pkttime_;
#endif
		if (extradelay > 0) {
			TV_ADD_DELTA(&cl->undertime_, extradelay, &cl->undertime_);
			delay += extradelay;
		}

		cl->sleeping_ = 1;
		cl->stats_.delays++;

		/*
		 * Since packets are phased randomly with respect to the
		 * clock, 1 tick (the next clock tick) can be an arbitrarily
		 * short time so we have to wait for at least two ticks.
		 * NOTE:  If there's no other traffic, we need the timer as
		 * a 'backstop' to restart this class.
		 */
		if (delay > tick * 2) {
			/* FreeBSD rounds up the tick */
			t = hzto(&cl->undertime_);
		} else
			t = 2;
		CALLOUT_RESET(&cl->callout_, t,
			      (timeout_t *)rmc_restart, (caddr_t)cl);
	}
}

/*
 * void
 * rmc_restart() - is just a helper routine for rmc_delay_action -- it is
 *	called by the system timer code & is responsible checking if the
 *	class is still sleeping (it might have been restarted as a side
 *	effect of the queue scan on a packet arrival) and, if so, restarting
 *	output for the class.  Inspecting the class state & restarting output
 *	require locking the class structure.  In general the driver is
 *	responsible for locking but this is the only routine that is not
 *	called directly or indirectly from the interface driver so it has
 *	know about system locking conventions.  Under bsd, locking is done
 *	by raising IPL to splimp so that's what's implemented here.  On a
 *	different system this would probably need to be changed.
 *
 *	Returns:	NONE
 */

static void
rmc_restart(struct rm_class *cl)
{
	struct rm_ifdat	*ifd = cl->ifdat_;
	int		 s;

	s = splnet();
	IFQ_LOCK(ifd->ifq_);
	if (cl->sleeping_) {
		cl->sleeping_ = 0;
		cl->undertime_.tv_sec = 0;

		if (ifd->queued_ < ifd->maxqueued_ && ifd->restart != NULL) {
			CBQTRACE(rmc_restart, 'trts', cl->stats_.handle);
			(ifd->restart)(ifd->ifq_);
		}
	}
	IFQ_UNLOCK(ifd->ifq_);
	splx(s);
}

/*
 * void
 * rmc_root_overlimit(struct rm_class *cl) - This the generic overlimit
 *	handling routine for the root class of the link sharing structure.
 *
 *	Returns: NONE
 */

static void
rmc_root_overlimit(struct rm_class *cl, struct rm_class *borrow)
{
    panic("rmc_root_overlimit");
}

/*
 * Packet Queue handling routines.  Eventually, this is to localize the
 *	effects on the code whether queues are red queues or droptail
 *	queues.
 */

static int
_rmc_addq(rm_class_t *cl, mbuf_t *m)
{
#ifdef ALTQ_RIO
	if (q_is_rio(cl->q_))
		return rio_addq((rio_t *)cl->red_, cl->q_, m, cl->pktattr_);
#endif
#ifdef ALTQ_RED
	if (q_is_red(cl->q_))
		return red_addq(cl->red_, cl->q_, m, cl->pktattr_);
#endif /* ALTQ_RED */
#ifdef ALTQ_CODEL
	if (q_is_codel(cl->q_))
		return codel_addq(cl->codel_, cl->q_, m);
#endif

	if (cl->flags_ & RMCF_CLEARDSCP)
		write_dsfield(m, cl->pktattr_, 0);

	_addq(cl->q_, m);
	return (0);
}

/* note: _rmc_dropq is not called for red */
static void
_rmc_dropq(rm_class_t *cl)
{
	mbuf_t	*m;

	if ((m = _getq(cl->q_)) != NULL)
		m_freem(m);
}

static mbuf_t *
_rmc_getq(rm_class_t *cl)
{
#ifdef ALTQ_RIO
	if (q_is_rio(cl->q_))
		return rio_getq((rio_t *)cl->red_, cl->q_);
#endif
#ifdef ALTQ_RED
	if (q_is_red(cl->q_))
		return red_getq(cl->red_, cl->q_);
#endif
#ifdef ALTQ_CODEL
	if (q_is_codel(cl->q_))
		return codel_getq(cl->codel_, cl->q_);
#endif
	return _getq(cl->q_);
}

static mbuf_t *
_rmc_pollq(rm_class_t *cl)
{
	return qhead(cl->q_);
}

#ifdef CBQ_TRACE

struct cbqtrace		 cbqtrace_buffer[NCBQTRACE+1];
struct cbqtrace		*cbqtrace_ptr = NULL;
int			 cbqtrace_count;

/*
 * DDB hook to trace cbq events:
 *  the last 1024 events are held in a circular buffer.
 *  use "call cbqtrace_dump(N)" to display 20 events from Nth event.
 */
void cbqtrace_dump(int);
static char *rmc_funcname(void *);

static struct rmc_funcs {
	void	*func;
	char	*name;
} rmc_funcs[] =
{
	rmc_init,		"rmc_init",
	rmc_queue_packet,	"rmc_queue_packet",
	rmc_under_limit,	"rmc_under_limit",
	rmc_update_class_util,	"rmc_update_class_util",
	rmc_delay_action,	"rmc_delay_action",
	rmc_restart,		"rmc_restart",
	_rmc_wrr_dequeue_next,	"_rmc_wrr_dequeue_next",
	NULL,			NULL
};

static char *rmc_funcname(void *func)
{
	struct rmc_funcs *fp;

	for (fp = rmc_funcs; fp->func != NULL; fp++)
		if (fp->func == func)
			return (fp->name);
	return ("unknown");
}

void cbqtrace_dump(int counter)
{
	int	 i, *p;
	char	*cp;

	counter = counter % NCBQTRACE;
	p = (int *)&cbqtrace_buffer[counter];

	for (i=0; i<20; i++) {
		printf("[0x%x] ", *p++);
		printf("%s: ", rmc_funcname((void *)*p++));
		cp = (char *)p++;
		printf("%c%c%c%c: ", cp[0], cp[1], cp[2], cp[3]);
		printf("%d\n",*p++);

		if (p >= (int *)&cbqtrace_buffer[NCBQTRACE])
			p = (int *)cbqtrace_buffer;
	}
}
#endif /* CBQ_TRACE */
#endif /* ALTQ_CBQ */

#if defined(ALTQ_CBQ) || defined(ALTQ_RED) || defined(ALTQ_RIO) || \
    defined(ALTQ_HFSC) || defined(ALTQ_PRIQ) || defined(ALTQ_CODEL)
#if !defined(__GNUC__) || defined(ALTQ_DEBUG)

void
_addq(class_queue_t *q, mbuf_t *m)
{
        mbuf_t	*m0;

	if ((m0 = qtail(q)) != NULL)
		m->m_nextpkt = m0->m_nextpkt;
	else
		m0 = m;
	m0->m_nextpkt = m;
	qtail(q) = m;
	qlen(q)++;
}

mbuf_t *
_getq(class_queue_t *q)
{
	mbuf_t	*m, *m0;

	if ((m = qtail(q)) == NULL)
		return (NULL);
	if ((m0 = m->m_nextpkt) != m)
		m->m_nextpkt = m0->m_nextpkt;
	else {
		ASSERT(qlen(q) == 1);
		qtail(q) = NULL;
	}
	qlen(q)--;
	m0->m_nextpkt = NULL;
	return (m0);
}

/* drop a packet at the tail of the queue */
mbuf_t *
_getq_tail(class_queue_t *q)
{
	mbuf_t	*m, *m0, *prev;

	if ((m = m0 = qtail(q)) == NULL)
		return NULL;
	do {
		prev = m0;
		m0 = m0->m_nextpkt;
	} while (m0 != m);
	prev->m_nextpkt = m->m_nextpkt;
	if (prev == m)  {
		ASSERT(qlen(q) == 1);
		qtail(q) = NULL;
	} else
		qtail(q) = prev;
	qlen(q)--;
	m->m_nextpkt = NULL;
	return (m);
}

/* randomly select a packet in the queue */
mbuf_t *
_getq_random(class_queue_t *q)
{
	struct mbuf	*m;
	int		 i, n;

	if ((m = qtail(q)) == NULL)
		return NULL;
	if (m->m_nextpkt == m) {
		ASSERT(qlen(q) == 1);
		qtail(q) = NULL;
	} else {
		struct mbuf *prev = NULL;

		n = arc4random() % qlen(q) + 1;
		for (i = 0; i < n; i++) {
			prev = m;
			m = m->m_nextpkt;
		}
		prev->m_nextpkt = m->m_nextpkt;
		if (m == qtail(q))
			qtail(q) = prev;
	}
	qlen(q)--;
	m->m_nextpkt = NULL;
	return (m);
}

void
_removeq(class_queue_t *q, mbuf_t *m)
{
	mbuf_t	*m0, *prev;

	m0 = qtail(q);
	do {
		prev = m0;
		m0 = m0->m_nextpkt;
	} while (m0 != m);
	prev->m_nextpkt = m->m_nextpkt;
	if (prev == m)
		qtail(q) = NULL;
	else if (qtail(q) == m)
		qtail(q) = prev;
	qlen(q)--;
}

void
_flushq(class_queue_t *q)
{
	mbuf_t *m;

	while ((m = _getq(q)) != NULL)
		m_freem(m);
	ASSERT(qlen(q) == 0);
}

#endif /* !__GNUC__ || ALTQ_DEBUG */
#endif /* ALTQ_CBQ || ALTQ_RED || ALTQ_RIO || ALTQ_HFSC || ALTQ_PRIQ */
