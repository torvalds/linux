/*	$OpenBSD: ptrace.h,v 1.16 2020/03/16 11:58:46 mpi Exp $	*/
/*	$NetBSD: ptrace.h,v 1.21 1996/02/09 18:25:26 christos Exp $	*/

/*-
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
 */

#ifndef	_SYS_PTRACE_H_
#define	_SYS_PTRACE_H_

#define	PT_TRACE_ME	0	/* child declares it's being traced */
#define	PT_READ_I	1	/* read word in child's I space */
#define	PT_READ_D	2	/* read word in child's D space */
#define	PT_WRITE_I	4	/* write word in child's I space */
#define	PT_WRITE_D	5	/* write word in child's D space */
#define	PT_CONTINUE	7	/* continue the child */
#define	PT_KILL		8	/* kill the child process */
#define	PT_ATTACH	9	/* attach to running process */
#define	PT_DETACH	10	/* detach from running process */
#define PT_IO		11	/* do I/O to/from the stopped process. */

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
#define PIOD_READ_AUXV	5	/* Read from aux array */

#define PT_SET_EVENT_MASK	12
#define PT_GET_EVENT_MASK	13

typedef struct ptrace_event {
	int	pe_set_event;
} ptrace_event_t;

#define PTRACE_FORK	0x0002	/* Report forks */

#define PT_GET_PROCESS_STATE	14

typedef struct ptrace_state {
	int	pe_report_event;
	pid_t	pe_other_pid;
	pid_t	pe_tid;
} ptrace_state_t;

#define PT_GET_THREAD_FIRST	15
#define PT_GET_THREAD_NEXT	16

struct ptrace_thread_state {
	pid_t	pts_tid;
};

#define	PT_FIRSTMACH	32	/* for machine-specific requests */
#include <machine/ptrace.h>	/* machine-specific requests, if any */

#ifdef _KERNEL

/*
 * There is a bunch of PT_ requests that are machine dependent, but not
 * optional. Check if they were defined by MD code here.
 */
#if !defined(PT_GETREGS) || !defined(PT_SETREGS)
#error Machine dependent ptrace not complete.
#endif

struct reg;
#if defined(PT_GETFPREGS) || defined(PT_SETFPREGS)
struct fpreg;
#endif

void	process_reparent(struct process *_child, struct process *_newparent);
void	process_untrace(struct process *_tr);
#ifdef PT_GETFPREGS
int	process_read_fpregs(struct proc *_t, struct fpreg *);
#endif
int	process_read_regs(struct proc *_t, struct reg *);
int	process_set_pc(struct proc *_t, caddr_t _addr);
int	process_sstep(struct proc *_t, int _sstep);
#ifdef PT_SETFPREGS
int	process_write_fpregs(struct proc *_t, struct fpreg *);
#endif
int	process_write_regs(struct proc *_t, struct reg *);
int	process_checkioperm(struct proc *_curp, struct process *_tr);
int	process_domem(struct proc *_curp, struct process *_tr, struct uio *,
	    int _req);

#ifndef FIX_SSTEP
#define FIX_SSTEP(p)
#endif

#else /* !_KERNEL */

#include <sys/cdefs.h>

__BEGIN_DECLS
int	ptrace(int _request, pid_t _pid, caddr_t _addr, int _data);
__END_DECLS

#endif /* !_KERNEL */

#endif	/* !_SYS_PTRACE_H_ */
