/* SPDX-License-Identifier: GPL-2.0 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM osnoise

#if !defined(_OSNOISE_TRACE_H) || defined(TRACE_HEADER_MULTI_READ)

#ifndef _OSNOISE_TRACE_H
#define _OSNOISE_TRACE_H
/*
 * osnoise sample structure definition. Used to store the statistics of a
 * sample run.
 */
struct osnoise_sample {
	u64			runtime;	/* runtime */
	u64			noise;		/* noise */
	u64			max_sample;	/* max single noise sample */
	int			hw_count;	/* # HW (incl. hypervisor) interference */
	int			nmi_count;	/* # NMIs during this sample */
	int			irq_count;	/* # IRQs during this sample */
	int			softirq_count;	/* # softirqs during this sample */
	int			thread_count;	/* # threads during this sample */
};

#ifdef CONFIG_TIMERLAT_TRACER
/*
 * timerlat sample structure definition. Used to store the statistics of
 * a sample run.
 */
struct timerlat_sample {
	u64			timer_latency;	/* timer_latency */
	unsigned int		seqnum;		/* unique sequence */
	int			context;	/* timer context */
};
#endif // CONFIG_TIMERLAT_TRACER
#endif // _OSNOISE_TRACE_H

#include <linux/tracepoint.h>
TRACE_EVENT(osnoise_sample,

	TP_PROTO(struct osnoise_sample *s),

	TP_ARGS(s),

	TP_STRUCT__entry(
		__field(	u64,		runtime	)
		__field(	u64,		noise	)
		__field(	u64,		max_sample	)
		__field(	int,		hw_count	)
		__field(	int,		irq_count	)
		__field(	int,		nmi_count	)
		__field(	int, 		softirq_count	)
		__field(	int,		thread_count	)
	),

	TP_fast_assign(
		__entry->runtime = s->runtime;
		__entry->noise = s->noise;
		__entry->max_sample = s->max_sample;
		__entry->hw_count = s->hw_count;
		__entry->irq_count = s->irq_count;
		__entry->nmi_count = s->nmi_count;
		__entry->softirq_count = s->softirq_count;
		__entry->thread_count = s->thread_count;
	),

	TP_printk("runtime=%llu noise=%llu max_sample=%llu hw_count=%d"
		  " irq_count=%d nmi_count=%d softirq_count=%d"
		  " thread_count=%d",
		  __entry->runtime,
		  __entry->noise,
		  __entry->max_sample,
		  __entry->hw_count,
		  __entry->irq_count,
		  __entry->nmi_count,
		  __entry->softirq_count,
		  __entry->thread_count)
);

#ifdef CONFIG_TIMERLAT_TRACER
TRACE_EVENT(timerlat_sample,

	TP_PROTO(struct timerlat_sample *s),

	TP_ARGS(s),

	TP_STRUCT__entry(
		__field(	u64,		timer_latency	)
		__field(	unsigned int,	seqnum		)
		__field(	int,		context		)
	),

	TP_fast_assign(
		__entry->timer_latency = s->timer_latency;
		__entry->seqnum = s->seqnum;
		__entry->context = s->context;
	),

	TP_printk("timer_latency=%llu seqnum=%u context=%d",
		  __entry->timer_latency,
		  __entry->seqnum,
		  __entry->context)
);
#endif // CONFIG_TIMERLAT_TRACER

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
