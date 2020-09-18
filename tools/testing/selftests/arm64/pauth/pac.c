// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2020 ARM Limited

#include <sys/auxv.h>
#include <signal.h>
#include <setjmp.h>

#include "../../kselftest_harness.h"
#include "helper.h"

#define PAC_COLLISION_ATTEMPTS 10
/*
 * The kernel sets TBID by default. So bits 55 and above should remain
 * untouched no matter what.
 * The VA space size is 48 bits. Bigger is opt-in.
 */
#define PAC_MASK (~0xff80ffffffffffff)
#define ASSERT_PAUTH_ENABLED() \
do { \
	unsigned long hwcaps = getauxval(AT_HWCAP); \
	/* data key instructions are not in NOP space. This prevents a SIGILL */ \
	ASSERT_NE(0, hwcaps & HWCAP_PACA) TH_LOG("PAUTH not enabled"); \
} while (0)
#define ASSERT_GENERIC_PAUTH_ENABLED() \
do { \
	unsigned long hwcaps = getauxval(AT_HWCAP); \
	/* generic key instructions are not in NOP space. This prevents a SIGILL */ \
	ASSERT_NE(0, hwcaps & HWCAP_PACG) TH_LOG("Generic PAUTH not enabled"); \
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

/*
 * There are no separate pac* and aut* controls so checking only the pac*
 * instructions is sufficient
 */
TEST(pac_instructions_not_nop)
{
	size_t keyia = 0;
	size_t keyib = 0;
	size_t keyda = 0;
	size_t keydb = 0;

	ASSERT_PAUTH_ENABLED();

	for (int i = 0; i < PAC_COLLISION_ATTEMPTS; i++) {
		keyia |= keyia_sign(i) & PAC_MASK;
		keyib |= keyib_sign(i) & PAC_MASK;
		keyda |= keyda_sign(i) & PAC_MASK;
		keydb |= keydb_sign(i) & PAC_MASK;
	}

	ASSERT_NE(0, keyia) TH_LOG("keyia instructions did nothing");
	ASSERT_NE(0, keyib) TH_LOG("keyib instructions did nothing");
	ASSERT_NE(0, keyda) TH_LOG("keyda instructions did nothing");
	ASSERT_NE(0, keydb) TH_LOG("keydb instructions did nothing");
}

TEST(pac_instructions_not_nop_generic)
{
	size_t keyg = 0;

	ASSERT_GENERIC_PAUTH_ENABLED();

	for (int i = 0; i < PAC_COLLISION_ATTEMPTS; i++)
		keyg |= keyg_sign(i) & PAC_MASK;

	ASSERT_NE(0, keyg)  TH_LOG("keyg instructions did nothing");
}

TEST_HARNESS_MAIN
