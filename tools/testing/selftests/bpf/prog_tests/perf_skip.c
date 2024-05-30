// SPDX-License-Identifier: GPL-2.0
#define _GNU_SOURCE

#include <test_progs.h>
#include "test_perf_skip.skel.h"
#include <linux/compiler.h>
#include <linux/hw_breakpoint.h>
#include <sys/mman.h>

#ifndef TRAP_PERF
#define TRAP_PERF 6
#endif

int sigio_count, sigtrap_count;

static void handle_sigio(int sig __always_unused)
{
	++sigio_count;
}

static void handle_sigtrap(int signum __always_unused,
			   siginfo_t *info,
			   void *ucontext __always_unused)
{
	ASSERT_EQ(info->si_code, TRAP_PERF, "si_code");
	++sigtrap_count;
}

static noinline int test_function(void)
{
	asm volatile ("");
	return 0;
}

void serial_test_perf_skip(void)
{
	struct sigaction action = {};
	struct sigaction previous_sigtrap;
	sighandler_t previous_sigio = SIG_ERR;
	struct test_perf_skip *skel = NULL;
	struct perf_event_attr attr = {};
	int perf_fd = -1;
	int err;
	struct f_owner_ex owner;
	struct bpf_link *prog_link = NULL;

	action.sa_flags = SA_SIGINFO | SA_NODEFER;
	action.sa_sigaction = handle_sigtrap;
	sigemptyset(&action.sa_mask);
	if (!ASSERT_OK(sigaction(SIGTRAP, &action, &previous_sigtrap), "sigaction"))
		return;

	previous_sigio = signal(SIGIO, handle_sigio);
	if (!ASSERT_NEQ(previous_sigio, SIG_ERR, "signal"))
		goto cleanup;

	skel = test_perf_skip__open_and_load();
	if (!ASSERT_OK_PTR(skel, "skel_load"))
		goto cleanup;

	attr.type = PERF_TYPE_BREAKPOINT;
	attr.size = sizeof(attr);
	attr.bp_type = HW_BREAKPOINT_X;
	attr.bp_addr = (uintptr_t)test_function;
	attr.bp_len = sizeof(long);
	attr.sample_period = 1;
	attr.sample_type = PERF_SAMPLE_IP;
	attr.pinned = 1;
	attr.exclude_kernel = 1;
	attr.exclude_hv = 1;
	attr.precise_ip = 3;
	attr.sigtrap = 1;
	attr.remove_on_exec = 1;

	perf_fd = syscall(__NR_perf_event_open, &attr, 0, -1, -1, 0);
	if (perf_fd < 0 && (errno == ENOENT || errno == EOPNOTSUPP)) {
		printf("SKIP:no PERF_TYPE_BREAKPOINT/HW_BREAKPOINT_X\n");
		test__skip();
		goto cleanup;
	}
	if (!ASSERT_OK(perf_fd < 0, "perf_event_open"))
		goto cleanup;

	/* Configure the perf event to signal on sample. */
	err = fcntl(perf_fd, F_SETFL, O_ASYNC);
	if (!ASSERT_OK(err, "fcntl(F_SETFL, O_ASYNC)"))
		goto cleanup;

	owner.type = F_OWNER_TID;
	owner.pid = syscall(__NR_gettid);
	err = fcntl(perf_fd, F_SETOWN_EX, &owner);
	if (!ASSERT_OK(err, "fcntl(F_SETOWN_EX)"))
		goto cleanup;

	/* Allow at most one sample. A sample rejected by bpf should
	 * not count against this.
	 */
	err = ioctl(perf_fd, PERF_EVENT_IOC_REFRESH, 1);
	if (!ASSERT_OK(err, "ioctl(PERF_EVENT_IOC_REFRESH)"))
		goto cleanup;

	prog_link = bpf_program__attach_perf_event(skel->progs.handler, perf_fd);
	if (!ASSERT_OK_PTR(prog_link, "bpf_program__attach_perf_event"))
		goto cleanup;

	/* Configure the bpf program to suppress the sample. */
	skel->bss->ip = (uintptr_t)test_function;
	test_function();

	ASSERT_EQ(sigio_count, 0, "sigio_count");
	ASSERT_EQ(sigtrap_count, 0, "sigtrap_count");

	/* Configure the bpf program to allow the sample. */
	skel->bss->ip = 0;
	test_function();

	ASSERT_EQ(sigio_count, 1, "sigio_count");
	ASSERT_EQ(sigtrap_count, 1, "sigtrap_count");

	/* Test that the sample above is the only one allowed (by perf, not
	 * by bpf)
	 */
	test_function();

	ASSERT_EQ(sigio_count, 1, "sigio_count");
	ASSERT_EQ(sigtrap_count, 1, "sigtrap_count");

cleanup:
	bpf_link__destroy(prog_link);
	if (perf_fd >= 0)
		close(perf_fd);
	test_perf_skip__destroy(skel);

	if (previous_sigio != SIG_ERR)
		signal(SIGIO, previous_sigio);
	sigaction(SIGTRAP, &previous_sigtrap, NULL);
}
