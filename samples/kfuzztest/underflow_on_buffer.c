// SPDX-License-Identifier: GPL-2.0
/*
 * This file contains a KFuzzTest example target that ensures that a buffer
 * underflow on a region triggers a KASAN OOB access report.
 *
 * Copyright 2025 Google LLC
 */
#include <linux/kfuzztest.h>

static void underflow_on_buffer(char *buf, size_t buflen)
{
	size_t i;

	pr_info("buf = [%px, %px)", buf, buf + buflen);

	/* First ensure that all bytes in arg->b are accessible. */
	for (i = 0; i < buflen; i++)
		READ_ONCE(buf[i]);
	/*
	 * Provoke a buffer overflow on the first byte preceding b, triggering
	 * a KASAN report.
	 */
	READ_ONCE(*((char *)buf - 1));
}

struct some_buffer {
	char *buf;
	size_t buflen;
};

/**
 * Tests that the region between struct some_buffer and the expanded *buf field
 * is correctly poisoned by accessing the first byte before *buf.
 */
FUZZ_TEST(test_underflow_on_buffer, struct some_buffer)
{
	KFUZZTEST_EXPECT_NOT_NULL(some_buffer, buf);
	KFUZZTEST_ANNOTATE_LEN(some_buffer, buflen, buf);

	underflow_on_buffer(arg->buf, arg->buflen);
}
