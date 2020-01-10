// SPDX-License-Identifier: GPL-2.0
/*
 * lib/smp_processor_id.c
 *
 * DEBUG_PREEMPT variant of smp_processor_id().
 */
#include <linux/export.h>
#include <linux/kprobes.h>
#include <linux/sched.h>

notrace static nokprobe_inline
unsigned int check_preemption_disabled(const char *what1, const char *what2)
{
	int this_cpu = raw_smp_processor_id();

	if (likely(preempt_count()))
		goto out;

	if (irqs_disabled())
		goto out;

	/*
	 * Kernel threads bound to a single CPU can safely use
	 * smp_processor_id():
	 */
	if (current->nr_cpus_allowed == 1)
		goto out;

	/*
	 * It is valid to assume CPU-locality during early bootup:
	 */
	if (system_state < SYSTEM_SCHEDULING)
		goto out;

	/*
	 * Avoid recursion:
	 */
	preempt_disable_notrace();

	if (!printk_ratelimit())
		goto out_enable;

	printk(KERN_ERR "BUG: using %s%s() in preemptible [%08x] code: %s/%d\n",
		what1, what2, preempt_count() - 1, current->comm, current->pid);

	printk("caller is %pS\n", __builtin_return_address(0));
	dump_stack();

out_enable:
	preempt_enable_no_resched_notrace();
out:
	return this_cpu;
}

notrace unsigned int debug_smp_processor_id(void)
{
	return check_preemption_disabled("smp_processor_id", "");
}
EXPORT_SYMBOL(debug_smp_processor_id);
NOKPROBE_SYMBOL(debug_smp_processor_id);

notrace void __this_cpu_preempt_check(const char *op)
{
	check_preemption_disabled("__this_cpu_", op);
}
EXPORT_SYMBOL(__this_cpu_preempt_check);
NOKPROBE_SYMBOL(__this_cpu_preempt_check);
