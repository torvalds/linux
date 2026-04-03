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
#include "cifsglob.h"
#include "smb2glob.h"
#include "smb2proto.h"

static void
test_cmp_map(struct kunit *test, const struct status_to_posix_error *expect)
{
	const struct status_to_posix_error *result;

	result = smb2_get_err_map_test(expect->smb2_status);
	KUNIT_ASSERT_NOT_NULL(test, result);
	KUNIT_EXPECT_EQ(test, expect->smb2_status, result->smb2_status);
	KUNIT_EXPECT_EQ(test, expect->posix_error, result->posix_error);
	KUNIT_EXPECT_STREQ(test, expect->status_string, result->status_string);
}

static void maperror_test_check_search(struct kunit *test)
{
	unsigned int i;

	for (i = 0; i < smb2_error_map_num; i++)
		test_cmp_map(test, &smb2_error_map_table_test[i]);
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
MODULE_DESCRIPTION("KUnit tests of SMB2 maperror");
