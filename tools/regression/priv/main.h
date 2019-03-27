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

#define	UID_ROOT	0
#define	UID_OWNER	100
#define	UID_OTHER	200
#define	UID_THIRD	300

#define	GID_WHEEL	0
#define	GID_OWNER	100
#define	GID_OTHER	200

#define	KENV_VAR_NAME	"test"
#define	KENV_VAR_VALUE	"test"
#define	KENV_VAR_LEN	sizeof(KENV_VAR_VALUE)

/*
 * Library routines used by many tests.
 */
void	setup_dir(const char *test, char *dpathp, uid_t uid, gid_t gid,
	    mode_t mode);
void	setup_file(const char *test, char *fpathp, uid_t uid, gid_t gid,
	    mode_t mode);
void	expect(const char *test, int error, int expected_error,
	    int expected_errno);

/*
 * Definition for a particular test, both used to manage the test list in
 * main.c, and passed to tests so they can be aware of which specific test is
 * running if particular method implementations are shared across tests.
 */
struct test {
	const char	*t_name;
	int		(*t_setup_func)(int asroot, int injail,
			    struct test *test);
	void		(*t_test_func)(int asroot, int injail,
			    struct test *test);
	void		(*t_cleanup_func)(int asroot, int injail,
			    struct test *test);
};

/*
 * Prototypes for test functions that will be hooked up to the test vector in
 * main.c.  It's possible to imagine more dynamic (convenient?) ways to do
 * this.
 */
int	priv_acct_setup(int, int, struct test *);
void	priv_acct_enable(int, int, struct test *);
void	priv_acct_disable(int, int, struct test *);
void	priv_acct_rotate(int, int, struct test *);
void	priv_acct_noopdisable(int, int, struct test *);
void	priv_acct_cleanup(int, int, struct test *);

int	priv_adjtime_setup(int, int, struct test *);
void	priv_adjtime_set(int, int, struct test *);
void	priv_adjtime_cleanup(int, int, struct test *);

int	priv_audit_submit_setup(int, int, struct test *);
void	priv_audit_submit(int, int, struct test *);
void	priv_audit_submit_cleanup(int, int, struct test *);

int	priv_audit_control_setup(int, int, struct test *);
void	priv_audit_control(int, int, struct test *);
void	priv_audit_control_cleanup(int, int, struct test *);

int	priv_audit_getaudit_setup(int, int, struct test *);
void	priv_audit_getaudit(int, int, struct test *);
void	priv_audit_getaudit_addr(int, int, struct test *);
void	priv_audit_getaudit_cleanup(int, int, struct test *);

int	priv_audit_setaudit_setup(int, int, struct test *);
void	priv_audit_setaudit(int, int, struct test *);
void	priv_audit_setaudit_addr(int, int, struct test *);
void	priv_audit_setaudit_cleanup(int, int, struct test *);

int	priv_clock_settime_setup(int, int, struct test *);
void	priv_clock_settime(int, int, struct test *);
void	priv_clock_settime_cleanup(int, int, struct test *);

int	priv_cred_setup(int, int, struct test *);
void	priv_cred_setuid(int, int, struct test *);
void	priv_cred_seteuid(int, int, struct test *);
void	priv_cred_setgid(int, int, struct test *);
void	priv_cred_setegid(int, int, struct test *);
void	priv_cred_setgroups(int, int, struct test *);
void	priv_cred_setreuid(int, int, struct test *);
void	priv_cred_setregid(int, int, struct test *);
void	priv_cred_setresuid(int, int, struct test *);
void	priv_cred_setresgid(int, int, struct test *);
void	priv_cred_cleanup(int, int, struct test *);

int	priv_io_setup(int, int, struct test *);
void	priv_io(int, int, struct test *);
void	priv_io_cleanup(int, int, struct test *);

int	priv_kenv_set_setup(int, int, struct test *);
void	priv_kenv_set(int, int, struct test *);
void	priv_kenv_set_cleanup(int, int, struct test *);

int	priv_kenv_unset_setup(int, int, struct test *);
void	priv_kenv_unset(int, int, struct test *);
void	priv_kenv_unset_cleanup(int, int, struct test *);

int	priv_msgbuf_privonly_setup(int, int, struct test *);
void	priv_msgbuf_privonly(int, int, struct test *);

int	priv_msgbuf_unprivok_setup(int, int, struct test *);
void	priv_msgbuf_unprivok(int, int, struct test *);

void	priv_msgbuf_cleanup(int, int, struct test *);

void	priv_netinet_ipsec_pfkey(int, int, struct test *);
int	priv_netinet_ipsec_policy4_bypass_setup(int, int, struct test *);
void	priv_netinet_ipsec_policy4_bypass(int, int, struct test *);
int	priv_netinet_ipsec_policy6_bypass_setup(int, int, struct test *);
void	priv_netinet_ipsec_policy6_bypass(int, int, struct test *);
void	priv_netinet_ipsec_policy_bypass_cleanup(int, int, struct test *);
int	priv_netinet_ipsec_policy4_entrust_setup(int, int, struct test *);
void	priv_netinet_ipsec_policy4_entrust(int, int, struct test *);
int	priv_netinet_ipsec_policy6_entrust_setup(int, int, struct test *);
void	priv_netinet_ipsec_policy6_entrust(int, int, struct test *);
void	priv_netinet_ipsec_policy_entrust_cleanup(int, int, struct test *);

int	priv_netinet_raw_setup(int, int, struct test *);
void	priv_netinet_raw(int, int, struct test *);
void	priv_netinet_raw_cleanup(int, int, struct test *);

int	priv_proc_setlogin_setup(int, int, struct test *);
void	priv_proc_setlogin(int, int, struct test *);
void	priv_proc_setlogin_cleanup(int, int, struct test *);

int	priv_proc_setrlimit_setup(int, int, struct test *);
void	priv_proc_setrlimit_raisemax(int, int, struct test *);
void	priv_proc_setrlimit_raisecur(int, int, struct test *);
void	priv_proc_setrlimit_raisecur_nopriv(int, int, struct test *);
void	priv_proc_setrlimit_cleanup(int, int, struct test *);

int	priv_sched_rtprio_setup(int, int, struct test *);
void	priv_sched_rtprio_curproc_normal(int, int, struct test *);
void	priv_sched_rtprio_curproc_idle(int, int, struct test *);
void	priv_sched_rtprio_curproc_realtime(int, int, struct test *);

void	priv_sched_rtprio_myproc_normal(int, int, struct test *);
void	priv_sched_rtprio_myproc_idle(int, int, struct test *);
void	priv_sched_rtprio_myproc_realtime(int, int, struct test *);

void	priv_sched_rtprio_aproc_normal(int, int, struct test *);
void	priv_sched_rtprio_aproc_idle(int, int, struct test *);
void	priv_sched_rtprio_aproc_realtime(int, int, struct test *);
void	priv_sched_rtprio_cleanup(int, int, struct test *);

int	priv_sched_setpriority_setup(int, int, struct test *);
void	priv_sched_setpriority_curproc(int, int, struct test *);
void	priv_sched_setpriority_myproc(int, int, struct test *);
void	priv_sched_setpriority_aproc(int, int, struct test *);
void	priv_sched_setpriority_cleanup(int, int, struct test *);

int	priv_settimeofday_setup(int, int, struct test *);
void	priv_settimeofday(int, int, struct test *);
void	priv_settimeofday_cleanup(int, int, struct test *);

int	priv_sysctl_write_setup(int, int, struct test *);
void	priv_sysctl_write(int, int, struct test *);
void	priv_sysctl_writejail(int, int, struct test *);
void	priv_sysctl_write_cleanup(int, int, struct test *);

int	priv_vfs_chflags_froot_setup(int, int, struct test *);
void	priv_vfs_chflags_froot_uflags(int, int, struct test *);
void	priv_vfs_chflags_froot_sflags(int, int, struct test *);

int	priv_vfs_chflags_fowner_setup(int, int, struct test *);
void	priv_vfs_chflags_fowner_uflags(int, int, struct test *);
void	priv_vfs_chflags_fowner_sflags(int, int, struct test *);

int	priv_vfs_chflags_fother_setup(int, int, struct test *);
void	priv_vfs_chflags_fother_uflags(int, int, struct test *);
void	priv_vfs_chflags_fother_sflags(int, int, struct test *);

void	priv_vfs_chflags_cleanup(int, int, struct test *);

int	priv_vfs_chmod_froot_setup(int, int, struct test *);
void	priv_vfs_chmod_froot(int, int, struct test *);

int	priv_vfs_chmod_fowner_setup(int, int, struct test *);
void	priv_vfs_chmod_fowner(int, int, struct test *);

int	priv_vfs_chmod_fother_setup(int, int, struct test *);
void	priv_vfs_chmod_fother(int, int, struct test *);

void	priv_vfs_chmod_cleanup(int, int, struct test *);

int	priv_vfs_chown_uid_setup(int, int, struct test *);
void	priv_vfs_chown_uid(int, int, struct test *);

int	priv_vfs_chown_mygid_setup(int, int, struct test *);
void	priv_vfs_chown_mygid(int, int, struct test *);

int	priv_vfs_chown_othergid_setup(int, int, struct test *);
void	priv_vfs_chown_othergid(int, int, struct test *);

void	priv_vfs_chown_cleanup(int, int, struct test *);

int	priv_vfs_chroot_setup(int, int, struct test *);
void	priv_vfs_chroot(int, int, struct test *);
void	priv_vfs_chroot_cleanup(int, int, struct test *);

int	priv_vfs_clearsugid_setup(int, int, struct test *);
void	priv_vfs_clearsugid_chgrp(int, int, struct test *);
void	priv_vfs_clearsugid_extattr(int, int, struct test *);
void	priv_vfs_clearsugid_write(int, int, struct test *);
void	priv_vfs_clearsugid_cleanup(int, int, struct test *);

int	priv_vfs_extattr_system_setup(int, int, struct test *);
void	priv_vfs_extattr_system(int, int, struct test *);
void	priv_vfs_extattr_system_cleanup(int, int, struct test *);

int	priv_vfs_fhopen_setup(int, int, struct test *);
void	priv_vfs_fhopen(int, int, struct test *);
void	priv_vfs_fhopen_cleanup(int, int, struct test *);

int	priv_vfs_fhstat_setup(int, int, struct test *);
void	priv_vfs_fhstat(int, int, struct test *);
void	priv_vfs_fhstat_cleanup(int, int, struct test *);

int	priv_vfs_fhstatfs_setup(int, int, struct test *);
void	priv_vfs_fhstatfs(int, int, struct test *);
void	priv_vfs_fhstatfs_cleanup(int, int, struct test *);

int	priv_vfs_generation_setup(int, int, struct test *);
void	priv_vfs_generation(int, int, struct test *);
void	priv_vfs_generation_cleanup(int, int, struct test *);

int	priv_vfs_getfh_setup(int, int, struct test *);
void	priv_vfs_getfh(int, int, struct test *);
void	priv_vfs_getfh_cleanup(int, int, struct test *);

int	priv_vfs_readwrite_fowner_setup(int, int, struct test *);
void	priv_vfs_readwrite_fowner(int, int, struct test *);

int	priv_vfs_readwrite_fgroup_setup(int, int, struct test *);
void	priv_vfs_readwrite_fgroup(int, int, struct test *);

int	priv_vfs_readwrite_fother_setup(int, int, struct test *);
void	priv_vfs_readwrite_fother(int, int, struct test *);

void	priv_vfs_readwrite_cleanup(int, int, struct test *);

int	priv_vfs_setgid_fowner_setup(int, int, struct test *);
void	priv_vfs_setgid_fowner(int, int, struct test *);

int	priv_vfs_setgid_fother_setup(int, int, struct test *);
void	priv_vfs_setgid_fother(int, int, struct test *);

void	priv_vfs_setgid_cleanup(int, int, struct test *);

int	priv_vfs_stickyfile_dir_fowner_setup(int, int, struct test *);

void	priv_vfs_stickyfile_dir_fowner(int, int, struct test *);
int	priv_vfs_stickyfile_dir_fother_setup(int, int, struct test *);
void	priv_vfs_stickyfile_dir_fother(int, int, struct test *);

void	priv_vfs_stickyfile_dir_cleanup(int, int, struct test *);

int	priv_vfs_stickyfile_file_fowner_setup(int, int, struct test *);
void	priv_vfs_stickyfile_file_fowner(int, int, struct test *);

int	priv_vfs_stickyfile_file_fother_setup(int, int, struct test *);
void	priv_vfs_stickyfile_file_fother(int, int, struct test *);

void	priv_vfs_stickyfile_file_cleanup(int, int, struct test *);

int	priv_vfs_utimes_froot_setup(int, int, struct test *);
void	priv_vfs_utimes_froot(int, int, struct test *);
void	priv_vfs_utimes_froot_null(int, int, struct test *);

int	priv_vfs_utimes_fowner_setup(int, int, struct test *);
void	priv_vfs_utimes_fowner(int, int, struct test *);
void	priv_vfs_utimes_fowner_null(int, int, struct test *);

int	priv_vfs_utimes_fother_setup(int, int, struct test *);
void	priv_vfs_utimes_fother(int, int, struct test *);
void	priv_vfs_utimes_fother_null(int, int, struct test *);

void	priv_vfs_utimes_cleanup(int, int, struct test *);

int	priv_vm_madv_protect_setup(int, int, struct test *);
void	priv_vm_madv_protect(int, int, struct test *);
void	priv_vm_madv_protect_cleanup(int, int, struct test *);

int	priv_vm_mlock_setup(int, int, struct test *);
void	priv_vm_mlock(int, int, struct test *);
void	priv_vm_mlock_cleanup(int, int, struct test *);

int	priv_vm_munlock_setup(int, int, struct test *);
void	priv_vm_munlock(int, int, struct test *);
void	priv_vm_munlock_cleanup(int, int, struct test *);
