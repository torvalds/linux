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
#include <net/af_unix.h>

struct dentry;
struct landlock_domain;
struct landlock_hierarchy;
struct landlock_rule;
struct landlock_ruleset;
struct path;
struct sock;
struct task_struct;

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

/*
 * Fills the dense per-domain-layer array layers (one access mask per layer,
 * indexed by level - 1) from rule's sparse layer stack, keeping only the
 * requested rights (access_request).  Layers with no matching rule entry get
 * a zero mask.  Shared by the check_rule_fs and check_rule_net events.
 *
 * rule->layers is sorted by ascending level, with levels in the domain's
 * [1, num_layers] range (see landlock_merge_ruleset()), so every entry maps
 * to a slot.  A leftover entry would be a malformed rule; the zero-filled
 * slots keep the output and the array bounds safe regardless.
 */
static inline void
__trace_landlock_fill_layers(access_mask_t *const layers,
			     const size_t num_layers,
			     const struct landlock_rule *const rule,
			     const access_mask_t access_request)
{
	size_t i = 0;

	for (size_t level = 1; level <= num_layers; level++) {
		access_mask_t grants = 0;

		if (i < rule->num_layers && level == rule->layers[i].level) {
			grants = rule->layers[i].access & access_request;
			i++;
		}
		layers[level - 1] = grants;
	}

	/* A leftover entry means an out-of-range or unsorted rule level. */
	WARN_ON_ONCE(i < rule->num_layers);
}

/*
 * Renders the dense per-domain-layer access array as symbolic flag names for
 * the grants field: layers wrapped in "{}", flags within a layer joined by
 * "|", layers separated by ",", an empty layer rendered as nothing.
 * Open-codes the flag walk because trace_print_flags_seq() NUL-terminates per
 * call and so cannot be chained into a single field.  The shared names table
 * covers every access right, so masked bits are always named.  Returns the
 * trace_seq position like __print_flags().
 */
static inline const char *__trace_landlock_print_layers(
	struct trace_seq *p, const access_mask_t *const layers,
	const size_t num_layers, const struct trace_print_flags *const names,
	const size_t names_size)
{
	const char *const ret = trace_seq_buffer_ptr(p);

	trace_seq_putc(p, '{');
	for (size_t i = 0; i < num_layers; i++) {
		access_mask_t mask = layers[i];
		bool first = true;

		if (i)
			trace_seq_putc(p, ',');
		for (size_t j = 0; mask && j < names_size; j++) {
			if ((mask & names[j].mask) != names[j].mask)
				continue;
			if (!first)
				trace_seq_putc(p, '|');
			trace_seq_puts(p, names[j].name);
			mask &= ~names[j].mask;
			first = false;
		}
	}
	trace_seq_putc(p, '}');
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
 * Decision context
 * ~~~~~~~~~~~~~~~~
 *
 * A denial event, together with the lifecycle events, exposes the full
 * set of inputs the verdict consumed, so a consumer that tracked domain
 * creation (landlock_create_ruleset, landlock_create_domain) can verify
 * or reproduce the Landlock decision rather than merely observe it
 * happened.  In who/what/why terms: who is the denying domain (the domain
 * field, always the subject that enforced the policy, never the current
 * task), what is the operation and its object, and why is every other
 * input the verdict weighed.
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
 *
 * Rule-check fields
 * ~~~~~~~~~~~~~~~~~
 *
 * The check_rule events fire during an access check, once per matching
 * rule, before the final allow-or-deny verdict.  They share domain (the
 * enforcing domain being evaluated), access_request (the access mask being
 * checked), and rule (the matching rule, with per-layer access masks).
 *
 * Denial fields
 * ~~~~~~~~~~~~~
 *
 * Every denial event shares three fields.  domain is the ID of the
 * innermost domain that blocked the access.  same_exec tells whether the
 * current task is the same executable that entered that domain.  logged is
 * the domain's audit-logging decision for this denial (its log_status is
 * enabled and the per-execution flag selected by same_exec is set); a
 * stateless ftrace filter can select the denials the domain submits to
 * audit with logged==1, without reconstructing it from the per-execution
 * log flags.  Denial events order their fields as domain, same_exec,
 * logged, then blockers (deny_access events only), then the type-specific
 * object fields, then any variable-length field.
 *
 * Relational referents
 * ~~~~~~~~~~~~~~~~~~~~~
 *
 * A scope or ptrace verdict compares two domains, so the other party's
 * domain is part of the decision context.  It is exposed as a scalar
 * domain ID (0 when that party is unsandboxed): target_domain (signal),
 * peer_domain (abstract unix socket), tracee_domain (ptrace).  With both
 * IDs in the stream, a consumer that tracked domain creation can relate
 * the two parties without kernel-internal state.  The ID is a scalar
 * snapshot, not a live domain pointer that could dangle: an optional
 * relational referent is a scalar (0 sentinel), not a nullable pointer.
 */

/*
 * Prints a per-layer access mask array (the dynamic array @array) as symbolic
 * flag names using the shared @flag_names list (a _LANDLOCK_*_NAMES macro).
 * Stays outside CREATE_TRACE_POINTS: TP_printk is expanded in the print-output
 * pass where that macro is undefined.
 */
#define __print_landlock_layers(array, flag_names...)			\
	({								\
		static const struct trace_print_flags __layer_names[] = { \
			flag_names					\
		};							\
		__trace_landlock_print_layers(				\
			p, __get_dynamic_array(array),			\
			__get_dynamic_array_len(array) /		\
				sizeof(access_mask_t),			\
			__layer_names, ARRAY_SIZE(__layer_names));	\
	})

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

/**
 * landlock_create_domain - New domain created
 *
 * @domain: Newly created domain (never NULL, immutable after creation).
 *          @domain->hierarchy->id is its unique ID, shared with the
 *          landlock_enforce_domain and landlock_free_domain events;
 *          @domain->hierarchy->details holds the requesting process.
 * @ruleset: Source ruleset frozen into the domain (never NULL).  The
 *           ruleset lock is held across the emission, so a BPF program
 *           reading it via BTF sees the exact merged ruleset;
 *           @ruleset->id / @ruleset->version identify it.
 *
 * Emitted by sys_landlock_restrict_self() once, in the requesting
 * thread's context, right after the merge and before thread-sync.  The
 * flags-only path (ruleset_fd == -1) creates no domain and does not
 * emit this event.  Paired with the per-thread landlock_enforce_domain
 * (join on @domain->hierarchy->id) and balanced by a matching
 * landlock_free_domain event.
 */
TRACE_EVENT(landlock_create_domain,

	TP_PROTO(const struct landlock_domain *domain,
		 const struct landlock_ruleset *ruleset),

	TP_ARGS(domain, ruleset),

	TP_STRUCT__entry(
		__field(	__u64,		domain_id	)
		__field(	__u64,		parent_id	)
		__field(	__u64,		ruleset_id	)
		__field(	__u32,		ruleset_version	)
	),

	TP_fast_assign(
		lockdep_assert_held(&ruleset->lock);
		__entry->domain_id	= domain->hierarchy->id;
		__entry->parent_id	= domain->hierarchy->parent ?
					  domain->hierarchy->parent->id : 0;
		__entry->ruleset_id	= ruleset->id;
		__entry->ruleset_version = ruleset->version;
	),

	TP_printk("domain=%llx parent=%llx ruleset=%llx.%u",
		__entry->domain_id, __entry->parent_id,
		__entry->ruleset_id, __entry->ruleset_version)
);

/**
 * landlock_enforce_domain - Domain enforced on a thread
 *
 * @domain: Domain now enforced on the current thread (never NULL,
 *          immutable; read locklessly).  Correlate to
 *          landlock_create_domain via @domain->hierarchy->id for the
 *          source ruleset and requesting thread, or read
 *          @domain->hierarchy->details for the requesting process.
 * @complete: Set on the single event that concludes the operation, after
 *            all its other enforcements; filter on it for one event per
 *            operation.
 * @process_wide: The enforcement covers every eligible (non-exiting)
 *                thread of the process: set when the caller used
 *                %LANDLOCK_RESTRICT_SELF_TSYNC or the process is
 *                single-threaded.  A lone thread whose group still
 *                holds a zombie leader is not counted single-threaded,
 *                so process_wide == 0 never proves the opposite.
 * @no_new_privs: The enforcing thread's no_new_privs state at
 *                enforcement time: 1 if set (by a prior
 *                :manpage:`prctl(2)` %PR_SET_NO_NEW_PRIVS or by
 *                %LANDLOCK_RESTRICT_SELF_NO_NEW_PRIVS), 0 if the domain
 *                was enforced with %CAP_SYS_ADMIN instead.
 *
 * Emitted for each thread sys_landlock_restrict_self() enforces the
 * domain on, in that thread's own context, right after its
 * commit_creds(), so it fires only once the thread is irreversibly
 * enforcing the domain (aborted operations emit none).  Not
 * balanced; every enforcement falls between the domain's
 * landlock_create_domain and landlock_free_domain events.
 *
 * @complete == 1 && @process_wide == 1 means the whole process is
 * sandboxed by @domain, durably (Landlock domains are monotonic and
 * inherited on :manpage:`clone(2)`).
 */
TRACE_EVENT(landlock_enforce_domain,

	TP_PROTO(const struct landlock_domain *domain, bool complete,
		 bool process_wide, bool no_new_privs),

	TP_ARGS(domain, complete, process_wide, no_new_privs),

	TP_STRUCT__entry(
		__field(	__u64,		domain_id	)
		__field(	bool,		complete	)
		__field(	bool,		process_wide	)
		__field(	bool,		no_new_privs	)
	),

	TP_fast_assign(
		__entry->domain_id	= domain->hierarchy->id;
		__entry->complete	= complete;
		__entry->process_wide	= process_wide;
		__entry->no_new_privs	= no_new_privs;
	),

	TP_printk("domain=%llx complete=%d process_wide=%d no_new_privs=%d",
		__entry->domain_id, __entry->complete, __entry->process_wide,
		__entry->no_new_privs)
);

/**
 * landlock_free_domain - Domain freed
 *
 * @hierarchy: Hierarchy node being freed (never NULL).
 *
 * Emitted when the hierarchy node's last reference is dropped: its
 * refcount reaches zero after all child domains have released their
 * parent reference.  A committed domain is
 * freed from a kworker via landlock_put_domain_deferred() (the credential
 * free path runs in RCU context, where sleeping is forbidden), so the
 * current task is not the sandboxed task that triggered the free.  Balanced
 * by a matching landlock_create_domain event.
 */
TRACE_EVENT(landlock_free_domain,

	TP_PROTO(const struct landlock_hierarchy *hierarchy),

	TP_ARGS(hierarchy),

	TP_STRUCT__entry(
		__field(	__u64,		domain_id	)
		__field(	__u64,		denials		)
	),

	TP_fast_assign(
		__entry->domain_id	= hierarchy->id;
		__entry->denials	= atomic64_read(&hierarchy->num_denials);
	),

	TP_printk("domain=%llx denials=%llu",
		__entry->domain_id, __entry->denials)
);

/**
 * landlock_check_rule_fs - Filesystem rule evaluated during access check
 *
 * @domain: Enforcing domain (never NULL).
 * @rule: Matching rule with per-layer access masks (never NULL).
 * @access_request: Access mask evaluated against the rule (the domain's
 *                   handled mask during rename/link double-checks).
 * @dentry: Filesystem dentry being checked (never NULL).
 *
 * Emitted for each rule that matches during a filesystem access check.
 * The grants array shows the requested rights the rule grants at each
 * domain layer.  See Documentation/trace/events-landlock.rst for how to
 * interpret it.
 */
TRACE_EVENT(landlock_check_rule_fs,

	TP_PROTO(const struct landlock_domain *domain,
		 const struct landlock_rule *rule,
		 access_mask_t access_request, const struct dentry *dentry),

	TP_ARGS(domain, rule, access_request, dentry),

	TP_STRUCT__entry(
		__field(	__u64,		domain_id	)
		__field(	access_mask_t,	access_request	)
		__field(	dev_t,		dev		)
		__field(	ino_t,		ino		)
		__dynamic_array(access_mask_t,	grants,
				domain->num_layers)
	),

	TP_fast_assign(
		__entry->domain_id	= domain->hierarchy->id;
		__entry->access_request	= access_request;
		__entry->dev		= dentry->d_sb->s_dev;
		__entry->ino		= d_backing_inode(dentry)->i_ino;

		__trace_landlock_fill_layers(__get_dynamic_array(grants),
					     __get_dynamic_array_len(grants) /
						     sizeof(access_mask_t),
					     rule, access_request);
	),

	TP_printk("domain=%llx access_request=%s dev=%u:%u ino=%lu grants=%s",
		__entry->domain_id,
		__print_flags(__entry->access_request, "|", _LANDLOCK_ACCESS_FS_NAMES),
		MAJOR(__entry->dev), MINOR(__entry->dev), __entry->ino,
		__print_landlock_layers(grants, _LANDLOCK_ACCESS_FS_NAMES))
);

/**
 * landlock_check_rule_net - Network port rule evaluated during access check
 *
 * @domain: Enforcing domain (never NULL).
 * @rule: Matching rule with per-layer access masks (never NULL).
 * @access_request: Access mask being requested.
 * @port: Network port being checked (host endianness).
 *
 * Emitted for each rule that matches during a network access check.  The
 * grants array shows the requested rights the rule grants at each domain
 * layer.  See Documentation/trace/events-landlock.rst for how to
 * interpret it.
 */
TRACE_EVENT(landlock_check_rule_net,

	TP_PROTO(const struct landlock_domain *domain,
		 const struct landlock_rule *rule,
		 access_mask_t access_request, __u64 port),

	TP_ARGS(domain, rule, access_request, port),

	TP_STRUCT__entry(
		__field(	__u64,		domain_id	)
		__field(	access_mask_t,	access_request	)
		__field(	__u64,		port		)
		__dynamic_array(access_mask_t,	grants,
				domain->num_layers)
	),

	TP_fast_assign(
		__entry->domain_id	= domain->hierarchy->id;
		__entry->access_request	= access_request;
		__entry->port		= port;

		__trace_landlock_fill_layers(__get_dynamic_array(grants),
					     __get_dynamic_array_len(grants) /
						     sizeof(access_mask_t),
					     rule, access_request);
	),

	TP_printk("domain=%llx access_request=%s port=%llu grants=%s",
		__entry->domain_id,
		__print_flags(__entry->access_request, "|", _LANDLOCK_ACCESS_NET_NAMES),
		__entry->port,
		__print_landlock_layers(grants, _LANDLOCK_ACCESS_NET_NAMES))
);

/**
 * landlock_deny_access_fs - Filesystem access denied
 *
 * @hierarchy: Denying domain's hierarchy node (never NULL); its id is the
 *             domain field.
 * @same_exec: Whether the current task entered the denying domain itself.
 * @logged: The domain's audit-logging decision for this denial.
 * @blockers: Access mask that was blocked (zero for a mount-topology
 *            change, whose only blocker is the operation itself).
 * @path: Filesystem path that was denied (never NULL).
 * @pathname: Resolved path string (never NULL; an error placeholder on
 *            resolution failure).
 *
 * Emitted when a Landlock domain denies a filesystem access.
 */
TRACE_EVENT(landlock_deny_access_fs,

	TP_PROTO(const struct landlock_hierarchy *hierarchy, bool same_exec,
		 bool logged, access_mask_t blockers, const struct path *path,
		 const char *pathname),

	TP_ARGS(hierarchy, same_exec, logged, blockers, path, pathname),

	TP_STRUCT__entry(
		__field(	__u64,		domain_id	)
		__field(	bool,		same_exec	)
		__field(	bool,		logged		)
		__field(	access_mask_t,	blockers	)
		__field(	dev_t,		dev		)
		__field(	ino_t,		ino		)
		__string(	pathname,	pathname	)
	),

	TP_fast_assign(
		const struct inode *inode = d_backing_inode(path->dentry);

		__entry->domain_id	= hierarchy->id;
		__entry->same_exec	= same_exec;
		__entry->logged		= logged;
		__entry->blockers	= blockers;
		__entry->dev		= path->dentry->d_sb->s_dev;
		/*
		 * A negative dentry has no backing inode, so mirror the
		 * guard in dump_common_audit_data() and report inode 0.
		 */
		__entry->ino		= inode ? inode->i_ino : 0;
		__assign_str(pathname);
	),

	TP_printk("domain=%llx same_exec=%d logged=%d blockers=%s dev=%u:%u ino=%lu path=%s",
		__entry->domain_id, __entry->same_exec, __entry->logged,
		__print_flags(__entry->blockers, "|", _LANDLOCK_ACCESS_FS_NAMES),
		MAJOR(__entry->dev), MINOR(__entry->dev), __entry->ino,
		__trace_print_untrusted_str(p, __get_str(pathname),
					    __get_dynamic_array_len(pathname) - 1))
);

/**
 * landlock_deny_access_net - Network access denied
 *
 * @hierarchy: Denying domain's hierarchy node (never NULL); its id is the
 *             domain field.
 * @same_exec: Whether the current task entered the denying domain itself.
 * @logged: The domain's audit-logging decision for this denial.
 * @blockers: Access mask that was blocked.
 * @sk: Socket object (never NULL), read without a socket lock, so its
 *      fields are a best-effort snapshot.  The denied endpoint is not
 *      available: the hook runs before :manpage:`bind(2)` /
 *      :manpage:`connect(2)` sets the socket addresses.
 * @sport: Source port in host endianness, set for bind denials (zero for
 *         an autobind/ephemeral port); zero for connect and send denials.
 * @dport: Destination port in host endianness, set for connect and send
 *         denials; zero for bind denials, and also zero for a UDP send to
 *         an AF_UNSPEC address on an IPv6 socket (indistinguishable from a
 *         real destination port 0).  The bind-vs-connect direction is
 *         given by @blockers, not by which port is set.
 *
 * Emitted when a Landlock domain denies a network operation.
 *
 * The port fields are converted from the socket's network byte order to
 * host endianness before emitting.
 */
TRACE_EVENT(landlock_deny_access_net,

	TP_PROTO(const struct landlock_hierarchy *hierarchy, bool same_exec,
		 bool logged, access_mask_t blockers, const struct sock *sk,
		 __u64 sport, __u64 dport),

	TP_ARGS(hierarchy, same_exec, logged, blockers, sk, sport, dport),

	TP_STRUCT__entry(
		__field(	__u64,		domain_id	)
		__field(	bool,		same_exec	)
		__field(	bool,		logged		)
		__field(	access_mask_t,	blockers	)
		__field(	__u64,		sport		)
		__field(	__u64,		dport		)
	),

	TP_fast_assign(
		__entry->domain_id	= hierarchy->id;
		__entry->same_exec	= same_exec;
		__entry->logged		= logged;
		__entry->blockers	= blockers;
		__entry->sport		= sport;
		__entry->dport		= dport;
	),

	TP_printk("domain=%llx same_exec=%d logged=%d blockers=%s sport=%llu dport=%llu",
		__entry->domain_id, __entry->same_exec, __entry->logged,
		__print_flags(__entry->blockers, "|", _LANDLOCK_ACCESS_NET_NAMES),
		__entry->sport, __entry->dport)
);

/**
 * landlock_deny_ptrace - Ptrace access denied by a Landlock domain
 *
 * @hierarchy: Denying domain's hierarchy node (never NULL); its id is the
 *             domain field.
 * @same_exec: Whether the current task entered the denying domain itself.
 * @logged: The domain's audit-logging decision for this denial.
 * @tracee_domain_id: The tracee's Landlock domain ID, or 0 if the tracee
 *                    is unsandboxed.
 * @tracee: The target task ptrace acted on (never NULL).  tracee_pid is
 *          the init-namespace TGID (like audit's opid).
 *
 * Emitted when a Landlock domain denies a ptrace operation.
 */
TRACE_EVENT(landlock_deny_ptrace,

	TP_PROTO(const struct landlock_hierarchy *hierarchy, bool same_exec,
		 bool logged, u64 tracee_domain_id,
		 const struct task_struct *tracee),

	TP_ARGS(hierarchy, same_exec, logged, tracee_domain_id, tracee),

	TP_STRUCT__entry(
		__field(	__u64,		domain_id	)
		__field(	bool,		same_exec	)
		__field(	bool,		logged		)
		__field(	__u64,		tracee_domain_id)
		__field(	pid_t,		tracee_pid	)
		__string(	tracee_comm,	tracee->comm	)
	),

	TP_fast_assign(
		__entry->domain_id	= hierarchy->id;
		__entry->same_exec	= same_exec;
		__entry->logged		= logged;
		__entry->tracee_domain_id = tracee_domain_id;
		__entry->tracee_pid	= task_tgid_nr((struct task_struct *)tracee);
		__assign_str(tracee_comm);
	),

	TP_printk("domain=%llx same_exec=%d logged=%d tracee_domain=%llx tracee_pid=%d tracee_comm=%s",
		__entry->domain_id, __entry->same_exec, __entry->logged,
		__entry->tracee_domain_id, __entry->tracee_pid,
		__trace_print_untrusted_str(p, __get_str(tracee_comm),
					    __get_dynamic_array_len(tracee_comm) - 1))
);

/**
 * landlock_deny_scope_signal - Signal delivery denied by
 *                               LANDLOCK_SCOPE_SIGNAL
 *
 * @hierarchy: Denying domain's hierarchy node (never NULL); its id is the
 *             domain field.
 * @same_exec: Whether the current task entered the denying domain itself.
 * @logged: The domain's audit-logging decision for this denial.
 * @target_domain_id: The target's Landlock domain ID, or 0 if the target
 *                    is unsandboxed.
 * @target: The task the signal was aimed at (never NULL).  target_pid is
 *          the init-namespace TGID (like audit's opid).
 *
 * Emitted when a Landlock domain denies signal delivery to a scoped-out
 * target.
 */
TRACE_EVENT(landlock_deny_scope_signal,

	TP_PROTO(const struct landlock_hierarchy *hierarchy, bool same_exec,
		 bool logged, u64 target_domain_id,
		 const struct task_struct *target),

	TP_ARGS(hierarchy, same_exec, logged, target_domain_id, target),

	TP_STRUCT__entry(
		__field(	__u64,		domain_id	)
		__field(	bool,		same_exec	)
		__field(	bool,		logged		)
		__field(	__u64,		target_domain_id)
		__field(	pid_t,		target_pid	)
		__string(	target_comm,	target->comm	)
	),

	TP_fast_assign(
		__entry->domain_id	= hierarchy->id;
		__entry->same_exec	= same_exec;
		__entry->logged		= logged;
		__entry->target_domain_id = target_domain_id;
		__entry->target_pid	= task_tgid_nr((struct task_struct *)target);
		__assign_str(target_comm);
	),

	TP_printk("domain=%llx same_exec=%d logged=%d target_domain=%llx target_pid=%d target_comm=%s",
		__entry->domain_id, __entry->same_exec, __entry->logged,
		__entry->target_domain_id, __entry->target_pid,
		__trace_print_untrusted_str(p, __get_str(target_comm),
					    __get_dynamic_array_len(target_comm) - 1))
);

/**
 * landlock_deny_scope_abstract_unix_socket - Abstract unix socket access
 *     denied by LANDLOCK_SCOPE_ABSTRACT_UNIX_SOCKET
 *
 * @hierarchy: Denying domain's hierarchy node (never NULL); its id is the
 *             domain field.
 * @same_exec: Whether the current task entered the denying domain itself.
 * @logged: The domain's audit-logging decision for this denial.
 * @peer_domain_id: The peer's Landlock domain ID, or 0 if the peer is
 *                  unsandboxed.
 * @peer: Peer socket (never NULL).  peer_pid is best-effort: it is 0 for
 *        a datagram peer (no SO_PEERCRED), so sun_path is the reliable
 *        peer identifier.
 *
 * Emitted when a Landlock domain denies access to a scoped-out abstract
 * unix socket.
 */
TRACE_EVENT(landlock_deny_scope_abstract_unix_socket,

	TP_PROTO(const struct landlock_hierarchy *hierarchy, bool same_exec,
		 bool logged, u64 peer_domain_id, const struct sock *peer),

	TP_ARGS(hierarchy, same_exec, logged, peer_domain_id, peer),

	TP_STRUCT__entry(
		__field(	__u64,		domain_id	)
		__field(	bool,		same_exec	)
		__field(	bool,		logged		)
		__field(	__u64,		peer_domain_id	)
		__field(	pid_t,		peer_pid	)
		/*
		 * Abstract socket names are untrusted binary data from
		 * user space.  Use __string_len because abstract names
		 * are not NUL-terminated; their length is determined by
		 * addr->len.  unix_sk(peer)->addr is stable here because
		 * the caller (hook_unix_stream_connect or
		 * hook_unix_may_send) holds unix_state_lock(peer).
		 */
		__string_len(	sun_path,
				unix_sk(peer)->addr ?
					unix_sk(peer)->addr->name->sun_path + 1 :
					"",
				unix_sk(peer)->addr ?
					unix_sk(peer)->addr->len -
						offsetof(struct sockaddr_un,
							 sun_path) - 1 :
					0)
	),

	TP_fast_assign(
		struct pid *peer_pid;

		lockdep_assert_held(&unix_sk(peer)->lock);
		__entry->domain_id	= hierarchy->id;
		__entry->same_exec	= same_exec;
		__entry->logged		= logged;
		__entry->peer_domain_id	= peer_domain_id;
		/*
		 * Best-effort (0 for a datagram peer).  sk_peer_pid is
		 * canonically guarded by sk->sk_peer_lock, but the target
		 * peer's peercred is set once and not updated concurrently in
		 * these hooks, so this READ_ONCE() is safe; sun_path is the
		 * reliable identifier.
		 */
		peer_pid		= READ_ONCE(peer->sk_peer_pid);
		__entry->peer_pid	= peer_pid ? pid_nr(peer_pid) : 0;
		__assign_str(sun_path);
	),

	TP_printk("domain=%llx same_exec=%d logged=%d peer_domain=%llx peer_pid=%d sun_path=%s",
		__entry->domain_id, __entry->same_exec, __entry->logged,
		__entry->peer_domain_id, __entry->peer_pid,
		__trace_print_untrusted_str(p, __get_str(sun_path),
					    __get_dynamic_array_len(sun_path) - 1))
);

#undef _LANDLOCK_NAME_ENTRY

#endif /* _TRACE_LANDLOCK_H */

/* This part must be outside protection */
#include <trace/define_trace.h>

/* clang-format on */
