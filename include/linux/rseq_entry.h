/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_RSEQ_ENTRY_H
#define _LINUX_RSEQ_ENTRY_H

/* Must be outside the CONFIG_RSEQ guard to resolve the stubs */
#ifdef CONFIG_RSEQ_STATS
#include <linux/percpu.h>

struct rseq_stats {
	unsigned long	exit;
	unsigned long	signal;
	unsigned long	slowpath;
	unsigned long	ids;
	unsigned long	cs;
	unsigned long	clear;
	unsigned long	fixup;
};

DECLARE_PER_CPU(struct rseq_stats, rseq_stats);

/*
 * Slow path has interrupts and preemption enabled, but the fast path
 * runs with interrupts disabled so there is no point in having the
 * preemption checks implied in __this_cpu_inc() for every operation.
 */
#ifdef RSEQ_BUILD_SLOW_PATH
#define rseq_stat_inc(which)	this_cpu_inc((which))
#else
#define rseq_stat_inc(which)	raw_cpu_inc((which))
#endif

#else /* CONFIG_RSEQ_STATS */
#define rseq_stat_inc(x)	do { } while (0)
#endif /* !CONFIG_RSEQ_STATS */

#ifdef CONFIG_RSEQ
#include <linux/jump_label.h>
#include <linux/rseq.h>

#include <linux/tracepoint-defs.h>

#ifdef CONFIG_TRACEPOINTS
DECLARE_TRACEPOINT(rseq_update);
DECLARE_TRACEPOINT(rseq_ip_fixup);
void __rseq_trace_update(struct task_struct *t);
void __rseq_trace_ip_fixup(unsigned long ip, unsigned long start_ip,
			   unsigned long offset, unsigned long abort_ip);

static inline void rseq_trace_update(struct task_struct *t, struct rseq_ids *ids)
{
	if (tracepoint_enabled(rseq_update) && ids)
		__rseq_trace_update(t);
}

static inline void rseq_trace_ip_fixup(unsigned long ip, unsigned long start_ip,
				       unsigned long offset, unsigned long abort_ip)
{
	if (tracepoint_enabled(rseq_ip_fixup))
		__rseq_trace_ip_fixup(ip, start_ip, offset, abort_ip);
}

#else /* CONFIG_TRACEPOINT */
static inline void rseq_trace_update(struct task_struct *t, struct rseq_ids *ids) { }
static inline void rseq_trace_ip_fixup(unsigned long ip, unsigned long start_ip,
				       unsigned long offset, unsigned long abort_ip) { }
#endif /* !CONFIG_TRACEPOINT */

DECLARE_STATIC_KEY_MAYBE(CONFIG_RSEQ_DEBUG_DEFAULT_ENABLE, rseq_debug_enabled);

static __always_inline void rseq_note_user_irq_entry(void)
{
	if (IS_ENABLED(CONFIG_GENERIC_IRQ_ENTRY))
		current->rseq.event.user_irq = true;
}

static __always_inline void rseq_exit_to_user_mode(void)
{
	struct rseq_event *ev = &current->rseq.event;

	rseq_stat_inc(rseq_stats.exit);

	if (IS_ENABLED(CONFIG_DEBUG_RSEQ))
		WARN_ON_ONCE(ev->sched_switch);

	/*
	 * Ensure that event (especially user_irq) is cleared when the
	 * interrupt did not result in a schedule and therefore the
	 * rseq processing did not clear it.
	 */
	ev->events = 0;
}

#else /* CONFIG_RSEQ */
static inline void rseq_note_user_irq_entry(void) { }
static inline void rseq_exit_to_user_mode(void) { }
#endif /* !CONFIG_RSEQ */

#endif /* _LINUX_RSEQ_ENTRY_H */
