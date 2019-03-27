/*-
 * Copyright (c) 2006 nCircle Network Security, Inc.
 * Copyright (c) 2007 Robert N. M. Watson
 * All rights reserved.
 *
 * This software was developed by Robert N. M. Watson for the TrustedBSD
 * Project under contract to nCircle Network Security, Inc.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR, NCIRCLE NETWORK SECURITY,
 * INC., OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED
 * TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * $FreeBSD$
 */

/*
 * Test that privilege is required to lower nice value; first test with, then
 * without.  There are two failure modes associated with privilege: the right
 * to renice a process with a different uid, and the right to renice to a
 * lower priority.  Because both the real and effective uid are part of the
 * permissions test, we have to create two children processes with different
 * uids.
 */

#include <sys/types.h>
#include <sys/resource.h>
#include <sys/wait.h>

#include <err.h>
#include <errno.h>
#include <signal.h>
#include <stdlib.h>
#include <unistd.h>

#include "main.h"

static int	childproc_running;
static pid_t	childproc;

int
priv_sched_setpriority_setup(int asroot, int injail, struct test *test)
{
	int another_uid, need_child;

	/*
	 * Some tests require a second process with specific credentials.
	 * Set that up here, and kill in cleanup.
	 */
	need_child = 0;
	if (test->t_test_func == priv_sched_setpriority_aproc) {
		need_child = 1;
		another_uid = 1;
	}
	if (test->t_test_func == priv_sched_setpriority_myproc)
		need_child = 1;

	if (need_child) {
		childproc = fork();
		if (childproc < 0) {
			warn("priv_sched_setup: fork");
			return (-1);
		}
		if (childproc == 0) {
			if (another_uid) {
				if (setresuid(UID_THIRD, UID_THIRD,
				    UID_THIRD) < 0)
				err(-1, "setresuid(%d)", UID_THIRD);
			}
			while (1)
				sleep(1);
		}
		childproc_running = 1;
		sleep(1);	/* Allow dummy thread to change uids. */
	}
	return (0);
}

void
priv_sched_setpriority_curproc(int asroot, int injail, struct test *test)
{
	int error;

	error = setpriority(PRIO_PROCESS, 0, -1);
	if (asroot && injail)
		expect("priv_sched_setpriority_curproc(asroot, injail)",
		    error, -1, EACCES);
	if (asroot && !injail)
		expect("priv_sched_setpriority_curproc(asroot, !injail)",
		    error, 0, 0);
	if (!asroot && injail)
		expect("priv_sched_setpriority_curproc(!asroot, injail)",
		    error, -1, EACCES);
	if (!asroot && !injail)
		expect("priv_sched_setpriority_curproc(!asroot, !injail)",
		    error, -1, EACCES);
}

void
priv_sched_setpriority_myproc(int asroot, int injail, struct test *test)
{
	int error;

	error = setpriority(PRIO_PROCESS, 0, -1);
	if (asroot && injail)
		expect("priv_sched_setpriority_myproc(asroot, injail)",
		    error, -1, EACCES);
	if (asroot && !injail)
		expect("priv_sched_setpriority_myproc(asroot, !injail)",
		    error, 0, 0);
	if (!asroot && injail)
		expect("priv_sched_setpriority_myproc(!asroot, injail)",
		    error, -1, EACCES);
	if (!asroot && !injail)
		expect("priv_sched_setpriority_myproc(!asroot, !injail)",
		    error, -1, EACCES);
}

void
priv_sched_setpriority_aproc(int asroot, int injail, struct test *test)
{
	int error;

	error = setpriority(PRIO_PROCESS, 0, -1);
	if (asroot && injail)
		expect("priv_sched_setpriority_aproc(asroot, injail)",
		    error, -1, EACCES);
	if (asroot && !injail)
		expect("priv_sched_setpriority_aproc(asroot, !injail)",
		    error, 0, 0);
	if (!asroot && injail)
		expect("priv_sched_setpriority_aproc(!asroot, injail)",
		    error, -1, EACCES);
	if (!asroot && !injail)
		expect("priv_sched_setpriority_aproc(!asroot, !injail)",
		    error, -1, EACCES);
}

void
priv_sched_setpriority_cleanup(int asroot, int injail, struct test *test)
{
	pid_t pid;

	if (childproc_running) {
		(void)kill(childproc, SIGKILL);
		while (1) {
			pid = waitpid(childproc, NULL, 0);
			if (pid == -1)
				warn("waitpid(%d (test), NULL, 0)",
				    childproc);
			if (pid == childproc)
				break;
		}
		childproc_running = 0;
		childproc = -1;
	}
}
