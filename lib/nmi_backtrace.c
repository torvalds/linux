// SPDX-License-Identifier: GPL-2.0
/*
 *  NMI backtrace support
 *
 * Gratuitously copied from arch/x86/kernel/apic/hw_nmi.c by Russell King,
 * with the following header:
 *
 *  HW NMI watchdog support
 *
 *  started by Don Zickus, Copyright (C) 2010 Red Hat, Inc.
 *
 *  Arch specific calls to support NMI watchdog
 *
 *  Bits copied from original nmi.c file
 */
#include <linux/cpumask.h>
#include <linux/delay.h>
#include <linux/kprobes.h>
#include <linux/nmi.h>
#include <linux/cpu.h>
#include <linux/sched/debug.h>

#ifdef arch_trigger_cpumask_backtrace
/* For reliability, we're prepared to waste bits here. */
static DECLARE_BITMAP(backtrace_mask, NR_CPUS) __read_mostly;

/* "in progress" flag of arch_trigger_cpumask_backtrace */
static unsigned long backtrace_flag;

/*
 * When raise() is called it will be passed a pointer to the
 * backtrace_mask. Architectures that call nmi_cpu_backtrace()
 * directly from their raise() functions may rely on the mask
 * they are passed being updated as a side effect of this call.
 */
void nmi_trigger_cpumask_backtrace(const cpumask_t *mask,
				   bool exclude_self,
				   void (*raise)(cpumask_t *mask))
{
	int i, this_cpu = get_cpu();

	if (test_and_set_bit(0, &backtrace_flag)) {
		/*
		 * If there is already a trigger_all_cpu_backtrace() in progress
		 * (backtrace_flag == 1), don't output double cpu dump infos.
		 */
		put_cpu();
		return;
	}

	cpumask_copy(to_cpumask(backtrace_mask), mask);
	if (exclude_self)
		cpumask_clear_cpu(this_cpu, to_cpumask(backtrace_mask));

	/*
	 * Don't try to send an NMI to this cpu; it may work on some
	 * architectures, but on others it may not, and we'll get
	 * information at least as useful just by doing a dump_stack() here.
	 * Note that nmi_cpu_backtrace(NULL) will clear the cpu bit.
	 */
	if (cpumask_test_cpu(this_cpu, to_cpumask(backtrace_mask)))
		nmi_cpu_backtrace(NULL);

	if (!cpumask_empty(to_cpumask(backtrace_mask))) {
		pr_info("Sending NMI from CPU %d to CPUs %*pbl:\n",
			this_cpu, nr_cpumask_bits, to_cpumask(backtrace_mask));
		raise(to_cpumask(backtrace_mask));
	}

	/* Wait for up to 10 seconds for all CPUs to do the backtrace */
	for (i = 0; i < 10 * 1000; i++) {
		if (cpumask_empty(to_cpumask(backtrace_mask)))
			break;
		mdelay(1);
		touch_softlockup_watchdog();
	}

	/*
	 * Force flush any remote buffers that might be stuck in IRQ context
	 * and therefore could not run their irq_work.
	 */
	printk_trigger_flush();

	clear_bit_unlock(0, &backtrace_flag);
	put_cpu();
}

// Dump stacks even for idle CPUs.
static bool backtrace_idle;
module_param(backtrace_idle, bool, 0644);

bool nmi_cpu_backtrace(struct pt_regs *regs)
{
	int cpu = smp_processor_id();
	unsigned long flags;

	if (cpumask_test_cpu(cpu, to_cpumask(backtrace_mask))) {
		/*
		 * Allow nested NMI backtraces while serializing
		 * against other CPUs.
		 */
		printk_cpu_lock_irqsave(flags);
		if (!READ_ONCE(backtrace_idle) && regs && cpu_in_idle(instruction_pointer(regs))) {
			pr_warn("NMI backtrace for cpu %d skipped: idling at %pS\n",
				cpu, (void *)instruction_pointer(regs));
		} else {
			pr_warn("NMI backtrace for cpu %d\n", cpu);
			if (regs)
				show_regs(regs);
			else
				dump_stack();
		}
		printk_cpu_unlock_irqrestore(flags);
		cpumask_clear_cpu(cpu, to_cpumask(backtrace_mask));
		return true;
	}

	return false;
}
NOKPROBE_SYMBOL(nmi_cpu_backtrace);
#endif
