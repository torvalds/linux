// SPDX-License-Identifier: GPL-2.0-only
/*
 * KUnit tests for element fragmentation
 *
 * Copyright (C) 2023-2024 Intel Corporation
 */
#include <linux/ieee80211.h>
#include <net/cfg80211.h>
#include <kunit/test.h>

static void defragment_0(struct kunit *test)
{
	ssize_t ret;
	static const u8 input[] = {
		[0] = WLAN_EID_EXTENSION,
		[1] = 254,
		[2] = WLAN_EID_EXT_EHT_MULTI_LINK,
		[27] = 27,
		[123] = 123,
		[254 + 2] = WLAN_EID_FRAGMENT,
		[254 + 3] = 7,
		[254 + 3 + 7] = 0, /* for size */
	};
	u8 *data = kunit_kzalloc(test, sizeof(input), GFP_KERNEL);

	KUNIT_ASSERT_NOT_NULL(test, data);

	ret = cfg80211_defragment_element((void *)input,
					  input, sizeof(input),
					  NULL, 0,
					  WLAN_EID_FRAGMENT);
	KUNIT_EXPECT_EQ(test, ret, 253);
	ret = cfg80211_defragment_element((void *)input,
					  input, sizeof(input),
					  data, ret,
					  WLAN_EID_FRAGMENT);
	KUNIT_EXPECT_EQ(test, ret, 253);
	KUNIT_EXPECT_MEMEQ(test, data, input + 3, 253);
}

static void defragment_1(struct kunit *test)
{
	ssize_t ret;
	static const u8 input[] = {
		[0] = WLAN_EID_EXTENSION,
		[1] = 255,
		[2] = WLAN_EID_EXT_EHT_MULTI_LINK,
		[27] = 27,
		[123] = 123,
		[255 + 2] = WLAN_EID_FRAGMENT,
		[255 + 3] = 7,
		[255 + 3 + 1] = 0xaa,
		[255 + 3 + 8] = WLAN_EID_FRAGMENT, /* not used */
		[255 + 3 + 9] = 1,
		[255 + 3 + 10] = 0, /* for size */
	};
	u8 *data = kunit_kzalloc(test, sizeof(input), GFP_KERNEL);
	const struct element *elem;
	int count = 0;

	KUNIT_ASSERT_NOT_NULL(test, data);

	for_each_element(elem, input, sizeof(input))
		count++;

	/* check the elements are right */
	KUNIT_ASSERT_EQ(test, count, 3);

	ret = cfg80211_defragment_element((void *)input,
					  input, sizeof(input),
					  NULL, 0,
					  WLAN_EID_FRAGMENT);
	KUNIT_EXPECT_EQ(test, ret, 254 + 7);
	ret = cfg80211_defragment_element((void *)input,
					  input, sizeof(input),
					  data, ret,
					  WLAN_EID_FRAGMENT);
	/* this means the last fragment was not used */
	KUNIT_EXPECT_EQ(test, ret, 254 + 7);
	KUNIT_EXPECT_MEMEQ(test, data, input + 3, 254);
	KUNIT_EXPECT_MEMEQ(test, data + 254, input + 255 + 4, 7);
}

static void defragment_2(struct kunit *test)
{
	ssize_t ret;
	static const u8 input[] = {
		[0] = WLAN_EID_EXTENSION,
		[1] = 255,
		[2] = WLAN_EID_EXT_EHT_MULTI_LINK,
		[27] = 27,
		[123] = 123,

		[257 + 0] = WLAN_EID_FRAGMENT,
		[257 + 1] = 255,
		[257 + 20] = 0xaa,

		[2 * 257 + 0] = WLAN_EID_FRAGMENT,
		[2 * 257 + 1] = 1,
		[2 * 257 + 2] = 0xcc,
		[2 * 257 + 3] = WLAN_EID_FRAGMENT, /* not used */
		[2 * 257 + 4] = 1,
		[2 * 257 + 5] = 0, /* for size */
	};
	u8 *data = kunit_kzalloc(test, sizeof(input), GFP_KERNEL);
	const struct element *elem;
	int count = 0;

	KUNIT_ASSERT_NOT_NULL(test, data);

	for_each_element(elem, input, sizeof(input))
		count++;

	/* check the elements are right */
	KUNIT_ASSERT_EQ(test, count, 4);

	ret = cfg80211_defragment_element((void *)input,
					  input, sizeof(input),
					  NULL, 0,
					  WLAN_EID_FRAGMENT);
	/* this means the last fragment was not used */
	KUNIT_EXPECT_EQ(test, ret, 254 + 255 + 1);
	ret = cfg80211_defragment_element((void *)input,
					  input, sizeof(input),
					  data, ret,
					  WLAN_EID_FRAGMENT);
	KUNIT_EXPECT_EQ(test, ret, 254 + 255 + 1);
	KUNIT_EXPECT_MEMEQ(test, data, input + 3, 254);
	KUNIT_EXPECT_MEMEQ(test, data + 254, input + 257 + 2, 255);
	KUNIT_EXPECT_MEMEQ(test, data + 254 + 255, input + 2 * 257 + 2, 1);
}

static void defragment_at_end(struct kunit *test)
{
	ssize_t ret;
	static const u8 input[] = {
		[0] = WLAN_EID_EXTENSION,
		[1] = 255,
		[2] = WLAN_EID_EXT_EHT_MULTI_LINK,
		[27] = 27,
		[123] = 123,
		[255 + 2] = WLAN_EID_FRAGMENT,
		[255 + 3] = 7,
		[255 + 3 + 7] = 0, /* for size */
	};
	u8 *data = kunit_kzalloc(test, sizeof(input), GFP_KERNEL);

	KUNIT_ASSERT_NOT_NULL(test, data);

	ret = cfg80211_defragment_element((void *)input,
					  input, sizeof(input),
					  NULL, 0,
					  WLAN_EID_FRAGMENT);
	KUNIT_EXPECT_EQ(test, ret, 254 + 7);
	ret = cfg80211_defragment_element((void *)input,
					  input, sizeof(input),
					  data, ret,
					  WLAN_EID_FRAGMENT);
	KUNIT_EXPECT_EQ(test, ret, 254 + 7);
	KUNIT_EXPECT_MEMEQ(test, data, input + 3, 254);
	KUNIT_EXPECT_MEMEQ(test, data + 254, input + 255 + 4, 7);
}

static struct kunit_case element_fragmentation_test_cases[] = {
	KUNIT_CASE(defragment_0),
	KUNIT_CASE(defragment_1),
	KUNIT_CASE(defragment_2),
	KUNIT_CASE(defragment_at_end),
	{}
};

static struct kunit_suite element_fragmentation = {
	.name = "cfg80211-element-defragmentation",
	.test_cases = element_fragmentation_test_cases,
};

kunit_test_suite(element_fragmentation);
