// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * KUnit tests for commoncap.c security functions
 *
 * Tests for security-critical functions in the capability subsystem,
 * particularly namespace-related capability checks.
 */

#include <kunit/test.h>
#include <linux/user_namespace.h>
#include <linux/uidgid.h>
#include <linux/cred.h>
#include <linux/mnt_idmapping.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/refcount.h>

#ifdef CONFIG_SECURITY_COMMONCAP_KUNIT_TEST

/* Functions are static in commoncap.c, but we can call them since we're
 * included in the same compilation unit when tests are enabled.
 */

/**
 * test_vfsuid_root_in_currentns_init_ns - Test vfsuid_root_in_currentns with init ns
 *
 * Verifies that UID 0 in the init namespace correctly owns the current
 * namespace when running in init_user_ns.
 *
 * @test: KUnit test context
 */
static void test_vfsuid_root_in_currentns_init_ns(struct kunit *test)
{
	vfsuid_t vfsuid;
	kuid_t kuid;

	/* Create UID 0 in init namespace */
	kuid = KUIDT_INIT(0);
	vfsuid = VFSUIDT_INIT(kuid);

	/* In init namespace, UID 0 should own current namespace */
	KUNIT_EXPECT_TRUE(test, vfsuid_root_in_currentns(vfsuid));
}

/**
 * test_vfsuid_root_in_currentns_invalid - Test vfsuid_root_in_currentns with invalid vfsuid
 *
 * Verifies that an invalid vfsuid correctly returns false.
 *
 * @test: KUnit test context
 */
static void test_vfsuid_root_in_currentns_invalid(struct kunit *test)
{
	vfsuid_t invalid_vfsuid;

	/* Use the predefined invalid vfsuid */
	invalid_vfsuid = INVALID_VFSUID;

	/* Invalid vfsuid should return false */
	KUNIT_EXPECT_FALSE(test, vfsuid_root_in_currentns(invalid_vfsuid));
}

/**
 * test_vfsuid_root_in_currentns_nonzero - Test vfsuid_root_in_currentns with non-zero UID
 *
 * Verifies that a non-zero UID correctly returns false.
 *
 * @test: KUnit test context
 */
static void test_vfsuid_root_in_currentns_nonzero(struct kunit *test)
{
	vfsuid_t vfsuid;
	kuid_t kuid;

	/* Create a non-zero UID */
	kuid = KUIDT_INIT(1000);
	vfsuid = VFSUIDT_INIT(kuid);

	/* Non-zero UID should return false */
	KUNIT_EXPECT_FALSE(test, vfsuid_root_in_currentns(vfsuid));
}

/**
 * test_kuid_root_in_ns_init_ns_uid0 - Test kuid_root_in_ns with init namespace and UID 0
 *
 * Verifies that kuid_root_in_ns correctly identifies UID 0 in init namespace.
 * This tests the core namespace traversal logic. In init namespace, UID 0
 * maps to itself, so it should own the namespace.
 *
 * @test: KUnit test context
 */
static void test_kuid_root_in_ns_init_ns_uid0(struct kunit *test)
{
	kuid_t kuid;
	struct user_namespace *init_ns;

	kuid = KUIDT_INIT(0);
	init_ns = &init_user_ns;

	/* UID 0 should own init namespace */
	KUNIT_EXPECT_TRUE(test, kuid_root_in_ns(kuid, init_ns));
}

/**
 * test_kuid_root_in_ns_init_ns_nonzero - Test kuid_root_in_ns with init namespace and non-zero UID
 *
 * Verifies that kuid_root_in_ns correctly rejects non-zero UIDs in init namespace.
 * Only UID 0 should own a namespace.
 *
 * @test: KUnit test context
 */
static void test_kuid_root_in_ns_init_ns_nonzero(struct kunit *test)
{
	kuid_t kuid;
	struct user_namespace *init_ns;

	kuid = KUIDT_INIT(1000);
	init_ns = &init_user_ns;

	/* Non-zero UID should not own namespace */
	KUNIT_EXPECT_FALSE(test, kuid_root_in_ns(kuid, init_ns));
}

/**
 * create_test_user_ns_with_mapping - Create a mock user namespace with UID mapping
 *
 * Creates a minimal user namespace structure for testing where uid 0 in the
 * namespace maps to a specific kuid in the parent namespace.
 *
 * @test: KUnit test context
 * @parent_ns: Parent namespace (typically init_user_ns)
 * @mapped_kuid: The kuid that uid 0 in this namespace maps to in parent
 *
 * Returns: Pointer to allocated namespace, or NULL on failure
 */
static struct user_namespace *create_test_user_ns_with_mapping(struct kunit *test,
								 struct user_namespace *parent_ns,
								 kuid_t mapped_kuid)
{
	struct user_namespace *ns;
	struct uid_gid_extent extent;

	/* Allocate a test namespace - use kzalloc to zero all fields */
	ns = kunit_kzalloc(test, sizeof(*ns), GFP_KERNEL);
	if (!ns)
		return NULL;

	/* Initialize basic namespace structure fields */
	ns->parent = parent_ns;
	ns->level = parent_ns ? parent_ns->level + 1 : 0;
	ns->owner = mapped_kuid;
	ns->group = KGIDT_INIT(0);

	/* Initialize ns_common structure */
	refcount_set(&ns->ns.__ns_ref, 1);
	ns->ns.inum = 0; /* Mock inum */

	/* Set up uid mapping: uid 0 in this namespace maps to mapped_kuid in parent
	 * Format: first (uid in ns) : lower_first (kuid in parent) : count
	 * So: uid 0 in ns -> kuid mapped_kuid in parent
	 * This means from_kuid(ns, mapped_kuid) returns 0
	 */
	extent.first = 0;                              /* uid 0 in this namespace */
	extent.lower_first = __kuid_val(mapped_kuid);  /* maps to this kuid in parent */
	extent.count = 1;

	ns->uid_map.extent[0] = extent;
	ns->uid_map.nr_extents = 1;

	/* Set up gid mapping: gid 0 maps to gid 0 in parent (simplified) */
	extent.first = 0;
	extent.lower_first = 0;
	extent.count = 1;

	ns->gid_map.extent[0] = extent;
	ns->gid_map.nr_extents = 1;

	return ns;
}

/**
 * test_kuid_root_in_ns_with_mapping - Test kuid_root_in_ns with namespace where uid 0
 *				       maps to different kuid
 *
 * Creates a user namespace where uid 0 maps to kuid 1000 in the parent namespace.
 * Verifies that kuid_root_in_ns correctly identifies kuid 1000 as owning the namespace.
 *
 * Note: kuid_root_in_ns walks up the namespace hierarchy, so it checks the current
 * namespace first, then parent, then parent's parent, etc. So:
 * - kuid 1000 owns test_ns because from_kuid(test_ns, 1000) == 0
 * - kuid 0 also owns test_ns because from_kuid(init_user_ns, 0) == 0
 *   (checked in parent)
 *
 * This tests the actual functionality as requested: creating namespaces with
 * different values for the namespace's uid 0.
 *
 * @test: KUnit test context
 */
static void test_kuid_root_in_ns_with_mapping(struct kunit *test)
{
	struct user_namespace *test_ns;
	struct user_namespace *parent_ns;
	kuid_t mapped_kuid, other_kuid;

	parent_ns = &init_user_ns;
	mapped_kuid = KUIDT_INIT(1000);
	other_kuid = KUIDT_INIT(2000);

	test_ns = create_test_user_ns_with_mapping(test, parent_ns, mapped_kuid);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, test_ns);

	/* kuid 1000 should own test_ns because it maps to uid 0 in test_ns */
	KUNIT_EXPECT_TRUE(test, kuid_root_in_ns(mapped_kuid, test_ns));

	/* kuid 0 should also own test_ns (checked via parent init_user_ns) */
	KUNIT_EXPECT_TRUE(test, kuid_root_in_ns(KUIDT_INIT(0), test_ns));

	/* Other kuids should not own test_ns */
	KUNIT_EXPECT_FALSE(test, kuid_root_in_ns(other_kuid, test_ns));
	KUNIT_EXPECT_FALSE(test, kuid_root_in_ns(KUIDT_INIT(500), test_ns));
}

/**
 * test_kuid_root_in_ns_with_different_mappings - Test with multiple namespaces
 *
 * Creates multiple user namespaces with different UID mappings to verify
 * that kuid_root_in_ns correctly distinguishes between namespaces.
 *
 * Each namespace maps uid 0 to a different kuid, and we verify that each
 * kuid only owns its corresponding namespace (plus kuid 0 owns all via
 * init_user_ns parent).
 *
 * @test: KUnit test context
 */
static void test_kuid_root_in_ns_with_different_mappings(struct kunit *test)
{
	struct user_namespace *ns1, *ns2, *ns3;

	/* Create three independent namespaces, each mapping uid 0 to different kuids */
	ns1 = create_test_user_ns_with_mapping(test, &init_user_ns, KUIDT_INIT(1000));
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, ns1);

	ns2 = create_test_user_ns_with_mapping(test, &init_user_ns, KUIDT_INIT(2000));
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, ns2);

	ns3 = create_test_user_ns_with_mapping(test, &init_user_ns, KUIDT_INIT(3000));
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, ns3);

	/* Test ns1: kuid 1000 owns it, kuid 0 owns it (via parent), others do not */
	KUNIT_EXPECT_TRUE(test, kuid_root_in_ns(KUIDT_INIT(1000), ns1));
	KUNIT_EXPECT_TRUE(test, kuid_root_in_ns(KUIDT_INIT(0), ns1));
	KUNIT_EXPECT_FALSE(test, kuid_root_in_ns(KUIDT_INIT(2000), ns1));
	KUNIT_EXPECT_FALSE(test, kuid_root_in_ns(KUIDT_INIT(3000), ns1));

	/* Test ns2: kuid 2000 owns it, kuid 0 owns it (via parent), others do not */
	KUNIT_EXPECT_TRUE(test, kuid_root_in_ns(KUIDT_INIT(2000), ns2));
	KUNIT_EXPECT_TRUE(test, kuid_root_in_ns(KUIDT_INIT(0), ns2));
	KUNIT_EXPECT_FALSE(test, kuid_root_in_ns(KUIDT_INIT(1000), ns2));
	KUNIT_EXPECT_FALSE(test, kuid_root_in_ns(KUIDT_INIT(3000), ns2));

	/* Test ns3: kuid 3000 owns it, kuid 0 owns it (via parent), others do not */
	KUNIT_EXPECT_TRUE(test, kuid_root_in_ns(KUIDT_INIT(3000), ns3));
	KUNIT_EXPECT_TRUE(test, kuid_root_in_ns(KUIDT_INIT(0), ns3));
	KUNIT_EXPECT_FALSE(test, kuid_root_in_ns(KUIDT_INIT(1000), ns3));
	KUNIT_EXPECT_FALSE(test, kuid_root_in_ns(KUIDT_INIT(2000), ns3));
}

static struct kunit_case commoncap_test_cases[] = {
	KUNIT_CASE(test_vfsuid_root_in_currentns_init_ns),
	KUNIT_CASE(test_vfsuid_root_in_currentns_invalid),
	KUNIT_CASE(test_vfsuid_root_in_currentns_nonzero),
	KUNIT_CASE(test_kuid_root_in_ns_init_ns_uid0),
	KUNIT_CASE(test_kuid_root_in_ns_init_ns_nonzero),
	KUNIT_CASE(test_kuid_root_in_ns_with_mapping),
	KUNIT_CASE(test_kuid_root_in_ns_with_different_mappings),
	{}
};

static struct kunit_suite commoncap_test_suite = {
	.name = "commoncap",
	.test_cases = commoncap_test_cases,
};

kunit_test_suite(commoncap_test_suite);

MODULE_LICENSE("GPL");

#endif /* CONFIG_SECURITY_COMMONCAP_KUNIT_TEST */
