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
 * Privilege test framework.  Each test is encapsulated on a .c file
 * exporting a function that implements the test.  Each test is run from its
 * own child process, and they are run in sequence one at a time.
 */

#include <sys/param.h>
#include <sys/jail.h>
#include <sys/stat.h>
#include <sys/wait.h>

#include <netinet/in.h>

#include <err.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "main.h"

/*
 * If true, some test or preparatory step failed along the execution of this
 * program.
 *
 * Intuitively, we would define a counter instead of a boolean.  However,
 * we fork to run the subtests and keeping proper track of the number of
 * failed tests would be tricky and not provide any real value.
 */
static int something_failed = 0;

/*
 * Registration table of privilege tests.  Each test registers a name, a test
 * function, and a cleanup function to run after the test has completed,
 * regardless of success/failure.
 */
static struct test tests[] = {
	{ "priv_acct_enable", priv_acct_setup, priv_acct_enable,
	    priv_acct_cleanup },

	{ "priv_acct_disable", priv_acct_setup, priv_acct_disable,
	    priv_acct_cleanup },

	{ "priv_acct_rotate", priv_acct_setup, priv_acct_rotate,
	    priv_acct_cleanup },

	{ "priv_acct_noopdisable", priv_acct_setup, priv_acct_noopdisable,
	    priv_acct_cleanup },

	{ "priv_adjtime_set", priv_adjtime_setup, priv_adjtime_set,
	    priv_adjtime_cleanup },

	{ "priv_audit_submit", priv_audit_submit_setup, priv_audit_submit,
	    priv_audit_submit_cleanup },

	{ "priv_audit_control", priv_audit_control_setup, priv_audit_control,
	    priv_audit_control_cleanup },

	{ "priv_audit_getaudit", priv_audit_getaudit_setup,
	    priv_audit_getaudit, priv_audit_getaudit_cleanup },

	{ "priv_audit_getaudit_addr", priv_audit_getaudit_setup,
	    priv_audit_getaudit_addr, priv_audit_getaudit_cleanup },

	{ "priv_audit_setaudit", priv_audit_setaudit_setup,
	    priv_audit_setaudit, priv_audit_setaudit_cleanup },

	{ "priv_audit_setaudit_addr", priv_audit_setaudit_setup,
	    priv_audit_setaudit_addr, priv_audit_setaudit_cleanup },

	{ "priv_clock_settime", priv_clock_settime_setup, priv_clock_settime,
	    priv_clock_settime_cleanup },

	{ "priv_cred_setuid", priv_cred_setup, priv_cred_setuid,
	    priv_cred_cleanup },

	{ "priv_cred_seteuid", priv_cred_setup, priv_cred_seteuid,
	    priv_cred_cleanup },

	{ "priv_cred_setgid", priv_cred_setup, priv_cred_setgid,
	    priv_cred_cleanup },

	{ "priv_cred_setegid", priv_cred_setup, priv_cred_setegid,
	    priv_cred_cleanup },

	{ "priv_cred_setgroups", priv_cred_setup, priv_cred_setgroups,
	    priv_cred_cleanup },

	{ "priv_cred_setreuid", priv_cred_setup, priv_cred_setreuid,
	    priv_cred_cleanup },

	{ "priv_cred_setregid", priv_cred_setup, priv_cred_setregid,
	    priv_cred_cleanup },

	{ "priv_cred_setresuid", priv_cred_setup, priv_cred_setresuid,
	    priv_cred_cleanup },

	{ "priv_cred_setresgid", priv_cred_setup, priv_cred_setresgid,
	    priv_cred_cleanup },

	{ "priv_io", priv_io_setup, priv_io, priv_io_cleanup },

	{ "priv_kenv_set", priv_kenv_set_setup, priv_kenv_set,
	    priv_kenv_set_cleanup },

	{ "priv_kenv_unset", priv_kenv_unset_setup, priv_kenv_unset,
	    priv_kenv_unset_cleanup },

	{ "priv_msgbuf_privonly", priv_msgbuf_privonly_setup,
	    priv_msgbuf_privonly, priv_msgbuf_cleanup },

	{ "priv_msgbuf_unprivok", priv_msgbuf_unprivok_setup,
	   priv_msgbuf_unprivok, priv_msgbuf_cleanup },

	{ "priv_netinet_ipsec_pfkey", NULL, priv_netinet_ipsec_pfkey, NULL },

	{ "priv_netinet_ipsec_policy4_bypass",
	    priv_netinet_ipsec_policy4_bypass_setup,
	    priv_netinet_ipsec_policy4_bypass,
	    priv_netinet_ipsec_policy_bypass_cleanup },

#ifdef INET6
	{ "priv_netinet_ipsec_policy6_bypass",
	    priv_netinet_ipsec_policy6_bypass_setup,
	    priv_netinet_ipsec_policy6_bypass,
	    priv_netinet_ipsec_policy_bypass_cleanup },
#endif

	{ "priv_netinet_ipsec_policy4_entrust",
	    priv_netinet_ipsec_policy4_entrust_setup,
	    priv_netinet_ipsec_policy4_entrust,
	    priv_netinet_ipsec_policy_entrust_cleanup },

#ifdef INET6
	{ "priv_netinet_ipsec_policy6_entrust",
	    priv_netinet_ipsec_policy6_entrust_setup,
	    priv_netinet_ipsec_policy6_entrust,
	    priv_netinet_ipsec_policy_entrust_cleanup },
#endif

	{ "priv_netinet_raw", priv_netinet_raw_setup, priv_netinet_raw,
	    priv_netinet_raw_cleanup },

	{ "priv_proc_setlogin", priv_proc_setlogin_setup, priv_proc_setlogin,
	    priv_proc_setlogin_cleanup },

	{ "priv_proc_setrlimit_raisemax", priv_proc_setrlimit_setup,
	    priv_proc_setrlimit_raisemax, priv_proc_setrlimit_cleanup },

	{ "priv_proc_setrlimit_raisecur", priv_proc_setrlimit_setup,
	    priv_proc_setrlimit_raisecur, priv_proc_setrlimit_cleanup },

	{ "priv_proc_setrlimit_raisecur_nopriv", priv_proc_setrlimit_setup,
	    priv_proc_setrlimit_raisecur_nopriv,
	    priv_proc_setrlimit_cleanup },

	{ "priv_sched_rtprio_curproc_normal", priv_sched_rtprio_setup,
	    priv_sched_rtprio_curproc_normal, priv_sched_rtprio_cleanup },

	{ "priv_sched_rtprio_curproc_idle", priv_sched_rtprio_setup,
	    priv_sched_rtprio_curproc_idle, priv_sched_rtprio_cleanup },

	{ "priv_sched_rtprio_curproc_realtime", priv_sched_rtprio_setup,
	    priv_sched_rtprio_curproc_realtime, priv_sched_rtprio_cleanup },

	{ "priv_sched_rtprio_myproc_normal", priv_sched_rtprio_setup,
	    priv_sched_rtprio_myproc_normal, priv_sched_rtprio_cleanup },

	{ "priv_sched_rtprio_myproc_idle", priv_sched_rtprio_setup,
	    priv_sched_rtprio_myproc_idle, priv_sched_rtprio_cleanup },

	{ "priv_sched_rtprio_myproc_realtime", priv_sched_rtprio_setup,
	    priv_sched_rtprio_myproc_realtime, priv_sched_rtprio_cleanup },

	{ "priv_sched_rtprio_aproc_normal", priv_sched_rtprio_setup,
	    priv_sched_rtprio_aproc_normal, priv_sched_rtprio_cleanup },

	{ "priv_sched_rtprio_aproc_idle", priv_sched_rtprio_setup,
	    priv_sched_rtprio_aproc_idle, priv_sched_rtprio_cleanup },

	{ "priv_sched_rtprio_aproc_realtime", priv_sched_rtprio_setup,
	    priv_sched_rtprio_aproc_realtime, priv_sched_rtprio_cleanup },

	{ "priv_sched_setpriority_curproc", priv_sched_setpriority_setup,
	    priv_sched_setpriority_curproc, priv_sched_setpriority_cleanup },

	{ "priv_sched_setpriority_myproc", priv_sched_setpriority_setup,
	    priv_sched_setpriority_myproc, priv_sched_setpriority_cleanup },

	{ "priv_sched_setpriority_aproc", priv_sched_setpriority_setup,
	    priv_sched_setpriority_aproc, priv_sched_setpriority_cleanup },

	{ "priv_settimeofday", priv_settimeofday_setup, priv_settimeofday,
	    priv_settimeofday_cleanup },

	{ "priv_sysctl_write", priv_sysctl_write_setup, priv_sysctl_write,
	    priv_sysctl_write_cleanup },

	{ "priv_sysctl_writejail", priv_sysctl_write_setup,
	    priv_sysctl_writejail, priv_sysctl_write_cleanup },

	{ "priv_vfs_chflags_froot_uflags", priv_vfs_chflags_froot_setup,
	    priv_vfs_chflags_froot_uflags, priv_vfs_chflags_cleanup },

	{ "priv_vfs_chflags_froot_sflags", priv_vfs_chflags_froot_setup,
	    priv_vfs_chflags_froot_sflags, priv_vfs_chflags_cleanup },

	{ "priv_vfs_chflags_fowner_uflags", priv_vfs_chflags_fowner_setup,
	    priv_vfs_chflags_fowner_uflags, priv_vfs_chflags_cleanup },

	{ "priv_vfs_chflags_fowner_sflags", priv_vfs_chflags_fowner_setup,
	    priv_vfs_chflags_fowner_sflags, priv_vfs_chflags_cleanup },

	{ "priv_vfs_chflags_fother_uflags", priv_vfs_chflags_fother_setup,
	    priv_vfs_chflags_fother_uflags, priv_vfs_chflags_cleanup },

	{ "priv_vfs_chflags_fother_sflags", priv_vfs_chflags_fother_setup,
	    priv_vfs_chflags_fother_sflags, priv_vfs_chflags_cleanup },

	{ "priv_vfs_chmod_froot", priv_vfs_chmod_froot_setup,
	     priv_vfs_chmod_froot, priv_vfs_chmod_cleanup },

	{ "priv_vfs_chmod_fowner", priv_vfs_chmod_fowner_setup,
	     priv_vfs_chmod_fowner, priv_vfs_chmod_cleanup },

	{ "priv_vfs_chmod_fother", priv_vfs_chmod_fother_setup,
	     priv_vfs_chmod_fother, priv_vfs_chmod_cleanup },

	{ "priv_vfs_chown_uid", priv_vfs_chown_uid_setup, priv_vfs_chown_uid,
	    priv_vfs_chown_cleanup },

	{ "priv_vfs_chown_mygid", priv_vfs_chown_mygid_setup,
	    priv_vfs_chown_mygid, priv_vfs_chown_cleanup },

	{ "priv_vfs_chown_othergid", priv_vfs_chown_othergid_setup,
	    priv_vfs_chown_othergid, priv_vfs_chown_cleanup },

	{ "priv_vfs_chroot", priv_vfs_chroot_setup, priv_vfs_chroot,
	    priv_vfs_chroot_cleanup },

	{ "priv_vfs_clearsugid_chgrp", priv_vfs_clearsugid_setup,
	    priv_vfs_clearsugid_chgrp, priv_vfs_clearsugid_cleanup },

	{ "priv_vfs_clearsugid_extattr", priv_vfs_clearsugid_setup,
	    priv_vfs_clearsugid_extattr, priv_vfs_clearsugid_cleanup },

	{ "priv_vfs_clearsugid_write", priv_vfs_clearsugid_setup,
	    priv_vfs_clearsugid_write, priv_vfs_clearsugid_cleanup },

	{ "priv_vfs_extattr_system", priv_vfs_extattr_system_setup,
	    priv_vfs_extattr_system, priv_vfs_extattr_system_cleanup },

	{ "priv_vfs_fhopen", priv_vfs_fhopen_setup, priv_vfs_fhopen,
	    priv_vfs_fhopen_cleanup },

	{ "priv_vfs_fhstat", priv_vfs_fhstat_setup, priv_vfs_fhstat,
	    priv_vfs_fhstat_cleanup },

	{ "priv_vfs_fhstatfs", priv_vfs_fhstatfs_setup, priv_vfs_fhstatfs,
	    priv_vfs_fhstatfs_cleanup },

	{ "priv_vfs_generation", priv_vfs_generation_setup,
	    priv_vfs_generation, priv_vfs_generation_cleanup },

	{ "priv_vfs_getfh", priv_vfs_getfh_setup, priv_vfs_getfh,
	    priv_vfs_getfh_cleanup },

	{ "priv_vfs_readwrite_fowner", priv_vfs_readwrite_fowner_setup,
	    priv_vfs_readwrite_fowner, priv_vfs_readwrite_cleanup },

	{ "priv_vfs_readwrite_fgroup", priv_vfs_readwrite_fgroup_setup,
	    priv_vfs_readwrite_fgroup, priv_vfs_readwrite_cleanup },

	{ "priv_vfs_readwrite_fother", priv_vfs_readwrite_fother_setup,
	    priv_vfs_readwrite_fother, priv_vfs_readwrite_cleanup },

	{ "priv_vfs_setgid_fowner", priv_vfs_setgid_fowner_setup,
	    priv_vfs_setgid_fowner, priv_vfs_setgid_cleanup },

	{ "priv_vfs_setgid_fother", priv_vfs_setgid_fother_setup,
	    priv_vfs_setgid_fother, priv_vfs_setgid_cleanup },

	{ "priv_vfs_stickyfile_dir_fowner",
	    priv_vfs_stickyfile_dir_fowner_setup,
	    priv_vfs_stickyfile_dir_fowner,
	    priv_vfs_stickyfile_dir_cleanup },

	{ "priv_vfs_stickyfile_dir_fother",
	    priv_vfs_stickyfile_dir_fother_setup,
	    priv_vfs_stickyfile_dir_fother,
	    priv_vfs_stickyfile_dir_cleanup },

	{ "priv_vfs_stickyfile_file_fowner",
	    priv_vfs_stickyfile_file_fowner_setup,
	    priv_vfs_stickyfile_file_fowner,
	    priv_vfs_stickyfile_file_cleanup },

	{ "priv_vfs_stickyfile_file_fother",
	    priv_vfs_stickyfile_file_fother_setup,
	    priv_vfs_stickyfile_file_fother,
	    priv_vfs_stickyfile_file_cleanup },

	{ "priv_vfs_utimes_froot", priv_vfs_utimes_froot_setup,
	    priv_vfs_utimes_froot, priv_vfs_utimes_cleanup },

	{ "priv_vfs_utimes_froot_null", priv_vfs_utimes_froot_setup,
	    priv_vfs_utimes_froot_null, priv_vfs_utimes_cleanup },

	{ "priv_vfs_utimes_fowner", priv_vfs_utimes_fowner_setup,
	    priv_vfs_utimes_fowner, priv_vfs_utimes_cleanup },

	{ "priv_vfs_utimes_fowner_null", priv_vfs_utimes_fowner_setup,
	    priv_vfs_utimes_fowner_null, priv_vfs_utimes_cleanup },

	{ "priv_vfs_utimes_fother", priv_vfs_utimes_fother_setup,
	    priv_vfs_utimes_fother, priv_vfs_utimes_cleanup },

	{ "priv_vfs_utimes_fother_null", priv_vfs_utimes_fother_setup,
	    priv_vfs_utimes_fother_null, priv_vfs_utimes_cleanup },

	{ "priv_vm_madv_protect", priv_vm_madv_protect_setup,
	    priv_vm_madv_protect, priv_vm_madv_protect_cleanup },

	{ "priv_vm_mlock", priv_vm_mlock_setup, priv_vm_mlock,
	    priv_vm_mlock_cleanup },

	{ "priv_vm_munlock", priv_vm_munlock_setup, priv_vm_munlock,
	    priv_vm_munlock_cleanup },

};
static int tests_count = sizeof(tests) / sizeof(struct test);

void
expect(const char *test, int error, int expected_error, int expected_errno)
{

	if (error == 0) {
		if (expected_error != 0) {
			something_failed = 1;
			warnx("%s: returned 0", test);
		}
	} else {
		if (expected_error == 0) {
			something_failed = 1;
			warn("%s: returned (%d, %d)", test, error, errno);
		} else if (expected_errno != errno) {
			something_failed = 1;
			warn("%s: returned (%d, %d)", test, error, errno);
		}
	}
}

void
setup_dir(const char *test, char *dpathp, uid_t uid, gid_t gid, mode_t mode)
{

	strcpy(dpathp, "/tmp/priv.XXXXXXXXXXX");
	if (mkdtemp(dpathp) == NULL)
		err(-1, "test %s: mkdtemp", test);

	if (chown(dpathp, uid, gid) < 0)
		err(-1, "test %s: chown(%s, %d, %d)", test, dpathp, uid,
		    gid);

	if (chmod(dpathp, mode) < 0)
		err(-1, "test %s: chmod(%s, 0%o)", test, dpathp, mode);
}

void
setup_file(const char *test, char *fpathp, uid_t uid, gid_t gid, mode_t mode)
{
	int fd;

	strcpy(fpathp, "/tmp/priv.XXXXXXXXXXX");
	fd = mkstemp(fpathp);
	if (fd < 0)
		err(-1, "test %s: mkstemp", test);

	if (fchown(fd, uid, gid) < 0)
		err(-1, "test %s: fchown(%s, %d, %d)", test, fpathp, uid,
		    gid);

	if (fchmod(fd, mode) < 0)
		err(-1, "test %s: chmod(%s, 0%o)", test, fpathp, mode);

	close(fd);
}

/*
 * Irrevocably set credentials to specific uid and gid.
 */
static void
set_creds(const char *test, uid_t uid, gid_t gid)
{
	gid_t gids[1] = { gid };

	if (setgid(gid) < 0)
		err(-1, "test %s: setegid(%d)", test, gid);
	if (setgroups(sizeof(gids)/sizeof(gid_t), gids) < 0)
		err(-1, "test %s: setgroups(%d)", test, gid);
	if (setuid(uid) < 0)
		err(-1, "test %s: seteuid(%d)", test, uid);
}

static void
enter_jail(const char *test)
{
	struct jail j;
	struct in_addr ia4;
#ifdef INET6
	struct in6_addr ia6 = IN6ADDR_LOOPBACK_INIT;
#endif

	bzero(&j, sizeof(j));
	j.version = JAIL_API_VERSION;
	j.path = "/";
	j.hostname = "test";
	j.jailname = "regressions/priv";
	ia4.s_addr = htonl(INADDR_LOOPBACK);
	j.ip4s = 1;
	j.ip4 = &ia4;
#ifdef INET6
	j.ip6s = 1;
	j.ip6 = &ia6;
#endif
	if (jail(&j) < 0)
		err(-1, "test %s: jail", test);
}

static void
run_child(struct test *test, int asroot, int injail)
{

	setprogname(test->t_name);
	if (injail)
		enter_jail(test->t_name);
	if (!asroot)
		set_creds(test->t_name, UID_OWNER, GID_OWNER);
	test->t_test_func(asroot, injail, test);
}

/*
 * Run a test in a particular credential context -- always call the setup and
 * cleanup routines; if setup succeeds, also run the test.  Test cleanup must
 * handle cases where the setup has failed, so may need to maintain their own
 * state in order to know what needs cleaning up (such as whether temporary
 * files were created).
 */
static void
run(struct test *test, int asroot, int injail)
{
	pid_t childpid, pid;

	if (test->t_setup_func != NULL) {
		if ((test->t_setup_func)(asroot, injail, test) != 0) {
			warnx("run(%s, %d, %d) setup failed", test->t_name,
			    asroot, injail);
			goto cleanup;
		}
	}
	fflush(stdout);
	fflush(stderr);
	childpid = fork();
	if (childpid == -1) {
		warn("run(%s, %d, %d) fork failed", test->t_name, asroot,
		    injail);
		goto cleanup;
	}
	if (childpid == 0) {
		run_child(test, asroot, injail);
		fflush(stdout);
		fflush(stderr);
		exit(something_failed ? EXIT_FAILURE : EXIT_SUCCESS);
	} else {
		while (1) {
			int status;
			pid = waitpid(childpid, &status, 0);
			if (pid == -1) {
				something_failed = 1;
				warn("test: waitpid %s", test->t_name);
			}
			if (pid == childpid) {
				if (WIFEXITED(status) &&
				    WEXITSTATUS(status) == EXIT_SUCCESS) {
					/* All good in the subprocess! */
				} else {
					something_failed = 1;
				}
				break;
			}
		}
	}
	fflush(stdout);
	fflush(stderr);
cleanup:
	if (test->t_cleanup_func != NULL)
		test->t_cleanup_func(asroot, injail, test);
}

int
main(int argc, char *argv[])
{
	int i;

	/*
	 * This test suite will need to become quite a bit more enlightened
	 * if the notion of privilege is truly separated from root, as tests
	 * make assumptions about when privilege will be present.  In
	 * particular, VFS-related tests need to manage uids in order to
	 * force the use of privilege, and will likely need checking.
	 */
	if (getuid() != 0 && geteuid() != 0)
		errx(-1, "must be run as root");

	/*
	 * Run each test four times, varying whether the process is running
	 * as root and in jail in order to test all possible combinations.
	 */
	for (i = 0; i < tests_count; i++) {
		run(&tests[i], 0, 0);
		run(&tests[i], 0, 1);
		run(&tests[i], 1, 0);
		run(&tests[i], 1, 1);
	}
	return (something_failed ? EXIT_FAILURE : EXIT_SUCCESS);
}
