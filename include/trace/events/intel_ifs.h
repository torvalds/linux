/* SPDX-License-Identifier: GPL-2.0 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM intel_ifs

#if !defined(_TRACE_IFS_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_IFS_H

#include <linux/ktime.h>
#include <linux/tracepoint.h>

TRACE_EVENT(ifs_status,

	TP_PROTO(int batch, int start, int stop, u64 status),

	TP_ARGS(batch, start, stop, status),

	TP_STRUCT__entry(
		__field(	int,	batch	)
		__field(	u64,	status	)
		__field(	u16,	start	)
		__field(	u16,	stop	)
	),

	TP_fast_assign(
		__entry->batch	= batch;
		__entry->start	= start;
		__entry->stop	= stop;
		__entry->status	= status;
	),

	TP_printk("batch: %.2d, start: %.4x, stop: %.4x, status: %.16llx",
		__entry->batch,
		__entry->start,
		__entry->stop,
		__entry->status)
);

#endif /* _TRACE_IFS_H */

/* This part must be outside protection */
#include <trace/define_trace.h>
