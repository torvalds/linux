/* SPDX-License-Identifier: GPL-2.0 */

#define _GNU_SOURCE
#include <errno.h>
#include <linux/sched.h>
#include <linux/types.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sched.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include "pidfd.h"
#include "../kselftest.h"

#define ptr_to_u64(ptr) ((__u64)((uintptr_t)(ptr)))

static pid_t sys_clone3(struct clone_args *args)
{
	return syscall(__NR_clone3, args, sizeof(struct clone_args));
}

static int sys_waitid(int which, pid_t pid, siginfo_t *info, int options,
		      struct rusage *ru)
{
	return syscall(__NR_waitid, which, pid, info, options, ru);
}

static int test_pidfd_wait_simple(void)
{
	const char *test_name = "pidfd wait simple";
	int pidfd = -1, status = 0;
	pid_t parent_tid = -1;
	struct clone_args args = {
		.parent_tid = ptr_to_u64(&parent_tid),
		.pidfd = ptr_to_u64(&pidfd),
		.flags = CLONE_PIDFD | CLONE_PARENT_SETTID,
		.exit_signal = SIGCHLD,
	};
	int ret;
	pid_t pid;
	siginfo_t info = {
		.si_signo = 0,
	};

	pidfd = open("/proc/self", O_DIRECTORY | O_RDONLY | O_CLOEXEC);
	if (pidfd < 0)
		ksft_exit_fail_msg("%s test: failed to open /proc/self %s\n",
				   test_name, strerror(errno));

	pid = sys_waitid(P_PIDFD, pidfd, &info, WEXITED, NULL);
	if (pid == 0)
		ksft_exit_fail_msg(
			"%s test: succeeded to wait on invalid pidfd %s\n",
			test_name, strerror(errno));
	close(pidfd);
	pidfd = -1;

	pidfd = open("/dev/null", O_RDONLY | O_CLOEXEC);
	if (pidfd == 0)
		ksft_exit_fail_msg("%s test: failed to open /dev/null %s\n",
				   test_name, strerror(errno));

	pid = sys_waitid(P_PIDFD, pidfd, &info, WEXITED, NULL);
	if (pid == 0)
		ksft_exit_fail_msg(
			"%s test: succeeded to wait on invalid pidfd %s\n",
			test_name, strerror(errno));
	close(pidfd);
	pidfd = -1;

	pid = sys_clone3(&args);
	if (pid < 0)
		ksft_exit_fail_msg("%s test: failed to create new process %s\n",
				   test_name, strerror(errno));

	if (pid == 0)
		exit(EXIT_SUCCESS);

	pid = sys_waitid(P_PIDFD, pidfd, &info, WEXITED, NULL);
	if (pid < 0)
		ksft_exit_fail_msg(
			"%s test: failed to wait on process with pid %d and pidfd %d: %s\n",
			test_name, parent_tid, pidfd, strerror(errno));

	if (!WIFEXITED(info.si_status) || WEXITSTATUS(info.si_status))
		ksft_exit_fail_msg(
			"%s test: unexpected status received after waiting on process with pid %d and pidfd %d: %s\n",
			test_name, parent_tid, pidfd, strerror(errno));
	close(pidfd);

	if (info.si_signo != SIGCHLD)
		ksft_exit_fail_msg(
			"%s test: unexpected si_signo value %d received after waiting on process with pid %d and pidfd %d: %s\n",
			test_name, info.si_signo, parent_tid, pidfd,
			strerror(errno));

	if (info.si_code != CLD_EXITED)
		ksft_exit_fail_msg(
			"%s test: unexpected si_code value %d received after waiting on process with pid %d and pidfd %d: %s\n",
			test_name, info.si_code, parent_tid, pidfd,
			strerror(errno));

	if (info.si_pid != parent_tid)
		ksft_exit_fail_msg(
			"%s test: unexpected si_pid value %d received after waiting on process with pid %d and pidfd %d: %s\n",
			test_name, info.si_pid, parent_tid, pidfd,
			strerror(errno));

	ksft_test_result_pass("%s test: Passed\n", test_name);
	return 0;
}

static int test_pidfd_wait_states(void)
{
	const char *test_name = "pidfd wait states";
	int pidfd = -1, status = 0;
	pid_t parent_tid = -1;
	struct clone_args args = {
		.parent_tid = ptr_to_u64(&parent_tid),
		.pidfd = ptr_to_u64(&pidfd),
		.flags = CLONE_PIDFD | CLONE_PARENT_SETTID,
		.exit_signal = SIGCHLD,
	};
	int ret;
	pid_t pid;
	siginfo_t info = {
		.si_signo = 0,
	};

	pid = sys_clone3(&args);
	if (pid < 0)
		ksft_exit_fail_msg("%s test: failed to create new process %s\n",
				   test_name, strerror(errno));

	if (pid == 0) {
		kill(getpid(), SIGSTOP);
		kill(getpid(), SIGSTOP);
		exit(EXIT_SUCCESS);
	}

	ret = sys_waitid(P_PIDFD, pidfd, &info, WSTOPPED, NULL);
	if (ret < 0)
		ksft_exit_fail_msg(
			"%s test: failed to wait on WSTOPPED process with pid %d and pidfd %d: %s\n",
			test_name, parent_tid, pidfd, strerror(errno));

	if (info.si_signo != SIGCHLD)
		ksft_exit_fail_msg(
			"%s test: unexpected si_signo value %d received after waiting on process with pid %d and pidfd %d: %s\n",
			test_name, info.si_signo, parent_tid, pidfd,
			strerror(errno));

	if (info.si_code != CLD_STOPPED)
		ksft_exit_fail_msg(
			"%s test: unexpected si_code value %d received after waiting on process with pid %d and pidfd %d: %s\n",
			test_name, info.si_code, parent_tid, pidfd,
			strerror(errno));

	if (info.si_pid != parent_tid)
		ksft_exit_fail_msg(
			"%s test: unexpected si_pid value %d received after waiting on process with pid %d and pidfd %d: %s\n",
			test_name, info.si_pid, parent_tid, pidfd,
			strerror(errno));

	ret = sys_pidfd_send_signal(pidfd, SIGCONT, NULL, 0);
	if (ret < 0)
		ksft_exit_fail_msg(
			"%s test: failed to send signal to process with pid %d and pidfd %d: %s\n",
			test_name, parent_tid, pidfd, strerror(errno));

	ret = sys_waitid(P_PIDFD, pidfd, &info, WCONTINUED, NULL);
	if (ret < 0)
		ksft_exit_fail_msg(
			"%s test: failed to wait WCONTINUED on process with pid %d and pidfd %d: %s\n",
			test_name, parent_tid, pidfd, strerror(errno));

	if (info.si_signo != SIGCHLD)
		ksft_exit_fail_msg(
			"%s test: unexpected si_signo value %d received after waiting on process with pid %d and pidfd %d: %s\n",
			test_name, info.si_signo, parent_tid, pidfd,
			strerror(errno));

	if (info.si_code != CLD_CONTINUED)
		ksft_exit_fail_msg(
			"%s test: unexpected si_code value %d received after waiting on process with pid %d and pidfd %d: %s\n",
			test_name, info.si_code, parent_tid, pidfd,
			strerror(errno));

	if (info.si_pid != parent_tid)
		ksft_exit_fail_msg(
			"%s test: unexpected si_pid value %d received after waiting on process with pid %d and pidfd %d: %s\n",
			test_name, info.si_pid, parent_tid, pidfd,
			strerror(errno));

	ret = sys_waitid(P_PIDFD, pidfd, &info, WUNTRACED, NULL);
	if (ret < 0)
		ksft_exit_fail_msg(
			"%s test: failed to wait on WUNTRACED process with pid %d and pidfd %d: %s\n",
			test_name, parent_tid, pidfd, strerror(errno));

	if (info.si_signo != SIGCHLD)
		ksft_exit_fail_msg(
			"%s test: unexpected si_signo value %d received after waiting on process with pid %d and pidfd %d: %s\n",
			test_name, info.si_signo, parent_tid, pidfd,
			strerror(errno));

	if (info.si_code != CLD_STOPPED)
		ksft_exit_fail_msg(
			"%s test: unexpected si_code value %d received after waiting on process with pid %d and pidfd %d: %s\n",
			test_name, info.si_code, parent_tid, pidfd,
			strerror(errno));

	if (info.si_pid != parent_tid)
		ksft_exit_fail_msg(
			"%s test: unexpected si_pid value %d received after waiting on process with pid %d and pidfd %d: %s\n",
			test_name, info.si_pid, parent_tid, pidfd,
			strerror(errno));

	ret = sys_pidfd_send_signal(pidfd, SIGKILL, NULL, 0);
	if (ret < 0)
		ksft_exit_fail_msg(
			"%s test: failed to send SIGKILL to process with pid %d and pidfd %d: %s\n",
			test_name, parent_tid, pidfd, strerror(errno));

	ret = sys_waitid(P_PIDFD, pidfd, &info, WEXITED, NULL);
	if (ret < 0)
		ksft_exit_fail_msg(
			"%s test: failed to wait on WEXITED process with pid %d and pidfd %d: %s\n",
			test_name, parent_tid, pidfd, strerror(errno));

	if (info.si_signo != SIGCHLD)
		ksft_exit_fail_msg(
			"%s test: unexpected si_signo value %d received after waiting on process with pid %d and pidfd %d: %s\n",
			test_name, info.si_signo, parent_tid, pidfd,
			strerror(errno));

	if (info.si_code != CLD_KILLED)
		ksft_exit_fail_msg(
			"%s test: unexpected si_code value %d received after waiting on process with pid %d and pidfd %d: %s\n",
			test_name, info.si_code, parent_tid, pidfd,
			strerror(errno));

	if (info.si_pid != parent_tid)
		ksft_exit_fail_msg(
			"%s test: unexpected si_pid value %d received after waiting on process with pid %d and pidfd %d: %s\n",
			test_name, info.si_pid, parent_tid, pidfd,
			strerror(errno));

	close(pidfd);

	ksft_test_result_pass("%s test: Passed\n", test_name);
	return 0;
}

int main(int argc, char **argv)
{
	ksft_print_header();
	ksft_set_plan(2);

	test_pidfd_wait_simple();
	test_pidfd_wait_states();

	return ksft_exit_pass();
}
