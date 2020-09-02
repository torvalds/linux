// SPDX-License-Identifier: GPL-2.0+

/*
 * Ptrace test for hw breakpoints
 *
 * Based on tools/testing/selftests/breakpoints/breakpoint_test.c
 *
 * This test forks and the parent then traces the child doing various
 * types of ptrace enabled breakpoints
 *
 * Copyright (C) 2018 Michael Neuling, IBM Corporation.
 */

#include <sys/ptrace.h>
#include <unistd.h>
#include <stddef.h>
#include <sys/user.h>
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/syscall.h>
#include <linux/limits.h>
#include "ptrace.h"

#define SPRN_PVR	0x11F
#define PVR_8xx		0x00500000

bool is_8xx;

/*
 * Use volatile on all global var so that compiler doesn't
 * optimise their load/stores. Otherwise selftest can fail.
 */
static volatile __u64 glvar;

#define DAWR_MAX_LEN 512
static volatile __u8 big_var[DAWR_MAX_LEN] __attribute__((aligned(512)));

#define A_LEN 6
#define B_LEN 6
struct gstruct {
	__u8 a[A_LEN]; /* double word aligned */
	__u8 b[B_LEN]; /* double word unaligned */
};
static volatile struct gstruct gstruct __attribute__((aligned(512)));

static volatile char cwd[PATH_MAX] __attribute__((aligned(8)));

static void get_dbginfo(pid_t child_pid, struct ppc_debug_info *dbginfo)
{
	if (ptrace(PPC_PTRACE_GETHWDBGINFO, child_pid, NULL, dbginfo)) {
		perror("Can't get breakpoint info");
		exit(-1);
	}
}

static bool dawr_present(struct ppc_debug_info *dbginfo)
{
	return !!(dbginfo->features & PPC_DEBUG_FEATURE_DATA_BP_DAWR);
}

static void write_var(int len)
{
	__u8 *pcvar;
	__u16 *psvar;
	__u32 *pivar;
	__u64 *plvar;

	switch (len) {
	case 1:
		pcvar = (__u8 *)&glvar;
		*pcvar = 0xff;
		break;
	case 2:
		psvar = (__u16 *)&glvar;
		*psvar = 0xffff;
		break;
	case 4:
		pivar = (__u32 *)&glvar;
		*pivar = 0xffffffff;
		break;
	case 8:
		plvar = (__u64 *)&glvar;
		*plvar = 0xffffffffffffffffLL;
		break;
	}
}

static void read_var(int len)
{
	__u8 cvar __attribute__((unused));
	__u16 svar __attribute__((unused));
	__u32 ivar __attribute__((unused));
	__u64 lvar __attribute__((unused));

	switch (len) {
	case 1:
		cvar = (__u8)glvar;
		break;
	case 2:
		svar = (__u16)glvar;
		break;
	case 4:
		ivar = (__u32)glvar;
		break;
	case 8:
		lvar = (__u64)glvar;
		break;
	}
}

static void test_workload(void)
{
	__u8 cvar __attribute__((unused));
	__u32 ivar __attribute__((unused));
	int len = 0;

	if (ptrace(PTRACE_TRACEME, 0, NULL, 0)) {
		perror("Child can't be traced?");
		exit(-1);
	}

	/* Wake up father so that it sets up the first test */
	kill(getpid(), SIGUSR1);

	/* PTRACE_SET_DEBUGREG, WO test */
	for (len = 1; len <= sizeof(glvar); len <<= 1)
		write_var(len);

	/* PTRACE_SET_DEBUGREG, RO test */
	for (len = 1; len <= sizeof(glvar); len <<= 1)
		read_var(len);

	/* PTRACE_SET_DEBUGREG, RW test */
	for (len = 1; len <= sizeof(glvar); len <<= 1) {
		if (rand() % 2)
			read_var(len);
		else
			write_var(len);
	}

	/* PTRACE_SET_DEBUGREG, Kernel Access Userspace test */
	syscall(__NR_getcwd, &cwd, PATH_MAX);

	/* PPC_PTRACE_SETHWDEBUG, MODE_EXACT, WO test */
	write_var(1);

	/* PPC_PTRACE_SETHWDEBUG, MODE_EXACT, RO test */
	read_var(1);

	/* PPC_PTRACE_SETHWDEBUG, MODE_EXACT, RW test */
	if (rand() % 2)
		write_var(1);
	else
		read_var(1);

	/* PPC_PTRACE_SETHWDEBUG, MODE_EXACT, Kernel Access Userspace test */
	syscall(__NR_getcwd, &cwd, PATH_MAX);

	/* PPC_PTRACE_SETHWDEBUG, MODE_RANGE, DW ALIGNED, WO test */
	gstruct.a[rand() % A_LEN] = 'a';

	/* PPC_PTRACE_SETHWDEBUG, MODE_RANGE, DW ALIGNED, RO test */
	cvar = gstruct.a[rand() % A_LEN];

	/* PPC_PTRACE_SETHWDEBUG, MODE_RANGE, DW ALIGNED, RW test */
	if (rand() % 2)
		gstruct.a[rand() % A_LEN] = 'a';
	else
		cvar = gstruct.a[rand() % A_LEN];

	/* PPC_PTRACE_SETHWDEBUG, MODE_RANGE, DW UNALIGNED, WO test */
	gstruct.b[rand() % B_LEN] = 'b';

	/* PPC_PTRACE_SETHWDEBUG, MODE_RANGE, DW UNALIGNED, RO test */
	cvar = gstruct.b[rand() % B_LEN];

	/* PPC_PTRACE_SETHWDEBUG, MODE_RANGE, DW UNALIGNED, RW test */
	if (rand() % 2)
		gstruct.b[rand() % B_LEN] = 'b';
	else
		cvar = gstruct.b[rand() % B_LEN];

	/* PPC_PTRACE_SETHWDEBUG, MODE_RANGE, DW UNALIGNED, DAR OUTSIDE, RW test */
	if (rand() % 2)
		*((int *)(gstruct.a + 4)) = 10;
	else
		ivar = *((int *)(gstruct.a + 4));

	/* PPC_PTRACE_SETHWDEBUG. DAWR_MAX_LEN. RW test */
	if (rand() % 2)
		big_var[rand() % DAWR_MAX_LEN] = 'a';
	else
		cvar = big_var[rand() % DAWR_MAX_LEN];
}

static void check_success(pid_t child_pid, const char *name, const char *type,
			  unsigned long saddr, int len)
{
	int status;
	siginfo_t siginfo;
	unsigned long eaddr = (saddr + len - 1) | 0x7;

	saddr &= ~0x7;

	/* Wait for the child to SIGTRAP */
	wait(&status);

	ptrace(PTRACE_GETSIGINFO, child_pid, NULL, &siginfo);

	if (!WIFSTOPPED(status) || WSTOPSIG(status) != SIGTRAP ||
	    (unsigned long)siginfo.si_addr < saddr ||
	    (unsigned long)siginfo.si_addr > eaddr) {
		printf("%s, %s, len: %d: Fail\n", name, type, len);
		exit(-1);
	}

	printf("%s, %s, len: %d: Ok\n", name, type, len);

	if (!is_8xx) {
		/*
		 * For ptrace registered watchpoint, signal is generated
		 * before executing load/store. Singlestep the instruction
		 * and then continue the test.
		 */
		ptrace(PTRACE_SINGLESTEP, child_pid, NULL, 0);
		wait(NULL);
	}
}

static void ptrace_set_debugreg(pid_t child_pid, unsigned long wp_addr)
{
	if (ptrace(PTRACE_SET_DEBUGREG, child_pid, 0, wp_addr)) {
		perror("PTRACE_SET_DEBUGREG failed");
		exit(-1);
	}
}

static int ptrace_sethwdebug(pid_t child_pid, struct ppc_hw_breakpoint *info)
{
	int wh = ptrace(PPC_PTRACE_SETHWDEBUG, child_pid, 0, info);

	if (wh <= 0) {
		perror("PPC_PTRACE_SETHWDEBUG failed");
		exit(-1);
	}
	return wh;
}

static void ptrace_delhwdebug(pid_t child_pid, int wh)
{
	if (ptrace(PPC_PTRACE_DELHWDEBUG, child_pid, 0, wh) < 0) {
		perror("PPC_PTRACE_DELHWDEBUG failed");
		exit(-1);
	}
}

#define DABR_READ_SHIFT		0
#define DABR_WRITE_SHIFT	1
#define DABR_TRANSLATION_SHIFT	2

static int test_set_debugreg(pid_t child_pid)
{
	unsigned long wp_addr = (unsigned long)&glvar;
	char *name = "PTRACE_SET_DEBUGREG";
	int len;

	/* PTRACE_SET_DEBUGREG, WO test*/
	wp_addr &= ~0x7UL;
	wp_addr |= (1UL << DABR_WRITE_SHIFT);
	wp_addr |= (1UL << DABR_TRANSLATION_SHIFT);
	for (len = 1; len <= sizeof(glvar); len <<= 1) {
		ptrace_set_debugreg(child_pid, wp_addr);
		ptrace(PTRACE_CONT, child_pid, NULL, 0);
		check_success(child_pid, name, "WO", wp_addr, len);
	}

	/* PTRACE_SET_DEBUGREG, RO test */
	wp_addr &= ~0x7UL;
	wp_addr |= (1UL << DABR_READ_SHIFT);
	wp_addr |= (1UL << DABR_TRANSLATION_SHIFT);
	for (len = 1; len <= sizeof(glvar); len <<= 1) {
		ptrace_set_debugreg(child_pid, wp_addr);
		ptrace(PTRACE_CONT, child_pid, NULL, 0);
		check_success(child_pid, name, "RO", wp_addr, len);
	}

	/* PTRACE_SET_DEBUGREG, RW test */
	wp_addr &= ~0x7UL;
	wp_addr |= (1Ul << DABR_READ_SHIFT);
	wp_addr |= (1UL << DABR_WRITE_SHIFT);
	wp_addr |= (1UL << DABR_TRANSLATION_SHIFT);
	for (len = 1; len <= sizeof(glvar); len <<= 1) {
		ptrace_set_debugreg(child_pid, wp_addr);
		ptrace(PTRACE_CONT, child_pid, NULL, 0);
		check_success(child_pid, name, "RW", wp_addr, len);
	}

	ptrace_set_debugreg(child_pid, 0);
	return 0;
}

static int test_set_debugreg_kernel_userspace(pid_t child_pid)
{
	unsigned long wp_addr = (unsigned long)cwd;
	char *name = "PTRACE_SET_DEBUGREG";

	/* PTRACE_SET_DEBUGREG, Kernel Access Userspace test */
	wp_addr &= ~0x7UL;
	wp_addr |= (1Ul << DABR_READ_SHIFT);
	wp_addr |= (1UL << DABR_WRITE_SHIFT);
	wp_addr |= (1UL << DABR_TRANSLATION_SHIFT);
	ptrace_set_debugreg(child_pid, wp_addr);
	ptrace(PTRACE_CONT, child_pid, NULL, 0);
	check_success(child_pid, name, "Kernel Access Userspace", wp_addr, 8);

	ptrace_set_debugreg(child_pid, 0);
	return 0;
}

static void get_ppc_hw_breakpoint(struct ppc_hw_breakpoint *info, int type,
				  unsigned long addr, int len)
{
	info->version = 1;
	info->trigger_type = type;
	info->condition_mode = PPC_BREAKPOINT_CONDITION_NONE;
	info->addr = (__u64)addr;
	info->addr2 = (__u64)addr + len;
	info->condition_value = 0;
	if (!len)
		info->addr_mode = PPC_BREAKPOINT_MODE_EXACT;
	else
		info->addr_mode = PPC_BREAKPOINT_MODE_RANGE_INCLUSIVE;
}

static void test_sethwdebug_exact(pid_t child_pid)
{
	struct ppc_hw_breakpoint info;
	unsigned long wp_addr = (unsigned long)&glvar;
	char *name = "PPC_PTRACE_SETHWDEBUG, MODE_EXACT";
	int len = 1; /* hardcoded in kernel */
	int wh;

	/* PPC_PTRACE_SETHWDEBUG, MODE_EXACT, WO test */
	get_ppc_hw_breakpoint(&info, PPC_BREAKPOINT_TRIGGER_WRITE, wp_addr, 0);
	wh = ptrace_sethwdebug(child_pid, &info);
	ptrace(PTRACE_CONT, child_pid, NULL, 0);
	check_success(child_pid, name, "WO", wp_addr, len);
	ptrace_delhwdebug(child_pid, wh);

	/* PPC_PTRACE_SETHWDEBUG, MODE_EXACT, RO test */
	get_ppc_hw_breakpoint(&info, PPC_BREAKPOINT_TRIGGER_READ, wp_addr, 0);
	wh = ptrace_sethwdebug(child_pid, &info);
	ptrace(PTRACE_CONT, child_pid, NULL, 0);
	check_success(child_pid, name, "RO", wp_addr, len);
	ptrace_delhwdebug(child_pid, wh);

	/* PPC_PTRACE_SETHWDEBUG, MODE_EXACT, RW test */
	get_ppc_hw_breakpoint(&info, PPC_BREAKPOINT_TRIGGER_RW, wp_addr, 0);
	wh = ptrace_sethwdebug(child_pid, &info);
	ptrace(PTRACE_CONT, child_pid, NULL, 0);
	check_success(child_pid, name, "RW", wp_addr, len);
	ptrace_delhwdebug(child_pid, wh);
}

static void test_sethwdebug_exact_kernel_userspace(pid_t child_pid)
{
	struct ppc_hw_breakpoint info;
	unsigned long wp_addr = (unsigned long)&cwd;
	char *name = "PPC_PTRACE_SETHWDEBUG, MODE_EXACT";
	int len = 1; /* hardcoded in kernel */
	int wh;

	/* PPC_PTRACE_SETHWDEBUG, MODE_EXACT, Kernel Access Userspace test */
	get_ppc_hw_breakpoint(&info, PPC_BREAKPOINT_TRIGGER_WRITE, wp_addr, 0);
	wh = ptrace_sethwdebug(child_pid, &info);
	ptrace(PTRACE_CONT, child_pid, NULL, 0);
	check_success(child_pid, name, "Kernel Access Userspace", wp_addr, len);
	ptrace_delhwdebug(child_pid, wh);
}

static void test_sethwdebug_range_aligned(pid_t child_pid)
{
	struct ppc_hw_breakpoint info;
	unsigned long wp_addr;
	char *name = "PPC_PTRACE_SETHWDEBUG, MODE_RANGE, DW ALIGNED";
	int len;
	int wh;

	/* PPC_PTRACE_SETHWDEBUG, MODE_RANGE, DW ALIGNED, WO test */
	wp_addr = (unsigned long)&gstruct.a;
	len = A_LEN;
	get_ppc_hw_breakpoint(&info, PPC_BREAKPOINT_TRIGGER_WRITE, wp_addr, len);
	wh = ptrace_sethwdebug(child_pid, &info);
	ptrace(PTRACE_CONT, child_pid, NULL, 0);
	check_success(child_pid, name, "WO", wp_addr, len);
	ptrace_delhwdebug(child_pid, wh);

	/* PPC_PTRACE_SETHWDEBUG, MODE_RANGE, DW ALIGNED, RO test */
	wp_addr = (unsigned long)&gstruct.a;
	len = A_LEN;
	get_ppc_hw_breakpoint(&info, PPC_BREAKPOINT_TRIGGER_READ, wp_addr, len);
	wh = ptrace_sethwdebug(child_pid, &info);
	ptrace(PTRACE_CONT, child_pid, NULL, 0);
	check_success(child_pid, name, "RO", wp_addr, len);
	ptrace_delhwdebug(child_pid, wh);

	/* PPC_PTRACE_SETHWDEBUG, MODE_RANGE, DW ALIGNED, RW test */
	wp_addr = (unsigned long)&gstruct.a;
	len = A_LEN;
	get_ppc_hw_breakpoint(&info, PPC_BREAKPOINT_TRIGGER_RW, wp_addr, len);
	wh = ptrace_sethwdebug(child_pid, &info);
	ptrace(PTRACE_CONT, child_pid, NULL, 0);
	check_success(child_pid, name, "RW", wp_addr, len);
	ptrace_delhwdebug(child_pid, wh);
}

static void test_sethwdebug_range_unaligned(pid_t child_pid)
{
	struct ppc_hw_breakpoint info;
	unsigned long wp_addr;
	char *name = "PPC_PTRACE_SETHWDEBUG, MODE_RANGE, DW UNALIGNED";
	int len;
	int wh;

	/* PPC_PTRACE_SETHWDEBUG, MODE_RANGE, DW UNALIGNED, WO test */
	wp_addr = (unsigned long)&gstruct.b;
	len = B_LEN;
	get_ppc_hw_breakpoint(&info, PPC_BREAKPOINT_TRIGGER_WRITE, wp_addr, len);
	wh = ptrace_sethwdebug(child_pid, &info);
	ptrace(PTRACE_CONT, child_pid, NULL, 0);
	check_success(child_pid, name, "WO", wp_addr, len);
	ptrace_delhwdebug(child_pid, wh);

	/* PPC_PTRACE_SETHWDEBUG, MODE_RANGE, DW UNALIGNED, RO test */
	wp_addr = (unsigned long)&gstruct.b;
	len = B_LEN;
	get_ppc_hw_breakpoint(&info, PPC_BREAKPOINT_TRIGGER_READ, wp_addr, len);
	wh = ptrace_sethwdebug(child_pid, &info);
	ptrace(PTRACE_CONT, child_pid, NULL, 0);
	check_success(child_pid, name, "RO", wp_addr, len);
	ptrace_delhwdebug(child_pid, wh);

	/* PPC_PTRACE_SETHWDEBUG, MODE_RANGE, DW UNALIGNED, RW test */
	wp_addr = (unsigned long)&gstruct.b;
	len = B_LEN;
	get_ppc_hw_breakpoint(&info, PPC_BREAKPOINT_TRIGGER_RW, wp_addr, len);
	wh = ptrace_sethwdebug(child_pid, &info);
	ptrace(PTRACE_CONT, child_pid, NULL, 0);
	check_success(child_pid, name, "RW", wp_addr, len);
	ptrace_delhwdebug(child_pid, wh);

}

static void test_sethwdebug_range_unaligned_dar(pid_t child_pid)
{
	struct ppc_hw_breakpoint info;
	unsigned long wp_addr;
	char *name = "PPC_PTRACE_SETHWDEBUG, MODE_RANGE, DW UNALIGNED, DAR OUTSIDE";
	int len;
	int wh;

	/* PPC_PTRACE_SETHWDEBUG, MODE_RANGE, DW UNALIGNED, DAR OUTSIDE, RW test */
	wp_addr = (unsigned long)&gstruct.b;
	len = B_LEN;
	get_ppc_hw_breakpoint(&info, PPC_BREAKPOINT_TRIGGER_WRITE, wp_addr, len);
	wh = ptrace_sethwdebug(child_pid, &info);
	ptrace(PTRACE_CONT, child_pid, NULL, 0);
	check_success(child_pid, name, "RW", wp_addr, len);
	ptrace_delhwdebug(child_pid, wh);
}

static void test_sethwdebug_dawr_max_range(pid_t child_pid)
{
	struct ppc_hw_breakpoint info;
	unsigned long wp_addr;
	char *name = "PPC_PTRACE_SETHWDEBUG, DAWR_MAX_LEN";
	int len;
	int wh;

	/* PPC_PTRACE_SETHWDEBUG, DAWR_MAX_LEN, RW test */
	wp_addr = (unsigned long)big_var;
	len = DAWR_MAX_LEN;
	get_ppc_hw_breakpoint(&info, PPC_BREAKPOINT_TRIGGER_RW, wp_addr, len);
	wh = ptrace_sethwdebug(child_pid, &info);
	ptrace(PTRACE_CONT, child_pid, NULL, 0);
	check_success(child_pid, name, "RW", wp_addr, len);
	ptrace_delhwdebug(child_pid, wh);
}

/* Set the breakpoints and check the child successfully trigger them */
static void
run_tests(pid_t child_pid, struct ppc_debug_info *dbginfo, bool dawr)
{
	test_set_debugreg(child_pid);
	test_set_debugreg_kernel_userspace(child_pid);
	test_sethwdebug_exact(child_pid);
	test_sethwdebug_exact_kernel_userspace(child_pid);
	if (dbginfo->features & PPC_DEBUG_FEATURE_DATA_BP_RANGE) {
		test_sethwdebug_range_aligned(child_pid);
		if (dawr || is_8xx) {
			test_sethwdebug_range_unaligned(child_pid);
			test_sethwdebug_range_unaligned_dar(child_pid);
			test_sethwdebug_dawr_max_range(child_pid);
		}
	}
}

static int ptrace_hwbreak(void)
{
	pid_t child_pid;
	struct ppc_debug_info dbginfo;
	bool dawr;

	child_pid = fork();
	if (!child_pid) {
		test_workload();
		return 0;
	}

	wait(NULL);

	get_dbginfo(child_pid, &dbginfo);
	SKIP_IF(dbginfo.num_data_bps == 0);

	dawr = dawr_present(&dbginfo);
	run_tests(child_pid, &dbginfo, dawr);

	/* Let the child exit first. */
	ptrace(PTRACE_CONT, child_pid, NULL, 0);
	wait(NULL);

	/*
	 * Testcases exits immediately with -1 on any failure. If
	 * it has reached here, it means all tests were successful.
	 */
	return TEST_PASS;
}

int main(int argc, char **argv, char **envp)
{
	int pvr = 0;
	asm __volatile__ ("mfspr %0,%1" : "=r"(pvr) : "i"(SPRN_PVR));
	if (pvr == PVR_8xx)
		is_8xx = true;

	return test_harness(ptrace_hwbreak, "ptrace-hwbreak");
}
