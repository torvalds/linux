/*
 * drivers/power/smp.c - Functions for stopping other CPUs.
 *
 * Copyright 2004 Pavel Machek <pavel@suse.cz>
 * Copyright (C) 2002-2003 Nigel Cunningham <ncunningham@clear.net.nz>
 *
 * This file is released under the GPLv2.
 */

#undef DEBUG

#include <linux/smp_lock.h>
#include <linux/interrupt.h>
#include <linux/suspend.h>
#include <linux/module.h>
#include <asm/atomic.h>
#include <asm/tlbflush.h>

static atomic_t cpu_counter, freeze;


static void smp_pause(void * data)
{
	struct saved_context ctxt;
	__save_processor_state(&ctxt);
	printk("Sleeping in:\n");
	dump_stack();
	atomic_inc(&cpu_counter);
	while (atomic_read(&freeze)) {
		/* FIXME: restore takes place at random piece inside this.
		   This should probably be written in assembly, and
		   preserve general-purpose registers, too

		   What about stack? We may need to move to new stack here.

		   This should better be ran with interrupts disabled.
		 */
		cpu_relax();
		barrier();
	}
	atomic_dec(&cpu_counter);
	__restore_processor_state(&ctxt);
}

static cpumask_t oldmask;

void disable_nonboot_cpus(void)
{
	oldmask = current->cpus_allowed;
	set_cpus_allowed(current, cpumask_of_cpu(0));
	printk("Freezing CPUs (at %d)", _smp_processor_id());
	current->state = TASK_INTERRUPTIBLE;
	schedule_timeout(HZ);
	printk("...");
	BUG_ON(_smp_processor_id() != 0);

	/* FIXME: for this to work, all the CPUs must be running
	 * "idle" thread (or we deadlock). Is that guaranteed? */

	atomic_set(&cpu_counter, 0);
	atomic_set(&freeze, 1);
	smp_call_function(smp_pause, NULL, 0, 0);
	while (atomic_read(&cpu_counter) < (num_online_cpus() - 1)) {
		cpu_relax();
		barrier();
	}
	printk("ok\n");
}

void enable_nonboot_cpus(void)
{
	printk("Restarting CPUs");
	atomic_set(&freeze, 0);
	while (atomic_read(&cpu_counter)) {
		cpu_relax();
		barrier();
	}
	printk("...");
	set_cpus_allowed(current, oldmask);
	schedule();
	printk("ok\n");

}


