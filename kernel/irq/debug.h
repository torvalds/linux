/*
 * Debugging printout:
 */

#include <linux/kallsyms.h>

#define P(f) if (desc->status_use_accessors & f) printk("%14s set\n", #f)
#define PS(f) if (desc->istate & f) printk("%14s set\n", #f)
/* FIXME */
#define PD(f) do { } while (0)

static inline void print_irq_desc(unsigned int irq, struct irq_desc *desc)
{
	printk("irq %d, desc: %p, depth: %d, count: %d, unhandled: %d\n",
		irq, desc, desc->depth, desc->irq_count, desc->irqs_unhandled);
	printk("->handle_irq():  %p, ", desc->handle_irq);
	print_symbol("%s\n", (unsigned long)desc->handle_irq);
	printk("->irq_data.chip(): %p, ", desc->irq_data.chip);
	print_symbol("%s\n", (unsigned long)desc->irq_data.chip);
	printk("->action(): %p\n", desc->action);
	if (desc->action) {
		printk("->action->handler(): %p, ", desc->action->handler);
		print_symbol("%s\n", (unsigned long)desc->action->handler);
	}

	P(IRQ_LEVEL);
	P(IRQ_PER_CPU);
	P(IRQ_NOPROBE);
	P(IRQ_NOREQUEST);
	P(IRQ_NOAUTOEN);

	PS(IRQS_AUTODETECT);
	PS(IRQS_REPLAY);
	PS(IRQS_WAITING);
	PS(IRQS_PENDING);

	PD(IRQS_INPROGRESS);
	PD(IRQS_DISABLED);
	PD(IRQS_MASKED);
}

#undef P
#undef PS
#undef PD
