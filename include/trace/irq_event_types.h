
/* use <trace/irq.h> instead */
#ifndef TRACE_FORMAT
# error Do not include this file directly.
# error Unless you know what you are doing.
#endif

#undef TRACE_SYSTEM
#define TRACE_SYSTEM irq

TRACE_EVENT_FORMAT(irq_handler_entry,
	TP_PROTO(int irq, struct irqaction *action),
	TP_ARGS(irq, action),
	TP_FMT("irq=%d handler=%s", irq, action->name),
	TRACE_STRUCT(
		TRACE_FIELD(int, irq, irq)
	),
	TP_RAW_FMT("irq %d")
	);

TRACE_EVENT_FORMAT(irq_handler_exit,
	TP_PROTO(int irq, struct irqaction *action, int ret),
	TP_ARGS(irq, action, ret),
	TP_FMT("irq=%d handler=%s return=%s",
		irq, action->name, ret ? "handled" : "unhandled"),
	TRACE_STRUCT(
		TRACE_FIELD(int, irq, irq)
		TRACE_FIELD(int, ret, ret)
	),
	TP_RAW_FMT("irq %d ret %d")
	);

#undef TRACE_SYSTEM
