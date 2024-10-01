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
#include "user_events_selftests.h"

const char *dyn_file = "/sys/kernel/tracing/dynamic_events";
const char *abi_file = "/sys/kernel/tracing/user_events_data";
const char *enable_file = "/sys/kernel/tracing/events/user_events/__test_event/enable";

static int event_delete(void)
{
	int fd = open(abi_file, O_RDWR);
	int ret;

	if (fd < 0)
		return -1;

	ret = ioctl(fd, DIAG_IOCSDEL, "__test_event");

	close(fd);

	return ret;
}

static bool wait_for_delete(void)
{
	int i;

	for (i = 0; i < 1000; ++i) {
		int fd = open(enable_file, O_RDONLY);

		if (fd == -1)
			return true;

		close(fd);
		usleep(1000);
	}

	return false;
}

static int reg_event(int fd, int *check, int bit, const char *value)
{
	struct user_reg reg = {0};

	reg.size = sizeof(reg);
	reg.name_args = (__u64)value;
	reg.enable_bit = bit;
	reg.enable_addr = (__u64)check;
	reg.enable_size = sizeof(*check);

	if (ioctl(fd, DIAG_IOCSREG, &reg) == -1)
		return -1;

	return 0;
}

static int unreg_event(int fd, int *check, int bit)
{
	struct user_unreg unreg = {0};

	unreg.size = sizeof(unreg);
	unreg.disable_bit = bit;
	unreg.disable_addr = (__u64)check;

	return ioctl(fd, DIAG_IOCSUNREG, &unreg);
}

static int parse_dyn(const char *value)
{
	int fd = open(dyn_file, O_RDWR | O_APPEND);
	int len = strlen(value);
	int ret;

	if (fd == -1)
		return -1;

	ret = write(fd, value, len);

	if (ret == len)
		ret = 0;
	else
		ret = -1;

	close(fd);

	if (ret == 0)
		event_delete();

	return ret;
}

static int parse_abi(int *check, const char *value)
{
	int fd = open(abi_file, O_RDWR);
	int ret;

	if (fd == -1)
		return -1;

	/* Until we have persist flags via dynamic events, use the base name */
	if (value[0] != 'u' || value[1] != ':') {
		close(fd);
		return -1;
	}

	ret = reg_event(fd, check, 31, value + 2);

	if (ret != -1) {
		if (unreg_event(fd, check, 31) == -1)
			printf("WARN: Couldn't unreg event\n");
	}

	close(fd);

	return ret;
}

static int parse(int *check, const char *value)
{
	int abi_ret = parse_abi(check, value);
	int dyn_ret = parse_dyn(value);

	/* Ensure both ABI and DYN parse the same way */
	if (dyn_ret != abi_ret)
		return -1;

	return dyn_ret;
}

static int check_match(int *check, const char *first, const char *second, bool *match)
{
	int fd = open(abi_file, O_RDWR);
	int ret = -1;

	if (fd == -1)
		return -1;

	if (reg_event(fd, check, 31, first) == -1)
		goto cleanup;

	if (reg_event(fd, check, 30, second) == -1) {
		if (errno == EADDRINUSE) {
			/* Name is in use, with different fields */
			*match = false;
			ret = 0;
		}

		goto cleanup;
	}

	*match = true;
	ret = 0;
cleanup:
	unreg_event(fd, check, 31);
	unreg_event(fd, check, 30);

	close(fd);

	wait_for_delete();

	return ret;
}

#define TEST_MATCH(x, y) \
do { \
	bool match; \
	ASSERT_NE(-1, check_match(&self->check, x, y, &match)); \
	ASSERT_EQ(true, match); \
} while (0)

#define TEST_NMATCH(x, y) \
do { \
	bool match; \
	ASSERT_NE(-1, check_match(&self->check, x, y, &match)); \
	ASSERT_EQ(false, match); \
} while (0)

#define TEST_PARSE(x) ASSERT_NE(-1, parse(&self->check, x))

#define TEST_NPARSE(x) ASSERT_EQ(-1, parse(&self->check, x))

FIXTURE(user) {
	int check;
	bool umount;
};

FIXTURE_SETUP(user) {
	USER_EVENT_FIXTURE_SETUP(return, self->umount);
}

FIXTURE_TEARDOWN(user) {
	USER_EVENT_FIXTURE_TEARDOWN(self->umount);

	wait_for_delete();
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

TEST_F(user, matching) {
	/* Single name matches */
	TEST_MATCH("__test_event u32 a",
		   "__test_event u32 a");

	/* Multiple names match */
	TEST_MATCH("__test_event u32 a; u32 b",
		   "__test_event u32 a; u32 b");

	/* Multiple names match with dangling ; */
	TEST_MATCH("__test_event u32 a; u32 b",
		   "__test_event u32 a; u32 b;");

	/* Single name doesn't match */
	TEST_NMATCH("__test_event u32 a",
		    "__test_event u32 b");

	/* Multiple names don't match */
	TEST_NMATCH("__test_event u32 a; u32 b",
		    "__test_event u32 b; u32 a");

	/* Types don't match */
	TEST_NMATCH("__test_event u64 a; u64 b",
		    "__test_event u32 a; u32 b");

	/* Struct name and size matches */
	TEST_MATCH("__test_event struct my_struct a 20",
		   "__test_event struct my_struct a 20");

	/* Struct name don't match */
	TEST_NMATCH("__test_event struct my_struct a 20",
		    "__test_event struct my_struct b 20");

	/* Struct size don't match */
	TEST_NMATCH("__test_event struct my_struct a 20",
		    "__test_event struct my_struct a 21");
}

int main(int argc, char **argv)
{
	return test_harness_run(argc, argv);
}
