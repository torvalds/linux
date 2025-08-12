// SPDX-License-Identifier: GPL-2.0
/*
 * This file contains a KFuzzTest example target that ensures that a buffer
 * overflow on a nested region triggers a KASAN OOB access report.
 *
 * Copyright 2025 Google LLC
 */
#include <linux/kfuzztest.h>

static void overflow_on_nested_buffer(const char *a, size_t a_len, const char *b, size_t b_len)
{
	size_t i;
	pr_info("a = [%px, %px)", a, a + a_len);
	pr_info("b = [%px, %px)", b, b + b_len);

	/* Ensure that all bytes in arg->b are accessible. */
	for (i = 0; i < b_len; i++)
		READ_ONCE(b[i]);
	/*
	 * Check that all bytes in arg->a are accessible, and provoke an OOB on
	 * the first byte to the right of the buffer which will trigger a KASAN
	 * report.
	 */
	for (i = 0; i <= a_len; i++)
		READ_ONCE(a[i]);
}

struct nested_buffers {
	const char *a;
	size_t a_len;
	const char *b;
	size_t b_len;
};

/**
 * The KFuzzTest input format specifies that struct nested buffers should
 * be expanded as:
 *
 * | a | b | pad[8] | *a | pad[8] | *b |
 *
 * where the padded regions are poisoned. We expect to trigger a KASAN report by
 * overflowing one byte into the `a` buffer.
 */
FUZZ_TEST(test_overflow_on_nested_buffer, struct nested_buffers)
{
	KFUZZTEST_EXPECT_NOT_NULL(nested_buffers, a);
	KFUZZTEST_EXPECT_NOT_NULL(nested_buffers, b);
	KFUZZTEST_ANNOTATE_LEN(nested_buffers, a_len, a);
	KFUZZTEST_ANNOTATE_LEN(nested_buffers, b_len, b);

	overflow_on_nested_buffer(arg->a, arg->a_len, arg->b, arg->b_len);
}
