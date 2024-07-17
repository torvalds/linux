/* SPDX-License-Identifier: GPL-2.0 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM hw_pressure

#if !defined(_TRACE_THERMAL_PRESSURE_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_THERMAL_PRESSURE_H

#include <linux/tracepoint.h>

TRACE_EVENT(hw_pressure_update,
	TP_PROTO(int cpu, unsigned long hw_pressure),
	TP_ARGS(cpu, hw_pressure),

	TP_STRUCT__entry(
		__field(unsigned long, hw_pressure)
		__field(int, cpu)
	),

	TP_fast_assign(
		__entry->hw_pressure = hw_pressure;
		__entry->cpu = cpu;
	),

	TP_printk("cpu=%d hw_pressure=%lu", __entry->cpu, __entry->hw_pressure)
);
#endif /* _TRACE_THERMAL_PRESSURE_H */

/* This part must be outside protection */
#include <trace/define_trace.h>
