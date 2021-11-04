/* CPU control.
 * (C) 2001, 2002, 2003, 2004 Rusty Russell
 *
 * This code is licenced under the GPL.
 */
#include <linux/sched/mm.h>
#include <linux/proc_fs.h>
#include <linux/smp.h>
#include <linux/init.h>
#include <linux/notifier.h>
#include <linux/sched/signal.h>
#include <linux/sched/hotplug.h>
#include <linux/sched/isolation.h>
#include <linux/sched/task.h>
#include <linux/sched/smt.h>
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
#include <linux/tick.h>
#include <linux/irq.h>
#include <linux/nmi.h>
#include <linux/smpboot.h>
#include <linux/relay.h>
#include <linux/slab.h>
#include <linux/percpu-rwsem.h>
#include <linux/cpuset.h>
#include <uapi/linux/sched/types.h>

#include <trace/events/power.h>
#define CREATE_TRACE_POINTS
#include <trace/events/cpuhp.h>

#undef CREATE_TRACE_POINTS
#include <trace/hooks/sched.h>
#include <trace/hooks/cpu.h>

#include "smpboot.h"

/**
 * cpuhp_cpu_state - Per cpu hotplug state storage
 * @state:	The current cpu state
 * @target:	The target state
 * @thread:	Pointer to the hotplug thread
 * @should_run:	Thread should execute
 * @rollback:	Perform a rollback
 * @single:	Single callback invocation
 * @bringup:	Single callback bringup or teardown selector
 * @cb_state:	The state for a single callback (install/uninstall)
 * @result:	Result of the operation
 * @done_up:	Signal completion to the issuer of the task for cpu-up
 * @done_down:	Signal completion to the issuer of the task for cpu-down
 */
struct cpuhp_cpu_state {
	enum cpuhp_state	state;
	enum cpuhp_state	target;
	enum cpuhp_state	fail;
#ifdef CONFIG_SMP
	struct task_struct	*thread;
	bool			should_run;
	bool			rollback;
	bool			single;
	bool			bringup;
	struct hlist_node	*node;
	struct hlist_node	*last;
	enum cpuhp_state	cb_state;
	int			result;
	struct completion	done_up;
	struct completion	done_down;
#endif
};

static DEFINE_PER_CPU(struct cpuhp_cpu_state, cpuhp_state) = {
	.fail = CPUHP_INVALID,
};

#ifdef CONFIG_SMP
cpumask_t cpus_booted_once_mask;
#endif

#if defined(CONFIG_LOCKDEP) && defined(CONFIG_SMP)
static struct lockdep_map cpuhp_state_up_map =
	STATIC_LOCKDEP_MAP_INIT("cpuhp_state-up", &cpuhp_state_up_map);
static struct lockdep_map cpuhp_state_down_map =
	STATIC_LOCKDEP_MAP_INIT("cpuhp_state-down", &cpuhp_state_down_map);


static inline void cpuhp_lock_acquire(bool bringup)
{
	lock_map_acquire(bringup ? &cpuhp_state_up_map : &cpuhp_state_down_map);
}

static inline void cpuhp_lock_release(bool bringup)
{
	lock_map_release(bringup ? &cpuhp_state_up_map : &cpuhp_state_down_map);
}
#else

static inline void cpuhp_lock_acquire(bool bringup) { }
static inline void cpuhp_lock_release(bool bringup) { }

#endif

/**
 * cpuhp_step - Hotplug state machine step
 * @name:	Name of the step
 * @startup:	Startup function of the step
 * @teardown:	Teardown function of the step
 * @cant_stop:	Bringup/teardown can't be stopped at this step
 */
struct cpuhp_step {
	const char		*name;
	union {
		int		(*single)(unsigned int cpu);
		int		(*multi)(unsigned int cpu,
					 struct hlist_node *node);
	} startup;
	union {
		int		(*single)(unsigned int cpu);
		int		(*multi)(unsigned int cpu,
					 struct hlist_node *node);
	} teardown;
	struct hlist_head	list;
	bool			cant_stop;
	bool			multi_instance;
};

static DEFINE_MUTEX(cpuhp_state_mutex);
static struct cpuhp_step cpuhp_hp_states[];

static struct cpuhp_step *cpuhp_get_step(enum cpuhp_state state)
{
	return cpuhp_hp_states + state;
}

/**
 * cpuhp_invoke_callback _ Invoke the callbacks for a given state
 * @cpu:	The cpu for which the callback should be invoked
 * @state:	The state to do callbacks for
 * @bringup:	True if the bringup callback should be invoked
 * @node:	For multi-instance, do a single entry callback for install/remove
 * @lastp:	For multi-instance rollback, remember how far we got
 *
 * Called from cpu hotplug and from the state register machinery.
 */
static int cpuhp_invoke_callback(unsigned int cpu, enum cpuhp_state state,
				 bool bringup, struct hlist_node *node,
				 struct hlist_node **lastp)
{
	struct cpuhp_cpu_state *st = per_cpu_ptr(&cpuhp_state, cpu);
	struct cpuhp_step *step = cpuhp_get_step(state);
	int (*cbm)(unsigned int cpu, struct hlist_node *node);
	int (*cb)(unsigned int cpu);
	int ret, cnt;

	if (st->fail == state) {
		st->fail = CPUHP_INVALID;

		if (!(bringup ? step->startup.single : step->teardown.single))
			return 0;

		return -EAGAIN;
	}

	if (!step->multi_instance) {
		WARN_ON_ONCE(lastp && *lastp);
		cb = bringup ? step->startup.single : step->teardown.single;
		if (!cb)
			return 0;
		trace_cpuhp_enter(cpu, st->target, state, cb);
		ret = cb(cpu);
		trace_cpuhp_exit(cpu, st->state, state, ret);
		return ret;
	}
	cbm = bringup ? step->startup.multi : step->teardown.multi;
	if (!cbm)
		return 0;

	/* Single invocation for instance add/remove */
	if (node) {
		WARN_ON_ONCE(lastp && *lastp);
		trace_cpuhp_multi_enter(cpu, st->target, state, cbm, node);
		ret = cbm(cpu, node);
		trace_cpuhp_exit(cpu, st->state, state, ret);
		return ret;
	}

	/* State transition. Invoke on all instances */
	cnt = 0;
	hlist_for_each(node, &step->list) {
		if (lastp && node == *lastp)
			break;

		trace_cpuhp_multi_enter(cpu, st->target, state, cbm, node);
		ret = cbm(cpu, node);
		trace_cpuhp_exit(cpu, st->state, state, ret);
		if (ret) {
			if (!lastp)
				goto err;

			*lastp = node;
			return ret;
		}
		cnt++;
	}
	if (lastp)
		*lastp = NULL;
	return 0;
err:
	/* Rollback the instances if one failed */
	cbm = !bringup ? step->startup.multi : step->teardown.multi;
	if (!cbm)
		return ret;

	hlist_for_each(node, &step->list) {
		if (!cnt--)
			break;

		trace_cpuhp_multi_enter(cpu, st->target, state, cbm, node);
		ret = cbm(cpu, node);
		trace_cpuhp_exit(cpu, st->state, state, ret);
		/*
		 * Rollback must not fail,
		 */
		WARN_ON_ONCE(ret);
	}
	return ret;
}

#ifdef CONFIG_SMP
static bool cpuhp_is_ap_state(enum cpuhp_state state)
{
	/*
	 * The extra check for CPUHP_TEARDOWN_CPU is only for documentation
	 * purposes as that state is handled explicitly in cpu_down.
	 */
	return state > CPUHP_BRINGUP_CPU && state != CPUHP_TEARDOWN_CPU;
}

static inline void wait_for_ap_thread(struct cpuhp_cpu_state *st, bool bringup)
{
	struct completion *done = bringup ? &st->done_up : &st->done_down;
	wait_for_completion(done);
}

static inline void complete_ap_thread(struct cpuhp_cpu_state *st, bool bringup)
{
	struct completion *done = bringup ? &st->done_up : &st->done_down;
	complete(done);
}

/*
 * The former STARTING/DYING states, ran with IRQs disabled and must not fail.
 */
static bool cpuhp_is_atomic_state(enum cpuhp_state state)
{
	return CPUHP_AP_IDLE_DEAD <= state && state < CPUHP_AP_ONLINE;
}

/* Serializes the updates to cpu_online_mask, cpu_present_mask */
static DEFINE_MUTEX(cpu_add_remove_lock);
bool cpuhp_tasks_frozen;
EXPORT_SYMBOL_GPL(cpuhp_tasks_frozen);

/*
 * The following two APIs (cpu_maps_update_begin/done) must be used when
 * attempting to serialize the updates to cpu_online_mask & cpu_present_mask.
 */
void cpu_maps_update_begin(void)
{
	mutex_lock(&cpu_add_remove_lock);
}
EXPORT_SYMBOL_GPL(cpu_maps_update_begin);

void cpu_maps_update_done(void)
{
	mutex_unlock(&cpu_add_remove_lock);
}
EXPORT_SYMBOL_GPL(cpu_maps_update_done);

/*
 * If set, cpu_up and cpu_down will return -EBUSY and do nothing.
 * Should always be manipulated under cpu_add_remove_lock
 */
static int cpu_hotplug_disabled;

#ifdef CONFIG_HOTPLUG_CPU

DEFINE_STATIC_PERCPU_RWSEM(cpu_hotplug_lock);

void cpus_read_lock(void)
{
	percpu_down_read(&cpu_hotplug_lock);
}
EXPORT_SYMBOL_GPL(cpus_read_lock);

int cpus_read_trylock(void)
{
	return percpu_down_read_trylock(&cpu_hotplug_lock);
}
EXPORT_SYMBOL_GPL(cpus_read_trylock);

void cpus_read_unlock(void)
{
	percpu_up_read(&cpu_hotplug_lock);
}
EXPORT_SYMBOL_GPL(cpus_read_unlock);

void cpus_write_lock(void)
{
	percpu_down_write(&cpu_hotplug_lock);
}

void cpus_write_unlock(void)
{
	percpu_up_write(&cpu_hotplug_lock);
}

void lockdep_assert_cpus_held(void)
{
	/*
	 * We can't have hotplug operations before userspace starts running,
	 * and some init codepaths will knowingly not take the hotplug lock.
	 * This is all valid, so mute lockdep until it makes sense to report
	 * unheld locks.
	 */
	if (system_state < SYSTEM_RUNNING)
		return;

	percpu_rwsem_assert_held(&cpu_hotplug_lock);
}

static void lockdep_acquire_cpus_lock(void)
{
	rwsem_acquire(&cpu_hotplug_lock.dep_map, 0, 0, _THIS_IP_);
}

static void lockdep_release_cpus_lock(void)
{
	rwsem_release(&cpu_hotplug_lock.dep_map, _THIS_IP_);
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
	cpu_hotplug_disabled++;
	cpu_maps_update_done();
}
EXPORT_SYMBOL_GPL(cpu_hotplug_disable);

static void __cpu_hotplug_enable(void)
{
	if (WARN_ONCE(!cpu_hotplug_disabled, "Unbalanced cpu hotplug enable\n"))
		return;
	cpu_hotplug_disabled--;
}

void cpu_hotplug_enable(void)
{
	cpu_maps_update_begin();
	__cpu_hotplug_enable();
	cpu_maps_update_done();
}
EXPORT_SYMBOL_GPL(cpu_hotplug_enable);

#else

static void lockdep_acquire_cpus_lock(void)
{
}

static void lockdep_release_cpus_lock(void)
{
}

#endif	/* CONFIG_HOTPLUG_CPU */

/*
 * Architectures that need SMT-specific errata handling during SMT hotplug
 * should override this.
 */
void __weak arch_smt_update(void) { }

#ifdef CONFIG_HOTPLUG_SMT
enum cpuhp_smt_control cpu_smt_control __read_mostly = CPU_SMT_ENABLED;

void __init cpu_smt_disable(bool force)
{
	if (!cpu_smt_possible())
		return;

	if (force) {
		pr_info("SMT: Force disabled\n");
		cpu_smt_control = CPU_SMT_FORCE_DISABLED;
	} else {
		pr_info("SMT: disabled\n");
		cpu_smt_control = CPU_SMT_DISABLED;
	}
}

/*
 * The decision whether SMT is supported can only be done after the full
 * CPU identification. Called from architecture code.
 */
void __init cpu_smt_check_topology(void)
{
	if (!topology_smt_supported())
		cpu_smt_control = CPU_SMT_NOT_SUPPORTED;
}

static int __init smt_cmdline_disable(char *str)
{
	cpu_smt_disable(str && !strcmp(str, "force"));
	return 0;
}
early_param("nosmt", smt_cmdline_disable);

static inline bool cpu_smt_allowed(unsigned int cpu)
{
	if (cpu_smt_control == CPU_SMT_ENABLED)
		return true;

	if (topology_is_primary_thread(cpu))
		return true;

	/*
	 * On x86 it's required to boot all logical CPUs at least once so
	 * that the init code can get a chance to set CR4.MCE on each
	 * CPU. Otherwise, a broadcasted MCE observing CR4.MCE=0b on any
	 * core will shutdown the machine.
	 */
	return !cpumask_test_cpu(cpu, &cpus_booted_once_mask);
}

/* Returns true if SMT is not supported of forcefully (irreversibly) disabled */
bool cpu_smt_possible(void)
{
	return cpu_smt_control != CPU_SMT_FORCE_DISABLED &&
		cpu_smt_control != CPU_SMT_NOT_SUPPORTED;
}
EXPORT_SYMBOL_GPL(cpu_smt_possible);
#else
static inline bool cpu_smt_allowed(unsigned int cpu) { return true; }
#endif

static inline enum cpuhp_state
cpuhp_set_state(struct cpuhp_cpu_state *st, enum cpuhp_state target)
{
	enum cpuhp_state prev_state = st->state;

	st->rollback = false;
	st->last = NULL;

	st->target = target;
	st->single = false;
	st->bringup = st->state < target;

	return prev_state;
}

static inline void
cpuhp_reset_state(struct cpuhp_cpu_state *st, enum cpuhp_state prev_state)
{
	st->rollback = true;

	/*
	 * If we have st->last we need to undo partial multi_instance of this
	 * state first. Otherwise start undo at the previous state.
	 */
	if (!st->last) {
		if (st->bringup)
			st->state--;
		else
			st->state++;
	}

	st->target = prev_state;
	st->bringup = !st->bringup;
}

/* Regular hotplug invocation of the AP hotplug thread */
static void __cpuhp_kick_ap(struct cpuhp_cpu_state *st)
{
	if (!st->single && st->state == st->target)
		return;

	st->result = 0;
	/*
	 * Make sure the above stores are visible before should_run becomes
	 * true. Paired with the mb() above in cpuhp_thread_fun()
	 */
	smp_mb();
	st->should_run = true;
	wake_up_process(st->thread);
	wait_for_ap_thread(st, st->bringup);
}

static int cpuhp_kick_ap(struct cpuhp_cpu_state *st, enum cpuhp_state target)
{
	enum cpuhp_state prev_state;
	int ret;

	prev_state = cpuhp_set_state(st, target);
	__cpuhp_kick_ap(st);
	if ((ret = st->result)) {
		cpuhp_reset_state(st, prev_state);
		__cpuhp_kick_ap(st);
	}

	return ret;
}

static int bringup_wait_for_ap(unsigned int cpu)
{
	struct cpuhp_cpu_state *st = per_cpu_ptr(&cpuhp_state, cpu);

	/* Wait for the CPU to reach CPUHP_AP_ONLINE_IDLE */
	wait_for_ap_thread(st, true);
	if (WARN_ON_ONCE((!cpu_online(cpu))))
		return -ECANCELED;

	/* Unpark the hotplug thread of the target cpu */
	kthread_unpark(st->thread);

	/*
	 * SMT soft disabling on X86 requires to bring the CPU out of the
	 * BIOS 'wait for SIPI' state in order to set the CR4.MCE bit.  The
	 * CPU marked itself as booted_once in notify_cpu_starting() so the
	 * cpu_smt_allowed() check will now return false if this is not the
	 * primary sibling.
	 */
	if (!cpu_smt_allowed(cpu))
		return -ECANCELED;

	if (st->target <= CPUHP_AP_ONLINE_IDLE)
		return 0;

	return cpuhp_kick_ap(st, st->target);
}

static int bringup_cpu(unsigned int cpu)
{
	struct task_struct *idle = idle_thread_get(cpu);
	int ret;

	/*
	 * Some architectures have to walk the irq descriptors to
	 * setup the vector space for the cpu which comes online.
	 * Prevent irq alloc/free across the bringup.
	 */
	irq_lock_sparse();

	/* Arch-specific enabling code. */
	ret = __cpu_up(cpu, idle);
	irq_unlock_sparse();
	if (ret)
		return ret;
	return bringup_wait_for_ap(cpu);
}

static int finish_cpu(unsigned int cpu)
{
	struct task_struct *idle = idle_thread_get(cpu);
	struct mm_struct *mm = idle->active_mm;

	/*
	 * idle_task_exit() will have switched to &init_mm, now
	 * clean up any remaining active_mm state.
	 */
	if (mm != &init_mm)
		idle->active_mm = &init_mm;
	mmdrop(mm);
	return 0;
}

/*
 * Hotplug state machine related functions
 */

static void undo_cpu_up(unsigned int cpu, struct cpuhp_cpu_state *st)
{
	for (st->state--; st->state > st->target; st->state--)
		cpuhp_invoke_callback(cpu, st->state, false, NULL, NULL);
}

static inline bool can_rollback_cpu(struct cpuhp_cpu_state *st)
{
	if (IS_ENABLED(CONFIG_HOTPLUG_CPU))
		return true;
	/*
	 * When CPU hotplug is disabled, then taking the CPU down is not
	 * possible because takedown_cpu() and the architecture and
	 * subsystem specific mechanisms are not available. So the CPU
	 * which would be completely unplugged again needs to stay around
	 * in the current state.
	 */
	return st->state <= CPUHP_BRINGUP_CPU;
}

static int cpuhp_up_callbacks(unsigned int cpu, struct cpuhp_cpu_state *st,
			      enum cpuhp_state target)
{
	enum cpuhp_state prev_state = st->state;
	int ret = 0;

	while (st->state < target) {
		st->state++;
		ret = cpuhp_invoke_callback(cpu, st->state, true, NULL, NULL);
		if (ret) {
			if (can_rollback_cpu(st)) {
				st->target = prev_state;
				undo_cpu_up(cpu, st);
			}
			break;
		}
	}
	return ret;
}

/*
 * The cpu hotplug threads manage the bringup and teardown of the cpus
 */
static void cpuhp_create(unsigned int cpu)
{
	struct cpuhp_cpu_state *st = per_cpu_ptr(&cpuhp_state, cpu);

	init_completion(&st->done_up);
	init_completion(&st->done_down);
}

static int cpuhp_should_run(unsigned int cpu)
{
	struct cpuhp_cpu_state *st = this_cpu_ptr(&cpuhp_state);

	return st->should_run;
}

/*
 * Execute teardown/startup callbacks on the plugged cpu. Also used to invoke
 * callbacks when a state gets [un]installed at runtime.
 *
 * Each invocation of this function by the smpboot thread does a single AP
 * state callback.
 *
 * It has 3 modes of operation:
 *  - single: runs st->cb_state
 *  - up:     runs ++st->state, while st->state < st->target
 *  - down:   runs st->state--, while st->state > st->target
 *
 * When complete or on error, should_run is cleared and the completion is fired.
 */
static void cpuhp_thread_fun(unsigned int cpu)
{
	struct cpuhp_cpu_state *st = this_cpu_ptr(&cpuhp_state);
	bool bringup = st->bringup;
	enum cpuhp_state state;

	if (WARN_ON_ONCE(!st->should_run))
		return;

	/*
	 * ACQUIRE for the cpuhp_should_run() load of ->should_run. Ensures
	 * that if we see ->should_run we also see the rest of the state.
	 */
	smp_mb();

	/*
	 * The BP holds the hotplug lock, but we're now running on the AP,
	 * ensure that anybody asserting the lock is held, will actually find
	 * it so.
	 */
	lockdep_acquire_cpus_lock();
	cpuhp_lock_acquire(bringup);

	if (st->single) {
		state = st->cb_state;
		st->should_run = false;
	} else {
		if (bringup) {
			st->state++;
			state = st->state;
			st->should_run = (st->state < st->target);
			WARN_ON_ONCE(st->state > st->target);
		} else {
			state = st->state;
			st->state--;
			st->should_run = (st->state > st->target);
			WARN_ON_ONCE(st->state < st->target);
		}
	}

	WARN_ON_ONCE(!cpuhp_is_ap_state(state));

	if (cpuhp_is_atomic_state(state)) {
		local_irq_disable();
		st->result = cpuhp_invoke_callback(cpu, state, bringup, st->node, &st->last);
		local_irq_enable();

		/*
		 * STARTING/DYING must not fail!
		 */
		WARN_ON_ONCE(st->result);
	} else {
		st->result = cpuhp_invoke_callback(cpu, state, bringup, st->node, &st->last);
	}

	if (st->result) {
		/*
		 * If we fail on a rollback, we're up a creek without no
		 * paddle, no way forward, no way back. We loose, thanks for
		 * playing.
		 */
		WARN_ON_ONCE(st->rollback);
		st->should_run = false;
	}

	cpuhp_lock_release(bringup);
	lockdep_release_cpus_lock();

	if (!st->should_run)
		complete_ap_thread(st, bringup);
}

/* Invoke a single callback on a remote cpu */
static int
cpuhp_invoke_ap_callback(int cpu, enum cpuhp_state state, bool bringup,
			 struct hlist_node *node)
{
	struct cpuhp_cpu_state *st = per_cpu_ptr(&cpuhp_state, cpu);
	int ret;

	if (!cpu_online(cpu))
		return 0;

	cpuhp_lock_acquire(false);
	cpuhp_lock_release(false);

	cpuhp_lock_acquire(true);
	cpuhp_lock_release(true);

	/*
	 * If we are up and running, use the hotplug thread. For early calls
	 * we invoke the thread function directly.
	 */
	if (!st->thread)
		return cpuhp_invoke_callback(cpu, state, bringup, node, NULL);

	st->rollback = false;
	st->last = NULL;

	st->node = node;
	st->bringup = bringup;
	st->cb_state = state;
	st->single = true;

	__cpuhp_kick_ap(st);

	/*
	 * If we failed and did a partial, do a rollback.
	 */
	if ((ret = st->result) && st->last) {
		st->rollback = true;
		st->bringup = !bringup;

		__cpuhp_kick_ap(st);
	}

	/*
	 * Clean up the leftovers so the next hotplug operation wont use stale
	 * data.
	 */
	st->node = st->last = NULL;
	return ret;
}

static int cpuhp_kick_ap_work(unsigned int cpu)
{
	struct cpuhp_cpu_state *st = per_cpu_ptr(&cpuhp_state, cpu);
	enum cpuhp_state prev_state = st->state;
	int ret;

	cpuhp_lock_acquire(false);
	cpuhp_lock_release(false);

	cpuhp_lock_acquire(true);
	cpuhp_lock_release(true);

	trace_cpuhp_enter(cpu, st->target, prev_state, cpuhp_kick_ap_work);
	ret = cpuhp_kick_ap(st, st->target);
	trace_cpuhp_exit(cpu, st->state, prev_state, ret);

	return ret;
}

static struct smp_hotplug_thread cpuhp_threads = {
	.store			= &cpuhp_state.thread,
	.create			= &cpuhp_create,
	.thread_should_run	= cpuhp_should_run,
	.thread_fn		= cpuhp_thread_fun,
	.thread_comm		= "cpuhp/%u",
	.selfparking		= true,
};

void __init cpuhp_threads_init(void)
{
	BUG_ON(smpboot_register_percpu_thread(&cpuhp_threads));
	kthread_unpark(this_cpu_read(cpuhp_state.thread));
}

/*
 *
 * Serialize hotplug trainwrecks outside of the cpu_hotplug_lock
 * protected region.
 *
 * The operation is still serialized against concurrent CPU hotplug via
 * cpu_add_remove_lock, i.e. CPU map protection.  But it is _not_
 * serialized against other hotplug related activity like adding or
 * removing of state callbacks and state instances, which invoke either the
 * startup or the teardown callback of the affected state.
 *
 * This is required for subsystems which are unfixable vs. CPU hotplug and
 * evade lock inversion problems by scheduling work which has to be
 * completed _before_ cpu_up()/_cpu_down() returns.
 *
 * Don't even think about adding anything to this for any new code or even
 * drivers. It's only purpose is to keep existing lock order trainwrecks
 * working.
 *
 * For cpu_down() there might be valid reasons to finish cleanups which are
 * not required to be done under cpu_hotplug_lock, but that's a different
 * story and would be not invoked via this.
 */
static void cpu_up_down_serialize_trainwrecks(bool tasks_frozen)
{
	/*
	 * cpusets delegate hotplug operations to a worker to "solve" the
	 * lock order problems. Wait for the worker, but only if tasks are
	 * _not_ frozen (suspend, hibernate) as that would wait forever.
	 *
	 * The wait is required because otherwise the hotplug operation
	 * returns with inconsistent state, which could even be observed in
	 * user space when a new CPU is brought up. The CPU plug uevent
	 * would be delivered and user space reacting on it would fail to
	 * move tasks to the newly plugged CPU up to the point where the
	 * work has finished because up to that point the newly plugged CPU
	 * is not assignable in cpusets/cgroups. On unplug that's not
	 * necessarily a visible issue, but it is still inconsistent state,
	 * which is the real problem which needs to be "fixed". This can't
	 * prevent the transient state between scheduling the work and
	 * returning from waiting for it.
	 */
	if (!tasks_frozen)
		cpuset_wait_for_hotplug();
}

#ifdef CONFIG_HOTPLUG_CPU
#ifndef arch_clear_mm_cpumask_cpu
#define arch_clear_mm_cpumask_cpu(cpu, mm) cpumask_clear_cpu(cpu, mm_cpumask(mm))
#endif

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
		arch_clear_mm_cpumask_cpu(cpu, t->mm);
		task_unlock(t);
	}
	rcu_read_unlock();
}

/* Take this CPU down. */
static int take_cpu_down(void *_param)
{
	struct cpuhp_cpu_state *st = this_cpu_ptr(&cpuhp_state);
	enum cpuhp_state target = max((int)st->target, CPUHP_AP_OFFLINE);
	int err, cpu = smp_processor_id();
	int ret;

	/* Ensure this CPU doesn't handle any more interrupts. */
	err = __cpu_disable();
	if (err < 0)
		return err;

	/*
	 * We get here while we are in CPUHP_TEARDOWN_CPU state and we must not
	 * do this step again.
	 */
	WARN_ON(st->state != CPUHP_TEARDOWN_CPU);
	st->state--;
	/* Invoke the former CPU_DYING callbacks */
	for (; st->state > target; st->state--) {
		ret = cpuhp_invoke_callback(cpu, st->state, false, NULL, NULL);
		/*
		 * DYING must not fail!
		 */
		WARN_ON_ONCE(ret);
	}

	/* Give up timekeeping duties */
	tick_handover_do_timer();
	/* Remove CPU from timer broadcasting */
	tick_offline_cpu(cpu);
	/* Park the stopper thread */
	stop_machine_park(cpu);
	return 0;
}

static int takedown_cpu(unsigned int cpu)
{
	struct cpuhp_cpu_state *st = per_cpu_ptr(&cpuhp_state, cpu);
	int err;

	/* Park the smpboot threads */
	kthread_park(per_cpu_ptr(&cpuhp_state, cpu)->thread);

	/*
	 * Prevent irq alloc/free while the dying cpu reorganizes the
	 * interrupt affinities.
	 */
	irq_lock_sparse();

	/*
	 * So now all preempt/rcu users must observe !cpu_active().
	 */
	err = stop_machine_cpuslocked(take_cpu_down, NULL, cpumask_of(cpu));
	if (err) {
		/* CPU refused to die */
		irq_unlock_sparse();
		/* Unpark the hotplug thread so we can rollback there */
		kthread_unpark(per_cpu_ptr(&cpuhp_state, cpu)->thread);
		return err;
	}
	BUG_ON(cpu_online(cpu));

	/*
	 * The teardown callback for CPUHP_AP_SCHED_STARTING will have removed
	 * all runnable tasks from the CPU, there's only the idle task left now
	 * that the migration thread is done doing the stop_machine thing.
	 *
	 * Wait for the stop thread to go away.
	 */
	wait_for_ap_thread(st, false);
	BUG_ON(st->state != CPUHP_AP_IDLE_DEAD);

	/* Interrupts are moved away from the dying cpu, reenable alloc/free */
	irq_unlock_sparse();

	hotplug_cpu__broadcast_tick_pull(cpu);
	/* This actually kills the CPU. */
	__cpu_die(cpu);

	tick_cleanup_dead_cpu(cpu);
	rcutree_migrate_callbacks(cpu);
	return 0;
}

static void cpuhp_complete_idle_dead(void *arg)
{
	struct cpuhp_cpu_state *st = arg;

	complete_ap_thread(st, false);
}

void cpuhp_report_idle_dead(void)
{
	struct cpuhp_cpu_state *st = this_cpu_ptr(&cpuhp_state);

	BUG_ON(st->state != CPUHP_AP_OFFLINE);
	rcu_report_dead(smp_processor_id());
	st->state = CPUHP_AP_IDLE_DEAD;
	/*
	 * We cannot call complete after rcu_report_dead() so we delegate it
	 * to an online cpu.
	 */
	smp_call_function_single(cpumask_first(cpu_online_mask),
				 cpuhp_complete_idle_dead, st, 0);
}

static void undo_cpu_down(unsigned int cpu, struct cpuhp_cpu_state *st)
{
	for (st->state++; st->state < st->target; st->state++)
		cpuhp_invoke_callback(cpu, st->state, true, NULL, NULL);
}

static int cpuhp_down_callbacks(unsigned int cpu, struct cpuhp_cpu_state *st,
				enum cpuhp_state target)
{
	enum cpuhp_state prev_state = st->state;
	int ret = 0;

	for (; st->state > target; st->state--) {
		ret = cpuhp_invoke_callback(cpu, st->state, false, NULL, NULL);
		if (ret) {
			st->target = prev_state;
			if (st->state < prev_state)
				undo_cpu_down(cpu, st);
			break;
		}
	}
	return ret;
}

/* Requires cpu_add_remove_lock to be held */
static int __ref _cpu_down(unsigned int cpu, int tasks_frozen,
			   enum cpuhp_state target)
{
	struct cpuhp_cpu_state *st = per_cpu_ptr(&cpuhp_state, cpu);
	int prev_state, ret = 0;

	if (num_active_cpus() == 1 && cpu_active(cpu))
		return -EBUSY;

	if (!cpu_present(cpu))
		return -EINVAL;

	cpus_write_lock();

	cpuhp_tasks_frozen = tasks_frozen;

	prev_state = cpuhp_set_state(st, target);
	/*
	 * If the current CPU state is in the range of the AP hotplug thread,
	 * then we need to kick the thread.
	 */
	if (st->state > CPUHP_TEARDOWN_CPU) {
		st->target = max((int)target, CPUHP_TEARDOWN_CPU);
		ret = cpuhp_kick_ap_work(cpu);
		/*
		 * The AP side has done the error rollback already. Just
		 * return the error code..
		 */
		if (ret)
			goto out;

		/*
		 * We might have stopped still in the range of the AP hotplug
		 * thread. Nothing to do anymore.
		 */
		if (st->state > CPUHP_TEARDOWN_CPU)
			goto out;

		st->target = target;
	}
	/*
	 * The AP brought itself down to CPUHP_TEARDOWN_CPU. So we need
	 * to do the further cleanups.
	 */
	ret = cpuhp_down_callbacks(cpu, st, target);
	if (ret && st->state == CPUHP_TEARDOWN_CPU && st->state < prev_state) {
		cpuhp_reset_state(st, prev_state);
		__cpuhp_kick_ap(st);
	}

out:
	cpus_write_unlock();
	/*
	 * Do post unplug cleanup. This is still protected against
	 * concurrent CPU hotplug via cpu_add_remove_lock.
	 */
	lockup_detector_cleanup();
	arch_smt_update();
	cpu_up_down_serialize_trainwrecks(tasks_frozen);
	return ret;
}

static int cpu_down_maps_locked(unsigned int cpu, enum cpuhp_state target)
{
	if (cpu_hotplug_disabled)
		return -EBUSY;
	return _cpu_down(cpu, 0, target);
}

static int cpu_down(unsigned int cpu, enum cpuhp_state target)
{
	int err;

	trace_android_vh_cpu_down(cpu);

	cpu_maps_update_begin();
	err = cpu_down_maps_locked(cpu, target);
	cpu_maps_update_done();
	return err;
}

/**
 * cpu_device_down - Bring down a cpu device
 * @dev: Pointer to the cpu device to offline
 *
 * This function is meant to be used by device core cpu subsystem only.
 *
 * Other subsystems should use remove_cpu() instead.
 */
int cpu_device_down(struct device *dev)
{
	return cpu_down(dev->id, CPUHP_OFFLINE);
}

int remove_cpu(unsigned int cpu)
{
	int ret;

	lock_device_hotplug();
	ret = device_offline(get_cpu_device(cpu));
	unlock_device_hotplug();

	return ret;
}
EXPORT_SYMBOL_GPL(remove_cpu);

extern bool dl_cpu_busy(unsigned int cpu);

int __pause_drain_rq(struct cpumask *cpus)
{
	unsigned int cpu;
	int err = 0;

	/*
	 * Disabling preemption avoids that one of the stopper, started from
	 * sched_cpu_drain_rq(), blocks firing draining for the whole cpumask.
	 */
	preempt_disable();
	for_each_cpu(cpu, cpus) {
		err = sched_cpu_drain_rq(cpu);
		if (err)
			break;
	}
	preempt_enable();

	return err;
}

void __wait_drain_rq(struct cpumask *cpus)
{
	unsigned int cpu;

	for_each_cpu(cpu, cpus)
		sched_cpu_drain_rq_wait(cpu);
}

/* if rt task, set to cfs and return previous prio */
static int pause_reduce_prio(void)
{
	int prev_prio = -1;

	if (current->prio < MAX_RT_PRIO) {
		struct sched_param param = { .sched_priority = 0 };

		prev_prio = current->prio;
		sched_setscheduler_nocheck(current, SCHED_NORMAL, &param);
	}

	return prev_prio;
}

/* if previous prio was set, restore */
static void pause_restore_prio(int prev_prio)
{
	if (prev_prio >= 0 && prev_prio < MAX_RT_PRIO) {
		struct sched_param param = { .sched_priority = MAX_RT_PRIO-1-prev_prio };

		sched_setscheduler_nocheck(current, SCHED_FIFO, &param);
	}
}

int pause_cpus(struct cpumask *cpus)
{
	int err = 0;
	int cpu;
	u64 start_time = 0;
	int prev_prio;

	start_time = sched_clock();

	cpu_maps_update_begin();

	if (cpu_hotplug_disabled) {
		err = -EBUSY;
		goto err_cpu_maps_update;
	}

	/* Pausing an already inactive CPU isn't an error */
	cpumask_and(cpus, cpus, cpu_active_mask);

	for_each_cpu(cpu, cpus) {
		if (!cpu_online(cpu) || dl_cpu_busy(cpu) ||
			get_cpu_device(cpu)->offline_disabled == true) {
			err = -EBUSY;
			goto err_cpu_maps_update;
		}
	}

	if (cpumask_weight(cpus) >= num_active_cpus()) {
		err = -EBUSY;
		goto err_cpu_maps_update;
	}

	if (cpumask_empty(cpus))
		goto err_cpu_maps_update;

	/*
	 * Lazy migration:
	 *
	 * We do care about how fast a CPU can go idle and stay this in this
	 * state. If we try to take the cpus_write_lock() here, we would have
	 * to wait for a few dozens of ms, as this function might schedule.
	 * However, we can, as a first step, flip the active mask and migrate
	 * anything currently on the run-queue, to give a chance to the paused
	 * CPUs to reach quickly an idle state. There's a risk meanwhile for
	 * another CPU to observe an out-of-date active_mask or to incompletely
	 * update a cpuset. Both problems would be resolved later in the slow
	 * path, which ensures active_mask synchronization, triggers a cpuset
	 * rebuild and migrate any task that would have escaped the lazy
	 * migration.
	 */
	for_each_cpu(cpu, cpus)
		set_cpu_active(cpu, false);
	err = __pause_drain_rq(cpus);
	if (err) {
		__wait_drain_rq(cpus);
		for_each_cpu(cpu, cpus)
			set_cpu_active(cpu, true);
		goto err_cpu_maps_update;
	}

	prev_prio = pause_reduce_prio();

	/*
	 * Slow path deactivation:
	 *
	 * Now that paused CPUs are most likely idle, we can go through a
	 * complete scheduler deactivation.
	 *
	 * The cpu_active_mask being already set and cpus_write_lock calling
	 * synchronize_rcu(), we know that all preempt-disabled and RCU users
	 * will observe the updated value.
	 */
	cpus_write_lock();

	__wait_drain_rq(cpus);

	cpuhp_tasks_frozen = 0;

	if (sched_cpus_deactivate_nosync(cpus)) {
		err = -EBUSY;
		goto err_cpus_write_unlock;
	}

	err = __pause_drain_rq(cpus);
	__wait_drain_rq(cpus);
	if (err) {
		for_each_cpu(cpu, cpus)
			sched_cpu_activate(cpu);
		goto err_cpus_write_unlock;
	}

	/*
	 * Even if living on the side of the regular HP path, pause is using
	 * one of the HP step (CPUHP_AP_ACTIVE). This should be reflected on the
	 * current state of the CPU.
	 */
	for_each_cpu(cpu, cpus) {
		struct cpuhp_cpu_state *st = per_cpu_ptr(&cpuhp_state, cpu);

		st->state = CPUHP_AP_ACTIVE - 1;
		st->target = st->state;
	}

err_cpus_write_unlock:
	cpus_write_unlock();
	pause_restore_prio(prev_prio);
err_cpu_maps_update:
	cpu_maps_update_done();

	trace_cpuhp_pause(cpus, start_time, 1);

	return err;
}
EXPORT_SYMBOL_GPL(pause_cpus);

int resume_cpus(struct cpumask *cpus)
{
	unsigned int cpu;
	int err = 0;
	u64 start_time = 0;
	int prev_prio;

	start_time = sched_clock();

	cpu_maps_update_begin();

	if (cpu_hotplug_disabled) {
		err = -EBUSY;
		goto err_cpu_maps_update;
	}

	/* Resuming an already active CPU isn't an error */
	cpumask_andnot(cpus, cpus, cpu_active_mask);

	for_each_cpu(cpu, cpus) {
		if (!cpu_online(cpu)) {
			err = -EBUSY;
			goto err_cpu_maps_update;
		}
	}

	if (cpumask_empty(cpus))
		goto err_cpu_maps_update;

	for_each_cpu(cpu, cpus)
		set_cpu_active(cpu, true);

	trace_android_rvh_resume_cpus(cpus, &err);
	if (err)
		goto err_cpu_maps_update;

	prev_prio = pause_reduce_prio();

	/* Lazy Resume. Build domains through schedule a workqueue on
	 * resuming cpu. This is so that the resuming cpu can work more
	 * early, and cannot add additional load to other busy cpu.
	 */
	cpuset_update_active_cpus_affine(cpumask_first(cpus));

	cpus_write_lock();

	cpuhp_tasks_frozen = 0;

	if (sched_cpus_activate(cpus)) {
		err = -EBUSY;
		goto err_cpus_write_unlock;
	}

	/*
	 * see pause_cpus.
	 */
	for_each_cpu(cpu, cpus) {
		struct cpuhp_cpu_state *st = per_cpu_ptr(&cpuhp_state, cpu);

		st->state = CPUHP_ONLINE;
		st->target = st->state;
	}

err_cpus_write_unlock:
	cpus_write_unlock();
	pause_restore_prio(prev_prio);
err_cpu_maps_update:
	cpu_maps_update_done();

	trace_cpuhp_pause(cpus, start_time, 0);

	return err;
}
EXPORT_SYMBOL_GPL(resume_cpus);

void smp_shutdown_nonboot_cpus(unsigned int primary_cpu)
{
	unsigned int cpu;
	int error;

	cpu_maps_update_begin();

	/*
	 * Make certain the cpu I'm about to reboot on is online.
	 *
	 * This is inline to what migrate_to_reboot_cpu() already do.
	 */
	if (!cpu_online(primary_cpu))
		primary_cpu = cpumask_first(cpu_online_mask);

	for_each_online_cpu(cpu) {
		if (cpu == primary_cpu)
			continue;

		error = cpu_down_maps_locked(cpu, CPUHP_OFFLINE);
		if (error) {
			pr_err("Failed to offline CPU%d - error=%d",
				cpu, error);
			break;
		}
	}

	/*
	 * Ensure all but the reboot CPU are offline.
	 */
	BUG_ON(num_online_cpus() > 1);

	/*
	 * Make sure the CPUs won't be enabled by someone else after this
	 * point. Kexec will reboot to a new kernel shortly resetting
	 * everything along the way.
	 */
	cpu_hotplug_disabled++;

	cpu_maps_update_done();
}

#else
#define takedown_cpu		NULL
#endif /*CONFIG_HOTPLUG_CPU*/

/**
 * notify_cpu_starting(cpu) - Invoke the callbacks on the starting CPU
 * @cpu: cpu that just started
 *
 * It must be called by the arch code on the new cpu, before the new cpu
 * enables interrupts and before the "boot" cpu returns from __cpu_up().
 */
void notify_cpu_starting(unsigned int cpu)
{
	struct cpuhp_cpu_state *st = per_cpu_ptr(&cpuhp_state, cpu);
	enum cpuhp_state target = min((int)st->target, CPUHP_AP_ONLINE);
	int ret;

	rcu_cpu_starting(cpu);	/* Enables RCU usage on this CPU. */
	cpumask_set_cpu(cpu, &cpus_booted_once_mask);
	while (st->state < target) {
		st->state++;
		ret = cpuhp_invoke_callback(cpu, st->state, true, NULL, NULL);
		/*
		 * STARTING must not fail!
		 */
		WARN_ON_ONCE(ret);
	}
}

/*
 * Called from the idle task. Wake up the controlling task which brings the
 * hotplug thread of the upcoming CPU up and then delegates the rest of the
 * online bringup to the hotplug thread.
 */
void cpuhp_online_idle(enum cpuhp_state state)
{
	struct cpuhp_cpu_state *st = this_cpu_ptr(&cpuhp_state);

	/* Happens for the boot cpu */
	if (state != CPUHP_AP_ONLINE_IDLE)
		return;

	/*
	 * Unpart the stopper thread before we start the idle loop (and start
	 * scheduling); this ensures the stopper task is always available.
	 */
	stop_machine_unpark(smp_processor_id());

	st->state = CPUHP_AP_ONLINE_IDLE;
	complete_ap_thread(st, true);
}

static int switch_to_rt_policy(void)
{
	struct sched_param param = { .sched_priority = MAX_RT_PRIO - 1 };
	unsigned int policy = current->policy;

	if (policy == SCHED_NORMAL)
		/* Switch to SCHED_FIFO from SCHED_NORMAL. */
		return sched_setscheduler_nocheck(current, SCHED_FIFO, &param);
	else
		return 1;
}

static int switch_to_fair_policy(void)
{
	struct sched_param param = { .sched_priority = 0 };

	return sched_setscheduler_nocheck(current, SCHED_NORMAL, &param);
}

/* Requires cpu_add_remove_lock to be held */
static int _cpu_up(unsigned int cpu, int tasks_frozen, enum cpuhp_state target)
{
	struct cpuhp_cpu_state *st = per_cpu_ptr(&cpuhp_state, cpu);
	struct task_struct *idle;
	int ret = 0;

	cpus_write_lock();

	if (!cpu_present(cpu)) {
		ret = -EINVAL;
		goto out;
	}

	/*
	 * The caller of cpu_up() might have raced with another
	 * caller. Nothing to do.
	 */
	if (st->state >= target)
		goto out;

	if (st->state == CPUHP_OFFLINE) {
		/* Let it fail before we try to bring the cpu up */
		idle = idle_thread_get(cpu);
		if (IS_ERR(idle)) {
			ret = PTR_ERR(idle);
			goto out;
		}
	}

	cpuhp_tasks_frozen = tasks_frozen;

	cpuhp_set_state(st, target);
	/*
	 * If the current CPU state is in the range of the AP hotplug thread,
	 * then we need to kick the thread once more.
	 */
	if (st->state > CPUHP_BRINGUP_CPU) {
		ret = cpuhp_kick_ap_work(cpu);
		/*
		 * The AP side has done the error rollback already. Just
		 * return the error code..
		 */
		if (ret)
			goto out;
	}

	/*
	 * Try to reach the target state. We max out on the BP at
	 * CPUHP_BRINGUP_CPU. After that the AP hotplug thread is
	 * responsible for bringing it up to the target state.
	 */
	target = min((int)target, CPUHP_BRINGUP_CPU);
	ret = cpuhp_up_callbacks(cpu, st, target);
out:
	cpus_write_unlock();
	arch_smt_update();
	cpu_up_down_serialize_trainwrecks(tasks_frozen);
	return ret;
}

static int cpu_up(unsigned int cpu, enum cpuhp_state target)
{
	int err = 0;
	int switch_err;

	if (!cpu_possible(cpu)) {
		pr_err("can't online cpu %d because it is not configured as may-hotadd at boot time\n",
		       cpu);
#if defined(CONFIG_IA64)
		pr_err("please check additional_cpus= boot parameter\n");
#endif
		return -EINVAL;
	}

	trace_android_vh_cpu_up(cpu);

	/*
	 * CPU hotplug operations consists of many steps and each step
	 * calls a callback of core kernel subsystem. CPU hotplug-in
	 * operation may get preempted by other CFS tasks and whole
	 * operation of cpu hotplug in CPU gets delayed. Switch the
	 * current task to SCHED_FIFO from SCHED_NORMAL, so that
	 * hotplug in operation may complete quickly in heavy loaded
	 * conditions and new CPU will start handle the workload.
	 */

	switch_err = switch_to_rt_policy();

	err = try_online_node(cpu_to_node(cpu));
	if (err)
		goto switch_out;

	cpu_maps_update_begin();

	if (cpu_hotplug_disabled) {
		err = -EBUSY;
		goto out;
	}
	if (!cpu_smt_allowed(cpu)) {
		err = -EPERM;
		goto out;
	}

	err = _cpu_up(cpu, 0, target);
out:
	cpu_maps_update_done();
switch_out:
	if (!switch_err) {
		switch_err = switch_to_fair_policy();
		if (switch_err)
			pr_err("Hotplug policy switch err=%d Task %s pid=%d\n",
				switch_err, current->comm, current->pid);
	}

	return err;
}

/**
 * cpu_device_up - Bring up a cpu device
 * @dev: Pointer to the cpu device to online
 *
 * This function is meant to be used by device core cpu subsystem only.
 *
 * Other subsystems should use add_cpu() instead.
 */
int cpu_device_up(struct device *dev)
{
	return cpu_up(dev->id, CPUHP_ONLINE);
}

int add_cpu(unsigned int cpu)
{
	int ret;

	lock_device_hotplug();
	ret = device_online(get_cpu_device(cpu));
	unlock_device_hotplug();

	return ret;
}
EXPORT_SYMBOL_GPL(add_cpu);

/**
 * bringup_hibernate_cpu - Bring up the CPU that we hibernated on
 * @sleep_cpu: The cpu we hibernated on and should be brought up.
 *
 * On some architectures like arm64, we can hibernate on any CPU, but on
 * wake up the CPU we hibernated on might be offline as a side effect of
 * using maxcpus= for example.
 */
int bringup_hibernate_cpu(unsigned int sleep_cpu)
{
	int ret;

	if (!cpu_online(sleep_cpu)) {
		pr_info("Hibernated on a CPU that is offline! Bringing CPU up.\n");
		ret = cpu_up(sleep_cpu, CPUHP_ONLINE);
		if (ret) {
			pr_err("Failed to bring hibernate-CPU up!\n");
			return ret;
		}
	}
	return 0;
}

void bringup_nonboot_cpus(unsigned int setup_max_cpus)
{
	unsigned int cpu;

	for_each_present_cpu(cpu) {
		if (num_online_cpus() >= setup_max_cpus)
			break;
		if (!cpu_online(cpu))
			cpu_up(cpu, CPUHP_ONLINE);
	}
}

#ifdef CONFIG_PM_SLEEP_SMP
static cpumask_var_t frozen_cpus;

int freeze_secondary_cpus(int primary)
{
	int cpu, error = 0;

	cpu_maps_update_begin();
	if (primary == -1) {
		primary = cpumask_first(cpu_online_mask);
		if (!housekeeping_cpu(primary, HK_FLAG_TIMER))
			primary = housekeeping_any_cpu(HK_FLAG_TIMER);
	} else {
		if (!cpu_online(primary))
			primary = cpumask_first(cpu_online_mask);
	}

	/*
	 * We take down all of the non-boot CPUs in one shot to avoid races
	 * with the userspace trying to use the CPU hotplug at the same time
	 */
	cpumask_clear(frozen_cpus);

	pr_info("Disabling non-boot CPUs ...\n");
	for_each_online_cpu(cpu) {
		if (cpu == primary)
			continue;

		if (pm_wakeup_pending()) {
			pr_info("Wakeup pending. Abort CPU freeze\n");
			error = -EBUSY;
			break;
		}

		trace_suspend_resume(TPS("CPU_OFF"), cpu, true);
		error = _cpu_down(cpu, 1, CPUHP_OFFLINE);
		trace_suspend_resume(TPS("CPU_OFF"), cpu, false);
		if (!error)
			cpumask_set_cpu(cpu, frozen_cpus);
		else {
			pr_err("Error taking CPU%d down: %d\n", cpu, error);
			break;
		}
	}

	if (!error)
		BUG_ON(num_online_cpus() > 1);
	else
		pr_err("Non-boot CPUs are not disabled\n");

	/*
	 * Make sure the CPUs won't be enabled by someone else. We need to do
	 * this even in case of failure as all freeze_secondary_cpus() users are
	 * supposed to do thaw_secondary_cpus() on the failure path.
	 */
	cpu_hotplug_disabled++;

	cpu_maps_update_done();
	return error;
}

void __weak arch_thaw_secondary_cpus_begin(void)
{
}

void __weak arch_thaw_secondary_cpus_end(void)
{
}

void thaw_secondary_cpus(void)
{
	int cpu, error;
	struct device *cpu_device;

	/* Allow everyone to use the CPU hotplug again */
	cpu_maps_update_begin();
	__cpu_hotplug_enable();
	if (cpumask_empty(frozen_cpus))
		goto out;

	pr_info("Enabling non-boot CPUs ...\n");

	arch_thaw_secondary_cpus_begin();

	for_each_cpu(cpu, frozen_cpus) {
		trace_suspend_resume(TPS("CPU_ON"), cpu, true);
		error = _cpu_up(cpu, 1, CPUHP_ONLINE);
		trace_suspend_resume(TPS("CPU_ON"), cpu, false);
		if (!error) {
			pr_info("CPU%d is up\n", cpu);
			cpu_device = get_cpu_device(cpu);
			if (!cpu_device)
				pr_err("%s: failed to get cpu%d device\n",
				       __func__, cpu);
			else
				kobject_uevent(&cpu_device->kobj, KOBJ_ONLINE);
			continue;
		}
		pr_warn("Error taking CPU%d up: %d\n", cpu, error);
	}

	arch_thaw_secondary_cpus_end();

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

int __boot_cpu_id;

#endif /* CONFIG_SMP */

/* Boot processor state steps */
static struct cpuhp_step cpuhp_hp_states[] = {
	[CPUHP_OFFLINE] = {
		.name			= "offline",
		.startup.single		= NULL,
		.teardown.single	= NULL,
	},
#ifdef CONFIG_SMP
	[CPUHP_CREATE_THREADS]= {
		.name			= "threads:prepare",
		.startup.single		= smpboot_create_threads,
		.teardown.single	= NULL,
		.cant_stop		= true,
	},
	[CPUHP_PERF_PREPARE] = {
		.name			= "perf:prepare",
		.startup.single		= perf_event_init_cpu,
		.teardown.single	= perf_event_exit_cpu,
	},
	[CPUHP_WORKQUEUE_PREP] = {
		.name			= "workqueue:prepare",
		.startup.single		= workqueue_prepare_cpu,
		.teardown.single	= NULL,
	},
	[CPUHP_HRTIMERS_PREPARE] = {
		.name			= "hrtimers:prepare",
		.startup.single		= hrtimers_prepare_cpu,
		.teardown.single	= hrtimers_dead_cpu,
	},
	[CPUHP_SMPCFD_PREPARE] = {
		.name			= "smpcfd:prepare",
		.startup.single		= smpcfd_prepare_cpu,
		.teardown.single	= smpcfd_dead_cpu,
	},
	[CPUHP_RELAY_PREPARE] = {
		.name			= "relay:prepare",
		.startup.single		= relay_prepare_cpu,
		.teardown.single	= NULL,
	},
	[CPUHP_SLAB_PREPARE] = {
		.name			= "slab:prepare",
		.startup.single		= slab_prepare_cpu,
		.teardown.single	= slab_dead_cpu,
	},
	[CPUHP_RCUTREE_PREP] = {
		.name			= "RCU/tree:prepare",
		.startup.single		= rcutree_prepare_cpu,
		.teardown.single	= rcutree_dead_cpu,
	},
	/*
	 * On the tear-down path, timers_dead_cpu() must be invoked
	 * before blk_mq_queue_reinit_notify() from notify_dead(),
	 * otherwise a RCU stall occurs.
	 */
	[CPUHP_TIMERS_PREPARE] = {
		.name			= "timers:prepare",
		.startup.single		= timers_prepare_cpu,
		.teardown.single	= timers_dead_cpu,
	},
	/* Kicks the plugged cpu into life */
	[CPUHP_BRINGUP_CPU] = {
		.name			= "cpu:bringup",
		.startup.single		= bringup_cpu,
		.teardown.single	= finish_cpu,
		.cant_stop		= true,
	},
	/* Final state before CPU kills itself */
	[CPUHP_AP_IDLE_DEAD] = {
		.name			= "idle:dead",
	},
	/*
	 * Last state before CPU enters the idle loop to die. Transient state
	 * for synchronization.
	 */
	[CPUHP_AP_OFFLINE] = {
		.name			= "ap:offline",
		.cant_stop		= true,
	},
	/* First state is scheduler control. Interrupts are disabled */
	[CPUHP_AP_SCHED_STARTING] = {
		.name			= "sched:starting",
		.startup.single		= sched_cpu_starting,
		.teardown.single	= sched_cpu_dying,
	},
	[CPUHP_AP_RCUTREE_DYING] = {
		.name			= "RCU/tree:dying",
		.startup.single		= NULL,
		.teardown.single	= rcutree_dying_cpu,
	},
	[CPUHP_AP_SMPCFD_DYING] = {
		.name			= "smpcfd:dying",
		.startup.single		= NULL,
		.teardown.single	= smpcfd_dying_cpu,
	},
	/* Entry state on starting. Interrupts enabled from here on. Transient
	 * state for synchronsization */
	[CPUHP_AP_ONLINE] = {
		.name			= "ap:online",
	},
	/*
	 * Handled on controll processor until the plugged processor manages
	 * this itself.
	 */
	[CPUHP_TEARDOWN_CPU] = {
		.name			= "cpu:teardown",
		.startup.single		= NULL,
		.teardown.single	= takedown_cpu,
		.cant_stop		= true,
	},
	/* Handle smpboot threads park/unpark */
	[CPUHP_AP_SMPBOOT_THREADS] = {
		.name			= "smpboot/threads:online",
		.startup.single		= smpboot_unpark_threads,
		.teardown.single	= smpboot_park_threads,
	},
	[CPUHP_AP_IRQ_AFFINITY_ONLINE] = {
		.name			= "irq/affinity:online",
		.startup.single		= irq_affinity_online_cpu,
		.teardown.single	= NULL,
	},
	[CPUHP_AP_PERF_ONLINE] = {
		.name			= "perf:online",
		.startup.single		= perf_event_init_cpu,
		.teardown.single	= perf_event_exit_cpu,
	},
	[CPUHP_AP_WATCHDOG_ONLINE] = {
		.name			= "lockup_detector:online",
		.startup.single		= lockup_detector_online_cpu,
		.teardown.single	= lockup_detector_offline_cpu,
	},
	[CPUHP_AP_WORKQUEUE_ONLINE] = {
		.name			= "workqueue:online",
		.startup.single		= workqueue_online_cpu,
		.teardown.single	= workqueue_offline_cpu,
	},
	[CPUHP_AP_RCUTREE_ONLINE] = {
		.name			= "RCU/tree:online",
		.startup.single		= rcutree_online_cpu,
		.teardown.single	= rcutree_offline_cpu,
	},
#endif
	/*
	 * The dynamically registered state space is here
	 */

#ifdef CONFIG_SMP
	/* Last state is scheduler control setting the cpu active */
	[CPUHP_AP_ACTIVE] = {
		.name			= "sched:active",
		.startup.single		= sched_cpu_activate,
		.teardown.single	= sched_cpu_deactivate,
	},
#endif

	/* CPU is fully up and running. */
	[CPUHP_ONLINE] = {
		.name			= "online",
		.startup.single		= NULL,
		.teardown.single	= NULL,
	},
};

/* Sanity check for callbacks */
static int cpuhp_cb_check(enum cpuhp_state state)
{
	if (state <= CPUHP_OFFLINE || state >= CPUHP_ONLINE)
		return -EINVAL;
	return 0;
}

/*
 * Returns a free for dynamic slot assignment of the Online state. The states
 * are protected by the cpuhp_slot_states mutex and an empty slot is identified
 * by having no name assigned.
 */
static int cpuhp_reserve_state(enum cpuhp_state state)
{
	enum cpuhp_state i, end;
	struct cpuhp_step *step;

	switch (state) {
	case CPUHP_AP_ONLINE_DYN:
		step = cpuhp_hp_states + CPUHP_AP_ONLINE_DYN;
		end = CPUHP_AP_ONLINE_DYN_END;
		break;
	case CPUHP_BP_PREPARE_DYN:
		step = cpuhp_hp_states + CPUHP_BP_PREPARE_DYN;
		end = CPUHP_BP_PREPARE_DYN_END;
		break;
	default:
		return -EINVAL;
	}

	for (i = state; i <= end; i++, step++) {
		if (!step->name)
			return i;
	}
	WARN(1, "No more dynamic states available for CPU hotplug\n");
	return -ENOSPC;
}

static int cpuhp_store_callbacks(enum cpuhp_state state, const char *name,
				 int (*startup)(unsigned int cpu),
				 int (*teardown)(unsigned int cpu),
				 bool multi_instance)
{
	/* (Un)Install the callbacks for further cpu hotplug operations */
	struct cpuhp_step *sp;
	int ret = 0;

	/*
	 * If name is NULL, then the state gets removed.
	 *
	 * CPUHP_AP_ONLINE_DYN and CPUHP_BP_PREPARE_DYN are handed out on
	 * the first allocation from these dynamic ranges, so the removal
	 * would trigger a new allocation and clear the wrong (already
	 * empty) state, leaving the callbacks of the to be cleared state
	 * dangling, which causes wreckage on the next hotplug operation.
	 */
	if (name && (state == CPUHP_AP_ONLINE_DYN ||
		     state == CPUHP_BP_PREPARE_DYN)) {
		ret = cpuhp_reserve_state(state);
		if (ret < 0)
			return ret;
		state = ret;
	}
	sp = cpuhp_get_step(state);
	if (name && sp->name)
		return -EBUSY;

	sp->startup.single = startup;
	sp->teardown.single = teardown;
	sp->name = name;
	sp->multi_instance = multi_instance;
	INIT_HLIST_HEAD(&sp->list);
	return ret;
}

static void *cpuhp_get_teardown_cb(enum cpuhp_state state)
{
	return cpuhp_get_step(state)->teardown.single;
}

/*
 * Call the startup/teardown function for a step either on the AP or
 * on the current CPU.
 */
static int cpuhp_issue_call(int cpu, enum cpuhp_state state, bool bringup,
			    struct hlist_node *node)
{
	struct cpuhp_step *sp = cpuhp_get_step(state);
	int ret;

	/*
	 * If there's nothing to do, we done.
	 * Relies on the union for multi_instance.
	 */
	if ((bringup && !sp->startup.single) ||
	    (!bringup && !sp->teardown.single))
		return 0;
	/*
	 * The non AP bound callbacks can fail on bringup. On teardown
	 * e.g. module removal we crash for now.
	 */
#ifdef CONFIG_SMP
	if (cpuhp_is_ap_state(state))
		ret = cpuhp_invoke_ap_callback(cpu, state, bringup, node);
	else
		ret = cpuhp_invoke_callback(cpu, state, bringup, node, NULL);
#else
	ret = cpuhp_invoke_callback(cpu, state, bringup, node, NULL);
#endif
	BUG_ON(ret && !bringup);
	return ret;
}

/*
 * Called from __cpuhp_setup_state on a recoverable failure.
 *
 * Note: The teardown callbacks for rollback are not allowed to fail!
 */
static void cpuhp_rollback_install(int failedcpu, enum cpuhp_state state,
				   struct hlist_node *node)
{
	int cpu;

	/* Roll back the already executed steps on the other cpus */
	for_each_present_cpu(cpu) {
		struct cpuhp_cpu_state *st = per_cpu_ptr(&cpuhp_state, cpu);
		int cpustate = st->state;

		if (cpu >= failedcpu)
			break;

		/* Did we invoke the startup call on that cpu ? */
		if (cpustate >= state)
			cpuhp_issue_call(cpu, state, false, node);
	}
}

int __cpuhp_state_add_instance_cpuslocked(enum cpuhp_state state,
					  struct hlist_node *node,
					  bool invoke)
{
	struct cpuhp_step *sp;
	int cpu;
	int ret;

	lockdep_assert_cpus_held();

	sp = cpuhp_get_step(state);
	if (sp->multi_instance == false)
		return -EINVAL;

	mutex_lock(&cpuhp_state_mutex);

	if (!invoke || !sp->startup.multi)
		goto add_node;

	/*
	 * Try to call the startup callback for each present cpu
	 * depending on the hotplug state of the cpu.
	 */
	for_each_present_cpu(cpu) {
		struct cpuhp_cpu_state *st = per_cpu_ptr(&cpuhp_state, cpu);
		int cpustate = st->state;

		if (cpustate < state)
			continue;

		ret = cpuhp_issue_call(cpu, state, true, node);
		if (ret) {
			if (sp->teardown.multi)
				cpuhp_rollback_install(cpu, state, node);
			goto unlock;
		}
	}
add_node:
	ret = 0;
	hlist_add_head(node, &sp->list);
unlock:
	mutex_unlock(&cpuhp_state_mutex);
	return ret;
}

int __cpuhp_state_add_instance(enum cpuhp_state state, struct hlist_node *node,
			       bool invoke)
{
	int ret;

	cpus_read_lock();
	ret = __cpuhp_state_add_instance_cpuslocked(state, node, invoke);
	cpus_read_unlock();
	return ret;
}
EXPORT_SYMBOL_GPL(__cpuhp_state_add_instance);

/**
 * __cpuhp_setup_state_cpuslocked - Setup the callbacks for an hotplug machine state
 * @state:		The state to setup
 * @invoke:		If true, the startup function is invoked for cpus where
 *			cpu state >= @state
 * @startup:		startup callback function
 * @teardown:		teardown callback function
 * @multi_instance:	State is set up for multiple instances which get
 *			added afterwards.
 *
 * The caller needs to hold cpus read locked while calling this function.
 * Returns:
 *   On success:
 *      Positive state number if @state is CPUHP_AP_ONLINE_DYN
 *      0 for all other states
 *   On failure: proper (negative) error code
 */
int __cpuhp_setup_state_cpuslocked(enum cpuhp_state state,
				   const char *name, bool invoke,
				   int (*startup)(unsigned int cpu),
				   int (*teardown)(unsigned int cpu),
				   bool multi_instance)
{
	int cpu, ret = 0;
	bool dynstate;

	lockdep_assert_cpus_held();

	if (cpuhp_cb_check(state) || !name)
		return -EINVAL;

	mutex_lock(&cpuhp_state_mutex);

	ret = cpuhp_store_callbacks(state, name, startup, teardown,
				    multi_instance);

	dynstate = state == CPUHP_AP_ONLINE_DYN;
	if (ret > 0 && dynstate) {
		state = ret;
		ret = 0;
	}

	if (ret || !invoke || !startup)
		goto out;

	/*
	 * Try to call the startup callback for each present cpu
	 * depending on the hotplug state of the cpu.
	 */
	for_each_present_cpu(cpu) {
		struct cpuhp_cpu_state *st = per_cpu_ptr(&cpuhp_state, cpu);
		int cpustate = st->state;

		if (cpustate < state)
			continue;

		ret = cpuhp_issue_call(cpu, state, true, NULL);
		if (ret) {
			if (teardown)
				cpuhp_rollback_install(cpu, state, NULL);
			cpuhp_store_callbacks(state, NULL, NULL, NULL, false);
			goto out;
		}
	}
out:
	mutex_unlock(&cpuhp_state_mutex);
	/*
	 * If the requested state is CPUHP_AP_ONLINE_DYN, return the
	 * dynamically allocated state in case of success.
	 */
	if (!ret && dynstate)
		return state;
	return ret;
}
EXPORT_SYMBOL(__cpuhp_setup_state_cpuslocked);

int __cpuhp_setup_state(enum cpuhp_state state,
			const char *name, bool invoke,
			int (*startup)(unsigned int cpu),
			int (*teardown)(unsigned int cpu),
			bool multi_instance)
{
	int ret;

	cpus_read_lock();
	ret = __cpuhp_setup_state_cpuslocked(state, name, invoke, startup,
					     teardown, multi_instance);
	cpus_read_unlock();
	return ret;
}
EXPORT_SYMBOL(__cpuhp_setup_state);

int __cpuhp_state_remove_instance(enum cpuhp_state state,
				  struct hlist_node *node, bool invoke)
{
	struct cpuhp_step *sp = cpuhp_get_step(state);
	int cpu;

	BUG_ON(cpuhp_cb_check(state));

	if (!sp->multi_instance)
		return -EINVAL;

	cpus_read_lock();
	mutex_lock(&cpuhp_state_mutex);

	if (!invoke || !cpuhp_get_teardown_cb(state))
		goto remove;
	/*
	 * Call the teardown callback for each present cpu depending
	 * on the hotplug state of the cpu. This function is not
	 * allowed to fail currently!
	 */
	for_each_present_cpu(cpu) {
		struct cpuhp_cpu_state *st = per_cpu_ptr(&cpuhp_state, cpu);
		int cpustate = st->state;

		if (cpustate >= state)
			cpuhp_issue_call(cpu, state, false, node);
	}

remove:
	hlist_del(node);
	mutex_unlock(&cpuhp_state_mutex);
	cpus_read_unlock();

	return 0;
}
EXPORT_SYMBOL_GPL(__cpuhp_state_remove_instance);

/**
 * __cpuhp_remove_state_cpuslocked - Remove the callbacks for an hotplug machine state
 * @state:	The state to remove
 * @invoke:	If true, the teardown function is invoked for cpus where
 *		cpu state >= @state
 *
 * The caller needs to hold cpus read locked while calling this function.
 * The teardown callback is currently not allowed to fail. Think
 * about module removal!
 */
void __cpuhp_remove_state_cpuslocked(enum cpuhp_state state, bool invoke)
{
	struct cpuhp_step *sp = cpuhp_get_step(state);
	int cpu;

	BUG_ON(cpuhp_cb_check(state));

	lockdep_assert_cpus_held();

	mutex_lock(&cpuhp_state_mutex);
	if (sp->multi_instance) {
		WARN(!hlist_empty(&sp->list),
		     "Error: Removing state %d which has instances left.\n",
		     state);
		goto remove;
	}

	if (!invoke || !cpuhp_get_teardown_cb(state))
		goto remove;

	/*
	 * Call the teardown callback for each present cpu depending
	 * on the hotplug state of the cpu. This function is not
	 * allowed to fail currently!
	 */
	for_each_present_cpu(cpu) {
		struct cpuhp_cpu_state *st = per_cpu_ptr(&cpuhp_state, cpu);
		int cpustate = st->state;

		if (cpustate >= state)
			cpuhp_issue_call(cpu, state, false, NULL);
	}
remove:
	cpuhp_store_callbacks(state, NULL, NULL, NULL, false);
	mutex_unlock(&cpuhp_state_mutex);
}
EXPORT_SYMBOL(__cpuhp_remove_state_cpuslocked);

void __cpuhp_remove_state(enum cpuhp_state state, bool invoke)
{
	cpus_read_lock();
	__cpuhp_remove_state_cpuslocked(state, invoke);
	cpus_read_unlock();
}
EXPORT_SYMBOL(__cpuhp_remove_state);

#ifdef CONFIG_HOTPLUG_SMT
static void cpuhp_offline_cpu_device(unsigned int cpu)
{
	struct device *dev = get_cpu_device(cpu);

	dev->offline = true;
	/* Tell user space about the state change */
	kobject_uevent(&dev->kobj, KOBJ_OFFLINE);
}

static void cpuhp_online_cpu_device(unsigned int cpu)
{
	struct device *dev = get_cpu_device(cpu);

	dev->offline = false;
	/* Tell user space about the state change */
	kobject_uevent(&dev->kobj, KOBJ_ONLINE);
}

int cpuhp_smt_disable(enum cpuhp_smt_control ctrlval)
{
	int cpu, ret = 0;

	cpu_maps_update_begin();
	for_each_online_cpu(cpu) {
		if (topology_is_primary_thread(cpu))
			continue;
		ret = cpu_down_maps_locked(cpu, CPUHP_OFFLINE);
		if (ret)
			break;
		/*
		 * As this needs to hold the cpu maps lock it's impossible
		 * to call device_offline() because that ends up calling
		 * cpu_down() which takes cpu maps lock. cpu maps lock
		 * needs to be held as this might race against in kernel
		 * abusers of the hotplug machinery (thermal management).
		 *
		 * So nothing would update device:offline state. That would
		 * leave the sysfs entry stale and prevent onlining after
		 * smt control has been changed to 'off' again. This is
		 * called under the sysfs hotplug lock, so it is properly
		 * serialized against the regular offline usage.
		 */
		cpuhp_offline_cpu_device(cpu);
	}
	if (!ret)
		cpu_smt_control = ctrlval;
	cpu_maps_update_done();
	return ret;
}

int cpuhp_smt_enable(void)
{
	int cpu, ret = 0;

	cpu_maps_update_begin();
	cpu_smt_control = CPU_SMT_ENABLED;
	for_each_present_cpu(cpu) {
		/* Skip online CPUs and CPUs on offline nodes */
		if (cpu_online(cpu) || !node_online(cpu_to_node(cpu)))
			continue;
		ret = _cpu_up(cpu, 0, CPUHP_ONLINE);
		if (ret)
			break;
		/* See comment in cpuhp_smt_disable() */
		cpuhp_online_cpu_device(cpu);
	}
	cpu_maps_update_done();
	return ret;
}
#endif

#if defined(CONFIG_SYSFS) && defined(CONFIG_HOTPLUG_CPU)
static ssize_t show_cpuhp_state(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct cpuhp_cpu_state *st = per_cpu_ptr(&cpuhp_state, dev->id);

	return sprintf(buf, "%d\n", st->state);
}
static DEVICE_ATTR(state, 0444, show_cpuhp_state, NULL);

static ssize_t write_cpuhp_target(struct device *dev,
				  struct device_attribute *attr,
				  const char *buf, size_t count)
{
	struct cpuhp_cpu_state *st = per_cpu_ptr(&cpuhp_state, dev->id);
	struct cpuhp_step *sp;
	int target, ret;

	ret = kstrtoint(buf, 10, &target);
	if (ret)
		return ret;

#ifdef CONFIG_CPU_HOTPLUG_STATE_CONTROL
	if (target < CPUHP_OFFLINE || target > CPUHP_ONLINE)
		return -EINVAL;
#else
	if (target != CPUHP_OFFLINE && target != CPUHP_ONLINE)
		return -EINVAL;
#endif

	ret = lock_device_hotplug_sysfs();
	if (ret)
		return ret;

	mutex_lock(&cpuhp_state_mutex);
	sp = cpuhp_get_step(target);
	ret = !sp->name || sp->cant_stop ? -EINVAL : 0;
	mutex_unlock(&cpuhp_state_mutex);
	if (ret)
		goto out;

	if (st->state < target)
		ret = cpu_up(dev->id, target);
	else
		ret = cpu_down(dev->id, target);
out:
	unlock_device_hotplug();
	return ret ? ret : count;
}

static ssize_t show_cpuhp_target(struct device *dev,
				 struct device_attribute *attr, char *buf)
{
	struct cpuhp_cpu_state *st = per_cpu_ptr(&cpuhp_state, dev->id);

	return sprintf(buf, "%d\n", st->target);
}
static DEVICE_ATTR(target, 0644, show_cpuhp_target, write_cpuhp_target);


static ssize_t write_cpuhp_fail(struct device *dev,
				struct device_attribute *attr,
				const char *buf, size_t count)
{
	struct cpuhp_cpu_state *st = per_cpu_ptr(&cpuhp_state, dev->id);
	struct cpuhp_step *sp;
	int fail, ret;

	ret = kstrtoint(buf, 10, &fail);
	if (ret)
		return ret;

	if (fail < CPUHP_OFFLINE || fail > CPUHP_ONLINE)
		return -EINVAL;

	/*
	 * Cannot fail STARTING/DYING callbacks.
	 */
	if (cpuhp_is_atomic_state(fail))
		return -EINVAL;

	/*
	 * Cannot fail anything that doesn't have callbacks.
	 */
	mutex_lock(&cpuhp_state_mutex);
	sp = cpuhp_get_step(fail);
	if (!sp->startup.single && !sp->teardown.single)
		ret = -EINVAL;
	mutex_unlock(&cpuhp_state_mutex);
	if (ret)
		return ret;

	st->fail = fail;

	return count;
}

static ssize_t show_cpuhp_fail(struct device *dev,
			       struct device_attribute *attr, char *buf)
{
	struct cpuhp_cpu_state *st = per_cpu_ptr(&cpuhp_state, dev->id);

	return sprintf(buf, "%d\n", st->fail);
}

static DEVICE_ATTR(fail, 0644, show_cpuhp_fail, write_cpuhp_fail);

static struct attribute *cpuhp_cpu_attrs[] = {
	&dev_attr_state.attr,
	&dev_attr_target.attr,
	&dev_attr_fail.attr,
	NULL
};

static const struct attribute_group cpuhp_cpu_attr_group = {
	.attrs = cpuhp_cpu_attrs,
	.name = "hotplug",
	NULL
};

static ssize_t show_cpuhp_states(struct device *dev,
				 struct device_attribute *attr, char *buf)
{
	ssize_t cur, res = 0;
	int i;

	mutex_lock(&cpuhp_state_mutex);
	for (i = CPUHP_OFFLINE; i <= CPUHP_ONLINE; i++) {
		struct cpuhp_step *sp = cpuhp_get_step(i);

		if (sp->name) {
			cur = sprintf(buf, "%3d: %s\n", i, sp->name);
			buf += cur;
			res += cur;
		}
	}
	mutex_unlock(&cpuhp_state_mutex);
	return res;
}
static DEVICE_ATTR(states, 0444, show_cpuhp_states, NULL);

static struct attribute *cpuhp_cpu_root_attrs[] = {
	&dev_attr_states.attr,
	NULL
};

static const struct attribute_group cpuhp_cpu_root_attr_group = {
	.attrs = cpuhp_cpu_root_attrs,
	.name = "hotplug",
	NULL
};

#ifdef CONFIG_HOTPLUG_SMT

static ssize_t
__store_smt_control(struct device *dev, struct device_attribute *attr,
		    const char *buf, size_t count)
{
	int ctrlval, ret;

	if (sysfs_streq(buf, "on"))
		ctrlval = CPU_SMT_ENABLED;
	else if (sysfs_streq(buf, "off"))
		ctrlval = CPU_SMT_DISABLED;
	else if (sysfs_streq(buf, "forceoff"))
		ctrlval = CPU_SMT_FORCE_DISABLED;
	else
		return -EINVAL;

	if (cpu_smt_control == CPU_SMT_FORCE_DISABLED)
		return -EPERM;

	if (cpu_smt_control == CPU_SMT_NOT_SUPPORTED)
		return -ENODEV;

	ret = lock_device_hotplug_sysfs();
	if (ret)
		return ret;

	if (ctrlval != cpu_smt_control) {
		switch (ctrlval) {
		case CPU_SMT_ENABLED:
			ret = cpuhp_smt_enable();
			break;
		case CPU_SMT_DISABLED:
		case CPU_SMT_FORCE_DISABLED:
			ret = cpuhp_smt_disable(ctrlval);
			break;
		}
	}

	unlock_device_hotplug();
	return ret ? ret : count;
}

#else /* !CONFIG_HOTPLUG_SMT */
static ssize_t
__store_smt_control(struct device *dev, struct device_attribute *attr,
		    const char *buf, size_t count)
{
	return -ENODEV;
}
#endif /* CONFIG_HOTPLUG_SMT */

static const char *smt_states[] = {
	[CPU_SMT_ENABLED]		= "on",
	[CPU_SMT_DISABLED]		= "off",
	[CPU_SMT_FORCE_DISABLED]	= "forceoff",
	[CPU_SMT_NOT_SUPPORTED]		= "notsupported",
	[CPU_SMT_NOT_IMPLEMENTED]	= "notimplemented",
};

static ssize_t
show_smt_control(struct device *dev, struct device_attribute *attr, char *buf)
{
	const char *state = smt_states[cpu_smt_control];

	return snprintf(buf, PAGE_SIZE - 2, "%s\n", state);
}

static ssize_t
store_smt_control(struct device *dev, struct device_attribute *attr,
		  const char *buf, size_t count)
{
	return __store_smt_control(dev, attr, buf, count);
}
static DEVICE_ATTR(control, 0644, show_smt_control, store_smt_control);

static ssize_t
show_smt_active(struct device *dev, struct device_attribute *attr, char *buf)
{
	return snprintf(buf, PAGE_SIZE - 2, "%d\n", sched_smt_active());
}
static DEVICE_ATTR(active, 0444, show_smt_active, NULL);

static struct attribute *cpuhp_smt_attrs[] = {
	&dev_attr_control.attr,
	&dev_attr_active.attr,
	NULL
};

static const struct attribute_group cpuhp_smt_attr_group = {
	.attrs = cpuhp_smt_attrs,
	.name = "smt",
	NULL
};

static int __init cpu_smt_sysfs_init(void)
{
	return sysfs_create_group(&cpu_subsys.dev_root->kobj,
				  &cpuhp_smt_attr_group);
}

static int __init cpuhp_sysfs_init(void)
{
	int cpu, ret;

	ret = cpu_smt_sysfs_init();
	if (ret)
		return ret;

	ret = sysfs_create_group(&cpu_subsys.dev_root->kobj,
				 &cpuhp_cpu_root_attr_group);
	if (ret)
		return ret;

	for_each_possible_cpu(cpu) {
		struct device *dev = get_cpu_device(cpu);

		if (!dev)
			continue;
		ret = sysfs_create_group(&dev->kobj, &cpuhp_cpu_attr_group);
		if (ret)
			return ret;
	}
	return 0;
}
device_initcall(cpuhp_sysfs_init);
#endif /* CONFIG_SYSFS && CONFIG_HOTPLUG_CPU */

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
struct cpumask __cpu_possible_mask __read_mostly
	= {CPU_BITS_ALL};
#else
struct cpumask __cpu_possible_mask __read_mostly;
#endif
EXPORT_SYMBOL(__cpu_possible_mask);

struct cpumask __cpu_online_mask __read_mostly;
EXPORT_SYMBOL(__cpu_online_mask);

struct cpumask __cpu_present_mask __read_mostly;
EXPORT_SYMBOL(__cpu_present_mask);

struct cpumask __cpu_active_mask __read_mostly;
EXPORT_SYMBOL(__cpu_active_mask);

atomic_t __num_online_cpus __read_mostly;
EXPORT_SYMBOL(__num_online_cpus);

void init_cpu_present(const struct cpumask *src)
{
	cpumask_copy(&__cpu_present_mask, src);
}

void init_cpu_possible(const struct cpumask *src)
{
	cpumask_copy(&__cpu_possible_mask, src);
}

void init_cpu_online(const struct cpumask *src)
{
	cpumask_copy(&__cpu_online_mask, src);
}

void set_cpu_online(unsigned int cpu, bool online)
{
	/*
	 * atomic_inc/dec() is required to handle the horrid abuse of this
	 * function by the reboot and kexec code which invoke it from
	 * IPI/NMI broadcasts when shutting down CPUs. Invocation from
	 * regular CPU hotplug is properly serialized.
	 *
	 * Note, that the fact that __num_online_cpus is of type atomic_t
	 * does not protect readers which are not serialized against
	 * concurrent hotplug operations.
	 */
	if (online) {
		if (!cpumask_test_and_set_cpu(cpu, &__cpu_online_mask))
			atomic_inc(&__num_online_cpus);
	} else {
		if (cpumask_test_and_clear_cpu(cpu, &__cpu_online_mask))
			atomic_dec(&__num_online_cpus);
	}
}

/*
 * Activate the first processor.
 */
void __init boot_cpu_init(void)
{
	int cpu = smp_processor_id();

	/* Mark the boot cpu "present", "online" etc for SMP and UP case */
	set_cpu_online(cpu, true);
	set_cpu_active(cpu, true);
	set_cpu_present(cpu, true);
	set_cpu_possible(cpu, true);

#ifdef CONFIG_SMP
	__boot_cpu_id = cpu;
#endif
}

/*
 * Must be called _AFTER_ setting up the per_cpu areas
 */
void __init boot_cpu_hotplug_init(void)
{
#ifdef CONFIG_SMP
	cpumask_set_cpu(smp_processor_id(), &cpus_booted_once_mask);
#endif
	this_cpu_write(cpuhp_state.state, CPUHP_ONLINE);
}

/*
 * These are used for a global "mitigations=" cmdline option for toggling
 * optional CPU mitigations.
 */
enum cpu_mitigations {
	CPU_MITIGATIONS_OFF,
	CPU_MITIGATIONS_AUTO,
	CPU_MITIGATIONS_AUTO_NOSMT,
};

static enum cpu_mitigations cpu_mitigations __ro_after_init =
	CPU_MITIGATIONS_AUTO;

static int __init mitigations_parse_cmdline(char *arg)
{
	if (!strcmp(arg, "off"))
		cpu_mitigations = CPU_MITIGATIONS_OFF;
	else if (!strcmp(arg, "auto"))
		cpu_mitigations = CPU_MITIGATIONS_AUTO;
	else if (!strcmp(arg, "auto,nosmt"))
		cpu_mitigations = CPU_MITIGATIONS_AUTO_NOSMT;
	else
		pr_crit("Unsupported mitigations=%s, system may still be vulnerable\n",
			arg);

	return 0;
}
early_param("mitigations", mitigations_parse_cmdline);

/* mitigations=off */
bool cpu_mitigations_off(void)
{
	return cpu_mitigations == CPU_MITIGATIONS_OFF;
}
EXPORT_SYMBOL_GPL(cpu_mitigations_off);

/* mitigations=auto,nosmt */
bool cpu_mitigations_auto_nosmt(void)
{
	return cpu_mitigations == CPU_MITIGATIONS_AUTO_NOSMT;
}
EXPORT_SYMBOL_GPL(cpu_mitigations_auto_nosmt);
