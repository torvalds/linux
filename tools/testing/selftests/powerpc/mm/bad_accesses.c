// SPDX-License-Identifier: GPL-2.0+
//
// Copyright 2019, Michael Ellerman, IBM Corp.
//
// Test that out-of-bounds reads/writes behave as expected.

#include <setjmp.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include "utils.h"

// Old distros (Ubuntu 16.04 at least) don't define this
#ifndef SEGV_BNDERR
#define SEGV_BNDERR	3
#endif

// 64-bit kernel is always here
#define PAGE_OFFSET	(0xcul << 60)

static unsigned long kernel_virt_end;

static volatile int fault_code;
static volatile unsigned long fault_addr;
static jmp_buf setjmp_env;

static void segv_handler(int n, siginfo_t *info, void *ctxt_v)
{
	fault_code = info->si_code;
	fault_addr = (unsigned long)info->si_addr;
	siglongjmp(setjmp_env, 1);
}

int bad_access(char *p, bool write)
{
	char x;

	fault_code = 0;
	fault_addr = 0;

	if (sigsetjmp(setjmp_env, 1) == 0) {
		if (write)
			*p = 1;
		else
			x = *p;

		printf("Bad - no SEGV! (%c)\n", x);
		return 1;
	}

	// If we see MAPERR that means we took a page fault rather than an SLB
	// miss. We only expect to take page faults for addresses within the
	// valid kernel range.
	FAIL_IF(fault_code == SEGV_MAPERR && \
		(fault_addr < PAGE_OFFSET || fault_addr >= kernel_virt_end));

	FAIL_IF(fault_code != SEGV_MAPERR && fault_code != SEGV_BNDERR);

	return 0;
}

static int test(void)
{
	unsigned long i, j, addr, region_shift, page_shift, page_size;
	struct sigaction sig;
	bool hash_mmu;

	sig = (struct sigaction) {
		.sa_sigaction = segv_handler,
		.sa_flags = SA_SIGINFO,
	};

	FAIL_IF(sigaction(SIGSEGV, &sig, NULL) != 0);

	FAIL_IF(using_hash_mmu(&hash_mmu));

	page_size = sysconf(_SC_PAGESIZE);
	if (page_size == (64 * 1024))
		page_shift = 16;
	else
		page_shift = 12;

	if (page_size == (64 * 1024) || !hash_mmu) {
		region_shift = 52;

		// We have 7 512T regions (4 kernel linear, vmalloc, io, vmemmap)
		kernel_virt_end = PAGE_OFFSET + (7 * (512ul << 40));
	} else if (page_size == (4 * 1024) && hash_mmu) {
		region_shift = 46;

		// We have 7 64T regions (4 kernel linear, vmalloc, io, vmemmap)
		kernel_virt_end = PAGE_OFFSET + (7 * (64ul << 40));
	} else
		FAIL_IF(true);

	printf("Using %s MMU, PAGE_SIZE = %dKB start address 0x%016lx\n",
	       hash_mmu ? "hash" : "radix",
	       (1 << page_shift) >> 10,
	       1ul << region_shift);

	// This generates access patterns like:
	//   0x0010000000000000
	//   0x0010000000010000
	//   0x0010000000020000
	//   ...
	//   0x0014000000000000
	//   0x0018000000000000
	//   0x0020000000000000
	//   0x0020000000010000
	//   0x0020000000020000
	//   ...
	//   0xf400000000000000
	//   0xf800000000000000

	for (i = 1; i <= ((0xful << 60) >> region_shift); i++) {
		for (j = page_shift - 1; j < 60; j++) {
			unsigned long base, delta;

			base  = i << region_shift;
			delta = 1ul << j;

			if (delta >= base)
				break;

			addr = (base | delta) & ~((1 << page_shift) - 1);

			FAIL_IF(bad_access((char *)addr, false));
			FAIL_IF(bad_access((char *)addr, true));
		}
	}

	return 0;
}

int main(void)
{
	test_harness_set_timeout(300);
	return test_harness(test, "bad_accesses");
}
