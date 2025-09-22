// SPDX-License-Identifier: MIT
/*
 * Copyright © 2016 Intel Corporation
 */

#include "gem/i915_gem_context.h"
#include "gt/intel_ring.h"

#include "i915_drv.h"
#include "intel_context.h"
#include "intel_engine_pm.h"

#include "mock_engine.h"
#include "selftests/mock_request.h"

static int mock_timeline_pin(struct intel_timeline *tl)
{
	int err;

	if (WARN_ON(!i915_gem_object_trylock(tl->hwsp_ggtt->obj, NULL)))
		return -EBUSY;

	err = intel_timeline_pin_map(tl);
	i915_gem_object_unlock(tl->hwsp_ggtt->obj);
	if (err)
		return err;

	atomic_inc(&tl->pin_count);
	return 0;
}

static void mock_timeline_unpin(struct intel_timeline *tl)
{
	GEM_BUG_ON(!atomic_read(&tl->pin_count));
	atomic_dec(&tl->pin_count);
}

static struct i915_vma *create_ring_vma(struct i915_ggtt *ggtt, int size)
{
	struct i915_address_space *vm = &ggtt->vm;
	struct drm_i915_private *i915 = vm->i915;
	struct drm_i915_gem_object *obj;
	struct i915_vma *vma;

	obj = i915_gem_object_create_internal(i915, size);
	if (IS_ERR(obj))
		return ERR_CAST(obj);

	vma = i915_vma_instance(obj, vm, NULL);
	if (IS_ERR(vma))
		goto err;

	return vma;

err:
	i915_gem_object_put(obj);
	return vma;
}

static struct intel_ring *mock_ring(struct intel_engine_cs *engine)
{
	const unsigned long sz = PAGE_SIZE;
	struct intel_ring *ring;

	ring = kzalloc(sizeof(*ring) + sz, GFP_KERNEL);
	if (!ring)
		return NULL;

	kref_init(&ring->ref);
	ring->size = sz;
	ring->effective_size = sz;
	ring->vaddr = (void *)(ring + 1);
	atomic_set(&ring->pin_count, 1);

	ring->vma = create_ring_vma(engine->gt->ggtt, PAGE_SIZE);
	if (IS_ERR(ring->vma)) {
		kfree(ring);
		return NULL;
	}

	intel_ring_update_space(ring);

	return ring;
}

static void mock_ring_free(struct intel_ring *ring)
{
	i915_vma_put(ring->vma);

	kfree(ring);
}

static struct i915_request *first_request(struct mock_engine *engine)
{
	return list_first_entry_or_null(&engine->hw_queue,
					struct i915_request,
					mock.link);
}

static void advance(struct i915_request *request)
{
	list_del_init(&request->mock.link);
	i915_request_mark_complete(request);
	GEM_BUG_ON(!i915_request_completed(request));

	intel_engine_signal_breadcrumbs(request->engine);
}

static void hw_delay_complete(struct timer_list *t)
{
	struct mock_engine *engine = from_timer(engine, t, hw_delay);
	struct i915_request *request;
	unsigned long flags;

	spin_lock_irqsave(&engine->hw_lock, flags);

	/* Timer fired, first request is complete */
	request = first_request(engine);
	if (request)
		advance(request);

	/*
	 * Also immediately signal any subsequent 0-delay requests, but
	 * requeue the timer for the next delayed request.
	 */
	while ((request = first_request(engine))) {
		if (request->mock.delay) {
			mod_timer(&engine->hw_delay,
				  jiffies + request->mock.delay);
			break;
		}

		advance(request);
	}

	spin_unlock_irqrestore(&engine->hw_lock, flags);
}

static void mock_context_unpin(struct intel_context *ce)
{
}

static void mock_context_post_unpin(struct intel_context *ce)
{
	i915_vma_unpin(ce->ring->vma);
}

static void mock_context_destroy(struct kref *ref)
{
	struct intel_context *ce = container_of(ref, typeof(*ce), ref);

	GEM_BUG_ON(intel_context_is_pinned(ce));

	if (test_bit(CONTEXT_ALLOC_BIT, &ce->flags)) {
		mock_ring_free(ce->ring);
		mock_timeline_unpin(ce->timeline);
	}

	intel_context_fini(ce);
	intel_context_free(ce);
}

static int mock_context_alloc(struct intel_context *ce)
{
	int err;

	ce->ring = mock_ring(ce->engine);
	if (!ce->ring)
		return -ENOMEM;

	ce->timeline = intel_timeline_create(ce->engine->gt);
	if (IS_ERR(ce->timeline)) {
		kfree(ce->engine);
		return PTR_ERR(ce->timeline);
	}

	err = mock_timeline_pin(ce->timeline);
	if (err) {
		intel_timeline_put(ce->timeline);
		ce->timeline = NULL;
		return err;
	}

	return 0;
}

static int mock_context_pre_pin(struct intel_context *ce,
				struct i915_gem_ww_ctx *ww, void **unused)
{
	return i915_vma_pin_ww(ce->ring->vma, ww, 0, 0, PIN_GLOBAL | PIN_HIGH);
}

static int mock_context_pin(struct intel_context *ce, void *unused)
{
	return 0;
}

static void mock_context_reset(struct intel_context *ce)
{
}

static const struct intel_context_ops mock_context_ops = {
	.alloc = mock_context_alloc,

	.pre_pin = mock_context_pre_pin,
	.pin = mock_context_pin,
	.unpin = mock_context_unpin,
	.post_unpin = mock_context_post_unpin,

	.enter = intel_context_enter_engine,
	.exit = intel_context_exit_engine,

	.reset = mock_context_reset,
	.destroy = mock_context_destroy,
};

static int mock_request_alloc(struct i915_request *request)
{
	INIT_LIST_HEAD(&request->mock.link);
	request->mock.delay = 0;

	return 0;
}

static int mock_emit_flush(struct i915_request *request,
			   unsigned int flags)
{
	return 0;
}

static u32 *mock_emit_breadcrumb(struct i915_request *request, u32 *cs)
{
	return cs;
}

static void mock_submit_request(struct i915_request *request)
{
	struct mock_engine *engine =
		container_of(request->engine, typeof(*engine), base);
	unsigned long flags;

	i915_request_submit(request);

	spin_lock_irqsave(&engine->hw_lock, flags);
	list_add_tail(&request->mock.link, &engine->hw_queue);
	if (list_is_first(&request->mock.link, &engine->hw_queue)) {
		if (request->mock.delay)
			mod_timer(&engine->hw_delay,
				  jiffies + request->mock.delay);
		else
			advance(request);
	}
	spin_unlock_irqrestore(&engine->hw_lock, flags);
}

static void mock_add_to_engine(struct i915_request *rq)
{
	lockdep_assert_held(&rq->engine->sched_engine->lock);
	list_move_tail(&rq->sched.link, &rq->engine->sched_engine->requests);
}

static void mock_remove_from_engine(struct i915_request *rq)
{
	struct intel_engine_cs *engine, *locked;

	/*
	 * Virtual engines complicate acquiring the engine timeline lock,
	 * as their rq->engine pointer is not stable until under that
	 * engine lock. The simple ploy we use is to take the lock then
	 * check that the rq still belongs to the newly locked engine.
	 */

	locked = READ_ONCE(rq->engine);
	spin_lock_irq(&locked->sched_engine->lock);
	while (unlikely(locked != (engine = READ_ONCE(rq->engine)))) {
		spin_unlock(&locked->sched_engine->lock);
		spin_lock(&engine->sched_engine->lock);
		locked = engine;
	}
	list_del_init(&rq->sched.link);
	spin_unlock_irq(&locked->sched_engine->lock);
}

static void mock_reset_prepare(struct intel_engine_cs *engine)
{
}

static void mock_reset_rewind(struct intel_engine_cs *engine, bool stalled)
{
	GEM_BUG_ON(stalled);
}

static void mock_reset_cancel(struct intel_engine_cs *engine)
{
	struct mock_engine *mock =
		container_of(engine, typeof(*mock), base);
	struct i915_request *rq;
	unsigned long flags;

	del_timer_sync(&mock->hw_delay);

	spin_lock_irqsave(&engine->sched_engine->lock, flags);

	/* Mark all submitted requests as skipped. */
	list_for_each_entry(rq, &engine->sched_engine->requests, sched.link)
		i915_request_put(i915_request_mark_eio(rq));
	intel_engine_signal_breadcrumbs(engine);

	/* Cancel and submit all pending requests. */
	list_for_each_entry(rq, &mock->hw_queue, mock.link) {
		if (i915_request_mark_eio(rq)) {
			__i915_request_submit(rq);
			i915_request_put(rq);
		}
	}
	INIT_LIST_HEAD(&mock->hw_queue);

	spin_unlock_irqrestore(&engine->sched_engine->lock, flags);
}

static void mock_reset_finish(struct intel_engine_cs *engine)
{
}

static void mock_engine_release(struct intel_engine_cs *engine)
{
	struct mock_engine *mock =
		container_of(engine, typeof(*mock), base);

	GEM_BUG_ON(timer_pending(&mock->hw_delay));

	i915_sched_engine_put(engine->sched_engine);
	intel_breadcrumbs_put(engine->breadcrumbs);

	intel_context_unpin(engine->kernel_context);
	intel_context_put(engine->kernel_context);

	intel_engine_fini_retire(engine);
}

struct intel_engine_cs *mock_engine(struct drm_i915_private *i915,
				    const char *name,
				    int id)
{
	struct mock_engine *engine;

	GEM_BUG_ON(id >= I915_NUM_ENGINES);
	GEM_BUG_ON(!to_gt(i915)->uncore);

	engine = kzalloc(sizeof(*engine) + PAGE_SIZE, GFP_KERNEL);
	if (!engine)
		return NULL;

	/* minimal engine setup for requests */
	engine->base.i915 = i915;
	engine->base.gt = to_gt(i915);
	engine->base.uncore = to_gt(i915)->uncore;
	snprintf(engine->base.name, sizeof(engine->base.name), "%s", name);
	engine->base.id = id;
	engine->base.mask = BIT(id);
	engine->base.legacy_idx = INVALID_ENGINE;
	engine->base.instance = id;
	engine->base.status_page.addr = (void *)(engine + 1);

	engine->base.cops = &mock_context_ops;
	engine->base.request_alloc = mock_request_alloc;
	engine->base.emit_flush = mock_emit_flush;
	engine->base.emit_fini_breadcrumb = mock_emit_breadcrumb;
	engine->base.submit_request = mock_submit_request;
	engine->base.add_active_request = mock_add_to_engine;
	engine->base.remove_active_request = mock_remove_from_engine;

	engine->base.reset.prepare = mock_reset_prepare;
	engine->base.reset.rewind = mock_reset_rewind;
	engine->base.reset.cancel = mock_reset_cancel;
	engine->base.reset.finish = mock_reset_finish;

	engine->base.release = mock_engine_release;

	to_gt(i915)->engine[id] = &engine->base;
	to_gt(i915)->engine_class[0][id] = &engine->base;

	/* fake hw queue */
	mtx_init(&engine->hw_lock, IPL_TTY);
	timer_setup(&engine->hw_delay, hw_delay_complete, 0);
	INIT_LIST_HEAD(&engine->hw_queue);

	intel_engine_add_user(&engine->base);

	return &engine->base;
}

int mock_engine_init(struct intel_engine_cs *engine)
{
	struct intel_context *ce;

	INIT_LIST_HEAD(&engine->pinned_contexts_list);

	engine->sched_engine = i915_sched_engine_create(ENGINE_MOCK);
	if (!engine->sched_engine)
		return -ENOMEM;
	engine->sched_engine->private_data = engine;

	intel_engine_init_execlists(engine);
	intel_engine_init__pm(engine);
	intel_engine_init_retire(engine);

	engine->breadcrumbs = intel_breadcrumbs_create(NULL);
	if (!engine->breadcrumbs)
		goto err_schedule;

	ce = create_kernel_context(engine);
	if (IS_ERR(ce))
		goto err_breadcrumbs;

	/* We insist the kernel context is using the status_page */
	engine->status_page.vma = ce->timeline->hwsp_ggtt;

	engine->kernel_context = ce;
	return 0;

err_breadcrumbs:
	intel_breadcrumbs_put(engine->breadcrumbs);
err_schedule:
	i915_sched_engine_put(engine->sched_engine);
	return -ENOMEM;
}

void mock_engine_flush(struct intel_engine_cs *engine)
{
	struct mock_engine *mock =
		container_of(engine, typeof(*mock), base);
	struct i915_request *request, *rn;

	del_timer_sync(&mock->hw_delay);

	spin_lock_irq(&mock->hw_lock);
	list_for_each_entry_safe(request, rn, &mock->hw_queue, mock.link)
		advance(request);
	spin_unlock_irq(&mock->hw_lock);
}

void mock_engine_reset(struct intel_engine_cs *engine)
{
}
