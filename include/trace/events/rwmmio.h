/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2021-2022 Qualcomm Innovation Center, Inc. All rights reserved.
 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM rwmmio

#if !defined(_TRACE_RWMMIO_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_RWMMIO_H

#include <linux/tracepoint.h>

DECLARE_EVENT_CLASS(rwmmio_rw_template,

	TP_PROTO(unsigned long caller, u64 val, u8 width, volatile void __iomem *addr),

	TP_ARGS(caller, val, width, addr),

	TP_STRUCT__entry(
		__field(unsigned long, caller)
		__field(unsigned long, addr)
		__field(u64, val)
		__field(u8, width)
	),

	TP_fast_assign(
		__entry->caller = caller;
		__entry->val = val;
		__entry->addr = (unsigned long)addr;
		__entry->width = width;
	),

	TP_printk("%pS width=%d val=%#llx addr=%#lx",
		(void *)__entry->caller, __entry->width,
		__entry->val, __entry->addr)
);

DEFINE_EVENT(rwmmio_rw_template, rwmmio_write,
	TP_PROTO(unsigned long caller, u64 val, u8 width, volatile void __iomem *addr),
	TP_ARGS(caller, val, width, addr)
);

DEFINE_EVENT(rwmmio_rw_template, rwmmio_post_write,
	TP_PROTO(unsigned long caller, u64 val, u8 width, volatile void __iomem *addr),
	TP_ARGS(caller, val, width, addr)
);

TRACE_EVENT(rwmmio_read,

	TP_PROTO(unsigned long caller, u8 width, const volatile void __iomem *addr),

	TP_ARGS(caller, width, addr),

	TP_STRUCT__entry(
		__field(unsigned long, caller)
		__field(unsigned long, addr)
		__field(u8, width)
	),

	TP_fast_assign(
		__entry->caller = caller;
		__entry->addr = (unsigned long)addr;
		__entry->width = width;
	),

	TP_printk("%pS width=%d addr=%#lx",
		 (void *)__entry->caller, __entry->width, __entry->addr)
);

TRACE_EVENT(rwmmio_post_read,

	TP_PROTO(unsigned long caller, u64 val, u8 width, const volatile void __iomem *addr),

	TP_ARGS(caller, val, width, addr),

	TP_STRUCT__entry(
		__field(unsigned long, caller)
		__field(unsigned long, addr)
		__field(u64, val)
		__field(u8, width)
	),

	TP_fast_assign(
		__entry->caller = caller;
		__entry->val = val;
		__entry->addr = (unsigned long)addr;
		__entry->width = width;
	),

	TP_printk("%pS width=%d val=%#llx addr=%#lx",
		 (void *)__entry->caller, __entry->width,
		 __entry->val, __entry->addr)
);

#endif /* _TRACE_RWMMIO_H */

#include <trace/define_trace.h>
