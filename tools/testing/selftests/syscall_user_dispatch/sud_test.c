// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2020 Collabora Ltd.
 *
 * Test code for syscall user dispatch
 */

#define _GNU_SOURCE
#include <sys/prctl.h>
#include <sys/sysinfo.h>
#include <sys/syscall.h>
#include <signal.h>

#include <asm/unistd.h>
#include "../kselftest_harness.h"

#ifndef PR_SET_SYSCALL_USER_DISPATCH
# define PR_SET_SYSCALL_USER_DISPATCH	59
# define PR_SYS_DISPATCH_OFF	0
# define PR_SYS_DISPATCH_ON	1
# define SYSCALL_DISPATCH_FILTER_ALLOW	0
# define SYSCALL_DISPATCH_FILTER_BLOCK	1
#endif

#ifndef SYS_USER_DISPATCH
# define SYS_USER_DISPATCH	2
#endif

#ifdef __NR_syscalls
# define MAGIC_SYSCALL_1 (__NR_syscalls + 1) /* Bad Linux syscall number */
#else
# define MAGIC_SYSCALL_1 (0xff00)  /* Bad Linux syscall number */
#endif

#define SYSCALL_DISPATCH_ON(x) ((x) = SYSCALL_DISPATCH_FILTER_BLOCK)
#define SYSCALL_DISPATCH_OFF(x) ((x) = SYSCALL_DISPATCH_FILTER_ALLOW)

/* Test Summary:
 *
 * - dispatch_trigger_sigsys: Verify if PR_SET_SYSCALL_USER_DISPATCH is
 *   able to trigger SIGSYS on a syscall.
 *
 * - bad_selector: Test that a bad selector value triggers SIGSYS with
 *   si_errno EINVAL.
 *
 * - bad_prctl_param: Test that the API correctly rejects invalid
 *   parameters on prctl
 *
 * - dispatch_and_return: Test that a syscall is selectively dispatched
 *   to userspace depending on the value of selector.
 *
 * - disable_dispatch: Test that the PR_SYS_DISPATCH_OFF correctly
 *   disables the dispatcher
 *
 * - direct_dispatch_range: Test that a syscall within the allowed range
 *   can bypass the dispatcher.
 */

TEST_SIGNAL(dispatch_trigger_sigsys, SIGSYS)
{
	char sel = SYSCALL_DISPATCH_FILTER_ALLOW;
	struct sysinfo info;
	int ret;

	ret = sysinfo(&info);
	ASSERT_EQ(0, ret);

	ret = prctl(PR_SET_SYSCALL_USER_DISPATCH, PR_SYS_DISPATCH_ON, 0, 0, &sel);
	ASSERT_EQ(0, ret) {
		TH_LOG("Kernel does not support CONFIG_SYSCALL_USER_DISPATCH");
	}

	SYSCALL_DISPATCH_ON(sel);

	sysinfo(&info);

	EXPECT_FALSE(true) {
		TH_LOG("Unreachable!");
	}
}

static void prctl_valid(struct __test_metadata *_metadata,
			unsigned long op, unsigned long off,
			unsigned long size, void *sel)
{
	EXPECT_EQ(0, prctl(PR_SET_SYSCALL_USER_DISPATCH, op, off, size, sel));
}

static void prctl_invalid(struct __test_metadata *_metadata,
			  unsigned long op, unsigned long off,
			  unsigned long size, void *sel, int err)
{
	EXPECT_EQ(-1, prctl(PR_SET_SYSCALL_USER_DISPATCH, op, off, size, sel));
	EXPECT_EQ(err, errno);
}

TEST(bad_prctl_param)
{
	char sel = SYSCALL_DISPATCH_FILTER_ALLOW;
	int op;

	/* Invalid op */
	op = -1;
	prctl_invalid(_metadata, op, 0, 0, &sel, EINVAL);

	/* PR_SYS_DISPATCH_OFF */
	op = PR_SYS_DISPATCH_OFF;

	/* offset != 0 */
	prctl_invalid(_metadata, op, 0x1, 0x0, 0, EINVAL);

	/* len != 0 */
	prctl_invalid(_metadata, op, 0x0, 0xff, 0, EINVAL);

	/* sel != NULL */
	prctl_invalid(_metadata, op, 0x0, 0x0, &sel, EINVAL);

	/* Valid parameter */
	prctl_valid(_metadata, op, 0x0, 0x0, 0x0);

	/* PR_SYS_DISPATCH_ON */
	op = PR_SYS_DISPATCH_ON;

	/* Dispatcher region is bad (offset > 0 && len == 0) */
	prctl_invalid(_metadata, op, 0x1, 0x0, &sel, EINVAL);
	prctl_invalid(_metadata, op, -1L, 0x0, &sel, EINVAL);

	/* Invalid selector */
	prctl_invalid(_metadata, op, 0x0, 0x1, (void *) -1, EFAULT);

	/*
	 * Dispatcher range overflows unsigned long
	 */
	prctl_invalid(_metadata, PR_SYS_DISPATCH_ON, 1, -1L, &sel, EINVAL);

	/*
	 * Allowed range overflows usigned long
	 */
	prctl_invalid(_metadata, PR_SYS_DISPATCH_ON, -1L, 0x1, &sel, EINVAL);
}

/*
 * Use global selector for handle_sigsys tests, to avoid passing
 * selector to signal handler
 */
char glob_sel;
int nr_syscalls_emulated;
int si_code;
int si_errno;

static void handle_sigsys(int sig, siginfo_t *info, void *ucontext)
{
	si_code = info->si_code;
	si_errno = info->si_errno;

	if (info->si_syscall == MAGIC_SYSCALL_1)
		nr_syscalls_emulated++;

	/* In preparation for sigreturn. */
	SYSCALL_DISPATCH_OFF(glob_sel);

	/*
	 * The tests for argument handling assume that `syscall(x) == x`. This
	 * is a NOP on x86 because the syscall number is passed in %rax, which
	 * happens to also be the function ABI return register.  Other
	 * architectures may need to swizzle the arguments around.
	 */
#if defined(__riscv)
/* REG_A7 is not defined in libc headers */
# define REG_A7 (REG_A0 + 7)

	((ucontext_t *)ucontext)->uc_mcontext.__gregs[REG_A0] =
			((ucontext_t *)ucontext)->uc_mcontext.__gregs[REG_A7];
#endif
}

TEST(dispatch_and_return)
{
	long ret;
	struct sigaction act;
	sigset_t mask;

	glob_sel = 0;
	nr_syscalls_emulated = 0;
	si_code = 0;
	si_errno = 0;

	memset(&act, 0, sizeof(act));
	sigemptyset(&mask);

	act.sa_sigaction = handle_sigsys;
	act.sa_flags = SA_SIGINFO;
	act.sa_mask = mask;

	ret = sigaction(SIGSYS, &act, NULL);
	ASSERT_EQ(0, ret);

	/* Make sure selector is good prior to prctl. */
	SYSCALL_DISPATCH_OFF(glob_sel);

	ret = prctl(PR_SET_SYSCALL_USER_DISPATCH, PR_SYS_DISPATCH_ON, 0, 0, &glob_sel);
	ASSERT_EQ(0, ret) {
		TH_LOG("Kernel does not support CONFIG_SYSCALL_USER_DISPATCH");
	}

	/* MAGIC_SYSCALL_1 doesn't exist. */
	SYSCALL_DISPATCH_OFF(glob_sel);
	ret = syscall(MAGIC_SYSCALL_1);
	EXPECT_EQ(-1, ret) {
		TH_LOG("Dispatch triggered unexpectedly");
	}

	/* MAGIC_SYSCALL_1 should be emulated. */
	nr_syscalls_emulated = 0;
	SYSCALL_DISPATCH_ON(glob_sel);

	ret = syscall(MAGIC_SYSCALL_1);
	EXPECT_EQ(MAGIC_SYSCALL_1, ret) {
		TH_LOG("Failed to intercept syscall");
	}
	EXPECT_EQ(1, nr_syscalls_emulated) {
		TH_LOG("Failed to emulate syscall");
	}
	ASSERT_EQ(SYS_USER_DISPATCH, si_code) {
		TH_LOG("Bad si_code in SIGSYS");
	}
	ASSERT_EQ(0, si_errno) {
		TH_LOG("Bad si_errno in SIGSYS");
	}
}

TEST_SIGNAL(bad_selector, SIGSYS)
{
	long ret;
	struct sigaction act;
	sigset_t mask;
	struct sysinfo info;

	glob_sel = SYSCALL_DISPATCH_FILTER_ALLOW;
	nr_syscalls_emulated = 0;
	si_code = 0;
	si_errno = 0;

	memset(&act, 0, sizeof(act));
	sigemptyset(&mask);

	act.sa_sigaction = handle_sigsys;
	act.sa_flags = SA_SIGINFO;
	act.sa_mask = mask;

	ret = sigaction(SIGSYS, &act, NULL);
	ASSERT_EQ(0, ret);

	/* Make sure selector is good prior to prctl. */
	SYSCALL_DISPATCH_OFF(glob_sel);

	ret = prctl(PR_SET_SYSCALL_USER_DISPATCH, PR_SYS_DISPATCH_ON, 0, 0, &glob_sel);
	ASSERT_EQ(0, ret) {
		TH_LOG("Kernel does not support CONFIG_SYSCALL_USER_DISPATCH");
	}

	glob_sel = -1;

	sysinfo(&info);

	/* Even though it is ready to catch SIGSYS, the signal is
	 * supposed to be uncatchable.
	 */

	EXPECT_FALSE(true) {
		TH_LOG("Unreachable!");
	}
}

TEST(disable_dispatch)
{
	int ret;
	struct sysinfo info;
	char sel = 0;

	ret = prctl(PR_SET_SYSCALL_USER_DISPATCH, PR_SYS_DISPATCH_ON, 0, 0, &sel);
	ASSERT_EQ(0, ret) {
		TH_LOG("Kernel does not support CONFIG_SYSCALL_USER_DISPATCH");
	}

	/* MAGIC_SYSCALL_1 doesn't exist. */
	SYSCALL_DISPATCH_OFF(glob_sel);

	ret = prctl(PR_SET_SYSCALL_USER_DISPATCH, PR_SYS_DISPATCH_OFF, 0, 0, 0);
	EXPECT_EQ(0, ret) {
		TH_LOG("Failed to unset syscall user dispatch");
	}

	/* Shouldn't have any effect... */
	SYSCALL_DISPATCH_ON(glob_sel);

	ret = syscall(__NR_sysinfo, &info);
	EXPECT_EQ(0, ret) {
		TH_LOG("Dispatch triggered unexpectedly");
	}
}

TEST(direct_dispatch_range)
{
	int ret = 0;
	struct sysinfo info;
	char sel = SYSCALL_DISPATCH_FILTER_ALLOW;

	/*
	 * Instead of calculating libc addresses; allow the entire
	 * memory map and lock the selector.
	 */
	ret = prctl(PR_SET_SYSCALL_USER_DISPATCH, PR_SYS_DISPATCH_ON, 0, -1L, &sel);
	ASSERT_EQ(0, ret) {
		TH_LOG("Kernel does not support CONFIG_SYSCALL_USER_DISPATCH");
	}

	SYSCALL_DISPATCH_ON(sel);

	ret = sysinfo(&info);
	ASSERT_EQ(0, ret) {
		TH_LOG("Dispatch triggered unexpectedly");
	}
}

TEST_HARNESS_MAIN
