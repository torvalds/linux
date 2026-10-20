// SPDX-License-Identifier: GPL-2.0
/*
 * KUnit test for user namespace map insertion and sorting.
 */

#define pr_fmt(fmt) "user_namespace: " fmt

#include <kunit/test.h>
#include <linux/user_namespace.h>

#define NR_EXTENTS (UID_GID_MAP_MAX_BASE_EXTENTS + 5)

static void user_ns_map_insert(struct kunit *test)
{
	struct uid_gid_map map;
	struct uid_gid_extent extent;
	int i, ret;

	memset(&map, 0, sizeof(map));

	/* Insert up to UID_GID_MAP_MAX_BASE_EXTENTS elements */
	for (i = 0; i < UID_GID_MAP_MAX_BASE_EXTENTS; i++) {
		extent.first = i * 10;
		extent.lower_first = i * 100;
		extent.count = 5;

		ret = uid_gid_map_insert_extent(&map, &extent);
		KUNIT_ASSERT_EQ(test, ret, 0);
	}

	KUNIT_EXPECT_EQ(test, map.nr_extents, UID_GID_MAP_MAX_BASE_EXTENTS);

	/* Verify the elements ended up in the 'extent' array */
	for (i = 0; i < UID_GID_MAP_MAX_BASE_EXTENTS; i++) {
		KUNIT_EXPECT_EQ(test, map.extent[i].first, i * 10);
		KUNIT_EXPECT_EQ(test, map.extent[i].lower_first, i * 100);
		KUNIT_EXPECT_EQ(test, map.extent[i].count, 5);
	}
}

static void user_ns_map_insert_extended(struct kunit *test)
{
	struct uid_gid_map map;
	struct uid_gid_extent extent;
	int i, ret;

	memset(&map, 0, sizeof(map));

	/* Insert more than UID_GID_MAP_MAX_BASE_EXTENTS elements */
	for (i = 0; i < NR_EXTENTS; i++) {
		int value = 9 - i;

		extent.first = value * 10;
		extent.lower_first = value * 100;
		extent.count = 5;

		ret = uid_gid_map_insert_extent(&map, &extent);
		KUNIT_ASSERT_EQ(test, ret, 0);
	}

	KUNIT_EXPECT_EQ(test, map.nr_extents, NR_EXTENTS);

	/* Now sort the map to set up reverse mapping */
	ret = uid_gid_map_sort(&map);
	KUNIT_ASSERT_EQ(test, ret, 0);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, map.reverse);

	/* Verify the elements are in 'forward' and that sorting is correct */
	for (i = 0; i < map.nr_extents; i++) {
		KUNIT_EXPECT_EQ(test, map.forward[i].first, i * 10);
		KUNIT_EXPECT_EQ(test, map.forward[i].lower_first, i * 100);
		KUNIT_EXPECT_EQ(test, map.forward[i].count, 5);

		KUNIT_EXPECT_EQ(test, map.reverse[i].first, i * 10);
		KUNIT_EXPECT_EQ(test, map.reverse[i].lower_first, i * 100);
		KUNIT_EXPECT_EQ(test, map.reverse[i].count, 5);
	}

	kfree(map.forward);
	kfree(map.reverse);
}

static struct kunit_case user_ns_map_test_cases[] = {
	KUNIT_CASE(user_ns_map_insert),
	KUNIT_CASE(user_ns_map_insert_extended),
	{}
};

static struct kunit_suite user_ns_map_test_suite = {
	.name = "user_ns_map",
	.test_cases = user_ns_map_test_cases,
};

kunit_test_suite(user_ns_map_test_suite);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("KUnit test for user namespace map insertion");
MODULE_IMPORT_NS("EXPORTED_FOR_KUNIT_TESTING");
