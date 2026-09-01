// SPDX-License-Identifier: GPL-2.0-only
/*
 * Landlock - Log helpers
 *
 * Copyright © 2023-2025 Microsoft Corporation
 * Copyright © 2026 Cloudflare, Inc.
 */

#include <kunit/test.h>
#include <linux/bitops.h>
#include <uapi/linux/landlock.h>

#include "access.h"
#include "audit.h"
#include "common.h"
#include "cred.h"
#include "domain.h"
#include "limits.h"
#include "log.h"
#include "ruleset.h"
#include "trace.h"

static struct landlock_hierarchy *
get_hierarchy(const struct landlock_domain *const domain, const size_t layer)
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
	struct landlock_domain dom2 = {
		.hierarchy = &dom2_hierarchy,
		.num_layers = 3,
	};

	KUNIT_EXPECT_EQ(test, 10, get_hierarchy(&dom2, 0)->id);
	KUNIT_EXPECT_EQ(test, 20, get_hierarchy(&dom2, 1)->id);
	KUNIT_EXPECT_EQ(test, 30, get_hierarchy(&dom2, 2)->id);
	/* KUNIT_EXPECT_EQ(test, 30, get_hierarchy(&dom2, -1)->id); */
}

#endif /* CONFIG_SECURITY_LANDLOCK_KUNIT_TEST */

/* Get the youngest layer that denied the access_request. */
static size_t get_denied_layer(const struct landlock_domain *const domain,
			       access_mask_t *const access_request,
			       const struct layer_masks *masks)
{
	for (ssize_t i = ARRAY_SIZE(masks->layers) - 1; i >= 0; i--) {
		if (masks->layers[i].access & *access_request) {
			*access_request &= masks->layers[i].access;
			return i;
		}
	}

	/* Not found - fall back to default values */
	*access_request = 0;
	return domain->num_layers - 1;
}

#ifdef CONFIG_SECURITY_LANDLOCK_KUNIT_TEST

static void test_get_denied_layer(struct kunit *const test)
{
	const struct landlock_domain dom = {
		.num_layers = 5,
	};
	const struct layer_masks masks = {
		.layers[0].access = LANDLOCK_ACCESS_FS_EXECUTE |
				    LANDLOCK_ACCESS_FS_READ_DIR,
		.layers[1].access = LANDLOCK_ACCESS_FS_READ_FILE |
				    LANDLOCK_ACCESS_FS_READ_DIR,
		.layers[2].access = LANDLOCK_ACCESS_FS_REMOVE_DIR,
	};
	access_mask_t access;

	access = LANDLOCK_ACCESS_FS_EXECUTE;
	KUNIT_EXPECT_EQ(test, 0, get_denied_layer(&dom, &access, &masks));
	KUNIT_EXPECT_EQ(test, access, LANDLOCK_ACCESS_FS_EXECUTE);

	access = LANDLOCK_ACCESS_FS_READ_FILE;
	KUNIT_EXPECT_EQ(test, 1, get_denied_layer(&dom, &access, &masks));
	KUNIT_EXPECT_EQ(test, access, LANDLOCK_ACCESS_FS_READ_FILE);

	access = LANDLOCK_ACCESS_FS_READ_DIR;
	KUNIT_EXPECT_EQ(test, 1, get_denied_layer(&dom, &access, &masks));
	KUNIT_EXPECT_EQ(test, access, LANDLOCK_ACCESS_FS_READ_DIR);

	access = LANDLOCK_ACCESS_FS_READ_FILE | LANDLOCK_ACCESS_FS_READ_DIR;
	KUNIT_EXPECT_EQ(test, 1, get_denied_layer(&dom, &access, &masks));
	KUNIT_EXPECT_EQ(test, access,
			LANDLOCK_ACCESS_FS_READ_FILE |
				LANDLOCK_ACCESS_FS_READ_DIR);

	access = LANDLOCK_ACCESS_FS_EXECUTE | LANDLOCK_ACCESS_FS_READ_DIR;
	KUNIT_EXPECT_EQ(test, 1, get_denied_layer(&dom, &access, &masks));
	KUNIT_EXPECT_EQ(test, access, LANDLOCK_ACCESS_FS_READ_DIR);

	access = LANDLOCK_ACCESS_FS_WRITE_FILE;
	KUNIT_EXPECT_EQ(test, 4, get_denied_layer(&dom, &access, &masks));
	KUNIT_EXPECT_EQ(test, access, 0);
}

#endif /* CONFIG_SECURITY_LANDLOCK_KUNIT_TEST */

static size_t
get_layer_from_deny_masks(access_mask_t *const access_request,
			  const access_mask_t all_existing_optional_access,
			  const deny_masks_t deny_masks,
			  optional_access_t quiet_optional_accesses,
			  bool *quiet)
{
	const unsigned long access_opt = all_existing_optional_access;
	const unsigned long access_req = *access_request;
	access_mask_t missing = 0;
	size_t youngest_layer = 0;
	size_t access_index = 0;
	unsigned long access_bit;
	bool should_quiet = false;

	/* This will require change with new object types. */
	WARN_ON_ONCE(access_opt != _LANDLOCK_ACCESS_FS_OPTIONAL);

	for_each_set_bit(access_bit, &access_opt,
			 BITS_PER_TYPE(access_mask_t)) {
		if (access_req & BIT(access_bit)) {
			const size_t layer =
				(deny_masks >>
				 (access_index *
				  HWEIGHT(LANDLOCK_MAX_NUM_LAYERS - 1))) &
				(LANDLOCK_MAX_NUM_LAYERS - 1);
			const bool layer_has_quiet =
				!!(quiet_optional_accesses & BIT(access_index));

			if (layer > youngest_layer) {
				youngest_layer = layer;
				missing = BIT(access_bit);
				should_quiet = layer_has_quiet;
			} else if (layer == youngest_layer) {
				missing |= BIT(access_bit);
				/*
				 * Whether the layer has rules with quiet flag
				 * covering the file accessed does not depend on
				 * the access, and so the following
				 * WARN_ON_ONCE() should not fail.
				 */
				WARN_ON_ONCE(should_quiet && !layer_has_quiet);
				should_quiet = layer_has_quiet;
			}
		}
		access_index++;
	}

	*access_request = missing;
	*quiet = should_quiet;
	return youngest_layer;
}

#ifdef CONFIG_SECURITY_LANDLOCK_KUNIT_TEST

static void test_get_layer_from_deny_masks(struct kunit *const test)
{
	deny_masks_t deny_mask;
	access_mask_t access;
	optional_access_t quiet_optional_accesses;
	bool quiet;

	/* truncate:0 ioctl_dev:2 */
	deny_mask = 0x20;
	quiet_optional_accesses = 0;

	access = LANDLOCK_ACCESS_FS_TRUNCATE;
	KUNIT_EXPECT_EQ(test, 0,
			get_layer_from_deny_masks(
				&access, _LANDLOCK_ACCESS_FS_OPTIONAL,
				deny_mask, quiet_optional_accesses, &quiet));
	KUNIT_EXPECT_EQ(test, access, LANDLOCK_ACCESS_FS_TRUNCATE);
	KUNIT_EXPECT_EQ(test, quiet, false);

	access = LANDLOCK_ACCESS_FS_IOCTL_DEV;
	KUNIT_EXPECT_EQ(test, 2,
			get_layer_from_deny_masks(
				&access, _LANDLOCK_ACCESS_FS_OPTIONAL,
				deny_mask, quiet_optional_accesses, &quiet));
	KUNIT_EXPECT_EQ(test, access, LANDLOCK_ACCESS_FS_IOCTL_DEV);
	KUNIT_EXPECT_EQ(test, quiet, false);

	access = LANDLOCK_ACCESS_FS_TRUNCATE | LANDLOCK_ACCESS_FS_IOCTL_DEV;
	KUNIT_EXPECT_EQ(test, 2,
			get_layer_from_deny_masks(
				&access, _LANDLOCK_ACCESS_FS_OPTIONAL,
				deny_mask, quiet_optional_accesses, &quiet));
	KUNIT_EXPECT_EQ(test, access, LANDLOCK_ACCESS_FS_IOCTL_DEV);
	KUNIT_EXPECT_EQ(test, quiet, false);

	/* layer denying truncate: quiet, ioctl: not quiet */
	quiet_optional_accesses = 0b01;

	access = LANDLOCK_ACCESS_FS_TRUNCATE;
	KUNIT_EXPECT_EQ(test, 0,
			get_layer_from_deny_masks(
				&access, _LANDLOCK_ACCESS_FS_OPTIONAL,
				deny_mask, quiet_optional_accesses, &quiet));
	KUNIT_EXPECT_EQ(test, access, LANDLOCK_ACCESS_FS_TRUNCATE);
	KUNIT_EXPECT_EQ(test, quiet, true);

	access = LANDLOCK_ACCESS_FS_IOCTL_DEV;
	KUNIT_EXPECT_EQ(test, 2,
			get_layer_from_deny_masks(
				&access, _LANDLOCK_ACCESS_FS_OPTIONAL,
				deny_mask, quiet_optional_accesses, &quiet));
	KUNIT_EXPECT_EQ(test, access, LANDLOCK_ACCESS_FS_IOCTL_DEV);
	KUNIT_EXPECT_EQ(test, quiet, false);

	access = LANDLOCK_ACCESS_FS_TRUNCATE | LANDLOCK_ACCESS_FS_IOCTL_DEV;
	KUNIT_EXPECT_EQ(test, 2,
			get_layer_from_deny_masks(
				&access, _LANDLOCK_ACCESS_FS_OPTIONAL,
				deny_mask, quiet_optional_accesses, &quiet));
	KUNIT_EXPECT_EQ(test, access, LANDLOCK_ACCESS_FS_IOCTL_DEV);
	KUNIT_EXPECT_EQ(test, quiet, false);

	/* Reverse order - truncate:2 ioctl_dev:0 */
	deny_mask = 0x02;
	quiet_optional_accesses = 0;

	access = LANDLOCK_ACCESS_FS_TRUNCATE;
	KUNIT_EXPECT_EQ(test, 2,
			get_layer_from_deny_masks(
				&access, _LANDLOCK_ACCESS_FS_OPTIONAL,
				deny_mask, quiet_optional_accesses, &quiet));
	KUNIT_EXPECT_EQ(test, access, LANDLOCK_ACCESS_FS_TRUNCATE);
	KUNIT_EXPECT_EQ(test, quiet, false);

	access = LANDLOCK_ACCESS_FS_IOCTL_DEV;
	KUNIT_EXPECT_EQ(test, 0,
			get_layer_from_deny_masks(
				&access, _LANDLOCK_ACCESS_FS_OPTIONAL,
				deny_mask, quiet_optional_accesses, &quiet));
	KUNIT_EXPECT_EQ(test, access, LANDLOCK_ACCESS_FS_IOCTL_DEV);
	KUNIT_EXPECT_EQ(test, quiet, false);

	access = LANDLOCK_ACCESS_FS_TRUNCATE | LANDLOCK_ACCESS_FS_IOCTL_DEV;
	KUNIT_EXPECT_EQ(test, 2,
			get_layer_from_deny_masks(
				&access, _LANDLOCK_ACCESS_FS_OPTIONAL,
				deny_mask, quiet_optional_accesses, &quiet));
	KUNIT_EXPECT_EQ(test, access, LANDLOCK_ACCESS_FS_TRUNCATE);
	KUNIT_EXPECT_EQ(test, quiet, false);

	/* layer denying truncate: quiet, ioctl: not quiet */
	quiet_optional_accesses = 0b01;

	access = LANDLOCK_ACCESS_FS_TRUNCATE;
	KUNIT_EXPECT_EQ(test, 2,
			get_layer_from_deny_masks(
				&access, _LANDLOCK_ACCESS_FS_OPTIONAL,
				deny_mask, quiet_optional_accesses, &quiet));
	KUNIT_EXPECT_EQ(test, access, LANDLOCK_ACCESS_FS_TRUNCATE);
	KUNIT_EXPECT_EQ(test, quiet, true);

	access = LANDLOCK_ACCESS_FS_IOCTL_DEV;
	KUNIT_EXPECT_EQ(test, 0,
			get_layer_from_deny_masks(
				&access, _LANDLOCK_ACCESS_FS_OPTIONAL,
				deny_mask, quiet_optional_accesses, &quiet));
	KUNIT_EXPECT_EQ(test, access, LANDLOCK_ACCESS_FS_IOCTL_DEV);
	KUNIT_EXPECT_EQ(test, quiet, false);

	access = LANDLOCK_ACCESS_FS_TRUNCATE | LANDLOCK_ACCESS_FS_IOCTL_DEV;
	KUNIT_EXPECT_EQ(test, 2,
			get_layer_from_deny_masks(
				&access, _LANDLOCK_ACCESS_FS_OPTIONAL,
				deny_mask, quiet_optional_accesses, &quiet));
	KUNIT_EXPECT_EQ(test, access, LANDLOCK_ACCESS_FS_TRUNCATE);
	KUNIT_EXPECT_EQ(test, quiet, true);

	/* layer denying truncate: not quiet, ioctl: quiet */
	quiet_optional_accesses = 0b10;

	access = LANDLOCK_ACCESS_FS_TRUNCATE;
	KUNIT_EXPECT_EQ(test, 2,
			get_layer_from_deny_masks(
				&access, _LANDLOCK_ACCESS_FS_OPTIONAL,
				deny_mask, quiet_optional_accesses, &quiet));
	KUNIT_EXPECT_EQ(test, access, LANDLOCK_ACCESS_FS_TRUNCATE);
	KUNIT_EXPECT_EQ(test, quiet, false);

	access = LANDLOCK_ACCESS_FS_IOCTL_DEV;
	KUNIT_EXPECT_EQ(test, 0,
			get_layer_from_deny_masks(
				&access, _LANDLOCK_ACCESS_FS_OPTIONAL,
				deny_mask, quiet_optional_accesses, &quiet));
	KUNIT_EXPECT_EQ(test, access, LANDLOCK_ACCESS_FS_IOCTL_DEV);
	KUNIT_EXPECT_EQ(test, quiet, true);

	access = LANDLOCK_ACCESS_FS_TRUNCATE | LANDLOCK_ACCESS_FS_IOCTL_DEV;
	KUNIT_EXPECT_EQ(test, 2,
			get_layer_from_deny_masks(
				&access, _LANDLOCK_ACCESS_FS_OPTIONAL,
				deny_mask, quiet_optional_accesses, &quiet));
	KUNIT_EXPECT_EQ(test, access, LANDLOCK_ACCESS_FS_TRUNCATE);
	KUNIT_EXPECT_EQ(test, quiet, false);

	/* truncate:15 ioctl_dev:15 */
	deny_mask = 0xff;
	quiet_optional_accesses = 0;

	access = LANDLOCK_ACCESS_FS_TRUNCATE;
	KUNIT_EXPECT_EQ(test, 15,
			get_layer_from_deny_masks(
				&access, _LANDLOCK_ACCESS_FS_OPTIONAL,
				deny_mask, quiet_optional_accesses, &quiet));
	KUNIT_EXPECT_EQ(test, access, LANDLOCK_ACCESS_FS_TRUNCATE);
	KUNIT_EXPECT_EQ(test, quiet, false);

	access = LANDLOCK_ACCESS_FS_TRUNCATE | LANDLOCK_ACCESS_FS_IOCTL_DEV;
	KUNIT_EXPECT_EQ(test, 15,
			get_layer_from_deny_masks(
				&access, _LANDLOCK_ACCESS_FS_OPTIONAL,
				deny_mask, quiet_optional_accesses, &quiet));
	KUNIT_EXPECT_EQ(test, access,
			LANDLOCK_ACCESS_FS_TRUNCATE |
				LANDLOCK_ACCESS_FS_IOCTL_DEV);
	KUNIT_EXPECT_EQ(test, quiet, false);

	/* Both quiet (same layer so quietness must be the same) */
	quiet_optional_accesses = 0b11;

	access = LANDLOCK_ACCESS_FS_TRUNCATE;
	KUNIT_EXPECT_EQ(test, 15,
			get_layer_from_deny_masks(
				&access, _LANDLOCK_ACCESS_FS_OPTIONAL,
				deny_mask, quiet_optional_accesses, &quiet));
	KUNIT_EXPECT_EQ(test, access, LANDLOCK_ACCESS_FS_TRUNCATE);
	KUNIT_EXPECT_EQ(test, quiet, true);

	access = LANDLOCK_ACCESS_FS_TRUNCATE | LANDLOCK_ACCESS_FS_IOCTL_DEV;
	KUNIT_EXPECT_EQ(test, 15,
			get_layer_from_deny_masks(
				&access, _LANDLOCK_ACCESS_FS_OPTIONAL,
				deny_mask, quiet_optional_accesses, &quiet));
	KUNIT_EXPECT_EQ(test, access,
			LANDLOCK_ACCESS_FS_TRUNCATE |
				LANDLOCK_ACCESS_FS_IOCTL_DEV);
	KUNIT_EXPECT_EQ(test, quiet, true);
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

	if (request->deny_masks) {
		if (WARN_ON_ONCE(!request->all_existing_optional_access))
			return false;
		static_assert(sizeof(request->all_existing_optional_access) ==
			      sizeof(u32));
		if (WARN_ON_ONCE(
			    request->quiet_optional_accesses >=
			    BIT(hweight32(
				    request->all_existing_optional_access))))
			return false;
	}

	return true;
}

static access_mask_t
pick_access_mask_for_request_type(const enum landlock_request_type type,
				  const struct access_masks access_masks)
{
	switch (type) {
	case LANDLOCK_REQUEST_FS_ACCESS:
		return access_masks.fs;
	case LANDLOCK_REQUEST_NET_ACCESS:
		return access_masks.net;
	default:
		WARN_ONCE(1, "Invalid request type %d passed to %s", type,
			  __func__);
		return 0;
	}
}

/*
 * Whether a quiet rule silences the denial: the rule must cover the whole
 * denied access in the layer that denied it (a quiet rule in a non-denying
 * layer does not suppress the denial).
 */
static bool
is_denial_quieted(const struct landlock_request *const request,
		  const struct landlock_hierarchy *const youngest_denied,
		  const access_mask_t missing, const bool object_quiet_flag)
{
	if (object_quiet_flag) {
		const access_mask_t quiet_mask =
			pick_access_mask_for_request_type(
				request->type, youngest_denied->quiet_masks);

		return (quiet_mask & missing) == missing;
	}

	/*
	 * Either the object is not quiet, or this is a scope request.  We check
	 * request->type to distinguish between the two cases.
	 */
	switch (request->type) {
	case LANDLOCK_REQUEST_SCOPE_SIGNAL:
		return !!(youngest_denied->quiet_masks.scope &
			  LANDLOCK_SCOPE_SIGNAL);
	case LANDLOCK_REQUEST_SCOPE_ABSTRACT_UNIX_SOCKET:
		return !!(youngest_denied->quiet_masks.scope &
			  LANDLOCK_SCOPE_ABSTRACT_UNIX_SOCKET);
	/*
	 * Leave LANDLOCK_REQUEST_PTRACE and LANDLOCK_REQUEST_FS_CHANGE_TOPOLOGY
	 * unhandled for now - they are never quiet.
	 */
	default:
		return false;
	}
}

/*
 * Computes whether a denial from youngest_denied is selected for logging by the
 * domain's policy: its logging must not be disabled (by both per-execution
 * flags being off, or by an ancestor's
 * LANDLOCK_RESTRICT_SELF_LOG_SUBDOMAINS_OFF), the per-execution flag matching
 * same_exec must be set, and no quiet rule may cover the denied access.
 * landlock_log_denial() computes this once and passes it to
 * landlock_audit_denial(), which additionally requires audit_enabled.
 */
static bool
is_denial_logged(const struct landlock_request *const request,
		 const struct landlock_hierarchy *const youngest_denied,
		 const access_mask_t missing, const bool same_exec,
		 const bool object_quiet_flag)
{
	if (READ_ONCE(youngest_denied->log_status) == LANDLOCK_LOG_DISABLED)
		return false;

	if (!(same_exec ? youngest_denied->log_same_exec :
			  youngest_denied->log_new_exec))
		return false;

	return !is_denial_quieted(request, youngest_denied, missing,
				  object_quiet_flag);
}

/**
 * landlock_log_denial - Log a denied access
 *
 * @subject: The Landlock subject's credential denying an action.
 * @request: Detail of the user space request.
 */
void landlock_log_denial(const struct landlock_cred_security *const subject,
			 const struct landlock_request *const request)
{
	struct landlock_hierarchy *youngest_denied;
	size_t youngest_layer;
	access_mask_t missing;
	bool object_quiet_flag = false;

	if (WARN_ON_ONCE(!subject || !subject->domain ||
			 !subject->domain->hierarchy || !request))
		return;

	if (!is_valid_request(request))
		return;

	missing = request->access;
	if (missing) {
		/* Gets the nearest domain that denies the request. */
		if (request->layer_masks) {
			youngest_layer = get_denied_layer(subject->domain,
							  &missing,
							  request->layer_masks);
			object_quiet_flag =
				request->layer_masks->layers[youngest_layer]
					.quiet;
		} else {
			youngest_layer = get_layer_from_deny_masks(
				&missing, _LANDLOCK_ACCESS_FS_OPTIONAL,
				request->deny_masks,
				request->quiet_optional_accesses,
				&object_quiet_flag);
		}
		youngest_denied =
			get_hierarchy(subject->domain, youngest_layer);
	} else {
		youngest_layer = request->layer_plus_one - 1;
		youngest_denied =
			get_hierarchy(subject->domain, youngest_layer);
	}

	const bool same_exec = !!(subject->domain_exec & BIT(youngest_layer));
	const bool logged = is_denial_logged(request, youngest_denied, missing,
					     same_exec, object_quiet_flag);

	/*
	 * Consistently keeps track of the number of denied access requests even
	 * if audit is currently disabled, or if audit rules currently exclude
	 * this record type, or if landlock_restrict_self(2)'s flags quiet logs.
	 */
	atomic64_inc(&youngest_denied->num_denials);

	landlock_trace_denial(request, youngest_denied, missing, same_exec,
			      logged);
	landlock_audit_denial(request, youngest_denied, missing, logged);
}

/**
 * landlock_log_free_domain - Log domain deallocation
 *
 * @hierarchy: The domain's hierarchy being deallocated.
 *
 * Called from landlock_put_domain_deferred() (via a work queue scheduled by
 * hook_cred_free()) or directly from landlock_put_domain().
 */
void landlock_log_free_domain(const struct landlock_hierarchy *const hierarchy)
{
	if (WARN_ON_ONCE(!hierarchy))
		return;

	landlock_trace_free_domain(hierarchy);
	landlock_audit_free_domain(hierarchy);
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
	.name = "landlock_log",
	.test_cases = test_cases,
};

kunit_test_suite(test_suite);

#endif /* CONFIG_SECURITY_LANDLOCK_KUNIT_TEST */
