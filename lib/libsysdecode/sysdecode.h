/*-
 * Copyright (c) 2015 John H. Baldwin <jhb@FreeBSD.org>
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
 *
 * $FreeBSD$
 */

#ifndef __SYSDECODE_H__
#define	__SYSDECODE_H__

enum sysdecode_abi {
	SYSDECODE_ABI_UNKNOWN = 0,
	SYSDECODE_ABI_FREEBSD,
	SYSDECODE_ABI_FREEBSD32,
	SYSDECODE_ABI_LINUX,
	SYSDECODE_ABI_LINUX32,
	SYSDECODE_ABI_CLOUDABI64,
	SYSDECODE_ABI_CLOUDABI32
};

int	sysdecode_abi_to_freebsd_errno(enum sysdecode_abi _abi, int _error);
bool	sysdecode_access_mode(FILE *_fp, int _mode, int *_rem);
const char *sysdecode_acltype(int _type);
const char *sysdecode_atfd(int _fd);
bool	sysdecode_atflags(FILE *_fp, int _flags, int *_rem);
bool	sysdecode_cap_fcntlrights(FILE *_fp, uint32_t _rights, uint32_t *_rem);
void	sysdecode_cap_rights(FILE *_fp, cap_rights_t *_rightsp);
const char *sysdecode_cmsg_type(int _cmsg_level, int _cmsg_type);
const char *sysdecode_extattrnamespace(int _namespace);
const char *sysdecode_fadvice(int _advice);
void	sysdecode_fcntl_arg(FILE *_fp, int _cmd, uintptr_t _arg, int _base);
bool	sysdecode_fcntl_arg_p(int _cmd);
const char *sysdecode_fcntl_cmd(int _cmd);
bool	sysdecode_fcntl_fileflags(FILE *_fp, int _flags, int *_rem);
bool	sysdecode_fileflags(FILE *_fp, fflags_t _flags, fflags_t *_rem);
bool	sysdecode_filemode(FILE *_fp, int _mode, int *_rem);
bool	sysdecode_flock_operation(FILE *_fp, int _operation, int *_rem);
int	sysdecode_freebsd_to_abi_errno(enum sysdecode_abi _abi, int _error);
const char *sysdecode_getfsstat_mode(int _mode);
const char *sysdecode_getrusage_who(int _who);
const char *sysdecode_idtype(int _idtype);
const char *sysdecode_ioctlname(unsigned long _val);
const char *sysdecode_ipproto(int _protocol);
void	sysdecode_kevent_fflags(FILE *_fp, short _filter, int _fflags,
	    int _base);
const char *sysdecode_kevent_filter(int _filter);
bool	sysdecode_kevent_flags(FILE *_fp, int _flags, int *_rem);
const char *sysdecode_kldsym_cmd(int _cmd);
const char *sysdecode_kldunload_flags(int _flags);
const char *sysdecode_lio_listio_mode(int _mode);
const char *sysdecode_madvice(int _advice);
const char *sysdecode_minherit_inherit(int _inherit);
const char *sysdecode_msgctl_cmd(int _cmd);
bool	sysdecode_mlockall_flags(FILE *_fp, int _flags, int *_rem);
bool	sysdecode_mmap_flags(FILE *_fp, int _flags, int *_rem);
bool	sysdecode_mmap_prot(FILE *_fp, int _prot, int *_rem);
bool	sysdecode_mount_flags(FILE *_fp, int _flags, int *_rem);
bool	sysdecode_msg_flags(FILE *_fp, int _flags, int *_rem);
bool	sysdecode_msync_flags(FILE *_fp, int _flags, int *_rem);
const char *sysdecode_nfssvc_flags(int _flags);
bool	sysdecode_open_flags(FILE *_fp, int _flags, int *_rem);
const char *sysdecode_pathconf_name(int _name);
bool	sysdecode_pipe2_flags(FILE *_fp, int _flags, int *_rem);
const char *sysdecode_prio_which(int _which);
const char *sysdecode_procctl_cmd(int _cmd);
const char *sysdecode_ptrace_request(int _request);
bool	sysdecode_quotactl_cmd(FILE *_fp, int _cmd);
bool	sysdecode_reboot_howto(FILE *_fp, int _howto, int *_rem);
bool	sysdecode_rfork_flags(FILE *_fp, int _flags, int *_rem);
const char *sysdecode_rlimit(int _resource);
const char *sysdecode_rtprio_function(int _function);
const char *sysdecode_scheduler_policy(int _policy);
bool	sysdecode_sctp_nxt_flags(FILE *_fp, int _flags, int *_rem);
const char *sysdecode_sctp_pr_policy(int _policy);
bool	sysdecode_sctp_rcv_flags(FILE *_fp, int _flags, int *_rem);
void	sysdecode_sctp_sinfo_flags(FILE *_fp, int _sinfo_flags);
bool	sysdecode_sctp_snd_flags(FILE *_fp, int _flags, int *_rem);
const char *sysdecode_semctl_cmd(int _cmd);
bool	sysdecode_semget_flags(FILE *_fp, int _flag, int *_rem);
bool	sysdecode_sendfile_flags(FILE *_fp, int _flags, int *_rem);
bool	sysdecode_shmat_flags(FILE *_fp, int _flags, int *_rem);
const char *sysdecode_shmctl_cmd(int _cmd);
const char *sysdecode_shutdown_how(int _how);
const char *sysdecode_sigbus_code(int _si_code);
const char *sysdecode_sigchld_code(int _si_code);
const char *sysdecode_sigcode(int _sig, int _si_code);
const char *sysdecode_sigfpe_code(int _si_code);
const char *sysdecode_sigill_code(int _si_code);
const char *sysdecode_signal(int _sig);
const char *sysdecode_sigprocmask_how(int _how);
const char *sysdecode_sigsegv_code(int _si_code);
const char *sysdecode_sigtrap_code(int _si_code);
const char *sysdecode_sockaddr_family(int _sa_family);
const char *sysdecode_socketdomain(int _domain);
const char *sysdecode_socket_protocol(int _domain, int _protocol);
bool	sysdecode_socket_type(FILE *_fp, int _type, int *_rem);
const char *sysdecode_sockopt_level(int _level);
const char *sysdecode_sockopt_name(int _level, int _optname);
const char *sysdecode_syscallname(enum sysdecode_abi _abi, unsigned int _code);
const char *sysdecode_sysarch_number(int _number);
bool	sysdecode_thr_create_flags(FILE *_fp, int _flags, int *_rem);
bool	sysdecode_umtx_cvwait_flags(FILE *_fp, u_long _flags, u_long *_rem);
const char *sysdecode_umtx_op(int _op);
bool	sysdecode_umtx_rwlock_flags(FILE *_fp, u_long _flags, u_long *_rem);
int	sysdecode_utrace(FILE *_fp, void *_buf, size_t _len);
bool	sysdecode_vmprot(FILE *_fp, int _type, int *_rem);
const char *sysdecode_vmresult(int _result);
bool	sysdecode_wait4_options(FILE *_fp, int _options, int *_rem);
bool	sysdecode_wait6_options(FILE *_fp, int _options, int *_rem);
const char *sysdecode_whence(int _whence);

#endif /* !__SYSDECODE_H__ */
