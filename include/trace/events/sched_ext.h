/* SPDX-License-Identifier: GPL-2.0 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM sched_ext

#if !defined(_TRACE_SCHED_EXT_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_SCHED_EXT_H

#include <linux/tracepoint.h>

TRACE_EVENT(sched_ext_dump,

	TP_PROTO(const char *line),

	TP_ARGS(line),

	TP_STRUCT__entry(
		__string(line, line)
	),

	TP_fast_assign(
		__assign_str(line);
	),

	TP_printk("%s",
		__get_str(line)
	)
);

TRACE_EVENT(sched_ext_event,
	    TP_PROTO(const char *name, __s64 delta),
	    TP_ARGS(name, delta),

	TP_STRUCT__entry(
		__string(name, name)
		__field(	__s64,		delta		)
	),

	TP_fast_assign(
		__assign_str(name);
		__entry->delta		= delta;
	),

	TP_printk("name %s delta %lld",
		  __get_str(name), __entry->delta
	)
);

TRACE_EVENT(sched_ext_bypass_lb,

	TP_PROTO(__u32 node, __u32 nr_cpus, __u32 nr_tasks, __u32 nr_balanced,
		 __u32 before_min, __u32 before_max,
		 __u32 after_min, __u32 after_max),

	TP_ARGS(node, nr_cpus, nr_tasks, nr_balanced,
		before_min, before_max, after_min, after_max),

	TP_STRUCT__entry(
		__field(	__u32,		node		)
		__field(	__u32,		nr_cpus		)
		__field(	__u32,		nr_tasks	)
		__field(	__u32,		nr_balanced	)
		__field(	__u32,		before_min	)
		__field(	__u32,		before_max	)
		__field(	__u32,		after_min	)
		__field(	__u32,		after_max	)
	),

	TP_fast_assign(
		__entry->node		= node;
		__entry->nr_cpus	= nr_cpus;
		__entry->nr_tasks	= nr_tasks;
		__entry->nr_balanced	= nr_balanced;
		__entry->before_min	= before_min;
		__entry->before_max	= before_max;
		__entry->after_min	= after_min;
		__entry->after_max	= after_max;
	),

	TP_printk("node %u: nr_cpus=%u nr_tasks=%u nr_balanced=%u min=%u->%u max=%u->%u",
		  __entry->node, __entry->nr_cpus,
		  __entry->nr_tasks, __entry->nr_balanced,
		  __entry->before_min, __entry->after_min,
		  __entry->before_max, __entry->after_max
	)
);

#endif /* _TRACE_SCHED_EXT_H */

/* This part must be outside protection */
#include <trace/define_trace.h>
