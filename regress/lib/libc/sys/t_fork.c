/*	$OpenBSD: t_fork.c,v 1.5 2021/12/13 16:56:48 deraadt Exp $	*/
/*	$NetBSD: t_fork.c,v 1.4 2019/04/06 15:41:54 kamil Exp $	*/

/*-
 * Copyright (c) 2018, 2019 The NetBSD Foundation, Inc.
 * All rights reserved.
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
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */
#include "macros.h"

#include <sys/types.h>
#include <sys/signal.h>
#ifdef __OpenBSD__
#include <sys/proc.h>
#endif
#include <sys/sysctl.h>
#include <sys/wait.h>
#include <sched.h>
#include <signal.h>
#include <stdbool.h>
#include <stdlib.h>
#include <unistd.h>
#include <err.h>
#include <errno.h>

#include "atf-c.h"

#ifdef VFORK
#define FORK vfork
#else
#define FORK fork
#endif

/*
 * A child process cannot call atf functions and expect them to magically
 * work like in the parent.
 * The printf(3) messaging from a child will not work out of the box as well
 * without estabilishing a communication protocol with its parent. To not
 * overcomplicate the tests - do not log from a child and use err(3)/errx(3)
 * wrapped with ASSERT_EQ()/ASSERT_NEQ() as that is guaranteed to work.
 */
#define ASSERT_EQ(x, y)								\
do {										\
	uintmax_t vx = (x);							\
	uintmax_t vy = (y);							\
	int ret = vx == vy;							\
	if (!ret)								\
		errx(EXIT_FAILURE, "%s:%d %s(): Assertion failed for: "		\
		    "%s(%ju) == %s(%ju)", __FILE__, __LINE__, __func__,		\
		    #x, vx, #y, vy);						\
} while (/*CONSTCOND*/0)

#define ASSERT_NEQ(x, y)							\
do {										\
	uintmax_t vx = (x);							\
	uintmax_t vy = (y);							\
	int ret = vx != vy;							\
	if (!ret)								\
		errx(EXIT_FAILURE, "%s:%d %s(): Assertion failed for: "		\
		    "%s(%ju) != %s(%ju)", __FILE__, __LINE__, __func__,		\
		    #x, vx, #y, vy);						\
} while (/*CONSTCOND*/0)

static pid_t
await_stopped_child(pid_t process)
{
	struct kinfo_proc2 *p = NULL;
	size_t i, len;
	pid_t child = -1;

	int name[] = {
		[0] = CTL_KERN,
		[1] = KERN_PROC2,
		[2] = KERN_PROC_ALL,
		[3] = 0,
		[4] = sizeof(struct kinfo_proc2),
		[5] = 0
	};

	const size_t namelen = __arraycount(name);

	/* Await the process becoming a zombie */
	while(1) {
		name[5] = 0;

		ASSERT_EQ(sysctl(name, namelen, 0, &len, NULL, 0), 0);

		ASSERT_EQ(reallocarr(&p, len, sizeof(struct kinfo_proc2)), 0);

		name[5] = len;

		ASSERT_EQ(sysctl(name, namelen, p, &len, NULL, 0), 0);

		for (i = 0; i < len/sizeof(struct kinfo_proc2); i++) {
			if (p[i].p_pid == getpid())
				continue;
			if (p[i].p_ppid != process)
				continue;
			if (p[i].p_stat != LSSTOP)
				continue;
			child = p[i].p_pid;
			break;
		}

		if (child != -1)
			break;

		ASSERT_EQ(usleep(1000), 0);
	}

	/* Free the buffer */
	ASSERT_EQ(reallocarr(&p, 0, sizeof(struct kinfo_proc2)), 0);

	return child;
}

static void
raise_raw(int sig)
{
	int rv, status;
	pid_t child, parent, watcher, wpid;
	int expect_core = (sig == SIGABRT) ? 1 : 0;

	/*
	 * Spawn a dedicated thread to watch for a stopped child and emit
	 * the SIGKILL signal to it.
	 *
	 * This is required in vfork(2)ing parent and optional in fork(2).
	 *
	 * vfork(2) might clobber watcher, this means that it's safer and
	 * simpler to reparent this process to initproc and forget about it.
	 */
	if (sig == SIGSTOP
#ifndef VFORK
	    || (sig == SIGTSTP || sig == SIGTTIN || sig == SIGTTOU)
#endif
	    ) {

		parent = getpid();

		watcher = fork();
		ATF_REQUIRE(watcher != 1);
		if (watcher == 0) {
			/* Double fork(2) trick to reparent to initproc */
			watcher = fork();
			ASSERT_NEQ(watcher, -1);
			if (watcher != 0)
				_exit(0);

			child = await_stopped_child(parent);

			errno = 0;
			rv = kill(child, SIGKILL);
			ASSERT_EQ(rv, 0);
			ASSERT_EQ(errno, 0);

			/* This exit value will be collected by initproc */
			_exit(0);
		}

		wpid = waitpid(watcher, &status, 0);

		ATF_REQUIRE_EQ(wpid, watcher);

		ATF_REQUIRE(WIFEXITED(status));
		ATF_REQUIRE(!WIFCONTINUED(status));
		ATF_REQUIRE(!WIFSIGNALED(status));
		ATF_REQUIRE(!WIFSTOPPED(status));
		ATF_REQUIRE_EQ(WEXITSTATUS(status), 0);
	}

	child = FORK();
	ATF_REQUIRE(child != 1);
	if (child == 0) {
		rv = raise(sig);
		ASSERT_EQ(rv, 0);
		_exit(0);
	}
	wpid = waitpid(child, &status, 0);

	ATF_REQUIRE_EQ(wpid, child);

	switch (sig) {
	case SIGKILL:
	case SIGABRT:
	case SIGHUP:
		ATF_REQUIRE(!WIFEXITED(status));
		ATF_REQUIRE(!WIFCONTINUED(status));
		ATF_REQUIRE(WIFSIGNALED(status));
		ATF_REQUIRE(!WIFSTOPPED(status));
		ATF_REQUIRE_EQ(WTERMSIG(status), sig);
		ATF_REQUIRE_EQ(!!WCOREDUMP(status), expect_core);
		break;
#ifdef VFORK
	case SIGTSTP:
	case SIGTTIN:
	case SIGTTOU:
#endif
	case SIGCONT:
		ATF_REQUIRE(WIFEXITED(status));
		ATF_REQUIRE(!WIFCONTINUED(status));
		ATF_REQUIRE(!WIFSIGNALED(status));
		ATF_REQUIRE(!WIFSTOPPED(status));
		ATF_REQUIRE_EQ(WEXITSTATUS(status), 0);
		break;
#ifndef VFORK
	case SIGTSTP:
	case SIGTTIN:
	case SIGTTOU:
#endif
	case SIGSTOP:
		ATF_REQUIRE(!WIFEXITED(status));
		ATF_REQUIRE(!WIFCONTINUED(status));
		ATF_REQUIRE(WIFSIGNALED(status));
		ATF_REQUIRE(!WIFSTOPPED(status));
		ATF_REQUIRE_EQ(WTERMSIG(status), SIGKILL);
		ATF_REQUIRE_EQ(!!WCOREDUMP(status), 0);
	}
}

#define RAISE(test, sig)							\
ATF_TC(test);									\
ATF_TC_HEAD(test, tc)								\
{										\
										\
	atf_tc_set_md_var(tc, "descr",						\
	    "raise " #sig " in a child");					\
}										\
										\
ATF_TC_BODY(test, tc)								\
{										\
										\
	raise_raw(sig);								\
}

RAISE(raise1, SIGKILL) /* non-maskable */
RAISE(raise2, SIGSTOP) /* non-maskable */
RAISE(raise3, SIGTSTP) /* ignored in vfork(2) */
RAISE(raise4, SIGTTIN) /* ignored in vfork(2) */
RAISE(raise5, SIGTTOU) /* ignored in vfork(2) */
RAISE(raise6, SIGABRT) /* regular abort trap */
RAISE(raise7, SIGHUP)  /* hangup */
RAISE(raise8, SIGCONT) /* continued? */

/// ----------------------------------------------------------------------------

static int
clone_func(void *arg __unused)
{

	return 0;
}

static void
nested_raw(const char *fn, volatile int flags)
{
	int status;
	pid_t child, child2, wpid;
	const size_t stack_size = 1024 * 1024;
	void *stack, *stack_base;
                
	stack = malloc(stack_size);
	ATF_REQUIRE(stack != NULL);

#ifdef __MACHINE_STACK_GROWS_UP
	stack_base = stack;
#else
	stack_base = (char *)stack + stack_size;
#endif

	flags |= SIGCHLD;

	child = FORK();
	ATF_REQUIRE(child != 1);
	if (child == 0) {
		if (strcmp(fn, "fork") == 0)
			child2 = fork();
		else if (strcmp(fn, "vfork") == 0)
			child2 = vfork();
#ifndef __OpenBSD__
		else if (strcmp(fn, "clone") == 0)
			child2 = __clone(clone_func, stack_base, flags, NULL);
#endif
		else
			__unreachable();

		ASSERT_NEQ(child2, -1);

		if ((strcmp(fn, "fork") == 0) || (strcmp(fn, "vfork") == 0)) {
			if (child2 == 0)
				_exit(0);
		}

		wpid = waitpid(child2, &status, 0);
		ASSERT_EQ(child2, wpid);
		ASSERT_EQ(!!WIFEXITED(status), true);
		ASSERT_EQ(!!WIFCONTINUED(status), false);
		ASSERT_EQ(!!WIFSIGNALED(status), false);
		ASSERT_EQ(!!WIFSTOPPED(status), false);
		ASSERT_EQ(WEXITSTATUS(status), 0);

		_exit(0);
	}
	wpid = waitpid(child, &status, 0);

	ATF_REQUIRE_EQ(wpid, child);
	ATF_REQUIRE_EQ(!!WIFEXITED(status), true);
	ATF_REQUIRE_EQ(!!WIFCONTINUED(status), false);
	ATF_REQUIRE_EQ(!!WIFSIGNALED(status), false);
	ATF_REQUIRE_EQ(!!WIFSTOPPED(status), false);
	ATF_REQUIRE_EQ(WEXITSTATUS(status), 0);
}

#define NESTED(test, fn, flags)							\
ATF_TC(test);									\
ATF_TC_HEAD(test, tc)								\
{										\
										\
	atf_tc_set_md_var(tc, "descr",						\
	    "Test nested " #fn " in a child");					\
}										\
										\
ATF_TC_BODY(test, tc)								\
{										\
										\
	nested_raw(#fn, flags);							\
}

NESTED(nested_fork, fork, 0)
NESTED(nested_vfork, vfork, 0)
#ifndef __OpenBSD__
NESTED(nested_clone, clone, 0)
NESTED(nested_clone_vm, clone, CLONE_VM)
NESTED(nested_clone_fs, clone, CLONE_FS)
NESTED(nested_clone_files, clone, CLONE_FILES)
//NESTED(nested_clone_sighand, clone, CLONE_SIGHAND) // XXX
NESTED(nested_clone_vfork, clone, CLONE_VFORK)
#endif

ATF_TP_ADD_TCS(tp)
{
	ATF_TP_ADD_TC(tp, raise1);
	ATF_TP_ADD_TC(tp, raise2);
	ATF_TP_ADD_TC(tp, raise3);
	ATF_TP_ADD_TC(tp, raise4);
	ATF_TP_ADD_TC(tp, raise5);
	ATF_TP_ADD_TC(tp, raise6);
	ATF_TP_ADD_TC(tp, raise7);
	ATF_TP_ADD_TC(tp, raise8);

	ATF_TP_ADD_TC(tp, nested_fork);
	ATF_TP_ADD_TC(tp, nested_vfork);
#ifndef __OpenBSD__
	ATF_TP_ADD_TC(tp, nested_clone);
	ATF_TP_ADD_TC(tp, nested_clone_vm);
	ATF_TP_ADD_TC(tp, nested_clone_fs);
	ATF_TP_ADD_TC(tp, nested_clone_files);
//	ATF_TP_ADD_TC(tp, nested_clone_sighand); // XXX
	ATF_TP_ADD_TC(tp, nested_clone_vfork);
#endif

	return atf_no_error();
}
