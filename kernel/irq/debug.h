/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Debugging printout:
 */

#define ___P(f) if (desc->status_use_accessors & f) printk("%14s set\n", #f)
#define ___PS(f) if (desc->istate & f) printk("%14s set\n", #f)
/* FIXME */
#define ___PD(f) do { } while (0)

static inline void print_irq_desc(unsigned int irq, struct irq_desc *desc)
{
	static DEFINE_RATELIMIT_STATE(ratelimit, 5 * HZ, 5);

	if (!__ratelimit(&ratelimit))
		return;

	printk("irq %d, desc: %p, depth: %d, count: %d, unhandled: %d\n",
		irq, desc, desc->depth, desc->irq_count, desc->irqs_unhandled);
	printk("->handle_irq():  %p, %pS\n",
		desc->handle_irq, desc->handle_irq);
	printk("->irq_data.chip(): %p, %pS\n",
		desc->irq_data.chip, desc->irq_data.chip);
	printk("->action(): %p\n", desc->action);
	if (desc->action) {
		printk("->action->handler(): %p, %pS\n",
			desc->action->handler, desc->action->handler);
	}

	___P(IRQ_LEVEL);
	___P(IRQ_PER_CPU);
	___P(IRQ_NOPROBE);
	___P(IRQ_NOREQUEST);
	___P(IRQ_NOTHREAD);
	___P(IRQ_NOAUTOEN);

	___PS(IRQS_AUTODETECT);
	___PS(IRQS_REPLAY);
	___PS(IRQS_WAITING);
	___PS(IRQS_PENDING);

	___PD(IRQS_INPROGRESS);
	___PD(IRQS_DISABLED);
	___PD(IRQS_MASKED);
}

#undef ___P
#undef ___PS
#undef ___PD
