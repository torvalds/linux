
/* use <trace/irq.h> instead */
#ifndef TRACE_FORMAT
# error Do not include this file directly.
# error Unless you know what you are doing.
#endif

#undef TRACE_SYSTEM
#define TRACE_SYSTEM irq

TRACE_FORMAT(irq_handler_entry,
	TPPROTO(int irq, struct irqaction *action),
	TPARGS(irq, action),
	TPFMT("irq=%d handler=%s", irq, action->name));

TRACE_FORMAT(irq_handler_exit,
	TPPROTO(int irq, struct irqaction *action, int ret),
	TPARGS(irq, action, ret),
	TPFMT("irq=%d handler=%s return=%s",
		irq, action->name, ret ? "handled" : "unhandled"));

#undef TRACE_SYSTEM
