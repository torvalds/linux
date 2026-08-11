/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Landlock - Tracepoint helpers
 *
 * Copyright © 2025 Microsoft Corporation
 * Copyright © 2026 Cloudflare, Inc.
 */

#ifndef _SECURITY_LANDLOCK_TRACE_H
#define _SECURITY_LANDLOCK_TRACE_H

struct landlock_hierarchy;

#ifdef CONFIG_TRACEPOINTS

void landlock_trace_free_domain(
	const struct landlock_hierarchy *const hierarchy);

#else /* CONFIG_TRACEPOINTS */

static inline void
landlock_trace_free_domain(const struct landlock_hierarchy *const hierarchy)
{
}

#endif /* CONFIG_TRACEPOINTS */

#endif /* _SECURITY_LANDLOCK_TRACE_H */
