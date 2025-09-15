/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_STOP_MACHINE
#define _LINUX_STOP_MACHINE

#include <linux/cpu.h>
#include <linux/cpumask_types.h>
#include <linux/smp.h>
#include <linux/list.h>

/*
 * stop_cpu[s]() is simplistic per-cpu maximum priority cpu
 * monopolization mechanism.  The caller can specify a non-sleeping
 * function to be executed on a single or multiple cpus preempting all
 * other processes and monopolizing those cpus until it finishes.
 *
 * Resources for this mechanism are preallocated when a cpu is brought
 * up and requests are guaranteed to be served as long as the target
 * cpus are online.
 */
typedef int (*cpu_stop_fn_t)(void *arg);

#ifdef CONFIG_SMP

struct cpu_stop_work {
	struct list_head	list;		/* cpu_stopper->works */
	cpu_stop_fn_t		fn;
	unsigned long		caller;
	void			*arg;
	struct cpu_stop_done	*done;
};

int stop_one_cpu(unsigned int cpu, cpu_stop_fn_t fn, void *arg);
int stop_two_cpus(unsigned int cpu1, unsigned int cpu2, cpu_stop_fn_t fn, void *arg);
bool stop_one_cpu_nowait(unsigned int cpu, cpu_stop_fn_t fn, void *arg,
			 struct cpu_stop_work *work_buf);
void stop_machine_park(int cpu);
void stop_machine_unpark(int cpu);
void stop_machine_yield(const struct cpumask *cpumask);

extern void print_stop_info(const char *log_lvl, struct task_struct *task);

#else	/* CONFIG_SMP */

#include <linux/workqueue.h>

struct cpu_stop_work {
	struct work_struct	work;
	cpu_stop_fn_t		fn;
	void			*arg;
};

static inline int stop_one_cpu(unsigned int cpu, cpu_stop_fn_t fn, void *arg)
{
	int ret = -ENOENT;
	preempt_disable();
	if (cpu == smp_processor_id())
		ret = fn(arg);
	preempt_enable();
	return ret;
}

static void stop_one_cpu_nowait_workfn(struct work_struct *work)
{
	struct cpu_stop_work *stwork =
		container_of(work, struct cpu_stop_work, work);
	preempt_disable();
	stwork->fn(stwork->arg);
	preempt_enable();
}

static inline bool stop_one_cpu_nowait(unsigned int cpu,
				       cpu_stop_fn_t fn, void *arg,
				       struct cpu_stop_work *work_buf)
{
	if (cpu == smp_processor_id()) {
		INIT_WORK(&work_buf->work, stop_one_cpu_nowait_workfn);
		work_buf->fn = fn;
		work_buf->arg = arg;
		schedule_work(&work_buf->work);
		return true;
	}

	return false;
}

static inline void print_stop_info(const char *log_lvl, struct task_struct *task) { }

#endif	/* CONFIG_SMP */

/*
 * stop_machine "Bogolock": stop the entire machine, disable interrupts.
 * This is a very heavy lock, which is equivalent to grabbing every raw
 * spinlock (and more).  So the "read" side to such a lock is anything
 * which disables preemption.
 */
#if defined(CONFIG_SMP) || defined(CONFIG_HOTPLUG_CPU)

/**
 * stop_machine: freeze the machine on all CPUs and run this function
 * @fn: the function to run
 * @data: the data ptr to pass to @fn()
 * @cpus: the cpus to run @fn() on (NULL = run on each online CPU)
 *
 * Description: This causes a thread to be scheduled on every CPU, which
 * will run with interrupts disabled.  Each CPU specified by @cpus will
 * run @fn.  While @fn is executing, there will no other CPUs holding
 * a raw spinlock or running within any other type of preempt-disabled
 * region of code.
 *
 * When @cpus specifies only a single CPU, this can be thought of as
 * a reader-writer lock where readers disable preemption (for example,
 * by holding a raw spinlock) and where the insanely heavy writers run
 * @fn while also preventing any other CPU from doing any useful work.
 * These writers can also be thought of as having implicitly grabbed every
 * raw spinlock in the kernel.
 *
 * When @fn is a no-op, this can be thought of as an RCU implementation
 * where readers again disable preemption and writers use stop_machine()
 * in place of synchronize_rcu(), albeit with orders of magnitude more
 * disruption than even that of synchronize_rcu_expedited().
 *
 * Although only one stop_machine() operation can proceed at a time,
 * the possibility of blocking in cpus_read_lock() means that the caller
 * cannot usefully rely on this serialization.
 *
 * Return: 0 if all invocations of @fn return zero.  Otherwise, the
 * value returned by an arbitrarily chosen member of the set of calls to
 * @fn that returned non-zero.
 */
int stop_machine(cpu_stop_fn_t fn, void *data, const struct cpumask *cpus);

/**
 * stop_machine_cpuslocked: freeze the machine on all CPUs and run this function
 * @fn: the function to run
 * @data: the data ptr to pass to @fn()
 * @cpus: the cpus to run @fn() on (NULL = run on each online CPU)
 *
 * Same as above.  Avoids nested calls to cpus_read_lock().
 *
 * Context: Must be called from within a cpus_read_lock() protected region.
 */
int stop_machine_cpuslocked(cpu_stop_fn_t fn, void *data, const struct cpumask *cpus);

/**
 * stop_core_cpuslocked: - stop all threads on just one core
 * @cpu: any cpu in the targeted core
 * @fn: the function to run on each CPU in the core containing @cpu
 * @data: the data ptr to pass to @fn()
 *
 * Same as above, but instead of every CPU, only the logical CPUs of the
 * single core containing @cpu are affected.
 *
 * Context: Must be called from within a cpus_read_lock() protected region.
 *
 * Return: 0 if all invocations of @fn return zero.  Otherwise, the
 * value returned by an arbitrarily chosen member of the set of calls to
 * @fn that returned non-zero.
 */
int stop_core_cpuslocked(unsigned int cpu, cpu_stop_fn_t fn, void *data);

int stop_machine_from_inactive_cpu(cpu_stop_fn_t fn, void *data,
				   const struct cpumask *cpus);
#else	/* CONFIG_SMP || CONFIG_HOTPLUG_CPU */

static __always_inline int stop_machine_cpuslocked(cpu_stop_fn_t fn, void *data,
					  const struct cpumask *cpus)
{
	unsigned long flags;
	int ret;
	local_irq_save(flags);
	ret = fn(data);
	local_irq_restore(flags);
	return ret;
}

static __always_inline int
stop_machine(cpu_stop_fn_t fn, void *data, const struct cpumask *cpus)
{
	return stop_machine_cpuslocked(fn, data, cpus);
}

static __always_inline int
stop_machine_from_inactive_cpu(cpu_stop_fn_t fn, void *data,
			       const struct cpumask *cpus)
{
	return stop_machine(fn, data, cpus);
}

#endif	/* CONFIG_SMP || CONFIG_HOTPLUG_CPU */
#endif	/* _LINUX_STOP_MACHINE */
