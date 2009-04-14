/*
 * IRQ subsystem internal functions and variables:
 */

extern int noirqdebug;

/* Set default functions for irq_chip structures: */
extern void irq_chip_set_defaults(struct irq_chip *chip);

/* Set default handler: */
extern void compat_irq_chip_set_default_handler(struct irq_desc *desc);

extern int __irq_set_trigger(struct irq_desc *desc, unsigned int irq,
		unsigned long flags);
extern void __disable_irq(struct irq_desc *desc, unsigned int irq, bool susp);
extern void __enable_irq(struct irq_desc *desc, unsigned int irq, bool resume);

extern struct lock_class_key irq_desc_lock_class;
extern void init_kstat_irqs(struct irq_desc *desc, int cpu, int nr);
extern void clear_kstat_irqs(struct irq_desc *desc);
extern spinlock_t sparse_irq_lock;

#ifdef CONFIG_SPARSE_IRQ
/* irq_desc_ptrs allocated at boot time */
extern struct irq_desc **irq_desc_ptrs;
#else
/* irq_desc_ptrs is a fixed size array */
extern struct irq_desc *irq_desc_ptrs[NR_IRQS];
#endif

#ifdef CONFIG_PROC_FS
extern void register_irq_proc(unsigned int irq, struct irq_desc *desc);
extern void register_handler_proc(unsigned int irq, struct irqaction *action);
extern void unregister_handler_proc(unsigned int irq, struct irqaction *action);
#else
static inline void register_irq_proc(unsigned int irq, struct irq_desc *desc) { }
static inline void register_handler_proc(unsigned int irq,
					 struct irqaction *action) { }
static inline void unregister_handler_proc(unsigned int irq,
					   struct irqaction *action) { }
#endif

extern int irq_select_affinity_usr(unsigned int irq);

/*
 * Debugging printout:
 */

#include <linux/kallsyms.h>

#define P(f) if (desc->status & f) printk("%14s set\n", #f)

static inline void print_irq_desc(unsigned int irq, struct irq_desc *desc)
{
	printk("irq %d, desc: %p, depth: %d, count: %d, unhandled: %d\n",
		irq, desc, desc->depth, desc->irq_count, desc->irqs_unhandled);
	printk("->handle_irq():  %p, ", desc->handle_irq);
	print_symbol("%s\n", (unsigned long)desc->handle_irq);
	printk("->chip(): %p, ", desc->chip);
	print_symbol("%s\n", (unsigned long)desc->chip);
	printk("->action(): %p\n", desc->action);
	if (desc->action) {
		printk("->action->handler(): %p, ", desc->action->handler);
		print_symbol("%s\n", (unsigned long)desc->action->handler);
	}

	P(IRQ_INPROGRESS);
	P(IRQ_DISABLED);
	P(IRQ_PENDING);
	P(IRQ_REPLAY);
	P(IRQ_AUTODETECT);
	P(IRQ_WAITING);
	P(IRQ_LEVEL);
	P(IRQ_MASKED);
#ifdef CONFIG_IRQ_PER_CPU
	P(IRQ_PER_CPU);
#endif
	P(IRQ_NOPROBE);
	P(IRQ_NOREQUEST);
	P(IRQ_NOAUTOEN);
}

#undef P

