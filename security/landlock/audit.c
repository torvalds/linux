// SPDX-License-Identifier: GPL-2.0-only
/*
 * Landlock - Audit helpers
 *
 * Copyright © 2023-2025 Microsoft Corporation
 */

#include <kunit/test.h>
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
#include "ruleset.h"

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
};

static_assert(ARRAY_SIZE(fs_access_strings) == LANDLOCK_NUM_ACCESS_FS);

static const char *const net_access_strings[] = {
	[BIT_INDEX(LANDLOCK_ACCESS_NET_BIND_TCP)] = "net.bind_tcp",
	[BIT_INDEX(LANDLOCK_ACCESS_NET_CONNECT_TCP)] = "net.connect_tcp",
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

static struct landlock_hierarchy *
get_hierarchy(const struct landlock_ruleset *const domain, const size_t layer)
{
	struct landlock_hierarchy *hierarchy = domain->hierarchy;
	ssize_t i;

	if (WARN_ON_ONCE(layer >= domain->num_layers))
		return hierarchy;

	for (i = domain->num_layers - 1; i > layer; i--) {
		if (WARN_ON_ONCE(!hierarchy->parent))
			break;

		hierarchy = hierarchy->parent;
	}

	return hierarchy;
}

#ifdef CONFIG_SECURITY_LANDLOCK_KUNIT_TEST

static void test_get_hierarchy(struct kunit *const test)
{
	struct landlock_hierarchy dom0_hierarchy = {
		.id = 10,
	};
	struct landlock_hierarchy dom1_hierarchy = {
		.parent = &dom0_hierarchy,
		.id = 20,
	};
	struct landlock_hierarchy dom2_hierarchy = {
		.parent = &dom1_hierarchy,
		.id = 30,
	};
	struct landlock_ruleset dom2 = {
		.hierarchy = &dom2_hierarchy,
		.num_layers = 3,
	};

	KUNIT_EXPECT_EQ(test, 10, get_hierarchy(&dom2, 0)->id);
	KUNIT_EXPECT_EQ(test, 20, get_hierarchy(&dom2, 1)->id);
	KUNIT_EXPECT_EQ(test, 30, get_hierarchy(&dom2, 2)->id);
	/* KUNIT_EXPECT_EQ(test, 30, get_hierarchy(&dom2, -1)->id); */
}

#endif /* CONFIG_SECURITY_LANDLOCK_KUNIT_TEST */

static size_t get_denied_layer(const struct landlock_ruleset *const domain,
			       access_mask_t *const access_request,
			       const layer_mask_t (*const layer_masks)[],
			       const size_t layer_masks_size)
{
	const unsigned long access_req = *access_request;
	unsigned long access_bit;
	access_mask_t missing = 0;
	long youngest_layer = -1;

	for_each_set_bit(access_bit, &access_req, layer_masks_size) {
		const access_mask_t mask = (*layer_masks)[access_bit];
		long layer;

		if (!mask)
			continue;

		/* __fls(1) == 0 */
		layer = __fls(mask);
		if (layer > youngest_layer) {
			youngest_layer = layer;
			missing = BIT(access_bit);
		} else if (layer == youngest_layer) {
			missing |= BIT(access_bit);
		}
	}

	*access_request = missing;
	if (youngest_layer == -1)
		return domain->num_layers - 1;

	return youngest_layer;
}

#ifdef CONFIG_SECURITY_LANDLOCK_KUNIT_TEST

static void test_get_denied_layer(struct kunit *const test)
{
	const struct landlock_ruleset dom = {
		.num_layers = 5,
	};
	const layer_mask_t layer_masks[LANDLOCK_NUM_ACCESS_FS] = {
		[BIT_INDEX(LANDLOCK_ACCESS_FS_EXECUTE)] = BIT(0),
		[BIT_INDEX(LANDLOCK_ACCESS_FS_READ_FILE)] = BIT(1),
		[BIT_INDEX(LANDLOCK_ACCESS_FS_READ_DIR)] = BIT(1) | BIT(0),
		[BIT_INDEX(LANDLOCK_ACCESS_FS_REMOVE_DIR)] = BIT(2),
	};
	access_mask_t access;

	access = LANDLOCK_ACCESS_FS_EXECUTE;
	KUNIT_EXPECT_EQ(test, 0,
			get_denied_layer(&dom, &access, &layer_masks,
					 sizeof(layer_masks)));
	KUNIT_EXPECT_EQ(test, access, LANDLOCK_ACCESS_FS_EXECUTE);

	access = LANDLOCK_ACCESS_FS_READ_FILE;
	KUNIT_EXPECT_EQ(test, 1,
			get_denied_layer(&dom, &access, &layer_masks,
					 sizeof(layer_masks)));
	KUNIT_EXPECT_EQ(test, access, LANDLOCK_ACCESS_FS_READ_FILE);

	access = LANDLOCK_ACCESS_FS_READ_DIR;
	KUNIT_EXPECT_EQ(test, 1,
			get_denied_layer(&dom, &access, &layer_masks,
					 sizeof(layer_masks)));
	KUNIT_EXPECT_EQ(test, access, LANDLOCK_ACCESS_FS_READ_DIR);

	access = LANDLOCK_ACCESS_FS_READ_FILE | LANDLOCK_ACCESS_FS_READ_DIR;
	KUNIT_EXPECT_EQ(test, 1,
			get_denied_layer(&dom, &access, &layer_masks,
					 sizeof(layer_masks)));
	KUNIT_EXPECT_EQ(test, access,
			LANDLOCK_ACCESS_FS_READ_FILE |
				LANDLOCK_ACCESS_FS_READ_DIR);

	access = LANDLOCK_ACCESS_FS_EXECUTE | LANDLOCK_ACCESS_FS_READ_DIR;
	KUNIT_EXPECT_EQ(test, 1,
			get_denied_layer(&dom, &access, &layer_masks,
					 sizeof(layer_masks)));
	KUNIT_EXPECT_EQ(test, access, LANDLOCK_ACCESS_FS_READ_DIR);

	access = LANDLOCK_ACCESS_FS_WRITE_FILE;
	KUNIT_EXPECT_EQ(test, 4,
			get_denied_layer(&dom, &access, &layer_masks,
					 sizeof(layer_masks)));
	KUNIT_EXPECT_EQ(test, access, 0);
}

#endif /* CONFIG_SECURITY_LANDLOCK_KUNIT_TEST */

static size_t
get_layer_from_deny_masks(access_mask_t *const access_request,
			  const access_mask_t all_existing_optional_access,
			  const deny_masks_t deny_masks)
{
	const unsigned long access_opt = all_existing_optional_access;
	const unsigned long access_req = *access_request;
	access_mask_t missing = 0;
	size_t youngest_layer = 0;
	size_t access_index = 0;
	unsigned long access_bit;

	/* This will require change with new object types. */
	WARN_ON_ONCE(access_opt != _LANDLOCK_ACCESS_FS_OPTIONAL);

	for_each_set_bit(access_bit, &access_opt,
			 BITS_PER_TYPE(access_mask_t)) {
		if (access_req & BIT(access_bit)) {
			const size_t layer =
				(deny_masks >> (access_index * 4)) &
				(LANDLOCK_MAX_NUM_LAYERS - 1);

			if (layer > youngest_layer) {
				youngest_layer = layer;
				missing = BIT(access_bit);
			} else if (layer == youngest_layer) {
				missing |= BIT(access_bit);
			}
		}
		access_index++;
	}

	*access_request = missing;
	return youngest_layer;
}

#ifdef CONFIG_SECURITY_LANDLOCK_KUNIT_TEST

static void test_get_layer_from_deny_masks(struct kunit *const test)
{
	deny_masks_t deny_mask;
	access_mask_t access;

	/* truncate:0 ioctl_dev:2 */
	deny_mask = 0x20;

	access = LANDLOCK_ACCESS_FS_TRUNCATE;
	KUNIT_EXPECT_EQ(test, 0,
			get_layer_from_deny_masks(&access,
						  _LANDLOCK_ACCESS_FS_OPTIONAL,
						  deny_mask));
	KUNIT_EXPECT_EQ(test, access, LANDLOCK_ACCESS_FS_TRUNCATE);

	access = LANDLOCK_ACCESS_FS_TRUNCATE | LANDLOCK_ACCESS_FS_IOCTL_DEV;
	KUNIT_EXPECT_EQ(test, 2,
			get_layer_from_deny_masks(&access,
						  _LANDLOCK_ACCESS_FS_OPTIONAL,
						  deny_mask));
	KUNIT_EXPECT_EQ(test, access, LANDLOCK_ACCESS_FS_IOCTL_DEV);

	/* truncate:15 ioctl_dev:15 */
	deny_mask = 0xff;

	access = LANDLOCK_ACCESS_FS_TRUNCATE;
	KUNIT_EXPECT_EQ(test, 15,
			get_layer_from_deny_masks(&access,
						  _LANDLOCK_ACCESS_FS_OPTIONAL,
						  deny_mask));
	KUNIT_EXPECT_EQ(test, access, LANDLOCK_ACCESS_FS_TRUNCATE);

	access = LANDLOCK_ACCESS_FS_TRUNCATE | LANDLOCK_ACCESS_FS_IOCTL_DEV;
	KUNIT_EXPECT_EQ(test, 15,
			get_layer_from_deny_masks(&access,
						  _LANDLOCK_ACCESS_FS_OPTIONAL,
						  deny_mask));
	KUNIT_EXPECT_EQ(test, access,
			LANDLOCK_ACCESS_FS_TRUNCATE |
				LANDLOCK_ACCESS_FS_IOCTL_DEV);
}

#endif /* CONFIG_SECURITY_LANDLOCK_KUNIT_TEST */

static bool is_valid_request(const struct landlock_request *const request)
{
	if (WARN_ON_ONCE(request->layer_plus_one > LANDLOCK_MAX_NUM_LAYERS))
		return false;

	if (WARN_ON_ONCE(!(!!request->layer_plus_one ^ !!request->access)))
		return false;

	if (request->access) {
		if (WARN_ON_ONCE(!(!!request->layer_masks ^
				   !!request->all_existing_optional_access)))
			return false;
	} else {
		if (WARN_ON_ONCE(request->layer_masks ||
				 request->all_existing_optional_access))
			return false;
	}

	if (WARN_ON_ONCE(!!request->layer_masks ^ !!request->layer_masks_size))
		return false;

	if (request->deny_masks) {
		if (WARN_ON_ONCE(!request->all_existing_optional_access))
			return false;
	}

	return true;
}

/**
 * landlock_log_denial - Create audit records related to a denial
 *
 * @subject: The Landlock subject's credential denying an action.
 * @request: Detail of the user space request.
 */
void landlock_log_denial(const struct landlock_cred_security *const subject,
			 const struct landlock_request *const request)
{
	struct audit_buffer *ab;
	struct landlock_hierarchy *youngest_denied;
	size_t youngest_layer;
	access_mask_t missing;

	if (WARN_ON_ONCE(!subject || !subject->domain ||
			 !subject->domain->hierarchy || !request))
		return;

	if (!is_valid_request(request))
		return;

	missing = request->access;
	if (missing) {
		/* Gets the nearest domain that denies the request. */
		if (request->layer_masks) {
			youngest_layer = get_denied_layer(
				subject->domain, &missing, request->layer_masks,
				request->layer_masks_size);
		} else {
			youngest_layer = get_layer_from_deny_masks(
				&missing, request->all_existing_optional_access,
				request->deny_masks);
		}
		youngest_denied =
			get_hierarchy(subject->domain, youngest_layer);
	} else {
		youngest_layer = request->layer_plus_one - 1;
		youngest_denied =
			get_hierarchy(subject->domain, youngest_layer);
	}

	if (READ_ONCE(youngest_denied->log_status) == LANDLOCK_LOG_DISABLED)
		return;

	/*
	 * Consistently keeps track of the number of denied access requests
	 * even if audit is currently disabled, or if audit rules currently
	 * exclude this record type, or if landlock_restrict_self(2)'s flags
	 * quiet logs.
	 */
	atomic64_inc(&youngest_denied->num_denials);

	if (!audit_enabled)
		return;

	/* Checks if the current exec was restricting itself. */
	if (subject->domain_exec & BIT(youngest_layer)) {
		/* Ignores denials for the same execution. */
		if (!youngest_denied->log_same_exec)
			return;
	} else {
		/* Ignores denials after a new execution. */
		if (!youngest_denied->log_new_exec)
			return;
	}

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
 * landlock_log_drop_domain - Create an audit record on domain deallocation
 *
 * @hierarchy: The domain's hierarchy being deallocated.
 *
 * Only domains which previously appeared in the audit logs are logged again.
 * This is useful to know when a domain will never show again in the audit log.
 *
 * Called in a work queue scheduled by landlock_put_ruleset_deferred() called
 * by hook_cred_free().
 */
void landlock_log_drop_domain(const struct landlock_hierarchy *const hierarchy)
{
	struct audit_buffer *ab;

	if (WARN_ON_ONCE(!hierarchy))
		return;

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

#ifdef CONFIG_SECURITY_LANDLOCK_KUNIT_TEST

static struct kunit_case test_cases[] = {
	/* clang-format off */
	KUNIT_CASE(test_get_hierarchy),
	KUNIT_CASE(test_get_denied_layer),
	KUNIT_CASE(test_get_layer_from_deny_masks),
	{}
	/* clang-format on */
};

static struct kunit_suite test_suite = {
	.name = "landlock_audit",
	.test_cases = test_cases,
};

kunit_test_suite(test_suite);

#endif /* CONFIG_SECURITY_LANDLOCK_KUNIT_TEST */
