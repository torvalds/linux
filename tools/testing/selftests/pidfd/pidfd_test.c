/* SPDX-License-Identifier: GPL-2.0 */

#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <linux/types.h>
#include <sched.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syscall.h>
#include <sys/mount.h>
#include <sys/wait.h>
#include <unistd.h>

#include "../kselftest.h"

static inline int sys_pidfd_send_signal(int pidfd, int sig, siginfo_t *info,
					unsigned int flags)
{
	return syscall(__NR_pidfd_send_signal, pidfd, sig, info, flags);
}

static int signal_received;

static void set_signal_received_on_sigusr1(int sig)
{
	if (sig == SIGUSR1)
		signal_received = 1;
}

/*
 * Straightforward test to see whether pidfd_send_signal() works is to send
 * a signal to ourself.
 */
static int test_pidfd_send_signal_simple_success(void)
{
	int pidfd, ret;
	const char *test_name = "pidfd_send_signal send SIGUSR1";

	pidfd = open("/proc/self", O_DIRECTORY | O_CLOEXEC);
	if (pidfd < 0)
		ksft_exit_fail_msg(
			"%s test: Failed to open process file descriptor\n",
			test_name);

	signal(SIGUSR1, set_signal_received_on_sigusr1);

	ret = sys_pidfd_send_signal(pidfd, SIGUSR1, NULL, 0);
	close(pidfd);
	if (ret < 0)
		ksft_exit_fail_msg("%s test: Failed to send signal\n",
				   test_name);

	if (signal_received != 1)
		ksft_exit_fail_msg("%s test: Failed to receive signal\n",
				   test_name);

	signal_received = 0;
	ksft_test_result_pass("%s test: Sent signal\n", test_name);
	return 0;
}

static int wait_for_pid(pid_t pid)
{
	int status, ret;

again:
	ret = waitpid(pid, &status, 0);
	if (ret == -1) {
		if (errno == EINTR)
			goto again;

		return -1;
	}

	if (ret != pid)
		goto again;

	if (!WIFEXITED(status))
		return -1;

	return WEXITSTATUS(status);
}

static int test_pidfd_send_signal_exited_fail(void)
{
	int pidfd, ret, saved_errno;
	char buf[256];
	pid_t pid;
	const char *test_name = "pidfd_send_signal signal exited process";

	pid = fork();
	if (pid < 0)
		ksft_exit_fail_msg("%s test: Failed to create new process\n",
				   test_name);

	if (pid == 0)
		_exit(EXIT_SUCCESS);

	snprintf(buf, sizeof(buf), "/proc/%d", pid);

	pidfd = open(buf, O_DIRECTORY | O_CLOEXEC);

	(void)wait_for_pid(pid);

	if (pidfd < 0)
		ksft_exit_fail_msg(
			"%s test: Failed to open process file descriptor\n",
			test_name);

	ret = sys_pidfd_send_signal(pidfd, 0, NULL, 0);
	saved_errno = errno;
	close(pidfd);
	if (ret == 0)
		ksft_exit_fail_msg(
			"%s test: Managed to send signal to process even though it should have failed\n",
			test_name);

	if (saved_errno != ESRCH)
		ksft_exit_fail_msg(
			"%s test: Expected to receive ESRCH as errno value but received %d instead\n",
			test_name, saved_errno);

	ksft_test_result_pass("%s test: Failed to send signal as expected\n",
			      test_name);
	return 0;
}

/*
 * The kernel reserves 300 pids via RESERVED_PIDS in kernel/pid.c
 * That means, when it wraps around any pid < 300 will be skipped.
 * So we need to use a pid > 300 in order to test recycling.
 */
#define PID_RECYCLE 1000

/*
 * Maximum number of cycles we allow. This is equivalent to PID_MAX_DEFAULT.
 * If users set a higher limit or we have cycled PIDFD_MAX_DEFAULT number of
 * times then we skip the test to not go into an infinite loop or block for a
 * long time.
 */
#define PIDFD_MAX_DEFAULT 0x8000

/*
 * Define a few custom error codes for the child process to clearly indicate
 * what is happening. This way we can tell the difference between a system
 * error, a test error, etc.
 */
#define PIDFD_PASS 0
#define PIDFD_FAIL 1
#define PIDFD_ERROR 2
#define PIDFD_SKIP 3
#define PIDFD_XFAIL 4

static int test_pidfd_send_signal_recycled_pid_fail(void)
{
	int i, ret;
	pid_t pid1;
	const char *test_name = "pidfd_send_signal signal recycled pid";

	ret = unshare(CLONE_NEWPID);
	if (ret < 0)
		ksft_exit_fail_msg("%s test: Failed to unshare pid namespace\n",
				   test_name);

	ret = unshare(CLONE_NEWNS);
	if (ret < 0)
		ksft_exit_fail_msg(
			"%s test: Failed to unshare mount namespace\n",
			test_name);

	ret = mount(NULL, "/", NULL, MS_REC | MS_PRIVATE, 0);
	if (ret < 0)
		ksft_exit_fail_msg("%s test: Failed to remount / private\n",
				   test_name);

	/* pid 1 in new pid namespace */
	pid1 = fork();
	if (pid1 < 0)
		ksft_exit_fail_msg("%s test: Failed to create new process\n",
				   test_name);

	if (pid1 == 0) {
		char buf[256];
		pid_t pid2;
		int pidfd = -1;

		(void)umount2("/proc", MNT_DETACH);
		ret = mount("proc", "/proc", "proc", 0, NULL);
		if (ret < 0)
			_exit(PIDFD_ERROR);

		/* grab pid PID_RECYCLE */
		for (i = 0; i <= PIDFD_MAX_DEFAULT; i++) {
			pid2 = fork();
			if (pid2 < 0)
				_exit(PIDFD_ERROR);

			if (pid2 == 0)
				_exit(PIDFD_PASS);

			if (pid2 == PID_RECYCLE) {
				snprintf(buf, sizeof(buf), "/proc/%d", pid2);
				ksft_print_msg("pid to recycle is %d\n", pid2);
				pidfd = open(buf, O_DIRECTORY | O_CLOEXEC);
			}

			if (wait_for_pid(pid2))
				_exit(PIDFD_ERROR);

			if (pid2 >= PID_RECYCLE)
				break;
		}

		/*
		 * We want to be as predictable as we can so if we haven't been
		 * able to grab pid PID_RECYCLE skip the test.
		 */
		if (pid2 != PID_RECYCLE) {
			/* skip test */
			close(pidfd);
			_exit(PIDFD_SKIP);
		}

		if (pidfd < 0)
			_exit(PIDFD_ERROR);

		for (i = 0; i <= PIDFD_MAX_DEFAULT; i++) {
			char c;
			int pipe_fds[2];
			pid_t recycled_pid;
			int child_ret = PIDFD_PASS;

			ret = pipe2(pipe_fds, O_CLOEXEC);
			if (ret < 0)
				_exit(PIDFD_ERROR);

			recycled_pid = fork();
			if (recycled_pid < 0)
				_exit(PIDFD_ERROR);

			if (recycled_pid == 0) {
				close(pipe_fds[1]);
				(void)read(pipe_fds[0], &c, 1);
				close(pipe_fds[0]);

				_exit(PIDFD_PASS);
			}

			/*
			 * Stop the child so we can inspect whether we have
			 * recycled pid PID_RECYCLE.
			 */
			close(pipe_fds[0]);
			ret = kill(recycled_pid, SIGSTOP);
			close(pipe_fds[1]);
			if (ret) {
				(void)wait_for_pid(recycled_pid);
				_exit(PIDFD_ERROR);
			}

			/*
			 * We have recycled the pid. Try to signal it. This
			 * needs to fail since this is a different process than
			 * the one the pidfd refers to.
			 */
			if (recycled_pid == PID_RECYCLE) {
				ret = sys_pidfd_send_signal(pidfd, SIGCONT,
							    NULL, 0);
				if (ret && errno == ESRCH)
					child_ret = PIDFD_XFAIL;
				else
					child_ret = PIDFD_FAIL;
			}

			/* let the process move on */
			ret = kill(recycled_pid, SIGCONT);
			if (ret)
				(void)kill(recycled_pid, SIGKILL);

			if (wait_for_pid(recycled_pid))
				_exit(PIDFD_ERROR);

			switch (child_ret) {
			case PIDFD_FAIL:
				/* fallthrough */
			case PIDFD_XFAIL:
				_exit(child_ret);
			case PIDFD_PASS:
				break;
			default:
				/* not reached */
				_exit(PIDFD_ERROR);
			}

			/*
			 * If the user set a custom pid_max limit we could be
			 * in the millions.
			 * Skip the test in this case.
			 */
			if (recycled_pid > PIDFD_MAX_DEFAULT)
				_exit(PIDFD_SKIP);
		}

		/* failed to recycle pid */
		_exit(PIDFD_SKIP);
	}

	ret = wait_for_pid(pid1);
	switch (ret) {
	case PIDFD_FAIL:
		ksft_exit_fail_msg(
			"%s test: Managed to signal recycled pid %d\n",
			test_name, PID_RECYCLE);
	case PIDFD_PASS:
		ksft_exit_fail_msg("%s test: Failed to recycle pid %d\n",
				   test_name, PID_RECYCLE);
	case PIDFD_SKIP:
		ksft_print_msg("%s test: Skipping test\n", test_name);
		ret = 0;
		break;
	case PIDFD_XFAIL:
		ksft_test_result_pass(
			"%s test: Failed to signal recycled pid as expected\n",
			test_name);
		ret = 0;
		break;
	default /* PIDFD_ERROR */:
		ksft_exit_fail_msg("%s test: Error while running tests\n",
				   test_name);
	}

	return ret;
}

static int test_pidfd_send_signal_syscall_support(void)
{
	int pidfd, ret;
	const char *test_name = "pidfd_send_signal check for support";

	pidfd = open("/proc/self", O_DIRECTORY | O_CLOEXEC);
	if (pidfd < 0)
		ksft_exit_fail_msg(
			"%s test: Failed to open process file descriptor\n",
			test_name);

	ret = sys_pidfd_send_signal(pidfd, 0, NULL, 0);
	if (ret < 0) {
		/*
		 * pidfd_send_signal() will currently return ENOSYS when
		 * CONFIG_PROC_FS is not set.
		 */
		if (errno == ENOSYS)
			ksft_exit_skip(
				"%s test: pidfd_send_signal() syscall not supported (Ensure that CONFIG_PROC_FS=y is set)\n",
				test_name);

		ksft_exit_fail_msg("%s test: Failed to send signal\n",
				   test_name);
	}

	close(pidfd);
	ksft_test_result_pass(
		"%s test: pidfd_send_signal() syscall is supported. Tests can be executed\n",
		test_name);
	return 0;
}

int main(int argc, char **argv)
{
	ksft_print_header();

	test_pidfd_send_signal_syscall_support();
	test_pidfd_send_signal_simple_success();
	test_pidfd_send_signal_exited_fail();
	test_pidfd_send_signal_recycled_pid_fail();

	return ksft_exit_pass();
}
