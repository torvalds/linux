// SPDX-License-Identifier: GPL-2.0

/* kselftest for acct() system call
 *  The acct() system call enables or disables process accounting.
 */

#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <sys/wait.h>

#include "../kselftest.h"

int main(void)
{
	char filename[] = "process_log";
	FILE *fp;
	pid_t child_pid;
	int sz;

	// Setting up kselftest framework
	ksft_print_header();
	ksft_set_plan(1);

	// Check if test is run a root
	if (geteuid()) {
		ksft_test_result_skip("This test needs root to run!\n");
		return 1;
	}

	// Create file to log closed processes
	fp = fopen(filename, "w");

	if (!fp) {
		ksft_test_result_error("%s.\n", strerror(errno));
		ksft_finished();
		return 1;
	}

	acct(filename);

	// Handle error conditions
	if (errno) {
		ksft_test_result_error("%s.\n", strerror(errno));
		fclose(fp);
		ksft_finished();
		return 1;
	}

	// Create child process and wait for it to terminate.

	child_pid = fork();

	if (child_pid < 0) {
		ksft_test_result_error("Creating a child process to log failed\n");
		acct(NULL);
		return 1;
	} else if (child_pid > 0) {
		wait(NULL);
		fseek(fp, 0L, SEEK_END);
		sz = ftell(fp);

		acct(NULL);

		if (sz <= 0) {
			ksft_test_result_fail("Terminated child process not logged\n");
			ksft_exit_fail();
			return 1;
		}

		ksft_test_result_pass("Successfully logged terminated process.\n");
		fclose(fp);
		ksft_exit_pass();
		return 0;
	}

	return 1;
}
