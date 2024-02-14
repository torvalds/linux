/* SPDX-License-Identifier: GPL-2.0 */
#ifndef IRQ_POLL_H
#define IRQ_POLL_H

struct irq_poll;
typedef int (irq_poll_fn)(struct irq_poll *, int);

struct irq_poll {
	struct list_head list;
	unsigned long state;
	int weight;
	irq_poll_fn *poll;
};

enum {
	IRQ_POLL_F_SCHED	= 0,
	IRQ_POLL_F_DISABLE	= 1,
};

extern void irq_poll_sched(struct irq_poll *);
extern void irq_poll_init(struct irq_poll *, int, irq_poll_fn *);
extern void irq_poll_complete(struct irq_poll *);
extern void irq_poll_enable(struct irq_poll *);
extern void irq_poll_disable(struct irq_poll *);

#endif
