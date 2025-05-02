/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Landlock - Audit helpers
 *
 * Copyright © 2023-2025 Microsoft Corporation
 */

#ifndef _SECURITY_LANDLOCK_AUDIT_H
#define _SECURITY_LANDLOCK_AUDIT_H

#include <linux/audit.h>
#include <linux/lsm_audit.h>

#include "access.h"
#include "cred.h"

enum landlock_request_type {
	LANDLOCK_REQUEST_PTRACE = 1,
	LANDLOCK_REQUEST_FS_CHANGE_TOPOLOGY,
	LANDLOCK_REQUEST_FS_ACCESS,
	LANDLOCK_REQUEST_NET_ACCESS,
	LANDLOCK_REQUEST_SCOPE_ABSTRACT_UNIX_SOCKET,
	LANDLOCK_REQUEST_SCOPE_SIGNAL,
};

/*
 * We should be careful to only use a variable of this type for
 * landlock_log_denial().  This way, the compiler can remove it entirely if
 * CONFIG_AUDIT is not set.
 */
struct landlock_request {
	/* Mandatory fields. */
	enum landlock_request_type type;
	struct common_audit_data audit;

	/**
	 * layer_plus_one: First layer level that denies the request + 1.  The
	 * extra one is useful to detect uninitialized field.
	 */
	size_t layer_plus_one;

	/* Required field for configurable access control. */
	access_mask_t access;

	/* Required fields for requests with layer masks. */
	const layer_mask_t (*layer_masks)[];
	size_t layer_masks_size;

	/* Required fields for requests with deny masks. */
	const access_mask_t all_existing_optional_access;
	deny_masks_t deny_masks;
};

#ifdef CONFIG_AUDIT

void landlock_log_drop_domain(const struct landlock_hierarchy *const hierarchy);

void landlock_log_denial(const struct landlock_cred_security *const subject,
			 const struct landlock_request *const request);

#else /* CONFIG_AUDIT */

static inline void
landlock_log_drop_domain(const struct landlock_hierarchy *const hierarchy)
{
}

static inline void
landlock_log_denial(const struct landlock_cred_security *const subject,
		    const struct landlock_request *const request)
{
}

#endif /* CONFIG_AUDIT */

#endif /* _SECURITY_LANDLOCK_AUDIT_H */
