/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2009-2010
 *	Swinburne University of Technology, Melbourne, Australia
 * Copyright (c) 2010-2011 The FreeBSD Foundation
 * All rights reserved.
 *
 * This software was developed at the Centre for Advanced Internet
 * Architectures, Swinburne University of Technology, by David Hayes and
 * Lawrence Stewart, made possible in part by a grant from the Cisco University
 * Research Program Fund at Community Foundation Silicon Valley.
 *
 * Portions of this software were developed at the Centre for Advanced Internet
 * Architectures, Swinburne University of Technology, Melbourne, Australia by
 * David Hayes under sponsorship from the FreeBSD Foundation.
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
 * ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

/*
 * An implementation of the CAIA-Hamilton delay based congestion control
 * algorithm, based on "Improved coexistence and loss tolerance for delay based
 * TCP congestion control" by D. A. Hayes and G. Armitage., in 35th Annual IEEE
 * Conference on Local Computer Networks (LCN 2010), Denver, Colorado, USA,
 * 11-14 October 2010.
 *
 * Originally released as part of the NewTCP research project at Swinburne
 * University of Technology's Centre for Advanced Internet Architectures,
 * Melbourne, Australia, which was made possible in part by a grant from the
 * Cisco University Research Program Fund at Community Foundation Silicon
 * Valley. More details are available at:
 *   http://caia.swin.edu.au/urp/newtcp/
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/kernel.h>
#include <sys/khelp.h>
#include <sys/limits.h>
#include <sys/malloc.h>
#include <sys/module.h>
#include <sys/queue.h>
#include <sys/socket.h>
#include <sys/socketvar.h>
#include <sys/sysctl.h>
#include <sys/systm.h>

#include <net/vnet.h>

#include <netinet/tcp.h>
#include <netinet/tcp_seq.h>
#include <netinet/tcp_timer.h>
#include <netinet/tcp_var.h>
#include <netinet/cc/cc.h>
#include <netinet/cc/cc_module.h>

#include <netinet/khelp/h_ertt.h>

/*
 * Private signal type for rate based congestion signal.
 * See <netinet/cc.h> for appropriate bit-range to use for private signals.
 */
#define	CC_CHD_DELAY	0x02000000

/* Largest possible number returned by random(). */
#define	RANDOM_MAX	INT_MAX

static void	chd_ack_received(struct cc_var *ccv, uint16_t ack_type);
static void	chd_cb_destroy(struct cc_var *ccv);
static int	chd_cb_init(struct cc_var *ccv);
static void	chd_cong_signal(struct cc_var *ccv, uint32_t signal_type);
static void	chd_conn_init(struct cc_var *ccv);
static int	chd_mod_init(void);

struct chd {
	/*
	 * Shadow window - keeps track of what the NewReno congestion window
	 * would have been if delay-based cwnd backoffs had not been made. This
	 * functionality aids coexistence with loss-based TCP flows which may be
	 * sharing links along the path.
	 */
	unsigned long shadow_w;
	/*
	 * Loss-based TCP compatibility flag - When set, it turns on the shadow
	 * window functionality.
	 */
	int loss_compete;
	 /* The maximum round trip time seen within a measured rtt period. */
	int maxrtt_in_rtt;
	/* The previous qdly that caused cwnd to backoff. */
	int prev_backoff_qdly;
};

static int ertt_id;

VNET_DEFINE_STATIC(uint32_t, chd_qmin) = 5;
VNET_DEFINE_STATIC(uint32_t, chd_pmax) = 50;
VNET_DEFINE_STATIC(uint32_t, chd_loss_fair) = 1;
VNET_DEFINE_STATIC(uint32_t, chd_use_max) = 1;
VNET_DEFINE_STATIC(uint32_t, chd_qthresh) = 20;
#define	V_chd_qthresh	VNET(chd_qthresh)
#define	V_chd_qmin	VNET(chd_qmin)
#define	V_chd_pmax	VNET(chd_pmax)
#define	V_chd_loss_fair	VNET(chd_loss_fair)
#define	V_chd_use_max	VNET(chd_use_max)

static MALLOC_DEFINE(M_CHD, "chd data",
    "Per connection data required for the CHD congestion control algorithm");

struct cc_algo chd_cc_algo = {
	.name = "chd",
	.ack_received = chd_ack_received,
	.cb_destroy = chd_cb_destroy,
	.cb_init = chd_cb_init,
	.cong_signal = chd_cong_signal,
	.conn_init = chd_conn_init,
	.mod_init = chd_mod_init
};

static __inline void
chd_window_decrease(struct cc_var *ccv)
{
	unsigned long win;

	win = min(CCV(ccv, snd_wnd), CCV(ccv, snd_cwnd)) / CCV(ccv, t_maxseg);
	win -= max((win / 2), 1);
	CCV(ccv, snd_ssthresh) = max(win, 2) * CCV(ccv, t_maxseg);
}

/*
 * Probabilistic backoff function. Returns 1 if we should backoff or 0
 * otherwise. The calculation of p is similar to the calculation of p in cc_hd.
 */
static __inline int
should_backoff(int qdly, int maxqdly, struct chd *chd_data)
{
	unsigned long p, rand;

	rand = random();

	if (qdly < V_chd_qthresh) {
		chd_data->loss_compete = 0;
		p = (((RANDOM_MAX / 100) * V_chd_pmax) /
		    (V_chd_qthresh - V_chd_qmin)) *
		    (qdly - V_chd_qmin);
	} else {
		if (qdly > V_chd_qthresh) {
			p = (((RANDOM_MAX / 100) * V_chd_pmax) /
			    (maxqdly - V_chd_qthresh)) *
			    (maxqdly - qdly);
			if (V_chd_loss_fair && rand < p)
				chd_data->loss_compete = 1;
		} else {
			p = (RANDOM_MAX / 100) * V_chd_pmax;
			chd_data->loss_compete = 0;
		}
	}

	return (rand < p);
}

static __inline void
chd_window_increase(struct cc_var *ccv, int new_measurement)
{
	struct chd *chd_data;
	int incr;

	chd_data = ccv->cc_data;
	incr = 0;

	if (CCV(ccv, snd_cwnd) <= CCV(ccv, snd_ssthresh)) {
		/* Adapted from NewReno slow start. */
		if (V_tcp_do_rfc3465) {
			/* In slow-start with ABC enabled. */
			if (CCV(ccv, snd_nxt) == CCV(ccv, snd_max)) {
				/* Not due to RTO. */
				incr = min(ccv->bytes_this_ack,
				    V_tcp_abc_l_var * CCV(ccv, t_maxseg));
			} else {
				/* Due to RTO. */
				incr = min(ccv->bytes_this_ack,
				    CCV(ccv, t_maxseg));
			}
		} else
			incr = CCV(ccv, t_maxseg);

	} else { /* Congestion avoidance. */
		if (V_tcp_do_rfc3465) {
			if (ccv->flags & CCF_ABC_SENTAWND) {
				ccv->flags &= ~CCF_ABC_SENTAWND;
				incr = CCV(ccv, t_maxseg);
			}
		} else if (new_measurement)
			incr = CCV(ccv, t_maxseg);
	}

	if (chd_data->shadow_w > 0) {
		/* Track NewReno window. */
		chd_data->shadow_w = min(chd_data->shadow_w + incr,
		    TCP_MAXWIN << CCV(ccv, snd_scale));
	}

	CCV(ccv,snd_cwnd) = min(CCV(ccv, snd_cwnd) + incr,
	    TCP_MAXWIN << CCV(ccv, snd_scale));
}

/*
 * All ACK signals are used for timing measurements to determine delay-based
 * congestion. However, window increases are only performed when
 * ack_type == CC_ACK.
 */
static void
chd_ack_received(struct cc_var *ccv, uint16_t ack_type)
{
	struct chd *chd_data;
	struct ertt *e_t;
	int backoff, new_measurement, qdly, rtt;

	e_t = khelp_get_osd(CCV(ccv, osd), ertt_id);
	chd_data = ccv->cc_data;
	new_measurement = e_t->flags & ERTT_NEW_MEASUREMENT;
	backoff = qdly = 0;

	chd_data->maxrtt_in_rtt = imax(e_t->rtt, chd_data->maxrtt_in_rtt);

	if (new_measurement) {
		/*
		 * There is a new per RTT measurement, so check to see if there
		 * is delay based congestion.
		 */
		rtt = V_chd_use_max ? chd_data->maxrtt_in_rtt : e_t->rtt;
		chd_data->maxrtt_in_rtt = 0;

		if (rtt && e_t->minrtt && !IN_RECOVERY(CCV(ccv, t_flags))) {
			qdly = rtt - e_t->minrtt;
			if (qdly > V_chd_qmin) {
				/*
				 * Probabilistic delay based congestion
				 * indication.
				 */
				backoff = should_backoff(qdly,
				    e_t->maxrtt - e_t->minrtt, chd_data);
			} else
				chd_data->loss_compete = 0;
		}
		/* Reset per RTT measurement flag to start a new measurement. */
		e_t->flags &= ~ERTT_NEW_MEASUREMENT;
	}

	if (backoff) {
		/*
		 * Update shadow_w before delay based backoff.
		 */
		if (chd_data->loss_compete ||
		    qdly > chd_data->prev_backoff_qdly) {
			/*
			 * Delay is higher than when we backed off previously,
			 * so it is possible that this flow is competing with
			 * loss based flows.
			 */
			chd_data->shadow_w = max(CCV(ccv, snd_cwnd),
			    chd_data->shadow_w);
		} else {
			/*
			 * Reset shadow_w, as it is probable that this flow is
			 * not competing with loss based flows at the moment.
			 */
			chd_data->shadow_w = 0;
		}

		chd_data->prev_backoff_qdly = qdly;
		/*
		 * Send delay-based congestion signal to the congestion signal
		 * handler.
		 */
		chd_cong_signal(ccv, CC_CHD_DELAY);

	} else if (ack_type == CC_ACK)
		chd_window_increase(ccv, new_measurement);
}

static void
chd_cb_destroy(struct cc_var *ccv)
{

	free(ccv->cc_data, M_CHD);
}

static int
chd_cb_init(struct cc_var *ccv)
{
	struct chd *chd_data;

	chd_data = malloc(sizeof(struct chd), M_CHD, M_NOWAIT);
	if (chd_data == NULL)
		return (ENOMEM);

	chd_data->shadow_w = 0;
	ccv->cc_data = chd_data;

	return (0);
}

static void
chd_cong_signal(struct cc_var *ccv, uint32_t signal_type)
{
	struct ertt *e_t;
	struct chd *chd_data;
	int qdly;

	e_t = khelp_get_osd(CCV(ccv, osd), ertt_id);
	chd_data = ccv->cc_data;
	qdly = imax(e_t->rtt, chd_data->maxrtt_in_rtt) - e_t->minrtt;

	switch(signal_type) {
	case CC_CHD_DELAY:
		chd_window_decrease(ccv); /* Set new ssthresh. */
		CCV(ccv, snd_cwnd) = CCV(ccv, snd_ssthresh);
		CCV(ccv, snd_recover) = CCV(ccv, snd_max);
		ENTER_CONGRECOVERY(CCV(ccv, t_flags));
		break;

	case CC_NDUPACK: /* Packet loss. */
		/*
		 * Only react to loss as a congestion signal if qdly >
		 * V_chd_qthresh.  If qdly is less than qthresh, presume that
		 * this is a non congestion related loss. If qdly is greater
		 * than qthresh, assume that we are competing with loss based
		 * tcp flows and restore window from any unnecessary backoffs,
		 * before the decrease.
		 */
		if (!IN_RECOVERY(CCV(ccv, t_flags)) && qdly > V_chd_qthresh) {
			if (chd_data->loss_compete) {
				CCV(ccv, snd_cwnd) = max(CCV(ccv, snd_cwnd),
				    chd_data->shadow_w);
			}
			chd_window_decrease(ccv);
		} else {
			 /*
			  * This loss isn't congestion related, or already
			  * recovering from congestion.
			  */
			CCV(ccv, snd_ssthresh) = CCV(ccv, snd_cwnd);
			CCV(ccv, snd_recover) = CCV(ccv, snd_max);
		}

		if (chd_data->shadow_w > 0) {
			chd_data->shadow_w = max(chd_data->shadow_w /
			    CCV(ccv, t_maxseg) / 2, 2) * CCV(ccv, t_maxseg);
		}
		ENTER_FASTRECOVERY(CCV(ccv, t_flags));
		break;

	default:
		newreno_cc_algo.cong_signal(ccv, signal_type);
	}
}

static void
chd_conn_init(struct cc_var *ccv)
{
	struct chd *chd_data;

	chd_data = ccv->cc_data;
	chd_data->prev_backoff_qdly = 0;
	chd_data->maxrtt_in_rtt = 0;
	chd_data->loss_compete = 0;
	/*
	 * Initialise the shadow_cwnd to be equal to snd_cwnd in case we are
	 * competing with loss based flows from the start.
	 */
	chd_data->shadow_w = CCV(ccv, snd_cwnd);
}

static int
chd_mod_init(void)
{

	ertt_id = khelp_get_id("ertt");
	if (ertt_id <= 0) {
		printf("%s: h_ertt module not found\n", __func__);
		return (ENOENT);
	}

	chd_cc_algo.after_idle = newreno_cc_algo.after_idle;
	chd_cc_algo.post_recovery = newreno_cc_algo.post_recovery;

	return (0);
}

static int
chd_loss_fair_handler(SYSCTL_HANDLER_ARGS)
{
	int error;
	uint32_t new;

	new = V_chd_loss_fair;
	error = sysctl_handle_int(oidp, &new, 0, req);
	if (error == 0 && req->newptr != NULL) {
		if (new > 1)
			error = EINVAL;
		else
			V_chd_loss_fair = new;
	}

	return (error);
}

static int
chd_pmax_handler(SYSCTL_HANDLER_ARGS)
{
	int error;
	uint32_t new;

	new = V_chd_pmax;
	error = sysctl_handle_int(oidp, &new, 0, req);
	if (error == 0 && req->newptr != NULL) {
		if (new == 0 || new > 100)
			error = EINVAL;
		else
			V_chd_pmax = new;
	}

	return (error);
}

static int
chd_qthresh_handler(SYSCTL_HANDLER_ARGS)
{
	int error;
	uint32_t new;

	new = V_chd_qthresh;
	error = sysctl_handle_int(oidp, &new, 0, req);
	if (error == 0 && req->newptr != NULL) {
		if (new <= V_chd_qmin)
			error = EINVAL;
		else
			V_chd_qthresh = new;
	}

	return (error);
}

SYSCTL_DECL(_net_inet_tcp_cc_chd);
SYSCTL_NODE(_net_inet_tcp_cc, OID_AUTO, chd, CTLFLAG_RW, NULL,
    "CAIA Hamilton delay-based congestion control related settings");

SYSCTL_PROC(_net_inet_tcp_cc_chd, OID_AUTO, loss_fair,
    CTLFLAG_VNET | CTLTYPE_UINT | CTLFLAG_RW,
    &VNET_NAME(chd_loss_fair), 1, &chd_loss_fair_handler,
    "IU", "Flag to enable shadow window functionality.");

SYSCTL_PROC(_net_inet_tcp_cc_chd, OID_AUTO, pmax,
    CTLFLAG_VNET | CTLTYPE_UINT | CTLFLAG_RW,
    &VNET_NAME(chd_pmax), 5, &chd_pmax_handler,
    "IU", "Per RTT maximum backoff probability as a percentage");

SYSCTL_PROC(_net_inet_tcp_cc_chd, OID_AUTO, queue_threshold,
    CTLFLAG_VNET | CTLTYPE_UINT | CTLFLAG_RW,
    &VNET_NAME(chd_qthresh), 20, &chd_qthresh_handler,
    "IU", "Queueing congestion threshold in ticks");

SYSCTL_UINT(_net_inet_tcp_cc_chd, OID_AUTO, queue_min,
    CTLFLAG_VNET | CTLFLAG_RW, &VNET_NAME(chd_qmin), 5,
    "Minimum queueing delay threshold in ticks");

SYSCTL_UINT(_net_inet_tcp_cc_chd,  OID_AUTO, use_max,
    CTLFLAG_VNET | CTLFLAG_RW, &VNET_NAME(chd_use_max), 1,
    "Use the maximum RTT seen within the measurement period (RTT) "
    "as the basic delay measurement for the algorithm.");

DECLARE_CC_MODULE(chd, &chd_cc_algo);
MODULE_DEPEND(chd, ertt, 1, 1, 1);
