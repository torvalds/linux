/*
 * IRQ subsystem internal functions and variables:
 */
#include <linux/irqdesc.h>

extern int noirqdebug;

#define irq_data_to_desc(data)	container_of(data, struct irq_desc, irq_data)

/* Set default functions for irq_chip structures: */
extern void irq_chip_set_defaults(struct irq_chip *chip);

/* Set default handler: */
extern void compat_irq_chip_set_default_handler(struct irq_desc *desc);

extern int __irq_set_trigger(struct irq_desc *desc, unsigned int irq,
		unsigned long flags);
extern void __disable_irq(struct irq_desc *desc, unsigned int irq, bool susp);
extern void __enable_irq(struct irq_desc *desc, unsigned int irq, bool resume);

extern struct lock_class_key irq_desc_lock_class;
extern void init_kstat_irqs(struct irq_desc *desc, int node, int nr);
extern raw_spinlock_t sparse_irq_lock;

/* Resending of interrupts :*/
void check_irq_resend(struct irq_desc *desc, unsigned int irq);

#ifdef CONFIG_SPARSE_IRQ
void replace_irq_desc(unsigned int irq, struct irq_desc *desc);
#endif

#ifdef CONFIG_PROC_FS
extern void register_irq_proc(unsigned int irq, struct irq_desc *desc);
extern void unregister_irq_proc(unsigned int irq, struct irq_desc *desc);
extern void register_handler_proc(unsigned int irq, struct irqaction *action);
extern void unregister_handler_proc(unsigned int irq, struct irqaction *action);
#else
static inline void register_irq_proc(unsigned int irq, struct irq_desc *desc) { }
static inline void unregister_irq_proc(unsigned int irq, struct irq_desc *desc) { }
static inline void register_handler_proc(unsigned int irq,
					 struct irqaction *action) { }
static inline void unregister_handler_proc(unsigned int irq,
					   struct irqaction *action) { }
#endif

extern int irq_select_affinity_usr(unsigned int irq);

extern void irq_set_thread_affinity(struct irq_desc *desc);

#ifndef CONFIG_GENERIC_HARDIRQS_NO_DEPRECATED
static inline void irq_end(unsigned int irq, struct irq_desc *desc)
{
	if (desc->irq_data.chip && desc->irq_data.chip->end)
		desc->irq_data.chip->end(irq);
}
#else
static inline void irq_end(unsigned int irq, struct irq_desc *desc) { }
#endif

/* Inline functions for support of irq chips on slow busses */
static inline void chip_bus_lock(struct irq_desc *desc)
{
	if (unlikely(desc->irq_data.chip->irq_bus_lock))
		desc->irq_data.chip->irq_bus_lock(&desc->irq_data);
}

static inline void chip_bus_sync_unlock(struct irq_desc *desc)
{
	if (unlikely(desc->irq_data.chip->irq_bus_sync_unlock))
		desc->irq_data.chip->irq_bus_sync_unlock(&desc->irq_data);
}

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
	printk("->irq_data.chip(): %p, ", desc->irq_data.chip);
	print_symbol("%s\n", (unsigned long)desc->irq_data.chip);
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

/* Stuff below will be cleaned up after the sparse allocator is done */

#ifdef CONFIG_SMP
/**
 * alloc_desc_masks - allocate cpumasks for irq_desc
 * @desc:	pointer to irq_desc struct
 * @node:	node which will be handling the cpumasks
 * @boot:	true if need bootmem
 *
 * Allocates affinity and pending_mask cpumask if required.
 * Returns true if successful (or not required).
 */
static inline bool alloc_desc_masks(struct irq_desc *desc, int node,
							bool boot)
{
	gfp_t gfp = GFP_ATOMIC;

	if (boot)
		gfp = GFP_NOWAIT;

#ifdef CONFIG_CPUMASK_OFFSTACK
	if (!alloc_cpumask_var_node(&desc->irq_data.affinity, gfp, node))
		return false;

#ifdef CONFIG_GENERIC_PENDING_IRQ
	if (!alloc_cpumask_var_node(&desc->pending_mask, gfp, node)) {
		free_cpumask_var(desc->irq_data.affinity);
		return false;
	}
#endif
#endif
	return true;
}

static inline void init_desc_masks(struct irq_desc *desc)
{
	cpumask_setall(desc->irq_data.affinity);
#ifdef CONFIG_GENERIC_PENDING_IRQ
	cpumask_clear(desc->pending_mask);
#endif
}

/**
 * init_copy_desc_masks - copy cpumasks for irq_desc
 * @old_desc:	pointer to old irq_desc struct
 * @new_desc:	pointer to new irq_desc struct
 *
 * Insures affinity and pending_masks are copied to new irq_desc.
 * If !CONFIG_CPUMASKS_OFFSTACK the cpumasks are embedded in the
 * irq_desc struct so the copy is redundant.
 */

static inline void init_copy_desc_masks(struct irq_desc *old_desc,
					struct irq_desc *new_desc)
{
#ifdef CONFIG_CPUMASK_OFFSTACK
	cpumask_copy(new_desc->irq_data.affinity, old_desc->irq_data.affinity);

#ifdef CONFIG_GENERIC_PENDING_IRQ
	cpumask_copy(new_desc->pending_mask, old_desc->pending_mask);
#endif
#endif
}

static inline void free_desc_masks(struct irq_desc *old_desc,
				   struct irq_desc *new_desc)
{
	free_cpumask_var(old_desc->irq_data.affinity);

#ifdef CONFIG_GENERIC_PENDING_IRQ
	free_cpumask_var(old_desc->pending_mask);
#endif
}

#else /* !CONFIG_SMP */

static inline bool alloc_desc_masks(struct irq_desc *desc, int node,
								bool boot)
{
	return true;
}

static inline void init_desc_masks(struct irq_desc *desc)
{
}

static inline void init_copy_desc_masks(struct irq_desc *old_desc,
					struct irq_desc *new_desc)
{
}

static inline void free_desc_masks(struct irq_desc *old_desc,
				   struct irq_desc *new_desc)
{
}
#endif	/* CONFIG_SMP */
