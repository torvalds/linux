/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2018-2020, The Linux Foundation. All rights reserved.
 * Copyright (c) 2022-2023, Qualcomm Innovation Center, Inc. All rights reserved.
 */

#undef TRACE_SYSTEM
#define TRACE_SYSTEM mpm

#if !defined(_TRACE_MPM_) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_MPM_H_

#include <linux/tracepoint.h>

TRACE_EVENT(mpm_wakeup_enable_irqs,

	TP_PROTO(uint32_t index, uint32_t irqs),

	TP_ARGS(index, irqs),

	TP_STRUCT__entry(
		__field(uint32_t, index)
		__field(uint32_t, irqs)
	),

	TP_fast_assign(
		__entry->index = index;
		__entry->irqs = irqs;
	),

	TP_printk("index:%u  wakeup_capable_irqs:0x%x",
				__entry->index, __entry->irqs)
);

TRACE_EVENT(mpm_wakeup_pending_irqs,

	TP_PROTO(uint32_t index, uint32_t irqs),

	TP_ARGS(index, irqs),

	TP_STRUCT__entry(
		__field(uint32_t, index)
		__field(uint32_t, irqs)
	),

	TP_fast_assign(
		__entry->index = index;
		__entry->irqs = irqs;
	),

	TP_printk("index:%u wakeup_irqs:0x%x", __entry->index, __entry->irqs)
);

TRACE_EVENT(mpm_wakeup_time,

	TP_PROTO(bool from_idle, u64 wakeup, u64 current_ticks),

	TP_ARGS(from_idle, wakeup, current_ticks),

	TP_STRUCT__entry(
		__field(bool, from_idle)
		__field(u64, wakeup)
		__field(u64, current_ticks)
	),

	TP_fast_assign(
		__entry->from_idle = from_idle;
		__entry->wakeup = wakeup;
		__entry->current_ticks = current_ticks;
	),

	TP_printk("idle:%d wakeup:0x%llx current:0x%llx", __entry->from_idle,
				__entry->wakeup, __entry->current_ticks)
);

#endif
#define TRACE_INCLUDE_FILE mpm
#include <trace/define_trace.h>
