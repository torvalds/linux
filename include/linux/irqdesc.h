#ifndef _LINUX_IRQDESC_H
#define _LINUX_IRQDESC_H

/*
 * Core internal functions to deal with irq descriptors
 *
 * This include will move to kernel/irq once we cleaned up the tree.
 * For now it's included from <linux/irq.h>
 */

struct proc_dir_entry;
struct timer_rand_state;
/**
 * struct irq_desc - interrupt descriptor
 * @irq_data:		per irq and chip data passed down to chip functions
 * @timer_rand_state:	pointer to timer rand state struct
 * @kstat_irqs:		irq stats per cpu
 * @handle_irq:		highlevel irq-events handler [if NULL, __do_IRQ()]
 * @action:		the irq action chain
 * @status:		status information
 * @depth:		disable-depth, for nested irq_disable() calls
 * @wake_depth:		enable depth, for multiple set_irq_wake() callers
 * @irq_count:		stats field to detect stalled irqs
 * @last_unhandled:	aging timer for unhandled count
 * @irqs_unhandled:	stats field for spurious unhandled interrupts
 * @lock:		locking for SMP
 * @pending_mask:	pending rebalanced interrupts
 * @threads_active:	number of irqaction threads currently running
 * @wait_for_threads:	wait queue for sync_irq to wait for threaded handlers
 * @dir:		/proc/irq/ procfs entry
 * @name:		flow handler name for /proc/interrupts output
 */
struct irq_desc {

#ifdef CONFIG_GENERIC_HARDIRQS_NO_DEPRECATED
	struct irq_data		irq_data;
#else
	/*
	 * This union will go away, once we fixed the direct access to
	 * irq_desc all over the place. The direct fields are a 1:1
	 * overlay of irq_data.
	 */
	union {
		struct irq_data		irq_data;
		struct {
			unsigned int		irq;
			unsigned int		node;
			struct irq_chip		*chip;
			void			*handler_data;
			void			*chip_data;
			struct msi_desc		*msi_desc;
#ifdef CONFIG_SMP
			cpumask_var_t		affinity;
#endif
		};
	};
#endif

	struct timer_rand_state *timer_rand_state;
	unsigned int __percpu	*kstat_irqs;
	irq_flow_handler_t	handle_irq;
	struct irqaction	*action;	/* IRQ action list */
	unsigned int		status;		/* IRQ status */

	unsigned int		depth;		/* nested irq disables */
	unsigned int		wake_depth;	/* nested wake enables */
	unsigned int		irq_count;	/* For detecting broken IRQs */
	unsigned long		last_unhandled;	/* Aging timer for unhandled count */
	unsigned int		irqs_unhandled;
	raw_spinlock_t		lock;
#ifdef CONFIG_SMP
	const struct cpumask	*affinity_hint;
#ifdef CONFIG_GENERIC_PENDING_IRQ
	cpumask_var_t		pending_mask;
#endif
#endif
	atomic_t		threads_active;
	wait_queue_head_t       wait_for_threads;
#ifdef CONFIG_PROC_FS
	struct proc_dir_entry	*dir;
#endif
	const char		*name;
} ____cacheline_internodealigned_in_smp;

#ifndef CONFIG_SPARSE_IRQ
extern struct irq_desc irq_desc[NR_IRQS];
#endif

/* Will be removed once the last users in power and sh are gone */
extern struct irq_desc *irq_to_desc_alloc_node(unsigned int irq, int node);
static inline struct irq_desc *move_irq_desc(struct irq_desc *desc, int node)
{
	return desc;
}

#ifdef CONFIG_GENERIC_HARDIRQS

#define get_irq_desc_chip(desc)		((desc)->irq_data.chip)
#define get_irq_desc_chip_data(desc)	((desc)->irq_data.chip_data)
#define get_irq_desc_data(desc)		((desc)->irq_data.handler_data)
#define get_irq_desc_msi(desc)		((desc)->irq_data.msi_desc)

/*
 * Architectures call this to let the generic IRQ layer
 * handle an interrupt. If the descriptor is attached to an
 * irqchip-style controller then we call the ->handle_irq() handler,
 * and it calls __do_IRQ() if it's attached to an irqtype-style controller.
 */
static inline void generic_handle_irq_desc(unsigned int irq, struct irq_desc *desc)
{
	desc->handle_irq(irq, desc);
}

static inline void generic_handle_irq(unsigned int irq)
{
	generic_handle_irq_desc(irq, irq_to_desc(irq));
}

/* Test to see if a driver has successfully requested an irq */
static inline int irq_has_action(unsigned int irq)
{
	struct irq_desc *desc = irq_to_desc(irq);
	return desc->action != NULL;
}

static inline int irq_balancing_disabled(unsigned int irq)
{
	struct irq_desc *desc;

	desc = irq_to_desc(irq);
	return desc->status & IRQ_NO_BALANCING_MASK;
}

/* caller has locked the irq_desc and both params are valid */
static inline void __set_irq_handler_unlocked(int irq,
					      irq_flow_handler_t handler)
{
	struct irq_desc *desc;

	desc = irq_to_desc(irq);
	desc->handle_irq = handler;
}
#endif

#endif
