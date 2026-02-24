/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_HRTIMER_TYPES_H
#define _LINUX_HRTIMER_TYPES_H

#include <linux/types.h>
#include <linux/timerqueue_types.h>

struct hrtimer_clock_base;

/*
 * Return values for the callback function
 */
enum hrtimer_restart {
	HRTIMER_NORESTART,	/* Timer is not restarted */
	HRTIMER_RESTART,	/* Timer must be restarted */
};

/**
 * struct hrtimer - the basic hrtimer structure
 * @node:	timerqueue node, which also manages node.expires,
 *		the absolute expiry time in the hrtimers internal
 *		representation. The time is related to the clock on
 *		which the timer is based. Is setup by adding
 *		slack to the _softexpires value. For non range timers
 *		identical to _softexpires.
 * @_softexpires: the absolute earliest expiry time of the hrtimer.
 *		The time which was given as expiry time when the timer
 *		was armed.
 * @function:	timer expiry callback function
 * @base:	pointer to the timer base (per cpu and per clock)
 * @is_queued:	Indicates whether a timer is enqueued or not
 * @is_rel:	Set if the timer was armed relative
 * @is_soft:	Set if hrtimer will be expired in soft interrupt context.
 * @is_hard:	Set if hrtimer will be expired in hard interrupt context
 *		even on RT.
 * @is_lazy:	Set if the timer is frequently rearmed to avoid updates
 *		of the clock event device
 *
 * The hrtimer structure must be initialized by hrtimer_setup()
 */
struct hrtimer {
	struct timerqueue_node		node;
	ktime_t				_softexpires;
	enum hrtimer_restart		(*__private function)(struct hrtimer *);
	struct hrtimer_clock_base	*base;
	bool				is_queued;
	bool				is_rel;
	bool				is_soft;
	bool				is_hard;
	bool				is_lazy;
};

#endif /* _LINUX_HRTIMER_TYPES_H */
