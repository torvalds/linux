/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Landlock - Tracepoint helpers
 *
 * Copyright © 2025 Microsoft Corporation
 * Copyright © 2026 Cloudflare, Inc.
 */

#ifndef _SECURITY_LANDLOCK_TRACE_H
#define _SECURITY_LANDLOCK_TRACE_H

#include "access.h"

struct landlock_hierarchy;
struct landlock_request;

#ifdef CONFIG_TRACEPOINTS

void landlock_trace_free_domain(
	const struct landlock_hierarchy *const hierarchy);

void landlock_trace_denial(
	const struct landlock_request *const request,
	const struct landlock_hierarchy *const youngest_denied,
	const access_mask_t missing, const bool same_exec, const bool logged);

#else /* CONFIG_TRACEPOINTS */

static inline void
landlock_trace_free_domain(const struct landlock_hierarchy *const hierarchy)
{
}

static inline void
landlock_trace_denial(const struct landlock_request *const request,
		      const struct landlock_hierarchy *const youngest_denied,
		      const access_mask_t missing, const bool same_exec,
		      const bool logged)
{
}

#endif /* CONFIG_TRACEPOINTS */

#endif /* _SECURITY_LANDLOCK_TRACE_H */
