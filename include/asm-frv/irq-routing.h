/* irq-routing.h: multiplexed IRQ routing
 *
 * Copyright (C) 2004 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#ifndef _ASM_IRQ_ROUTING_H
#define _ASM_IRQ_ROUTING_H

#ifndef __ASSEMBLY__

#include <linux/spinlock.h>
#include <asm/irq.h>

struct irq_source;
struct irq_level;

/*
 * IRQ action distribution sets
 */
struct irq_group {
	int			first_irq;	/* first IRQ distributed here */
	void (*control)(struct irq_group *group, int index, int on);

	struct irqaction	*actions[NR_IRQ_ACTIONS_PER_GROUP];	/* IRQ action chains */
	struct irq_source	*sources[NR_IRQ_ACTIONS_PER_GROUP];	/* IRQ sources */
	int			disable_cnt[NR_IRQ_ACTIONS_PER_GROUP];	/* disable counts */
};

/*
 * IRQ source manager
 */
struct irq_source {
	struct irq_source	*next;
	struct irq_level	*level;
	const char		*muxname;
	volatile void __iomem	*muxdata;
	unsigned long		irqmask;

	void (*doirq)(struct irq_source *source);
};

/*
 * IRQ level management (per CPU IRQ priority / entry vector)
 */
struct irq_level {
	int			usage;
	int			disable_count;
	unsigned long		flags;		/* current SA_INTERRUPT and SA_SHIRQ settings */
	spinlock_t		lock;
	struct irq_source	*sources;
};

extern struct irq_level frv_irq_levels[16];
extern struct irq_group *irq_groups[NR_IRQ_GROUPS];

extern void frv_irq_route(struct irq_source *source, int irqlevel);
extern void frv_irq_route_external(struct irq_source *source, int irq);
extern void frv_irq_set_group(struct irq_group *group);
extern void distribute_irqs(struct irq_group *group, unsigned long irqmask);
extern void route_cpu_irqs(void);

#endif /* !__ASSEMBLY__ */

#endif /* _ASM_IRQ_ROUTING_H */
