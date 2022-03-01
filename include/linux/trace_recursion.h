/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_TRACE_RECURSION_H
#define _LINUX_TRACE_RECURSION_H

#include <linux/interrupt.h>
#include <linux/sched.h>

#ifdef CONFIG_TRACING

/* Only current can touch trace_recursion */

/*
 * For function tracing recursion:
 *  The order of these bits are important.
 *
 *  When function tracing occurs, the following steps are made:
 *   If arch does not support a ftrace feature:
 *    call internal function (uses INTERNAL bits) which calls...
 *   The function callback, which can use the FTRACE bits to
 *    check for recursion.
 */
enum {
	/* Function recursion bits */
	TRACE_FTRACE_BIT,
	TRACE_FTRACE_NMI_BIT,
	TRACE_FTRACE_IRQ_BIT,
	TRACE_FTRACE_SIRQ_BIT,
	TRACE_FTRACE_TRANSITION_BIT,

	/* Internal use recursion bits */
	TRACE_INTERNAL_BIT,
	TRACE_INTERNAL_NMI_BIT,
	TRACE_INTERNAL_IRQ_BIT,
	TRACE_INTERNAL_SIRQ_BIT,
	TRACE_INTERNAL_TRANSITION_BIT,

	TRACE_BRANCH_BIT,
/*
 * Abuse of the trace_recursion.
 * As we need a way to maintain state if we are tracing the function
 * graph in irq because we want to trace a particular function that
 * was called in irq context but we have irq tracing off. Since this
 * can only be modified by current, we can reuse trace_recursion.
 */
	TRACE_IRQ_BIT,

	/* Set if the function is in the set_graph_function file */
	TRACE_GRAPH_BIT,

	/*
	 * In the very unlikely case that an interrupt came in
	 * at a start of graph tracing, and we want to trace
	 * the function in that interrupt, the depth can be greater
	 * than zero, because of the preempted start of a previous
	 * trace. In an even more unlikely case, depth could be 2
	 * if a softirq interrupted the start of graph tracing,
	 * followed by an interrupt preempting a start of graph
	 * tracing in the softirq, and depth can even be 3
	 * if an NMI came in at the start of an interrupt function
	 * that preempted a softirq start of a function that
	 * preempted normal context!!!! Luckily, it can't be
	 * greater than 3, so the next two bits are a mask
	 * of what the depth is when we set TRACE_GRAPH_BIT
	 */

	TRACE_GRAPH_DEPTH_START_BIT,
	TRACE_GRAPH_DEPTH_END_BIT,

	/*
	 * To implement set_graph_notrace, if this bit is set, we ignore
	 * function graph tracing of called functions, until the return
	 * function is called to clear it.
	 */
	TRACE_GRAPH_NOTRACE_BIT,

	/* Used to prevent recursion recording from recursing. */
	TRACE_RECORD_RECURSION_BIT,
};

#define trace_recursion_set(bit)	do { (current)->trace_recursion |= (1<<(bit)); } while (0)
#define trace_recursion_clear(bit)	do { (current)->trace_recursion &= ~(1<<(bit)); } while (0)
#define trace_recursion_test(bit)	((current)->trace_recursion & (1<<(bit)))

#define trace_recursion_depth() \
	(((current)->trace_recursion >> TRACE_GRAPH_DEPTH_START_BIT) & 3)
#define trace_recursion_set_depth(depth) \
	do {								\
		current->trace_recursion &=				\
			~(3 << TRACE_GRAPH_DEPTH_START_BIT);		\
		current->trace_recursion |=				\
			((depth) & 3) << TRACE_GRAPH_DEPTH_START_BIT;	\
	} while (0)

#define TRACE_CONTEXT_BITS	4

#define TRACE_FTRACE_START	TRACE_FTRACE_BIT

#define TRACE_LIST_START	TRACE_INTERNAL_BIT

#define TRACE_CONTEXT_MASK	((1 << (TRACE_LIST_START + TRACE_CONTEXT_BITS)) - 1)

/*
 * Used for setting context
 *  NMI     = 0
 *  IRQ     = 1
 *  SOFTIRQ = 2
 *  NORMAL  = 3
 */
enum {
	TRACE_CTX_NMI,
	TRACE_CTX_IRQ,
	TRACE_CTX_SOFTIRQ,
	TRACE_CTX_NORMAL,
	TRACE_CTX_TRANSITION,
};

static __always_inline int trace_get_context_bit(void)
{
	unsigned char bit = interrupt_context_level();

	return TRACE_CTX_NORMAL - bit;
}

#ifdef CONFIG_FTRACE_RECORD_RECURSION
extern void ftrace_record_recursion(unsigned long ip, unsigned long parent_ip);
# define do_ftrace_record_recursion(ip, pip)				\
	do {								\
		if (!trace_recursion_test(TRACE_RECORD_RECURSION_BIT)) { \
			trace_recursion_set(TRACE_RECORD_RECURSION_BIT); \
			ftrace_record_recursion(ip, pip);		\
			trace_recursion_clear(TRACE_RECORD_RECURSION_BIT); \
		}							\
	} while (0)
#else
# define do_ftrace_record_recursion(ip, pip)	do { } while (0)
#endif

/*
 * Preemption is promised to be disabled when return bit >= 0.
 */
static __always_inline int trace_test_and_set_recursion(unsigned long ip, unsigned long pip,
							int start)
{
	unsigned int val = READ_ONCE(current->trace_recursion);
	int bit;

	bit = trace_get_context_bit() + start;
	if (unlikely(val & (1 << bit))) {
		/*
		 * If an interrupt occurs during a trace, and another trace
		 * happens in that interrupt but before the preempt_count is
		 * updated to reflect the new interrupt context, then this
		 * will think a recursion occurred, and the event will be dropped.
		 * Let a single instance happen via the TRANSITION_BIT to
		 * not drop those events.
		 */
		bit = TRACE_CTX_TRANSITION + start;
		if (val & (1 << bit)) {
			do_ftrace_record_recursion(ip, pip);
			return -1;
		}
	}

	val |= 1 << bit;
	current->trace_recursion = val;
	barrier();

	preempt_disable_notrace();

	return bit;
}

/*
 * Preemption will be enabled (if it was previously enabled).
 */
static __always_inline void trace_clear_recursion(int bit)
{
	preempt_enable_notrace();
	barrier();
	trace_recursion_clear(bit);
}

/**
 * ftrace_test_recursion_trylock - tests for recursion in same context
 *
 * Use this for ftrace callbacks. This will detect if the function
 * tracing recursed in the same context (normal vs interrupt),
 *
 * Returns: -1 if a recursion happened.
 *           >= 0 if no recursion.
 */
static __always_inline int ftrace_test_recursion_trylock(unsigned long ip,
							 unsigned long parent_ip)
{
	return trace_test_and_set_recursion(ip, parent_ip, TRACE_FTRACE_START);
}

/**
 * ftrace_test_recursion_unlock - called when function callback is complete
 * @bit: The return of a successful ftrace_test_recursion_trylock()
 *
 * This is used at the end of a ftrace callback.
 */
static __always_inline void ftrace_test_recursion_unlock(int bit)
{
	trace_clear_recursion(bit);
}

#endif /* CONFIG_TRACING */
#endif /* _LINUX_TRACE_RECURSION_H */
