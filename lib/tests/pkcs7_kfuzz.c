// SPDX-License-Identifier: GPL-2.0-or-later
/* 
 * PKCS#7 parser KFuzzTest target
 *
 * Copyright 2025 Google LLC
 */
#include <crypto/pkcs7.h>
#include <linux/kfuzztest.h>

struct pkcs7_parse_message_arg {
	const void *data;
	size_t datalen;
};

FUZZ_TEST(test_pkcs7_parse_message, struct pkcs7_parse_message_arg)
{
	KFUZZTEST_EXPECT_NOT_NULL(pkcs7_parse_message_arg, data);
	KFUZZTEST_ANNOTATE_LEN(pkcs7_parse_message_arg, datalen, data);
	KFUZZTEST_EXPECT_LE(pkcs7_parse_message_arg, datalen, 16 * PAGE_SIZE);

	pkcs7_parse_message(arg->data, arg->datalen);
}
