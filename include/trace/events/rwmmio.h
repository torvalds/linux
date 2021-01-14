/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2021, The Linux Foundation. All rights reserved.
 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM rwmmio

#if !defined(_TRACE_MMIO_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_MMIO_H

#include <linux/tracepoint.h>

TRACE_EVENT(rwmmio_write,

	TP_PROTO(unsigned long fn, u64 val, u8 width, volatile void __iomem *addr),

	TP_ARGS(fn, val, width, addr),

	TP_STRUCT__entry(
		__field(u64, fn)
		__field(u64, val)
		__field(u8, width)
		__field(u64, addr)
	),

	TP_fast_assign(
		__entry->fn = fn;
		__entry->val = val;
		__entry->width = width;
		__entry->addr = (u64)addr;
	),

	TP_printk("%llxS write addr=%llx of width=%x val=0x%llx\n",
		__entry->fn, __entry->addr, __entry->width, __entry->val)
);

TRACE_EVENT(rwmmio_read,

	TP_PROTO(unsigned long fn, u8 width, const volatile void __iomem *addr),

	TP_ARGS(fn, width, addr),

	TP_STRUCT__entry(
		__field(u64, fn)
		__field(u8, width)
		__field(u64, addr)
	),

	TP_fast_assign(
		__entry->fn = fn;
		__entry->width = width;
		__entry->addr = (u64)addr;
	),

	TP_printk("%llxS read addr=%llx of width=%x\n",
		 __entry->fn, __entry->addr, __entry->width)
);

TRACE_EVENT(rwmmio_post_read,

	TP_PROTO(unsigned long fn, u64 val, u8 width, const volatile void __iomem *addr),

	TP_ARGS(fn, val, width, addr),

	TP_STRUCT__entry(
		__field(u64, fn)
		__field(u64, val)
		__field(u8, width)
		__field(u64, addr)
	),

	TP_fast_assign(
		__entry->fn = fn;
		__entry->val = val;
		__entry->width = width;
		__entry->addr = (u64)addr;
	),

	TP_printk("%llxS read addr=%llx of width=%x val=0x%llx\n",
		 __entry->fn, __entry->addr, __entry->width, __entry->val)
);

#endif /* _TRACE_MMIO_H */

#include <trace/define_trace.h>
