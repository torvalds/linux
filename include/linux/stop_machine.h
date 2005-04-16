#ifndef _LINUX_STOP_MACHINE
#define _LINUX_STOP_MACHINE
/* "Bogolock": stop the entire machine, disable interrupts.  This is a
   very heavy lock, which is equivalent to grabbing every spinlock
   (and more).  So the "read" side to such a lock is anything which
   diables preeempt. */
#include <linux/config.h>
#include <linux/cpu.h>
#include <asm/system.h>

#if defined(CONFIG_STOP_MACHINE) && defined(CONFIG_SMP)
/**
 * stop_machine_run: freeze the machine on all CPUs and run this function
 * @fn: the function to run
 * @data: the data ptr for the @fn()
 * @cpu: the cpu to run @fn() on (or any, if @cpu == NR_CPUS.
 *
 * Description: This causes a thread to be scheduled on every other cpu,
 * each of which disables interrupts, and finally interrupts are disabled
 * on the current CPU.  The result is that noone is holding a spinlock
 * or inside any other preempt-disabled region when @fn() runs.
 *
 * This can be thought of as a very heavy write lock, equivalent to
 * grabbing every spinlock in the kernel. */
int stop_machine_run(int (*fn)(void *), void *data, unsigned int cpu);

/**
 * __stop_machine_run: freeze the machine on all CPUs and run this function
 * @fn: the function to run
 * @data: the data ptr for the @fn
 * @cpu: the cpu to run @fn on (or any, if @cpu == NR_CPUS.
 *
 * Description: This is a special version of the above, which returns the
 * thread which has run @fn(): kthread_stop will return the return value
 * of @fn().  Used by hotplug cpu.
 */
struct task_struct *__stop_machine_run(int (*fn)(void *), void *data,
				       unsigned int cpu);

#else

static inline int stop_machine_run(int (*fn)(void *), void *data,
				   unsigned int cpu)
{
	int ret;
	local_irq_disable();
	ret = fn(data);
	local_irq_enable();
	return ret;
}
#endif /* CONFIG_SMP */
#endif /* _LINUX_STOP_MACHINE */
