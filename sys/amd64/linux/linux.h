/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2013 Dmitry Chagin
 * Copyright (c) 1994-1996 Søren Schmidt
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
 *
 * $FreeBSD$
 */

#ifndef _AMD64_LINUX_H_
#define	_AMD64_LINUX_H_

#include <compat/linux/linux.h>
#include <amd64/linux/linux_syscall.h>

#define	LINUX_LEGACY_SYSCALLS

/*
 * debugging support
 */
extern u_char linux_debug_map[];
#define	ldebug(name)	isclr(linux_debug_map, LINUX_SYS_linux_ ## name)
#define	ARGS(nm, fmt)	"linux(%ld/%ld): "#nm"("fmt")\n",			\
			(long)td->td_proc->p_pid, (long)td->td_tid
#define	LMSG(fmt)	"linux(%ld/%ld): "fmt"\n",				\
			(long)td->td_proc->p_pid, (long)td->td_tid
#define	LINUX_DTRACE	linuxulator

#define	PTRIN(v)	(void *)(v)
#define	PTROUT(v)	(uintptr_t)(v)

#define	CP(src,dst,fld) do { (dst).fld = (src).fld; } while (0)
#define	CP2(src,dst,sfld,dfld) do { (dst).dfld = (src).sfld; } while (0)
#define	PTRIN_CP(src,dst,fld) \
	do { (dst).fld = PTRIN((src).fld); } while (0)

/*
 * Provide a separate set of types for the Linux types.
 */
typedef int32_t		l_int;
typedef int64_t		l_long;
typedef int16_t		l_short;
typedef uint32_t	l_uint;
typedef uint64_t	l_ulong;
typedef uint16_t	l_ushort;

typedef l_ulong		l_uintptr_t;
typedef l_long		l_clock_t;
typedef l_int		l_daddr_t;
typedef l_ulong		l_dev_t;
typedef l_uint		l_gid_t;
typedef l_ushort	l_gid16_t;
typedef l_uint		l_uid_t;
typedef	l_ushort	l_uid16_t;
typedef l_ulong		l_ino_t;
typedef l_int		l_key_t;
typedef l_long		l_loff_t;
typedef l_uint		l_mode_t;
typedef l_long		l_off_t;
typedef l_int		l_pid_t;
typedef l_ulong		l_size_t;
typedef l_long		l_ssize_t;
typedef l_long		l_suseconds_t;
typedef l_long		l_time_t;
typedef l_int		l_timer_t;
typedef l_int		l_mqd_t;
typedef l_size_t	l_socklen_t;
typedef	l_ulong		l_fd_mask;

typedef struct {
	l_int		val[2];
} l_fsid_t;

typedef struct {
	l_time_t	tv_sec;
	l_suseconds_t	tv_usec;
} l_timeval;

#define	l_fd_set	fd_set

/*
 * Miscellaneous
 */
#define LINUX_AT_COUNT		19	/* Count of used aux entry types. */

struct l___sysctl_args
{
	l_uintptr_t	name;
	l_int		nlen;
	l_uintptr_t	oldval;
	l_uintptr_t	oldlenp;
	l_uintptr_t	newval;
	l_size_t	newlen;
	l_ulong		__spare[4];
};

/* Resource limits */
#define	LINUX_RLIMIT_CPU	0
#define	LINUX_RLIMIT_FSIZE	1
#define	LINUX_RLIMIT_DATA	2
#define	LINUX_RLIMIT_STACK	3
#define	LINUX_RLIMIT_CORE	4
#define	LINUX_RLIMIT_RSS	5
#define	LINUX_RLIMIT_NPROC	6
#define	LINUX_RLIMIT_NOFILE	7
#define	LINUX_RLIMIT_MEMLOCK	8
#define	LINUX_RLIMIT_AS		9	/* Address space limit */

#define	LINUX_RLIM_NLIMITS	10

struct l_rlimit {
	l_ulong		rlim_cur;
	l_ulong		rlim_max;
};

/*
 * stat family of syscalls
 */
struct l_timespec {
	l_time_t	tv_sec;
	l_long		tv_nsec;
};

struct l_newstat {
	l_dev_t		st_dev;
	l_ino_t		st_ino;
	l_ulong		st_nlink;
	l_uint		st_mode;
	l_uid_t		st_uid;
	l_gid_t		st_gid;
	l_uint		__st_pad1;
	l_dev_t		st_rdev;
	l_off_t		st_size;
	l_long		st_blksize;
	l_long		st_blocks;
	struct l_timespec	st_atim;
	struct l_timespec	st_mtim;
	struct l_timespec	st_ctim;
	l_long		__unused1;
	l_long		__unused2;
	l_long		__unused3;
};

/* sigaction flags */
#define	LINUX_SA_NOCLDSTOP	0x00000001
#define	LINUX_SA_NOCLDWAIT	0x00000002
#define	LINUX_SA_SIGINFO	0x00000004
#define	LINUX_SA_RESTORER	0x04000000
#define	LINUX_SA_ONSTACK	0x08000000
#define	LINUX_SA_RESTART	0x10000000
#define	LINUX_SA_INTERRUPT	0x20000000
#define	LINUX_SA_NOMASK		0x40000000
#define	LINUX_SA_ONESHOT	0x80000000

/* sigprocmask actions */
#define	LINUX_SIG_BLOCK		0
#define	LINUX_SIG_UNBLOCK	1
#define	LINUX_SIG_SETMASK	2

/* sigaltstack */
#define	LINUX_MINSIGSTKSZ	2048

typedef void	(*l_handler_t)(l_int);

typedef struct {
	l_handler_t	lsa_handler;
	l_ulong		lsa_flags;
	l_uintptr_t	lsa_restorer;
	l_sigset_t	lsa_mask;
} l_sigaction_t;

typedef struct {
	l_uintptr_t	ss_sp;
	l_int		ss_flags;
	l_size_t	ss_size;
} l_stack_t;

struct l_fpstate {
	u_int16_t cwd;
	u_int16_t swd;
	u_int16_t twd;
	u_int16_t fop;
	u_int64_t rip;
	u_int64_t rdp;
	u_int32_t mxcsr;
	u_int32_t mxcsr_mask;
	u_int32_t st_space[32];
	u_int32_t xmm_space[64];
	u_int32_t reserved2[24];
};

struct l_sigcontext {
	l_ulong		sc_r8;
	l_ulong		sc_r9;
	l_ulong		sc_r10;
	l_ulong		sc_r11;
	l_ulong		sc_r12;
	l_ulong		sc_r13;
	l_ulong		sc_r14;
	l_ulong		sc_r15;
	l_ulong		sc_rdi;
	l_ulong		sc_rsi;
	l_ulong		sc_rbp;
	l_ulong		sc_rbx;
	l_ulong		sc_rdx;
	l_ulong		sc_rax;
	l_ulong		sc_rcx;
	l_ulong		sc_rsp;
	l_ulong		sc_rip;
	l_ulong		sc_rflags;
	l_ushort	sc_cs;
	l_ushort	sc_gs;
	l_ushort	sc_fs;
	l_ushort	sc___pad0;
	l_ulong		sc_err;
	l_ulong		sc_trapno;
	l_sigset_t	sc_mask;
	l_ulong		sc_cr2;
	struct l_fpstate *sc_fpstate;
	l_ulong		sc_reserved1[8];
};

struct l_ucontext {
	l_ulong		uc_flags;
	l_uintptr_t	uc_link;
	l_stack_t	uc_stack;
	struct l_sigcontext	uc_mcontext;
	l_sigset_t	uc_sigmask;
};

#define LINUX_SI_PREAMBLE_SIZE	(4 * sizeof(int))
#define	LINUX_SI_MAX_SIZE	128
#define	LINUX_SI_PAD_SIZE	((LINUX_SI_MAX_SIZE - \
				    LINUX_SI_PREAMBLE_SIZE) / sizeof(l_int))
typedef union l_sigval {
	l_int		sival_int;
	l_uintptr_t	sival_ptr;
} l_sigval_t;

typedef struct l_siginfo {
	l_int		lsi_signo;
	l_int		lsi_errno;
	l_int		lsi_code;
	union {
		l_int	_pad[LINUX_SI_PAD_SIZE];

		struct {
			l_pid_t		_pid;
			l_uid_t		_uid;
		} _kill;

		struct {
			l_timer_t	_tid;
			l_int		_overrun;
			char		_pad[sizeof(l_uid_t) - sizeof(int)];
			union l_sigval	_sigval;
			l_uint		_sys_private;
		} _timer;

		struct {
			l_pid_t		_pid;		/* sender's pid */
			l_uid_t		_uid;		/* sender's uid */
			union l_sigval	_sigval;
		} _rt;

		struct {
			l_pid_t		_pid;		/* which child */
			l_uid_t		_uid;		/* sender's uid */
			l_int		_status;	/* exit code */
			l_clock_t	_utime;
			l_clock_t	_stime;
		} _sigchld;

		struct {
			l_uintptr_t	_addr;	/* Faulting insn/memory ref. */
		} _sigfault;

		struct {
			l_long		_band;	/* POLL_IN,POLL_OUT,POLL_MSG */
			l_int		_fd;
		} _sigpoll;
	} _sifields;
} l_siginfo_t;

#define	lsi_pid		_sifields._kill._pid
#define	lsi_uid		_sifields._kill._uid
#define	lsi_tid		_sifields._timer._tid
#define	lsi_overrun	_sifields._timer._overrun
#define	lsi_sys_private	_sifields._timer._sys_private
#define	lsi_status	_sifields._sigchld._status
#define	lsi_utime	_sifields._sigchld._utime
#define	lsi_stime	_sifields._sigchld._stime
#define	lsi_value	_sifields._rt._sigval
#define	lsi_int		_sifields._rt._sigval.sival_int
#define	lsi_ptr		_sifields._rt._sigval.sival_ptr
#define	lsi_addr	_sifields._sigfault._addr
#define	lsi_band	_sifields._sigpoll._band
#define	lsi_fd		_sifields._sigpoll._fd

/*
 * We make the stack look like Linux expects it when calling a signal
 * handler, but use the BSD way of calling the handler and sigreturn().
 * This means that we need to pass the pointer to the handler too.
 * It is appended to the frame to not interfere with the rest of it.
 */

struct l_rt_sigframe {
	struct l_ucontext	sf_sc;
	struct l_siginfo	sf_si;
	l_handler_t		sf_handler;
};

/*
 * mount flags
 */
#define	LINUX_MS_RDONLY		0x0001
#define	LINUX_MS_NOSUID		0x0002
#define	LINUX_MS_NODEV		0x0004
#define	LINUX_MS_NOEXEC		0x0008
#define	LINUX_MS_REMOUNT	0x0020

/*
 * SystemV IPC defines
 */
#define	LINUX_IPC_RMID		0
#define	LINUX_IPC_SET		1
#define	LINUX_IPC_STAT		2
#define	LINUX_IPC_INFO		3

#define	LINUX_SHM_LOCK		11
#define	LINUX_SHM_UNLOCK	12
#define	LINUX_SHM_STAT		13
#define	LINUX_SHM_INFO		14

#define	LINUX_SHM_RDONLY	0x1000
#define	LINUX_SHM_RND		0x2000
#define	LINUX_SHM_REMAP		0x4000

/* semctl commands */
#define	LINUX_GETPID		11
#define	LINUX_GETVAL		12
#define	LINUX_GETALL		13
#define	LINUX_GETNCNT		14
#define	LINUX_GETZCNT		15
#define	LINUX_SETVAL		16
#define	LINUX_SETALL		17
#define	LINUX_SEM_STAT		18
#define	LINUX_SEM_INFO		19

union l_semun {
	l_int		val;
	l_uintptr_t	buf;
	l_uintptr_t	array;
	l_uintptr_t	__buf;
	l_uintptr_t	__pad;
};

struct l_sockaddr {
	l_ushort	sa_family;
	char		sa_data[14];
};

struct l_ifmap {
	l_ulong		mem_start;
	l_ulong		mem_end;
	l_ushort	base_addr;
	u_char		irq;
	u_char		dma;
	u_char		port;
} __packed;

#define	LINUX_IFHWADDRLEN	6
#define	LINUX_IFNAMSIZ		16

struct l_ifreq {
	union {
		char	ifrn_name[LINUX_IFNAMSIZ];
	} ifr_ifrn;

	union {
		struct l_sockaddr	ifru_addr;
		struct l_sockaddr	ifru_dstaddr;
		struct l_sockaddr	ifru_broadaddr;
		struct l_sockaddr	ifru_netmask;
		struct l_sockaddr	ifru_hwaddr;
		l_short		ifru_flags[1];
		l_int		ifru_ivalue;
		l_int		ifru_mtu;
		struct l_ifmap	ifru_map;
		char		ifru_slave[LINUX_IFNAMSIZ];
		l_uintptr_t	ifru_data;
	} ifr_ifru;
} __packed;

#define	ifr_name	ifr_ifrn.ifrn_name	/* Interface name */
#define	ifr_hwaddr	ifr_ifru.ifru_hwaddr	/* MAC address */
#define	ifr_ifindex	ifr_ifru.ifru_ivalue	/* Interface index */

struct l_ifconf {
	int	ifc_len;
	union {
		l_uintptr_t	ifcu_buf;
		l_uintptr_t	ifcu_req;
	} ifc_ifcu;
};

#define	ifc_buf		ifc_ifcu.ifcu_buf
#define	ifc_req		ifc_ifcu.ifcu_req

/*
 * poll()
 */
#define	LINUX_POLLIN		0x0001
#define	LINUX_POLLPRI		0x0002
#define	LINUX_POLLOUT		0x0004
#define	LINUX_POLLERR		0x0008
#define	LINUX_POLLHUP		0x0010
#define	LINUX_POLLNVAL		0x0020
#define	LINUX_POLLRDNORM	0x0040
#define	LINUX_POLLRDBAND	0x0080
#define	LINUX_POLLWRNORM	0x0100
#define	LINUX_POLLWRBAND	0x0200
#define	LINUX_POLLMSG		0x0400

struct l_pollfd {
	l_int		fd;
	l_short		events;
	l_short		revents;
};

#define LINUX_ARCH_SET_GS		0x1001
#define LINUX_ARCH_SET_FS		0x1002
#define LINUX_ARCH_GET_FS		0x1003
#define LINUX_ARCH_GET_GS		0x1004

#define	linux_copyout_rusage(r, u)	copyout(r, u, sizeof(*r))

/* robust futexes */
struct linux_robust_list {
	l_uintptr_t			next;
};

struct linux_robust_list_head {
	struct linux_robust_list	list;
	l_long				futex_offset;
	l_uintptr_t			pending_list;
};

#endif /* !_AMD64_LINUX_H_ */
