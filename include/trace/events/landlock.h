/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright © 2025 Microsoft Corporation
 * Copyright © 2026 Cloudflare, Inc.
 */

#undef TRACE_SYSTEM
#define TRACE_SYSTEM landlock

#if !defined(_TRACE_LANDLOCK_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_LANDLOCK_H

#include <linux/landlock.h>
#include <linux/tracepoint.h>

struct landlock_ruleset;

/* clang-format off */

/* Maps a shared _LANDLOCK_*_NAMES entry to a __print_flags() pair. */
#define _LANDLOCK_NAME_ENTRY(mask, name) { mask, name }

/**
 * DOC: Landlock trace events
 *
 * These guarantees and constraints hold for every Landlock tracepoint.
 * A new tracepoint must uphold them, and an eBPF consumer can rely on
 * them.
 *
 * Lifecycle consistency
 * ~~~~~~~~~~~~~~~~~~~~~~
 *
 * Lifecycle events are balanced: a creation event always has a matching
 * deallocation event and vice versa, so an eBPF program can model object
 * lifetimes from the trace stream without reconciliation logic.  A creation
 * event fires while the object is still private to the calling thread
 * (landlock_create_ruleset fires before the ruleset's file descriptor is
 * installed, so it cannot race a concurrent :manpage:`close(2)`); if fd
 * installation later fails and the ruleset is freed, free_ruleset still
 * fires, keeping the pair balanced.  The domain pair (create_domain and
 * free_domain) is balanced the same way: create_domain fires when the
 * domain is created (under the ruleset lock, before thread-sync), and
 * free_domain fires when it is freed.  A rare thread-sync failure aborts
 * the just-created domain, which then emits both events (its creation, then
 * an immediate free).  Denial events fire only for denials that actually
 * happen.
 *
 * Pointer access
 * ~~~~~~~~~~~~~~
 *
 * All pointer arguments in TP_PROTO are guaranteed non-NULL by the
 * caller, but pointers reached through them may still be NULL (e.g.,
 * hierarchy->parent at a root domain) and must be checked.  eBPF programs
 * read these pointers via BTF for richer introspection than the
 * TP_STRUCT__entry fields, which serve TP_printk display only.
 *
 * Mutable object pointers are passed while the caller holds the object's
 * lock, so TP_fast_assign and a BTF reader see the exact object the event
 * reports, a snapshot no concurrent writer can change: add_rule holds the
 * modified ruleset's lock, and create_domain holds the ruleset lock across
 * the emission (before the thread-sync wait) so the inspected ruleset is
 * the one merged into the domain.  Objects immutable at the emission site
 * (a domain after creation, a hierarchy at its last reference) need no
 * lock.  A few values that no held lock protects are a best-effort
 * lockless snapshot instead: a task's comm, and the deny_access_net struct
 * sock (whose network hook holds no socket lock), matching how the sched
 * and signal trace events sample comm.
 */

/**
 * landlock_create_ruleset - New ruleset created
 *
 * @ruleset: Newly created ruleset (never NULL); not yet shared via an fd,
 *           so no lock is needed.
 *
 * Emitted by sys_landlock_create_ruleset() while the new ruleset is still
 * private to the calling thread, before its file descriptor is installed,
 * so it cannot race a concurrent :manpage:`close(2)`.  Balanced by a
 * matching landlock_free_ruleset event.
 */
TRACE_EVENT(landlock_create_ruleset,

	TP_PROTO(const struct landlock_ruleset *ruleset),

	TP_ARGS(ruleset),

	TP_STRUCT__entry(
		__field(	__u64,		ruleset_id	)
		__field(	access_mask_t,	handled_fs	)
		__field(	access_mask_t,	handled_net	)
		__field(	access_mask_t,	scoped		)
	),

	TP_fast_assign(
		__entry->ruleset_id	= ruleset->id;
		__entry->handled_fs	= ruleset->handled_masks.fs;
		__entry->handled_net	= ruleset->handled_masks.net;
		__entry->scoped		= ruleset->handled_masks.scope;
	),

	TP_printk("ruleset=%llx handled_fs=%s handled_net=%s scoped=%s",
		__entry->ruleset_id,
		__print_flags(__entry->handled_fs, "|", _LANDLOCK_ACCESS_FS_NAMES),
		__print_flags(__entry->handled_net, "|", _LANDLOCK_ACCESS_NET_NAMES),
		__print_flags(__entry->scoped, "|", _LANDLOCK_SCOPE_NAMES))
);

/**
 * landlock_free_ruleset - Ruleset freed
 *
 * @ruleset: Ruleset being freed (never NULL); at its last reference, so no
 *           lock is needed.
 *
 * Emitted when a ruleset's last reference is dropped (typically when
 * the creating process closes the ruleset file descriptor).  Fires even
 * when file-descriptor installation failed after creation, keeping the
 * create/free pair balanced.
 */
TRACE_EVENT(landlock_free_ruleset,

	TP_PROTO(const struct landlock_ruleset *ruleset),

	TP_ARGS(ruleset),

	TP_STRUCT__entry(
		__field(	__u64,		ruleset_id	)
	),

	TP_fast_assign(
		__entry->ruleset_id	= ruleset->id;
	),

	TP_printk("ruleset=%llx", __entry->ruleset_id)
);

#undef _LANDLOCK_NAME_ENTRY

#endif /* _TRACE_LANDLOCK_H */

/* This part must be outside protection */
#include <trace/define_trace.h>

/* clang-format on */
