// SPDX-License-Identifier: GPL-2.0
/*
 * Test that we can take signals with and without the VDSO mapped, which trigger
 * different paths in the signal handling code.
 *
 * See handle_rt_signal64() and setup_trampoline() in signal_64.c
 */

#define _GNU_SOURCE

#include <errno.h>
#include <stdio.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <unistd.h>

// Ensure assert() is not compiled out
#undef NDEBUG
#include <assert.h>

#include "utils.h"

static int search_proc_maps(char *needle, unsigned long *low, unsigned long *high)
{
	unsigned long start, end;
	static char buf[4096];
	char name[128];
	FILE *f;
	int rc = -1;

	f = fopen("/proc/self/maps", "r");
	if (!f) {
		perror("fopen");
		return -1;
	}

	while (fgets(buf, sizeof(buf), f)) {
		rc = sscanf(buf, "%lx-%lx %*c%*c%*c%*c %*x %*d:%*d %*d %127s\n",
			    &start, &end, name);
		if (rc == 2)
			continue;

		if (rc != 3) {
			printf("sscanf errored\n");
			rc = -1;
			break;
		}

		if (strstr(name, needle)) {
			*low = start;
			*high = end - 1;
			rc = 0;
			break;
		}
	}

	fclose(f);

	return rc;
}

static volatile sig_atomic_t took_signal = 0;

static void sigusr1_handler(int sig)
{
	took_signal++;
}

int test_sigreturn_vdso(void)
{
	unsigned long low, high, size;
	struct sigaction act;
	char *p;

	act.sa_handler = sigusr1_handler;
	act.sa_flags = 0;
	sigemptyset(&act.sa_mask);

	assert(sigaction(SIGUSR1, &act, NULL) == 0);

	// Confirm the VDSO is mapped, and work out where it is
	assert(search_proc_maps("[vdso]", &low, &high) == 0);
	size = high - low + 1;
	printf("VDSO is at 0x%lx-0x%lx (%lu bytes)\n", low, high, size);

	kill(getpid(), SIGUSR1);
	assert(took_signal == 1);
	printf("Signal delivered OK with VDSO mapped\n");

	// Remap the VDSO somewhere else
	p = mmap(NULL, size, PROT_READ|PROT_WRITE, MAP_ANONYMOUS|MAP_PRIVATE, -1, 0);
	assert(p != MAP_FAILED);
	assert(mremap((void *)low, size, size, MREMAP_MAYMOVE|MREMAP_FIXED, p) != MAP_FAILED);
	assert(search_proc_maps("[vdso]", &low, &high) == 0);
	size = high - low + 1;
	printf("VDSO moved to 0x%lx-0x%lx (%lu bytes)\n", low, high, size);

	kill(getpid(), SIGUSR1);
	assert(took_signal == 2);
	printf("Signal delivered OK with VDSO moved\n");

	assert(munmap((void *)low, size) == 0);
	printf("Unmapped VDSO\n");

	// Confirm the VDSO is not mapped anymore
	assert(search_proc_maps("[vdso]", &low, &high) != 0);

	// Make the stack executable
	assert(search_proc_maps("[stack]", &low, &high) == 0);
	size = high - low + 1;
	mprotect((void *)low, size, PROT_READ|PROT_WRITE|PROT_EXEC);
	printf("Remapped the stack executable\n");

	kill(getpid(), SIGUSR1);
	assert(took_signal == 3);
	printf("Signal delivered OK with VDSO unmapped\n");

	return 0;
}

int main(void)
{
	return test_harness(test_sigreturn_vdso, "sigreturn_vdso");
}
