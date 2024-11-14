// SPDX-License-Identifier: GPL-2.0-only

#include "../hwprobe/hwprobe.h"
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/wait.h>

bool is_vector_supported(void)
{
	struct riscv_hwprobe pair;

	pair.key = RISCV_HWPROBE_KEY_IMA_EXT_0;
	riscv_hwprobe(&pair, 1, 0, NULL, 0);
	return pair.value & RISCV_HWPROBE_EXT_ZVE32X;
}

int launch_test(char *next_program, int test_inherit)
{
	char *exec_argv[3], *exec_envp[1];
	int rc, pid, status;

	pid = fork();
	if (pid < 0) {
		printf("fork failed %d", pid);
		return -1;
	}

	if (!pid) {
		exec_argv[0] = next_program;
		exec_argv[1] = test_inherit != 0 ? "x" : NULL;
		exec_argv[2] = NULL;
		exec_envp[0] = NULL;
		/* launch the program again to check inherit */
		rc = execve(next_program, exec_argv, exec_envp);
		if (rc) {
			perror("execve");
			printf("child execve failed %d\n", rc);
			exit(-1);
		}
	}

	rc = waitpid(-1, &status, 0);
	if (rc < 0) {
		printf("waitpid failed\n");
		return -3;
	}

	if ((WIFEXITED(status) && WEXITSTATUS(status) == -1) ||
	    WIFSIGNALED(status)) {
		printf("child exited abnormally\n");
		return -4;
	}

	return WEXITSTATUS(status);
}
