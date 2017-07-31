/*
 * linux/kernel/time/tick-broadcast-hrtimer.c
 * This file emulates a local clock event device
 * via a pseudo clock device.
 */
#include <linux/cpu.h>
#include <linux/err.h>
#include <linux/hrtimer.h>
#include <linux/interrupt.h>
#include <linux/percpu.h>
#include <linux/profile.h>
#include <linux/clockchips.h>
#include <linux/sched.h>
#include <linux/smp.h>
#include <linux/module.h>

#include "tick-internal.h"

static struct hrtimer bctimer;

static int bc_shutdown(struct clock_event_device *evt)
{
	/*
	 * Note, we cannot cancel the timer here as we might
	 * run into the following live lock scenario:
	 *
	 * cpu 0		cpu1
	 * lock(broadcast_lock);
	 *			hrtimer_interrupt()
	 *			bc_handler()
	 *			   tick_handle_oneshot_broadcast();
	 *			    lock(broadcast_lock);
	 * hrtimer_cancel()
	 *  wait_for_callback()
	 */
	hrtimer_try_to_cancel(&bctimer);
	return 0;
}

/*
 * This is called from the guts of the broadcast code when the cpu
 * which is about to enter idle has the earliest broadcast timer event.
 */
static int bc_set_next(ktime_t expires, struct clock_event_device *bc)
{
	int bc_moved;
	/*
	 * We try to cancel the timer first. If the callback is on
	 * flight on some other cpu then we let it handle it. If we
	 * were able to cancel the timer nothing can rearm it as we
	 * own broadcast_lock.
	 *
	 * However we can also be called from the event handler of
	 * ce_broadcast_hrtimer itself when it expires. We cannot
	 * restart the timer because we are in the callback, but we
	 * can set the expiry time and let the callback return
	 * HRTIMER_RESTART.
	 *
	 * Since we are in the idle loop at this point and because
	 * hrtimer_{start/cancel} functions call into tracing,
	 * calls to these functions must be bound within RCU_NONIDLE.
	 */
	RCU_NONIDLE({
			bc_moved = hrtimer_try_to_cancel(&bctimer) >= 0;
			if (bc_moved)
				hrtimer_start(&bctimer, expires,
					      HRTIMER_MODE_ABS_PINNED);});
	if (bc_moved) {
		/* Bind the "device" to the cpu */
		bc->bound_on = smp_processor_id();
	} else if (bc->bound_on == smp_processor_id()) {
		hrtimer_set_expires(&bctimer, expires);
	}
	return 0;
}

static struct clock_event_device ce_broadcast_hrtimer = {
	.name			= "bc_hrtimer",
	.set_state_shutdown	= bc_shutdown,
	.set_next_ktime		= bc_set_next,
	.features		= CLOCK_EVT_FEAT_ONESHOT |
				  CLOCK_EVT_FEAT_KTIME |
				  CLOCK_EVT_FEAT_HRTIMER,
	.rating			= 0,
	.bound_on		= -1,
	.min_delta_ns		= 1,
	.max_delta_ns		= KTIME_MAX,
	.min_delta_ticks	= 1,
	.max_delta_ticks	= ULONG_MAX,
	.mult			= 1,
	.shift			= 0,
	.cpumask		= cpu_all_mask,
};

static enum hrtimer_restart bc_handler(struct hrtimer *t)
{
	ce_broadcast_hrtimer.event_handler(&ce_broadcast_hrtimer);

	if (clockevent_state_oneshot(&ce_broadcast_hrtimer))
		if (ce_broadcast_hrtimer.next_event != KTIME_MAX)
			return HRTIMER_RESTART;

	return HRTIMER_NORESTART;
}

void tick_setup_hrtimer_broadcast(void)
{
	hrtimer_init(&bctimer, CLOCK_MONOTONIC, HRTIMER_MODE_ABS);
	bctimer.function = bc_handler;
	clockevents_register_device(&ce_broadcast_hrtimer);
}
