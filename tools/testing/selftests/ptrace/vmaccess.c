// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (c) 2020 Bernd Edlinger <bernd.edlinger@hotmail.de>
 * All rights reserved.
 *
 * Check whether /proc/$pid/mem can be accessed without causing deadlocks
 * when de_thread is blocked with ->cred_guard_mutex held.
 */

#include "../kselftest_harness.h"
#include <stdio.h>
#include <fcntl.h>
#include <pthread.h>
#include <signal.h>
#include <unistd.h>
#include <sys/ptrace.h>

static void *thread(void *arg)
{
	ptrace(PTRACE_TRACEME, 0, 0L, 0L);
	return NULL;
}

TEST(vmaccess)
{
	int f, pid = fork();
	char mm[64];

	if (!pid) {
		pthread_t pt;

		pthread_create(&pt, NULL, thread, NULL);
		pthread_join(pt, NULL);
		execlp("true", "true", NULL);
	}

	sleep(1);
	sprintf(mm, "/proc/%d/mem", pid);
	f = open(mm, O_RDONLY);
	ASSERT_GE(f, 0);
	close(f);
	f = kill(pid, SIGCONT);
	ASSERT_EQ(f, 0);
}

TEST(attach)
{
	int s, k, pid = fork();

	if (!pid) {
		pthread_t pt;

		pthread_create(&pt, NULL, thread, NULL);
		pthread_join(pt, NULL);
		execlp("sleep", "sleep", "2", NULL);
	}

	sleep(1);
	k = ptrace(PTRACE_ATTACH, pid, 0L, 0L);
	ASSERT_EQ(errno, EAGAIN);
	ASSERT_EQ(k, -1);
	k = waitpid(-1, &s, WNOHANG);
	ASSERT_NE(k, -1);
	ASSERT_NE(k, 0);
	ASSERT_NE(k, pid);
	ASSERT_EQ(WIFEXITED(s), 1);
	ASSERT_EQ(WEXITSTATUS(s), 0);
	sleep(1);
	k = ptrace(PTRACE_ATTACH, pid, 0L, 0L);
	ASSERT_EQ(k, 0);
	k = waitpid(-1, &s, 0);
	ASSERT_EQ(k, pid);
	ASSERT_EQ(WIFSTOPPED(s), 1);
	ASSERT_EQ(WSTOPSIG(s), SIGSTOP);
	k = ptrace(PTRACE_DETACH, pid, 0L, 0L);
	ASSERT_EQ(k, 0);
	k = waitpid(-1, &s, 0);
	ASSERT_EQ(k, pid);
	ASSERT_EQ(WIFEXITED(s), 1);
	ASSERT_EQ(WEXITSTATUS(s), 0);
	k = waitpid(-1, NULL, 0);
	ASSERT_EQ(k, -1);
	ASSERT_EQ(errno, ECHILD);
}

TEST_HARNESS_MAIN
