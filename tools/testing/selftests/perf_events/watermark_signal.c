// SPDX-License-Identifier: GPL-2.0
#define _GNU_SOURCE

#include <errno.h>
#include <fcntl.h>
#include <linux/perf_event.h>
#include <stddef.h>
#include <sched.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <unistd.h>

#include "../kselftest_harness.h"

static int sigio_count;

static void handle_sigio(int signum __maybe_unused,
			 siginfo_t *oh __maybe_unused,
			 void *uc __maybe_unused)
{
	++sigio_count;
}

static void do_child(void)
{
	raise(SIGSTOP);

	for (int i = 0; i < 20; ++i)
		sleep(1);

	raise(SIGSTOP);

	exit(0);
}

TEST(watermark_signal)
{
	struct perf_event_attr attr;
	struct perf_event_mmap_page *p = NULL;
	struct sigaction previous_sigio, sigio = { 0 };
	pid_t child = -1;
	int child_status;
	int fd = -1;
	long page_size = sysconf(_SC_PAGE_SIZE);

	sigio.sa_sigaction = handle_sigio;
	EXPECT_EQ(sigaction(SIGIO, &sigio, &previous_sigio), 0);

	memset(&attr, 0, sizeof(attr));
	attr.size = sizeof(attr);
	attr.type = PERF_TYPE_SOFTWARE;
	attr.config = PERF_COUNT_SW_DUMMY;
	attr.sample_period = 1;
	attr.disabled = 1;
	attr.watermark = 1;
	attr.context_switch = 1;
	attr.wakeup_watermark = 1;

	child = fork();
	EXPECT_GE(child, 0);
	if (child == 0)
		do_child();
	else if (child < 0) {
		perror("fork()");
		goto cleanup;
	}

	if (waitpid(child, &child_status, WSTOPPED) != child ||
	    !(WIFSTOPPED(child_status) && WSTOPSIG(child_status) == SIGSTOP)) {
		fprintf(stderr,
			"failed to synchronize with child errno=%d status=%x\n",
			errno,
			child_status);
		goto cleanup;
	}

	fd = syscall(__NR_perf_event_open, &attr, child, -1, -1,
		     PERF_FLAG_FD_CLOEXEC);
	if (fd < 0) {
		fprintf(stderr, "failed opening event %llx\n", attr.config);
		goto cleanup;
	}

	if (fcntl(fd, F_SETFL, FASYNC)) {
		perror("F_SETFL FASYNC");
		goto cleanup;
	}

	if (fcntl(fd, F_SETOWN, getpid())) {
		perror("F_SETOWN getpid()");
		goto cleanup;
	}

	if (fcntl(fd, F_SETSIG, SIGIO)) {
		perror("F_SETSIG SIGIO");
		goto cleanup;
	}

	p = mmap(NULL, 2 * page_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	if (p == NULL) {
		perror("mmap");
		goto cleanup;
	}

	if (ioctl(fd, PERF_EVENT_IOC_ENABLE, 0)) {
		perror("PERF_EVENT_IOC_ENABLE");
		goto cleanup;
	}

	if (kill(child, SIGCONT) < 0) {
		perror("SIGCONT");
		goto cleanup;
	}

	if (waitpid(child, &child_status, WSTOPPED) != -1 || errno != EINTR)
		fprintf(stderr,
			"expected SIGIO to terminate wait errno=%d status=%x\n%d",
			errno,
			child_status,
			sigio_count);

	EXPECT_GE(sigio_count, 1);

cleanup:
	if (p != NULL)
		munmap(p, 2 * page_size);

	if (fd >= 0)
		close(fd);

	if (child > 0) {
		kill(child, SIGKILL);
		waitpid(child, NULL, 0);
	}

	sigaction(SIGIO, &previous_sigio, NULL);
}

TEST_HARNESS_MAIN
