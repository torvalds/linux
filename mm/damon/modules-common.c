// SPDX-License-Identifier: GPL-2.0
/*
 * Common Primitives for DAMON Modules
 *
 * Author: SeongJae Park <sjpark@amazon.de>
 */

#include <linux/damon.h>

#include "modules-common.h"

/*
 * Allocate, set, and return a DAMON context for the physical address space.
 * @ctxp:	Pointer to save the point to the newly created context
 * @targetp:	Pointer to save the point to the newly created target
 */
int damon_modules_new_paddr_ctx_target(struct damon_ctx **ctxp,
		struct damon_target **targetp)
{
	struct damon_ctx *ctx;
	struct damon_target *target;

	ctx = damon_new_ctx();
	if (!ctx)
		return -ENOMEM;

	if (damon_select_ops(ctx, DAMON_OPS_PADDR)) {
		damon_destroy_ctx(ctx);
		return -EINVAL;
	}

	target = damon_new_target();
	if (!target) {
		damon_destroy_ctx(ctx);
		return -ENOMEM;
	}
	damon_add_target(ctx, target);

	*ctxp = ctx;
	*targetp = target;
	return 0;
}
