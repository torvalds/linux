/*-
 * SPDX-License-Identifier: BSD-4-Clause
 *
 * Copyright 1997 Sean Eric Fagan
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by Sean Eric Fagan
 * 4. Neither the name of the author may be used to endorse or promote
 *    products derived from this software without specific prior written
 *    permission.
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

/*
 * System call arguments come in several flavours:
 * Hex -- values that should be printed in hex (addresses)
 * Octal -- Same as above, but octal
 * Int -- normal integer values (file descriptors, for example)
 * LongHex -- long value that should be printed in hex
 * Name -- pointer to a NULL-terminated string.
 * BinString -- pointer to an array of chars, printed via strvisx().
 * Ptr -- pointer to some unspecified structure.  Just print as hex for now.
 * Stat -- a pointer to a stat buffer.  Prints a couple fields.
 * Stat11 -- a pointer to a freebsd 11 stat buffer.  Prints a couple fields.
 * StatFs -- a pointer to a statfs buffer.  Prints a few fields.
 * Ioctl -- an ioctl command.  Woefully limited.
 * Quad -- a double-word value.  e.g., lseek(int, offset_t, int)
 * Signal -- a signal number.  Prints the signal name (SIGxxx)
 * Sockaddr -- a pointer to a struct sockaddr.  Prints symbolic AF, and IP:Port
 * StringArray -- a pointer to an array of string pointers.
 * Timespec -- a pointer to a struct timespec.  Prints both elements.
 * Timeval -- a pointer to a struct timeval.  Prints both elements.
 * Timeval2 -- a pointer to two struct timevals.  Prints both elements of both.
 * Itimerval -- a pointer to a struct itimerval.  Prints all elements.
 * Pollfd -- a pointer to an array of struct pollfd.  Prints .fd and .events.
 * Fd_set -- a pointer to an array of fd_set.  Prints the fds that are set.
 * Sigaction -- a pointer to a struct sigaction.  Prints all elements.
 * Sigset -- a pointer to a sigset_t.  Prints the signals that are set.
 * Sigprocmask -- the first argument to sigprocmask().  Prints the name.
 * Kevent -- a pointer to an array of struct kevents.  Prints all elements.
 * Pathconf -- the 2nd argument of pathconf().
 * Utrace -- utrace(2) buffer.
 * CapRights -- a pointer to a cap_rights_t.  Prints all set capabilities.
 *
 * In addition, the pointer types (String, Ptr) may have OUT masked in --
 * this means that the data is set on *return* from the system call -- or
 * IN (meaning that the data is passed *into* the system call).
 */

enum Argtype {
	None = 1,

	/* Scalar integers. */
	Socklent,
	Octal,
	Int,
	UInt,
	Hex,
	Long,
	LongHex,
	Sizet,
	Quad,
	QuadHex,

	/* Encoded scalar values. */
	Accessmode,
	Acltype,
	Atfd,
	Atflags,
	CapFcntlRights,
	Extattrnamespace,
	Fadvice,
	Fcntl,
	Fcntlflag,
	FileFlags,
	Flockop,
	Getfsstatmode,
	Idtype,
	Ioctl,
	Kldsymcmd,
	Kldunloadflags,
	Madvice,
	Minherit,
	Msgflags,
	Mlockall,
	Mmapflags,
	Mountflags,
	Mprot,
	Msync,
	Open,
	Pathconf,
	Pipe2,
	Procctl,
	Priowhich,
	Ptraceop,
	Quotactlcmd,
	Reboothowto,
	Resource,
	Rforkflags,
	Rtpriofunc,
	RusageWho,
	Schedpolicy,
	Shutdown,
	Signal,
	Sigprocmask,
	Sockdomain,
	Sockoptlevel,
	Sockoptname,
	Sockprotocol,
	Socktype,
	Sysarch,
	Umtxop,
	Waitoptions,
	Whence,

	/* Pointers to non-structures. */
	Ptr,
	BinString,
	CapRights,
	ExecArgs,
	ExecEnv,
	ExitStatus,
	Fd_set,
	IntArray,
	Iovec,
	Name,
	PipeFds,
	PSig,
	PQuadHex,
	PUInt,
	Readlinkres,
	ShmName,
	StringArray,

	/* Pointers to structures. */
	Itimerval,
	Kevent,
	Kevent11,
	LinuxSockArgs,
	Msghdr,
	Pollfd,
	Rlimit,
	Rusage,
	Schedparam,
	Sctpsndrcvinfo,
	Sigaction,
	Siginfo,
	Sigset,
	Sockaddr,
	Stat,
	Stat11,
	StatFs,
	Timespec,
	Timespec2,
	Timeval,
	Timeval2,
	Utrace,

	CloudABIAdvice,
	CloudABIClockID,
	CloudABIFDSFlags,
	CloudABIFDStat,
	CloudABIFileStat,
	CloudABIFileType,
	CloudABIFSFlags,
	CloudABILookup,
	CloudABIMFlags,
	CloudABIMProt,
	CloudABIMSFlags,
	CloudABIOFlags,
	CloudABISDFlags,
	CloudABISignal,
	CloudABISockStat,
	CloudABISSFlags,
	CloudABITimestamp,
	CloudABIULFlags,
	CloudABIWhence,

	MAX_ARG_TYPE,
};

#define	ARG_MASK	0xff
#define	OUT	0x100
#define	IN	/*0x20*/0

_Static_assert(ARG_MASK > MAX_ARG_TYPE,
    "ARG_MASK overlaps with Argtype values");

struct syscall_args {
	enum Argtype type;
	int offset;
};

struct syscall {
	STAILQ_ENTRY(syscall) entries;
	const char *name;
	u_int ret_type;	/* 0, 1, or 2 return values */
	u_int nargs;	/* actual number of meaningful arguments */
			/* Hopefully, no syscalls with > 10 args */
	struct syscall_args args[10];
	struct timespec time; /* Time spent for this call */
	int ncalls;	/* Number of calls */
	int nerror;	/* Number of calls that returned with error */
	bool unknown;	/* Unknown system call */
};

struct syscall *get_syscall(struct threadinfo *, u_int, u_int);
char *print_arg(struct syscall_args *, unsigned long*, long *, struct trussinfo *);

/*
 * Linux Socket defines
 */
#define LINUX_SOCKET		1
#define LINUX_BIND		2
#define LINUX_CONNECT		3
#define LINUX_LISTEN		4
#define LINUX_ACCEPT		5
#define LINUX_GETSOCKNAME	6
#define LINUX_GETPEERNAME	7
#define LINUX_SOCKETPAIR	8
#define LINUX_SEND		9
#define LINUX_RECV		10
#define LINUX_SENDTO		11
#define LINUX_RECVFROM		12
#define LINUX_SHUTDOWN		13
#define LINUX_SETSOCKOPT	14
#define LINUX_GETSOCKOPT	15
#define LINUX_SENDMSG		16
#define LINUX_RECVMSG		17

#define PAD_(t) (sizeof(register_t) <= sizeof(t) ? \
    0 : sizeof(register_t) - sizeof(t))

#if BYTE_ORDER == LITTLE_ENDIAN
#define PADL_(t)	0
#define PADR_(t)	PAD_(t)
#else
#define PADL_(t)	PAD_(t)
#define PADR_(t)	0
#endif

typedef int     l_int;
typedef uint32_t    l_ulong;

struct linux_socketcall_args {
    char what_l_[PADL_(l_int)]; l_int what; char what_r_[PADR_(l_int)];
    char args_l_[PADL_(l_ulong)]; l_ulong args; char args_r_[PADR_(l_ulong)];
};

void init_syscalls(void);
void print_syscall(struct trussinfo *);
void print_syscall_ret(struct trussinfo *, int, long *);
void print_summary(struct trussinfo *trussinfo);
