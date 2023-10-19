// SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB
/*
 * Copyright (c) 2018, Mellanox Technologies inc.  All rights reserved.
 */

#include <linux/dim.h>

/*
 * Net DIM profiles:
 *        There are different set of profiles for each CQ period mode.
 *        There are different set of profiles for RX/TX CQs.
 *        Each profile size must be of NET_DIM_PARAMS_NUM_PROFILES
 */
#define NET_DIM_PARAMS_NUM_PROFILES 5
#define NET_DIM_DEFAULT_RX_CQ_PKTS_FROM_EQE 256
#define NET_DIM_DEFAULT_TX_CQ_PKTS_FROM_EQE 128
#define NET_DIM_DEF_PROFILE_CQE 1
#define NET_DIM_DEF_PROFILE_EQE 1

#define NET_DIM_RX_EQE_PROFILES { \
	{.usec = 1,   .pkts = NET_DIM_DEFAULT_RX_CQ_PKTS_FROM_EQE,}, \
	{.usec = 8,   .pkts = NET_DIM_DEFAULT_RX_CQ_PKTS_FROM_EQE,}, \
	{.usec = 64,  .pkts = NET_DIM_DEFAULT_RX_CQ_PKTS_FROM_EQE,}, \
	{.usec = 128, .pkts = NET_DIM_DEFAULT_RX_CQ_PKTS_FROM_EQE,}, \
	{.usec = 256, .pkts = NET_DIM_DEFAULT_RX_CQ_PKTS_FROM_EQE,}  \
}

#define NET_DIM_RX_CQE_PROFILES { \
	{.usec = 2,  .pkts = 256,},             \
	{.usec = 8,  .pkts = 128,},             \
	{.usec = 16, .pkts = 64,},              \
	{.usec = 32, .pkts = 64,},              \
	{.usec = 64, .pkts = 64,}               \
}

#define NET_DIM_TX_EQE_PROFILES { \
	{.usec = 1,   .pkts = NET_DIM_DEFAULT_TX_CQ_PKTS_FROM_EQE,},  \
	{.usec = 8,   .pkts = NET_DIM_DEFAULT_TX_CQ_PKTS_FROM_EQE,},  \
	{.usec = 32,  .pkts = NET_DIM_DEFAULT_TX_CQ_PKTS_FROM_EQE,},  \
	{.usec = 64,  .pkts = NET_DIM_DEFAULT_TX_CQ_PKTS_FROM_EQE,},  \
	{.usec = 128, .pkts = NET_DIM_DEFAULT_TX_CQ_PKTS_FROM_EQE,}   \
}

#define NET_DIM_TX_CQE_PROFILES { \
	{.usec = 5,  .pkts = 128,},  \
	{.usec = 8,  .pkts = 64,},  \
	{.usec = 16, .pkts = 32,},  \
	{.usec = 32, .pkts = 32,},  \
	{.usec = 64, .pkts = 32,}   \
}

static const struct dim_cq_moder
rx_profile[DIM_CQ_PERIOD_NUM_MODES][NET_DIM_PARAMS_NUM_PROFILES] = {
	NET_DIM_RX_EQE_PROFILES,
	NET_DIM_RX_CQE_PROFILES,
};

static const struct dim_cq_moder
tx_profile[DIM_CQ_PERIOD_NUM_MODES][NET_DIM_PARAMS_NUM_PROFILES] = {
	NET_DIM_TX_EQE_PROFILES,
	NET_DIM_TX_CQE_PROFILES,
};

struct dim_cq_moder
net_dim_get_rx_moderation(u8 cq_period_mode, int ix)
{
	struct dim_cq_moder cq_moder = rx_profile[cq_period_mode][ix];

	cq_moder.cq_period_mode = cq_period_mode;
	return cq_moder;
}
EXPORT_SYMBOL(net_dim_get_rx_moderation);

struct dim_cq_moder
net_dim_get_def_rx_moderation(u8 cq_period_mode)
{
	u8 profile_ix = cq_period_mode == DIM_CQ_PERIOD_MODE_START_FROM_CQE ?
			NET_DIM_DEF_PROFILE_CQE : NET_DIM_DEF_PROFILE_EQE;

	return net_dim_get_rx_moderation(cq_period_mode, profile_ix);
}
EXPORT_SYMBOL(net_dim_get_def_rx_moderation);

struct dim_cq_moder
net_dim_get_tx_moderation(u8 cq_period_mode, int ix)
{
	struct dim_cq_moder cq_moder = tx_profile[cq_period_mode][ix];

	cq_moder.cq_period_mode = cq_period_mode;
	return cq_moder;
}
EXPORT_SYMBOL(net_dim_get_tx_moderation);

struct dim_cq_moder
net_dim_get_def_tx_moderation(u8 cq_period_mode)
{
	u8 profile_ix = cq_period_mode == DIM_CQ_PERIOD_MODE_START_FROM_CQE ?
			NET_DIM_DEF_PROFILE_CQE : NET_DIM_DEF_PROFILE_EQE;

	return net_dim_get_tx_moderation(cq_period_mode, profile_ix);
}
EXPORT_SYMBOL(net_dim_get_def_tx_moderation);

static int net_dim_step(struct dim *dim)
{
	if (dim->tired == (NET_DIM_PARAMS_NUM_PROFILES * 2))
		return DIM_TOO_TIRED;

	switch (dim->tune_state) {
	case DIM_PARKING_ON_TOP:
	case DIM_PARKING_TIRED:
		break;
	case DIM_GOING_RIGHT:
		if (dim->profile_ix == (NET_DIM_PARAMS_NUM_PROFILES - 1))
			return DIM_ON_EDGE;
		dim->profile_ix++;
		dim->steps_right++;
		break;
	case DIM_GOING_LEFT:
		if (dim->profile_ix == 0)
			return DIM_ON_EDGE;
		dim->profile_ix--;
		dim->steps_left++;
		break;
	}

	dim->tired++;
	return DIM_STEPPED;
}

static void net_dim_exit_parking(struct dim *dim)
{
	dim->tune_state = dim->profile_ix ? DIM_GOING_LEFT : DIM_GOING_RIGHT;
	net_dim_step(dim);
}

static int net_dim_stats_compare(struct dim_stats *curr,
				 struct dim_stats *prev)
{
	if (!prev->bpms)
		return curr->bpms ? DIM_STATS_BETTER : DIM_STATS_SAME;

	if (IS_SIGNIFICANT_DIFF(curr->bpms, prev->bpms))
		return (curr->bpms > prev->bpms) ? DIM_STATS_BETTER :
						   DIM_STATS_WORSE;

	if (!prev->ppms)
		return curr->ppms ? DIM_STATS_BETTER :
				    DIM_STATS_SAME;

	if (IS_SIGNIFICANT_DIFF(curr->ppms, prev->ppms))
		return (curr->ppms > prev->ppms) ? DIM_STATS_BETTER :
						   DIM_STATS_WORSE;

	if (!prev->epms)
		return DIM_STATS_SAME;

	if (IS_SIGNIFICANT_DIFF(curr->epms, prev->epms))
		return (curr->epms < prev->epms) ? DIM_STATS_BETTER :
						   DIM_STATS_WORSE;

	return DIM_STATS_SAME;
}

static bool net_dim_decision(struct dim_stats *curr_stats, struct dim *dim)
{
	int prev_state = dim->tune_state;
	int prev_ix = dim->profile_ix;
	int stats_res;
	int step_res;

	switch (dim->tune_state) {
	case DIM_PARKING_ON_TOP:
		stats_res = net_dim_stats_compare(curr_stats,
						  &dim->prev_stats);
		if (stats_res != DIM_STATS_SAME)
			net_dim_exit_parking(dim);
		break;

	case DIM_PARKING_TIRED:
		dim->tired--;
		if (!dim->tired)
			net_dim_exit_parking(dim);
		break;

	case DIM_GOING_RIGHT:
	case DIM_GOING_LEFT:
		stats_res = net_dim_stats_compare(curr_stats,
						  &dim->prev_stats);
		if (stats_res != DIM_STATS_BETTER)
			dim_turn(dim);

		if (dim_on_top(dim)) {
			dim_park_on_top(dim);
			break;
		}

		step_res = net_dim_step(dim);
		switch (step_res) {
		case DIM_ON_EDGE:
			dim_park_on_top(dim);
			break;
		case DIM_TOO_TIRED:
			dim_park_tired(dim);
			break;
		}

		break;
	}

	if (prev_state != DIM_PARKING_ON_TOP ||
	    dim->tune_state != DIM_PARKING_ON_TOP)
		dim->prev_stats = *curr_stats;

	return dim->profile_ix != prev_ix;
}

void net_dim(struct dim *dim, struct dim_sample end_sample)
{
	struct dim_stats curr_stats;
	u16 nevents;

	switch (dim->state) {
	case DIM_MEASURE_IN_PROGRESS:
		nevents = BIT_GAP(BITS_PER_TYPE(u16),
				  end_sample.event_ctr,
				  dim->start_sample.event_ctr);
		if (nevents < DIM_NEVENTS)
			break;
		dim_calc_stats(&dim->start_sample, &end_sample, &curr_stats);
		if (net_dim_decision(&curr_stats, dim)) {
			dim->state = DIM_APPLY_NEW_PROFILE;
			schedule_work(&dim->work);
			break;
		}
		fallthrough;
	case DIM_START_MEASURE:
		dim_update_sample(end_sample.event_ctr, end_sample.pkt_ctr,
				  end_sample.byte_ctr, &dim->start_sample);
		dim->state = DIM_MEASURE_IN_PROGRESS;
		break;
	case DIM_APPLY_NEW_PROFILE:
		break;
	}
}
EXPORT_SYMBOL(net_dim);
