/* SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB */
/* Copyright (c) 2019 Mellanox Technologies. */

#ifndef DIM_H
#define DIM_H

#include <linux/module.h>

#define DIM_NEVENTS 64

/* more than 10% difference */
#define IS_SIGNIFICANT_DIFF(val, ref) \
	(((100UL * abs((val) - (ref))) / (ref)) > 10)
#define BIT_GAP(bits, end, start) ((((end) - (start)) + BIT_ULL(bits)) \
& (BIT_ULL(bits) - 1))

struct net_dim_cq_moder {
	u16 usec;
	u16 pkts;
	u8 cq_period_mode;
};

struct net_dim_sample {
	ktime_t time;
	u32 pkt_ctr;
	u32 byte_ctr;
	u16 event_ctr;
};

struct dim_stats {
	int ppms; /* packets per msec */
	int bpms; /* bytes per msec */
	int epms; /* events per msec */
};

struct net_dim { /* Dynamic Interrupt Moderation */
	u8 state;
	struct dim_stats prev_stats;
	struct net_dim_sample start_sample;
	struct work_struct work;
	u8 profile_ix;
	u8 mode;
	u8 tune_state;
	u8 steps_right;
	u8 steps_left;
	u8 tired;
};

enum {
	DIM_CQ_PERIOD_MODE_START_FROM_EQE = 0x0,
	DIM_CQ_PERIOD_MODE_START_FROM_CQE = 0x1,
	DIM_CQ_PERIOD_NUM_MODES
};

enum {
	DIM_START_MEASURE,
	DIM_MEASURE_IN_PROGRESS,
	DIM_APPLY_NEW_PROFILE,
};

enum {
	DIM_PARKING_ON_TOP,
	DIM_PARKING_TIRED,
	DIM_GOING_RIGHT,
	DIM_GOING_LEFT,
};

enum {
	DIM_STATS_WORSE,
	DIM_STATS_SAME,
	DIM_STATS_BETTER,
};

enum {
	DIM_STEPPED,
	DIM_TOO_TIRED,
	DIM_ON_EDGE,
};

static inline bool dim_on_top(struct net_dim *dim)
{
	switch (dim->tune_state) {
	case DIM_PARKING_ON_TOP:
	case DIM_PARKING_TIRED:
		return true;
	case DIM_GOING_RIGHT:
		return (dim->steps_left > 1) && (dim->steps_right == 1);
	default: /* DIM_GOING_LEFT */
		return (dim->steps_right > 1) && (dim->steps_left == 1);
	}
}

static inline void dim_turn(struct net_dim *dim)
{
	switch (dim->tune_state) {
	case DIM_PARKING_ON_TOP:
	case DIM_PARKING_TIRED:
		break;
	case DIM_GOING_RIGHT:
		dim->tune_state = DIM_GOING_LEFT;
		dim->steps_left = 0;
		break;
	case DIM_GOING_LEFT:
		dim->tune_state = DIM_GOING_RIGHT;
		dim->steps_right = 0;
		break;
	}
}

static inline void dim_park_on_top(struct net_dim *dim)
{
	dim->steps_right  = 0;
	dim->steps_left   = 0;
	dim->tired        = 0;
	dim->tune_state   = DIM_PARKING_ON_TOP;
}

static inline void dim_park_tired(struct net_dim *dim)
{
	dim->steps_right  = 0;
	dim->steps_left   = 0;
	dim->tune_state   = DIM_PARKING_TIRED;
}

static inline void
net_dim_sample(u16 event_ctr, u64 packets, u64 bytes, struct net_dim_sample *s)
{
	s->time	     = ktime_get();
	s->pkt_ctr   = packets;
	s->byte_ctr  = bytes;
	s->event_ctr = event_ctr;
}

static inline void
dim_calc_stats(struct net_dim_sample *start, struct net_dim_sample *end,
	       struct dim_stats *curr_stats)
{
	/* u32 holds up to 71 minutes, should be enough */
	u32 delta_us = ktime_us_delta(end->time, start->time);
	u32 npkts = BIT_GAP(BITS_PER_TYPE(u32), end->pkt_ctr, start->pkt_ctr);
	u32 nbytes = BIT_GAP(BITS_PER_TYPE(u32), end->byte_ctr,
			     start->byte_ctr);

	if (!delta_us)
		return;

	curr_stats->ppms = DIV_ROUND_UP(npkts * USEC_PER_MSEC, delta_us);
	curr_stats->bpms = DIV_ROUND_UP(nbytes * USEC_PER_MSEC, delta_us);
	curr_stats->epms = DIV_ROUND_UP(DIM_NEVENTS * USEC_PER_MSEC,
					delta_us);
}

#endif /* DIM_H */
