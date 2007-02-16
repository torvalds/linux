/*
 * linux/kernel/time/tick-broadcast.c
 *
 * This file contains functions which emulate a local clock-event
 * device via a broadcast event source.
 *
 * Copyright(C) 2005-2006, Thomas Gleixner <tglx@linutronix.de>
 * Copyright(C) 2005-2007, Red Hat, Inc., Ingo Molnar
 * Copyright(C) 2006-2007, Timesys Corp., Thomas Gleixner
 *
 * This code is licenced under the GPL version 2. For details see
 * kernel-base/COPYING.
 */
#include <linux/cpu.h>
#include <linux/err.h>
#include <linux/hrtimer.h>
#include <linux/irq.h>
#include <linux/percpu.h>
#include <linux/profile.h>
#include <linux/sched.h>
#include <linux/tick.h>

#include "tick-internal.h"

/*
 * Broadcast support for broken x86 hardware, where the local apic
 * timer stops in C3 state.
 */

struct tick_device tick_broadcast_device;
static cpumask_t tick_broadcast_mask;
DEFINE_SPINLOCK(tick_broadcast_lock);

/*
 * Start the device in periodic mode
 */
static void tick_broadcast_start_periodic(struct clock_event_device *bc)
{
	if (bc && bc->mode == CLOCK_EVT_MODE_SHUTDOWN)
		tick_setup_periodic(bc, 1);
}

/*
 * Check, if the device can be utilized as broadcast device:
 */
int tick_check_broadcast_device(struct clock_event_device *dev)
{
	if (tick_broadcast_device.evtdev ||
	    (dev->features & CLOCK_EVT_FEAT_C3STOP))
		return 0;

	clockevents_exchange_device(NULL, dev);
	tick_broadcast_device.evtdev = dev;
	if (!cpus_empty(tick_broadcast_mask))
		tick_broadcast_start_periodic(dev);
	return 1;
}

/*
 * Check, if the device is the broadcast device
 */
int tick_is_broadcast_device(struct clock_event_device *dev)
{
	return (dev && tick_broadcast_device.evtdev == dev);
}

/*
 * Check, if the device is disfunctional and a place holder, which
 * needs to be handled by the broadcast device.
 */
int tick_device_uses_broadcast(struct clock_event_device *dev, int cpu)
{
	unsigned long flags;
	int ret = 0;

	spin_lock_irqsave(&tick_broadcast_lock, flags);

	/*
	 * Devices might be registered with both periodic and oneshot
	 * mode disabled. This signals, that the device needs to be
	 * operated from the broadcast device and is a placeholder for
	 * the cpu local device.
	 */
	if (!tick_device_is_functional(dev)) {
		dev->event_handler = tick_handle_periodic;
		cpu_set(cpu, tick_broadcast_mask);
		tick_broadcast_start_periodic(tick_broadcast_device.evtdev);
		ret = 1;
	}

	spin_unlock_irqrestore(&tick_broadcast_lock, flags);
	return ret;
}

/*
 * Broadcast the event to the cpus, which are set in the mask
 */
int tick_do_broadcast(cpumask_t mask)
{
	int ret = 0, cpu = smp_processor_id();
	struct tick_device *td;

	/*
	 * Check, if the current cpu is in the mask
	 */
	if (cpu_isset(cpu, mask)) {
		cpu_clear(cpu, mask);
		td = &per_cpu(tick_cpu_device, cpu);
		td->evtdev->event_handler(td->evtdev);
		ret = 1;
	}

	if (!cpus_empty(mask)) {
		/*
		 * It might be necessary to actually check whether the devices
		 * have different broadcast functions. For now, just use the
		 * one of the first device. This works as long as we have this
		 * misfeature only on x86 (lapic)
		 */
		cpu = first_cpu(mask);
		td = &per_cpu(tick_cpu_device, cpu);
		td->evtdev->broadcast(mask);
		ret = 1;
	}
	return ret;
}

/*
 * Periodic broadcast:
 * - invoke the broadcast handlers
 */
static void tick_do_periodic_broadcast(void)
{
	cpumask_t mask;

	spin_lock(&tick_broadcast_lock);

	cpus_and(mask, cpu_online_map, tick_broadcast_mask);
	tick_do_broadcast(mask);

	spin_unlock(&tick_broadcast_lock);
}

/*
 * Event handler for periodic broadcast ticks
 */
static void tick_handle_periodic_broadcast(struct clock_event_device *dev)
{
	dev->next_event.tv64 = KTIME_MAX;

	tick_do_periodic_broadcast();

	/*
	 * The device is in periodic mode. No reprogramming necessary:
	 */
	if (dev->mode == CLOCK_EVT_MODE_PERIODIC)
		return;

	/*
	 * Setup the next period for devices, which do not have
	 * periodic mode:
	 */
	for (;;) {
		ktime_t next = ktime_add(dev->next_event, tick_period);

		if (!clockevents_program_event(dev, next, ktime_get()))
			return;
		tick_do_periodic_broadcast();
	}
}

/*
 * Powerstate information: The system enters/leaves a state, where
 * affected devices might stop
 */
static void tick_do_broadcast_on_off(void *why)
{
	struct clock_event_device *bc, *dev;
	struct tick_device *td;
	unsigned long flags, *reason = why;
	int cpu;

	spin_lock_irqsave(&tick_broadcast_lock, flags);

	cpu = smp_processor_id();
	td = &per_cpu(tick_cpu_device, cpu);
	dev = td->evtdev;
	bc = tick_broadcast_device.evtdev;

	/*
	 * Is the device in broadcast mode forever or is it not
	 * affected by the powerstate ?
	 */
	if (!dev || !tick_device_is_functional(dev) ||
	    !(dev->features & CLOCK_EVT_FEAT_C3STOP))
		goto out;

	if (*reason == CLOCK_EVT_NOTIFY_BROADCAST_ON) {
		if (!cpu_isset(cpu, tick_broadcast_mask)) {
			cpu_set(cpu, tick_broadcast_mask);
			if (td->mode == TICKDEV_MODE_PERIODIC)
				clockevents_set_mode(dev,
						     CLOCK_EVT_MODE_SHUTDOWN);
		}
	} else {
		if (cpu_isset(cpu, tick_broadcast_mask)) {
			cpu_clear(cpu, tick_broadcast_mask);
			if (td->mode == TICKDEV_MODE_PERIODIC)
				tick_setup_periodic(dev, 0);
		}
	}

	if (cpus_empty(tick_broadcast_mask))
		clockevents_set_mode(bc, CLOCK_EVT_MODE_SHUTDOWN);
	else {
		if (tick_broadcast_device.mode == TICKDEV_MODE_PERIODIC)
			tick_broadcast_start_periodic(bc);
	}
out:
	spin_unlock_irqrestore(&tick_broadcast_lock, flags);
}

/*
 * Powerstate information: The system enters/leaves a state, where
 * affected devices might stop.
 */
void tick_broadcast_on_off(unsigned long reason, int *oncpu)
{
	int cpu = get_cpu();

	if (cpu == *oncpu)
		tick_do_broadcast_on_off(&reason);
	else
		smp_call_function_single(*oncpu, tick_do_broadcast_on_off,
					 &reason, 1, 1);
	put_cpu();
}

/*
 * Set the periodic handler depending on broadcast on/off
 */
void tick_set_periodic_handler(struct clock_event_device *dev, int broadcast)
{
	if (!broadcast)
		dev->event_handler = tick_handle_periodic;
	else
		dev->event_handler = tick_handle_periodic_broadcast;
}

/*
 * Remove a CPU from broadcasting
 */
void tick_shutdown_broadcast(unsigned int *cpup)
{
	struct clock_event_device *bc;
	unsigned long flags;
	unsigned int cpu = *cpup;

	spin_lock_irqsave(&tick_broadcast_lock, flags);

	bc = tick_broadcast_device.evtdev;
	cpu_clear(cpu, tick_broadcast_mask);

	if (tick_broadcast_device.mode == TICKDEV_MODE_PERIODIC) {
		if (bc && cpus_empty(tick_broadcast_mask))
			clockevents_set_mode(bc, CLOCK_EVT_MODE_SHUTDOWN);
	}

	spin_unlock_irqrestore(&tick_broadcast_lock, flags);
}
