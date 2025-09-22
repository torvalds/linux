/*	$OpenBSD: ktrace.h,v 1.50 2024/07/27 02:10:26 guenther Exp $	*/
/*	$NetBSD: ktrace.h,v 1.12 1996/02/04 02:12:29 christos Exp $	*/

/*
 * Copyright (c) 1988, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *	@(#)ktrace.h	8.1 (Berkeley) 6/2/93
 */

#include <sys/uio.h>
#include <sys/syslimits.h>
#include <sys/signal.h>
#include <sys/time.h>

/*
 * operations to ktrace system call  (KTROP(op))
 */
#define KTROP_SET		0	/* set trace points */
#define KTROP_CLEAR		1	/* clear trace points */
#define KTROP_CLEARFILE		2	/* stop all tracing to file */
#define	KTROP(o)		((o)&3)	/* macro to extract operation */
/*
 * flags (ORed in with operation)
 */
#define KTRFLAG_DESCEND		4	/* perform op on all children too */

/*
 * ktrace record header
 */
struct ktr_header {
	uint	ktr_type;		/* trace record type */
	pid_t	ktr_pid;		/* process id */
	pid_t	ktr_tid;		/* thread id */
	struct	timespec ktr_time;	/* timestamp */
	char	ktr_comm[_MAXCOMLEN];	/* command name, incl NUL */
	size_t	ktr_len;		/* length of buf */
};

/*
 * ktrace record types
 */

 /*
 * KTR_START - start of trace record, one per ktrace(KTROP_SET) syscall
 */
#define KTR_START	0x4b545200	/* "KTR" */

/*
 * KTR_SYSCALL - system call record
 */
#define KTR_SYSCALL	1
struct ktr_syscall {
	int	ktr_code;		/* syscall number */
	int	ktr_argsize;		/* size of arguments */
	/*
	 * followed by ktr_argsize/sizeof(register_t) "register_t"s
	 */
};

/*
 * KTR_SYSRET - return from system call record
 */
#define KTR_SYSRET	2
struct ktr_sysret {
	int	ktr_code;
	int	ktr_error;
	/*
	 * If ktr_error is zero, then followed by retval: register_t for
	 * all syscalls except lseek(), which uses long long
	 */
};

/*
 * KTR_NAMEI - namei record
 */
#define KTR_NAMEI	3
	/* record contains pathname */

/*
 * KTR_GENIO - trace generic process i/o
 */
#define KTR_GENIO	4
struct ktr_genio {
	int	ktr_fd;
	enum	uio_rw ktr_rw;
	/*
	 * followed by data successfully read/written
	 */
};

/*
 * KTR_PSIG - trace processed signal
 */
#define	KTR_PSIG	5
struct ktr_psig {
	int	signo;
	sig_t	action;
	int	mask;
	int	code;
	siginfo_t si;
};

/*
 * KTR_STRUCT - misc. structs
 */
#define KTR_STRUCT	8
	/*
	 * record contains null-terminated struct name followed by
	 * struct contents
	 */
struct sockaddr;
struct stat;

/*
 * KTR_USER - user record
 */
#define KTR_USER	9
#define KTR_USER_MAXIDLEN	20
#define KTR_USER_MAXLEN		2048	/* maximum length of passed data */
struct ktr_user {
	char    ktr_id[KTR_USER_MAXIDLEN];      /* string id of caller */
	/*
	 * Followed by ktr_len - sizeof(struct ktr_user) of user data.
	 */
};

/*
 * KTR_EXECARGS and KTR_EXECENV - args and environment records
 */
#define KTR_EXECARGS	10
#define KTR_EXECENV	11


/*
 * KTR_PLEDGE - details of pledge violation
 */
#define	KTR_PLEDGE	12
struct ktr_pledge {
	int		error;
	int		syscall;
	uint64_t	code;
};

/*
 * KTR_PINSYSCALL - details of pinsyscall violation
 */
#define	KTR_PINSYSCALL	13
struct ktr_pinsyscall {
	int		error;
	int		syscall;
	vaddr_t		addr;
};

/*
 * kernel trace points (in ps_traceflag)
 */
#define KTRFAC_MASK	0x00ffffff
#define KTRFAC_SYSCALL	(1<<KTR_SYSCALL)
#define KTRFAC_SYSRET	(1<<KTR_SYSRET)
#define KTRFAC_NAMEI	(1<<KTR_NAMEI)
#define KTRFAC_GENIO	(1<<KTR_GENIO)
#define	KTRFAC_PSIG	(1<<KTR_PSIG)
#define KTRFAC_STRUCT   (1<<KTR_STRUCT)
#define KTRFAC_USER	(1<<KTR_USER)
#define KTRFAC_EXECARGS	(1<<KTR_EXECARGS)
#define KTRFAC_EXECENV	(1<<KTR_EXECENV)
#define	KTRFAC_PLEDGE	(1<<KTR_PLEDGE)
#define	KTRFAC_PINSYSCALL	(1<<KTR_PINSYSCALL)

/*
 * trace flags (also in ps_traceflag)
 */
#define KTRFAC_ROOT	0x80000000U	/* root set this trace */
#define KTRFAC_INHERIT	0x40000000	/* pass trace flags to children */

#ifndef	_KERNEL

#include <sys/cdefs.h>

__BEGIN_DECLS
int	ktrace(const char *, int, int, pid_t);
int	utrace(const char *, const void *, size_t);
__END_DECLS

#else

/*
 * Test for kernel trace point
 */
#define KTRPOINT(p, type)	\
	((p)->p_p->ps_traceflag & (1<<(type)) && ((p)->p_flag & P_INKTR) == 0)

void ktrgenio(struct proc *, int, enum uio_rw, struct iovec *, ssize_t);
void ktrnamei(struct proc *, char *);
void ktrpsig(struct proc *, int, sig_t, int, int, siginfo_t *);
void ktrsyscall(struct proc *, register_t, size_t, register_t []);
void ktrsysret(struct proc *, register_t, int, const register_t [2]);
int ktruser(struct proc *, const char *, const void *, size_t);
void ktrexec(struct proc *, int, const char *, ssize_t);
void ktrpledge(struct proc *, int, uint64_t, int);
void ktrpinsyscall(struct proc *, int, int, vaddr_t);

void ktrcleartrace(struct process *);
void ktrsettrace(struct process *, int, struct vnode *, struct ucred *);

void    ktrstruct(struct proc *, const char *, const void *, size_t);

/* please keep these sorted by second argument to ktrstruct() */
#define ktrabstimespec(p, s) \
	ktrstruct(p, "abstimespec", s, sizeof(struct timespec))
#define ktrabstimeval(p, s) \
	ktrstruct(p, "abstimeval", s, sizeof(struct timeval))
#define ktrcmsghdr(p, s, l) \
	ktrstruct(p, "cmsghdr", s, l)
#define ktrfds(p, s, c) \
	ktrstruct(p, "fds", s, (c) * sizeof(int))
#define ktrfdset(p, s, l) \
	ktrstruct(p, "fdset", s, l)
#define ktrflock(p, s) \
	ktrstruct(p, "flock", s, sizeof(struct flock))
#define ktriovec(p, s, c) \
	ktrstruct(p, "iovec", s, (c) * sizeof(struct iovec))
#define ktritimerval(p, s) \
	ktrstruct(p, "itimerval", s, sizeof(struct itimerval))
#define ktrevent(p, s, c) \
	ktrstruct(p, "kevent", s, (c) * sizeof(struct kevent))
#define ktrmmsghdr(p, s) \
	ktrstruct(p, "mmsghdr", s, sizeof(struct mmsghdr))
#define ktrmsghdr(p, s) \
	ktrstruct(p, "msghdr", s, sizeof(struct msghdr))
#define ktrpollfd(p, s, c) \
	ktrstruct(p, "pollfd", s, (c) * sizeof(struct pollfd))
#define ktrquota(p, s) \
	ktrstruct(p, "quota", s, sizeof(struct dqblk))
#define ktrreltimespec(p, s) \
	ktrstruct(p, "reltimespec", s, sizeof(struct timespec))
#define ktrreltimeval(p, s) \
	ktrstruct(p, "reltimeval", s, sizeof(struct timeval))
#define ktrrlimit(p, s) \
	ktrstruct(p, "rlimit", s, sizeof(struct rlimit))
#define ktrrusage(p, s) \
	ktrstruct(p, "rusage", s, sizeof(struct rusage))
#define ktrsigaction(p, s) \
	ktrstruct(p, "sigaction", s, sizeof(struct sigaction))
#define ktrsiginfo(p, s) \
	ktrstruct(p, "siginfo", s, sizeof(siginfo_t))
#define ktrsockaddr(p, s, l) \
	ktrstruct(p, "sockaddr", s, l)
#define ktrstat(p, s) \
	ktrstruct(p, "stat", s, sizeof(struct stat))

#endif	/* !_KERNEL */
