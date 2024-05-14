// SPDX-License-Identifier: GPL-2.0
/*
 * Test for remove_on_exec.
 *
 * Copyright (C) 2021, Google LLC.
 */

#define _GNU_SOURCE

/* We need the latest siginfo from the kernel repo. */
#include <sys/types.h>
#include <asm/siginfo.h>
#define __have_siginfo_t 1
#define __have_sigval_t 1
#define __have_sigevent_t 1
#define __siginfo_t_defined
#define __sigval_t_defined
#define __sigevent_t_defined
#define _BITS_SIGINFO_CONSTS_H 1
#define _BITS_SIGEVENT_CONSTS_H 1

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <linux/perf_event.h>
#include <pthread.h>
#include <signal.h>
#include <sys/ioctl.h>
#include <sys/syscall.h>
#include <unistd.h>

#include "../kselftest_harness.h"

static volatile int signal_count;

static struct perf_event_attr make_event_attr(void)
{
	struct perf_event_attr attr = {
		.type		= PERF_TYPE_HARDWARE,
		.size		= sizeof(attr),
		.config		= PERF_COUNT_HW_INSTRUCTIONS,
		.sample_period	= 1000,
		.exclude_kernel = 1,
		.exclude_hv	= 1,
		.disabled	= 1,
		.inherit	= 1,
		/*
		 * Children normally retain their inherited event on exec; with
		 * remove_on_exec, we'll remove their event, but the parent and
		 * any other non-exec'd children will keep their events.
		 */
		.remove_on_exec = 1,
		.sigtrap	= 1,
	};
	return attr;
}

static void sigtrap_handler(int signum, siginfo_t *info, void *ucontext)
{
	if (info->si_code != TRAP_PERF) {
		fprintf(stderr, "%s: unexpected si_code %d\n", __func__, info->si_code);
		return;
	}

	signal_count++;
}

FIXTURE(remove_on_exec)
{
	struct sigaction oldact;
	int fd;
};

FIXTURE_SETUP(remove_on_exec)
{
	struct perf_event_attr attr = make_event_attr();
	struct sigaction action = {};

	signal_count = 0;

	/* Initialize sigtrap handler. */
	action.sa_flags = SA_SIGINFO | SA_NODEFER;
	action.sa_sigaction = sigtrap_handler;
	sigemptyset(&action.sa_mask);
	ASSERT_EQ(sigaction(SIGTRAP, &action, &self->oldact), 0);

	/* Initialize perf event. */
	self->fd = syscall(__NR_perf_event_open, &attr, 0, -1, -1, PERF_FLAG_FD_CLOEXEC);
	ASSERT_NE(self->fd, -1);
}

FIXTURE_TEARDOWN(remove_on_exec)
{
	close(self->fd);
	sigaction(SIGTRAP, &self->oldact, NULL);
}

/* Verify event propagates to fork'd child. */
TEST_F(remove_on_exec, fork_only)
{
	int status;
	pid_t pid = fork();

	if (pid == 0) {
		ASSERT_EQ(signal_count, 0);
		ASSERT_EQ(ioctl(self->fd, PERF_EVENT_IOC_ENABLE, 0), 0);
		while (!signal_count);
		_exit(42);
	}

	while (!signal_count); /* Child enables event. */
	EXPECT_EQ(waitpid(pid, &status, 0), pid);
	EXPECT_EQ(WEXITSTATUS(status), 42);
}

/*
 * Verify that event does _not_ propagate to fork+exec'd child; event enabled
 * after fork+exec.
 */
TEST_F(remove_on_exec, fork_exec_then_enable)
{
	pid_t pid_exec, pid_only_fork;
	int pipefd[2];
	int tmp;

	/*
	 * Non-exec child, to ensure exec does not affect inherited events of
	 * other children.
	 */
	pid_only_fork = fork();
	if (pid_only_fork == 0) {
		/* Block until parent enables event. */
		while (!signal_count);
		_exit(42);
	}

	ASSERT_NE(pipe(pipefd), -1);
	pid_exec = fork();
	if (pid_exec == 0) {
		ASSERT_NE(dup2(pipefd[1], STDOUT_FILENO), -1);
		close(pipefd[0]);
		execl("/proc/self/exe", "exec_child", NULL);
		_exit((perror("exec failed"), 1));
	}
	close(pipefd[1]);

	ASSERT_EQ(waitpid(pid_exec, &tmp, WNOHANG), 0); /* Child is running. */
	/* Wait for exec'd child to start spinning. */
	EXPECT_EQ(read(pipefd[0], &tmp, sizeof(int)), sizeof(int));
	EXPECT_EQ(tmp, 42);
	close(pipefd[0]);
	/* Now we can enable the event, knowing the child is doing work. */
	EXPECT_EQ(ioctl(self->fd, PERF_EVENT_IOC_ENABLE, 0), 0);
	/* If the event propagated to the exec'd child, it will exit normally... */
	usleep(100000); /* ... give time for event to trigger (in case of bug). */
	EXPECT_EQ(waitpid(pid_exec, &tmp, WNOHANG), 0); /* Should still be running. */
	EXPECT_EQ(kill(pid_exec, SIGKILL), 0);

	/* Verify removal from child did not affect this task's event. */
	tmp = signal_count;
	while (signal_count == tmp); /* Should not hang! */
	/* Nor should it have affected the first child. */
	EXPECT_EQ(waitpid(pid_only_fork, &tmp, 0), pid_only_fork);
	EXPECT_EQ(WEXITSTATUS(tmp), 42);
}

/*
 * Verify that event does _not_ propagate to fork+exec'd child; event enabled
 * before fork+exec.
 */
TEST_F(remove_on_exec, enable_then_fork_exec)
{
	pid_t pid_exec;
	int tmp;

	EXPECT_EQ(ioctl(self->fd, PERF_EVENT_IOC_ENABLE, 0), 0);

	pid_exec = fork();
	if (pid_exec == 0) {
		execl("/proc/self/exe", "exec_child", NULL);
		_exit((perror("exec failed"), 1));
	}

	/*
	 * The child may exit abnormally at any time if the event propagated and
	 * a SIGTRAP is sent before the handler was set up.
	 */
	usleep(100000); /* ... give time for event to trigger (in case of bug). */
	EXPECT_EQ(waitpid(pid_exec, &tmp, WNOHANG), 0); /* Should still be running. */
	EXPECT_EQ(kill(pid_exec, SIGKILL), 0);

	/* Verify removal from child did not affect this task's event. */
	tmp = signal_count;
	while (signal_count == tmp); /* Should not hang! */
}

TEST_F(remove_on_exec, exec_stress)
{
	pid_t pids[30];
	int i, tmp;

	for (i = 0; i < sizeof(pids) / sizeof(pids[0]); i++) {
		pids[i] = fork();
		if (pids[i] == 0) {
			execl("/proc/self/exe", "exec_child", NULL);
			_exit((perror("exec failed"), 1));
		}

		/* Some forked with event disabled, rest with enabled. */
		if (i > 10)
			EXPECT_EQ(ioctl(self->fd, PERF_EVENT_IOC_ENABLE, 0), 0);
	}

	usleep(100000); /* ... give time for event to trigger (in case of bug). */

	for (i = 0; i < sizeof(pids) / sizeof(pids[0]); i++) {
		/* All children should still be running. */
		EXPECT_EQ(waitpid(pids[i], &tmp, WNOHANG), 0);
		EXPECT_EQ(kill(pids[i], SIGKILL), 0);
	}

	/* Verify event is still alive. */
	tmp = signal_count;
	while (signal_count == tmp);
}

/* For exec'd child. */
static void exec_child(void)
{
	struct sigaction action = {};
	const int val = 42;

	/* Set up sigtrap handler in case we erroneously receive a trap. */
	action.sa_flags = SA_SIGINFO | SA_NODEFER;
	action.sa_sigaction = sigtrap_handler;
	sigemptyset(&action.sa_mask);
	if (sigaction(SIGTRAP, &action, NULL))
		_exit((perror("sigaction failed"), 1));

	/* Signal parent that we're starting to spin. */
	if (write(STDOUT_FILENO, &val, sizeof(int)) == -1)
		_exit((perror("write failed"), 1));

	/* Should hang here until killed. */
	while (!signal_count);
}

#define main test_main
TEST_HARNESS_MAIN
#undef main
int main(int argc, char *argv[])
{
	if (!strcmp(argv[0], "exec_child")) {
		exec_child();
		return 1;
	}

	return test_main(argc, argv);
}
