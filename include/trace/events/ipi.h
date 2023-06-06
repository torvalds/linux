/* SPDX-License-Identifier: GPL-2.0 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM ipi

#if !defined(_TRACE_IPI_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_IPI_H

#include <linux/tracepoint.h>

/**
 * ipi_raise - called when a smp cross call is made
 *
 * @mask: mask of recipient CPUs for the IPI
 * @reason: string identifying the IPI purpose
 *
 * It is necessary for @reason to be a static string declared with
 * __tracepoint_string.
 */
TRACE_EVENT(ipi_raise,

	TP_PROTO(const struct cpumask *mask, const char *reason),

	TP_ARGS(mask, reason),

	TP_STRUCT__entry(
		__bitmask(target_cpus, nr_cpumask_bits)
		__field(const char *, reason)
	),

	TP_fast_assign(
		__assign_bitmask(target_cpus, cpumask_bits(mask), nr_cpumask_bits);
		__entry->reason = reason;
	),

	TP_printk("target_mask=%s (%s)", __get_bitmask(target_cpus), __entry->reason)
);

TRACE_EVENT(ipi_send_cpu,

	TP_PROTO(const unsigned int cpu, unsigned long callsite, void *callback),

	TP_ARGS(cpu, callsite, callback),

	TP_STRUCT__entry(
		__field(unsigned int, cpu)
		__field(void *, callsite)
		__field(void *, callback)
	),

	TP_fast_assign(
		__entry->cpu = cpu;
		__entry->callsite = (void *)callsite;
		__entry->callback = callback;
	),

	TP_printk("cpu=%u callsite=%pS callback=%pS",
		  __entry->cpu, __entry->callsite, __entry->callback)
);

TRACE_EVENT(ipi_send_cpumask,

	TP_PROTO(const struct cpumask *cpumask, unsigned long callsite, void *callback),

	TP_ARGS(cpumask, callsite, callback),

	TP_STRUCT__entry(
		__cpumask(cpumask)
		__field(void *, callsite)
		__field(void *, callback)
	),

	TP_fast_assign(
		__assign_cpumask(cpumask, cpumask_bits(cpumask));
		__entry->callsite = (void *)callsite;
		__entry->callback = callback;
	),

	TP_printk("cpumask=%s callsite=%pS callback=%pS",
		  __get_cpumask(cpumask), __entry->callsite, __entry->callback)
);

DECLARE_EVENT_CLASS(ipi_handler,

	TP_PROTO(const char *reason),

	TP_ARGS(reason),

	TP_STRUCT__entry(
		__field(const char *, reason)
	),

	TP_fast_assign(
		__entry->reason = reason;
	),

	TP_printk("(%s)", __entry->reason)
);

/**
 * ipi_entry - called immediately before the IPI handler
 *
 * @reason: string identifying the IPI purpose
 *
 * It is necessary for @reason to be a static string declared with
 * __tracepoint_string, ideally the same as used with trace_ipi_raise
 * for that IPI.
 */
DEFINE_EVENT(ipi_handler, ipi_entry,

	TP_PROTO(const char *reason),

	TP_ARGS(reason)
);

/**
 * ipi_exit - called immediately after the IPI handler returns
 *
 * @reason: string identifying the IPI purpose
 *
 * It is necessary for @reason to be a static string declared with
 * __tracepoint_string, ideally the same as used with trace_ipi_raise for
 * that IPI.
 */
DEFINE_EVENT(ipi_handler, ipi_exit,

	TP_PROTO(const char *reason),

	TP_ARGS(reason)
);

#endif /* _TRACE_IPI_H */

/* This part must be outside protection */
#include <trace/define_trace.h>
