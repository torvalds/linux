// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2023 ARM Limited.
 */

#include <limits.h>
#include <stdbool.h>

#include <linux/prctl.h>

#include <sys/mman.h>
#include <asm/mman.h>
#include <linux/sched.h>

#include "kselftest.h"
#include "gcs-util.h"

/* nolibc doesn't have sysconf(), just hard code the maximum */
static size_t page_size = 65536;

static  __attribute__((noinline)) void valid_gcs_function(void)
{
	/* Do something the compiler can't optimise out */
	my_syscall1(__NR_prctl, PR_SVE_GET_VL);
}

static inline int gcs_set_status(unsigned long mode)
{
	bool enabling = mode & PR_SHADOW_STACK_ENABLE;
	int ret;
	unsigned long new_mode;

	/*
	 * The prctl takes 1 argument but we need to ensure that the
	 * other 3 values passed in registers to the syscall are zero
	 * since the kernel validates them.
	 */
	ret = my_syscall5(__NR_prctl, PR_SET_SHADOW_STACK_STATUS, mode,
			  0, 0, 0);

	if (ret == 0) {
		ret = my_syscall5(__NR_prctl, PR_GET_SHADOW_STACK_STATUS,
				  &new_mode, 0, 0, 0);
		if (ret == 0) {
			if (new_mode != mode) {
				ksft_print_msg("Mode set to %lx not %lx\n",
					       new_mode, mode);
				ret = -EINVAL;
			}
		} else {
			ksft_print_msg("Failed to validate mode: %d\n", ret);
		}

		if (enabling != chkfeat_gcs()) {
			ksft_print_msg("%senabled by prctl but %senabled in CHKFEAT\n",
				       enabling ? "" : "not ",
				       chkfeat_gcs() ? "" : "not ");
			ret = -EINVAL;
		}
	}

	return ret;
}

/* Try to read the status */
static bool read_status(void)
{
	unsigned long state;
	int ret;

	ret = my_syscall5(__NR_prctl, PR_GET_SHADOW_STACK_STATUS,
			  &state, 0, 0, 0);
	if (ret != 0) {
		ksft_print_msg("Failed to read state: %d\n", ret);
		return false;
	}

	return state & PR_SHADOW_STACK_ENABLE;
}

/* Just a straight enable */
static bool base_enable(void)
{
	int ret;

	ret = gcs_set_status(PR_SHADOW_STACK_ENABLE);
	if (ret) {
		ksft_print_msg("PR_SHADOW_STACK_ENABLE failed %d\n", ret);
		return false;
	}

	return true;
}

/* Check we can read GCSPR_EL0 when GCS is enabled */
static bool read_gcspr_el0(void)
{
	unsigned long *gcspr_el0;

	ksft_print_msg("GET GCSPR\n");
	gcspr_el0 = get_gcspr();
	ksft_print_msg("GCSPR_EL0 is %p\n", gcspr_el0);

	return true;
}

/* Also allow writes to stack */
static bool enable_writeable(void)
{
	int ret;

	ret = gcs_set_status(PR_SHADOW_STACK_ENABLE | PR_SHADOW_STACK_WRITE);
	if (ret) {
		ksft_print_msg("PR_SHADOW_STACK_ENABLE writeable failed: %d\n", ret);
		return false;
	}

	ret = gcs_set_status(PR_SHADOW_STACK_ENABLE);
	if (ret) {
		ksft_print_msg("failed to restore plain enable %d\n", ret);
		return false;
	}

	return true;
}

/* Also allow writes to stack */
static bool enable_push_pop(void)
{
	int ret;

	ret = gcs_set_status(PR_SHADOW_STACK_ENABLE | PR_SHADOW_STACK_PUSH);
	if (ret) {
		ksft_print_msg("PR_SHADOW_STACK_ENABLE with push failed: %d\n",
			       ret);
		return false;
	}

	ret = gcs_set_status(PR_SHADOW_STACK_ENABLE);
	if (ret) {
		ksft_print_msg("failed to restore plain enable %d\n", ret);
		return false;
	}

	return true;
}

/* Enable GCS and allow everything */
static bool enable_all(void)
{
	int ret;

	ret = gcs_set_status(PR_SHADOW_STACK_ENABLE | PR_SHADOW_STACK_PUSH |
			     PR_SHADOW_STACK_WRITE);
	if (ret) {
		ksft_print_msg("PR_SHADOW_STACK_ENABLE with everything failed: %d\n",
			       ret);
		return false;
	}

	ret = gcs_set_status(PR_SHADOW_STACK_ENABLE);
	if (ret) {
		ksft_print_msg("failed to restore plain enable %d\n", ret);
		return false;
	}

	return true;
}

static bool enable_invalid(void)
{
	int ret = gcs_set_status(ULONG_MAX);
	if (ret == 0) {
		ksft_print_msg("GCS_SET_STATUS %lx succeeded\n", ULONG_MAX);
		return false;
	}

	return true;
}

/* Map a GCS */
static bool map_guarded_stack(void)
{
	int ret;
	uint64_t *buf;
	uint64_t expected_cap;
	int elem;
	bool pass = true;

	buf = (void *)my_syscall3(__NR_map_shadow_stack, 0, page_size,
				  SHADOW_STACK_SET_MARKER |
				  SHADOW_STACK_SET_TOKEN);
	if (buf == MAP_FAILED) {
		ksft_print_msg("Failed to map %lu byte GCS: %d\n",
			       page_size, errno);
		return false;
	}
	ksft_print_msg("Mapped GCS at %p-%p\n", buf,
		       (void *)((uint64_t)buf + page_size));

	/* The top of the newly allocated region should be 0 */
	elem = (page_size / sizeof(uint64_t)) - 1;
	if (buf[elem]) {
		ksft_print_msg("Last entry is 0x%llx not 0x0\n", buf[elem]);
		pass = false;
	}

	/* Then a valid cap token */
	elem--;
	expected_cap = ((uint64_t)buf + page_size - 16);
	expected_cap &= GCS_CAP_ADDR_MASK;
	expected_cap |= GCS_CAP_VALID_TOKEN;
	if (buf[elem] != expected_cap) {
		ksft_print_msg("Cap entry is 0x%llx not 0x%llx\n",
			       buf[elem], expected_cap);
		pass = false;
	}
	ksft_print_msg("cap token is 0x%llx\n", buf[elem]);

	/* The rest should be zeros */
	for (elem = 0; elem < page_size / sizeof(uint64_t) - 2; elem++) {
		if (!buf[elem])
			continue;
		ksft_print_msg("GCS slot %d is 0x%llx not 0x0\n",
			       elem, buf[elem]);
		pass = false;
	}

	ret = munmap(buf, page_size);
	if (ret != 0) {
		ksft_print_msg("Failed to unmap %ld byte GCS: %d\n",
			       page_size, errno);
		pass = false;
	}

	return pass;
}

/* A fork()ed process can run */
static bool test_fork(void)
{
	unsigned long child_mode;
	int ret, status;
	pid_t pid;
	bool pass = true;

	pid = fork();
	if (pid == -1) {
		ksft_print_msg("fork() failed: %d\n", errno);
		pass = false;
		goto out;
	}
	if (pid == 0) {
		/* In child, make sure we can call a function, read
		 * the GCS pointer and status and then exit */
		valid_gcs_function();
		get_gcspr();

		ret = my_syscall5(__NR_prctl, PR_GET_SHADOW_STACK_STATUS,
				  &child_mode, 0, 0, 0);
		if (ret == 0 && !(child_mode & PR_SHADOW_STACK_ENABLE)) {
			ksft_print_msg("GCS not enabled in child\n");
			ret = -EINVAL;
		}

		exit(ret);
	}

	/*
	 * In parent, check we can still do function calls then block
	 * for the child.
	 */
	valid_gcs_function();

	ksft_print_msg("Waiting for child %d\n", pid);

	ret = waitpid(pid, &status, 0);
	if (ret == -1) {
		ksft_print_msg("Failed to wait for child: %d\n",
			       errno);
		return false;
	}

	if (!WIFEXITED(status)) {
		ksft_print_msg("Child exited due to signal %d\n",
			       WTERMSIG(status));
		pass = false;
	} else {
		if (WEXITSTATUS(status)) {
			ksft_print_msg("Child exited with status %d\n",
				       WEXITSTATUS(status));
			pass = false;
		}
	}

out:

	return pass;
}

typedef bool (*gcs_test)(void);

static struct {
	char *name;
	gcs_test test;
	bool needs_enable;
} tests[] = {
	{ "read_status", read_status },
	{ "base_enable", base_enable, true },
	{ "read_gcspr_el0", read_gcspr_el0 },
	{ "enable_writeable", enable_writeable, true },
	{ "enable_push_pop", enable_push_pop, true },
	{ "enable_all", enable_all, true },
	{ "enable_invalid", enable_invalid, true },
	{ "map_guarded_stack", map_guarded_stack },
	{ "fork", test_fork },
};

int main(void)
{
	int i, ret;
	unsigned long gcs_mode;

	ksft_print_header();

	/*
	 * We don't have getauxval() with nolibc so treat a failure to
	 * read GCS state as a lack of support and skip.
	 */
	ret = my_syscall5(__NR_prctl, PR_GET_SHADOW_STACK_STATUS,
			  &gcs_mode, 0, 0, 0);
	if (ret != 0)
		ksft_exit_skip("Failed to read GCS state: %d\n", ret);

	if (!(gcs_mode & PR_SHADOW_STACK_ENABLE)) {
		gcs_mode = PR_SHADOW_STACK_ENABLE;
		ret = my_syscall5(__NR_prctl, PR_SET_SHADOW_STACK_STATUS,
				  gcs_mode, 0, 0, 0);
		if (ret != 0)
			ksft_exit_fail_msg("Failed to enable GCS: %d\n", ret);
	}

	ksft_set_plan(ARRAY_SIZE(tests));

	for (i = 0; i < ARRAY_SIZE(tests); i++) {
		ksft_test_result((*tests[i].test)(), "%s\n", tests[i].name);
	}

	/* One last test: disable GCS, we can do this one time */
	my_syscall5(__NR_prctl, PR_SET_SHADOW_STACK_STATUS, 0, 0, 0, 0);
	if (ret != 0)
		ksft_print_msg("Failed to disable GCS: %d\n", ret);

	ksft_finished();

	return 0;
}
