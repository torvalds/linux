// SPDX-License-Identifier: GPL-2.0+

/*
 * Copyright 2018 IBM Corporation.
 */

#define __SANE_USERSPACE_TYPES__

#include <sys/types.h>
#include <stdint.h>
#include <unistd.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "utils.h"
#include "flush_utils.h"

static inline __u64 load(void *addr)
{
	__u64 tmp;

	asm volatile("ld %0,0(%1)" : "=r"(tmp) : "b"(addr));

	return tmp;
}

void syscall_loop(char *p, unsigned long iterations,
		  unsigned long zero_size)
{
	for (unsigned long i = 0; i < iterations; i++) {
		for (unsigned long j = 0; j < zero_size; j += CACHELINE_SIZE)
			load(p + j);
		getppid();
	}
}

static void sigill_handler(int signr, siginfo_t *info, void *unused)
{
	static int warned;
	ucontext_t *ctx = (ucontext_t *)unused;
	unsigned long *pc = &UCONTEXT_NIA(ctx);

	/* mtspr 3,RS to check for move to DSCR below */
	if ((*((unsigned int *)*pc) & 0xfc1fffff) == 0x7c0303a6) {
		if (!warned++)
			printf("WARNING: Skipping over dscr setup. Consider running 'ppc64_cpu --dscr=1' manually.\n");
		*pc += 4;
	} else {
		printf("SIGILL at %p\n", pc);
		abort();
	}
}

void set_dscr(unsigned long val)
{
	static int init;
	struct sigaction sa;

	if (!init) {
		memset(&sa, 0, sizeof(sa));
		sa.sa_sigaction = sigill_handler;
		sa.sa_flags = SA_SIGINFO;
		if (sigaction(SIGILL, &sa, NULL))
			perror("sigill_handler");
		init = 1;
	}

	asm volatile("mtspr %1,%0" : : "r" (val), "i" (SPRN_DSCR));
}
