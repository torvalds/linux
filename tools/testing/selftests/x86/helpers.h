// SPDX-License-Identifier: GPL-2.0-only
#ifndef __SELFTESTS_X86_HELPERS_H
#define __SELFTESTS_X86_HELPERS_H

#include <signal.h>
#include <string.h>

#include <asm/processor-flags.h>

#include "../kselftest.h"

static inline unsigned long get_eflags(void)
{
#ifdef __x86_64__
	return __builtin_ia32_readeflags_u64();
#else
	return __builtin_ia32_readeflags_u32();
#endif
}

static inline void set_eflags(unsigned long eflags)
{
#ifdef __x86_64__
	__builtin_ia32_writeeflags_u64(eflags);
#else
	__builtin_ia32_writeeflags_u32(eflags);
#endif
}

static inline void sethandler(int sig, void (*handler)(int, siginfo_t *, void *), int flags)
{
	struct sigaction sa;

	memset(&sa, 0, sizeof(sa));
	sa.sa_sigaction = handler;
	sa.sa_flags = SA_SIGINFO | flags;
	sigemptyset(&sa.sa_mask);
	if (sigaction(sig, &sa, 0))
		ksft_exit_fail_msg("sigaction failed");
}

static inline void clearhandler(int sig)
{
	struct sigaction sa;

	memset(&sa, 0, sizeof(sa));
	sa.sa_handler = SIG_DFL;
	sigemptyset(&sa.sa_mask);
	if (sigaction(sig, &sa, 0))
		ksft_exit_fail_msg("sigaction failed");
}

#endif /* __SELFTESTS_X86_HELPERS_H */
