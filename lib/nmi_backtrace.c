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

#ifdef arch_trigger_all_cpu_backtrace
/* For reliability, we're prepared to waste bits here. */
static DECLARE_BITMAP(backtrace_mask, NR_CPUS) __read_mostly;

/* "in progress" flag of arch_trigger_all_cpu_backtrace */
static unsigned long backtrace_flag;

/*
 * When raise() is called it will be is passed a pointer to the
 * backtrace_mask. Architectures that call nmi_cpu_backtrace()
 * directly from their raise() functions may rely on the mask
 * they are passed being updated as a side effect of this call.
 */
void nmi_trigger_all_cpu_backtrace(bool include_self,
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

	cpumask_copy(to_cpumask(backtrace_mask), cpu_online_mask);
	if (!include_self)
		cpumask_clear_cpu(this_cpu, to_cpumask(backtrace_mask));

	if (!cpumask_empty(to_cpumask(backtrace_mask))) {
		pr_info("Sending NMI to %s CPUs:\n",
			(include_self ? "all" : "other"));
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
	printk_nmi_flush();

	clear_bit_unlock(0, &backtrace_flag);
	put_cpu();
}

bool nmi_cpu_backtrace(struct pt_regs *regs)
{
	int cpu = smp_processor_id();

	if (cpumask_test_cpu(cpu, to_cpumask(backtrace_mask))) {
		pr_warn("NMI backtrace for cpu %d\n", cpu);
		if (regs)
			show_regs(regs);
		else
			dump_stack();
		cpumask_clear_cpu(cpu, to_cpumask(backtrace_mask));
		return true;
	}

	return false;
}
NOKPROBE_SYMBOL(nmi_cpu_backtrace);
#endif
