// SPDX-License-Identifier: GPL-2.0-only
/*
 * Generic helpers for smp ipi calls
 *
 * (C) Jens Axboe <jens.axboe@oracle.com> 2008
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/irq_work.h>
#include <linux/rcupdate.h>
#include <linux/rculist.h>
#include <linux/kernel.h>
#include <linux/export.h>
#include <linux/percpu.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/gfp.h>
#include <linux/smp.h>
#include <linux/cpu.h>
#include <linux/sched.h>
#include <linux/sched/idle.h>
#include <linux/hypervisor.h>
#include <linux/sched/clock.h>
#include <linux/nmi.h>
#include <linux/sched/debug.h>
#include <linux/jump_label.h>
#include <linux/string_choices.h>

#include <trace/events/ipi.h>
#define CREATE_TRACE_POINTS
#include <trace/events/csd.h>
#undef CREATE_TRACE_POINTS

#include "smpboot.h"
#include "sched/smp.h"

#define CSD_TYPE(_csd)	((_csd)->node.u_flags & CSD_FLAG_TYPE_MASK)

struct call_function_data {
	call_single_data_t	__percpu *csd;
	cpumask_var_t		cpumask;
	cpumask_var_t		cpumask_ipi;
};

static DEFINE_PER_CPU_ALIGNED(struct call_function_data, cfd_data);

static DEFINE_PER_CPU_SHARED_ALIGNED(struct llist_head, call_single_queue);

static DEFINE_PER_CPU(atomic_t, trigger_backtrace) = ATOMIC_INIT(1);

static void __flush_smp_call_function_queue(bool warn_cpu_offline);

int smpcfd_prepare_cpu(unsigned int cpu)
{
	struct call_function_data *cfd = &per_cpu(cfd_data, cpu);

	if (!zalloc_cpumask_var_node(&cfd->cpumask, GFP_KERNEL,
				     cpu_to_node(cpu)))
		return -ENOMEM;
	if (!zalloc_cpumask_var_node(&cfd->cpumask_ipi, GFP_KERNEL,
				     cpu_to_node(cpu))) {
		free_cpumask_var(cfd->cpumask);
		return -ENOMEM;
	}
	cfd->csd = alloc_percpu(call_single_data_t);
	if (!cfd->csd) {
		free_cpumask_var(cfd->cpumask);
		free_cpumask_var(cfd->cpumask_ipi);
		return -ENOMEM;
	}

	return 0;
}

int smpcfd_dead_cpu(unsigned int cpu)
{
	struct call_function_data *cfd = &per_cpu(cfd_data, cpu);

	free_cpumask_var(cfd->cpumask);
	free_cpumask_var(cfd->cpumask_ipi);
	free_percpu(cfd->csd);
	return 0;
}

int smpcfd_dying_cpu(unsigned int cpu)
{
	/*
	 * The IPIs for the smp-call-function callbacks queued by other
	 * CPUs might arrive late, either due to hardware latencies or
	 * because this CPU disabled interrupts (inside stop-machine)
	 * before the IPIs were sent. So flush out any pending callbacks
	 * explicitly (without waiting for the IPIs to arrive), to
	 * ensure that the outgoing CPU doesn't go offline with work
	 * still pending.
	 */
	__flush_smp_call_function_queue(false);
	irq_work_run();
	return 0;
}

void __init call_function_init(void)
{
	int i;

	for_each_possible_cpu(i)
		init_llist_head(&per_cpu(call_single_queue, i));

	smpcfd_prepare_cpu(smp_processor_id());
}

static __always_inline void
send_call_function_single_ipi(int cpu)
{
	if (call_function_single_prep_ipi(cpu)) {
		trace_ipi_send_cpu(cpu, _RET_IP_,
				   generic_smp_call_function_single_interrupt);
		arch_send_call_function_single_ipi(cpu);
	}
}

static __always_inline void
send_call_function_ipi_mask(struct cpumask *mask)
{
	trace_ipi_send_cpumask(mask, _RET_IP_,
			       generic_smp_call_function_single_interrupt);
	arch_send_call_function_ipi_mask(mask);
}

static __always_inline void
csd_do_func(smp_call_func_t func, void *info, call_single_data_t *csd)
{
	trace_csd_function_entry(func, csd);
	func(info);
	trace_csd_function_exit(func, csd);
}

#ifdef CONFIG_CSD_LOCK_WAIT_DEBUG

static DEFINE_STATIC_KEY_MAYBE(CONFIG_CSD_LOCK_WAIT_DEBUG_DEFAULT, csdlock_debug_enabled);

/*
 * Parse the csdlock_debug= kernel boot parameter.
 *
 * If you need to restore the old "ext" value that once provided
 * additional debugging information, reapply the following commits:
 *
 * de7b09ef658d ("locking/csd_lock: Prepare more CSD lock debugging")
 * a5aabace5fb8 ("locking/csd_lock: Add more data to CSD lock debugging")
 */
static int __init csdlock_debug(char *str)
{
	int ret;
	unsigned int val = 0;

	ret = get_option(&str, &val);
	if (ret) {
		if (val)
			static_branch_enable(&csdlock_debug_enabled);
		else
			static_branch_disable(&csdlock_debug_enabled);
	}

	return 1;
}
__setup("csdlock_debug=", csdlock_debug);

static DEFINE_PER_CPU(call_single_data_t *, cur_csd);
static DEFINE_PER_CPU(smp_call_func_t, cur_csd_func);
static DEFINE_PER_CPU(void *, cur_csd_info);

static ulong csd_lock_timeout = 5000;  /* CSD lock timeout in milliseconds. */
module_param(csd_lock_timeout, ulong, 0644);
static int panic_on_ipistall;  /* CSD panic timeout in milliseconds, 300000 for five minutes. */
module_param(panic_on_ipistall, int, 0644);

static atomic_t csd_bug_count = ATOMIC_INIT(0);

/* Record current CSD work for current CPU, NULL to erase. */
static void __csd_lock_record(call_single_data_t *csd)
{
	if (!csd) {
		smp_mb(); /* NULL cur_csd after unlock. */
		__this_cpu_write(cur_csd, NULL);
		return;
	}
	__this_cpu_write(cur_csd_func, csd->func);
	__this_cpu_write(cur_csd_info, csd->info);
	smp_wmb(); /* func and info before csd. */
	__this_cpu_write(cur_csd, csd);
	smp_mb(); /* Update cur_csd before function call. */
		  /* Or before unlock, as the case may be. */
}

static __always_inline void csd_lock_record(call_single_data_t *csd)
{
	if (static_branch_unlikely(&csdlock_debug_enabled))
		__csd_lock_record(csd);
}

static int csd_lock_wait_getcpu(call_single_data_t *csd)
{
	unsigned int csd_type;

	csd_type = CSD_TYPE(csd);
	if (csd_type == CSD_TYPE_ASYNC || csd_type == CSD_TYPE_SYNC)
		return csd->node.dst; /* Other CSD_TYPE_ values might not have ->dst. */
	return -1;
}

static atomic_t n_csd_lock_stuck;

/**
 * csd_lock_is_stuck - Has a CSD-lock acquisition been stuck too long?
 *
 * Returns @true if a CSD-lock acquisition is stuck and has been stuck
 * long enough for a "non-responsive CSD lock" message to be printed.
 */
bool csd_lock_is_stuck(void)
{
	return !!atomic_read(&n_csd_lock_stuck);
}

/*
 * Complain if too much time spent waiting.  Note that only
 * the CSD_TYPE_SYNC/ASYNC types provide the destination CPU,
 * so waiting on other types gets much less information.
 */
static bool csd_lock_wait_toolong(call_single_data_t *csd, u64 ts0, u64 *ts1, int *bug_id, unsigned long *nmessages)
{
	int cpu = -1;
	int cpux;
	bool firsttime;
	u64 ts2, ts_delta;
	call_single_data_t *cpu_cur_csd;
	unsigned int flags = READ_ONCE(csd->node.u_flags);
	unsigned long long csd_lock_timeout_ns = csd_lock_timeout * NSEC_PER_MSEC;

	if (!(flags & CSD_FLAG_LOCK)) {
		if (!unlikely(*bug_id))
			return true;
		cpu = csd_lock_wait_getcpu(csd);
		pr_alert("csd: CSD lock (#%d) got unstuck on CPU#%02d, CPU#%02d released the lock.\n",
			 *bug_id, raw_smp_processor_id(), cpu);
		atomic_dec(&n_csd_lock_stuck);
		return true;
	}

	ts2 = ktime_get_mono_fast_ns();
	/* How long since we last checked for a stuck CSD lock.*/
	ts_delta = ts2 - *ts1;
	if (likely(ts_delta <= csd_lock_timeout_ns * (*nmessages + 1) *
			       (!*nmessages ? 1 : (ilog2(num_online_cpus()) / 2 + 1)) ||
		   csd_lock_timeout_ns == 0))
		return false;

	if (ts0 > ts2) {
		/* Our own sched_clock went backward; don't blame another CPU. */
		ts_delta = ts0 - ts2;
		pr_alert("sched_clock on CPU %d went backward by %llu ns\n", raw_smp_processor_id(), ts_delta);
		*ts1 = ts2;
		return false;
	}

	firsttime = !*bug_id;
	if (firsttime)
		*bug_id = atomic_inc_return(&csd_bug_count);
	cpu = csd_lock_wait_getcpu(csd);
	if (WARN_ONCE(cpu < 0 || cpu >= nr_cpu_ids, "%s: cpu = %d\n", __func__, cpu))
		cpux = 0;
	else
		cpux = cpu;
	cpu_cur_csd = smp_load_acquire(&per_cpu(cur_csd, cpux)); /* Before func and info. */
	/* How long since this CSD lock was stuck. */
	ts_delta = ts2 - ts0;
	pr_alert("csd: %s non-responsive CSD lock (#%d) on CPU#%d, waiting %lld ns for CPU#%02d %pS(%ps).\n",
		 firsttime ? "Detected" : "Continued", *bug_id, raw_smp_processor_id(), (s64)ts_delta,
		 cpu, csd->func, csd->info);
	(*nmessages)++;
	if (firsttime)
		atomic_inc(&n_csd_lock_stuck);
	/*
	 * If the CSD lock is still stuck after 5 minutes, it is unlikely
	 * to become unstuck. Use a signed comparison to avoid triggering
	 * on underflows when the TSC is out of sync between sockets.
	 */
	BUG_ON(panic_on_ipistall > 0 && (s64)ts_delta > ((s64)panic_on_ipistall * NSEC_PER_MSEC));
	if (cpu_cur_csd && csd != cpu_cur_csd) {
		pr_alert("\tcsd: CSD lock (#%d) handling prior %pS(%ps) request.\n",
			 *bug_id, READ_ONCE(per_cpu(cur_csd_func, cpux)),
			 READ_ONCE(per_cpu(cur_csd_info, cpux)));
	} else {
		pr_alert("\tcsd: CSD lock (#%d) %s.\n",
			 *bug_id, !cpu_cur_csd ? "unresponsive" : "handling this request");
	}
	if (cpu >= 0) {
		if (atomic_cmpxchg_acquire(&per_cpu(trigger_backtrace, cpu), 1, 0))
			dump_cpu_task(cpu);
		if (!cpu_cur_csd) {
			pr_alert("csd: Re-sending CSD lock (#%d) IPI from CPU#%02d to CPU#%02d\n", *bug_id, raw_smp_processor_id(), cpu);
			arch_send_call_function_single_ipi(cpu);
		}
	}
	if (firsttime)
		dump_stack();
	*ts1 = ts2;

	return false;
}

/*
 * csd_lock/csd_unlock used to serialize access to per-cpu csd resources
 *
 * For non-synchronous ipi calls the csd can still be in use by the
 * previous function call. For multi-cpu calls its even more interesting
 * as we'll have to ensure no other cpu is observing our csd.
 */
static void __csd_lock_wait(call_single_data_t *csd)
{
	unsigned long nmessages = 0;
	int bug_id = 0;
	u64 ts0, ts1;

	ts1 = ts0 = ktime_get_mono_fast_ns();
	for (;;) {
		if (csd_lock_wait_toolong(csd, ts0, &ts1, &bug_id, &nmessages))
			break;
		cpu_relax();
	}
	smp_acquire__after_ctrl_dep();
}

static __always_inline void csd_lock_wait(call_single_data_t *csd)
{
	if (static_branch_unlikely(&csdlock_debug_enabled)) {
		__csd_lock_wait(csd);
		return;
	}

	smp_cond_load_acquire(&csd->node.u_flags, !(VAL & CSD_FLAG_LOCK));
}
#else
static void csd_lock_record(call_single_data_t *csd)
{
}

static __always_inline void csd_lock_wait(call_single_data_t *csd)
{
	smp_cond_load_acquire(&csd->node.u_flags, !(VAL & CSD_FLAG_LOCK));
}
#endif

static __always_inline void csd_lock(call_single_data_t *csd)
{
	csd_lock_wait(csd);
	csd->node.u_flags |= CSD_FLAG_LOCK;

	/*
	 * prevent CPU from reordering the above assignment
	 * to ->flags with any subsequent assignments to other
	 * fields of the specified call_single_data_t structure:
	 */
	smp_wmb();
}

static __always_inline void csd_unlock(call_single_data_t *csd)
{
	WARN_ON(!(csd->node.u_flags & CSD_FLAG_LOCK));

	/*
	 * ensure we're all done before releasing data:
	 */
	smp_store_release(&csd->node.u_flags, 0);
}

static DEFINE_PER_CPU_SHARED_ALIGNED(call_single_data_t, csd_data);

void __smp_call_single_queue(int cpu, struct llist_node *node)
{
	/*
	 * We have to check the type of the CSD before queueing it, because
	 * once queued it can have its flags cleared by
	 *   flush_smp_call_function_queue()
	 * even if we haven't sent the smp_call IPI yet (e.g. the stopper
	 * executes migration_cpu_stop() on the remote CPU).
	 */
	if (trace_csd_queue_cpu_enabled()) {
		call_single_data_t *csd;
		smp_call_func_t func;

		csd = container_of(node, call_single_data_t, node.llist);
		func = CSD_TYPE(csd) == CSD_TYPE_TTWU ?
			sched_ttwu_pending : csd->func;

		trace_csd_queue_cpu(cpu, _RET_IP_, func, csd);
	}

	/*
	 * The list addition should be visible to the target CPU when it pops
	 * the head of the list to pull the entry off it in the IPI handler
	 * because of normal cache coherency rules implied by the underlying
	 * llist ops.
	 *
	 * If IPIs can go out of order to the cache coherency protocol
	 * in an architecture, sufficient synchronisation should be added
	 * to arch code to make it appear to obey cache coherency WRT
	 * locking and barrier primitives. Generic code isn't really
	 * equipped to do the right thing...
	 */
	if (llist_add(node, &per_cpu(call_single_queue, cpu)))
		send_call_function_single_ipi(cpu);
}

/*
 * Insert a previously allocated call_single_data_t element
 * for execution on the given CPU. data must already have
 * ->func, ->info, and ->flags set.
 */
static int generic_exec_single(int cpu, call_single_data_t *csd)
{
	if (cpu == smp_processor_id()) {
		smp_call_func_t func = csd->func;
		void *info = csd->info;
		unsigned long flags;

		/*
		 * We can unlock early even for the synchronous on-stack case,
		 * since we're doing this from the same CPU..
		 */
		csd_lock_record(csd);
		csd_unlock(csd);
		local_irq_save(flags);
		csd_do_func(func, info, NULL);
		csd_lock_record(NULL);
		local_irq_restore(flags);
		return 0;
	}

	if ((unsigned)cpu >= nr_cpu_ids || !cpu_online(cpu)) {
		csd_unlock(csd);
		return -ENXIO;
	}

	__smp_call_single_queue(cpu, &csd->node.llist);

	return 0;
}

/**
 * generic_smp_call_function_single_interrupt - Execute SMP IPI callbacks
 *
 * Invoked by arch to handle an IPI for call function single.
 * Must be called with interrupts disabled.
 */
void generic_smp_call_function_single_interrupt(void)
{
	__flush_smp_call_function_queue(true);
}

/**
 * __flush_smp_call_function_queue - Flush pending smp-call-function callbacks
 *
 * @warn_cpu_offline: If set to 'true', warn if callbacks were queued on an
 *		      offline CPU. Skip this check if set to 'false'.
 *
 * Flush any pending smp-call-function callbacks queued on this CPU. This is
 * invoked by the generic IPI handler, as well as by a CPU about to go offline,
 * to ensure that all pending IPI callbacks are run before it goes completely
 * offline.
 *
 * Loop through the call_single_queue and run all the queued callbacks.
 * Must be called with interrupts disabled.
 */
static void __flush_smp_call_function_queue(bool warn_cpu_offline)
{
	call_single_data_t *csd, *csd_next;
	struct llist_node *entry, *prev;
	struct llist_head *head;
	static bool warned;
	atomic_t *tbt;

	lockdep_assert_irqs_disabled();

	/* Allow waiters to send backtrace NMI from here onwards */
	tbt = this_cpu_ptr(&trigger_backtrace);
	atomic_set_release(tbt, 1);

	head = this_cpu_ptr(&call_single_queue);
	entry = llist_del_all(head);
	entry = llist_reverse_order(entry);

	/* There shouldn't be any pending callbacks on an offline CPU. */
	if (unlikely(warn_cpu_offline && !cpu_online(smp_processor_id()) &&
		     !warned && entry != NULL)) {
		warned = true;
		WARN(1, "IPI on offline CPU %d\n", smp_processor_id());

		/*
		 * We don't have to use the _safe() variant here
		 * because we are not invoking the IPI handlers yet.
		 */
		llist_for_each_entry(csd, entry, node.llist) {
			switch (CSD_TYPE(csd)) {
			case CSD_TYPE_ASYNC:
			case CSD_TYPE_SYNC:
			case CSD_TYPE_IRQ_WORK:
				pr_warn("IPI callback %pS sent to offline CPU\n",
					csd->func);
				break;

			case CSD_TYPE_TTWU:
				pr_warn("IPI task-wakeup sent to offline CPU\n");
				break;

			default:
				pr_warn("IPI callback, unknown type %d, sent to offline CPU\n",
					CSD_TYPE(csd));
				break;
			}
		}
	}

	/*
	 * First; run all SYNC callbacks, people are waiting for us.
	 */
	prev = NULL;
	llist_for_each_entry_safe(csd, csd_next, entry, node.llist) {
		/* Do we wait until *after* callback? */
		if (CSD_TYPE(csd) == CSD_TYPE_SYNC) {
			smp_call_func_t func = csd->func;
			void *info = csd->info;

			if (prev) {
				prev->next = &csd_next->node.llist;
			} else {
				entry = &csd_next->node.llist;
			}

			csd_lock_record(csd);
			csd_do_func(func, info, csd);
			csd_unlock(csd);
			csd_lock_record(NULL);
		} else {
			prev = &csd->node.llist;
		}
	}

	if (!entry)
		return;

	/*
	 * Second; run all !SYNC callbacks.
	 */
	prev = NULL;
	llist_for_each_entry_safe(csd, csd_next, entry, node.llist) {
		int type = CSD_TYPE(csd);

		if (type != CSD_TYPE_TTWU) {
			if (prev) {
				prev->next = &csd_next->node.llist;
			} else {
				entry = &csd_next->node.llist;
			}

			if (type == CSD_TYPE_ASYNC) {
				smp_call_func_t func = csd->func;
				void *info = csd->info;

				csd_lock_record(csd);
				csd_unlock(csd);
				csd_do_func(func, info, csd);
				csd_lock_record(NULL);
			} else if (type == CSD_TYPE_IRQ_WORK) {
				irq_work_single(csd);
			}

		} else {
			prev = &csd->node.llist;
		}
	}

	/*
	 * Third; only CSD_TYPE_TTWU is left, issue those.
	 */
	if (entry) {
		csd = llist_entry(entry, typeof(*csd), node.llist);
		csd_do_func(sched_ttwu_pending, entry, csd);
	}
}


/**
 * flush_smp_call_function_queue - Flush pending smp-call-function callbacks
 *				   from task context (idle, migration thread)
 *
 * When TIF_POLLING_NRFLAG is supported and a CPU is in idle and has it
 * set, then remote CPUs can avoid sending IPIs and wake the idle CPU by
 * setting TIF_NEED_RESCHED. The idle task on the woken up CPU has to
 * handle queued SMP function calls before scheduling.
 *
 * The migration thread has to ensure that an eventually pending wakeup has
 * been handled before it migrates a task.
 */
void flush_smp_call_function_queue(void)
{
	unsigned int was_pending;
	unsigned long flags;

	if (llist_empty(this_cpu_ptr(&call_single_queue)))
		return;

	local_irq_save(flags);
	/* Get the already pending soft interrupts for RT enabled kernels */
	was_pending = local_softirq_pending();
	__flush_smp_call_function_queue(true);
	if (local_softirq_pending())
		do_softirq_post_smp_call_flush(was_pending);

	local_irq_restore(flags);
}

/*
 * smp_call_function_single - Run a function on a specific CPU
 * @func: The function to run. This must be fast and non-blocking.
 * @info: An arbitrary pointer to pass to the function.
 * @wait: If true, wait until function has completed on other CPUs.
 *
 * Returns 0 on success, else a negative status code.
 */
int smp_call_function_single(int cpu, smp_call_func_t func, void *info,
			     int wait)
{
	call_single_data_t *csd;
	call_single_data_t csd_stack = {
		.node = { .u_flags = CSD_FLAG_LOCK | CSD_TYPE_SYNC, },
	};
	int this_cpu;
	int err;

	/*
	 * prevent preemption and reschedule on another processor,
	 * as well as CPU removal
	 */
	this_cpu = get_cpu();

	/*
	 * Can deadlock when called with interrupts disabled.
	 * We allow cpu's that are not yet online though, as no one else can
	 * send smp call function interrupt to this cpu and as such deadlocks
	 * can't happen.
	 */
	WARN_ON_ONCE(cpu_online(this_cpu) && irqs_disabled()
		     && !oops_in_progress);

	/*
	 * When @wait we can deadlock when we interrupt between llist_add() and
	 * arch_send_call_function_ipi*(); when !@wait we can deadlock due to
	 * csd_lock() on because the interrupt context uses the same csd
	 * storage.
	 */
	WARN_ON_ONCE(!in_task());

	csd = &csd_stack;
	if (!wait) {
		csd = this_cpu_ptr(&csd_data);
		csd_lock(csd);
	}

	csd->func = func;
	csd->info = info;
#ifdef CONFIG_CSD_LOCK_WAIT_DEBUG
	csd->node.src = smp_processor_id();
	csd->node.dst = cpu;
#endif

	err = generic_exec_single(cpu, csd);

	if (wait)
		csd_lock_wait(csd);

	put_cpu();

	return err;
}
EXPORT_SYMBOL(smp_call_function_single);

/**
 * smp_call_function_single_async() - Run an asynchronous function on a
 * 			         specific CPU.
 * @cpu: The CPU to run on.
 * @csd: Pre-allocated and setup data structure
 *
 * Like smp_call_function_single(), but the call is asynchonous and
 * can thus be done from contexts with disabled interrupts.
 *
 * The caller passes his own pre-allocated data structure
 * (ie: embedded in an object) and is responsible for synchronizing it
 * such that the IPIs performed on the @csd are strictly serialized.
 *
 * If the function is called with one csd which has not yet been
 * processed by previous call to smp_call_function_single_async(), the
 * function will return immediately with -EBUSY showing that the csd
 * object is still in progress.
 *
 * NOTE: Be careful, there is unfortunately no current debugging facility to
 * validate the correctness of this serialization.
 *
 * Return: %0 on success or negative errno value on error
 */
int smp_call_function_single_async(int cpu, call_single_data_t *csd)
{
	int err = 0;

	preempt_disable();

	if (csd->node.u_flags & CSD_FLAG_LOCK) {
		err = -EBUSY;
		goto out;
	}

	csd->node.u_flags = CSD_FLAG_LOCK;
	smp_wmb();

	err = generic_exec_single(cpu, csd);

out:
	preempt_enable();

	return err;
}
EXPORT_SYMBOL_GPL(smp_call_function_single_async);

/*
 * smp_call_function_any - Run a function on any of the given cpus
 * @mask: The mask of cpus it can run on.
 * @func: The function to run. This must be fast and non-blocking.
 * @info: An arbitrary pointer to pass to the function.
 * @wait: If true, wait until function has completed.
 *
 * Returns 0 on success, else a negative status code (if no cpus were online).
 *
 * Selection preference:
 *	1) current cpu if in @mask
 *	2) any cpu of current node if in @mask
 *	3) any other online cpu in @mask
 */
int smp_call_function_any(const struct cpumask *mask,
			  smp_call_func_t func, void *info, int wait)
{
	unsigned int cpu;
	const struct cpumask *nodemask;
	int ret;

	/* Try for same CPU (cheapest) */
	cpu = get_cpu();
	if (cpumask_test_cpu(cpu, mask))
		goto call;

	/* Try for same node. */
	nodemask = cpumask_of_node(cpu_to_node(cpu));
	for (cpu = cpumask_first_and(nodemask, mask); cpu < nr_cpu_ids;
	     cpu = cpumask_next_and(cpu, nodemask, mask)) {
		if (cpu_online(cpu))
			goto call;
	}

	/* Any online will do: smp_call_function_single handles nr_cpu_ids. */
	cpu = cpumask_any_and(mask, cpu_online_mask);
call:
	ret = smp_call_function_single(cpu, func, info, wait);
	put_cpu();
	return ret;
}
EXPORT_SYMBOL_GPL(smp_call_function_any);

/*
 * Flags to be used as scf_flags argument of smp_call_function_many_cond().
 *
 * %SCF_WAIT:		Wait until function execution is completed
 * %SCF_RUN_LOCAL:	Run also locally if local cpu is set in cpumask
 */
#define SCF_WAIT	(1U << 0)
#define SCF_RUN_LOCAL	(1U << 1)

static void smp_call_function_many_cond(const struct cpumask *mask,
					smp_call_func_t func, void *info,
					unsigned int scf_flags,
					smp_cond_func_t cond_func)
{
	int cpu, last_cpu, this_cpu = smp_processor_id();
	struct call_function_data *cfd;
	bool wait = scf_flags & SCF_WAIT;
	int nr_cpus = 0;
	bool run_remote = false;
	bool run_local = false;

	lockdep_assert_preemption_disabled();

	/*
	 * Can deadlock when called with interrupts disabled.
	 * We allow cpu's that are not yet online though, as no one else can
	 * send smp call function interrupt to this cpu and as such deadlocks
	 * can't happen.
	 */
	if (cpu_online(this_cpu) && !oops_in_progress &&
	    !early_boot_irqs_disabled)
		lockdep_assert_irqs_enabled();

	/*
	 * When @wait we can deadlock when we interrupt between llist_add() and
	 * arch_send_call_function_ipi*(); when !@wait we can deadlock due to
	 * csd_lock() on because the interrupt context uses the same csd
	 * storage.
	 */
	WARN_ON_ONCE(!in_task());

	/* Check if we need local execution. */
	if ((scf_flags & SCF_RUN_LOCAL) && cpumask_test_cpu(this_cpu, mask) &&
	    (!cond_func || cond_func(this_cpu, info)))
		run_local = true;

	/* Check if we need remote execution, i.e., any CPU excluding this one. */
	cpu = cpumask_first_and(mask, cpu_online_mask);
	if (cpu == this_cpu)
		cpu = cpumask_next_and(cpu, mask, cpu_online_mask);
	if (cpu < nr_cpu_ids)
		run_remote = true;

	if (run_remote) {
		cfd = this_cpu_ptr(&cfd_data);
		cpumask_and(cfd->cpumask, mask, cpu_online_mask);
		__cpumask_clear_cpu(this_cpu, cfd->cpumask);

		cpumask_clear(cfd->cpumask_ipi);
		for_each_cpu(cpu, cfd->cpumask) {
			call_single_data_t *csd = per_cpu_ptr(cfd->csd, cpu);

			if (cond_func && !cond_func(cpu, info)) {
				__cpumask_clear_cpu(cpu, cfd->cpumask);
				continue;
			}

			csd_lock(csd);
			if (wait)
				csd->node.u_flags |= CSD_TYPE_SYNC;
			csd->func = func;
			csd->info = info;
#ifdef CONFIG_CSD_LOCK_WAIT_DEBUG
			csd->node.src = smp_processor_id();
			csd->node.dst = cpu;
#endif
			trace_csd_queue_cpu(cpu, _RET_IP_, func, csd);

			if (llist_add(&csd->node.llist, &per_cpu(call_single_queue, cpu))) {
				__cpumask_set_cpu(cpu, cfd->cpumask_ipi);
				nr_cpus++;
				last_cpu = cpu;
			}
		}

		/*
		 * Choose the most efficient way to send an IPI. Note that the
		 * number of CPUs might be zero due to concurrent changes to the
		 * provided mask.
		 */
		if (nr_cpus == 1)
			send_call_function_single_ipi(last_cpu);
		else if (likely(nr_cpus > 1))
			send_call_function_ipi_mask(cfd->cpumask_ipi);
	}

	if (run_local) {
		unsigned long flags;

		local_irq_save(flags);
		csd_do_func(func, info, NULL);
		local_irq_restore(flags);
	}

	if (run_remote && wait) {
		for_each_cpu(cpu, cfd->cpumask) {
			call_single_data_t *csd;

			csd = per_cpu_ptr(cfd->csd, cpu);
			csd_lock_wait(csd);
		}
	}
}

/**
 * smp_call_function_many(): Run a function on a set of CPUs.
 * @mask: The set of cpus to run on (only runs on online subset).
 * @func: The function to run. This must be fast and non-blocking.
 * @info: An arbitrary pointer to pass to the function.
 * @wait: Bitmask that controls the operation. If %SCF_WAIT is set, wait
 *        (atomically) until function has completed on other CPUs. If
 *        %SCF_RUN_LOCAL is set, the function will also be run locally
 *        if the local CPU is set in the @cpumask.
 *
 * If @wait is true, then returns once @func has returned.
 *
 * You must not call this function with disabled interrupts or from a
 * hardware interrupt handler or from a bottom half handler. Preemption
 * must be disabled when calling this function.
 */
void smp_call_function_many(const struct cpumask *mask,
			    smp_call_func_t func, void *info, bool wait)
{
	smp_call_function_many_cond(mask, func, info, wait * SCF_WAIT, NULL);
}
EXPORT_SYMBOL(smp_call_function_many);

/**
 * smp_call_function(): Run a function on all other CPUs.
 * @func: The function to run. This must be fast and non-blocking.
 * @info: An arbitrary pointer to pass to the function.
 * @wait: If true, wait (atomically) until function has completed
 *        on other CPUs.
 *
 * Returns 0.
 *
 * If @wait is true, then returns once @func has returned; otherwise
 * it returns just before the target cpu calls @func.
 *
 * You must not call this function with disabled interrupts or from a
 * hardware interrupt handler or from a bottom half handler.
 */
void smp_call_function(smp_call_func_t func, void *info, int wait)
{
	preempt_disable();
	smp_call_function_many(cpu_online_mask, func, info, wait);
	preempt_enable();
}
EXPORT_SYMBOL(smp_call_function);

/* Setup configured maximum number of CPUs to activate */
unsigned int setup_max_cpus = NR_CPUS;
EXPORT_SYMBOL(setup_max_cpus);


/*
 * Setup routine for controlling SMP activation
 *
 * Command-line option of "nosmp" or "maxcpus=0" will disable SMP
 * activation entirely (the MPS table probe still happens, though).
 *
 * Command-line option of "maxcpus=<NUM>", where <NUM> is an integer
 * greater than 0, limits the maximum number of CPUs activated in
 * SMP mode to <NUM>.
 */

void __weak __init arch_disable_smp_support(void) { }

static int __init nosmp(char *str)
{
	setup_max_cpus = 0;
	arch_disable_smp_support();

	return 0;
}

early_param("nosmp", nosmp);

/* this is hard limit */
static int __init nrcpus(char *str)
{
	int nr_cpus;

	if (get_option(&str, &nr_cpus) && nr_cpus > 0 && nr_cpus < nr_cpu_ids)
		set_nr_cpu_ids(nr_cpus);

	return 0;
}

early_param("nr_cpus", nrcpus);

static int __init maxcpus(char *str)
{
	get_option(&str, &setup_max_cpus);
	if (setup_max_cpus == 0)
		arch_disable_smp_support();

	return 0;
}

early_param("maxcpus", maxcpus);

#if (NR_CPUS > 1) && !defined(CONFIG_FORCE_NR_CPUS)
/* Setup number of possible processor ids */
unsigned int nr_cpu_ids __read_mostly = NR_CPUS;
EXPORT_SYMBOL(nr_cpu_ids);
#endif

/* An arch may set nr_cpu_ids earlier if needed, so this would be redundant */
void __init setup_nr_cpu_ids(void)
{
	set_nr_cpu_ids(find_last_bit(cpumask_bits(cpu_possible_mask), NR_CPUS) + 1);
}

/* Called by boot processor to activate the rest. */
void __init smp_init(void)
{
	int num_nodes, num_cpus;

	idle_threads_init();
	cpuhp_threads_init();

	pr_info("Bringing up secondary CPUs ...\n");

	bringup_nonboot_cpus(setup_max_cpus);

	num_nodes = num_online_nodes();
	num_cpus  = num_online_cpus();
	pr_info("Brought up %d node%s, %d CPU%s\n",
		num_nodes, str_plural(num_nodes), num_cpus, str_plural(num_cpus));

	/* Any cleanup work */
	smp_cpus_done(setup_max_cpus);
}

/*
 * on_each_cpu_cond(): Call a function on each processor for which
 * the supplied function cond_func returns true, optionally waiting
 * for all the required CPUs to finish. This may include the local
 * processor.
 * @cond_func:	A callback function that is passed a cpu id and
 *		the info parameter. The function is called
 *		with preemption disabled. The function should
 *		return a blooean value indicating whether to IPI
 *		the specified CPU.
 * @func:	The function to run on all applicable CPUs.
 *		This must be fast and non-blocking.
 * @info:	An arbitrary pointer to pass to both functions.
 * @wait:	If true, wait (atomically) until function has
 *		completed on other CPUs.
 *
 * Preemption is disabled to protect against CPUs going offline but not online.
 * CPUs going online during the call will not be seen or sent an IPI.
 *
 * You must not call this function with disabled interrupts or
 * from a hardware interrupt handler or from a bottom half handler.
 */
void on_each_cpu_cond_mask(smp_cond_func_t cond_func, smp_call_func_t func,
			   void *info, bool wait, const struct cpumask *mask)
{
	unsigned int scf_flags = SCF_RUN_LOCAL;

	if (wait)
		scf_flags |= SCF_WAIT;

	preempt_disable();
	smp_call_function_many_cond(mask, func, info, scf_flags, cond_func);
	preempt_enable();
}
EXPORT_SYMBOL(on_each_cpu_cond_mask);

static void do_nothing(void *unused)
{
}

/**
 * kick_all_cpus_sync - Force all cpus out of idle
 *
 * Used to synchronize the update of pm_idle function pointer. It's
 * called after the pointer is updated and returns after the dummy
 * callback function has been executed on all cpus. The execution of
 * the function can only happen on the remote cpus after they have
 * left the idle function which had been called via pm_idle function
 * pointer. So it's guaranteed that nothing uses the previous pointer
 * anymore.
 */
void kick_all_cpus_sync(void)
{
	/* Make sure the change is visible before we kick the cpus */
	smp_mb();
	smp_call_function(do_nothing, NULL, 1);
}
EXPORT_SYMBOL_GPL(kick_all_cpus_sync);

/**
 * wake_up_all_idle_cpus - break all cpus out of idle
 * wake_up_all_idle_cpus try to break all cpus which is in idle state even
 * including idle polling cpus, for non-idle cpus, we will do nothing
 * for them.
 */
void wake_up_all_idle_cpus(void)
{
	int cpu;

	for_each_possible_cpu(cpu) {
		preempt_disable();
		if (cpu != smp_processor_id() && cpu_online(cpu))
			wake_up_if_idle(cpu);
		preempt_enable();
	}
}
EXPORT_SYMBOL_GPL(wake_up_all_idle_cpus);

/**
 * struct smp_call_on_cpu_struct - Call a function on a specific CPU
 * @work: &work_struct
 * @done: &completion to signal
 * @func: function to call
 * @data: function's data argument
 * @ret: return value from @func
 * @cpu: target CPU (%-1 for any CPU)
 *
 * Used to call a function on a specific cpu and wait for it to return.
 * Optionally make sure the call is done on a specified physical cpu via vcpu
 * pinning in order to support virtualized environments.
 */
struct smp_call_on_cpu_struct {
	struct work_struct	work;
	struct completion	done;
	int			(*func)(void *);
	void			*data;
	int			ret;
	int			cpu;
};

static void smp_call_on_cpu_callback(struct work_struct *work)
{
	struct smp_call_on_cpu_struct *sscs;

	sscs = container_of(work, struct smp_call_on_cpu_struct, work);
	if (sscs->cpu >= 0)
		hypervisor_pin_vcpu(sscs->cpu);
	sscs->ret = sscs->func(sscs->data);
	if (sscs->cpu >= 0)
		hypervisor_pin_vcpu(-1);

	complete(&sscs->done);
}

int smp_call_on_cpu(unsigned int cpu, int (*func)(void *), void *par, bool phys)
{
	struct smp_call_on_cpu_struct sscs = {
		.done = COMPLETION_INITIALIZER_ONSTACK(sscs.done),
		.func = func,
		.data = par,
		.cpu  = phys ? cpu : -1,
	};

	INIT_WORK_ONSTACK(&sscs.work, smp_call_on_cpu_callback);

	if (cpu >= nr_cpu_ids || !cpu_online(cpu))
		return -ENXIO;

	queue_work_on(cpu, system_wq, &sscs.work);
	wait_for_completion(&sscs.done);
	destroy_work_on_stack(&sscs.work);

	return sscs.ret;
}
EXPORT_SYMBOL_GPL(smp_call_on_cpu);
