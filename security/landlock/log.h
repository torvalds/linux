/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Landlock - Log helpers
 *
 * Copyright © 2023-2025 Microsoft Corporation
 * Copyright © 2026 Cloudflare, Inc.
 */

#ifndef _SECURITY_LANDLOCK_LOG_H
#define _SECURITY_LANDLOCK_LOG_H

#include <linux/lsm_audit.h>

#include "access.h"

struct landlock_cred_security;
struct landlock_hierarchy;
struct sockaddr;
struct task_struct;

enum landlock_request_type {
	LANDLOCK_REQUEST_PTRACE = 1,
	LANDLOCK_REQUEST_FS_CHANGE_TOPOLOGY,
	LANDLOCK_REQUEST_FS_ACCESS,
	LANDLOCK_REQUEST_NET_ACCESS,
	LANDLOCK_REQUEST_SCOPE_ABSTRACT_UNIX_SOCKET,
	LANDLOCK_REQUEST_SCOPE_SIGNAL,
};

struct landlock_blockers {
	access_mask_t access;
	enum landlock_request_type type;
};

#ifdef CONFIG_TRACEPOINTS

struct landlock_net_trace {
	const struct sockaddr *address;
	int addrlen;
	u16 socket_family;
};

struct landlock_ptrace_trace {
	u64 tracee_domain_id;
	const struct task_struct *tracer;
};

struct landlock_signal_trace {
	u64 target_domain_id;
	int signal;
};

#endif /* CONFIG_TRACEPOINTS */

/*
 * We should be careful to only use a variable of this type for
 * landlock_log_denial().  This way, the compiler can remove it entirely if
 * CONFIG_SECURITY_LANDLOCK_LOG is not set.
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
	const struct layer_masks *layer_masks;

	/* Required fields for requests with deny masks. */
	const access_mask_t all_existing_optional_access;
	deny_masks_t deny_masks;
	optional_access_t quiet_optional_accesses;

#ifdef CONFIG_TRACEPOINTS
	union {
		/*
		 * Other-party domain ID for an abstract UNIX socket scope
		 * denial, or 0 if that party is unsandboxed.  Store an ID, not
		 * a pointer: the other task can replace its credential and free
		 * the domain it referenced.
		 */
		u64 other_domain_id;

		/* Synchronous context for a network denial. */
		const struct landlock_net_trace *trace_net;
		/* Synchronous context for a ptrace denial. */
		const struct landlock_ptrace_trace *trace_ptrace;
		/* Synchronous context for a signal denial. */
		const struct landlock_signal_trace *trace_signal;
	};
#endif /* CONFIG_TRACEPOINTS */
};

#ifdef CONFIG_SECURITY_LANDLOCK_LOG

void landlock_log_free_domain(const struct landlock_hierarchy *const hierarchy);

void landlock_log_denial(const struct landlock_cred_security *const subject,
			 const struct landlock_request *const request);

#else /* CONFIG_SECURITY_LANDLOCK_LOG */

static inline void
landlock_log_free_domain(const struct landlock_hierarchy *const hierarchy)
{
}

static inline void
landlock_log_denial(const struct landlock_cred_security *const subject,
		    const struct landlock_request *const request)
{
}

#endif /* CONFIG_SECURITY_LANDLOCK_LOG */

#endif /* _SECURITY_LANDLOCK_LOG_H */
