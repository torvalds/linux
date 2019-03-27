/*
 * PIE - Proportional Integral controller Enhanced AQM algorithm.
 *
 * $FreeBSD$
 * 
 * Copyright (C) 2016 Centre for Advanced Internet Architectures,
 *  Swinburne University of Technology, Melbourne, Australia.
 * Portions of this code were made possible in part by a gift from 
 *  The Comcast Innovation Fund.
 * Implemented by Rasool Al-Saadi <ralsaadi@swin.edu.au>
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

#ifndef _IP_DN_AQM_PIE_H
#define _IP_DN_AQM_PIE_H

#define DN_AQM_PIE 2
#define PIE_DQ_THRESHOLD_BITS 14
/* 2^14 =16KB */
#define PIE_DQ_THRESHOLD (1L << PIE_DQ_THRESHOLD_BITS) 
#define MEAN_PKTSIZE 800

/* 31-bits because random() generates range from 0->(2**31)-1 */
#define PIE_PROB_BITS 31
#define PIE_MAX_PROB ((1LL<<PIE_PROB_BITS) -1)

/* for 16-bits, we have 3-bits for integer part and 13-bits for fraction */
#define PIE_FIX_POINT_BITS 13
#define PIE_SCALE (1L<<PIE_FIX_POINT_BITS)


/* PIE options */
enum {
	PIE_ECN_ENABLED =1,
	PIE_CAPDROP_ENABLED = 2,
	PIE_ON_OFF_MODE_ENABLED = 4,
	PIE_DEPRATEEST_ENABLED = 8,
	PIE_DERAND_ENABLED = 16
};

/* PIE parameters */
struct dn_aqm_pie_parms {
	aqm_time_t	qdelay_ref;	/* AQM Latency Target (default: 15ms) */
	aqm_time_t	tupdate;		/* a period to calculate drop probability (default:15ms) */
	aqm_time_t	max_burst;	/* AQM Max Burst Allowance (default: 150ms) */
	uint16_t	max_ecnth;	/*AQM Max ECN Marking Threshold (default: 10%) */
	uint16_t	alpha;			/* (default: 1/8) */
	uint16_t	beta;			/* (default: 1+1/4) */
	uint32_t	flags;			/* PIE options */
};

/* PIE status variables */
struct pie_status{
	struct callout	aqm_pie_callout;
	aqm_time_t	burst_allowance;
	uint32_t	drop_prob;
	aqm_time_t	current_qdelay;
	aqm_time_t	qdelay_old;
	uint64_t	accu_prob;
	aqm_time_t	measurement_start;
	aqm_time_t	avg_dq_time;
	uint32_t	dq_count;
	uint32_t	sflags;
	struct dn_aqm_pie_parms *parms;	/* pointer to PIE configurations */
	/* pointer to parent queue of FQ-PIE sub-queues, or  queue of owner fs. */
	struct dn_queue	*pq;	
	struct mtx	lock_mtx;
	uint32_t one_third_q_size; /* 1/3 of queue size, for speed optization */
};

enum { 
	ENQUE = 1,
	DROP,
	MARKECN
};

/* PIE current state */
enum { 
	PIE_ACTIVE = 1,
	PIE_INMEASUREMENT = 2
};

/* 
 * Check if eneque should drop packet to control delay or not based on
 * PIe algorithm.
 * return  DROP if it is time to drop or  ENQUE otherwise.
 * This function is used by PIE and FQ-PIE.
 */
__inline static int
drop_early(struct pie_status *pst, uint32_t qlen)
{
	struct dn_aqm_pie_parms *pprms;

	pprms = pst->parms;

	/* queue is not congested */

	if ((pst->qdelay_old < (pprms->qdelay_ref >> 1)
		&& pst->drop_prob < PIE_MAX_PROB / 5 )
		||  qlen <= 2 * MEAN_PKTSIZE)
		return ENQUE;


	if (pst->drop_prob == 0)
		pst->accu_prob = 0;

	/* increment accu_prob */
	if (pprms->flags & PIE_DERAND_ENABLED)
		pst->accu_prob += pst->drop_prob;

	/* De-randomize option 
	 * if accu_prob < 0.85 -> enqueue
	 * if accu_prob>8.5 ->drop
	 * between 0.85 and 8.5 || !De-randomize --> drop on prob
	 * 
	 * (0.85 = 17/20 ,8.5 = 17/2)
	 */
	if (pprms->flags & PIE_DERAND_ENABLED) {
		if(pst->accu_prob < (uint64_t) (PIE_MAX_PROB * 17 / 20))
			return ENQUE;
		 if( pst->accu_prob >= (uint64_t) (PIE_MAX_PROB * 17 / 2))
			return DROP;
	}

	if (random() < pst->drop_prob) {
		pst->accu_prob = 0;
		return DROP;
	}

	return ENQUE;
}

#endif
