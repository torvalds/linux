// SPDX-License-Identifier: GPL-2.0-only

#include <linux/sched.h>
#include <linux/wait.h>

#include "kselftest.h"

#define SYS_TPIDR2 "S3_3_C13_C0_5"

#define EXPECTED_TESTS 5

static void set_tpidr2(uint64_t val)
{
	asm volatile (
		"msr	" SYS_TPIDR2 ", %0\n"
		:
		: "r"(val)
		: "cc");
}

static uint64_t get_tpidr2(void)
{
	uint64_t val;

	asm volatile (
		"mrs	%0, " SYS_TPIDR2 "\n"
		: "=r"(val)
		:
		: "cc");

	return val;
}

/* Processes should start with TPIDR2 == 0 */
static int default_value(void)
{
	return get_tpidr2() == 0;
}

/* If we set TPIDR2 we should read that value */
static int write_read(void)
{
	set_tpidr2(getpid());

	return getpid() == get_tpidr2();
}

/* If we set a value we should read the same value after scheduling out */
static int write_sleep_read(void)
{
	set_tpidr2(getpid());

	msleep(100);

	return getpid() == get_tpidr2();
}

/*
 * If we fork the value in the parent should be unchanged and the
 * child should start with the same value and be able to set its own
 * value.
 */
static int write_fork_read(void)
{
	pid_t newpid, waiting, oldpid;
	int status;

	set_tpidr2(getpid());

	oldpid = getpid();
	newpid = fork();
	if (newpid == 0) {
		/* In child */
		if (get_tpidr2() != oldpid) {
			ksft_print_msg("TPIDR2 changed in child: %llx\n",
				       get_tpidr2());
			exit(0);
		}

		set_tpidr2(getpid());
		if (get_tpidr2() == getpid()) {
			exit(1);
		} else {
			ksft_print_msg("Failed to set TPIDR2 in child\n");
			exit(0);
		}
	}
	if (newpid < 0) {
		ksft_print_msg("fork() failed: %d\n", newpid);
		return 0;
	}

	for (;;) {
		waiting = waitpid(newpid, &status, 0);

		if (waiting < 0) {
			if (errno == EINTR)
				continue;
			ksft_print_msg("waitpid() failed: %d\n", errno);
			return 0;
		}
		if (waiting != newpid) {
			ksft_print_msg("waitpid() returned wrong PID: %d != %d\n",
				       waiting, newpid);
			return 0;
		}

		if (!WIFEXITED(status)) {
			ksft_print_msg("child did not exit\n");
			return 0;
		}

		if (getpid() != get_tpidr2()) {
			ksft_print_msg("TPIDR2 corrupted in parent\n");
			return 0;
		}

		return WEXITSTATUS(status);
	}
}

/*
 * sys_clone() has a lot of per architecture variation so just define
 * it here rather than adding it to nolibc, plus the raw API is a
 * little more convenient for this test.
 */
static int sys_clone(unsigned long clone_flags, unsigned long newsp,
		     int *parent_tidptr, unsigned long tls,
		     int *child_tidptr)
{
	return my_syscall5(__NR_clone, clone_flags, newsp, parent_tidptr, tls,
			   child_tidptr);
}

#define __STACK_SIZE (8 * 1024 * 1024)

/*
 * If we clone with CLONE_VM then the value in the parent should
 * be unchanged and the child should start with zero and be able to
 * set its own value.
 */
static int write_clone_read(void)
{
	int parent_tid, child_tid;
	pid_t parent, waiting;
	int ret, status;
	void *stack;

	parent = getpid();
	set_tpidr2(parent);

	stack = malloc(__STACK_SIZE);
	if (!stack) {
		ksft_print_msg("malloc() failed\n");
		return 0;
	}

	ret = sys_clone(CLONE_VM, (unsigned long)stack + __STACK_SIZE,
			&parent_tid, 0, &child_tid);
	if (ret == -1) {
		ksft_print_msg("clone() failed: %d\n", errno);
		return 0;
	}

	if (ret == 0) {
		/* In child */
		if (get_tpidr2() != 0) {
			ksft_print_msg("TPIDR2 non-zero in child: %llx\n",
				       get_tpidr2());
			exit(0);
		}

		if (gettid() == 0)
			ksft_print_msg("Child TID==0\n");
		set_tpidr2(gettid());
		if (get_tpidr2() == gettid()) {
			exit(1);
		} else {
			ksft_print_msg("Failed to set TPIDR2 in child\n");
			exit(0);
		}
	}

	for (;;) {
		waiting = waitpid(ret, &status, __WCLONE);

		if (waiting < 0) {
			if (errno == EINTR)
				continue;
			ksft_print_msg("waitpid() failed: %d\n", errno);
			return 0;
		}
		if (waiting != ret) {
			ksft_print_msg("waitpid() returned wrong PID %d\n",
				       waiting);
			return 0;
		}

		if (!WIFEXITED(status)) {
			ksft_print_msg("child did not exit\n");
			return 0;
		}

		if (parent != get_tpidr2()) {
			ksft_print_msg("TPIDR2 corrupted in parent\n");
			return 0;
		}

		return WEXITSTATUS(status);
	}
}

int main(int argc, char **argv)
{
	int ret;

	ksft_print_header();
	ksft_set_plan(5);

	ksft_print_msg("PID: %d\n", getpid());

	/*
	 * This test is run with nolibc which doesn't support hwcap and
	 * it's probably disproportionate to implement so instead check
	 * for the default vector length configuration in /proc.
	 */
	ret = open("/proc/sys/abi/sme_default_vector_length", O_RDONLY, 0);
	if (ret >= 0) {
		ksft_test_result(default_value(), "default_value\n");
		ksft_test_result(write_read(), "write_read\n");
		ksft_test_result(write_sleep_read(), "write_sleep_read\n");
		ksft_test_result(write_fork_read(), "write_fork_read\n");
		ksft_test_result(write_clone_read(), "write_clone_read\n");

	} else {
		ksft_print_msg("SME support not present\n");

		ksft_test_result_skip("default_value\n");
		ksft_test_result_skip("write_read\n");
		ksft_test_result_skip("write_sleep_read\n");
		ksft_test_result_skip("write_fork_read\n");
		ksft_test_result_skip("write_clone_read\n");
	}

	ksft_finished();
}
