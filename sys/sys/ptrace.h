/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1984, 1993
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
 *	@(#)ptrace.h	8.2 (Berkeley) 1/4/94
 * $FreeBSD$
 */

#ifndef	_SYS_PTRACE_H_
#define	_SYS_PTRACE_H_

#include <sys/signal.h>
#include <sys/param.h>
#include <machine/reg.h>

#define	PT_TRACE_ME	0	/* child declares it's being traced */
#define	PT_READ_I	1	/* read word in child's I space */
#define	PT_READ_D	2	/* read word in child's D space */
/* was	PT_READ_U	3	 * read word in child's user structure */
#define	PT_WRITE_I	4	/* write word in child's I space */
#define	PT_WRITE_D	5	/* write word in child's D space */
/* was	PT_WRITE_U	6	 * write word in child's user structure */
#define	PT_CONTINUE	7	/* continue the child */
#define	PT_KILL		8	/* kill the child process */
#define	PT_STEP		9	/* single step the child */

#define	PT_ATTACH	10	/* trace some running process */
#define	PT_DETACH	11	/* stop tracing a process */
#define PT_IO		12	/* do I/O to/from stopped process. */
#define	PT_LWPINFO	13	/* Info about the LWP that stopped. */
#define PT_GETNUMLWPS	14	/* get total number of threads */
#define PT_GETLWPLIST	15	/* get thread list */
#define PT_CLEARSTEP	16	/* turn off single step */
#define PT_SETSTEP	17	/* turn on single step */
#define PT_SUSPEND	18	/* suspend a thread */
#define PT_RESUME	19	/* resume a thread */

#define	PT_TO_SCE	20
#define	PT_TO_SCX	21
#define	PT_SYSCALL	22

#define	PT_FOLLOW_FORK	23
#define	PT_LWP_EVENTS	24	/* report LWP birth and exit */

#define	PT_GET_EVENT_MASK 25	/* get mask of optional events */
#define	PT_SET_EVENT_MASK 26	/* set mask of optional events */

#define	PT_GET_SC_ARGS	27	/* fetch syscall args */

#define PT_GETREGS      33	/* get general-purpose registers */
#define PT_SETREGS      34	/* set general-purpose registers */
#define PT_GETFPREGS    35	/* get floating-point registers */
#define PT_SETFPREGS    36	/* set floating-point registers */
#define PT_GETDBREGS    37	/* get debugging registers */
#define PT_SETDBREGS    38	/* set debugging registers */

#define	PT_VM_TIMESTAMP	40	/* Get VM version (timestamp) */
#define	PT_VM_ENTRY	41	/* Get VM map (entry) */

#define PT_FIRSTMACH    64	/* for machine-specific requests */
#include <machine/ptrace.h>	/* machine-specific requests, if any */

/* Events used with PT_GET_EVENT_MASK and PT_SET_EVENT_MASK */
#define	PTRACE_EXEC	0x0001
#define	PTRACE_SCE	0x0002
#define	PTRACE_SCX	0x0004
#define	PTRACE_SYSCALL	(PTRACE_SCE | PTRACE_SCX)
#define	PTRACE_FORK	0x0008
#define	PTRACE_LWP	0x0010
#define	PTRACE_VFORK	0x0020

#define	PTRACE_DEFAULT	(PTRACE_EXEC)

struct ptrace_io_desc {
	int	piod_op;	/* I/O operation */
	void	*piod_offs;	/* child offset */
	void	*piod_addr;	/* parent offset */
	size_t	piod_len;	/* request length */
};

/*
 * Operations in piod_op.
 */
#define PIOD_READ_D	1	/* Read from D space */
#define PIOD_WRITE_D	2	/* Write to D space */
#define PIOD_READ_I	3	/* Read from I space */
#define PIOD_WRITE_I	4	/* Write to I space */

/* Argument structure for PT_LWPINFO. */
struct ptrace_lwpinfo {
	lwpid_t	pl_lwpid;	/* LWP described. */
	int	pl_event;	/* Event that stopped the LWP. */
#define	PL_EVENT_NONE	0
#define	PL_EVENT_SIGNAL	1
	int	pl_flags;	/* LWP flags. */
#define	PL_FLAG_SA	0x01	/* M:N thread */
#define	PL_FLAG_BOUND	0x02	/* M:N bound thread */
#define	PL_FLAG_SCE	0x04	/* syscall enter point */
#define	PL_FLAG_SCX	0x08	/* syscall leave point */
#define	PL_FLAG_EXEC	0x10	/* exec(2) succeeded */
#define	PL_FLAG_SI	0x20	/* siginfo is valid */
#define	PL_FLAG_FORKED	0x40	/* new child */
#define	PL_FLAG_CHILD	0x80	/* I am from child */
#define	PL_FLAG_BORN	0x100	/* new LWP */
#define	PL_FLAG_EXITED	0x200	/* exiting LWP */
#define	PL_FLAG_VFORKED	0x400	/* new child via vfork */
#define	PL_FLAG_VFORK_DONE 0x800 /* vfork parent has resumed */
	sigset_t	pl_sigmask;	/* LWP signal mask */
	sigset_t	pl_siglist;	/* LWP pending signal */
	struct __siginfo pl_siginfo;	/* siginfo for signal */
	char		pl_tdname[MAXCOMLEN + 1]; /* LWP name */
	pid_t		pl_child_pid;	/* New child pid */
	u_int		pl_syscall_code;
	u_int		pl_syscall_narg;
};

#if defined(_WANT_LWPINFO32) || (defined(_KERNEL) && defined(__LP64__))
struct ptrace_lwpinfo32 {
	lwpid_t	pl_lwpid;	/* LWP described. */
	int	pl_event;	/* Event that stopped the LWP. */
	int	pl_flags;	/* LWP flags. */
	sigset_t	pl_sigmask;	/* LWP signal mask */
	sigset_t	pl_siglist;	/* LWP pending signal */
	struct siginfo32 pl_siginfo;	/* siginfo for signal */
	char		pl_tdname[MAXCOMLEN + 1]; /* LWP name. */
	pid_t		pl_child_pid;	/* New child pid */
	u_int		pl_syscall_code;
	u_int		pl_syscall_narg;
};
#endif

/* Argument structure for PT_VM_ENTRY. */
struct ptrace_vm_entry {
	int		pve_entry;	/* Entry number used for iteration. */
	int		pve_timestamp;	/* Generation number of VM map. */
	u_long		pve_start;	/* Start VA of range. */
	u_long		pve_end;	/* End VA of range (incl). */
	u_long		pve_offset;	/* Offset in backing object. */
	u_int		pve_prot;	/* Protection of memory range. */
	u_int		pve_pathlen;	/* Size of path. */
	long		pve_fileid;	/* File ID. */
	uint32_t	pve_fsid;	/* File system ID. */
	char		*pve_path;	/* Path name of object. */
};

#ifdef _KERNEL

int	ptrace_set_pc(struct thread *_td, unsigned long _addr);
int	ptrace_single_step(struct thread *_td);
int	ptrace_clear_single_step(struct thread *_td);

#ifdef __HAVE_PTRACE_MACHDEP
int	cpu_ptrace(struct thread *_td, int _req, void *_addr, int _data);
#endif

/*
 * These are prototypes for functions that implement some of the
 * debugging functionality exported by procfs / linprocfs and by the
 * ptrace(2) syscall.  They used to be part of procfs, but they don't
 * really belong there.
 */
struct reg;
struct fpreg;
struct dbreg;
struct uio;
int	proc_read_regs(struct thread *_td, struct reg *_reg);
int	proc_write_regs(struct thread *_td, struct reg *_reg);
int	proc_read_fpregs(struct thread *_td, struct fpreg *_fpreg);
int	proc_write_fpregs(struct thread *_td, struct fpreg *_fpreg);
int	proc_read_dbregs(struct thread *_td, struct dbreg *_dbreg);
int	proc_write_dbregs(struct thread *_td, struct dbreg *_dbreg);
int	proc_sstep(struct thread *_td);
int	proc_rwmem(struct proc *_p, struct uio *_uio);
ssize_t	proc_readmem(struct thread *_td, struct proc *_p, vm_offset_t _va,
	    void *_buf, size_t _len);
ssize_t	proc_writemem(struct thread *_td, struct proc *_p, vm_offset_t _va,
	    void *_buf, size_t _len);
#ifdef COMPAT_FREEBSD32
struct reg32;
struct fpreg32;
struct dbreg32;
int	proc_read_regs32(struct thread *_td, struct reg32 *_reg32);
int	proc_write_regs32(struct thread *_td, struct reg32 *_reg32);
int	proc_read_fpregs32(struct thread *_td, struct fpreg32 *_fpreg32);
int	proc_write_fpregs32(struct thread *_td, struct fpreg32 *_fpreg32);
int	proc_read_dbregs32(struct thread *_td, struct dbreg32 *_dbreg32);
int	proc_write_dbregs32(struct thread *_td, struct dbreg32 *_dbreg32);
#endif
#else /* !_KERNEL */

#include <sys/cdefs.h>

__BEGIN_DECLS
int	ptrace(int _request, pid_t _pid, caddr_t _addr, int _data);
__END_DECLS

#endif /* !_KERNEL */

#endif	/* !_SYS_PTRACE_H_ */
