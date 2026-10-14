// SPDX-License-Identifier: GPL-2.0-only
/*
 * Landlock - Namespace hooks
 *
 * Copyright © 2026 Cloudflare, Inc.
 */

#include <linux/bitops.h>
#include <linux/bug.h>
#include <linux/lsm_audit.h>
#include <linux/lsm_hooks.h>
#include <linux/ns/ns_common_types.h>
#include <linux/ns_common.h>
#include <linux/nsproxy.h>
#include <uapi/linux/landlock.h>

#include "cred.h"
#include "domain.h"
#include "limits.h"
#include "log.h"
#include "ns.h"
#include "ruleset.h"
#include "setup.h"

#define _LANDLOCK_NS_TYPE(type, flag) flag,

static const u32 landlock_namespace_types[] = {
	/* clang-format off */
	FOR_EACH_NS_TYPE(_LANDLOCK_NS_TYPE)
	/* clang-format on */
};

#undef _LANDLOCK_NS_TYPE

static_assert(ARRAY_SIZE(landlock_namespace_types) ==
	      LANDLOCK_NUM_NAMESPACE_TYPE);

/* Ensures the audit ns_id field can hold ns_common.ns_id without truncation. */
static_assert(sizeof(((struct common_audit_data *)NULL)->u.ns.ns_id) >=
	      sizeof(((struct ns_common *)NULL)->ns_id));

/**
 * landlock_ns_type_to_bit - Convert a namespace type to a compact bitmask
 *
 * @ns_type: Namespace type (``CLONE_NEW*``).
 *
 * Return: The compact bit for @ns_type, or 0 if @ns_type is invalid (with a
 * warning).
 */
u64 landlock_ns_type_to_bit(const u32 ns_type)
{
	size_t i;

	for (i = 0; i < ARRAY_SIZE(landlock_namespace_types); i++) {
		if (landlock_namespace_types[i] == ns_type)
			return BIT_ULL(i);
	}

	WARN_ONCE(1, "Unknown namespace type 0x%x\n", ns_type);
	return 0;
}

/**
 * landlock_ns_types_to_bits - Convert namespace types to a compact bitmask
 *
 * @ns_types: Bitmask of namespace types (``CLONE_NEW*``).
 *
 * Return: The compact bits for all known @ns_types.  Warns if unknown bits are
 * present (callers must pre-mask user input).
 */
u64 landlock_ns_types_to_bits(const u64 ns_types)
{
	u64 bits = 0;
	size_t i;

	/* Callers pre-mask (CLONE_NS_ALL); the WARN guards future callers. */
	WARN_ON_ONCE(ns_types & ~(u64)CLONE_NS_ALL);
	for (i = 0; i < ARRAY_SIZE(landlock_namespace_types); i++) {
		if (ns_types & landlock_namespace_types[i])
			bits |= BIT_ULL(i);
	}
	return bits;
}

static const struct access_masks ns_permission = {
	.permissions = LANDLOCK_PERMISSION_NAMESPACE_USE,
};

/**
 * check_ns_type - Check namespace use permission
 *
 * @ns: The namespace being allocated or installed.
 *
 * Shared check for namespace_init (creation via clone(2), unshare(2),
 * open_tree(2), or fsmount(2)) and namespace_install (use via setns(2)): denies
 * when the namespace type is not in the domain's allowed set.  At allocation
 * time @ns->ns_id is still zero and is logged as such.
 *
 * Return: 0 if allowed, -EPERM if denied.
 */
static int check_ns_type(struct ns_common *const ns)
{
	const struct landlock_cred_security *subject;
	size_t denied_layer;

	subject = landlock_get_applicable_subject(current_cred(), ns_permission,
						  NULL);
	if (!subject)
		return 0;

	denied_layer = landlock_permission_is_denied(
		subject->domain, LANDLOCK_PERMISSION_NAMESPACE_USE,
		landlock_ns_type_to_bit(ns->ns_type));
	if (!denied_layer)
		return 0;

	landlock_log_denial(subject,
			    &(struct landlock_request){
				    .type = LANDLOCK_REQUEST_NAMESPACE,
				    .audit.type = LSM_AUDIT_DATA_NS,
				    .permission =
					    LANDLOCK_PERMISSION_NAMESPACE_USE,
				    .audit.u.ns.ns_type = ns->ns_type,
				    .audit.u.ns.ns_id = ns->ns_id,
				    .layer_plus_one = denied_layer,
			    });
	return -EPERM;
}

static int hook_namespace_init(struct ns_common *const ns)
{
	return check_ns_type(ns);
}

static int hook_namespace_install(const struct nsset *const nsset,
				  struct ns_common *const ns)
{
	return check_ns_type(ns);
}

static struct security_hook_list landlock_hooks[] __ro_after_init = {
	LSM_HOOK_INIT(namespace_init, hook_namespace_init),
	LSM_HOOK_INIT(namespace_install, hook_namespace_install),
};

__init void landlock_add_ns_hooks(void)
{
	security_add_hooks(landlock_hooks, ARRAY_SIZE(landlock_hooks),
			   &landlock_lsmid);
}

#ifdef CONFIG_SECURITY_LANDLOCK_KUNIT_TEST

#include <kunit/test.h>

static void test_ns_type_to_bit(struct kunit *const test)
{
	u64 seen = 0;
	size_t i;

	for (i = 0; i < ARRAY_SIZE(landlock_namespace_types); i++) {
		const u64 bit =
			landlock_ns_type_to_bit(landlock_namespace_types[i]);

		KUNIT_EXPECT_NE(test, 0ULL, bit);
		KUNIT_EXPECT_EQ(test, 0ULL, seen & bit);
		seen |= bit;
	}

	KUNIT_EXPECT_EQ(test, GENMASK_ULL(LANDLOCK_NUM_NAMESPACE_TYPE - 1, 0),
			seen);
}

static void test_ns_type_to_bit_unknown(struct kunit *const test)
{
	if (!IS_ENABLED(CONFIG_BUG))
		kunit_skip(test, "requires CONFIG_BUG");

	/* clang-format off */
	kunit_warning_suppress(test) {
		/* clang-format on */
		KUNIT_EXPECT_EQ(test, 0ULL,
				landlock_ns_type_to_bit(CLONE_THREAD));
		KUNIT_EXPECT_SUPPRESSED_WARNING_COUNT(test, 1);
	}
}

static void test_ns_types_to_bits_all(struct kunit *const test)
{
	KUNIT_EXPECT_EQ(test, GENMASK_ULL(LANDLOCK_NUM_NAMESPACE_TYPE - 1, 0),
			landlock_ns_types_to_bits(CLONE_NS_ALL));
}

static void test_ns_types_to_bits_single(struct kunit *const test)
{
	size_t i;

	for (i = 0; i < ARRAY_SIZE(landlock_namespace_types); i++)
		KUNIT_EXPECT_EQ(
			test,
			landlock_ns_type_to_bit(landlock_namespace_types[i]),
			landlock_ns_types_to_bits(landlock_namespace_types[i]));
}

static void test_ns_types_to_bits_unknown(struct kunit *const test)
{
	if (!IS_ENABLED(CONFIG_BUG))
		kunit_skip(test, "requires CONFIG_BUG");

	/* clang-format off */
	kunit_warning_suppress(test) {
		/* clang-format on */
		KUNIT_EXPECT_EQ(test, 0ULL,
				landlock_ns_types_to_bits(CLONE_THREAD));
		KUNIT_EXPECT_SUPPRESSED_WARNING_COUNT(test, 1);
	}
}

static void test_ns_types_to_bits_zero(struct kunit *const test)
{
	KUNIT_EXPECT_EQ(test, 0ULL, landlock_ns_types_to_bits(0));
}

static struct kunit_case test_cases[] = {
	KUNIT_CASE(test_ns_type_to_bit),
	KUNIT_CASE(test_ns_type_to_bit_unknown),
	KUNIT_CASE(test_ns_types_to_bits_all),
	KUNIT_CASE(test_ns_types_to_bits_single),
	KUNIT_CASE(test_ns_types_to_bits_unknown),
	KUNIT_CASE(test_ns_types_to_bits_zero),
	{}
};

static struct kunit_suite test_suite = {
	.name = "landlock_ns",
	.test_cases = test_cases,
};

kunit_test_suite(test_suite);

#endif /* CONFIG_SECURITY_LANDLOCK_KUNIT_TEST */
