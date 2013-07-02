/*
 * Originally done by Vince Weaver <vincent.weaver@maine.edu> for
 * perf_event_tests (git://github.com/deater/perf_event_tests)
 */

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

#define EXECUTIONS 10000
#define THRESHOLD  100

int test__bp_signal_overflow(void)
{
	struct perf_event_attr pe;
	struct sigaction sa;
	long long count;
	int fd, i, fails = 0;

	/* setup SIGIO signal handler */
	memset(&sa, 0, sizeof(struct sigaction));
	sa.sa_sigaction = (void *) sig_handler;
	sa.sa_flags = SA_SIGINFO;

	if (sigaction(SIGIO, &sa, NULL) < 0) {
		pr_debug("failed setting up signal handler\n");
		return TEST_FAIL;
	}

	memset(&pe, 0, sizeof(struct perf_event_attr));
	pe.type = PERF_TYPE_BREAKPOINT;
	pe.size = sizeof(struct perf_event_attr);

	pe.config = 0;
	pe.bp_type = HW_BREAKPOINT_X;
	pe.bp_addr = (unsigned long) test_function;
	pe.bp_len = sizeof(long);

	pe.sample_period = THRESHOLD;
	pe.sample_type = PERF_SAMPLE_IP;
	pe.wakeup_events = 1;

	pe.disabled = 1;
	pe.exclude_kernel = 1;
	pe.exclude_hv = 1;

	fd = sys_perf_event_open(&pe, 0, -1, -1, 0);
	if (fd < 0) {
		pr_debug("failed opening event %llx\n", pe.config);
		return TEST_FAIL;
	}

	fcntl(fd, F_SETFL, O_RDWR|O_NONBLOCK|O_ASYNC);
	fcntl(fd, F_SETSIG, SIGIO);
	fcntl(fd, F_SETOWN, getpid());

	ioctl(fd, PERF_EVENT_IOC_RESET, 0);
	ioctl(fd, PERF_EVENT_IOC_ENABLE, 0);

	for (i = 0; i < EXECUTIONS; i++)
		test_function();

	ioctl(fd, PERF_EVENT_IOC_DISABLE, 0);

	count = bp_count(fd);

	close(fd);

	pr_debug("count %lld, overflow %d\n",
		 count, overflows);

	if (count != EXECUTIONS) {
		pr_debug("\tWrong number of executions %lld != %d\n",
		count, EXECUTIONS);
		fails++;
	}

	if (overflows != EXECUTIONS / THRESHOLD) {
		pr_debug("\tWrong number of overflows %d != %d\n",
		overflows, EXECUTIONS / THRESHOLD);
		fails++;
	}

	return fails ? TEST_FAIL : TEST_OK;
}
