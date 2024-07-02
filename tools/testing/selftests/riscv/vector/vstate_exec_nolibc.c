// SPDX-License-Identifier: GPL-2.0-only

#include <linux/wait.h>

#define THIS_PROGRAM "./vstate_exec_nolibc"

int main(int argc, char **argv)
{
	int rc, pid, status, test_inherit = 0;
	long ctrl, ctrl_c;
	char *exec_argv[2], *exec_envp[2];

	if (argc > 1)
		test_inherit = 1;

	ctrl = my_syscall1(__NR_prctl, PR_RISCV_V_GET_CONTROL);
	if (ctrl < 0) {
		puts("PR_RISCV_V_GET_CONTROL is not supported\n");
		return ctrl;
	}

	if (test_inherit) {
		pid = fork();
		if (pid == -1) {
			puts("fork failed\n");
			exit(-1);
		}

		/* child  */
		if (!pid) {
			exec_argv[0] = THIS_PROGRAM;
			exec_argv[1] = NULL;
			exec_envp[0] = NULL;
			exec_envp[1] = NULL;
			/* launch the program again to check inherit */
			rc = execve(THIS_PROGRAM, exec_argv, exec_envp);
			if (rc) {
				puts("child execve failed\n");
				exit(-1);
			}
		}

	} else {
		pid = fork();
		if (pid == -1) {
			puts("fork failed\n");
			exit(-1);
		}

		if (!pid) {
			rc = my_syscall1(__NR_prctl, PR_RISCV_V_GET_CONTROL);
			if (rc != ctrl) {
				puts("child's vstate_ctrl not equal to parent's\n");
				exit(-1);
			}
			asm volatile (".option push\n\t"
				      ".option arch, +v\n\t"
				      "vsetvli x0, x0, e32, m8, ta, ma\n\t"
				      ".option pop\n\t"
				      );
			exit(ctrl);
		}
	}

	rc = waitpid(-1, &status, 0);

	if (WIFEXITED(status) && WEXITSTATUS(status) == -1) {
		puts("child exited abnormally\n");
		exit(-1);
	}

	if (WIFSIGNALED(status)) {
		if (WTERMSIG(status) != SIGILL) {
			puts("child was terminated by unexpected signal\n");
			exit(-1);
		}

		if ((ctrl & PR_RISCV_V_VSTATE_CTRL_CUR_MASK) != PR_RISCV_V_VSTATE_CTRL_OFF) {
			puts("child signaled by illegal V access but vstate_ctrl is not off\n");
			exit(-1);
		}

		/* child terminated, and its vstate_ctrl is off */
		exit(ctrl);
	}

	ctrl_c = WEXITSTATUS(status);
	if (test_inherit) {
		if (ctrl & PR_RISCV_V_VSTATE_CTRL_INHERIT) {
			if (!(ctrl_c & PR_RISCV_V_VSTATE_CTRL_INHERIT)) {
				puts("parent has inherit bit, but child has not\n");
				exit(-1);
			}
		}
		rc = (ctrl & PR_RISCV_V_VSTATE_CTRL_NEXT_MASK) >> 2;
		if (rc != PR_RISCV_V_VSTATE_CTRL_DEFAULT) {
			if (rc != (ctrl_c & PR_RISCV_V_VSTATE_CTRL_CUR_MASK)) {
				puts("parent's next setting does not equal to child's\n");
				exit(-1);
			}

			if (!(ctrl & PR_RISCV_V_VSTATE_CTRL_INHERIT)) {
				if ((ctrl_c & PR_RISCV_V_VSTATE_CTRL_NEXT_MASK) !=
				    PR_RISCV_V_VSTATE_CTRL_DEFAULT) {
					puts("must clear child's next vstate_ctrl if !inherit\n");
					exit(-1);
				}
			}
		}
	}
	return ctrl;
}
