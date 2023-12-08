/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * IRQ offload/bypass manager
 *
 * Copyright (C) 2015 Red Hat, Inc.
 * Copyright (c) 2015 Linaro Ltd.
 */
#ifndef IRQBYPASS_H
#define IRQBYPASS_H

#include <linux/list.h>

struct irq_bypass_consumer;

/*
 * Theory of operation
 *
 * The IRQ bypass manager is a simple set of lists and callbacks that allows
 * IRQ producers (ex. physical interrupt sources) to be matched to IRQ
 * consumers (ex. virtualization hardware that allows IRQ bypass or offload)
 * via a shared token (ex. eventfd_ctx).  Producers and consumers register
 * independently.  When a token match is found, the optional @stop callback
 * will be called for each participant.  The pair will then be connected via
 * the @add_* callbacks, and finally the optional @start callback will allow
 * any final coordination.  When either participant is unregistered, the
 * process is repeated using the @del_* callbacks in place of the @add_*
 * callbacks.  Match tokens must be unique per producer/consumer, 1:N pairings
 * are not supported.
 */

/**
 * struct irq_bypass_producer - IRQ bypass producer definition
 * @node: IRQ bypass manager private list management
 * @token: opaque token to match between producer and consumer (non-NULL)
 * @irq: Linux IRQ number for the producer device
 * @add_consumer: Connect the IRQ producer to an IRQ consumer (optional)
 * @del_consumer: Disconnect the IRQ producer from an IRQ consumer (optional)
 * @stop: Perform any quiesce operations necessary prior to add/del (optional)
 * @start: Perform any startup operations necessary after add/del (optional)
 *
 * The IRQ bypass producer structure represents an interrupt source for
 * participation in possible host bypass, for instance an interrupt vector
 * for a physical device assigned to a VM.
 */
struct irq_bypass_producer {
	struct list_head node;
	void *token;
	int irq;
	int (*add_consumer)(struct irq_bypass_producer *,
			    struct irq_bypass_consumer *);
	void (*del_consumer)(struct irq_bypass_producer *,
			     struct irq_bypass_consumer *);
	void (*stop)(struct irq_bypass_producer *);
	void (*start)(struct irq_bypass_producer *);
};

/**
 * struct irq_bypass_consumer - IRQ bypass consumer definition
 * @node: IRQ bypass manager private list management
 * @token: opaque token to match between producer and consumer (non-NULL)
 * @add_producer: Connect the IRQ consumer to an IRQ producer
 * @del_producer: Disconnect the IRQ consumer from an IRQ producer
 * @stop: Perform any quiesce operations necessary prior to add/del (optional)
 * @start: Perform any startup operations necessary after add/del (optional)
 *
 * The IRQ bypass consumer structure represents an interrupt sink for
 * participation in possible host bypass, for instance a hypervisor may
 * support offloads to allow bypassing the host entirely or offload
 * portions of the interrupt handling to the VM.
 */
struct irq_bypass_consumer {
	struct list_head node;
	void *token;
	int (*add_producer)(struct irq_bypass_consumer *,
			    struct irq_bypass_producer *);
	void (*del_producer)(struct irq_bypass_consumer *,
			     struct irq_bypass_producer *);
	void (*stop)(struct irq_bypass_consumer *);
	void (*start)(struct irq_bypass_consumer *);
};

int irq_bypass_register_producer(struct irq_bypass_producer *);
void irq_bypass_unregister_producer(struct irq_bypass_producer *);
int irq_bypass_register_consumer(struct irq_bypass_consumer *);
void irq_bypass_unregister_consumer(struct irq_bypass_consumer *);

#endif /* IRQBYPASS_H */
