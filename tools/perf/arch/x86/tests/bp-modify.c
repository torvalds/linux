// SPDX-License-Identifier: GPL-2.0
#include <linux/compiler.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/user.h>
#include <syscall.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ptrace.h>
#include <asm/ptrace.h>
#include <errno.h>
#include "debug.h"
#include "tests/tests.h"
#include "arch-tests.h"

static noinline int bp_1(void)
{
	pr_debug("in %s\n", __func__);
	return 0;
}

static noinline int bp_2(void)
{
	pr_debug("in %s\n", __func__);
	return 0;
}

static int spawn_child(void)
{
	int child = fork();

	if (child == 0) {
		/*
		 * The child sets itself for as tracee and
		 * waits in signal for parent to trace it,
		 * then it calls bp_1 and quits.
		 */
		int err = ptrace(PTRACE_TRACEME, 0, NULL, NULL);

		if (err) {
			pr_debug("failed to PTRACE_TRACEME\n");
			exit(1);
		}

		raise(SIGCONT);
		bp_1();
		exit(0);
	}

	return child;
}

/*
 * This tests creates HW breakpoint, tries to
 * change it and checks it was properly changed.
 */
static int bp_modify1(void)
{
	pid_t child;
	int status;
	unsigned long rip = 0, dr7 = 1;

	child = spawn_child();

	waitpid(child, &status, 0);
	if (WIFEXITED(status)) {
		pr_debug("tracee exited prematurely 1\n");
		return TEST_FAIL;
	}

	/*
	 * The parent does following steps:
	 *  - creates a new breakpoint (id 0) for bp_2 function
	 *  - changes that breakpoint to bp_1 function
	 *  - waits for the breakpoint to hit and checks
	 *    it has proper rip of bp_1 function
	 *  - detaches the child
	 */
	if (ptrace(PTRACE_POKEUSER, child,
		   offsetof(struct user, u_debugreg[0]), bp_2)) {
		pr_debug("failed to set breakpoint, 1st time: %s\n",
			 strerror(errno));
		goto out;
	}

	if (ptrace(PTRACE_POKEUSER, child,
		   offsetof(struct user, u_debugreg[0]), bp_1)) {
		pr_debug("failed to set breakpoint, 2nd time: %s\n",
			 strerror(errno));
		goto out;
	}

	if (ptrace(PTRACE_POKEUSER, child,
		   offsetof(struct user, u_debugreg[7]), dr7)) {
		pr_debug("failed to set dr7: %s\n", strerror(errno));
		goto out;
	}

	if (ptrace(PTRACE_CONT, child, NULL, NULL)) {
		pr_debug("failed to PTRACE_CONT: %s\n", strerror(errno));
		goto out;
	}

	waitpid(child, &status, 0);
	if (WIFEXITED(status)) {
		pr_debug("tracee exited prematurely 2\n");
		return TEST_FAIL;
	}

	rip = ptrace(PTRACE_PEEKUSER, child,
		     offsetof(struct user_regs_struct, rip), NULL);
	if (rip == (unsigned long) -1) {
		pr_debug("failed to PTRACE_PEEKUSER: %s\n",
			 strerror(errno));
		goto out;
	}

	pr_debug("rip %lx, bp_1 %p\n", rip, bp_1);

out:
	if (ptrace(PTRACE_DETACH, child, NULL, NULL)) {
		pr_debug("failed to PTRACE_DETACH: %s", strerror(errno));
		return TEST_FAIL;
	}

	return rip == (unsigned long) bp_1 ? TEST_OK : TEST_FAIL;
}

/*
 * This tests creates HW breakpoint, tries to
 * change it to bogus value and checks the original
 * breakpoint is hit.
 */
static int bp_modify2(void)
{
	pid_t child;
	int status;
	unsigned long rip = 0, dr7 = 1;

	child = spawn_child();

	waitpid(child, &status, 0);
	if (WIFEXITED(status)) {
		pr_debug("tracee exited prematurely 1\n");
		return TEST_FAIL;
	}

	/*
	 * The parent does following steps:
	 *  - creates a new breakpoint (id 0) for bp_1 function
	 *  - tries to change that breakpoint to (-1) address
	 *  - waits for the breakpoint to hit and checks
	 *    it has proper rip of bp_1 function
	 *  - detaches the child
	 */
	if (ptrace(PTRACE_POKEUSER, child,
		   offsetof(struct user, u_debugreg[0]), bp_1)) {
		pr_debug("failed to set breakpoint: %s\n",
			 strerror(errno));
		goto out;
	}

	if (ptrace(PTRACE_POKEUSER, child,
		   offsetof(struct user, u_debugreg[7]), dr7)) {
		pr_debug("failed to set dr7: %s\n", strerror(errno));
		goto out;
	}

	if (!ptrace(PTRACE_POKEUSER, child,
		   offsetof(struct user, u_debugreg[0]), (unsigned long) (-1))) {
		pr_debug("failed, breakpoint set to bogus address\n");
		goto out;
	}

	if (ptrace(PTRACE_CONT, child, NULL, NULL)) {
		pr_debug("failed to PTRACE_CONT: %s\n", strerror(errno));
		goto out;
	}

	waitpid(child, &status, 0);
	if (WIFEXITED(status)) {
		pr_debug("tracee exited prematurely 2\n");
		return TEST_FAIL;
	}

	rip = ptrace(PTRACE_PEEKUSER, child,
		     offsetof(struct user_regs_struct, rip), NULL);
	if (rip == (unsigned long) -1) {
		pr_debug("failed to PTRACE_PEEKUSER: %s\n",
			 strerror(errno));
		goto out;
	}

	pr_debug("rip %lx, bp_1 %p\n", rip, bp_1);

out:
	if (ptrace(PTRACE_DETACH, child, NULL, NULL)) {
		pr_debug("failed to PTRACE_DETACH: %s", strerror(errno));
		return TEST_FAIL;
	}

	return rip == (unsigned long) bp_1 ? TEST_OK : TEST_FAIL;
}

int test__bp_modify(struct test_suite *test __maybe_unused,
		    int subtest __maybe_unused)
{
	TEST_ASSERT_VAL("modify test 1 failed\n", !bp_modify1());
	TEST_ASSERT_VAL("modify test 2 failed\n", !bp_modify2());

	return 0;
}
