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
#include <linux/oom.h>
#include <linux/rcupdate.h>
#include <linux/export.h>
#include <linux/bug.h>
#include <linux/kthread.h>
#include <linux/stop_machine.h>
#include <linux/mutex.h>
#include <linux/gfp.h>
#include <linux/suspend.h>
#include <linux/lockdep.h>
#include <trace/events/power.h>

#include "smpboot.h"

#ifdef CONFIG_SMP
/* Serializes the updates to cpu_online_mask, cpu_present_mask */
static DEFINE_MUTEX(cpu_add_remove_lock);

/*
 * The following two APIs (cpu_maps_update_begin/done) must be used when
 * attempting to serialize the updates to cpu_online_mask & cpu_present_mask.
 * The APIs cpu_notifier_register_begin/done() must be used to protect CPU
 * hotplug callback (un)registration performed using __register_cpu_notifier()
 * or __unregister_cpu_notifier().
 */
void cpu_maps_update_begin(void)
{
	mutex_lock(&cpu_add_remove_lock);
}
EXPORT_SYMBOL(cpu_notifier_register_begin);

void cpu_maps_update_done(void)
{
	mutex_unlock(&cpu_add_remove_lock);
}
EXPORT_SYMBOL(cpu_notifier_register_done);

static RAW_NOTIFIER_HEAD(cpu_chain);

/* If set, cpu_up and cpu_down will return -EBUSY and do nothing.
 * Should always be manipulated under cpu_add_remove_lock
 */
static int cpu_hotplug_disabled;

#ifdef CONFIG_HOTPLUG_CPU

static struct {
	struct task_struct *active_writer;
	struct mutex lock; /* Synchronizes accesses to refcount, */
	/*
	 * Also blocks the new readers during
	 * an ongoing cpu hotplug operation.
	 */
	int refcount;

#ifdef CONFIG_DEBUG_LOCK_ALLOC
	struct lockdep_map dep_map;
#endif
} cpu_hotplug = {
	.active_writer = NULL,
	.lock = __MUTEX_INITIALIZER(cpu_hotplug.lock),
	.refcount = 0,
#ifdef CONFIG_DEBUG_LOCK_ALLOC
	.dep_map = {.name = "cpu_hotplug.lock" },
#endif
};

/* Lockdep annotations for get/put_online_cpus() and cpu_hotplug_begin/end() */
#define cpuhp_lock_acquire_read() lock_map_acquire_read(&cpu_hotplug.dep_map)
#define cpuhp_lock_acquire_tryread() \
				  lock_map_acquire_tryread(&cpu_hotplug.dep_map)
#define cpuhp_lock_acquire()      lock_map_acquire(&cpu_hotplug.dep_map)
#define cpuhp_lock_release()      lock_map_release(&cpu_hotplug.dep_map)

void get_online_cpus(void)
{
	might_sleep();
	if (cpu_hotplug.active_writer == current)
		return;
	cpuhp_lock_acquire_read();
	mutex_lock(&cpu_hotplug.lock);
	cpu_hotplug.refcount++;
	mutex_unlock(&cpu_hotplug.lock);
}
EXPORT_SYMBOL_GPL(get_online_cpus);

bool try_get_online_cpus(void)
{
	if (cpu_hotplug.active_writer == current)
		return true;
	if (!mutex_trylock(&cpu_hotplug.lock))
		return false;
	cpuhp_lock_acquire_tryread();
	cpu_hotplug.refcount++;
	mutex_unlock(&cpu_hotplug.lock);
	return true;
}
EXPORT_SYMBOL_GPL(try_get_online_cpus);

void put_online_cpus(void)
{
	if (cpu_hotplug.active_writer == current)
		return;
	mutex_lock(&cpu_hotplug.lock);

	if (WARN_ON(!cpu_hotplug.refcount))
		cpu_hotplug.refcount++; /* try to fix things up */

	if (!--cpu_hotplug.refcount && unlikely(cpu_hotplug.active_writer))
		wake_up_process(cpu_hotplug.active_writer);
	mutex_unlock(&cpu_hotplug.lock);
	cpuhp_lock_release();

}
EXPORT_SYMBOL_GPL(put_online_cpus);

/*
 * This ensures that the hotplug operation can begin only when the
 * refcount goes to zero.
 *
 * Note that during a cpu-hotplug operation, the new readers, if any,
 * will be blocked by the cpu_hotplug.lock
 *
 * Since cpu_hotplug_begin() is always called after invoking
 * cpu_maps_update_begin(), we can be sure that only one writer is active.
 *
 * Note that theoretically, there is a possibility of a livelock:
 * - Refcount goes to zero, last reader wakes up the sleeping
 *   writer.
 * - Last reader unlocks the cpu_hotplug.lock.
 * - A new reader arrives at this moment, bumps up the refcount.
 * - The writer acquires the cpu_hotplug.lock finds the refcount
 *   non zero and goes to sleep again.
 *
 * However, this is very difficult to achieve in practice since
 * get_online_cpus() not an api which is called all that often.
 *
 */
void cpu_hotplug_begin(void)
{
	cpu_hotplug.active_writer = current;

	cpuhp_lock_acquire();
	for (;;) {
		mutex_lock(&cpu_hotplug.lock);
		if (likely(!cpu_hotplug.refcount))
			break;
		__set_current_state(TASK_UNINTERRUPTIBLE);
		mutex_unlock(&cpu_hotplug.lock);
		schedule();
	}
}

void cpu_hotplug_done(void)
{
	cpu_hotplug.active_writer = NULL;
	mutex_unlock(&cpu_hotplug.lock);
	cpuhp_lock_release();
}

/*
 * Wait for currently running CPU hotplug operations to complete (if any) and
 * disable future CPU hotplug (from sysfs). The 'cpu_add_remove_lock' protects
 * the 'cpu_hotplug_disabled' flag. The same lock is also acquired by the
 * hotplug path before performing hotplug operations. So acquiring that lock
 * guarantees mutual exclusion from any currently running hotplug operations.
 */
void cpu_hotplug_disable(void)
{
	cpu_maps_update_begin();
	cpu_hotplug_disabled = 1;
	cpu_maps_update_done();
}

void cpu_hotplug_enable(void)
{
	cpu_maps_update_begin();
	cpu_hotplug_disabled = 0;
	cpu_maps_update_done();
}

#endif	/* CONFIG_HOTPLUG_CPU */

/* Need to know about CPUs going up/down? */
int __ref register_cpu_notifier(struct notifier_block *nb)
{
	int ret;
	cpu_maps_update_begin();
	ret = raw_notifier_chain_register(&cpu_chain, nb);
	cpu_maps_update_done();
	return ret;
}

int __ref __register_cpu_notifier(struct notifier_block *nb)
{
	return raw_notifier_chain_register(&cpu_chain, nb);
}

static int __cpu_notify(unsigned long val, void *v, int nr_to_call,
			int *nr_calls)
{
	int ret;

	ret = __raw_notifier_call_chain(&cpu_chain, val, v, nr_to_call,
					nr_calls);

	return notifier_to_errno(ret);
}

static int cpu_notify(unsigned long val, void *v)
{
	return __cpu_notify(val, v, -1, NULL);
}

#ifdef CONFIG_HOTPLUG_CPU

static void cpu_notify_nofail(unsigned long val, void *v)
{
	BUG_ON(cpu_notify(val, v));
}
EXPORT_SYMBOL(register_cpu_notifier);
EXPORT_SYMBOL(__register_cpu_notifier);

void __ref unregister_cpu_notifier(struct notifier_block *nb)
{
	cpu_maps_update_begin();
	raw_notifier_chain_unregister(&cpu_chain, nb);
	cpu_maps_update_done();
}
EXPORT_SYMBOL(unregister_cpu_notifier);

void __ref __unregister_cpu_notifier(struct notifier_block *nb)
{
	raw_notifier_chain_unregister(&cpu_chain, nb);
}
EXPORT_SYMBOL(__unregister_cpu_notifier);

/**
 * clear_tasks_mm_cpumask - Safely clear tasks' mm_cpumask for a CPU
 * @cpu: a CPU id
 *
 * This function walks all processes, finds a valid mm struct for each one and
 * then clears a corresponding bit in mm's cpumask.  While this all sounds
 * trivial, there are various non-obvious corner cases, which this function
 * tries to solve in a safe manner.
 *
 * Also note that the function uses a somewhat relaxed locking scheme, so it may
 * be called only for an already offlined CPU.
 */
void clear_tasks_mm_cpumask(int cpu)
{
	struct task_struct *p;

	/*
	 * This function is called after the cpu is taken down and marked
	 * offline, so its not like new tasks will ever get this cpu set in
	 * their mm mask. -- Peter Zijlstra
	 * Thus, we may use rcu_read_lock() here, instead of grabbing
	 * full-fledged tasklist_lock.
	 */
	WARN_ON(cpu_online(cpu));
	rcu_read_lock();
	for_each_process(p) {
		struct task_struct *t;

		/*
		 * Main thread might exit, but other threads may still have
		 * a valid mm. Find one.
		 */
		t = find_lock_task_mm(p);
		if (!t)
			continue;
		cpumask_clear_cpu(cpu, mm_cpumask(t->mm));
		task_unlock(t);
	}
	rcu_read_unlock();
}

static inline void check_for_tasks(int dead_cpu)
{
	struct task_struct *g, *p;

	read_lock_irq(&tasklist_lock);
	do_each_thread(g, p) {
		if (!p->on_rq)
			continue;
		/*
		 * We do the check with unlocked task_rq(p)->lock.
		 * Order the reading to do not warn about a task,
		 * which was running on this cpu in the past, and
		 * it's just been woken on another cpu.
		 */
		rmb();
		if (task_cpu(p) != dead_cpu)
			continue;

		pr_warn("Task %s (pid=%d) is on cpu %d (state=%ld, flags=%x)\n",
			p->comm, task_pid_nr(p), dead_cpu, p->state, p->flags);
	} while_each_thread(g, p);
	read_unlock_irq(&tasklist_lock);
}

struct take_cpu_down_param {
	unsigned long mod;
	void *hcpu;
};

/* Take this CPU down. */
static int __ref take_cpu_down(void *_param)
{
	struct take_cpu_down_param *param = _param;
	int err;

	/* Ensure this CPU doesn't handle any more interrupts. */
	err = __cpu_disable();
	if (err < 0)
		return err;

	cpu_notify(CPU_DYING | param->mod, param->hcpu);
	/* Park the stopper thread */
	kthread_park(current);
	return 0;
}

/* Requires cpu_add_remove_lock to be held */
static int __ref _cpu_down(unsigned int cpu, int tasks_frozen)
{
	int err, nr_calls = 0;
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

	cpu_hotplug_begin();

	err = __cpu_notify(CPU_DOWN_PREPARE | mod, hcpu, -1, &nr_calls);
	if (err) {
		nr_calls--;
		__cpu_notify(CPU_DOWN_FAILED | mod, hcpu, nr_calls, NULL);
		pr_warn("%s: attempt to take down CPU %u failed\n",
			__func__, cpu);
		goto out_release;
	}

	/*
	 * By now we've cleared cpu_active_mask, wait for all preempt-disabled
	 * and RCU users of this state to go away such that all new such users
	 * will observe it.
	 *
	 * For CONFIG_PREEMPT we have preemptible RCU and its sync_rcu() might
	 * not imply sync_sched(), so explicitly call both.
	 *
	 * Do sync before park smpboot threads to take care the rcu boost case.
	 */
#ifdef CONFIG_PREEMPT
	synchronize_sched();
#endif
	synchronize_rcu();

	smpboot_park_threads(cpu);

	/*
	 * So now all preempt/rcu users must observe !cpu_active().
	 */

	err = __stop_machine(take_cpu_down, &tcd_param, cpumask_of(cpu));
	if (err) {
		/* CPU didn't die: tell everyone.  Can't complain. */
		smpboot_unpark_threads(cpu);
		cpu_notify_nofail(CPU_DOWN_FAILED | mod, hcpu);
		goto out_release;
	}
	BUG_ON(cpu_online(cpu));

	/*
	 * The migration_call() CPU_DYING callback will have removed all
	 * runnable tasks from the cpu, there's only the idle task left now
	 * that the migration thread is done doing the stop_machine thing.
	 *
	 * Wait for the stop thread to go away.
	 */
	while (!idle_cpu(cpu))
		cpu_relax();

	/* This actually kills the CPU. */
	__cpu_die(cpu);

	/* CPU is completely dead: tell everyone.  Too late to complain. */
	cpu_notify_nofail(CPU_DEAD | mod, hcpu);

	check_for_tasks(cpu);

out_release:
	cpu_hotplug_done();
	if (!err)
		cpu_notify_nofail(CPU_POST_DEAD | mod, hcpu);
	return err;
}

int __ref cpu_down(unsigned int cpu)
{
	int err;

	cpu_maps_update_begin();

	if (cpu_hotplug_disabled) {
		err = -EBUSY;
		goto out;
	}

	err = _cpu_down(cpu, 0);

out:
	cpu_maps_update_done();
	return err;
}
EXPORT_SYMBOL(cpu_down);
#endif /*CONFIG_HOTPLUG_CPU*/

/* Requires cpu_add_remove_lock to be held */
static int _cpu_up(unsigned int cpu, int tasks_frozen)
{
	int ret, nr_calls = 0;
	void *hcpu = (void *)(long)cpu;
	unsigned long mod = tasks_frozen ? CPU_TASKS_FROZEN : 0;
	struct task_struct *idle;

	cpu_hotplug_begin();

	if (cpu_online(cpu) || !cpu_present(cpu)) {
		ret = -EINVAL;
		goto out;
	}

	idle = idle_thread_get(cpu);
	if (IS_ERR(idle)) {
		ret = PTR_ERR(idle);
		goto out;
	}

	ret = smpboot_create_threads(cpu);
	if (ret)
		goto out;

	ret = __cpu_notify(CPU_UP_PREPARE | mod, hcpu, -1, &nr_calls);
	if (ret) {
		nr_calls--;
		pr_warn("%s: attempt to bring up CPU %u failed\n",
			__func__, cpu);
		goto out_notify;
	}

	/* Arch-specific enabling code. */
	ret = __cpu_up(cpu, idle);
	if (ret != 0)
		goto out_notify;
	BUG_ON(!cpu_online(cpu));

	/* Wake the per cpu threads */
	smpboot_unpark_threads(cpu);

	/* Now call notifier in preparation. */
	cpu_notify(CPU_ONLINE | mod, hcpu);

out_notify:
	if (ret != 0)
		__cpu_notify(CPU_UP_CANCELED | mod, hcpu, nr_calls, NULL);
out:
	cpu_hotplug_done();

	return ret;
}

int cpu_up(unsigned int cpu)
{
	int err = 0;

	if (!cpu_possible(cpu)) {
		pr_err("can't online cpu %d because it is not configured as may-hotadd at boot time\n",
		       cpu);
#if defined(CONFIG_IA64)
		pr_err("please check additional_cpus= boot parameter\n");
#endif
		return -EINVAL;
	}

	err = try_online_node(cpu_to_node(cpu));
	if (err)
		return err;

	cpu_maps_update_begin();

	if (cpu_hotplug_disabled) {
		err = -EBUSY;
		goto out;
	}

	err = _cpu_up(cpu, 0);

out:
	cpu_maps_update_done();
	return err;
}
EXPORT_SYMBOL_GPL(cpu_up);

#ifdef CONFIG_PM_SLEEP_SMP
static cpumask_var_t frozen_cpus;

int disable_nonboot_cpus(void)
{
	int cpu, first_cpu, error = 0;

	cpu_maps_update_begin();
	first_cpu = cpumask_first(cpu_online_mask);
	/*
	 * We take down all of the non-boot CPUs in one shot to avoid races
	 * with the userspace trying to use the CPU hotplug at the same time
	 */
	cpumask_clear(frozen_cpus);

	pr_info("Disabling non-boot CPUs ...\n");
	for_each_online_cpu(cpu) {
		if (cpu == first_cpu)
			continue;
		trace_suspend_resume(TPS("CPU_OFF"), cpu, true);
		error = _cpu_down(cpu, 1);
		trace_suspend_resume(TPS("CPU_OFF"), cpu, false);
		if (!error)
			cpumask_set_cpu(cpu, frozen_cpus);
		else {
			pr_err("Error taking CPU%d down: %d\n", cpu, error);
			break;
		}
	}

	if (!error) {
		BUG_ON(num_online_cpus() > 1);
		/* Make sure the CPUs won't be enabled by someone else */
		cpu_hotplug_disabled = 1;
	} else {
		pr_err("Non-boot CPUs are not disabled\n");
	}
	cpu_maps_update_done();
	return error;
}

void __weak arch_enable_nonboot_cpus_begin(void)
{
}

void __weak arch_enable_nonboot_cpus_end(void)
{
}

void __ref enable_nonboot_cpus(void)
{
	int cpu, error;

	/* Allow everyone to use the CPU hotplug again */
	cpu_maps_update_begin();
	cpu_hotplug_disabled = 0;
	if (cpumask_empty(frozen_cpus))
		goto out;

	pr_info("Enabling non-boot CPUs ...\n");

	arch_enable_nonboot_cpus_begin();

	for_each_cpu(cpu, frozen_cpus) {
		trace_suspend_resume(TPS("CPU_ON"), cpu, true);
		error = _cpu_up(cpu, 1);
		trace_suspend_resume(TPS("CPU_ON"), cpu, false);
		if (!error) {
			pr_info("CPU%d is up\n", cpu);
			continue;
		}
		pr_warn("Error taking CPU%d up: %d\n", cpu, error);
	}

	arch_enable_nonboot_cpus_end();

	cpumask_clear(frozen_cpus);
out:
	cpu_maps_update_done();
}

static int __init alloc_frozen_cpus(void)
{
	if (!alloc_cpumask_var(&frozen_cpus, GFP_KERNEL|__GFP_ZERO))
		return -ENOMEM;
	return 0;
}
core_initcall(alloc_frozen_cpus);

/*
 * When callbacks for CPU hotplug notifications are being executed, we must
 * ensure that the state of the system with respect to the tasks being frozen
 * or not, as reported by the notification, remains unchanged *throughout the
 * duration* of the execution of the callbacks.
 * Hence we need to prevent the freezer from racing with regular CPU hotplug.
 *
 * This synchronization is implemented by mutually excluding regular CPU
 * hotplug and Suspend/Hibernate call paths by hooking onto the Suspend/
 * Hibernate notifications.
 */
static int
cpu_hotplug_pm_callback(struct notifier_block *nb,
			unsigned long action, void *ptr)
{
	switch (action) {

	case PM_SUSPEND_PREPARE:
	case PM_HIBERNATION_PREPARE:
		cpu_hotplug_disable();
		break;

	case PM_POST_SUSPEND:
	case PM_POST_HIBERNATION:
		cpu_hotplug_enable();
		break;

	default:
		return NOTIFY_DONE;
	}

	return NOTIFY_OK;
}


static int __init cpu_hotplug_pm_sync_init(void)
{
	/*
	 * cpu_hotplug_pm_callback has higher priority than x86
	 * bsp_pm_callback which depends on cpu_hotplug_pm_callback
	 * to disable cpu hotplug to avoid cpu hotplug race.
	 */
	pm_notifier(cpu_hotplug_pm_callback, 0);
	return 0;
}
core_initcall(cpu_hotplug_pm_sync_init);

#endif /* CONFIG_PM_SLEEP_SMP */

/**
 * notify_cpu_starting(cpu) - call the CPU_STARTING notifiers
 * @cpu: cpu that just started
 *
 * This function calls the cpu_chain notifiers with CPU_STARTING.
 * It must be called by the arch code on the new cpu, before the new cpu
 * enables interrupts and before the "boot" cpu returns from __cpu_up().
 */
void notify_cpu_starting(unsigned int cpu)
{
	unsigned long val = CPU_STARTING;

#ifdef CONFIG_PM_SLEEP_SMP
	if (frozen_cpus != NULL && cpumask_test_cpu(cpu, frozen_cpus))
		val = CPU_STARTING_FROZEN;
#endif /* CONFIG_PM_SLEEP_SMP */
	cpu_notify(val, (void *)(long)cpu);
}

#endif /* CONFIG_SMP */

/*
 * cpu_bit_bitmap[] is a special, "compressed" data structure that
 * represents all NR_CPUS bits binary values of 1<<nr.
 *
 * It is used by cpumask_of() to get a constant address to a CPU
 * mask value that has a single bit set only.
 */

/* cpu_bit_bitmap[0] is empty - so we can back into it */
#define MASK_DECLARE_1(x)	[x+1][0] = (1UL << (x))
#define MASK_DECLARE_2(x)	MASK_DECLARE_1(x), MASK_DECLARE_1(x+1)
#define MASK_DECLARE_4(x)	MASK_DECLARE_2(x), MASK_DECLARE_2(x+2)
#define MASK_DECLARE_8(x)	MASK_DECLARE_4(x), MASK_DECLARE_4(x+4)

const unsigned long cpu_bit_bitmap[BITS_PER_LONG+1][BITS_TO_LONGS(NR_CPUS)] = {

	MASK_DECLARE_8(0),	MASK_DECLARE_8(8),
	MASK_DECLARE_8(16),	MASK_DECLARE_8(24),
#if BITS_PER_LONG > 32
	MASK_DECLARE_8(32),	MASK_DECLARE_8(40),
	MASK_DECLARE_8(48),	MASK_DECLARE_8(56),
#endif
};
EXPORT_SYMBOL_GPL(cpu_bit_bitmap);

const DECLARE_BITMAP(cpu_all_bits, NR_CPUS) = CPU_BITS_ALL;
EXPORT_SYMBOL(cpu_all_bits);

#ifdef CONFIG_INIT_ALL_POSSIBLE
static DECLARE_BITMAP(cpu_possible_bits, CONFIG_NR_CPUS) __read_mostly
	= CPU_BITS_ALL;
#else
static DECLARE_BITMAP(cpu_possible_bits, CONFIG_NR_CPUS) __read_mostly;
#endif
const struct cpumask *const cpu_possible_mask = to_cpumask(cpu_possible_bits);
EXPORT_SYMBOL(cpu_possible_mask);

static DECLARE_BITMAP(cpu_online_bits, CONFIG_NR_CPUS) __read_mostly;
const struct cpumask *const cpu_online_mask = to_cpumask(cpu_online_bits);
EXPORT_SYMBOL(cpu_online_mask);

static DECLARE_BITMAP(cpu_present_bits, CONFIG_NR_CPUS) __read_mostly;
const struct cpumask *const cpu_present_mask = to_cpumask(cpu_present_bits);
EXPORT_SYMBOL(cpu_present_mask);

static DECLARE_BITMAP(cpu_active_bits, CONFIG_NR_CPUS) __read_mostly;
const struct cpumask *const cpu_active_mask = to_cpumask(cpu_active_bits);
EXPORT_SYMBOL(cpu_active_mask);

void set_cpu_possible(unsigned int cpu, bool possible)
{
	if (possible)
		cpumask_set_cpu(cpu, to_cpumask(cpu_possible_bits));
	else
		cpumask_clear_cpu(cpu, to_cpumask(cpu_possible_bits));
}

void set_cpu_present(unsigned int cpu, bool present)
{
	if (present)
		cpumask_set_cpu(cpu, to_cpumask(cpu_present_bits));
	else
		cpumask_clear_cpu(cpu, to_cpumask(cpu_present_bits));
}

void set_cpu_online(unsigned int cpu, bool online)
{
	if (online) {
		cpumask_set_cpu(cpu, to_cpumask(cpu_online_bits));
		cpumask_set_cpu(cpu, to_cpumask(cpu_active_bits));
	} else {
		cpumask_clear_cpu(cpu, to_cpumask(cpu_online_bits));
	}
}

void set_cpu_active(unsigned int cpu, bool active)
{
	if (active)
		cpumask_set_cpu(cpu, to_cpumask(cpu_active_bits));
	else
		cpumask_clear_cpu(cpu, to_cpumask(cpu_active_bits));
}

void init_cpu_present(const struct cpumask *src)
{
	cpumask_copy(to_cpumask(cpu_present_bits), src);
}

void init_cpu_possible(const struct cpumask *src)
{
	cpumask_copy(to_cpumask(cpu_possible_bits), src);
}

void init_cpu_online(const struct cpumask *src)
{
	cpumask_copy(to_cpumask(cpu_online_bits), src);
}
