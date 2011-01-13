#ifndef __NET_SCHED_RED_H
#define __NET_SCHED_RED_H

#include <linux/types.h>
#include <net/pkt_sched.h>
#include <net/inet_ecn.h>
#include <net/dsfield.h>

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

#define RED_STAB_SIZE	256
#define RED_STAB_MASK	(RED_STAB_SIZE - 1)

struct red_stats {
	u32		prob_drop;	/* Early probability drops */
	u32		prob_mark;	/* Early probability marks */
	u32		forced_drop;	/* Forced drops, qavg > max_thresh */
	u32		forced_mark;	/* Forced marks, qavg > max_thresh */
	u32		pdrop;          /* Drops due to queue limits */
	u32		other;          /* Drops due to drop() calls */
	u32		backlog;
};

struct red_parms {
	/* Parameters */
	u32		qth_min;	/* Min avg length threshold: A scaled */
	u32		qth_max;	/* Max avg length threshold: A scaled */
	u32		Scell_max;
	u32		Rmask;		/* Cached random mask, see red_rmask */
	u8		Scell_log;
	u8		Wlog;		/* log(W)		*/
	u8		Plog;		/* random number bits	*/
	u8		Stab[RED_STAB_SIZE];

	/* Variables */
	int		qcount;		/* Number of packets since last random
					   number generation */
	u32		qR;		/* Cached random number */

	unsigned long	qavg;		/* Average queue length: A scaled */
	psched_time_t	qidlestart;	/* Start of current idle period */
};

static inline u32 red_rmask(u8 Plog)
{
	return Plog < 32 ? ((1 << Plog) - 1) : ~0UL;
}

static inline void red_set_parms(struct red_parms *p,
				 u32 qth_min, u32 qth_max, u8 Wlog, u8 Plog,
				 u8 Scell_log, u8 *stab)
{
	/* Reset average queue length, the value is strictly bound
	 * to the parameters below, reseting hurts a bit but leaving
	 * it might result in an unreasonable qavg for a while. --TGR
	 */
	p->qavg		= 0;

	p->qcount	= -1;
	p->qth_min	= qth_min << Wlog;
	p->qth_max	= qth_max << Wlog;
	p->Wlog		= Wlog;
	p->Plog		= Plog;
	p->Rmask	= red_rmask(Plog);
	p->Scell_log	= Scell_log;
	p->Scell_max	= (255 << Scell_log);

	memcpy(p->Stab, stab, sizeof(p->Stab));
}

static inline int red_is_idling(struct red_parms *p)
{
	return p->qidlestart != PSCHED_PASTPERFECT;
}

static inline void red_start_of_idle_period(struct red_parms *p)
{
	p->qidlestart = psched_get_time();
}

static inline void red_end_of_idle_period(struct red_parms *p)
{
	p->qidlestart = PSCHED_PASTPERFECT;
}

static inline void red_restart(struct red_parms *p)
{
	red_end_of_idle_period(p);
	p->qavg = 0;
	p->qcount = -1;
}

static inline unsigned long red_calc_qavg_from_idle_time(struct red_parms *p)
{
	psched_time_t now;
	long us_idle;
	int  shift;

	now = psched_get_time();
	us_idle = psched_tdiff_bounded(now, p->qidlestart, p->Scell_max);

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
	 * 	p->qavg *= (1-W)^m
	 *
	 * This is an apparently overcomplicated solution (f.e. we have to
	 * precompute a table to make this calculation in reasonable time)
	 * I believe that a simpler model may be used here,
	 * but it is field for experiments.
	 */

	shift = p->Stab[(us_idle >> p->Scell_log) & RED_STAB_MASK];

	if (shift)
		return p->qavg >> shift;
	else {
		/* Approximate initial part of exponent with linear function:
		 *
		 * 	(1-W)^m ~= 1-mW + ...
		 *
		 * Seems, it is the best solution to
		 * problem of too coarse exponent tabulation.
		 */
		us_idle = (p->qavg * (u64)us_idle) >> p->Scell_log;

		if (us_idle < (p->qavg >> 1))
			return p->qavg - us_idle;
		else
			return p->qavg >> 1;
	}
}

static inline unsigned long red_calc_qavg_no_idle_time(struct red_parms *p,
						       unsigned int backlog)
{
	/*
	 * NOTE: p->qavg is fixed point number with point at Wlog.
	 * The formula below is equvalent to floating point
	 * version:
	 *
	 * 	qavg = qavg*(1-W) + backlog*W;
	 *
	 * --ANK (980924)
	 */
	return p->qavg + (backlog - (p->qavg >> p->Wlog));
}

static inline unsigned long red_calc_qavg(struct red_parms *p,
					  unsigned int backlog)
{
	if (!red_is_idling(p))
		return red_calc_qavg_no_idle_time(p, backlog);
	else
		return red_calc_qavg_from_idle_time(p);
}

static inline u32 red_random(struct red_parms *p)
{
	return net_random() & p->Rmask;
}

static inline int red_mark_probability(struct red_parms *p, unsigned long qavg)
{
	/* The formula used below causes questions.

	   OK. qR is random number in the interval 0..Rmask
	   i.e. 0..(2^Plog). If we used floating point
	   arithmetics, it would be: (2^Plog)*rnd_num,
	   where rnd_num is less 1.

	   Taking into account, that qavg have fixed
	   point at Wlog, and Plog is related to max_P by
	   max_P = (qth_max-qth_min)/2^Plog; two lines
	   below have the following floating point equivalent:

	   max_P*(qavg - qth_min)/(qth_max-qth_min) < rnd/qcount

	   Any questions? --ANK (980924)
	 */
	return !(((qavg - p->qth_min) >> p->Wlog) * p->qcount < p->qR);
}

enum {
	RED_BELOW_MIN_THRESH,
	RED_BETWEEN_TRESH,
	RED_ABOVE_MAX_TRESH,
};

static inline int red_cmp_thresh(struct red_parms *p, unsigned long qavg)
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

static inline int red_action(struct red_parms *p, unsigned long qavg)
{
	switch (red_cmp_thresh(p, qavg)) {
		case RED_BELOW_MIN_THRESH:
			p->qcount = -1;
			return RED_DONT_MARK;

		case RED_BETWEEN_TRESH:
			if (++p->qcount) {
				if (red_mark_probability(p, qavg)) {
					p->qcount = 0;
					p->qR = red_random(p);
					return RED_PROB_MARK;
				}
			} else
				p->qR = red_random(p);

			return RED_DONT_MARK;

		case RED_ABOVE_MAX_TRESH:
			p->qcount = -1;
			return RED_HARD_MARK;
	}

	BUG();
	return RED_DONT_MARK;
}

#endif
