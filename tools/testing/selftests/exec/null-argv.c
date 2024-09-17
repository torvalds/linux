// SPDX-License-Identifier: GPL-2.0-only
/* Test that empty argvs are swapped out for a single empty string. */
#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>

#include "../kselftest.h"

#define FORK(exec)				\
do {						\
	pid = fork();				\
	if (pid == 0) {				\
		/* Child */			\
		exec; /* Some kind of exec */	\
		perror("# " #exec);		\
		return 1;			\
	}					\
	check_result(pid, #exec);		\
} while (0)

void check_result(pid_t pid, const char *msg)
{
	int wstatus;

	if (pid == (pid_t)-1) {
		perror("# fork");
		ksft_test_result_fail("fork failed: %s\n", msg);
		return;
	}
	if (waitpid(pid, &wstatus, 0) < 0) {
		perror("# waitpid");
		ksft_test_result_fail("waitpid failed: %s\n", msg);
		return;
	}
	if (!WIFEXITED(wstatus)) {
		ksft_test_result_fail("child did not exit: %s\n", msg);
		return;
	}
	if (WEXITSTATUS(wstatus) != 0) {
		ksft_test_result_fail("non-zero exit: %s\n", msg);
		return;
	}
	ksft_test_result_pass("%s\n", msg);
}

int main(int argc, char *argv[], char *envp[])
{
	pid_t pid;
	static char * const args[] = { NULL };
	static char * const str[] = { "", NULL };

	/* argc counting checks */
	if (argc < 1) {
		fprintf(stderr, "# FAIL: saw argc == 0 (old kernel?)\n");
		return 1;
	}
	if (argc != 1) {
		fprintf(stderr, "# FAIL: unknown argc (%d)\n", argc);
		return 1;
	}
	if (argv[0][0] == '\0') {
		/* Good, we found a NULL terminated string at argv[0]! */
		return 0;
	}

	/* Test runner. */
	ksft_print_header();
	ksft_set_plan(5);

	FORK(execve(argv[0], str, NULL));
	FORK(execve(argv[0], NULL, NULL));
	FORK(execve(argv[0], NULL, envp));
	FORK(execve(argv[0], args, NULL));
	FORK(execve(argv[0], args, envp));

	ksft_exit(ksft_cnt.ksft_pass == ksft_plan);
}
