// SPDX-License-Identifier: GPL-2.0
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <linux/compiler.h>
#include <linux/hw_breakpoint.h>
#include <linux/kernel.h>
#include "tests.h"
#include "debug.h"
#include "event.h"
#include "cloexec.h"
#include "../perf-sys.h"

#define WP_TEST_ASSERT_VAL(fd, text, val)       \
do {                                            \
	long long count;                        \
	wp_read(fd, &count, sizeof(long long)); \
	TEST_ASSERT_VAL(text, count == val);    \
} while (0)

volatile u64 data1;
volatile u8 data2[3];

#ifndef __s390x__
static int wp_read(int fd, long long *count, int size)
{
	int ret = read(fd, count, size);

	if (ret != size) {
		pr_debug("failed to read: %d\n", ret);
		return -1;
	}
	return 0;
}

static void get__perf_event_attr(struct perf_event_attr *attr, int wp_type,
				 void *wp_addr, unsigned long wp_len)
{
	memset(attr, 0, sizeof(struct perf_event_attr));
	attr->type           = PERF_TYPE_BREAKPOINT;
	attr->size           = sizeof(struct perf_event_attr);
	attr->config         = 0;
	attr->bp_type        = wp_type;
	attr->bp_addr        = (unsigned long)wp_addr;
	attr->bp_len         = wp_len;
	attr->sample_period  = 1;
	attr->sample_type    = PERF_SAMPLE_IP;
	attr->exclude_kernel = 1;
	attr->exclude_hv     = 1;
}

static int __event(int wp_type, void *wp_addr, unsigned long wp_len)
{
	int fd;
	struct perf_event_attr attr;

	get__perf_event_attr(&attr, wp_type, wp_addr, wp_len);
	fd = sys_perf_event_open(&attr, 0, -1, -1,
				 perf_event_open_cloexec_flag());
	if (fd < 0)
		pr_debug("failed opening event %x\n", attr.bp_type);

	return fd;
}
#endif

static int test__wp_ro(struct test_suite *test __maybe_unused,
		       int subtest __maybe_unused)
{
#if defined(__s390x__) || defined(__x86_64__) || defined(__i386__)
	return TEST_SKIP;
#else
	int fd;
	unsigned long tmp, tmp1 = rand();

	fd = __event(HW_BREAKPOINT_R, (void *)&data1, sizeof(data1));
	if (fd < 0)
		return -1;

	tmp = data1;
	WP_TEST_ASSERT_VAL(fd, "RO watchpoint", 1);

	data1 = tmp1 + tmp;
	WP_TEST_ASSERT_VAL(fd, "RO watchpoint", 1);

	close(fd);
	return 0;
#endif
}

static int test__wp_wo(struct test_suite *test __maybe_unused,
		       int subtest __maybe_unused)
{
#if defined(__s390x__)
	return TEST_SKIP;
#else
	int fd;
	unsigned long tmp, tmp1 = rand();

	fd = __event(HW_BREAKPOINT_W, (void *)&data1, sizeof(data1));
	if (fd < 0)
		return -1;

	tmp = data1;
	WP_TEST_ASSERT_VAL(fd, "WO watchpoint", 0);

	data1 = tmp1 + tmp;
	WP_TEST_ASSERT_VAL(fd, "WO watchpoint", 1);

	close(fd);
	return 0;
#endif
}

static int test__wp_rw(struct test_suite *test __maybe_unused,
		       int subtest __maybe_unused)
{
#if defined(__s390x__)
	return TEST_SKIP;
#else
	int fd;
	unsigned long tmp, tmp1 = rand();

	fd = __event(HW_BREAKPOINT_R | HW_BREAKPOINT_W, (void *)&data1,
		     sizeof(data1));
	if (fd < 0)
		return -1;

	tmp = data1;
	WP_TEST_ASSERT_VAL(fd, "RW watchpoint", 1);

	data1 = tmp1 + tmp;
	WP_TEST_ASSERT_VAL(fd, "RW watchpoint", 2);

	close(fd);
	return 0;
#endif
}

static int test__wp_modify(struct test_suite *test __maybe_unused, int subtest __maybe_unused)
{
#if defined(__s390x__)
	return TEST_SKIP;
#else
	int fd, ret;
	unsigned long tmp = rand();
	struct perf_event_attr new_attr;

	fd = __event(HW_BREAKPOINT_W, (void *)&data1, sizeof(data1));
	if (fd < 0)
		return -1;

	data1 = tmp;
	WP_TEST_ASSERT_VAL(fd, "Modify watchpoint", 1);

	/* Modify watchpoint with disabled = 1 */
	get__perf_event_attr(&new_attr, HW_BREAKPOINT_W, (void *)&data2[0],
			     sizeof(u8) * 2);
	new_attr.disabled = 1;
	ret = ioctl(fd, PERF_EVENT_IOC_MODIFY_ATTRIBUTES, &new_attr);
	if (ret < 0) {
		if (errno == ENOTTY) {
			test->test_cases[subtest].skip_reason = "missing kernel support";
			ret = TEST_SKIP;
		}

		pr_debug("ioctl(PERF_EVENT_IOC_MODIFY_ATTRIBUTES) failed\n");
		close(fd);
		return ret;
	}

	data2[1] = tmp; /* Not Counted */
	WP_TEST_ASSERT_VAL(fd, "Modify watchpoint", 1);

	/* Enable the event */
	ioctl(fd, PERF_EVENT_IOC_ENABLE, 0);
	if (ret < 0) {
		pr_debug("Failed to enable event\n");
		close(fd);
		return ret;
	}

	data2[1] = tmp; /* Counted */
	WP_TEST_ASSERT_VAL(fd, "Modify watchpoint", 2);

	data2[2] = tmp; /* Not Counted */
	WP_TEST_ASSERT_VAL(fd, "Modify watchpoint", 2);

	close(fd);
	return 0;
#endif
}

static struct test_case wp_tests[] = {
	TEST_CASE_REASON("Read Only Watchpoint", wp_ro, "missing hardware support"),
	TEST_CASE_REASON("Write Only Watchpoint", wp_wo, "missing hardware support"),
	TEST_CASE_REASON("Read / Write Watchpoint", wp_rw, "missing hardware support"),
	TEST_CASE_REASON("Modify Watchpoint", wp_modify, "missing hardware support"),
	{ .name = NULL, }
};

struct test_suite suite__wp = {
	.desc = "Watchpoint",
	.test_cases = wp_tests,
};
