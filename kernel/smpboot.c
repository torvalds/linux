/*
 * Common SMP CPU bringup/teardown functions
 */
#include <linux/err.h>
#include <linux/smp.h>
#include <linux/init.h>
#include <linux/sched.h>
#include <linux/percpu.h>
#include <linux/workqueue.h>

#include "smpboot.h"

#ifdef CONFIG_GENERIC_SMP_IDLE_THREAD
struct create_idle {
	struct work_struct	work;
	struct task_struct	*idle;
	struct completion	done;
	unsigned int cpu;
};

static void __cpuinit do_fork_idle(struct work_struct *work)
{
	struct create_idle *c = container_of(work, struct create_idle, work);

	c->idle = fork_idle(c->cpu);
	complete(&c->done);
}

static struct task_struct * __cpuinit idle_thread_create(unsigned int cpu)
{
	struct create_idle c_idle = {
		.cpu	= cpu,
		.done	= COMPLETION_INITIALIZER_ONSTACK(c_idle.done),
	};

	INIT_WORK_ONSTACK(&c_idle.work, do_fork_idle);
	schedule_work(&c_idle.work);
	wait_for_completion(&c_idle.done);
	destroy_work_on_stack(&c_idle.work);
	return c_idle.idle;
}

/*
 * For the hotplug case we keep the task structs around and reuse
 * them.
 */
static DEFINE_PER_CPU(struct task_struct *, idle_threads);

static inline struct task_struct *get_idle_for_cpu(unsigned int cpu)
{
	struct task_struct *tsk = per_cpu(idle_threads, cpu);

	if (!tsk)
		return idle_thread_create(cpu);
	init_idle(tsk, cpu);
	return tsk;
}

struct task_struct * __cpuinit idle_thread_get(unsigned int cpu)
{
	return per_cpu(idle_threads, cpu);
}

void __init idle_thread_set_boot_cpu(void)
{
	per_cpu(idle_threads, smp_processor_id()) = current;
}

/**
 * idle_thread_init - Initialize the idle thread for a cpu
 * @cpu:	The cpu for which the idle thread should be initialized
 *
 * Creates the thread if it does not exist.
 */
static int __cpuinit idle_thread_init(unsigned int cpu)
{
	struct task_struct *idle = get_idle_for_cpu(cpu);

	if (IS_ERR(idle)) {
		printk(KERN_ERR "failed fork for CPU %u\n", cpu);
		return PTR_ERR(idle);
	}
	per_cpu(idle_threads, cpu) = idle;
	return 0;
}
#else
static inline int idle_thread_init(unsigned int cpu) { return 0; }
#endif

/**
 * smpboot_prepare - generic smpboot preparation
 */
int __cpuinit smpboot_prepare(unsigned int cpu)
{
	return idle_thread_init(cpu);
}
