#undef TRACE_SYSTEM
#define TRACE_SYSTEM fs

#if !defined(_TRACE_FS_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_FS_H

#include <linux/fs.h>
#include <linux/tracepoint.h>

TRACE_EVENT(do_sys_open,

	TP_PROTO(char *filename, int flags, int mode),

	TP_ARGS(filename, flags, mode),

	TP_STRUCT__entry(
		__string(	filename, filename		)
		__field(	int, flags			)
		__field(	int, mode			)
	),

	TP_fast_assign(
		__assign_str(filename, filename);
		__entry->flags = flags;
		__entry->mode = mode;
	),

	TP_printk("\"%s\" %x %o",
		  __get_str(filename), __entry->flags, __entry->mode)
);

TRACE_EVENT(open_exec,

	TP_PROTO(char *filename),

	TP_ARGS(filename),

	TP_STRUCT__entry(
		__string(	filename, filename		)
	),

	TP_fast_assign(
		__assign_str(filename, filename);
	),

	TP_printk("\"%s\"",
		  __get_str(filename))
);

#endif /* _TRACE_FS_H */

/* This part must be outside protection */
#include <trace/define_trace.h>
