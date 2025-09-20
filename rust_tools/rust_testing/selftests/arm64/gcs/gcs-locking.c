// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2023 ARM Limited.
 *
 * Tests for GCS mode locking.  These tests rely on both having GCS
 * unconfigured on entry and on the kselftest harness running each
 * test in a fork()ed process which will have it's own mode.
 */

#include <limits.h>

#include <sys/auxv.h>
#include <sys/prctl.h>

#include <asm/hwcap.h>

#include "kselftest_harness.h"

#include "gcs-util.h"

#define my_syscall2(num, arg1, arg2)                                          \
({                                                                            \
	register long _num  __asm__ ("x8") = (num);                           \
	register long _arg1 __asm__ ("x0") = (long)(arg1);                    \
	register long _arg2 __asm__ ("x1") = (long)(arg2);                    \
	register long _arg3 __asm__ ("x2") = 0;                               \
	register long _arg4 __asm__ ("x3") = 0;                               \
	register long _arg5 __asm__ ("x4") = 0;                               \
	                                                                      \
	__asm__  volatile (                                                   \
		"svc #0\n"                                                    \
		: "=r"(_arg1)                                                 \
		: "r"(_arg1), "r"(_arg2),                                     \
		  "r"(_arg3), "r"(_arg4),                                     \
		  "r"(_arg5), "r"(_num)					      \
		: "memory", "cc"                                              \
	);                                                                    \
	_arg1;                                                                \
})

/* No mode bits are rejected for locking */
TEST(lock_all_modes)
{
	int ret;

	ret = prctl(PR_LOCK_SHADOW_STACK_STATUS, ULONG_MAX, 0, 0, 0);
	ASSERT_EQ(ret, 0);
}

FIXTURE(valid_modes)
{
};

FIXTURE_VARIANT(valid_modes)
{
	unsigned long mode;
};

FIXTURE_VARIANT_ADD(valid_modes, enable)
{
	.mode = PR_SHADOW_STACK_ENABLE,
};

FIXTURE_VARIANT_ADD(valid_modes, enable_write)
{
	.mode = PR_SHADOW_STACK_ENABLE | PR_SHADOW_STACK_WRITE,
};

FIXTURE_VARIANT_ADD(valid_modes, enable_push)
{
	.mode = PR_SHADOW_STACK_ENABLE | PR_SHADOW_STACK_PUSH,
};

FIXTURE_VARIANT_ADD(valid_modes, enable_write_push)
{
	.mode = PR_SHADOW_STACK_ENABLE | PR_SHADOW_STACK_WRITE |
		PR_SHADOW_STACK_PUSH,
};

FIXTURE_SETUP(valid_modes)
{
}

FIXTURE_TEARDOWN(valid_modes)
{
}

/* We can set the mode at all */
TEST_F(valid_modes, set)
{
	int ret;

	ret = my_syscall2(__NR_prctl, PR_SET_SHADOW_STACK_STATUS,
			  variant->mode);
	ASSERT_EQ(ret, 0);

	_exit(0);
}

/* Enabling, locking then disabling is rejected */
TEST_F(valid_modes, enable_lock_disable)
{
	unsigned long mode;
	int ret;

	ret = my_syscall2(__NR_prctl, PR_SET_SHADOW_STACK_STATUS,
			  variant->mode);
	ASSERT_EQ(ret, 0);

	ret = prctl(PR_GET_SHADOW_STACK_STATUS, &mode, 0, 0, 0);
	ASSERT_EQ(ret, 0);
	ASSERT_EQ(mode, variant->mode);

	ret = prctl(PR_LOCK_SHADOW_STACK_STATUS, variant->mode, 0, 0, 0);
	ASSERT_EQ(ret, 0);

	ret = my_syscall2(__NR_prctl, PR_SET_SHADOW_STACK_STATUS, 0);
	ASSERT_EQ(ret, -EBUSY);

	_exit(0);
}

/* Locking then enabling is rejected */
TEST_F(valid_modes, lock_enable)
{
	unsigned long mode;
	int ret;

	ret = prctl(PR_LOCK_SHADOW_STACK_STATUS, variant->mode, 0, 0, 0);
	ASSERT_EQ(ret, 0);

	ret = my_syscall2(__NR_prctl, PR_SET_SHADOW_STACK_STATUS,
			  variant->mode);
	ASSERT_EQ(ret, -EBUSY);

	ret = prctl(PR_GET_SHADOW_STACK_STATUS, &mode, 0, 0, 0);
	ASSERT_EQ(ret, 0);
	ASSERT_EQ(mode, 0);

	_exit(0);
}

/* Locking then changing other modes is fine */
TEST_F(valid_modes, lock_enable_disable_others)
{
	unsigned long mode;
	int ret;

	ret = my_syscall2(__NR_prctl, PR_SET_SHADOW_STACK_STATUS,
			  variant->mode);
	ASSERT_EQ(ret, 0);

	ret = prctl(PR_GET_SHADOW_STACK_STATUS, &mode, 0, 0, 0);
	ASSERT_EQ(ret, 0);
	ASSERT_EQ(mode, variant->mode);

	ret = prctl(PR_LOCK_SHADOW_STACK_STATUS, variant->mode, 0, 0, 0);
	ASSERT_EQ(ret, 0);

	ret = my_syscall2(__NR_prctl, PR_SET_SHADOW_STACK_STATUS,
			  PR_SHADOW_STACK_ALL_MODES);
	ASSERT_EQ(ret, 0);

	ret = prctl(PR_GET_SHADOW_STACK_STATUS, &mode, 0, 0, 0);
	ASSERT_EQ(ret, 0);
	ASSERT_EQ(mode, PR_SHADOW_STACK_ALL_MODES);


	ret = my_syscall2(__NR_prctl, PR_SET_SHADOW_STACK_STATUS,
			  variant->mode);
	ASSERT_EQ(ret, 0);

	ret = prctl(PR_GET_SHADOW_STACK_STATUS, &mode, 0, 0, 0);
	ASSERT_EQ(ret, 0);
	ASSERT_EQ(mode, variant->mode);

	_exit(0);
}

int main(int argc, char **argv)
{
	unsigned long mode;
	int ret;

	if (!(getauxval(AT_HWCAP) & HWCAP_GCS))
		ksft_exit_skip("SKIP GCS not supported\n");

	ret = prctl(PR_GET_SHADOW_STACK_STATUS, &mode, 0, 0, 0);
	if (ret) {
		ksft_print_msg("Failed to read GCS state: %d\n", ret);
		return EXIT_FAILURE;
	}

	if (mode & PR_SHADOW_STACK_ENABLE) {
		ksft_print_msg("GCS was enabled, test unsupported\n");
		return KSFT_SKIP;
	}

	return test_harness_run(argc, argv);
}
