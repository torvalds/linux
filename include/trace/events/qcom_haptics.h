/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2022, Qualcomm Innovation Center, Inc. All rights reserved.
 */

#undef TRACE_SYSTEM
#define TRACE_SYSTEM qcom_haptics

#if !defined(_TRACE_QCOM_HAPTICS_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_QCOM_HAPTICS_H

#include <linux/tracepoint.h>

TRACE_EVENT(qcom_haptics_fifo_hw_status,
	TP_PROTO(u32 filled, u32 available, bool full, bool empty),

	TP_ARGS(filled, available, full, empty),

	TP_STRUCT__entry(
		__field(u32, filled)
		__field(u32, available)
		__field(bool, full)
		__field(bool, empty)
	),

	TP_fast_assign(
		__entry->filled = filled;
		__entry->available = available;
		__entry->full = full;
		__entry->empty = empty;
	),

	TP_printk("FIFO HW status: filled = %u, available = %u, full = %d, empty = %d\n",
		__entry->filled, __entry->available, __entry->full, __entry->empty)
);

TRACE_EVENT(qcom_haptics_fifo_prgm_status,
	TP_PROTO(u32 total, u32 written, u32 num_bytes),

	TP_ARGS(total, written, num_bytes),

	TP_STRUCT__entry(
		__field(u32, total)
		__field(u32, written)
		__field(u32, num_bytes)
	),

	TP_fast_assign(
		__entry->total = total;
		__entry->written = written;
		__entry->num_bytes = num_bytes;
	),

	TP_printk("FIFO program status: writing %u bytes, written %u bytes, total %u bytes\n",
		__entry->num_bytes, __entry->written, __entry->total)
);


TRACE_EVENT(qcom_haptics_play,
	TP_PROTO(bool enable),

	TP_ARGS(enable),

	TP_STRUCT__entry(
		__field(bool, enable)
	),

	TP_fast_assign(
		__entry->enable = enable;
	),

	TP_printk("play enable: %d\n", __entry->enable)
);

TRACE_EVENT(qcom_haptics_status,
	TP_PROTO(const char *name, u8 msb, u8 lsb),

	TP_ARGS(name, msb, lsb),

	TP_STRUCT__entry(
		__string(id_name, name)
		__field(u8, msb)
		__field(u8, lsb)
	),

	TP_fast_assign(
		__assign_str(id_name, name);
		__entry->msb = msb;
		__entry->lsb = lsb;
	),

	TP_printk("haptics %s status: MSB %#x, LSB %#x\n", __get_str(id_name),
		__entry->msb, __entry->lsb)
);

#endif /* _TRACE_QCOM_HAPTICS_H */

#include <trace/define_trace.h>
