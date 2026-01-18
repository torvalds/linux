// SPDX-License-Identifier: LGPL-2.1
/*
 *
 *   KUnit tests of SMB2 maperror
 *
 *   Copyright (C) 2025 KylinSoft Co., Ltd. All rights reserved.
 *   Author(s): ChenXiaoSong <chenxiaosong@kylinos.cn>
 *
 */

#include <kunit/test.h>

static void
test_cmp_map(struct kunit *test, const struct status_to_posix_error *expect)
{
	const struct status_to_posix_error *result;

	result = smb2_get_err_map(expect->smb2_status);
	KUNIT_EXPECT_PTR_NE(test, NULL, result);
	KUNIT_EXPECT_EQ(test, expect->smb2_status, result->smb2_status);
	KUNIT_EXPECT_EQ(test, expect->posix_error, result->posix_error);
	KUNIT_EXPECT_STREQ(test, expect->status_string, result->status_string);
}

static void maperror_test_check_search(struct kunit *test)
{
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(smb2_error_map_table); i++)
		test_cmp_map(test, &smb2_error_map_table[i]);
}

static struct kunit_case maperror_test_cases[] = {
	KUNIT_CASE(maperror_test_check_search),
	{}
};

static struct kunit_suite maperror_suite = {
	.name = "smb2_maperror",
	.test_cases = maperror_test_cases,
};

kunit_test_suite(maperror_suite);

MODULE_LICENSE("GPL");
