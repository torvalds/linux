// SPDX-License-Identifier: GPL-2.0-only
/*
 * sysret_ss_attrs.c - test that syscalls return valid hidden SS attributes
 * Copyright (c) 2015 Andrew Lutomirski
 *
 * On AMD CPUs, SYSRET can return with a valid SS descriptor with with
 * the hidden attributes set to an unusable state.  Make sure the kernel
 * doesn't let this happen.
 */

#define _GNU_SOURCE

#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <err.h>
#include <stddef.h>
#include <stdbool.h>
#include <pthread.h>

static void *threadproc(void *ctx)
{
	/*
	 * Do our best to cause sleeps on this CPU to exit the kernel and
	 * re-enter with SS = 0.
	 */
	while (true)
		;

	return NULL;
}

#ifdef __x86_64__
extern unsigned long call32_from_64(void *stack, void (*function)(void));

asm (".pushsection .text\n\t"
     ".code32\n\t"
     "test_ss:\n\t"
     "pushl $0\n\t"
     "popl %eax\n\t"
     "ret\n\t"
     ".code64");
extern void test_ss(void);
#endif

int main()
{
	/*
	 * Start a busy-looping thread on the same CPU we're on.
	 * For simplicity, just stick everything to CPU 0.  This will
	 * fail in some containers, but that's probably okay.
	 */
	cpu_set_t cpuset;
	CPU_ZERO(&cpuset);
	CPU_SET(0, &cpuset);
	if (sched_setaffinity(0, sizeof(cpuset), &cpuset) != 0)
		printf("[WARN]\tsched_setaffinity failed\n");

	pthread_t thread;
	if (pthread_create(&thread, 0, threadproc, 0) != 0)
		err(1, "pthread_create");

#ifdef __x86_64__
	unsigned char *stack32 = mmap(NULL, 4096, PROT_READ | PROT_WRITE,
				      MAP_32BIT | MAP_ANONYMOUS | MAP_PRIVATE,
				      -1, 0);
	if (stack32 == MAP_FAILED)
		err(1, "mmap");
#endif

	printf("[RUN]\tSyscalls followed by SS validation\n");

	for (int i = 0; i < 1000; i++) {
		/*
		 * Go to sleep and return using sysret (if we're 64-bit
		 * or we're 32-bit on AMD on a 64-bit kernel).  On AMD CPUs,
		 * SYSRET doesn't fix up the cached SS descriptor, so the
		 * kernel needs some kind of workaround to make sure that we
		 * end the system call with a valid stack segment.  This
		 * can be a confusing failure because the SS *selector*
		 * is the same regardless.
		 */
		usleep(2);

#ifdef __x86_64__
		/*
		 * On 32-bit, just doing a syscall through glibc is enough
		 * to cause a crash if our cached SS descriptor is invalid.
		 * On 64-bit, it's not, so try extra hard.
		 */
		call32_from_64(stack32 + 4088, test_ss);
#endif
	}

	printf("[OK]\tWe survived\n");

#ifdef __x86_64__
	munmap(stack32, 4096);
#endif

	return 0;
}
