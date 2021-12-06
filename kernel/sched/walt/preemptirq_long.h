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

/* reference preemptirq_template */
DECLARE_EVENT_CLASS(preemptirq_long_template,

	TP_PROTO(u64 delta, unsigned long ip, unsigned long parent_ip,
		unsigned long pparent_ip, unsigned long ppparent_ip),

	TP_ARGS(delta, ip, parent_ip, pparent_ip, ppparent_ip),

	TP_STRUCT__entry(
		__field(u64, delta)
		__field(unsigned long, caller_offs)
		__field(unsigned long, parent_offs)
		__field(unsigned long, pparent_offs)
		__field(unsigned long, ppparent_offs)
	),

	TP_fast_assign(
		__entry->delta = delta;
		__entry->caller_offs = ip;
		__entry->parent_offs = parent_ip;
		__entry->pparent_offs = pparent_ip;
		__entry->ppparent_offs = ppparent_ip;
	),

	TP_printk("delta=%llu(ns) caller=%ps <- %ps <- %ps <- %ps",
		__entry->delta, __entry->caller_offs,
		__entry->parent_offs,  __entry->pparent_offs,  __entry->ppparent_offs)
);

DEFINE_EVENT(preemptirq_long_template, irq_disable_long,
	     TP_PROTO(u64 delta, unsigned long ip, unsigned long parent_ip,
		      unsigned long pparent_ip, unsigned long ppparent_ip),
	     TP_ARGS(delta, ip, parent_ip, pparent_ip, ppparent_ip));

DEFINE_EVENT(preemptirq_long_template, preempt_disable_long,
	     TP_PROTO(u64 delta, unsigned long ip, unsigned long parent_ip,
		      unsigned long pparent_ip, unsigned long ppparent_ip),
	     TP_ARGS(delta, ip, parent_ip, pparent_ip, ppparent_ip));

#endif /* _TRACE_PREEMPTIRQ_LONG_H */

/* This part must be outside protection */
#include <trace/define_trace.h>
