// SPDX-License-Identifier: GPL-2.0-only
/*
 * Landlock - Capability hooks
 *
 * Copyright © 2026 Cloudflare, Inc.
 */

#include <linux/capability.h>
#include <linux/cred.h>
#include <linux/lsm_audit.h>
#include <linux/lsm_hooks.h>
#include <uapi/linux/landlock.h>

#include "cap.h"
#include "cred.h"
#include "domain.h"
#include "limits.h"
#include "log.h"
#include "ruleset.h"
#include "setup.h"

static const struct access_masks cap_permission = {
	.permissions = LANDLOCK_PERMISSION_CAPABILITY_USE,
};

/**
 * hook_capable - Deny capability use for Landlock-sandboxed processes
 *
 * @cred: Credentials being checked.
 * @ns: Target user namespace (intentionally ignored; policy depends on the
 *      acting credentials and @cap, not namespace ownership).
 * @cap: Capability number (CAP_*).
 * @opts: Capability check options.  CAP_OPT_NOAUDIT skips the denial record:
 *        no audit record, no trace event, and no denial count.
 *
 * Pure bitmask check: denies the capability if it is not in the layer's allowed
 * set.  This hook is purely restrictive: commoncap is registered with
 * LSM_ORDER_FIRST so cap_capable() always runs first, which means Landlock can
 * deny capabilities that commoncap would allow, but never grant capabilities
 * that commoncap denied.
 *
 * Return: 0 if allowed, -EPERM if capability use is denied.
 */
static int hook_capable(const struct cred *cred, struct user_namespace *ns,
			int cap, unsigned int opts)
{
	const struct landlock_cred_security *subject;
	size_t denied_layer;

	subject = landlock_get_applicable_subject(cred, cap_permission, NULL);
	if (!subject)
		return 0;

	denied_layer = landlock_permission_is_denied(
		subject->domain, LANDLOCK_PERMISSION_CAPABILITY_USE,
		landlock_cap_to_bit(cap));
	if (!denied_layer)
		return 0;

	if (!(opts & CAP_OPT_NOAUDIT))
		landlock_log_denial(
			subject,
			&(struct landlock_request){
				.type = LANDLOCK_REQUEST_CAPABILITY,
				.audit.type = LSM_AUDIT_DATA_CAP,
				.audit.u.cap = cap,
				.permission =
					LANDLOCK_PERMISSION_CAPABILITY_USE,
				.layer_plus_one = denied_layer,
			});

	return -EPERM;
}

static struct security_hook_list landlock_hooks[] __ro_after_init = {
	LSM_HOOK_INIT(capable, hook_capable),
};

__init void landlock_add_cap_hooks(void)
{
	security_add_hooks(landlock_hooks, ARRAY_SIZE(landlock_hooks),
			   &landlock_lsmid);
}

#ifdef CONFIG_SECURITY_LANDLOCK_KUNIT_TEST

#include <kunit/test.h>

static void test_cap_to_bit(struct kunit *const test)
{
	KUNIT_EXPECT_EQ(test, BIT_ULL(0), landlock_cap_to_bit(0));
	KUNIT_EXPECT_EQ(test, BIT_ULL(CAP_NET_RAW),
			landlock_cap_to_bit(CAP_NET_RAW));
	KUNIT_EXPECT_EQ(test, BIT_ULL(CAP_SYS_ADMIN),
			landlock_cap_to_bit(CAP_SYS_ADMIN));
	KUNIT_EXPECT_EQ(test, BIT_ULL(CAP_LAST_CAP),
			landlock_cap_to_bit(CAP_LAST_CAP));
}

static void test_cap_to_bit_invalid(struct kunit *const test)
{
	if (!IS_ENABLED(CONFIG_BUG))
		kunit_skip(test, "requires CONFIG_BUG");

	/* clang-format off */
	kunit_warning_suppress(test) {
		/* clang-format on */
		KUNIT_EXPECT_EQ(test, 0ULL, landlock_cap_to_bit(-1));
		KUNIT_EXPECT_EQ(test, 0ULL,
				landlock_cap_to_bit(CAP_LAST_CAP + 1));
	}
}

static void test_caps_to_bits_valid(struct kunit *const test)
{
	KUNIT_EXPECT_EQ(test, (u64)CAP_VALID_MASK,
			landlock_caps_to_bits(CAP_VALID_MASK));
	KUNIT_EXPECT_EQ(test, BIT_ULL(CAP_NET_RAW),
			landlock_caps_to_bits(BIT_ULL(CAP_NET_RAW)));
}

static void test_caps_to_bits_unknown(struct kunit *const test)
{
	if (!IS_ENABLED(CONFIG_BUG))
		kunit_skip(test, "requires CONFIG_BUG");

	/* clang-format off */
	kunit_warning_suppress(test) {
		/* clang-format on */
		KUNIT_EXPECT_EQ(
			test, 0ULL,
			landlock_caps_to_bits(BIT_ULL(CAP_LAST_CAP + 1)));
	}
}

static void test_caps_to_bits_zero(struct kunit *const test)
{
	KUNIT_EXPECT_EQ(test, 0ULL, landlock_caps_to_bits(0));
}

static struct kunit_case test_cases[] = {
	/* clang-format off */
	KUNIT_CASE(test_cap_to_bit),
	KUNIT_CASE(test_cap_to_bit_invalid),
	KUNIT_CASE(test_caps_to_bits_valid),
	KUNIT_CASE(test_caps_to_bits_unknown),
	KUNIT_CASE(test_caps_to_bits_zero),
	{}
	/* clang-format on */
};

static struct kunit_suite test_suite = {
	.name = "landlock_cap",
	.test_cases = test_cases,
};

kunit_test_suite(test_suite);

#endif /* CONFIG_SECURITY_LANDLOCK_KUNIT_TEST */
