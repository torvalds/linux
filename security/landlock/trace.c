// SPDX-License-Identifier: GPL-2.0-only
/*
 * Landlock - Tracepoint helpers
 *
 * Copyright © 2025 Microsoft Corporation
 * Copyright © 2026 Cloudflare, Inc.
 */

#include "trace.h"
#include "domain.h"
#include "ruleset.h"

/*
 * Generates the tracepoint definitions in this translation unit.  The trace
 * event header dereferences the traced objects in TP_fast_assign, so the full
 * struct definitions (e.g. ruleset.h, domain.h) must be included before it.
 */
#define CREATE_TRACE_POINTS
#include <trace/events/landlock.h>

/**
 * landlock_trace_free_domain - Emit a tracepoint on domain deallocation
 *
 * @hierarchy: The domain's hierarchy being deallocated.
 *
 * Fires only for a hierarchy whose creation event was emitted, i.e. one that
 * left LANDLOCK_LOG_UNCOMMITTED in landlock_restrict_self().  This keeps the
 * create/free pair balanced: a hierarchy that never became observable is freed
 * silently, while a domain that landlock_restrict_self() created and a
 * thread-sync failure then aborted still fires free_domain, because its
 * creation event already fired.
 *
 * Called from landlock_log_free_domain().
 */
void landlock_trace_free_domain(const struct landlock_hierarchy *const hierarchy)
{
	/*
	 * The log_status read is a correctness guard (keep the create/free pair
	 * balanced), not a cost guard, so this cold path needs no
	 * trace_..._enabled() check: the tracepoint is a static-branch no-op
	 * when disabled.  The denial path guards trace_..._enabled() instead
	 * because it does expensive __getname()/path work before emitting.
	 */
	if (READ_ONCE(hierarchy->log_status) != LANDLOCK_LOG_UNCOMMITTED)
		trace_landlock_free_domain(hierarchy);
}
