/*-
 * Copyright (c) 2016 Jilles Tjoelker
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

#include <sys/procctl.h>
#include <sys/procdesc.h>
#include <sys/wait.h>

#include <atf-c.h>
#include <errno.h>
#include <signal.h>
#include <unistd.h>

static void
dummy_sighandler(int sig __unused, siginfo_t *info __unused, void *ctx __unused)
{
}

ATF_TC_WITHOUT_HEAD(reaper_wait_child_first);
ATF_TC_BODY(reaper_wait_child_first, tc)
{
	pid_t parent, child, grandchild, pid;
	int status, r;
	int pip[2];

	/* Be paranoid. */
	pid = waitpid(-1, NULL, WNOHANG);
	ATF_REQUIRE(pid == -1 && errno == ECHILD);

	parent = getpid();
	r = procctl(P_PID, parent, PROC_REAP_ACQUIRE, NULL);
	ATF_REQUIRE_EQ(0, r);

	r = pipe(pip);
	ATF_REQUIRE_EQ(0, r);

	child = fork();
	ATF_REQUIRE(child != -1);
	if (child == 0) {
		if (close(pip[1]) != 0)
			_exit(100);
		grandchild = fork();
		if (grandchild == -1)
			_exit(101);
		else if (grandchild == 0) {
			if (read(pip[0], &(uint8_t){ 0 }, 1) != 0)
				_exit(102);
			if (getppid() != parent)
				_exit(103);
			_exit(2);
		} else
			_exit(3);
	}

	pid = waitpid(child, &status, 0);
	ATF_REQUIRE_EQ(child, pid);
	r = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
	ATF_CHECK_EQ(3, r);

	r = close(pip[1]);
	ATF_REQUIRE_EQ(0, r);

	pid = waitpid(-1, &status, 0);
	ATF_REQUIRE(pid > 0 && pid != child);
	r = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
	ATF_CHECK_EQ(2, r);

	r = close(pip[0]);
	ATF_REQUIRE_EQ(0, r);
}

ATF_TC_WITHOUT_HEAD(reaper_wait_grandchild_first);
ATF_TC_BODY(reaper_wait_grandchild_first, tc)
{
	pid_t parent, child, grandchild, pid;
	int status, r;

	/* Be paranoid. */
	pid = waitpid(-1, NULL, WNOHANG);
	ATF_REQUIRE(pid == -1 && errno == ECHILD);

	parent = getpid();
	r = procctl(P_PID, parent, PROC_REAP_ACQUIRE, NULL);
	ATF_REQUIRE_EQ(0, r);

	child = fork();
	ATF_REQUIRE(child != -1);
	if (child == 0) {
		grandchild = fork();
		if (grandchild == -1)
			_exit(101);
		else if (grandchild == 0)
			_exit(2);
		else {
			if (waitid(P_PID, grandchild, NULL,
			    WNOWAIT | WEXITED) != 0)
				_exit(102);
			_exit(3);
		}
	}

	pid = waitpid(child, &status, 0);
	ATF_REQUIRE_EQ(child, pid);
	r = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
	ATF_CHECK_EQ(3, r);

	pid = waitpid(-1, &status, 0);
	ATF_REQUIRE(pid > 0 && pid != child);
	r = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
	ATF_CHECK_EQ(2, r);
}

ATF_TC(reaper_sigchld_child_first);
ATF_TC_HEAD(reaper_sigchld_child_first, tc)
{
	atf_tc_set_md_var(tc, "timeout", "2");
}
ATF_TC_BODY(reaper_sigchld_child_first, tc)
{
	struct sigaction act;
	sigset_t mask;
	siginfo_t info;
	pid_t parent, child, grandchild, pid;
	int r;
	int pip[2];

	/* Be paranoid. */
	pid = waitpid(-1, NULL, WNOHANG);
	ATF_REQUIRE(pid == -1 && errno == ECHILD);

	act.sa_sigaction = dummy_sighandler;
	act.sa_flags = SA_SIGINFO | SA_RESTART;
	r = sigemptyset(&act.sa_mask);
	ATF_REQUIRE_EQ(0, r);
	r = sigaction(SIGCHLD, &act, NULL);
	ATF_REQUIRE_EQ(0, r);

	r = sigemptyset(&mask);
	ATF_REQUIRE_EQ(0, r);
	r = sigaddset(&mask, SIGCHLD);
	ATF_REQUIRE_EQ(0, r);
	r = sigprocmask(SIG_BLOCK, &mask, NULL);
	ATF_REQUIRE_EQ(0, r);

	parent = getpid();
	r = procctl(P_PID, parent, PROC_REAP_ACQUIRE, NULL);
	ATF_REQUIRE_EQ(0, r);

	r = pipe(pip);
	ATF_REQUIRE_EQ(0, r);

	child = fork();
	ATF_REQUIRE(child != -1);
	if (child == 0) {
		if (close(pip[1]) != 0)
			_exit(100);
		grandchild = fork();
		if (grandchild == -1)
			_exit(101);
		else if (grandchild == 0) {
			if (read(pip[0], &(uint8_t){ 0 }, 1) != 0)
				_exit(102);
			if (getppid() != parent)
				_exit(103);
			_exit(2);
		} else
			_exit(3);
	}

	r = sigwaitinfo(&mask, &info);
	ATF_REQUIRE_EQ(SIGCHLD, r);
	ATF_CHECK_EQ(SIGCHLD, info.si_signo);
	ATF_CHECK_EQ(CLD_EXITED, info.si_code);
	ATF_CHECK_EQ(3, info.si_status);
	ATF_CHECK_EQ(child, info.si_pid);

	pid = waitpid(child, NULL, 0);
	ATF_REQUIRE_EQ(child, pid);

	r = close(pip[1]);
	ATF_REQUIRE_EQ(0, r);

	r = sigwaitinfo(&mask, &info);
	ATF_REQUIRE_EQ(SIGCHLD, r);
	ATF_CHECK_EQ(SIGCHLD, info.si_signo);
	ATF_CHECK_EQ(CLD_EXITED, info.si_code);
	ATF_CHECK_EQ(2, info.si_status);
	grandchild = info.si_pid;
	ATF_REQUIRE(grandchild > 0);
	ATF_REQUIRE(grandchild != parent);
	ATF_REQUIRE(grandchild != child);

	pid = waitpid(-1, NULL, 0);
	ATF_REQUIRE_EQ(grandchild, pid);

	r = close(pip[0]);
	ATF_REQUIRE_EQ(0, r);
}

ATF_TC(reaper_sigchld_grandchild_first);
ATF_TC_HEAD(reaper_sigchld_grandchild_first, tc)
{
	atf_tc_set_md_var(tc, "timeout", "2");
}
ATF_TC_BODY(reaper_sigchld_grandchild_first, tc)
{
	struct sigaction act;
	sigset_t mask;
	siginfo_t info;
	pid_t parent, child, grandchild, pid;
	int r;

	/* Be paranoid. */
	pid = waitpid(-1, NULL, WNOHANG);
	ATF_REQUIRE(pid == -1 && errno == ECHILD);

	act.sa_sigaction = dummy_sighandler;
	act.sa_flags = SA_SIGINFO | SA_RESTART;
	r = sigemptyset(&act.sa_mask);
	ATF_REQUIRE_EQ(0, r);
	r = sigaction(SIGCHLD, &act, NULL);
	ATF_REQUIRE_EQ(0, r);

	r = sigemptyset(&mask);
	ATF_REQUIRE_EQ(0, r);
	r = sigaddset(&mask, SIGCHLD);
	ATF_REQUIRE_EQ(0, r);
	r = sigprocmask(SIG_BLOCK, &mask, NULL);
	ATF_REQUIRE_EQ(0, r);

	parent = getpid();
	r = procctl(P_PID, parent, PROC_REAP_ACQUIRE, NULL);
	ATF_REQUIRE_EQ(0, r);

	child = fork();
	ATF_REQUIRE(child != -1);
	if (child == 0) {
		grandchild = fork();
		if (grandchild == -1)
			_exit(101);
		else if (grandchild == 0)
			_exit(2);
		else {
			if (waitid(P_PID, grandchild, NULL,
			    WNOWAIT | WEXITED) != 0)
				_exit(102);
			_exit(3);
		}
	}

	pid = waitpid(child, NULL, 0);
	ATF_REQUIRE_EQ(child, pid);

	r = sigwaitinfo(&mask, &info);
	ATF_REQUIRE_EQ(SIGCHLD, r);
	ATF_CHECK_EQ(SIGCHLD, info.si_signo);
	ATF_CHECK_EQ(CLD_EXITED, info.si_code);
	ATF_CHECK_EQ(2, info.si_status);
	grandchild = info.si_pid;
	ATF_REQUIRE(grandchild > 0);
	ATF_REQUIRE(grandchild != parent);
	ATF_REQUIRE(grandchild != child);

	pid = waitpid(-1, NULL, 0);
	ATF_REQUIRE_EQ(grandchild, pid);
}

ATF_TC_WITHOUT_HEAD(reaper_status);
ATF_TC_BODY(reaper_status, tc)
{
	struct procctl_reaper_status st;
	ssize_t sr;
	pid_t parent, child, pid;
	int r, status;
	int pip[2];

	parent = getpid();
	r = procctl(P_PID, parent, PROC_REAP_STATUS, &st);
	ATF_REQUIRE_EQ(0, r);
	ATF_CHECK_EQ(0, st.rs_flags & REAPER_STATUS_OWNED);
	ATF_CHECK(st.rs_children > 0);
	ATF_CHECK(st.rs_descendants > 0);
	ATF_CHECK(st.rs_descendants >= st.rs_children);
	ATF_CHECK(st.rs_reaper != parent);
	ATF_CHECK(st.rs_reaper > 0);

	r = procctl(P_PID, parent, PROC_REAP_ACQUIRE, NULL);
	ATF_REQUIRE_EQ(0, r);

	r = procctl(P_PID, parent, PROC_REAP_STATUS, &st);
	ATF_REQUIRE_EQ(0, r);
	ATF_CHECK_EQ(REAPER_STATUS_OWNED,
	    st.rs_flags & (REAPER_STATUS_OWNED | REAPER_STATUS_REALINIT));
	ATF_CHECK_EQ(0, st.rs_children);
	ATF_CHECK_EQ(0, st.rs_descendants);
	ATF_CHECK(st.rs_reaper == parent);
	ATF_CHECK_EQ(-1, st.rs_pid);

	r = pipe(pip);
	ATF_REQUIRE_EQ(0, r);
	child = fork();
	ATF_REQUIRE(child != -1);
	if (child == 0) {
		if (close(pip[0]) != 0)
			_exit(100);
		if (procctl(P_PID, parent, PROC_REAP_STATUS, &st) != 0)
			_exit(101);
		if (write(pip[1], &st, sizeof(st)) != (ssize_t)sizeof(st))
			_exit(102);
		if (procctl(P_PID, getpid(), PROC_REAP_STATUS, &st) != 0)
			_exit(103);
		if (write(pip[1], &st, sizeof(st)) != (ssize_t)sizeof(st))
			_exit(104);
		_exit(0);
	}
	r = close(pip[1]);
	ATF_REQUIRE_EQ(0, r);

	sr = read(pip[0], &st, sizeof(st));
	ATF_REQUIRE_EQ((ssize_t)sizeof(st), sr);
	ATF_CHECK_EQ(REAPER_STATUS_OWNED,
	    st.rs_flags & (REAPER_STATUS_OWNED | REAPER_STATUS_REALINIT));
	ATF_CHECK_EQ(1, st.rs_children);
	ATF_CHECK_EQ(1, st.rs_descendants);
	ATF_CHECK(st.rs_reaper == parent);
	ATF_CHECK_EQ(child, st.rs_pid);
	sr = read(pip[0], &st, sizeof(st));
	ATF_REQUIRE_EQ((ssize_t)sizeof(st), sr);
	ATF_CHECK_EQ(0,
	    st.rs_flags & (REAPER_STATUS_OWNED | REAPER_STATUS_REALINIT));
	ATF_CHECK_EQ(1, st.rs_children);
	ATF_CHECK_EQ(1, st.rs_descendants);
	ATF_CHECK(st.rs_reaper == parent);
	ATF_CHECK_EQ(child, st.rs_pid);

	r = close(pip[0]);
	ATF_REQUIRE_EQ(0, r);
	pid = waitpid(child, &status, 0);
	ATF_REQUIRE_EQ(child, pid);
	ATF_CHECK_EQ(0, status);

	r = procctl(P_PID, parent, PROC_REAP_STATUS, &st);
	ATF_REQUIRE_EQ(0, r);
	ATF_CHECK_EQ(REAPER_STATUS_OWNED,
	    st.rs_flags & (REAPER_STATUS_OWNED | REAPER_STATUS_REALINIT));
	ATF_CHECK_EQ(0, st.rs_children);
	ATF_CHECK_EQ(0, st.rs_descendants);
	ATF_CHECK(st.rs_reaper == parent);
	ATF_CHECK_EQ(-1, st.rs_pid);
}

ATF_TC_WITHOUT_HEAD(reaper_getpids);
ATF_TC_BODY(reaper_getpids, tc)
{
	struct procctl_reaper_pidinfo info[10];
	ssize_t sr;
	pid_t parent, child, grandchild, pid;
	int r, status, childidx;
	int pipa[2], pipb[2];

	parent = getpid();
	r = procctl(P_PID, parent, PROC_REAP_ACQUIRE, NULL);
	ATF_REQUIRE_EQ(0, r);

	memset(info, '\0', sizeof(info));
	r = procctl(P_PID, parent, PROC_REAP_GETPIDS,
	    &(struct procctl_reaper_pids){
	    .rp_count = sizeof(info) / sizeof(info[0]),
	    .rp_pids = info
	    });
	ATF_CHECK_EQ(0, r);
	ATF_CHECK_EQ(0, info[0].pi_flags & REAPER_PIDINFO_VALID);

	r = pipe(pipa);
	ATF_REQUIRE_EQ(0, r);
	r = pipe(pipb);
	ATF_REQUIRE_EQ(0, r);
	child = fork();
	ATF_REQUIRE(child != -1);
	if (child == 0) {
		if (close(pipa[1]) != 0)
			_exit(100);
		if (close(pipb[0]) != 0)
			_exit(100);
		if (read(pipa[0], &(uint8_t){ 0 }, 1) != 1)
			_exit(101);
		grandchild = fork();
		if (grandchild == -1)
			_exit(102);
		if (grandchild == 0) {
			if (write(pipb[1], &(uint8_t){ 0 }, 1) != 1)
				_exit(103);
			if (read(pipa[0], &(uint8_t){ 0 }, 1) != 1)
				_exit(104);
			_exit(0);
		}
		for (;;)
			pause();
	}
	r = close(pipa[0]);
	ATF_REQUIRE_EQ(0, r);
	r = close(pipb[1]);
	ATF_REQUIRE_EQ(0, r);

	memset(info, '\0', sizeof(info));
	r = procctl(P_PID, parent, PROC_REAP_GETPIDS,
	    &(struct procctl_reaper_pids){
	    .rp_count = sizeof(info) / sizeof(info[0]),
	    .rp_pids = info
	    });
	ATF_CHECK_EQ(0, r);
	ATF_CHECK_EQ(REAPER_PIDINFO_VALID | REAPER_PIDINFO_CHILD,
	    info[0].pi_flags & (REAPER_PIDINFO_VALID | REAPER_PIDINFO_CHILD));
	ATF_CHECK_EQ(child, info[0].pi_pid);
	ATF_CHECK_EQ(child, info[0].pi_subtree);
	ATF_CHECK_EQ(0, info[1].pi_flags & REAPER_PIDINFO_VALID);

	sr = write(pipa[1], &(uint8_t){ 0 }, 1);
	ATF_REQUIRE_EQ(1, sr);
	sr = read(pipb[0], &(uint8_t){ 0 }, 1);
	ATF_REQUIRE_EQ(1, sr);

	memset(info, '\0', sizeof(info));
	r = procctl(P_PID, parent, PROC_REAP_GETPIDS,
	    &(struct procctl_reaper_pids){
	    .rp_count = sizeof(info) / sizeof(info[0]),
	    .rp_pids = info
	    });
	ATF_CHECK_EQ(0, r);
	ATF_CHECK_EQ(REAPER_PIDINFO_VALID,
	    info[0].pi_flags & REAPER_PIDINFO_VALID);
	ATF_CHECK_EQ(REAPER_PIDINFO_VALID,
	    info[1].pi_flags & REAPER_PIDINFO_VALID);
	ATF_CHECK_EQ(0, info[2].pi_flags & REAPER_PIDINFO_VALID);
	ATF_CHECK_EQ(child, info[0].pi_subtree);
	ATF_CHECK_EQ(child, info[1].pi_subtree);
	childidx = info[1].pi_pid == child ? 1 : 0;
	ATF_CHECK_EQ(REAPER_PIDINFO_CHILD,
	    info[childidx].pi_flags & REAPER_PIDINFO_CHILD);
	ATF_CHECK_EQ(0, info[childidx ^ 1].pi_flags & REAPER_PIDINFO_CHILD);
	ATF_CHECK(info[childidx].pi_pid == child);
	grandchild = info[childidx ^ 1].pi_pid;
	ATF_CHECK(grandchild > 0);
	ATF_CHECK(grandchild != child);
	ATF_CHECK(grandchild != parent);

	r = kill(child, SIGTERM);
	ATF_REQUIRE_EQ(0, r);

	pid = waitpid(child, &status, 0);
	ATF_REQUIRE_EQ(child, pid);
	ATF_CHECK(WIFSIGNALED(status) && WTERMSIG(status) == SIGTERM);

	memset(info, '\0', sizeof(info));
	r = procctl(P_PID, parent, PROC_REAP_GETPIDS,
	    &(struct procctl_reaper_pids){
	    .rp_count = sizeof(info) / sizeof(info[0]),
	    .rp_pids = info
	    });
	ATF_CHECK_EQ(0, r);
	ATF_CHECK_EQ(REAPER_PIDINFO_VALID,
	    info[0].pi_flags & REAPER_PIDINFO_VALID);
	ATF_CHECK_EQ(0, info[1].pi_flags & REAPER_PIDINFO_VALID);
	ATF_CHECK_EQ(child, info[0].pi_subtree);
	ATF_CHECK_EQ(REAPER_PIDINFO_CHILD,
	    info[0].pi_flags & REAPER_PIDINFO_CHILD);
	ATF_CHECK_EQ(grandchild, info[0].pi_pid);

	sr = write(pipa[1], &(uint8_t){ 0 }, 1);
	ATF_REQUIRE_EQ(1, sr);

	memset(info, '\0', sizeof(info));
	r = procctl(P_PID, parent, PROC_REAP_GETPIDS,
	    &(struct procctl_reaper_pids){
	    .rp_count = sizeof(info) / sizeof(info[0]),
	    .rp_pids = info
	    });
	ATF_CHECK_EQ(0, r);
	ATF_CHECK_EQ(REAPER_PIDINFO_VALID,
	    info[0].pi_flags & REAPER_PIDINFO_VALID);
	ATF_CHECK_EQ(0, info[1].pi_flags & REAPER_PIDINFO_VALID);
	ATF_CHECK_EQ(child, info[0].pi_subtree);
	ATF_CHECK_EQ(REAPER_PIDINFO_CHILD,
	    info[0].pi_flags & REAPER_PIDINFO_CHILD);
	ATF_CHECK_EQ(grandchild, info[0].pi_pid);

	pid = waitpid(grandchild, &status, 0);
	ATF_REQUIRE_EQ(grandchild, pid);
	ATF_CHECK_EQ(0, status);

	memset(info, '\0', sizeof(info));
	r = procctl(P_PID, parent, PROC_REAP_GETPIDS,
	    &(struct procctl_reaper_pids){
	    .rp_count = sizeof(info) / sizeof(info[0]),
	    .rp_pids = info
	    });
	ATF_CHECK_EQ(0, r);
	ATF_CHECK_EQ(0, info[0].pi_flags & REAPER_PIDINFO_VALID);

	r = close(pipa[1]);
	ATF_REQUIRE_EQ(0, r);
	r = close(pipb[0]);
	ATF_REQUIRE_EQ(0, r);
}

ATF_TC_WITHOUT_HEAD(reaper_kill_badsig);
ATF_TC_BODY(reaper_kill_badsig, tc)
{
	struct procctl_reaper_kill params;
	pid_t parent;
	int r;

	parent = getpid();
	r = procctl(P_PID, parent, PROC_REAP_ACQUIRE, NULL);
	ATF_REQUIRE_EQ(0, r);

	params.rk_sig = -1;
	params.rk_flags = 0;
	r = procctl(P_PID, parent, PROC_REAP_KILL, &params);
	ATF_CHECK(r == -1 && errno == EINVAL);
}

ATF_TC_WITHOUT_HEAD(reaper_kill_sigzero);
ATF_TC_BODY(reaper_kill_sigzero, tc)
{
	struct procctl_reaper_kill params;
	pid_t parent;
	int r;

	parent = getpid();
	r = procctl(P_PID, parent, PROC_REAP_ACQUIRE, NULL);
	ATF_REQUIRE_EQ(0, r);

	params.rk_sig = 0;
	params.rk_flags = 0;
	r = procctl(P_PID, parent, PROC_REAP_KILL, &params);
	ATF_CHECK(r == -1 && errno == EINVAL);
}

ATF_TC_WITHOUT_HEAD(reaper_kill_empty);
ATF_TC_BODY(reaper_kill_empty, tc)
{
	struct procctl_reaper_kill params;
	pid_t parent;
	int r;

	parent = getpid();
	r = procctl(P_PID, parent, PROC_REAP_ACQUIRE, NULL);
	ATF_REQUIRE_EQ(0, r);

	params.rk_sig = SIGTERM;
	params.rk_flags = 0;
	params.rk_killed = 77;
	r = procctl(P_PID, parent, PROC_REAP_KILL, &params);
	ATF_CHECK(r == -1 && errno == ESRCH);
	ATF_CHECK_EQ(0, params.rk_killed);
}

ATF_TC_WITHOUT_HEAD(reaper_kill_normal);
ATF_TC_BODY(reaper_kill_normal, tc)
{
	struct procctl_reaper_kill params;
	ssize_t sr;
	pid_t parent, child, grandchild, pid;
	int r, status;
	int pip[2];

	parent = getpid();
	r = procctl(P_PID, parent, PROC_REAP_ACQUIRE, NULL);
	ATF_REQUIRE_EQ(0, r);

	r = pipe(pip);
	ATF_REQUIRE_EQ(0, r);
	child = fork();
	ATF_REQUIRE(child != -1);
	if (child == 0) {
		if (close(pip[0]) != 0)
			_exit(100);
		grandchild = fork();
		if (grandchild == -1)
			_exit(101);
		if (grandchild == 0) {
			if (write(pip[1], &(uint8_t){ 0 }, 1) != 1)
				_exit(102);
			for (;;)
				pause();
		}
		for (;;)
			pause();
	}
	r = close(pip[1]);
	ATF_REQUIRE_EQ(0, r);

	sr = read(pip[0], &(uint8_t){ 0 }, 1);
	ATF_REQUIRE_EQ(1, sr);

	params.rk_sig = SIGTERM;
	params.rk_flags = 0;
	params.rk_killed = 77;
	r = procctl(P_PID, parent, PROC_REAP_KILL, &params);
	ATF_CHECK_EQ(0, r);
	ATF_CHECK_EQ(2, params.rk_killed);

	pid = waitpid(child, &status, 0);
	ATF_REQUIRE_EQ(child, pid);
	ATF_CHECK(WIFSIGNALED(status) && WTERMSIG(status) == SIGTERM);

	pid = waitpid(-1, &status, 0);
	ATF_REQUIRE(pid > 0);
	ATF_CHECK(pid != parent);
	ATF_CHECK(pid != child);
	ATF_CHECK(WIFSIGNALED(status) && WTERMSIG(status) == SIGTERM);

	r = close(pip[0]);
	ATF_REQUIRE_EQ(0, r);
}

ATF_TC_WITHOUT_HEAD(reaper_kill_subtree);
ATF_TC_BODY(reaper_kill_subtree, tc)
{
	struct procctl_reaper_kill params;
	ssize_t sr;
	pid_t parent, child1, child2, grandchild1, grandchild2, pid;
	int r, status;
	int pip[2];

	parent = getpid();
	r = procctl(P_PID, parent, PROC_REAP_ACQUIRE, NULL);
	ATF_REQUIRE_EQ(0, r);

	r = pipe(pip);
	ATF_REQUIRE_EQ(0, r);
	child1 = fork();
	ATF_REQUIRE(child1 != -1);
	if (child1 == 0) {
		if (close(pip[0]) != 0)
			_exit(100);
		grandchild1 = fork();
		if (grandchild1 == -1)
			_exit(101);
		if (grandchild1 == 0) {
			if (write(pip[1], &(uint8_t){ 0 }, 1) != 1)
				_exit(102);
			for (;;)
				pause();
		}
		for (;;)
			pause();
	}
	child2 = fork();
	ATF_REQUIRE(child2 != -1);
	if (child2 == 0) {
		if (close(pip[0]) != 0)
			_exit(100);
		grandchild2 = fork();
		if (grandchild2 == -1)
			_exit(101);
		if (grandchild2 == 0) {
			if (write(pip[1], &(uint8_t){ 0 }, 1) != 1)
				_exit(102);
			for (;;)
				pause();
		}
		for (;;)
			pause();
	}
	r = close(pip[1]);
	ATF_REQUIRE_EQ(0, r);

	sr = read(pip[0], &(uint8_t){ 0 }, 1);
	ATF_REQUIRE_EQ(1, sr);
	sr = read(pip[0], &(uint8_t){ 0 }, 1);
	ATF_REQUIRE_EQ(1, sr);

	params.rk_sig = SIGUSR1;
	params.rk_flags = REAPER_KILL_SUBTREE;
	params.rk_subtree = child1;
	params.rk_killed = 77;
	r = procctl(P_PID, parent, PROC_REAP_KILL, &params);
	ATF_REQUIRE_EQ(0, r);
	ATF_REQUIRE_EQ(2, params.rk_killed);
	ATF_CHECK_EQ(-1, params.rk_fpid);

	pid = waitpid(child1, &status, 0);
	ATF_REQUIRE_EQ(child1, pid);
	ATF_CHECK(WIFSIGNALED(status) && WTERMSIG(status) == SIGUSR1);

	pid = waitpid(-1, &status, 0);
	ATF_REQUIRE(pid > 0);
	ATF_CHECK(pid != parent);
	ATF_CHECK(pid != child1);
	ATF_CHECK(pid != child2);
	ATF_CHECK(WIFSIGNALED(status) && WTERMSIG(status) == SIGUSR1);

	params.rk_sig = SIGUSR2;
	params.rk_flags = REAPER_KILL_SUBTREE;
	params.rk_subtree = child2;
	params.rk_killed = 77;
	r = procctl(P_PID, parent, PROC_REAP_KILL, &params);
	ATF_REQUIRE_EQ(0, r);
	ATF_REQUIRE_EQ(2, params.rk_killed);
	ATF_CHECK_EQ(-1, params.rk_fpid);

	pid = waitpid(child2, &status, 0);
	ATF_REQUIRE_EQ(child2, pid);
	ATF_CHECK(WIFSIGNALED(status) && WTERMSIG(status) == SIGUSR2);

	pid = waitpid(-1, &status, 0);
	ATF_REQUIRE(pid > 0);
	ATF_CHECK(pid != parent);
	ATF_CHECK(pid != child1);
	ATF_CHECK(pid != child2);
	ATF_CHECK(WIFSIGNALED(status) && WTERMSIG(status) == SIGUSR2);

	r = close(pip[0]);
	ATF_REQUIRE_EQ(0, r);
}

ATF_TC_WITHOUT_HEAD(reaper_pdfork);
ATF_TC_BODY(reaper_pdfork, tc)
{
	struct procctl_reaper_status st;
	pid_t child, grandchild, parent, pid;
	int pd, r, status;

	parent = getpid();
	r = procctl(P_PID, parent, PROC_REAP_ACQUIRE, NULL);
	ATF_REQUIRE_EQ(r, 0);

	child = pdfork(&pd, 0);
	ATF_REQUIRE(child != -1);
	if (child == 0) {
		grandchild = pdfork(&pd, 0);
		if (grandchild == -1)
			_exit(1);
		if (grandchild == 0)
			pause();
		_exit(0);
	}
	pid = waitpid(child, &status, 0);
	ATF_REQUIRE_EQ(pid, child);
	r = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
	ATF_REQUIRE_EQ(r, 0);

	r = procctl(P_PID, parent, PROC_REAP_STATUS, &st);
	ATF_REQUIRE_EQ(r, 0);
	ATF_CHECK((st.rs_flags & REAPER_STATUS_OWNED) != 0);
	ATF_CHECK(st.rs_reaper == parent);
	ATF_CHECK(st.rs_children == 1);
	ATF_CHECK(st.rs_descendants == 1);
}

ATF_TP_ADD_TCS(tp)
{

	ATF_TP_ADD_TC(tp, reaper_wait_child_first);
	ATF_TP_ADD_TC(tp, reaper_wait_grandchild_first);
	ATF_TP_ADD_TC(tp, reaper_sigchld_child_first);
	ATF_TP_ADD_TC(tp, reaper_sigchld_grandchild_first);
	ATF_TP_ADD_TC(tp, reaper_status);
	ATF_TP_ADD_TC(tp, reaper_getpids);
	ATF_TP_ADD_TC(tp, reaper_kill_badsig);
	ATF_TP_ADD_TC(tp, reaper_kill_sigzero);
	ATF_TP_ADD_TC(tp, reaper_kill_empty);
	ATF_TP_ADD_TC(tp, reaper_kill_normal);
	ATF_TP_ADD_TC(tp, reaper_kill_subtree);
	ATF_TP_ADD_TC(tp, reaper_pdfork);
	return (atf_no_error());
}
