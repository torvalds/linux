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

#include "smpboot.h"
#include "sched/smp.h"

#define CSD_TYPE(_csd)	((_csd)->node.u_flags & CSD_FLAG_TYPE_MASK)

#ifdef CONFIG_CSD_LOCK_WAIT_DEBUG
union cfd_seq_cnt {
	u64		val;
	struct {
		u64	src:16;
		u64	dst:16;
#define CFD_SEQ_NOCPU	0xffff
		u64	type:4;
#define CFD_SEQ_QUEUE	0
#define CFD_SEQ_IPI	1
#define CFD_SEQ_NOIPI	2
#define CFD_SEQ_PING	3
#define CFD_SEQ_PINGED	4
#define CFD_SEQ_HANDLE	5
#define CFD_SEQ_DEQUEUE	6
#define CFD_SEQ_IDLE	7
#define CFD_SEQ_GOTIPI	8
#define CFD_SEQ_HDLEND	9
		u64	cnt:28;
	}		u;
};

static char *seq_type[] = {
	[CFD_SEQ_QUEUE]		= "queue",
	[CFD_SEQ_IPI]		= "ipi",
	[CFD_SEQ_NOIPI]		= "noipi",
	[CFD_SEQ_PING]		= "ping",
	[CFD_SEQ_PINGED]	= "pinged",
	[CFD_SEQ_HANDLE]	= "handle",
	[CFD_SEQ_DEQUEUE]	= "dequeue (src CPU 0 == empty)",
	[CFD_SEQ_IDLE]		= "idle",
	[CFD_SEQ_GOTIPI]	= "gotipi",
	[CFD_SEQ_HDLEND]	= "hdlend (src CPU 0 == early)",
};

struct cfd_seq_local {
	u64	ping;
	u64	pinged;
	u64	handle;
	u64	dequeue;
	u64	idle;
	u64	gotipi;
	u64	hdlend;
};
#endif

struct cfd_percpu {
	call_single_data_t	csd;
#ifdef CONFIG_CSD_LOCK_WAIT_DEBUG
	u64	seq_queue;
	u64	seq_ipi;
	u64	seq_noipi;
#endif
};

struct call_function_data {
	struct cfd_percpu	__percpu *pcpu;
	cpumask_var_t		cpumask;
	cpumask_var_t		cpumask_ipi;
};

static DEFINE_PER_CPU_ALIGNED(struct call_function_data, cfd_data);

static DEFINE_PER_CPU_SHARED_ALIGNED(struct llist_head, call_single_queue);

static void flush_smp_call_function_queue(bool warn_cpu_offline);

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
	cfd->pcpu = alloc_percpu(struct cfd_percpu);
	if (!cfd->pcpu) {
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
	free_percpu(cfd->pcpu);
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
	flush_smp_call_function_queue(false);
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

#ifdef CONFIG_CSD_LOCK_WAIT_DEBUG

static DEFINE_STATIC_KEY_FALSE(csdlock_debug_enabled);
static DEFINE_STATIC_KEY_FALSE(csdlock_debug_extended);

static int __init csdlock_debug(char *str)
{
	unsigned int val = 0;

	if (str && !strcmp(str, "ext")) {
		val = 1;
		static_branch_enable(&csdlock_debug_extended);
	} else
		get_option(&str, &val);

	if (val)
		static_branch_enable(&csdlock_debug_enabled);

	return 0;
}
early_param("csdlock_debug", csdlock_debug);

static DEFINE_PER_CPU(call_single_data_t *, cur_csd);
static DEFINE_PER_CPU(smp_call_func_t, cur_csd_func);
static DEFINE_PER_CPU(void *, cur_csd_info);
static DEFINE_PER_CPU(struct cfd_seq_local, cfd_seq_local);

#define CSD_LOCK_TIMEOUT (5ULL * NSEC_PER_SEC)
static atomic_t csd_bug_count = ATOMIC_INIT(0);
static u64 cfd_seq;

#define CFD_SEQ(s, d, t, c)	\
	(union cfd_seq_cnt){ .u.src = s, .u.dst = d, .u.type = t, .u.cnt = c }

static u64 cfd_seq_inc(unsigned int src, unsigned int dst, unsigned int type)
{
	union cfd_seq_cnt new, old;

	new = CFD_SEQ(src, dst, type, 0);

	do {
		old.val = READ_ONCE(cfd_seq);
		new.u.cnt = old.u.cnt + 1;
	} while (cmpxchg(&cfd_seq, old.val, new.val) != old.val);

	return old.val;
}

#define cfd_seq_store(var, src, dst, type)				\
	do {								\
		if (static_branch_unlikely(&csdlock_debug_extended))	\
			var = cfd_seq_inc(src, dst, type);		\
	} while (0)

/* Record current CSD work for current CPU, NULL to erase. */
static void __csd_lock_record(struct __call_single_data *csd)
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

static __always_inline void csd_lock_record(struct __call_single_data *csd)
{
	if (static_branch_unlikely(&csdlock_debug_enabled))
		__csd_lock_record(csd);
}

static int csd_lock_wait_getcpu(struct __call_single_data *csd)
{
	unsigned int csd_type;

	csd_type = CSD_TYPE(csd);
	if (csd_type == CSD_TYPE_ASYNC || csd_type == CSD_TYPE_SYNC)
		return csd->node.dst; /* Other CSD_TYPE_ values might not have ->dst. */
	return -1;
}

static void cfd_seq_data_add(u64 val, unsigned int src, unsigned int dst,
			     unsigned int type, union cfd_seq_cnt *data,
			     unsigned int *n_data, unsigned int now)
{
	union cfd_seq_cnt new[2];
	unsigned int i, j, k;

	new[0].val = val;
	new[1] = CFD_SEQ(src, dst, type, new[0].u.cnt + 1);

	for (i = 0; i < 2; i++) {
		if (new[i].u.cnt <= now)
			new[i].u.cnt |= 0x80000000U;
		for (j = 0; j < *n_data; j++) {
			if (new[i].u.cnt == data[j].u.cnt) {
				/* Direct read value trumps generated one. */
				if (i == 0)
					data[j].val = new[i].val;
				break;
			}
			if (new[i].u.cnt < data[j].u.cnt) {
				for (k = *n_data; k > j; k--)
					data[k].val = data[k - 1].val;
				data[j].val = new[i].val;
				(*n_data)++;
				break;
			}
		}
		if (j == *n_data) {
			data[j].val = new[i].val;
			(*n_data)++;
		}
	}
}

static const char *csd_lock_get_type(unsigned int type)
{
	return (type >= ARRAY_SIZE(seq_type)) ? "?" : seq_type[type];
}

static void csd_lock_print_extended(struct __call_single_data *csd, int cpu)
{
	struct cfd_seq_local *seq = &per_cpu(cfd_seq_local, cpu);
	unsigned int srccpu = csd->node.src;
	struct call_function_data *cfd = per_cpu_ptr(&cfd_data, srccpu);
	struct cfd_percpu *pcpu = per_cpu_ptr(cfd->pcpu, cpu);
	unsigned int now;
	union cfd_seq_cnt data[2 * ARRAY_SIZE(seq_type)];
	unsigned int n_data = 0, i;

	data[0].val = READ_ONCE(cfd_seq);
	now = data[0].u.cnt;

	cfd_seq_data_add(pcpu->seq_queue,			srccpu, cpu,	       CFD_SEQ_QUEUE,  data, &n_data, now);
	cfd_seq_data_add(pcpu->seq_ipi,				srccpu, cpu,	       CFD_SEQ_IPI,    data, &n_data, now);
	cfd_seq_data_add(pcpu->seq_noipi,			srccpu, cpu,	       CFD_SEQ_NOIPI,  data, &n_data, now);

	cfd_seq_data_add(per_cpu(cfd_seq_local.ping, srccpu),	srccpu, CFD_SEQ_NOCPU, CFD_SEQ_PING,   data, &n_data, now);
	cfd_seq_data_add(per_cpu(cfd_seq_local.pinged, srccpu), srccpu, CFD_SEQ_NOCPU, CFD_SEQ_PINGED, data, &n_data, now);

	cfd_seq_data_add(seq->idle,    CFD_SEQ_NOCPU, cpu, CFD_SEQ_IDLE,    data, &n_data, now);
	cfd_seq_data_add(seq->gotipi,  CFD_SEQ_NOCPU, cpu, CFD_SEQ_GOTIPI,  data, &n_data, now);
	cfd_seq_data_add(seq->handle,  CFD_SEQ_NOCPU, cpu, CFD_SEQ_HANDLE,  data, &n_data, now);
	cfd_seq_data_add(seq->dequeue, CFD_SEQ_NOCPU, cpu, CFD_SEQ_DEQUEUE, data, &n_data, now);
	cfd_seq_data_add(seq->hdlend,  CFD_SEQ_NOCPU, cpu, CFD_SEQ_HDLEND,  data, &n_data, now);

	for (i = 0; i < n_data; i++) {
		pr_alert("\tcsd: cnt(%07x): %04x->%04x %s\n",
			 data[i].u.cnt & ~0x80000000U, data[i].u.src,
			 data[i].u.dst, csd_lock_get_type(data[i].u.type));
	}
	pr_alert("\tcsd: cnt now: %07x\n", now);
}

/*
 * Complain if too much time spent waiting.  Note that only
 * the CSD_TYPE_SYNC/ASYNC types provide the destination CPU,
 * so waiting on other types gets much less information.
 */
static bool csd_lock_wait_toolong(struct __call_single_data *csd, u64 ts0, u64 *ts1, int *bug_id)
{
	int cpu = -1;
	int cpux;
	bool firsttime;
	u64 ts2, ts_delta;
	call_single_data_t *cpu_cur_csd;
	unsigned int flags = READ_ONCE(csd->node.u_flags);

	if (!(flags & CSD_FLAG_LOCK)) {
		if (!unlikely(*bug_id))
			return true;
		cpu = csd_lock_wait_getcpu(csd);
		pr_alert("csd: CSD lock (#%d) got unstuck on CPU#%02d, CPU#%02d released the lock.\n",
			 *bug_id, raw_smp_processor_id(), cpu);
		return true;
	}

	ts2 = sched_clock();
	ts_delta = ts2 - *ts1;
	if (likely(ts_delta <= CSD_LOCK_TIMEOUT))
		return false;

	firsttime = !*bug_id;
	if (firsttime)
		*bug_id = atomic_inc_return(&csd_bug_count);
	cpu = csd_lock_wait_getcpu(csd);
	if (WARN_ONCE(cpu < 0 || cpu >= nr_cpu_ids, "%s: cpu = %d\n", __func__, cpu))
		cpux = 0;
	else
		cpux = cpu;
	cpu_cur_csd = smp_load_acquire(&per_cpu(cur_csd, cpux)); /* Before func and info. */
	pr_alert("csd: %s non-responsive CSD lock (#%d) on CPU#%d, waiting %llu ns for CPU#%02d %pS(%ps).\n",
		 firsttime ? "Detected" : "Continued", *bug_id, raw_smp_processor_id(), ts2 - ts0,
		 cpu, csd->func, csd->info);
	if (cpu_cur_csd && csd != cpu_cur_csd) {
		pr_alert("\tcsd: CSD lock (#%d) handling prior %pS(%ps) request.\n",
			 *bug_id, READ_ONCE(per_cpu(cur_csd_func, cpux)),
			 READ_ONCE(per_cpu(cur_csd_info, cpux)));
	} else {
		pr_alert("\tcsd: CSD lock (#%d) %s.\n",
			 *bug_id, !cpu_cur_csd ? "unresponsive" : "handling this request");
	}
	if (cpu >= 0) {
		if (static_branch_unlikely(&csdlock_debug_extended))
			csd_lock_print_extended(csd, cpu);
		if (!trigger_single_cpu_backtrace(cpu))
			dump_cpu_task(cpu);
		if (!cpu_cur_csd) {
			pr_alert("csd: Re-sending CSD lock (#%d) IPI from CPU#%02d to CPU#%02d\n", *bug_id, raw_smp_processor_id(), cpu);
			arch_send_call_function_single_ipi(cpu);
		}
	}
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
static void __csd_lock_wait(struct __call_single_data *csd)
{
	int bug_id = 0;
	u64 ts0, ts1;

	ts1 = ts0 = sched_clock();
	for (;;) {
		if (csd_lock_wait_toolong(csd, ts0, &ts1, &bug_id))
			break;
		cpu_relax();
	}
	smp_acquire__after_ctrl_dep();
}

static __always_inline void csd_lock_wait(struct __call_single_data *csd)
{
	if (static_branch_unlikely(&csdlock_debug_enabled)) {
		__csd_lock_wait(csd);
		return;
	}

	smp_cond_load_acquire(&csd->node.u_flags, !(VAL & CSD_FLAG_LOCK));
}

static void __smp_call_single_queue_debug(int cpu, struct llist_node *node)
{
	unsigned int this_cpu = smp_processor_id();
	struct cfd_seq_local *seq = this_cpu_ptr(&cfd_seq_local);
	struct call_function_data *cfd = this_cpu_ptr(&cfd_data);
	struct cfd_percpu *pcpu = per_cpu_ptr(cfd->pcpu, cpu);

	cfd_seq_store(pcpu->seq_queue, this_cpu, cpu, CFD_SEQ_QUEUE);
	if (llist_add(node, &per_cpu(call_single_queue, cpu))) {
		cfd_seq_store(pcpu->seq_ipi, this_cpu, cpu, CFD_SEQ_IPI);
		cfd_seq_store(seq->ping, this_cpu, cpu, CFD_SEQ_PING);
		send_call_function_single_ipi(cpu);
		cfd_seq_store(seq->pinged, this_cpu, cpu, CFD_SEQ_PINGED);
	} else {
		cfd_seq_store(pcpu->seq_noipi, this_cpu, cpu, CFD_SEQ_NOIPI);
	}
}
#else
#define cfd_seq_store(var, src, dst, type)

static void csd_lock_record(struct __call_single_data *csd)
{
}

static __always_inline void csd_lock_wait(struct __call_single_data *csd)
{
	smp_cond_load_acquire(&csd->node.u_flags, !(VAL & CSD_FLAG_LOCK));
}
#endif

static __always_inline void csd_lock(struct __call_single_data *csd)
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

static __always_inline void csd_unlock(struct __call_single_data *csd)
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
#ifdef CONFIG_CSD_LOCK_WAIT_DEBUG
	if (static_branch_unlikely(&csdlock_debug_extended)) {
		unsigned int type;

		type = CSD_TYPE(container_of(node, call_single_data_t,
					     node.llist));
		if (type == CSD_TYPE_SYNC || type == CSD_TYPE_ASYNC) {
			__smp_call_single_queue_debug(cpu, node);
			return;
		}
	}
#endif

	/*
	 * The list addition should be visible before sending the IPI
	 * handler locks the list to pull the entry off it because of
	 * normal cache coherency rules implied by spinlocks.
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
static int generic_exec_single(int cpu, struct __call_single_data *csd)
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
		func(info);
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
	cfd_seq_store(this_cpu_ptr(&cfd_seq_local)->gotipi, CFD_SEQ_NOCPU,
		      smp_processor_id(), CFD_SEQ_GOTIPI);
	flush_smp_call_function_queue(true);
}

/**
 * flush_smp_call_function_queue - Flush pending smp-call-function callbacks
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
static void flush_smp_call_function_queue(bool warn_cpu_offline)
{
	call_single_data_t *csd, *csd_next;
	struct llist_node *entry, *prev;
	struct llist_head *head;
	static bool warned;

	lockdep_assert_irqs_disabled();

	head = this_cpu_ptr(&call_single_queue);
	cfd_seq_store(this_cpu_ptr(&cfd_seq_local)->handle, CFD_SEQ_NOCPU,
		      smp_processor_id(), CFD_SEQ_HANDLE);
	entry = llist_del_all(head);
	cfd_seq_store(this_cpu_ptr(&cfd_seq_local)->dequeue,
		      /* Special meaning of source cpu: 0 == queue empty */
		      entry ? CFD_SEQ_NOCPU : 0,
		      smp_processor_id(), CFD_SEQ_DEQUEUE);
	entry = llist_reverse_order(entry);

	/* There shouldn't be any pending callbacks on an offline CPU. */
	if (unlikely(warn_cpu_offline && !cpu_online(smp_processor_id()) &&
		     !warned && !llist_empty(head))) {
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
			func(info);
			csd_unlock(csd);
			csd_lock_record(NULL);
		} else {
			prev = &csd->node.llist;
		}
	}

	if (!entry) {
		cfd_seq_store(this_cpu_ptr(&cfd_seq_local)->hdlend,
			      0, smp_processor_id(),
			      CFD_SEQ_HDLEND);
		return;
	}

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
				func(info);
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
	if (entry)
		sched_ttwu_pending(entry);

	cfd_seq_store(this_cpu_ptr(&cfd_seq_local)->hdlend, CFD_SEQ_NOCPU,
		      smp_processor_id(), CFD_SEQ_HDLEND);
}

void flush_smp_call_function_from_idle(void)
{
	unsigned long flags;

	if (llist_empty(this_cpu_ptr(&call_single_queue)))
		return;

	cfd_seq_store(this_cpu_ptr(&cfd_seq_local)->idle, CFD_SEQ_NOCPU,
		      smp_processor_id(), CFD_SEQ_IDLE);
	local_irq_save(flags);
	flush_smp_call_function_queue(true);
	if (local_softirq_pending())
		do_softirq();

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
int smp_call_function_single_async(int cpu, struct __call_single_data *csd)
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
	bool run_remote = false;
	bool run_local = false;
	int nr_cpus = 0;

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
	if ((scf_flags & SCF_RUN_LOCAL) && cpumask_test_cpu(this_cpu, mask))
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
			struct cfd_percpu *pcpu = per_cpu_ptr(cfd->pcpu, cpu);
			call_single_data_t *csd = &pcpu->csd;

			if (cond_func && !cond_func(cpu, info))
				continue;

			csd_lock(csd);
			if (wait)
				csd->node.u_flags |= CSD_TYPE_SYNC;
			csd->func = func;
			csd->info = info;
#ifdef CONFIG_CSD_LOCK_WAIT_DEBUG
			csd->node.src = smp_processor_id();
			csd->node.dst = cpu;
#endif
			cfd_seq_store(pcpu->seq_queue, this_cpu, cpu, CFD_SEQ_QUEUE);
			if (llist_add(&csd->node.llist, &per_cpu(call_single_queue, cpu))) {
				__cpumask_set_cpu(cpu, cfd->cpumask_ipi);
				nr_cpus++;
				last_cpu = cpu;

				cfd_seq_store(pcpu->seq_ipi, this_cpu, cpu, CFD_SEQ_IPI);
			} else {
				cfd_seq_store(pcpu->seq_noipi, this_cpu, cpu, CFD_SEQ_NOIPI);
			}
		}

		cfd_seq_store(this_cpu_ptr(&cfd_seq_local)->ping, this_cpu, CFD_SEQ_NOCPU, CFD_SEQ_PING);

		/*
		 * Choose the most efficient way to send an IPI. Note that the
		 * number of CPUs might be zero due to concurrent changes to the
		 * provided mask.
		 */
		if (nr_cpus == 1)
			send_call_function_single_ipi(last_cpu);
		else if (likely(nr_cpus > 1))
			arch_send_call_function_ipi_mask(cfd->cpumask_ipi);

		cfd_seq_store(this_cpu_ptr(&cfd_seq_local)->pinged, this_cpu, CFD_SEQ_NOCPU, CFD_SEQ_PINGED);
	}

	if (run_local && (!cond_func || cond_func(this_cpu, info))) {
		unsigned long flags;

		local_irq_save(flags);
		func(info);
		local_irq_restore(flags);
	}

	if (run_remote && wait) {
		for_each_cpu(cpu, cfd->cpumask) {
			call_single_data_t *csd;

			csd = &per_cpu_ptr(cfd->pcpu, cpu)->csd;
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

void __weak arch_disable_smp_support(void) { }

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
		nr_cpu_ids = nr_cpus;

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

/* Setup number of possible processor ids */
unsigned int nr_cpu_ids __read_mostly = NR_CPUS;
EXPORT_SYMBOL(nr_cpu_ids);

/* An arch may set nr_cpu_ids earlier if needed, so this would be redundant */
void __init setup_nr_cpu_ids(void)
{
	nr_cpu_ids = find_last_bit(cpumask_bits(cpu_possible_mask),NR_CPUS) + 1;
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
		num_nodes, (num_nodes > 1 ? "s" : ""),
		num_cpus,  (num_cpus  > 1 ? "s" : ""));

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

	return sscs.ret;
}
EXPORT_SYMBOL_GPL(smp_call_on_cpu);
