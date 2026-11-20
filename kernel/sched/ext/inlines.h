/* SPDX-License-Identifier: GPL-2.0 */
/*
 * BPF extensible scheduler class: Documentation/scheduler/sched-ext.rst
 *
 * Inline definitions layered on top of internal.h and cid.h.
 *
 * Copyright (c) 2026 Meta Platforms, Inc. and affiliates.
 * Copyright (c) 2026 Tejun Heo <tj@kernel.org>
 */
#ifndef _KERNEL_SCHED_EXT_INLINES_H
#define _KERNEL_SCHED_EXT_INLINES_H

#include "internal.h"
#include "cid.h"

/* what dispatch concluded, consumed by the pick that follows */
enum scx_dsp_verdict {
	SCX_DSP_NONE,		/* nothing to run */
	SCX_DSP_LOCAL,		/* local DSQ has tasks */
	SCX_DSP_PREV,		/* keep running @prev */
	SCX_DSP_RETRY,		/* restart the pick */
};

/**
 * scx_dsp_prev_stale - Test whether dispatch's @prev went stale
 * @rq: rq being dispatched for
 * @prev: @rq's donor when dispatch started
 *
 * @prev can stop being @rq's donor while dispatch has the rq lock dropped. With
 * proxy execution, @prev may not be the running task, and another CPU can reset
 * the donor to rq->curr. With core scheduling, the core pick runs dispatch for
 * the sibling rqs too, and a sibling can switch @prev out. Dispatch restarts
 * the pick when that happened and doesn't hand the old donor to the BPF
 * scheduler.
 *
 * With proxy execution alone, the donor only ever changes to rq->curr during a
 * pick. Only __schedule() on this CPU sets a different donor, after the pick,
 * so the restarted pick takes rq->curr as @prev and can't go stale again. Under
 * core scheduling, the restart goes through the core pick, which already
 * retries whenever dispatch dropped the lock.
 */
static inline bool scx_dsp_prev_stale(struct rq *rq, struct task_struct *prev)
{
	return (sched_proxy_exec() || sched_core_enabled(rq)) && unlikely(prev != rq->donor);
}

/*
 * One user of this function is scx_bpf_sub_dispatch() which can be called
 * recursively as sub-sched dispatches nest. Always inline to reduce stack usage
 * from the call frame.
 */
static __always_inline enum scx_dsp_verdict
scx_dispatch_sched(struct scx_sched *sch, struct rq *rq,
		   struct task_struct *prev, bool nested)
{
	struct scx_dsp_ctx *dspc = &this_cpu_ptr(sch->pcpu)->dsp_ctx;
	int nr_loops = SCX_DSP_MAX_LOOPS;
	s32 cpu = cpu_of(rq);
	bool prev_on_sch;

	if (scx_consume_global_dsq(sch, rq))
		return SCX_DSP_LOCAL;

	/*
	 * A bypass consume puts a task on the cpu being dispatched for, to run
	 * right away. That is an IMMED insert and needs only
	 * %SCX_CAP_ENQ_IMMED, the base cap. Dispatch descends into @sch only on
	 * a cpu where @sch holds the base cap, so @sch holds it here. The same
	 * holds for the consume at the end of this function.
	 */
	if (scx_bypass_dsp_enabled(sch)) {
		/* if @sch is bypassing, only the bypass DSQs are active */
		if (scx_bypassing(sch, cpu)) {
			if (scx_consume_dispatch_q(sch, rq, scx_bypass_dsq(sch, cpu),
						   SCX_ENQ_IMMED))
				return SCX_DSP_LOCAL;
			return SCX_DSP_NONE;
		}

#ifdef CONFIG_EXT_SUB_SCHED
		/*
		 * If @sch isn't bypassing but its children are, @sch is
		 * responsible for making forward progress for both its own
		 * tasks that aren't bypassing and the bypassing descendants'
		 * tasks. The following implements a simple built-in behavior -
		 * let each CPU try to run the bypass DSQ every Nth time.
		 *
		 * Later, if necessary, we can add an ops flag to suppress the
		 * auto-consumption and a kfunc to consume the bypass DSQ and,
		 * so that the BPF scheduler can fully control scheduling of
		 * bypassed tasks.
		 */
		struct scx_sched_pcpu *pcpu = per_cpu_ptr(sch->pcpu, cpu);

		if (!(pcpu->bypass_host_seq++ % SCX_BYPASS_HOST_NTH) &&
		    scx_consume_dispatch_q(sch, rq, scx_bypass_dsq(sch, cpu),
					   SCX_ENQ_IMMED)) {
			__scx_add_event(sch, SCX_EV_SUB_BYPASS_DISPATCH, 1);
			return SCX_DSP_LOCAL;
		}
#endif	/* CONFIG_EXT_SUB_SCHED */
	}

	/*
	 * scx_rq_online() can't be used. Its cpu_active() test goes false
	 * before CPU hotplug waits for an RCU grace period, and
	 * handle_hotplug() moves this CPU's tasks to the local DSQ only after
	 * the wait. The grace period can depend on those tasks running.
	 */
	if (unlikely(!SCX_HAS_OP(sch, dispatch)) || !(rq->scx.flags & SCX_RQ_ONLINE))
		return SCX_DSP_NONE;

	dspc->rq = rq;

	/*
	 * The dispatch loop. Because scx_flush_dispatch_buf() may drop the rq
	 * lock, the local DSQ might still end up empty after a successful
	 * ops.dispatch(). If the local DSQ is empty even after ops.dispatch()
	 * produced some tasks, retry. The BPF scheduler may depend on this
	 * looping behavior to simplify its implementation.
	 */
	do {
		dspc->nr_tasks = 0;

#ifdef CONFIG_EXT_SUB_SCHED
		/* stash @prev so that nested invocations can access it */
		if (!nested)
			rq->scx.sub_dispatch_prev = prev;
#endif

		/* the dispatch so far may have dropped the rq lock */
		prev_on_sch = !scx_dsp_prev_stale(rq, prev) &&
			      prev->sched_class == &ext_sched_class &&
			      scx_task_on_sched(sch, prev);

		SCX_CALL_OP(sch, dispatch, rq, scx_cpu_arg(cpu),
			    prev_on_sch ? prev : NULL);

#ifdef CONFIG_EXT_SUB_SCHED
		if (!nested)
			rq->scx.sub_dispatch_prev = NULL;
#endif

		scx_flush_dispatch_buf(sch, rq);

		/* ops.dispatch() and the flush may have dropped the rq lock */
		if (!scx_dsp_prev_stale(rq, prev) &&
		    (prev->scx.flags & SCX_TASK_QUEUED) && prev->scx.slice)
			return SCX_DSP_PREV;
		if (rq->scx.local_dsq.nr)
			return SCX_DSP_LOCAL;
		if (scx_consume_global_dsq(sch, rq))
			return SCX_DSP_LOCAL;

		/*
		 * ops.dispatch() can trap us in this loop by repeatedly
		 * dispatching ineligible tasks. Break out once in a while to
		 * allow the watchdog to run. As IRQ can't be enabled in
		 * dispatch, we want to complete this scheduling cycle and then
		 * start a new one. IOW, we want to call resched_curr() on the
		 * next, most likely idle, task, not the current one. Use
		 * __scx_bpf_kick_cpu() for deferred kicking.
		 */
		if (unlikely(!--nr_loops)) {
			scx_kick_cpu(sch, cpu, 0);
			break;
		}
	} while (dspc->nr_tasks);

	/*
	 * Prevent the CPU from going idle while bypassed descendants have tasks
	 * queued. Without this fallback, bypassed tasks could stall if the host
	 * scheduler's ops.dispatch() doesn't yield any tasks.
	 */
	if (scx_bypass_dsp_enabled(sch) &&
	    scx_consume_dispatch_q(sch, rq, scx_bypass_dsq(sch, cpu), SCX_ENQ_IMMED)) {
		__scx_add_event(sch, SCX_EV_SUB_BYPASS_DISPATCH, 1);
		return SCX_DSP_LOCAL;
	}

	return SCX_DSP_NONE;
}

#endif /* _KERNEL_SCHED_EXT_INLINES_H */
