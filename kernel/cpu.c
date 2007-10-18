/* CPU control.
 * (C) 2001, 2002, 2003, 2004 Rusty Russell
 *
 * This code is licenced under the GPL.
 */
#include <linux/proc_fs.h>
#include <linux/smp.h>
#include <linux/init.h>
#include <linux/notifier.h>
#include <linux/sched.h>
#include <linux/unistd.h>
#include <linux/cpu.h>
#include <linux/module.h>
#include <linux/kthread.h>
#include <linux/stop_machine.h>
#include <linux/mutex.h>

/* This protects CPUs going up and down... */
static DEFINE_MUTEX(cpu_add_remove_lock);
static DEFINE_MUTEX(cpu_bitmask_lock);

static __cpuinitdata RAW_NOTIFIER_HEAD(cpu_chain);

/* If set, cpu_up and cpu_down will return -EBUSY and do nothing.
 * Should always be manipulated under cpu_add_remove_lock
 */
static int cpu_hotplug_disabled;

#ifdef CONFIG_HOTPLUG_CPU

/* Crappy recursive lock-takers in cpufreq! Complain loudly about idiots */
static struct task_struct *recursive;
static int recursive_depth;

void lock_cpu_hotplug(void)
{
	struct task_struct *tsk = current;

	if (tsk == recursive) {
		static int warnings = 10;
		if (warnings) {
			printk(KERN_ERR "Lukewarm IQ detected in hotplug locking\n");
			WARN_ON(1);
			warnings--;
		}
		recursive_depth++;
		return;
	}
	mutex_lock(&cpu_bitmask_lock);
	recursive = tsk;
}
EXPORT_SYMBOL_GPL(lock_cpu_hotplug);

void unlock_cpu_hotplug(void)
{
	WARN_ON(recursive != current);
	if (recursive_depth) {
		recursive_depth--;
		return;
	}
	recursive = NULL;
	mutex_unlock(&cpu_bitmask_lock);
}
EXPORT_SYMBOL_GPL(unlock_cpu_hotplug);

#endif	/* CONFIG_HOTPLUG_CPU */

/* Need to know about CPUs going up/down? */
int __cpuinit register_cpu_notifier(struct notifier_block *nb)
{
	int ret;
	mutex_lock(&cpu_add_remove_lock);
	ret = raw_notifier_chain_register(&cpu_chain, nb);
	mutex_unlock(&cpu_add_remove_lock);
	return ret;
}

#ifdef CONFIG_HOTPLUG_CPU

EXPORT_SYMBOL(register_cpu_notifier);

void unregister_cpu_notifier(struct notifier_block *nb)
{
	mutex_lock(&cpu_add_remove_lock);
	raw_notifier_chain_unregister(&cpu_chain, nb);
	mutex_unlock(&cpu_add_remove_lock);
}
EXPORT_SYMBOL(unregister_cpu_notifier);

static inline void check_for_tasks(int cpu)
{
	struct task_struct *p;

	write_lock_irq(&tasklist_lock);
	for_each_process(p) {
		if (task_cpu(p) == cpu &&
		    (!cputime_eq(p->utime, cputime_zero) ||
		     !cputime_eq(p->stime, cputime_zero)))
			printk(KERN_WARNING "Task %s (pid = %d) is on cpu %d\
				(state = %ld, flags = %x) \n",
				 p->comm, p->pid, cpu, p->state, p->flags);
	}
	write_unlock_irq(&tasklist_lock);
}

struct take_cpu_down_param {
	unsigned long mod;
	void *hcpu;
};

/* Take this CPU down. */
static int take_cpu_down(void *_param)
{
	struct take_cpu_down_param *param = _param;
	int err;

	raw_notifier_call_chain(&cpu_chain, CPU_DYING | param->mod,
				param->hcpu);
	/* Ensure this CPU doesn't handle any more interrupts. */
	err = __cpu_disable();
	if (err < 0)
		return err;

	/* Force idle task to run as soon as we yield: it should
	   immediately notice cpu is offline and die quickly. */
	sched_idle_next();
	return 0;
}

/* Requires cpu_add_remove_lock to be held */
static int _cpu_down(unsigned int cpu, int tasks_frozen)
{
	int err, nr_calls = 0;
	struct task_struct *p;
	cpumask_t old_allowed, tmp;
	void *hcpu = (void *)(long)cpu;
	unsigned long mod = tasks_frozen ? CPU_TASKS_FROZEN : 0;
	struct take_cpu_down_param tcd_param = {
		.mod = mod,
		.hcpu = hcpu,
	};

	if (num_online_cpus() == 1)
		return -EBUSY;

	if (!cpu_online(cpu))
		return -EINVAL;

	raw_notifier_call_chain(&cpu_chain, CPU_LOCK_ACQUIRE, hcpu);
	err = __raw_notifier_call_chain(&cpu_chain, CPU_DOWN_PREPARE | mod,
					hcpu, -1, &nr_calls);
	if (err == NOTIFY_BAD) {
		nr_calls--;
		__raw_notifier_call_chain(&cpu_chain, CPU_DOWN_FAILED | mod,
					  hcpu, nr_calls, NULL);
		printk("%s: attempt to take down CPU %u failed\n",
				__FUNCTION__, cpu);
		err = -EINVAL;
		goto out_release;
	}

	/* Ensure that we are not runnable on dying cpu */
	old_allowed = current->cpus_allowed;
	tmp = CPU_MASK_ALL;
	cpu_clear(cpu, tmp);
	set_cpus_allowed(current, tmp);

	mutex_lock(&cpu_bitmask_lock);
	p = __stop_machine_run(take_cpu_down, &tcd_param, cpu);
	mutex_unlock(&cpu_bitmask_lock);

	if (IS_ERR(p) || cpu_online(cpu)) {
		/* CPU didn't die: tell everyone.  Can't complain. */
		if (raw_notifier_call_chain(&cpu_chain, CPU_DOWN_FAILED | mod,
					    hcpu) == NOTIFY_BAD)
			BUG();

		if (IS_ERR(p)) {
			err = PTR_ERR(p);
			goto out_allowed;
		}
		goto out_thread;
	}

	/* Wait for it to sleep (leaving idle task). */
	while (!idle_cpu(cpu))
		yield();

	/* This actually kills the CPU. */
	__cpu_die(cpu);

	/* CPU is completely dead: tell everyone.  Too late to complain. */
	if (raw_notifier_call_chain(&cpu_chain, CPU_DEAD | mod,
				    hcpu) == NOTIFY_BAD)
		BUG();

	check_for_tasks(cpu);

out_thread:
	err = kthread_stop(p);
out_allowed:
	set_cpus_allowed(current, old_allowed);
out_release:
	raw_notifier_call_chain(&cpu_chain, CPU_LOCK_RELEASE, hcpu);
	return err;
}

int cpu_down(unsigned int cpu)
{
	int err = 0;

	mutex_lock(&cpu_add_remove_lock);
	if (cpu_hotplug_disabled)
		err = -EBUSY;
	else
		err = _cpu_down(cpu, 0);

	mutex_unlock(&cpu_add_remove_lock);
	return err;
}
#endif /*CONFIG_HOTPLUG_CPU*/

/* Requires cpu_add_remove_lock to be held */
static int __cpuinit _cpu_up(unsigned int cpu, int tasks_frozen)
{
	int ret, nr_calls = 0;
	void *hcpu = (void *)(long)cpu;
	unsigned long mod = tasks_frozen ? CPU_TASKS_FROZEN : 0;

	if (cpu_online(cpu) || !cpu_present(cpu))
		return -EINVAL;

	raw_notifier_call_chain(&cpu_chain, CPU_LOCK_ACQUIRE, hcpu);
	ret = __raw_notifier_call_chain(&cpu_chain, CPU_UP_PREPARE | mod, hcpu,
							-1, &nr_calls);
	if (ret == NOTIFY_BAD) {
		nr_calls--;
		printk("%s: attempt to bring up CPU %u failed\n",
				__FUNCTION__, cpu);
		ret = -EINVAL;
		goto out_notify;
	}

	/* Arch-specific enabling code. */
	mutex_lock(&cpu_bitmask_lock);
	ret = __cpu_up(cpu);
	mutex_unlock(&cpu_bitmask_lock);
	if (ret != 0)
		goto out_notify;
	BUG_ON(!cpu_online(cpu));

	/* Now call notifier in preparation. */
	raw_notifier_call_chain(&cpu_chain, CPU_ONLINE | mod, hcpu);

out_notify:
	if (ret != 0)
		__raw_notifier_call_chain(&cpu_chain,
				CPU_UP_CANCELED | mod, hcpu, nr_calls, NULL);
	raw_notifier_call_chain(&cpu_chain, CPU_LOCK_RELEASE, hcpu);

	return ret;
}

int __cpuinit cpu_up(unsigned int cpu)
{
	int err = 0;

	mutex_lock(&cpu_add_remove_lock);
	if (cpu_hotplug_disabled)
		err = -EBUSY;
	else
		err = _cpu_up(cpu, 0);

	mutex_unlock(&cpu_add_remove_lock);
	return err;
}

#ifdef CONFIG_PM_SLEEP_SMP
static cpumask_t frozen_cpus;

int disable_nonboot_cpus(void)
{
	int cpu, first_cpu, error = 0;

	mutex_lock(&cpu_add_remove_lock);
	first_cpu = first_cpu(cpu_online_map);
	/* We take down all of the non-boot CPUs in one shot to avoid races
	 * with the userspace trying to use the CPU hotplug at the same time
	 */
	cpus_clear(frozen_cpus);
	printk("Disabling non-boot CPUs ...\n");
	for_each_online_cpu(cpu) {
		if (cpu == first_cpu)
			continue;
		error = _cpu_down(cpu, 1);
		if (!error) {
			cpu_set(cpu, frozen_cpus);
			printk("CPU%d is down\n", cpu);
		} else {
			printk(KERN_ERR "Error taking CPU%d down: %d\n",
				cpu, error);
			break;
		}
	}
	if (!error) {
		BUG_ON(num_online_cpus() > 1);
		/* Make sure the CPUs won't be enabled by someone else */
		cpu_hotplug_disabled = 1;
	} else {
		printk(KERN_ERR "Non-boot CPUs are not disabled\n");
	}
	mutex_unlock(&cpu_add_remove_lock);
	return error;
}

void enable_nonboot_cpus(void)
{
	int cpu, error;

	/* Allow everyone to use the CPU hotplug again */
	mutex_lock(&cpu_add_remove_lock);
	cpu_hotplug_disabled = 0;
	if (cpus_empty(frozen_cpus))
		goto out;

	printk("Enabling non-boot CPUs ...\n");
	for_each_cpu_mask(cpu, frozen_cpus) {
		error = _cpu_up(cpu, 1);
		if (!error) {
			printk("CPU%d is up\n", cpu);
			continue;
		}
		printk(KERN_WARNING "Error taking CPU%d up: %d\n", cpu, error);
	}
	cpus_clear(frozen_cpus);
out:
	mutex_unlock(&cpu_add_remove_lock);
}
#endif /* CONFIG_PM_SLEEP_SMP */
