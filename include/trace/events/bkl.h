#undef TRACE_SYSTEM
#define TRACE_SYSTEM bkl

#if !defined(_TRACE_BKL_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_BKL_H

#include <linux/tracepoint.h>

TRACE_EVENT(lock_kernel,

	TP_PROTO(const char *func, const char *file, int line),

	TP_ARGS(func, file, line),

	TP_STRUCT__entry(
		__field(	int,		depth			)
		__field_ext(	const char *,	func, FILTER_PTR_STRING	)
		__field_ext(	const char *,	file, FILTER_PTR_STRING	)
		__field(	int,		line			)
	),

	TP_fast_assign(
		/* We want to record the lock_depth after lock is acquired */
		__entry->depth = current->lock_depth + 1;
		__entry->func = func;
		__entry->file = file;
		__entry->line = line;
	),

	TP_printk("depth=%d file:line=%s:%d func=%s()", __entry->depth,
		  __entry->file, __entry->line, __entry->func)
);

TRACE_EVENT(unlock_kernel,

	TP_PROTO(const char *func, const char *file, int line),

	TP_ARGS(func, file, line),

	TP_STRUCT__entry(
		__field(int,		depth		)
		__field(const char *,	func		)
		__field(const char *,	file		)
		__field(int,		line		)
	),

	TP_fast_assign(
		__entry->depth = current->lock_depth;
		__entry->func = func;
		__entry->file = file;
		__entry->line = line;
	),

	TP_printk("depth=%d file:line=%s:%d func=%s()", __entry->depth,
		  __entry->file, __entry->line, __entry->func)
);

#endif /* _TRACE_BKL_H */

/* This part must be outside protection */
#include <trace/define_trace.h>
