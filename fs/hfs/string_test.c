// SPDX-License-Identifier: GPL-2.0
/*
 * KUnit tests for HFS string operations
 *
 * Copyright (C) 2025 Viacheslav Dubeyko <slava@dubeyko.com>
 */

#include <kunit/test.h>
#include <linux/dcache.h>
#include "hfs_fs.h"

/* Test hfs_strcmp function */
static void hfs_strcmp_test(struct kunit *test)
{
	/* Test equal strings */
	KUNIT_EXPECT_EQ(test, 0, hfs_strcmp("hello", 5, "hello", 5));
	KUNIT_EXPECT_EQ(test, 0, hfs_strcmp("test", 4, "test", 4));
	KUNIT_EXPECT_EQ(test, 0, hfs_strcmp("", 0, "", 0));

	/* Test unequal strings */
	KUNIT_EXPECT_NE(test, 0, hfs_strcmp("hello", 5, "world", 5));
	KUNIT_EXPECT_NE(test, 0, hfs_strcmp("test", 4, "testing", 7));

	/* Test different lengths */
	KUNIT_EXPECT_LT(test, hfs_strcmp("test", 4, "testing", 7), 0);
	KUNIT_EXPECT_GT(test, hfs_strcmp("testing", 7, "test", 4), 0);

	/* Test case insensitive comparison (HFS should handle case) */
	KUNIT_EXPECT_EQ(test, 0, hfs_strcmp("Test", 4, "TEST", 4));
	KUNIT_EXPECT_EQ(test, 0, hfs_strcmp("hello", 5, "HELLO", 5));

	/* Test with special characters */
	KUNIT_EXPECT_EQ(test, 0, hfs_strcmp("file.txt", 8, "file.txt", 8));
	KUNIT_EXPECT_NE(test, 0, hfs_strcmp("file.txt", 8, "file.dat", 8));

	/* Test boundary cases */
	KUNIT_EXPECT_EQ(test, 0, hfs_strcmp("a", 1, "a", 1));
	KUNIT_EXPECT_NE(test, 0, hfs_strcmp("a", 1, "b", 1));
}

/* Test hfs_hash_dentry function */
static void hfs_hash_dentry_test(struct kunit *test)
{
	struct qstr test_name1, test_name2, test_name3;
	struct dentry dentry = {};
	char name1[] = "testfile";
	char name2[] = "TestFile";
	char name3[] = "different";

	/* Initialize test strings */
	test_name1.name = name1;
	test_name1.len = strlen(name1);
	test_name1.hash = 0;

	test_name2.name = name2;
	test_name2.len = strlen(name2);
	test_name2.hash = 0;

	test_name3.name = name3;
	test_name3.len = strlen(name3);
	test_name3.hash = 0;

	/* Test hashing */
	KUNIT_EXPECT_EQ(test, 0, hfs_hash_dentry(&dentry, &test_name1));
	KUNIT_EXPECT_EQ(test, 0, hfs_hash_dentry(&dentry, &test_name2));
	KUNIT_EXPECT_EQ(test, 0, hfs_hash_dentry(&dentry, &test_name3));

	/* Case insensitive names should hash the same */
	KUNIT_EXPECT_EQ(test, test_name1.hash, test_name2.hash);

	/* Different names should have different hashes */
	KUNIT_EXPECT_NE(test, test_name1.hash, test_name3.hash);
}

/* Test hfs_compare_dentry function */
static void hfs_compare_dentry_test(struct kunit *test)
{
	struct qstr test_name;
	struct dentry dentry = {};
	char name[] = "TestFile";

	test_name.name = name;
	test_name.len = strlen(name);

	/* Test exact match */
	KUNIT_EXPECT_EQ(test, 0, hfs_compare_dentry(&dentry, 8,
						    "TestFile", &test_name));

	/* Test case insensitive match */
	KUNIT_EXPECT_EQ(test, 0, hfs_compare_dentry(&dentry, 8,
						    "testfile", &test_name));
	KUNIT_EXPECT_EQ(test, 0, hfs_compare_dentry(&dentry, 8,
						    "TESTFILE", &test_name));

	/* Test different names */
	KUNIT_EXPECT_EQ(test, 1, hfs_compare_dentry(&dentry, 8,
						    "DiffFile", &test_name));

	/* Test different lengths */
	KUNIT_EXPECT_EQ(test, 1, hfs_compare_dentry(&dentry, 7,
						    "TestFil", &test_name));
	KUNIT_EXPECT_EQ(test, 1, hfs_compare_dentry(&dentry, 9,
						    "TestFiles", &test_name));

	/* Test empty string */
	test_name.name = "";
	test_name.len = 0;
	KUNIT_EXPECT_EQ(test, 0, hfs_compare_dentry(&dentry, 0, "", &test_name));

	/* Test HFS_NAMELEN boundary */
	test_name.name = "This_is_a_very_long_filename_that_exceeds_normal_limits";
	test_name.len = strlen(test_name.name);
	KUNIT_EXPECT_EQ(test, 0, hfs_compare_dentry(&dentry, HFS_NAMELEN,
			"This_is_a_very_long_filename_th", &test_name));
}

static struct kunit_case hfs_string_test_cases[] = {
	KUNIT_CASE(hfs_strcmp_test),
	KUNIT_CASE(hfs_hash_dentry_test),
	KUNIT_CASE(hfs_compare_dentry_test),
	{}
};

static struct kunit_suite hfs_string_test_suite = {
	.name = "hfs_string",
	.test_cases = hfs_string_test_cases,
};

kunit_test_suite(hfs_string_test_suite);

MODULE_DESCRIPTION("KUnit tests for HFS string operations");
MODULE_LICENSE("GPL");
MODULE_IMPORT_NS("EXPORTED_FOR_KUNIT_TESTING");
