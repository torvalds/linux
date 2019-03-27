/*-
 * Copyright (c) 2018 Thomas Munro
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <assert.h>
#include <atf-c.h>
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>
#include <sys/procctl.h>
#include <sys/ptrace.h>
#include <sys/signal.h>
#include <sys/types.h>

static void
dummy_signal_handler(int signum)
{
}

ATF_TC_WITHOUT_HEAD(arg_validation);
ATF_TC_BODY(arg_validation, tc)
{
	int signum;
	int rc;

	/* bad signal */
	signum = 8888;
	rc = procctl(P_PID, 0, PROC_PDEATHSIG_CTL, &signum);
	ATF_CHECK_EQ(-1, rc);
	ATF_CHECK_EQ(EINVAL, errno);

	/* bad id type */
	signum = SIGINFO;
	rc = procctl(8888, 0, PROC_PDEATHSIG_CTL, &signum);
	ATF_CHECK_EQ(-1, rc);
	ATF_CHECK_EQ(EINVAL, errno);

	/* bad id (pid that doesn't match mine or zero) */
	signum = SIGINFO;
	rc = procctl(P_PID, (((getpid() + 1) % 10) + 100),
	    PROC_PDEATHSIG_CTL, &signum);
	ATF_CHECK_EQ(-1, rc);
	ATF_CHECK_EQ(EINVAL, errno);

	/* null pointer */
	signum = SIGINFO;
	rc = procctl(P_PID, 0, PROC_PDEATHSIG_CTL, NULL);
	ATF_CHECK_EQ(-1, rc);
	ATF_CHECK_EQ(EFAULT, errno);

	/* good (pid == 0) */
	signum = SIGINFO;
	rc = procctl(P_PID, 0, PROC_PDEATHSIG_CTL, &signum);
	ATF_CHECK_EQ(0, rc);

	/* good (pid == my pid) */
	signum = SIGINFO;
	rc = procctl(P_PID, getpid(), PROC_PDEATHSIG_CTL, &signum);
	ATF_CHECK_EQ(0, rc);

	/* check that we can read the signal number back */
	signum = 0xdeadbeef;
	rc = procctl(P_PID, 0, PROC_PDEATHSIG_STATUS, &signum);
	ATF_CHECK_EQ(0, rc);
	ATF_CHECK_EQ(SIGINFO, signum);
}

ATF_TC_WITHOUT_HEAD(fork_no_inherit);
ATF_TC_BODY(fork_no_inherit, tc)
{
	int status;
	int signum;
	int rc;

	/* request a signal on parent death in the parent */
	signum = SIGINFO;
	rc = procctl(P_PID, 0, PROC_PDEATHSIG_CTL, &signum);

	rc = fork();
	ATF_REQUIRE(rc != -1);
	if (rc == 0) {
		/* check that we didn't inherit the setting */
		signum = 0xdeadbeef;
		rc = procctl(P_PID, 0, PROC_PDEATHSIG_STATUS, &signum);
		assert(rc == 0);
		assert(signum == 0);
		_exit(0);
	}

	/* wait for the child to exit successfully */
	waitpid(rc, &status, 0);
	ATF_CHECK_EQ(0, status);
}

ATF_TC_WITHOUT_HEAD(exec_inherit);
ATF_TC_BODY(exec_inherit, tc)
{
	int status;
	int rc;

	rc = fork();
	ATF_REQUIRE(rc != -1);
	if (rc == 0) {
		char exec_path[1024];
		int signum;

		/* compute the path of the helper executable */
		snprintf(exec_path, sizeof(exec_path), "%s/pdeathsig_helper",
			 atf_tc_get_config_var(tc, "srcdir"));

		/* request a signal on parent death and register a handler */
		signum = SIGINFO;
		rc = procctl(P_PID, 0, PROC_PDEATHSIG_CTL, &signum);
		assert(rc == 0);

		/* execute helper program: it asserts that it has the setting */
		rc = execl(exec_path, exec_path, NULL);
		assert(rc == 0);
		_exit(0);
	}

	/* wait for the child to exit successfully */
	waitpid(rc, &status, 0);
	ATF_CHECK_EQ(0, status);
}

ATF_TC_WITHOUT_HEAD(signal_delivered);
ATF_TC_BODY(signal_delivered, tc)
{
	sigset_t sigset;
	int signum;
	int rc;
	int pipe_ca[2];
	int pipe_cb[2];
	char buffer;

	rc = pipe(pipe_ca);
	ATF_REQUIRE(rc == 0);
	rc = pipe(pipe_cb);
	ATF_REQUIRE(rc == 0);

	rc = fork();
	ATF_REQUIRE(rc != -1);
	if (rc == 0) {
		rc = fork();
		assert(rc >= 0);
		if (rc == 0) {
			/* process C */
			signum = SIGINFO;

			/* block signals so we can handle them synchronously */
			rc = sigfillset(&sigset);
			assert(rc == 0);
			rc = sigprocmask(SIG_SETMASK, &sigset, NULL);
			assert(rc == 0);

			/* register a dummy handler or the kernel will not queue it */
			signal(signum, dummy_signal_handler);

			/* request a signal on death of our parent B */
			rc = procctl(P_PID, 0, PROC_PDEATHSIG_CTL, &signum);
			assert(rc == 0);

			/* tell B that we're ready for it to exit now */
			rc = write(pipe_cb[1], ".", 1);
			assert(rc == 1);

			/* wait for B to die and signal us... */
			signum = 0xdeadbeef;
			rc = sigwait(&sigset, &signum);
			assert(rc == 0);
			assert(signum == SIGINFO);

			/* tell A the test passed */
			rc = write(pipe_ca[1], ".", 1);
			assert(rc == 1);
			_exit(0);
		}

		/* process B */

		/* wait for C to tell us it is ready for us to exit */
		rc = read(pipe_cb[0], &buffer, 1);
		assert(rc == 1);

		/* now we exit so that C gets a signal */
		_exit(0);
	}
	/* process A */

	/* wait for C to tell us the test passed */
	rc = read(pipe_ca[0], &buffer, 1);
	ATF_CHECK_EQ(1, rc);
}

ATF_TC_WITHOUT_HEAD(signal_delivered_ptrace);
ATF_TC_BODY(signal_delivered_ptrace, tc)
{
	sigset_t sigset;
	int signum;
	int rc;
	int pipe_ca[2];
	int pipe_db[2];
	char buffer;
	int status;

	rc = pipe(pipe_ca);
	ATF_REQUIRE(rc == 0);
	rc = pipe(pipe_db);
	ATF_REQUIRE(rc == 0);

	rc = fork();
	ATF_REQUIRE(rc != -1);
	if (rc == 0) {
		pid_t c_pid;

		/* process B */

		rc = fork();
		assert(rc >= 0);
		if (rc == 0) {
			/* process C */
			signum = SIGINFO;

			/* block signals so we can handle them synchronously */
			rc = sigfillset(&sigset);
			assert(rc == 0);
			rc = sigprocmask(SIG_SETMASK, &sigset, NULL);
			assert(rc == 0);

			/* register a dummy handler or the kernel will not queue it */
			signal(signum, dummy_signal_handler);

			/* request a signal on parent death and register a handler */
			rc = procctl(P_PID, 0, PROC_PDEATHSIG_CTL, &signum);
			assert(rc == 0);

			/* wait for B to die and signal us... */
			signum = 0xdeadbeef;
			rc = sigwait(&sigset, &signum);
			assert(rc == 0);
			assert(signum == SIGINFO);

			/* tell A the test passed */
			rc = write(pipe_ca[1], ".", 1);
			assert(rc == 1);
			_exit(0);
		}
		c_pid = rc;


		/* fork another process to ptrace C */
		rc = fork();
		assert(rc >= 0);
		if (rc == 0) {

			/* process D */
			rc = ptrace(PT_ATTACH, c_pid, 0, 0);
			assert(rc == 0);

			waitpid(c_pid, &status, 0);
			assert(WIFSTOPPED(status));
			assert(WSTOPSIG(status) == SIGSTOP);

			rc = ptrace(PT_CONTINUE, c_pid, (caddr_t) 1, 0);
			assert(rc == 0);

			/* tell B that we're ready for it to exit now */
			rc = write(pipe_db[1], ".", 1);
			assert(rc == 1);

			waitpid(c_pid, &status, 0);
			assert(WIFSTOPPED(status));
			assert(WSTOPSIG(status) == SIGINFO);

			rc = ptrace(PT_CONTINUE, c_pid, (caddr_t) 1,
				    WSTOPSIG(status));
			assert(rc == 0);

			ptrace(PT_DETACH, c_pid, 0, 0);

			_exit(0);
		}

		/* wait for D to tell us it is ready for us to exit */
		rc = read(pipe_db[0], &buffer, 1);
		assert(rc == 1);

		/* now we exit so that C gets a signal */
		_exit(0);
	}

	/* process A */

	/* wait for C to tell us the test passed */
	rc = read(pipe_ca[0], &buffer, 1);
	ATF_CHECK_EQ(1, rc);
}

ATF_TP_ADD_TCS(tp)
{
	ATF_TP_ADD_TC(tp, arg_validation);
	ATF_TP_ADD_TC(tp, fork_no_inherit);
	ATF_TP_ADD_TC(tp, exec_inherit);
	ATF_TP_ADD_TC(tp, signal_delivered);
	ATF_TP_ADD_TC(tp, signal_delivered_ptrace);
	return (atf_no_error());
}
