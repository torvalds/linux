/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Landlock - Audit helpers
 *
 * Copyright © 2023-2025 Microsoft Corporation
 */

#ifndef _SECURITY_LANDLOCK_AUDIT_H
#define _SECURITY_LANDLOCK_AUDIT_H

#include <linux/types.h>

#include "access.h"

struct landlock_hierarchy;
struct landlock_request;

#ifdef CONFIG_AUDIT

void landlock_audit_denial(const struct landlock_request *const request,
			   struct landlock_hierarchy *const youngest_denied,
			   const access_mask_t missing, const bool logged);

void landlock_audit_free_domain(
	const struct landlock_hierarchy *const hierarchy);

#else /* CONFIG_AUDIT */

static inline void
landlock_audit_denial(const struct landlock_request *const request,
		      struct landlock_hierarchy *const youngest_denied,
		      const access_mask_t missing, const bool logged)
{
}

static inline void
landlock_audit_free_domain(const struct landlock_hierarchy *const hierarchy)
{
}

#endif /* CONFIG_AUDIT */

#endif /* _SECURITY_LANDLOCK_AUDIT_H */
