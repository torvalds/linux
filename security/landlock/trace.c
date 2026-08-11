// SPDX-License-Identifier: GPL-2.0-only
/*
 * Landlock - Tracepoint helpers
 *
 * Copyright © 2025 Microsoft Corporation
 * Copyright © 2026 Cloudflare, Inc.
 */

#include <linux/cleanup.h>
#include <linux/dcache.h>
#include <linux/err.h>
#include <linux/fs.h>
#include <linux/lsm_audit.h>
#include <net/sock.h>

#include "access.h"
#include "domain.h"
#include "fs.h"
#include "log.h"
#include "ruleset.h"
#include "trace.h"

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

/**
 * landlock_trace_denial - Emit a tracepoint for a denied access request
 *
 * @request: Detail of the user space request.
 * @youngest_denied: The youngest hierarchy node that denied the access.
 * @missing: The set of denied access rights.
 * @same_exec: Whether the current task is the same executable that called
 *             landlock_restrict_self() for the denying domain, as computed
 *             by landlock_log_denial().
 * @logged: Whether the domain's policy selects this denial for logging, as
 *          computed by landlock_log_denial().
 *
 * Emits the tracepoint matching @request->type when its event is enabled.
 * Unlike audit, fires regardless of @logged; the value is recorded in the event
 * so consumers can filter on it.
 *
 * Called from landlock_log_denial().
 */
void landlock_trace_denial(
	const struct landlock_request *const request,
	const struct landlock_hierarchy *const youngest_denied,
	const access_mask_t missing, const bool same_exec, const bool logged)
{
	switch (request->type) {
	case LANDLOCK_REQUEST_FS_ACCESS:
	case LANDLOCK_REQUEST_FS_CHANGE_TOPOLOGY:
		if (trace_landlock_deny_access_fs_enabled()) {
			char *buf __free(__putname) = __getname();
			struct path dentry_path;
			const char *pathname;
			const struct path *path = NULL;

			/*
			 * Selects the path from the audit data type, as
			 * dump_common_audit_data() does.  A FS_ACCESS denial
			 * carries a file (hook_file_truncate) or an ioctl op
			 * (hook_file_ioctl) rather than a path;
			 * FS_CHANGE_TOPOLOGY carries a path or a bare dentry.
			 * Reading the wrong union member would dereference
			 * garbage, so every reachable type is handled here.
			 */
			switch (request->audit.type) {
			case LSM_AUDIT_DATA_FILE:
				path = &request->audit.u.file->f_path;
				break;
			case LSM_AUDIT_DATA_IOCTL_OP:
				path = &request->audit.u.op->path;
				break;
			case LSM_AUDIT_DATA_DENTRY:
				/*
				 * Build a path on the stack with the real
				 * dentry so TP_fast_assign can extract dev and
				 * ino; the mnt field is unused there.
				 */
				dentry_path = (struct path){
					.dentry = request->audit.u.dentry,
				};
				path = &dentry_path;
				break;
			case LSM_AUDIT_DATA_PATH:
				path = &request->audit.u.path;
				break;
			default:
				WARN_ONCE(1,
					  "Unhandled Landlock FS audit type %d",
					  request->audit.type);
				break;
			}

			if (!path)
				break;

			if (!buf) {
				pathname = "<no_mem>";
			} else if (request->audit.type ==
				   LSM_AUDIT_DATA_DENTRY) {
				/* No vfsmount: render the dentry path alone. */
				pathname = dentry_path_raw(
					request->audit.u.dentry, buf, PATH_MAX);
				if (IS_ERR(pathname))
					pathname =
						PTR_ERR(pathname) ==
								-ENAMETOOLONG ?
							"<too_long>" :
							"<unreachable>";
			} else {
				pathname = resolve_path_for_trace(path, buf);
			}

			trace_landlock_deny_access_fs(youngest_denied,
						      same_exec, logged,
						      missing, path, pathname);
		}
		break;
	case LANDLOCK_REQUEST_NET_ACCESS:
		if (trace_landlock_deny_access_net_enabled())
			trace_landlock_deny_access_net(
				youngest_denied, same_exec, logged, missing,
				request->audit.u.net->sk,
				ntohs(request->audit.u.net->sport),
				ntohs(request->audit.u.net->dport));
		break;
	case LANDLOCK_REQUEST_PTRACE:
		if (trace_landlock_deny_ptrace_enabled())
			trace_landlock_deny_ptrace(youngest_denied, same_exec,
						   logged,
						   request->other_domain_id,
						   request->audit.u.tsk);
		break;
	case LANDLOCK_REQUEST_SCOPE_SIGNAL:
		if (trace_landlock_deny_scope_signal_enabled())
			trace_landlock_deny_scope_signal(
				youngest_denied, same_exec, logged,
				request->other_domain_id, request->audit.u.tsk);
		break;
	case LANDLOCK_REQUEST_SCOPE_ABSTRACT_UNIX_SOCKET:
		if (trace_landlock_deny_scope_abstract_unix_socket_enabled())
			trace_landlock_deny_scope_abstract_unix_socket(
				youngest_denied, same_exec, logged,
				request->other_domain_id,
				request->audit.u.net->sk);
		break;
	default:
		WARN_ONCE(1, "Unhandled Landlock request type %d",
			  request->type);
		break;
	}
}
