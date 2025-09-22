/*	$OpenBSD: unistd.h,v 1.112 2025/05/24 06:49:16 deraadt Exp $ */
/*	$NetBSD: unistd.h,v 1.26.4.1 1996/05/28 02:31:51 mrg Exp $	*/

/*-
 * Copyright (c) 1991 The Regents of the University of California.
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
 *	@(#)unistd.h	5.13 (Berkeley) 6/17/91
 */

#ifndef _UNISTD_H_
#define	_UNISTD_H_

#include <sys/_null.h>
#include <sys/types.h>
#include <sys/unistd.h>

#define	STDIN_FILENO	0	/* standard input file descriptor */
#define	STDOUT_FILENO	1	/* standard output file descriptor */
#define	STDERR_FILENO	2	/* standard error file descriptor */

#if __XPG_VISIBLE || __POSIX_VISIBLE >= 200112
#define F_ULOCK         0	/* unlock locked section */
#define F_LOCK          1	/* lock a section for exclusive use */
#define F_TLOCK         2	/* test and lock a section for exclusive use */
#define F_TEST          3	/* test a section for locks by other procs */
#endif

/*
 * POSIX options and option groups we unconditionally do or don't
 * implement.  Please keep this list in alphabetical order.
 *
 * Anything which is defined as zero below **must** have an
 * implementation for the corresponding sysconf() which is able to
 * determine conclusively whether or not the feature is supported.
 * Anything which is defined as other than -1 below **must** have
 * complete headers, types, and function declarations as specified by
 * the POSIX standard; however, if the relevant sysconf() function
 * returns -1, the functions may be stubbed out.
 */
#define _POSIX_ADVISORY_INFO			(-1)
#define _POSIX_ASYNCHRONOUS_IO			(-1)
#define _POSIX_BARRIERS				200112L
#define _POSIX_CHOWN_RESTRICTED			1
#define _POSIX_CLOCK_SELECTION			(-1)
#define _POSIX_CPUTIME				200809L
#define _POSIX_FSYNC				200112L
#define _POSIX_IPV6				0
#define _POSIX_JOB_CONTROL			1
#define _POSIX_MAPPED_FILES			200112L
#define _POSIX_MEMLOCK				200112L
#define _POSIX_MEMLOCK_RANGE			200112L
#define _POSIX_MEMORY_PROTECTION		200112L
#define _POSIX_MESSAGE_PASSING			(-1)
#define _POSIX_MONOTONIC_CLOCK			200112L
#define _POSIX_NO_TRUNC				1
#define _POSIX_PRIORITIZED_IO			(-1)
#define _POSIX_PRIORITY_SCHEDULING		(-1)
#define _POSIX_RAW_SOCKETS			200112L
#define _POSIX_READER_WRITER_LOCKS		200112L
#define _POSIX_REALTIME_SIGNALS			(-1)
#define _POSIX_REGEXP				1
#define _POSIX_SAVED_IDS			1
#define _POSIX_SEMAPHORES			200112L
#define _POSIX_SHARED_MEMORY_OBJECTS		200809L
#define _POSIX_SHELL				1
#define _POSIX_SPAWN				200112L
#define _POSIX_SPIN_LOCKS			200112L
#define _POSIX_SPORADIC_SERVER			(-1)
#define _POSIX_SYNCHRONIZED_IO			(-1)
#define _POSIX_THREAD_ATTR_STACKADDR		200112L
#define _POSIX_THREAD_ATTR_STACKSIZE		200112L
#define _POSIX_THREAD_CPUTIME			200809L
#define _POSIX_THREAD_PRIO_INHERIT		(-1)
#define _POSIX_THREAD_PRIO_PROTECT		(-1)
#define _POSIX_THREAD_PRIORITY_SCHEDULING	(-1)
#define _POSIX_THREAD_PROCESS_SHARED		(-1)
#define _POSIX_THREAD_ROBUST_PRIO_INHERIT	(-1)
#define _POSIX_THREAD_ROBUST_PRIO_PROTECT	(-1)
#define _POSIX_THREAD_SAFE_FUNCTIONS		200112L
#define _POSIX_THREAD_SPORADIC_SERVER		(-1)
#define _POSIX_THREADS				200112L
#define _POSIX_TIMEOUTS				200112L
#define _POSIX_TIMERS				(-1)
#define _POSIX_TRACE				(-1)
#define _POSIX_TRACE_EVENT_FILTER		(-1)
#define _POSIX_TRACE_INHERIT			(-1)
#define _POSIX_TRACE_LOG			(-1)
#define _POSIX_TYPED_MEMORY_OBJECTS		(-1)
#define _POSIX2_C_BIND				200112L
#define _POSIX2_C_DEV				(-1) /* need C99 utility */
#define _POSIX2_CHAR_TERM			1
#define _POSIX2_FORT_DEV			(-1) /* need fort77 utility */
#define _POSIX2_FORT_RUN			(-1) /* need asa utility */
#define _POSIX2_LOCALEDEF			(-1)
#define _POSIX2_PBS				(-1)
#define _POSIX2_PBS_ACCOUNTING			(-1)
#define _POSIX2_PBS_CHECKPOINT			(-1)
#define _POSIX2_PBS_LOCATE			(-1)
#define _POSIX2_PBS_MESSAGE			(-1)
#define _POSIX2_PBS_TRACK			(-1)
#define _POSIX2_SW_DEV				200112L
#define _POSIX2_UPE				200112L
#define _POSIX_V6_ILP32_OFF32			(-1)
#define _POSIX_V6_ILP32_OFFBIG			0
#define _POSIX_V6_LP64_OFF64			0
#define _POSIX_V6_LPBIG_OFFBIG			0
#define _POSIX_V7_ILP32_OFF32			(-1)
#define _POSIX_V7_ILP32_OFFBIG			0
#define _POSIX_V7_LP64_OFF64			0
#define _POSIX_V7_LPBIG_OFFBIG			0

#define _XOPEN_CRYPT				1
#define _XOPEN_ENH_I18N				(-1) /* mandatory in XSI */
#define _XOPEN_LEGACY				(-1)
#define _XOPEN_REALTIME				(-1)
#define _XOPEN_REALTIME_THREADS			(-1)
#define _XOPEN_SHM				1
#define _XOPEN_STREAMS				(-1)
#define _XOPEN_UUCP				(-1)
#define _XOPEN_UNIX				(-1)

/* Define the POSIX.2 version we target for compliance. */
#define _POSIX2_VERSION				200809L

/* the sysconf(3) variable values are part of the ABI */

/* configurable system variables */
#define	_SC_ARG_MAX		 1
#define	_SC_CHILD_MAX		 2
#define	_SC_CLK_TCK		 3
#define	_SC_NGROUPS_MAX		 4
#define	_SC_OPEN_MAX		 5
#define	_SC_JOB_CONTROL		 6
#define	_SC_SAVED_IDS		 7
#define	_SC_VERSION		 8
#define	_SC_BC_BASE_MAX		 9
#define	_SC_BC_DIM_MAX		10
#define	_SC_BC_SCALE_MAX	11
#define	_SC_BC_STRING_MAX	12
#define	_SC_COLL_WEIGHTS_MAX	13
#define	_SC_EXPR_NEST_MAX	14
#define	_SC_LINE_MAX		15
#define	_SC_RE_DUP_MAX		16
#define	_SC_2_VERSION		17
#define	_SC_2_C_BIND		18
#define	_SC_2_C_DEV		19
#define	_SC_2_CHAR_TERM		20
#define	_SC_2_FORT_DEV		21
#define	_SC_2_FORT_RUN		22
#define	_SC_2_LOCALEDEF		23
#define	_SC_2_SW_DEV		24
#define	_SC_2_UPE		25
#define	_SC_STREAM_MAX		26
#define	_SC_TZNAME_MAX		27
#define	_SC_PAGESIZE		28
#define	_SC_PAGE_SIZE		_SC_PAGESIZE	/* 1170 compatibility */
#define	_SC_FSYNC		29
#define	_SC_XOPEN_SHM		30
#define	_SC_SEM_NSEMS_MAX	31
#define	_SC_SEM_VALUE_MAX	32
#define	_SC_HOST_NAME_MAX	33
#define	_SC_MONOTONIC_CLOCK	34
#define	_SC_2_PBS		35
#define	_SC_2_PBS_ACCOUNTING	36
#define	_SC_2_PBS_CHECKPOINT	37
#define	_SC_2_PBS_LOCATE	38
#define	_SC_2_PBS_MESSAGE	39
#define	_SC_2_PBS_TRACK		40
#define	_SC_ADVISORY_INFO	41
#define	_SC_AIO_LISTIO_MAX	42
#define	_SC_AIO_MAX		43
#define	_SC_AIO_PRIO_DELTA_MAX	44
#define	_SC_ASYNCHRONOUS_IO	45
#define	_SC_ATEXIT_MAX		46
#define	_SC_BARRIERS		47
#define	_SC_CLOCK_SELECTION	48
#define	_SC_CPUTIME		49
#define	_SC_DELAYTIMER_MAX	50
#define	_SC_IOV_MAX		51
#define	_SC_IPV6		52
#define	_SC_MAPPED_FILES	53
#define	_SC_MEMLOCK		54
#define	_SC_MEMLOCK_RANGE	55
#define	_SC_MEMORY_PROTECTION	56
#define	_SC_MESSAGE_PASSING	57
#define	_SC_MQ_OPEN_MAX		58
#define	_SC_MQ_PRIO_MAX		59
#define	_SC_PRIORITIZED_IO	60
#define	_SC_PRIORITY_SCHEDULING	61
#define	_SC_RAW_SOCKETS		62
#define	_SC_READER_WRITER_LOCKS	63
#define	_SC_REALTIME_SIGNALS	64
#define	_SC_REGEXP		65
#define	_SC_RTSIG_MAX		66
#define	_SC_SEMAPHORES		67
#define	_SC_SHARED_MEMORY_OBJECTS 68
#define	_SC_SHELL		69
#define	_SC_SIGQUEUE_MAX	70
#define	_SC_SPAWN		71
#define	_SC_SPIN_LOCKS		72
#define	_SC_SPORADIC_SERVER	73
#define	_SC_SS_REPL_MAX		74
#define	_SC_SYNCHRONIZED_IO	75
#define	_SC_SYMLOOP_MAX		76
#define	_SC_THREAD_ATTR_STACKADDR 77
#define	_SC_THREAD_ATTR_STACKSIZE 78
#define	_SC_THREAD_CPUTIME	79
#define	_SC_THREAD_DESTRUCTOR_ITERATIONS 80
#define	_SC_THREAD_KEYS_MAX	81
#define	_SC_THREAD_PRIO_INHERIT	82
#define	_SC_THREAD_PRIO_PROTECT	83
#define	_SC_THREAD_PRIORITY_SCHEDULING 84
#define	_SC_THREAD_PROCESS_SHARED 85
#define	_SC_THREAD_ROBUST_PRIO_INHERIT 86
#define	_SC_THREAD_ROBUST_PRIO_PROTECT 87
#define	_SC_THREAD_SPORADIC_SERVER 88
#define	_SC_THREAD_STACK_MIN	89
#define	_SC_THREAD_THREADS_MAX	90
#define	_SC_THREADS		91
#define	_SC_TIMEOUTS		92
#define	_SC_TIMER_MAX		93
#define	_SC_TIMERS		94
#define	_SC_TRACE		95
#define	_SC_TRACE_EVENT_FILTER	96
#define	_SC_TRACE_EVENT_NAME_MAX 97
#define	_SC_TRACE_INHERIT	98
#define	_SC_TRACE_LOG		99
#define	_SC_GETGR_R_SIZE_MAX	100
#define	_SC_GETPW_R_SIZE_MAX	101
#define	_SC_LOGIN_NAME_MAX	102
#define	_SC_THREAD_SAFE_FUNCTIONS 103
#define	_SC_TRACE_NAME_MAX      104
#define	_SC_TRACE_SYS_MAX       105
#define	_SC_TRACE_USER_EVENT_MAX 106
#define	_SC_TTY_NAME_MAX	107
#define	_SC_TYPED_MEMORY_OBJECTS 108
#define	_SC_V6_ILP32_OFF32	109
#define	_SC_V6_ILP32_OFFBIG	110
#define	_SC_V6_LP64_OFF64	111
#define	_SC_V6_LPBIG_OFFBIG	112
#define	_SC_V7_ILP32_OFF32	113
#define	_SC_V7_ILP32_OFFBIG	114
#define	_SC_V7_LP64_OFF64	115
#define	_SC_V7_LPBIG_OFFBIG	116
#define	_SC_XOPEN_CRYPT		117
#define	_SC_XOPEN_ENH_I18N	118
#define	_SC_XOPEN_LEGACY	119
#define	_SC_XOPEN_REALTIME	120
#define	_SC_XOPEN_REALTIME_THREADS 121
#define	_SC_XOPEN_STREAMS	122
#define	_SC_XOPEN_UNIX		123
#define	_SC_XOPEN_UUCP		124
#define	_SC_XOPEN_VERSION	125

#define	_SC_PHYS_PAGES		500
#define	_SC_AVPHYS_PAGES	501
#define	_SC_NPROCESSORS_CONF	502
#define	_SC_NPROCESSORS_ONLN	503

/* configurable system strings */
#define	_CS_PATH				 1
#define	_CS_POSIX_V6_ILP32_OFF32_CFLAGS		 2
#define	_CS_POSIX_V6_ILP32_OFF32_LDFLAGS	 3
#define	_CS_POSIX_V6_ILP32_OFF32_LIBS		 4
#define	_CS_POSIX_V6_ILP32_OFFBIG_CFLAGS	 5
#define	_CS_POSIX_V6_ILP32_OFFBIG_LDFLAGS	 6
#define	_CS_POSIX_V6_ILP32_OFFBIG_LIBS		 7
#define	_CS_POSIX_V6_LP64_OFF64_CFLAGS		 8
#define	_CS_POSIX_V6_LP64_OFF64_LDFLAGS		 9
#define	_CS_POSIX_V6_LP64_OFF64_LIBS		10
#define	_CS_POSIX_V6_LPBIG_OFFBIG_CFLAGS	11
#define	_CS_POSIX_V6_LPBIG_OFFBIG_LDFLAGS	12
#define	_CS_POSIX_V6_LPBIG_OFFBIG_LIBS		13
#define	_CS_POSIX_V6_WIDTH_RESTRICTED_ENVS	14
#define	_CS_V6_ENV				15
#define	_CS_POSIX_V7_ILP32_OFF32_CFLAGS		16
#define	_CS_POSIX_V7_ILP32_OFF32_LDFLAGS	17
#define	_CS_POSIX_V7_ILP32_OFF32_LIBS		18
#define	_CS_POSIX_V7_ILP32_OFFBIG_CFLAGS	19
#define	_CS_POSIX_V7_ILP32_OFFBIG_LDFLAGS	20
#define	_CS_POSIX_V7_ILP32_OFFBIG_LIBS		21
#define	_CS_POSIX_V7_LP64_OFF64_CFLAGS		22
#define	_CS_POSIX_V7_LP64_OFF64_LDFLAGS		23
#define	_CS_POSIX_V7_LP64_OFF64_LIBS		24
#define	_CS_POSIX_V7_LPBIG_OFFBIG_CFLAGS	25
#define	_CS_POSIX_V7_LPBIG_OFFBIG_LDFLAGS	26
#define	_CS_POSIX_V7_LPBIG_OFFBIG_LIBS		27
#define	_CS_POSIX_V7_THREADS_CFLAGS		28
#define	_CS_POSIX_V7_THREADS_LDFLAGS		29
#define	_CS_POSIX_V7_WIDTH_RESTRICTED_ENVS	30
#define	_CS_V7_ENV				31

#ifndef	_INTPTR_T_DEFINED_
#define	_INTPTR_T_DEFINED_
typedef	__intptr_t		intptr_t;
#endif

__BEGIN_DECLS
__dead void	 _exit(int);
int	 access(const char *, int);
unsigned int alarm(unsigned int);
int	 chdir(const char *);
int	 chown(const char *, uid_t, gid_t);
int	 close(int);
int	 dup(int);
int	 dup2(int, int);
int	 execl(const char *, const char *, ...) 
	    __attribute__((__sentinel__));
int	 execle(const char *, const char *, ...);
int	 execlp(const char *, const char *, ...) 
	    __attribute__((__sentinel__));
int	 execv(const char *, char *const *);
int	 execve(const char *, char *const *, char *const *);
int	 execvp(const char *, char *const *);
#if __BSD_VISIBLE
int	 execvpe(const char *, char *const *, char *const *);
#endif
pid_t	 fork(void);
long	 fpathconf(int, int);
char	*getcwd(char *, size_t)
		__attribute__((__bounded__(__string__,1,2)));
gid_t	 getegid(void);
uid_t	 geteuid(void);
gid_t	 getgid(void);
int	 getgroups(int, gid_t *);
char	*getlogin(void);
pid_t	 getpgrp(void);
pid_t	 getpid(void);
pid_t	 getppid(void);
uid_t	 getuid(void);
int	 isatty(int);
int	 link(const char *, const char *);
off_t	 lseek(int, off_t, int);
long	 pathconf(const char *, int);
#if __BSD_VISIBLE
long	 pathconfat(int, const char *, int, int);
#endif
int	 pause(void);
int	 pipe(int *);
ssize_t	 read(int, void *, size_t)
		__attribute__((__bounded__(__buffer__,2,3)));
int	 rmdir(const char *);
int	 setgid(gid_t);
int	 setuid(uid_t);
unsigned int sleep(unsigned int);
long	 sysconf(int);
pid_t	 tcgetpgrp(int);
int	 tcsetpgrp(int, pid_t);
char	*ttyname(int);
int	 unlink(const char *);
ssize_t	 write(int, const void *, size_t)
		__attribute__((__bounded__(__buffer__,2,3)));

#if __POSIX_VISIBLE || __XPG_VISIBLE >= 300
pid_t	 setsid(void);
int	 setpgid(pid_t, pid_t);
#endif

#if __POSIX_VISIBLE >= 199209 || __XPG_VISIBLE
size_t	 confstr(int, char *, size_t)
		__attribute__((__bounded__(__string__,2,3)));
#ifndef _GETOPT_DEFINED_
#define _GETOPT_DEFINED_
int	 getopt(int, char * const *, const char *);
extern	 char *optarg;			/* getopt(3) external variables */
extern	 int opterr, optind, optopt, optreset;
#endif /* _GETOPT_DEFINED_ */
#endif

#if __POSIX_VISIBLE >= 199506 || __XPG_VISIBLE
int	 fsync(int);
int	 ftruncate(int, off_t);
int	 getlogin_r(char *, size_t)
		__attribute__((__bounded__(__string__,1,2)));
ssize_t	 readlink(const char * __restrict, char * __restrict, size_t)
		__attribute__ ((__bounded__(__string__,2,3)));
#endif
#if __POSIX_VISIBLE >= 199506
int	 fdatasync(int);
#endif

#if __XPG_VISIBLE || __BSD_VISIBLE
char	*crypt(const char *, const char *);
int	 fchdir(int);
int	 fchown(int, uid_t, gid_t);
long	 gethostid(void);
char	*getwd(char *)
		__attribute__ ((__bounded__(__minbytes__,1,1024)));
int	 lchown(const char *, uid_t, gid_t);
int	 mkstemp(char *);
char	*mktemp(char *);
int	 nice(int);
int	 setregid(gid_t, gid_t);
int	 setreuid(uid_t, uid_t);
void	 swab(const void *__restrict, void *__restrict, ssize_t);
void	 sync(void);
int	 truncate(const char *, off_t);
useconds_t	 ualarm(useconds_t, useconds_t);
int	 usleep(useconds_t);
pid_t	 vfork(void);
#endif

#if __POSIX_VISIBLE >= 200809 || __XPG_VISIBLE >= 420
pid_t	 getpgid(pid_t);
pid_t	 getsid(pid_t);
#endif

#if __XPG_VISIBLE >= 500
ssize_t  pread(int, void *, size_t, off_t)
		__attribute__((__bounded__(__buffer__,2,3)));
ssize_t  pwrite(int, const void *, size_t, off_t)
		__attribute__((__bounded__(__buffer__,2,3)));
int	 ttyname_r(int, char *, size_t)
	    __attribute__((__bounded__(__string__,2,3)));
#endif

#if __BSD_VISIBLE ||  __XPG_VISIBLE <= 500
/* Interfaces withdrawn by X/Open Issue 5 Version 0 */
int	 brk(void *);
int	 chroot(const char *);
int	 getdtablesize(void);
int	 getpagesize(void);
char	*getpass(const char *);
void	*sbrk(int);
#endif

#if __POSIX_VISIBLE >= 200112 || __XPG_VISIBLE >= 420
int     lockf(int, int, off_t);
#endif

#if __POSIX_VISIBLE >= 200112 || __XPG_VISIBLE >= 420 || __BSD_VISIBLE
int	 symlink(const char *, const char *);
int	 gethostname(char *, size_t)
		__attribute__ ((__bounded__(__string__,1,2)));
int	 setegid(gid_t);
int	 seteuid(uid_t);
#endif

#if __POSIX_VISIBLE >= 200809
int	faccessat(int, const char *, int, int);
int	fchownat(int, const char *, uid_t, gid_t, int);
int	linkat(int, const char *, int, const char *, int);
ssize_t	readlinkat(int, const char *, char *, size_t)
		__attribute__ ((__bounded__(__string__,3,4)));
int	symlinkat(const char *, int, const char *);
int	unlinkat(int, const char *, int);
#endif

#if __POSIX_VISIBLE >= 202405 || __BSD_VISIBLE
int	getentropy(void *, size_t);
#endif
#if __XPG_VISIBLE >= 800 || __BSD_VISIBLE
int	getresgid(gid_t *, gid_t *, gid_t *);
int	getresuid(uid_t *, uid_t *, uid_t *);
int	setresgid(gid_t, gid_t, gid_t);
int	setresuid(uid_t, uid_t, uid_t);
#endif

#if __BSD_VISIBLE
int	dup3(int, int, int);
int	pipe2(int [2], int);
#endif

#if __BSD_VISIBLE
int	 acct(const char *);
int	 closefrom(int);
int	 crypt_checkpass(const char *, const char *);
int	 crypt_newhash(const char *, const char *, char *, size_t);
void	 endusershell(void);
char	*fflagstostr(u_int32_t);
int	 getdomainname(char *, size_t)
		__attribute__ ((__bounded__(__string__,1,2)));
int	 getdtablecount(void);
int	 getgrouplist(const char *, gid_t, gid_t *, int *);
mode_t	 getmode(const void *, mode_t);
pid_t	 getthrid(void);
int	 getthrname(pid_t, char *, size_t);
char	*getusershell(void);
int	 initgroups(const char *, gid_t);
int	 issetugid(void);
char	*mkdtemp(char *);
int	 mkstemps(char *, int);
int	 nfssvc(int, void *);
int	 profil(void *, size_t, size_t, unsigned long, unsigned int, int)
		__attribute__ ((__bounded__(__string__,1,2)));
int	 quotactl(const char *, int, int, char *);
int	 rcmd(char **, int, const char *,
	    const char *, const char *, int *);
int	 rcmd_af(char **, int, const char *,
	    const char *, const char *, int *, int);
int	 rcmdsh(char **, int, const char *,
	    const char *, const char *, char *);
int	 reboot(int);
int	 revoke(const char *);
int	 rresvport(int *);
int	 rresvport_af(int *, int);
int	 ruserok(const char *, int, const char *, const char *);
int	 setdomainname(const char *, size_t);
int	 setgroups(int, const gid_t *);
int	 sethostid(long);
int	 sethostname(const char *, size_t);
int	 setlogin(const char *);
void	*setmode(const char *);
int	 setpgrp(pid_t _pid, pid_t _pgrp);	/* BSD compat version */
int	 setthrname(pid_t, const char *);
void	 setusershell(void);
int	 strtofflags(char **, u_int32_t *, u_int32_t *);
int	 swapctl(int cmd, const void *arg, int misc);
int	 pledge(const char *, const char *);
int	 unveil(const char *, const char *);
pid_t	 __tfork_thread(const struct __tfork *, size_t, void (*)(void *),
	    void *);
#endif /* __BSD_VISIBLE */
__END_DECLS

#endif /* !_UNISTD_H_ */
