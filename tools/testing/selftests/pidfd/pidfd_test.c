/* SPDX-License-Identifier: GPL-2.0 */

#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <linux/types.h>
#include <pthread.h>
#include <sched.h>
#include <signal.h>
#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <syscall.h>
#include <sys/epoll.h>
#include <sys/mman.h>
#include <sys/mount.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include "pidfd.h"
#include "../kselftest.h"

#define str(s) _str(s)
#define _str(s) #s
#define CHILD_THREAD_MIN_WAIT 3 /* seconds */

#define MAX_EVENTS 5

static bool have_pidfd_send_signal;

static pid_t pidfd_clone(int flags, int *pidfd, int (*fn)(void *))
{
	size_t stack_size = 1024;
	char *stack[1024] = { 0 };

#ifdef __ia64__
	return __clone2(fn, stack, stack_size, flags | SIGCHLD, NULL, pidfd);
#else
	return clone(fn, stack + stack_size, flags | SIGCHLD, NULL, pidfd);
#endif
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

	if (!have_pidfd_send_signal) {
		ksft_test_result_skip(
			"%s test: pidfd_send_signal() syscall not supported\n",
			test_name);
		return 0;
	}

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

static int test_pidfd_send_signal_exited_fail(void)
{
	int pidfd, ret, saved_errno;
	char buf[256];
	pid_t pid;
	const char *test_name = "pidfd_send_signal signal exited process";

	if (!have_pidfd_send_signal) {
		ksft_test_result_skip(
			"%s test: pidfd_send_signal() syscall not supported\n",
			test_name);
		return 0;
	}

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
 * Maximum number of cycles we allow. This is equivalent to PID_MAX_DEFAULT.
 * If users set a higher limit or we have cycled PIDFD_MAX_DEFAULT number of
 * times then we skip the test to not go into an infinite loop or block for a
 * long time.
 */
#define PIDFD_MAX_DEFAULT 0x8000

static int test_pidfd_send_signal_recycled_pid_fail(void)
{
	int i, ret;
	pid_t pid1;
	const char *test_name = "pidfd_send_signal signal recycled pid";

	if (!have_pidfd_send_signal) {
		ksft_test_result_skip(
			"%s test: pidfd_send_signal() syscall not supported\n",
			test_name);
		return 0;
	}

	ret = unshare(CLONE_NEWPID);
	if (ret < 0) {
		if (errno == EPERM) {
			ksft_test_result_skip("%s test: Unsharing pid namespace not permitted\n",
					      test_name);
			return 0;
		}
		ksft_exit_fail_msg("%s test: Failed to unshare pid namespace\n",
				   test_name);
	}

	ret = unshare(CLONE_NEWNS);
	if (ret < 0) {
		if (errno == EPERM) {
			ksft_test_result_skip("%s test: Unsharing mount namespace not permitted\n",
					      test_name);
			return 0;
		}
		ksft_exit_fail_msg("%s test: Failed to unshare mount namespace\n",
				   test_name);
	}

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
		ksft_test_result_skip("%s test: Skipping test\n", test_name);
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
		if (errno == ENOSYS) {
			ksft_test_result_skip(
				"%s test: pidfd_send_signal() syscall not supported\n",
				test_name);
			return 0;
		}
		ksft_exit_fail_msg("%s test: Failed to send signal\n",
				   test_name);
	}

	have_pidfd_send_signal = true;
	close(pidfd);
	ksft_test_result_pass(
		"%s test: pidfd_send_signal() syscall is supported. Tests can be executed\n",
		test_name);
	return 0;
}

static void *test_pidfd_poll_exec_thread(void *priv)
{
	ksft_print_msg("Child Thread: starting. pid %d tid %d ; and sleeping\n",
			getpid(), syscall(SYS_gettid));
	ksft_print_msg("Child Thread: doing exec of sleep\n");

	execl("/bin/sleep", "sleep", str(CHILD_THREAD_MIN_WAIT), (char *)NULL);

	ksft_print_msg("Child Thread: DONE. pid %d tid %d\n",
			getpid(), syscall(SYS_gettid));
	return NULL;
}

static void poll_pidfd(const char *test_name, int pidfd)
{
	int c;
	int epoll_fd = epoll_create1(EPOLL_CLOEXEC);
	struct epoll_event event, events[MAX_EVENTS];

	if (epoll_fd == -1)
		ksft_exit_fail_msg("%s test: Failed to create epoll file descriptor "
				   "(errno %d)\n",
				   test_name, errno);

	event.events = EPOLLIN;
	event.data.fd = pidfd;

	if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, pidfd, &event)) {
		ksft_exit_fail_msg("%s test: Failed to add epoll file descriptor "
				   "(errno %d)\n",
				   test_name, errno);
	}

	c = epoll_wait(epoll_fd, events, MAX_EVENTS, 5000);
	if (c != 1 || !(events[0].events & EPOLLIN))
		ksft_exit_fail_msg("%s test: Unexpected epoll_wait result (c=%d, events=%x) ",
				   "(errno %d)\n",
				   test_name, c, events[0].events, errno);

	close(epoll_fd);
	return;

}

static int child_poll_exec_test(void *args)
{
	pthread_t t1;

	ksft_print_msg("Child (pidfd): starting. pid %d tid %d\n", getpid(),
			syscall(SYS_gettid));
	pthread_create(&t1, NULL, test_pidfd_poll_exec_thread, NULL);
	/*
	 * Exec in the non-leader thread will destroy the leader immediately.
	 * If the wait in the parent returns too soon, the test fails.
	 */
	while (1)
		sleep(1);
}

static void test_pidfd_poll_exec(int use_waitpid)
{
	int pid, pidfd = 0;
	int status, ret;
	time_t prog_start = time(NULL);
	const char *test_name = "pidfd_poll check for premature notification on child thread exec";

	ksft_print_msg("Parent: pid: %d\n", getpid());
	pid = pidfd_clone(CLONE_PIDFD, &pidfd, child_poll_exec_test);
	if (pid < 0)
		ksft_exit_fail_msg("%s test: pidfd_clone failed (ret %d, errno %d)\n",
				   test_name, pid, errno);

	ksft_print_msg("Parent: Waiting for Child (%d) to complete.\n", pid);

	if (use_waitpid) {
		ret = waitpid(pid, &status, 0);
		if (ret == -1)
			ksft_print_msg("Parent: error\n");

		if (ret == pid)
			ksft_print_msg("Parent: Child process waited for.\n");
	} else {
		poll_pidfd(test_name, pidfd);
	}

	time_t prog_time = time(NULL) - prog_start;

	ksft_print_msg("Time waited for child: %lu\n", prog_time);

	close(pidfd);

	if (prog_time < CHILD_THREAD_MIN_WAIT || prog_time > CHILD_THREAD_MIN_WAIT + 2)
		ksft_exit_fail_msg("%s test: Failed\n", test_name);
	else
		ksft_test_result_pass("%s test: Passed\n", test_name);
}

static void *test_pidfd_poll_leader_exit_thread(void *priv)
{
	ksft_print_msg("Child Thread: starting. pid %d tid %d ; and sleeping\n",
			getpid(), syscall(SYS_gettid));
	sleep(CHILD_THREAD_MIN_WAIT);
	ksft_print_msg("Child Thread: DONE. pid %d tid %d\n", getpid(), syscall(SYS_gettid));
	return NULL;
}

static time_t *child_exit_secs;
static int child_poll_leader_exit_test(void *args)
{
	pthread_t t1, t2;

	ksft_print_msg("Child: starting. pid %d tid %d\n", getpid(), syscall(SYS_gettid));
	pthread_create(&t1, NULL, test_pidfd_poll_leader_exit_thread, NULL);
	pthread_create(&t2, NULL, test_pidfd_poll_leader_exit_thread, NULL);

	/*
	 * glibc exit calls exit_group syscall, so explicity call exit only
	 * so that only the group leader exits, leaving the threads alone.
	 */
	*child_exit_secs = time(NULL);
	syscall(SYS_exit, 0);
	/* Never reached, but appeases compiler thinking we should return. */
	exit(0);
}

static void test_pidfd_poll_leader_exit(int use_waitpid)
{
	int pid, pidfd = 0;
	int status, ret = 0;
	const char *test_name = "pidfd_poll check for premature notification on non-empty"
				"group leader exit";

	child_exit_secs = mmap(NULL, sizeof *child_exit_secs, PROT_READ | PROT_WRITE,
			MAP_SHARED | MAP_ANONYMOUS, -1, 0);

	if (child_exit_secs == MAP_FAILED)
		ksft_exit_fail_msg("%s test: mmap failed (errno %d)\n",
				   test_name, errno);

	ksft_print_msg("Parent: pid: %d\n", getpid());
	pid = pidfd_clone(CLONE_PIDFD, &pidfd, child_poll_leader_exit_test);
	if (pid < 0)
		ksft_exit_fail_msg("%s test: pidfd_clone failed (ret %d, errno %d)\n",
				   test_name, pid, errno);

	ksft_print_msg("Parent: Waiting for Child (%d) to complete.\n", pid);

	if (use_waitpid) {
		ret = waitpid(pid, &status, 0);
		if (ret == -1)
			ksft_print_msg("Parent: error\n");
	} else {
		/*
		 * This sleep tests for the case where if the child exits, and is in
		 * EXIT_ZOMBIE, but the thread group leader is non-empty, then the poll
		 * doesn't prematurely return even though there are active threads
		 */
		sleep(1);
		poll_pidfd(test_name, pidfd);
	}

	if (ret == pid)
		ksft_print_msg("Parent: Child process waited for.\n");

	time_t since_child_exit = time(NULL) - *child_exit_secs;

	ksft_print_msg("Time since child exit: %lu\n", since_child_exit);

	close(pidfd);

	if (since_child_exit < CHILD_THREAD_MIN_WAIT ||
			since_child_exit > CHILD_THREAD_MIN_WAIT + 2)
		ksft_exit_fail_msg("%s test: Failed\n", test_name);
	else
		ksft_test_result_pass("%s test: Passed\n", test_name);
}

int main(int argc, char **argv)
{
	ksft_print_header();
	ksft_set_plan(8);

	test_pidfd_poll_exec(0);
	test_pidfd_poll_exec(1);
	test_pidfd_poll_leader_exit(0);
	test_pidfd_poll_leader_exit(1);
	test_pidfd_send_signal_syscall_support();
	test_pidfd_send_signal_simple_success();
	test_pidfd_send_signal_exited_fail();
	test_pidfd_send_signal_recycled_pid_fail();

	return ksft_exit_pass();
}
