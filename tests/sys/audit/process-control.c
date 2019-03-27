/*-
 * Copyright (c) 2018 Aniket Pandey
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
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#include <sys/param.h>
#include <sys/capsicum.h>
#include <sys/uio.h>
#include <sys/ktrace.h>
#include <sys/mman.h>
#include <sys/procctl.h>
#include <sys/ptrace.h>
#include <sys/resource.h>
#include <sys/rtprio.h>
#include <sys/stat.h>
#include <sys/sysctl.h>
#include <sys/time.h>
#include <sys/wait.h>

#include <atf-c.h>
#include <fcntl.h>
#include <signal.h>
#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>

#include "utils.h"

static pid_t pid;
static int filedesc, status;
static struct pollfd fds[1];
static char pcregex[80];
static const char *auclass = "pc";


ATF_TC_WITH_CLEANUP(fork_success);
ATF_TC_HEAD(fork_success, tc)
{
	atf_tc_set_md_var(tc, "descr", "Tests the audit of a successful "
					"fork(2) call");
}

ATF_TC_BODY(fork_success, tc)
{
	pid = getpid();
	snprintf(pcregex, sizeof(pcregex), "fork.*%d.*return,success", pid);

	FILE *pipefd = setup(fds, auclass);
	/* Check if fork(2) succeded. If so, exit from the child process */
	ATF_REQUIRE((pid = fork()) != -1);
	if (pid)
		check_audit(fds, pcregex, pipefd);
	else
		_exit(0);
}

ATF_TC_CLEANUP(fork_success, tc)
{
	cleanup();
}

/*
 * No fork(2) in failure mode since possibilities for failure are only when
 * user is not privileged or when the number of processes exceed KERN_MAXPROC.
 */


ATF_TC_WITH_CLEANUP(_exit_success);
ATF_TC_HEAD(_exit_success, tc)
{
	atf_tc_set_md_var(tc, "descr", "Tests the audit of a successful "
					"_exit(2) call");
}

ATF_TC_BODY(_exit_success, tc)
{
	FILE *pipefd = setup(fds, auclass);
	ATF_REQUIRE((pid = fork()) != -1);
	if (pid) {
		snprintf(pcregex, sizeof(pcregex), "exit.*%d.*success", pid);
		check_audit(fds, pcregex, pipefd);
	}
	else
		_exit(0);
}

ATF_TC_CLEANUP(_exit_success, tc)
{
	cleanup();
}

/*
 * _exit(2) never returns, hence the auditing by default is always successful
 */


ATF_TC_WITH_CLEANUP(rfork_success);
ATF_TC_HEAD(rfork_success, tc)
{
	atf_tc_set_md_var(tc, "descr", "Tests the audit of a successful "
					"rfork(2) call");
}

ATF_TC_BODY(rfork_success, tc)
{
	pid = getpid();
	snprintf(pcregex, sizeof(pcregex), "rfork.*%d.*return,success", pid);

	FILE *pipefd = setup(fds, auclass);
	ATF_REQUIRE((pid = rfork(RFPROC)) != -1);
	if (pid)
		check_audit(fds, pcregex, pipefd);
	else
		_exit(0);
}

ATF_TC_CLEANUP(rfork_success, tc)
{
	cleanup();
}


ATF_TC_WITH_CLEANUP(rfork_failure);
ATF_TC_HEAD(rfork_failure, tc)
{
	atf_tc_set_md_var(tc, "descr", "Tests the audit of an unsuccessful "
					"rfork(2) call");
}

ATF_TC_BODY(rfork_failure, tc)
{
	pid = getpid();
	snprintf(pcregex, sizeof(pcregex), "rfork.*%d.*return,failure", pid);

	FILE *pipefd = setup(fds, auclass);
	/* Failure reason: Invalid argument */
	ATF_REQUIRE_EQ(-1, rfork(-1));
	check_audit(fds, pcregex, pipefd);
}

ATF_TC_CLEANUP(rfork_failure, tc)
{
	cleanup();
}


ATF_TC_WITH_CLEANUP(wait4_success);
ATF_TC_HEAD(wait4_success, tc)
{
	atf_tc_set_md_var(tc, "descr", "Tests the audit of a successful "
					"wait4(2) call");
}

ATF_TC_BODY(wait4_success, tc)
{
	pid = getpid();
	snprintf(pcregex, sizeof(pcregex), "wait4.*%d.*return,success", pid);

	ATF_REQUIRE((pid = fork()) != -1);
	if (pid) {
		FILE *pipefd = setup(fds, auclass);
		/* wpid = -1 : Wait for any child process */
		ATF_REQUIRE(wait4(-1, &status, 0, NULL) != -1);
		check_audit(fds, pcregex, pipefd);
	}
	else
		_exit(0);
}

ATF_TC_CLEANUP(wait4_success, tc)
{
	cleanup();
}


ATF_TC_WITH_CLEANUP(wait4_failure);
ATF_TC_HEAD(wait4_failure, tc)
{
	atf_tc_set_md_var(tc, "descr", "Tests the audit of an unsuccessful "
					"wait4(2) call");
}

ATF_TC_BODY(wait4_failure, tc)
{
	pid = getpid();
	snprintf(pcregex, sizeof(pcregex), "wait4.*%d.*return,failure", pid);

	FILE *pipefd = setup(fds, auclass);
	/* Failure reason: No child process to wait for */
	ATF_REQUIRE_EQ(-1, wait4(-1, NULL, 0, NULL));
	check_audit(fds, pcregex, pipefd);
}

ATF_TC_CLEANUP(wait4_failure, tc)
{
	cleanup();
}


ATF_TC_WITH_CLEANUP(wait6_success);
ATF_TC_HEAD(wait6_success, tc)
{
	atf_tc_set_md_var(tc, "descr", "Tests the audit of a successful "
					"wait6(2) call");
}

ATF_TC_BODY(wait6_success, tc)
{
	pid = getpid();
	snprintf(pcregex, sizeof(pcregex), "wait6.*%d.*return,success", pid);

	ATF_REQUIRE((pid = fork()) != -1);
	if (pid) {
		FILE *pipefd = setup(fds, auclass);
		ATF_REQUIRE(wait6(P_ALL, 0, &status, WEXITED, NULL,NULL) != -1);
		check_audit(fds, pcregex, pipefd);
	}
	else
		_exit(0);
}

ATF_TC_CLEANUP(wait6_success, tc)
{
	cleanup();
}


ATF_TC_WITH_CLEANUP(wait6_failure);
ATF_TC_HEAD(wait6_failure, tc)
{
	atf_tc_set_md_var(tc, "descr", "Tests the audit of an unsuccessful "
					"wait6(2) call");
}

ATF_TC_BODY(wait6_failure, tc)
{
	pid = getpid();
	snprintf(pcregex, sizeof(pcregex), "wait6.*%d.*return,failure", pid);

	FILE *pipefd = setup(fds, auclass);
	/* Failure reason: Invalid argument */
	ATF_REQUIRE_EQ(-1, wait6(0, 0, NULL, 0, NULL, NULL));
	check_audit(fds, pcregex, pipefd);
}

ATF_TC_CLEANUP(wait6_failure, tc)
{
	cleanup();
}


ATF_TC_WITH_CLEANUP(kill_success);
ATF_TC_HEAD(kill_success, tc)
{
	atf_tc_set_md_var(tc, "descr", "Tests the audit of a successful "
					"kill(2) call");
}

ATF_TC_BODY(kill_success, tc)
{
	pid = getpid();
	snprintf(pcregex, sizeof(pcregex), "kill.*%d.*return,success", pid);

	FILE *pipefd = setup(fds, auclass);
	/* Don't send any signal to anyone, live in peace! */
	ATF_REQUIRE_EQ(0, kill(0, 0));
	check_audit(fds, pcregex, pipefd);
}

ATF_TC_CLEANUP(kill_success, tc)
{
	cleanup();
}


ATF_TC_WITH_CLEANUP(kill_failure);
ATF_TC_HEAD(kill_failure, tc)
{
	atf_tc_set_md_var(tc, "descr", "Tests the audit of an unsuccessful "
					"kill(2) call");
}

ATF_TC_BODY(kill_failure, tc)
{
	pid = getpid();
	snprintf(pcregex, sizeof(pcregex), "kill.*%d.*return,failure", pid);

	FILE *pipefd = setup(fds, auclass);
	/*
	 * Failure reason: Non existent process with PID '-2'
	 * Note: '-1' is not used as it means sending no signal to
	 * all non-system processes: A successful invocation
	 */
	ATF_REQUIRE_EQ(-1, kill(0, -2));
	check_audit(fds, pcregex, pipefd);
}

ATF_TC_CLEANUP(kill_failure, tc)
{
	cleanup();
}


ATF_TC_WITH_CLEANUP(chdir_success);
ATF_TC_HEAD(chdir_success, tc)
{
	atf_tc_set_md_var(tc, "descr", "Tests the audit of a successful "
					"chdir(2) call");
}

ATF_TC_BODY(chdir_success, tc)
{
	pid = getpid();
	snprintf(pcregex, sizeof(pcregex), "chdir.*/.*%d.*return,success", pid);

	FILE *pipefd = setup(fds, auclass);
	ATF_REQUIRE_EQ(0, chdir("/"));
	check_audit(fds, pcregex, pipefd);
}

ATF_TC_CLEANUP(chdir_success, tc)
{
	cleanup();
}


ATF_TC_WITH_CLEANUP(chdir_failure);
ATF_TC_HEAD(chdir_failure, tc)
{
	atf_tc_set_md_var(tc, "descr", "Tests the audit of an unsuccessful "
					"chdir(2) call");
}

ATF_TC_BODY(chdir_failure, tc)
{
	pid = getpid();
	snprintf(pcregex, sizeof(pcregex), "chdir.*%d.*return,failure", pid);

	FILE *pipefd = setup(fds, auclass);
	/* Failure reason: Bad address */
	ATF_REQUIRE_EQ(-1, chdir(NULL));
	check_audit(fds, pcregex, pipefd);
}

ATF_TC_CLEANUP(chdir_failure, tc)
{
	cleanup();
}


ATF_TC_WITH_CLEANUP(fchdir_success);
ATF_TC_HEAD(fchdir_success, tc)
{
	atf_tc_set_md_var(tc, "descr", "Tests the audit of a successful "
					"fchdir(2) call");
}

ATF_TC_BODY(fchdir_success, tc)
{
	/* Build an absolute path to the test-case directory */
	char dirpath[50];
	ATF_REQUIRE(getcwd(dirpath, sizeof(dirpath)) != NULL);
	ATF_REQUIRE((filedesc = open(dirpath, O_RDONLY)) != -1);

	/* Audit record generated by fchdir(2) does not contain filedesc */
	pid = getpid();
	snprintf(pcregex, sizeof(pcregex), "fchdir.*%d.*return,success", pid);

	FILE *pipefd = setup(fds, auclass);
	ATF_REQUIRE_EQ(0, fchdir(filedesc));
	check_audit(fds, pcregex, pipefd);
	close(filedesc);
}

ATF_TC_CLEANUP(fchdir_success, tc)
{
	cleanup();
}


ATF_TC_WITH_CLEANUP(fchdir_failure);
ATF_TC_HEAD(fchdir_failure, tc)
{
	atf_tc_set_md_var(tc, "descr", "Tests the audit of an unsuccessful "
					"fchdir(2) call");
}

ATF_TC_BODY(fchdir_failure, tc)
{
	pid = getpid();
	snprintf(pcregex, sizeof(pcregex), "fchdir.*%d.*return,failure", pid);

	FILE *pipefd = setup(fds, auclass);
	/* Failure reason: Bad directory address */
	ATF_REQUIRE_EQ(-1, fchdir(-1));
	check_audit(fds, pcregex, pipefd);
}

ATF_TC_CLEANUP(fchdir_failure, tc)
{
	cleanup();
}


ATF_TC_WITH_CLEANUP(chroot_success);
ATF_TC_HEAD(chroot_success, tc)
{
	atf_tc_set_md_var(tc, "descr", "Tests the audit of a successful "
					"chroot(2) call");
}

ATF_TC_BODY(chroot_success, tc)
{
	pid = getpid();
	snprintf(pcregex, sizeof(pcregex), "chroot.*%d.*return,success", pid);

	FILE *pipefd = setup(fds, auclass);
	/* We don't want to change the root directory, hence '/' */
	ATF_REQUIRE_EQ(0, chroot("/"));
	check_audit(fds, pcregex, pipefd);
}

ATF_TC_CLEANUP(chroot_success, tc)
{
	cleanup();
}


ATF_TC_WITH_CLEANUP(chroot_failure);
ATF_TC_HEAD(chroot_failure, tc)
{
	atf_tc_set_md_var(tc, "descr", "Tests the audit of an unsuccessful "
					"chroot(2) call");
}

ATF_TC_BODY(chroot_failure, tc)
{
	pid = getpid();
	snprintf(pcregex, sizeof(pcregex), "chroot.*%d.*return,failure", pid);

	FILE *pipefd = setup(fds, auclass);
	ATF_REQUIRE_EQ(-1, chroot(NULL));
	check_audit(fds, pcregex, pipefd);
}

ATF_TC_CLEANUP(chroot_failure, tc)
{
	cleanup();
}


ATF_TC_WITH_CLEANUP(umask_success);
ATF_TC_HEAD(umask_success, tc)
{
	atf_tc_set_md_var(tc, "descr", "Tests the audit of a successful "
					"umask(2) call");
}

ATF_TC_BODY(umask_success, tc)
{
	pid = getpid();
	snprintf(pcregex, sizeof(pcregex), "umask.*%d.*return,success", pid);

	FILE *pipefd = setup(fds, auclass);
	umask(0);
	check_audit(fds, pcregex, pipefd);
}

ATF_TC_CLEANUP(umask_success, tc)
{
	cleanup();
}

/*
 * umask(2) system call never fails. Hence, no test case for failure mode
 */


ATF_TC_WITH_CLEANUP(setuid_success);
ATF_TC_HEAD(setuid_success, tc)
{
	atf_tc_set_md_var(tc, "descr", "Tests the audit of a successful "
					"setuid(2) call");
}

ATF_TC_BODY(setuid_success, tc)
{
	pid = getpid();
	snprintf(pcregex, sizeof(pcregex), "setuid.*%d.*return,success", pid);

	FILE *pipefd = setup(fds, auclass);
	/* Since we're privileged, we'll let ourselves be privileged! */
	ATF_REQUIRE_EQ(0, setuid(0));
	check_audit(fds, pcregex, pipefd);
}

ATF_TC_CLEANUP(setuid_success, tc)
{
	cleanup();
}

/*
 * setuid(2) fails only when the current user is not root. So no test case for
 * failure mode since the required_user="root"
 */


ATF_TC_WITH_CLEANUP(seteuid_success);
ATF_TC_HEAD(seteuid_success, tc)
{
	atf_tc_set_md_var(tc, "descr", "Tests the audit of a successful "
					"seteuid(2) call");
}

ATF_TC_BODY(seteuid_success, tc)
{
	pid = getpid();
	snprintf(pcregex, sizeof(pcregex), "seteuid.*%d.*return,success", pid);

	FILE *pipefd = setup(fds, auclass);
	/* This time, we'll let ourselves be 'effectively' privileged! */
	ATF_REQUIRE_EQ(0, seteuid(0));
	check_audit(fds, pcregex, pipefd);
}

ATF_TC_CLEANUP(seteuid_success, tc)
{
	cleanup();
}

/*
 * seteuid(2) fails only when the current user is not root. So no test case for
 * failure mode since the required_user="root"
 */


ATF_TC_WITH_CLEANUP(setgid_success);
ATF_TC_HEAD(setgid_success, tc)
{
	atf_tc_set_md_var(tc, "descr", "Tests the audit of a successful "
					"setgid(2) call");
}

ATF_TC_BODY(setgid_success, tc)
{
	pid = getpid();
	snprintf(pcregex, sizeof(pcregex), "setgid.*%d.*return,success", pid);

	FILE *pipefd = setup(fds, auclass);
	ATF_REQUIRE_EQ(0, setgid(0));
	check_audit(fds, pcregex, pipefd);
}

ATF_TC_CLEANUP(setgid_success, tc)
{
	cleanup();
}

/*
 * setgid(2) fails only when the current user is not root. So no test case for
 * failure mode since the required_user="root"
 */


ATF_TC_WITH_CLEANUP(setegid_success);
ATF_TC_HEAD(setegid_success, tc)
{
	atf_tc_set_md_var(tc, "descr", "Tests the audit of a successful "
					"setegid(2) call");
}

ATF_TC_BODY(setegid_success, tc)
{
	pid = getpid();
	snprintf(pcregex, sizeof(pcregex), "setegid.*%d.*return,success", pid);

	FILE *pipefd = setup(fds, auclass);
	ATF_REQUIRE_EQ(0, setegid(0));
	check_audit(fds, pcregex, pipefd);
}

ATF_TC_CLEANUP(setegid_success, tc)
{
	cleanup();
}

/*
 * setegid(2) fails only when the current user is not root. So no test case for
 * failure mode since the required_user="root"
 */


ATF_TC_WITH_CLEANUP(setregid_success);
ATF_TC_HEAD(setregid_success, tc)
{
	atf_tc_set_md_var(tc, "descr", "Tests the audit of a successful "
					"setregid(2) call");
}

ATF_TC_BODY(setregid_success, tc)
{
	pid = getpid();
	snprintf(pcregex, sizeof(pcregex), "setregid.*%d.*return,success", pid);

	FILE *pipefd = setup(fds, auclass);
	/* setregid(-1, -1) does not change any real or effective GIDs */
	ATF_REQUIRE_EQ(0, setregid(-1, -1));
	check_audit(fds, pcregex, pipefd);
}

ATF_TC_CLEANUP(setregid_success, tc)
{
	cleanup();
}

/*
 * setregid(2) fails only when the current user is not root. So no test case for
 * failure mode since the required_user="root"
 */


ATF_TC_WITH_CLEANUP(setreuid_success);
ATF_TC_HEAD(setreuid_success, tc)
{
	atf_tc_set_md_var(tc, "descr", "Tests the audit of a successful "
					"setreuid(2) call");
}

ATF_TC_BODY(setreuid_success, tc)
{
	pid = getpid();
	snprintf(pcregex, sizeof(pcregex), "setreuid.*%d.*return,success", pid);

	FILE *pipefd = setup(fds, auclass);
	/* setreuid(-1, -1) does not change any real or effective UIDs */
	ATF_REQUIRE_EQ(0, setreuid(-1, -1));
	check_audit(fds, pcregex, pipefd);
}

ATF_TC_CLEANUP(setreuid_success, tc)
{
	cleanup();
}

/*
 * setregid(2) fails only when the current user is not root. So no test case for
 * failure mode since the required_user="root"
 */


ATF_TC_WITH_CLEANUP(setresuid_success);
ATF_TC_HEAD(setresuid_success, tc)
{
	atf_tc_set_md_var(tc, "descr", "Tests the audit of a successful "
					"setresuid(2) call");
}

ATF_TC_BODY(setresuid_success, tc)
{
	pid = getpid();
	snprintf(pcregex, sizeof(pcregex), "setresuid.*%d.*return,success", pid);

	FILE *pipefd = setup(fds, auclass);
	/* setresuid(-1, -1, -1) does not change real, effective & saved UIDs */
	ATF_REQUIRE_EQ(0, setresuid(-1, -1, -1));
	check_audit(fds, pcregex, pipefd);
}

ATF_TC_CLEANUP(setresuid_success, tc)
{
	cleanup();
}

/*
 * setresuid(2) fails only when the current user is not root. So no test case
 * for failure mode since the required_user="root"
 */


ATF_TC_WITH_CLEANUP(setresgid_success);
ATF_TC_HEAD(setresgid_success, tc)
{
	atf_tc_set_md_var(tc, "descr", "Tests the audit of a successful "
					"setresgid(2) call");
}

ATF_TC_BODY(setresgid_success, tc)
{
	pid = getpid();
	snprintf(pcregex, sizeof(pcregex), "setresgid.*%d.*ret.*success", pid);

	FILE *pipefd = setup(fds, auclass);
	/* setresgid(-1, -1, -1) does not change real, effective & saved GIDs */
	ATF_REQUIRE_EQ(0, setresgid(-1, -1, -1));
	check_audit(fds, pcregex, pipefd);
}

ATF_TC_CLEANUP(setresgid_success, tc)
{
	cleanup();
}

/*
 * setresgid(2) fails only when the current user is not root. So no test case
 * for failure mode since the required_user="root"
 */


ATF_TC_WITH_CLEANUP(getresuid_success);
ATF_TC_HEAD(getresuid_success, tc)
{
	atf_tc_set_md_var(tc, "descr", "Tests the audit of a successful "
					"getresuid(2) call");
}

ATF_TC_BODY(getresuid_success, tc)
{
	pid = getpid();
	snprintf(pcregex, sizeof(pcregex), "getresuid.*%d.*ret.*success", pid);

	FILE *pipefd = setup(fds, auclass);
	ATF_REQUIRE_EQ(0, getresuid(NULL, NULL, NULL));
	check_audit(fds, pcregex, pipefd);
}

ATF_TC_CLEANUP(getresuid_success, tc)
{
	cleanup();
}


ATF_TC_WITH_CLEANUP(getresuid_failure);
ATF_TC_HEAD(getresuid_failure, tc)
{
	atf_tc_set_md_var(tc, "descr", "Tests the audit of an unsuccessful "
					"getresuid(2) call");
}

ATF_TC_BODY(getresuid_failure, tc)
{
	pid = getpid();
	snprintf(pcregex, sizeof(pcregex), "getresuid.*%d.*ret.*failure", pid);

	FILE *pipefd = setup(fds, auclass);
	/* Failure reason: Invalid address "-1" */
	ATF_REQUIRE_EQ(-1, getresuid((uid_t *)-1, NULL, NULL));
	check_audit(fds, pcregex, pipefd);
}

ATF_TC_CLEANUP(getresuid_failure, tc)
{
	cleanup();
}


ATF_TC_WITH_CLEANUP(getresgid_success);
ATF_TC_HEAD(getresgid_success, tc)
{
	atf_tc_set_md_var(tc, "descr", "Tests the audit of a successful "
					"getresgid(2) call");
}

ATF_TC_BODY(getresgid_success, tc)
{
	pid = getpid();
	snprintf(pcregex, sizeof(pcregex), "getresgid.*%d.*ret.*success", pid);

	FILE *pipefd = setup(fds, auclass);
	ATF_REQUIRE_EQ(0, getresgid(NULL, NULL, NULL));
	check_audit(fds, pcregex, pipefd);
}

ATF_TC_CLEANUP(getresgid_success, tc)
{
	cleanup();
}


ATF_TC_WITH_CLEANUP(getresgid_failure);
ATF_TC_HEAD(getresgid_failure, tc)
{
	atf_tc_set_md_var(tc, "descr", "Tests the audit of an unsuccessful "
					"getresgid(2) call");
}

ATF_TC_BODY(getresgid_failure, tc)
{
	pid = getpid();
	snprintf(pcregex, sizeof(pcregex), "getresgid.*%d.*ret.*failure", pid);

	FILE *pipefd = setup(fds, auclass);
	/* Failure reason: Invalid address "-1" */
	ATF_REQUIRE_EQ(-1, getresgid((gid_t *)-1, NULL, NULL));
	check_audit(fds, pcregex, pipefd);
}

ATF_TC_CLEANUP(getresgid_failure, tc)
{
	cleanup();
}


ATF_TC_WITH_CLEANUP(setpriority_success);
ATF_TC_HEAD(setpriority_success, tc)
{
	atf_tc_set_md_var(tc, "descr", "Tests the audit of a successful "
					"setpriority(2) call");
}

ATF_TC_BODY(setpriority_success, tc)
{
	pid = getpid();
	snprintf(pcregex, sizeof(pcregex), "setpriority.*%d.*success", pid);

	FILE *pipefd = setup(fds, auclass);
	ATF_REQUIRE_EQ(0, setpriority(PRIO_PROCESS, 0, 0));
	check_audit(fds, pcregex, pipefd);
}

ATF_TC_CLEANUP(setpriority_success, tc)
{
	cleanup();
}


ATF_TC_WITH_CLEANUP(setpriority_failure);
ATF_TC_HEAD(setpriority_failure, tc)
{
	atf_tc_set_md_var(tc, "descr", "Tests the audit of an unsuccessful "
					"setpriority(2) call");
}

ATF_TC_BODY(setpriority_failure, tc)
{
	pid = getpid();
	snprintf(pcregex, sizeof(pcregex), "setpriority.*%d.*failure", pid);

	FILE *pipefd = setup(fds, auclass);
	ATF_REQUIRE_EQ(-1, setpriority(-1, -1, -1));
	check_audit(fds, pcregex, pipefd);
}

ATF_TC_CLEANUP(setpriority_failure, tc)
{
	cleanup();
}


ATF_TC_WITH_CLEANUP(setgroups_success);
ATF_TC_HEAD(setgroups_success, tc)
{
	atf_tc_set_md_var(tc, "descr", "Tests the audit of a successful "
					"setgroups(2) call");
}

ATF_TC_BODY(setgroups_success, tc)
{
	gid_t gids[5];
	pid = getpid();
	snprintf(pcregex, sizeof(pcregex), "setgroups.*%d.*ret.*success", pid);
	/* Retrieve the current group access list to be used with setgroups */
	ATF_REQUIRE(getgroups(sizeof(gids)/sizeof(gids[0]), gids) != -1);

	FILE *pipefd = setup(fds, auclass);
	ATF_REQUIRE_EQ(0, setgroups(sizeof(gids)/sizeof(gids[0]), gids));
	check_audit(fds, pcregex, pipefd);
}

ATF_TC_CLEANUP(setgroups_success, tc)
{
	cleanup();
}


ATF_TC_WITH_CLEANUP(setgroups_failure);
ATF_TC_HEAD(setgroups_failure, tc)
{
	atf_tc_set_md_var(tc, "descr", "Tests the audit of an unsuccessful "
					"setgroups(2) call");
}

ATF_TC_BODY(setgroups_failure, tc)
{
	pid = getpid();
	snprintf(pcregex, sizeof(pcregex), "setgroups.*%d.*ret.*failure", pid);

	FILE *pipefd = setup(fds, auclass);
	ATF_REQUIRE_EQ(-1, setgroups(-1, NULL));
	check_audit(fds, pcregex, pipefd);
}

ATF_TC_CLEANUP(setgroups_failure, tc)
{
	cleanup();
}


ATF_TC_WITH_CLEANUP(setpgrp_success);
ATF_TC_HEAD(setpgrp_success, tc)
{
	atf_tc_set_md_var(tc, "descr", "Tests the audit of a successful "
					"setpgrp(2) call");
}

ATF_TC_BODY(setpgrp_success, tc)
{
	/* Main procedure is carried out from within the child process */
	ATF_REQUIRE((pid = fork()) != -1);
	if (pid) {
		ATF_REQUIRE(wait(&status) != -1);
	} else {
		pid = getpid();
		snprintf(pcregex, sizeof(pcregex), "setpgrp.*%d.*success", pid);

		FILE *pipefd = setup(fds, auclass);
		ATF_REQUIRE_EQ(0, setpgrp(0, 0));
		check_audit(fds, pcregex, pipefd);
	}
}

ATF_TC_CLEANUP(setpgrp_success, tc)
{
	cleanup();
}


ATF_TC_WITH_CLEANUP(setpgrp_failure);
ATF_TC_HEAD(setpgrp_failure, tc)
{
	atf_tc_set_md_var(tc, "descr", "Tests the audit of an unsuccessful "
					"setpgrp(2) call");
}

ATF_TC_BODY(setpgrp_failure, tc)
{
	pid = getpid();
	snprintf(pcregex, sizeof(pcregex), "setpgrp.*%d.*return,failure", pid);

	FILE *pipefd = setup(fds, auclass);
	ATF_REQUIRE_EQ(-1, setpgrp(-1, -1));
	check_audit(fds, pcregex, pipefd);
}

ATF_TC_CLEANUP(setpgrp_failure, tc)
{
	cleanup();
}


ATF_TC_WITH_CLEANUP(setsid_success);
ATF_TC_HEAD(setsid_success, tc)
{
	atf_tc_set_md_var(tc, "descr", "Tests the audit of a successful "
					"setsid(2) call");
}

ATF_TC_BODY(setsid_success, tc)
{
	/* Main procedure is carried out from within the child process */
	ATF_REQUIRE((pid = fork()) != -1);
	if (pid) {
		ATF_REQUIRE(wait(&status) != -1);
	} else {
		pid = getpid();
		snprintf(pcregex, sizeof(pcregex), "setsid.*%d.*success", pid);

		FILE *pipefd = setup(fds, auclass);
		ATF_REQUIRE(setsid() != -1);
		check_audit(fds, pcregex, pipefd);
	}
}

ATF_TC_CLEANUP(setsid_success, tc)
{
	cleanup();
}


ATF_TC_WITH_CLEANUP(setsid_failure);
ATF_TC_HEAD(setsid_failure, tc)
{
	atf_tc_set_md_var(tc, "descr", "Tests the audit of an unsuccessful "
					"setsid(2) call");
}

ATF_TC_BODY(setsid_failure, tc)
{
	pid = getpid();
	snprintf(pcregex, sizeof(pcregex), "setsid.*%d.*return,failure", pid);

	/*
	 * Here, we are intentionally ignoring the output of the setsid()
	 * call because it may or may not be a process leader already. But it
	 * ensures that the next invocation of setsid() will definitely fail.
	 */
	setsid();
	FILE *pipefd = setup(fds, auclass);
	/*
	 * Failure reason: [EPERM] Creating a new session is not permitted
	 * as the PID of calling process matches the PGID of a process group
	 * created by premature setsid() call.
	 */
	ATF_REQUIRE_EQ(-1, setsid());
	check_audit(fds, pcregex, pipefd);
}

ATF_TC_CLEANUP(setsid_failure, tc)
{
	cleanup();
}


ATF_TC_WITH_CLEANUP(setrlimit_success);
ATF_TC_HEAD(setrlimit_success, tc)
{
	atf_tc_set_md_var(tc, "descr", "Tests the audit of a successful "
					"setrlimit(2) call");
}

ATF_TC_BODY(setrlimit_success, tc)
{
	struct rlimit rlp;
	pid = getpid();
	snprintf(pcregex, sizeof(pcregex), "setrlimit.*%d.*ret.*success", pid);
	/* Retrieve the system resource consumption limit to be used later on */
	ATF_REQUIRE_EQ(0, getrlimit(RLIMIT_FSIZE, &rlp));

	FILE *pipefd = setup(fds, auclass);
	ATF_REQUIRE_EQ(0, setrlimit(RLIMIT_FSIZE, &rlp));
	check_audit(fds, pcregex, pipefd);
}

ATF_TC_CLEANUP(setrlimit_success, tc)
{
	cleanup();
}


ATF_TC_WITH_CLEANUP(setrlimit_failure);
ATF_TC_HEAD(setrlimit_failure, tc)
{
	atf_tc_set_md_var(tc, "descr", "Tests the audit of an unsuccessful "
					"setrlimit(2) call");
}

ATF_TC_BODY(setrlimit_failure, tc)
{
	pid = getpid();
	snprintf(pcregex, sizeof(pcregex), "setrlimit.*%d.*ret.*failure", pid);

	FILE *pipefd = setup(fds, auclass);
	ATF_REQUIRE_EQ(-1, setrlimit(RLIMIT_FSIZE, NULL));
	check_audit(fds, pcregex, pipefd);
}

ATF_TC_CLEANUP(setrlimit_failure, tc)
{
	cleanup();
}


ATF_TC_WITH_CLEANUP(mlock_success);
ATF_TC_HEAD(mlock_success, tc)
{
	atf_tc_set_md_var(tc, "descr", "Tests the audit of a successful "
					"mlock(2) call");
}

ATF_TC_BODY(mlock_success, tc)
{
	pid = getpid();
	snprintf(pcregex, sizeof(pcregex), "mlock.*%d.*return,success", pid);

	FILE *pipefd = setup(fds, auclass);
	ATF_REQUIRE_EQ(0, mlock(NULL, 0));
	check_audit(fds, pcregex, pipefd);
}

ATF_TC_CLEANUP(mlock_success, tc)
{
	cleanup();
}


ATF_TC_WITH_CLEANUP(mlock_failure);
ATF_TC_HEAD(mlock_failure, tc)
{
	atf_tc_set_md_var(tc, "descr", "Tests the audit of an unsuccessful "
					"mlock(2) call");
}

ATF_TC_BODY(mlock_failure, tc)
{
	pid = getpid();
	snprintf(pcregex, sizeof(pcregex), "mlock.*%d.*return,failure", pid);

	FILE *pipefd = setup(fds, auclass);
	ATF_REQUIRE_EQ(-1, mlock((void *)(-1), -1));
	check_audit(fds, pcregex, pipefd);
}

ATF_TC_CLEANUP(mlock_failure, tc)
{
	cleanup();
}


ATF_TC_WITH_CLEANUP(munlock_success);
ATF_TC_HEAD(munlock_success, tc)
{
	atf_tc_set_md_var(tc, "descr", "Tests the audit of a successful "
					"munlock(2) call");
}

ATF_TC_BODY(munlock_success, tc)
{
	pid = getpid();
	snprintf(pcregex, sizeof(pcregex), "munlock.*%d.*return,success", pid);

	FILE *pipefd = setup(fds, auclass);
	ATF_REQUIRE_EQ(0, munlock(NULL, 0));
	check_audit(fds, pcregex, pipefd);
}

ATF_TC_CLEANUP(munlock_success, tc)
{
	cleanup();
}


ATF_TC_WITH_CLEANUP(munlock_failure);
ATF_TC_HEAD(munlock_failure, tc)
{
	atf_tc_set_md_var(tc, "descr", "Tests the audit of an unsuccessful "
					"munlock(2) call");
}

ATF_TC_BODY(munlock_failure, tc)
{
	pid = getpid();
	snprintf(pcregex, sizeof(pcregex), "munlock.*%d.*return,failure", pid);

	FILE *pipefd = setup(fds, auclass);
	ATF_REQUIRE_EQ(-1, munlock((void *)(-1), -1));
	check_audit(fds, pcregex, pipefd);
}

ATF_TC_CLEANUP(munlock_failure, tc)
{
	cleanup();
}


ATF_TC_WITH_CLEANUP(minherit_success);
ATF_TC_HEAD(minherit_success, tc)
{
	atf_tc_set_md_var(tc, "descr", "Tests the audit of a successful "
					"minherit(2) call");
}

ATF_TC_BODY(minherit_success, tc)
{
	pid = getpid();
	snprintf(pcregex, sizeof(pcregex), "minherit.*%d.*return,success", pid);

	FILE *pipefd = setup(fds, auclass);
	ATF_REQUIRE_EQ(0, minherit(NULL, 0, INHERIT_ZERO));
	check_audit(fds, pcregex, pipefd);
}

ATF_TC_CLEANUP(minherit_success, tc)
{
	cleanup();
}


ATF_TC_WITH_CLEANUP(minherit_failure);
ATF_TC_HEAD(minherit_failure, tc)
{
	atf_tc_set_md_var(tc, "descr", "Tests the audit of an unsuccessful "
					"minherit(2) call");
}

ATF_TC_BODY(minherit_failure, tc)
{
	pid = getpid();
	snprintf(pcregex, sizeof(pcregex), "minherit.*%d.*return,failure", pid);

	FILE *pipefd = setup(fds, auclass);
	ATF_REQUIRE_EQ(-1, minherit((void *)(-1), -1, 0));
	check_audit(fds, pcregex, pipefd);
}

ATF_TC_CLEANUP(minherit_failure, tc)
{
	cleanup();
}


ATF_TC_WITH_CLEANUP(setlogin_success);
ATF_TC_HEAD(setlogin_success, tc)
{
	atf_tc_set_md_var(tc, "descr", "Tests the audit of a successful "
					"setlogin(2) call");
}

ATF_TC_BODY(setlogin_success, tc)
{
	char *name;
	pid = getpid();
	snprintf(pcregex, sizeof(pcregex), "setlogin.*%d.*return,success", pid);

	/* Retrieve the current user's login name to be used with setlogin(2) */
	ATF_REQUIRE((name = getlogin()) != NULL);
	FILE *pipefd = setup(fds, auclass);
	ATF_REQUIRE_EQ(0, setlogin(name));
	check_audit(fds, pcregex, pipefd);
}

ATF_TC_CLEANUP(setlogin_success, tc)
{
	cleanup();
}


ATF_TC_WITH_CLEANUP(setlogin_failure);
ATF_TC_HEAD(setlogin_failure, tc)
{
	atf_tc_set_md_var(tc, "descr", "Tests the audit of an unsuccessful "
					"setlogin(2) call");
}

ATF_TC_BODY(setlogin_failure, tc)
{
	pid = getpid();
	snprintf(pcregex, sizeof(pcregex), "setlogin.*%d.*return,failure", pid);

	FILE *pipefd = setup(fds, auclass);
	ATF_REQUIRE_EQ(-1, setlogin(NULL));
	check_audit(fds, pcregex, pipefd);
}

ATF_TC_CLEANUP(setlogin_failure, tc)
{
	cleanup();
}


ATF_TC_WITH_CLEANUP(rtprio_success);
ATF_TC_HEAD(rtprio_success, tc)
{
	atf_tc_set_md_var(tc, "descr", "Tests the audit of a successful "
					"rtprio(2) call");
}

ATF_TC_BODY(rtprio_success, tc)
{
	struct rtprio rtp;
	pid = getpid();
	snprintf(pcregex, sizeof(pcregex), "rtprio.*%d.*return,success", pid);

	FILE *pipefd = setup(fds, auclass);
	ATF_REQUIRE_EQ(0, rtprio(RTP_LOOKUP, 0, &rtp));
	check_audit(fds, pcregex, pipefd);
}

ATF_TC_CLEANUP(rtprio_success, tc)
{
	cleanup();
}


ATF_TC_WITH_CLEANUP(rtprio_failure);
ATF_TC_HEAD(rtprio_failure, tc)
{
	atf_tc_set_md_var(tc, "descr", "Tests the audit of an unsuccessful "
					"rtprio(2) call");
}

ATF_TC_BODY(rtprio_failure, tc)
{
	pid = getpid();
	snprintf(pcregex, sizeof(pcregex), "rtprio.*%d.*return,failure", pid);

	FILE *pipefd = setup(fds, auclass);
	ATF_REQUIRE_EQ(-1, rtprio(-1, -1, NULL));
	check_audit(fds, pcregex, pipefd);
}

ATF_TC_CLEANUP(rtprio_failure, tc)
{
	cleanup();
}


ATF_TC_WITH_CLEANUP(profil_success);
ATF_TC_HEAD(profil_success, tc)
{
	atf_tc_set_md_var(tc, "descr", "Tests the audit of a successful "
					"profil(2) call");
}

ATF_TC_BODY(profil_success, tc)
{
	pid = getpid();
	snprintf(pcregex, sizeof(pcregex), "profil.*%d.*return,success", pid);

	char samples[20];
	FILE *pipefd = setup(fds, auclass);
	/* Set scale argument as 0 to disable profiling of current process */
	ATF_REQUIRE_EQ(0, profil(samples, sizeof(samples), 0, 0));
	check_audit(fds, pcregex, pipefd);
}

ATF_TC_CLEANUP(profil_success, tc)
{
	cleanup();
}


ATF_TC_WITH_CLEANUP(profil_failure);
ATF_TC_HEAD(profil_failure, tc)
{
	atf_tc_set_md_var(tc, "descr", "Tests the audit of an unsuccessful "
					"profil(2) call");
}

ATF_TC_BODY(profil_failure, tc)
{
	pid = getpid();
	snprintf(pcregex, sizeof(pcregex), "profil.*%d.*return,failure", pid);

	FILE *pipefd = setup(fds, auclass);
	ATF_REQUIRE_EQ(-1, profil((char *)(SIZE_MAX), -1, -1, -1));
	check_audit(fds, pcregex, pipefd);
}

ATF_TC_CLEANUP(profil_failure, tc)
{
	cleanup();
}


ATF_TC_WITH_CLEANUP(ptrace_success);
ATF_TC_HEAD(ptrace_success, tc)
{
	atf_tc_set_md_var(tc, "descr", "Tests the audit of a successful "
					"ptrace(2) call");
}

ATF_TC_BODY(ptrace_success, tc)
{
	pid = getpid();
	snprintf(pcregex, sizeof(pcregex), "ptrace.*%d.*return,success", pid);

	FILE *pipefd = setup(fds, auclass);
	ATF_REQUIRE_EQ(0, ptrace(PT_TRACE_ME, 0, NULL, 0));
	check_audit(fds, pcregex, pipefd);
}

ATF_TC_CLEANUP(ptrace_success, tc)
{
	cleanup();
}


ATF_TC_WITH_CLEANUP(ptrace_failure);
ATF_TC_HEAD(ptrace_failure, tc)
{
	atf_tc_set_md_var(tc, "descr", "Tests the audit of an unsuccessful "
					"ptrace(2) call");
}

ATF_TC_BODY(ptrace_failure, tc)
{
	pid = getpid();
	snprintf(pcregex, sizeof(pcregex), "ptrace.*%d.*return,failure", pid);

	FILE *pipefd = setup(fds, auclass);
	ATF_REQUIRE_EQ(-1, ptrace(-1, 0, NULL, 0));
	check_audit(fds, pcregex, pipefd);
}

ATF_TC_CLEANUP(ptrace_failure, tc)
{
	cleanup();
}


ATF_TC_WITH_CLEANUP(ktrace_success);
ATF_TC_HEAD(ktrace_success, tc)
{
	atf_tc_set_md_var(tc, "descr", "Tests the audit of a successful "
					"ktrace(2) call");
}

ATF_TC_BODY(ktrace_success, tc)
{
	pid = getpid();
	snprintf(pcregex, sizeof(pcregex), "ktrace.*%d.*return,success", pid);

	FILE *pipefd = setup(fds, auclass);
	ATF_REQUIRE_EQ(0, ktrace(NULL, KTROP_CLEAR, KTRFAC_SYSCALL, pid));
	check_audit(fds, pcregex, pipefd);
}

ATF_TC_CLEANUP(ktrace_success, tc)
{
	cleanup();
}


ATF_TC_WITH_CLEANUP(ktrace_failure);
ATF_TC_HEAD(ktrace_failure, tc)
{
	atf_tc_set_md_var(tc, "descr", "Tests the audit of an unsuccessful "
					"ktrace(2) call");
}

ATF_TC_BODY(ktrace_failure, tc)
{
	pid = getpid();
	snprintf(pcregex, sizeof(pcregex), "ktrace.*%d.*return,failure", pid);

	FILE *pipefd = setup(fds, auclass);
	ATF_REQUIRE_EQ(-1, ktrace(NULL, -1, -1, 0));
	check_audit(fds, pcregex, pipefd);
}

ATF_TC_CLEANUP(ktrace_failure, tc)
{
	cleanup();
}


ATF_TC_WITH_CLEANUP(procctl_success);
ATF_TC_HEAD(procctl_success, tc)
{
	atf_tc_set_md_var(tc, "descr", "Tests the audit of a successful "
					"procctl(2) call");
}

ATF_TC_BODY(procctl_success, tc)
{
	pid = getpid();
	snprintf(pcregex, sizeof(pcregex), "procctl.*%d.*return,success", pid);

	struct procctl_reaper_status reapstat;
	FILE *pipefd = setup(fds, auclass);
	/* Retrieve information about the reaper of current process (pid) */
	ATF_REQUIRE_EQ(0, procctl(P_PID, pid, PROC_REAP_STATUS, &reapstat));
	check_audit(fds, pcregex, pipefd);
}

ATF_TC_CLEANUP(procctl_success, tc)
{
	cleanup();
}


ATF_TC_WITH_CLEANUP(procctl_failure);
ATF_TC_HEAD(procctl_failure, tc)
{
	atf_tc_set_md_var(tc, "descr", "Tests the audit of an unsuccessful "
					"procctl(2) call");
}

ATF_TC_BODY(procctl_failure, tc)
{
	pid = getpid();
	snprintf(pcregex, sizeof(pcregex), "procctl.*%d.*return,failure", pid);

	FILE *pipefd = setup(fds, auclass);
	ATF_REQUIRE_EQ(-1, procctl(-1, -1, -1, NULL));
	check_audit(fds, pcregex, pipefd);
}

ATF_TC_CLEANUP(procctl_failure, tc)
{
	cleanup();
}


ATF_TC_WITH_CLEANUP(cap_enter_success);
ATF_TC_HEAD(cap_enter_success, tc)
{
	atf_tc_set_md_var(tc, "descr", "Tests the audit of a successful "
					"cap_enter(2) call");
}

ATF_TC_BODY(cap_enter_success, tc)
{
	int capinfo;
	size_t len = sizeof(capinfo);
	const char *capname = "kern.features.security_capability_mode";
	ATF_REQUIRE_EQ(0, sysctlbyname(capname, &capinfo, &len, NULL, 0));

	/* Without CAPABILITY_MODE enabled, cap_enter() returns ENOSYS */
	if (!capinfo)
		atf_tc_skip("Capsicum is not enabled in the system");

	FILE *pipefd = setup(fds, auclass);
	ATF_REQUIRE((pid = fork()) != -1);
	if (pid) {
		snprintf(pcregex, sizeof(pcregex),
			"cap_enter.*%d.*return,success", pid);
		ATF_REQUIRE(wait(&status) != -1);
		check_audit(fds, pcregex, pipefd);
	}
	else {
		ATF_REQUIRE_EQ(0, cap_enter());
		_exit(0);
	}
}

ATF_TC_CLEANUP(cap_enter_success, tc)
{
	cleanup();
}


ATF_TC_WITH_CLEANUP(cap_getmode_success);
ATF_TC_HEAD(cap_getmode_success, tc)
{
	atf_tc_set_md_var(tc, "descr", "Tests the audit of a successful "
					"cap_getmode(2) call");
}

ATF_TC_BODY(cap_getmode_success, tc)
{
	int capinfo, modep;
	size_t len = sizeof(capinfo);
	const char *capname = "kern.features.security_capability_mode";
	ATF_REQUIRE_EQ(0, sysctlbyname(capname, &capinfo, &len, NULL, 0));

	/* Without CAPABILITY_MODE enabled, cap_getmode() returns ENOSYS */
	if (!capinfo)
		atf_tc_skip("Capsicum is not enabled in the system");

	pid = getpid();
	snprintf(pcregex, sizeof(pcregex), "cap_getmode.*%d.*success", pid);

	FILE *pipefd = setup(fds, auclass);
	ATF_REQUIRE_EQ(0, cap_getmode(&modep));
	check_audit(fds, pcregex, pipefd);
}

ATF_TC_CLEANUP(cap_getmode_success, tc)
{
	cleanup();
}


ATF_TC_WITH_CLEANUP(cap_getmode_failure);
ATF_TC_HEAD(cap_getmode_failure, tc)
{
	atf_tc_set_md_var(tc, "descr", "Tests the audit of an unsuccessful "
					"cap_getmode(2) call");
}

ATF_TC_BODY(cap_getmode_failure, tc)
{
	pid = getpid();
	snprintf(pcregex, sizeof(pcregex), "cap_getmode.*%d.*failure", pid);

	FILE *pipefd = setup(fds, auclass);
	/* cap_getmode(2) can either fail with EFAULT or ENOSYS */
	ATF_REQUIRE_EQ(-1, cap_getmode(NULL));
	check_audit(fds, pcregex, pipefd);
}

ATF_TC_CLEANUP(cap_getmode_failure, tc)
{
	cleanup();
}


ATF_TP_ADD_TCS(tp)
{
	ATF_TP_ADD_TC(tp, fork_success);
	ATF_TP_ADD_TC(tp, _exit_success);
	ATF_TP_ADD_TC(tp, rfork_success);
	ATF_TP_ADD_TC(tp, rfork_failure);

	ATF_TP_ADD_TC(tp, wait4_success);
	ATF_TP_ADD_TC(tp, wait4_failure);
	ATF_TP_ADD_TC(tp, wait6_success);
	ATF_TP_ADD_TC(tp, wait6_failure);
	ATF_TP_ADD_TC(tp, kill_success);
	ATF_TP_ADD_TC(tp, kill_failure);

	ATF_TP_ADD_TC(tp, chdir_success);
	ATF_TP_ADD_TC(tp, chdir_failure);
	ATF_TP_ADD_TC(tp, fchdir_success);
	ATF_TP_ADD_TC(tp, fchdir_failure);
	ATF_TP_ADD_TC(tp, chroot_success);
	ATF_TP_ADD_TC(tp, chroot_failure);

	ATF_TP_ADD_TC(tp, umask_success);
	ATF_TP_ADD_TC(tp, setuid_success);
	ATF_TP_ADD_TC(tp, seteuid_success);
	ATF_TP_ADD_TC(tp, setgid_success);
	ATF_TP_ADD_TC(tp, setegid_success);

	ATF_TP_ADD_TC(tp, setreuid_success);
	ATF_TP_ADD_TC(tp, setregid_success);
	ATF_TP_ADD_TC(tp, setresuid_success);
	ATF_TP_ADD_TC(tp, setresgid_success);

	ATF_TP_ADD_TC(tp, getresuid_success);
	ATF_TP_ADD_TC(tp, getresuid_failure);
	ATF_TP_ADD_TC(tp, getresgid_success);
	ATF_TP_ADD_TC(tp, getresgid_failure);

	ATF_TP_ADD_TC(tp, setpriority_success);
	ATF_TP_ADD_TC(tp, setpriority_failure);
	ATF_TP_ADD_TC(tp, setgroups_success);
	ATF_TP_ADD_TC(tp, setgroups_failure);
	ATF_TP_ADD_TC(tp, setpgrp_success);
	ATF_TP_ADD_TC(tp, setpgrp_failure);
	ATF_TP_ADD_TC(tp, setsid_success);
	ATF_TP_ADD_TC(tp, setsid_failure);
	ATF_TP_ADD_TC(tp, setrlimit_success);
	ATF_TP_ADD_TC(tp, setrlimit_failure);

	ATF_TP_ADD_TC(tp, mlock_success);
	ATF_TP_ADD_TC(tp, mlock_failure);
	ATF_TP_ADD_TC(tp, munlock_success);
	ATF_TP_ADD_TC(tp, munlock_failure);
	ATF_TP_ADD_TC(tp, minherit_success);
	ATF_TP_ADD_TC(tp, minherit_failure);

	ATF_TP_ADD_TC(tp, setlogin_success);
	ATF_TP_ADD_TC(tp, setlogin_failure);
	ATF_TP_ADD_TC(tp, rtprio_success);
	ATF_TP_ADD_TC(tp, rtprio_failure);

	ATF_TP_ADD_TC(tp, profil_success);
	ATF_TP_ADD_TC(tp, profil_failure);
	ATF_TP_ADD_TC(tp, ptrace_success);
	ATF_TP_ADD_TC(tp, ptrace_failure);
	ATF_TP_ADD_TC(tp, ktrace_success);
	ATF_TP_ADD_TC(tp, ktrace_failure);
	ATF_TP_ADD_TC(tp, procctl_success);
	ATF_TP_ADD_TC(tp, procctl_failure);

	ATF_TP_ADD_TC(tp, cap_enter_success);
	ATF_TP_ADD_TC(tp, cap_getmode_success);
	ATF_TP_ADD_TC(tp, cap_getmode_failure);

	return (atf_no_error());
}
