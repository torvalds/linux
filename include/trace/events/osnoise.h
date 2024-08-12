/* SPDX-License-Identifier: GPL-2.0 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM osnoise

#if !defined(_OSNOISE_TRACE_H) || defined(TRACE_HEADER_MULTI_READ)
#define _OSNOISE_TRACE_H

#include <linux/tracepoint.h>
TRACE_EVENT(thread_noise,

	TP_PROTO(struct task_struct *t, u64 start, u64 duration),

	TP_ARGS(t, start, duration),

	TP_STRUCT__entry(
		__array(	char,		comm,	TASK_COMM_LEN)
		__field(	u64,		start	)
		__field(	u64,		duration)
		__field(	pid_t,		pid	)
	),

	TP_fast_assign(
		memcpy(__entry->comm, t->comm, TASK_COMM_LEN);
		__entry->pid = t->pid;
		__entry->start = start;
		__entry->duration = duration;
	),

	TP_printk("%8s:%d start %llu.%09u duration %llu ns",
		__entry->comm,
		__entry->pid,
		__print_ns_to_secs(__entry->start),
		__print_ns_without_secs(__entry->start),
		__entry->duration)
);

TRACE_EVENT(softirq_noise,

	TP_PROTO(int vector, u64 start, u64 duration),

	TP_ARGS(vector, start, duration),

	TP_STRUCT__entry(
		__field(	u64,		start	)
		__field(	u64,		duration)
		__field(	int,		vector	)
	),

	TP_fast_assign(
		__entry->vector = vector;
		__entry->start = start;
		__entry->duration = duration;
	),

	TP_printk("%8s:%d start %llu.%09u duration %llu ns",
		show_softirq_name(__entry->vector),
		__entry->vector,
		__print_ns_to_secs(__entry->start),
		__print_ns_without_secs(__entry->start),
		__entry->duration)
);

TRACE_EVENT(irq_noise,

	TP_PROTO(int vector, const char *desc, u64 start, u64 duration),

	TP_ARGS(vector, desc, start, duration),

	TP_STRUCT__entry(
		__field(	u64,		start	)
		__field(	u64,		duration)
		__string(	desc,		desc    )
		__field(	int,		vector	)

	),

	TP_fast_assign(
		__assign_str(desc);
		__entry->vector = vector;
		__entry->start = start;
		__entry->duration = duration;
	),

	TP_printk("%s:%d start %llu.%09u duration %llu ns",
		__get_str(desc),
		__entry->vector,
		__print_ns_to_secs(__entry->start),
		__print_ns_without_secs(__entry->start),
		__entry->duration)
);

TRACE_EVENT(nmi_noise,

	TP_PROTO(u64 start, u64 duration),

	TP_ARGS(start, duration),

	TP_STRUCT__entry(
		__field(	u64,		start	)
		__field(	u64,		duration)
	),

	TP_fast_assign(
		__entry->start = start;
		__entry->duration = duration;
	),

	TP_printk("start %llu.%09u duration %llu ns",
		__print_ns_to_secs(__entry->start),
		__print_ns_without_secs(__entry->start),
		__entry->duration)
);

TRACE_EVENT(sample_threshold,

	TP_PROTO(u64 start, u64 duration, u64 interference),

	TP_ARGS(start, duration, interference),

	TP_STRUCT__entry(
		__field(	u64,		start	)
		__field(	u64,		duration)
		__field(	u64,		interference)
	),

	TP_fast_assign(
		__entry->start = start;
		__entry->duration = duration;
		__entry->interference = interference;
	),

	TP_printk("start %llu.%09u duration %llu ns interference %llu",
		__print_ns_to_secs(__entry->start),
		__print_ns_without_secs(__entry->start),
		__entry->duration,
		__entry->interference)
);

#endif /* _TRACE_OSNOISE_H */

/* This part must be outside protection */
#include <trace/define_trace.h>
