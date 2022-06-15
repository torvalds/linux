/* SPDX-License-Identifier: GPL-2.0 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM thermal_pressure

#if !defined(_TRACE_THERMAL_PRESSURE_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_THERMAL_PRESSURE_H

#include <linux/tracepoint.h>

TRACE_EVENT(thermal_pressure_update,
	TP_PROTO(int cpu, unsigned long thermal_pressure),
	TP_ARGS(cpu, thermal_pressure),

	TP_STRUCT__entry(
		__field(unsigned long, thermal_pressure)
		__field(int, cpu)
	),

	TP_fast_assign(
		__entry->thermal_pressure = thermal_pressure;
		__entry->cpu = cpu;
	),

	TP_printk("cpu=%d thermal_pressure=%lu", __entry->cpu, __entry->thermal_pressure)
);
#endif /* _TRACE_THERMAL_PRESSURE_H */

/* This part must be outside protection */
#include <trace/define_trace.h>
