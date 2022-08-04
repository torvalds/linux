// SPDX-License-Identifier: GPL-2.0
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/ioctl.h>
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

static int wp_ro_test(void)
{
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
}

static int wp_wo_test(void)
{
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
}

static int wp_rw_test(void)
{
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
}

static int wp_modify_test(void)
{
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
}

static bool wp_ro_supported(void)
{
#if defined (__x86_64__) || defined (__i386__)
	return false;
#else
	return true;
#endif
}

static const char *wp_ro_skip_msg(void)
{
#if defined (__x86_64__) || defined (__i386__)
	return "missing hardware support";
#else
	return NULL;
#endif
}

static struct {
	const char *desc;
	int (*target_func)(void);
	bool (*is_supported)(void);
	const char *(*skip_msg)(void);
} wp_testcase_table[] = {
	{
		.desc = "Read Only Watchpoint",
		.target_func = &wp_ro_test,
		.is_supported = &wp_ro_supported,
		.skip_msg = &wp_ro_skip_msg,
	},
	{
		.desc = "Write Only Watchpoint",
		.target_func = &wp_wo_test,
	},
	{
		.desc = "Read / Write Watchpoint",
		.target_func = &wp_rw_test,
	},
	{
		.desc = "Modify Watchpoint",
		.target_func = &wp_modify_test,
	},
};

int test__wp_subtest_get_nr(void)
{
	return (int)ARRAY_SIZE(wp_testcase_table);
}

const char *test__wp_subtest_get_desc(int i)
{
	if (i < 0 || i >= (int)ARRAY_SIZE(wp_testcase_table))
		return NULL;
	return wp_testcase_table[i].desc;
}

const char *test__wp_subtest_skip_reason(int i)
{
	if (i < 0 || i >= (int)ARRAY_SIZE(wp_testcase_table))
		return NULL;
	if (!wp_testcase_table[i].skip_msg)
		return NULL;
	return wp_testcase_table[i].skip_msg();
}

int test__wp(struct test *test __maybe_unused, int i)
{
	if (i < 0 || i >= (int)ARRAY_SIZE(wp_testcase_table))
		return TEST_FAIL;

	if (wp_testcase_table[i].is_supported &&
	    !wp_testcase_table[i].is_supported())
		return TEST_SKIP;

	return !wp_testcase_table[i].target_func() ? TEST_OK : TEST_FAIL;
}

/* The s390 so far does not have support for
 * instruction breakpoint using the perf_event_open() system call.
 */
bool test__wp_is_supported(void)
{
#if defined(__s390x__)
	return false;
#else
	return true;
#endif
}
