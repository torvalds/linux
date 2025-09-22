// SPDX-License-Identifier: MIT
/*
 * Copyright © 2018 Intel Corporation
 */

#include <linux/sort.h>

#include "i915_drv.h"

#include "intel_gt_requests.h"
#include "i915_selftest.h"
#include "selftest_engine_heartbeat.h"

static void reset_heartbeat(struct intel_engine_cs *engine)
{
	intel_engine_set_heartbeat(engine,
				   engine->defaults.heartbeat_interval_ms);
}

static int timeline_sync(struct intel_timeline *tl)
{
	struct dma_fence *fence;
	long timeout;

	fence = i915_active_fence_get(&tl->last_request);
	if (!fence)
		return 0;

	timeout = dma_fence_wait_timeout(fence, true, HZ / 2);
	dma_fence_put(fence);
	if (timeout < 0)
		return timeout;

	return 0;
}

static int engine_sync_barrier(struct intel_engine_cs *engine)
{
	return timeline_sync(engine->kernel_context->timeline);
}

struct pulse {
	struct i915_active active;
	struct kref kref;
};

static int pulse_active(struct i915_active *active)
{
	kref_get(&container_of(active, struct pulse, active)->kref);
	return 0;
}

static void pulse_free(struct kref *kref)
{
	struct pulse *p = container_of(kref, typeof(*p), kref);

	i915_active_fini(&p->active);
	kfree(p);
}

static void pulse_put(struct pulse *p)
{
	kref_put(&p->kref, pulse_free);
}

static void pulse_retire(struct i915_active *active)
{
	pulse_put(container_of(active, struct pulse, active));
}

static struct pulse *pulse_create(void)
{
	struct pulse *p;

	p = kmalloc(sizeof(*p), GFP_KERNEL);
	if (!p)
		return p;

	kref_init(&p->kref);
	i915_active_init(&p->active, pulse_active, pulse_retire, 0);

	return p;
}

static void pulse_unlock_wait(struct pulse *p)
{
	wait_var_event_timeout(&p->active, i915_active_is_idle(&p->active), HZ);
}

static int __live_idle_pulse(struct intel_engine_cs *engine,
			     int (*fn)(struct intel_engine_cs *cs))
{
	struct pulse *p;
	int err;

	GEM_BUG_ON(!intel_engine_pm_is_awake(engine));

	p = pulse_create();
	if (!p)
		return -ENOMEM;

	err = i915_active_acquire(&p->active);
	if (err)
		goto out;

	err = i915_active_acquire_preallocate_barrier(&p->active, engine);
	if (err) {
		i915_active_release(&p->active);
		goto out;
	}

	i915_active_acquire_barrier(&p->active);
	i915_active_release(&p->active);

	GEM_BUG_ON(i915_active_is_idle(&p->active));
	GEM_BUG_ON(llist_empty(&engine->barrier_tasks));

	err = fn(engine);
	if (err)
		goto out;

	GEM_BUG_ON(!llist_empty(&engine->barrier_tasks));

	if (engine_sync_barrier(engine)) {
		struct drm_printer m = drm_err_printer(&engine->i915->drm, "pulse");

		drm_printf(&m, "%s: no heartbeat pulse?\n", engine->name);
		intel_engine_dump(engine, &m, "%s", engine->name);

		err = -ETIME;
		goto out;
	}

	GEM_BUG_ON(READ_ONCE(engine->serial) != engine->wakeref_serial);

	pulse_unlock_wait(p); /* synchronize with the retirement callback */

	if (!i915_active_is_idle(&p->active)) {
		struct drm_printer m = drm_err_printer(&engine->i915->drm, "pulse");

		drm_printf(&m, "%s: heartbeat pulse did not flush idle tasks\n",
			   engine->name);
		i915_active_print(&p->active, &m);

		err = -EINVAL;
		goto out;
	}

out:
	pulse_put(p);
	return err;
}

static int live_idle_flush(void *arg)
{
	struct intel_gt *gt = arg;
	struct intel_engine_cs *engine;
	enum intel_engine_id id;
	int err = 0;

	/* Check that we can flush the idle barriers */

	for_each_engine(engine, gt, id) {
		st_engine_heartbeat_disable(engine);
		err = __live_idle_pulse(engine, intel_engine_flush_barriers);
		st_engine_heartbeat_enable(engine);
		if (err)
			break;
	}

	return err;
}

static int live_idle_pulse(void *arg)
{
	struct intel_gt *gt = arg;
	struct intel_engine_cs *engine;
	enum intel_engine_id id;
	int err = 0;

	/* Check that heartbeat pulses flush the idle barriers */

	for_each_engine(engine, gt, id) {
		st_engine_heartbeat_disable(engine);
		err = __live_idle_pulse(engine, intel_engine_pulse);
		st_engine_heartbeat_enable(engine);
		if (err && err != -ENODEV)
			break;

		err = 0;
	}

	return err;
}

static int __live_heartbeat_off(struct intel_engine_cs *engine)
{
	int err;

	intel_engine_pm_get(engine);

	engine->serial++;
	flush_delayed_work(&engine->heartbeat.work);
	if (!delayed_work_pending(&engine->heartbeat.work)) {
		pr_err("%s: heartbeat not running\n",
		       engine->name);
		err = -EINVAL;
		goto err_pm;
	}

	err = intel_engine_set_heartbeat(engine, 0);
	if (err)
		goto err_pm;

	engine->serial++;
	flush_delayed_work(&engine->heartbeat.work);
	if (delayed_work_pending(&engine->heartbeat.work)) {
		pr_err("%s: heartbeat still running\n",
		       engine->name);
		err = -EINVAL;
		goto err_beat;
	}

	if (READ_ONCE(engine->heartbeat.systole)) {
		pr_err("%s: heartbeat still allocated\n",
		       engine->name);
		err = -EINVAL;
		goto err_beat;
	}

err_beat:
	reset_heartbeat(engine);
err_pm:
	intel_engine_pm_put(engine);
	return err;
}

static int live_heartbeat_off(void *arg)
{
	struct intel_gt *gt = arg;
	struct intel_engine_cs *engine;
	enum intel_engine_id id;
	int err = 0;

	/* Check that we can turn off heartbeat and not interrupt VIP */
	if (!CONFIG_DRM_I915_HEARTBEAT_INTERVAL)
		return 0;

	for_each_engine(engine, gt, id) {
		if (!intel_engine_has_preemption(engine))
			continue;

		err = __live_heartbeat_off(engine);
		if (err)
			break;
	}

	return err;
}

int intel_heartbeat_live_selftests(struct drm_i915_private *i915)
{
	static const struct i915_subtest tests[] = {
		SUBTEST(live_idle_flush),
		SUBTEST(live_idle_pulse),
		SUBTEST(live_heartbeat_off),
	};
	int saved_hangcheck;
	int err;

	if (intel_gt_is_wedged(to_gt(i915)))
		return 0;

	saved_hangcheck = i915->params.enable_hangcheck;
	i915->params.enable_hangcheck = INT_MAX;

	err = intel_gt_live_subtests(tests, to_gt(i915));

	i915->params.enable_hangcheck = saved_hangcheck;
	return err;
}

void st_engine_heartbeat_disable(struct intel_engine_cs *engine)
{
	engine->props.heartbeat_interval_ms = 0;

	intel_engine_pm_get(engine);
	intel_engine_park_heartbeat(engine);
}

void st_engine_heartbeat_enable(struct intel_engine_cs *engine)
{
	intel_engine_pm_put(engine);

	engine->props.heartbeat_interval_ms =
		engine->defaults.heartbeat_interval_ms;
}

void st_engine_heartbeat_disable_no_pm(struct intel_engine_cs *engine)
{
	engine->props.heartbeat_interval_ms = 0;

	/*
	 * Park the heartbeat but without holding the PM lock as that
	 * makes the engines appear not-idle. Note that if/when unpark
	 * is called due to the PM lock being acquired later the
	 * heartbeat still won't be enabled because of the above = 0.
	 */
	if (intel_engine_pm_get_if_awake(engine)) {
		intel_engine_park_heartbeat(engine);
		intel_engine_pm_put(engine);
	}
}

void st_engine_heartbeat_enable_no_pm(struct intel_engine_cs *engine)
{
	engine->props.heartbeat_interval_ms =
		engine->defaults.heartbeat_interval_ms;
}
