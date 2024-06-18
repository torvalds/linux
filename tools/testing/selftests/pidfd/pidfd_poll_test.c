// SPDX-License-Identifier: GPL-2.0

#define _GNU_SOURCE
#include <errno.h>
#include <linux/types.h>
#include <poll.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syscall.h>
#include <sys/wait.h>
#include <unistd.h>

#include "pidfd.h"
#include "../kselftest.h"

static bool timeout;

static void handle_alarm(int sig)
{
	timeout = true;
}

int main(int argc, char **argv)
{
	struct pollfd fds;
	int iter, nevents;
	int nr_iterations = 10000;

	fds.events = POLLIN;

	if (argc > 2)
		ksft_exit_fail_msg("Unexpected command line argument\n");

	if (argc == 2) {
		nr_iterations = atoi(argv[1]);
		if (nr_iterations <= 0)
			ksft_exit_fail_msg("invalid input parameter %s\n",
					argv[1]);
	}

	ksft_print_msg("running pidfd poll test for %d iterations\n",
		nr_iterations);

	for (iter = 0; iter < nr_iterations; iter++) {
		int pidfd;
		int child_pid = fork();

		if (child_pid < 0) {
			if (errno == EAGAIN) {
				iter--;
				continue;
			}
			ksft_exit_fail_msg(
				"%s - failed to fork a child process\n",
				strerror(errno));
		}

		if (child_pid == 0) {
			/* Child process just sleeps for a min and exits */
			sleep(60);
			exit(EXIT_SUCCESS);
		}

		/* Parent kills the child and waits for its death */
		pidfd = sys_pidfd_open(child_pid, 0);
		if (pidfd < 0)
			ksft_exit_fail_msg("%s - pidfd_open failed\n",
					strerror(errno));

		/* Setup 3 sec alarm - plenty of time */
		if (signal(SIGALRM, handle_alarm) == SIG_ERR)
			ksft_exit_fail_msg("%s - signal failed\n",
					strerror(errno));
		alarm(3);

		/* Send SIGKILL to the child */
		if (sys_pidfd_send_signal(pidfd, SIGKILL, NULL, 0))
			ksft_exit_fail_msg("%s - pidfd_send_signal failed\n",
					strerror(errno));

		/* Wait for the death notification */
		fds.fd = pidfd;
		nevents = poll(&fds, 1, -1);

		/* Check for error conditions */
		if (nevents < 0)
			ksft_exit_fail_msg("%s - poll failed\n",
					strerror(errno));

		if (nevents != 1)
			ksft_exit_fail_msg("unexpected poll result: %d\n",
					nevents);

		if (!(fds.revents & POLLIN))
			ksft_exit_fail_msg(
				"unexpected event type received: 0x%x\n",
				fds.revents);

		if (timeout)
			ksft_exit_fail_msg(
				"death notification wait timeout\n");

		close(pidfd);
		/* Wait for child to prevent zombies */
		if (waitpid(child_pid, NULL, 0) < 0)
			ksft_exit_fail_msg("%s - waitpid failed\n",
					strerror(errno));

	}

	ksft_test_result_pass("pidfd poll test: pass\n");
	ksft_exit_pass();
}
