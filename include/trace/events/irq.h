/* SPDX-License-Identifier: GPL-2.0 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM irq

#if !defined(_TRACE_IRQ_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_IRQ_H

#include <linux/tracepoint.h>

struct irqaction;
struct softirq_action;

#define SOFTIRQ_NAME_LIST				\
			 softirq_name(HI)		\
			 softirq_name(TIMER)		\
			 softirq_name(NET_TX)		\
			 softirq_name(NET_RX)		\
			 softirq_name(BLOCK)		\
			 softirq_name(IRQ_POLL)		\
			 softirq_name(TASKLET)		\
			 softirq_name(SCHED)		\
			 softirq_name(HRTIMER)		\
			 softirq_name_end(RCU)

#undef softirq_name
#undef softirq_name_end

#define softirq_name(sirq) TRACE_DEFINE_ENUM(sirq##_SOFTIRQ);
#define softirq_name_end(sirq)  TRACE_DEFINE_ENUM(sirq##_SOFTIRQ);

SOFTIRQ_NAME_LIST

#undef softirq_name
#undef softirq_name_end

#define softirq_name(sirq) { sirq##_SOFTIRQ, #sirq },
#define softirq_name_end(sirq) { sirq##_SOFTIRQ, #sirq }

#define show_softirq_name(val)				\
	__print_symbolic(val, SOFTIRQ_NAME_LIST)

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
		__assign_str(name);
	),

	TP_printk("irq=%d name=%s", __entry->irq, __get_str(name))
);

/**
 * irq_handler_exit - called immediately after the irq action handler returns
 * @irq: irq number
 * @action: pointer to struct irqaction
 * @ret: return value
 *
 * If the @ret value is set to IRQ_HANDLED, then we know that the corresponding
 * @action->handler successfully handled this irq. Otherwise, the irq might be
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

	TP_printk("irq=%d ret=%s",
		  __entry->irq, __entry->ret ? "handled" : "unhandled")
);

DECLARE_EVENT_CLASS(softirq,

	TP_PROTO(unsigned int vec_nr),

	TP_ARGS(vec_nr),

	TP_STRUCT__entry(
		__field(	unsigned int,	vec	)
	),

	TP_fast_assign(
		__entry->vec = vec_nr;
	),

	TP_printk("vec=%u [action=%s]", __entry->vec,
		  show_softirq_name(__entry->vec))
);

/**
 * softirq_entry - called immediately before the softirq handler
 * @vec_nr:  softirq vector number
 *
 * When used in combination with the softirq_exit tracepoint
 * we can determine the softirq handler routine.
 */
DEFINE_EVENT(softirq, softirq_entry,

	TP_PROTO(unsigned int vec_nr),

	TP_ARGS(vec_nr)
);

/**
 * softirq_exit - called immediately after the softirq handler returns
 * @vec_nr:  softirq vector number
 *
 * When used in combination with the softirq_entry tracepoint
 * we can determine the softirq handler routine.
 */
DEFINE_EVENT(softirq, softirq_exit,

	TP_PROTO(unsigned int vec_nr),

	TP_ARGS(vec_nr)
);

/**
 * softirq_raise - called immediately when a softirq is raised
 * @vec_nr:  softirq vector number
 *
 * When used in combination with the softirq_entry tracepoint
 * we can determine the softirq raise to run latency.
 */
DEFINE_EVENT(softirq, softirq_raise,

	TP_PROTO(unsigned int vec_nr),

	TP_ARGS(vec_nr)
);

DECLARE_EVENT_CLASS(tasklet,

	TP_PROTO(struct tasklet_struct *t, void *func),

	TP_ARGS(t, func),

	TP_STRUCT__entry(
		__field(	void *,	tasklet)
		__field(	void *,	func)
	),

	TP_fast_assign(
		__entry->tasklet = t;
		__entry->func = func;
	),

	TP_printk("tasklet=%ps function=%ps", __entry->tasklet, __entry->func)
);

/**
 * tasklet_entry - called immediately before the tasklet is run
 * @t: tasklet pointer
 * @func: tasklet callback or function being run
 *
 * Used to find individual tasklet execution time
 */
DEFINE_EVENT(tasklet, tasklet_entry,

	TP_PROTO(struct tasklet_struct *t, void *func),

	TP_ARGS(t, func)
);

/**
 * tasklet_exit - called immediately after the tasklet is run
 * @t: tasklet pointer
 * @func: tasklet callback or function being run
 *
 * Used to find individual tasklet execution time
 */
DEFINE_EVENT(tasklet, tasklet_exit,

	TP_PROTO(struct tasklet_struct *t, void *func),

	TP_ARGS(t, func)
);

#endif /*  _TRACE_IRQ_H */

/* This part must be outside protection */
#include <trace/define_trace.h>
