// SPDX-License-Identifier: MIT
/*
 * Copyright © 2015-2021 Intel Corporation
 */

#include <linux/kthread.h>
#include <linux/string_helpers.h>
#include <trace/events/dma_fence.h>
#ifdef notyet
#include <uapi/linux/sched/types.h>
#endif

#include "i915_drv.h"
#include "i915_trace.h"
#include "intel_breadcrumbs.h"
#include "intel_context.h"
#include "intel_engine_pm.h"
#include "intel_gt_pm.h"
#include "intel_gt_requests.h"

static bool irq_enable(struct intel_breadcrumbs *b)
{
	return intel_engine_irq_enable(b->irq_engine);
}

static void irq_disable(struct intel_breadcrumbs *b)
{
	intel_engine_irq_disable(b->irq_engine);
}

static void __intel_breadcrumbs_arm_irq(struct intel_breadcrumbs *b)
{
	intel_wakeref_t wakeref;

	/*
	 * Since we are waiting on a request, the GPU should be busy
	 * and should have its own rpm reference.
	 */
	wakeref = intel_gt_pm_get_if_awake(b->irq_engine->gt);
	if (GEM_WARN_ON(!wakeref))
		return;

	/*
	 * The breadcrumb irq will be disarmed on the interrupt after the
	 * waiters are signaled. This gives us a single interrupt window in
	 * which we can add a new waiter and avoid the cost of re-enabling
	 * the irq.
	 */
	WRITE_ONCE(b->irq_armed, wakeref);

	/* Requests may have completed before we could enable the interrupt. */
	if (!b->irq_enabled++ && b->irq_enable(b))
		irq_work_queue(&b->irq_work);
}

static void intel_breadcrumbs_arm_irq(struct intel_breadcrumbs *b)
{
	if (!b->irq_engine)
		return;

	spin_lock(&b->irq_lock);
	if (!b->irq_armed)
		__intel_breadcrumbs_arm_irq(b);
	spin_unlock(&b->irq_lock);
}

static void __intel_breadcrumbs_disarm_irq(struct intel_breadcrumbs *b)
{
	intel_wakeref_t wakeref = b->irq_armed;

	GEM_BUG_ON(!b->irq_enabled);
	if (!--b->irq_enabled)
		b->irq_disable(b);

	WRITE_ONCE(b->irq_armed, 0);
	intel_gt_pm_put_async(b->irq_engine->gt, wakeref);
}

static void intel_breadcrumbs_disarm_irq(struct intel_breadcrumbs *b)
{
	spin_lock(&b->irq_lock);
	if (b->irq_armed)
		__intel_breadcrumbs_disarm_irq(b);
	spin_unlock(&b->irq_lock);
}

static void add_signaling_context(struct intel_breadcrumbs *b,
				  struct intel_context *ce)
{
	lockdep_assert_held(&ce->signal_lock);

	spin_lock(&b->signalers_lock);
	list_add_rcu(&ce->signal_link, &b->signalers);
	spin_unlock(&b->signalers_lock);
}

static bool remove_signaling_context(struct intel_breadcrumbs *b,
				     struct intel_context *ce)
{
	lockdep_assert_held(&ce->signal_lock);

	if (!list_empty(&ce->signals))
		return false;

	spin_lock(&b->signalers_lock);
	list_del_rcu(&ce->signal_link);
	spin_unlock(&b->signalers_lock);

	return true;
}

__maybe_unused static bool
check_signal_order(struct intel_context *ce, struct i915_request *rq)
{
	if (rq->context != ce)
		return false;

	if (!list_is_last(&rq->signal_link, &ce->signals) &&
	    i915_seqno_passed(rq->fence.seqno,
			      list_next_entry(rq, signal_link)->fence.seqno))
		return false;

	if (!list_is_first(&rq->signal_link, &ce->signals) &&
	    i915_seqno_passed(list_prev_entry(rq, signal_link)->fence.seqno,
			      rq->fence.seqno))
		return false;

	return true;
}

static bool
__dma_fence_signal(struct dma_fence *fence)
{
	return !test_and_set_bit(DMA_FENCE_FLAG_SIGNALED_BIT, &fence->flags);
}

static void
__dma_fence_signal__timestamp(struct dma_fence *fence, ktime_t timestamp)
{
	fence->timestamp = timestamp;
	set_bit(DMA_FENCE_FLAG_TIMESTAMP_BIT, &fence->flags);
	trace_dma_fence_signaled(fence);
}

static void
__dma_fence_signal__notify(struct dma_fence *fence,
			   const struct list_head *list)
{
	struct dma_fence_cb *cur, *tmp;

	lockdep_assert_held(fence->lock);

	list_for_each_entry_safe(cur, tmp, list, node) {
		INIT_LIST_HEAD(&cur->node);
		cur->func(fence, cur);
	}
}

static void add_retire(struct intel_breadcrumbs *b, struct intel_timeline *tl)
{
	if (b->irq_engine)
		intel_engine_add_retire(b->irq_engine, tl);
}

static struct llist_node *
slist_add(struct llist_node *node, struct llist_node *head)
{
	node->next = head;
	return node;
}

static void signal_irq_work(struct irq_work *work)
{
	struct intel_breadcrumbs *b = container_of(work, typeof(*b), irq_work);
	const ktime_t timestamp = ktime_get();
	struct llist_node *signal, *sn;
	struct intel_context *ce;

	signal = NULL;
	if (unlikely(!llist_empty(&b->signaled_requests)))
		signal = llist_del_all(&b->signaled_requests);

	/*
	 * Keep the irq armed until the interrupt after all listeners are gone.
	 *
	 * Enabling/disabling the interrupt is rather costly, roughly a couple
	 * of hundred microseconds. If we are proactive and enable/disable
	 * the interrupt around every request that wants a breadcrumb, we
	 * quickly drown in the extra orders of magnitude of latency imposed
	 * on request submission.
	 *
	 * So we try to be lazy, and keep the interrupts enabled until no
	 * more listeners appear within a breadcrumb interrupt interval (that
	 * is until a request completes that no one cares about). The
	 * observation is that listeners come in batches, and will often
	 * listen to a bunch of requests in succession. Though note on icl+,
	 * interrupts are always enabled due to concerns with rc6 being
	 * dysfunctional with per-engine interrupt masking.
	 *
	 * We also try to avoid raising too many interrupts, as they may
	 * be generated by userspace batches and it is unfortunately rather
	 * too easy to drown the CPU under a flood of GPU interrupts. Thus
	 * whenever no one appears to be listening, we turn off the interrupts.
	 * Fewer interrupts should conserve power -- at the very least, fewer
	 * interrupt draw less ire from other users of the system and tools
	 * like powertop.
	 */
	if (!signal && READ_ONCE(b->irq_armed) && list_empty(&b->signalers))
		intel_breadcrumbs_disarm_irq(b);

	rcu_read_lock();
	atomic_inc(&b->signaler_active);
	list_for_each_entry_rcu(ce, &b->signalers, signal_link) {
		struct i915_request *rq;

		list_for_each_entry_rcu(rq, &ce->signals, signal_link) {
			bool release;

			if (!__i915_request_is_complete(rq))
				break;

			if (!test_and_clear_bit(I915_FENCE_FLAG_SIGNAL,
						&rq->fence.flags))
				break;

			/*
			 * Queue for execution after dropping the signaling
			 * spinlock as the callback chain may end up adding
			 * more signalers to the same context or engine.
			 */
			spin_lock(&ce->signal_lock);
			list_del_rcu(&rq->signal_link);
			release = remove_signaling_context(b, ce);
			spin_unlock(&ce->signal_lock);
			if (release) {
				if (intel_timeline_is_last(ce->timeline, rq))
					add_retire(b, ce->timeline);
				intel_context_put(ce);
			}

			if (__dma_fence_signal(&rq->fence))
				/* We own signal_node now, xfer to local list */
				signal = slist_add(&rq->signal_node, signal);
			else
				i915_request_put(rq);
		}
	}
	atomic_dec(&b->signaler_active);
	rcu_read_unlock();

	llist_for_each_safe(signal, sn, signal) {
		struct i915_request *rq =
			llist_entry(signal, typeof(*rq), signal_node);
		struct list_head cb_list;

		if (rq->engine->sched_engine->retire_inflight_request_prio)
			rq->engine->sched_engine->retire_inflight_request_prio(rq);

		spin_lock(&rq->lock);
		list_replace(&rq->fence.cb_list, &cb_list);
		__dma_fence_signal__timestamp(&rq->fence, timestamp);
		__dma_fence_signal__notify(&rq->fence, &cb_list);
		spin_unlock(&rq->lock);

		i915_request_put(rq);
	}

	/* Lazy irq enabling after HW submission */
	if (!READ_ONCE(b->irq_armed) && !list_empty(&b->signalers))
		intel_breadcrumbs_arm_irq(b);

	/* And confirm that we still want irqs enabled before we yield */
	if (READ_ONCE(b->irq_armed) && !atomic_read(&b->active))
		intel_breadcrumbs_disarm_irq(b);
}

struct intel_breadcrumbs *
intel_breadcrumbs_create(struct intel_engine_cs *irq_engine)
{
	struct intel_breadcrumbs *b;

	b = kzalloc(sizeof(*b), GFP_KERNEL);
	if (!b)
		return NULL;

	kref_init(&b->ref);

	mtx_init(&b->signalers_lock, IPL_TTY);
	INIT_LIST_HEAD(&b->signalers);
	init_llist_head(&b->signaled_requests);

	mtx_init(&b->irq_lock, IPL_TTY);
	init_irq_work(&b->irq_work, signal_irq_work);

	b->irq_engine = irq_engine;
	b->irq_enable = irq_enable;
	b->irq_disable = irq_disable;

	return b;
}

void intel_breadcrumbs_reset(struct intel_breadcrumbs *b)
{
	unsigned long flags;

	if (!b->irq_engine)
		return;

	spin_lock_irqsave(&b->irq_lock, flags);

	if (b->irq_enabled)
		b->irq_enable(b);
	else
		b->irq_disable(b);

	spin_unlock_irqrestore(&b->irq_lock, flags);
}

void __intel_breadcrumbs_park(struct intel_breadcrumbs *b)
{
	if (!READ_ONCE(b->irq_armed))
		return;

	/* Kick the work once more to drain the signalers, and disarm the irq */
	irq_work_queue(&b->irq_work);
}

void intel_breadcrumbs_free(struct kref *kref)
{
	struct intel_breadcrumbs *b = container_of(kref, typeof(*b), ref);

	irq_work_sync(&b->irq_work);
	GEM_BUG_ON(!list_empty(&b->signalers));
	GEM_BUG_ON(b->irq_armed);

	kfree(b);
}

static void irq_signal_request(struct i915_request *rq,
			       struct intel_breadcrumbs *b)
{
	if (!__dma_fence_signal(&rq->fence))
		return;

	i915_request_get(rq);
	if (llist_add(&rq->signal_node, &b->signaled_requests))
		irq_work_queue(&b->irq_work);
}

static void insert_breadcrumb(struct i915_request *rq)
{
	struct intel_breadcrumbs *b = READ_ONCE(rq->engine)->breadcrumbs;
	struct intel_context *ce = rq->context;
	struct list_head *pos;

	if (test_bit(I915_FENCE_FLAG_SIGNAL, &rq->fence.flags))
		return;

	/*
	 * If the request is already completed, we can transfer it
	 * straight onto a signaled list, and queue the irq worker for
	 * its signal completion.
	 */
	if (__i915_request_is_complete(rq)) {
		irq_signal_request(rq, b);
		return;
	}

	if (list_empty(&ce->signals)) {
		intel_context_get(ce);
		add_signaling_context(b, ce);
		pos = &ce->signals;
	} else {
		/*
		 * We keep the seqno in retirement order, so we can break
		 * inside intel_engine_signal_breadcrumbs as soon as we've
		 * passed the last completed request (or seen a request that
		 * hasn't event started). We could walk the timeline->requests,
		 * but keeping a separate signalers_list has the advantage of
		 * hopefully being much smaller than the full list and so
		 * provides faster iteration and detection when there are no
		 * more interrupts required for this context.
		 *
		 * We typically expect to add new signalers in order, so we
		 * start looking for our insertion point from the tail of
		 * the list.
		 */
		list_for_each_prev(pos, &ce->signals) {
			struct i915_request *it =
				list_entry(pos, typeof(*it), signal_link);

			if (i915_seqno_passed(rq->fence.seqno, it->fence.seqno))
				break;
		}
	}

	i915_request_get(rq);
	list_add_rcu(&rq->signal_link, pos);
	GEM_BUG_ON(!check_signal_order(ce, rq));
	GEM_BUG_ON(test_bit(DMA_FENCE_FLAG_SIGNALED_BIT, &rq->fence.flags));
	set_bit(I915_FENCE_FLAG_SIGNAL, &rq->fence.flags);

	/*
	 * Defer enabling the interrupt to after HW submission and recheck
	 * the request as it may have completed and raised the interrupt as
	 * we were attaching it into the lists.
	 */
	if (!READ_ONCE(b->irq_armed) || __i915_request_is_complete(rq))
		irq_work_queue(&b->irq_work);
}

bool i915_request_enable_breadcrumb(struct i915_request *rq)
{
	struct intel_context *ce = rq->context;

	/* Serialises with i915_request_retire() using rq->lock */
	if (test_bit(DMA_FENCE_FLAG_SIGNALED_BIT, &rq->fence.flags))
		return true;

	/*
	 * Peek at i915_request_submit()/i915_request_unsubmit() status.
	 *
	 * If the request is not yet active (and not signaled), we will
	 * attach the breadcrumb later.
	 */
	if (!test_bit(I915_FENCE_FLAG_ACTIVE, &rq->fence.flags))
		return true;

	spin_lock(&ce->signal_lock);
	if (test_bit(I915_FENCE_FLAG_ACTIVE, &rq->fence.flags))
		insert_breadcrumb(rq);
	spin_unlock(&ce->signal_lock);

	return true;
}

void i915_request_cancel_breadcrumb(struct i915_request *rq)
{
	struct intel_breadcrumbs *b = READ_ONCE(rq->engine)->breadcrumbs;
	struct intel_context *ce = rq->context;
	bool release;

	spin_lock(&ce->signal_lock);
	if (!test_and_clear_bit(I915_FENCE_FLAG_SIGNAL, &rq->fence.flags)) {
		spin_unlock(&ce->signal_lock);
		return;
	}

	list_del_rcu(&rq->signal_link);
	release = remove_signaling_context(b, ce);
	spin_unlock(&ce->signal_lock);
	if (release)
		intel_context_put(ce);

	if (__i915_request_is_complete(rq))
		irq_signal_request(rq, b);

	i915_request_put(rq);
}

void intel_context_remove_breadcrumbs(struct intel_context *ce,
				      struct intel_breadcrumbs *b)
{
	struct i915_request *rq, *rn;
	bool release = false;
	unsigned long flags;

	spin_lock_irqsave(&ce->signal_lock, flags);

	if (list_empty(&ce->signals))
		goto unlock;

	list_for_each_entry_safe(rq, rn, &ce->signals, signal_link) {
		GEM_BUG_ON(!__i915_request_is_complete(rq));
		if (!test_and_clear_bit(I915_FENCE_FLAG_SIGNAL,
					&rq->fence.flags))
			continue;

		list_del_rcu(&rq->signal_link);
		irq_signal_request(rq, b);
		i915_request_put(rq);
	}
	release = remove_signaling_context(b, ce);

unlock:
	spin_unlock_irqrestore(&ce->signal_lock, flags);
	if (release)
		intel_context_put(ce);

	while (atomic_read(&b->signaler_active))
		cpu_relax();
}

static void print_signals(struct intel_breadcrumbs *b, struct drm_printer *p)
{
	struct intel_context *ce;
	struct i915_request *rq;

	drm_printf(p, "Signals:\n");

	rcu_read_lock();
	list_for_each_entry_rcu(ce, &b->signalers, signal_link) {
		list_for_each_entry_rcu(rq, &ce->signals, signal_link)
			drm_printf(p, "\t[%llx:%llx%s] @ %dms\n",
				   rq->fence.context, rq->fence.seqno,
				   __i915_request_is_complete(rq) ? "!" :
				   __i915_request_has_started(rq) ? "*" :
				   "",
				   jiffies_to_msecs(jiffies - rq->emitted_jiffies));
	}
	rcu_read_unlock();
}

void intel_engine_print_breadcrumbs(struct intel_engine_cs *engine,
				    struct drm_printer *p)
{
	struct intel_breadcrumbs *b;

	b = engine->breadcrumbs;
	if (!b)
		return;

	drm_printf(p, "IRQ: %s\n", str_enabled_disabled(b->irq_armed));
	if (!list_empty(&b->signalers))
		print_signals(b, p);
}
