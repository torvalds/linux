// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2025 Intel Corporation
 */
#define _GNU_SOURCE

#include <err.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ucontext.h>

#ifdef __x86_64__
# define REG_IP REG_RIP
#else
# define REG_IP REG_EIP
#endif

static void sethandler(int sig, void (*handler)(int, siginfo_t *, void *), int flags)
{
	struct sigaction sa;

	memset(&sa, 0, sizeof(sa));
	sa.sa_sigaction = handler;
	sa.sa_flags = SA_SIGINFO | flags;
	sigemptyset(&sa.sa_mask);

	if (sigaction(sig, &sa, 0))
		err(1, "sigaction");

	return;
}

static void sigtrap(int sig, siginfo_t *info, void *ctx_void)
{
	ucontext_t *ctx = (ucontext_t *)ctx_void;
	static unsigned int loop_count_on_same_ip;
	static unsigned long last_trap_ip;

	if (last_trap_ip == ctx->uc_mcontext.gregs[REG_IP]) {
		printf("\tTrapped at %016lx\n", last_trap_ip);

		/*
		 * If the same IP is hit more than 10 times in a row, it is
		 * _considered_ an infinite loop.
		 */
		if (++loop_count_on_same_ip > 10) {
			printf("[FAIL]\tDetected SIGTRAP infinite loop\n");
			exit(1);
		}

		return;
	}

	loop_count_on_same_ip = 0;
	last_trap_ip = ctx->uc_mcontext.gregs[REG_IP];
	printf("\tTrapped at %016lx\n", last_trap_ip);
}

int main(int argc, char *argv[])
{
	sethandler(SIGTRAP, sigtrap, 0);

	/*
	 * Set the Trap Flag (TF) to single-step the test code, therefore to
	 * trigger a SIGTRAP signal after each instruction until the TF is
	 * cleared.
	 *
	 * Because the arithmetic flags are not significant here, the TF is
	 * set by pushing 0x302 onto the stack and then popping it into the
	 * flags register.
	 *
	 * Four instructions in the following asm code are executed with the
	 * TF set, thus the SIGTRAP handler is expected to run four times.
	 */
	printf("[RUN]\tSIGTRAP infinite loop detection\n");
	asm volatile(
#ifdef __x86_64__
		/*
		 * Avoid clobbering the redzone
		 *
		 * Equivalent to "sub $128, %rsp", however -128 can be encoded
		 * in a single byte immediate while 128 uses 4 bytes.
		 */
		"add $-128, %rsp\n\t"
#endif
		"push $0x302\n\t"
		"popf\n\t"
		"nop\n\t"
		"nop\n\t"
		"push $0x202\n\t"
		"popf\n\t"
#ifdef __x86_64__
		"sub $-128, %rsp\n\t"
#endif
	);

	printf("[OK]\tNo SIGTRAP infinite loop detected\n");
	return 0;
}
