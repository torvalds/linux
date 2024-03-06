/* SPDX-License-Identifier: GPL-2.0 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM csd

#if !defined(_TRACE_CSD_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_CSD_H

#include <linux/tracepoint.h>

TRACE_EVENT(csd_queue_cpu,

	TP_PROTO(const unsigned int cpu,
		unsigned long callsite,
		smp_call_func_t func,
		call_single_data_t *csd),

	TP_ARGS(cpu, callsite, func, csd),

	TP_STRUCT__entry(
		__field(unsigned int, cpu)
		__field(void *, callsite)
		__field(void *, func)
		__field(void *, csd)
		),

	    TP_fast_assign(
		__entry->cpu = cpu;
		__entry->callsite = (void *)callsite;
		__entry->func = func;
		__entry->csd  = csd;
		),

	TP_printk("cpu=%u callsite=%pS func=%ps csd=%p",
		__entry->cpu, __entry->callsite, __entry->func, __entry->csd)
	);

/*
 * Tracepoints for a function which is called as an effect of smp_call_function.*
 */
DECLARE_EVENT_CLASS(csd_function,

	TP_PROTO(smp_call_func_t func, call_single_data_t *csd),

	TP_ARGS(func, csd),

	TP_STRUCT__entry(
		__field(void *,	func)
		__field(void *,	csd)
	),

	TP_fast_assign(
		__entry->func	= func;
		__entry->csd	= csd;
	),

	TP_printk("func=%ps, csd=%p", __entry->func, __entry->csd)
);

DEFINE_EVENT(csd_function, csd_function_entry,
	TP_PROTO(smp_call_func_t func, call_single_data_t *csd),
	TP_ARGS(func, csd)
);

DEFINE_EVENT(csd_function, csd_function_exit,
	TP_PROTO(smp_call_func_t func, call_single_data_t *csd),
	TP_ARGS(func, csd)
);

#endif /* _TRACE_CSD_H */

/* This part must be outside protection */
#include <trace/define_trace.h>
