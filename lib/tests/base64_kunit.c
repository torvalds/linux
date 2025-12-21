// SPDX-License-Identifier: GPL-2.0
/*
 * base64_kunit_test.c - KUnit tests for base64 encoding and decoding functions
 *
 * Copyright (c) 2025, Guan-Chun Wu <409411716@gms.tku.edu.tw>
 */

#include <kunit/test.h>
#include <linux/base64.h>

/* ---------- Benchmark helpers ---------- */
static u64 bench_encode_ns(const u8 *data, int len, char *dst, int reps,
			   enum base64_variant variant)
{
	u64 t0, t1;

	t0 = ktime_get_ns();
	for (int i = 0; i < reps; i++)
		base64_encode(data, len, dst, true, variant);
	t1 = ktime_get_ns();

	return div64_u64(t1 - t0, (u64)reps);
}

static u64 bench_decode_ns(const char *data, int len, u8 *dst, int reps,
			   enum base64_variant variant)
{
	u64 t0, t1;

	t0 = ktime_get_ns();
	for (int i = 0; i < reps; i++)
		base64_decode(data, len, dst, true, variant);
	t1 = ktime_get_ns();

	return div64_u64(t1 - t0, (u64)reps);
}

static void run_perf_and_check(struct kunit *test, const char *label, int size,
			       enum base64_variant variant)
{
	const int reps = 1000;
	size_t outlen = DIV_ROUND_UP(size, 3) * 4;
	u8 *in = kmalloc(size, GFP_KERNEL);
	char *enc = kmalloc(outlen, GFP_KERNEL);
	u8 *decoded = kmalloc(size, GFP_KERNEL);

	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, in);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, enc);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, decoded);

	get_random_bytes(in, size);
	int enc_len = base64_encode(in, size, enc, true, variant);
	int dec_len = base64_decode(enc, enc_len, decoded, true, variant);

	/* correctness sanity check */
	KUNIT_EXPECT_EQ(test, dec_len, size);
	KUNIT_EXPECT_MEMEQ(test, decoded, in, size);

	/* benchmark encode */

	u64 t1 = bench_encode_ns(in, size, enc, reps, variant);

	kunit_info(test, "[%s] encode run : %lluns", label, t1);

	u64 t2 = bench_decode_ns(enc, enc_len, decoded, reps, variant);

	kunit_info(test, "[%s] decode run : %lluns", label, t2);

	kfree(in);
	kfree(enc);
	kfree(decoded);
}

static void base64_performance_tests(struct kunit *test)
{
	/* run on STD variant only */
	run_perf_and_check(test, "64B", 64, BASE64_STD);
	run_perf_and_check(test, "1KB", 1024, BASE64_STD);
}

/* ---------- Helpers for encode ---------- */
static void expect_encode_ok(struct kunit *test, const u8 *src, int srclen,
			     const char *expected, bool padding,
			     enum base64_variant variant)
{
	char buf[128];
	int encoded_len = base64_encode(src, srclen, buf, padding, variant);

	buf[encoded_len] = '\0';

	KUNIT_EXPECT_EQ(test, encoded_len, strlen(expected));
	KUNIT_EXPECT_STREQ(test, buf, expected);
}

/* ---------- Helpers for decode ---------- */
static void expect_decode_ok(struct kunit *test, const char *src,
			     const u8 *expected, int expected_len, bool padding,
			     enum base64_variant variant)
{
	u8 buf[128];
	int decoded_len = base64_decode(src, strlen(src), buf, padding, variant);

	KUNIT_EXPECT_EQ(test, decoded_len, expected_len);
	KUNIT_EXPECT_MEMEQ(test, buf, expected, expected_len);
}

static void expect_decode_err(struct kunit *test, const char *src,
			      int srclen, bool padding,
			      enum base64_variant variant)
{
	u8 buf[64];
	int decoded_len = base64_decode(src, srclen, buf, padding, variant);

	KUNIT_EXPECT_EQ(test, decoded_len, -1);
}

/* ---------- Encode Tests ---------- */
static void base64_std_encode_tests(struct kunit *test)
{
	/* With padding */
	expect_encode_ok(test, (const u8 *)"", 0, "", true, BASE64_STD);
	expect_encode_ok(test, (const u8 *)"f", 1, "Zg==", true, BASE64_STD);
	expect_encode_ok(test, (const u8 *)"fo", 2, "Zm8=", true, BASE64_STD);
	expect_encode_ok(test, (const u8 *)"foo", 3, "Zm9v", true, BASE64_STD);
	expect_encode_ok(test, (const u8 *)"foob", 4, "Zm9vYg==", true, BASE64_STD);
	expect_encode_ok(test, (const u8 *)"fooba", 5, "Zm9vYmE=", true, BASE64_STD);
	expect_encode_ok(test, (const u8 *)"foobar", 6, "Zm9vYmFy", true, BASE64_STD);

	/* Extra cases with padding */
	expect_encode_ok(test, (const u8 *)"Hello, world!", 13, "SGVsbG8sIHdvcmxkIQ==",
			 true, BASE64_STD);
	expect_encode_ok(test, (const u8 *)"ABCDEFGHIJKLMNOPQRSTUVWXYZ", 26,
			 "QUJDREVGR0hJSktMTU5PUFFSU1RVVldYWVo=", true, BASE64_STD);
	expect_encode_ok(test, (const u8 *)"abcdefghijklmnopqrstuvwxyz", 26,
			 "YWJjZGVmZ2hpamtsbW5vcHFyc3R1dnd4eXo=", true, BASE64_STD);
	expect_encode_ok(test, (const u8 *)"0123456789+/", 12, "MDEyMzQ1Njc4OSsv",
			 true, BASE64_STD);

	/* Without padding */
	expect_encode_ok(test, (const u8 *)"", 0, "", false, BASE64_STD);
	expect_encode_ok(test, (const u8 *)"f", 1, "Zg", false, BASE64_STD);
	expect_encode_ok(test, (const u8 *)"fo", 2, "Zm8", false, BASE64_STD);
	expect_encode_ok(test, (const u8 *)"foo", 3, "Zm9v", false, BASE64_STD);
	expect_encode_ok(test, (const u8 *)"foob", 4, "Zm9vYg", false, BASE64_STD);
	expect_encode_ok(test, (const u8 *)"fooba", 5, "Zm9vYmE", false, BASE64_STD);
	expect_encode_ok(test, (const u8 *)"foobar", 6, "Zm9vYmFy", false, BASE64_STD);

	/* Extra cases without padding */
	expect_encode_ok(test, (const u8 *)"Hello, world!", 13, "SGVsbG8sIHdvcmxkIQ",
			 false, BASE64_STD);
	expect_encode_ok(test, (const u8 *)"ABCDEFGHIJKLMNOPQRSTUVWXYZ", 26,
			 "QUJDREVGR0hJSktMTU5PUFFSU1RVVldYWVo", false, BASE64_STD);
	expect_encode_ok(test, (const u8 *)"abcdefghijklmnopqrstuvwxyz", 26,
			 "YWJjZGVmZ2hpamtsbW5vcHFyc3R1dnd4eXo", false, BASE64_STD);
	expect_encode_ok(test, (const u8 *)"0123456789+/", 12, "MDEyMzQ1Njc4OSsv",
			 false, BASE64_STD);
}

/* ---------- Decode Tests ---------- */
static void base64_std_decode_tests(struct kunit *test)
{
	/* -------- With padding --------*/
	expect_decode_ok(test, "", (const u8 *)"", 0, true, BASE64_STD);
	expect_decode_ok(test, "Zg==", (const u8 *)"f", 1, true, BASE64_STD);
	expect_decode_ok(test, "Zm8=", (const u8 *)"fo", 2, true, BASE64_STD);
	expect_decode_ok(test, "Zm9v", (const u8 *)"foo", 3, true, BASE64_STD);
	expect_decode_ok(test, "Zm9vYg==", (const u8 *)"foob", 4, true, BASE64_STD);
	expect_decode_ok(test, "Zm9vYmE=", (const u8 *)"fooba", 5, true, BASE64_STD);
	expect_decode_ok(test, "Zm9vYmFy", (const u8 *)"foobar", 6, true, BASE64_STD);
	expect_decode_ok(test, "SGVsbG8sIHdvcmxkIQ==", (const u8 *)"Hello, world!", 13,
			 true, BASE64_STD);
	expect_decode_ok(test, "QUJDREVGR0hJSktMTU5PUFFSU1RVVldYWVo=",
			 (const u8 *)"ABCDEFGHIJKLMNOPQRSTUVWXYZ", 26, true, BASE64_STD);
	expect_decode_ok(test, "YWJjZGVmZ2hpamtsbW5vcHFyc3R1dnd4eXo=",
			 (const u8 *)"abcdefghijklmnopqrstuvwxyz", 26, true, BASE64_STD);

	/* Error cases */
	expect_decode_err(test, "Zg=!", 4, true, BASE64_STD);
	expect_decode_err(test, "Zm$=", 4, true, BASE64_STD);
	expect_decode_err(test, "Z===", 4, true, BASE64_STD);
	expect_decode_err(test, "Zg", 2, true, BASE64_STD);
	expect_decode_err(test, "Zm9v====", 8, true, BASE64_STD);
	expect_decode_err(test, "Zm==A", 5, true, BASE64_STD);

	{
		char with_nul[4] = { 'Z', 'g', '\0', '=' };

		expect_decode_err(test, with_nul, 4, true, BASE64_STD);
	}

	/* -------- Without padding --------*/
	expect_decode_ok(test, "", (const u8 *)"", 0, false, BASE64_STD);
	expect_decode_ok(test, "Zg", (const u8 *)"f", 1, false, BASE64_STD);
	expect_decode_ok(test, "Zm8", (const u8 *)"fo", 2, false, BASE64_STD);
	expect_decode_ok(test, "Zm9v", (const u8 *)"foo", 3, false, BASE64_STD);
	expect_decode_ok(test, "Zm9vYg", (const u8 *)"foob", 4, false, BASE64_STD);
	expect_decode_ok(test, "Zm9vYmE", (const u8 *)"fooba", 5, false, BASE64_STD);
	expect_decode_ok(test, "Zm9vYmFy", (const u8 *)"foobar", 6, false, BASE64_STD);
	expect_decode_ok(test, "TWFu", (const u8 *)"Man", 3, false, BASE64_STD);
	expect_decode_ok(test, "SGVsbG8sIHdvcmxkIQ", (const u8 *)"Hello, world!", 13,
			 false, BASE64_STD);
	expect_decode_ok(test, "QUJDREVGR0hJSktMTU5PUFFSU1RVVldYWVo",
			 (const u8 *)"ABCDEFGHIJKLMNOPQRSTUVWXYZ", 26, false, BASE64_STD);
	expect_decode_ok(test, "YWJjZGVmZ2hpamtsbW5vcHFyc3R1dnd4eXo",
			 (const u8 *)"abcdefghijklmnopqrstuvwxyz", 26, false, BASE64_STD);
	expect_decode_ok(test, "MDEyMzQ1Njc4OSsv", (const u8 *)"0123456789+/", 12,
			 false, BASE64_STD);

	/* Error cases */
	expect_decode_err(test, "Zg=!", 4, false, BASE64_STD);
	expect_decode_err(test, "Zm$=", 4, false, BASE64_STD);
	expect_decode_err(test, "Z===", 4, false, BASE64_STD);
	expect_decode_err(test, "Zg=", 3, false, BASE64_STD);
	expect_decode_err(test, "Zm9v====", 8, false, BASE64_STD);
	expect_decode_err(test, "Zm==v", 4, false, BASE64_STD);

	{
		char with_nul[4] = { 'Z', 'g', '\0', '=' };

		expect_decode_err(test, with_nul, 4, false, BASE64_STD);
	}
}

/* ---------- Variant tests (URLSAFE / IMAP) ---------- */
static void base64_variant_tests(struct kunit *test)
{
	const u8 sample1[] = { 0x00, 0xfb, 0xff, 0x7f, 0x80 };
	char std_buf[128], url_buf[128], imap_buf[128];
	u8 back[128];
	int n_std, n_url, n_imap, m;
	int i;

	n_std = base64_encode(sample1, sizeof(sample1), std_buf, false, BASE64_STD);
	n_url = base64_encode(sample1, sizeof(sample1), url_buf, false, BASE64_URLSAFE);
	std_buf[n_std] = '\0';
	url_buf[n_url] = '\0';

	for (i = 0; i < n_std; i++) {
		if (std_buf[i] == '+')
			std_buf[i] = '-';
		else if (std_buf[i] == '/')
			std_buf[i] = '_';
	}
	KUNIT_EXPECT_STREQ(test, std_buf, url_buf);

	m = base64_decode(url_buf, n_url, back, false, BASE64_URLSAFE);
	KUNIT_EXPECT_EQ(test, m, (int)sizeof(sample1));
	KUNIT_EXPECT_MEMEQ(test, back, sample1, sizeof(sample1));

	n_std  = base64_encode(sample1, sizeof(sample1), std_buf, false, BASE64_STD);
	n_imap = base64_encode(sample1, sizeof(sample1), imap_buf, false, BASE64_IMAP);
	std_buf[n_std]   = '\0';
	imap_buf[n_imap] = '\0';

	for (i = 0; i < n_std; i++)
		if (std_buf[i] == '/')
			std_buf[i] = ',';
	KUNIT_EXPECT_STREQ(test, std_buf, imap_buf);

	m = base64_decode(imap_buf, n_imap, back, false, BASE64_IMAP);
	KUNIT_EXPECT_EQ(test, m, (int)sizeof(sample1));
	KUNIT_EXPECT_MEMEQ(test, back, sample1, sizeof(sample1));

	{
		const char *bad = "Zg==";
		u8 tmp[8];

		m = base64_decode(bad, strlen(bad), tmp, false, BASE64_URLSAFE);
		KUNIT_EXPECT_EQ(test, m, -1);

		m = base64_decode(bad, strlen(bad), tmp, false, BASE64_IMAP);
		KUNIT_EXPECT_EQ(test, m, -1);
	}
}

/* ---------- Test registration ---------- */
static struct kunit_case base64_test_cases[] = {
	KUNIT_CASE(base64_performance_tests),
	KUNIT_CASE(base64_std_encode_tests),
	KUNIT_CASE(base64_std_decode_tests),
	KUNIT_CASE(base64_variant_tests),
	{}
};

static struct kunit_suite base64_test_suite = {
	.name = "base64",
	.test_cases = base64_test_cases,
};

kunit_test_suite(base64_test_suite);

MODULE_AUTHOR("Guan-Chun Wu <409411716@gms.tku.edu.tw>");
MODULE_DESCRIPTION("KUnit tests for Base64 encoding/decoding, including performance checks");
MODULE_LICENSE("GPL");
