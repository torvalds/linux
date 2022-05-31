// SPDX-License-Identifier: GPL-2.0
/*
 * Powerpc needs __SANE_USERSPACE_TYPES__ before <linux/types.h> to select
 * 'int-ll64.h' and avoid compile warnings when printing __u64 with %llu.
 */
#define __SANE_USERSPACE_TYPES__

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <linux/hw_breakpoint.h>

#include "tests.h"
#include "debug.h"
#include "event.h"
#include "../perf-sys.h"
#include "cloexec.h"

/*
 * PowerPC and S390 do not support creation of instruction breakpoints using the
 * perf_event interface.
 *
 * Just disable the test for these architectures until these issues are
 * resolved.
 */
#if defined(__powerpc__) || defined(__s390x__)
#define BP_ACCOUNT_IS_SUPPORTED 0
#else
#define BP_ACCOUNT_IS_SUPPORTED 1
#endif

static volatile long the_var;

static noinline int test_function(void)
{
	return 0;
}

static int __event(bool is_x, void *addr, struct perf_event_attr *attr)
{
	int fd;

	memset(attr, 0, sizeof(struct perf_event_attr));
	attr->type = PERF_TYPE_BREAKPOINT;
	attr->size = sizeof(struct perf_event_attr);

	attr->config = 0;
	attr->bp_type = is_x ? HW_BREAKPOINT_X : HW_BREAKPOINT_W;
	attr->bp_addr = (unsigned long) addr;
	attr->bp_len = sizeof(long);

	attr->sample_period = 1;
	attr->sample_type = PERF_SAMPLE_IP;

	attr->exclude_kernel = 1;
	attr->exclude_hv = 1;

	fd = sys_perf_event_open(attr, -1, 0, -1,
				 perf_event_open_cloexec_flag());
	if (fd < 0) {
		pr_debug("failed opening event %llx\n", attr->config);
		return TEST_FAIL;
	}

	return fd;
}

static int wp_event(void *addr, struct perf_event_attr *attr)
{
	return __event(false, addr, attr);
}

static int bp_event(void *addr, struct perf_event_attr *attr)
{
	return __event(true, addr, attr);
}

static int bp_accounting(int wp_cnt, int share)
{
	struct perf_event_attr attr, attr_mod, attr_new;
	int i, fd[wp_cnt], fd_wp, ret;

	for (i = 0; i < wp_cnt; i++) {
		fd[i] = wp_event((void *)&the_var, &attr);
		TEST_ASSERT_VAL("failed to create wp\n", fd[i] != -1);
		pr_debug("wp %d created\n", i);
	}

	attr_mod = attr;
	attr_mod.bp_type = HW_BREAKPOINT_X;
	attr_mod.bp_addr = (unsigned long) test_function;

	ret = ioctl(fd[0], PERF_EVENT_IOC_MODIFY_ATTRIBUTES, &attr_mod);
	TEST_ASSERT_VAL("failed to modify wp\n", ret == 0);

	pr_debug("wp 0 modified to bp\n");

	if (!share) {
		fd_wp = wp_event((void *)&the_var, &attr_new);
		TEST_ASSERT_VAL("failed to create max wp\n", fd_wp != -1);
		pr_debug("wp max created\n");
	}

	for (i = 0; i < wp_cnt; i++)
		close(fd[i]);

	return 0;
}

static int detect_cnt(bool is_x)
{
	struct perf_event_attr attr;
	void *addr = is_x ? (void *)test_function : (void *)&the_var;
	int fd[100], cnt = 0, i;

	while (1) {
		if (cnt == 100) {
			pr_debug("way too many debug registers, fix the test\n");
			return 0;
		}
		fd[cnt] = __event(is_x, addr, &attr);

		if (fd[cnt] < 0)
			break;
		cnt++;
	}

	for (i = 0; i < cnt; i++)
		close(fd[i]);

	return cnt;
}

static int detect_ioctl(void)
{
	struct perf_event_attr attr;
	int fd, ret = 1;

	fd = wp_event((void *) &the_var, &attr);
	if (fd > 0) {
		ret = ioctl(fd, PERF_EVENT_IOC_MODIFY_ATTRIBUTES, &attr);
		close(fd);
	}

	return ret ? 0 : 1;
}

static int detect_share(int wp_cnt, int bp_cnt)
{
	struct perf_event_attr attr;
	int i, fd[wp_cnt + bp_cnt], ret;

	for (i = 0; i < wp_cnt; i++) {
		fd[i] = wp_event((void *)&the_var, &attr);
		TEST_ASSERT_VAL("failed to create wp\n", fd[i] != -1);
	}

	for (; i < (bp_cnt + wp_cnt); i++) {
		fd[i] = bp_event((void *)test_function, &attr);
		if (fd[i] == -1)
			break;
	}

	ret = i != (bp_cnt + wp_cnt);

	while (i--)
		close(fd[i]);

	return ret;
}

/*
 * This test does following:
 *   - detects the number of watch/break-points,
 *     skip test if any is missing
 *   - detects PERF_EVENT_IOC_MODIFY_ATTRIBUTES ioctl,
 *     skip test if it's missing
 *   - detects if watchpoints and breakpoints share
 *     same slots
 *   - create all possible watchpoints on cpu 0
 *   - change one of it to breakpoint
 *   - in case wp and bp do not share slots,
 *     we create another watchpoint to ensure
 *     the slot accounting is correct
 */
static int test__bp_accounting(struct test_suite *test __maybe_unused, int subtest __maybe_unused)
{
	int has_ioctl = detect_ioctl();
	int wp_cnt = detect_cnt(false);
	int bp_cnt = detect_cnt(true);
	int share  = detect_share(wp_cnt, bp_cnt);

	if (!BP_ACCOUNT_IS_SUPPORTED) {
		pr_debug("Test not supported on this architecture");
		return TEST_SKIP;
	}

	pr_debug("watchpoints count %d, breakpoints count %d, has_ioctl %d, share %d\n",
		 wp_cnt, bp_cnt, has_ioctl, share);

	if (!wp_cnt || !bp_cnt || !has_ioctl)
		return TEST_SKIP;

	return bp_accounting(wp_cnt, share);
}

DEFINE_SUITE("Breakpoint accounting", bp_accounting);
