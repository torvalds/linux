// SPDX-License-Identifier: GPL-2.0-only
/*
 * Landlock - Audit helpers
 *
 * Copyright © 2023-2025 Microsoft Corporation
 */

#include <linux/audit.h>
#include <linux/bitops.h>
#include <linux/lsm_audit.h>
#include <linux/pid.h>
#include <uapi/linux/landlock.h>

#include "access.h"
#include "audit.h"
#include "common.h"
#include "cred.h"
#include "domain.h"
#include "limits.h"
#include "log.h"

static const char *const fs_access_strings[] = {
	[BIT_INDEX(LANDLOCK_ACCESS_FS_EXECUTE)] = "fs.execute",
	[BIT_INDEX(LANDLOCK_ACCESS_FS_WRITE_FILE)] = "fs.write_file",
	[BIT_INDEX(LANDLOCK_ACCESS_FS_READ_FILE)] = "fs.read_file",
	[BIT_INDEX(LANDLOCK_ACCESS_FS_READ_DIR)] = "fs.read_dir",
	[BIT_INDEX(LANDLOCK_ACCESS_FS_REMOVE_DIR)] = "fs.remove_dir",
	[BIT_INDEX(LANDLOCK_ACCESS_FS_REMOVE_FILE)] = "fs.remove_file",
	[BIT_INDEX(LANDLOCK_ACCESS_FS_MAKE_CHAR)] = "fs.make_char",
	[BIT_INDEX(LANDLOCK_ACCESS_FS_MAKE_DIR)] = "fs.make_dir",
	[BIT_INDEX(LANDLOCK_ACCESS_FS_MAKE_REG)] = "fs.make_reg",
	[BIT_INDEX(LANDLOCK_ACCESS_FS_MAKE_SOCK)] = "fs.make_sock",
	[BIT_INDEX(LANDLOCK_ACCESS_FS_MAKE_FIFO)] = "fs.make_fifo",
	[BIT_INDEX(LANDLOCK_ACCESS_FS_MAKE_BLOCK)] = "fs.make_block",
	[BIT_INDEX(LANDLOCK_ACCESS_FS_MAKE_SYM)] = "fs.make_sym",
	[BIT_INDEX(LANDLOCK_ACCESS_FS_REFER)] = "fs.refer",
	[BIT_INDEX(LANDLOCK_ACCESS_FS_TRUNCATE)] = "fs.truncate",
	[BIT_INDEX(LANDLOCK_ACCESS_FS_IOCTL_DEV)] = "fs.ioctl_dev",
	[BIT_INDEX(LANDLOCK_ACCESS_FS_RESOLVE_UNIX)] = "fs.resolve_unix",
};

static_assert(ARRAY_SIZE(fs_access_strings) == LANDLOCK_NUM_ACCESS_FS);

static const char *const net_access_strings[] = {
	[BIT_INDEX(LANDLOCK_ACCESS_NET_BIND_TCP)] = "net.bind_tcp",
	[BIT_INDEX(LANDLOCK_ACCESS_NET_CONNECT_TCP)] = "net.connect_tcp",
	[BIT_INDEX(LANDLOCK_ACCESS_NET_BIND_UDP)] = "net.bind_udp",
	[BIT_INDEX(LANDLOCK_ACCESS_NET_CONNECT_SEND_UDP)] =
		"net.connect_send_udp",
};

static_assert(ARRAY_SIZE(net_access_strings) == LANDLOCK_NUM_ACCESS_NET);

static __attribute_const__ const char *
get_blocker(const enum landlock_request_type type,
	    const unsigned long access_bit)
{
	switch (type) {
	case LANDLOCK_REQUEST_PTRACE:
		WARN_ON_ONCE(access_bit != -1);
		return "ptrace";

	case LANDLOCK_REQUEST_FS_CHANGE_TOPOLOGY:
		WARN_ON_ONCE(access_bit != -1);
		return "fs.change_topology";

	case LANDLOCK_REQUEST_FS_ACCESS:
		if (WARN_ON_ONCE(access_bit >= ARRAY_SIZE(fs_access_strings)))
			return "unknown";
		return fs_access_strings[access_bit];

	case LANDLOCK_REQUEST_NET_ACCESS:
		if (WARN_ON_ONCE(access_bit >= ARRAY_SIZE(net_access_strings)))
			return "unknown";
		return net_access_strings[access_bit];

	case LANDLOCK_REQUEST_SCOPE_ABSTRACT_UNIX_SOCKET:
		WARN_ON_ONCE(access_bit != -1);
		return "scope.abstract_unix_socket";

	case LANDLOCK_REQUEST_SCOPE_SIGNAL:
		WARN_ON_ONCE(access_bit != -1);
		return "scope.signal";
	}

	WARN_ON_ONCE(1);
	return "unknown";
}

static void log_blockers(struct audit_buffer *const ab,
			 const enum landlock_request_type type,
			 const access_mask_t access)
{
	const unsigned long access_mask = access;
	unsigned long access_bit;
	bool is_first = true;

	for_each_set_bit(access_bit, &access_mask, BITS_PER_TYPE(access)) {
		audit_log_format(ab, "%s%s", is_first ? "" : ",",
				 get_blocker(type, access_bit));
		is_first = false;
	}
	if (is_first)
		audit_log_format(ab, "%s", get_blocker(type, -1));
}

static void log_domain(struct landlock_hierarchy *const hierarchy)
{
	struct audit_buffer *ab;

	/* Ignores already logged domains.  */
	if (READ_ONCE(hierarchy->log_status) == LANDLOCK_LOG_RECORDED)
		return;

	/* Uses consistent allocation flags wrt common_lsm_audit(). */
	ab = audit_log_start(audit_context(), GFP_ATOMIC | __GFP_NOWARN,
			     AUDIT_LANDLOCK_DOMAIN);
	if (!ab)
		return;

	WARN_ON_ONCE(hierarchy->id == 0);
	audit_log_format(
		ab,
		"domain=%llx status=allocated mode=enforcing pid=%d uid=%u exe=",
		hierarchy->id, pid_nr(hierarchy->details->pid),
		hierarchy->details->uid);
	audit_log_untrustedstring(ab, hierarchy->details->exe_path);
	audit_log_format(ab, " comm=");
	audit_log_untrustedstring(ab, hierarchy->details->comm);
	audit_log_end(ab);

	/*
	 * There may be race condition leading to logging of the same domain
	 * several times but that is OK.
	 */
	WRITE_ONCE(hierarchy->log_status, LANDLOCK_LOG_RECORDED);
}

/**
 * landlock_audit_denial - Create an audit record for a denied access request
 *
 * @request: Detail of the user space request.
 * @youngest_denied: The youngest hierarchy node that denied the access.
 * @missing: The set of denied access rights.
 * @logged: Whether the denial is selected for logging, as computed by
 *          landlock_log_denial() (domain policy and quiet rules).
 *
 * Emits the record when audit is enabled and the denial is selected for
 * logging.
 */
void landlock_audit_denial(const struct landlock_request *const request,
			   struct landlock_hierarchy *const youngest_denied,
			   const access_mask_t missing, const bool logged)
{
	struct audit_buffer *ab;

	if (!audit_enabled)
		return;

	/*
	 * Skips denials the domain's policy or a quiet rule excludes from
	 * logging (folded into @logged by landlock_log_denial()).
	 */
	if (!logged)
		return;

	/* Uses consistent allocation flags wrt common_lsm_audit(). */
	ab = audit_log_start(audit_context(), GFP_ATOMIC | __GFP_NOWARN,
			     AUDIT_LANDLOCK_ACCESS);
	if (!ab)
		return;

	audit_log_format(ab, "domain=%llx blockers=", youngest_denied->id);
	log_blockers(ab, request->type, missing);
	audit_log_lsm_data(ab, &request->audit);
	audit_log_end(ab);

	/* Logs this domain the first time it shows in log. */
	log_domain(youngest_denied);
}

/**
 * landlock_audit_free_domain - Create an audit record on domain deallocation
 *
 * @hierarchy: The domain's hierarchy being deallocated.
 *
 * Only domains which previously appeared in the audit logs are logged again.
 * This is useful to know when a domain will never show again in the audit log.
 *
 * Called from landlock_log_free_domain().
 */
void landlock_audit_free_domain(const struct landlock_hierarchy *const hierarchy)
{
	struct audit_buffer *ab;

	if (!audit_enabled)
		return;

	/* Ignores domains that were not logged.  */
	if (READ_ONCE(hierarchy->log_status) != LANDLOCK_LOG_RECORDED)
		return;

	/*
	 * If logging of domain allocation succeeded, warns about failure to log
	 * domain deallocation to highlight unbalanced domain lifetime logs.
	 */
	ab = audit_log_start(audit_context(), GFP_KERNEL,
			     AUDIT_LANDLOCK_DOMAIN);
	if (!ab)
		return;

	audit_log_format(ab, "domain=%llx status=deallocated denials=%llu",
			 hierarchy->id, atomic64_read(&hierarchy->num_denials));
	audit_log_end(ab);
}
