// SPDX-License-Identifier: GPL-2.0

#include <linux/cpu.h>
#include <linux/cpumask.h>
#include <linux/kernel.h>
#include <linux/nmi.h>
#include <linux/percpu-defs.h>

static cpumask_t __read_mostly watchdog_cpus;

static unsigned int watchdog_next_cpu(unsigned int cpu)
{
	unsigned int next_cpu;

	next_cpu = cpumask_next(cpu, &watchdog_cpus);
	if (next_cpu >= nr_cpu_ids)
		next_cpu = cpumask_first(&watchdog_cpus);

	if (next_cpu == cpu)
		return nr_cpu_ids;

	return next_cpu;
}

int __init watchdog_hardlockup_probe(void)
{
	return 0;
}

void watchdog_hardlockup_enable(unsigned int cpu)
{
	unsigned int next_cpu;

	/*
	 * The new CPU will be marked online before the hrtimer interrupt
	 * gets a chance to run on it. If another CPU tests for a
	 * hardlockup on the new CPU before it has run its the hrtimer
	 * interrupt, it will get a false positive. Touch the watchdog on
	 * the new CPU to delay the check for at least 3 sampling periods
	 * to guarantee one hrtimer has run on the new CPU.
	 */
	watchdog_hardlockup_touch_cpu(cpu);

	/*
	 * We are going to check the next CPU. Our watchdog_hrtimer
	 * need not be zero if the CPU has already been online earlier.
	 * Touch the watchdog on the next CPU to avoid false positive
	 * if we try to check it in less then 3 interrupts.
	 */
	next_cpu = watchdog_next_cpu(cpu);
	if (next_cpu < nr_cpu_ids)
		watchdog_hardlockup_touch_cpu(next_cpu);

	/*
	 * Makes sure that watchdog is touched on this CPU before
	 * other CPUs could see it in watchdog_cpus. The counter
	 * part is in watchdog_buddy_check_hardlockup().
	 */
	smp_wmb();

	cpumask_set_cpu(cpu, &watchdog_cpus);
}

void watchdog_hardlockup_disable(unsigned int cpu)
{
	unsigned int next_cpu = watchdog_next_cpu(cpu);

	/*
	 * Offlining this CPU will cause the CPU before this one to start
	 * checking the one after this one. If this CPU just finished checking
	 * the next CPU and updating hrtimer_interrupts_saved, and then the
	 * previous CPU checks it within one sample period, it will trigger a
	 * false positive. Touch the watchdog on the next CPU to prevent it.
	 */
	if (next_cpu < nr_cpu_ids)
		watchdog_hardlockup_touch_cpu(next_cpu);

	/*
	 * Makes sure that watchdog is touched on the next CPU before
	 * this CPU disappear in watchdog_cpus. The counter part is in
	 * watchdog_buddy_check_hardlockup().
	 */
	smp_wmb();

	cpumask_clear_cpu(cpu, &watchdog_cpus);
}

void watchdog_buddy_check_hardlockup(int hrtimer_interrupts)
{
	unsigned int next_cpu;

	/*
	 * Test for hardlockups every 3 samples. The sample period is
	 *  watchdog_thresh * 2 / 5, so 3 samples gets us back to slightly over
	 *  watchdog_thresh (over by 20%).
	 */
	if (hrtimer_interrupts % 3 != 0)
		return;

	/* check for a hardlockup on the next CPU */
	next_cpu = watchdog_next_cpu(smp_processor_id());
	if (next_cpu >= nr_cpu_ids)
		return;

	/*
	 * Make sure that the watchdog was touched on next CPU when
	 * watchdog_next_cpu() returned another one because of
	 * a change in watchdog_hardlockup_enable()/disable().
	 */
	smp_rmb();

	watchdog_hardlockup_check(next_cpu, NULL);
}
