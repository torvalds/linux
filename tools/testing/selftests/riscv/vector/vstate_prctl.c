// SPDX-License-Identifier: GPL-2.0-only
#include <sys/prctl.h>
#include <unistd.h>
#include <errno.h>
#include <sys/wait.h>

#include "../hwprobe/hwprobe.h"
#include "../../kselftest.h"

#define NEXT_PROGRAM "./vstate_exec_nolibc"
static int launch_test(int test_inherit)
{
	char *exec_argv[3], *exec_envp[1];
	int rc, pid, status;

	pid = fork();
	if (pid < 0) {
		ksft_test_result_fail("fork failed %d", pid);
		return -1;
	}

	if (!pid) {
		exec_argv[0] = NEXT_PROGRAM;
		exec_argv[1] = test_inherit != 0 ? "x" : NULL;
		exec_argv[2] = NULL;
		exec_envp[0] = NULL;
		/* launch the program again to check inherit */
		rc = execve(NEXT_PROGRAM, exec_argv, exec_envp);
		if (rc) {
			perror("execve");
			ksft_test_result_fail("child execve failed %d\n", rc);
			exit(-1);
		}
	}

	rc = waitpid(-1, &status, 0);
	if (rc < 0) {
		ksft_test_result_fail("waitpid failed\n");
		return -3;
	}

	if ((WIFEXITED(status) && WEXITSTATUS(status) == -1) ||
	    WIFSIGNALED(status)) {
		ksft_test_result_fail("child exited abnormally\n");
		return -4;
	}

	return WEXITSTATUS(status);
}

int test_and_compare_child(long provided, long expected, int inherit)
{
	int rc;

	rc = prctl(PR_RISCV_V_SET_CONTROL, provided);
	if (rc != 0) {
		ksft_test_result_fail("prctl with provided arg %lx failed with code %d\n",
				      provided, rc);
		return -1;
	}
	rc = launch_test(inherit);
	if (rc != expected) {
		ksft_test_result_fail("Test failed, check %d != %ld\n", rc,
				      expected);
		return -2;
	}
	return 0;
}

#define PR_RISCV_V_VSTATE_CTRL_CUR_SHIFT	0
#define PR_RISCV_V_VSTATE_CTRL_NEXT_SHIFT	2

int main(void)
{
	struct riscv_hwprobe pair;
	long flag, expected;
	long rc;

	pair.key = RISCV_HWPROBE_KEY_IMA_EXT_0;
	rc = riscv_hwprobe(&pair, 1, 0, NULL, 0);
	if (rc < 0) {
		ksft_test_result_fail("hwprobe() failed with %ld\n", rc);
		return -1;
	}

	if (pair.key != RISCV_HWPROBE_KEY_IMA_EXT_0) {
		ksft_test_result_fail("hwprobe cannot probe RISCV_HWPROBE_KEY_IMA_EXT_0\n");
		return -2;
	}

	if (!(pair.value & RISCV_HWPROBE_IMA_V)) {
		rc = prctl(PR_RISCV_V_GET_CONTROL);
		if (rc != -1 || errno != EINVAL) {
			ksft_test_result_fail("GET_CONTROL should fail on kernel/hw without V\n");
			return -3;
		}

		rc = prctl(PR_RISCV_V_SET_CONTROL, PR_RISCV_V_VSTATE_CTRL_ON);
		if (rc != -1 || errno != EINVAL) {
			ksft_test_result_fail("GET_CONTROL should fail on kernel/hw without V\n");
			return -4;
		}

		ksft_test_result_skip("Vector not supported\n");
		return 0;
	}

	flag = PR_RISCV_V_VSTATE_CTRL_ON;
	rc = prctl(PR_RISCV_V_SET_CONTROL, flag);
	if (rc != 0) {
		ksft_test_result_fail("Enabling V for current should always success\n");
		return -5;
	}

	flag = PR_RISCV_V_VSTATE_CTRL_OFF;
	rc = prctl(PR_RISCV_V_SET_CONTROL, flag);
	if (rc != -1 || errno != EPERM) {
		ksft_test_result_fail("Disabling current's V alive must fail with EPERM(%d)\n",
				      errno);
		return -5;
	}

	/* Turn on next's vector explicitly and test */
	flag = PR_RISCV_V_VSTATE_CTRL_ON << PR_RISCV_V_VSTATE_CTRL_NEXT_SHIFT;
	if (test_and_compare_child(flag, PR_RISCV_V_VSTATE_CTRL_ON, 0))
		return -6;

	/* Turn off next's vector explicitly and test */
	flag = PR_RISCV_V_VSTATE_CTRL_OFF << PR_RISCV_V_VSTATE_CTRL_NEXT_SHIFT;
	if (test_and_compare_child(flag, PR_RISCV_V_VSTATE_CTRL_OFF, 0))
		return -7;

	/* Turn on next's vector explicitly and test inherit */
	flag = PR_RISCV_V_VSTATE_CTRL_ON << PR_RISCV_V_VSTATE_CTRL_NEXT_SHIFT;
	flag |= PR_RISCV_V_VSTATE_CTRL_INHERIT;
	expected = flag | PR_RISCV_V_VSTATE_CTRL_ON;
	if (test_and_compare_child(flag, expected, 0))
		return -8;

	if (test_and_compare_child(flag, expected, 1))
		return -9;

	/* Turn off next's vector explicitly and test inherit */
	flag = PR_RISCV_V_VSTATE_CTRL_OFF << PR_RISCV_V_VSTATE_CTRL_NEXT_SHIFT;
	flag |= PR_RISCV_V_VSTATE_CTRL_INHERIT;
	expected = flag | PR_RISCV_V_VSTATE_CTRL_OFF;
	if (test_and_compare_child(flag, expected, 0))
		return -10;

	if (test_and_compare_child(flag, expected, 1))
		return -11;

	/* arguments should fail with EINVAL */
	rc = prctl(PR_RISCV_V_SET_CONTROL, 0xff0);
	if (rc != -1 || errno != EINVAL) {
		ksft_test_result_fail("Undefined control argument should return EINVAL\n");
		return -12;
	}

	rc = prctl(PR_RISCV_V_SET_CONTROL, 0x3);
	if (rc != -1 || errno != EINVAL) {
		ksft_test_result_fail("Undefined control argument should return EINVAL\n");
		return -12;
	}

	rc = prctl(PR_RISCV_V_SET_CONTROL, 0xc);
	if (rc != -1 || errno != EINVAL) {
		ksft_test_result_fail("Undefined control argument should return EINVAL\n");
		return -12;
	}

	rc = prctl(PR_RISCV_V_SET_CONTROL, 0xc);
	if (rc != -1 || errno != EINVAL) {
		ksft_test_result_fail("Undefined control argument should return EINVAL\n");
		return -12;
	}

	ksft_test_result_pass("tests for riscv_v_vstate_ctrl pass\n");
	ksft_exit_pass();
	return 0;
}
