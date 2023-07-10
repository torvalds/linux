// SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB
/*
 * Copyright (c) 2019, Mellanox Technologies inc.  All rights reserved.
 */

#include <linux/dim.h>

static int rdma_dim_step(struct dim *dim)
{
	if (dim->tune_state == DIM_GOING_RIGHT) {
		if (dim->profile_ix == (RDMA_DIM_PARAMS_NUM_PROFILES - 1))
			return DIM_ON_EDGE;
		dim->profile_ix++;
		dim->steps_right++;
	}
	if (dim->tune_state == DIM_GOING_LEFT) {
		if (dim->profile_ix == 0)
			return DIM_ON_EDGE;
		dim->profile_ix--;
		dim->steps_left++;
	}

	return DIM_STEPPED;
}

static int rdma_dim_stats_compare(struct dim_stats *curr,
				  struct dim_stats *prev)
{
	/* first stat */
	if (!prev->cpms)
		return DIM_STATS_SAME;

	if (IS_SIGNIFICANT_DIFF(curr->cpms, prev->cpms))
		return (curr->cpms > prev->cpms) ? DIM_STATS_BETTER :
						DIM_STATS_WORSE;

	if (IS_SIGNIFICANT_DIFF(curr->cpe_ratio, prev->cpe_ratio))
		return (curr->cpe_ratio > prev->cpe_ratio) ? DIM_STATS_BETTER :
						DIM_STATS_WORSE;

	return DIM_STATS_SAME;
}

static bool rdma_dim_decision(struct dim_stats *curr_stats, struct dim *dim)
{
	int prev_ix = dim->profile_ix;
	u8 state = dim->tune_state;
	int stats_res;
	int step_res;

	if (state != DIM_PARKING_ON_TOP && state != DIM_PARKING_TIRED) {
		stats_res = rdma_dim_stats_compare(curr_stats,
						   &dim->prev_stats);

		switch (stats_res) {
		case DIM_STATS_SAME:
			if (curr_stats->cpe_ratio <= 50 * prev_ix)
				dim->profile_ix = 0;
			break;
		case DIM_STATS_WORSE:
			dim_turn(dim);
			/* fall through */
		case DIM_STATS_BETTER:
			step_res = rdma_dim_step(dim);
			if (step_res == DIM_ON_EDGE)
				dim_turn(dim);
			break;
		}
	}

	dim->prev_stats = *curr_stats;

	return dim->profile_ix != prev_ix;
}

void rdma_dim(struct dim *dim, u64 completions)
{
	struct dim_sample *curr_sample = &dim->measuring_sample;
	struct dim_stats curr_stats;
	u32 nevents;

	dim_update_sample_with_comps(curr_sample->event_ctr + 1, 0, 0,
				     curr_sample->comp_ctr + completions,
				     &dim->measuring_sample);

	switch (dim->state) {
	case DIM_MEASURE_IN_PROGRESS:
		nevents = curr_sample->event_ctr - dim->start_sample.event_ctr;
		if (nevents < DIM_NEVENTS)
			break;
		if (!dim_calc_stats(&dim->start_sample, curr_sample, &curr_stats))
			break;
		if (rdma_dim_decision(&curr_stats, dim)) {
			dim->state = DIM_APPLY_NEW_PROFILE;
			schedule_work(&dim->work);
			break;
		}
		/* fall through */
	case DIM_START_MEASURE:
		dim->state = DIM_MEASURE_IN_PROGRESS;
		dim_update_sample_with_comps(curr_sample->event_ctr, 0, 0,
					     curr_sample->comp_ctr,
					     &dim->start_sample);
		break;
	case DIM_APPLY_NEW_PROFILE:
		break;
	}
}
EXPORT_SYMBOL(rdma_dim);
