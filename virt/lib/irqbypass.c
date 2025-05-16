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

static LIST_HEAD(producers);
static LIST_HEAD(consumers);
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
}

/**
 * irq_bypass_register_producer - register IRQ bypass producer
 * @producer: pointer to producer structure
 * @eventfd: pointer to the eventfd context associated with the producer
 *
 * Add the provided IRQ producer to the list of producers and connect
 * with any matching eventfd found on the IRQ consumers list.
 */
int irq_bypass_register_producer(struct irq_bypass_producer *producer,
				 struct eventfd_ctx *eventfd)
{
	struct irq_bypass_producer *tmp;
	struct irq_bypass_consumer *consumer;
	int ret;

	if (WARN_ON_ONCE(producer->eventfd))
		return -EINVAL;

	mutex_lock(&lock);

	list_for_each_entry(tmp, &producers, node) {
		if (tmp->eventfd == eventfd) {
			ret = -EBUSY;
			goto out_err;
		}
	}

	list_for_each_entry(consumer, &consumers, node) {
		if (consumer->eventfd == eventfd) {
			ret = __connect(producer, consumer);
			if (ret)
				goto out_err;
			break;
		}
	}

	producer->eventfd = eventfd;
	list_add(&producer->node, &producers);

	mutex_unlock(&lock);

	return 0;
out_err:
	mutex_unlock(&lock);
	return ret;
}
EXPORT_SYMBOL_GPL(irq_bypass_register_producer);

/**
 * irq_bypass_unregister_producer - unregister IRQ bypass producer
 * @producer: pointer to producer structure
 *
 * Remove a previously registered IRQ producer from the list of producers
 * and disconnect it from any connected IRQ consumer.
 */
void irq_bypass_unregister_producer(struct irq_bypass_producer *producer)
{
	struct irq_bypass_producer *tmp;
	struct irq_bypass_consumer *consumer;

	if (!producer->eventfd)
		return;

	mutex_lock(&lock);

	list_for_each_entry(tmp, &producers, node) {
		if (tmp->eventfd != producer->eventfd)
			continue;

		list_for_each_entry(consumer, &consumers, node) {
			if (consumer->eventfd == producer->eventfd) {
				__disconnect(producer, consumer);
				break;
			}
		}

		producer->eventfd = NULL;
		list_del(&producer->node);
		break;
	}

	WARN_ON_ONCE(producer->eventfd);
	mutex_unlock(&lock);
}
EXPORT_SYMBOL_GPL(irq_bypass_unregister_producer);

/**
 * irq_bypass_register_consumer - register IRQ bypass consumer
 * @consumer: pointer to consumer structure
 * @eventfd: pointer to the eventfd context associated with the consumer
 *
 * Add the provided IRQ consumer to the list of consumers and connect
 * with any matching eventfd found on the IRQ producer list.
 */
int irq_bypass_register_consumer(struct irq_bypass_consumer *consumer,
				 struct eventfd_ctx *eventfd)
{
	struct irq_bypass_consumer *tmp;
	struct irq_bypass_producer *producer;
	int ret;

	if (WARN_ON_ONCE(consumer->eventfd))
		return -EINVAL;

	if (!consumer->add_producer || !consumer->del_producer)
		return -EINVAL;

	mutex_lock(&lock);

	list_for_each_entry(tmp, &consumers, node) {
		if (tmp->eventfd == eventfd) {
			ret = -EBUSY;
			goto out_err;
		}
	}

	list_for_each_entry(producer, &producers, node) {
		if (producer->eventfd == eventfd) {
			ret = __connect(producer, consumer);
			if (ret)
				goto out_err;
			break;
		}
	}

	consumer->eventfd = eventfd;
	list_add(&consumer->node, &consumers);

	mutex_unlock(&lock);

	return 0;
out_err:
	mutex_unlock(&lock);
	return ret;
}
EXPORT_SYMBOL_GPL(irq_bypass_register_consumer);

/**
 * irq_bypass_unregister_consumer - unregister IRQ bypass consumer
 * @consumer: pointer to consumer structure
 *
 * Remove a previously registered IRQ consumer from the list of consumers
 * and disconnect it from any connected IRQ producer.
 */
void irq_bypass_unregister_consumer(struct irq_bypass_consumer *consumer)
{
	struct irq_bypass_consumer *tmp;
	struct irq_bypass_producer *producer;

	if (!consumer->eventfd)
		return;

	mutex_lock(&lock);

	list_for_each_entry(tmp, &consumers, node) {
		if (tmp != consumer)
			continue;

		list_for_each_entry(producer, &producers, node) {
			if (producer->eventfd == consumer->eventfd) {
				__disconnect(producer, consumer);
				break;
			}
		}

		consumer->eventfd = NULL;
		list_del(&consumer->node);
		break;
	}

	WARN_ON_ONCE(consumer->eventfd);
	mutex_unlock(&lock);
}
EXPORT_SYMBOL_GPL(irq_bypass_unregister_consumer);
