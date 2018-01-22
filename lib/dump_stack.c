// SPDX-License-Identifier: GPL-2.0
/*
 * Provide a default dump_stack() function for architectures
 * which don't implement their own.
 */

#include <linux/kernel.h>
#include <linux/export.h>
#include <linux/sched.h>
#include <linux/sched/debug.h>
#include <linux/smp.h>
#include <linux/atomic.h>

static void __dump_stack(void)
{
	dump_stack_print_info(KERN_DEFAULT);
	show_stack(NULL, NULL);
}

/**
 * dump_stack - dump the current task information and its stack trace
 *
 * Architectures can override this implementation by implementing its own.
 */
#ifdef CONFIG_SMP
static atomic_t dump_lock = ATOMIC_INIT(-1);

asmlinkage __visible void dump_stack(void)
{
	unsigned long flags;
	int was_locked;
	int old;
	int cpu;

	/*
	 * Permit this cpu to perform nested stack dumps while serialising
	 * against other CPUs
	 */
retry:
	local_irq_save(flags);
	cpu = smp_processor_id();
	old = atomic_cmpxchg(&dump_lock, -1, cpu);
	if (old == -1) {
		was_locked = 0;
	} else if (old == cpu) {
		was_locked = 1;
	} else {
		local_irq_restore(flags);
		cpu_relax();
		goto retry;
	}

	__dump_stack();

	if (!was_locked)
		atomic_set(&dump_lock, -1);

	local_irq_restore(flags);
}
#else
asmlinkage __visible void dump_stack(void)
{
	__dump_stack();
}
#endif
EXPORT_SYMBOL(dump_stack);
