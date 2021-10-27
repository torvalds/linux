/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2021 The Linux Foundation. All rights reserved.
 */

#undef TRACE_SYSTEM
#define TRACE_SYSTEM preemptirq_long

#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH .

#if !defined(_TRACE_PREEMPTIRQ_LONG_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_PREEMPTIRQ_LONG_H

#include <linux/tracepoint.h>

DECLARE_EVENT_CLASS(preemptirq_long_template,

	TP_PROTO(u64 delta),

	TP_ARGS(delta),

	TP_STRUCT__entry(
		__field(u64, delta)
	),

	TP_fast_assign(
		__entry->delta = delta;
	),

	TP_printk("delta=%llu(ns)", __entry->delta)
);

DEFINE_EVENT(preemptirq_long_template, irq_disable_long,
	     TP_PROTO(u64 delta),
	     TP_ARGS(delta));

DEFINE_EVENT(preemptirq_long_template, preempt_disable_long,
	     TP_PROTO(u64 delta),
	     TP_ARGS(delta));

#endif /* _TRACE_PREEMPTIRQ_LONG_H */

/* This part must be outside protection */
#include <trace/define_trace.h>
