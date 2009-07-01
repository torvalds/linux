#if !defined(_TRACE_KVM_MAIN_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_KVM_MAIN_H

#include <linux/tracepoint.h>

#undef TRACE_SYSTEM
#define TRACE_SYSTEM kvm
#define TRACE_INCLUDE_FILE kvm

#if defined(__KVM_HAVE_IOAPIC)
TRACE_EVENT(kvm_set_irq,
	TP_PROTO(unsigned int gsi, int level, int irq_source_id),
	TP_ARGS(gsi, level, irq_source_id),

	TP_STRUCT__entry(
		__field(	unsigned int,	gsi		)
		__field(	int,		level		)
		__field(	int,		irq_source_id	)
	),

	TP_fast_assign(
		__entry->gsi		= gsi;
		__entry->level		= level;
		__entry->irq_source_id	= irq_source_id;
	),

	TP_printk("gsi %u level %d source %d",
		  __entry->gsi, __entry->level, __entry->irq_source_id)
);


#define kvm_irqchips						\
	{KVM_IRQCHIP_PIC_MASTER,	"PIC master"},		\
	{KVM_IRQCHIP_PIC_SLAVE,		"PIC slave"},		\
	{KVM_IRQCHIP_IOAPIC,		"IOAPIC"}

TRACE_EVENT(kvm_ack_irq,
	TP_PROTO(unsigned int irqchip, unsigned int pin),
	TP_ARGS(irqchip, pin),

	TP_STRUCT__entry(
		__field(	unsigned int,	irqchip		)
		__field(	unsigned int,	pin		)
	),

	TP_fast_assign(
		__entry->irqchip	= irqchip;
		__entry->pin		= pin;
	),

	TP_printk("irqchip %s pin %u",
		  __print_symbolic(__entry->irqchip, kvm_irqchips),
		 __entry->pin)
);



#endif /* defined(__KVM_HAVE_IOAPIC) */
#endif /* _TRACE_KVM_MAIN_H */

/* This part must be outside protection */
#include <trace/define_trace.h>
