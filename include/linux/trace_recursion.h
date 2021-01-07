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
 *   If callback is registered to the "global" list, the list
 *    function is called and recursion checks the GLOBAL bits.
 *    then this function calls...
 *   The function callback, which can use the FTRACE bits to
 *    check for recursion.
 *
 * Now if the arch does not support a feature, and it calls
 * the global list function which calls the ftrace callback
 * all three of these steps will do a recursion protection.
 * There's no reason to do one if the previous caller already
 * did. The recursion that we are protecting against will
 * go through the same steps again.
 *
 * To prevent the multiple recursion checks, if a recursion
 * bit is set that is higher than the MAX bit of the current
 * check, then we know that the check was made by the previous
 * caller, and we can skip the current check.
 */
enum {
	/* Function recursion bits */
	TRACE_FTRACE_BIT,
	TRACE_FTRACE_NMI_BIT,
	TRACE_FTRACE_IRQ_BIT,
	TRACE_FTRACE_SIRQ_BIT,

	/* INTERNAL_BITs must be greater than FTRACE_BITs */
	TRACE_INTERNAL_BIT,
	TRACE_INTERNAL_NMI_BIT,
	TRACE_INTERNAL_IRQ_BIT,
	TRACE_INTERNAL_SIRQ_BIT,

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

	/*
	 * When transitioning between context, the preempt_count() may
	 * not be correct. Allow for a single recursion to cover this case.
	 */
	TRACE_TRANSITION_BIT,

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
#define TRACE_FTRACE_MAX	((1 << (TRACE_FTRACE_START + TRACE_CONTEXT_BITS)) - 1)

#define TRACE_LIST_START	TRACE_INTERNAL_BIT
#define TRACE_LIST_MAX		((1 << (TRACE_LIST_START + TRACE_CONTEXT_BITS)) - 1)

#define TRACE_CONTEXT_MASK	TRACE_LIST_MAX

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
};

static __always_inline int trace_get_context_bit(void)
{
	unsigned long pc = preempt_count();

	if (!(pc & (NMI_MASK | HARDIRQ_MASK | SOFTIRQ_OFFSET)))
		return TRACE_CTX_NORMAL;
	else
		return pc & NMI_MASK ? TRACE_CTX_NMI :
			pc & HARDIRQ_MASK ? TRACE_CTX_IRQ : TRACE_CTX_SOFTIRQ;
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

static __always_inline int trace_test_and_set_recursion(unsigned long ip, unsigned long pip,
							int start, int max)
{
	unsigned int val = READ_ONCE(current->trace_recursion);
	int bit;

	/* A previous recursion check was made */
	if ((val & TRACE_CONTEXT_MASK) > max)
		return 0;

	bit = trace_get_context_bit() + start;
	if (unlikely(val & (1 << bit))) {
		/*
		 * It could be that preempt_count has not been updated during
		 * a switch between contexts. Allow for a single recursion.
		 */
		bit = TRACE_TRANSITION_BIT;
		if (val & (1 << bit)) {
			do_ftrace_record_recursion(ip, pip);
			return -1;
		}
	} else {
		/* Normal check passed, clear the transition to allow it again */
		val &= ~(1 << TRACE_TRANSITION_BIT);
	}

	val |= 1 << bit;
	current->trace_recursion = val;
	barrier();

	return bit + 1;
}

static __always_inline void trace_clear_recursion(int bit)
{
	if (!bit)
		return;

	barrier();
	bit--;
	trace_recursion_clear(bit);
}

/**
 * ftrace_test_recursion_trylock - tests for recursion in same context
 *
 * Use this for ftrace callbacks. This will detect if the function
 * tracing recursed in the same context (normal vs interrupt),
 *
 * Returns: -1 if a recursion happened.
 *           >= 0 if no recursion
 */
static __always_inline int ftrace_test_recursion_trylock(unsigned long ip,
							 unsigned long parent_ip)
{
	return trace_test_and_set_recursion(ip, parent_ip, TRACE_FTRACE_START, TRACE_FTRACE_MAX);
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
