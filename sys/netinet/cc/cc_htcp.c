/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2007-2008
 * 	Swinburne University of Technology, Melbourne, Australia
 * Copyright (c) 2009-2010 Lawrence Stewart <lstewart@freebsd.org>
 * Copyright (c) 2010 The FreeBSD Foundation
 * All rights reserved.
 *
 * This software was developed at the Centre for Advanced Internet
 * Architectures, Swinburne University of Technology, by Lawrence Stewart and
 * James Healy, made possible in part by a grant from the Cisco University
 * Research Program Fund at Community Foundation Silicon Valley.
 *
 * Portions of this software were developed at the Centre for Advanced
 * Internet Architectures, Swinburne University of Technology, Melbourne,
 * Australia by David Hayes under sponsorship from the FreeBSD Foundation.
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
 * An implementation of the H-TCP congestion control algorithm for FreeBSD,
 * based on the Internet Draft "draft-leith-tcp-htcp-06.txt" by Leith and
 * Shorten. Originally released as part of the NewTCP research project at
 * Swinburne University of Technology's Centre for Advanced Internet
 * Architectures, Melbourne, Australia, which was made possible in part by a
 * grant from the Cisco University Research Program Fund at Community Foundation
 * Silicon Valley. More details are available at:
 *   http://caia.swin.edu.au/urp/newtcp/
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/kernel.h>
#include <sys/limits.h>
#include <sys/malloc.h>
#include <sys/module.h>
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

/* Fixed point math shifts. */
#define HTCP_SHIFT 8
#define HTCP_ALPHA_INC_SHIFT 4

#define HTCP_INIT_ALPHA 1
#define HTCP_DELTA_L hz		/* 1 sec in ticks. */
#define HTCP_MINBETA 128	/* 0.5 << HTCP_SHIFT. */
#define HTCP_MAXBETA 204	/* ~0.8 << HTCP_SHIFT. */
#define HTCP_MINROWE 26		/* ~0.1 << HTCP_SHIFT. */
#define HTCP_MAXROWE 512	/* 2 << HTCP_SHIFT. */

/* RTT_ref (ms) used in the calculation of alpha if RTT scaling is enabled. */
#define HTCP_RTT_REF 100

/* Don't trust SRTT until this many samples have been taken. */
#define HTCP_MIN_RTT_SAMPLES 8

/*
 * HTCP_CALC_ALPHA performs a fixed point math calculation to determine the
 * value of alpha, based on the function defined in the HTCP spec.
 *
 * i.e. 1 + 10(delta - delta_l) + ((delta - delta_l) / 2) ^ 2
 *
 * "diff" is passed in to the macro as "delta - delta_l" and is expected to be
 * in units of ticks.
 *
 * The joyousnous of fixed point maths means our function implementation looks a
 * little funky...
 *
 * In order to maintain some precision in the calculations, a fixed point shift
 * HTCP_ALPHA_INC_SHIFT is used to ensure the integer divisions don't
 * truncate the results too badly.
 *
 * The "16" value is the "1" term in the alpha function shifted up by
 * HTCP_ALPHA_INC_SHIFT
 *
 * The "160" value is the "10" multiplier in the alpha function multiplied by
 * 2^HTCP_ALPHA_INC_SHIFT
 *
 * Specifying these as constants reduces the computations required. After
 * up-shifting all the terms in the function and performing the required
 * calculations, we down-shift the final result by HTCP_ALPHA_INC_SHIFT to
 * ensure it is back in the correct range.
 *
 * The "hz" terms are required as kernels can be configured to run with
 * different tick timers, which we have to adjust for in the alpha calculation
 * (which originally was defined in terms of seconds).
 *
 * We also have to be careful to constrain the value of diff such that it won't
 * overflow whilst performing the calculation. The middle term i.e. (160 * diff)
 * / hz is the limiting factor in the calculation. We must constrain diff to be
 * less than the max size of an int divided by the constant 160 figure
 * i.e. diff < INT_MAX / 160
 *
 * NB: Changing HTCP_ALPHA_INC_SHIFT will require you to MANUALLY update the
 * constants used in this function!
 */
#define HTCP_CALC_ALPHA(diff) \
((\
	(16) + \
	((160 * (diff)) / hz) + \
	(((diff) / hz) * (((diff) << HTCP_ALPHA_INC_SHIFT) / (4 * hz))) \
) >> HTCP_ALPHA_INC_SHIFT)

static void	htcp_ack_received(struct cc_var *ccv, uint16_t type);
static void	htcp_cb_destroy(struct cc_var *ccv);
static int	htcp_cb_init(struct cc_var *ccv);
static void	htcp_cong_signal(struct cc_var *ccv, uint32_t type);
static int	htcp_mod_init(void);
static void	htcp_post_recovery(struct cc_var *ccv);
static void	htcp_recalc_alpha(struct cc_var *ccv);
static void	htcp_recalc_beta(struct cc_var *ccv);
static void	htcp_record_rtt(struct cc_var *ccv);
static void	htcp_ssthresh_update(struct cc_var *ccv);

struct htcp {
	/* cwnd before entering cong recovery. */
	unsigned long	prev_cwnd;
	/* cwnd additive increase parameter. */
	int		alpha;
	/* cwnd multiplicative decrease parameter. */
	int		beta;
	/* Largest rtt seen for the flow. */
	int		maxrtt;
	/* Shortest rtt seen for the flow. */
	int		minrtt;
	/* Time of last congestion event in ticks. */
	int		t_last_cong;
};

static int htcp_rtt_ref;
/*
 * The maximum number of ticks the value of diff can reach in
 * htcp_recalc_alpha() before alpha will stop increasing due to overflow.
 * See comment above HTCP_CALC_ALPHA for more info.
 */
static int htcp_max_diff = INT_MAX / ((1 << HTCP_ALPHA_INC_SHIFT) * 10);

/* Per-netstack vars. */
VNET_DEFINE_STATIC(u_int, htcp_adaptive_backoff) = 0;
VNET_DEFINE_STATIC(u_int, htcp_rtt_scaling) = 0;
#define	V_htcp_adaptive_backoff    VNET(htcp_adaptive_backoff)
#define	V_htcp_rtt_scaling    VNET(htcp_rtt_scaling)

static MALLOC_DEFINE(M_HTCP, "htcp data",
    "Per connection data required for the HTCP congestion control algorithm");

struct cc_algo htcp_cc_algo = {
	.name = "htcp",
	.ack_received = htcp_ack_received,
	.cb_destroy = htcp_cb_destroy,
	.cb_init = htcp_cb_init,
	.cong_signal = htcp_cong_signal,
	.mod_init = htcp_mod_init,
	.post_recovery = htcp_post_recovery,
};

static void
htcp_ack_received(struct cc_var *ccv, uint16_t type)
{
	struct htcp *htcp_data;

	htcp_data = ccv->cc_data;
	htcp_record_rtt(ccv);

	/*
	 * Regular ACK and we're not in cong/fast recovery and we're cwnd
	 * limited and we're either not doing ABC or are slow starting or are
	 * doing ABC and we've sent a cwnd's worth of bytes.
	 */
	if (type == CC_ACK && !IN_RECOVERY(CCV(ccv, t_flags)) &&
	    (ccv->flags & CCF_CWND_LIMITED) && (!V_tcp_do_rfc3465 ||
	    CCV(ccv, snd_cwnd) <= CCV(ccv, snd_ssthresh) ||
	    (V_tcp_do_rfc3465 && ccv->flags & CCF_ABC_SENTAWND))) {
		htcp_recalc_beta(ccv);
		htcp_recalc_alpha(ccv);
		/*
		 * Use the logic in NewReno ack_received() for slow start and
		 * for the first HTCP_DELTA_L ticks after either the flow starts
		 * or a congestion event (when alpha equals 1).
		 */
		if (htcp_data->alpha == 1 ||
		    CCV(ccv, snd_cwnd) <= CCV(ccv, snd_ssthresh))
			newreno_cc_algo.ack_received(ccv, type);
		else {
			if (V_tcp_do_rfc3465) {
				/* Increment cwnd by alpha segments. */
				CCV(ccv, snd_cwnd) += htcp_data->alpha *
				    CCV(ccv, t_maxseg);
				ccv->flags &= ~CCF_ABC_SENTAWND;
			} else
				/*
				 * Increment cwnd by alpha/cwnd segments to
				 * approximate an increase of alpha segments
				 * per RTT.
				 */
				CCV(ccv, snd_cwnd) += (((htcp_data->alpha <<
				    HTCP_SHIFT) / (CCV(ccv, snd_cwnd) /
				    CCV(ccv, t_maxseg))) * CCV(ccv, t_maxseg))
				    >> HTCP_SHIFT;
		}
	}
}

static void
htcp_cb_destroy(struct cc_var *ccv)
{
	free(ccv->cc_data, M_HTCP);
}

static int
htcp_cb_init(struct cc_var *ccv)
{
	struct htcp *htcp_data;

	htcp_data = malloc(sizeof(struct htcp), M_HTCP, M_NOWAIT);

	if (htcp_data == NULL)
		return (ENOMEM);

	/* Init some key variables with sensible defaults. */
	htcp_data->alpha = HTCP_INIT_ALPHA;
	htcp_data->beta = HTCP_MINBETA;
	htcp_data->maxrtt = TCPTV_SRTTBASE;
	htcp_data->minrtt = TCPTV_SRTTBASE;
	htcp_data->prev_cwnd = 0;
	htcp_data->t_last_cong = ticks;

	ccv->cc_data = htcp_data;

	return (0);
}

/*
 * Perform any necessary tasks before we enter congestion recovery.
 */
static void
htcp_cong_signal(struct cc_var *ccv, uint32_t type)
{
	struct htcp *htcp_data;

	htcp_data = ccv->cc_data;

	switch (type) {
	case CC_NDUPACK:
		if (!IN_FASTRECOVERY(CCV(ccv, t_flags))) {
			if (!IN_CONGRECOVERY(CCV(ccv, t_flags))) {
				/*
				 * Apply hysteresis to maxrtt to ensure
				 * reductions in the RTT are reflected in our
				 * measurements.
				 */
				htcp_data->maxrtt = (htcp_data->minrtt +
				    (htcp_data->maxrtt - htcp_data->minrtt) *
				    95) / 100;
				htcp_ssthresh_update(ccv);
				htcp_data->t_last_cong = ticks;
				htcp_data->prev_cwnd = CCV(ccv, snd_cwnd);
			}
			ENTER_RECOVERY(CCV(ccv, t_flags));
		}
		break;

	case CC_ECN:
		if (!IN_CONGRECOVERY(CCV(ccv, t_flags))) {
			/*
			 * Apply hysteresis to maxrtt to ensure reductions in
			 * the RTT are reflected in our measurements.
			 */
			htcp_data->maxrtt = (htcp_data->minrtt + (htcp_data->maxrtt -
			    htcp_data->minrtt) * 95) / 100;
			htcp_ssthresh_update(ccv);
			CCV(ccv, snd_cwnd) = CCV(ccv, snd_ssthresh);
			htcp_data->t_last_cong = ticks;
			htcp_data->prev_cwnd = CCV(ccv, snd_cwnd);
			ENTER_CONGRECOVERY(CCV(ccv, t_flags));
		}
		break;

	case CC_RTO:
		/*
		 * Grab the current time and record it so we know when the
		 * most recent congestion event was. Only record it when the
		 * timeout has fired more than once, as there is a reasonable
		 * chance the first one is a false alarm and may not indicate
		 * congestion.
		 */
		if (CCV(ccv, t_rxtshift) >= 2)
			htcp_data->t_last_cong = ticks;
		break;
	}
}

static int
htcp_mod_init(void)
{

	htcp_cc_algo.after_idle = newreno_cc_algo.after_idle;

	/*
	 * HTCP_RTT_REF is defined in ms, and t_srtt in the tcpcb is stored in
	 * units of TCP_RTT_SCALE*hz. Scale HTCP_RTT_REF to be in the same units
	 * as t_srtt.
	 */
	htcp_rtt_ref = (HTCP_RTT_REF * TCP_RTT_SCALE * hz) / 1000;

	return (0);
}

/*
 * Perform any necessary tasks before we exit congestion recovery.
 */
static void
htcp_post_recovery(struct cc_var *ccv)
{
	int pipe;
	struct htcp *htcp_data;

	pipe = 0;
	htcp_data = ccv->cc_data;

	if (IN_FASTRECOVERY(CCV(ccv, t_flags))) {
		/*
		 * If inflight data is less than ssthresh, set cwnd
		 * conservatively to avoid a burst of data, as suggested in the
		 * NewReno RFC. Otherwise, use the HTCP method.
		 *
		 * XXXLAS: Find a way to do this without needing curack
		 */
		if (V_tcp_do_rfc6675_pipe)
			pipe = tcp_compute_pipe(ccv->ccvc.tcp);
		else
			pipe = CCV(ccv, snd_max) - ccv->curack;
		
		if (pipe < CCV(ccv, snd_ssthresh))
			CCV(ccv, snd_cwnd) = pipe + CCV(ccv, t_maxseg);
		else
			CCV(ccv, snd_cwnd) = max(1, ((htcp_data->beta *
			    htcp_data->prev_cwnd / CCV(ccv, t_maxseg))
			    >> HTCP_SHIFT)) * CCV(ccv, t_maxseg);
	}
}

static void
htcp_recalc_alpha(struct cc_var *ccv)
{
	struct htcp *htcp_data;
	int alpha, diff, now;

	htcp_data = ccv->cc_data;
	now = ticks;

	/*
	 * If ticks has wrapped around (will happen approximately once every 49
	 * days on a machine with the default kern.hz=1000) and a flow straddles
	 * the wrap point, our alpha calcs will be completely wrong. We cut our
	 * losses and restart alpha from scratch by setting t_last_cong = now -
	 * HTCP_DELTA_L.
	 *
	 * This does not deflate our cwnd at all. It simply slows the rate cwnd
	 * is growing by until alpha regains the value it held prior to taking
	 * this drastic measure.
	 */
	if (now < htcp_data->t_last_cong)
		htcp_data->t_last_cong = now - HTCP_DELTA_L;

	diff = now - htcp_data->t_last_cong - HTCP_DELTA_L;

	/* Cap alpha if the value of diff would overflow HTCP_CALC_ALPHA(). */
	if (diff < htcp_max_diff) {
		/*
		 * If it has been more than HTCP_DELTA_L ticks since congestion,
		 * increase alpha according to the function defined in the spec.
		 */
		if (diff > 0) {
			alpha = HTCP_CALC_ALPHA(diff);

			/*
			 * Adaptive backoff fairness adjustment:
			 * 2 * (1 - beta) * alpha_raw
			 */
			if (V_htcp_adaptive_backoff)
				alpha = max(1, (2 * ((1 << HTCP_SHIFT) -
				    htcp_data->beta) * alpha) >> HTCP_SHIFT);

			/*
			 * RTT scaling: (RTT / RTT_ref) * alpha
			 * alpha will be the raw value from HTCP_CALC_ALPHA() if
			 * adaptive backoff is off, or the adjusted value if
			 * adaptive backoff is on.
			 */
			if (V_htcp_rtt_scaling)
				alpha = max(1, (min(max(HTCP_MINROWE,
				    (CCV(ccv, t_srtt) << HTCP_SHIFT) /
				    htcp_rtt_ref), HTCP_MAXROWE) * alpha)
				    >> HTCP_SHIFT);

		} else
			alpha = 1;

		htcp_data->alpha = alpha;
	}
}

static void
htcp_recalc_beta(struct cc_var *ccv)
{
	struct htcp *htcp_data;

	htcp_data = ccv->cc_data;

	/*
	 * TCPTV_SRTTBASE is the initialised value of each connection's SRTT, so
	 * we only calc beta if the connection's SRTT has been changed from its
	 * initial value. beta is bounded to ensure it is always between
	 * HTCP_MINBETA and HTCP_MAXBETA.
	 */
	if (V_htcp_adaptive_backoff && htcp_data->minrtt != TCPTV_SRTTBASE &&
	    htcp_data->maxrtt != TCPTV_SRTTBASE)
		htcp_data->beta = min(max(HTCP_MINBETA,
		    (htcp_data->minrtt << HTCP_SHIFT) / htcp_data->maxrtt),
		    HTCP_MAXBETA);
	else
		htcp_data->beta = HTCP_MINBETA;
}

/*
 * Record the minimum and maximum RTT seen for the connection. These are used in
 * the calculation of beta if adaptive backoff is enabled.
 */
static void
htcp_record_rtt(struct cc_var *ccv)
{
	struct htcp *htcp_data;

	htcp_data = ccv->cc_data;

	/* XXXLAS: Should there be some hysteresis for minrtt? */

	/*
	 * Record the current SRTT as our minrtt if it's the smallest we've seen
	 * or minrtt is currently equal to its initialised value. Ignore SRTT
	 * until a min number of samples have been taken.
	 */
	if ((CCV(ccv, t_srtt) < htcp_data->minrtt ||
	    htcp_data->minrtt == TCPTV_SRTTBASE) &&
	    (CCV(ccv, t_rttupdated) >= HTCP_MIN_RTT_SAMPLES))
		htcp_data->minrtt = CCV(ccv, t_srtt);

	/*
	 * Record the current SRTT as our maxrtt if it's the largest we've
	 * seen. Ignore SRTT until a min number of samples have been taken.
	 */
	if (CCV(ccv, t_srtt) > htcp_data->maxrtt
	    && CCV(ccv, t_rttupdated) >= HTCP_MIN_RTT_SAMPLES)
		htcp_data->maxrtt = CCV(ccv, t_srtt);
}

/*
 * Update the ssthresh in the event of congestion.
 */
static void
htcp_ssthresh_update(struct cc_var *ccv)
{
	struct htcp *htcp_data;

	htcp_data = ccv->cc_data;

	/*
	 * On the first congestion event, set ssthresh to cwnd * 0.5, on
	 * subsequent congestion events, set it to cwnd * beta.
	 */
	if (CCV(ccv, snd_ssthresh) == TCP_MAXWIN << TCP_MAX_WINSHIFT)
		CCV(ccv, snd_ssthresh) = ((u_long)CCV(ccv, snd_cwnd) *
		    HTCP_MINBETA) >> HTCP_SHIFT;
	else {
		htcp_recalc_beta(ccv);
		CCV(ccv, snd_ssthresh) = ((u_long)CCV(ccv, snd_cwnd) *
		    htcp_data->beta) >> HTCP_SHIFT;
	}
}


SYSCTL_DECL(_net_inet_tcp_cc_htcp);
SYSCTL_NODE(_net_inet_tcp_cc, OID_AUTO, htcp, CTLFLAG_RW,
    NULL, "H-TCP related settings");
SYSCTL_UINT(_net_inet_tcp_cc_htcp, OID_AUTO, adaptive_backoff,
    CTLFLAG_VNET | CTLFLAG_RW, &VNET_NAME(htcp_adaptive_backoff), 0,
    "enable H-TCP adaptive backoff");
SYSCTL_UINT(_net_inet_tcp_cc_htcp, OID_AUTO, rtt_scaling,
    CTLFLAG_VNET | CTLFLAG_RW, &VNET_NAME(htcp_rtt_scaling), 0,
    "enable H-TCP RTT scaling");

DECLARE_CC_MODULE(htcp, &htcp_cc_algo);
