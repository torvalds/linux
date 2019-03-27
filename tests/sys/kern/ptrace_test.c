/*-
 * Copyright (c) 2015 John Baldwin <jhb@FreeBSD.org>
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

#include <sys/types.h>
#include <sys/cpuset.h>
#include <sys/event.h>
#include <sys/file.h>
#include <sys/time.h>
#include <sys/procctl.h>
#include <sys/ptrace.h>
#include <sys/queue.h>
#include <sys/runq.h>
#include <sys/syscall.h>
#include <sys/sysctl.h>
#include <sys/user.h>
#include <sys/wait.h>
#include <errno.h>
#include <machine/cpufunc.h>
#include <pthread.h>
#include <sched.h>
#include <semaphore.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <atf-c.h>

/*
 * Architectures with a user-visible breakpoint().
 */
#if defined(__aarch64__) || defined(__amd64__) || defined(__arm__) ||	\
    defined(__i386__) || defined(__mips__) || defined(__riscv) ||	\
    defined(__sparc64__)
#define	HAVE_BREAKPOINT
#endif

/*
 * Adjust PC to skip over a breakpoint when stopped for a breakpoint trap.
 */
#ifdef HAVE_BREAKPOINT
#if defined(__aarch64__)
#define	SKIP_BREAK(reg)	((reg)->elr += 4)
#elif defined(__amd64__) || defined(__i386__)
#define	SKIP_BREAK(reg)
#elif defined(__arm__)
#define	SKIP_BREAK(reg)	((reg)->r_pc += 4)
#elif defined(__mips__)
#define	SKIP_BREAK(reg)	((reg)->r_regs[PC] += 4)
#elif defined(__riscv)
#define	SKIP_BREAK(reg)	((reg)->sepc += 4)
#elif defined(__sparc64__)
#define	SKIP_BREAK(reg)	do {						\
	(reg)->r_tpc = (reg)->r_tnpc + 4;				\
	(reg)->r_tnpc += 8;						\
} while (0)
#endif
#endif

/*
 * A variant of ATF_REQUIRE that is suitable for use in child
 * processes.  This only works if the parent process is tripped up by
 * the early exit and fails some requirement itself.
 */
#define	CHILD_REQUIRE(exp) do {						\
		if (!(exp))						\
			child_fail_require(__FILE__, __LINE__,		\
			    #exp " not met");				\
	} while (0)

static __dead2 void
child_fail_require(const char *file, int line, const char *str)
{
	char buf[128];

	snprintf(buf, sizeof(buf), "%s:%d: %s\n", file, line, str);
	write(2, buf, strlen(buf));
	_exit(32);
}

static void
trace_me(void)
{

	/* Attach the parent process as a tracer of this process. */
	CHILD_REQUIRE(ptrace(PT_TRACE_ME, 0, NULL, 0) != -1);

	/* Trigger a stop. */
	raise(SIGSTOP);
}

static void
attach_child(pid_t pid)
{
	pid_t wpid;
	int status;

	ATF_REQUIRE(ptrace(PT_ATTACH, pid, NULL, 0) == 0);

	wpid = waitpid(pid, &status, 0);
	ATF_REQUIRE(wpid == pid);
	ATF_REQUIRE(WIFSTOPPED(status));
	ATF_REQUIRE(WSTOPSIG(status) == SIGSTOP);
}

static void
wait_for_zombie(pid_t pid)
{

	/*
	 * Wait for a process to exit.  This is kind of gross, but
	 * there is not a better way.
	 *
	 * Prior to r325719, the kern.proc.pid.<pid> sysctl failed
	 * with ESRCH.  After that change, a valid struct kinfo_proc
	 * is returned for zombies with ki_stat set to SZOMB.
	 */
	for (;;) {
		struct kinfo_proc kp;
		size_t len;
		int mib[4];

		mib[0] = CTL_KERN;
		mib[1] = KERN_PROC;
		mib[2] = KERN_PROC_PID;
		mib[3] = pid;
		len = sizeof(kp);
		if (sysctl(mib, nitems(mib), &kp, &len, NULL, 0) == -1) {
			ATF_REQUIRE(errno == ESRCH);
			break;
		}
		if (kp.ki_stat == SZOMB)
			break;
		usleep(5000);
	}
}

/*
 * Verify that a parent debugger process "sees" the exit of a debugged
 * process exactly once when attached via PT_TRACE_ME.
 */
ATF_TC_WITHOUT_HEAD(ptrace__parent_wait_after_trace_me);
ATF_TC_BODY(ptrace__parent_wait_after_trace_me, tc)
{
	pid_t child, wpid;
	int status;

	ATF_REQUIRE((child = fork()) != -1);
	if (child == 0) {
		/* Child process. */
		trace_me();

		_exit(1);
	}

	/* Parent process. */

	/* The first wait() should report the stop from SIGSTOP. */
	wpid = waitpid(child, &status, 0);
	ATF_REQUIRE(wpid == child);
	ATF_REQUIRE(WIFSTOPPED(status));
	ATF_REQUIRE(WSTOPSIG(status) == SIGSTOP);

	/* Continue the child ignoring the SIGSTOP. */
	ATF_REQUIRE(ptrace(PT_CONTINUE, child, (caddr_t)1, 0) != -1);

	/* The second wait() should report the exit status. */
	wpid = waitpid(child, &status, 0);
	ATF_REQUIRE(wpid == child);
	ATF_REQUIRE(WIFEXITED(status));
	ATF_REQUIRE(WEXITSTATUS(status) == 1);

	/* The child should no longer exist. */
	wpid = waitpid(child, &status, 0);
	ATF_REQUIRE(wpid == -1);
	ATF_REQUIRE(errno == ECHILD);
}

/*
 * Verify that a parent debugger process "sees" the exit of a debugged
 * process exactly once when attached via PT_ATTACH.
 */
ATF_TC_WITHOUT_HEAD(ptrace__parent_wait_after_attach);
ATF_TC_BODY(ptrace__parent_wait_after_attach, tc)
{
	pid_t child, wpid;
	int cpipe[2], status;
	char c;

	ATF_REQUIRE(pipe(cpipe) == 0);
	ATF_REQUIRE((child = fork()) != -1);
	if (child == 0) {
		/* Child process. */
		close(cpipe[0]);

		/* Wait for the parent to attach. */
		CHILD_REQUIRE(read(cpipe[1], &c, sizeof(c)) == 0);

		_exit(1);
	}
	close(cpipe[1]);

	/* Parent process. */

	/* Attach to the child process. */
	attach_child(child);

	/* Continue the child ignoring the SIGSTOP. */
	ATF_REQUIRE(ptrace(PT_CONTINUE, child, (caddr_t)1, 0) != -1);

	/* Signal the child to exit. */
	close(cpipe[0]);

	/* The second wait() should report the exit status. */
	wpid = waitpid(child, &status, 0);
	ATF_REQUIRE(wpid == child);
	ATF_REQUIRE(WIFEXITED(status));
	ATF_REQUIRE(WEXITSTATUS(status) == 1);

	/* The child should no longer exist. */
	wpid = waitpid(child, &status, 0);
	ATF_REQUIRE(wpid == -1);
	ATF_REQUIRE(errno == ECHILD);
}

/*
 * Verify that a parent process "sees" the exit of a debugged process only
 * after the debugger has seen it.
 */
ATF_TC_WITHOUT_HEAD(ptrace__parent_sees_exit_after_child_debugger);
ATF_TC_BODY(ptrace__parent_sees_exit_after_child_debugger, tc)
{
	pid_t child, debugger, wpid;
	int cpipe[2], dpipe[2], status;
	char c;

	ATF_REQUIRE(pipe(cpipe) == 0);
	ATF_REQUIRE((child = fork()) != -1);

	if (child == 0) {
		/* Child process. */
		close(cpipe[0]);

		/* Wait for parent to be ready. */
		CHILD_REQUIRE(read(cpipe[1], &c, sizeof(c)) == sizeof(c));

		_exit(1);
	}
	close(cpipe[1]);

	ATF_REQUIRE(pipe(dpipe) == 0);
	ATF_REQUIRE((debugger = fork()) != -1);

	if (debugger == 0) {
		/* Debugger process. */
		close(dpipe[0]);

		CHILD_REQUIRE(ptrace(PT_ATTACH, child, NULL, 0) != -1);

		wpid = waitpid(child, &status, 0);
		CHILD_REQUIRE(wpid == child);
		CHILD_REQUIRE(WIFSTOPPED(status));
		CHILD_REQUIRE(WSTOPSIG(status) == SIGSTOP);

		CHILD_REQUIRE(ptrace(PT_CONTINUE, child, (caddr_t)1, 0) != -1);

		/* Signal parent that debugger is attached. */
		CHILD_REQUIRE(write(dpipe[1], &c, sizeof(c)) == sizeof(c));

		/* Wait for parent's failed wait. */
		CHILD_REQUIRE(read(dpipe[1], &c, sizeof(c)) == 0);

		wpid = waitpid(child, &status, 0);
		CHILD_REQUIRE(wpid == child);
		CHILD_REQUIRE(WIFEXITED(status));
		CHILD_REQUIRE(WEXITSTATUS(status) == 1);

		_exit(0);
	}
	close(dpipe[1]);

	/* Parent process. */

	/* Wait for the debugger to attach to the child. */
	ATF_REQUIRE(read(dpipe[0], &c, sizeof(c)) == sizeof(c));

	/* Release the child. */
	ATF_REQUIRE(write(cpipe[0], &c, sizeof(c)) == sizeof(c));
	ATF_REQUIRE(read(cpipe[0], &c, sizeof(c)) == 0);
	close(cpipe[0]);

	wait_for_zombie(child);

	/*
	 * This wait should return a pid of 0 to indicate no status to
	 * report.  The parent should see the child as non-exited
	 * until the debugger sees the exit.
	 */
	wpid = waitpid(child, &status, WNOHANG);
	ATF_REQUIRE(wpid == 0);

	/* Signal the debugger to wait for the child. */
	close(dpipe[0]);

	/* Wait for the debugger. */
	wpid = waitpid(debugger, &status, 0);
	ATF_REQUIRE(wpid == debugger);
	ATF_REQUIRE(WIFEXITED(status));
	ATF_REQUIRE(WEXITSTATUS(status) == 0);

	/* The child process should now be ready. */
	wpid = waitpid(child, &status, WNOHANG);
	ATF_REQUIRE(wpid == child);
	ATF_REQUIRE(WIFEXITED(status));
	ATF_REQUIRE(WEXITSTATUS(status) == 1);
}

/*
 * Verify that a parent process "sees" the exit of a debugged process
 * only after a non-direct-child debugger has seen it.  In particular,
 * various wait() calls in the parent must avoid failing with ESRCH by
 * checking the parent's orphan list for the debugee.
 */
ATF_TC_WITHOUT_HEAD(ptrace__parent_sees_exit_after_unrelated_debugger);
ATF_TC_BODY(ptrace__parent_sees_exit_after_unrelated_debugger, tc)
{
	pid_t child, debugger, fpid, wpid;
	int cpipe[2], dpipe[2], status;
	char c;

	ATF_REQUIRE(pipe(cpipe) == 0);
	ATF_REQUIRE((child = fork()) != -1);

	if (child == 0) {
		/* Child process. */
		close(cpipe[0]);

		/* Wait for parent to be ready. */
		CHILD_REQUIRE(read(cpipe[1], &c, sizeof(c)) == sizeof(c));

		_exit(1);
	}
	close(cpipe[1]);

	ATF_REQUIRE(pipe(dpipe) == 0);
	ATF_REQUIRE((debugger = fork()) != -1);

	if (debugger == 0) {
		/* Debugger parent. */

		/*
		 * Fork again and drop the debugger parent so that the
		 * debugger is not a child of the main parent.
		 */
		CHILD_REQUIRE((fpid = fork()) != -1);
		if (fpid != 0)
			_exit(2);

		/* Debugger process. */
		close(dpipe[0]);

		CHILD_REQUIRE(ptrace(PT_ATTACH, child, NULL, 0) != -1);

		wpid = waitpid(child, &status, 0);
		CHILD_REQUIRE(wpid == child);
		CHILD_REQUIRE(WIFSTOPPED(status));
		CHILD_REQUIRE(WSTOPSIG(status) == SIGSTOP);

		CHILD_REQUIRE(ptrace(PT_CONTINUE, child, (caddr_t)1, 0) != -1);

		/* Signal parent that debugger is attached. */
		CHILD_REQUIRE(write(dpipe[1], &c, sizeof(c)) == sizeof(c));

		/* Wait for parent's failed wait. */
		CHILD_REQUIRE(read(dpipe[1], &c, sizeof(c)) == sizeof(c));

		wpid = waitpid(child, &status, 0);
		CHILD_REQUIRE(wpid == child);
		CHILD_REQUIRE(WIFEXITED(status));
		CHILD_REQUIRE(WEXITSTATUS(status) == 1);

		_exit(0);
	}
	close(dpipe[1]);

	/* Parent process. */

	/* Wait for the debugger parent process to exit. */
	wpid = waitpid(debugger, &status, 0);
	ATF_REQUIRE(wpid == debugger);
	ATF_REQUIRE(WIFEXITED(status));
	ATF_REQUIRE(WEXITSTATUS(status) == 2);

	/* A WNOHANG wait here should see the non-exited child. */
	wpid = waitpid(child, &status, WNOHANG);
	ATF_REQUIRE(wpid == 0);

	/* Wait for the debugger to attach to the child. */
	ATF_REQUIRE(read(dpipe[0], &c, sizeof(c)) == sizeof(c));

	/* Release the child. */
	ATF_REQUIRE(write(cpipe[0], &c, sizeof(c)) == sizeof(c));
	ATF_REQUIRE(read(cpipe[0], &c, sizeof(c)) == 0);
	close(cpipe[0]);

	wait_for_zombie(child);

	/*
	 * This wait should return a pid of 0 to indicate no status to
	 * report.  The parent should see the child as non-exited
	 * until the debugger sees the exit.
	 */
	wpid = waitpid(child, &status, WNOHANG);
	ATF_REQUIRE(wpid == 0);

	/* Signal the debugger to wait for the child. */
	ATF_REQUIRE(write(dpipe[0], &c, sizeof(c)) == sizeof(c));

	/* Wait for the debugger. */
	ATF_REQUIRE(read(dpipe[0], &c, sizeof(c)) == 0);
	close(dpipe[0]);

	/* The child process should now be ready. */
	wpid = waitpid(child, &status, WNOHANG);
	ATF_REQUIRE(wpid == child);
	ATF_REQUIRE(WIFEXITED(status));
	ATF_REQUIRE(WEXITSTATUS(status) == 1);
}

/*
 * The parent process should always act the same regardless of how the
 * debugger is attached to it.
 */
static __dead2 void
follow_fork_parent(bool use_vfork)
{
	pid_t fpid, wpid;
	int status;

	if (use_vfork)
		CHILD_REQUIRE((fpid = vfork()) != -1);
	else
		CHILD_REQUIRE((fpid = fork()) != -1);

	if (fpid == 0)
		/* Child */
		_exit(2);

	wpid = waitpid(fpid, &status, 0);
	CHILD_REQUIRE(wpid == fpid);
	CHILD_REQUIRE(WIFEXITED(status));
	CHILD_REQUIRE(WEXITSTATUS(status) == 2);

	_exit(1);
}

/*
 * Helper routine for follow fork tests.  This waits for two stops
 * that report both "sides" of a fork.  It returns the pid of the new
 * child process.
 */
static pid_t
handle_fork_events(pid_t parent, struct ptrace_lwpinfo *ppl)
{
	struct ptrace_lwpinfo pl;
	bool fork_reported[2];
	pid_t child, wpid;
	int i, status;

	fork_reported[0] = false;
	fork_reported[1] = false;
	child = -1;
	
	/*
	 * Each process should report a fork event.  The parent should
	 * report a PL_FLAG_FORKED event, and the child should report
	 * a PL_FLAG_CHILD event.
	 */
	for (i = 0; i < 2; i++) {
		wpid = wait(&status);
		ATF_REQUIRE(wpid > 0);
		ATF_REQUIRE(WIFSTOPPED(status));

		ATF_REQUIRE(ptrace(PT_LWPINFO, wpid, (caddr_t)&pl,
		    sizeof(pl)) != -1);
		ATF_REQUIRE((pl.pl_flags & (PL_FLAG_FORKED | PL_FLAG_CHILD)) !=
		    0);
		ATF_REQUIRE((pl.pl_flags & (PL_FLAG_FORKED | PL_FLAG_CHILD)) !=
		    (PL_FLAG_FORKED | PL_FLAG_CHILD));
		if (pl.pl_flags & PL_FLAG_CHILD) {
			ATF_REQUIRE(wpid != parent);
			ATF_REQUIRE(WSTOPSIG(status) == SIGSTOP);
			ATF_REQUIRE(!fork_reported[1]);
			if (child == -1)
				child = wpid;
			else
				ATF_REQUIRE(child == wpid);
			if (ppl != NULL)
				ppl[1] = pl;
			fork_reported[1] = true;
		} else {
			ATF_REQUIRE(wpid == parent);
			ATF_REQUIRE(WSTOPSIG(status) == SIGTRAP);
			ATF_REQUIRE(!fork_reported[0]);
			if (child == -1)
				child = pl.pl_child_pid;
			else
				ATF_REQUIRE(child == pl.pl_child_pid);
			if (ppl != NULL)
				ppl[0] = pl;
			fork_reported[0] = true;
		}
	}

	return (child);
}

/*
 * Verify that a new child process is stopped after a followed fork and
 * that the traced parent sees the exit of the child after the debugger
 * when both processes remain attached to the debugger.
 */
ATF_TC_WITHOUT_HEAD(ptrace__follow_fork_both_attached);
ATF_TC_BODY(ptrace__follow_fork_both_attached, tc)
{
	pid_t children[2], fpid, wpid;
	int status;

	ATF_REQUIRE((fpid = fork()) != -1);
	if (fpid == 0) {
		trace_me();
		follow_fork_parent(false);
	}

	/* Parent process. */
	children[0] = fpid;

	/* The first wait() should report the stop from SIGSTOP. */
	wpid = waitpid(children[0], &status, 0);
	ATF_REQUIRE(wpid == children[0]);
	ATF_REQUIRE(WIFSTOPPED(status));
	ATF_REQUIRE(WSTOPSIG(status) == SIGSTOP);

	ATF_REQUIRE(ptrace(PT_FOLLOW_FORK, children[0], NULL, 1) != -1);

	/* Continue the child ignoring the SIGSTOP. */
	ATF_REQUIRE(ptrace(PT_CONTINUE, children[0], (caddr_t)1, 0) != -1);

	children[1] = handle_fork_events(children[0], NULL);
	ATF_REQUIRE(children[1] > 0);

	ATF_REQUIRE(ptrace(PT_CONTINUE, children[0], (caddr_t)1, 0) != -1);
	ATF_REQUIRE(ptrace(PT_CONTINUE, children[1], (caddr_t)1, 0) != -1);

	/*
	 * The child can't exit until the grandchild reports status, so the
	 * grandchild should report its exit first to the debugger.
	 */
	wpid = wait(&status);
	ATF_REQUIRE(wpid == children[1]);
	ATF_REQUIRE(WIFEXITED(status));
	ATF_REQUIRE(WEXITSTATUS(status) == 2);

	wpid = wait(&status);
	ATF_REQUIRE(wpid == children[0]);
	ATF_REQUIRE(WIFEXITED(status));
	ATF_REQUIRE(WEXITSTATUS(status) == 1);

	wpid = wait(&status);
	ATF_REQUIRE(wpid == -1);
	ATF_REQUIRE(errno == ECHILD);
}

/*
 * Verify that a new child process is stopped after a followed fork
 * and that the traced parent sees the exit of the child when the new
 * child process is detached after it reports its fork.
 */
ATF_TC_WITHOUT_HEAD(ptrace__follow_fork_child_detached);
ATF_TC_BODY(ptrace__follow_fork_child_detached, tc)
{
	pid_t children[2], fpid, wpid;
	int status;

	ATF_REQUIRE((fpid = fork()) != -1);
	if (fpid == 0) {
		trace_me();
		follow_fork_parent(false);
	}

	/* Parent process. */
	children[0] = fpid;

	/* The first wait() should report the stop from SIGSTOP. */
	wpid = waitpid(children[0], &status, 0);
	ATF_REQUIRE(wpid == children[0]);
	ATF_REQUIRE(WIFSTOPPED(status));
	ATF_REQUIRE(WSTOPSIG(status) == SIGSTOP);

	ATF_REQUIRE(ptrace(PT_FOLLOW_FORK, children[0], NULL, 1) != -1);

	/* Continue the child ignoring the SIGSTOP. */
	ATF_REQUIRE(ptrace(PT_CONTINUE, children[0], (caddr_t)1, 0) != -1);

	children[1] = handle_fork_events(children[0], NULL);
	ATF_REQUIRE(children[1] > 0);

	ATF_REQUIRE(ptrace(PT_CONTINUE, children[0], (caddr_t)1, 0) != -1);
	ATF_REQUIRE(ptrace(PT_DETACH, children[1], (caddr_t)1, 0) != -1);

	/*
	 * Should not see any status from the grandchild now, only the
	 * child.
	 */
	wpid = wait(&status);
	ATF_REQUIRE(wpid == children[0]);
	ATF_REQUIRE(WIFEXITED(status));
	ATF_REQUIRE(WEXITSTATUS(status) == 1);

	wpid = wait(&status);
	ATF_REQUIRE(wpid == -1);
	ATF_REQUIRE(errno == ECHILD);
}

/*
 * Verify that a new child process is stopped after a followed fork
 * and that the traced parent sees the exit of the child when the
 * traced parent is detached after the fork.
 */
ATF_TC_WITHOUT_HEAD(ptrace__follow_fork_parent_detached);
ATF_TC_BODY(ptrace__follow_fork_parent_detached, tc)
{
	pid_t children[2], fpid, wpid;
	int status;

	ATF_REQUIRE((fpid = fork()) != -1);
	if (fpid == 0) {
		trace_me();
		follow_fork_parent(false);
	}

	/* Parent process. */
	children[0] = fpid;

	/* The first wait() should report the stop from SIGSTOP. */
	wpid = waitpid(children[0], &status, 0);
	ATF_REQUIRE(wpid == children[0]);
	ATF_REQUIRE(WIFSTOPPED(status));
	ATF_REQUIRE(WSTOPSIG(status) == SIGSTOP);

	ATF_REQUIRE(ptrace(PT_FOLLOW_FORK, children[0], NULL, 1) != -1);

	/* Continue the child ignoring the SIGSTOP. */
	ATF_REQUIRE(ptrace(PT_CONTINUE, children[0], (caddr_t)1, 0) != -1);

	children[1] = handle_fork_events(children[0], NULL);
	ATF_REQUIRE(children[1] > 0);

	ATF_REQUIRE(ptrace(PT_DETACH, children[0], (caddr_t)1, 0) != -1);
	ATF_REQUIRE(ptrace(PT_CONTINUE, children[1], (caddr_t)1, 0) != -1);

	/*
	 * The child can't exit until the grandchild reports status, so the
	 * grandchild should report its exit first to the debugger.
	 *
	 * Even though the child process is detached, it is still a
	 * child of the debugger, so it will still report it's exit
	 * after the grandchild.
	 */
	wpid = wait(&status);
	ATF_REQUIRE(wpid == children[1]);
	ATF_REQUIRE(WIFEXITED(status));
	ATF_REQUIRE(WEXITSTATUS(status) == 2);

	wpid = wait(&status);
	ATF_REQUIRE(wpid == children[0]);
	ATF_REQUIRE(WIFEXITED(status));
	ATF_REQUIRE(WEXITSTATUS(status) == 1);

	wpid = wait(&status);
	ATF_REQUIRE(wpid == -1);
	ATF_REQUIRE(errno == ECHILD);
}

static void
attach_fork_parent(int cpipe[2])
{
	pid_t fpid;

	close(cpipe[0]);

	/* Double-fork to disassociate from the debugger. */
	CHILD_REQUIRE((fpid = fork()) != -1);
	if (fpid != 0)
		_exit(3);
	
	/* Send the pid of the disassociated child to the debugger. */
	fpid = getpid();
	CHILD_REQUIRE(write(cpipe[1], &fpid, sizeof(fpid)) == sizeof(fpid));

	/* Wait for the debugger to attach. */
	CHILD_REQUIRE(read(cpipe[1], &fpid, sizeof(fpid)) == 0);
}

/*
 * Verify that a new child process is stopped after a followed fork and
 * that the traced parent sees the exit of the child after the debugger
 * when both processes remain attached to the debugger.  In this test
 * the parent that forks is not a direct child of the debugger.
 */
ATF_TC_WITHOUT_HEAD(ptrace__follow_fork_both_attached_unrelated_debugger);
ATF_TC_BODY(ptrace__follow_fork_both_attached_unrelated_debugger, tc)
{
	pid_t children[2], fpid, wpid;
	int cpipe[2], status;

	ATF_REQUIRE(pipe(cpipe) == 0);
	ATF_REQUIRE((fpid = fork()) != -1);
	if (fpid == 0) {
		attach_fork_parent(cpipe);
		follow_fork_parent(false);
	}

	/* Parent process. */
	close(cpipe[1]);

	/* Wait for the direct child to exit. */
	wpid = waitpid(fpid, &status, 0);
	ATF_REQUIRE(wpid == fpid);
	ATF_REQUIRE(WIFEXITED(status));
	ATF_REQUIRE(WEXITSTATUS(status) == 3);

	/* Read the pid of the fork parent. */
	ATF_REQUIRE(read(cpipe[0], &children[0], sizeof(children[0])) ==
	    sizeof(children[0]));

	/* Attach to the fork parent. */
	attach_child(children[0]);

	ATF_REQUIRE(ptrace(PT_FOLLOW_FORK, children[0], NULL, 1) != -1);

	/* Continue the fork parent ignoring the SIGSTOP. */
	ATF_REQUIRE(ptrace(PT_CONTINUE, children[0], (caddr_t)1, 0) != -1);

	/* Signal the fork parent to continue. */
	close(cpipe[0]);

	children[1] = handle_fork_events(children[0], NULL);
	ATF_REQUIRE(children[1] > 0);

	ATF_REQUIRE(ptrace(PT_CONTINUE, children[0], (caddr_t)1, 0) != -1);
	ATF_REQUIRE(ptrace(PT_CONTINUE, children[1], (caddr_t)1, 0) != -1);

	/*
	 * The fork parent can't exit until the child reports status,
	 * so the child should report its exit first to the debugger.
	 */
	wpid = wait(&status);
	ATF_REQUIRE(wpid == children[1]);
	ATF_REQUIRE(WIFEXITED(status));
	ATF_REQUIRE(WEXITSTATUS(status) == 2);

	wpid = wait(&status);
	ATF_REQUIRE(wpid == children[0]);
	ATF_REQUIRE(WIFEXITED(status));
	ATF_REQUIRE(WEXITSTATUS(status) == 1);

	wpid = wait(&status);
	ATF_REQUIRE(wpid == -1);
	ATF_REQUIRE(errno == ECHILD);
}

/*
 * Verify that a new child process is stopped after a followed fork
 * and that the traced parent sees the exit of the child when the new
 * child process is detached after it reports its fork.  In this test
 * the parent that forks is not a direct child of the debugger.
 */
ATF_TC_WITHOUT_HEAD(ptrace__follow_fork_child_detached_unrelated_debugger);
ATF_TC_BODY(ptrace__follow_fork_child_detached_unrelated_debugger, tc)
{
	pid_t children[2], fpid, wpid;
	int cpipe[2], status;

	ATF_REQUIRE(pipe(cpipe) == 0);
	ATF_REQUIRE((fpid = fork()) != -1);
	if (fpid == 0) {
		attach_fork_parent(cpipe);
		follow_fork_parent(false);
	}

	/* Parent process. */
	close(cpipe[1]);

	/* Wait for the direct child to exit. */
	wpid = waitpid(fpid, &status, 0);
	ATF_REQUIRE(wpid == fpid);
	ATF_REQUIRE(WIFEXITED(status));
	ATF_REQUIRE(WEXITSTATUS(status) == 3);

	/* Read the pid of the fork parent. */
	ATF_REQUIRE(read(cpipe[0], &children[0], sizeof(children[0])) ==
	    sizeof(children[0]));

	/* Attach to the fork parent. */
	attach_child(children[0]);

	ATF_REQUIRE(ptrace(PT_FOLLOW_FORK, children[0], NULL, 1) != -1);

	/* Continue the fork parent ignoring the SIGSTOP. */
	ATF_REQUIRE(ptrace(PT_CONTINUE, children[0], (caddr_t)1, 0) != -1);

	/* Signal the fork parent to continue. */
	close(cpipe[0]);

	children[1] = handle_fork_events(children[0], NULL);
	ATF_REQUIRE(children[1] > 0);

	ATF_REQUIRE(ptrace(PT_CONTINUE, children[0], (caddr_t)1, 0) != -1);
	ATF_REQUIRE(ptrace(PT_DETACH, children[1], (caddr_t)1, 0) != -1);

	/*
	 * Should not see any status from the child now, only the fork
	 * parent.
	 */
	wpid = wait(&status);
	ATF_REQUIRE(wpid == children[0]);
	ATF_REQUIRE(WIFEXITED(status));
	ATF_REQUIRE(WEXITSTATUS(status) == 1);

	wpid = wait(&status);
	ATF_REQUIRE(wpid == -1);
	ATF_REQUIRE(errno == ECHILD);
}

/*
 * Verify that a new child process is stopped after a followed fork
 * and that the traced parent sees the exit of the child when the
 * traced parent is detached after the fork.  In this test the parent
 * that forks is not a direct child of the debugger.
 */
ATF_TC_WITHOUT_HEAD(ptrace__follow_fork_parent_detached_unrelated_debugger);
ATF_TC_BODY(ptrace__follow_fork_parent_detached_unrelated_debugger, tc)
{
	pid_t children[2], fpid, wpid;
	int cpipe[2], status;

	ATF_REQUIRE(pipe(cpipe) == 0);
	ATF_REQUIRE((fpid = fork()) != -1);
	if (fpid == 0) {
		attach_fork_parent(cpipe);
		follow_fork_parent(false);
	}

	/* Parent process. */
	close(cpipe[1]);

	/* Wait for the direct child to exit. */
	wpid = waitpid(fpid, &status, 0);
	ATF_REQUIRE(wpid == fpid);
	ATF_REQUIRE(WIFEXITED(status));
	ATF_REQUIRE(WEXITSTATUS(status) == 3);

	/* Read the pid of the fork parent. */
	ATF_REQUIRE(read(cpipe[0], &children[0], sizeof(children[0])) ==
	    sizeof(children[0]));

	/* Attach to the fork parent. */
	attach_child(children[0]);

	ATF_REQUIRE(ptrace(PT_FOLLOW_FORK, children[0], NULL, 1) != -1);

	/* Continue the fork parent ignoring the SIGSTOP. */
	ATF_REQUIRE(ptrace(PT_CONTINUE, children[0], (caddr_t)1, 0) != -1);

	/* Signal the fork parent to continue. */
	close(cpipe[0]);

	children[1] = handle_fork_events(children[0], NULL);
	ATF_REQUIRE(children[1] > 0);

	ATF_REQUIRE(ptrace(PT_DETACH, children[0], (caddr_t)1, 0) != -1);
	ATF_REQUIRE(ptrace(PT_CONTINUE, children[1], (caddr_t)1, 0) != -1);

	/*
	 * Should not see any status from the fork parent now, only
	 * the child.
	 */
	wpid = wait(&status);
	ATF_REQUIRE(wpid == children[1]);
	ATF_REQUIRE(WIFEXITED(status));
	ATF_REQUIRE(WEXITSTATUS(status) == 2);

	wpid = wait(&status);
	ATF_REQUIRE(wpid == -1);
	ATF_REQUIRE(errno == ECHILD);
}

/*
 * Verify that a child process does not see an unrelated debugger as its
 * parent but sees its original parent process.
 */
ATF_TC_WITHOUT_HEAD(ptrace__getppid);
ATF_TC_BODY(ptrace__getppid, tc)
{
	pid_t child, debugger, ppid, wpid;
	int cpipe[2], dpipe[2], status;
	char c;

	ATF_REQUIRE(pipe(cpipe) == 0);
	ATF_REQUIRE((child = fork()) != -1);

	if (child == 0) {
		/* Child process. */
		close(cpipe[0]);

		/* Wait for parent to be ready. */
		CHILD_REQUIRE(read(cpipe[1], &c, sizeof(c)) == sizeof(c));

		/* Report the parent PID to the parent. */
		ppid = getppid();
		CHILD_REQUIRE(write(cpipe[1], &ppid, sizeof(ppid)) ==
		    sizeof(ppid));

		_exit(1);
	}
	close(cpipe[1]);

	ATF_REQUIRE(pipe(dpipe) == 0);
	ATF_REQUIRE((debugger = fork()) != -1);

	if (debugger == 0) {
		/* Debugger process. */
		close(dpipe[0]);

		CHILD_REQUIRE(ptrace(PT_ATTACH, child, NULL, 0) != -1);

		wpid = waitpid(child, &status, 0);
		CHILD_REQUIRE(wpid == child);
		CHILD_REQUIRE(WIFSTOPPED(status));
		CHILD_REQUIRE(WSTOPSIG(status) == SIGSTOP);

		CHILD_REQUIRE(ptrace(PT_CONTINUE, child, (caddr_t)1, 0) != -1);

		/* Signal parent that debugger is attached. */
		CHILD_REQUIRE(write(dpipe[1], &c, sizeof(c)) == sizeof(c));

		/* Wait for traced child to exit. */
		wpid = waitpid(child, &status, 0);
		CHILD_REQUIRE(wpid == child);
		CHILD_REQUIRE(WIFEXITED(status));
		CHILD_REQUIRE(WEXITSTATUS(status) == 1);

		_exit(0);
	}
	close(dpipe[1]);

	/* Parent process. */

	/* Wait for the debugger to attach to the child. */
	ATF_REQUIRE(read(dpipe[0], &c, sizeof(c)) == sizeof(c));

	/* Release the child. */
	ATF_REQUIRE(write(cpipe[0], &c, sizeof(c)) == sizeof(c));

	/* Read the parent PID from the child. */
	ATF_REQUIRE(read(cpipe[0], &ppid, sizeof(ppid)) == sizeof(ppid));
	close(cpipe[0]);

	ATF_REQUIRE(ppid == getpid());

	/* Wait for the debugger. */
	wpid = waitpid(debugger, &status, 0);
	ATF_REQUIRE(wpid == debugger);
	ATF_REQUIRE(WIFEXITED(status));
	ATF_REQUIRE(WEXITSTATUS(status) == 0);

	/* The child process should now be ready. */
	wpid = waitpid(child, &status, WNOHANG);
	ATF_REQUIRE(wpid == child);
	ATF_REQUIRE(WIFEXITED(status));
	ATF_REQUIRE(WEXITSTATUS(status) == 1);
}

/*
 * Verify that pl_syscall_code in struct ptrace_lwpinfo for a new
 * child process created via fork() reports the correct value.
 */
ATF_TC_WITHOUT_HEAD(ptrace__new_child_pl_syscall_code_fork);
ATF_TC_BODY(ptrace__new_child_pl_syscall_code_fork, tc)
{
	struct ptrace_lwpinfo pl[2];
	pid_t children[2], fpid, wpid;
	int status;

	ATF_REQUIRE((fpid = fork()) != -1);
	if (fpid == 0) {
		trace_me();
		follow_fork_parent(false);
	}

	/* Parent process. */
	children[0] = fpid;

	/* The first wait() should report the stop from SIGSTOP. */
	wpid = waitpid(children[0], &status, 0);
	ATF_REQUIRE(wpid == children[0]);
	ATF_REQUIRE(WIFSTOPPED(status));
	ATF_REQUIRE(WSTOPSIG(status) == SIGSTOP);

	ATF_REQUIRE(ptrace(PT_FOLLOW_FORK, children[0], NULL, 1) != -1);

	/* Continue the child ignoring the SIGSTOP. */
	ATF_REQUIRE(ptrace(PT_CONTINUE, children[0], (caddr_t)1, 0) != -1);

	/* Wait for both halves of the fork event to get reported. */
	children[1] = handle_fork_events(children[0], pl);
	ATF_REQUIRE(children[1] > 0);

	ATF_REQUIRE((pl[0].pl_flags & PL_FLAG_SCX) != 0);
	ATF_REQUIRE((pl[1].pl_flags & PL_FLAG_SCX) != 0);
	ATF_REQUIRE(pl[0].pl_syscall_code == SYS_fork);
	ATF_REQUIRE(pl[0].pl_syscall_code == pl[1].pl_syscall_code);
	ATF_REQUIRE(pl[0].pl_syscall_narg == pl[1].pl_syscall_narg);

	ATF_REQUIRE(ptrace(PT_CONTINUE, children[0], (caddr_t)1, 0) != -1);
	ATF_REQUIRE(ptrace(PT_CONTINUE, children[1], (caddr_t)1, 0) != -1);

	/*
	 * The child can't exit until the grandchild reports status, so the
	 * grandchild should report its exit first to the debugger.
	 */
	wpid = wait(&status);
	ATF_REQUIRE(wpid == children[1]);
	ATF_REQUIRE(WIFEXITED(status));
	ATF_REQUIRE(WEXITSTATUS(status) == 2);

	wpid = wait(&status);
	ATF_REQUIRE(wpid == children[0]);
	ATF_REQUIRE(WIFEXITED(status));
	ATF_REQUIRE(WEXITSTATUS(status) == 1);

	wpid = wait(&status);
	ATF_REQUIRE(wpid == -1);
	ATF_REQUIRE(errno == ECHILD);
}

/*
 * Verify that pl_syscall_code in struct ptrace_lwpinfo for a new
 * child process created via vfork() reports the correct value.
 */
ATF_TC_WITHOUT_HEAD(ptrace__new_child_pl_syscall_code_vfork);
ATF_TC_BODY(ptrace__new_child_pl_syscall_code_vfork, tc)
{
	struct ptrace_lwpinfo pl[2];
	pid_t children[2], fpid, wpid;
	int status;

	ATF_REQUIRE((fpid = fork()) != -1);
	if (fpid == 0) {
		trace_me();
		follow_fork_parent(true);
	}

	/* Parent process. */
	children[0] = fpid;

	/* The first wait() should report the stop from SIGSTOP. */
	wpid = waitpid(children[0], &status, 0);
	ATF_REQUIRE(wpid == children[0]);
	ATF_REQUIRE(WIFSTOPPED(status));
	ATF_REQUIRE(WSTOPSIG(status) == SIGSTOP);

	ATF_REQUIRE(ptrace(PT_FOLLOW_FORK, children[0], NULL, 1) != -1);

	/* Continue the child ignoring the SIGSTOP. */
	ATF_REQUIRE(ptrace(PT_CONTINUE, children[0], (caddr_t)1, 0) != -1);

	/* Wait for both halves of the fork event to get reported. */
	children[1] = handle_fork_events(children[0], pl);
	ATF_REQUIRE(children[1] > 0);

	ATF_REQUIRE((pl[0].pl_flags & PL_FLAG_SCX) != 0);
	ATF_REQUIRE((pl[1].pl_flags & PL_FLAG_SCX) != 0);
	ATF_REQUIRE(pl[0].pl_syscall_code == SYS_vfork);
	ATF_REQUIRE(pl[0].pl_syscall_code == pl[1].pl_syscall_code);
	ATF_REQUIRE(pl[0].pl_syscall_narg == pl[1].pl_syscall_narg);

	ATF_REQUIRE(ptrace(PT_CONTINUE, children[0], (caddr_t)1, 0) != -1);
	ATF_REQUIRE(ptrace(PT_CONTINUE, children[1], (caddr_t)1, 0) != -1);

	/*
	 * The child can't exit until the grandchild reports status, so the
	 * grandchild should report its exit first to the debugger.
	 */
	wpid = wait(&status);
	ATF_REQUIRE(wpid == children[1]);
	ATF_REQUIRE(WIFEXITED(status));
	ATF_REQUIRE(WEXITSTATUS(status) == 2);

	wpid = wait(&status);
	ATF_REQUIRE(wpid == children[0]);
	ATF_REQUIRE(WIFEXITED(status));
	ATF_REQUIRE(WEXITSTATUS(status) == 1);

	wpid = wait(&status);
	ATF_REQUIRE(wpid == -1);
	ATF_REQUIRE(errno == ECHILD);
}

static void *
simple_thread(void *arg __unused)
{

	pthread_exit(NULL);
}

static __dead2 void
simple_thread_main(void)
{
	pthread_t thread;

	CHILD_REQUIRE(pthread_create(&thread, NULL, simple_thread, NULL) == 0);
	CHILD_REQUIRE(pthread_join(thread, NULL) == 0);
	exit(1);
}

/*
 * Verify that pl_syscall_code in struct ptrace_lwpinfo for a new
 * thread reports the correct value.
 */
ATF_TC_WITHOUT_HEAD(ptrace__new_child_pl_syscall_code_thread);
ATF_TC_BODY(ptrace__new_child_pl_syscall_code_thread, tc)
{
	struct ptrace_lwpinfo pl;
	pid_t fpid, wpid;
	lwpid_t mainlwp;
	int status;

	ATF_REQUIRE((fpid = fork()) != -1);
	if (fpid == 0) {
		trace_me();
		simple_thread_main();
	}

	/* The first wait() should report the stop from SIGSTOP. */
	wpid = waitpid(fpid, &status, 0);
	ATF_REQUIRE(wpid == fpid);
	ATF_REQUIRE(WIFSTOPPED(status));
	ATF_REQUIRE(WSTOPSIG(status) == SIGSTOP);

	ATF_REQUIRE(ptrace(PT_LWPINFO, wpid, (caddr_t)&pl,
	    sizeof(pl)) != -1);
	mainlwp = pl.pl_lwpid;

	/*
	 * Continue the child ignoring the SIGSTOP and tracing all
	 * system call exits.
	 */
	ATF_REQUIRE(ptrace(PT_TO_SCX, fpid, (caddr_t)1, 0) != -1);

	/*
	 * Wait for the new thread to arrive.  pthread_create() might
	 * invoke any number of system calls.  For now we just wait
	 * for the new thread to arrive and make sure it reports a
	 * valid system call code.  If ptrace grows thread event
	 * reporting then this test can be made more precise.
	 */
	for (;;) {
		wpid = waitpid(fpid, &status, 0);
		ATF_REQUIRE(wpid == fpid);
		ATF_REQUIRE(WIFSTOPPED(status));
		ATF_REQUIRE(WSTOPSIG(status) == SIGTRAP);
		
		ATF_REQUIRE(ptrace(PT_LWPINFO, wpid, (caddr_t)&pl,
		    sizeof(pl)) != -1);
		ATF_REQUIRE((pl.pl_flags & PL_FLAG_SCX) != 0);
		ATF_REQUIRE(pl.pl_syscall_code != 0);
		if (pl.pl_lwpid != mainlwp)
			/* New thread seen. */
			break;

		ATF_REQUIRE(ptrace(PT_CONTINUE, fpid, (caddr_t)1, 0) == 0);
	}

	/* Wait for the child to exit. */
	ATF_REQUIRE(ptrace(PT_CONTINUE, fpid, (caddr_t)1, 0) == 0);
	for (;;) {
		wpid = waitpid(fpid, &status, 0);
		ATF_REQUIRE(wpid == fpid);
		if (WIFEXITED(status))
			break;
		
		ATF_REQUIRE(WIFSTOPPED(status));
		ATF_REQUIRE(WSTOPSIG(status) == SIGTRAP);
		ATF_REQUIRE(ptrace(PT_CONTINUE, fpid, (caddr_t)1, 0) == 0);
	}
		
	ATF_REQUIRE(WEXITSTATUS(status) == 1);

	wpid = wait(&status);
	ATF_REQUIRE(wpid == -1);
	ATF_REQUIRE(errno == ECHILD);
}

/*
 * Verify that the expected LWP events are reported for a child thread.
 */
ATF_TC_WITHOUT_HEAD(ptrace__lwp_events);
ATF_TC_BODY(ptrace__lwp_events, tc)
{
	struct ptrace_lwpinfo pl;
	pid_t fpid, wpid;
	lwpid_t lwps[2];
	int status;

	ATF_REQUIRE((fpid = fork()) != -1);
	if (fpid == 0) {
		trace_me();
		simple_thread_main();
	}

	/* The first wait() should report the stop from SIGSTOP. */
	wpid = waitpid(fpid, &status, 0);
	ATF_REQUIRE(wpid == fpid);
	ATF_REQUIRE(WIFSTOPPED(status));
	ATF_REQUIRE(WSTOPSIG(status) == SIGSTOP);

	ATF_REQUIRE(ptrace(PT_LWPINFO, wpid, (caddr_t)&pl,
	    sizeof(pl)) != -1);
	lwps[0] = pl.pl_lwpid;

	ATF_REQUIRE(ptrace(PT_LWP_EVENTS, wpid, NULL, 1) == 0);

	/* Continue the child ignoring the SIGSTOP. */
	ATF_REQUIRE(ptrace(PT_CONTINUE, fpid, (caddr_t)1, 0) == 0);

	/* The first event should be for the child thread's birth. */
	wpid = waitpid(fpid, &status, 0);
	ATF_REQUIRE(wpid == fpid);
	ATF_REQUIRE(WIFSTOPPED(status));
	ATF_REQUIRE(WSTOPSIG(status) == SIGTRAP);
		
	ATF_REQUIRE(ptrace(PT_LWPINFO, wpid, (caddr_t)&pl, sizeof(pl)) != -1);
	ATF_REQUIRE((pl.pl_flags & (PL_FLAG_BORN | PL_FLAG_SCX)) ==
	    (PL_FLAG_BORN | PL_FLAG_SCX));
	ATF_REQUIRE(pl.pl_lwpid != lwps[0]);
	lwps[1] = pl.pl_lwpid;

	ATF_REQUIRE(ptrace(PT_CONTINUE, fpid, (caddr_t)1, 0) == 0);

	/* The next event should be for the child thread's death. */
	wpid = waitpid(fpid, &status, 0);
	ATF_REQUIRE(wpid == fpid);
	ATF_REQUIRE(WIFSTOPPED(status));
	ATF_REQUIRE(WSTOPSIG(status) == SIGTRAP);
		
	ATF_REQUIRE(ptrace(PT_LWPINFO, wpid, (caddr_t)&pl, sizeof(pl)) != -1);
	ATF_REQUIRE((pl.pl_flags & (PL_FLAG_EXITED | PL_FLAG_SCE)) ==
	    (PL_FLAG_EXITED | PL_FLAG_SCE));
	ATF_REQUIRE(pl.pl_lwpid == lwps[1]);

	ATF_REQUIRE(ptrace(PT_CONTINUE, fpid, (caddr_t)1, 0) == 0);

	/* The last event should be for the child process's exit. */
	wpid = waitpid(fpid, &status, 0);
	ATF_REQUIRE(WIFEXITED(status));
	ATF_REQUIRE(WEXITSTATUS(status) == 1);

	wpid = wait(&status);
	ATF_REQUIRE(wpid == -1);
	ATF_REQUIRE(errno == ECHILD);
}

static void *
exec_thread(void *arg __unused)
{

	execl("/usr/bin/true", "true", NULL);
	exit(127);
}

static __dead2 void
exec_thread_main(void)
{
	pthread_t thread;

	CHILD_REQUIRE(pthread_create(&thread, NULL, exec_thread, NULL) == 0);
	for (;;)
		sleep(60);
	exit(1);
}

/*
 * Verify that the expected LWP events are reported for a multithreaded
 * process that calls execve(2).
 */
ATF_TC_WITHOUT_HEAD(ptrace__lwp_events_exec);
ATF_TC_BODY(ptrace__lwp_events_exec, tc)
{
	struct ptrace_lwpinfo pl;
	pid_t fpid, wpid;
	lwpid_t lwps[2];
	int status;

	ATF_REQUIRE((fpid = fork()) != -1);
	if (fpid == 0) {
		trace_me();
		exec_thread_main();
	}

	/* The first wait() should report the stop from SIGSTOP. */
	wpid = waitpid(fpid, &status, 0);
	ATF_REQUIRE(wpid == fpid);
	ATF_REQUIRE(WIFSTOPPED(status));
	ATF_REQUIRE(WSTOPSIG(status) == SIGSTOP);

	ATF_REQUIRE(ptrace(PT_LWPINFO, wpid, (caddr_t)&pl,
	    sizeof(pl)) != -1);
	lwps[0] = pl.pl_lwpid;

	ATF_REQUIRE(ptrace(PT_LWP_EVENTS, wpid, NULL, 1) == 0);

	/* Continue the child ignoring the SIGSTOP. */
	ATF_REQUIRE(ptrace(PT_CONTINUE, fpid, (caddr_t)1, 0) == 0);

	/* The first event should be for the child thread's birth. */
	wpid = waitpid(fpid, &status, 0);
	ATF_REQUIRE(wpid == fpid);
	ATF_REQUIRE(WIFSTOPPED(status));
	ATF_REQUIRE(WSTOPSIG(status) == SIGTRAP);
		
	ATF_REQUIRE(ptrace(PT_LWPINFO, wpid, (caddr_t)&pl, sizeof(pl)) != -1);
	ATF_REQUIRE((pl.pl_flags & (PL_FLAG_BORN | PL_FLAG_SCX)) ==
	    (PL_FLAG_BORN | PL_FLAG_SCX));
	ATF_REQUIRE(pl.pl_lwpid != lwps[0]);
	lwps[1] = pl.pl_lwpid;

	ATF_REQUIRE(ptrace(PT_CONTINUE, fpid, (caddr_t)1, 0) == 0);

	/*
	 * The next event should be for the main thread's death due to
	 * single threading from execve().
	 */
	wpid = waitpid(fpid, &status, 0);
	ATF_REQUIRE(wpid == fpid);
	ATF_REQUIRE(WIFSTOPPED(status));
	ATF_REQUIRE(WSTOPSIG(status) == SIGTRAP);

	ATF_REQUIRE(ptrace(PT_LWPINFO, wpid, (caddr_t)&pl, sizeof(pl)) != -1);
	ATF_REQUIRE((pl.pl_flags & (PL_FLAG_EXITED | PL_FLAG_SCE)) ==
	    (PL_FLAG_EXITED));
	ATF_REQUIRE(pl.pl_lwpid == lwps[0]);

	ATF_REQUIRE(ptrace(PT_CONTINUE, fpid, (caddr_t)1, 0) == 0);

	/* The next event should be for the child process's exec. */
	wpid = waitpid(fpid, &status, 0);
	ATF_REQUIRE(WIFSTOPPED(status));
	ATF_REQUIRE(WSTOPSIG(status) == SIGTRAP);

	ATF_REQUIRE(ptrace(PT_LWPINFO, wpid, (caddr_t)&pl, sizeof(pl)) != -1);
	ATF_REQUIRE((pl.pl_flags & (PL_FLAG_EXEC | PL_FLAG_SCX)) ==
	    (PL_FLAG_EXEC | PL_FLAG_SCX));
	ATF_REQUIRE(pl.pl_lwpid == lwps[1]);

	ATF_REQUIRE(ptrace(PT_CONTINUE, fpid, (caddr_t)1, 0) == 0);

	/* The last event should be for the child process's exit. */
	wpid = waitpid(fpid, &status, 0);
	ATF_REQUIRE(WIFEXITED(status));
	ATF_REQUIRE(WEXITSTATUS(status) == 0);

	wpid = wait(&status);
	ATF_REQUIRE(wpid == -1);
	ATF_REQUIRE(errno == ECHILD);
}

static void
handler(int sig __unused)
{
}

static void
signal_main(void)
{

	signal(SIGINFO, handler);
	raise(SIGINFO);
	exit(0);
}

/*
 * Verify that the expected ptrace event is reported for a signal.
 */
ATF_TC_WITHOUT_HEAD(ptrace__siginfo);
ATF_TC_BODY(ptrace__siginfo, tc)
{
	struct ptrace_lwpinfo pl;
	pid_t fpid, wpid;
	int status;

	ATF_REQUIRE((fpid = fork()) != -1);
	if (fpid == 0) {
		trace_me();
		signal_main();
	}

	/* The first wait() should report the stop from SIGSTOP. */
	wpid = waitpid(fpid, &status, 0);
	ATF_REQUIRE(wpid == fpid);
	ATF_REQUIRE(WIFSTOPPED(status));
	ATF_REQUIRE(WSTOPSIG(status) == SIGSTOP);

	ATF_REQUIRE(ptrace(PT_CONTINUE, fpid, (caddr_t)1, 0) == 0);

	/* The next event should be for the SIGINFO. */
	wpid = waitpid(fpid, &status, 0);
	ATF_REQUIRE(WIFSTOPPED(status));
	ATF_REQUIRE(WSTOPSIG(status) == SIGINFO);

	ATF_REQUIRE(ptrace(PT_LWPINFO, wpid, (caddr_t)&pl, sizeof(pl)) != -1);
	ATF_REQUIRE(pl.pl_event == PL_EVENT_SIGNAL);
	ATF_REQUIRE(pl.pl_flags & PL_FLAG_SI);
	ATF_REQUIRE(pl.pl_siginfo.si_code == SI_LWP);
	ATF_REQUIRE(pl.pl_siginfo.si_pid == wpid);

	ATF_REQUIRE(ptrace(PT_CONTINUE, fpid, (caddr_t)1, 0) == 0);

	/* The last event should be for the child process's exit. */
	wpid = waitpid(fpid, &status, 0);
	ATF_REQUIRE(WIFEXITED(status));
	ATF_REQUIRE(WEXITSTATUS(status) == 0);

	wpid = wait(&status);
	ATF_REQUIRE(wpid == -1);
	ATF_REQUIRE(errno == ECHILD);
}

/*
 * Verify that the expected ptrace events are reported for PTRACE_EXEC.
 */
ATF_TC_WITHOUT_HEAD(ptrace__ptrace_exec_disable);
ATF_TC_BODY(ptrace__ptrace_exec_disable, tc)
{
	pid_t fpid, wpid;
	int events, status;

	ATF_REQUIRE((fpid = fork()) != -1);
	if (fpid == 0) {
		trace_me();
		exec_thread(NULL);
	}

	/* The first wait() should report the stop from SIGSTOP. */
	wpid = waitpid(fpid, &status, 0);
	ATF_REQUIRE(wpid == fpid);
	ATF_REQUIRE(WIFSTOPPED(status));
	ATF_REQUIRE(WSTOPSIG(status) == SIGSTOP);

	events = 0;
	ATF_REQUIRE(ptrace(PT_SET_EVENT_MASK, fpid, (caddr_t)&events,
	    sizeof(events)) == 0);

	ATF_REQUIRE(ptrace(PT_CONTINUE, fpid, (caddr_t)1, 0) == 0);

	/* Should get one event at exit. */
	wpid = waitpid(fpid, &status, 0);
	ATF_REQUIRE(WIFEXITED(status));
	ATF_REQUIRE(WEXITSTATUS(status) == 0);

	wpid = wait(&status);
	ATF_REQUIRE(wpid == -1);
	ATF_REQUIRE(errno == ECHILD);
}

ATF_TC_WITHOUT_HEAD(ptrace__ptrace_exec_enable);
ATF_TC_BODY(ptrace__ptrace_exec_enable, tc)
{
	struct ptrace_lwpinfo pl;
	pid_t fpid, wpid;
	int events, status;

	ATF_REQUIRE((fpid = fork()) != -1);
	if (fpid == 0) {
		trace_me();
		exec_thread(NULL);
	}

	/* The first wait() should report the stop from SIGSTOP. */
	wpid = waitpid(fpid, &status, 0);
	ATF_REQUIRE(wpid == fpid);
	ATF_REQUIRE(WIFSTOPPED(status));
	ATF_REQUIRE(WSTOPSIG(status) == SIGSTOP);

	events = PTRACE_EXEC;
	ATF_REQUIRE(ptrace(PT_SET_EVENT_MASK, fpid, (caddr_t)&events,
	    sizeof(events)) == 0);

	ATF_REQUIRE(ptrace(PT_CONTINUE, fpid, (caddr_t)1, 0) == 0);

	/* The next event should be for the child process's exec. */
	wpid = waitpid(fpid, &status, 0);
	ATF_REQUIRE(WIFSTOPPED(status));
	ATF_REQUIRE(WSTOPSIG(status) == SIGTRAP);

	ATF_REQUIRE(ptrace(PT_LWPINFO, wpid, (caddr_t)&pl, sizeof(pl)) != -1);
	ATF_REQUIRE((pl.pl_flags & (PL_FLAG_EXEC | PL_FLAG_SCX)) ==
	    (PL_FLAG_EXEC | PL_FLAG_SCX));

	ATF_REQUIRE(ptrace(PT_CONTINUE, fpid, (caddr_t)1, 0) == 0);

	/* The last event should be for the child process's exit. */
	wpid = waitpid(fpid, &status, 0);
	ATF_REQUIRE(WIFEXITED(status));
	ATF_REQUIRE(WEXITSTATUS(status) == 0);

	wpid = wait(&status);
	ATF_REQUIRE(wpid == -1);
	ATF_REQUIRE(errno == ECHILD);
}

ATF_TC_WITHOUT_HEAD(ptrace__event_mask);
ATF_TC_BODY(ptrace__event_mask, tc)
{
	pid_t fpid, wpid;
	int events, status;

	ATF_REQUIRE((fpid = fork()) != -1);
	if (fpid == 0) {
		trace_me();
		exit(0);
	}

	/* The first wait() should report the stop from SIGSTOP. */
	wpid = waitpid(fpid, &status, 0);
	ATF_REQUIRE(wpid == fpid);
	ATF_REQUIRE(WIFSTOPPED(status));
	ATF_REQUIRE(WSTOPSIG(status) == SIGSTOP);

	/* PT_FOLLOW_FORK should toggle the state of PTRACE_FORK. */
	ATF_REQUIRE(ptrace(PT_FOLLOW_FORK, fpid, NULL, 1) != -1);
	ATF_REQUIRE(ptrace(PT_GET_EVENT_MASK, fpid, (caddr_t)&events,
	    sizeof(events)) == 0);
	ATF_REQUIRE(events & PTRACE_FORK);
	ATF_REQUIRE(ptrace(PT_FOLLOW_FORK, fpid, NULL, 0) != -1);
	ATF_REQUIRE(ptrace(PT_GET_EVENT_MASK, fpid, (caddr_t)&events,
	    sizeof(events)) == 0);
	ATF_REQUIRE(!(events & PTRACE_FORK));

	/* PT_LWP_EVENTS should toggle the state of PTRACE_LWP. */
	ATF_REQUIRE(ptrace(PT_LWP_EVENTS, fpid, NULL, 1) != -1);
	ATF_REQUIRE(ptrace(PT_GET_EVENT_MASK, fpid, (caddr_t)&events,
	    sizeof(events)) == 0);
	ATF_REQUIRE(events & PTRACE_LWP);
	ATF_REQUIRE(ptrace(PT_LWP_EVENTS, fpid, NULL, 0) != -1);
	ATF_REQUIRE(ptrace(PT_GET_EVENT_MASK, fpid, (caddr_t)&events,
	    sizeof(events)) == 0);
	ATF_REQUIRE(!(events & PTRACE_LWP));

	ATF_REQUIRE(ptrace(PT_CONTINUE, fpid, (caddr_t)1, 0) == 0);

	/* Should get one event at exit. */
	wpid = waitpid(fpid, &status, 0);
	ATF_REQUIRE(WIFEXITED(status));
	ATF_REQUIRE(WEXITSTATUS(status) == 0);

	wpid = wait(&status);
	ATF_REQUIRE(wpid == -1);
	ATF_REQUIRE(errno == ECHILD);
}

/*
 * Verify that the expected ptrace events are reported for PTRACE_VFORK.
 */
ATF_TC_WITHOUT_HEAD(ptrace__ptrace_vfork);
ATF_TC_BODY(ptrace__ptrace_vfork, tc)
{
	struct ptrace_lwpinfo pl;
	pid_t fpid, wpid;
	int events, status;

	ATF_REQUIRE((fpid = fork()) != -1);
	if (fpid == 0) {
		trace_me();
		follow_fork_parent(true);
	}

	/* The first wait() should report the stop from SIGSTOP. */
	wpid = waitpid(fpid, &status, 0);
	ATF_REQUIRE(wpid == fpid);
	ATF_REQUIRE(WIFSTOPPED(status));
	ATF_REQUIRE(WSTOPSIG(status) == SIGSTOP);

	ATF_REQUIRE(ptrace(PT_GET_EVENT_MASK, fpid, (caddr_t)&events,
	    sizeof(events)) == 0);
	events |= PTRACE_VFORK;
	ATF_REQUIRE(ptrace(PT_SET_EVENT_MASK, fpid, (caddr_t)&events,
	    sizeof(events)) == 0);
	
	/* Continue the child ignoring the SIGSTOP. */
	ATF_REQUIRE(ptrace(PT_CONTINUE, fpid, (caddr_t)1, 0) != -1);

	/* The next event should report the end of the vfork. */
	wpid = wait(&status);
	ATF_REQUIRE(wpid == fpid);
	ATF_REQUIRE(WIFSTOPPED(status));
	ATF_REQUIRE(WSTOPSIG(status) == SIGTRAP);
	ATF_REQUIRE(ptrace(PT_LWPINFO, wpid, (caddr_t)&pl, sizeof(pl)) != -1);
	ATF_REQUIRE((pl.pl_flags & PL_FLAG_VFORK_DONE) != 0);

	ATF_REQUIRE(ptrace(PT_CONTINUE, fpid, (caddr_t)1, 0) != -1);

	wpid = wait(&status);
	ATF_REQUIRE(wpid == fpid);
	ATF_REQUIRE(WIFEXITED(status));
	ATF_REQUIRE(WEXITSTATUS(status) == 1);

	wpid = wait(&status);
	ATF_REQUIRE(wpid == -1);
	ATF_REQUIRE(errno == ECHILD);
}

ATF_TC_WITHOUT_HEAD(ptrace__ptrace_vfork_follow);
ATF_TC_BODY(ptrace__ptrace_vfork_follow, tc)
{
	struct ptrace_lwpinfo pl[2];
	pid_t children[2], fpid, wpid;
	int events, status;

	ATF_REQUIRE((fpid = fork()) != -1);
	if (fpid == 0) {
		trace_me();
		follow_fork_parent(true);
	}

	/* Parent process. */
	children[0] = fpid;

	/* The first wait() should report the stop from SIGSTOP. */
	wpid = waitpid(children[0], &status, 0);
	ATF_REQUIRE(wpid == children[0]);
	ATF_REQUIRE(WIFSTOPPED(status));
	ATF_REQUIRE(WSTOPSIG(status) == SIGSTOP);

	ATF_REQUIRE(ptrace(PT_GET_EVENT_MASK, children[0], (caddr_t)&events,
	    sizeof(events)) == 0);
	events |= PTRACE_FORK | PTRACE_VFORK;
	ATF_REQUIRE(ptrace(PT_SET_EVENT_MASK, children[0], (caddr_t)&events,
	    sizeof(events)) == 0);

	/* Continue the child ignoring the SIGSTOP. */
	ATF_REQUIRE(ptrace(PT_CONTINUE, children[0], (caddr_t)1, 0) != -1);

	/* Wait for both halves of the fork event to get reported. */
	children[1] = handle_fork_events(children[0], pl);
	ATF_REQUIRE(children[1] > 0);

	ATF_REQUIRE((pl[0].pl_flags & PL_FLAG_VFORKED) != 0);

	ATF_REQUIRE(ptrace(PT_CONTINUE, children[0], (caddr_t)1, 0) != -1);
	ATF_REQUIRE(ptrace(PT_CONTINUE, children[1], (caddr_t)1, 0) != -1);

	/*
	 * The child can't exit until the grandchild reports status, so the
	 * grandchild should report its exit first to the debugger.
	 */
	wpid = waitpid(children[1], &status, 0);
	ATF_REQUIRE(wpid == children[1]);
	ATF_REQUIRE(WIFEXITED(status));
	ATF_REQUIRE(WEXITSTATUS(status) == 2);

	/*
	 * The child should report it's vfork() completion before it
	 * exits.
	 */
	wpid = wait(&status);
	ATF_REQUIRE(wpid == children[0]);
	ATF_REQUIRE(WIFSTOPPED(status));
	ATF_REQUIRE(WSTOPSIG(status) == SIGTRAP);
	ATF_REQUIRE(ptrace(PT_LWPINFO, wpid, (caddr_t)&pl[0], sizeof(pl[0])) !=
	    -1);
	ATF_REQUIRE((pl[0].pl_flags & PL_FLAG_VFORK_DONE) != 0);

	ATF_REQUIRE(ptrace(PT_CONTINUE, children[0], (caddr_t)1, 0) != -1);

	wpid = wait(&status);
	ATF_REQUIRE(wpid == children[0]);
	ATF_REQUIRE(WIFEXITED(status));
	ATF_REQUIRE(WEXITSTATUS(status) == 1);

	wpid = wait(&status);
	ATF_REQUIRE(wpid == -1);
	ATF_REQUIRE(errno == ECHILD);
}

#ifdef HAVE_BREAKPOINT
/*
 * Verify that no more events are reported after PT_KILL except for the
 * process exit when stopped due to a breakpoint trap.
 */
ATF_TC_WITHOUT_HEAD(ptrace__PT_KILL_breakpoint);
ATF_TC_BODY(ptrace__PT_KILL_breakpoint, tc)
{
	pid_t fpid, wpid;
	int status;

	ATF_REQUIRE((fpid = fork()) != -1);
	if (fpid == 0) {
		trace_me();
		breakpoint();
		exit(1);
	}

	/* The first wait() should report the stop from SIGSTOP. */
	wpid = waitpid(fpid, &status, 0);
	ATF_REQUIRE(wpid == fpid);
	ATF_REQUIRE(WIFSTOPPED(status));
	ATF_REQUIRE(WSTOPSIG(status) == SIGSTOP);

	/* Continue the child ignoring the SIGSTOP. */
	ATF_REQUIRE(ptrace(PT_CONTINUE, fpid, (caddr_t)1, 0) == 0);

	/* The second wait() should report hitting the breakpoint. */
	wpid = waitpid(fpid, &status, 0);
	ATF_REQUIRE(wpid == fpid);
	ATF_REQUIRE(WIFSTOPPED(status));
	ATF_REQUIRE(WSTOPSIG(status) == SIGTRAP);

	/* Kill the child process. */
	ATF_REQUIRE(ptrace(PT_KILL, fpid, 0, 0) == 0);

	/* The last wait() should report the SIGKILL. */
	wpid = waitpid(fpid, &status, 0);
	ATF_REQUIRE(wpid == fpid);
	ATF_REQUIRE(WIFSIGNALED(status));
	ATF_REQUIRE(WTERMSIG(status) == SIGKILL);

	wpid = wait(&status);
	ATF_REQUIRE(wpid == -1);
	ATF_REQUIRE(errno == ECHILD);
}
#endif /* HAVE_BREAKPOINT */

/*
 * Verify that no more events are reported after PT_KILL except for the
 * process exit when stopped inside of a system call.
 */
ATF_TC_WITHOUT_HEAD(ptrace__PT_KILL_system_call);
ATF_TC_BODY(ptrace__PT_KILL_system_call, tc)
{
	struct ptrace_lwpinfo pl;
	pid_t fpid, wpid;
	int status;

	ATF_REQUIRE((fpid = fork()) != -1);
	if (fpid == 0) {
		trace_me();
		getpid();
		exit(1);
	}

	/* The first wait() should report the stop from SIGSTOP. */
	wpid = waitpid(fpid, &status, 0);
	ATF_REQUIRE(wpid == fpid);
	ATF_REQUIRE(WIFSTOPPED(status));
	ATF_REQUIRE(WSTOPSIG(status) == SIGSTOP);

	/* Continue the child ignoring the SIGSTOP and tracing system calls. */
	ATF_REQUIRE(ptrace(PT_SYSCALL, fpid, (caddr_t)1, 0) == 0);

	/* The second wait() should report a system call entry for getpid(). */
	wpid = waitpid(fpid, &status, 0);
	ATF_REQUIRE(wpid == fpid);
	ATF_REQUIRE(WIFSTOPPED(status));
	ATF_REQUIRE(WSTOPSIG(status) == SIGTRAP);

	ATF_REQUIRE(ptrace(PT_LWPINFO, wpid, (caddr_t)&pl, sizeof(pl)) != -1);
	ATF_REQUIRE(pl.pl_flags & PL_FLAG_SCE);

	/* Kill the child process. */
	ATF_REQUIRE(ptrace(PT_KILL, fpid, 0, 0) == 0);

	/* The last wait() should report the SIGKILL. */
	wpid = waitpid(fpid, &status, 0);
	ATF_REQUIRE(wpid == fpid);
	ATF_REQUIRE(WIFSIGNALED(status));
	ATF_REQUIRE(WTERMSIG(status) == SIGKILL);

	wpid = wait(&status);
	ATF_REQUIRE(wpid == -1);
	ATF_REQUIRE(errno == ECHILD);
}

/*
 * Verify that no more events are reported after PT_KILL except for the
 * process exit when killing a multithreaded process.
 */
ATF_TC_WITHOUT_HEAD(ptrace__PT_KILL_threads);
ATF_TC_BODY(ptrace__PT_KILL_threads, tc)
{
	struct ptrace_lwpinfo pl;
	pid_t fpid, wpid;
	lwpid_t main_lwp;
	int status;

	ATF_REQUIRE((fpid = fork()) != -1);
	if (fpid == 0) {
		trace_me();
		simple_thread_main();
	}

	/* The first wait() should report the stop from SIGSTOP. */
	wpid = waitpid(fpid, &status, 0);
	ATF_REQUIRE(wpid == fpid);
	ATF_REQUIRE(WIFSTOPPED(status));
	ATF_REQUIRE(WSTOPSIG(status) == SIGSTOP);

	ATF_REQUIRE(ptrace(PT_LWPINFO, wpid, (caddr_t)&pl,
	    sizeof(pl)) != -1);
	main_lwp = pl.pl_lwpid;

	ATF_REQUIRE(ptrace(PT_LWP_EVENTS, wpid, NULL, 1) == 0);

	/* Continue the child ignoring the SIGSTOP. */
	ATF_REQUIRE(ptrace(PT_CONTINUE, fpid, (caddr_t)1, 0) == 0);

	/* The first event should be for the child thread's birth. */
	wpid = waitpid(fpid, &status, 0);
	ATF_REQUIRE(wpid == fpid);
	ATF_REQUIRE(WIFSTOPPED(status));
	ATF_REQUIRE(WSTOPSIG(status) == SIGTRAP);
		
	ATF_REQUIRE(ptrace(PT_LWPINFO, wpid, (caddr_t)&pl, sizeof(pl)) != -1);
	ATF_REQUIRE((pl.pl_flags & (PL_FLAG_BORN | PL_FLAG_SCX)) ==
	    (PL_FLAG_BORN | PL_FLAG_SCX));
	ATF_REQUIRE(pl.pl_lwpid != main_lwp);

	/* Kill the child process. */
	ATF_REQUIRE(ptrace(PT_KILL, fpid, 0, 0) == 0);

	/* The last wait() should report the SIGKILL. */
	wpid = waitpid(fpid, &status, 0);
	ATF_REQUIRE(wpid == fpid);
	ATF_REQUIRE(WIFSIGNALED(status));
	ATF_REQUIRE(WTERMSIG(status) == SIGKILL);

	wpid = wait(&status);
	ATF_REQUIRE(wpid == -1);
	ATF_REQUIRE(errno == ECHILD);
}

static void *
mask_usr1_thread(void *arg)
{
	pthread_barrier_t *pbarrier;
	sigset_t sigmask;

	pbarrier = (pthread_barrier_t*)arg;

	sigemptyset(&sigmask);
	sigaddset(&sigmask, SIGUSR1);
	CHILD_REQUIRE(pthread_sigmask(SIG_BLOCK, &sigmask, NULL) == 0);

	/* Sync up with other thread after sigmask updated. */
	pthread_barrier_wait(pbarrier);

	for (;;)
		sleep(60);

	return (NULL);
}

/*
 * Verify that the SIGKILL from PT_KILL takes priority over other signals
 * and prevents spurious stops due to those other signals.
 */
ATF_TC(ptrace__PT_KILL_competing_signal);
ATF_TC_HEAD(ptrace__PT_KILL_competing_signal, tc)
{

	atf_tc_set_md_var(tc, "require.user", "root");
}
ATF_TC_BODY(ptrace__PT_KILL_competing_signal, tc)
{
	pid_t fpid, wpid;
	int status;
	cpuset_t setmask;
	pthread_t t;
	pthread_barrier_t barrier;
	struct sched_param sched_param;

	ATF_REQUIRE((fpid = fork()) != -1);
	if (fpid == 0) {
		/* Bind to one CPU so only one thread at a time will run. */
		CPU_ZERO(&setmask);
		CPU_SET(0, &setmask);
		cpusetid_t setid;
		CHILD_REQUIRE(cpuset(&setid) == 0);
		CHILD_REQUIRE(cpuset_setaffinity(CPU_LEVEL_CPUSET,
		    CPU_WHICH_CPUSET, setid, sizeof(setmask), &setmask) == 0);

		CHILD_REQUIRE(pthread_barrier_init(&barrier, NULL, 2) == 0);

		CHILD_REQUIRE(pthread_create(&t, NULL, mask_usr1_thread,
		    (void*)&barrier) == 0);

		/*
		 * Give the main thread higher priority. The test always
		 * assumes that, if both threads are able to run, the main
		 * thread runs first.
		 */
		sched_param.sched_priority =
		    (sched_get_priority_max(SCHED_FIFO) +
		    sched_get_priority_min(SCHED_FIFO)) / 2;
		CHILD_REQUIRE(pthread_setschedparam(pthread_self(),
		    SCHED_FIFO, &sched_param) == 0);
		sched_param.sched_priority -= RQ_PPQ;
		CHILD_REQUIRE(pthread_setschedparam(t, SCHED_FIFO,
		    &sched_param) == 0);

		sigset_t sigmask;
		sigemptyset(&sigmask);
		sigaddset(&sigmask, SIGUSR2);
		CHILD_REQUIRE(pthread_sigmask(SIG_BLOCK, &sigmask, NULL) == 0);

		/* Sync up with other thread after sigmask updated. */
		pthread_barrier_wait(&barrier);

		trace_me();

		for (;;)
			sleep(60);

		exit(1);
	}

	/* The first wait() should report the stop from SIGSTOP. */
	wpid = waitpid(fpid, &status, 0);
	ATF_REQUIRE(wpid == fpid);
	ATF_REQUIRE(WIFSTOPPED(status));
	ATF_REQUIRE(WSTOPSIG(status) == SIGSTOP);

	/* Continue the child ignoring the SIGSTOP. */
	ATF_REQUIRE(ptrace(PT_CONTINUE, fpid, (caddr_t)1, 0) == 0);

	/* Send a signal that only the second thread can handle. */
	ATF_REQUIRE(kill(fpid, SIGUSR2) == 0);

	/* The second wait() should report the SIGUSR2. */
	wpid = waitpid(fpid, &status, 0);
	ATF_REQUIRE(wpid == fpid);
	ATF_REQUIRE(WIFSTOPPED(status));
	ATF_REQUIRE(WSTOPSIG(status) == SIGUSR2);

	/* Send a signal that only the first thread can handle. */
	ATF_REQUIRE(kill(fpid, SIGUSR1) == 0);

	/* Replace the SIGUSR2 with a kill. */
	ATF_REQUIRE(ptrace(PT_KILL, fpid, 0, 0) == 0);

	/* The last wait() should report the SIGKILL (not the SIGUSR signal). */
	wpid = waitpid(fpid, &status, 0);
	ATF_REQUIRE(wpid == fpid);
	ATF_REQUIRE(WIFSIGNALED(status));
	ATF_REQUIRE(WTERMSIG(status) == SIGKILL);

	wpid = wait(&status);
	ATF_REQUIRE(wpid == -1);
	ATF_REQUIRE(errno == ECHILD);
}

/*
 * Verify that the SIGKILL from PT_KILL takes priority over other stop events
 * and prevents spurious stops caused by those events.
 */
ATF_TC(ptrace__PT_KILL_competing_stop);
ATF_TC_HEAD(ptrace__PT_KILL_competing_stop, tc)
{

	atf_tc_set_md_var(tc, "require.user", "root");
}
ATF_TC_BODY(ptrace__PT_KILL_competing_stop, tc)
{
	pid_t fpid, wpid;
	int status;
	cpuset_t setmask;
	pthread_t t;
	pthread_barrier_t barrier;
	lwpid_t main_lwp;
	struct ptrace_lwpinfo pl;
	struct sched_param sched_param;

	ATF_REQUIRE((fpid = fork()) != -1);
	if (fpid == 0) {
		trace_me();

		/* Bind to one CPU so only one thread at a time will run. */
		CPU_ZERO(&setmask);
		CPU_SET(0, &setmask);
		cpusetid_t setid;
		CHILD_REQUIRE(cpuset(&setid) == 0);
		CHILD_REQUIRE(cpuset_setaffinity(CPU_LEVEL_CPUSET,
		    CPU_WHICH_CPUSET, setid, sizeof(setmask), &setmask) == 0);

		CHILD_REQUIRE(pthread_barrier_init(&barrier, NULL, 2) == 0);

		CHILD_REQUIRE(pthread_create(&t, NULL, mask_usr1_thread,
		    (void*)&barrier) == 0);

		/*
		 * Give the main thread higher priority. The test always
		 * assumes that, if both threads are able to run, the main
		 * thread runs first.
		 */
		sched_param.sched_priority =
		    (sched_get_priority_max(SCHED_FIFO) +
		    sched_get_priority_min(SCHED_FIFO)) / 2;
		CHILD_REQUIRE(pthread_setschedparam(pthread_self(),
		    SCHED_FIFO, &sched_param) == 0);
		sched_param.sched_priority -= RQ_PPQ;
		CHILD_REQUIRE(pthread_setschedparam(t, SCHED_FIFO,
		    &sched_param) == 0);

		sigset_t sigmask;
		sigemptyset(&sigmask);
		sigaddset(&sigmask, SIGUSR2);
		CHILD_REQUIRE(pthread_sigmask(SIG_BLOCK, &sigmask, NULL) == 0);

		/* Sync up with other thread after sigmask updated. */
		pthread_barrier_wait(&barrier);

		/* Sync up with the test before doing the getpid(). */
		raise(SIGSTOP);

		getpid();
		exit(1);
	}

	/* The first wait() should report the stop from SIGSTOP. */
	wpid = waitpid(fpid, &status, 0);
	ATF_REQUIRE(wpid == fpid);
	ATF_REQUIRE(WIFSTOPPED(status));
	ATF_REQUIRE(WSTOPSIG(status) == SIGSTOP);

	ATF_REQUIRE(ptrace(PT_LWPINFO, wpid, (caddr_t)&pl, sizeof(pl)) != -1);
	main_lwp = pl.pl_lwpid;

	/* Continue the child ignoring the SIGSTOP and tracing system calls. */
	ATF_REQUIRE(ptrace(PT_SYSCALL, fpid, (caddr_t)1, 0) == 0);

	/*
	 * Continue until child is done with setup, which is indicated with
	 * SIGSTOP. Ignore system calls in the meantime.
	 */
	for (;;) {
		wpid = waitpid(fpid, &status, 0);
		ATF_REQUIRE(wpid == fpid);
		ATF_REQUIRE(WIFSTOPPED(status));
		if (WSTOPSIG(status) == SIGTRAP) {
			ATF_REQUIRE(ptrace(PT_LWPINFO, wpid, (caddr_t)&pl,
			    sizeof(pl)) != -1);
			ATF_REQUIRE(pl.pl_flags & (PL_FLAG_SCE | PL_FLAG_SCX));
		} else {
			ATF_REQUIRE(WSTOPSIG(status) == SIGSTOP);
			break;
		}
		ATF_REQUIRE(ptrace(PT_SYSCALL, fpid, (caddr_t)1, 0) == 0);
	}

	/* Proceed, allowing main thread to hit syscall entry for getpid(). */
	ATF_REQUIRE(ptrace(PT_SYSCALL, fpid, (caddr_t)1, 0) == 0);

	wpid = waitpid(fpid, &status, 0);
	ATF_REQUIRE(wpid == fpid);
	ATF_REQUIRE(WIFSTOPPED(status));
	ATF_REQUIRE(WSTOPSIG(status) == SIGTRAP);

	ATF_REQUIRE(ptrace(PT_LWPINFO, wpid, (caddr_t)&pl,
	    sizeof(pl)) != -1);
	ATF_REQUIRE(pl.pl_lwpid == main_lwp);
	ATF_REQUIRE(pl.pl_flags & PL_FLAG_SCE);
	/* Prevent the main thread from hitting its syscall exit for now. */
	ATF_REQUIRE(ptrace(PT_SUSPEND, main_lwp, 0, 0) == 0);

	/*
	 * Proceed, allowing second thread to hit syscall exit for
	 * pthread_barrier_wait().
	 */
	ATF_REQUIRE(ptrace(PT_SYSCALL, fpid, (caddr_t)1, 0) == 0);

	wpid = waitpid(fpid, &status, 0);
	ATF_REQUIRE(wpid == fpid);
	ATF_REQUIRE(WIFSTOPPED(status));
	ATF_REQUIRE(WSTOPSIG(status) == SIGTRAP);

	ATF_REQUIRE(ptrace(PT_LWPINFO, wpid, (caddr_t)&pl,
	    sizeof(pl)) != -1);
	ATF_REQUIRE(pl.pl_lwpid != main_lwp);
	ATF_REQUIRE(pl.pl_flags & PL_FLAG_SCX);

	/* Send a signal that only the second thread can handle. */
	ATF_REQUIRE(kill(fpid, SIGUSR2) == 0);

	ATF_REQUIRE(ptrace(PT_SYSCALL, fpid, (caddr_t)1, 0) == 0);

	/* The next wait() should report the SIGUSR2. */
	wpid = waitpid(fpid, &status, 0);
	ATF_REQUIRE(wpid == fpid);
	ATF_REQUIRE(WIFSTOPPED(status));
	ATF_REQUIRE(WSTOPSIG(status) == SIGUSR2);

	/* Allow the main thread to try to finish its system call. */
	ATF_REQUIRE(ptrace(PT_RESUME, main_lwp, 0, 0) == 0);

	/*
	 * At this point, the main thread is in the middle of a system call and
	 * has been resumed. The second thread has taken a SIGUSR2 which will
	 * be replaced with a SIGKILL below. The main thread will get to run
	 * first. It should notice the kill request (even though the signal
	 * replacement occurred in the other thread) and exit accordingly.  It
	 * should not stop for the system call exit event.
	 */

	/* Replace the SIGUSR2 with a kill. */
	ATF_REQUIRE(ptrace(PT_KILL, fpid, 0, 0) == 0);

	/* The last wait() should report the SIGKILL (not a syscall exit). */
	wpid = waitpid(fpid, &status, 0);
	ATF_REQUIRE(wpid == fpid);
	ATF_REQUIRE(WIFSIGNALED(status));
	ATF_REQUIRE(WTERMSIG(status) == SIGKILL);

	wpid = wait(&status);
	ATF_REQUIRE(wpid == -1);
	ATF_REQUIRE(errno == ECHILD);
}

static void
sigusr1_handler(int sig)
{

	CHILD_REQUIRE(sig == SIGUSR1);
	_exit(2);
}

/*
 * Verify that even if the signal queue is full for a child process,
 * a PT_KILL will kill the process.
 */
ATF_TC_WITHOUT_HEAD(ptrace__PT_KILL_with_signal_full_sigqueue);
ATF_TC_BODY(ptrace__PT_KILL_with_signal_full_sigqueue, tc)
{
	pid_t fpid, wpid;
	int status;
	int max_pending_per_proc;
	size_t len;
	int i;

	ATF_REQUIRE(signal(SIGUSR1, sigusr1_handler) != SIG_ERR);

	ATF_REQUIRE((fpid = fork()) != -1);
	if (fpid == 0) {
		trace_me();
		exit(1);
	}

	/* The first wait() should report the stop from SIGSTOP. */
	wpid = waitpid(fpid, &status, 0);
	ATF_REQUIRE(wpid == fpid);
	ATF_REQUIRE(WIFSTOPPED(status));
	ATF_REQUIRE(WSTOPSIG(status) == SIGSTOP);

	len = sizeof(max_pending_per_proc);
	ATF_REQUIRE(sysctlbyname("kern.sigqueue.max_pending_per_proc",
	    &max_pending_per_proc, &len, NULL, 0) == 0);

	/* Fill the signal queue. */
	for (i = 0; i < max_pending_per_proc; ++i)
		ATF_REQUIRE(kill(fpid, SIGUSR1) == 0);

	/* Kill the child process. */
	ATF_REQUIRE(ptrace(PT_KILL, fpid, 0, 0) == 0);

	/* The last wait() should report the SIGKILL. */
	wpid = waitpid(fpid, &status, 0);
	ATF_REQUIRE(wpid == fpid);
	ATF_REQUIRE(WIFSIGNALED(status));
	ATF_REQUIRE(WTERMSIG(status) == SIGKILL);

	wpid = wait(&status);
	ATF_REQUIRE(wpid == -1);
	ATF_REQUIRE(errno == ECHILD);
}

/*
 * Verify that when stopped at a system call entry, a signal can be
 * requested with PT_CONTINUE which will be delivered once the system
 * call is complete.
 */
ATF_TC_WITHOUT_HEAD(ptrace__PT_CONTINUE_with_signal_system_call_entry);
ATF_TC_BODY(ptrace__PT_CONTINUE_with_signal_system_call_entry, tc)
{
	struct ptrace_lwpinfo pl;
	pid_t fpid, wpid;
	int status;

	ATF_REQUIRE(signal(SIGUSR1, sigusr1_handler) != SIG_ERR);

	ATF_REQUIRE((fpid = fork()) != -1);
	if (fpid == 0) {
		trace_me();
		getpid();
		exit(1);
	}

	/* The first wait() should report the stop from SIGSTOP. */
	wpid = waitpid(fpid, &status, 0);
	ATF_REQUIRE(wpid == fpid);
	ATF_REQUIRE(WIFSTOPPED(status));
	ATF_REQUIRE(WSTOPSIG(status) == SIGSTOP);

	/* Continue the child ignoring the SIGSTOP and tracing system calls. */
	ATF_REQUIRE(ptrace(PT_SYSCALL, fpid, (caddr_t)1, 0) == 0);

	/* The second wait() should report a system call entry for getpid(). */
	wpid = waitpid(fpid, &status, 0);
	ATF_REQUIRE(wpid == fpid);
	ATF_REQUIRE(WIFSTOPPED(status));
	ATF_REQUIRE(WSTOPSIG(status) == SIGTRAP);

	ATF_REQUIRE(ptrace(PT_LWPINFO, wpid, (caddr_t)&pl, sizeof(pl)) != -1);
	ATF_REQUIRE(pl.pl_flags & PL_FLAG_SCE);

	/* Continue the child process with a signal. */
	ATF_REQUIRE(ptrace(PT_CONTINUE, fpid, (caddr_t)1, SIGUSR1) == 0);

	for (;;) {
		/*
		 * The last wait() should report exit 2, i.e., a normal _exit
		 * from the signal handler. In the meantime, catch and proceed
		 * past any syscall stops.
		 */
		wpid = waitpid(fpid, &status, 0);
		ATF_REQUIRE(wpid == fpid);
		if (WIFSTOPPED(status) && WSTOPSIG(status) == SIGTRAP) {
			ATF_REQUIRE(ptrace(PT_LWPINFO, wpid, (caddr_t)&pl, sizeof(pl)) != -1);
			ATF_REQUIRE(pl.pl_flags & (PL_FLAG_SCE | PL_FLAG_SCX));
			ATF_REQUIRE(ptrace(PT_CONTINUE, fpid, (caddr_t)1, 0) == 0);
		} else {
			ATF_REQUIRE(WIFEXITED(status));
			ATF_REQUIRE(WEXITSTATUS(status) == 2);
			break;
		}
	}

	wpid = wait(&status);
	ATF_REQUIRE(wpid == -1);
	ATF_REQUIRE(errno == ECHILD);
}

static void
sigusr1_counting_handler(int sig)
{
	static int counter = 0;

	CHILD_REQUIRE(sig == SIGUSR1);
	counter++;
	if (counter == 2)
		_exit(2);
}

/*
 * Verify that, when continuing from a stop at system call entry and exit,
 * a signal can be requested from both stops, and both will be delivered when
 * the system call is complete.
 */
ATF_TC_WITHOUT_HEAD(ptrace__PT_CONTINUE_with_signal_system_call_entry_and_exit);
ATF_TC_BODY(ptrace__PT_CONTINUE_with_signal_system_call_entry_and_exit, tc)
{
	struct ptrace_lwpinfo pl;
	pid_t fpid, wpid;
	int status;

	ATF_REQUIRE(signal(SIGUSR1, sigusr1_counting_handler) != SIG_ERR);

	ATF_REQUIRE((fpid = fork()) != -1);
	if (fpid == 0) {
		trace_me();
		getpid();
		exit(1);
	}

	/* The first wait() should report the stop from SIGSTOP. */
	wpid = waitpid(fpid, &status, 0);
	ATF_REQUIRE(wpid == fpid);
	ATF_REQUIRE(WIFSTOPPED(status));
	ATF_REQUIRE(WSTOPSIG(status) == SIGSTOP);

	/* Continue the child ignoring the SIGSTOP and tracing system calls. */
	ATF_REQUIRE(ptrace(PT_SYSCALL, fpid, (caddr_t)1, 0) == 0);

	/* The second wait() should report a system call entry for getpid(). */
	wpid = waitpid(fpid, &status, 0);
	ATF_REQUIRE(wpid == fpid);
	ATF_REQUIRE(WIFSTOPPED(status));
	ATF_REQUIRE(WSTOPSIG(status) == SIGTRAP);

	ATF_REQUIRE(ptrace(PT_LWPINFO, wpid, (caddr_t)&pl, sizeof(pl)) != -1);
	ATF_REQUIRE(pl.pl_flags & PL_FLAG_SCE);

	/* Continue the child process with a signal. */
	ATF_REQUIRE(ptrace(PT_CONTINUE, fpid, (caddr_t)1, SIGUSR1) == 0);

	/* The third wait() should report a system call exit for getpid(). */
	wpid = waitpid(fpid, &status, 0);
	ATF_REQUIRE(wpid == fpid);
	ATF_REQUIRE(WIFSTOPPED(status));
	ATF_REQUIRE(WSTOPSIG(status) == SIGTRAP);

	ATF_REQUIRE(ptrace(PT_LWPINFO, wpid, (caddr_t)&pl, sizeof(pl)) != -1);
	ATF_REQUIRE(pl.pl_flags & PL_FLAG_SCX);

	/* Continue the child process with a signal. */
	ATF_REQUIRE(ptrace(PT_CONTINUE, fpid, (caddr_t)1, SIGUSR1) == 0);

	for (;;) {
		/*
		 * The last wait() should report exit 2, i.e., a normal _exit
		 * from the signal handler. In the meantime, catch and proceed
		 * past any syscall stops.
		 */
		wpid = waitpid(fpid, &status, 0);
		ATF_REQUIRE(wpid == fpid);
		if (WIFSTOPPED(status) && WSTOPSIG(status) == SIGTRAP) {
			ATF_REQUIRE(ptrace(PT_LWPINFO, wpid, (caddr_t)&pl, sizeof(pl)) != -1);
			ATF_REQUIRE(pl.pl_flags & (PL_FLAG_SCE | PL_FLAG_SCX));
			ATF_REQUIRE(ptrace(PT_CONTINUE, fpid, (caddr_t)1, 0) == 0);
		} else {
			ATF_REQUIRE(WIFEXITED(status));
			ATF_REQUIRE(WEXITSTATUS(status) == 2);
			break;
		}
	}

	wpid = wait(&status);
	ATF_REQUIRE(wpid == -1);
	ATF_REQUIRE(errno == ECHILD);
}

/*
 * Verify that even if the signal queue is full for a child process,
 * a PT_CONTINUE with a signal will not result in loss of that signal.
 */
ATF_TC_WITHOUT_HEAD(ptrace__PT_CONTINUE_with_signal_full_sigqueue);
ATF_TC_BODY(ptrace__PT_CONTINUE_with_signal_full_sigqueue, tc)
{
	pid_t fpid, wpid;
	int status;
	int max_pending_per_proc;
	size_t len;
	int i;

	ATF_REQUIRE(signal(SIGUSR2, handler) != SIG_ERR);
	ATF_REQUIRE(signal(SIGUSR1, sigusr1_handler) != SIG_ERR);

	ATF_REQUIRE((fpid = fork()) != -1);
	if (fpid == 0) {
		trace_me();
		exit(1);
	}

	/* The first wait() should report the stop from SIGSTOP. */
	wpid = waitpid(fpid, &status, 0);
	ATF_REQUIRE(wpid == fpid);
	ATF_REQUIRE(WIFSTOPPED(status));
	ATF_REQUIRE(WSTOPSIG(status) == SIGSTOP);

	len = sizeof(max_pending_per_proc);
	ATF_REQUIRE(sysctlbyname("kern.sigqueue.max_pending_per_proc",
	    &max_pending_per_proc, &len, NULL, 0) == 0);

	/* Fill the signal queue. */
	for (i = 0; i < max_pending_per_proc; ++i)
		ATF_REQUIRE(kill(fpid, SIGUSR2) == 0);

	/* Continue with signal. */
	ATF_REQUIRE(ptrace(PT_CONTINUE, fpid, (caddr_t)1, SIGUSR1) == 0);

	for (;;) {
		wpid = waitpid(fpid, &status, 0);
		ATF_REQUIRE(wpid == fpid);
		if (WIFSTOPPED(status)) {
			ATF_REQUIRE(WSTOPSIG(status) == SIGUSR2);
			ATF_REQUIRE(ptrace(PT_CONTINUE, fpid, (caddr_t)1, 0) == 0);
		} else {
			/*
			 * The last wait() should report normal _exit from the
			 * SIGUSR1 handler.
			 */
			ATF_REQUIRE(WIFEXITED(status));
			ATF_REQUIRE(WEXITSTATUS(status) == 2);
			break;
		}
	}

	wpid = wait(&status);
	ATF_REQUIRE(wpid == -1);
	ATF_REQUIRE(errno == ECHILD);
}

static sem_t sigusr1_sem;
static int got_usr1;

static void
sigusr1_sempost_handler(int sig __unused)
{

	got_usr1++;
	CHILD_REQUIRE(sem_post(&sigusr1_sem) == 0);
}

/*
 * Verify that even if the signal queue is full for a child process,
 * and the signal is masked, a PT_CONTINUE with a signal will not
 * result in loss of that signal.
 */
ATF_TC_WITHOUT_HEAD(ptrace__PT_CONTINUE_with_signal_masked_full_sigqueue);
ATF_TC_BODY(ptrace__PT_CONTINUE_with_signal_masked_full_sigqueue, tc)
{
	struct ptrace_lwpinfo pl;
	pid_t fpid, wpid;
	int status, err;
	int max_pending_per_proc;
	size_t len;
	int i;
	sigset_t sigmask;

	ATF_REQUIRE(signal(SIGUSR2, handler) != SIG_ERR);
	ATF_REQUIRE(sem_init(&sigusr1_sem, 0, 0) == 0);
	ATF_REQUIRE(signal(SIGUSR1, sigusr1_sempost_handler) != SIG_ERR);

	got_usr1 = 0;
	ATF_REQUIRE((fpid = fork()) != -1);
	if (fpid == 0) {
		CHILD_REQUIRE(sigemptyset(&sigmask) == 0);
		CHILD_REQUIRE(sigaddset(&sigmask, SIGUSR1) == 0);
		CHILD_REQUIRE(sigprocmask(SIG_BLOCK, &sigmask, NULL) == 0);

		trace_me();
		CHILD_REQUIRE(got_usr1 == 0);

		/* Allow the pending SIGUSR1 in now. */
		CHILD_REQUIRE(sigprocmask(SIG_UNBLOCK, &sigmask, NULL) == 0);
		/* Wait to receive the SIGUSR1. */
		do {
			err = sem_wait(&sigusr1_sem);
			CHILD_REQUIRE(err == 0 || errno == EINTR);
		} while (err != 0 && errno == EINTR);
		CHILD_REQUIRE(got_usr1 == 1);
		exit(1);
	}

	/* The first wait() should report the stop from SIGSTOP. */
	wpid = waitpid(fpid, &status, 0);
	ATF_REQUIRE(wpid == fpid);
	ATF_REQUIRE(WIFSTOPPED(status));
	ATF_REQUIRE(WSTOPSIG(status) == SIGSTOP);

	len = sizeof(max_pending_per_proc);
	ATF_REQUIRE(sysctlbyname("kern.sigqueue.max_pending_per_proc",
	    &max_pending_per_proc, &len, NULL, 0) == 0);

	/* Fill the signal queue. */
	for (i = 0; i < max_pending_per_proc; ++i)
		ATF_REQUIRE(kill(fpid, SIGUSR2) == 0);

	/* Continue with signal. */
	ATF_REQUIRE(ptrace(PT_CONTINUE, fpid, (caddr_t)1, SIGUSR1) == 0);

	/* Collect and ignore all of the SIGUSR2. */
	for (i = 0; i < max_pending_per_proc; ++i) {
		wpid = waitpid(fpid, &status, 0);
		ATF_REQUIRE(wpid == fpid);
		ATF_REQUIRE(WIFSTOPPED(status));
		ATF_REQUIRE(WSTOPSIG(status) == SIGUSR2);
		ATF_REQUIRE(ptrace(PT_CONTINUE, fpid, (caddr_t)1, 0) == 0);
	}

	/* Now our PT_CONTINUE'd SIGUSR1 should cause a stop after unmask. */
	wpid = waitpid(fpid, &status, 0);
	ATF_REQUIRE(wpid == fpid);
	ATF_REQUIRE(WIFSTOPPED(status));
	ATF_REQUIRE(WSTOPSIG(status) == SIGUSR1);
	ATF_REQUIRE(ptrace(PT_LWPINFO, fpid, (caddr_t)&pl, sizeof(pl)) != -1);
	ATF_REQUIRE(pl.pl_siginfo.si_signo == SIGUSR1);

	/* Continue the child, ignoring the SIGUSR1. */
	ATF_REQUIRE(ptrace(PT_CONTINUE, fpid, (caddr_t)1, 0) == 0);

	/* The last wait() should report exit after receiving SIGUSR1. */
	wpid = waitpid(fpid, &status, 0);
	ATF_REQUIRE(wpid == fpid);
	ATF_REQUIRE(WIFEXITED(status));
	ATF_REQUIRE(WEXITSTATUS(status) == 1);

	wpid = wait(&status);
	ATF_REQUIRE(wpid == -1);
	ATF_REQUIRE(errno == ECHILD);
}

/*
 * Verify that, after stopping due to a signal, that signal can be
 * replaced with another signal.
 */
ATF_TC_WITHOUT_HEAD(ptrace__PT_CONTINUE_change_sig);
ATF_TC_BODY(ptrace__PT_CONTINUE_change_sig, tc)
{
	struct ptrace_lwpinfo pl;
	pid_t fpid, wpid;
	int status;

	ATF_REQUIRE((fpid = fork()) != -1);
	if (fpid == 0) {
		trace_me();
		sleep(20);
		exit(1);
	}

	/* The first wait() should report the stop from SIGSTOP. */
	wpid = waitpid(fpid, &status, 0);
	ATF_REQUIRE(wpid == fpid);
	ATF_REQUIRE(WIFSTOPPED(status));
	ATF_REQUIRE(WSTOPSIG(status) == SIGSTOP);

	ATF_REQUIRE(ptrace(PT_CONTINUE, fpid, (caddr_t)1, 0) == 0);

	/* Send a signal without ptrace. */
	ATF_REQUIRE(kill(fpid, SIGINT) == 0);

	/* The second wait() should report a SIGINT was received. */
	wpid = waitpid(fpid, &status, 0);
	ATF_REQUIRE(wpid == fpid);
	ATF_REQUIRE(WIFSTOPPED(status));
	ATF_REQUIRE(WSTOPSIG(status) == SIGINT);

	ATF_REQUIRE(ptrace(PT_LWPINFO, wpid, (caddr_t)&pl, sizeof(pl)) != -1);
	ATF_REQUIRE(pl.pl_flags & PL_FLAG_SI);
	ATF_REQUIRE(pl.pl_siginfo.si_signo == SIGINT);

	/* Continue the child process with a different signal. */
	ATF_REQUIRE(ptrace(PT_CONTINUE, fpid, (caddr_t)1, SIGTERM) == 0);

	/*
	 * The last wait() should report having died due to the new
	 * signal, SIGTERM.
	 */
	wpid = waitpid(fpid, &status, 0);
	ATF_REQUIRE(wpid == fpid);
	ATF_REQUIRE(WIFSIGNALED(status));
	ATF_REQUIRE(WTERMSIG(status) == SIGTERM);

	wpid = wait(&status);
	ATF_REQUIRE(wpid == -1);
	ATF_REQUIRE(errno == ECHILD);
}

/*
 * Verify that a signal can be passed through to the child even when there
 * was no true signal originally. Such cases arise when a SIGTRAP is
 * invented for e.g, system call stops.
 */
ATF_TC_WITHOUT_HEAD(ptrace__PT_CONTINUE_with_sigtrap_system_call_entry);
ATF_TC_BODY(ptrace__PT_CONTINUE_with_sigtrap_system_call_entry, tc)
{
	struct ptrace_lwpinfo pl;
	struct rlimit rl;
	pid_t fpid, wpid;
	int status;

	ATF_REQUIRE((fpid = fork()) != -1);
	if (fpid == 0) {
		trace_me();
		/* SIGTRAP expected to cause exit on syscall entry. */
		rl.rlim_cur = rl.rlim_max = 0;
		ATF_REQUIRE(setrlimit(RLIMIT_CORE, &rl) == 0);
		getpid();
		exit(1);
	}

	/* The first wait() should report the stop from SIGSTOP. */
	wpid = waitpid(fpid, &status, 0);
	ATF_REQUIRE(wpid == fpid);
	ATF_REQUIRE(WIFSTOPPED(status));
	ATF_REQUIRE(WSTOPSIG(status) == SIGSTOP);

	/* Continue the child ignoring the SIGSTOP and tracing system calls. */
	ATF_REQUIRE(ptrace(PT_SYSCALL, fpid, (caddr_t)1, 0) == 0);

	/* The second wait() should report a system call entry for getpid(). */
	wpid = waitpid(fpid, &status, 0);
	ATF_REQUIRE(wpid == fpid);
	ATF_REQUIRE(WIFSTOPPED(status));
	ATF_REQUIRE(WSTOPSIG(status) == SIGTRAP);

	ATF_REQUIRE(ptrace(PT_LWPINFO, wpid, (caddr_t)&pl, sizeof(pl)) != -1);
	ATF_REQUIRE(pl.pl_flags & PL_FLAG_SCE);

	/* Continue the child process with a SIGTRAP. */
	ATF_REQUIRE(ptrace(PT_CONTINUE, fpid, (caddr_t)1, SIGTRAP) == 0);

	for (;;) {
		/*
		 * The last wait() should report exit due to SIGTRAP.  In the
		 * meantime, catch and proceed past any syscall stops.
		 */
		wpid = waitpid(fpid, &status, 0);
		ATF_REQUIRE(wpid == fpid);
		if (WIFSTOPPED(status) && WSTOPSIG(status) == SIGTRAP) {
			ATF_REQUIRE(ptrace(PT_LWPINFO, wpid, (caddr_t)&pl, sizeof(pl)) != -1);
			ATF_REQUIRE(pl.pl_flags & (PL_FLAG_SCE | PL_FLAG_SCX));
			ATF_REQUIRE(ptrace(PT_CONTINUE, fpid, (caddr_t)1, 0) == 0);
		} else {
			ATF_REQUIRE(WIFSIGNALED(status));
			ATF_REQUIRE(WTERMSIG(status) == SIGTRAP);
			break;
		}
	}

	wpid = wait(&status);
	ATF_REQUIRE(wpid == -1);
	ATF_REQUIRE(errno == ECHILD);

}

/*
 * A mixed bag PT_CONTINUE with signal test.
 */
ATF_TC_WITHOUT_HEAD(ptrace__PT_CONTINUE_with_signal_mix);
ATF_TC_BODY(ptrace__PT_CONTINUE_with_signal_mix, tc)
{
	struct ptrace_lwpinfo pl;
	pid_t fpid, wpid;
	int status;

	ATF_REQUIRE(signal(SIGUSR1, sigusr1_counting_handler) != SIG_ERR);

	ATF_REQUIRE((fpid = fork()) != -1);
	if (fpid == 0) {
		trace_me();
		getpid();
		exit(1);
	}

	/* The first wait() should report the stop from SIGSTOP. */
	wpid = waitpid(fpid, &status, 0);
	ATF_REQUIRE(wpid == fpid);
	ATF_REQUIRE(WIFSTOPPED(status));
	ATF_REQUIRE(WSTOPSIG(status) == SIGSTOP);

	/* Continue the child ignoring the SIGSTOP and tracing system calls. */
	ATF_REQUIRE(ptrace(PT_SYSCALL, fpid, (caddr_t)1, 0) == 0);

	/* The second wait() should report a system call entry for getpid(). */
	wpid = waitpid(fpid, &status, 0);
	ATF_REQUIRE(wpid == fpid);
	ATF_REQUIRE(WIFSTOPPED(status));
	ATF_REQUIRE(WSTOPSIG(status) == SIGTRAP);

	ATF_REQUIRE(ptrace(PT_LWPINFO, wpid, (caddr_t)&pl, sizeof(pl)) != -1);
	ATF_REQUIRE(pl.pl_flags & PL_FLAG_SCE);

	/* Continue with the first SIGUSR1. */
	ATF_REQUIRE(ptrace(PT_CONTINUE, fpid, (caddr_t)1, SIGUSR1) == 0);

	/* The next wait() should report a system call exit for getpid(). */
	wpid = waitpid(fpid, &status, 0);
	ATF_REQUIRE(wpid == fpid);
	ATF_REQUIRE(WIFSTOPPED(status));
	ATF_REQUIRE(WSTOPSIG(status) == SIGTRAP);

	ATF_REQUIRE(ptrace(PT_LWPINFO, wpid, (caddr_t)&pl, sizeof(pl)) != -1);
	ATF_REQUIRE(pl.pl_flags & PL_FLAG_SCX);

	/* Send an ABRT without ptrace. */
	ATF_REQUIRE(kill(fpid, SIGABRT) == 0);

	/* Continue normally. */
	ATF_REQUIRE(ptrace(PT_CONTINUE, fpid, (caddr_t)1, 0) == 0);

	/* The next wait() should report the SIGABRT. */
	wpid = waitpid(fpid, &status, 0);
	ATF_REQUIRE(wpid == fpid);
	ATF_REQUIRE(WIFSTOPPED(status));
	ATF_REQUIRE(WSTOPSIG(status) == SIGABRT);

	ATF_REQUIRE(ptrace(PT_LWPINFO, wpid, (caddr_t)&pl, sizeof(pl)) != -1);
	ATF_REQUIRE(pl.pl_flags & PL_FLAG_SI);
	ATF_REQUIRE(pl.pl_siginfo.si_signo == SIGABRT);

	/* Continue, replacing the SIGABRT with another SIGUSR1. */
	ATF_REQUIRE(ptrace(PT_CONTINUE, fpid, (caddr_t)1, SIGUSR1) == 0);

	for (;;) {
		/*
		 * The last wait() should report exit 2, i.e., a normal _exit
		 * from the signal handler. In the meantime, catch and proceed
		 * past any syscall stops.
		 */
		wpid = waitpid(fpid, &status, 0);
		ATF_REQUIRE(wpid == fpid);
		if (WIFSTOPPED(status) && WSTOPSIG(status) == SIGTRAP) {
			ATF_REQUIRE(ptrace(PT_LWPINFO, wpid, (caddr_t)&pl, sizeof(pl)) != -1);
			ATF_REQUIRE(pl.pl_flags & (PL_FLAG_SCE | PL_FLAG_SCX));
			ATF_REQUIRE(ptrace(PT_CONTINUE, fpid, (caddr_t)1, 0) == 0);
		} else {
			ATF_REQUIRE(WIFEXITED(status));
			ATF_REQUIRE(WEXITSTATUS(status) == 2);
			break;
		}
	}

	wpid = wait(&status);
	ATF_REQUIRE(wpid == -1);
	ATF_REQUIRE(errno == ECHILD);

}

/*
 * Verify a signal delivered by ptrace is noticed by kevent(2).
 */
ATF_TC_WITHOUT_HEAD(ptrace__PT_CONTINUE_with_signal_kqueue);
ATF_TC_BODY(ptrace__PT_CONTINUE_with_signal_kqueue, tc)
{
	pid_t fpid, wpid;
	int status, kq, nevents;
	struct kevent kev;

	ATF_REQUIRE(signal(SIGUSR1, SIG_IGN) != SIG_ERR);

	ATF_REQUIRE((fpid = fork()) != -1);
	if (fpid == 0) {
		CHILD_REQUIRE((kq = kqueue()) > 0);
		EV_SET(&kev, SIGUSR1, EVFILT_SIGNAL, EV_ADD, 0, 0, 0);
		CHILD_REQUIRE(kevent(kq, &kev, 1, NULL, 0, NULL) == 0);

		trace_me();

		for (;;) {
			nevents = kevent(kq, NULL, 0, &kev, 1, NULL);
			if (nevents == -1 && errno == EINTR)
				continue;
			CHILD_REQUIRE(nevents > 0);
			CHILD_REQUIRE(kev.filter == EVFILT_SIGNAL);
			CHILD_REQUIRE(kev.ident == SIGUSR1);
			break;
		}

		exit(1);
	}

	/* The first wait() should report the stop from SIGSTOP. */
	wpid = waitpid(fpid, &status, 0);
	ATF_REQUIRE(wpid == fpid);
	ATF_REQUIRE(WIFSTOPPED(status));
	ATF_REQUIRE(WSTOPSIG(status) == SIGSTOP);

	/* Continue with the SIGUSR1. */
	ATF_REQUIRE(ptrace(PT_CONTINUE, fpid, (caddr_t)1, SIGUSR1) == 0);

	/*
	 * The last wait() should report normal exit with code 1.
	 */
	wpid = waitpid(fpid, &status, 0);
	ATF_REQUIRE(wpid == fpid);
	ATF_REQUIRE(WIFEXITED(status));
	ATF_REQUIRE(WEXITSTATUS(status) == 1);

	wpid = wait(&status);
	ATF_REQUIRE(wpid == -1);
	ATF_REQUIRE(errno == ECHILD);
}

static void *
signal_thread(void *arg)
{
	int err;
	sigset_t sigmask;

	pthread_barrier_t *pbarrier = (pthread_barrier_t*)arg;

	/* Wait for this thread to receive a SIGUSR1. */
	do {
		err = sem_wait(&sigusr1_sem);
		CHILD_REQUIRE(err == 0 || errno == EINTR);
	} while (err != 0 && errno == EINTR);

	/* Free our companion thread from the barrier. */
	pthread_barrier_wait(pbarrier);

	/*
	 * Swap ignore duties; the next SIGUSR1 should go to the
	 * other thread.
	 */
	CHILD_REQUIRE(sigemptyset(&sigmask) == 0);
	CHILD_REQUIRE(sigaddset(&sigmask, SIGUSR1) == 0);
	CHILD_REQUIRE(pthread_sigmask(SIG_BLOCK, &sigmask, NULL) == 0);

	/* Sync up threads after swapping signal masks. */
	pthread_barrier_wait(pbarrier);

	/* Wait until our companion has received its SIGUSR1. */
	pthread_barrier_wait(pbarrier);

	return (NULL);
}

/*
 * Verify that a traced process with blocked signal received the
 * signal from kill() once unmasked.
 */
ATF_TC_WITHOUT_HEAD(ptrace__killed_with_sigmask);
ATF_TC_BODY(ptrace__killed_with_sigmask, tc)
{
	struct ptrace_lwpinfo pl;
	pid_t fpid, wpid;
	int status, err;
	sigset_t sigmask;

	ATF_REQUIRE(sem_init(&sigusr1_sem, 0, 0) == 0);
	ATF_REQUIRE(signal(SIGUSR1, sigusr1_sempost_handler) != SIG_ERR);
	got_usr1 = 0;

	ATF_REQUIRE((fpid = fork()) != -1);
	if (fpid == 0) {
		CHILD_REQUIRE(sigemptyset(&sigmask) == 0);
		CHILD_REQUIRE(sigaddset(&sigmask, SIGUSR1) == 0);
		CHILD_REQUIRE(sigprocmask(SIG_BLOCK, &sigmask, NULL) == 0);

		trace_me();
		CHILD_REQUIRE(got_usr1 == 0);

		/* Allow the pending SIGUSR1 in now. */
		CHILD_REQUIRE(sigprocmask(SIG_UNBLOCK, &sigmask, NULL) == 0);
		/* Wait to receive a SIGUSR1. */
		do {
			err = sem_wait(&sigusr1_sem);
			CHILD_REQUIRE(err == 0 || errno == EINTR);
		} while (err != 0 && errno == EINTR);
		CHILD_REQUIRE(got_usr1 == 1);
		exit(1);
	}

	/* The first wait() should report the stop from SIGSTOP. */
	wpid = waitpid(fpid, &status, 0);
	ATF_REQUIRE(wpid == fpid);
	ATF_REQUIRE(WIFSTOPPED(status));
	ATF_REQUIRE(WSTOPSIG(status) == SIGSTOP);
	ATF_REQUIRE(ptrace(PT_LWPINFO, fpid, (caddr_t)&pl, sizeof(pl)) != -1);
	ATF_REQUIRE(pl.pl_siginfo.si_signo == SIGSTOP);

	/* Send blocked SIGUSR1 which should cause a stop. */
	ATF_REQUIRE(kill(fpid, SIGUSR1) == 0);

	/* Continue the child ignoring the SIGSTOP. */
	ATF_REQUIRE(ptrace(PT_CONTINUE, fpid, (caddr_t)1, 0) == 0);

	/* The next wait() should report the kill(SIGUSR1) was received. */
	wpid = waitpid(fpid, &status, 0);
	ATF_REQUIRE(wpid == fpid);
	ATF_REQUIRE(WIFSTOPPED(status));
	ATF_REQUIRE(WSTOPSIG(status) == SIGUSR1);
	ATF_REQUIRE(ptrace(PT_LWPINFO, fpid, (caddr_t)&pl, sizeof(pl)) != -1);
	ATF_REQUIRE(pl.pl_siginfo.si_signo == SIGUSR1);

	/* Continue the child, allowing in the SIGUSR1. */
	ATF_REQUIRE(ptrace(PT_CONTINUE, fpid, (caddr_t)1, SIGUSR1) == 0);

	/* The last wait() should report normal exit with code 1. */
	wpid = waitpid(fpid, &status, 0);
	ATF_REQUIRE(wpid == fpid);
	ATF_REQUIRE(WIFEXITED(status));
	ATF_REQUIRE(WEXITSTATUS(status) == 1);

	wpid = wait(&status);
	ATF_REQUIRE(wpid == -1);
	ATF_REQUIRE(errno == ECHILD);
}

/*
 * Verify that a traced process with blocked signal received the
 * signal from PT_CONTINUE once unmasked.
 */
ATF_TC_WITHOUT_HEAD(ptrace__PT_CONTINUE_with_sigmask);
ATF_TC_BODY(ptrace__PT_CONTINUE_with_sigmask, tc)
{
	struct ptrace_lwpinfo pl;
	pid_t fpid, wpid;
	int status, err;
	sigset_t sigmask;

	ATF_REQUIRE(sem_init(&sigusr1_sem, 0, 0) == 0);
	ATF_REQUIRE(signal(SIGUSR1, sigusr1_sempost_handler) != SIG_ERR);
	got_usr1 = 0;

	ATF_REQUIRE((fpid = fork()) != -1);
	if (fpid == 0) {
		CHILD_REQUIRE(sigemptyset(&sigmask) == 0);
		CHILD_REQUIRE(sigaddset(&sigmask, SIGUSR1) == 0);
		CHILD_REQUIRE(sigprocmask(SIG_BLOCK, &sigmask, NULL) == 0);

		trace_me();
		CHILD_REQUIRE(got_usr1 == 0);

		/* Allow the pending SIGUSR1 in now. */
		CHILD_REQUIRE(sigprocmask(SIG_UNBLOCK, &sigmask, NULL) == 0);
		/* Wait to receive a SIGUSR1. */
		do {
			err = sem_wait(&sigusr1_sem);
			CHILD_REQUIRE(err == 0 || errno == EINTR);
		} while (err != 0 && errno == EINTR);

		CHILD_REQUIRE(got_usr1 == 1);
		exit(1);
	}

	/* The first wait() should report the stop from SIGSTOP. */
	wpid = waitpid(fpid, &status, 0);
	ATF_REQUIRE(wpid == fpid);
	ATF_REQUIRE(WIFSTOPPED(status));
	ATF_REQUIRE(WSTOPSIG(status) == SIGSTOP);
	ATF_REQUIRE(ptrace(PT_LWPINFO, fpid, (caddr_t)&pl, sizeof(pl)) != -1);
	ATF_REQUIRE(pl.pl_siginfo.si_signo == SIGSTOP);

	/* Continue the child replacing SIGSTOP with SIGUSR1. */
	ATF_REQUIRE(ptrace(PT_CONTINUE, fpid, (caddr_t)1, SIGUSR1) == 0);

	/* The next wait() should report the SIGUSR1 was received. */
	wpid = waitpid(fpid, &status, 0);
	ATF_REQUIRE(wpid == fpid);
	ATF_REQUIRE(WIFSTOPPED(status));
	ATF_REQUIRE(WSTOPSIG(status) == SIGUSR1);
	ATF_REQUIRE(ptrace(PT_LWPINFO, fpid, (caddr_t)&pl, sizeof(pl)) != -1);
	ATF_REQUIRE(pl.pl_siginfo.si_signo == SIGUSR1);

	/* Continue the child, ignoring the SIGUSR1. */
	ATF_REQUIRE(ptrace(PT_CONTINUE, fpid, (caddr_t)1, 0) == 0);

	/* The last wait() should report normal exit with code 1. */
	wpid = waitpid(fpid, &status, 0);
	ATF_REQUIRE(wpid == fpid);
	ATF_REQUIRE(WIFEXITED(status));
	ATF_REQUIRE(WEXITSTATUS(status) == 1);

	wpid = wait(&status);
	ATF_REQUIRE(wpid == -1);
	ATF_REQUIRE(errno == ECHILD);
}

/*
 * Verify that if ptrace stops due to a signal but continues with
 * a different signal that the new signal is routed to a thread
 * that can accept it, and that the thread is awakened by the signal
 * in a timely manner.
 */
ATF_TC_WITHOUT_HEAD(ptrace__PT_CONTINUE_with_signal_thread_sigmask);
ATF_TC_BODY(ptrace__PT_CONTINUE_with_signal_thread_sigmask, tc)
{
	pid_t fpid, wpid;
	int status, err;
	pthread_t t;
	sigset_t sigmask;
	pthread_barrier_t barrier;

	ATF_REQUIRE(pthread_barrier_init(&barrier, NULL, 2) == 0);
	ATF_REQUIRE(sem_init(&sigusr1_sem, 0, 0) == 0);
	ATF_REQUIRE(signal(SIGUSR1, sigusr1_sempost_handler) != SIG_ERR);

	ATF_REQUIRE((fpid = fork()) != -1);
	if (fpid == 0) {
		CHILD_REQUIRE(pthread_create(&t, NULL, signal_thread, (void*)&barrier) == 0);

		/* The other thread should receive the first SIGUSR1. */
		CHILD_REQUIRE(sigemptyset(&sigmask) == 0);
		CHILD_REQUIRE(sigaddset(&sigmask, SIGUSR1) == 0);
		CHILD_REQUIRE(pthread_sigmask(SIG_BLOCK, &sigmask, NULL) == 0);

		trace_me();

		/* Wait until other thread has received its SIGUSR1. */
		pthread_barrier_wait(&barrier);

		/*
		 * Swap ignore duties; the next SIGUSR1 should go to this
		 * thread.
		 */
		CHILD_REQUIRE(pthread_sigmask(SIG_UNBLOCK, &sigmask, NULL) == 0);

		/* Sync up threads after swapping signal masks. */
		pthread_barrier_wait(&barrier);

		/*
		 * Sync up with test code; we're ready for the next SIGUSR1
		 * now.
		 */
		raise(SIGSTOP);

		/* Wait for this thread to receive a SIGUSR1. */
		do {
			err = sem_wait(&sigusr1_sem);
			CHILD_REQUIRE(err == 0 || errno == EINTR);
		} while (err != 0 && errno == EINTR);

		/* Free the other thread from the barrier. */
		pthread_barrier_wait(&barrier);

		CHILD_REQUIRE(pthread_join(t, NULL) == 0);

		exit(1);
	}

	/* The first wait() should report the stop from SIGSTOP. */
	wpid = waitpid(fpid, &status, 0);
	ATF_REQUIRE(wpid == fpid);
	ATF_REQUIRE(WIFSTOPPED(status));
	ATF_REQUIRE(WSTOPSIG(status) == SIGSTOP);

	/* Continue the child ignoring the SIGSTOP. */
	ATF_REQUIRE(ptrace(PT_CONTINUE, fpid, (caddr_t)1, 0) == 0);

	/*
	 * Send a signal without ptrace that either thread will accept (USR2,
	 * in this case).
	 */
	ATF_REQUIRE(kill(fpid, SIGUSR2) == 0);
	
	/* The second wait() should report a SIGUSR2 was received. */
	wpid = waitpid(fpid, &status, 0);
	ATF_REQUIRE(wpid == fpid);
	ATF_REQUIRE(WIFSTOPPED(status));
	ATF_REQUIRE(WSTOPSIG(status) == SIGUSR2);

	/* Continue the child, changing the signal to USR1. */
	ATF_REQUIRE(ptrace(PT_CONTINUE, fpid, (caddr_t)1, SIGUSR1) == 0);

	/* The next wait() should report the stop from SIGSTOP. */
	wpid = waitpid(fpid, &status, 0);
	ATF_REQUIRE(wpid == fpid);
	ATF_REQUIRE(WIFSTOPPED(status));
	ATF_REQUIRE(WSTOPSIG(status) == SIGSTOP);

	/* Continue the child ignoring the SIGSTOP. */
	ATF_REQUIRE(ptrace(PT_CONTINUE, fpid, (caddr_t)1, 0) == 0);

	ATF_REQUIRE(kill(fpid, SIGUSR2) == 0);

	/* The next wait() should report a SIGUSR2 was received. */
	wpid = waitpid(fpid, &status, 0);
	ATF_REQUIRE(wpid == fpid);
	ATF_REQUIRE(WIFSTOPPED(status));
	ATF_REQUIRE(WSTOPSIG(status) == SIGUSR2);

	/* Continue the child, changing the signal to USR1. */
	ATF_REQUIRE(ptrace(PT_CONTINUE, fpid, (caddr_t)1, SIGUSR1) == 0);

	/* The last wait() should report normal exit with code 1. */
	wpid = waitpid(fpid, &status, 0);
	ATF_REQUIRE(wpid == fpid);
	ATF_REQUIRE(WIFEXITED(status));
	ATF_REQUIRE(WEXITSTATUS(status) == 1);

	wpid = wait(&status);
	ATF_REQUIRE(wpid == -1);
	ATF_REQUIRE(errno == ECHILD);
}

static void *
raise_sigstop_thread(void *arg __unused)
{

	raise(SIGSTOP);
	return NULL;
}

static void *
sleep_thread(void *arg __unused)
{

	sleep(60);
	return NULL;
}

static void
terminate_with_pending_sigstop(bool sigstop_from_main_thread)
{
	pid_t fpid, wpid;
	int status, i;
	cpuset_t setmask;
	cpusetid_t setid;
	pthread_t t;

	/*
	 * Become the reaper for this process tree. We need to be able to check
	 * that both child and grandchild have died.
	 */
	ATF_REQUIRE(procctl(P_PID, getpid(), PROC_REAP_ACQUIRE, NULL) == 0);

	fpid = fork();
	ATF_REQUIRE(fpid >= 0);
	if (fpid == 0) {
		fpid = fork();
		CHILD_REQUIRE(fpid >= 0);
		if (fpid == 0) {
			trace_me();

			/* Pin to CPU 0 to serialize thread execution. */
			CPU_ZERO(&setmask);
			CPU_SET(0, &setmask);
			CHILD_REQUIRE(cpuset(&setid) == 0);
			CHILD_REQUIRE(cpuset_setaffinity(CPU_LEVEL_CPUSET,
			    CPU_WHICH_CPUSET, setid,
			    sizeof(setmask), &setmask) == 0);

			if (sigstop_from_main_thread) {
				/*
				 * We expect the SIGKILL sent when our parent
				 * dies to be delivered to the new thread.
				 * Raise the SIGSTOP in this thread so the
				 * threads compete.
				 */
				CHILD_REQUIRE(pthread_create(&t, NULL,
				    sleep_thread, NULL) == 0);
				raise(SIGSTOP);
			} else {
				/*
				 * We expect the SIGKILL to be delivered to
				 * this thread. After creating the new thread,
				 * just get off the CPU so the other thread can
				 * raise the SIGSTOP.
				 */
				CHILD_REQUIRE(pthread_create(&t, NULL,
				    raise_sigstop_thread, NULL) == 0);
				sleep(60);
			}

			exit(0);
		}
		/* First stop is trace_me() immediately after fork. */
		wpid = waitpid(fpid, &status, 0);
		CHILD_REQUIRE(wpid == fpid);
		CHILD_REQUIRE(WIFSTOPPED(status));
		CHILD_REQUIRE(WSTOPSIG(status) == SIGSTOP);

		CHILD_REQUIRE(ptrace(PT_CONTINUE, fpid, (caddr_t)1, 0) == 0);

		/* Second stop is from the raise(SIGSTOP). */
		wpid = waitpid(fpid, &status, 0);
		CHILD_REQUIRE(wpid == fpid);
		CHILD_REQUIRE(WIFSTOPPED(status));
		CHILD_REQUIRE(WSTOPSIG(status) == SIGSTOP);

		/*
		 * Terminate tracing process without detaching. Our child
		 * should be killed.
		 */
		exit(0);
	}

	/*
	 * We should get a normal exit from our immediate child and a SIGKILL
	 * exit from our grandchild. The latter case is the interesting one.
	 * Our grandchild should not have stopped due to the SIGSTOP that was
	 * left dangling when its parent died.
	 */
	for (i = 0; i < 2; ++i) {
		wpid = wait(&status);
		if (wpid == fpid)
			ATF_REQUIRE(WIFEXITED(status) &&
			    WEXITSTATUS(status) == 0);
		else
			ATF_REQUIRE(WIFSIGNALED(status) &&
			    WTERMSIG(status) == SIGKILL);
	}
}

/*
 * These two tests ensure that if the tracing process exits without detaching
 * just after the child received a SIGSTOP, the child is cleanly killed and
 * doesn't go to sleep due to the SIGSTOP. The parent's death will send a
 * SIGKILL to the child. If the SIGKILL and the SIGSTOP are handled by
 * different threads, the SIGKILL must win.  There are two variants of this
 * test, designed to catch the case where the SIGKILL is delivered to the
 * younger thread (the first test) and the case where the SIGKILL is delivered
 * to the older thread (the second test). This behavior has changed in the
 * past, so make no assumption.
 */
ATF_TC(ptrace__parent_terminate_with_pending_sigstop1);
ATF_TC_HEAD(ptrace__parent_terminate_with_pending_sigstop1, tc)
{

	atf_tc_set_md_var(tc, "require.user", "root");
}
ATF_TC_BODY(ptrace__parent_terminate_with_pending_sigstop1, tc)
{

	terminate_with_pending_sigstop(true);
}

ATF_TC(ptrace__parent_terminate_with_pending_sigstop2);
ATF_TC_HEAD(ptrace__parent_terminate_with_pending_sigstop2, tc)
{

	atf_tc_set_md_var(tc, "require.user", "root");
}
ATF_TC_BODY(ptrace__parent_terminate_with_pending_sigstop2, tc)
{

	terminate_with_pending_sigstop(false);
}

/*
 * Verify that after ptrace() discards a SIGKILL signal, the event mask
 * is not modified.
 */
ATF_TC_WITHOUT_HEAD(ptrace__event_mask_sigkill_discard);
ATF_TC_BODY(ptrace__event_mask_sigkill_discard, tc)
{
	struct ptrace_lwpinfo pl;
	pid_t fpid, wpid;
	int status, event_mask, new_event_mask;

	ATF_REQUIRE((fpid = fork()) != -1);
	if (fpid == 0) {
		trace_me();
		raise(SIGSTOP);
		exit(0);
	}

	/* The first wait() should report the stop from trace_me(). */
	wpid = waitpid(fpid, &status, 0);
	ATF_REQUIRE(wpid == fpid);
	ATF_REQUIRE(WIFSTOPPED(status));
	ATF_REQUIRE(WSTOPSIG(status) == SIGSTOP);

	/* Set several unobtrusive event bits. */
	event_mask = PTRACE_EXEC | PTRACE_FORK | PTRACE_LWP;
	ATF_REQUIRE(ptrace(PT_SET_EVENT_MASK, wpid, (caddr_t)&event_mask,
	    sizeof(event_mask)) == 0);

	/* Send a SIGKILL without using ptrace. */
	ATF_REQUIRE(kill(fpid, SIGKILL) == 0);

	/* Continue the child ignoring the SIGSTOP. */
	ATF_REQUIRE(ptrace(PT_CONTINUE, fpid, (caddr_t)1, 0) == 0);

	/* The next stop should be due to the SIGKILL. */
	wpid = waitpid(fpid, &status, 0);
	ATF_REQUIRE(wpid == fpid);
	ATF_REQUIRE(WIFSTOPPED(status));
	ATF_REQUIRE(WSTOPSIG(status) == SIGKILL);

	ATF_REQUIRE(ptrace(PT_LWPINFO, wpid, (caddr_t)&pl, sizeof(pl)) != -1);
	ATF_REQUIRE(pl.pl_flags & PL_FLAG_SI);
	ATF_REQUIRE(pl.pl_siginfo.si_signo == SIGKILL);

	/* Continue the child ignoring the SIGKILL. */
	ATF_REQUIRE(ptrace(PT_CONTINUE, fpid, (caddr_t)1, 0) == 0);

	/* The next wait() should report the stop from SIGSTOP. */
	wpid = waitpid(fpid, &status, 0);
	ATF_REQUIRE(wpid == fpid);
	ATF_REQUIRE(WIFSTOPPED(status));
	ATF_REQUIRE(WSTOPSIG(status) == SIGSTOP);

	/* Check the current event mask. It should not have changed. */
	new_event_mask = 0;
	ATF_REQUIRE(ptrace(PT_GET_EVENT_MASK, wpid, (caddr_t)&new_event_mask,
	    sizeof(new_event_mask)) == 0);
	ATF_REQUIRE(event_mask == new_event_mask);

	/* Continue the child to let it exit. */
	ATF_REQUIRE(ptrace(PT_CONTINUE, fpid, (caddr_t)1, 0) == 0);

	/* The last event should be for the child process's exit. */
	wpid = waitpid(fpid, &status, 0);
	ATF_REQUIRE(WIFEXITED(status));
	ATF_REQUIRE(WEXITSTATUS(status) == 0);

	wpid = wait(&status);
	ATF_REQUIRE(wpid == -1);
	ATF_REQUIRE(errno == ECHILD);
}

static void *
flock_thread(void *arg)
{
	int fd;

	fd = *(int *)arg;
	(void)flock(fd, LOCK_EX);
	(void)flock(fd, LOCK_UN);
	return (NULL);
}

/*
 * Verify that PT_ATTACH will suspend threads sleeping in an SBDRY section.
 * We rely on the fact that the lockf implementation sets SBDRY before blocking
 * on a lock. This is a regression test for r318191.
 */
ATF_TC_WITHOUT_HEAD(ptrace__PT_ATTACH_with_SBDRY_thread);
ATF_TC_BODY(ptrace__PT_ATTACH_with_SBDRY_thread, tc)
{
	pthread_barrier_t barrier;
	pthread_barrierattr_t battr;
	char tmpfile[64];
	pid_t child, wpid;
	int error, fd, i, status;

	ATF_REQUIRE(pthread_barrierattr_init(&battr) == 0);
	ATF_REQUIRE(pthread_barrierattr_setpshared(&battr,
	    PTHREAD_PROCESS_SHARED) == 0);
	ATF_REQUIRE(pthread_barrier_init(&barrier, &battr, 2) == 0);

	(void)snprintf(tmpfile, sizeof(tmpfile), "./ptrace.XXXXXX");
	fd = mkstemp(tmpfile);
	ATF_REQUIRE(fd >= 0);

	ATF_REQUIRE((child = fork()) != -1);
	if (child == 0) {
		pthread_t t[2];
		int cfd;

		error = pthread_barrier_wait(&barrier);
		if (error != 0 && error != PTHREAD_BARRIER_SERIAL_THREAD)
			_exit(1);

		cfd = open(tmpfile, O_RDONLY);
		if (cfd < 0)
			_exit(1);

		/*
		 * We want at least two threads blocked on the file lock since
		 * the SIGSTOP from PT_ATTACH may kick one of them out of
		 * sleep.
		 */
		if (pthread_create(&t[0], NULL, flock_thread, &cfd) != 0)
			_exit(1);
		if (pthread_create(&t[1], NULL, flock_thread, &cfd) != 0)
			_exit(1);
		if (pthread_join(t[0], NULL) != 0)
			_exit(1);
		if (pthread_join(t[1], NULL) != 0)
			_exit(1);
		_exit(0);
	}

	ATF_REQUIRE(flock(fd, LOCK_EX) == 0);

	error = pthread_barrier_wait(&barrier);
	ATF_REQUIRE(error == 0 || error == PTHREAD_BARRIER_SERIAL_THREAD);

	/*
	 * Give the child some time to block. Is there a better way to do this?
	 */
	sleep(1);

	/*
	 * Attach and give the child 3 seconds to stop.
	 */
	ATF_REQUIRE(ptrace(PT_ATTACH, child, NULL, 0) == 0);
	for (i = 0; i < 3; i++) {
		wpid = waitpid(child, &status, WNOHANG);
		if (wpid == child && WIFSTOPPED(status) &&
		    WSTOPSIG(status) == SIGSTOP)
			break;
		sleep(1);
	}
	ATF_REQUIRE_MSG(i < 3, "failed to stop child process after PT_ATTACH");

	ATF_REQUIRE(ptrace(PT_DETACH, child, NULL, 0) == 0);

	ATF_REQUIRE(flock(fd, LOCK_UN) == 0);
	ATF_REQUIRE(unlink(tmpfile) == 0);
	ATF_REQUIRE(close(fd) == 0);
}

static void
sigusr1_step_handler(int sig)
{

	CHILD_REQUIRE(sig == SIGUSR1);
	raise(SIGABRT);
}

/*
 * Verify that PT_STEP with a signal invokes the signal before
 * stepping the next instruction (and that the next instruction is
 * stepped correctly).
 */
ATF_TC_WITHOUT_HEAD(ptrace__PT_STEP_with_signal);
ATF_TC_BODY(ptrace__PT_STEP_with_signal, tc)
{
	struct ptrace_lwpinfo pl;
	pid_t fpid, wpid;
	int status;

	ATF_REQUIRE((fpid = fork()) != -1);
	if (fpid == 0) {
		trace_me();
		signal(SIGUSR1, sigusr1_step_handler);
		raise(SIGABRT);
		exit(1);
	}

	/* The first wait() should report the stop from SIGSTOP. */
	wpid = waitpid(fpid, &status, 0);
	ATF_REQUIRE(wpid == fpid);
	ATF_REQUIRE(WIFSTOPPED(status));
	ATF_REQUIRE(WSTOPSIG(status) == SIGSTOP);

	ATF_REQUIRE(ptrace(PT_CONTINUE, fpid, (caddr_t)1, 0) == 0);

	/* The next stop should report the SIGABRT in the child body. */
	wpid = waitpid(fpid, &status, 0);
	ATF_REQUIRE(wpid == fpid);
	ATF_REQUIRE(WIFSTOPPED(status));
	ATF_REQUIRE(WSTOPSIG(status) == SIGABRT);

	ATF_REQUIRE(ptrace(PT_LWPINFO, wpid, (caddr_t)&pl, sizeof(pl)) != -1);
	ATF_REQUIRE(pl.pl_flags & PL_FLAG_SI);
	ATF_REQUIRE(pl.pl_siginfo.si_signo == SIGABRT);

	/* Step the child process inserting SIGUSR1. */
	ATF_REQUIRE(ptrace(PT_STEP, fpid, (caddr_t)1, SIGUSR1) == 0);

	/* The next stop should report the SIGABRT in the signal handler. */
	wpid = waitpid(fpid, &status, 0);
	ATF_REQUIRE(wpid == fpid);
	ATF_REQUIRE(WIFSTOPPED(status));
	ATF_REQUIRE(WSTOPSIG(status) == SIGABRT);

	ATF_REQUIRE(ptrace(PT_LWPINFO, wpid, (caddr_t)&pl, sizeof(pl)) != -1);
	ATF_REQUIRE(pl.pl_flags & PL_FLAG_SI);
	ATF_REQUIRE(pl.pl_siginfo.si_signo == SIGABRT);

	/* Continue the child process discarding the signal. */
	ATF_REQUIRE(ptrace(PT_CONTINUE, fpid, (caddr_t)1, 0) == 0);

	/* The next stop should report a trace trap from PT_STEP. */
	wpid = waitpid(fpid, &status, 0);
	ATF_REQUIRE(wpid == fpid);
	ATF_REQUIRE(WIFSTOPPED(status));
	ATF_REQUIRE(WSTOPSIG(status) == SIGTRAP);

	ATF_REQUIRE(ptrace(PT_LWPINFO, wpid, (caddr_t)&pl, sizeof(pl)) != -1);
	ATF_REQUIRE(pl.pl_flags & PL_FLAG_SI);
	ATF_REQUIRE(pl.pl_siginfo.si_signo == SIGTRAP);
	ATF_REQUIRE(pl.pl_siginfo.si_code == TRAP_TRACE);

	/* Continue the child to let it exit. */
	ATF_REQUIRE(ptrace(PT_CONTINUE, fpid, (caddr_t)1, 0) == 0);

	/* The last event should be for the child process's exit. */
	wpid = waitpid(fpid, &status, 0);
	ATF_REQUIRE(WIFEXITED(status));
	ATF_REQUIRE(WEXITSTATUS(status) == 1);

	wpid = wait(&status);
	ATF_REQUIRE(wpid == -1);
	ATF_REQUIRE(errno == ECHILD);
}

#ifdef HAVE_BREAKPOINT
/*
 * Verify that a SIGTRAP event with the TRAP_BRKPT code is reported
 * for a breakpoint trap.
 */
ATF_TC_WITHOUT_HEAD(ptrace__breakpoint_siginfo);
ATF_TC_BODY(ptrace__breakpoint_siginfo, tc)
{
	struct ptrace_lwpinfo pl;
	pid_t fpid, wpid;
	int status;

	ATF_REQUIRE((fpid = fork()) != -1);
	if (fpid == 0) {
		trace_me();
		breakpoint();
		exit(1);
	}

	/* The first wait() should report the stop from SIGSTOP. */
	wpid = waitpid(fpid, &status, 0);
	ATF_REQUIRE(wpid == fpid);
	ATF_REQUIRE(WIFSTOPPED(status));
	ATF_REQUIRE(WSTOPSIG(status) == SIGSTOP);

	/* Continue the child ignoring the SIGSTOP. */
	ATF_REQUIRE(ptrace(PT_CONTINUE, fpid, (caddr_t)1, 0) == 0);

	/* The second wait() should report hitting the breakpoint. */
	wpid = waitpid(fpid, &status, 0);
	ATF_REQUIRE(wpid == fpid);
	ATF_REQUIRE(WIFSTOPPED(status));
	ATF_REQUIRE(WSTOPSIG(status) == SIGTRAP);

	ATF_REQUIRE(ptrace(PT_LWPINFO, wpid, (caddr_t)&pl, sizeof(pl)) != -1);
	ATF_REQUIRE((pl.pl_flags & PL_FLAG_SI) != 0);
	ATF_REQUIRE(pl.pl_siginfo.si_signo == SIGTRAP);
	ATF_REQUIRE(pl.pl_siginfo.si_code == TRAP_BRKPT);

	/* Kill the child process. */
	ATF_REQUIRE(ptrace(PT_KILL, fpid, 0, 0) == 0);

	/* The last wait() should report the SIGKILL. */
	wpid = waitpid(fpid, &status, 0);
	ATF_REQUIRE(wpid == fpid);
	ATF_REQUIRE(WIFSIGNALED(status));
	ATF_REQUIRE(WTERMSIG(status) == SIGKILL);

	wpid = wait(&status);
	ATF_REQUIRE(wpid == -1);
	ATF_REQUIRE(errno == ECHILD);
}
#endif /* HAVE_BREAKPOINT */

/*
 * Verify that a SIGTRAP event with the TRAP_TRACE code is reported
 * for a single-step trap from PT_STEP.
 */
ATF_TC_WITHOUT_HEAD(ptrace__step_siginfo);
ATF_TC_BODY(ptrace__step_siginfo, tc)
{
	struct ptrace_lwpinfo pl;
	pid_t fpid, wpid;
	int status;

	ATF_REQUIRE((fpid = fork()) != -1);
	if (fpid == 0) {
		trace_me();
		exit(1);
	}

	/* The first wait() should report the stop from SIGSTOP. */
	wpid = waitpid(fpid, &status, 0);
	ATF_REQUIRE(wpid == fpid);
	ATF_REQUIRE(WIFSTOPPED(status));
	ATF_REQUIRE(WSTOPSIG(status) == SIGSTOP);

	/* Step the child ignoring the SIGSTOP. */
	ATF_REQUIRE(ptrace(PT_STEP, fpid, (caddr_t)1, 0) == 0);

	/* The second wait() should report a single-step trap. */
	wpid = waitpid(fpid, &status, 0);
	ATF_REQUIRE(wpid == fpid);
	ATF_REQUIRE(WIFSTOPPED(status));
	ATF_REQUIRE(WSTOPSIG(status) == SIGTRAP);

	ATF_REQUIRE(ptrace(PT_LWPINFO, wpid, (caddr_t)&pl, sizeof(pl)) != -1);
	ATF_REQUIRE((pl.pl_flags & PL_FLAG_SI) != 0);
	ATF_REQUIRE(pl.pl_siginfo.si_signo == SIGTRAP);
	ATF_REQUIRE(pl.pl_siginfo.si_code == TRAP_TRACE);

	/* Continue the child process. */
	ATF_REQUIRE(ptrace(PT_CONTINUE, fpid, (caddr_t)1, 0) == 0);

	/* The last event should be for the child process's exit. */
	wpid = waitpid(fpid, &status, 0);
	ATF_REQUIRE(WIFEXITED(status));
	ATF_REQUIRE(WEXITSTATUS(status) == 1);

	wpid = wait(&status);
	ATF_REQUIRE(wpid == -1);
	ATF_REQUIRE(errno == ECHILD);
}

#if defined(HAVE_BREAKPOINT) && defined(SKIP_BREAK)
static void *
continue_thread(void *arg __unused)
{
	breakpoint();
	return (NULL);
}

static __dead2 void
continue_thread_main(void)
{
	pthread_t threads[2];

	CHILD_REQUIRE(pthread_create(&threads[0], NULL, continue_thread,
	    NULL) == 0);
	CHILD_REQUIRE(pthread_create(&threads[1], NULL, continue_thread,
	    NULL) == 0);
	CHILD_REQUIRE(pthread_join(threads[0], NULL) == 0);
	CHILD_REQUIRE(pthread_join(threads[1], NULL) == 0);
	exit(1);
}

/*
 * Ensure that PT_CONTINUE clears the status of the thread that
 * triggered the stop even if a different thread's LWP was passed to
 * PT_CONTINUE.
 */
ATF_TC_WITHOUT_HEAD(ptrace__PT_CONTINUE_different_thread);
ATF_TC_BODY(ptrace__PT_CONTINUE_different_thread, tc)
{
	struct ptrace_lwpinfo pl;
	pid_t fpid, wpid;
	lwpid_t lwps[2];
	bool hit_break[2];
	struct reg reg;
	int i, j, status;

	ATF_REQUIRE((fpid = fork()) != -1);
	if (fpid == 0) {
		trace_me();
		continue_thread_main();
	}

	/* The first wait() should report the stop from SIGSTOP. */
	wpid = waitpid(fpid, &status, 0);
	ATF_REQUIRE(wpid == fpid);
	ATF_REQUIRE(WIFSTOPPED(status));
	ATF_REQUIRE(WSTOPSIG(status) == SIGSTOP);

	ATF_REQUIRE(ptrace(PT_LWPINFO, wpid, (caddr_t)&pl,
	    sizeof(pl)) != -1);

	ATF_REQUIRE(ptrace(PT_LWP_EVENTS, wpid, NULL, 1) == 0);

	/* Continue the child ignoring the SIGSTOP. */
	ATF_REQUIRE(ptrace(PT_CONTINUE, fpid, (caddr_t)1, 0) == 0);

	/* One of the new threads should report it's birth. */
	wpid = waitpid(fpid, &status, 0);
	ATF_REQUIRE(wpid == fpid);
	ATF_REQUIRE(WIFSTOPPED(status));
	ATF_REQUIRE(WSTOPSIG(status) == SIGTRAP);

	ATF_REQUIRE(ptrace(PT_LWPINFO, wpid, (caddr_t)&pl, sizeof(pl)) != -1);
	ATF_REQUIRE((pl.pl_flags & (PL_FLAG_BORN | PL_FLAG_SCX)) ==
	    (PL_FLAG_BORN | PL_FLAG_SCX));
	lwps[0] = pl.pl_lwpid;

	/*
	 * Suspend this thread to ensure both threads are alive before
	 * hitting the breakpoint.
	 */
	ATF_REQUIRE(ptrace(PT_SUSPEND, lwps[0], NULL, 0) != -1);

	ATF_REQUIRE(ptrace(PT_CONTINUE, fpid, (caddr_t)1, 0) == 0);

	/* Second thread should report it's birth. */
	wpid = waitpid(fpid, &status, 0);
	ATF_REQUIRE(wpid == fpid);
	ATF_REQUIRE(WIFSTOPPED(status));
	ATF_REQUIRE(WSTOPSIG(status) == SIGTRAP);

	ATF_REQUIRE(ptrace(PT_LWPINFO, wpid, (caddr_t)&pl, sizeof(pl)) != -1);
	ATF_REQUIRE((pl.pl_flags & (PL_FLAG_BORN | PL_FLAG_SCX)) ==
	    (PL_FLAG_BORN | PL_FLAG_SCX));
	ATF_REQUIRE(pl.pl_lwpid != lwps[0]);
	lwps[1] = pl.pl_lwpid;

	/* Resume both threads waiting for breakpoint events. */
	hit_break[0] = hit_break[1] = false;
	ATF_REQUIRE(ptrace(PT_RESUME, lwps[0], NULL, 0) != -1);
	ATF_REQUIRE(ptrace(PT_CONTINUE, fpid, (caddr_t)1, 0) == 0);

	/* One thread should report a breakpoint. */
	wpid = waitpid(fpid, &status, 0);
	ATF_REQUIRE(wpid == fpid);
	ATF_REQUIRE(WIFSTOPPED(status));
	ATF_REQUIRE(WSTOPSIG(status) == SIGTRAP);

	ATF_REQUIRE(ptrace(PT_LWPINFO, wpid, (caddr_t)&pl, sizeof(pl)) != -1);
	ATF_REQUIRE((pl.pl_flags & PL_FLAG_SI) != 0);
	ATF_REQUIRE(pl.pl_siginfo.si_signo == SIGTRAP &&
	    pl.pl_siginfo.si_code == TRAP_BRKPT);
	if (pl.pl_lwpid == lwps[0])
		i = 0;
	else
		i = 1;
	hit_break[i] = true;
	ATF_REQUIRE(ptrace(PT_GETREGS, pl.pl_lwpid, (caddr_t)&reg, 0) != -1);
	SKIP_BREAK(&reg);
	ATF_REQUIRE(ptrace(PT_SETREGS, pl.pl_lwpid, (caddr_t)&reg, 0) != -1);

	/*
	 * Resume both threads but pass the other thread's LWPID to
	 * PT_CONTINUE.
	 */
	ATF_REQUIRE(ptrace(PT_CONTINUE, lwps[i ^ 1], (caddr_t)1, 0) == 0);

	/*
	 * Will now get two thread exit events and one more breakpoint
	 * event.
	 */
	for (j = 0; j < 3; j++) {
		wpid = waitpid(fpid, &status, 0);
		ATF_REQUIRE(wpid == fpid);
		ATF_REQUIRE(WIFSTOPPED(status));
		ATF_REQUIRE(WSTOPSIG(status) == SIGTRAP);

		ATF_REQUIRE(ptrace(PT_LWPINFO, wpid, (caddr_t)&pl,
		    sizeof(pl)) != -1);
		
		if (pl.pl_lwpid == lwps[0])
			i = 0;
		else
			i = 1;

		ATF_REQUIRE_MSG(lwps[i] != 0, "event for exited thread");
		if (pl.pl_flags & PL_FLAG_EXITED) {
			ATF_REQUIRE_MSG(hit_break[i],
			    "exited thread did not report breakpoint");
			lwps[i] = 0;
		} else {
			ATF_REQUIRE((pl.pl_flags & PL_FLAG_SI) != 0);
			ATF_REQUIRE(pl.pl_siginfo.si_signo == SIGTRAP &&
			    pl.pl_siginfo.si_code == TRAP_BRKPT);
			ATF_REQUIRE_MSG(!hit_break[i],
			    "double breakpoint event");
			hit_break[i] = true;
			ATF_REQUIRE(ptrace(PT_GETREGS, pl.pl_lwpid, (caddr_t)&reg,
			    0) != -1);
			SKIP_BREAK(&reg);
			ATF_REQUIRE(ptrace(PT_SETREGS, pl.pl_lwpid, (caddr_t)&reg,
			    0) != -1);
		}

		ATF_REQUIRE(ptrace(PT_CONTINUE, fpid, (caddr_t)1, 0) == 0);
	}

	/* Both threads should have exited. */
	ATF_REQUIRE(lwps[0] == 0);
	ATF_REQUIRE(lwps[1] == 0);

	/* The last event should be for the child process's exit. */
	wpid = waitpid(fpid, &status, 0);
	ATF_REQUIRE(WIFEXITED(status));
	ATF_REQUIRE(WEXITSTATUS(status) == 1);

	wpid = wait(&status);
	ATF_REQUIRE(wpid == -1);
	ATF_REQUIRE(errno == ECHILD);
}
#endif

/*
 * Verify that PT_LWPINFO doesn't return stale siginfo.
 */
ATF_TC_WITHOUT_HEAD(ptrace__PT_LWPINFO_stale_siginfo);
ATF_TC_BODY(ptrace__PT_LWPINFO_stale_siginfo, tc)
{
	struct ptrace_lwpinfo pl;
	pid_t fpid, wpid;
	int events, status;

	ATF_REQUIRE((fpid = fork()) != -1);
	if (fpid == 0) {
		trace_me();
		raise(SIGABRT);
		exit(1);
	}

	/* The first wait() should report the stop from SIGSTOP. */
	wpid = waitpid(fpid, &status, 0);
	ATF_REQUIRE(wpid == fpid);
	ATF_REQUIRE(WIFSTOPPED(status));
	ATF_REQUIRE(WSTOPSIG(status) == SIGSTOP);

	ATF_REQUIRE(ptrace(PT_CONTINUE, fpid, (caddr_t)1, 0) == 0);

	/* The next stop should report the SIGABRT in the child body. */
	wpid = waitpid(fpid, &status, 0);
	ATF_REQUIRE(wpid == fpid);
	ATF_REQUIRE(WIFSTOPPED(status));
	ATF_REQUIRE(WSTOPSIG(status) == SIGABRT);

	ATF_REQUIRE(ptrace(PT_LWPINFO, wpid, (caddr_t)&pl, sizeof(pl)) != -1);
	ATF_REQUIRE(pl.pl_flags & PL_FLAG_SI);
	ATF_REQUIRE(pl.pl_siginfo.si_signo == SIGABRT);

	/*
	 * Continue the process ignoring the signal, but enabling
	 * syscall traps.
	 */
	ATF_REQUIRE(ptrace(PT_SYSCALL, fpid, (caddr_t)1, 0) == 0);

	/*
	 * The next stop should report a system call entry from
	 * exit().  PL_FLAGS_SI should not be set.
	 */
	wpid = waitpid(fpid, &status, 0);
	ATF_REQUIRE(wpid == fpid);
	ATF_REQUIRE(WIFSTOPPED(status));
	ATF_REQUIRE(WSTOPSIG(status) == SIGTRAP);

	ATF_REQUIRE(ptrace(PT_LWPINFO, wpid, (caddr_t)&pl, sizeof(pl)) != -1);
	ATF_REQUIRE(pl.pl_flags & PL_FLAG_SCE);
	ATF_REQUIRE((pl.pl_flags & PL_FLAG_SI) == 0);

	/* Disable syscall tracing and continue the child to let it exit. */
	ATF_REQUIRE(ptrace(PT_GET_EVENT_MASK, fpid, (caddr_t)&events,
	    sizeof(events)) == 0);
	events &= ~PTRACE_SYSCALL;
	ATF_REQUIRE(ptrace(PT_SET_EVENT_MASK, fpid, (caddr_t)&events,
	    sizeof(events)) == 0);
	ATF_REQUIRE(ptrace(PT_CONTINUE, fpid, (caddr_t)1, 0) == 0);

	/* The last event should be for the child process's exit. */
	wpid = waitpid(fpid, &status, 0);
	ATF_REQUIRE(WIFEXITED(status));
	ATF_REQUIRE(WEXITSTATUS(status) == 1);

	wpid = wait(&status);
	ATF_REQUIRE(wpid == -1);
	ATF_REQUIRE(errno == ECHILD);
}

ATF_TP_ADD_TCS(tp)
{

	ATF_TP_ADD_TC(tp, ptrace__parent_wait_after_trace_me);
	ATF_TP_ADD_TC(tp, ptrace__parent_wait_after_attach);
	ATF_TP_ADD_TC(tp, ptrace__parent_sees_exit_after_child_debugger);
	ATF_TP_ADD_TC(tp, ptrace__parent_sees_exit_after_unrelated_debugger);
	ATF_TP_ADD_TC(tp, ptrace__follow_fork_both_attached);
	ATF_TP_ADD_TC(tp, ptrace__follow_fork_child_detached);
	ATF_TP_ADD_TC(tp, ptrace__follow_fork_parent_detached);
	ATF_TP_ADD_TC(tp, ptrace__follow_fork_both_attached_unrelated_debugger);
	ATF_TP_ADD_TC(tp,
	    ptrace__follow_fork_child_detached_unrelated_debugger);
	ATF_TP_ADD_TC(tp,
	    ptrace__follow_fork_parent_detached_unrelated_debugger);
	ATF_TP_ADD_TC(tp, ptrace__getppid);
	ATF_TP_ADD_TC(tp, ptrace__new_child_pl_syscall_code_fork);
	ATF_TP_ADD_TC(tp, ptrace__new_child_pl_syscall_code_vfork);
	ATF_TP_ADD_TC(tp, ptrace__new_child_pl_syscall_code_thread);
	ATF_TP_ADD_TC(tp, ptrace__lwp_events);
	ATF_TP_ADD_TC(tp, ptrace__lwp_events_exec);
	ATF_TP_ADD_TC(tp, ptrace__siginfo);
	ATF_TP_ADD_TC(tp, ptrace__ptrace_exec_disable);
	ATF_TP_ADD_TC(tp, ptrace__ptrace_exec_enable);
	ATF_TP_ADD_TC(tp, ptrace__event_mask);
	ATF_TP_ADD_TC(tp, ptrace__ptrace_vfork);
	ATF_TP_ADD_TC(tp, ptrace__ptrace_vfork_follow);
#ifdef HAVE_BREAKPOINT
	ATF_TP_ADD_TC(tp, ptrace__PT_KILL_breakpoint);
#endif
	ATF_TP_ADD_TC(tp, ptrace__PT_KILL_system_call);
	ATF_TP_ADD_TC(tp, ptrace__PT_KILL_threads);
	ATF_TP_ADD_TC(tp, ptrace__PT_KILL_competing_signal);
	ATF_TP_ADD_TC(tp, ptrace__PT_KILL_competing_stop);
	ATF_TP_ADD_TC(tp, ptrace__PT_KILL_with_signal_full_sigqueue);
	ATF_TP_ADD_TC(tp, ptrace__PT_CONTINUE_with_signal_system_call_entry);
	ATF_TP_ADD_TC(tp,
	    ptrace__PT_CONTINUE_with_signal_system_call_entry_and_exit);
	ATF_TP_ADD_TC(tp, ptrace__PT_CONTINUE_with_signal_full_sigqueue);
	ATF_TP_ADD_TC(tp, ptrace__PT_CONTINUE_with_signal_masked_full_sigqueue);
	ATF_TP_ADD_TC(tp, ptrace__PT_CONTINUE_change_sig);
	ATF_TP_ADD_TC(tp, ptrace__PT_CONTINUE_with_sigtrap_system_call_entry);
	ATF_TP_ADD_TC(tp, ptrace__PT_CONTINUE_with_signal_mix);
	ATF_TP_ADD_TC(tp, ptrace__PT_CONTINUE_with_signal_kqueue);
	ATF_TP_ADD_TC(tp, ptrace__killed_with_sigmask);
	ATF_TP_ADD_TC(tp, ptrace__PT_CONTINUE_with_sigmask);
	ATF_TP_ADD_TC(tp, ptrace__PT_CONTINUE_with_signal_thread_sigmask);
	ATF_TP_ADD_TC(tp, ptrace__parent_terminate_with_pending_sigstop1);
	ATF_TP_ADD_TC(tp, ptrace__parent_terminate_with_pending_sigstop2);
	ATF_TP_ADD_TC(tp, ptrace__event_mask_sigkill_discard);
	ATF_TP_ADD_TC(tp, ptrace__PT_ATTACH_with_SBDRY_thread);
	ATF_TP_ADD_TC(tp, ptrace__PT_STEP_with_signal);
#ifdef HAVE_BREAKPOINT
	ATF_TP_ADD_TC(tp, ptrace__breakpoint_siginfo);
#endif
	ATF_TP_ADD_TC(tp, ptrace__step_siginfo);
#if defined(HAVE_BREAKPOINT) && defined(SKIP_BREAK)
	ATF_TP_ADD_TC(tp, ptrace__PT_CONTINUE_different_thread);
#endif
	ATF_TP_ADD_TC(tp, ptrace__PT_LWPINFO_stale_siginfo);

	return (atf_no_error());
}
