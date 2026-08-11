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

struct landlock_cred_security;
struct landlock_hierarchy;
struct landlock_request;

#ifdef CONFIG_AUDIT

void landlock_audit_denial(const struct landlock_cred_security *const subject,
			   const struct landlock_request *const request,
			   struct landlock_hierarchy *const youngest_denied,
			   const size_t youngest_layer,
			   const access_mask_t missing,
			   const bool object_quiet_flag);

void landlock_audit_free_domain(
	const struct landlock_hierarchy *const hierarchy);

#else /* CONFIG_AUDIT */

static inline void
landlock_audit_denial(const struct landlock_cred_security *const subject,
		      const struct landlock_request *const request,
		      struct landlock_hierarchy *const youngest_denied,
		      const size_t youngest_layer, const access_mask_t missing,
		      const bool object_quiet_flag)
{
}

static inline void
landlock_audit_free_domain(const struct landlock_hierarchy *const hierarchy)
{
}

#endif /* CONFIG_AUDIT */

#endif /* _SECURITY_LANDLOCK_AUDIT_H */
