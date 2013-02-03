#ifndef _LINUX_IRQ_WORK_H
#define _LINUX_IRQ_WORK_H

#include <linux/llist.h>

struct irq_work {
	unsigned long flags;
	struct llist_node llnode;
	void (*func)(struct irq_work *);
};

static inline
void init_irq_work(struct irq_work *work, void (*func)(struct irq_work *))
{
	work->flags = 0;
	work->func = func;
}

void irq_work_queue(struct irq_work *work);
void irq_work_run(void);
void irq_work_sync(struct irq_work *work);

#endif /* _LINUX_IRQ_WORK_H */
