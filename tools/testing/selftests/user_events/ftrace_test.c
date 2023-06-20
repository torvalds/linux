// SPDX-License-Identifier: GPL-2.0
/*
 * User Events FTrace Test Program
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
#include <sys/uio.h>
#include <unistd.h>

#include "../kselftest_harness.h"

const char *data_file = "/sys/kernel/tracing/user_events_data";
const char *status_file = "/sys/kernel/tracing/user_events_status";
const char *enable_file = "/sys/kernel/tracing/events/user_events/__test_event/enable";
const char *trace_file = "/sys/kernel/tracing/trace";
const char *fmt_file = "/sys/kernel/tracing/events/user_events/__test_event/format";

static int trace_bytes(void)
{
	int fd = open(trace_file, O_RDONLY);
	char buf[256];
	int bytes = 0, got;

	if (fd == -1)
		return -1;

	while (true) {
		got = read(fd, buf, sizeof(buf));

		if (got == -1)
			return -1;

		if (got == 0)
			break;

		bytes += got;
	}

	close(fd);

	return bytes;
}

static int skip_until_empty_line(FILE *fp)
{
	int c, last = 0;

	while (true) {
		c = getc(fp);

		if (c == EOF)
			break;

		if (last == '\n' && c == '\n')
			return 0;

		last = c;
	}

	return -1;
}

static int get_print_fmt(char *buffer, int len)
{
	FILE *fp = fopen(fmt_file, "r");
	char *newline;

	if (!fp)
		return -1;

	/* Read until empty line (Skip Common) */
	if (skip_until_empty_line(fp) < 0)
		goto err;

	/* Read until empty line (Skip Properties) */
	if (skip_until_empty_line(fp) < 0)
		goto err;

	/* Read in print_fmt: */
	if (fgets(buffer, len, fp) == NULL)
		goto err;

	newline = strchr(buffer, '\n');

	if (newline)
		*newline = '\0';

	fclose(fp);

	return 0;
err:
	fclose(fp);

	return -1;
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

static int clear(int *check)
{
	struct user_unreg unreg = {0};
	int fd;

	unreg.size = sizeof(unreg);
	unreg.disable_bit = 31;
	unreg.disable_addr = (__u64)check;

	fd = open(data_file, O_RDWR);

	if (fd == -1)
		return -1;

	if (ioctl(fd, DIAG_IOCSUNREG, &unreg) == -1)
		if (errno != ENOENT)
			goto fail;

	if (ioctl(fd, DIAG_IOCSDEL, "__test_event") == -1) {
		if (errno == EBUSY) {
			if (!wait_for_delete())
				goto fail;
		} else if (errno != ENOENT)
			goto fail;
	}

	close(fd);

	return 0;
fail:
	close(fd);

	return -1;
}

static int check_print_fmt(const char *event, const char *expected, int *check)
{
	struct user_reg reg = {0};
	char print_fmt[256];
	int ret;
	int fd;

	/* Ensure cleared */
	ret = clear(check);

	if (ret != 0)
		return ret;

	fd = open(data_file, O_RDWR);

	if (fd == -1)
		return fd;

	reg.size = sizeof(reg);
	reg.name_args = (__u64)event;
	reg.enable_bit = 31;
	reg.enable_addr = (__u64)check;
	reg.enable_size = sizeof(*check);

	/* Register should work */
	ret = ioctl(fd, DIAG_IOCSREG, &reg);

	if (ret != 0) {
		close(fd);
		printf("Reg failed in fmt\n");
		return ret;
	}

	/* Ensure correct print_fmt */
	ret = get_print_fmt(print_fmt, sizeof(print_fmt));

	close(fd);

	if (ret != 0)
		return ret;

	return strcmp(print_fmt, expected);
}

FIXTURE(user) {
	int status_fd;
	int data_fd;
	int enable_fd;
	int check;
};

FIXTURE_SETUP(user) {
	self->status_fd = open(status_file, O_RDONLY);
	ASSERT_NE(-1, self->status_fd);

	self->data_fd = open(data_file, O_RDWR);
	ASSERT_NE(-1, self->data_fd);

	self->enable_fd = -1;
}

FIXTURE_TEARDOWN(user) {
	close(self->status_fd);
	close(self->data_fd);

	if (self->enable_fd != -1) {
		write(self->enable_fd, "0", sizeof("0"));
		close(self->enable_fd);
	}

	if (clear(&self->check) != 0)
		printf("WARNING: Clear didn't work!\n");
}

TEST_F(user, register_events) {
	struct user_reg reg = {0};
	struct user_unreg unreg = {0};

	reg.size = sizeof(reg);
	reg.name_args = (__u64)"__test_event u32 field1; u32 field2";
	reg.enable_bit = 31;
	reg.enable_addr = (__u64)&self->check;
	reg.enable_size = sizeof(self->check);

	unreg.size = sizeof(unreg);
	unreg.disable_bit = 31;
	unreg.disable_addr = (__u64)&self->check;

	/* Register should work */
	ASSERT_EQ(0, ioctl(self->data_fd, DIAG_IOCSREG, &reg));
	ASSERT_EQ(0, reg.write_index);

	/* Multiple registers to the same addr + bit should fail */
	ASSERT_EQ(-1, ioctl(self->data_fd, DIAG_IOCSREG, &reg));
	ASSERT_EQ(EADDRINUSE, errno);

	/* Multiple registers to same name should result in same index */
	reg.enable_bit = 30;
	ASSERT_EQ(0, ioctl(self->data_fd, DIAG_IOCSREG, &reg));
	ASSERT_EQ(0, reg.write_index);

	/* Multiple registers to same name but different args should fail */
	reg.enable_bit = 29;
	reg.name_args = (__u64)"__test_event u32 field1;";
	ASSERT_EQ(-1, ioctl(self->data_fd, DIAG_IOCSREG, &reg));
	ASSERT_EQ(EADDRINUSE, errno);

	/* Ensure disabled */
	self->enable_fd = open(enable_file, O_RDWR);
	ASSERT_NE(-1, self->enable_fd);
	ASSERT_NE(-1, write(self->enable_fd, "0", sizeof("0")))

	/* Enable event and ensure bits updated in status */
	ASSERT_NE(-1, write(self->enable_fd, "1", sizeof("1")))
	ASSERT_EQ(1 << reg.enable_bit, self->check);

	/* Disable event and ensure bits updated in status */
	ASSERT_NE(-1, write(self->enable_fd, "0", sizeof("0")))
	ASSERT_EQ(0, self->check);

	/* File still open should return -EBUSY for delete */
	ASSERT_EQ(-1, ioctl(self->data_fd, DIAG_IOCSDEL, "__test_event"));
	ASSERT_EQ(EBUSY, errno);

	/* Unregister */
	ASSERT_EQ(0, ioctl(self->data_fd, DIAG_IOCSUNREG, &unreg));
	unreg.disable_bit = 30;
	ASSERT_EQ(0, ioctl(self->data_fd, DIAG_IOCSUNREG, &unreg));

	/* Delete should have been auto-done after close and unregister */
	close(self->data_fd);

	ASSERT_EQ(true, wait_for_delete());
}

TEST_F(user, write_events) {
	struct user_reg reg = {0};
	struct iovec io[3];
	__u32 field1, field2;
	int before = 0, after = 0;

	reg.size = sizeof(reg);
	reg.name_args = (__u64)"__test_event u32 field1; u32 field2";
	reg.enable_bit = 31;
	reg.enable_addr = (__u64)&self->check;
	reg.enable_size = sizeof(self->check);

	field1 = 1;
	field2 = 2;

	io[0].iov_base = &reg.write_index;
	io[0].iov_len = sizeof(reg.write_index);
	io[1].iov_base = &field1;
	io[1].iov_len = sizeof(field1);
	io[2].iov_base = &field2;
	io[2].iov_len = sizeof(field2);

	/* Register should work */
	ASSERT_EQ(0, ioctl(self->data_fd, DIAG_IOCSREG, &reg));
	ASSERT_EQ(0, reg.write_index);
	ASSERT_EQ(0, self->check);

	/* Write should fail on invalid slot with ENOENT */
	io[0].iov_base = &field2;
	io[0].iov_len = sizeof(field2);
	ASSERT_EQ(-1, writev(self->data_fd, (const struct iovec *)io, 3));
	ASSERT_EQ(ENOENT, errno);
	io[0].iov_base = &reg.write_index;
	io[0].iov_len = sizeof(reg.write_index);

	/* Enable event */
	self->enable_fd = open(enable_file, O_RDWR);
	ASSERT_NE(-1, write(self->enable_fd, "1", sizeof("1")))

	/* Event should now be enabled */
	ASSERT_NE(1 << reg.enable_bit, self->check);

	/* Write should make it out to ftrace buffers */
	before = trace_bytes();
	ASSERT_NE(-1, writev(self->data_fd, (const struct iovec *)io, 3));
	after = trace_bytes();
	ASSERT_GT(after, before);

	/* Negative index should fail with EINVAL */
	reg.write_index = -1;
	ASSERT_EQ(-1, writev(self->data_fd, (const struct iovec *)io, 3));
	ASSERT_EQ(EINVAL, errno);
}

TEST_F(user, write_empty_events) {
	struct user_reg reg = {0};
	struct iovec io[1];
	int before = 0, after = 0;

	reg.size = sizeof(reg);
	reg.name_args = (__u64)"__test_event";
	reg.enable_bit = 31;
	reg.enable_addr = (__u64)&self->check;
	reg.enable_size = sizeof(self->check);

	io[0].iov_base = &reg.write_index;
	io[0].iov_len = sizeof(reg.write_index);

	/* Register should work */
	ASSERT_EQ(0, ioctl(self->data_fd, DIAG_IOCSREG, &reg));
	ASSERT_EQ(0, reg.write_index);
	ASSERT_EQ(0, self->check);

	/* Enable event */
	self->enable_fd = open(enable_file, O_RDWR);
	ASSERT_NE(-1, write(self->enable_fd, "1", sizeof("1")))

	/* Event should now be enabled */
	ASSERT_EQ(1 << reg.enable_bit, self->check);

	/* Write should make it out to ftrace buffers */
	before = trace_bytes();
	ASSERT_NE(-1, writev(self->data_fd, (const struct iovec *)io, 1));
	after = trace_bytes();
	ASSERT_GT(after, before);
}

TEST_F(user, write_fault) {
	struct user_reg reg = {0};
	struct iovec io[2];
	int l = sizeof(__u64);
	void *anon;

	reg.size = sizeof(reg);
	reg.name_args = (__u64)"__test_event u64 anon";
	reg.enable_bit = 31;
	reg.enable_addr = (__u64)&self->check;
	reg.enable_size = sizeof(self->check);

	anon = mmap(NULL, l, PROT_READ, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	ASSERT_NE(MAP_FAILED, anon);

	io[0].iov_base = &reg.write_index;
	io[0].iov_len = sizeof(reg.write_index);
	io[1].iov_base = anon;
	io[1].iov_len = l;

	/* Register should work */
	ASSERT_EQ(0, ioctl(self->data_fd, DIAG_IOCSREG, &reg));
	ASSERT_EQ(0, reg.write_index);

	/* Write should work normally */
	ASSERT_NE(-1, writev(self->data_fd, (const struct iovec *)io, 2));

	/* Faulted data should zero fill and work */
	ASSERT_EQ(0, madvise(anon, l, MADV_DONTNEED));
	ASSERT_NE(-1, writev(self->data_fd, (const struct iovec *)io, 2));
	ASSERT_EQ(0, munmap(anon, l));
}

TEST_F(user, write_validator) {
	struct user_reg reg = {0};
	struct iovec io[3];
	int loc, bytes;
	char data[8];
	int before = 0, after = 0;

	reg.size = sizeof(reg);
	reg.name_args = (__u64)"__test_event __rel_loc char[] data";
	reg.enable_bit = 31;
	reg.enable_addr = (__u64)&self->check;
	reg.enable_size = sizeof(self->check);

	/* Register should work */
	ASSERT_EQ(0, ioctl(self->data_fd, DIAG_IOCSREG, &reg));
	ASSERT_EQ(0, reg.write_index);
	ASSERT_EQ(0, self->check);

	io[0].iov_base = &reg.write_index;
	io[0].iov_len = sizeof(reg.write_index);
	io[1].iov_base = &loc;
	io[1].iov_len = sizeof(loc);
	io[2].iov_base = data;
	bytes = snprintf(data, sizeof(data), "Test") + 1;
	io[2].iov_len = bytes;

	/* Undersized write should fail */
	ASSERT_EQ(-1, writev(self->data_fd, (const struct iovec *)io, 1));
	ASSERT_EQ(EINVAL, errno);

	/* Enable event */
	self->enable_fd = open(enable_file, O_RDWR);
	ASSERT_NE(-1, write(self->enable_fd, "1", sizeof("1")))

	/* Event should now be enabled */
	ASSERT_EQ(1 << reg.enable_bit, self->check);

	/* Full in-bounds write should work */
	before = trace_bytes();
	loc = DYN_LOC(0, bytes);
	ASSERT_NE(-1, writev(self->data_fd, (const struct iovec *)io, 3));
	after = trace_bytes();
	ASSERT_GT(after, before);

	/* Out of bounds write should fault (offset way out) */
	loc = DYN_LOC(1024, bytes);
	ASSERT_EQ(-1, writev(self->data_fd, (const struct iovec *)io, 3));
	ASSERT_EQ(EFAULT, errno);

	/* Out of bounds write should fault (offset 1 byte out) */
	loc = DYN_LOC(1, bytes);
	ASSERT_EQ(-1, writev(self->data_fd, (const struct iovec *)io, 3));
	ASSERT_EQ(EFAULT, errno);

	/* Out of bounds write should fault (size way out) */
	loc = DYN_LOC(0, bytes + 1024);
	ASSERT_EQ(-1, writev(self->data_fd, (const struct iovec *)io, 3));
	ASSERT_EQ(EFAULT, errno);

	/* Out of bounds write should fault (size 1 byte out) */
	loc = DYN_LOC(0, bytes + 1);
	ASSERT_EQ(-1, writev(self->data_fd, (const struct iovec *)io, 3));
	ASSERT_EQ(EFAULT, errno);

	/* Non-Null should fault */
	memset(data, 'A', sizeof(data));
	loc = DYN_LOC(0, bytes);
	ASSERT_EQ(-1, writev(self->data_fd, (const struct iovec *)io, 3));
	ASSERT_EQ(EFAULT, errno);
}

TEST_F(user, print_fmt) {
	int ret;

	ret = check_print_fmt("__test_event __rel_loc char[] data",
			      "print fmt: \"data=%s\", __get_rel_str(data)",
			      &self->check);
	ASSERT_EQ(0, ret);

	ret = check_print_fmt("__test_event __data_loc char[] data",
			      "print fmt: \"data=%s\", __get_str(data)",
			      &self->check);
	ASSERT_EQ(0, ret);

	ret = check_print_fmt("__test_event s64 data",
			      "print fmt: \"data=%lld\", REC->data",
			      &self->check);
	ASSERT_EQ(0, ret);

	ret = check_print_fmt("__test_event u64 data",
			      "print fmt: \"data=%llu\", REC->data",
			      &self->check);
	ASSERT_EQ(0, ret);

	ret = check_print_fmt("__test_event s32 data",
			      "print fmt: \"data=%d\", REC->data",
			      &self->check);
	ASSERT_EQ(0, ret);

	ret = check_print_fmt("__test_event u32 data",
			      "print fmt: \"data=%u\", REC->data",
			      &self->check);
	ASSERT_EQ(0, ret);

	ret = check_print_fmt("__test_event int data",
			      "print fmt: \"data=%d\", REC->data",
			      &self->check);
	ASSERT_EQ(0, ret);

	ret = check_print_fmt("__test_event unsigned int data",
			      "print fmt: \"data=%u\", REC->data",
			      &self->check);
	ASSERT_EQ(0, ret);

	ret = check_print_fmt("__test_event s16 data",
			      "print fmt: \"data=%d\", REC->data",
			      &self->check);
	ASSERT_EQ(0, ret);

	ret = check_print_fmt("__test_event u16 data",
			      "print fmt: \"data=%u\", REC->data",
			      &self->check);
	ASSERT_EQ(0, ret);

	ret = check_print_fmt("__test_event short data",
			      "print fmt: \"data=%d\", REC->data",
			      &self->check);
	ASSERT_EQ(0, ret);

	ret = check_print_fmt("__test_event unsigned short data",
			      "print fmt: \"data=%u\", REC->data",
			      &self->check);
	ASSERT_EQ(0, ret);

	ret = check_print_fmt("__test_event s8 data",
			      "print fmt: \"data=%d\", REC->data",
			      &self->check);
	ASSERT_EQ(0, ret);

	ret = check_print_fmt("__test_event u8 data",
			      "print fmt: \"data=%u\", REC->data",
			      &self->check);
	ASSERT_EQ(0, ret);

	ret = check_print_fmt("__test_event char data",
			      "print fmt: \"data=%d\", REC->data",
			      &self->check);
	ASSERT_EQ(0, ret);

	ret = check_print_fmt("__test_event unsigned char data",
			      "print fmt: \"data=%u\", REC->data",
			      &self->check);
	ASSERT_EQ(0, ret);

	ret = check_print_fmt("__test_event char[4] data",
			      "print fmt: \"data=%s\", REC->data",
			      &self->check);
	ASSERT_EQ(0, ret);
}

int main(int argc, char **argv)
{
	return test_harness_run(argc, argv);
}
