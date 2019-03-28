// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2016, Linaro Ltd - Daniel Lezcano <daniel.lezcano@linaro.org>

#include <linux/kernel.h>
#include <linux/percpu.h>
#include <linux/slab.h>
#include <linux/static_key.h>
#include <linux/interrupt.h>
#include <linux/idr.h>
#include <linux/irq.h>

#include <trace/events/irq.h>

#include "internals.h"

DEFINE_STATIC_KEY_FALSE(irq_timing_enabled);

DEFINE_PER_CPU(struct irq_timings, irq_timings);

struct irqt_stat {
	u64     next_evt;
};

static DEFINE_IDR(irqt_stats);

void irq_timings_enable(void)
{
	static_branch_enable(&irq_timing_enabled);
}

void irq_timings_disable(void)
{
	static_branch_disable(&irq_timing_enabled);
}

/**
 * irq_timings_next_event - Return when the next event is supposed to arrive
 *
 * During the last busy cycle, the number of interrupts is incremented
 * and stored in the irq_timings structure. This information is
 * necessary to:
 *
 * - know if the index in the table wrapped up:
 *
 *      If more than the array size interrupts happened during the
 *      last busy/idle cycle, the index wrapped up and we have to
 *      begin with the next element in the array which is the last one
 *      in the sequence, otherwise it is a the index 0.
 *
 * - have an indication of the interrupts activity on this CPU
 *   (eg. irq/sec)
 *
 * The values are 'consumed' after inserting in the statistical model,
 * thus the count is reinitialized.
 *
 * The array of values **must** be browsed in the time direction, the
 * timestamp must increase between an element and the next one.
 *
 * Returns a nanosec time based estimation of the earliest interrupt,
 * U64_MAX otherwise.
 */
u64 irq_timings_next_event(u64 now)
{
	/*
	 * This function must be called with the local irq disabled in
	 * order to prevent the timings circular buffer to be updated
	 * while we are reading it.
	 */
	lockdep_assert_irqs_disabled();

	return 0;
}

void irq_timings_free(int irq)
{
	struct irqt_stat __percpu *s;

	s = idr_find(&irqt_stats, irq);
	if (s) {
		free_percpu(s);
		idr_remove(&irqt_stats, irq);
	}
}

int irq_timings_alloc(int irq)
{
	struct irqt_stat __percpu *s;
	int id;

	/*
	 * Some platforms can have the same private interrupt per cpu,
	 * so this function may be be called several times with the
	 * same interrupt number. Just bail out in case the per cpu
	 * stat structure is already allocated.
	 */
	s = idr_find(&irqt_stats, irq);
	if (s)
		return 0;

	s = alloc_percpu(*s);
	if (!s)
		return -ENOMEM;

	idr_preload(GFP_KERNEL);
	id = idr_alloc(&irqt_stats, s, irq, irq + 1, GFP_NOWAIT);
	idr_preload_end();

	if (id < 0) {
		free_percpu(s);
		return id;
	}

	return 0;
}
