/*
 * Copyright (c) 2016, Mellanox Technologies. All rights reserved.
 * Copyright (c) 2017-2018, Broadcom Limited. All rights reserved.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * OpenIB.org BSD license below:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and/or other materials
 *        provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#ifndef NET_DIM_H
#define NET_DIM_H

#include <linux/module.h>
#include <linux/dim.h>

#define NET_DIM_PARAMS_NUM_PROFILES 5
/* Netdev dynamic interrupt moderation profiles */
#define NET_DIM_DEFAULT_RX_CQ_MODERATION_PKTS_FROM_EQE 256
#define NET_DIM_DEFAULT_TX_CQ_MODERATION_PKTS_FROM_EQE 128
#define NET_DIM_DEF_PROFILE_CQE 1
#define NET_DIM_DEF_PROFILE_EQE 1

/* All profiles sizes must be NET_PARAMS_DIM_NUM_PROFILES */
#define NET_DIM_RX_EQE_PROFILES { \
	{1,   NET_DIM_DEFAULT_RX_CQ_MODERATION_PKTS_FROM_EQE}, \
	{8,   NET_DIM_DEFAULT_RX_CQ_MODERATION_PKTS_FROM_EQE}, \
	{64,  NET_DIM_DEFAULT_RX_CQ_MODERATION_PKTS_FROM_EQE}, \
	{128, NET_DIM_DEFAULT_RX_CQ_MODERATION_PKTS_FROM_EQE}, \
	{256, NET_DIM_DEFAULT_RX_CQ_MODERATION_PKTS_FROM_EQE}, \
}

#define NET_DIM_RX_CQE_PROFILES { \
	{2,  256},             \
	{8,  128},             \
	{16, 64},              \
	{32, 64},              \
	{64, 64}               \
}

#define NET_DIM_TX_EQE_PROFILES { \
	{1,   NET_DIM_DEFAULT_TX_CQ_MODERATION_PKTS_FROM_EQE},  \
	{8,   NET_DIM_DEFAULT_TX_CQ_MODERATION_PKTS_FROM_EQE},  \
	{32,  NET_DIM_DEFAULT_TX_CQ_MODERATION_PKTS_FROM_EQE},  \
	{64,  NET_DIM_DEFAULT_TX_CQ_MODERATION_PKTS_FROM_EQE},  \
	{128, NET_DIM_DEFAULT_TX_CQ_MODERATION_PKTS_FROM_EQE}   \
}

#define NET_DIM_TX_CQE_PROFILES { \
	{5,  128},  \
	{8,  64},  \
	{16, 32},  \
	{32, 32},  \
	{64, 32}   \
}

static const struct net_dim_cq_moder
rx_profile[NET_DIM_CQ_PERIOD_NUM_MODES][NET_DIM_PARAMS_NUM_PROFILES] = {
	NET_DIM_RX_EQE_PROFILES,
	NET_DIM_RX_CQE_PROFILES,
};

static const struct net_dim_cq_moder
tx_profile[NET_DIM_CQ_PERIOD_NUM_MODES][NET_DIM_PARAMS_NUM_PROFILES] = {
	NET_DIM_TX_EQE_PROFILES,
	NET_DIM_TX_CQE_PROFILES,
};

static inline struct net_dim_cq_moder
net_dim_get_rx_moderation(u8 cq_period_mode, int ix)
{
	struct net_dim_cq_moder cq_moder = rx_profile[cq_period_mode][ix];

	cq_moder.cq_period_mode = cq_period_mode;
	return cq_moder;
}

static inline struct net_dim_cq_moder
net_dim_get_def_rx_moderation(u8 cq_period_mode)
{
	u8 profile_ix = cq_period_mode == NET_DIM_CQ_PERIOD_MODE_START_FROM_CQE ?
			NET_DIM_DEF_PROFILE_CQE : NET_DIM_DEF_PROFILE_EQE;

	return net_dim_get_rx_moderation(cq_period_mode, profile_ix);
}

static inline struct net_dim_cq_moder
net_dim_get_tx_moderation(u8 cq_period_mode, int ix)
{
	struct net_dim_cq_moder cq_moder = tx_profile[cq_period_mode][ix];

	cq_moder.cq_period_mode = cq_period_mode;
	return cq_moder;
}

static inline struct net_dim_cq_moder
net_dim_get_def_tx_moderation(u8 cq_period_mode)
{
	u8 profile_ix = cq_period_mode == NET_DIM_CQ_PERIOD_MODE_START_FROM_CQE ?
			NET_DIM_DEF_PROFILE_CQE : NET_DIM_DEF_PROFILE_EQE;

	return net_dim_get_tx_moderation(cq_period_mode, profile_ix);
}

static inline int net_dim_step(struct net_dim *dim)
{
	if (dim->tired == (NET_DIM_PARAMS_NUM_PROFILES * 2))
		return NET_DIM_TOO_TIRED;

	switch (dim->tune_state) {
	case NET_DIM_PARKING_ON_TOP:
	case NET_DIM_PARKING_TIRED:
		break;
	case NET_DIM_GOING_RIGHT:
		if (dim->profile_ix == (NET_DIM_PARAMS_NUM_PROFILES - 1))
			return NET_DIM_ON_EDGE;
		dim->profile_ix++;
		dim->steps_right++;
		break;
	case NET_DIM_GOING_LEFT:
		if (dim->profile_ix == 0)
			return NET_DIM_ON_EDGE;
		dim->profile_ix--;
		dim->steps_left++;
		break;
	}

	dim->tired++;
	return NET_DIM_STEPPED;
}

static inline void net_dim_exit_parking(struct net_dim *dim)
{
	dim->tune_state = dim->profile_ix ? NET_DIM_GOING_LEFT :
					  NET_DIM_GOING_RIGHT;
	net_dim_step(dim);
}

static inline int net_dim_stats_compare(struct net_dim_stats *curr,
					struct net_dim_stats *prev)
{
	if (!prev->bpms)
		return curr->bpms ? NET_DIM_STATS_BETTER :
				    NET_DIM_STATS_SAME;

	if (IS_SIGNIFICANT_DIFF(curr->bpms, prev->bpms))
		return (curr->bpms > prev->bpms) ? NET_DIM_STATS_BETTER :
						   NET_DIM_STATS_WORSE;

	if (!prev->ppms)
		return curr->ppms ? NET_DIM_STATS_BETTER :
				    NET_DIM_STATS_SAME;

	if (IS_SIGNIFICANT_DIFF(curr->ppms, prev->ppms))
		return (curr->ppms > prev->ppms) ? NET_DIM_STATS_BETTER :
						   NET_DIM_STATS_WORSE;

	if (!prev->epms)
		return NET_DIM_STATS_SAME;

	if (IS_SIGNIFICANT_DIFF(curr->epms, prev->epms))
		return (curr->epms < prev->epms) ? NET_DIM_STATS_BETTER :
						   NET_DIM_STATS_WORSE;

	return NET_DIM_STATS_SAME;
}

static inline bool net_dim_decision(struct net_dim_stats *curr_stats,
				    struct net_dim *dim)
{
	int prev_state = dim->tune_state;
	int prev_ix = dim->profile_ix;
	int stats_res;
	int step_res;

	switch (dim->tune_state) {
	case NET_DIM_PARKING_ON_TOP:
		stats_res = net_dim_stats_compare(curr_stats, &dim->prev_stats);
		if (stats_res != NET_DIM_STATS_SAME)
			net_dim_exit_parking(dim);
		break;

	case NET_DIM_PARKING_TIRED:
		dim->tired--;
		if (!dim->tired)
			net_dim_exit_parking(dim);
		break;

	case NET_DIM_GOING_RIGHT:
	case NET_DIM_GOING_LEFT:
		stats_res = net_dim_stats_compare(curr_stats, &dim->prev_stats);
		if (stats_res != NET_DIM_STATS_BETTER)
			net_dim_turn(dim);

		if (net_dim_on_top(dim)) {
			net_dim_park_on_top(dim);
			break;
		}

		step_res = net_dim_step(dim);
		switch (step_res) {
		case NET_DIM_ON_EDGE:
			net_dim_park_on_top(dim);
			break;
		case NET_DIM_TOO_TIRED:
			net_dim_park_tired(dim);
			break;
		}

		break;
	}

	if ((prev_state      != NET_DIM_PARKING_ON_TOP) ||
	    (dim->tune_state != NET_DIM_PARKING_ON_TOP))
		dim->prev_stats = *curr_stats;

	return dim->profile_ix != prev_ix;
}

static inline void net_dim(struct net_dim *dim,
			   struct net_dim_sample end_sample)
{
	struct net_dim_stats curr_stats;
	u16 nevents;

	switch (dim->state) {
	case NET_DIM_MEASURE_IN_PROGRESS:
		nevents = BIT_GAP(BITS_PER_TYPE(u16),
				  end_sample.event_ctr,
				  dim->start_sample.event_ctr);
		if (nevents < NET_DIM_NEVENTS)
			break;
		net_dim_calc_stats(&dim->start_sample, &end_sample,
				   &curr_stats);
		if (net_dim_decision(&curr_stats, dim)) {
			dim->state = NET_DIM_APPLY_NEW_PROFILE;
			schedule_work(&dim->work);
			break;
		}
		/* fall through */
	case NET_DIM_START_MEASURE:
		net_dim_sample(end_sample.event_ctr, end_sample.pkt_ctr, end_sample.byte_ctr,
			       &dim->start_sample);
		dim->state = NET_DIM_MEASURE_IN_PROGRESS;
		break;
	case NET_DIM_APPLY_NEW_PROFILE:
		break;
	}
}

#endif /* NET_DIM_H */
