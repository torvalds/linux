/* SPDX-License-Identifier: GPL-2.0 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM intel_ifs

#if !defined(_TRACE_IFS_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_IFS_H

#include <linux/ktime.h>
#include <linux/tracepoint.h>

TRACE_EVENT(ifs_status,

	TP_PROTO(int cpu, int start, int stop, u64 status),

	TP_ARGS(cpu, start, stop, status),

	TP_STRUCT__entry(
		__field(	u64,	status	)
		__field(	int,	cpu	)
		__field(	u16,	start	)
		__field(	u16,	stop	)
	),

	TP_fast_assign(
		__entry->cpu	= cpu;
		__entry->start	= start;
		__entry->stop	= stop;
		__entry->status	= status;
	),

	TP_printk("cpu: %d, start: %.4x, stop: %.4x, status: %.16llx",
		__entry->cpu,
		__entry->start,
		__entry->stop,
		__entry->status)
);

#endif /* _TRACE_IFS_H */

/* This part must be outside protection */
#include <trace/define_trace.h>
