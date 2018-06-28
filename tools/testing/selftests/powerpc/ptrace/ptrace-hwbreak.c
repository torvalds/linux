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
#include "ptrace.h"

/* Breakpoint access modes */
enum {
	BP_X = 1,
	BP_RW = 2,
	BP_W = 4,
};

static pid_t child_pid;
static struct ppc_debug_info dbginfo;

static void get_dbginfo(void)
{
	int ret;

	ret = ptrace(PPC_PTRACE_GETHWDBGINFO, child_pid, NULL, &dbginfo);
	if (ret) {
		perror("Can't get breakpoint info\n");
		exit(-1);
	}
}

static bool hwbreak_present(void)
{
	return (dbginfo.num_data_bps != 0);
}

static bool dawr_present(void)
{
	return !!(dbginfo.features & PPC_DEBUG_FEATURE_DATA_BP_DAWR);
}

static void set_breakpoint_addr(void *addr)
{
	int ret;

	ret = ptrace(PTRACE_SET_DEBUGREG, child_pid, 0, addr);
	if (ret) {
		perror("Can't set breakpoint addr\n");
		exit(-1);
	}
}

static int set_hwbreakpoint_addr(void *addr, int range)
{
	int ret;

	struct ppc_hw_breakpoint info;

	info.version = 1;
	info.trigger_type = PPC_BREAKPOINT_TRIGGER_RW;
	info.addr_mode = PPC_BREAKPOINT_MODE_EXACT;
	if (range > 0)
		info.addr_mode = PPC_BREAKPOINT_MODE_RANGE_INCLUSIVE;
	info.condition_mode = PPC_BREAKPOINT_CONDITION_NONE;
	info.addr = (__u64)addr;
	info.addr2 = (__u64)addr + range;
	info.condition_value = 0;

	ret = ptrace(PPC_PTRACE_SETHWDEBUG, child_pid, 0, &info);
	if (ret < 0) {
		perror("Can't set breakpoint\n");
		exit(-1);
	}
	return ret;
}

static int del_hwbreakpoint_addr(int watchpoint_handle)
{
	int ret;

	ret = ptrace(PPC_PTRACE_DELHWDEBUG, child_pid, 0, watchpoint_handle);
	if (ret < 0) {
		perror("Can't delete hw breakpoint\n");
		exit(-1);
	}
	return ret;
}

#define DAWR_LENGTH_MAX 512

/* Dummy variables to test read/write accesses */
static unsigned long long
	dummy_array[DAWR_LENGTH_MAX / sizeof(unsigned long long)]
	__attribute__((aligned(512)));
static unsigned long long *dummy_var = dummy_array;

static void write_var(int len)
{
	long long *plval;
	char *pcval;
	short *psval;
	int *pival;

	switch (len) {
	case 1:
		pcval = (char *)dummy_var;
		*pcval = 0xff;
		break;
	case 2:
		psval = (short *)dummy_var;
		*psval = 0xffff;
		break;
	case 4:
		pival = (int *)dummy_var;
		*pival = 0xffffffff;
		break;
	case 8:
		plval = (long long *)dummy_var;
		*plval = 0xffffffffffffffffLL;
		break;
	}
}

static void read_var(int len)
{
	char cval __attribute__((unused));
	short sval __attribute__((unused));
	int ival __attribute__((unused));
	long long lval __attribute__((unused));

	switch (len) {
	case 1:
		cval = *(char *)dummy_var;
		break;
	case 2:
		sval = *(short *)dummy_var;
		break;
	case 4:
		ival = *(int *)dummy_var;
		break;
	case 8:
		lval = *(long long *)dummy_var;
		break;
	}
}

/*
 * Do the r/w accesses to trigger the breakpoints. And run
 * the usual traps.
 */
static void trigger_tests(void)
{
	int len, ret;

	ret = ptrace(PTRACE_TRACEME, 0, NULL, 0);
	if (ret) {
		perror("Can't be traced?\n");
		return;
	}

	/* Wake up father so that it sets up the first test */
	kill(getpid(), SIGUSR1);

	/* Test write watchpoints */
	for (len = 1; len <= sizeof(long); len <<= 1)
		write_var(len);

	/* Test read/write watchpoints (on read accesses) */
	for (len = 1; len <= sizeof(long); len <<= 1)
		read_var(len);

	/* Test when breakpoint is unset */

	/* Test write watchpoints */
	for (len = 1; len <= sizeof(long); len <<= 1)
		write_var(len);

	/* Test read/write watchpoints (on read accesses) */
	for (len = 1; len <= sizeof(long); len <<= 1)
		read_var(len);
}

static void check_success(const char *msg)
{
	const char *msg2;
	int status;

	/* Wait for the child to SIGTRAP */
	wait(&status);

	msg2 = "Failed";

	if (WIFSTOPPED(status) && WSTOPSIG(status) == SIGTRAP) {
		msg2 = "Child process hit the breakpoint";
	}

	printf("%s Result: [%s]\n", msg, msg2);
}

static void launch_watchpoints(char *buf, int mode, int len,
			       struct ppc_debug_info *dbginfo, bool dawr)
{
	const char *mode_str;
	unsigned long data = (unsigned long)(dummy_var);
	int wh, range;

	data &= ~0x7UL;

	if (mode == BP_W) {
		data |= (1UL << 1);
		mode_str = "write";
	} else {
		data |= (1UL << 0);
		data |= (1UL << 1);
		mode_str = "read";
	}

	/* Set DABR_TRANSLATION bit */
	data |= (1UL << 2);

	/* use PTRACE_SET_DEBUGREG breakpoints */
	set_breakpoint_addr((void *)data);
	ptrace(PTRACE_CONT, child_pid, NULL, 0);
	sprintf(buf, "Test %s watchpoint with len: %d ", mode_str, len);
	check_success(buf);
	/* Unregister hw brkpoint */
	set_breakpoint_addr(NULL);

	data = (data & ~7); /* remove dabr control bits */

	/* use PPC_PTRACE_SETHWDEBUG breakpoint */
	if (!(dbginfo->features & PPC_DEBUG_FEATURE_DATA_BP_RANGE))
		return; /* not supported */
	wh = set_hwbreakpoint_addr((void *)data, 0);
	ptrace(PTRACE_CONT, child_pid, NULL, 0);
	sprintf(buf, "Test %s watchpoint with len: %d ", mode_str, len);
	check_success(buf);
	/* Unregister hw brkpoint */
	del_hwbreakpoint_addr(wh);

	/* try a wider range */
	range = 8;
	if (dawr)
		range = 512 - ((int)data & (DAWR_LENGTH_MAX - 1));
	wh = set_hwbreakpoint_addr((void *)data, range);
	ptrace(PTRACE_CONT, child_pid, NULL, 0);
	sprintf(buf, "Test %s watchpoint with len: %d ", mode_str, len);
	check_success(buf);
	/* Unregister hw brkpoint */
	del_hwbreakpoint_addr(wh);
}

/* Set the breakpoints and check the child successfully trigger them */
static int launch_tests(bool dawr)
{
	char buf[1024];
	int len, i, status;

	struct ppc_debug_info dbginfo;

	i = ptrace(PPC_PTRACE_GETHWDBGINFO, child_pid, NULL, &dbginfo);
	if (i) {
		perror("Can't set breakpoint info\n");
		exit(-1);
	}
	if (!(dbginfo.features & PPC_DEBUG_FEATURE_DATA_BP_RANGE))
		printf("WARNING: Kernel doesn't support PPC_PTRACE_SETHWDEBUG\n");

	/* Write watchpoint */
	for (len = 1; len <= sizeof(long); len <<= 1)
		launch_watchpoints(buf, BP_W, len, &dbginfo, dawr);

	/* Read-Write watchpoint */
	for (len = 1; len <= sizeof(long); len <<= 1)
		launch_watchpoints(buf, BP_RW, len, &dbginfo, dawr);

	ptrace(PTRACE_CONT, child_pid, NULL, 0);

	/*
	 * Now we have unregistered the breakpoint, access by child
	 * should not cause SIGTRAP.
	 */

	wait(&status);

	if (WIFSTOPPED(status) && WSTOPSIG(status) == SIGTRAP) {
		printf("FAIL: Child process hit the breakpoint, which is not expected\n");
		ptrace(PTRACE_CONT, child_pid, NULL, 0);
		return TEST_FAIL;
	}

	if (WIFEXITED(status))
		printf("Child exited normally\n");

	return TEST_PASS;
}

static int ptrace_hwbreak(void)
{
	pid_t pid;
	int ret;
	bool dawr;

	pid = fork();
	if (!pid) {
		trigger_tests();
		return 0;
	}

	wait(NULL);

	child_pid = pid;

	get_dbginfo();
	SKIP_IF(!hwbreak_present());
	dawr = dawr_present();

	ret = launch_tests(dawr);

	wait(NULL);

	return ret;
}

int main(int argc, char **argv, char **envp)
{
	return test_harness(ptrace_hwbreak, "ptrace-hwbreak");
}
