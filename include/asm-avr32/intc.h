#ifndef __ASM_AVR32_INTC_H
#define __ASM_AVR32_INTC_H

#include <linux/sysdev.h>
#include <linux/interrupt.h>

struct irq_controller;
struct irqaction;
struct pt_regs;

struct platform_device;

/* Information about the internal interrupt controller */
struct intc_device {
	/* ioremapped address of configuration block */
	void __iomem *regs;

	/* the physical device */
	struct platform_device *pdev;

	/* Number of interrupt lines per group. */
	unsigned int irqs_per_group;

	/* The highest group ID + 1 */
	unsigned int nr_groups;

	/*
	 * Bitfield indicating which groups are actually in use.  The
	 * size of the array is
	 * ceil(group_max / (8 * sizeof(unsigned int))).
	 */
	unsigned int group_mask[];
};

struct irq_controller_class {
	/*
	 * A short name identifying this kind of controller.
	 */
	const char *typename;
	/*
	 * Handle the IRQ.  Must do any necessary acking and masking.
	 */
	irqreturn_t (*handle)(int irq, void *dev_id, struct pt_regs *regs);
	/*
	 * Register a new IRQ handler.
	 */
	int (*setup)(struct irq_controller *ctrl, unsigned int irq,
		     struct irqaction *action);
	/*
	 * Unregister a IRQ handler.
	 */
	void (*free)(struct irq_controller *ctrl, unsigned int irq,
		     void *dev_id);
	/*
	 * Mask the IRQ in the interrupt controller.
	 */
	void (*mask)(struct irq_controller *ctrl, unsigned int irq);
	/*
	 * Unmask the IRQ in the interrupt controller.
	 */
	void (*unmask)(struct irq_controller *ctrl, unsigned int irq);
	/*
	 * Set the type of the IRQ. See below for possible types.
	 * Return -EINVAL if a given type is not supported
	 */
	int (*set_type)(struct irq_controller *ctrl, unsigned int irq,
			unsigned int type);
	/*
	 * Return the IRQ type currently set
	 */
	unsigned int (*get_type)(struct irq_controller *ctrl, unsigned int irq);
};

struct irq_controller {
	struct irq_controller_class *class;
	unsigned int irq_group;
	unsigned int first_irq;
	unsigned int nr_irqs;
	struct list_head list;
};

struct intc_group_desc {
	struct irq_controller *ctrl;
	irqreturn_t (*handle)(int, void *, struct pt_regs *);
	unsigned long flags;
	void *dev_id;
	const char *devname;
};

/*
 * The internal interrupt controller.  Defined in board/part-specific
 * devices.c.
 * TODO: Should probably be defined per-cpu.
 */
extern struct intc_device intc;

extern int request_internal_irq(unsigned int irq,
				irqreturn_t (*handler)(int, void *, struct pt_regs *),
				unsigned long irqflags,
				const char *devname, void *dev_id);
extern void free_internal_irq(unsigned int irq);

/* Only used by time_init() */
extern int setup_internal_irq(unsigned int irq, struct intc_group_desc *desc);

/*
 * Set interrupt priority for a given group. `group' can be found by
 * using irq_to_group(irq). Priority can be from 0 (lowest) to 3
 * (highest). Higher-priority interrupts will preempt lower-priority
 * interrupts (unless interrupts are masked globally).
 *
 * This function does not check for conflicts within a group.
 */
extern int intc_set_priority(unsigned int group,
			     unsigned int priority);

/*
 * Returns a bitmask of pending interrupts in a group.
 */
extern unsigned long intc_get_pending(unsigned int group);

/*
 * Register a new external interrupt controller.  Returns the first
 * external IRQ number that is assigned to the new controller.
 */
extern int intc_register_controller(struct irq_controller *ctrl);

#endif /* __ASM_AVR32_INTC_H */
