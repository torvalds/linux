// SPDX-License-Identifier: GPL-2.0+
//
// Copyright 2022, Michael Ellerman, IBM Corp.
//
// Test that the 4PB address space SLB handling doesn't corrupt userspace registers
// (r9-r13) due to a SLB fault while saving the PPR.
//
// The bug was introduced in f384796c4 ("powerpc/mm: Add support for handling > 512TB
// address in SLB miss") and fixed in 4c2de74cc869 ("powerpc/64: Interrupts save PPR on
// stack rather than thread_struct").
//
// To hit the bug requires the task struct and kernel stack to be in different segments.
// Usually that requires more than 1TB of RAM, or if that's not practical, boot the kernel
// with "disable_1tb_segments".
//
// The test works by creating mappings above 512TB, to trigger the large address space
// support. It creates 64 mappings, double the size of the SLB, to cause SLB faults on
// each access (assuming naive replacement). It then loops over those mappings touching
// each, and checks that r9-r13 aren't corrupted.
//
// It then forks another child and tries again, because a new child process will get a new
// kernel stack and thread struct allocated, which may be more optimally placed to trigger
// the bug. It would probably be better to leave the previous child processes hanging
// around, so that kernel stack & thread struct allocations are not reused, but that would
// amount to a 30 second fork bomb. The current design reliably triggers the bug on
// unpatched kernels.

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include "utils.h"

#ifndef MAP_FIXED_NOREPLACE
#define MAP_FIXED_NOREPLACE MAP_FIXED // "Should be safe" above 512TB
#endif

#define BASE_ADDRESS (1ul << 50) // 1PB
#define STRIDE	     (2ul << 40) // 2TB
#define SLB_SIZE     32
#define NR_MAPPINGS  (SLB_SIZE * 2)

static volatile sig_atomic_t signaled;

static void signal_handler(int sig)
{
	signaled = 1;
}

#define CHECK_REG(_reg)                                                                \
	if (_reg != _reg##_orig) {                                                     \
		printf(str(_reg) " corrupted! Expected 0x%lx != 0x%lx\n", _reg##_orig, \
		       _reg);                                                          \
		_exit(1);                                                              \
	}

static int touch_mappings(void)
{
	unsigned long r9_orig, r10_orig, r11_orig, r12_orig, r13_orig;
	unsigned long r9, r10, r11, r12, r13;
	unsigned long addr, *p;
	int i;

	for (i = 0; i < NR_MAPPINGS; i++) {
		addr = BASE_ADDRESS + (i * STRIDE);
		p = (unsigned long *)addr;

		asm volatile("mr   %0, %%r9	;" // Read original GPR values
			     "mr   %1, %%r10	;"
			     "mr   %2, %%r11	;"
			     "mr   %3, %%r12	;"
			     "mr   %4, %%r13	;"
			     "std %10, 0(%11)   ;" // Trigger SLB fault
			     "mr   %5, %%r9	;" // Save possibly corrupted values
			     "mr   %6, %%r10	;"
			     "mr   %7, %%r11	;"
			     "mr   %8, %%r12	;"
			     "mr   %9, %%r13	;"
			     "mr   %%r9,  %0	;" // Restore original values
			     "mr   %%r10, %1	;"
			     "mr   %%r11, %2	;"
			     "mr   %%r12, %3	;"
			     "mr   %%r13, %4	;"
			     : "=&b"(r9_orig), "=&b"(r10_orig), "=&b"(r11_orig),
			       "=&b"(r12_orig), "=&b"(r13_orig), "=&b"(r9), "=&b"(r10),
			       "=&b"(r11), "=&b"(r12), "=&b"(r13)
			     : "b"(i), "b"(p)
			     : "r9", "r10", "r11", "r12", "r13");

		CHECK_REG(r9);
		CHECK_REG(r10);
		CHECK_REG(r11);
		CHECK_REG(r12);
		CHECK_REG(r13);
	}

	return 0;
}

static int test(void)
{
	unsigned long page_size, addr, *p;
	struct sigaction action;
	bool hash_mmu;
	int i, status;
	pid_t pid;

	// This tests a hash MMU specific bug.
	FAIL_IF(using_hash_mmu(&hash_mmu));
	SKIP_IF(!hash_mmu);

	page_size = sysconf(_SC_PAGESIZE);

	for (i = 0; i < NR_MAPPINGS; i++) {
		addr = BASE_ADDRESS + (i * STRIDE);

		p = mmap((void *)addr, page_size, PROT_READ | PROT_WRITE,
			 MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0);
		if (p == MAP_FAILED) {
			perror("mmap");
			printf("Error: couldn't mmap(), confirm kernel has 4PB support?\n");
			return 1;
		}
	}

	action.sa_handler = signal_handler;
	action.sa_flags = SA_RESTART;
	FAIL_IF(sigaction(SIGALRM, &action, NULL) < 0);

	// Seen to always crash in under ~10s on affected kernels.
	alarm(30);

	while (!signaled) {
		// Fork new processes, to increase the chance that we hit the case where
		// the kernel stack and task struct are in different segments.
		pid = fork();
		if (pid == 0)
			exit(touch_mappings());

		FAIL_IF(waitpid(-1, &status, 0) == -1);
		FAIL_IF(WIFSIGNALED(status));
		FAIL_IF(!WIFEXITED(status));
		FAIL_IF(WEXITSTATUS(status));
	}

	return 0;
}

int main(void)
{
	return test_harness(test, "large_vm_gpr_corruption");
}
