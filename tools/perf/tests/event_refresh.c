// SPDX-License-Identifier: GPL-2.0
/*
 * Re-arm a sampling event from its overflow signal handler with
 * PERF_EVENT_IOC_REFRESH and check that every overflow is delivered.
 */
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <signal.h>
#include <string.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <linux/perf_event.h>

#include "cloexec.h"
#include "debug.h"
#include "tests.h"
#include "tp_pmu.h"
#include <internal/lib.h> // page_size
#include "../perf-sys.h"

#define NR_EVENTS 1000
/* A real-time signal is queued, so no overflow can be coalesced away. */
#define REFRESH_SIGNAL (SIGRTMIN + 1)

static volatile sig_atomic_t overflows, hups, refresh_errno;

static void sig_handler(int signum __maybe_unused, siginfo_t *info,
			void *uc __maybe_unused)
{
	int saved_errno = errno;

	overflows++;
	if (info->si_code == POLL_HUP)
		hups++;
	if (ioctl(info->si_fd, PERF_EVENT_IOC_REFRESH, 1) < 0)
		refresh_errno = errno;
	errno = saved_errno;
}

/* Count overflow signals for NR_EVENTS calls of trigger(), period 1. */
static int count_overflows(struct perf_event_attr *attr, void (*trigger)(int i))
{
	struct sigaction sa, old_sa;
	uint64_t count = 0;
	int fd, i, err = TEST_FAIL;

	attr->size = sizeof(*attr);
	attr->sample_period = 1;
	attr->disabled = 1;

	fd = sys_perf_event_open(attr, 0, -1, -1, perf_event_open_cloexec_flag());
	if (fd < 0) {
		int open_errno = errno;

		pr_debug("failed opening event: %s\n", strerror(open_errno));
		return open_errno == EACCES || open_errno == EPERM ? TEST_SKIP : TEST_FAIL;
	}

	memset(&sa, 0, sizeof(sa));
	sa.sa_sigaction = sig_handler;
	sa.sa_flags = SA_SIGINFO;
	if (sigaction(REFRESH_SIGNAL, &sa, &old_sa) < 0) {
		pr_debug("failed setting up signal handler\n");
		close(fd);
		return TEST_FAIL;
	}

	if (fcntl(fd, F_SETFL, O_RDWR | O_NONBLOCK | O_ASYNC) < 0 ||
	    fcntl(fd, F_SETSIG, REFRESH_SIGNAL) < 0 ||
	    fcntl(fd, F_SETOWN, getpid()) < 0) {
		pr_debug("failed setting up signal delivery: %s\n", strerror(errno));
		goto out;
	}

	overflows = 0;
	hups = 0;
	refresh_errno = 0;
	if (ioctl(fd, PERF_EVENT_IOC_REFRESH, 1) < 0) {
		pr_debug("PERF_EVENT_IOC_REFRESH failed: %s\n", strerror(errno));
		goto out;
	}
	for (i = 0; i < NR_EVENTS; i++)
		trigger(i);
	ioctl(fd, PERF_EVENT_IOC_DISABLE, 0);

	if (read(fd, &count, sizeof(count)) != sizeof(count))
		pr_debug("failed to read the event count\n");
	pr_debug("%d events, count %" PRIu64 ", %d overflows (%d POLL_HUP), re-arm errno %d\n",
		 NR_EVENTS, count, overflows, hups, refresh_errno);
	/* Unrelated page faults can only add overflows, never hide one. */
	if (overflows >= NR_EVENTS)
		err = TEST_OK;
out:
	/* Close first: a late signal must still find our handler. */
	close(fd);
	sigaction(REFRESH_SIGNAL, &old_sa, NULL);
	return err;
}

static char *pages;

static void touch_page(int i)
{
	pages[(long)i * page_size] = 1;
}

static int test__event_refresh_sw(struct test_suite *test __maybe_unused,
				  int subtest __maybe_unused)
{
	struct perf_event_attr attr = {
		.type = PERF_TYPE_SOFTWARE,
		.config = PERF_COUNT_SW_PAGE_FAULTS,
	};
	int err;

	pages = mmap(NULL, (long)NR_EVENTS * page_size, PROT_READ | PROT_WRITE,
		     MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if (pages == MAP_FAILED)
		return TEST_FAIL;
	/* One fault per touched page: a huge page would cover many of them. */
	madvise(pages, (long)NR_EVENTS * page_size, MADV_NOHUGEPAGE);

	err = count_overflows(&attr, touch_page);
	munmap(pages, (long)NR_EVENTS * page_size);
	return err;
}

static void call_getppid(int i __maybe_unused)
{
	syscall(SYS_getppid);
}

static int test__event_refresh_tp(struct test_suite *test __maybe_unused,
				  int subtest __maybe_unused)
{
	struct perf_event_attr attr = {
		.type = PERF_TYPE_TRACEPOINT,
	};
	int id = tp_pmu__id("syscalls", "sys_enter_getppid");

	if (id < 0) {
		pr_debug("no syscalls:sys_enter_getppid tracepoint\n");
		return TEST_SKIP;
	}
	attr.config = id;

	return count_overflows(&attr, call_getppid);
}

static struct test_case tests__event_refresh[] = {
	TEST_CASE_REASON("Software event", event_refresh_sw, "permissions"),
	TEST_CASE_REASON("Tracepoint event", event_refresh_tp, "permissions"),
	{	.name = NULL, }
};

struct test_suite suite__event_refresh = {
	.desc = "Event re-armed with PERF_EVENT_IOC_REFRESH",
	.test_cases = tests__event_refresh,
};
