// SPDX-License-Identifier: GPL-2.0
/*
 * KUnit tests for HFS+ Unicode string operations
 *
 * Copyright (C) 2025 Viacheslav Dubeyko <slava@dubeyko.com>
 */

#include <kunit/test.h>
#include <linux/nls.h>
#include <linux/dcache.h>
#include <linux/stringhash.h>
#include "hfsplus_fs.h"
#include "unicode.h"

struct test_mock_string_env {
	struct hfsplus_unistr str1;
	struct hfsplus_unistr str2;
	char *buf;
	u32 buf_size;
};

static struct test_mock_string_env *setup_mock_str_env(u32 buf_size)
{
	struct test_mock_string_env *env;

	env = kzalloc_obj(struct test_mock_string_env);
	if (!env)
		return NULL;

	env->buf = kzalloc(buf_size, GFP_KERNEL);
	if (!env->buf) {
		kfree(env);
		return NULL;
	}

	env->buf_size = buf_size;

	return env;
}

static void free_mock_str_env(struct test_mock_string_env *env)
{
	kfree(env->buf);
	kfree(env);
}

/* Helper function to create hfsplus_unistr */
static void create_unistr(struct hfsplus_unistr *ustr, const char *ascii_str)
{
	int len = strlen(ascii_str);
	int i;

	memset(ustr->unicode, 0, sizeof(ustr->unicode));

	ustr->length = cpu_to_be16(len);
	for (i = 0; i < len && i < HFSPLUS_MAX_STRLEN; i++)
		ustr->unicode[i] = cpu_to_be16((u16)ascii_str[i]);
}

static void corrupt_unistr(struct hfsplus_unistr *ustr)
{
	ustr->length = cpu_to_be16(U16_MAX);
}

/* Test hfsplus_strcasecmp function */
static void hfsplus_strcasecmp_test(struct kunit *test)
{
	struct test_mock_string_env *mock_env;

	mock_env = setup_mock_str_env(HFSPLUS_MAX_STRLEN + 1);
	KUNIT_ASSERT_NOT_NULL(test, mock_env);

	/* Test identical strings */
	create_unistr(&mock_env->str1, "hello");
	create_unistr(&mock_env->str2, "hello");
	KUNIT_EXPECT_EQ(test, 0, hfsplus_strcasecmp(&mock_env->str1,
						    &mock_env->str2));

	/* Test case insensitive comparison */
	create_unistr(&mock_env->str1, "Hello");
	create_unistr(&mock_env->str2, "hello");
	KUNIT_EXPECT_EQ(test, 0, hfsplus_strcasecmp(&mock_env->str1,
						    &mock_env->str2));

	create_unistr(&mock_env->str1, "HELLO");
	create_unistr(&mock_env->str2, "hello");
	KUNIT_EXPECT_EQ(test, 0, hfsplus_strcasecmp(&mock_env->str1,
						    &mock_env->str2));

	/* Test different strings */
	create_unistr(&mock_env->str1, "apple");
	create_unistr(&mock_env->str2, "banana");
	KUNIT_EXPECT_LT(test, hfsplus_strcasecmp(&mock_env->str1,
						 &mock_env->str2), 0);

	create_unistr(&mock_env->str1, "zebra");
	create_unistr(&mock_env->str2, "apple");
	KUNIT_EXPECT_GT(test, hfsplus_strcasecmp(&mock_env->str1,
						 &mock_env->str2), 0);

	/* Test different lengths */
	create_unistr(&mock_env->str1, "test");
	create_unistr(&mock_env->str2, "testing");
	KUNIT_EXPECT_LT(test, hfsplus_strcasecmp(&mock_env->str1,
						 &mock_env->str2), 0);

	create_unistr(&mock_env->str1, "testing");
	create_unistr(&mock_env->str2, "test");
	KUNIT_EXPECT_GT(test, hfsplus_strcasecmp(&mock_env->str1,
						 &mock_env->str2), 0);

	/* Test empty strings */
	create_unistr(&mock_env->str1, "");
	create_unistr(&mock_env->str2, "");
	KUNIT_EXPECT_EQ(test, 0, hfsplus_strcasecmp(&mock_env->str1,
						    &mock_env->str2));

	create_unistr(&mock_env->str1, "");
	create_unistr(&mock_env->str2, "test");
	KUNIT_EXPECT_LT(test, hfsplus_strcasecmp(&mock_env->str1,
						 &mock_env->str2), 0);

	/* Test single characters */
	create_unistr(&mock_env->str1, "A");
	create_unistr(&mock_env->str2, "a");
	KUNIT_EXPECT_EQ(test, 0, hfsplus_strcasecmp(&mock_env->str1,
						    &mock_env->str2));

	create_unistr(&mock_env->str1, "A");
	create_unistr(&mock_env->str2, "B");
	KUNIT_EXPECT_LT(test, hfsplus_strcasecmp(&mock_env->str1,
						 &mock_env->str2), 0);

	/* Test maximum length strings */
	memset(mock_env->buf, 'a', HFSPLUS_MAX_STRLEN);
	mock_env->buf[HFSPLUS_MAX_STRLEN] = '\0';
	create_unistr(&mock_env->str1, mock_env->buf);
	create_unistr(&mock_env->str2, mock_env->buf);
	KUNIT_EXPECT_EQ(test, 0, hfsplus_strcasecmp(&mock_env->str1,
						    &mock_env->str2));

	/* Change one character in the middle */
	mock_env->buf[HFSPLUS_MAX_STRLEN / 2] = 'b';
	create_unistr(&mock_env->str2, mock_env->buf);
	KUNIT_EXPECT_LT(test, hfsplus_strcasecmp(&mock_env->str1,
						 &mock_env->str2), 0);

	/* Test corrupted strings */
	create_unistr(&mock_env->str1, "");
	corrupt_unistr(&mock_env->str1);
	create_unistr(&mock_env->str2, "");
	KUNIT_EXPECT_NE(test, 0, hfsplus_strcasecmp(&mock_env->str1,
						    &mock_env->str2));

	create_unistr(&mock_env->str1, "");
	create_unistr(&mock_env->str2, "");
	corrupt_unistr(&mock_env->str2);
	KUNIT_EXPECT_NE(test, 0, hfsplus_strcasecmp(&mock_env->str1,
						    &mock_env->str2));

	create_unistr(&mock_env->str1, "test");
	corrupt_unistr(&mock_env->str1);
	create_unistr(&mock_env->str2, "testing");
	KUNIT_EXPECT_GT(test, hfsplus_strcasecmp(&mock_env->str1,
						 &mock_env->str2), 0);

	create_unistr(&mock_env->str1, "test");
	create_unistr(&mock_env->str2, "testing");
	corrupt_unistr(&mock_env->str2);
	KUNIT_EXPECT_LT(test, hfsplus_strcasecmp(&mock_env->str1,
						 &mock_env->str2), 0);

	create_unistr(&mock_env->str1, "testing");
	corrupt_unistr(&mock_env->str1);
	create_unistr(&mock_env->str2, "test");
	KUNIT_EXPECT_GT(test, hfsplus_strcasecmp(&mock_env->str1,
						 &mock_env->str2), 0);

	create_unistr(&mock_env->str1, "testing");
	create_unistr(&mock_env->str2, "test");
	corrupt_unistr(&mock_env->str2);
	KUNIT_EXPECT_LT(test, hfsplus_strcasecmp(&mock_env->str1,
						 &mock_env->str2), 0);

	free_mock_str_env(mock_env);
}

/* Test hfsplus_strcmp function (case-sensitive) */
static void hfsplus_strcmp_test(struct kunit *test)
{
	struct test_mock_string_env *mock_env;

	mock_env = setup_mock_str_env(HFSPLUS_MAX_STRLEN + 1);
	KUNIT_ASSERT_NOT_NULL(test, mock_env);

	/* Test identical strings */
	create_unistr(&mock_env->str1, "hello");
	create_unistr(&mock_env->str2, "hello");
	KUNIT_EXPECT_EQ(test, 0, hfsplus_strcmp(&mock_env->str1,
						&mock_env->str2));

	/* Test case sensitive comparison - should NOT be equal */
	create_unistr(&mock_env->str1, "Hello");
	create_unistr(&mock_env->str2, "hello");
	KUNIT_EXPECT_NE(test, 0, hfsplus_strcmp(&mock_env->str1,
						&mock_env->str2));
	 /* 'H' < 'h' in Unicode */
	KUNIT_EXPECT_LT(test, hfsplus_strcmp(&mock_env->str1,
					     &mock_env->str2), 0);

	/* Test lexicographic ordering */
	create_unistr(&mock_env->str1, "apple");
	create_unistr(&mock_env->str2, "banana");
	KUNIT_EXPECT_LT(test, hfsplus_strcmp(&mock_env->str1,
					     &mock_env->str2), 0);

	create_unistr(&mock_env->str1, "zebra");
	create_unistr(&mock_env->str2, "apple");
	KUNIT_EXPECT_GT(test, hfsplus_strcmp(&mock_env->str1,
					     &mock_env->str2), 0);

	/* Test different lengths with common prefix */
	create_unistr(&mock_env->str1, "test");
	create_unistr(&mock_env->str2, "testing");
	KUNIT_EXPECT_LT(test, hfsplus_strcmp(&mock_env->str1,
					     &mock_env->str2), 0);

	create_unistr(&mock_env->str1, "testing");
	create_unistr(&mock_env->str2, "test");
	KUNIT_EXPECT_GT(test, hfsplus_strcmp(&mock_env->str1,
					     &mock_env->str2), 0);

	/* Test empty strings */
	create_unistr(&mock_env->str1, "");
	create_unistr(&mock_env->str2, "");
	KUNIT_EXPECT_EQ(test, 0, hfsplus_strcmp(&mock_env->str1,
						&mock_env->str2));

	/* Test maximum length strings */
	memset(mock_env->buf, 'a', HFSPLUS_MAX_STRLEN);
	mock_env->buf[HFSPLUS_MAX_STRLEN] = '\0';
	create_unistr(&mock_env->str1, mock_env->buf);
	create_unistr(&mock_env->str2, mock_env->buf);
	KUNIT_EXPECT_EQ(test, 0, hfsplus_strcmp(&mock_env->str1,
						&mock_env->str2));

	/* Change one character in the middle */
	mock_env->buf[HFSPLUS_MAX_STRLEN / 2] = 'b';
	create_unistr(&mock_env->str2, mock_env->buf);
	KUNIT_EXPECT_LT(test, hfsplus_strcmp(&mock_env->str1,
					     &mock_env->str2), 0);

	/* Test corrupted strings */
	create_unistr(&mock_env->str1, "");
	corrupt_unistr(&mock_env->str1);
	create_unistr(&mock_env->str2, "");
	KUNIT_EXPECT_NE(test, 0, hfsplus_strcmp(&mock_env->str1,
						&mock_env->str2));

	create_unistr(&mock_env->str1, "");
	create_unistr(&mock_env->str2, "");
	corrupt_unistr(&mock_env->str2);
	KUNIT_EXPECT_NE(test, 0, hfsplus_strcmp(&mock_env->str1,
						&mock_env->str2));

	create_unistr(&mock_env->str1, "test");
	corrupt_unistr(&mock_env->str1);
	create_unistr(&mock_env->str2, "testing");
	KUNIT_EXPECT_LT(test, hfsplus_strcmp(&mock_env->str1,
					     &mock_env->str2), 0);

	create_unistr(&mock_env->str1, "test");
	create_unistr(&mock_env->str2, "testing");
	corrupt_unistr(&mock_env->str2);
	KUNIT_EXPECT_LT(test, hfsplus_strcmp(&mock_env->str1,
					     &mock_env->str2), 0);

	create_unistr(&mock_env->str1, "testing");
	corrupt_unistr(&mock_env->str1);
	create_unistr(&mock_env->str2, "test");
	KUNIT_EXPECT_GT(test, hfsplus_strcmp(&mock_env->str1,
					     &mock_env->str2), 0);

	create_unistr(&mock_env->str1, "testing");
	create_unistr(&mock_env->str2, "test");
	corrupt_unistr(&mock_env->str2);
	KUNIT_EXPECT_GT(test, hfsplus_strcmp(&mock_env->str1,
					     &mock_env->str2), 0);

	free_mock_str_env(mock_env);
}

/* Test Unicode edge cases */
static void hfsplus_unicode_edge_cases_test(struct kunit *test)
{
	struct test_mock_string_env *mock_env;

	mock_env = setup_mock_str_env(HFSPLUS_MAX_STRLEN + 1);
	KUNIT_ASSERT_NOT_NULL(test, mock_env);

	/* Test with special characters */
	mock_env->str1.length = cpu_to_be16(3);
	mock_env->str1.unicode[0] = cpu_to_be16(0x00E9); /* é */
	mock_env->str1.unicode[1] = cpu_to_be16(0x00F1); /* ñ */
	mock_env->str1.unicode[2] = cpu_to_be16(0x00FC); /* ü */

	mock_env->str2.length = cpu_to_be16(3);
	mock_env->str2.unicode[0] = cpu_to_be16(0x00E9); /* é */
	mock_env->str2.unicode[1] = cpu_to_be16(0x00F1); /* ñ */
	mock_env->str2.unicode[2] = cpu_to_be16(0x00FC); /* ü */

	KUNIT_EXPECT_EQ(test, 0, hfsplus_strcmp(&mock_env->str1,
						&mock_env->str2));
	KUNIT_EXPECT_EQ(test, 0, hfsplus_strcasecmp(&mock_env->str1,
						    &mock_env->str2));

	/* Test with different special characters */
	mock_env->str2.unicode[1] = cpu_to_be16(0x00F2); /* ò */
	KUNIT_EXPECT_NE(test, 0, hfsplus_strcmp(&mock_env->str1,
						&mock_env->str2));

	/* Test null characters within string (should be handled correctly) */
	mock_env->str1.length = cpu_to_be16(3);
	mock_env->str1.unicode[0] = cpu_to_be16('a');
	mock_env->str1.unicode[1] = cpu_to_be16(0x0000); /* null */
	mock_env->str1.unicode[2] = cpu_to_be16('b');

	mock_env->str2.length = cpu_to_be16(3);
	mock_env->str2.unicode[0] = cpu_to_be16('a');
	mock_env->str2.unicode[1] = cpu_to_be16(0x0000); /* null */
	mock_env->str2.unicode[2] = cpu_to_be16('b');

	KUNIT_EXPECT_EQ(test, 0, hfsplus_strcmp(&mock_env->str1,
						&mock_env->str2));

	free_mock_str_env(mock_env);
}

/* Test boundary conditions */
static void hfsplus_unicode_boundary_test(struct kunit *test)
{
	struct test_mock_string_env *mock_env;
	int i;

	mock_env = setup_mock_str_env(HFSPLUS_MAX_STRLEN + 1);
	KUNIT_ASSERT_NOT_NULL(test, mock_env);

	/* Test maximum length boundary */
	mock_env->str1.length = cpu_to_be16(HFSPLUS_MAX_STRLEN);
	mock_env->str2.length = cpu_to_be16(HFSPLUS_MAX_STRLEN);

	for (i = 0; i < HFSPLUS_MAX_STRLEN; i++) {
		mock_env->str1.unicode[i] = cpu_to_be16('A');
		mock_env->str2.unicode[i] = cpu_to_be16('A');
	}

	KUNIT_EXPECT_EQ(test, 0, hfsplus_strcmp(&mock_env->str1,
						&mock_env->str2));

	/* Change last character */
	mock_env->str2.unicode[HFSPLUS_MAX_STRLEN - 1] = cpu_to_be16('B');
	KUNIT_EXPECT_LT(test, hfsplus_strcmp(&mock_env->str1,
					     &mock_env->str2), 0);

	/* Test zero length strings */
	mock_env->str1.length = cpu_to_be16(0);
	mock_env->str2.length = cpu_to_be16(0);
	KUNIT_EXPECT_EQ(test, 0, hfsplus_strcmp(&mock_env->str1,
						&mock_env->str2));
	KUNIT_EXPECT_EQ(test, 0, hfsplus_strcasecmp(&mock_env->str1,
						    &mock_env->str2));

	/* Test one character vs empty */
	mock_env->str1.length = cpu_to_be16(1);
	mock_env->str1.unicode[0] = cpu_to_be16('A');
	mock_env->str2.length = cpu_to_be16(0);
	KUNIT_EXPECT_GT(test, hfsplus_strcmp(&mock_env->str1,
					     &mock_env->str2), 0);
	KUNIT_EXPECT_GT(test, hfsplus_strcasecmp(&mock_env->str1,
						 &mock_env->str2), 0);

	free_mock_str_env(mock_env);
}

/* Mock superblock and NLS table for testing hfsplus_uni2asc */
struct test_mock_sb {
	struct nls_table nls;
	struct hfsplus_sb_info sb_info;
	struct super_block sb;
};

static struct test_mock_sb *setup_mock_sb(void)
{
	struct test_mock_sb *ptr;

	ptr = kzalloc_obj(struct test_mock_sb);
	if (!ptr)
		return NULL;

	ptr->nls.charset = "utf8";
	ptr->nls.uni2char = NULL; /* Will use default behavior */
	ptr->sb_info.nls = &ptr->nls;
	ptr->sb.s_fs_info = &ptr->sb_info;

	/* Set default flags - no decomposition, no case folding */
	clear_bit(HFSPLUS_SB_NODECOMPOSE, &ptr->sb_info.flags);
	clear_bit(HFSPLUS_SB_CASEFOLD, &ptr->sb_info.flags);

	return ptr;
}

static void free_mock_sb(struct test_mock_sb *ptr)
{
	kfree(ptr);
}

/* Simple uni2char implementation for testing */
static int test_uni2char(wchar_t uni, unsigned char *out, int boundlen)
{
	if (boundlen <= 0)
		return -ENAMETOOLONG;

	if (uni < 0x80) {
		*out = (unsigned char)uni;
		return 1;
	}

	/* For non-ASCII, just use '?' as fallback */
	*out = '?';
	return 1;
}

/* Test hfsplus_uni2asc basic functionality */
static void hfsplus_uni2asc_basic_test(struct kunit *test)
{
	struct test_mock_sb *mock_sb;
	struct test_mock_string_env *mock_env;
	int len, result;

	mock_env = setup_mock_str_env(HFSPLUS_MAX_STRLEN + 1);
	KUNIT_ASSERT_NOT_NULL(test, mock_env);

	mock_sb = setup_mock_sb();
	KUNIT_ASSERT_NOT_NULL(test, mock_sb);

	mock_sb->nls.uni2char = test_uni2char;

	/* Test simple ASCII string conversion */
	create_unistr(&mock_env->str1, "hello");
	len = mock_env->buf_size;
	result = hfsplus_uni2asc_str(&mock_sb->sb, &mock_env->str1,
				     mock_env->buf, &len);

	KUNIT_EXPECT_EQ(test, 0, result);
	KUNIT_EXPECT_EQ(test, 5, len);
	KUNIT_EXPECT_STREQ(test, "hello", mock_env->buf);

	/* Test empty string */
	create_unistr(&mock_env->str1, "");
	len = mock_env->buf_size;
	result = hfsplus_uni2asc_str(&mock_sb->sb, &mock_env->str1,
				     mock_env->buf, &len);

	KUNIT_EXPECT_EQ(test, 0, result);
	KUNIT_EXPECT_EQ(test, 0, len);

	/* Test single character */
	create_unistr(&mock_env->str1, "A");
	len = mock_env->buf_size;
	result = hfsplus_uni2asc_str(&mock_sb->sb, &mock_env->str1,
				     mock_env->buf, &len);

	KUNIT_EXPECT_EQ(test, 0, result);
	KUNIT_EXPECT_EQ(test, 1, len);
	KUNIT_EXPECT_EQ(test, 'A', mock_env->buf[0]);

	free_mock_str_env(mock_env);
	free_mock_sb(mock_sb);
}

/* Test special character handling */
static void hfsplus_uni2asc_special_chars_test(struct kunit *test)
{
	struct test_mock_sb *mock_sb;
	struct test_mock_string_env *mock_env;
	int len, result;

	mock_env = setup_mock_str_env(HFSPLUS_MAX_STRLEN + 1);
	KUNIT_ASSERT_NOT_NULL(test, mock_env);

	mock_sb = setup_mock_sb();
	KUNIT_ASSERT_NOT_NULL(test, mock_sb);

	mock_sb->nls.uni2char = test_uni2char;

	/* Test null character conversion (should become 0x2400) */
	mock_env->str1.length = cpu_to_be16(1);
	mock_env->str1.unicode[0] = cpu_to_be16(0x0000);
	len = mock_env->buf_size;
	result = hfsplus_uni2asc_str(&mock_sb->sb, &mock_env->str1,
				     mock_env->buf, &len);

	KUNIT_EXPECT_EQ(test, 0, result);
	KUNIT_EXPECT_EQ(test, 1, len);
	/* Our test implementation returns '?' for non-ASCII */
	KUNIT_EXPECT_EQ(test, '?', mock_env->buf[0]);

	/* Test forward slash conversion (should become colon) */
	mock_env->str1.length = cpu_to_be16(1);
	mock_env->str1.unicode[0] = cpu_to_be16('/');
	len = mock_env->buf_size;
	result = hfsplus_uni2asc_str(&mock_sb->sb, &mock_env->str1,
				     mock_env->buf, &len);

	KUNIT_EXPECT_EQ(test, 0, result);
	KUNIT_EXPECT_EQ(test, 1, len);
	KUNIT_EXPECT_EQ(test, ':', mock_env->buf[0]);

	/* Test string with mixed special characters */
	mock_env->str1.length = cpu_to_be16(3);
	mock_env->str1.unicode[0] = cpu_to_be16('a');
	mock_env->str1.unicode[1] = cpu_to_be16('/');
	mock_env->str1.unicode[2] = cpu_to_be16('b');
	len = mock_env->buf_size;
	result = hfsplus_uni2asc_str(&mock_sb->sb, &mock_env->str1,
				     mock_env->buf, &len);

	KUNIT_EXPECT_EQ(test, 0, result);
	KUNIT_EXPECT_EQ(test, 3, len);
	KUNIT_EXPECT_EQ(test, 'a', mock_env->buf[0]);
	KUNIT_EXPECT_EQ(test, ':', mock_env->buf[1]);
	KUNIT_EXPECT_EQ(test, 'b', mock_env->buf[2]);

	free_mock_str_env(mock_env);
	free_mock_sb(mock_sb);
}

/* Test buffer length handling */
static void hfsplus_uni2asc_buffer_test(struct kunit *test)
{
	struct test_mock_sb *mock_sb;
	struct test_mock_string_env *mock_env;
	int len, result;

	mock_env = setup_mock_str_env(10);
	KUNIT_ASSERT_NOT_NULL(test, mock_env);

	mock_sb = setup_mock_sb();
	KUNIT_ASSERT_NOT_NULL(test, mock_sb);

	mock_sb->nls.uni2char = test_uni2char;

	/* Test insufficient buffer space */
	create_unistr(&mock_env->str1, "toolongstring");
	len = 5; /* Buffer too small */
	result = hfsplus_uni2asc_str(&mock_sb->sb, &mock_env->str1,
				     mock_env->buf, &len);

	KUNIT_EXPECT_EQ(test, -ENAMETOOLONG, result);
	KUNIT_EXPECT_EQ(test, 5, len); /* Should be set to consumed length */

	/* Test exact buffer size */
	create_unistr(&mock_env->str1, "exact");
	len = 5;
	result = hfsplus_uni2asc_str(&mock_sb->sb, &mock_env->str1,
				     mock_env->buf, &len);

	KUNIT_EXPECT_EQ(test, 0, result);
	KUNIT_EXPECT_EQ(test, 5, len);

	/* Test zero length buffer */
	create_unistr(&mock_env->str1, "test");
	len = 0;
	result = hfsplus_uni2asc_str(&mock_sb->sb, &mock_env->str1,
				     mock_env->buf, &len);

	KUNIT_EXPECT_EQ(test, -ENAMETOOLONG, result);
	KUNIT_EXPECT_EQ(test, 0, len);

	free_mock_str_env(mock_env);
	free_mock_sb(mock_sb);
}

/* Test corrupted unicode string handling */
static void hfsplus_uni2asc_corrupted_test(struct kunit *test)
{
	struct test_mock_sb *mock_sb;
	struct test_mock_string_env *mock_env;
	int len, result;

	mock_env = setup_mock_str_env(HFSPLUS_MAX_STRLEN + 1);
	KUNIT_ASSERT_NOT_NULL(test, mock_env);

	mock_sb = setup_mock_sb();
	KUNIT_ASSERT_NOT_NULL(test, mock_sb);

	mock_sb->nls.uni2char = test_uni2char;

	/* Test corrupted length (too large) */
	create_unistr(&mock_env->str1, "test");
	corrupt_unistr(&mock_env->str1); /* Sets length to U16_MAX */
	len = mock_env->buf_size;

	result = hfsplus_uni2asc_str(&mock_sb->sb, &mock_env->str1,
				     mock_env->buf, &len);

	/* Should still work but with corrected length */
	KUNIT_EXPECT_EQ(test, 0, result);
	/*
	 * Length should be corrected to HFSPLUS_MAX_STRLEN
	 * and processed accordingly
	 */
	KUNIT_EXPECT_GT(test, len, 0);

	free_mock_str_env(mock_env);
	free_mock_sb(mock_sb);
}

/* Test edge cases and boundary conditions */
static void hfsplus_uni2asc_edge_cases_test(struct kunit *test)
{
	struct test_mock_sb *mock_sb;
	struct test_mock_string_env *mock_env;
	int len, result;
	int i;

	mock_env = setup_mock_str_env(HFSPLUS_MAX_STRLEN * 2);
	KUNIT_ASSERT_NOT_NULL(test, mock_env);

	mock_sb = setup_mock_sb();
	KUNIT_ASSERT_NOT_NULL(test, mock_sb);

	mock_sb->nls.uni2char = test_uni2char;

	/* Test maximum length string */
	mock_env->str1.length = cpu_to_be16(HFSPLUS_MAX_STRLEN);
	for (i = 0; i < HFSPLUS_MAX_STRLEN; i++)
		mock_env->str1.unicode[i] = cpu_to_be16('a');

	len = mock_env->buf_size;
	result = hfsplus_uni2asc_str(&mock_sb->sb, &mock_env->str1,
				     mock_env->buf, &len);

	KUNIT_EXPECT_EQ(test, 0, result);
	KUNIT_EXPECT_EQ(test, HFSPLUS_MAX_STRLEN, len);

	/* Verify all characters are 'a' */
	for (i = 0; i < HFSPLUS_MAX_STRLEN; i++)
		KUNIT_EXPECT_EQ(test, 'a', mock_env->buf[i]);

	/* Test string with high Unicode values (non-ASCII) */
	mock_env->str1.length = cpu_to_be16(3);
	mock_env->str1.unicode[0] = cpu_to_be16(0x00E9); /* é */
	mock_env->str1.unicode[1] = cpu_to_be16(0x00F1); /* ñ */
	mock_env->str1.unicode[2] = cpu_to_be16(0x00FC); /* ü */
	len = mock_env->buf_size;
	result = hfsplus_uni2asc_str(&mock_sb->sb, &mock_env->str1,
				     mock_env->buf, &len);

	KUNIT_EXPECT_EQ(test, 0, result);
	KUNIT_EXPECT_EQ(test, 3, len);
	/* Our test implementation converts non-ASCII to '?' */
	KUNIT_EXPECT_EQ(test, '?', mock_env->buf[0]);
	KUNIT_EXPECT_EQ(test, '?', mock_env->buf[1]);
	KUNIT_EXPECT_EQ(test, '?', mock_env->buf[2]);

	free_mock_str_env(mock_env);
	free_mock_sb(mock_sb);
}

/* Simple char2uni implementation for testing */
static int test_char2uni(const unsigned char *rawstring,
			 int boundlen, wchar_t *uni)
{
	if (boundlen <= 0)
		return -EINVAL;

	*uni = (wchar_t)*rawstring;
	return 1;
}

/* Helper function to check unicode string contents */
static void check_unistr_content(struct kunit *test,
				 struct hfsplus_unistr *ustr,
				 const char *expected_ascii)
{
	int expected_len = strlen(expected_ascii);
	int actual_len = be16_to_cpu(ustr->length);
	int i;

	KUNIT_EXPECT_EQ(test, expected_len, actual_len);

	for (i = 0; i < expected_len && i < actual_len; i++) {
		u16 expected_char = (u16)expected_ascii[i];
		u16 actual_char = be16_to_cpu(ustr->unicode[i]);

		KUNIT_EXPECT_EQ(test, expected_char, actual_char);
	}
}

/* Test hfsplus_asc2uni basic functionality */
static void hfsplus_asc2uni_basic_test(struct kunit *test)
{
	struct test_mock_sb *mock_sb;
	struct test_mock_string_env *mock_env;
	int result;

	mock_env = setup_mock_str_env(HFSPLUS_MAX_STRLEN + 1);
	KUNIT_ASSERT_NOT_NULL(test, mock_env);

	mock_sb = setup_mock_sb();
	KUNIT_ASSERT_NOT_NULL(test, mock_sb);

	mock_sb->nls.char2uni = test_char2uni;

	/* Test simple ASCII string conversion */
	result = hfsplus_asc2uni(&mock_sb->sb, &mock_env->str1,
				 HFSPLUS_MAX_STRLEN, "hello", 5,
				 HFS_REGULAR_NAME);

	KUNIT_EXPECT_EQ(test, 0, result);
	check_unistr_content(test, &mock_env->str1, "hello");

	/* Test empty string */
	result = hfsplus_asc2uni(&mock_sb->sb, &mock_env->str1,
				 HFSPLUS_MAX_STRLEN, "", 0,
				 HFS_REGULAR_NAME);

	KUNIT_EXPECT_EQ(test, 0, result);
	KUNIT_EXPECT_EQ(test, 0, be16_to_cpu(mock_env->str1.length));

	/* Test single character */
	result = hfsplus_asc2uni(&mock_sb->sb, &mock_env->str1,
				 HFSPLUS_MAX_STRLEN, "A", 1,
				 HFS_REGULAR_NAME);

	KUNIT_EXPECT_EQ(test, 0, result);
	check_unistr_content(test, &mock_env->str1, "A");

	/* Test null-terminated string with explicit length */
	result = hfsplus_asc2uni(&mock_sb->sb, &mock_env->str1,
				 HFSPLUS_MAX_STRLEN, "test\0extra", 4,
				 HFS_REGULAR_NAME);

	KUNIT_EXPECT_EQ(test, 0, result);
	check_unistr_content(test, &mock_env->str1, "test");

	free_mock_str_env(mock_env);
	free_mock_sb(mock_sb);
}

/* Test special character handling in asc2uni */
static void hfsplus_asc2uni_special_chars_test(struct kunit *test)
{
	struct test_mock_sb *mock_sb;
	struct test_mock_string_env *mock_env;
	int result;

	mock_env = setup_mock_str_env(HFSPLUS_MAX_STRLEN + 1);
	KUNIT_ASSERT_NOT_NULL(test, mock_env);

	mock_sb = setup_mock_sb();
	KUNIT_ASSERT_NOT_NULL(test, mock_sb);

	mock_sb->nls.char2uni = test_char2uni;

	/* Test colon conversion (should become forward slash) */
	result = hfsplus_asc2uni(&mock_sb->sb, &mock_env->str1,
				 HFSPLUS_MAX_STRLEN, ":", 1,
				 HFS_REGULAR_NAME);

	KUNIT_EXPECT_EQ(test, 0, result);
	KUNIT_EXPECT_EQ(test, 1, be16_to_cpu(mock_env->str1.length));
	KUNIT_EXPECT_EQ(test, '/', be16_to_cpu(mock_env->str1.unicode[0]));

	/* Test string with mixed special characters */
	result = hfsplus_asc2uni(&mock_sb->sb, &mock_env->str1,
				 HFSPLUS_MAX_STRLEN, "a:b", 3,
				 HFS_REGULAR_NAME);

	KUNIT_EXPECT_EQ(test, 0, result);
	KUNIT_EXPECT_EQ(test, 3, be16_to_cpu(mock_env->str1.length));
	KUNIT_EXPECT_EQ(test, 'a', be16_to_cpu(mock_env->str1.unicode[0]));
	KUNIT_EXPECT_EQ(test, '/', be16_to_cpu(mock_env->str1.unicode[1]));
	KUNIT_EXPECT_EQ(test, 'b', be16_to_cpu(mock_env->str1.unicode[2]));

	/* Test multiple special characters */
	result = hfsplus_asc2uni(&mock_sb->sb, &mock_env->str1,
				 HFSPLUS_MAX_STRLEN, ":::", 3,
				 HFS_REGULAR_NAME);

	KUNIT_EXPECT_EQ(test, 0, result);
	KUNIT_EXPECT_EQ(test, 3, be16_to_cpu(mock_env->str1.length));
	KUNIT_EXPECT_EQ(test, '/', be16_to_cpu(mock_env->str1.unicode[0]));
	KUNIT_EXPECT_EQ(test, '/', be16_to_cpu(mock_env->str1.unicode[1]));
	KUNIT_EXPECT_EQ(test, '/', be16_to_cpu(mock_env->str1.unicode[2]));

	free_mock_str_env(mock_env);
	free_mock_sb(mock_sb);
}

/* Test buffer length limits */
static void hfsplus_asc2uni_buffer_limits_test(struct kunit *test)
{
	struct test_mock_sb *mock_sb;
	struct test_mock_string_env *mock_env;
	int result;

	mock_env = setup_mock_str_env(HFSPLUS_MAX_STRLEN + 10);
	KUNIT_ASSERT_NOT_NULL(test, mock_env);

	mock_sb = setup_mock_sb();
	KUNIT_ASSERT_NOT_NULL(test, mock_sb);

	mock_sb->nls.char2uni = test_char2uni;

	/* Test exact maximum length */
	memset(mock_env->buf, 'a', HFSPLUS_MAX_STRLEN);
	result = hfsplus_asc2uni(&mock_sb->sb,
				 &mock_env->str1, HFSPLUS_MAX_STRLEN,
				 mock_env->buf, HFSPLUS_MAX_STRLEN,
				 HFS_REGULAR_NAME);

	KUNIT_EXPECT_EQ(test, 0, result);
	KUNIT_EXPECT_EQ(test, HFSPLUS_MAX_STRLEN,
			be16_to_cpu(mock_env->str1.length));

	/* Test exceeding maximum length */
	memset(mock_env->buf, 'a', HFSPLUS_MAX_STRLEN + 5);
	result = hfsplus_asc2uni(&mock_sb->sb,
				 &mock_env->str1, HFSPLUS_MAX_STRLEN,
				 mock_env->buf, HFSPLUS_MAX_STRLEN + 5,
				 HFS_REGULAR_NAME);

	KUNIT_EXPECT_EQ(test, -ENAMETOOLONG, result);
	KUNIT_EXPECT_EQ(test, HFSPLUS_MAX_STRLEN,
			be16_to_cpu(mock_env->str1.length));

	/* Test with smaller max_unistr_len */
	result = hfsplus_asc2uni(&mock_sb->sb,
				 &mock_env->str1, 5, "toolongstring", 13,
				 HFS_REGULAR_NAME);

	KUNIT_EXPECT_EQ(test, -ENAMETOOLONG, result);
	KUNIT_EXPECT_EQ(test, 5, be16_to_cpu(mock_env->str1.length));

	/* Test zero max length */
	result = hfsplus_asc2uni(&mock_sb->sb, &mock_env->str1, 0, "test", 4,
				 HFS_REGULAR_NAME);

	KUNIT_EXPECT_EQ(test, -ENAMETOOLONG, result);
	KUNIT_EXPECT_EQ(test, 0, be16_to_cpu(mock_env->str1.length));

	free_mock_str_env(mock_env);
	free_mock_sb(mock_sb);
}

/* Test error handling and edge cases */
static void hfsplus_asc2uni_edge_cases_test(struct kunit *test)
{
	struct test_mock_sb *mock_sb;
	struct hfsplus_unistr ustr;
	char test_str[] = {'a', '\0', 'b'};
	int result;

	mock_sb = setup_mock_sb();
	KUNIT_ASSERT_NOT_NULL(test, mock_sb);

	mock_sb->nls.char2uni = test_char2uni;

	/* Test zero length input */
	result = hfsplus_asc2uni(&mock_sb->sb,
				 &ustr, HFSPLUS_MAX_STRLEN, "test", 0,
				 HFS_REGULAR_NAME);

	KUNIT_EXPECT_EQ(test, 0, result);
	KUNIT_EXPECT_EQ(test, 0, be16_to_cpu(ustr.length));

	/* Test input with length mismatch */
	result = hfsplus_asc2uni(&mock_sb->sb,
				 &ustr, HFSPLUS_MAX_STRLEN, "hello", 3,
				 HFS_REGULAR_NAME);

	KUNIT_EXPECT_EQ(test, 0, result);
	check_unistr_content(test, &ustr, "hel");

	/* Test with various printable ASCII characters */
	result = hfsplus_asc2uni(&mock_sb->sb,
				 &ustr, HFSPLUS_MAX_STRLEN, "ABC123!@#", 9,
				 HFS_REGULAR_NAME);

	KUNIT_EXPECT_EQ(test, 0, result);
	check_unistr_content(test, &ustr, "ABC123!@#");

	/* Test null character in the middle */
	result = hfsplus_asc2uni(&mock_sb->sb,
				 &ustr, HFSPLUS_MAX_STRLEN, test_str, 3,
				 HFS_REGULAR_NAME);

	KUNIT_EXPECT_EQ(test, 0, result);
	KUNIT_EXPECT_EQ(test, 3, be16_to_cpu(ustr.length));
	KUNIT_EXPECT_EQ(test, 'a', be16_to_cpu(ustr.unicode[0]));
	KUNIT_EXPECT_EQ(test, 0, be16_to_cpu(ustr.unicode[1]));
	KUNIT_EXPECT_EQ(test, 'b', be16_to_cpu(ustr.unicode[2]));

	free_mock_sb(mock_sb);
}

/* Test decomposition flag behavior */
static void hfsplus_asc2uni_decompose_test(struct kunit *test)
{
	struct test_mock_sb *mock_sb;
	struct test_mock_string_env *mock_env;
	int result;

	mock_env = setup_mock_str_env(HFSPLUS_MAX_STRLEN + 1);
	KUNIT_ASSERT_NOT_NULL(test, mock_env);

	mock_sb = setup_mock_sb();
	KUNIT_ASSERT_NOT_NULL(test, mock_sb);

	mock_sb->nls.char2uni = test_char2uni;

	/* Test with decomposition disabled (default) */
	clear_bit(HFSPLUS_SB_NODECOMPOSE, &mock_sb->sb_info.flags);
	result = hfsplus_asc2uni(&mock_sb->sb, &mock_env->str1,
				 HFSPLUS_MAX_STRLEN, "test", 4,
				 HFS_REGULAR_NAME);

	KUNIT_EXPECT_EQ(test, 0, result);
	check_unistr_content(test, &mock_env->str1, "test");

	/* Test with decomposition enabled */
	set_bit(HFSPLUS_SB_NODECOMPOSE, &mock_sb->sb_info.flags);
	result = hfsplus_asc2uni(&mock_sb->sb, &mock_env->str2,
				 HFSPLUS_MAX_STRLEN, "test", 4,
				 HFS_REGULAR_NAME);

	KUNIT_EXPECT_EQ(test, 0, result);
	check_unistr_content(test, &mock_env->str2, "test");

	/* For simple ASCII, both should produce the same result */
	KUNIT_EXPECT_EQ(test,
			be16_to_cpu(mock_env->str1.length),
			be16_to_cpu(mock_env->str2.length));

	free_mock_str_env(mock_env);
	free_mock_sb(mock_sb);
}

/*
 * Real UTF-8 <-> wchar_t conversion, mirroring what the "utf8" NLS table
 * actually does (see char2uni()/uni2char() in fs/nls/nls_utf8.c). The
 * test_char2uni()/test_uni2char() stand-ins above only handle one raw
 * byte per character, which cannot represent the combining marks (e.g.
 * U+0301, U+0323) the canonical-reordering tests below need.
 */
static int test_char2uni_utf8(const unsigned char *rawstring, int boundlen,
			      wchar_t *uni)
{
	unicode_t u;
	int n = utf8_to_utf32(rawstring, boundlen, &u);

	if (n < 0 || u > MAX_WCHAR_T) {
		*uni = 0x3f; /* ? */
		return -EINVAL;
	}
	*uni = (wchar_t)u;
	return n;
}

static int test_uni2char_utf8(wchar_t uni, unsigned char *out, int boundlen)
{
	int n = utf32_to_utf8(uni, out, boundlen);

	if (n < 0) {
		*out = '?';
		return -EINVAL;
	}
	return n;
}

/*
 * Test that hfsplus_asc2uni() brings combining marks contributed by
 * different source characters into Unicode canonical (combining-class)
 * order, instead of just storing them in whatever order they were typed.
 *
 * U+0301 (COMBINING ACUTE ACCENT) has combining class 230; U+0323
 * (COMBINING DOT BELOW) has combining class 220. Since 220 < 230,
 * canonical order requires the dot-below before the acute. A name
 * created with them typed in the "wrong" order must still end up stored
 * in canonical order - this is exactly what macOS's own fsck_hfs
 * (FixDecomps() in CatalogCheck.c) requires and flags as "Illegal name"
 * when it isn't true.
 */
static void hfsplus_asc2uni_combining_reorder_test(struct kunit *test)
{
	struct test_mock_sb *mock_sb;
	struct hfsplus_unistr ustr;
	/* "e" + COMBINING ACUTE ACCENT (U+0301) + COMBINING DOT BELOW (U+0323) */
	static const char wrong_order[] = "e\xcc\x81\xcc\xa3";
	/* "e" + COMBINING DOT BELOW (U+0323) + COMBINING ACUTE ACCENT (U+0301) */
	static const char canonical_order[] = "e\xcc\xa3\xcc\x81";
	/* Two class-230 marks: GRAVE (U+0300) then ACUTE (U+0301) */
	static const char same_class[] = "e\xcc\x80\xcc\x81";
	int result;

	mock_sb = setup_mock_sb();
	KUNIT_ASSERT_NOT_NULL(test, mock_sb);

	mock_sb->nls.char2uni = test_char2uni_utf8;

	/* Typed out of canonical order: must be reordered on storage. */
	result = hfsplus_asc2uni(&mock_sb->sb, &ustr, HFSPLUS_MAX_STRLEN,
				 wrong_order, strlen(wrong_order),
				 HFS_REGULAR_NAME);

	KUNIT_EXPECT_EQ(test, 0, result);
	KUNIT_EXPECT_EQ(test, 3, be16_to_cpu(ustr.length));
	KUNIT_EXPECT_EQ(test, 'e', be16_to_cpu(ustr.unicode[0]));
	KUNIT_EXPECT_EQ(test, 0x0323, be16_to_cpu(ustr.unicode[1]));
	KUNIT_EXPECT_EQ(test, 0x0301, be16_to_cpu(ustr.unicode[2]));

	/* Already in canonical order: must come out unchanged. */
	result = hfsplus_asc2uni(&mock_sb->sb, &ustr, HFSPLUS_MAX_STRLEN,
				 canonical_order, strlen(canonical_order),
				 HFS_REGULAR_NAME);

	KUNIT_EXPECT_EQ(test, 0, result);
	KUNIT_EXPECT_EQ(test, 3, be16_to_cpu(ustr.length));
	KUNIT_EXPECT_EQ(test, 'e', be16_to_cpu(ustr.unicode[0]));
	KUNIT_EXPECT_EQ(test, 0x0323, be16_to_cpu(ustr.unicode[1]));
	KUNIT_EXPECT_EQ(test, 0x0301, be16_to_cpu(ustr.unicode[2]));

	/* Two marks of equal combining class must keep their relative
	 * (input) order - the sort must be stable, not just "sorted".
	 */
	result = hfsplus_asc2uni(&mock_sb->sb, &ustr, HFSPLUS_MAX_STRLEN,
				 same_class, strlen(same_class),
				 HFS_REGULAR_NAME);

	KUNIT_EXPECT_EQ(test, 0, result);
	KUNIT_EXPECT_EQ(test, 3, be16_to_cpu(ustr.length));
	KUNIT_EXPECT_EQ(test, 'e', be16_to_cpu(ustr.unicode[0]));
	KUNIT_EXPECT_EQ(test, 0x0300, be16_to_cpu(ustr.unicode[1]));
	KUNIT_EXPECT_EQ(test, 0x0301, be16_to_cpu(ustr.unicode[2]));

	free_mock_sb(mock_sb);
}

/*
 * Test that a name written with combining marks in non-canonical order
 * still reads back as valid, correct UTF-8 once reordered - i.e. that
 * hfsplus_asc2uni() and hfsplus_uni2asc_str() stay consistent with each
 * other across the reordering.
 *
 * NODECOMPOSE is set so hfsplus_uni2asc_str() doesn't also recompose
 * "e" + COMBINING DOT BELOW back into the precomposed U+1EB9 ("e"): that
 * composition behavior is real (and already covered elsewhere), but it
 * would obscure what this test is actually checking.
 */
static void hfsplus_unicode_combining_reorder_roundtrip_test(struct kunit *test)
{
	struct test_mock_sb *mock_sb;
	struct hfsplus_unistr ustr;
	static const char wrong_order[] = "e\xcc\x81\xcc\xa3";
	static const char expected[] = "e\xcc\xa3\xcc\x81"; /* canonical order */
	char buf[32];
	int len = sizeof(buf);
	int result;

	mock_sb = setup_mock_sb();
	KUNIT_ASSERT_NOT_NULL(test, mock_sb);

	set_bit(HFSPLUS_SB_NODECOMPOSE, &mock_sb->sb_info.flags);
	mock_sb->nls.char2uni = test_char2uni_utf8;
	mock_sb->nls.uni2char = test_uni2char_utf8;

	result = hfsplus_asc2uni(&mock_sb->sb, &ustr, HFSPLUS_MAX_STRLEN,
				 wrong_order, strlen(wrong_order),
				 HFS_REGULAR_NAME);
	KUNIT_EXPECT_EQ(test, 0, result);

	result = hfsplus_uni2asc_str(&mock_sb->sb, &ustr, buf, &len);

	KUNIT_EXPECT_EQ(test, 0, result);
	KUNIT_EXPECT_EQ(test, (int)strlen(expected), len);
	KUNIT_EXPECT_MEMEQ(test, expected, buf, len);

	free_mock_sb(mock_sb);
}

/*
 * Test that hfsplus_asc2uni() decomposes a character using the corrected
 * (post-2002/"Jaguar") canonical decomposition even when Apple Technote
 * #1150's own decomposition table doesn't have an entry for it.
 *
 * U+01F8 (LATIN CAPITAL LETTER N WITH GRAVE) is exactly one of the
 * characters macOS's own fsck_hfs (FixDecomps() in CatalogCheck.c) has
 * flagged "Illegal name" for since 2002 when found stored undecomposed.
 */
static void hfsplus_asc2uni_legacy_decomp_test(struct kunit *test)
{
	struct test_mock_sb *mock_sb;
	struct hfsplus_unistr ustr;
	static const char input[] = "\xc7\xb8"; /* U+01F8 */
	int result;

	mock_sb = setup_mock_sb();
	KUNIT_ASSERT_NOT_NULL(test, mock_sb);

	mock_sb->nls.char2uni = test_char2uni_utf8;

	result = hfsplus_asc2uni(&mock_sb->sb, &ustr, HFSPLUS_MAX_STRLEN,
				 input, strlen(input), HFS_REGULAR_NAME);

	KUNIT_EXPECT_EQ(test, 0, result);
	KUNIT_EXPECT_EQ(test, 2, be16_to_cpu(ustr.length));
	KUNIT_EXPECT_EQ(test, 'N', be16_to_cpu(ustr.unicode[0]));
	KUNIT_EXPECT_EQ(test, 0x0300, be16_to_cpu(ustr.unicode[1]));

	free_mock_sb(mock_sb);
}

/*
 * Test that hfsplus_asc2uni() corrects two more of fsck_hfs's known
 * legacy decomposition sequences once combining marks from independently
 * typed characters end up adjacent:
 *
 *  - GREEK SMALL LETTER ALPHA (U+03B1) + COMBINING VERTICAL LINE ABOVE
 *    (U+030D) must become U+03B1 + COMBINING ACUTE ACCENT (U+0301).
 *  - BENGALI LETTER BA (U+09AC) + BENGALI SIGN NUKTA (U+09BC) must become
 *    the single character BENGALI LETTER RA WITH MIDDLE DIAGONAL (U+09B0).
 */
static void hfsplus_asc2uni_legacy_seq_fixup_test(struct kunit *test)
{
	struct test_mock_sb *mock_sb;
	struct hfsplus_unistr ustr;
	static const char greek_input[] = "\xce\xb1\xcc\x8d"; /* U+03B1 U+030D */
	static const char bengali_input[] = "\xe0\xa6\xac\xe0\xa6\xbc"; /* U+09AC U+09BC */
	int result;

	mock_sb = setup_mock_sb();
	KUNIT_ASSERT_NOT_NULL(test, mock_sb);

	mock_sb->nls.char2uni = test_char2uni_utf8;

	result = hfsplus_asc2uni(&mock_sb->sb, &ustr, HFSPLUS_MAX_STRLEN,
				 greek_input, strlen(greek_input),
				 HFS_REGULAR_NAME);

	KUNIT_EXPECT_EQ(test, 0, result);
	KUNIT_EXPECT_EQ(test, 2, be16_to_cpu(ustr.length));
	KUNIT_EXPECT_EQ(test, 0x03b1, be16_to_cpu(ustr.unicode[0]));
	KUNIT_EXPECT_EQ(test, 0x0301, be16_to_cpu(ustr.unicode[1]));

	result = hfsplus_asc2uni(&mock_sb->sb, &ustr, HFSPLUS_MAX_STRLEN,
				 bengali_input, strlen(bengali_input),
				 HFS_REGULAR_NAME);

	KUNIT_EXPECT_EQ(test, 0, result);
	KUNIT_EXPECT_EQ(test, 1, be16_to_cpu(ustr.length));
	KUNIT_EXPECT_EQ(test, 0x09b0, be16_to_cpu(ustr.unicode[0]));

	free_mock_sb(mock_sb);
}

/* Mock dentry for testing hfsplus_hash_dentry */
static struct dentry test_dentry;

static void setup_mock_dentry(struct super_block *sb)
{
	memset(&test_dentry, 0, sizeof(test_dentry));
	test_dentry.d_sb = sb;
}

/* Helper function to create qstr */
static void create_qstr(struct qstr *str, const char *name)
{
	str->name = name;
	str->len = strlen(name);
	str->hash = 0; /* Will be set by hash function */
}

/* Test hfsplus_hash_dentry basic functionality */
static void hfsplus_hash_dentry_basic_test(struct kunit *test)
{
	struct test_mock_sb *mock_sb;
	struct qstr str1, str2;
	int result;

	mock_sb = setup_mock_sb();
	KUNIT_ASSERT_NOT_NULL(test, mock_sb);

	setup_mock_dentry(&mock_sb->sb);
	mock_sb->nls.char2uni = test_char2uni;

	/* Test basic string hashing */
	create_qstr(&str1, "hello");
	result = hfsplus_hash_dentry(&test_dentry, &str1);

	KUNIT_EXPECT_EQ(test, 0, result);
	KUNIT_EXPECT_NE(test, 0, str1.hash);

	/* Test that identical strings produce identical hashes */
	create_qstr(&str2, "hello");
	result = hfsplus_hash_dentry(&test_dentry, &str2);

	KUNIT_EXPECT_EQ(test, 0, result);
	KUNIT_EXPECT_EQ(test, str1.hash, str2.hash);

	/* Test empty string */
	create_qstr(&str1, "");
	result = hfsplus_hash_dentry(&test_dentry, &str1);

	/* Empty string should still produce a hash */
	KUNIT_EXPECT_EQ(test, 0, result);

	/* Test single character */
	create_qstr(&str1, "A");
	result = hfsplus_hash_dentry(&test_dentry, &str1);

	KUNIT_EXPECT_EQ(test, 0, result);
	KUNIT_EXPECT_NE(test, 0, str1.hash);

	free_mock_sb(mock_sb);
}

/* Test case folding behavior in hash */
static void hfsplus_hash_dentry_casefold_test(struct kunit *test)
{
	struct test_mock_sb *mock_sb;
	struct qstr str1, str2;
	int result;

	mock_sb = setup_mock_sb();
	KUNIT_ASSERT_NOT_NULL(test, mock_sb);

	setup_mock_dentry(&mock_sb->sb);
	mock_sb->nls.char2uni = test_char2uni;

	/* Test with case folding disabled (default) */
	clear_bit(HFSPLUS_SB_CASEFOLD, &mock_sb->sb_info.flags);

	create_qstr(&str1, "Hello");
	result = hfsplus_hash_dentry(&test_dentry, &str1);
	KUNIT_EXPECT_EQ(test, 0, result);

	create_qstr(&str2, "hello");
	result = hfsplus_hash_dentry(&test_dentry, &str2);
	KUNIT_EXPECT_EQ(test, 0, result);

	/*
	 * Without case folding, different cases
	 * should produce different hashes
	 */
	KUNIT_EXPECT_NE(test, str1.hash, str2.hash);

	/* Test with case folding enabled */
	set_bit(HFSPLUS_SB_CASEFOLD, &mock_sb->sb_info.flags);

	create_qstr(&str1, "Hello");
	result = hfsplus_hash_dentry(&test_dentry, &str1);
	KUNIT_EXPECT_EQ(test, 0, result);

	create_qstr(&str2, "hello");
	result = hfsplus_hash_dentry(&test_dentry, &str2);
	KUNIT_EXPECT_EQ(test, 0, result);

	/* With case folding, different cases should produce same hash */
	KUNIT_EXPECT_EQ(test, str1.hash, str2.hash);

	/* Test mixed case */
	create_qstr(&str1, "HeLLo");
	result = hfsplus_hash_dentry(&test_dentry, &str1);
	KUNIT_EXPECT_EQ(test, 0, result);
	KUNIT_EXPECT_EQ(test, str1.hash, str2.hash);

	free_mock_sb(mock_sb);
}

/* Test special character handling in hash */
static void hfsplus_hash_dentry_special_chars_test(struct kunit *test)
{
	struct test_mock_sb *mock_sb;
	struct qstr str1, str2;
	int result;

	mock_sb = setup_mock_sb();
	KUNIT_ASSERT_NOT_NULL(test, mock_sb);

	setup_mock_dentry(&mock_sb->sb);
	mock_sb->nls.char2uni = test_char2uni;

	/* Test colon conversion (: becomes /) */
	create_qstr(&str1, "file:name");
	result = hfsplus_hash_dentry(&test_dentry, &str1);
	KUNIT_EXPECT_EQ(test, 0, result);

	create_qstr(&str2, "file/name");
	result = hfsplus_hash_dentry(&test_dentry, &str2);
	KUNIT_EXPECT_EQ(test, 0, result);

	/* After conversion, these should produce the same hash */
	KUNIT_EXPECT_EQ(test, str1.hash, str2.hash);

	/* Test multiple special characters */
	create_qstr(&str1, ":::");
	result = hfsplus_hash_dentry(&test_dentry, &str1);
	KUNIT_EXPECT_EQ(test, 0, result);

	create_qstr(&str2, "///");
	result = hfsplus_hash_dentry(&test_dentry, &str2);
	KUNIT_EXPECT_EQ(test, 0, result);

	KUNIT_EXPECT_EQ(test, str1.hash, str2.hash);

	free_mock_sb(mock_sb);
}

/* Test decomposition flag behavior in hash */
static void hfsplus_hash_dentry_decompose_test(struct kunit *test)
{
	struct test_mock_sb *mock_sb;
	struct qstr str1, str2;
	int result;

	mock_sb = setup_mock_sb();
	KUNIT_ASSERT_NOT_NULL(test, mock_sb);

	setup_mock_dentry(&mock_sb->sb);
	mock_sb->nls.char2uni = test_char2uni;

	/* Test with decomposition disabled (default) */
	clear_bit(HFSPLUS_SB_NODECOMPOSE, &mock_sb->sb_info.flags);

	create_qstr(&str1, "test");
	result = hfsplus_hash_dentry(&test_dentry, &str1);
	KUNIT_EXPECT_EQ(test, 0, result);

	/* Test with decomposition enabled */
	set_bit(HFSPLUS_SB_NODECOMPOSE, &mock_sb->sb_info.flags);

	create_qstr(&str2, "test");
	result = hfsplus_hash_dentry(&test_dentry, &str2);
	KUNIT_EXPECT_EQ(test, 0, result);

	/*
	 * For simple ASCII, decomposition shouldn't change
	 * the hash much but the function should still work correctly
	 */
	KUNIT_EXPECT_NE(test, 0, str2.hash);

	free_mock_sb(mock_sb);
}

/* Test hash consistency and distribution */
static void hfsplus_hash_dentry_consistency_test(struct kunit *test)
{
	struct test_mock_sb *mock_sb;
	struct qstr str1, str2, str3;
	unsigned long hash1;
	int result;

	mock_sb = setup_mock_sb();
	KUNIT_ASSERT_NOT_NULL(test, mock_sb);

	setup_mock_dentry(&mock_sb->sb);
	mock_sb->nls.char2uni = test_char2uni;

	/* Test that same string always produces same hash */
	create_qstr(&str1, "consistent");
	result = hfsplus_hash_dentry(&test_dentry, &str1);
	KUNIT_EXPECT_EQ(test, 0, result);
	hash1 = str1.hash;

	create_qstr(&str2, "consistent");
	result = hfsplus_hash_dentry(&test_dentry, &str2);
	KUNIT_EXPECT_EQ(test, 0, result);

	KUNIT_EXPECT_EQ(test, hash1, str2.hash);

	/* Test that different strings produce different hashes */
	create_qstr(&str3, "different");
	result = hfsplus_hash_dentry(&test_dentry, &str3);
	KUNIT_EXPECT_EQ(test, 0, result);

	KUNIT_EXPECT_NE(test, str1.hash, str3.hash);

	/* Test similar strings should have different hashes */
	create_qstr(&str1, "file1");
	result = hfsplus_hash_dentry(&test_dentry, &str1);
	KUNIT_EXPECT_EQ(test, 0, result);

	create_qstr(&str2, "file2");
	result = hfsplus_hash_dentry(&test_dentry, &str2);
	KUNIT_EXPECT_EQ(test, 0, result);

	KUNIT_EXPECT_NE(test, str1.hash, str2.hash);

	free_mock_sb(mock_sb);
}

/* Test edge cases and boundary conditions */
static void hfsplus_hash_dentry_edge_cases_test(struct kunit *test)
{
	struct test_mock_sb *mock_sb;
	struct test_mock_string_env *mock_env;
	struct qstr str;
	int result;

	mock_env = setup_mock_str_env(HFSPLUS_MAX_STRLEN + 1);
	KUNIT_ASSERT_NOT_NULL(test, mock_env);

	mock_sb = setup_mock_sb();
	KUNIT_ASSERT_NOT_NULL(test, mock_sb);

	setup_mock_dentry(&mock_sb->sb);
	mock_sb->nls.char2uni = test_char2uni;

	/* Test very long filename */
	memset(mock_env->buf, 'a', mock_env->buf_size - 1);
	mock_env->buf[mock_env->buf_size - 1] = '\0';

	create_qstr(&str, mock_env->buf);
	result = hfsplus_hash_dentry(&test_dentry, &str);

	KUNIT_EXPECT_EQ(test, 0, result);
	KUNIT_EXPECT_NE(test, 0, str.hash);

	/* Test filename with all printable ASCII characters */
	create_qstr(&str, "!@#$%^&*()_+-=[]{}|;':\",./<>?");
	result = hfsplus_hash_dentry(&test_dentry, &str);

	KUNIT_EXPECT_EQ(test, 0, result);
	KUNIT_EXPECT_NE(test, 0, str.hash);

	/* Test with embedded null (though not typical for filenames) */
	str.name = "file\0hidden";
	str.len = 11; /* Include the null and text after it */
	str.hash = 0;
	result = hfsplus_hash_dentry(&test_dentry, &str);

	KUNIT_EXPECT_EQ(test, 0, result);
	KUNIT_EXPECT_NE(test, 0, str.hash);

	free_mock_str_env(mock_env);
	free_mock_sb(mock_sb);
}

/*
 * Test that hfsplus_hash_dentry() hashes two names identically when they
 * differ only in the (non-canonical) typed order of the same combining
 * marks - both must canonicalize to the same stored form, so they must
 * hash the same or dcache lookups would spuriously miss.
 */
static void hfsplus_hash_dentry_combining_reorder_test(struct kunit *test)
{
	struct test_mock_sb *mock_sb;
	struct qstr str1, str2;
	int result;

	mock_sb = setup_mock_sb();
	KUNIT_ASSERT_NOT_NULL(test, mock_sb);

	setup_mock_dentry(&mock_sb->sb);
	mock_sb->nls.char2uni = test_char2uni_utf8;

	create_qstr(&str1, "e\xcc\x81\xcc\xa3"); /* acute, then dot-below */
	result = hfsplus_hash_dentry(&test_dentry, &str1);
	KUNIT_EXPECT_EQ(test, 0, result);

	create_qstr(&str2, "e\xcc\xa3\xcc\x81"); /* dot-below, then acute */
	result = hfsplus_hash_dentry(&test_dentry, &str2);
	KUNIT_EXPECT_EQ(test, 0, result);

	KUNIT_EXPECT_EQ(test, str1.hash, str2.hash);

	free_mock_sb(mock_sb);
}

/* Test hfsplus_compare_dentry basic functionality */
static void hfsplus_compare_dentry_basic_test(struct kunit *test)
{
	struct test_mock_sb *mock_sb;
	struct qstr name;
	int result;

	mock_sb = setup_mock_sb();
	KUNIT_ASSERT_NOT_NULL(test, mock_sb);

	setup_mock_dentry(&mock_sb->sb);
	mock_sb->nls.char2uni = test_char2uni;

	/* Test identical strings */
	create_qstr(&name, "hello");
	result = hfsplus_compare_dentry(&test_dentry, 5, "hello", &name);
	KUNIT_EXPECT_EQ(test, 0, result);

	/* Test different strings - lexicographic order */
	create_qstr(&name, "world");
	result = hfsplus_compare_dentry(&test_dentry, 5, "hello", &name);
	KUNIT_EXPECT_LT(test, result, 0); /* "hello" < "world" */

	result = hfsplus_compare_dentry(&test_dentry, 5, "world", &name);
	KUNIT_EXPECT_EQ(test, 0, result);

	create_qstr(&name, "hello");
	result = hfsplus_compare_dentry(&test_dentry, 5, "world", &name);
	KUNIT_EXPECT_GT(test, result, 0); /* "world" > "hello" */

	/* Test empty strings */
	create_qstr(&name, "");
	result = hfsplus_compare_dentry(&test_dentry, 0, "", &name);
	KUNIT_EXPECT_EQ(test, 0, result);

	/* Test one empty, one non-empty */
	create_qstr(&name, "test");
	result = hfsplus_compare_dentry(&test_dentry, 0, "", &name);
	KUNIT_EXPECT_LT(test, result, 0); /* "" < "test" */

	create_qstr(&name, "");
	result = hfsplus_compare_dentry(&test_dentry, 4, "test", &name);
	KUNIT_EXPECT_GT(test, result, 0); /* "test" > "" */

	free_mock_sb(mock_sb);
}

/* Test case folding behavior in comparison */
static void hfsplus_compare_dentry_casefold_test(struct kunit *test)
{
	struct test_mock_sb *mock_sb;
	struct qstr name;
	int result;

	mock_sb = setup_mock_sb();
	KUNIT_ASSERT_NOT_NULL(test, mock_sb);

	setup_mock_dentry(&mock_sb->sb);
	mock_sb->nls.char2uni = test_char2uni;

	/* Test with case folding disabled (default) */
	clear_bit(HFSPLUS_SB_CASEFOLD, &mock_sb->sb_info.flags);

	create_qstr(&name, "hello");
	result = hfsplus_compare_dentry(&test_dentry, 5, "Hello", &name);
	/* Case sensitive: "Hello" != "hello" */
	KUNIT_EXPECT_NE(test, 0, result);

	create_qstr(&name, "Hello");
	result = hfsplus_compare_dentry(&test_dentry, 5, "hello", &name);
	/* Case sensitive: "hello" != "Hello" */
	KUNIT_EXPECT_NE(test, 0, result);

	/* Test with case folding enabled */
	set_bit(HFSPLUS_SB_CASEFOLD, &mock_sb->sb_info.flags);

	create_qstr(&name, "hello");
	result = hfsplus_compare_dentry(&test_dentry, 5, "Hello", &name);
	/* Case insensitive: "Hello" == "hello" */
	KUNIT_EXPECT_EQ(test, 0, result);

	create_qstr(&name, "Hello");
	result = hfsplus_compare_dentry(&test_dentry, 5, "hello", &name);
	/* Case insensitive: "hello" == "Hello" */
	KUNIT_EXPECT_EQ(test, 0, result);

	/* Test mixed case */
	create_qstr(&name, "TeSt");
	result = hfsplus_compare_dentry(&test_dentry, 4, "test", &name);
	KUNIT_EXPECT_EQ(test, 0, result);

	create_qstr(&name, "test");
	result = hfsplus_compare_dentry(&test_dentry, 4, "TEST", &name);
	KUNIT_EXPECT_EQ(test, 0, result);

	free_mock_sb(mock_sb);
}

/* Test special character handling in comparison */
static void hfsplus_compare_dentry_special_chars_test(struct kunit *test)
{
	struct test_mock_sb *mock_sb;
	struct qstr name;
	int result;

	mock_sb = setup_mock_sb();
	KUNIT_ASSERT_NOT_NULL(test, mock_sb);

	setup_mock_dentry(&mock_sb->sb);
	mock_sb->nls.char2uni = test_char2uni;

	/* Test colon conversion (: becomes /) */
	create_qstr(&name, "file/name");
	result = hfsplus_compare_dentry(&test_dentry, 9, "file:name", &name);
	/* "file:name" == "file/name" after conversion */
	KUNIT_EXPECT_EQ(test, 0, result);

	create_qstr(&name, "file:name");
	result = hfsplus_compare_dentry(&test_dentry, 9, "file/name", &name);
	/* "file/name" == "file:name" after conversion */
	KUNIT_EXPECT_EQ(test, 0, result);

	/* Test multiple special characters */
	create_qstr(&name, "///");
	result = hfsplus_compare_dentry(&test_dentry, 3, ":::", &name);
	KUNIT_EXPECT_EQ(test, 0, result);

	/* Test mixed special and regular characters */
	create_qstr(&name, "a/b:c");
	result = hfsplus_compare_dentry(&test_dentry, 5, "a:b/c", &name);
	/* Both become "a/b/c" after conversion */
	KUNIT_EXPECT_EQ(test, 0, result);

	free_mock_sb(mock_sb);
}

/* Test length differences */
static void hfsplus_compare_dentry_length_test(struct kunit *test)
{
	struct test_mock_sb *mock_sb;
	struct qstr name;
	int result;

	mock_sb = setup_mock_sb();
	KUNIT_ASSERT_NOT_NULL(test, mock_sb);

	setup_mock_dentry(&mock_sb->sb);
	mock_sb->nls.char2uni = test_char2uni;

	/* Test different lengths with common prefix */
	create_qstr(&name, "testing");
	result = hfsplus_compare_dentry(&test_dentry, 4, "test", &name);
	KUNIT_EXPECT_LT(test, result, 0); /* "test" < "testing" */

	create_qstr(&name, "test");
	result = hfsplus_compare_dentry(&test_dentry, 7, "testing", &name);
	KUNIT_EXPECT_GT(test, result, 0); /* "testing" > "test" */

	/* Test exact length match */
	create_qstr(&name, "exact");
	result = hfsplus_compare_dentry(&test_dentry, 5, "exact", &name);
	KUNIT_EXPECT_EQ(test, 0, result);

	/* Test length parameter vs actual string content */
	create_qstr(&name, "hello");
	result = hfsplus_compare_dentry(&test_dentry, 3, "hel", &name);
	KUNIT_EXPECT_LT(test, result, 0); /* "hel" < "hello" */

	/* Test longer first string but shorter length parameter */
	create_qstr(&name, "hi");
	result = hfsplus_compare_dentry(&test_dentry, 2, "hello", &name);
	/* "he" < "hi" (only first 2 chars compared) */
	KUNIT_EXPECT_LT(test, result, 0);

	free_mock_sb(mock_sb);
}

/* Test decomposition flag behavior */
static void hfsplus_compare_dentry_decompose_test(struct kunit *test)
{
	struct test_mock_sb *mock_sb;
	struct qstr name;
	int result;

	mock_sb = setup_mock_sb();
	KUNIT_ASSERT_NOT_NULL(test, mock_sb);

	setup_mock_dentry(&mock_sb->sb);
	mock_sb->nls.char2uni = test_char2uni;

	/* Test with decomposition disabled (default) */
	clear_bit(HFSPLUS_SB_NODECOMPOSE, &mock_sb->sb_info.flags);

	create_qstr(&name, "test");
	result = hfsplus_compare_dentry(&test_dentry, 4, "test", &name);
	KUNIT_EXPECT_EQ(test, 0, result);

	/* Test with decomposition enabled */
	set_bit(HFSPLUS_SB_NODECOMPOSE, &mock_sb->sb_info.flags);

	create_qstr(&name, "test");
	result = hfsplus_compare_dentry(&test_dentry, 4, "test", &name);
	KUNIT_EXPECT_EQ(test, 0, result);

	/* For simple ASCII, decomposition shouldn't affect the result */
	create_qstr(&name, "different");
	result = hfsplus_compare_dentry(&test_dentry, 4, "test", &name);
	KUNIT_EXPECT_NE(test, 0, result);

	free_mock_sb(mock_sb);
}

/* Test edge cases and boundary conditions */
static void hfsplus_compare_dentry_edge_cases_test(struct kunit *test)
{
	struct test_mock_sb *mock_sb;
	struct qstr name;
	char *long_str;
	char *long_str2;
	u32 str_size = HFSPLUS_MAX_STRLEN + 1;
	struct qstr null_name = {
		.name = "a\0b",
		.len = 3,
		.hash = 0
	};
	int result;

	mock_sb = setup_mock_sb();
	KUNIT_ASSERT_NOT_NULL(test, mock_sb);

	setup_mock_dentry(&mock_sb->sb);
	mock_sb->nls.char2uni = test_char2uni;

	long_str = kzalloc(str_size, GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, long_str);

	long_str2 = kzalloc(str_size, GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, long_str2);

	/* Test very long strings */
	memset(long_str, 'a', str_size - 1);
	long_str[str_size - 1] = '\0';

	create_qstr(&name, long_str);
	result = hfsplus_compare_dentry(&test_dentry, str_size - 1,
					long_str, &name);
	KUNIT_EXPECT_EQ(test, 0, result);

	/* Test with difference at the end of long strings */
	memset(long_str2, 'a', str_size - 1);
	long_str2[str_size - 1] = '\0';
	long_str2[str_size - 2] = 'b';
	create_qstr(&name, long_str2);
	result = hfsplus_compare_dentry(&test_dentry, str_size - 1,
					long_str, &name);
	KUNIT_EXPECT_LT(test, result, 0); /* 'a' < 'b' */

	/* Test single character differences */
	create_qstr(&name, "b");
	result = hfsplus_compare_dentry(&test_dentry, 1, "a", &name);
	KUNIT_EXPECT_LT(test, result, 0); /* 'a' < 'b' */

	create_qstr(&name, "a");
	result = hfsplus_compare_dentry(&test_dentry, 1, "b", &name);
	KUNIT_EXPECT_GT(test, result, 0); /* 'b' > 'a' */

	/* Test with null characters in the middle */
	result = hfsplus_compare_dentry(&test_dentry, 3, "a\0b", &null_name);
	KUNIT_EXPECT_EQ(test, 0, result);

	/* Test all printable ASCII characters */
	create_qstr(&name, "!@#$%^&*()");
	result = hfsplus_compare_dentry(&test_dentry, 10, "!@#$%^&*()", &name);
	KUNIT_EXPECT_EQ(test, 0, result);

	kfree(long_str);
	kfree(long_str2);
	free_mock_sb(mock_sb);
}

/* Test combined flag behaviors */
static void hfsplus_compare_dentry_combined_flags_test(struct kunit *test)
{
	struct test_mock_sb *mock_sb;
	struct qstr name;
	int result;

	mock_sb = setup_mock_sb();
	KUNIT_ASSERT_NOT_NULL(test, mock_sb);

	setup_mock_dentry(&mock_sb->sb);
	mock_sb->nls.char2uni = test_char2uni;

	/* Test with both casefold and decompose enabled */
	set_bit(HFSPLUS_SB_CASEFOLD, &mock_sb->sb_info.flags);
	set_bit(HFSPLUS_SB_NODECOMPOSE, &mock_sb->sb_info.flags);

	create_qstr(&name, "hello");
	result = hfsplus_compare_dentry(&test_dentry, 5, "HELLO", &name);
	KUNIT_EXPECT_EQ(test, 0, result);

	/* Test special chars with case folding */
	create_qstr(&name, "File/Name");
	result = hfsplus_compare_dentry(&test_dentry, 9, "file:name", &name);
	KUNIT_EXPECT_EQ(test, 0, result);

	/* Test with both flags disabled */
	clear_bit(HFSPLUS_SB_CASEFOLD, &mock_sb->sb_info.flags);
	clear_bit(HFSPLUS_SB_NODECOMPOSE, &mock_sb->sb_info.flags);

	create_qstr(&name, "hello");
	result = hfsplus_compare_dentry(&test_dentry, 5, "HELLO", &name);
	KUNIT_EXPECT_NE(test, 0, result); /* Case sensitive */

	/* But special chars should still be converted */
	create_qstr(&name, "file/name");
	result = hfsplus_compare_dentry(&test_dentry, 9, "file:name", &name);
	KUNIT_EXPECT_EQ(test, 0, result);

	free_mock_sb(mock_sb);
}

/*
 * Test that hfsplus_compare_dentry() treats two names as equal when they
 * differ only in the (non-canonical) typed order of the same combining
 * marks, since both refer to the same canonically-ordered catalog entry.
 */
static void hfsplus_compare_dentry_combining_reorder_test(struct kunit *test)
{
	struct test_mock_sb *mock_sb;
	struct qstr name;
	int result;

	mock_sb = setup_mock_sb();
	KUNIT_ASSERT_NOT_NULL(test, mock_sb);

	setup_mock_dentry(&mock_sb->sb);
	mock_sb->nls.char2uni = test_char2uni_utf8;

	create_qstr(&name, "e\xcc\xa3\xcc\x81"); /* dot-below, then acute */
	result = hfsplus_compare_dentry(&test_dentry, 5, "e\xcc\x81\xcc\xa3",
					&name); /* acute, then dot-below */
	KUNIT_EXPECT_EQ(test, 0, result);

	free_mock_sb(mock_sb);
}

/*
 * Test that a precomposed character using one of fsck_hfs's known legacy
 * decompositions compares equal to the already-decomposed form of the
 * same character - i.e. that hfsplus_compare_dentry() applies the same
 * legacy-decomposition correction as hfsplus_asc2uni() does on storage,
 * so a lookup finds the entry regardless of which form was typed.
 */
static void hfsplus_compare_dentry_legacy_decomp_test(struct kunit *test)
{
	struct test_mock_sb *mock_sb;
	struct qstr name;
	int result;

	mock_sb = setup_mock_sb();
	KUNIT_ASSERT_NOT_NULL(test, mock_sb);

	setup_mock_dentry(&mock_sb->sb);
	mock_sb->nls.char2uni = test_char2uni_utf8;

	/* "N" + COMBINING GRAVE ACCENT (U+0300), already decomposed */
	create_qstr(&name, "N\xcc\x80");
	/* U+01F8, precomposed */
	result = hfsplus_compare_dentry(&test_dentry, 2, "\xc7\xb8", &name);
	KUNIT_EXPECT_EQ(test, 0, result);

	free_mock_sb(mock_sb);
}

static struct kunit_case hfsplus_unicode_test_cases[] = {
	KUNIT_CASE(hfsplus_strcasecmp_test),
	KUNIT_CASE(hfsplus_strcmp_test),
	KUNIT_CASE(hfsplus_unicode_edge_cases_test),
	KUNIT_CASE(hfsplus_unicode_boundary_test),
	KUNIT_CASE(hfsplus_uni2asc_basic_test),
	KUNIT_CASE(hfsplus_uni2asc_special_chars_test),
	KUNIT_CASE(hfsplus_uni2asc_buffer_test),
	KUNIT_CASE(hfsplus_uni2asc_corrupted_test),
	KUNIT_CASE(hfsplus_uni2asc_edge_cases_test),
	KUNIT_CASE(hfsplus_asc2uni_basic_test),
	KUNIT_CASE(hfsplus_asc2uni_special_chars_test),
	KUNIT_CASE(hfsplus_asc2uni_buffer_limits_test),
	KUNIT_CASE(hfsplus_asc2uni_edge_cases_test),
	KUNIT_CASE(hfsplus_asc2uni_decompose_test),
	KUNIT_CASE(hfsplus_asc2uni_combining_reorder_test),
	KUNIT_CASE(hfsplus_unicode_combining_reorder_roundtrip_test),
	KUNIT_CASE(hfsplus_asc2uni_legacy_decomp_test),
	KUNIT_CASE(hfsplus_asc2uni_legacy_seq_fixup_test),
	KUNIT_CASE(hfsplus_hash_dentry_basic_test),
	KUNIT_CASE(hfsplus_hash_dentry_casefold_test),
	KUNIT_CASE(hfsplus_hash_dentry_special_chars_test),
	KUNIT_CASE(hfsplus_hash_dentry_decompose_test),
	KUNIT_CASE(hfsplus_hash_dentry_consistency_test),
	KUNIT_CASE(hfsplus_hash_dentry_edge_cases_test),
	KUNIT_CASE(hfsplus_hash_dentry_combining_reorder_test),
	KUNIT_CASE(hfsplus_compare_dentry_basic_test),
	KUNIT_CASE(hfsplus_compare_dentry_casefold_test),
	KUNIT_CASE(hfsplus_compare_dentry_special_chars_test),
	KUNIT_CASE(hfsplus_compare_dentry_length_test),
	KUNIT_CASE(hfsplus_compare_dentry_decompose_test),
	KUNIT_CASE(hfsplus_compare_dentry_edge_cases_test),
	KUNIT_CASE(hfsplus_compare_dentry_combined_flags_test),
	KUNIT_CASE(hfsplus_compare_dentry_combining_reorder_test),
	KUNIT_CASE(hfsplus_compare_dentry_legacy_decomp_test),
	{}
};

static struct kunit_suite hfsplus_unicode_test_suite = {
	.name = "hfsplus_unicode",
	.test_cases = hfsplus_unicode_test_cases,
};

kunit_test_suite(hfsplus_unicode_test_suite);

MODULE_DESCRIPTION("KUnit tests for HFS+ Unicode string operations");
MODULE_LICENSE("GPL");
MODULE_IMPORT_NS("EXPORTED_FOR_KUNIT_TESTING");
