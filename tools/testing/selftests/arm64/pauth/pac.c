// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2020 ARM Limited

#include <sys/auxv.h>
#include <signal.h>
#include <setjmp.h>

#include "../../kselftest_harness.h"
#include "helper.h"

#define ASSERT_PAUTH_ENABLED() \
do { \
	unsigned long hwcaps = getauxval(AT_HWCAP); \
	/* data key instructions are not in NOP space. This prevents a SIGILL */ \
	ASSERT_NE(0, hwcaps & HWCAP_PACA) TH_LOG("PAUTH not enabled"); \
} while (0)

sigjmp_buf jmpbuf;
void pac_signal_handler(int signum, siginfo_t *si, void *uc)
{
	if (signum == SIGSEGV || signum == SIGILL)
		siglongjmp(jmpbuf, 1);
}

/* check that a corrupted PAC results in SIGSEGV or SIGILL */
TEST(corrupt_pac)
{
	struct sigaction sa;

	ASSERT_PAUTH_ENABLED();
	if (sigsetjmp(jmpbuf, 1) == 0) {
		sa.sa_sigaction = pac_signal_handler;
		sa.sa_flags = SA_SIGINFO | SA_RESETHAND;
		sigemptyset(&sa.sa_mask);

		sigaction(SIGSEGV, &sa, NULL);
		sigaction(SIGILL, &sa, NULL);

		pac_corruptor();
		ASSERT_TRUE(0) TH_LOG("SIGSEGV/SIGILL signal did not occur");
	}
}

TEST_HARNESS_MAIN
