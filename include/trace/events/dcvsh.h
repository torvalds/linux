/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2022-2023, Qualcomm Innovation Center, Inc. All rights reserved.
 */

#undef TRACE_SYSTEM
#define TRACE_SYSTEM dcvsh

#if !defined(_TRACE_DCVSH_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_DCVSH_H

#include <linux/tracepoint.h>

TRACE_EVENT(dcvsh_freq,

	TP_PROTO(unsigned long cpu, unsigned long req_freq,
		 unsigned long throttled_freq, unsigned long thermal_pressure),

	TP_ARGS(cpu, req_freq, throttled_freq, thermal_pressure),

	TP_STRUCT__entry(
		__field(unsigned long, cpu)
		__field(unsigned long, req_freq)
		__field(unsigned long, throttled_freq)
		__field(unsigned long, thermal_pressure)
	),

	TP_fast_assign(
		__entry->cpu = cpu;
		__entry->req_freq = req_freq;
		__entry->throttled_freq = throttled_freq;
		__entry->thermal_pressure = thermal_pressure;
	),

	TP_printk("cpu:%lu requested_freq:%lu throttled_freq:%lu thermal_pressure_freq:%lu",
		__entry->cpu,
		__entry->req_freq,
		__entry->throttled_freq,
		__entry->thermal_pressure)
);

TRACE_EVENT(dcvsh_throttle,

	TP_PROTO(unsigned long cpu, bool state),

	TP_ARGS(cpu, state),

	TP_STRUCT__entry(
		__field(unsigned long, cpu)
		__field(bool, state)
	),

	TP_fast_assign(
		__entry->cpu = cpu;
		__entry->state = state;
	),

	TP_printk("cpu:%lu throttle_%s",
		__entry->cpu,
		__entry->state ? "begin" : "end")
);

#endif /* _TRACE_DCVSH_H */

/* This part must be outside protection */
#include <trace/define_trace.h>
