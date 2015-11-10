#ifndef IRQ_POLL_H
#define IRQ_POLL_H

struct irq_poll;
typedef int (irq_poll_fn)(struct irq_poll *, int);

struct irq_poll {
	struct list_head list;
	unsigned long state;
	unsigned long data;
	int weight;
	int max;
	irq_poll_fn *poll;
};

enum {
	IRQ_POLL_F_SCHED	= 0,
	IRQ_POLL_F_DISABLE	= 1,
};

/*
 * Returns 0 if we successfully set the IRQ_POLL_F_SCHED bit, indicating
 * that we were the first to acquire this iop for scheduling. If this iop
 * is currently disabled, return "failure".
 */
static inline int irq_poll_sched_prep(struct irq_poll *iop)
{
	if (!test_bit(IRQ_POLL_F_DISABLE, &iop->state))
		return test_and_set_bit(IRQ_POLL_F_SCHED, &iop->state);

	return 1;
}

static inline int irq_poll_disable_pending(struct irq_poll *iop)
{
	return test_bit(IRQ_POLL_F_DISABLE, &iop->state);
}

extern void irq_poll_sched(struct irq_poll *);
extern void irq_poll_init(struct irq_poll *, int, irq_poll_fn *);
extern void irq_poll_complete(struct irq_poll *);
extern void __irq_poll_complete(struct irq_poll *);
extern void irq_poll_enable(struct irq_poll *);
extern void irq_poll_disable(struct irq_poll *);

#endif
