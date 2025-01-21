#ifdef CONFIG_PREEMPTIRQ_TRACEPOINTS

#undef TRACE_SYSTEM
#define TRACE_SYSTEM preemptirq

#if !defined(_TRACE_PREEMPTIRQ_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_PREEMPTIRQ_H

#include <linux/ktime.h>
#include <linux/tracepoint.h>
#include <linux/string.h>
#include <asm/sections.h>

DECLARE_EVENT_CLASS(preemptirq_template,

	TP_PROTO(unsigned long ip, unsigned long parent_ip),

	TP_ARGS(ip, parent_ip),

	TP_STRUCT__entry(
		__field(s32, caller_offs)
		__field(s32, parent_offs)
	),

	TP_fast_assign(
		__entry->caller_offs = (s32)(ip - (unsigned long)_stext);
		__entry->parent_offs = (s32)(parent_ip - (unsigned long)_stext);
	),

	TP_printk("caller=%pS parent=%pS",
		  (void *)((unsigned long)(_stext) + __entry->caller_offs),
		  (void *)((unsigned long)(_stext) + __entry->parent_offs))
);

#ifdef CONFIG_TRACE_IRQFLAGS
DEFINE_EVENT(preemptirq_template, irq_disable,
	     TP_PROTO(unsigned long ip, unsigned long parent_ip),
	     TP_ARGS(ip, parent_ip));

DEFINE_EVENT(preemptirq_template, irq_enable,
	     TP_PROTO(unsigned long ip, unsigned long parent_ip),
	     TP_ARGS(ip, parent_ip));
#else
#define trace_irq_enable(...)
#define trace_irq_disable(...)
#endif

#ifdef CONFIG_TRACE_PREEMPT_TOGGLE
DEFINE_EVENT(preemptirq_template, preempt_disable,
	     TP_PROTO(unsigned long ip, unsigned long parent_ip),
	     TP_ARGS(ip, parent_ip));

DEFINE_EVENT(preemptirq_template, preempt_enable,
	     TP_PROTO(unsigned long ip, unsigned long parent_ip),
	     TP_ARGS(ip, parent_ip));
#else
#define trace_preempt_enable(...)
#define trace_preempt_disable(...)
#endif

#endif /* _TRACE_PREEMPTIRQ_H */

#include <trace/define_trace.h>

#else /* !CONFIG_PREEMPTIRQ_TRACEPOINTS */
#define trace_irq_enable(...)
#define trace_irq_disable(...)
#define trace_preempt_enable(...)
#define trace_preempt_disable(...)
#endif
