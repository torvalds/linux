/*
 * latency.c: Explicit system-wide latency-expectation infrastructure
 *
 * The purpose of this infrastructure is to allow device drivers to set
 * latency constraint they have and to collect and summarize these
 * expectations globally. The cummulated result can then be used by
 * power management and similar users to make decisions that have
 * tradoffs with a latency component.
 *
 * An example user of this are the x86 C-states; each higher C state saves
 * more power, but has a higher exit latency. For the idle loop power
 * code to make a good decision which C-state to use, information about
 * acceptable latencies is required.
 *
 * An example announcer of latency is an audio driver that knowns it
 * will get an interrupt when the hardware has 200 usec of samples
 * left in the DMA buffer; in that case the driver can set a latency
 * constraint of, say, 150 usec.
 *
 * Multiple drivers can each announce their maximum accepted latency,
 * to keep these appart, a string based identifier is used.
 *
 *
 * (C) Copyright 2006 Intel Corporation
 * Author: Arjan van de Ven <arjan@linux.intel.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; version 2
 * of the License.
 */

#include <linux/latency.h>
#include <linux/list.h>
#include <linux/spinlock.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/notifier.h>
#include <linux/jiffies.h>
#include <asm/atomic.h>

struct latency_info {
	struct list_head list;
	int usecs;
	char *identifier;
};

/*
 * locking rule: all modifications to current_max_latency and
 * latency_list need to be done while holding the latency_lock.
 * latency_lock needs to be taken _irqsave.
 */
static atomic_t current_max_latency;
static DEFINE_SPINLOCK(latency_lock);

static LIST_HEAD(latency_list);
static BLOCKING_NOTIFIER_HEAD(latency_notifier);

/*
 * This function returns the maximum latency allowed, which
 * happens to be the minimum of all maximum latencies on the
 * list.
 */
static int __find_max_latency(void)
{
	int min = INFINITE_LATENCY;
	struct latency_info *info;

	list_for_each_entry(info, &latency_list, list) {
		if (info->usecs < min)
			min = info->usecs;
	}
	return min;
}

/**
 * set_acceptable_latency - sets the maximum latency acceptable
 * @identifier: string that identifies this driver
 * @usecs: maximum acceptable latency for this driver
 *
 * This function informs the kernel that this device(driver)
 * can accept at most usecs latency. This setting is used for
 * power management and similar tradeoffs.
 *
 * This function sleeps and can only be called from process
 * context.
 * Calling this function with an existing identifier is valid
 * and will cause the existing latency setting to be changed.
 */
void set_acceptable_latency(char *identifier, int usecs)
{
	struct latency_info *info, *iter;
	unsigned long flags;
	int found_old = 0;

	info = kzalloc(sizeof(struct latency_info), GFP_KERNEL);
	if (!info)
		return;
	info->usecs = usecs;
	info->identifier = kstrdup(identifier, GFP_KERNEL);
	if (!info->identifier)
		goto free_info;

	spin_lock_irqsave(&latency_lock, flags);
	list_for_each_entry(iter, &latency_list, list) {
		if (strcmp(iter->identifier, identifier)==0) {
			found_old = 1;
			iter->usecs = usecs;
			break;
		}
	}
	if (!found_old)
		list_add(&info->list, &latency_list);

	if (usecs < atomic_read(&current_max_latency))
		atomic_set(&current_max_latency, usecs);

	spin_unlock_irqrestore(&latency_lock, flags);

	blocking_notifier_call_chain(&latency_notifier,
		atomic_read(&current_max_latency), NULL);

	/*
	 * if we inserted the new one, we're done; otherwise there was
	 * an existing one so we need to free the redundant data
	 */
	if (!found_old)
		return;

	kfree(info->identifier);
free_info:
	kfree(info);
}
EXPORT_SYMBOL_GPL(set_acceptable_latency);

/**
 * modify_acceptable_latency - changes the maximum latency acceptable
 * @identifier: string that identifies this driver
 * @usecs: maximum acceptable latency for this driver
 *
 * This function informs the kernel that this device(driver)
 * can accept at most usecs latency. This setting is used for
 * power management and similar tradeoffs.
 *
 * This function does not sleep and can be called in any context.
 * Trying to use a non-existing identifier silently gets ignored.
 *
 * Due to the atomic nature of this function, the modified latency
 * value will only be used for future decisions; past decisions
 * can still lead to longer latencies in the near future.
 */
void modify_acceptable_latency(char *identifier, int usecs)
{
	struct latency_info *iter;
	unsigned long flags;

	spin_lock_irqsave(&latency_lock, flags);
	list_for_each_entry(iter, &latency_list, list) {
		if (strcmp(iter->identifier, identifier) == 0) {
			iter->usecs = usecs;
			break;
		}
	}
	if (usecs < atomic_read(&current_max_latency))
		atomic_set(&current_max_latency, usecs);
	spin_unlock_irqrestore(&latency_lock, flags);
}
EXPORT_SYMBOL_GPL(modify_acceptable_latency);

/**
 * remove_acceptable_latency - removes the maximum latency acceptable
 * @identifier: string that identifies this driver
 *
 * This function removes a previously set maximum latency setting
 * for the driver and frees up any resources associated with the
 * bookkeeping needed for this.
 *
 * This function does not sleep and can be called in any context.
 * Trying to use a non-existing identifier silently gets ignored.
 */
void remove_acceptable_latency(char *identifier)
{
	unsigned long flags;
	int newmax = 0;
	struct latency_info *iter, *temp;

	spin_lock_irqsave(&latency_lock, flags);

	list_for_each_entry_safe(iter,  temp, &latency_list, list) {
		if (strcmp(iter->identifier, identifier) == 0) {
			list_del(&iter->list);
			newmax = iter->usecs;
			kfree(iter->identifier);
			kfree(iter);
			break;
		}
	}

	/* If we just deleted the system wide value, we need to
	 * recalculate with a full search
	 */
	if (newmax == atomic_read(&current_max_latency)) {
		newmax = __find_max_latency();
		atomic_set(&current_max_latency, newmax);
	}
	spin_unlock_irqrestore(&latency_lock, flags);
}
EXPORT_SYMBOL_GPL(remove_acceptable_latency);

/**
 * system_latency_constraint - queries the system wide latency maximum
 *
 * This function returns the system wide maximum latency in
 * microseconds.
 *
 * This function does not sleep and can be called in any context.
 */
int system_latency_constraint(void)
{
	return atomic_read(&current_max_latency);
}
EXPORT_SYMBOL_GPL(system_latency_constraint);

/**
 * synchronize_acceptable_latency - recalculates all latency decisions
 *
 * This function will cause a callback to various kernel pieces that
 * will make those pieces rethink their latency decisions. This implies
 * that if there are overlong latencies in hardware state already, those
 * latencies get taken right now. When this call completes no overlong
 * latency decisions should be active anymore.
 *
 * Typical usecase of this is after a modify_acceptable_latency() call,
 * which in itself is non-blocking and non-synchronizing.
 *
 * This function blocks and should not be called with locks held.
 */

void synchronize_acceptable_latency(void)
{
	blocking_notifier_call_chain(&latency_notifier,
		atomic_read(&current_max_latency), NULL);
}
EXPORT_SYMBOL_GPL(synchronize_acceptable_latency);

/*
 * Latency notifier: this notifier gets called when a non-atomic new
 * latency value gets set. The expectation nof the caller of the
 * non-atomic set is that when the call returns, future latencies
 * are within bounds, so the functions on the notifier list are
 * expected to take the overlong latencies immediately, inside the
 * callback, and not make a overlong latency decision anymore.
 *
 * The callback gets called when the new latency value is made
 * active so system_latency_constraint() returns the new latency.
 */
int register_latency_notifier(struct notifier_block * nb)
{
	return blocking_notifier_chain_register(&latency_notifier, nb);
}
EXPORT_SYMBOL_GPL(register_latency_notifier);

int unregister_latency_notifier(struct notifier_block * nb)
{
	return blocking_notifier_chain_unregister(&latency_notifier, nb);
}
EXPORT_SYMBOL_GPL(unregister_latency_notifier);

static __init int latency_init(void)
{
	atomic_set(&current_max_latency, INFINITE_LATENCY);
	/*
	 * we don't want by default to have longer latencies than 2 ticks,
	 * since that would cause lost ticks
	 */
	set_acceptable_latency("kernel", 2*1000000/HZ);
	return 0;
}

module_init(latency_init);
