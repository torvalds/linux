#undef TRACE_SYSTEM
#define TRACE_SYSTEM irq

#if !defined(_TRACE_IRQ_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_IRQ_H

#include <linux/tracepoint.h>
#include <linux/interrupt.h>

#define softirq_name(sirq) { sirq##_SOFTIRQ, #sirq }
#define show_softirq_name(val)				\
	__print_symbolic(val,				\
			 softirq_name(HI),		\
			 softirq_name(TIMER),		\
			 softirq_name(NET_TX),		\
			 softirq_name(NET_RX),		\
			 softirq_name(BLOCK),		\
			 softirq_name(BLOCK_IOPOLL),	\
			 softirq_name(TASKLET),		\
			 softirq_name(SCHED),		\
			 softirq_name(HRTIMER),		\
			 softirq_name(RCU))

/**
 * irq_handler_entry - called immediately before the irq action handler
 * @irq: irq number
 * @action: pointer to struct irqaction
 *
 * The struct irqaction pointed to by @action contains various
 * information about the handler, including the device name,
 * @action->name, and the device id, @action->dev_id. When used in
 * conjunction with the irq_handler_exit tracepoint, we can figure
 * out irq handler latencies.
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

/**
 * irq_handler_exit - called immediately after the irq action handler returns
 * @irq: irq number
 * @action: pointer to struct irqaction
 * @ret: return value
 *
 * If the @ret value is set to IRQ_HANDLED, then we know that the corresponding
 * @action->handler scuccessully handled this irq. Otherwise, the irq might be
 * a shared irq line, or the irq was not handled successfully. Can be used in
 * conjunction with the irq_handler_entry to understand irq handler latencies.
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

/**
 * softirq_entry - called immediately before the softirq handler
 * @h: pointer to struct softirq_action
 * @vec: pointer to first struct softirq_action in softirq_vec array
 *
 * The @h parameter, contains a pointer to the struct softirq_action
 * which has a pointer to the action handler that is called. By subtracting
 * the @vec pointer from the @h pointer, we can determine the softirq
 * number. Also, when used in combination with the softirq_exit tracepoint
 * we can determine the softirq latency.
 */
TRACE_EVENT(softirq_entry,

	TP_PROTO(struct softirq_action *h, struct softirq_action *vec),

	TP_ARGS(h, vec),

	TP_STRUCT__entry(
		__field(	int,	vec			)
	),

	TP_fast_assign(
		__entry->vec = (int)(h - vec);
	),

	TP_printk("softirq=%d action=%s", __entry->vec,
		  show_softirq_name(__entry->vec))
);

/**
 * softirq_exit - called immediately after the softirq handler returns
 * @h: pointer to struct softirq_action
 * @vec: pointer to first struct softirq_action in softirq_vec array
 *
 * The @h parameter contains a pointer to the struct softirq_action
 * that has handled the softirq. By subtracting the @vec pointer from
 * the @h pointer, we can determine the softirq number. Also, when used in
 * combination with the softirq_entry tracepoint we can determine the softirq
 * latency.
 */
TRACE_EVENT(softirq_exit,

	TP_PROTO(struct softirq_action *h, struct softirq_action *vec),

	TP_ARGS(h, vec),

	TP_STRUCT__entry(
		__field(	int,	vec			)
	),

	TP_fast_assign(
		__entry->vec = (int)(h - vec);
	),

	TP_printk("softirq=%d action=%s", __entry->vec,
		  show_softirq_name(__entry->vec))
);

#endif /*  _TRACE_IRQ_H */

/* This part must be outside protection */
#include <trace/define_trace.h>
