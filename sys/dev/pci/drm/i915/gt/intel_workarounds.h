/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2014-2018 Intel Corporation
 */

#ifndef _INTEL_WORKAROUNDS_H_
#define _INTEL_WORKAROUNDS_H_

#include <linux/slab.h>

#include "intel_workarounds_types.h"

struct drm_i915_private;
struct i915_request;
struct intel_engine_cs;
struct intel_gt;

static inline void intel_wa_list_free(struct i915_wa_list *wal)
{
	kfree(wal->list);
	memset(wal, 0, sizeof(*wal));
}

void intel_engine_init_ctx_wa(struct intel_engine_cs *engine);
int intel_engine_emit_ctx_wa(struct i915_request *rq);

void intel_gt_init_workarounds(struct intel_gt *gt);
void intel_gt_apply_workarounds(struct intel_gt *gt);
bool intel_gt_verify_workarounds(struct intel_gt *gt, const char *from);

void intel_engine_init_whitelist(struct intel_engine_cs *engine);
void intel_engine_apply_whitelist(struct intel_engine_cs *engine);

void intel_engine_init_workarounds(struct intel_engine_cs *engine);
void intel_engine_apply_workarounds(struct intel_engine_cs *engine);
int intel_engine_verify_workarounds(struct intel_engine_cs *engine,
				    const char *from);

#endif
