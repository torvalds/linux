// SPDX-License-Identifier: GPL-2.0-only
/*
 * fsgsbase_restore.c, test ptrace vs fsgsbase
 * Copyright (c) 2020 Andy Lutomirski
 *
 * This test case simulates a tracer redirecting tracee execution to
 * a function and then restoring tracee state using PTRACE_GETREGS and
 * PTRACE_SETREGS.  This is similar to what gdb does when doing
 * 'p func()'.  The catch is that this test has the called function
 * modify a segment register.  This makes sure that ptrace correctly
 * restores segment state when using PTRACE_SETREGS.
 *
 * This is not part of fsgsbase.c, because that test is 64-bit only.
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <sys/syscall.h>
#include <unistd.h>
#include <err.h>
#include <sys/user.h>
#include <asm/prctl.h>
#include <sys/prctl.h>
#include <asm/ldt.h>
#include <sys/mman.h>
#include <stddef.h>
#include <sys/ptrace.h>
#include <sys/wait.h>
#include <stdint.h>

#define EXPECTED_VALUE 0x1337f00d

#ifdef __x86_64__
# define SEG "%gs"
#else
# define SEG "%fs"
#endif

static unsigned int dereference_seg_base(void)
{
	int ret;
	asm volatile ("mov %" SEG ":(0), %0" : "=rm" (ret));
	return ret;
}

static void init_seg(void)
{
	unsigned int *target = mmap(
		NULL, sizeof(unsigned int),
		PROT_READ | PROT_WRITE,
		MAP_PRIVATE | MAP_ANONYMOUS | MAP_32BIT, -1, 0);
	if (target == MAP_FAILED)
		err(1, "mmap");

	*target = EXPECTED_VALUE;

	printf("\tsegment base address = 0x%lx\n", (unsigned long)target);

	struct user_desc desc = {
		.entry_number    = 0,
		.base_addr       = (unsigned int)(uintptr_t)target,
		.limit           = sizeof(unsigned int) - 1,
		.seg_32bit       = 1,
		.contents        = 0, /* Data, grow-up */
		.read_exec_only  = 0,
		.limit_in_pages  = 0,
		.seg_not_present = 0,
		.useable         = 0
	};
	if (syscall(SYS_modify_ldt, 1, &desc, sizeof(desc)) == 0) {
		printf("\tusing LDT slot 0\n");
		asm volatile ("mov %0, %" SEG :: "rm" ((unsigned short)0x7));
	} else {
		/* No modify_ldt for us (configured out, perhaps) */

		struct user_desc *low_desc = mmap(
			NULL, sizeof(desc),
			PROT_READ | PROT_WRITE,
			MAP_PRIVATE | MAP_ANONYMOUS | MAP_32BIT, -1, 0);
		memcpy(low_desc, &desc, sizeof(desc));

		low_desc->entry_number = -1;

		/* 32-bit set_thread_area */
		long ret;
		asm volatile ("int $0x80"
			      : "=a" (ret), "+m" (*low_desc)
			      : "a" (243), "b" (low_desc)
#ifdef __x86_64__
			      : "r8", "r9", "r10", "r11"
#endif
			);
		memcpy(&desc, low_desc, sizeof(desc));
		munmap(low_desc, sizeof(desc));

		if (ret != 0) {
			printf("[NOTE]\tcould not create a segment -- can't test anything\n");
			exit(0);
		}
		printf("\tusing GDT slot %d\n", desc.entry_number);

		unsigned short sel = (unsigned short)((desc.entry_number << 3) | 0x3);
		asm volatile ("mov %0, %" SEG :: "rm" (sel));
	}
}

static void tracee_zap_segment(void)
{
	/*
	 * The tracer will redirect execution here.  This is meant to
	 * work like gdb's 'p func()' feature.  The tricky bit is that
	 * we modify a segment register in order to make sure that ptrace
	 * can correctly restore segment registers.
	 */
	printf("\tTracee: in tracee_zap_segment()\n");

	/*
	 * Write a nonzero selector with base zero to the segment register.
	 * Using a null selector would defeat the test on AMD pre-Zen2
	 * CPUs, as such CPUs don't clear the base when loading a null
	 * selector.
	 */
	unsigned short sel;
	asm volatile ("mov %%ss, %0\n\t"
		      "mov %0, %" SEG
		      : "=rm" (sel));

	pid_t pid = getpid(), tid = syscall(SYS_gettid);

	printf("\tTracee is going back to sleep\n");
	syscall(SYS_tgkill, pid, tid, SIGSTOP);

	/* Should not get here. */
	while (true) {
		printf("[FAIL]\tTracee hit unreachable code\n");
		pause();
	}
}

int main()
{
	printf("\tSetting up a segment\n");
	init_seg();

	unsigned int val = dereference_seg_base();
	if (val != EXPECTED_VALUE) {
		printf("[FAIL]\tseg[0] == %x; should be %x\n", val, EXPECTED_VALUE);
		return 1;
	}
	printf("[OK]\tThe segment points to the right place.\n");

	pid_t chld = fork();
	if (chld < 0)
		err(1, "fork");

	if (chld == 0) {
		prctl(PR_SET_PDEATHSIG, SIGKILL, 0, 0, 0, 0);

		if (ptrace(PTRACE_TRACEME, 0, 0, 0) != 0)
			err(1, "PTRACE_TRACEME");

		pid_t pid = getpid(), tid = syscall(SYS_gettid);

		printf("\tTracee will take a nap until signaled\n");
		syscall(SYS_tgkill, pid, tid, SIGSTOP);

		printf("\tTracee was resumed.  Will re-check segment.\n");

		val = dereference_seg_base();
		if (val != EXPECTED_VALUE) {
			printf("[FAIL]\tseg[0] == %x; should be %x\n", val, EXPECTED_VALUE);
			exit(1);
		}

		printf("[OK]\tThe segment points to the right place.\n");
		exit(0);
	}

	int status;

	/* Wait for SIGSTOP. */
	if (waitpid(chld, &status, 0) != chld || !WIFSTOPPED(status))
		err(1, "waitpid");

	struct user_regs_struct regs;

	if (ptrace(PTRACE_GETREGS, chld, NULL, &regs) != 0)
		err(1, "PTRACE_GETREGS");

#ifdef __x86_64__
	printf("\tChild GS=0x%lx, GSBASE=0x%lx\n", (unsigned long)regs.gs, (unsigned long)regs.gs_base);
#else
	printf("\tChild FS=0x%lx\n", (unsigned long)regs.xfs);
#endif

	struct user_regs_struct regs2 = regs;
#ifdef __x86_64__
	regs2.rip = (unsigned long)tracee_zap_segment;
	regs2.rsp -= 128;	/* Don't clobber the redzone. */
#else
	regs2.eip = (unsigned long)tracee_zap_segment;
#endif

	printf("\tTracer: redirecting tracee to tracee_zap_segment()\n");
	if (ptrace(PTRACE_SETREGS, chld, NULL, &regs2) != 0)
		err(1, "PTRACE_GETREGS");
	if (ptrace(PTRACE_CONT, chld, NULL, NULL) != 0)
		err(1, "PTRACE_GETREGS");

	/* Wait for SIGSTOP. */
	if (waitpid(chld, &status, 0) != chld || !WIFSTOPPED(status))
		err(1, "waitpid");

	printf("\tTracer: restoring tracee state\n");
	if (ptrace(PTRACE_SETREGS, chld, NULL, &regs) != 0)
		err(1, "PTRACE_GETREGS");
	if (ptrace(PTRACE_DETACH, chld, NULL, NULL) != 0)
		err(1, "PTRACE_GETREGS");

	/* Wait for SIGSTOP. */
	if (waitpid(chld, &status, 0) != chld)
		err(1, "waitpid");

	if (WIFSIGNALED(status)) {
		printf("[FAIL]\tTracee crashed\n");
		return 1;
	}

	if (!WIFEXITED(status)) {
		printf("[FAIL]\tTracee stopped for an unexpected reason: %d\n", status);
		return 1;
	}

	int exitcode = WEXITSTATUS(status);
	if (exitcode != 0) {
		printf("[FAIL]\tTracee reported failure\n");
		return 1;
	}

	printf("[OK]\tAll is well.\n");
	return 0;
}
