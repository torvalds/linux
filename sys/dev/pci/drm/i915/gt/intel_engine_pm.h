/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2019 Intel Corporation
 */

#ifndef INTEL_ENGINE_PM_H
#define INTEL_ENGINE_PM_H

#include "i915_drv.h"
#include "i915_request.h"
#include "intel_engine_types.h"
#include "intel_wakeref.h"
#include "intel_gt.h"
#include "intel_gt_pm.h"

static inline bool
intel_engine_pm_is_awake(const struct intel_engine_cs *engine)
{
	return intel_wakeref_is_active(&engine->wakeref);
}

static inline void __intel_engine_pm_get(struct intel_engine_cs *engine)
{
	__intel_wakeref_get(&engine->wakeref);
}

static inline void intel_engine_pm_get(struct intel_engine_cs *engine)
{
	intel_wakeref_get(&engine->wakeref);
}

static inline bool intel_engine_pm_get_if_awake(struct intel_engine_cs *engine)
{
	return intel_wakeref_get_if_active(&engine->wakeref);
}

static inline void intel_engine_pm_might_get(struct intel_engine_cs *engine)
{
	if (!intel_engine_is_virtual(engine)) {
		intel_wakeref_might_get(&engine->wakeref);
	} else {
		struct intel_gt *gt = engine->gt;
		struct intel_engine_cs *tengine;
		intel_engine_mask_t tmp, mask = engine->mask;

		for_each_engine_masked(tengine, gt, mask, tmp)
			intel_wakeref_might_get(&tengine->wakeref);
	}
	intel_gt_pm_might_get(engine->gt);
}

static inline void intel_engine_pm_put(struct intel_engine_cs *engine)
{
	intel_wakeref_put(&engine->wakeref);
}

static inline void intel_engine_pm_put_async(struct intel_engine_cs *engine)
{
	intel_wakeref_put_async(&engine->wakeref);
}

static inline void intel_engine_pm_put_delay(struct intel_engine_cs *engine,
					     unsigned long delay)
{
	intel_wakeref_put_delay(&engine->wakeref, delay);
}

static inline void intel_engine_pm_flush(struct intel_engine_cs *engine)
{
	intel_wakeref_unlock_wait(&engine->wakeref);
}

static inline void intel_engine_pm_might_put(struct intel_engine_cs *engine)
{
	if (!intel_engine_is_virtual(engine)) {
		intel_wakeref_might_put(&engine->wakeref);
	} else {
		struct intel_gt *gt = engine->gt;
		struct intel_engine_cs *tengine;
		intel_engine_mask_t tmp, mask = engine->mask;

		for_each_engine_masked(tengine, gt, mask, tmp)
			intel_wakeref_might_put(&tengine->wakeref);
	}
	intel_gt_pm_might_put(engine->gt);
}

static inline struct i915_request *
intel_engine_create_kernel_request(struct intel_engine_cs *engine)
{
	struct i915_request *rq;

	/*
	 * The engine->kernel_context is special as it is used inside
	 * the engine-pm barrier (see __engine_park()), circumventing
	 * the usual mutexes and relying on the engine-pm barrier
	 * instead. So whenever we use the engine->kernel_context
	 * outside of the barrier, we must manually handle the
	 * engine wakeref to serialise with the use inside.
	 */
	intel_engine_pm_get(engine);
	rq = i915_request_create(engine->kernel_context);
	intel_engine_pm_put(engine);

	return rq;
}

void intel_engine_init__pm(struct intel_engine_cs *engine);

void intel_engine_reset_pinned_contexts(struct intel_engine_cs *engine);

#endif /* INTEL_ENGINE_PM_H */
