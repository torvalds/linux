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
#include <linux/string.h>
#include <linux/string_helpers.h>
#include <linux/tracepoint.h>
#include <linux/trace_seq.h>

struct landlock_ruleset;
struct path;

#ifdef CREATE_TRACE_POINTS

/*
 * Escapes @len bytes of an untrusted string into the trace sequence @p so it
 * cannot inject field separators or control characters into the ftrace text
 * output, and can be unambiguously recovered.  Called from the TP_printk() of
 * the tracepoints that expose paths and process names.  @len is passed by the
 * caller (rather than derived with strlen()) so a name that is not
 * NUL-terminated or carries embedded NUL bytes (an abstract socket name) is
 * escaped in full instead of being truncated at the first NUL.
 *
 * Return: a pointer into @p's buffer, or NULL if @src is NULL or the buffer is
 * exhausted (normal when the trace buffer is full).
 */
static inline const char *
__trace_print_untrusted_str(struct trace_seq *p, const char *src, size_t len)
{
	int escaped_size;
	char *buf;
	size_t buf_size = seq_buf_get_buf(&p->seq, &buf);
	const char *ret = trace_seq_buffer_ptr(p);

	/* Buffer exhaustion is normal when the trace buffer is full. */
	if (!src || buf_size == 0)
		return NULL;

	escaped_size =
		string_escape_mem(src, len, buf, buf_size,
				  ESCAPE_SPACE | ESCAPE_SPECIAL | ESCAPE_NAP |
					  ESCAPE_APPEND | ESCAPE_OCTAL,
				  " ='\"\\");
	if (unlikely(escaped_size >= buf_size)) {
		/* We need some room for the final '\0'. */
		seq_buf_set_overflow(&p->seq);
		p->full = 1;
		return NULL;
	}
	seq_buf_commit(&p->seq, escaped_size);
	trace_seq_putc(p, 0);
	return ret;
}

#endif /* CREATE_TRACE_POINTS */

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
 *
 * Field encoding
 * ~~~~~~~~~~~~~~
 *
 * Fields that mirror the Landlock UAPI use the same C types and endianness
 * (e.g. network ports are __u64 in host endianness, like
 * landlock_net_port_attr.port).  Per-event details, such as where a value
 * is byte-swapped, live in the field's own kdoc.
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
		__field(	__u32,		ruleset_version	)
		__field(	access_mask_t,	handled_fs	)
		__field(	access_mask_t,	handled_net	)
		__field(	access_mask_t,	scoped		)
	),

	TP_fast_assign(
		__entry->ruleset_id	= ruleset->id;
		__entry->ruleset_version = ruleset->version;
		__entry->handled_fs	= ruleset->handled_masks.fs;
		__entry->handled_net	= ruleset->handled_masks.net;
		__entry->scoped		= ruleset->handled_masks.scope;
	),

	TP_printk("ruleset=%llx.%u handled_fs=%s handled_net=%s scoped=%s",
		__entry->ruleset_id, __entry->ruleset_version,
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
		__field(	__u32,		ruleset_version	)
	),

	TP_fast_assign(
		__entry->ruleset_id	= ruleset->id;
		__entry->ruleset_version = ruleset->version;
	),

	TP_printk("ruleset=%llx.%u",
		__entry->ruleset_id, __entry->ruleset_version)
);

/**
 * landlock_add_rule_fs - Filesystem rule added to a ruleset
 *
 * @ruleset: Source ruleset (never NULL).
 * @access_rights: Effective access mask stored in the rule, not the raw
 *                 sys_landlock_add_rule() argument (unhandled rights
 *                 added).
 * @path: Filesystem path for the rule (never NULL).
 * @pathname: Resolved absolute path string (never NULL; error placeholder
 *            on resolution failure).
 *
 * Emitted by sys_landlock_add_rule() under the modified ruleset's lock, so
 * the reported ruleset is a stable snapshot that no concurrent writer can
 * change.
 */
TRACE_EVENT(landlock_add_rule_fs,

	TP_PROTO(const struct landlock_ruleset *ruleset,
		 access_mask_t access_rights, const struct path *path,
		 const char *pathname),

	TP_ARGS(ruleset, access_rights, path, pathname),

	TP_STRUCT__entry(
		__field(	__u64,		ruleset_id	)
		__field(	__u32,		ruleset_version	)
		__field(	access_mask_t,	access_rights	)
		__field(	dev_t,		dev		)
		__field(	ino_t,		ino		)
		__string(	pathname,	pathname	)
	),

	TP_fast_assign(
		lockdep_assert_held(&ruleset->lock);
		__entry->ruleset_id	= ruleset->id;
		__entry->ruleset_version = ruleset->version;
		__entry->access_rights	= access_rights;
		__entry->dev		= path->dentry->d_sb->s_dev;
		/*
		 * The inode number may not be the user-visible one,
		 * but it will be the same used by audit.
		 */
		__entry->ino		= d_backing_inode(path->dentry)->i_ino;
		__assign_str(pathname);
	),

	TP_printk("ruleset=%llx.%u access_rights=%s dev=%u:%u ino=%lu path=%s",
		__entry->ruleset_id, __entry->ruleset_version,
		__print_flags(__entry->access_rights, "|", _LANDLOCK_ACCESS_FS_NAMES),
		MAJOR(__entry->dev), MINOR(__entry->dev), __entry->ino,
		__trace_print_untrusted_str(p, __get_str(pathname),
					    __get_dynamic_array_len(pathname) - 1))
);

/**
 * landlock_add_rule_net - Network port rule added to a ruleset
 *
 * @ruleset: Source ruleset (never NULL).
 * @access_rights: Effective access mask stored in the rule, not the raw
 *                 sys_landlock_add_rule() argument (unhandled rights
 *                 added).
 * @port: Network port, the landlock_net_port_attr.port UAPI value
 *        forwarded directly.
 *
 * Emitted by sys_landlock_add_rule() under the modified ruleset's lock, so
 * the reported ruleset is a stable snapshot that no concurrent writer can
 * change.
 */
TRACE_EVENT(landlock_add_rule_net,

	TP_PROTO(const struct landlock_ruleset *ruleset,
		 access_mask_t access_rights, __u64 port),

	TP_ARGS(ruleset, access_rights, port),

	TP_STRUCT__entry(
		__field(	__u64,		ruleset_id	)
		__field(	__u32,		ruleset_version	)
		__field(	access_mask_t,	access_rights	)
		__field(	__u64,		port		)
	),

	TP_fast_assign(
		lockdep_assert_held(&ruleset->lock);
		__entry->ruleset_id	= ruleset->id;
		__entry->ruleset_version = ruleset->version;
		__entry->access_rights	= access_rights;
		__entry->port		= port;
	),

	TP_printk("ruleset=%llx.%u access_rights=%s port=%llu",
		__entry->ruleset_id, __entry->ruleset_version,
		__print_flags(__entry->access_rights, "|", _LANDLOCK_ACCESS_NET_NAMES),
		__entry->port)
);

#undef _LANDLOCK_NAME_ENTRY

#endif /* _TRACE_LANDLOCK_H */

/* This part must be outside protection */
#include <trace/define_trace.h>

/* clang-format on */
