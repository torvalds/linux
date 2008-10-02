/*
 * Copyright 2007, Mattias Nissler <mattias.nissler@gmx.de>
 * Copyright 2007, Stefano Brivio <stefano.brivio@polimi.it>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef RC80211_PID_H
#define RC80211_PID_H

/* Sampling period for measuring percentage of failed frames in ms. */
#define RC_PID_INTERVAL			125

/* Exponential averaging smoothness (used for I part of PID controller) */
#define RC_PID_SMOOTHING_SHIFT		3
#define RC_PID_SMOOTHING		(1 << RC_PID_SMOOTHING_SHIFT)

/* Sharpening factor (used for D part of PID controller) */
#define RC_PID_SHARPENING_FACTOR	0
#define RC_PID_SHARPENING_DURATION	0

/* Fixed point arithmetic shifting amount. */
#define RC_PID_ARITH_SHIFT		8

/* Fixed point arithmetic factor. */
#define RC_PID_ARITH_FACTOR		(1 << RC_PID_ARITH_SHIFT)

/* Proportional PID component coefficient. */
#define RC_PID_COEFF_P			15
/* Integral PID component coefficient. */
#define RC_PID_COEFF_I			9
/* Derivative PID component coefficient. */
#define RC_PID_COEFF_D			15

/* Target failed frames rate for the PID controller. NB: This effectively gives
 * maximum failed frames percentage we're willing to accept. If the wireless
 * link quality is good, the controller will fail to adjust failed frames
 * percentage to the target. This is intentional.
 */
#define RC_PID_TARGET_PF		14

/* Rate behaviour normalization quantity over time. */
#define RC_PID_NORM_OFFSET		3

/* Push high rates right after loading. */
#define RC_PID_FAST_START		0

/* Arithmetic right shift for positive and negative values for ISO C. */
#define RC_PID_DO_ARITH_RIGHT_SHIFT(x, y) \
	(x) < 0 ? -((-(x)) >> (y)) : (x) >> (y)

enum rc_pid_event_type {
	RC_PID_EVENT_TYPE_TX_STATUS,
	RC_PID_EVENT_TYPE_RATE_CHANGE,
	RC_PID_EVENT_TYPE_TX_RATE,
	RC_PID_EVENT_TYPE_PF_SAMPLE,
};

union rc_pid_event_data {
	/* RC_PID_EVENT_TX_STATUS */
	struct {
		struct ieee80211_tx_info tx_status;
	};
	/* RC_PID_EVENT_TYPE_RATE_CHANGE */
	/* RC_PID_EVENT_TYPE_TX_RATE */
	struct {
		int index;
		int rate;
	};
	/* RC_PID_EVENT_TYPE_PF_SAMPLE */
	struct {
		s32 pf_sample;
		s32 prop_err;
		s32 int_err;
		s32 der_err;
	};
};

struct rc_pid_event {
	/* The time when the event occured */
	unsigned long timestamp;

	/* Event ID number */
	unsigned int id;

	/* Type of event */
	enum rc_pid_event_type type;

	/* type specific data */
	union rc_pid_event_data data;
};

/* Size of the event ring buffer. */
#define RC_PID_EVENT_RING_SIZE 32

struct rc_pid_event_buffer {
	/* Counter that generates event IDs */
	unsigned int ev_count;

	/* Ring buffer of events */
	struct rc_pid_event ring[RC_PID_EVENT_RING_SIZE];

	/* Index to the entry in events_buf to be reused */
	unsigned int next_entry;

	/* Lock that guards against concurrent access to this buffer struct */
	spinlock_t lock;

	/* Wait queue for poll/select and blocking I/O */
	wait_queue_head_t waitqueue;
};

struct rc_pid_events_file_info {
	/* The event buffer we read */
	struct rc_pid_event_buffer *events;

	/* The entry we have should read next */
	unsigned int next_entry;
};

/**
 * struct rc_pid_debugfs_entries - tunable parameters
 *
 * Algorithm parameters, tunable via debugfs.
 * @dir: the debugfs directory for a specific phy
 * @target: target percentage for failed frames
 * @sampling_period: error sampling interval in milliseconds
 * @coeff_p: absolute value of the proportional coefficient
 * @coeff_i: absolute value of the integral coefficient
 * @coeff_d: absolute value of the derivative coefficient
 * @smoothing_shift: absolute value of the integral smoothing factor (i.e.
 *	amount of smoothing introduced by the exponential moving average)
 * @sharpen_factor: absolute value of the derivative sharpening factor (i.e.
 *	amount of emphasis given to the derivative term after low activity
 *	events)
 * @sharpen_duration: duration of the sharpening effect after the detected low
 *	activity event, relative to sampling_period
 * @norm_offset: amount of normalization periodically performed on the learnt
 *	rate behaviour values (lower means we should trust more what we learnt
 *	about behaviour of rates, higher means we should trust more the natural
 *	ordering of rates)
 */
struct rc_pid_debugfs_entries {
	struct dentry *dir;
	struct dentry *target;
	struct dentry *sampling_period;
	struct dentry *coeff_p;
	struct dentry *coeff_i;
	struct dentry *coeff_d;
	struct dentry *smoothing_shift;
	struct dentry *sharpen_factor;
	struct dentry *sharpen_duration;
	struct dentry *norm_offset;
};

void rate_control_pid_event_tx_status(struct rc_pid_event_buffer *buf,
				      struct ieee80211_tx_info *stat);

void rate_control_pid_event_rate_change(struct rc_pid_event_buffer *buf,
					       int index, int rate);

void rate_control_pid_event_tx_rate(struct rc_pid_event_buffer *buf,
					   int index, int rate);

void rate_control_pid_event_pf_sample(struct rc_pid_event_buffer *buf,
					     s32 pf_sample, s32 prop_err,
					     s32 int_err, s32 der_err);

void rate_control_pid_add_sta_debugfs(void *priv, void *priv_sta,
					     struct dentry *dir);

void rate_control_pid_remove_sta_debugfs(void *priv, void *priv_sta);

struct rc_pid_sta_info {
	unsigned long last_change;
	unsigned long last_sample;

	u32 tx_num_failed;
	u32 tx_num_xmit;

	/* Average failed frames percentage error (i.e. actual vs. target
	 * percentage), scaled by RC_PID_SMOOTHING. This value is computed
	 * using using an exponential weighted average technique:
	 *
	 *           (RC_PID_SMOOTHING - 1) * err_avg_old + err
	 * err_avg = ------------------------------------------
	 *                       RC_PID_SMOOTHING
	 *
	 * where err_avg is the new approximation, err_avg_old the previous one
	 * and err is the error w.r.t. to the current failed frames percentage
	 * sample. Note that the bigger RC_PID_SMOOTHING the more weight is
	 * given to the previous estimate, resulting in smoother behavior (i.e.
	 * corresponding to a longer integration window).
	 *
	 * For computation, we actually don't use the above formula, but this
	 * one:
	 *
	 * err_avg_scaled = err_avg_old_scaled - err_avg_old + err
	 *
	 * where:
	 * 	err_avg_scaled = err * RC_PID_SMOOTHING
	 * 	err_avg_old_scaled = err_avg_old * RC_PID_SMOOTHING
	 *
	 * This avoids floating point numbers and the per_failed_old value can
	 * easily be obtained by shifting per_failed_old_scaled right by
	 * RC_PID_SMOOTHING_SHIFT.
	 */
	s32 err_avg_sc;

	/* Last framed failes percentage sample. */
	u32 last_pf;

	/* Sharpening needed. */
	u8 sharp_cnt;

#ifdef CONFIG_MAC80211_DEBUGFS
	/* Event buffer */
	struct rc_pid_event_buffer events;

	/* Events debugfs file entry */
	struct dentry *events_entry;
#endif
};

/* Algorithm parameters. We keep them on a per-algorithm approach, so they can
 * be tuned individually for each interface.
 */
struct rc_pid_rateinfo {

	/* Map sorted rates to rates in ieee80211_hw_mode. */
	int index;

	/* Map rates in ieee80211_hw_mode to sorted rates. */
	int rev_index;

	/* Did we do any measurement on this rate? */
	bool valid;

	/* Comparison with the lowest rate. */
	int diff;
};

struct rc_pid_info {

	/* The failed frames percentage target. */
	unsigned int target;

	/* Rate at which failed frames percentage is sampled in 0.001s. */
	unsigned int sampling_period;

	/* P, I and D coefficients. */
	int coeff_p;
	int coeff_i;
	int coeff_d;

	/* Exponential averaging shift. */
	unsigned int smoothing_shift;

	/* Sharpening factor and duration. */
	unsigned int sharpen_factor;
	unsigned int sharpen_duration;

	/* Normalization offset. */
	unsigned int norm_offset;

	/* Rates information. */
	struct rc_pid_rateinfo *rinfo;

	/* Index of the last used rate. */
	int oldrate;

#ifdef CONFIG_MAC80211_DEBUGFS
	/* Debugfs entries created for the parameters above. */
	struct rc_pid_debugfs_entries dentries;
#endif
};

#endif /* RC80211_PID_H */
