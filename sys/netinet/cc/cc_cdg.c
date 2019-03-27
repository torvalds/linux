/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2009-2013
 * 	Swinburne University of Technology, Melbourne, Australia
 * All rights reserved.
 *
 * This software was developed at the Centre for Advanced Internet
 * Architectures, Swinburne University of Technology, by David Hayes, made
 * possible in part by a gift from The Cisco University Research Program Fund,
 * a corporate advised fund of Silicon Valley Community Foundation. Development
 * and testing were further assisted by a grant from the FreeBSD Foundation.
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
 * CAIA Delay-Gradient (CDG) congestion control algorithm
 *
 * An implemention of the delay-gradient congestion control algorithm proposed
 * in the following paper:
 *
 * D. A. Hayes and G. Armitage, "Revisiting TCP Congestion Control using Delay
 * Gradients", in IFIP Networking, Valencia, Spain, 9-13 May 2011.
 *
 * Developed as part of the NewTCP research project at Swinburne University of
 * Technology's Centre for Advanced Internet Architectures, Melbourne,
 * Australia. More details are available at:
 *   http://caia.swin.edu.au/urp/newtcp/
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/hhook.h>
#include <sys/kernel.h>
#include <sys/khelp.h>
#include <sys/limits.h>
#include <sys/lock.h>
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

#include <vm/uma.h>

#define	CDG_VERSION "0.1"

/* Private delay-gradient induced congestion control signal. */
#define	CC_CDG_DELAY 0x01000000

/* NewReno window deflation factor on loss (as a percentage). */
#define	RENO_BETA 50

/* Queue states. */
#define	CDG_Q_EMPTY	1
#define	CDG_Q_RISING	2
#define	CDG_Q_FALLING	3
#define	CDG_Q_FULL	4
#define	CDG_Q_UNKNOWN	9999

/* Number of bit shifts used in probexp lookup table. */
#define	EXP_PREC 15

/* Largest gradient represented in probexp lookup table. */
#define	MAXGRAD 5

/*
 * Delay Precision Enhance - number of bit shifts used for qtrend related
 * integer arithmetic precision.
 */
#define	D_P_E 7

struct qdiff_sample {
	long qdiff;
	STAILQ_ENTRY(qdiff_sample) qdiff_lnk;
};

struct cdg {
	long max_qtrend;
	long min_qtrend;
	STAILQ_HEAD(minrtts_head, qdiff_sample) qdiffmin_q;
	STAILQ_HEAD(maxrtts_head, qdiff_sample) qdiffmax_q;
	long window_incr;
	/* rttcount for window increase when in congestion avoidance */
	long rtt_count;
	/* maximum measured rtt within an rtt period */
	int maxrtt_in_rtt;
	/* maximum measured rtt within prev rtt period */
	int maxrtt_in_prevrtt;
	/* minimum measured rtt within an rtt period */
	int minrtt_in_rtt;
	/* minimum measured rtt within prev rtt period */
	int minrtt_in_prevrtt;
	/* consecutive congestion episode counter */
	uint32_t consec_cong_cnt;
	/* when tracking a new reno type loss window */
	uint32_t shadow_w;
	/* maximum number of samples in the moving average queue */
	int sample_q_size;
	/* number of samples in the moving average queue */
	int num_samples;
	/* estimate of the queue state of the path */
	int queue_state;
};

/*
 * Lookup table for:
 *   (1 - exp(-x)) << EXP_PREC, where x = [0,MAXGRAD] in 2^-7 increments
 *
 * Note: probexp[0] is set to 10 (not 0) as a safety for very low increase
 * gradients.
 */
static const int probexp[641] = {
   10,255,508,759,1008,1255,1501,1744,1985,2225,2463,2698,2932,3165,3395,3624,
   3850,4075,4299,4520,4740,4958,5175,5389,5602,5814,6024,6232,6438,6643,6846,
   7048,7248,7447,7644,7839,8033,8226,8417,8606,8794,8981,9166,9350,9532,9713,
   9892,10070,10247,10422,10596,10769,10940,11110,11278,11445,11611,11776,11939,
   12101,12262,12422,12580,12737,12893,13048,13201,13354,13505,13655,13803,13951,
   14097,14243,14387,14530,14672,14813,14952,15091,15229,15365,15500,15635,15768,
   15900,16032,16162,16291,16419,16547,16673,16798,16922,17046,17168,17289,17410,
   17529,17648,17766,17882,17998,18113,18227,18340,18453,18564,18675,18784,18893,
   19001,19108,19215,19320,19425,19529,19632,19734,19835,19936,20036,20135,20233,
   20331,20427,20523,20619,20713,20807,20900,20993,21084,21175,21265,21355,21444,
   21532,21619,21706,21792,21878,21962,22046,22130,22213,22295,22376,22457,22537,
   22617,22696,22774,22852,22929,23006,23082,23157,23232,23306,23380,23453,23525,
   23597,23669,23739,23810,23879,23949,24017,24085,24153,24220,24286,24352,24418,
   24483,24547,24611,24675,24738,24800,24862,24924,24985,25045,25106,25165,25224,
   25283,25341,25399,25456,25513,25570,25626,25681,25737,25791,25846,25899,25953,
   26006,26059,26111,26163,26214,26265,26316,26366,26416,26465,26514,26563,26611,
   26659,26707,26754,26801,26847,26893,26939,26984,27029,27074,27118,27162,27206,
   27249,27292,27335,27377,27419,27460,27502,27543,27583,27624,27664,27703,27743,
   27782,27821,27859,27897,27935,27973,28010,28047,28084,28121,28157,28193,28228,
   28263,28299,28333,28368,28402,28436,28470,28503,28536,28569,28602,28634,28667,
   28699,28730,28762,28793,28824,28854,28885,28915,28945,28975,29004,29034,29063,
   29092,29120,29149,29177,29205,29232,29260,29287,29314,29341,29368,29394,29421,
   29447,29472,29498,29524,29549,29574,29599,29623,29648,29672,29696,29720,29744,
   29767,29791,29814,29837,29860,29882,29905,29927,29949,29971,29993,30014,30036,
   30057,30078,30099,30120,30141,30161,30181,30201,30221,30241,30261,30280,30300,
   30319,30338,30357,30376,30394,30413,30431,30449,30467,30485,30503,30521,30538,
   30555,30573,30590,30607,30624,30640,30657,30673,30690,30706,30722,30738,30753,
   30769,30785,30800,30815,30831,30846,30861,30876,30890,30905,30919,30934,30948,
   30962,30976,30990,31004,31018,31031,31045,31058,31072,31085,31098,31111,31124,
   31137,31149,31162,31174,31187,31199,31211,31223,31235,31247,31259,31271,31283,
   31294,31306,31317,31328,31339,31351,31362,31373,31383,31394,31405,31416,31426,
   31436,31447,31457,31467,31477,31487,31497,31507,31517,31527,31537,31546,31556,
   31565,31574,31584,31593,31602,31611,31620,31629,31638,31647,31655,31664,31673,
   31681,31690,31698,31706,31715,31723,31731,31739,31747,31755,31763,31771,31778,
   31786,31794,31801,31809,31816,31824,31831,31838,31846,31853,31860,31867,31874,
   31881,31888,31895,31902,31908,31915,31922,31928,31935,31941,31948,31954,31960,
   31967,31973,31979,31985,31991,31997,32003,32009,32015,32021,32027,32033,32038,
   32044,32050,32055,32061,32066,32072,32077,32083,32088,32093,32098,32104,32109,
   32114,32119,32124,32129,32134,32139,32144,32149,32154,32158,32163,32168,32173,
   32177,32182,32186,32191,32195,32200,32204,32209,32213,32217,32222,32226,32230,
   32234,32238,32242,32247,32251,32255,32259,32263,32267,32270,32274,32278,32282,
   32286,32290,32293,32297,32301,32304,32308,32311,32315,32318,32322,32325,32329,
   32332,32336,32339,32342,32346,32349,32352,32356,32359,32362,32365,32368,32371,
   32374,32377,32381,32384,32387,32389,32392,32395,32398,32401,32404,32407,32410,
   32412,32415,32418,32421,32423,32426,32429,32431,32434,32437,32439,32442,32444,
   32447,32449,32452,32454,32457,32459,32461,32464,32466,32469,32471,32473,32476,
   32478,32480,32482,32485,32487,32489,32491,32493,32495,32497,32500,32502,32504,
   32506,32508,32510,32512,32514,32516,32518,32520,32522,32524,32526,32527,32529,
   32531,32533,32535,32537,32538,32540,32542,32544,32545,32547};

static uma_zone_t qdiffsample_zone;

static MALLOC_DEFINE(M_CDG, "cdg data",
  "Per connection data required for the CDG congestion control algorithm");

static int ertt_id;

VNET_DEFINE_STATIC(uint32_t, cdg_alpha_inc);
VNET_DEFINE_STATIC(uint32_t, cdg_beta_delay);
VNET_DEFINE_STATIC(uint32_t, cdg_beta_loss);
VNET_DEFINE_STATIC(uint32_t, cdg_smoothing_factor);
VNET_DEFINE_STATIC(uint32_t, cdg_exp_backoff_scale);
VNET_DEFINE_STATIC(uint32_t, cdg_consec_cong);
VNET_DEFINE_STATIC(uint32_t, cdg_hold_backoff);
#define	V_cdg_alpha_inc		VNET(cdg_alpha_inc)
#define	V_cdg_beta_delay	VNET(cdg_beta_delay)
#define	V_cdg_beta_loss		VNET(cdg_beta_loss)
#define	V_cdg_smoothing_factor	VNET(cdg_smoothing_factor)
#define	V_cdg_exp_backoff_scale	VNET(cdg_exp_backoff_scale)
#define	V_cdg_consec_cong	VNET(cdg_consec_cong)
#define	V_cdg_hold_backoff	VNET(cdg_hold_backoff)

/* Function prototypes. */
static int cdg_mod_init(void);
static int cdg_mod_destroy(void);
static void cdg_conn_init(struct cc_var *ccv);
static int cdg_cb_init(struct cc_var *ccv);
static void cdg_cb_destroy(struct cc_var *ccv);
static void cdg_cong_signal(struct cc_var *ccv, uint32_t signal_type);
static void cdg_ack_received(struct cc_var *ccv, uint16_t ack_type);

struct cc_algo cdg_cc_algo = {
	.name = "cdg",
	.mod_init = cdg_mod_init,
	.ack_received = cdg_ack_received,
	.cb_destroy = cdg_cb_destroy,
	.cb_init = cdg_cb_init,
	.conn_init = cdg_conn_init,
	.cong_signal = cdg_cong_signal,
	.mod_destroy = cdg_mod_destroy
};

/* Vnet created and being initialised. */
static void
cdg_init_vnet(const void *unused __unused)
{

	V_cdg_alpha_inc = 0;
	V_cdg_beta_delay = 70;
	V_cdg_beta_loss = 50;
	V_cdg_smoothing_factor = 8;
	V_cdg_exp_backoff_scale = 3;
	V_cdg_consec_cong = 5;
	V_cdg_hold_backoff = 5;
}

static int
cdg_mod_init(void)
{
	VNET_ITERATOR_DECL(v);

	ertt_id = khelp_get_id("ertt");
	if (ertt_id <= 0)
		return (EINVAL);

	qdiffsample_zone = uma_zcreate("cdg_qdiffsample",
	    sizeof(struct qdiff_sample), NULL, NULL, NULL, NULL, 0, 0);

	VNET_LIST_RLOCK();
	VNET_FOREACH(v) {
		CURVNET_SET(v);
		cdg_init_vnet(NULL);
		CURVNET_RESTORE();
	}
	VNET_LIST_RUNLOCK();

	cdg_cc_algo.post_recovery = newreno_cc_algo.post_recovery;
	cdg_cc_algo.after_idle = newreno_cc_algo.after_idle;

	return (0);
}

static int
cdg_mod_destroy(void)
{

	uma_zdestroy(qdiffsample_zone);
	return (0);
}

static int
cdg_cb_init(struct cc_var *ccv)
{
	struct cdg *cdg_data;

	cdg_data = malloc(sizeof(struct cdg), M_CDG, M_NOWAIT);
	if (cdg_data == NULL)
		return (ENOMEM);

	cdg_data->shadow_w = 0;
	cdg_data->max_qtrend = 0;
	cdg_data->min_qtrend = 0;
	cdg_data->queue_state = CDG_Q_UNKNOWN;
	cdg_data->maxrtt_in_rtt = 0;
	cdg_data->maxrtt_in_prevrtt = 0;
	cdg_data->minrtt_in_rtt = INT_MAX;
	cdg_data->minrtt_in_prevrtt = 0;
	cdg_data->window_incr = 0;
	cdg_data->rtt_count = 0;
	cdg_data->consec_cong_cnt = 0;
	cdg_data->sample_q_size = V_cdg_smoothing_factor;
	cdg_data->num_samples = 0;
	STAILQ_INIT(&cdg_data->qdiffmin_q);
	STAILQ_INIT(&cdg_data->qdiffmax_q);

	ccv->cc_data = cdg_data;

	return (0);
}

static void
cdg_conn_init(struct cc_var *ccv)
{
	struct cdg *cdg_data = ccv->cc_data;

	/*
	 * Initialise the shadow_cwnd in case we are competing with loss based
	 * flows from the start
	 */
	cdg_data->shadow_w = CCV(ccv, snd_cwnd);
}

static void
cdg_cb_destroy(struct cc_var *ccv)
{
	struct cdg *cdg_data;
	struct qdiff_sample *qds, *qds_n;

	cdg_data = ccv->cc_data;

	qds = STAILQ_FIRST(&cdg_data->qdiffmin_q);
	while (qds != NULL) {
		qds_n = STAILQ_NEXT(qds, qdiff_lnk);
		uma_zfree(qdiffsample_zone,qds);
		qds = qds_n;
	}

	qds = STAILQ_FIRST(&cdg_data->qdiffmax_q);
	while (qds != NULL) {
		qds_n = STAILQ_NEXT(qds, qdiff_lnk);
		uma_zfree(qdiffsample_zone,qds);
		qds = qds_n;
	}

	free(ccv->cc_data, M_CDG);
}

static int
cdg_beta_handler(SYSCTL_HANDLER_ARGS)
{
	int error;
	uint32_t new;

	new = *(uint32_t *)arg1;
	error = sysctl_handle_int(oidp, &new, 0, req);
	if (error == 0 && req->newptr != NULL) {
		if (new == 0 || new > 100)
			error = EINVAL;
		else
			*(uint32_t *)arg1 = new;
	}

	return (error);
}

static int
cdg_exp_backoff_scale_handler(SYSCTL_HANDLER_ARGS)
{
	int error;
	uint32_t new;

	new = *(uint32_t *)arg1;
	error = sysctl_handle_int(oidp, &new, 0, req);
	if (error == 0 && req->newptr != NULL) {
		if (new < 1)
			error = EINVAL;
		else
			*(uint32_t *)arg1 = new;
	}

	return (error);
}

static inline uint32_t
cdg_window_decrease(struct cc_var *ccv, unsigned long owin, unsigned int beta)
{

	return ((ulmin(CCV(ccv, snd_wnd), owin) * beta) / 100);
}

/*
 * Window increase function
 * This window increase function is independent of the initial window size
 * to ensure small window flows are not discriminated against (i.e. fairness).
 * It increases at 1pkt/rtt like Reno for alpha_inc rtts, and then 2pkts/rtt for
 * the next alpha_inc rtts, etc.
 */
static void
cdg_window_increase(struct cc_var *ccv, int new_measurement)
{
	struct cdg *cdg_data;
	int incr, s_w_incr;

	cdg_data = ccv->cc_data;
	incr = s_w_incr = 0;

	if (CCV(ccv, snd_cwnd) <= CCV(ccv, snd_ssthresh)) {
		/* Slow start. */
		incr = CCV(ccv, t_maxseg);
		s_w_incr = incr;
		cdg_data->window_incr = cdg_data->rtt_count = 0;
	} else {
		/* Congestion avoidance. */
		if (new_measurement) {
			s_w_incr = CCV(ccv, t_maxseg);
			if (V_cdg_alpha_inc == 0) {
				incr = CCV(ccv, t_maxseg);
			} else {
				if (++cdg_data->rtt_count >= V_cdg_alpha_inc) {
					cdg_data->window_incr++;
					cdg_data->rtt_count = 0;
				}
				incr = CCV(ccv, t_maxseg) *
				    cdg_data->window_incr;
			}
		}
	}

	if (cdg_data->shadow_w > 0)
		cdg_data->shadow_w = ulmin(cdg_data->shadow_w + s_w_incr,
		    TCP_MAXWIN << CCV(ccv, snd_scale));

	CCV(ccv, snd_cwnd) = ulmin(CCV(ccv, snd_cwnd) + incr,
	    TCP_MAXWIN << CCV(ccv, snd_scale));
}

static void
cdg_cong_signal(struct cc_var *ccv, uint32_t signal_type)
{
	struct cdg *cdg_data = ccv->cc_data;

	switch(signal_type) {
	case CC_CDG_DELAY:
		CCV(ccv, snd_ssthresh) = cdg_window_decrease(ccv,
		    CCV(ccv, snd_cwnd), V_cdg_beta_delay);
		CCV(ccv, snd_cwnd) = CCV(ccv, snd_ssthresh);
		CCV(ccv, snd_recover) = CCV(ccv, snd_max);
		cdg_data->window_incr = cdg_data->rtt_count = 0;
		ENTER_CONGRECOVERY(CCV(ccv, t_flags));
		break;
	case CC_NDUPACK:
		/*
		 * If already responding to congestion OR we have guessed no
		 * queue in the path is full.
		 */
		if (IN_CONGRECOVERY(CCV(ccv, t_flags)) ||
		    cdg_data->queue_state < CDG_Q_FULL) {
			CCV(ccv, snd_ssthresh) = CCV(ccv, snd_cwnd);
			CCV(ccv, snd_recover) = CCV(ccv, snd_max);
		} else {
			/*
			 * Loss is likely to be congestion related. We have
			 * inferred a queue full state, so have shadow window
			 * react to loss as NewReno would.
			 */
			if (cdg_data->shadow_w > 0)
				cdg_data->shadow_w = cdg_window_decrease(ccv,
				    cdg_data->shadow_w, RENO_BETA);

			CCV(ccv, snd_ssthresh) = max(cdg_data->shadow_w,
			    cdg_window_decrease(ccv, CCV(ccv, snd_cwnd),
			    V_cdg_beta_loss));

			cdg_data->window_incr = cdg_data->rtt_count = 0;
		}
		ENTER_RECOVERY(CCV(ccv, t_flags));
		break;
	default:
		newreno_cc_algo.cong_signal(ccv, signal_type);
		break;
	}
}

/*
 * Using a negative exponential probabilistic backoff so that sources with
 * varying RTTs which share the same link will, on average, have the same
 * probability of backoff over time.
 *
 * Prob_backoff = 1 - exp(-qtrend / V_cdg_exp_backoff_scale), where
 * V_cdg_exp_backoff_scale is the average qtrend for the exponential backoff.
 */
static inline int
prob_backoff(long qtrend)
{
	int backoff, idx, p;

	backoff = (qtrend > ((MAXGRAD * V_cdg_exp_backoff_scale) << D_P_E));

	if (!backoff) {
		if (V_cdg_exp_backoff_scale > 1)
			idx = (qtrend + V_cdg_exp_backoff_scale / 2) /
			    V_cdg_exp_backoff_scale;
		else
			idx = qtrend;

		/* Backoff probability proportional to rate of queue growth. */
		p = (INT_MAX / (1 << EXP_PREC)) * probexp[idx];
		backoff = (random() < p);
	}

	return (backoff);
}

static inline void
calc_moving_average(struct cdg *cdg_data, long qdiff_max, long qdiff_min)
{
	struct qdiff_sample *qds;

	++cdg_data->num_samples;
	if (cdg_data->num_samples > cdg_data->sample_q_size) {
		/* Minimum RTT. */
		qds = STAILQ_FIRST(&cdg_data->qdiffmin_q);
		cdg_data->min_qtrend =  cdg_data->min_qtrend +
		    (qdiff_min - qds->qdiff) / cdg_data->sample_q_size;
		STAILQ_REMOVE_HEAD(&cdg_data->qdiffmin_q, qdiff_lnk);
		qds->qdiff = qdiff_min;
		STAILQ_INSERT_TAIL(&cdg_data->qdiffmin_q, qds, qdiff_lnk);

		/* Maximum RTT. */
		qds = STAILQ_FIRST(&cdg_data->qdiffmax_q);
		cdg_data->max_qtrend =  cdg_data->max_qtrend +
		    (qdiff_max - qds->qdiff) / cdg_data->sample_q_size;
		STAILQ_REMOVE_HEAD(&cdg_data->qdiffmax_q, qdiff_lnk);
		qds->qdiff = qdiff_max;
		STAILQ_INSERT_TAIL(&cdg_data->qdiffmax_q, qds, qdiff_lnk);
		--cdg_data->num_samples;
	} else {
		qds = uma_zalloc(qdiffsample_zone, M_NOWAIT);
		if (qds != NULL) {
			cdg_data->min_qtrend = cdg_data->min_qtrend +
			    qdiff_min / cdg_data->sample_q_size;
			qds->qdiff = qdiff_min;
			STAILQ_INSERT_TAIL(&cdg_data->qdiffmin_q, qds,
			    qdiff_lnk);
		}

		qds = uma_zalloc(qdiffsample_zone, M_NOWAIT);
		if (qds) {
			cdg_data->max_qtrend = cdg_data->max_qtrend +
			    qdiff_max / cdg_data->sample_q_size;
			qds->qdiff = qdiff_max;
			STAILQ_INSERT_TAIL(&cdg_data->qdiffmax_q, qds,
			    qdiff_lnk);
		}
	}
}

static void
cdg_ack_received(struct cc_var *ccv, uint16_t ack_type)
{
	struct cdg *cdg_data;
	struct ertt *e_t;
	long qdiff_max, qdiff_min;
	int congestion, new_measurement, slowstart;

	cdg_data = ccv->cc_data;
	e_t = (struct ertt *)khelp_get_osd(CCV(ccv, osd), ertt_id);
	new_measurement = e_t->flags & ERTT_NEW_MEASUREMENT;
	congestion = 0;
	cdg_data->maxrtt_in_rtt = imax(e_t->rtt, cdg_data->maxrtt_in_rtt);
	cdg_data->minrtt_in_rtt = imin(e_t->rtt, cdg_data->minrtt_in_rtt);

	if (new_measurement) {
		slowstart = (CCV(ccv, snd_cwnd) <= CCV(ccv, snd_ssthresh));
		/*
		 * Update smoothed gradient measurements. Since we are only
		 * using one measurement per RTT, use max or min rtt_in_rtt.
		 * This is also less noisy than a sample RTT measurement. Max
		 * RTT measurements can have trouble due to OS issues.
		 */
		if (cdg_data->maxrtt_in_prevrtt) {
			qdiff_max = ((long)(cdg_data->maxrtt_in_rtt -
			    cdg_data->maxrtt_in_prevrtt) << D_P_E );
			qdiff_min = ((long)(cdg_data->minrtt_in_rtt -
			    cdg_data->minrtt_in_prevrtt) << D_P_E );

			if (cdg_data->sample_q_size == 0) {
				cdg_data->max_qtrend = qdiff_max;
				cdg_data->min_qtrend = qdiff_min;
			} else
				calc_moving_average(cdg_data, qdiff_max, qdiff_min);

			/* Probabilistic backoff with respect to gradient. */
			if (slowstart && qdiff_min > 0)
				congestion = prob_backoff(qdiff_min);
			else if (cdg_data->min_qtrend > 0)
				congestion = prob_backoff(cdg_data->min_qtrend);
			else if (slowstart && qdiff_max > 0)
				congestion = prob_backoff(qdiff_max);
			else if (cdg_data->max_qtrend > 0)
				congestion = prob_backoff(cdg_data->max_qtrend);
			
			/* Update estimate of queue state. */
			if (cdg_data->min_qtrend > 0 &&
			    cdg_data->max_qtrend <= 0) {
				cdg_data->queue_state = CDG_Q_FULL;
			} else if (cdg_data->min_qtrend >= 0 &&
			    cdg_data->max_qtrend < 0) {
				cdg_data->queue_state = CDG_Q_EMPTY;
				cdg_data->shadow_w = 0;
			} else if (cdg_data->min_qtrend > 0 &&
			    cdg_data->max_qtrend > 0) {
				cdg_data->queue_state = CDG_Q_RISING;
			} else if (cdg_data->min_qtrend < 0 &&
			    cdg_data->max_qtrend < 0) {
				cdg_data->queue_state = CDG_Q_FALLING;
			}

			if (cdg_data->min_qtrend < 0 ||
			    cdg_data->max_qtrend < 0)
				cdg_data->consec_cong_cnt = 0;
		}

		cdg_data->minrtt_in_prevrtt = cdg_data->minrtt_in_rtt;
		cdg_data->minrtt_in_rtt = INT_MAX;
		cdg_data->maxrtt_in_prevrtt = cdg_data->maxrtt_in_rtt;
		cdg_data->maxrtt_in_rtt = 0;
		e_t->flags &= ~ERTT_NEW_MEASUREMENT;
	}

	if (congestion) {
		cdg_data->consec_cong_cnt++;
		if (!IN_RECOVERY(CCV(ccv, t_flags))) {
			if (cdg_data->consec_cong_cnt <= V_cdg_consec_cong)
				cdg_cong_signal(ccv, CC_CDG_DELAY);
			else
				/*
				 * We have been backing off but the queue is not
				 * falling. Assume we are competing with
				 * loss-based flows and don't back off for the
				 * next V_cdg_hold_backoff RTT periods.
				 */
				if (cdg_data->consec_cong_cnt >=
				    V_cdg_consec_cong + V_cdg_hold_backoff)
					cdg_data->consec_cong_cnt = 0;

			/* Won't see effect until 2nd RTT. */
			cdg_data->maxrtt_in_prevrtt = 0;
			/*
			 * Resync shadow window in case we are competing with a
			 * loss based flow
			 */
			cdg_data->shadow_w = ulmax(CCV(ccv, snd_cwnd),
			    cdg_data->shadow_w);
		}
	} else if (ack_type == CC_ACK)
		cdg_window_increase(ccv, new_measurement);
}

/* When a vnet is created and being initialised, init the per-stack CDG vars. */
VNET_SYSINIT(cdg_init_vnet, SI_SUB_PROTO_BEGIN, SI_ORDER_FIRST,
    cdg_init_vnet, NULL);

SYSCTL_DECL(_net_inet_tcp_cc_cdg);
SYSCTL_NODE(_net_inet_tcp_cc, OID_AUTO, cdg, CTLFLAG_RW, NULL,
    "CAIA delay-gradient congestion control related settings");

SYSCTL_STRING(_net_inet_tcp_cc_cdg, OID_AUTO, version,
    CTLFLAG_RD, CDG_VERSION, sizeof(CDG_VERSION) - 1,
    "Current algorithm/implementation version number");

SYSCTL_UINT(_net_inet_tcp_cc_cdg, OID_AUTO, alpha_inc,
    CTLFLAG_VNET | CTLFLAG_RW, &VNET_NAME(cdg_alpha_inc), 0,
    "Increment the window increase factor alpha by 1 MSS segment every "
    "alpha_inc RTTs during congestion avoidance mode.");

SYSCTL_PROC(_net_inet_tcp_cc_cdg, OID_AUTO, beta_delay,
    CTLFLAG_VNET | CTLTYPE_UINT | CTLFLAG_RW, &VNET_NAME(cdg_beta_delay), 70,
    &cdg_beta_handler, "IU",
    "Delay-based window decrease factor as a percentage "
    "(on delay-based backoff, w = w * beta_delay / 100)");

SYSCTL_PROC(_net_inet_tcp_cc_cdg, OID_AUTO, beta_loss,
    CTLFLAG_VNET | CTLTYPE_UINT | CTLFLAG_RW, &VNET_NAME(cdg_beta_loss), 50,
    &cdg_beta_handler, "IU",
    "Loss-based window decrease factor as a percentage "
    "(on loss-based backoff, w = w * beta_loss / 100)");

SYSCTL_PROC(_net_inet_tcp_cc_cdg, OID_AUTO, exp_backoff_scale,
    CTLFLAG_VNET | CTLTYPE_UINT | CTLFLAG_RW,
    &VNET_NAME(cdg_exp_backoff_scale), 2, &cdg_exp_backoff_scale_handler, "IU",
    "Scaling parameter for the probabilistic exponential backoff");

SYSCTL_UINT(_net_inet_tcp_cc_cdg,  OID_AUTO, smoothing_factor,
    CTLFLAG_VNET | CTLFLAG_RW, &VNET_NAME(cdg_smoothing_factor), 8,
    "Number of samples used for moving average smoothing (0 = no smoothing)");

SYSCTL_UINT(_net_inet_tcp_cc_cdg, OID_AUTO, loss_compete_consec_cong,
    CTLFLAG_VNET | CTLFLAG_RW, &VNET_NAME(cdg_consec_cong), 5,
    "Number of consecutive delay-gradient based congestion episodes which will "
    "trigger loss based CC compatibility");

SYSCTL_UINT(_net_inet_tcp_cc_cdg, OID_AUTO, loss_compete_hold_backoff,
    CTLFLAG_VNET | CTLFLAG_RW, &VNET_NAME(cdg_hold_backoff), 5,
    "Number of consecutive delay-gradient based congestion episodes to hold "
    "the window backoff for loss based CC compatibility");

DECLARE_CC_MODULE(cdg, &cdg_cc_algo);

MODULE_DEPEND(cdg, ertt, 1, 1, 1);
