// SPDX-License-Identifier: GPL-2.0
#define _GNU_SOURCE
#include "../kselftest_harness.h"
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <sys/wait.h>
#include <sys/syscall.h>
#include <sys/prctl.h>

#include "linux/ptrace.h"

static int sys_ptrace(int request, pid_t pid, void *addr, void *data)
{
	return syscall(SYS_ptrace, request, pid, addr, data);
}

TEST(get_set_sud)
{
	struct ptrace_sud_config config;
	pid_t child;
	int ret = 0;
	int status;

	child = fork();
	ASSERT_GE(child, 0);
	if (child == 0) {
		ASSERT_EQ(0, sys_ptrace(PTRACE_TRACEME, 0, 0, 0)) {
			TH_LOG("PTRACE_TRACEME: %m");
		}
		kill(getpid(), SIGSTOP);
		_exit(1);
	}

	waitpid(child, &status, 0);

	memset(&config, 0xff, sizeof(config));
	config.mode = PR_SYS_DISPATCH_ON;

	ret = sys_ptrace(PTRACE_GET_SYSCALL_USER_DISPATCH_CONFIG, child,
			 (void *)sizeof(config), &config);

	ASSERT_EQ(ret, 0);
	ASSERT_EQ(config.mode, PR_SYS_DISPATCH_OFF);
	ASSERT_EQ(config.selector, 0);
	ASSERT_EQ(config.offset, 0);
	ASSERT_EQ(config.len, 0);

	config.mode = PR_SYS_DISPATCH_ON;
	config.selector = 0;
	config.offset = 0x400000;
	config.len = 0x1000;

	ret = sys_ptrace(PTRACE_SET_SYSCALL_USER_DISPATCH_CONFIG, child,
			 (void *)sizeof(config), &config);

	ASSERT_EQ(ret, 0);

	memset(&config, 1, sizeof(config));
	ret = sys_ptrace(PTRACE_GET_SYSCALL_USER_DISPATCH_CONFIG, child,
			 (void *)sizeof(config), &config);

	ASSERT_EQ(ret, 0);
	ASSERT_EQ(config.mode, PR_SYS_DISPATCH_ON);
	ASSERT_EQ(config.selector, 0);
	ASSERT_EQ(config.offset, 0x400000);
	ASSERT_EQ(config.len, 0x1000);

	kill(child, SIGKILL);
}

TEST_HARNESS_MAIN
