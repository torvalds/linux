/*
 * Generic entry point for the idle threads
 */
#include <linux/sched.h>
#include <linux/cpu.h>
#include <linux/cpuidle.h>
#include <linux/tick.h>
#include <linux/mm.h>
#include <linux/stackprotector.h>
#include <linux/suspend.h>

#include <asm/tlb.h>

#include <trace/events/power.h>

#include "sched.h"

/**
 * sched_idle_set_state - Record idle state for the current CPU.
 * @idle_state: State to record.
 */
void sched_idle_set_state(struct cpuidle_state *idle_state, int index)
{
	idle_set_state(this_rq(), idle_state);
	idle_set_state_idx(this_rq(), index);
}

static int __read_mostly cpu_idle_force_poll;

void cpu_idle_poll_ctrl(bool enable)
{
	if (enable) {
		cpu_idle_force_poll++;
	} else {
		cpu_idle_force_poll--;
		WARN_ON_ONCE(cpu_idle_force_poll < 0);
	}
}

#ifdef CONFIG_GENERIC_IDLE_POLL_SETUP
static int __init cpu_idle_poll_setup(char *__unused)
{
	cpu_idle_force_poll = 1;
	return 1;
}
__setup("nohlt", cpu_idle_poll_setup);

static int __init cpu_idle_nopoll_setup(char *__unused)
{
	cpu_idle_force_poll = 0;
	return 1;
}
__setup("hlt", cpu_idle_nopoll_setup);
#endif

static inline int cpu_idle_poll(void)
{
	rcu_idle_enter();
	trace_cpu_idle_rcuidle(0, smp_processor_id());
	local_irq_enable();
	stop_critical_timings();
	while (!tif_need_resched() &&
		(cpu_idle_force_poll || tick_check_broadcast_expired()))
		cpu_relax();
	start_critical_timings();
	trace_cpu_idle_rcuidle(PWR_EVENT_EXIT, smp_processor_id());
	rcu_idle_exit();
	return 1;
}

/* Weak implementations for optional arch specific functions */
void __weak arch_cpu_idle_prepare(void) { }
void __weak arch_cpu_idle_enter(void) { }
void __weak arch_cpu_idle_exit(void) { }
void __weak arch_cpu_idle_dead(void) { }
void __weak arch_cpu_idle(void)
{
	cpu_idle_force_poll = 1;
	local_irq_enable();
}

/**
 * default_idle_call - Default CPU idle routine.
 *
 * To use when the cpuidle framework cannot be used.
 */
void default_idle_call(void)
{
	if (current_clr_polling_and_test()) {
		local_irq_enable();
	} else {
		stop_critical_timings();
		arch_cpu_idle();
		start_critical_timings();
	}
}

static int call_cpuidle(struct cpuidle_driver *drv, struct cpuidle_device *dev,
		      int next_state)
{
	/* Fall back to the default arch idle method on errors. */
	if (next_state < 0) {
		default_idle_call();
		return next_state;
	}

	/*
	 * The idle task must be scheduled, it is pointless to go to idle, just
	 * update no idle residency and return.
	 */
	if (current_clr_polling_and_test()) {
		dev->last_residency = 0;
		local_irq_enable();
		return -EBUSY;
	}

	/*
	 * Enter the idle state previously returned by the governor decision.
	 * This function will block until an interrupt occurs and will take
	 * care of re-enabling the local interrupts
	 */
	return cpuidle_enter(drv, dev, next_state);
}

/**
 * cpuidle_idle_call - the main idle function
 *
 * NOTE: no locks or semaphores should be used here
 *
 * On archs that support TIF_POLLING_NRFLAG, is called with polling
 * set, and it returns with polling set.  If it ever stops polling, it
 * must clear the polling bit.
 */
static void cpuidle_idle_call(void)
{
	struct cpuidle_device *dev = __this_cpu_read(cpuidle_devices);
	struct cpuidle_driver *drv = cpuidle_get_cpu_driver(dev);
	int next_state, entered_state;

	/*
	 * Check if the idle task must be rescheduled. If it is the
	 * case, exit the function after re-enabling the local irq.
	 */
	if (need_resched()) {
		local_irq_enable();
		return;
	}

	/*
	 * Tell the RCU framework we are entering an idle section,
	 * so no more rcu read side critical sections and one more
	 * step to the grace period
	 */
	rcu_idle_enter();

	if (cpuidle_not_available(drv, dev)) {
		default_idle_call();
		goto exit_idle;
	}

	/*
	 * Suspend-to-idle ("freeze") is a system state in which all user space
	 * has been frozen, all I/O devices have been suspended and the only
	 * activity happens here and in iterrupts (if any).  In that case bypass
	 * the cpuidle governor and go stratight for the deepest idle state
	 * available.  Possibly also suspend the local tick and the entire
	 * timekeeping to prevent timer interrupts from kicking us out of idle
	 * until a proper wakeup interrupt happens.
	 */
	if (idle_should_freeze()) {
		entered_state = cpuidle_enter_freeze(drv, dev);
		if (entered_state >= 0) {
			local_irq_enable();
			goto exit_idle;
		}

		next_state = cpuidle_find_deepest_state(drv, dev);
		call_cpuidle(drv, dev, next_state);
	} else {
		/*
		 * Ask the cpuidle framework to choose a convenient idle state.
		 */
		next_state = cpuidle_select(drv, dev);
		entered_state = call_cpuidle(drv, dev, next_state);
		/*
		 * Give the governor an opportunity to reflect on the outcome
		 */
		cpuidle_reflect(dev, entered_state);
	}

exit_idle:
	__current_set_polling();

	/*
	 * It is up to the idle functions to reenable local interrupts
	 */
	if (WARN_ON_ONCE(irqs_disabled()))
		local_irq_enable();

	rcu_idle_exit();
}

DEFINE_PER_CPU(bool, cpu_dead_idle);

/*
 * Generic idle loop implementation
 *
 * Called with polling cleared.
 */
static void cpu_idle_loop(void)
{
	while (1) {
		/*
		 * If the arch has a polling bit, we maintain an invariant:
		 *
		 * Our polling bit is clear if we're not scheduled (i.e. if
		 * rq->curr != rq->idle).  This means that, if rq->idle has
		 * the polling bit set, then setting need_resched is
		 * guaranteed to cause the cpu to reschedule.
		 */

		__current_set_polling();
		quiet_vmstat();
		tick_nohz_idle_enter();

		while (!need_resched()) {
			check_pgt_cache();
			rmb();

			if (cpu_is_offline(smp_processor_id())) {
				rcu_cpu_notify(NULL, CPU_DYING_IDLE,
					       (void *)(long)smp_processor_id());
				smp_mb(); /* all activity before dead. */
				this_cpu_write(cpu_dead_idle, true);
				arch_cpu_idle_dead();
			}

			local_irq_disable();
			arch_cpu_idle_enter();

			/*
			 * In poll mode we reenable interrupts and spin.
			 *
			 * Also if we detected in the wakeup from idle
			 * path that the tick broadcast device expired
			 * for us, we don't want to go deep idle as we
			 * know that the IPI is going to arrive right
			 * away
			 */
			if (cpu_idle_force_poll || tick_check_broadcast_expired())
				cpu_idle_poll();
			else
				cpuidle_idle_call();

			arch_cpu_idle_exit();
		}

		/*
		 * Since we fell out of the loop above, we know
		 * TIF_NEED_RESCHED must be set, propagate it into
		 * PREEMPT_NEED_RESCHED.
		 *
		 * This is required because for polling idle loops we will
		 * not have had an IPI to fold the state for us.
		 */
		preempt_set_need_resched();
		tick_nohz_idle_exit();
		__current_clr_polling();

		/*
		 * We promise to call sched_ttwu_pending and reschedule
		 * if need_resched is set while polling is set.  That
		 * means that clearing polling needs to be visible
		 * before doing these things.
		 */
		smp_mb__after_atomic();

		sched_ttwu_pending();
		schedule_idle();
	}
}

void cpu_startup_entry(enum cpuhp_state state)
{
	/*
	 * This #ifdef needs to die, but it's too late in the cycle to
	 * make this generic (arm and sh have never invoked the canary
	 * init for the non boot cpus!). Will be fixed in 3.11
	 */
#ifdef CONFIG_X86
	/*
	 * If we're the non-boot CPU, nothing set the stack canary up
	 * for us. The boot CPU already has it initialized but no harm
	 * in doing it again. This is a good place for updating it, as
	 * we wont ever return from this function (so the invalid
	 * canaries already on the stack wont ever trigger).
	 */
	boot_init_stack_canary();
#endif
	arch_cpu_idle_prepare();
	cpu_idle_loop();
}
