#if !defined(_TRACE_IRQ_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_IRQ_H

#include <linux/tracepoint.h>
#include <linux/interrupt.h>

#undef TRACE_SYSTEM
#define TRACE_SYSTEM irq

/*
 * Tracepoint for entry of interrupt handler:
 */
TRACE_EVENT(irq_handler_entry,

	TP_PROTO(int irq, struct irqaction *action),

	TP_ARGS(irq, action),

	TP_STRUCT__entry(
		__field(	int,	irq		)
		__string(	name,	action->name	)
	),

	TP_fast_assign(
		__entry->irq = irq;
		__assign_str(name, action->name);
	),

	TP_printk("irq=%d handler=%s", __entry->irq, __get_str(name))
);

/*
 * Tracepoint for return of an interrupt handler:
 */
TRACE_EVENT(irq_handler_exit,

	TP_PROTO(int irq, struct irqaction *action, int ret),

	TP_ARGS(irq, action, ret),

	TP_STRUCT__entry(
		__field(	int,	irq	)
		__field(	int,	ret	)
	),

	TP_fast_assign(
		__entry->irq	= irq;
		__entry->ret	= ret;
	),

	TP_printk("irq=%d return=%s",
		  __entry->irq, __entry->ret ? "handled" : "unhandled")
);

TRACE_EVENT(softirq_entry,

	TP_PROTO(struct softirq_action *h, struct softirq_action *vec),

	TP_ARGS(h, vec),

	TP_STRUCT__entry(
		__field(	int,	vec			)
		__string(	name,	softirq_to_name[h-vec]	)
	),

	TP_fast_assign(
		__entry->vec = (int)(h - vec);
		__assign_str(name, softirq_to_name[h-vec]);
	),

	TP_printk("softirq=%d action=%s", __entry->vec, __get_str(name))
);

TRACE_EVENT(softirq_exit,

	TP_PROTO(struct softirq_action *h, struct softirq_action *vec),

	TP_ARGS(h, vec),

	TP_STRUCT__entry(
		__field(	int,	vec			)
		__string(	name,	softirq_to_name[h-vec]	)
	),

	TP_fast_assign(
		__entry->vec = (int)(h - vec);
		__assign_str(name, softirq_to_name[h-vec]);
	),

	TP_printk("softirq=%d action=%s", __entry->vec, __get_str(name))
);

#endif /*  _TRACE_IRQ_H */

/* This part must be outside protection */
#include <trace/define_trace.h>
