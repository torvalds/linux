// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2020 Facebook */

#include <test_progs.h>
#include <time.h>
#include "test_varlen.skel.h"

#define CHECK_VAL(got, exp) \
	CHECK((got) != (exp), "check", "got %ld != exp %ld\n", \
	      (long)(got), (long)(exp))

void test_varlen(void)
{
	int duration = 0, err;
	struct test_varlen* skel;
	struct test_varlen__bss *bss;
	struct test_varlen__data *data;
	const char str1[] = "Hello, ";
	const char str2[] = "World!";
	const char exp_str[] = "Hello, \0World!\0";
	const int size1 = sizeof(str1);
	const int size2 = sizeof(str2);

	skel = test_varlen__open_and_load();
	if (CHECK(!skel, "skel_open", "failed to open skeleton\n"))
		return;
	bss = skel->bss;
	data = skel->data;

	err = test_varlen__attach(skel);
	if (CHECK(err, "skel_attach", "skeleton attach failed: %d\n", err))
		goto cleanup;

	bss->test_pid = getpid();

	/* trigger everything */
	memcpy(bss->buf_in1, str1, size1);
	memcpy(bss->buf_in2, str2, size2);
	bss->capture = true;
	usleep(1);
	bss->capture = false;

	CHECK_VAL(bss->payload1_len1, size1);
	CHECK_VAL(bss->payload1_len2, size2);
	CHECK_VAL(bss->total1, size1 + size2);
	CHECK(memcmp(bss->payload1, exp_str, size1 + size2), "content_check",
	      "doesn't match!\n");

	CHECK_VAL(data->payload2_len1, size1);
	CHECK_VAL(data->payload2_len2, size2);
	CHECK_VAL(data->total2, size1 + size2);
	CHECK(memcmp(data->payload2, exp_str, size1 + size2), "content_check",
	      "doesn't match!\n");

	CHECK_VAL(data->payload3_len1, size1);
	CHECK_VAL(data->payload3_len2, size2);
	CHECK_VAL(data->total3, size1 + size2);
	CHECK(memcmp(data->payload3, exp_str, size1 + size2), "content_check",
	      "doesn't match!\n");

	CHECK_VAL(data->payload4_len1, size1);
	CHECK_VAL(data->payload4_len2, size2);
	CHECK_VAL(data->total4, size1 + size2);
	CHECK(memcmp(data->payload4, exp_str, size1 + size2), "content_check",
	      "doesn't match!\n");

	CHECK_VAL(bss->ret_bad_read, -EFAULT);
	CHECK_VAL(data->payload_bad[0], 0x42);
	CHECK_VAL(data->payload_bad[1], 0x42);
	CHECK_VAL(data->payload_bad[2], 0);
	CHECK_VAL(data->payload_bad[3], 0x42);
	CHECK_VAL(data->payload_bad[4], 0x42);
cleanup:
	test_varlen__destroy(skel);
}
