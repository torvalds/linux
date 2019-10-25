/* SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB */
/* Copyright (c) 2019 Mellanox Technologies. */

#ifndef DIM_H
#define DIM_H

#include <linux/module.h>

/**
 * Number of events between DIM iterations.
 * Causes a moderation of the algorithm run.
 */
#define DIM_NEVENTS 64

/**
 * Is a difference between values justifies taking an action.
 * We consider 10% difference as significant.
 */
#define IS_SIGNIFICANT_DIFF(val, ref) \
	(((100UL * abs((val) - (ref))) / (ref)) > 10)

/**
 * Calculate the gap between two values.
 * Take wrap-around and variable size into consideration.
 */
#define BIT_GAP(bits, end, start) ((((end) - (start)) + BIT_ULL(bits)) \
		& (BIT_ULL(bits) - 1))

/**
 * Structure for CQ moderation values.
 * Used for communications between DIM and its consumer.
 *
 * @usec: CQ timer suggestion (by DIM)
 * @pkts: CQ packet counter suggestion (by DIM)
 * @cq_period_mode: CQ priod count mode (from CQE/EQE)
 */
struct dim_cq_moder {
	u16 usec;
	u16 pkts;
	u16 comps;
	u8 cq_period_mode;
};

/**
 * Structure for DIM sample data.
 * Used for communications between DIM and its consumer.
 *
 * @time: Sample timestamp
 * @pkt_ctr: Number of packets
 * @byte_ctr: Number of bytes
 * @event_ctr: Number of events
 */
struct dim_sample {
	ktime_t time;
	u32 pkt_ctr;
	u32 byte_ctr;
	u16 event_ctr;
	u32 comp_ctr;
};

/**
 * Structure for DIM stats.
 * Used for holding current measured rates.
 *
 * @ppms: Packets per msec
 * @bpms: Bytes per msec
 * @epms: Events per msec
 */
struct dim_stats {
	int ppms; /* packets per msec */
	int bpms; /* bytes per msec */
	int epms; /* events per msec */
	int cpms; /* completions per msec */
	int cpe_ratio; /* ratio of completions to events */
};

/**
 * Main structure for dynamic interrupt moderation (DIM).
 * Used for holding all information about a specific DIM instance.
 *
 * @state: Algorithm state (see below)
 * @prev_stats: Measured rates from previous iteration (for comparison)
 * @start_sample: Sampled data at start of current iteration
 * @work: Work to perform on action required
 * @priv: A pointer to the struct that points to dim
 * @profile_ix: Current moderation profile
 * @mode: CQ period count mode
 * @tune_state: Algorithm tuning state (see below)
 * @steps_right: Number of steps taken towards higher moderation
 * @steps_left: Number of steps taken towards lower moderation
 * @tired: Parking depth counter
 */
struct dim {
	u8 state;
	struct dim_stats prev_stats;
	struct dim_sample start_sample;
	struct dim_sample measuring_sample;
	struct work_struct work;
	void *priv;
	u8 profile_ix;
	u8 mode;
	u8 tune_state;
	u8 steps_right;
	u8 steps_left;
	u8 tired;
};

/**
 * enum dim_cq_period_mode
 *
 * These are the modes for CQ period count.
 *
 * @DIM_CQ_PERIOD_MODE_START_FROM_EQE: Start counting from EQE
 * @DIM_CQ_PERIOD_MODE_START_FROM_CQE: Start counting from CQE (implies timer reset)
 * @DIM_CQ_PERIOD_NUM_MODES: Number of modes
 */
enum {
	DIM_CQ_PERIOD_MODE_START_FROM_EQE = 0x0,
	DIM_CQ_PERIOD_MODE_START_FROM_CQE = 0x1,
	DIM_CQ_PERIOD_NUM_MODES
};

/**
 * enum dim_state
 *
 * These are the DIM algorithm states.
 * These will determine if the algorithm is in a valid state to start an iteration.
 *
 * @DIM_START_MEASURE: This is the first iteration (also after applying a new profile)
 * @DIM_MEASURE_IN_PROGRESS: Algorithm is already in progress - check if
 * need to perform an action
 * @DIM_APPLY_NEW_PROFILE: DIM consumer is currently applying a profile - no need to measure
 */
enum {
	DIM_START_MEASURE,
	DIM_MEASURE_IN_PROGRESS,
	DIM_APPLY_NEW_PROFILE,
};

/**
 * enum dim_tune_state
 *
 * These are the DIM algorithm tune states.
 * These will determine which action the algorithm should perform.
 *
 * @DIM_PARKING_ON_TOP: Algorithm found a local top point - exit on significant difference
 * @DIM_PARKING_TIRED: Algorithm found a deep top point - don't exit if tired > 0
 * @DIM_GOING_RIGHT: Algorithm is currently trying higher moderation levels
 * @DIM_GOING_LEFT: Algorithm is currently trying lower moderation levels
 */
enum {
	DIM_PARKING_ON_TOP,
	DIM_PARKING_TIRED,
	DIM_GOING_RIGHT,
	DIM_GOING_LEFT,
};

/**
 * enum dim_stats_state
 *
 * These are the DIM algorithm statistics states.
 * These will determine the verdict of current iteration.
 *
 * @DIM_STATS_WORSE: Current iteration shows worse performance than before
 * @DIM_STATS_WORSE: Current iteration shows same performance than before
 * @DIM_STATS_WORSE: Current iteration shows better performance than before
 */
enum {
	DIM_STATS_WORSE,
	DIM_STATS_SAME,
	DIM_STATS_BETTER,
};

/**
 * enum dim_step_result
 *
 * These are the DIM algorithm step results.
 * These describe the result of a step.
 *
 * @DIM_STEPPED: Performed a regular step
 * @DIM_TOO_TIRED: Same kind of step was done multiple times - should go to
 * tired parking
 * @DIM_ON_EDGE: Stepped to the most left/right profile
 */
enum {
	DIM_STEPPED,
	DIM_TOO_TIRED,
	DIM_ON_EDGE,
};

/**
 *	dim_on_top - check if current state is a good place to stop (top location)
 *	@dim: DIM context
 *
 * Check if current profile is a good place to park at.
 * This will result in reducing the DIM checks frequency as we assume we
 * shouldn't probably change profiles, unless traffic pattern wasn't changed.
 */
bool dim_on_top(struct dim *dim);

/**
 *	dim_turn - change profile alterning direction
 *	@dim: DIM context
 *
 * Go left if we were going right and vice-versa.
 * Do nothing if currently parking.
 */
void dim_turn(struct dim *dim);

/**
 *	dim_park_on_top - enter a parking state on a top location
 *	@dim: DIM context
 *
 * Enter parking state.
 * Clear all movement history.
 */
void dim_park_on_top(struct dim *dim);

/**
 *	dim_park_tired - enter a tired parking state
 *	@dim: DIM context
 *
 * Enter parking state.
 * Clear all movement history and cause DIM checks frequency to reduce.
 */
void dim_park_tired(struct dim *dim);

/**
 *	dim_calc_stats - calculate the difference between two samples
 *	@start: start sample
 *	@end: end sample
 *	@curr_stats: delta between samples
 *
 * Calculate the delta between two samples (in data rates).
 * Takes into consideration counter wrap-around.
 */
void dim_calc_stats(struct dim_sample *start, struct dim_sample *end,
		    struct dim_stats *curr_stats);

/**
 *	dim_update_sample - set a sample's fields with give values
 *	@event_ctr: number of events to set
 *	@packets: number of packets to set
 *	@bytes: number of bytes to set
 *	@s: DIM sample
 */
static inline void
dim_update_sample(u16 event_ctr, u64 packets, u64 bytes, struct dim_sample *s)
{
	s->time	     = ktime_get();
	s->pkt_ctr   = packets;
	s->byte_ctr  = bytes;
	s->event_ctr = event_ctr;
}

/**
 *	dim_update_sample_with_comps - set a sample's fields with given
 *	values including the completion parameter
 *	@event_ctr: number of events to set
 *	@packets: number of packets to set
 *	@bytes: number of bytes to set
 *	@comps: number of completions to set
 *	@s: DIM sample
 */
static inline void
dim_update_sample_with_comps(u16 event_ctr, u64 packets, u64 bytes, u64 comps,
			     struct dim_sample *s)
{
	dim_update_sample(event_ctr, packets, bytes, s);
	s->comp_ctr = comps;
}

/* Net DIM */

/**
 *	net_dim_get_rx_moderation - provide a CQ moderation object for the given RX profile
 *	@cq_period_mode: CQ period mode
 *	@ix: Profile index
 */
struct dim_cq_moder net_dim_get_rx_moderation(u8 cq_period_mode, int ix);

/**
 *	net_dim_get_def_rx_moderation - provide the default RX moderation
 *	@cq_period_mode: CQ period mode
 */
struct dim_cq_moder net_dim_get_def_rx_moderation(u8 cq_period_mode);

/**
 *	net_dim_get_tx_moderation - provide a CQ moderation object for the given TX profile
 *	@cq_period_mode: CQ period mode
 *	@ix: Profile index
 */
struct dim_cq_moder net_dim_get_tx_moderation(u8 cq_period_mode, int ix);

/**
 *	net_dim_get_def_tx_moderation - provide the default TX moderation
 *	@cq_period_mode: CQ period mode
 */
struct dim_cq_moder net_dim_get_def_tx_moderation(u8 cq_period_mode);

/**
 *	net_dim - main DIM algorithm entry point
 *	@dim: DIM instance information
 *	@end_sample: Current data measurement
 *
 * Called by the consumer.
 * This is the main logic of the algorithm, where data is processed in order to decide on next
 * required action.
 */
void net_dim(struct dim *dim, struct dim_sample end_sample);

/* RDMA DIM */

/*
 * RDMA DIM profile:
 * profile size must be of RDMA_DIM_PARAMS_NUM_PROFILES.
 */
#define RDMA_DIM_PARAMS_NUM_PROFILES 9
#define RDMA_DIM_START_PROFILE 0

/**
 * rdma_dim - Runs the adaptive moderation.
 * @dim: The moderation struct.
 * @completions: The number of completions collected in this round.
 *
 * Each call to rdma_dim takes the latest amount of completions that
 * have been collected and counts them as a new event.
 * Once enough events have been collected the algorithm decides a new
 * moderation level.
 */
void rdma_dim(struct dim *dim, u64 completions);

#endif /* DIM_H */
