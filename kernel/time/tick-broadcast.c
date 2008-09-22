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
#include <linux/interrupt.h>
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
static DEFINE_SPINLOCK(tick_broadcast_lock);
static int tick_broadcast_force;

#ifdef CONFIG_TICK_ONESHOT
static void tick_broadcast_clear_oneshot(int cpu);
#else
static inline void tick_broadcast_clear_oneshot(int cpu) { }
#endif

/*
 * Debugging: see timer_list.c
 */
struct tick_device *tick_get_broadcast_device(void)
{
	return &tick_broadcast_device;
}

cpumask_t *tick_get_broadcast_mask(void)
{
	return &tick_broadcast_mask;
}

/*
 * Start the device in periodic mode
 */
static void tick_broadcast_start_periodic(struct clock_event_device *bc)
{
	if (bc)
		tick_setup_periodic(bc, 1);
}

/*
 * Check, if the device can be utilized as broadcast device:
 */
int tick_check_broadcast_device(struct clock_event_device *dev)
{
	if ((tick_broadcast_device.evtdev &&
	     tick_broadcast_device.evtdev->rating >= dev->rating) ||
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
	} else {
		/*
		 * When the new device is not affected by the stop
		 * feature and the cpu is marked in the broadcast mask
		 * then clear the broadcast bit.
		 */
		if (!(dev->features & CLOCK_EVT_FEAT_C3STOP)) {
			int cpu = smp_processor_id();

			cpu_clear(cpu, tick_broadcast_mask);
			tick_broadcast_clear_oneshot(cpu);
		}
	}
	spin_unlock_irqrestore(&tick_broadcast_lock, flags);
	return ret;
}

/*
 * Broadcast the event to the cpus, which are set in the mask
 */
static void tick_do_broadcast(cpumask_t mask)
{
	int cpu = smp_processor_id();
	struct tick_device *td;

	/*
	 * Check, if the current cpu is in the mask
	 */
	if (cpu_isset(cpu, mask)) {
		cpu_clear(cpu, mask);
		td = &per_cpu(tick_cpu_device, cpu);
		td->evtdev->event_handler(td->evtdev);
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
	}
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
	ktime_t next;

	tick_do_periodic_broadcast();

	/*
	 * The device is in periodic mode. No reprogramming necessary:
	 */
	if (dev->mode == CLOCK_EVT_MODE_PERIODIC)
		return;

	/*
	 * Setup the next period for devices, which do not have
	 * periodic mode. We read dev->next_event first and add to it
	 * when the event alrady expired. clockevents_program_event()
	 * sets dev->next_event only when the event is really
	 * programmed to the device.
	 */
	for (next = dev->next_event; ;) {
		next = ktime_add(next, tick_period);

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
	int cpu, bc_stopped;

	spin_lock_irqsave(&tick_broadcast_lock, flags);

	cpu = smp_processor_id();
	td = &per_cpu(tick_cpu_device, cpu);
	dev = td->evtdev;
	bc = tick_broadcast_device.evtdev;

	/*
	 * Is the device not affected by the powerstate ?
	 */
	if (!dev || !(dev->features & CLOCK_EVT_FEAT_C3STOP))
		goto out;

	if (!tick_device_is_functional(dev))
		goto out;

	bc_stopped = cpus_empty(tick_broadcast_mask);

	switch (*reason) {
	case CLOCK_EVT_NOTIFY_BROADCAST_ON:
	case CLOCK_EVT_NOTIFY_BROADCAST_FORCE:
		if (!cpu_isset(cpu, tick_broadcast_mask)) {
			cpu_set(cpu, tick_broadcast_mask);
			if (bc->mode == TICKDEV_MODE_PERIODIC)
				clockevents_shutdown(dev);
		}
		if (*reason == CLOCK_EVT_NOTIFY_BROADCAST_FORCE)
			tick_broadcast_force = 1;
		break;
	case CLOCK_EVT_NOTIFY_BROADCAST_OFF:
		if (!tick_broadcast_force &&
		    cpu_isset(cpu, tick_broadcast_mask)) {
			cpu_clear(cpu, tick_broadcast_mask);
			if (bc->mode == TICKDEV_MODE_PERIODIC)
				tick_setup_periodic(dev, 0);
		}
		break;
	}

	if (cpus_empty(tick_broadcast_mask)) {
		if (!bc_stopped)
			clockevents_shutdown(bc);
	} else if (bc_stopped) {
		if (tick_broadcast_device.mode == TICKDEV_MODE_PERIODIC)
			tick_broadcast_start_periodic(bc);
		else
			tick_broadcast_setup_oneshot(bc);
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
	if (!cpu_isset(*oncpu, cpu_online_map))
		printk(KERN_ERR "tick-broadcast: ignoring broadcast for "
		       "offline CPU #%d\n", *oncpu);
	else
		smp_call_function_single(*oncpu, tick_do_broadcast_on_off,
					 &reason, 1);
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
			clockevents_shutdown(bc);
	}

	spin_unlock_irqrestore(&tick_broadcast_lock, flags);
}

void tick_suspend_broadcast(void)
{
	struct clock_event_device *bc;
	unsigned long flags;

	spin_lock_irqsave(&tick_broadcast_lock, flags);

	bc = tick_broadcast_device.evtdev;
	if (bc)
		clockevents_shutdown(bc);

	spin_unlock_irqrestore(&tick_broadcast_lock, flags);
}

int tick_resume_broadcast(void)
{
	struct clock_event_device *bc;
	unsigned long flags;
	int broadcast = 0;

	spin_lock_irqsave(&tick_broadcast_lock, flags);

	bc = tick_broadcast_device.evtdev;

	if (bc) {
		clockevents_set_mode(bc, CLOCK_EVT_MODE_RESUME);

		switch (tick_broadcast_device.mode) {
		case TICKDEV_MODE_PERIODIC:
			if(!cpus_empty(tick_broadcast_mask))
				tick_broadcast_start_periodic(bc);
			broadcast = cpu_isset(smp_processor_id(),
					      tick_broadcast_mask);
			break;
		case TICKDEV_MODE_ONESHOT:
			broadcast = tick_resume_broadcast_oneshot(bc);
			break;
		}
	}
	spin_unlock_irqrestore(&tick_broadcast_lock, flags);

	return broadcast;
}


#ifdef CONFIG_TICK_ONESHOT

static cpumask_t tick_broadcast_oneshot_mask;

/*
 * Debugging: see timer_list.c
 */
cpumask_t *tick_get_broadcast_oneshot_mask(void)
{
	return &tick_broadcast_oneshot_mask;
}

static int tick_broadcast_set_event(ktime_t expires, int force)
{
	struct clock_event_device *bc = tick_broadcast_device.evtdev;

	return tick_dev_program_event(bc, expires, force);
}

int tick_resume_broadcast_oneshot(struct clock_event_device *bc)
{
	clockevents_set_mode(bc, CLOCK_EVT_MODE_ONESHOT);
	return 0;
}

/*
 * Handle oneshot mode broadcasting
 */
static void tick_handle_oneshot_broadcast(struct clock_event_device *dev)
{
	struct tick_device *td;
	cpumask_t mask;
	ktime_t now, next_event;
	int cpu;

	spin_lock(&tick_broadcast_lock);
again:
	dev->next_event.tv64 = KTIME_MAX;
	next_event.tv64 = KTIME_MAX;
	mask = CPU_MASK_NONE;
	now = ktime_get();
	/* Find all expired events */
	for_each_cpu_mask_nr(cpu, tick_broadcast_oneshot_mask) {
		td = &per_cpu(tick_cpu_device, cpu);
		if (td->evtdev->next_event.tv64 <= now.tv64)
			cpu_set(cpu, mask);
		else if (td->evtdev->next_event.tv64 < next_event.tv64)
			next_event.tv64 = td->evtdev->next_event.tv64;
	}

	/*
	 * Wakeup the cpus which have an expired event.
	 */
	tick_do_broadcast(mask);

	/*
	 * Two reasons for reprogram:
	 *
	 * - The global event did not expire any CPU local
	 * events. This happens in dyntick mode, as the maximum PIT
	 * delta is quite small.
	 *
	 * - There are pending events on sleeping CPUs which were not
	 * in the event mask
	 */
	if (next_event.tv64 != KTIME_MAX) {
		/*
		 * Rearm the broadcast device. If event expired,
		 * repeat the above
		 */
		if (tick_broadcast_set_event(next_event, 0))
			goto again;
	}
	spin_unlock(&tick_broadcast_lock);
}

/*
 * Powerstate information: The system enters/leaves a state, where
 * affected devices might stop
 */
void tick_broadcast_oneshot_control(unsigned long reason)
{
	struct clock_event_device *bc, *dev;
	struct tick_device *td;
	unsigned long flags;
	int cpu;

	spin_lock_irqsave(&tick_broadcast_lock, flags);

	/*
	 * Periodic mode does not care about the enter/exit of power
	 * states
	 */
	if (tick_broadcast_device.mode == TICKDEV_MODE_PERIODIC)
		goto out;

	bc = tick_broadcast_device.evtdev;
	cpu = smp_processor_id();
	td = &per_cpu(tick_cpu_device, cpu);
	dev = td->evtdev;

	if (!(dev->features & CLOCK_EVT_FEAT_C3STOP))
		goto out;

	if (reason == CLOCK_EVT_NOTIFY_BROADCAST_ENTER) {
		if (!cpu_isset(cpu, tick_broadcast_oneshot_mask)) {
			cpu_set(cpu, tick_broadcast_oneshot_mask);
			clockevents_set_mode(dev, CLOCK_EVT_MODE_SHUTDOWN);
			if (dev->next_event.tv64 < bc->next_event.tv64)
				tick_broadcast_set_event(dev->next_event, 1);
		}
	} else {
		if (cpu_isset(cpu, tick_broadcast_oneshot_mask)) {
			cpu_clear(cpu, tick_broadcast_oneshot_mask);
			clockevents_set_mode(dev, CLOCK_EVT_MODE_ONESHOT);
			if (dev->next_event.tv64 != KTIME_MAX)
				tick_program_event(dev->next_event, 1);
		}
	}

out:
	spin_unlock_irqrestore(&tick_broadcast_lock, flags);
}

/*
 * Reset the one shot broadcast for a cpu
 *
 * Called with tick_broadcast_lock held
 */
static void tick_broadcast_clear_oneshot(int cpu)
{
	cpu_clear(cpu, tick_broadcast_oneshot_mask);
}

static void tick_broadcast_init_next_event(cpumask_t *mask, ktime_t expires)
{
	struct tick_device *td;
	int cpu;

	for_each_cpu_mask_nr(cpu, *mask) {
		td = &per_cpu(tick_cpu_device, cpu);
		if (td->evtdev)
			td->evtdev->next_event = expires;
	}
}

/**
 * tick_broadcast_setup_oneshot - setup the broadcast device
 */
void tick_broadcast_setup_oneshot(struct clock_event_device *bc)
{
	/* Set it up only once ! */
	if (bc->event_handler != tick_handle_oneshot_broadcast) {
		int was_periodic = bc->mode == CLOCK_EVT_MODE_PERIODIC;
		int cpu = smp_processor_id();
		cpumask_t mask;

		bc->event_handler = tick_handle_oneshot_broadcast;
		clockevents_set_mode(bc, CLOCK_EVT_MODE_ONESHOT);

		/* Take the do_timer update */
		tick_do_timer_cpu = cpu;

		/*
		 * We must be careful here. There might be other CPUs
		 * waiting for periodic broadcast. We need to set the
		 * oneshot_mask bits for those and program the
		 * broadcast device to fire.
		 */
		mask = tick_broadcast_mask;
		cpu_clear(cpu, mask);
		cpus_or(tick_broadcast_oneshot_mask,
			tick_broadcast_oneshot_mask, mask);

		if (was_periodic && !cpus_empty(mask)) {
			tick_broadcast_init_next_event(&mask, tick_next_period);
			tick_broadcast_set_event(tick_next_period, 1);
		} else
			bc->next_event.tv64 = KTIME_MAX;
	}
}

/*
 * Select oneshot operating mode for the broadcast device
 */
void tick_broadcast_switch_to_oneshot(void)
{
	struct clock_event_device *bc;
	unsigned long flags;

	spin_lock_irqsave(&tick_broadcast_lock, flags);

	tick_broadcast_device.mode = TICKDEV_MODE_ONESHOT;
	bc = tick_broadcast_device.evtdev;
	if (bc)
		tick_broadcast_setup_oneshot(bc);
	spin_unlock_irqrestore(&tick_broadcast_lock, flags);
}


/*
 * Remove a dead CPU from broadcasting
 */
void tick_shutdown_broadcast_oneshot(unsigned int *cpup)
{
	unsigned long flags;
	unsigned int cpu = *cpup;

	spin_lock_irqsave(&tick_broadcast_lock, flags);

	/*
	 * Clear the broadcast mask flag for the dead cpu, but do not
	 * stop the broadcast device!
	 */
	cpu_clear(cpu, tick_broadcast_oneshot_mask);

	spin_unlock_irqrestore(&tick_broadcast_lock, flags);
}

#endif
