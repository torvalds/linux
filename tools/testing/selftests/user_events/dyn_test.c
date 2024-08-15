// SPDX-License-Identifier: GPL-2.0
/*
 * User Events Dyn Events Test Program
 *
 * Copyright (c) 2021 Beau Belgrave <beaub@linux.microsoft.com>
 */

#include <errno.h>
#include <linux/user_events.h>
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <unistd.h>

#include "../kselftest_harness.h"

const char *dyn_file = "/sys/kernel/debug/tracing/dynamic_events";
const char *clear = "!u:__test_event";

static int Append(const char *value)
{
	int fd = open(dyn_file, O_RDWR | O_APPEND);
	int ret = write(fd, value, strlen(value));

	close(fd);
	return ret;
}

#define CLEAR() \
do { \
	int ret = Append(clear); \
	if (ret == -1) \
		ASSERT_EQ(ENOENT, errno); \
} while (0)

#define TEST_PARSE(x) \
do { \
	ASSERT_NE(-1, Append(x)); \
	CLEAR(); \
} while (0)

#define TEST_NPARSE(x) ASSERT_EQ(-1, Append(x))

FIXTURE(user) {
};

FIXTURE_SETUP(user) {
	CLEAR();
}

FIXTURE_TEARDOWN(user) {
	CLEAR();
}

TEST_F(user, basic_types) {
	/* All should work */
	TEST_PARSE("u:__test_event u64 a");
	TEST_PARSE("u:__test_event u32 a");
	TEST_PARSE("u:__test_event u16 a");
	TEST_PARSE("u:__test_event u8 a");
	TEST_PARSE("u:__test_event char a");
	TEST_PARSE("u:__test_event unsigned char a");
	TEST_PARSE("u:__test_event int a");
	TEST_PARSE("u:__test_event unsigned int a");
	TEST_PARSE("u:__test_event short a");
	TEST_PARSE("u:__test_event unsigned short a");
	TEST_PARSE("u:__test_event char[20] a");
	TEST_PARSE("u:__test_event unsigned char[20] a");
	TEST_PARSE("u:__test_event char[0x14] a");
	TEST_PARSE("u:__test_event unsigned char[0x14] a");
	/* Bad size format should fail */
	TEST_NPARSE("u:__test_event char[aa] a");
	/* Large size should fail */
	TEST_NPARSE("u:__test_event char[9999] a");
	/* Long size string should fail */
	TEST_NPARSE("u:__test_event char[0x0000000000001] a");
}

TEST_F(user, loc_types) {
	/* All should work */
	TEST_PARSE("u:__test_event __data_loc char[] a");
	TEST_PARSE("u:__test_event __data_loc unsigned char[] a");
	TEST_PARSE("u:__test_event __rel_loc char[] a");
	TEST_PARSE("u:__test_event __rel_loc unsigned char[] a");
}

TEST_F(user, size_types) {
	/* Should work */
	TEST_PARSE("u:__test_event struct custom a 20");
	/* Size not specified on struct should fail */
	TEST_NPARSE("u:__test_event struct custom a");
	/* Size specified on non-struct should fail */
	TEST_NPARSE("u:__test_event char a 20");
}

TEST_F(user, flags) {
	/* Should work */
	TEST_PARSE("u:__test_event:BPF_ITER u32 a");
	/* Forward compat */
	TEST_PARSE("u:__test_event:BPF_ITER,FLAG_FUTURE u32 a");
}

TEST_F(user, matching) {
	/* Register */
	ASSERT_NE(-1, Append("u:__test_event struct custom a 20"));
	/* Should not match */
	TEST_NPARSE("!u:__test_event struct custom b");
	/* Should match */
	TEST_PARSE("!u:__test_event struct custom a");
	/* Multi field reg */
	ASSERT_NE(-1, Append("u:__test_event u32 a; u32 b"));
	/* Non matching cases */
	TEST_NPARSE("!u:__test_event u32 a");
	TEST_NPARSE("!u:__test_event u32 b");
	TEST_NPARSE("!u:__test_event u32 a; u32 ");
	TEST_NPARSE("!u:__test_event u32 a; u32 a");
	/* Matching case */
	TEST_PARSE("!u:__test_event u32 a; u32 b");
	/* Register */
	ASSERT_NE(-1, Append("u:__test_event u32 a; u32 b"));
	/* Ensure trailing semi-colon case */
	TEST_PARSE("!u:__test_event u32 a; u32 b;");
}

int main(int argc, char **argv)
{
	return test_harness_run(argc, argv);
}
