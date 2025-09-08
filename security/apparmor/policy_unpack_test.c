// SPDX-License-Identifier: GPL-2.0-only
/*
 * KUnit tests for AppArmor's policy unpack.
 */

#include <kunit/test.h>
#include <kunit/visibility.h>

#include "include/policy.h"
#include "include/policy_unpack.h"

#include <linux/unaligned.h>

#define TEST_STRING_NAME "TEST_STRING"
#define TEST_STRING_DATA "testing"
#define TEST_STRING_BUF_OFFSET \
	(3 + strlen(TEST_STRING_NAME) + 1)

#define TEST_U32_NAME "U32_TEST"
#define TEST_U32_DATA ((u32)0x01020304)
#define TEST_NAMED_U32_BUF_OFFSET \
	(TEST_STRING_BUF_OFFSET + 3 + strlen(TEST_STRING_DATA) + 1)
#define TEST_U32_BUF_OFFSET \
	(TEST_NAMED_U32_BUF_OFFSET + 3 + strlen(TEST_U32_NAME) + 1)

#define TEST_U16_OFFSET (TEST_U32_BUF_OFFSET + 3)
#define TEST_U16_DATA ((u16)(TEST_U32_DATA >> 16))

#define TEST_U64_NAME "U64_TEST"
#define TEST_U64_DATA ((u64)0x0102030405060708)
#define TEST_NAMED_U64_BUF_OFFSET (TEST_U32_BUF_OFFSET + sizeof(u32) + 1)
#define TEST_U64_BUF_OFFSET \
	(TEST_NAMED_U64_BUF_OFFSET + 3 + strlen(TEST_U64_NAME) + 1)

#define TEST_BLOB_NAME "BLOB_TEST"
#define TEST_BLOB_DATA "\xde\xad\x00\xbe\xef"
#define TEST_BLOB_DATA_SIZE (ARRAY_SIZE(TEST_BLOB_DATA))
#define TEST_NAMED_BLOB_BUF_OFFSET (TEST_U64_BUF_OFFSET + sizeof(u64) + 1)
#define TEST_BLOB_BUF_OFFSET \
	(TEST_NAMED_BLOB_BUF_OFFSET + 3 + strlen(TEST_BLOB_NAME) + 1)

#define TEST_ARRAY_NAME "ARRAY_TEST"
#define TEST_ARRAY_SIZE 16
#define TEST_NAMED_ARRAY_BUF_OFFSET \
	(TEST_BLOB_BUF_OFFSET + 5 + TEST_BLOB_DATA_SIZE)
#define TEST_ARRAY_BUF_OFFSET \
	(TEST_NAMED_ARRAY_BUF_OFFSET + 3 + strlen(TEST_ARRAY_NAME) + 1)

MODULE_IMPORT_NS("EXPORTED_FOR_KUNIT_TESTING");

struct policy_unpack_fixture {
	struct aa_ext *e;
	size_t e_size;
};

static struct aa_ext *build_aa_ext_struct(struct policy_unpack_fixture *puf,
					  struct kunit *test, size_t buf_size)
{
	char *buf;
	struct aa_ext *e;

	buf = kunit_kzalloc(test, buf_size, GFP_USER);
	KUNIT_EXPECT_NOT_ERR_OR_NULL(test, buf);

	e = kunit_kmalloc(test, sizeof(*e), GFP_USER);
	KUNIT_EXPECT_NOT_ERR_OR_NULL(test, e);

	e->start = buf;
	e->end = e->start + buf_size;
	e->pos = e->start;

	*buf = AA_NAME;
	*(buf + 1) = strlen(TEST_STRING_NAME) + 1;
	strscpy(buf + 3, TEST_STRING_NAME, e->end - (void *)(buf + 3));

	buf = e->start + TEST_STRING_BUF_OFFSET;
	*buf = AA_STRING;
	*(buf + 1) = strlen(TEST_STRING_DATA) + 1;
	strscpy(buf + 3, TEST_STRING_DATA, e->end - (void *)(buf + 3));
	buf = e->start + TEST_NAMED_U32_BUF_OFFSET;
	*buf = AA_NAME;
	*(buf + 1) = strlen(TEST_U32_NAME) + 1;
	strscpy(buf + 3, TEST_U32_NAME, e->end - (void *)(buf + 3));
	*(buf + 3 + strlen(TEST_U32_NAME) + 1) = AA_U32;
	put_unaligned_le32(TEST_U32_DATA, buf + 3 + strlen(TEST_U32_NAME) + 2);

	buf = e->start + TEST_NAMED_U64_BUF_OFFSET;
	*buf = AA_NAME;
	*(buf + 1) = strlen(TEST_U64_NAME) + 1;
	strscpy(buf + 3, TEST_U64_NAME, e->end - (void *)(buf + 3));
	*(buf + 3 + strlen(TEST_U64_NAME) + 1) = AA_U64;
	*((__le64 *)(buf + 3 + strlen(TEST_U64_NAME) + 2)) = cpu_to_le64(TEST_U64_DATA);

	buf = e->start + TEST_NAMED_BLOB_BUF_OFFSET;
	*buf = AA_NAME;
	*(buf + 1) = strlen(TEST_BLOB_NAME) + 1;
	strscpy(buf + 3, TEST_BLOB_NAME, e->end - (void *)(buf + 3));
	*(buf + 3 + strlen(TEST_BLOB_NAME) + 1) = AA_BLOB;
	*(buf + 3 + strlen(TEST_BLOB_NAME) + 2) = TEST_BLOB_DATA_SIZE;
	memcpy(buf + 3 + strlen(TEST_BLOB_NAME) + 6,
		TEST_BLOB_DATA, TEST_BLOB_DATA_SIZE);

	buf = e->start + TEST_NAMED_ARRAY_BUF_OFFSET;
	*buf = AA_NAME;
	*(buf + 1) = strlen(TEST_ARRAY_NAME) + 1;
	strscpy(buf + 3, TEST_ARRAY_NAME, e->end - (void *)(buf + 3));
	*(buf + 3 + strlen(TEST_ARRAY_NAME) + 1) = AA_ARRAY;
	put_unaligned_le16(TEST_ARRAY_SIZE, buf + 3 + strlen(TEST_ARRAY_NAME) + 2);

	return e;
}

static int policy_unpack_test_init(struct kunit *test)
{
	size_t e_size = TEST_ARRAY_BUF_OFFSET + sizeof(u16) + 1;
	struct policy_unpack_fixture *puf;

	puf = kunit_kmalloc(test, sizeof(*puf), GFP_USER);
	KUNIT_EXPECT_NOT_ERR_OR_NULL(test, puf);

	puf->e_size = e_size;
	puf->e = build_aa_ext_struct(puf, test, e_size);

	test->priv = puf;
	return 0;
}

static void policy_unpack_test_inbounds_when_inbounds(struct kunit *test)
{
	struct policy_unpack_fixture *puf = test->priv;

	KUNIT_EXPECT_TRUE(test, aa_inbounds(puf->e, 0));
	KUNIT_EXPECT_TRUE(test, aa_inbounds(puf->e, puf->e_size / 2));
	KUNIT_EXPECT_TRUE(test, aa_inbounds(puf->e, puf->e_size));
}

static void policy_unpack_test_inbounds_when_out_of_bounds(struct kunit *test)
{
	struct policy_unpack_fixture *puf = test->priv;

	KUNIT_EXPECT_FALSE(test, aa_inbounds(puf->e, puf->e_size + 1));
}

static void policy_unpack_test_unpack_array_with_null_name(struct kunit *test)
{
	struct policy_unpack_fixture *puf = test->priv;
	u16 array_size = 0;

	puf->e->pos += TEST_ARRAY_BUF_OFFSET;

	KUNIT_EXPECT_TRUE(test, aa_unpack_array(puf->e, NULL, &array_size));
	KUNIT_EXPECT_EQ(test, array_size, (u16)TEST_ARRAY_SIZE);
	KUNIT_EXPECT_PTR_EQ(test, puf->e->pos,
		puf->e->start + TEST_ARRAY_BUF_OFFSET + sizeof(u16) + 1);
}

static void policy_unpack_test_unpack_array_with_name(struct kunit *test)
{
	struct policy_unpack_fixture *puf = test->priv;
	const char name[] = TEST_ARRAY_NAME;
	u16 array_size = 0;

	puf->e->pos += TEST_NAMED_ARRAY_BUF_OFFSET;

	KUNIT_EXPECT_TRUE(test, aa_unpack_array(puf->e, name, &array_size));
	KUNIT_EXPECT_EQ(test, array_size, (u16)TEST_ARRAY_SIZE);
	KUNIT_EXPECT_PTR_EQ(test, puf->e->pos,
		puf->e->start + TEST_ARRAY_BUF_OFFSET + sizeof(u16) + 1);
}

static void policy_unpack_test_unpack_array_out_of_bounds(struct kunit *test)
{
	struct policy_unpack_fixture *puf = test->priv;
	const char name[] = TEST_ARRAY_NAME;
	u16 array_size;

	puf->e->pos += TEST_NAMED_ARRAY_BUF_OFFSET;
	puf->e->end = puf->e->start + TEST_ARRAY_BUF_OFFSET + sizeof(u16);

	KUNIT_EXPECT_FALSE(test, aa_unpack_array(puf->e, name, &array_size));
	KUNIT_EXPECT_PTR_EQ(test, puf->e->pos,
		puf->e->start + TEST_NAMED_ARRAY_BUF_OFFSET);
}

static void policy_unpack_test_unpack_blob_with_null_name(struct kunit *test)
{
	struct policy_unpack_fixture *puf = test->priv;
	char *blob = NULL;
	size_t size;

	puf->e->pos += TEST_BLOB_BUF_OFFSET;
	size = aa_unpack_blob(puf->e, &blob, NULL);

	KUNIT_ASSERT_EQ(test, size, TEST_BLOB_DATA_SIZE);
	KUNIT_EXPECT_TRUE(test,
		memcmp(blob, TEST_BLOB_DATA, TEST_BLOB_DATA_SIZE) == 0);
}

static void policy_unpack_test_unpack_blob_with_name(struct kunit *test)
{
	struct policy_unpack_fixture *puf = test->priv;
	char *blob = NULL;
	size_t size;

	puf->e->pos += TEST_NAMED_BLOB_BUF_OFFSET;
	size = aa_unpack_blob(puf->e, &blob, TEST_BLOB_NAME);

	KUNIT_ASSERT_EQ(test, size, TEST_BLOB_DATA_SIZE);
	KUNIT_EXPECT_TRUE(test,
		memcmp(blob, TEST_BLOB_DATA, TEST_BLOB_DATA_SIZE) == 0);
}

static void policy_unpack_test_unpack_blob_out_of_bounds(struct kunit *test)
{
	struct policy_unpack_fixture *puf = test->priv;
	char *blob = NULL;
	void *start;
	int size;

	puf->e->pos += TEST_NAMED_BLOB_BUF_OFFSET;
	start = puf->e->pos;
	puf->e->end = puf->e->start + TEST_BLOB_BUF_OFFSET
		+ TEST_BLOB_DATA_SIZE - 1;

	size = aa_unpack_blob(puf->e, &blob, TEST_BLOB_NAME);

	KUNIT_EXPECT_EQ(test, size, 0);
	KUNIT_EXPECT_PTR_EQ(test, puf->e->pos, start);
}

static void policy_unpack_test_unpack_str_with_null_name(struct kunit *test)
{
	struct policy_unpack_fixture *puf = test->priv;
	const char *string = NULL;
	size_t size;

	puf->e->pos += TEST_STRING_BUF_OFFSET;
	size = aa_unpack_str(puf->e, &string, NULL);

	KUNIT_EXPECT_EQ(test, size, strlen(TEST_STRING_DATA) + 1);
	KUNIT_EXPECT_STREQ(test, string, TEST_STRING_DATA);
}

static void policy_unpack_test_unpack_str_with_name(struct kunit *test)
{
	struct policy_unpack_fixture *puf = test->priv;
	const char *string = NULL;
	size_t size;

	size = aa_unpack_str(puf->e, &string, TEST_STRING_NAME);

	KUNIT_EXPECT_EQ(test, size, strlen(TEST_STRING_DATA) + 1);
	KUNIT_EXPECT_STREQ(test, string, TEST_STRING_DATA);
}

static void policy_unpack_test_unpack_str_out_of_bounds(struct kunit *test)
{
	struct policy_unpack_fixture *puf = test->priv;
	const char *string = NULL;
	void *start = puf->e->pos;
	int size;

	puf->e->end = puf->e->pos + TEST_STRING_BUF_OFFSET
		+ strlen(TEST_STRING_DATA) - 1;

	size = aa_unpack_str(puf->e, &string, TEST_STRING_NAME);

	KUNIT_EXPECT_EQ(test, size, 0);
	KUNIT_EXPECT_PTR_EQ(test, puf->e->pos, start);
}

static void policy_unpack_test_unpack_strdup_with_null_name(struct kunit *test)
{
	struct policy_unpack_fixture *puf = test->priv;
	char *string = NULL;
	size_t size;

	puf->e->pos += TEST_STRING_BUF_OFFSET;
	size = aa_unpack_strdup(puf->e, &string, NULL);

	KUNIT_EXPECT_EQ(test, size, strlen(TEST_STRING_DATA) + 1);
	KUNIT_EXPECT_FALSE(test,
			   ((uintptr_t)puf->e->start <= (uintptr_t)string)
			   && ((uintptr_t)string <= (uintptr_t)puf->e->end));
	KUNIT_EXPECT_STREQ(test, string, TEST_STRING_DATA);

	kfree(string);
}

static void policy_unpack_test_unpack_strdup_with_name(struct kunit *test)
{
	struct policy_unpack_fixture *puf = test->priv;
	char *string = NULL;
	size_t size;

	size = aa_unpack_strdup(puf->e, &string, TEST_STRING_NAME);

	KUNIT_EXPECT_EQ(test, size, strlen(TEST_STRING_DATA) + 1);
	KUNIT_EXPECT_FALSE(test,
			   ((uintptr_t)puf->e->start <= (uintptr_t)string)
			   && ((uintptr_t)string <= (uintptr_t)puf->e->end));
	KUNIT_EXPECT_STREQ(test, string, TEST_STRING_DATA);

	kfree(string);
}

static void policy_unpack_test_unpack_strdup_out_of_bounds(struct kunit *test)
{
	struct policy_unpack_fixture *puf = test->priv;
	void *start = puf->e->pos;
	char *string = NULL;
	int size;

	puf->e->end = puf->e->pos + TEST_STRING_BUF_OFFSET
		+ strlen(TEST_STRING_DATA) - 1;

	size = aa_unpack_strdup(puf->e, &string, TEST_STRING_NAME);

	KUNIT_EXPECT_EQ(test, size, 0);
	KUNIT_EXPECT_NULL(test, string);
	KUNIT_EXPECT_PTR_EQ(test, puf->e->pos, start);

	kfree(string);
}

static void policy_unpack_test_unpack_nameX_with_null_name(struct kunit *test)
{
	struct policy_unpack_fixture *puf = test->priv;
	bool success;

	puf->e->pos += TEST_U32_BUF_OFFSET;

	success = aa_unpack_nameX(puf->e, AA_U32, NULL);

	KUNIT_EXPECT_TRUE(test, success);
	KUNIT_EXPECT_PTR_EQ(test, puf->e->pos,
			    puf->e->start + TEST_U32_BUF_OFFSET + 1);
}

static void policy_unpack_test_unpack_nameX_with_wrong_code(struct kunit *test)
{
	struct policy_unpack_fixture *puf = test->priv;
	bool success;

	puf->e->pos += TEST_U32_BUF_OFFSET;

	success = aa_unpack_nameX(puf->e, AA_BLOB, NULL);

	KUNIT_EXPECT_FALSE(test, success);
	KUNIT_EXPECT_PTR_EQ(test, puf->e->pos,
			    puf->e->start + TEST_U32_BUF_OFFSET);
}

static void policy_unpack_test_unpack_nameX_with_name(struct kunit *test)
{
	struct policy_unpack_fixture *puf = test->priv;
	const char name[] = TEST_U32_NAME;
	bool success;

	puf->e->pos += TEST_NAMED_U32_BUF_OFFSET;

	success = aa_unpack_nameX(puf->e, AA_U32, name);

	KUNIT_EXPECT_TRUE(test, success);
	KUNIT_EXPECT_PTR_EQ(test, puf->e->pos,
			    puf->e->start + TEST_U32_BUF_OFFSET + 1);
}

static void policy_unpack_test_unpack_nameX_with_wrong_name(struct kunit *test)
{
	struct policy_unpack_fixture *puf = test->priv;
	static const char name[] = "12345678";
	bool success;

	puf->e->pos += TEST_NAMED_U32_BUF_OFFSET;

	success = aa_unpack_nameX(puf->e, AA_U32, name);

	KUNIT_EXPECT_FALSE(test, success);
	KUNIT_EXPECT_PTR_EQ(test, puf->e->pos,
			    puf->e->start + TEST_NAMED_U32_BUF_OFFSET);
}

static void policy_unpack_test_unpack_u16_chunk_basic(struct kunit *test)
{
	struct policy_unpack_fixture *puf = test->priv;
	char *chunk = NULL;
	size_t size;

	puf->e->pos += TEST_U16_OFFSET;
	/*
	 * WARNING: For unit testing purposes, we're pushing puf->e->end past
	 * the end of the allocated memory. Doing anything other than comparing
	 * memory addresses is dangerous.
	 */
	puf->e->end += TEST_U16_DATA;

	size = aa_unpack_u16_chunk(puf->e, &chunk);

	KUNIT_EXPECT_PTR_EQ(test, chunk,
			    puf->e->start + TEST_U16_OFFSET + 2);
	KUNIT_EXPECT_EQ(test, size, TEST_U16_DATA);
	KUNIT_EXPECT_PTR_EQ(test, puf->e->pos, (chunk + TEST_U16_DATA));
}

static void policy_unpack_test_unpack_u16_chunk_out_of_bounds_1(
		struct kunit *test)
{
	struct policy_unpack_fixture *puf = test->priv;
	char *chunk = NULL;
	size_t size;

	puf->e->pos = puf->e->end - 1;

	size = aa_unpack_u16_chunk(puf->e, &chunk);

	KUNIT_EXPECT_EQ(test, size, 0);
	KUNIT_EXPECT_NULL(test, chunk);
	KUNIT_EXPECT_PTR_EQ(test, puf->e->pos, puf->e->end - 1);
}

static void policy_unpack_test_unpack_u16_chunk_out_of_bounds_2(
		struct kunit *test)
{
	struct policy_unpack_fixture *puf = test->priv;
	char *chunk = NULL;
	size_t size;

	puf->e->pos += TEST_U16_OFFSET;
	/*
	 * WARNING: For unit testing purposes, we're pushing puf->e->end past
	 * the end of the allocated memory. Doing anything other than comparing
	 * memory addresses is dangerous.
	 */
	puf->e->end = puf->e->pos + TEST_U16_DATA - 1;

	size = aa_unpack_u16_chunk(puf->e, &chunk);

	KUNIT_EXPECT_EQ(test, size, 0);
	KUNIT_EXPECT_NULL(test, chunk);
	KUNIT_EXPECT_PTR_EQ(test, puf->e->pos, puf->e->start + TEST_U16_OFFSET);
}

static void policy_unpack_test_unpack_u32_with_null_name(struct kunit *test)
{
	struct policy_unpack_fixture *puf = test->priv;
	bool success;
	u32 data = 0;

	puf->e->pos += TEST_U32_BUF_OFFSET;

	success = aa_unpack_u32(puf->e, &data, NULL);

	KUNIT_EXPECT_TRUE(test, success);
	KUNIT_EXPECT_EQ(test, data, TEST_U32_DATA);
	KUNIT_EXPECT_PTR_EQ(test, puf->e->pos,
			puf->e->start + TEST_U32_BUF_OFFSET + sizeof(u32) + 1);
}

static void policy_unpack_test_unpack_u32_with_name(struct kunit *test)
{
	struct policy_unpack_fixture *puf = test->priv;
	const char name[] = TEST_U32_NAME;
	bool success;
	u32 data = 0;

	puf->e->pos += TEST_NAMED_U32_BUF_OFFSET;

	success = aa_unpack_u32(puf->e, &data, name);

	KUNIT_EXPECT_TRUE(test, success);
	KUNIT_EXPECT_EQ(test, data, TEST_U32_DATA);
	KUNIT_EXPECT_PTR_EQ(test, puf->e->pos,
			puf->e->start + TEST_U32_BUF_OFFSET + sizeof(u32) + 1);
}

static void policy_unpack_test_unpack_u32_out_of_bounds(struct kunit *test)
{
	struct policy_unpack_fixture *puf = test->priv;
	const char name[] = TEST_U32_NAME;
	bool success;
	u32 data = 0;

	puf->e->pos += TEST_NAMED_U32_BUF_OFFSET;
	puf->e->end = puf->e->start + TEST_U32_BUF_OFFSET + sizeof(u32);

	success = aa_unpack_u32(puf->e, &data, name);

	KUNIT_EXPECT_FALSE(test, success);
	KUNIT_EXPECT_PTR_EQ(test, puf->e->pos,
			puf->e->start + TEST_NAMED_U32_BUF_OFFSET);
}

static void policy_unpack_test_unpack_u64_with_null_name(struct kunit *test)
{
	struct policy_unpack_fixture *puf = test->priv;
	bool success;
	u64 data = 0;

	puf->e->pos += TEST_U64_BUF_OFFSET;

	success = aa_unpack_u64(puf->e, &data, NULL);

	KUNIT_EXPECT_TRUE(test, success);
	KUNIT_EXPECT_EQ(test, data, TEST_U64_DATA);
	KUNIT_EXPECT_PTR_EQ(test, puf->e->pos,
			puf->e->start + TEST_U64_BUF_OFFSET + sizeof(u64) + 1);
}

static void policy_unpack_test_unpack_u64_with_name(struct kunit *test)
{
	struct policy_unpack_fixture *puf = test->priv;
	const char name[] = TEST_U64_NAME;
	bool success;
	u64 data = 0;

	puf->e->pos += TEST_NAMED_U64_BUF_OFFSET;

	success = aa_unpack_u64(puf->e, &data, name);

	KUNIT_EXPECT_TRUE(test, success);
	KUNIT_EXPECT_EQ(test, data, TEST_U64_DATA);
	KUNIT_EXPECT_PTR_EQ(test, puf->e->pos,
			puf->e->start + TEST_U64_BUF_OFFSET + sizeof(u64) + 1);
}

static void policy_unpack_test_unpack_u64_out_of_bounds(struct kunit *test)
{
	struct policy_unpack_fixture *puf = test->priv;
	const char name[] = TEST_U64_NAME;
	bool success;
	u64 data = 0;

	puf->e->pos += TEST_NAMED_U64_BUF_OFFSET;
	puf->e->end = puf->e->start + TEST_U64_BUF_OFFSET + sizeof(u64);

	success = aa_unpack_u64(puf->e, &data, name);

	KUNIT_EXPECT_FALSE(test, success);
	KUNIT_EXPECT_PTR_EQ(test, puf->e->pos,
			puf->e->start + TEST_NAMED_U64_BUF_OFFSET);
}

static void policy_unpack_test_unpack_X_code_match(struct kunit *test)
{
	struct policy_unpack_fixture *puf = test->priv;
	bool success = aa_unpack_X(puf->e, AA_NAME);

	KUNIT_EXPECT_TRUE(test, success);
	KUNIT_EXPECT_TRUE(test, puf->e->pos == puf->e->start + 1);
}

static void policy_unpack_test_unpack_X_code_mismatch(struct kunit *test)
{
	struct policy_unpack_fixture *puf = test->priv;
	bool success = aa_unpack_X(puf->e, AA_STRING);

	KUNIT_EXPECT_FALSE(test, success);
	KUNIT_EXPECT_TRUE(test, puf->e->pos == puf->e->start);
}

static void policy_unpack_test_unpack_X_out_of_bounds(struct kunit *test)
{
	struct policy_unpack_fixture *puf = test->priv;
	bool success;

	puf->e->pos = puf->e->end;
	success = aa_unpack_X(puf->e, AA_NAME);

	KUNIT_EXPECT_FALSE(test, success);
}

static struct kunit_case apparmor_policy_unpack_test_cases[] = {
	KUNIT_CASE(policy_unpack_test_inbounds_when_inbounds),
	KUNIT_CASE(policy_unpack_test_inbounds_when_out_of_bounds),
	KUNIT_CASE(policy_unpack_test_unpack_array_with_null_name),
	KUNIT_CASE(policy_unpack_test_unpack_array_with_name),
	KUNIT_CASE(policy_unpack_test_unpack_array_out_of_bounds),
	KUNIT_CASE(policy_unpack_test_unpack_blob_with_null_name),
	KUNIT_CASE(policy_unpack_test_unpack_blob_with_name),
	KUNIT_CASE(policy_unpack_test_unpack_blob_out_of_bounds),
	KUNIT_CASE(policy_unpack_test_unpack_nameX_with_null_name),
	KUNIT_CASE(policy_unpack_test_unpack_nameX_with_wrong_code),
	KUNIT_CASE(policy_unpack_test_unpack_nameX_with_name),
	KUNIT_CASE(policy_unpack_test_unpack_nameX_with_wrong_name),
	KUNIT_CASE(policy_unpack_test_unpack_str_with_null_name),
	KUNIT_CASE(policy_unpack_test_unpack_str_with_name),
	KUNIT_CASE(policy_unpack_test_unpack_str_out_of_bounds),
	KUNIT_CASE(policy_unpack_test_unpack_strdup_with_null_name),
	KUNIT_CASE(policy_unpack_test_unpack_strdup_with_name),
	KUNIT_CASE(policy_unpack_test_unpack_strdup_out_of_bounds),
	KUNIT_CASE(policy_unpack_test_unpack_u16_chunk_basic),
	KUNIT_CASE(policy_unpack_test_unpack_u16_chunk_out_of_bounds_1),
	KUNIT_CASE(policy_unpack_test_unpack_u16_chunk_out_of_bounds_2),
	KUNIT_CASE(policy_unpack_test_unpack_u32_with_null_name),
	KUNIT_CASE(policy_unpack_test_unpack_u32_with_name),
	KUNIT_CASE(policy_unpack_test_unpack_u32_out_of_bounds),
	KUNIT_CASE(policy_unpack_test_unpack_u64_with_null_name),
	KUNIT_CASE(policy_unpack_test_unpack_u64_with_name),
	KUNIT_CASE(policy_unpack_test_unpack_u64_out_of_bounds),
	KUNIT_CASE(policy_unpack_test_unpack_X_code_match),
	KUNIT_CASE(policy_unpack_test_unpack_X_code_mismatch),
	KUNIT_CASE(policy_unpack_test_unpack_X_out_of_bounds),
	{},
};

static struct kunit_suite apparmor_policy_unpack_test_module = {
	.name = "apparmor_policy_unpack",
	.init = policy_unpack_test_init,
	.test_cases = apparmor_policy_unpack_test_cases,
};

kunit_test_suite(apparmor_policy_unpack_test_module);

MODULE_DESCRIPTION("KUnit tests for AppArmor's policy unpack");
MODULE_LICENSE("GPL");
