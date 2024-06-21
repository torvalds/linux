/* SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB */
/* Copyright (c) 2019 Mellanox Technologies. */

#ifndef DIM_H
#define DIM_H

#include <linux/bits.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/types.h>
#include <linux/workqueue.h>

struct net_device;

/* Number of DIM profiles and period mode. */
#define NET_DIM_PARAMS_NUM_PROFILES 5
#define NET_DIM_DEFAULT_RX_CQ_PKTS_FROM_EQE 256
#define NET_DIM_DEFAULT_TX_CQ_PKTS_FROM_EQE 128
#define NET_DIM_DEF_PROFILE_CQE 1
#define NET_DIM_DEF_PROFILE_EQE 1

/*
 * Number of events between DIM iterations.
 * Causes a moderation of the algorithm run.
 */
#define DIM_NEVENTS 64

/*
 * Is a difference between values justifies taking an action.
 * We consider 10% difference as significant.
 */
#define IS_SIGNIFICANT_DIFF(val, ref) \
	((ref) && (((100UL * abs((val) - (ref))) / (ref)) > 10))

/*
 * Calculate the gap between two values.
 * Take wrap-around and variable size into consideration.
 */
#define BIT_GAP(bits, end, start) ((((end) - (start)) + BIT_ULL(bits)) \
		& (BIT_ULL(bits) - 1))

/**
 * struct dim_cq_moder - Structure for CQ moderation values.
 * Used for communications between DIM and its consumer.
 *
 * @usec: CQ timer suggestion (by DIM)
 * @pkts: CQ packet counter suggestion (by DIM)
 * @comps: Completion counter
 * @cq_period_mode: CQ period count mode (from CQE/EQE)
 * @rcu: for asynchronous kfree_rcu
 */
struct dim_cq_moder {
	u16 usec;
	u16 pkts;
	u16 comps;
	u8 cq_period_mode;
	struct rcu_head rcu;
};

#define DIM_PROFILE_RX		BIT(0)	/* support rx profile modification */
#define DIM_PROFILE_TX		BIT(1)	/* support tx profile modification */

#define DIM_COALESCE_USEC	BIT(0)	/* support usec field modification */
#define DIM_COALESCE_PKTS	BIT(1)	/* support pkts field modification */
#define DIM_COALESCE_COMPS	BIT(2)	/* support comps field modification */

/**
 * struct dim_irq_moder - Structure for irq moderation information.
 * Used to collect irq moderation related information.
 *
 * @profile_flags: DIM_PROFILE_*
 * @coal_flags: DIM_COALESCE_* for Rx and Tx
 * @dim_rx_mode: Rx DIM period count mode: CQE or EQE
 * @dim_tx_mode: Tx DIM period count mode: CQE or EQE
 * @rx_profile: DIM profile list for Rx
 * @tx_profile: DIM profile list for Tx
 * @rx_dim_work: Rx DIM worker scheduled by net_dim()
 * @tx_dim_work: Tx DIM worker scheduled by net_dim()
 */
struct dim_irq_moder {
	u8 profile_flags;
	u8 coal_flags;
	u8 dim_rx_mode;
	u8 dim_tx_mode;
	struct dim_cq_moder __rcu *rx_profile;
	struct dim_cq_moder __rcu *tx_profile;
	void (*rx_dim_work)(struct work_struct *work);
	void (*tx_dim_work)(struct work_struct *work);
};

/**
 * struct dim_sample - Structure for DIM sample data.
 * Used for communications between DIM and its consumer.
 *
 * @time: Sample timestamp
 * @pkt_ctr: Number of packets
 * @byte_ctr: Number of bytes
 * @event_ctr: Number of events
 * @comp_ctr: Current completion counter
 */
struct dim_sample {
	ktime_t time;
	u32 pkt_ctr;
	u32 byte_ctr;
	u16 event_ctr;
	u32 comp_ctr;
};

/**
 * struct dim_stats - Structure for DIM stats.
 * Used for holding current measured rates.
 *
 * @ppms: Packets per msec
 * @bpms: Bytes per msec
 * @epms: Events per msec
 * @cpms: Completions per msec
 * @cpe_ratio: Ratio of completions to events
 */
struct dim_stats {
	int ppms; /* packets per msec */
	int bpms; /* bytes per msec */
	int epms; /* events per msec */
	int cpms; /* completions per msec */
	int cpe_ratio; /* ratio of completions to events */
};

/**
 * struct dim - Main structure for dynamic interrupt moderation (DIM).
 * Used for holding all information about a specific DIM instance.
 *
 * @state: Algorithm state (see below)
 * @prev_stats: Measured rates from previous iteration (for comparison)
 * @start_sample: Sampled data at start of current iteration
 * @measuring_sample: A &dim_sample that is used to update the current events
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
 * enum dim_cq_period_mode - Modes for CQ period count
 *
 * @DIM_CQ_PERIOD_MODE_START_FROM_EQE: Start counting from EQE
 * @DIM_CQ_PERIOD_MODE_START_FROM_CQE: Start counting from CQE (implies timer reset)
 * @DIM_CQ_PERIOD_NUM_MODES: Number of modes
 */
enum dim_cq_period_mode {
	DIM_CQ_PERIOD_MODE_START_FROM_EQE = 0x0,
	DIM_CQ_PERIOD_MODE_START_FROM_CQE = 0x1,
	DIM_CQ_PERIOD_NUM_MODES
};

/**
 * enum dim_state - DIM algorithm states
 *
 * These will determine if the algorithm is in a valid state to start an iteration.
 *
 * @DIM_START_MEASURE: This is the first iteration (also after applying a new profile)
 * @DIM_MEASURE_IN_PROGRESS: Algorithm is already in progress - check if
 * need to perform an action
 * @DIM_APPLY_NEW_PROFILE: DIM consumer is currently applying a profile - no need to measure
 */
enum dim_state {
	DIM_START_MEASURE,
	DIM_MEASURE_IN_PROGRESS,
	DIM_APPLY_NEW_PROFILE,
};

/**
 * enum dim_tune_state - DIM algorithm tune states
 *
 * These will determine which action the algorithm should perform.
 *
 * @DIM_PARKING_ON_TOP: Algorithm found a local top point - exit on significant difference
 * @DIM_PARKING_TIRED: Algorithm found a deep top point - don't exit if tired > 0
 * @DIM_GOING_RIGHT: Algorithm is currently trying higher moderation levels
 * @DIM_GOING_LEFT: Algorithm is currently trying lower moderation levels
 */
enum dim_tune_state {
	DIM_PARKING_ON_TOP,
	DIM_PARKING_TIRED,
	DIM_GOING_RIGHT,
	DIM_GOING_LEFT,
};

/**
 * enum dim_stats_state - DIM algorithm statistics states
 *
 * These will determine the verdict of current iteration.
 *
 * @DIM_STATS_WORSE: Current iteration shows worse performance than before
 * @DIM_STATS_SAME:  Current iteration shows same performance than before
 * @DIM_STATS_BETTER: Current iteration shows better performance than before
 */
enum dim_stats_state {
	DIM_STATS_WORSE,
	DIM_STATS_SAME,
	DIM_STATS_BETTER,
};

/**
 * enum dim_step_result - DIM algorithm step results
 *
 * These describe the result of a step.
 *
 * @DIM_STEPPED: Performed a regular step
 * @DIM_TOO_TIRED: Same kind of step was done multiple times - should go to
 * tired parking
 * @DIM_ON_EDGE: Stepped to the most left/right profile
 */
enum dim_step_result {
	DIM_STEPPED,
	DIM_TOO_TIRED,
	DIM_ON_EDGE,
};

/**
 * net_dim_init_irq_moder - collect information to initialize irq moderation
 * @dev: target network device
 * @profile_flags: Rx or Tx profile modification capability
 * @coal_flags: irq moderation params flags
 * @rx_mode: CQ period mode for Rx
 * @tx_mode: CQ period mode for Tx
 * @rx_dim_work: Rx worker called after dim decision
 * @tx_dim_work: Tx worker called after dim decision
 *
 * Return: 0 on success or a negative error code.
 */
int net_dim_init_irq_moder(struct net_device *dev, u8 profile_flags,
			   u8 coal_flags, u8 rx_mode, u8 tx_mode,
			   void (*rx_dim_work)(struct work_struct *work),
			   void (*tx_dim_work)(struct work_struct *work));

/**
 * net_dim_free_irq_moder - free fields for irq moderation
 * @dev: target network device
 */
void net_dim_free_irq_moder(struct net_device *dev);

/**
 * net_dim_setting - initialize DIM's cq mode and schedule worker
 * @dev: target network device
 * @dim: DIM context
 * @is_tx: true indicates the tx direction, false indicates the rx direction
 */
void net_dim_setting(struct net_device *dev, struct dim *dim, bool is_tx);

/**
 * net_dim_work_cancel - synchronously cancel dim's worker
 * @dim: DIM context
 */
void net_dim_work_cancel(struct dim *dim);

/**
 * net_dim_get_rx_irq_moder - get DIM rx results based on profile_ix
 * @dev: target network device
 * @dim: DIM context
 *
 * Return: DIM irq moderation
 */
struct dim_cq_moder
net_dim_get_rx_irq_moder(struct net_device *dev, struct dim *dim);

/**
 * net_dim_get_tx_irq_moder - get DIM tx results based on profile_ix
 * @dev: target network device
 * @dim: DIM context
 *
 * Return: DIM irq moderation
 */
struct dim_cq_moder
net_dim_get_tx_irq_moder(struct net_device *dev, struct dim *dim);

/**
 * net_dim_set_rx_mode - set DIM rx cq mode
 * @dev: target network device
 * @rx_mode: target rx cq mode
 */
void net_dim_set_rx_mode(struct net_device *dev, u8 rx_mode);

/**
 * net_dim_set_tx_mode - set DIM tx cq mode
 * @dev: target network device
 * @tx_mode: target tx cq mode
 */
void net_dim_set_tx_mode(struct net_device *dev, u8 tx_mode);

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
 *	dim_turn - change profile altering direction
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
 * Returned boolean indicates whether curr_stats are reliable.
 */
bool dim_calc_stats(struct dim_sample *start, struct dim_sample *end,
		    struct dim_stats *curr_stats);

/**
 *	dim_update_sample - set a sample's fields with given values
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
 * This is the main logic of the algorithm, where data is processed in order
 * to decide on next required action.
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
