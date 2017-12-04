#ifndef __NET_SCHED_RED_H
#define __NET_SCHED_RED_H

#include <linux/types.h>
#include <linux/bug.h>
#include <net/pkt_sched.h>
#include <net/inet_ecn.h>
#include <net/dsfield.h>
#include <linux/reciprocal_div.h>

/*	Random Early Detection (RED) algorithm.
	=======================================

	Source: Sally Floyd and Van Jacobson, "Random Early Detection Gateways
	for Congestion Avoidance", 1993, IEEE/ACM Transactions on Networking.

	This file codes a "divisionless" version of RED algorithm
	as written down in Fig.17 of the paper.

	Short description.
	------------------

	When a new packet arrives we calculate the average queue length:

	avg = (1-W)*avg + W*current_queue_len,

	W is the filter time constant (chosen as 2^(-Wlog)), it controls
	the inertia of the algorithm. To allow larger bursts, W should be
	decreased.

	if (avg > th_max) -> packet marked (dropped).
	if (avg < th_min) -> packet passes.
	if (th_min < avg < th_max) we calculate probability:

	Pb = max_P * (avg - th_min)/(th_max-th_min)

	and mark (drop) packet with this probability.
	Pb changes from 0 (at avg==th_min) to max_P (avg==th_max).
	max_P should be small (not 1), usually 0.01..0.02 is good value.

	max_P is chosen as a number, so that max_P/(th_max-th_min)
	is a negative power of two in order arithmetics to contain
	only shifts.


	Parameters, settable by user:
	-----------------------------

	qth_min		- bytes (should be < qth_max/2)
	qth_max		- bytes (should be at least 2*qth_min and less limit)
	Wlog	       	- bits (<32) log(1/W).
	Plog	       	- bits (<32)

	Plog is related to max_P by formula:

	max_P = (qth_max-qth_min)/2^Plog;

	F.e. if qth_max=128K and qth_min=32K, then Plog=22
	corresponds to max_P=0.02

	Scell_log
	Stab

	Lookup table for log((1-W)^(t/t_ave).


	NOTES:

	Upper bound on W.
	-----------------

	If you want to allow bursts of L packets of size S,
	you should choose W:

	L + 1 - th_min/S < (1-(1-W)^L)/W

	th_min/S = 32         th_min/S = 4

	log(W)	L
	-1	33
	-2	35
	-3	39
	-4	46
	-5	57
	-6	75
	-7	101
	-8	135
	-9	190
	etc.
 */

/*
 * Adaptative RED : An Algorithm for Increasing the Robustness of RED's AQM
 * (Sally FLoyd, Ramakrishna Gummadi, and Scott Shenker) August 2001
 *
 * Every 500 ms:
 *  if (avg > target and max_p <= 0.5)
 *   increase max_p : max_p += alpha;
 *  else if (avg < target and max_p >= 0.01)
 *   decrease max_p : max_p *= beta;
 *
 * target :[qth_min + 0.4*(qth_min - qth_max),
 *          qth_min + 0.6*(qth_min - qth_max)].
 * alpha : min(0.01, max_p / 4)
 * beta : 0.9
 * max_P is a Q0.32 fixed point number (with 32 bits mantissa)
 * max_P between 0.01 and 0.5 (1% - 50%) [ Its no longer a negative power of two ]
 */
#define RED_ONE_PERCENT ((u32)DIV_ROUND_CLOSEST(1ULL<<32, 100))

#define MAX_P_MIN (1 * RED_ONE_PERCENT)
#define MAX_P_MAX (50 * RED_ONE_PERCENT)
#define MAX_P_ALPHA(val) min(MAX_P_MIN, val / 4)

#define RED_STAB_SIZE	256
#define RED_STAB_MASK	(RED_STAB_SIZE - 1)

struct red_stats {
	u32		prob_drop;	/* Early probability drops */
	u32		prob_mark;	/* Early probability marks */
	u32		forced_drop;	/* Forced drops, qavg > max_thresh */
	u32		forced_mark;	/* Forced marks, qavg > max_thresh */
	u32		pdrop;          /* Drops due to queue limits */
	u32		other;          /* Drops due to drop() calls */
};

struct red_parms {
	/* Parameters */
	u32		qth_min;	/* Min avg length threshold: Wlog scaled */
	u32		qth_max;	/* Max avg length threshold: Wlog scaled */
	u32		Scell_max;
	u32		max_P;		/* probability, [0 .. 1.0] 32 scaled */
	/* reciprocal_value(max_P / qth_delta) */
	struct reciprocal_value	max_P_reciprocal;
	u32		qth_delta;	/* max_th - min_th */
	u32		target_min;	/* min_th + 0.4*(max_th - min_th) */
	u32		target_max;	/* min_th + 0.6*(max_th - min_th) */
	u8		Scell_log;
	u8		Wlog;		/* log(W)		*/
	u8		Plog;		/* random number bits	*/
	u8		Stab[RED_STAB_SIZE];
};

struct red_vars {
	/* Variables */
	int		qcount;		/* Number of packets since last random
					   number generation */
	u32		qR;		/* Cached random number */

	unsigned long	qavg;		/* Average queue length: Wlog scaled */
	ktime_t		qidlestart;	/* Start of current idle period */
};

static inline u32 red_maxp(u8 Plog)
{
	return Plog < 32 ? (~0U >> Plog) : ~0U;
}

static inline void red_set_vars(struct red_vars *v)
{
	/* Reset average queue length, the value is strictly bound
	 * to the parameters below, reseting hurts a bit but leaving
	 * it might result in an unreasonable qavg for a while. --TGR
	 */
	v->qavg		= 0;

	v->qcount	= -1;
}

static inline void red_set_parms(struct red_parms *p,
				 u32 qth_min, u32 qth_max, u8 Wlog, u8 Plog,
				 u8 Scell_log, u8 *stab, u32 max_P)
{
	int delta = qth_max - qth_min;
	u32 max_p_delta;

	p->qth_min	= qth_min << Wlog;
	p->qth_max	= qth_max << Wlog;
	p->Wlog		= Wlog;
	p->Plog		= Plog;
	if (delta <= 0)
		delta = 1;
	p->qth_delta	= delta;
	if (!max_P) {
		max_P = red_maxp(Plog);
		max_P *= delta; /* max_P = (qth_max - qth_min)/2^Plog */
	}
	p->max_P = max_P;
	max_p_delta = max_P / delta;
	max_p_delta = max(max_p_delta, 1U);
	p->max_P_reciprocal  = reciprocal_value(max_p_delta);

	/* RED Adaptative target :
	 * [min_th + 0.4*(min_th - max_th),
	 *  min_th + 0.6*(min_th - max_th)].
	 */
	delta /= 5;
	p->target_min = qth_min + 2*delta;
	p->target_max = qth_min + 3*delta;

	p->Scell_log	= Scell_log;
	p->Scell_max	= (255 << Scell_log);

	if (stab)
		memcpy(p->Stab, stab, sizeof(p->Stab));
}

static inline int red_is_idling(const struct red_vars *v)
{
	return v->qidlestart.tv64 != 0;
}

static inline void red_start_of_idle_period(struct red_vars *v)
{
	v->qidlestart = ktime_get();
}

static inline void red_end_of_idle_period(struct red_vars *v)
{
	v->qidlestart.tv64 = 0;
}

static inline void red_restart(struct red_vars *v)
{
	red_end_of_idle_period(v);
	v->qavg = 0;
	v->qcount = -1;
}

static inline unsigned long red_calc_qavg_from_idle_time(const struct red_parms *p,
							 const struct red_vars *v)
{
	s64 delta = ktime_us_delta(ktime_get(), v->qidlestart);
	long us_idle = min_t(s64, delta, p->Scell_max);
	int  shift;

	/*
	 * The problem: ideally, average length queue recalcultion should
	 * be done over constant clock intervals. This is too expensive, so
	 * that the calculation is driven by outgoing packets.
	 * When the queue is idle we have to model this clock by hand.
	 *
	 * SF+VJ proposed to "generate":
	 *
	 *	m = idletime / (average_pkt_size / bandwidth)
	 *
	 * dummy packets as a burst after idle time, i.e.
	 *
	 * 	v->qavg *= (1-W)^m
	 *
	 * This is an apparently overcomplicated solution (f.e. we have to
	 * precompute a table to make this calculation in reasonable time)
	 * I believe that a simpler model may be used here,
	 * but it is field for experiments.
	 */

	shift = p->Stab[(us_idle >> p->Scell_log) & RED_STAB_MASK];

	if (shift)
		return v->qavg >> shift;
	else {
		/* Approximate initial part of exponent with linear function:
		 *
		 * 	(1-W)^m ~= 1-mW + ...
		 *
		 * Seems, it is the best solution to
		 * problem of too coarse exponent tabulation.
		 */
		us_idle = (v->qavg * (u64)us_idle) >> p->Scell_log;

		if (us_idle < (v->qavg >> 1))
			return v->qavg - us_idle;
		else
			return v->qavg >> 1;
	}
}

static inline unsigned long red_calc_qavg_no_idle_time(const struct red_parms *p,
						       const struct red_vars *v,
						       unsigned int backlog)
{
	/*
	 * NOTE: v->qavg is fixed point number with point at Wlog.
	 * The formula below is equvalent to floating point
	 * version:
	 *
	 * 	qavg = qavg*(1-W) + backlog*W;
	 *
	 * --ANK (980924)
	 */
	return v->qavg + (backlog - (v->qavg >> p->Wlog));
}

static inline unsigned long red_calc_qavg(const struct red_parms *p,
					  const struct red_vars *v,
					  unsigned int backlog)
{
	if (!red_is_idling(v))
		return red_calc_qavg_no_idle_time(p, v, backlog);
	else
		return red_calc_qavg_from_idle_time(p, v);
}


static inline u32 red_random(const struct red_parms *p)
{
	return reciprocal_divide(prandom_u32(), p->max_P_reciprocal);
}

static inline int red_mark_probability(const struct red_parms *p,
				       const struct red_vars *v,
				       unsigned long qavg)
{
	/* The formula used below causes questions.

	   OK. qR is random number in the interval
		(0..1/max_P)*(qth_max-qth_min)
	   i.e. 0..(2^Plog). If we used floating point
	   arithmetics, it would be: (2^Plog)*rnd_num,
	   where rnd_num is less 1.

	   Taking into account, that qavg have fixed
	   point at Wlog, two lines
	   below have the following floating point equivalent:

	   max_P*(qavg - qth_min)/(qth_max-qth_min) < rnd/qcount

	   Any questions? --ANK (980924)
	 */
	return !(((qavg - p->qth_min) >> p->Wlog) * v->qcount < v->qR);
}

enum {
	RED_BELOW_MIN_THRESH,
	RED_BETWEEN_TRESH,
	RED_ABOVE_MAX_TRESH,
};

static inline int red_cmp_thresh(const struct red_parms *p, unsigned long qavg)
{
	if (qavg < p->qth_min)
		return RED_BELOW_MIN_THRESH;
	else if (qavg >= p->qth_max)
		return RED_ABOVE_MAX_TRESH;
	else
		return RED_BETWEEN_TRESH;
}

enum {
	RED_DONT_MARK,
	RED_PROB_MARK,
	RED_HARD_MARK,
};

static inline int red_action(const struct red_parms *p,
			     struct red_vars *v,
			     unsigned long qavg)
{
	switch (red_cmp_thresh(p, qavg)) {
		case RED_BELOW_MIN_THRESH:
			v->qcount = -1;
			return RED_DONT_MARK;

		case RED_BETWEEN_TRESH:
			if (++v->qcount) {
				if (red_mark_probability(p, v, qavg)) {
					v->qcount = 0;
					v->qR = red_random(p);
					return RED_PROB_MARK;
				}
			} else
				v->qR = red_random(p);

			return RED_DONT_MARK;

		case RED_ABOVE_MAX_TRESH:
			v->qcount = -1;
			return RED_HARD_MARK;
	}

	BUG();
	return RED_DONT_MARK;
}

static inline void red_adaptative_algo(struct red_parms *p, struct red_vars *v)
{
	unsigned long qavg;
	u32 max_p_delta;

	qavg = v->qavg;
	if (red_is_idling(v))
		qavg = red_calc_qavg_from_idle_time(p, v);

	/* v->qavg is fixed point number with point at Wlog */
	qavg >>= p->Wlog;

	if (qavg > p->target_max && p->max_P <= MAX_P_MAX)
		p->max_P += MAX_P_ALPHA(p->max_P); /* maxp = maxp + alpha */
	else if (qavg < p->target_min && p->max_P >= MAX_P_MIN)
		p->max_P = (p->max_P/10)*9; /* maxp = maxp * Beta */

	max_p_delta = DIV_ROUND_CLOSEST(p->max_P, p->qth_delta);
	max_p_delta = max(max_p_delta, 1U);
	p->max_P_reciprocal = reciprocal_value(max_p_delta);
}
#endif
