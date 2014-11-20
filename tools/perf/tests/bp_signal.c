/*
 * Inspired by breakpoint overflow test done by
 * Vince Weaver <vincent.weaver@maine.edu> for perf_event_tests
 * (git://github.com/deater/perf_event_tests)
 */

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
#include <time.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/mman.h>
#include <linux/compiler.h>
#include <linux/hw_breakpoint.h>

#include "tests.h"
#include "debug.h"
#include "perf.h"
#include "cloexec.h"

static int fd1;
static int fd2;
static int overflows;

__attribute__ ((noinline))
static int test_function(void)
{
	return time(NULL);
}

static void sig_handler(int signum __maybe_unused,
			siginfo_t *oh __maybe_unused,
			void *uc __maybe_unused)
{
	overflows++;

	if (overflows > 10) {
		/*
		 * This should be executed only once during
		 * this test, if we are here for the 10th
		 * time, consider this the recursive issue.
		 *
		 * We can get out of here by disable events,
		 * so no new SIGIO is delivered.
		 */
		ioctl(fd1, PERF_EVENT_IOC_DISABLE, 0);
		ioctl(fd2, PERF_EVENT_IOC_DISABLE, 0);
	}
}

static int bp_event(void *fn, int setup_signal)
{
	struct perf_event_attr pe;
	int fd;

	memset(&pe, 0, sizeof(struct perf_event_attr));
	pe.type = PERF_TYPE_BREAKPOINT;
	pe.size = sizeof(struct perf_event_attr);

	pe.config = 0;
	pe.bp_type = HW_BREAKPOINT_X;
	pe.bp_addr = (unsigned long) fn;
	pe.bp_len = sizeof(long);

	pe.sample_period = 1;
	pe.sample_type = PERF_SAMPLE_IP;
	pe.wakeup_events = 1;

	pe.disabled = 1;
	pe.exclude_kernel = 1;
	pe.exclude_hv = 1;

	fd = sys_perf_event_open(&pe, 0, -1, -1,
				 perf_event_open_cloexec_flag());
	if (fd < 0) {
		pr_debug("failed opening event %llx\n", pe.config);
		return TEST_FAIL;
	}

	if (setup_signal) {
		fcntl(fd, F_SETFL, O_RDWR|O_NONBLOCK|O_ASYNC);
		fcntl(fd, F_SETSIG, SIGIO);
		fcntl(fd, F_SETOWN, getpid());
	}

	ioctl(fd, PERF_EVENT_IOC_RESET, 0);

	return fd;
}

static long long bp_count(int fd)
{
	long long count;
	int ret;

	ret = read(fd, &count, sizeof(long long));
	if (ret != sizeof(long long)) {
		pr_debug("failed to read: %d\n", ret);
		return TEST_FAIL;
	}

	return count;
}

int test__bp_signal(void)
{
	struct sigaction sa;
	long long count1, count2;

	/* setup SIGIO signal handler */
	memset(&sa, 0, sizeof(struct sigaction));
	sa.sa_sigaction = (void *) sig_handler;
	sa.sa_flags = SA_SIGINFO;

	if (sigaction(SIGIO, &sa, NULL) < 0) {
		pr_debug("failed setting up signal handler\n");
		return TEST_FAIL;
	}

	/*
	 * We create following events:
	 *
	 * fd1 - breakpoint event on test_function with SIGIO
	 *       signal configured. We should get signal
	 *       notification each time the breakpoint is hit
	 *
	 * fd2 - breakpoint event on sig_handler without SIGIO
	 *       configured.
	 *
	 * Following processing should happen:
	 *   - execute test_function
	 *   - fd1 event breakpoint hit -> count1 == 1
	 *   - SIGIO is delivered       -> overflows == 1
	 *   - fd2 event breakpoint hit -> count2 == 1
	 *
	 * The test case check following error conditions:
	 * - we get stuck in signal handler because of debug
	 *   exception being triggered receursively due to
	 *   the wrong RF EFLAG management
	 *
	 * - we never trigger the sig_handler breakpoint due
	 *   to the rong RF EFLAG management
	 *
	 */

	fd1 = bp_event(test_function, 1);
	fd2 = bp_event(sig_handler, 0);

	ioctl(fd1, PERF_EVENT_IOC_ENABLE, 0);
	ioctl(fd2, PERF_EVENT_IOC_ENABLE, 0);

	/*
	 * Kick off the test by trigering 'fd1'
	 * breakpoint.
	 */
	test_function();

	ioctl(fd1, PERF_EVENT_IOC_DISABLE, 0);
	ioctl(fd2, PERF_EVENT_IOC_DISABLE, 0);

	count1 = bp_count(fd1);
	count2 = bp_count(fd2);

	close(fd1);
	close(fd2);

	pr_debug("count1 %lld, count2 %lld, overflow %d\n",
		 count1, count2, overflows);

	if (count1 != 1) {
		if (count1 == 11)
			pr_debug("failed: RF EFLAG recursion issue detected\n");
		else
			pr_debug("failed: wrong count for bp1%lld\n", count1);
	}

	if (overflows != 1)
		pr_debug("failed: wrong overflow hit\n");

	if (count2 != 1)
		pr_debug("failed: wrong count for bp2\n");

	return count1 == 1 && overflows == 1 && count2 == 1 ?
		TEST_OK : TEST_FAIL;
}
