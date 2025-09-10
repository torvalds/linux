// SPDX-License-Identifier: GPL-2.0-only
/*
 * IRQ offload/bypass manager
 *
 * Copyright (C) 2015 Red Hat, Inc.
 * Copyright (c) 2015 Linaro Ltd.
 *
 * Various virtualization hardware acceleration techniques allow bypassing or
 * offloading interrupts received from devices around the host kernel.  Posted
 * Interrupts on Intel VT-d systems can allow interrupts to be received
 * directly by a virtual machine.  ARM IRQ Forwarding allows forwarded physical
 * interrupts to be directly deactivated by the guest.  This manager allows
 * interrupt producers and consumers to find each other to enable this sort of
 * bypass.
 */

#include <linux/irqbypass.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/mutex.h>

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("IRQ bypass manager utility module");

static DEFINE_XARRAY(producers);
static DEFINE_XARRAY(consumers);
static DEFINE_MUTEX(lock);

/* @lock must be held when calling connect */
static int __connect(struct irq_bypass_producer *prod,
		     struct irq_bypass_consumer *cons)
{
	int ret = 0;

	if (prod->stop)
		prod->stop(prod);
	if (cons->stop)
		cons->stop(cons);

	if (prod->add_consumer)
		ret = prod->add_consumer(prod, cons);

	if (!ret) {
		ret = cons->add_producer(cons, prod);
		if (ret && prod->del_consumer)
			prod->del_consumer(prod, cons);
	}

	if (cons->start)
		cons->start(cons);
	if (prod->start)
		prod->start(prod);

	if (!ret) {
		prod->consumer = cons;
		cons->producer = prod;
	}
	return ret;
}

/* @lock must be held when calling disconnect */
static void __disconnect(struct irq_bypass_producer *prod,
			 struct irq_bypass_consumer *cons)
{
	if (prod->stop)
		prod->stop(prod);
	if (cons->stop)
		cons->stop(cons);

	cons->del_producer(cons, prod);

	if (prod->del_consumer)
		prod->del_consumer(prod, cons);

	if (cons->start)
		cons->start(cons);
	if (prod->start)
		prod->start(prod);

	prod->consumer = NULL;
	cons->producer = NULL;
}

/**
 * irq_bypass_register_producer - register IRQ bypass producer
 * @producer: pointer to producer structure
 * @eventfd: pointer to the eventfd context associated with the producer
 * @irq: Linux IRQ number of the underlying producer device
 *
 * Add the provided IRQ producer to the set of producers and connect with the
 * consumer with a matching eventfd, if one exists.
 */
int irq_bypass_register_producer(struct irq_bypass_producer *producer,
				 struct eventfd_ctx *eventfd, int irq)
{
	unsigned long index = (unsigned long)eventfd;
	struct irq_bypass_consumer *consumer;
	int ret;

	if (WARN_ON_ONCE(producer->eventfd))
		return -EINVAL;

	producer->irq = irq;

	guard(mutex)(&lock);

	ret = xa_insert(&producers, index, producer, GFP_KERNEL);
	if (ret)
		return ret;

	consumer = xa_load(&consumers, index);
	if (consumer) {
		ret = __connect(producer, consumer);
		if (ret) {
			WARN_ON_ONCE(xa_erase(&producers, index) != producer);
			return ret;
		}
	}

	producer->eventfd = eventfd;
	return 0;
}
EXPORT_SYMBOL_GPL(irq_bypass_register_producer);

/**
 * irq_bypass_unregister_producer - unregister IRQ bypass producer
 * @producer: pointer to producer structure
 *
 * Remove a previously registered IRQ producer (note, it's safe to call this
 * even if registration was unsuccessful).  Disconnect from the associated
 * consumer, if one exists.
 */
void irq_bypass_unregister_producer(struct irq_bypass_producer *producer)
{
	unsigned long index = (unsigned long)producer->eventfd;

	if (!producer->eventfd)
		return;

	guard(mutex)(&lock);

	if (producer->consumer)
		__disconnect(producer, producer->consumer);

	WARN_ON_ONCE(xa_erase(&producers, index) != producer);
	producer->eventfd = NULL;
}
EXPORT_SYMBOL_GPL(irq_bypass_unregister_producer);

/**
 * irq_bypass_register_consumer - register IRQ bypass consumer
 * @consumer: pointer to consumer structure
 * @eventfd: pointer to the eventfd context associated with the consumer
 *
 * Add the provided IRQ consumer to the set of consumers and connect with the
 * producer with a matching eventfd, if one exists.
 */
int irq_bypass_register_consumer(struct irq_bypass_consumer *consumer,
				 struct eventfd_ctx *eventfd)
{
	unsigned long index = (unsigned long)eventfd;
	struct irq_bypass_producer *producer;
	int ret;

	if (WARN_ON_ONCE(consumer->eventfd))
		return -EINVAL;

	if (!consumer->add_producer || !consumer->del_producer)
		return -EINVAL;

	guard(mutex)(&lock);

	ret = xa_insert(&consumers, index, consumer, GFP_KERNEL);
	if (ret)
		return ret;

	producer = xa_load(&producers, index);
	if (producer) {
		ret = __connect(producer, consumer);
		if (ret) {
			WARN_ON_ONCE(xa_erase(&consumers, index) != consumer);
			return ret;
		}
	}

	consumer->eventfd = eventfd;
	return 0;
}
EXPORT_SYMBOL_GPL(irq_bypass_register_consumer);

/**
 * irq_bypass_unregister_consumer - unregister IRQ bypass consumer
 * @consumer: pointer to consumer structure
 *
 * Remove a previously registered IRQ consumer (note, it's safe to call this
 * even if registration was unsuccessful).  Disconnect from the associated
 * producer, if one exists.
 */
void irq_bypass_unregister_consumer(struct irq_bypass_consumer *consumer)
{
	unsigned long index = (unsigned long)consumer->eventfd;

	if (!consumer->eventfd)
		return;

	guard(mutex)(&lock);

	if (consumer->producer)
		__disconnect(consumer->producer, consumer);

	WARN_ON_ONCE(xa_erase(&consumers, index) != consumer);
	consumer->eventfd = NULL;
}
EXPORT_SYMBOL_GPL(irq_bypass_unregister_consumer);
