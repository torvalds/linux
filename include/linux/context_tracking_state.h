/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_CONTEXT_TRACKING_STATE_H
#define _LINUX_CONTEXT_TRACKING_STATE_H

#include <linux/percpu.h>
#include <linux/static_key.h>
#include <linux/context_tracking_irq.h>

/* Offset to allow distinguishing irq vs. task-based idle entry/exit. */
#define DYNTICK_IRQ_NONIDLE	((LONG_MAX / 2) + 1)

enum ctx_state {
	CONTEXT_DISABLED	= -1,	/* returned by ct_state() if unknown */
	CONTEXT_KERNEL		= 0,
	CONTEXT_IDLE		= 1,
	CONTEXT_USER		= 2,
	CONTEXT_GUEST		= 3,
	CONTEXT_MAX		= 4,
};

/* Even value for idle, else odd. */
#define RCU_DYNTICKS_IDX CONTEXT_MAX

#define CT_STATE_MASK (CONTEXT_MAX - 1)
#define CT_DYNTICKS_MASK (~CT_STATE_MASK)

struct context_tracking {
#ifdef CONFIG_CONTEXT_TRACKING_USER
	/*
	 * When active is false, probes are unset in order
	 * to minimize overhead: TIF flags are cleared
	 * and calls to user_enter/exit are ignored. This
	 * may be further optimized using static keys.
	 */
	bool active;
	int recursion;
#endif
#ifdef CONFIG_CONTEXT_TRACKING
	atomic_t state;
#endif
#ifdef CONFIG_CONTEXT_TRACKING_IDLE
	long dynticks_nesting;		/* Track process nesting level. */
	long dynticks_nmi_nesting;	/* Track irq/NMI nesting level. */
#endif
};

#ifdef CONFIG_CONTEXT_TRACKING
DECLARE_PER_CPU(struct context_tracking, context_tracking);
#endif

#ifdef CONFIG_CONTEXT_TRACKING_USER
static __always_inline int __ct_state(void)
{
	return arch_atomic_read(this_cpu_ptr(&context_tracking.state)) & CT_STATE_MASK;
}
#endif

#ifdef CONFIG_CONTEXT_TRACKING_IDLE
static __always_inline int ct_dynticks(void)
{
	return atomic_read(this_cpu_ptr(&context_tracking.state)) & CT_DYNTICKS_MASK;
}

static __always_inline int ct_dynticks_cpu(int cpu)
{
	struct context_tracking *ct = per_cpu_ptr(&context_tracking, cpu);

	return atomic_read(&ct->state) & CT_DYNTICKS_MASK;
}

static __always_inline int ct_dynticks_cpu_acquire(int cpu)
{
	struct context_tracking *ct = per_cpu_ptr(&context_tracking, cpu);

	return atomic_read_acquire(&ct->state) & CT_DYNTICKS_MASK;
}

static __always_inline long ct_dynticks_nesting(void)
{
	return __this_cpu_read(context_tracking.dynticks_nesting);
}

static __always_inline long ct_dynticks_nesting_cpu(int cpu)
{
	struct context_tracking *ct = per_cpu_ptr(&context_tracking, cpu);

	return ct->dynticks_nesting;
}

static __always_inline long ct_dynticks_nmi_nesting(void)
{
	return __this_cpu_read(context_tracking.dynticks_nmi_nesting);
}

static __always_inline long ct_dynticks_nmi_nesting_cpu(int cpu)
{
	struct context_tracking *ct = per_cpu_ptr(&context_tracking, cpu);

	return ct->dynticks_nmi_nesting;
}
#endif /* #ifdef CONFIG_CONTEXT_TRACKING_IDLE */

#ifdef CONFIG_CONTEXT_TRACKING_USER
extern struct static_key_false context_tracking_key;

static __always_inline bool context_tracking_enabled(void)
{
	return static_branch_unlikely(&context_tracking_key);
}

static __always_inline bool context_tracking_enabled_cpu(int cpu)
{
	return context_tracking_enabled() && per_cpu(context_tracking.active, cpu);
}

static inline bool context_tracking_enabled_this_cpu(void)
{
	return context_tracking_enabled() && __this_cpu_read(context_tracking.active);
}

/**
 * ct_state() - return the current context tracking state if known
 *
 * Returns the current cpu's context tracking state if context tracking
 * is enabled.  If context tracking is disabled, returns
 * CONTEXT_DISABLED.  This should be used primarily for debugging.
 */
static __always_inline int ct_state(void)
{
	int ret;

	if (!context_tracking_enabled())
		return CONTEXT_DISABLED;

	preempt_disable();
	ret = __ct_state();
	preempt_enable();

	return ret;
}

#else
static __always_inline bool context_tracking_enabled(void) { return false; }
static __always_inline bool context_tracking_enabled_cpu(int cpu) { return false; }
static __always_inline bool context_tracking_enabled_this_cpu(void) { return false; }
#endif /* CONFIG_CONTEXT_TRACKING_USER */

#endif
