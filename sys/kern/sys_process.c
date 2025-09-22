/*	$OpenBSD: sys_process.c,v 1.106 2025/02/17 15:45:55 claudio Exp $	*/
/*	$NetBSD: sys_process.c,v 1.55 1996/05/15 06:17:47 tls Exp $	*/

/*-
 * Copyright (c) 1994 Christopher G. Demetriou.  All rights reserved.
 * Copyright (c) 1982, 1986, 1989, 1993
 *	The Regents of the University of California.  All rights reserved.
 * (c) UNIX System Laboratories, Inc.
 * All or some portions of this file are derived from material licensed
 * to the University of California by American Telephone and Telegraph
 * Co. or Unix System Laboratories, Inc. and are reproduced herein with
 * the permission of UNIX System Laboratories, Inc.
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
 *	from: @(#)sys_process.c	8.1 (Berkeley) 6/10/93
 */

/*
 * References:
 *	(1) Bach's "The Design of the UNIX Operating System",
 *	(2) sys/miscfs/procfs from UCB's 4.4BSD-Lite distribution,
 *	(3) the "4.4BSD Programmer's Reference Manual" published
 *		by USENIX and O'Reilly & Associates.
 * The 4.4BSD PRM does a reasonably good job of documenting what the various
 * ptrace() requests should actually do, and its text is quoted several times
 * in this file.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/exec.h>
#include <sys/proc.h>
#include <sys/signalvar.h>
#include <sys/errno.h>
#include <sys/malloc.h>
#include <sys/ptrace.h>
#include <sys/uio.h>
#include <sys/sched.h>
#include <sys/exec_elf.h>

#include <sys/mount.h>
#include <sys/syscallargs.h>

#include <uvm/uvm_extern.h>

#include <machine/reg.h>

#ifdef PTRACE

/*
 * Locks used to protect data:
 *	a	atomic
 */

static inline int	process_checktracestate(struct process *_curpr,
			    struct process *_tr, struct proc *_t);
static inline struct process *process_tprfind(pid_t _tpid, struct proc **_tp);

int	ptrace_ctrl(struct proc *, int, pid_t, caddr_t, int);
int	ptrace_ustate(struct proc *, int, pid_t, void *, int, register_t *);
int	ptrace_kstate(struct proc *, int, pid_t, void *);

int	global_ptrace;	/* [a] permit tracing of not children */


/*
 * Process debugging system call.
 */
int
sys_ptrace(struct proc *p, void *v, register_t *retval)
{
	struct sys_ptrace_args /* {
		syscallarg(int) req;
		syscallarg(pid_t) pid;
		syscallarg(caddr_t) addr;
		syscallarg(int) data;
	} */ *uap = v;
	int req = SCARG(uap, req);
	pid_t pid = SCARG(uap, pid);
	caddr_t uaddr = SCARG(uap, addr);	/* userspace */
	void *kaddr = NULL;			/* kernelspace */
	int data = SCARG(uap, data);
	union {
		struct ptrace_thread_state u_pts;
		struct ptrace_io_desc u_piod;
		struct ptrace_event u_pe;
		struct ptrace_state u_ps;
		register_t u_wcookie;
		register_t u_pacmask[2];
	} u;
	int size = 0;
	enum { NONE, IN, IN_ALLOC, OUT, OUT_ALLOC, IN_OUT } mode;
	int kstate = 0;
	int error;

	*retval = 0;

	/* Figure out what sort of copyin/out operations we'll do */
	switch (req) {
	case PT_TRACE_ME:
	case PT_CONTINUE:
	case PT_KILL:
	case PT_ATTACH:
	case PT_DETACH:
#ifdef PT_STEP
	case PT_STEP:
#endif
		/* control operations do no copyin/out; dispatch directly */
		return ptrace_ctrl(p, req, pid, uaddr, data);

	case PT_READ_I:
	case PT_READ_D:
	case PT_WRITE_I:
	case PT_WRITE_D:
		mode = NONE;
		break;
	case PT_IO:
		mode = IN_OUT;
		size = sizeof u.u_piod;
		data = size;	/* suppress the data == size check */
		break;
	case PT_GET_THREAD_FIRST:
		mode = OUT;
		size = sizeof u.u_pts;
		kstate = 1;
		break;
	case PT_GET_THREAD_NEXT:
		mode = IN_OUT;
		size = sizeof u.u_pts;
		kstate = 1;
		break;
	case PT_GET_EVENT_MASK:
		mode = OUT;
		size = sizeof u.u_pe;
		kstate = 1;
		break;
	case PT_SET_EVENT_MASK:
		mode = IN;
		size = sizeof u.u_pe;
		kstate = 1;
		break;
	case PT_GET_PROCESS_STATE:
		mode = OUT;
		size = sizeof u.u_ps;
		kstate = 1;
		break;
	case PT_GETREGS:
		mode = OUT_ALLOC;
		size = sizeof(struct reg);
		break;
	case PT_SETREGS:
		mode = IN_ALLOC;
		size = sizeof(struct reg);
		break;
#ifdef PT_GETFPREGS
	case PT_GETFPREGS:
		mode = OUT_ALLOC;
		size = sizeof(struct fpreg);
		break;
#endif
#ifdef PT_SETFPREGS
	case PT_SETFPREGS:
		mode = IN_ALLOC;
		size = sizeof(struct fpreg);
		break;
#endif
#ifdef PT_GETXMMREGS
	case PT_GETXMMREGS:
		mode = OUT_ALLOC;
		size = sizeof(struct xmmregs);
		break;
#endif
#ifdef PT_SETXMMREGS
	case PT_SETXMMREGS:
		mode = IN_ALLOC;
		size = sizeof(struct xmmregs);
		break;
#endif
#ifdef PT_WCOOKIE
	case PT_WCOOKIE:
		mode = OUT;
		size = sizeof u.u_wcookie;
		data = size;	/* suppress the data == size check */
		break;
#endif
#ifdef PT_PACMASK
	case PT_PACMASK:
		mode = OUT;
		size = sizeof u.u_pacmask;
		break;
#endif
#ifdef PT_GETXSTATE_INFO
	case PT_GETXSTATE_INFO:
		mode = OUT_ALLOC;
		size = sizeof(struct ptrace_xstate_info);
		break;
#endif
#ifdef PT_GETXSTATE
	case PT_GETXSTATE:
		mode = OUT_ALLOC;
		size = fpu_save_len;
		break;
#endif
#ifdef PT_SETXSTATE
	case PT_SETXSTATE:
		mode = IN_ALLOC;
		size = fpu_save_len;
		break;
#endif
	default:
		return EINVAL;
	}


	/* Now do any copyin()s and allocations in a consistent manner */
	switch (mode) {
	case NONE:
		kaddr = uaddr;
		break;
	case IN:
	case IN_OUT:
	case OUT:
		KASSERT(size <= sizeof u);
		if (data != size)
			return EINVAL;
		if (mode == OUT)
			memset(&u, 0, size);
		else { /* IN or IN_OUT */
			if ((error = copyin(uaddr, &u, size)))
				return error;
		}
		kaddr = &u;
		break;
	case IN_ALLOC:
		kaddr = malloc(size, M_TEMP, M_WAITOK);
		if ((error = copyin(uaddr, kaddr, size))) {
			free(kaddr, M_TEMP, size);
			return error;
		}
		break;
	case OUT_ALLOC:
		kaddr = malloc(size, M_TEMP, M_WAITOK | M_ZERO);
		break;
	}

	if (kstate)
		error = ptrace_kstate(p, req, pid, kaddr);
	else
		error = ptrace_ustate(p, req, pid, kaddr, data, retval);

	/* Do any copyout()s and frees */
	if (error == 0) {
		switch (mode) {
		case NONE:
		case IN:
		case IN_ALLOC:
			break;
		case IN_OUT:
		case OUT:
			error = copyout(&u, uaddr, size);
			if (req == PT_IO) {
				/* historically, errors here are ignored */
				error = 0;
			}
			break;
		case OUT_ALLOC:
			error = copyout(kaddr, uaddr, size);
			break;
		}
	}

	if (mode == IN_ALLOC || mode == OUT_ALLOC)
		free(kaddr, M_TEMP, size);
	return error;
}

/*
 * ptrace control requests: attach, detach, continue, kill, single-step, etc
 */
int
ptrace_ctrl(struct proc *p, int req, pid_t pid, caddr_t addr, int data)
{
	struct proc *t;				/* target thread */
	struct process *tr;			/* target process */
	int error = 0;

	switch (req) {
	case PT_TRACE_ME:
		/* Just set the trace flag. */
		tr = p->p_p;
		mtx_enter(&tr->ps_mtx);
		if (ISSET(tr->ps_flags, PS_TRACED)) {
			mtx_leave(&tr->ps_mtx);
			return EBUSY;
		}
		atomic_setbits_int(&tr->ps_flags, PS_TRACED);
		tr->ps_opptr = tr->ps_pptr;
		mtx_leave(&tr->ps_mtx);
		if (tr->ps_ptstat == NULL)
			tr->ps_ptstat = malloc(sizeof(*tr->ps_ptstat),
			    M_SUBPROC, M_WAITOK);
		memset(tr->ps_ptstat, 0, sizeof(*tr->ps_ptstat));
		return 0;

	/* calls that only operate on the PID */
	case PT_KILL:
	case PT_ATTACH:
	case PT_DETACH:
		/* Find the process we're supposed to be operating on. */
		if (pid > THREAD_PID_OFFSET) {
			error = ESRCH;
			goto fail;
		}
		/* FALLTHROUGH */

	/* calls that accept a PID or a thread ID */
	case PT_CONTINUE:
#ifdef PT_STEP
	case PT_STEP:
#endif
		if ((tr = process_tprfind(pid, &t)) == NULL) {
			error = ESRCH;
			goto fail;
		}
		break;
	}

	/* Check permissions/state */
	if (req != PT_ATTACH) {
		/* Check that the data is a valid signal number or zero. */
		if (req != PT_KILL && (data < 0 || data >= NSIG)) {
			error = EINVAL;
			goto fail;
		}

		/* Most operations require the target to already be traced */
		if ((error = process_checktracestate(p->p_p, tr, t)))
			goto fail;

		/* Do single-step fixup if needed. */
		FIX_SSTEP(t);
	} else {
		/*
		 * PT_ATTACH is the opposite; you can't attach to a process if:
		 *	(1) it's the process that's doing the attaching,
		 */
		if (tr == p->p_p) {
			error = EINVAL;
			goto fail;
		}

		/*
		 *	(2) it's a system process
		 */
		if (ISSET(tr->ps_flags, PS_SYSTEM)) {
			error = EPERM;
			goto fail;
		}

		/*
		 *	(3) it's already being traced, or
		 */
		if (ISSET(tr->ps_flags, PS_TRACED)) {
			error = EBUSY;
			goto fail;
		}

		/*
		 *	(4) it's in the middle of execve(2)
		 */
		if (ISSET(tr->ps_flags, PS_INEXEC)) {
			error = EAGAIN;
			goto fail;
		}

		/*
		 *	(5) it's not owned by you, or the last exec
		 *	    gave us setuid/setgid privs (unless
		 *	    you're root), or...
		 *
		 *      [Note: once PS_SUGID or PS_SUGIDEXEC gets set in
		 *	execve(), they stay set until the process does
		 *	another execve().  Hence this prevents a setuid
		 *	process which revokes its special privileges using
		 *	setuid() from being traced.  This is good security.]
		 */
		if ((tr->ps_ucred->cr_ruid != p->p_ucred->cr_ruid ||
		    ISSET(tr->ps_flags, PS_SUGIDEXEC | PS_SUGID)) &&
		    (error = suser(p)) != 0)
			goto fail;

		/*
		 * 	(5.5) it's not a child of the tracing process.
		 */
		if (atomic_load_int(&global_ptrace) == 0 &&
		    !inferior(tr, p->p_p) && (error = suser(p)) != 0)
			goto fail;

		/*
		 *	(6) ...it's init, which controls the security level
		 *	    of the entire system, and the system was not
		 *          compiled with permanently insecure mode turned
		 *	    on.
		 */
		if ((tr->ps_pid == 1) && (securelevel > -1)) {
			error = EPERM;
			goto fail;
		}

		/*
		 *	(7) it's an ancestor of the current process and
		 *	    not init (because that would create a loop in
		 *	    the process graph).
		 */
		if (tr->ps_pid != 1 && inferior(p->p_p, tr)) {
			error = EINVAL;
			goto fail;
		}
	}

	switch (req) {

#ifdef PT_STEP
	case PT_STEP:
		/*
		 * From the 4.4BSD PRM:
		 * "Execution continues as in request PT_CONTINUE; however
		 * as soon as possible after execution of at least one
		 * instruction, execution stops again. [ ... ]"
		 */
#endif
	case PT_CONTINUE:
		/*
		 * From the 4.4BSD PRM:
		 * "The data argument is taken as a signal number and the
		 * child's execution continues at location addr as if it
		 * incurred that signal.  Normally the signal number will
		 * be either 0 to indicate that the signal that caused the
		 * stop should be ignored, or that value fetched out of
		 * the process's image indicating which signal caused
		 * the stop.  If addr is (int *)1 then execution continues
		 * from where it stopped."
		 */

		/* If the address parameter is not (int *)1, set the pc. */
		if ((int *)addr != (int *)1)
			if ((error = process_set_pc(t, addr)) != 0)
				goto fail;

#ifdef PT_STEP
		/*
		 * Arrange for a single-step, if that's requested and possible.
		 */
		error = process_sstep(t, req == PT_STEP);
		if (error)
			goto fail;
#endif
		goto sendsig;

	case PT_DETACH:
		/*
		 * From the 4.4BSD PRM:
		 * "The data argument is taken as a signal number and the
		 * child's execution continues at location addr as if it
		 * incurred that signal.  Normally the signal number will
		 * be either 0 to indicate that the signal that caused the
		 * stop should be ignored, or that value fetched out of
		 * the process's image indicating which signal caused
		 * the stop.  If addr is (int *)1 then execution continues
		 * from where it stopped."
		 */

#ifdef PT_STEP
		/*
		 * Stop single stepping.
		 */
		error = process_sstep(t, 0);
		if (error)
			goto fail;
#endif

		mtx_enter(&tr->ps_mtx);
		process_untrace(tr);
		atomic_clearbits_int(&tr->ps_flags, PS_WAITED);
		mtx_leave(&tr->ps_mtx);

	sendsig:
		memset(tr->ps_ptstat, 0, sizeof(*tr->ps_ptstat));

		/* Finally, deliver the requested signal (or none). */
		mtx_enter(&tr->ps_mtx);
		if (tr->ps_trapped == t) {
			SCHED_LOCK();
			if (pid >= THREAD_PID_OFFSET)
				atomic_setbits_int(&t->p_flag,
				    P_TRACESINGLE);
			tr->ps_xsig = data;
			unsleep(t);
			setrunnable(t);
			SCHED_UNLOCK();
			mtx_leave(&tr->ps_mtx);
		} else if (pid < THREAD_PID_OFFSET) {
			mtx_leave(&tr->ps_mtx);
			if (data != 0)
				ptsignal(t, data, SPROCESS);
		} else {
			mtx_leave(&tr->ps_mtx);
			/* can not signal a single thread */
			error = EINVAL;
			goto fail;
		}
		break;

	case PT_KILL:
		/* just send the process a KILL signal. */
		data = SIGKILL;
		goto sendsig;	/* in PT_DETACH, above. */

	case PT_ATTACH:
		/*
		 * As was done in procfs:
		 * Go ahead and set the trace flag.
		 * Save the old parent (it's reset in
		 *   _DETACH, and also in kern_exit.c:wait4()
		 * Reparent the process so that the tracing
		 *   proc gets to see all the action.
		 * Stop the target.
		 */
		mtx_enter(&tr->ps_mtx);
		atomic_setbits_int(&tr->ps_flags, PS_TRACED);
		tr->ps_opptr = tr->ps_pptr;
		process_reparent(tr, p->p_p);
		mtx_leave(&tr->ps_mtx);
		if (tr->ps_ptstat == NULL)
			tr->ps_ptstat = malloc(sizeof(*tr->ps_ptstat),
			    M_SUBPROC, M_WAITOK);
		data = SIGSTOP;
		goto sendsig;
	default:
		KASSERTMSG(0, "%s: unhandled request %d", __func__, req);
		break;
	}

fail:
	return error;
}

/*
 * ptrace kernel-state requests: thread list, event mask, process state
 */
int
ptrace_kstate(struct proc *p, int req, pid_t pid, void *addr)
{
	struct process *tr;			/* target process */
	struct ptrace_event *pe = addr;
	int error;

	KASSERT((p->p_flag & P_SYSTEM) == 0);

	/* Find the process we're supposed to be operating on. */
	if ((tr = prfind(pid)) == NULL)
		return ESRCH;

	if ((error = process_checktracestate(p->p_p, tr, NULL)))
		return error;

	switch (req) {
	case PT_GET_THREAD_FIRST:
	case PT_GET_THREAD_NEXT:
	      {
		struct ptrace_thread_state *pts = addr;
		struct proc *t;

		if (req == PT_GET_THREAD_NEXT) {
			t = tfind_user(pts->pts_tid, tr);
			if (t == NULL || ISSET(t->p_flag, P_WEXIT))
				return ESRCH;
			t = TAILQ_NEXT(t, p_thr_link);
		} else {
			t = TAILQ_FIRST(&tr->ps_threads);
		}

		if (t == NULL)
			pts->pts_tid = -1;
		else
			pts->pts_tid = t->p_tid + THREAD_PID_OFFSET;
		return 0;
	      }
	}

	switch (req) {
	case PT_GET_EVENT_MASK:
		pe->pe_set_event = tr->ps_ptmask;
		break;
	case PT_SET_EVENT_MASK:
		tr->ps_ptmask = pe->pe_set_event;
		break;
	case PT_GET_PROCESS_STATE:
		mtx_enter(&tr->ps_mtx);
		if (tr->ps_trapped != NULL)
			tr->ps_ptstat->pe_tid = tr->ps_trapped->p_tid +
			    THREAD_PID_OFFSET;
		else
			tr->ps_ptstat->pe_tid = 0;
		mtx_leave(&tr->ps_mtx);
		memcpy(addr, tr->ps_ptstat, sizeof *tr->ps_ptstat);
		break;
	default:
		KASSERTMSG(0, "%s: unhandled request %d", __func__, req);
		break;
	}

	return 0;
}

/*
 * ptrace user-state requests: memory access, registers, stack cookie
 */
int
ptrace_ustate(struct proc *p, int req, pid_t pid, void *addr, int data,
    register_t *retval)
{
	struct proc *t;				/* target thread */
	struct process *tr;			/* target process */
	struct uio uio;
	struct iovec iov;
	int error, write;
	int temp = 0;

	KASSERT((p->p_flag & P_SYSTEM) == 0);

	/* Accept either PID or TID */
	if ((tr = process_tprfind(pid, &t)) == NULL)
		return ESRCH;

	if ((error = process_checktracestate(p->p_p, tr, t)))
		return error;

	FIX_SSTEP(t);

	/* Now do the operation. */
	write = 0;

	if ((error = process_checkioperm(p, tr)) != 0)
		return error;

	switch (req) {
	case PT_WRITE_I:		/* XXX no separate I and D spaces */
	case PT_WRITE_D:
		write = 1;
		temp = data;
	case PT_READ_I:		/* XXX no separate I and D spaces */
	case PT_READ_D:
		/* write = 0 done above. */
		iov.iov_base = (caddr_t)&temp;
		iov.iov_len = sizeof(int);
		uio.uio_iov = &iov;
		uio.uio_iovcnt = 1;
		uio.uio_offset = (off_t)(vaddr_t)addr;
		uio.uio_resid = sizeof(int);
		uio.uio_segflg = UIO_SYSSPACE;
		uio.uio_rw = write ? UIO_WRITE : UIO_READ;
		uio.uio_procp = p;
		error = process_domem(p, tr, &uio, write ? PT_WRITE_I :
				PT_READ_I);
		if (write == 0)
			*retval = temp;
		return error;

	case PT_IO:
	      {
		struct ptrace_io_desc *piod = addr;

		iov.iov_base = piod->piod_addr;
		iov.iov_len = piod->piod_len;
		uio.uio_iov = &iov;
		uio.uio_iovcnt = 1;
		uio.uio_offset = (off_t)(vaddr_t)piod->piod_offs;
		uio.uio_resid = piod->piod_len;
		uio.uio_segflg = UIO_USERSPACE;
		uio.uio_procp = p;
		switch (piod->piod_op) {
		case PIOD_READ_I:
			req = PT_READ_I;
			uio.uio_rw = UIO_READ;
			break;
		case PIOD_READ_D:
			req = PT_READ_D;
			uio.uio_rw = UIO_READ;
			break;
		case PIOD_WRITE_I:
			req = PT_WRITE_I;
			uio.uio_rw = UIO_WRITE;
			break;
		case PIOD_WRITE_D:
			req = PT_WRITE_D;
			uio.uio_rw = UIO_WRITE;
			break;
		case PIOD_READ_AUXV:
			req = PT_READ_D;
			uio.uio_rw = UIO_READ;
			temp = ELF_AUX_WORDS * sizeof(char *);
			if (uio.uio_offset > temp)
				return EIO;
			if (uio.uio_resid > temp - uio.uio_offset)
				uio.uio_resid = temp - uio.uio_offset;
			piod->piod_len = iov.iov_len = uio.uio_resid;
			uio.uio_offset += tr->ps_auxinfo;
#ifdef MACHINE_STACK_GROWS_UP
			if (uio.uio_offset < (off_t)tr->ps_strings)
				return EIO;
#else
			if (uio.uio_offset > (off_t)tr->ps_strings)
				return EIO;
			if ((uio.uio_offset + uio.uio_resid) >
			    (off_t)tr->ps_strings)
				uio.uio_resid = (off_t)tr->ps_strings -
				    uio.uio_offset;
#endif
			break;
		default:
			return EINVAL;
		}
		error = process_domem(p, tr, &uio, req);
		piod->piod_len -= uio.uio_resid;
		return error;
	      }

	case PT_SETREGS:
		return process_write_regs(t, addr);
	case PT_GETREGS:
		return process_read_regs(t, addr);

#ifdef PT_SETFPREGS
	case PT_SETFPREGS:
		return process_write_fpregs(t, addr);
#endif
#ifdef PT_SETFPREGS
	case PT_GETFPREGS:
		return process_read_fpregs(t, addr);
#endif
#ifdef PT_SETXMMREGS
	case PT_SETXMMREGS:
		return process_write_xmmregs(t, addr);
#endif
#ifdef PT_SETXMMREGS
	case PT_GETXMMREGS:
		return process_read_xmmregs(t, addr);
#endif
#ifdef PT_WCOOKIE
	case PT_WCOOKIE:
		*(register_t *)addr = process_get_wcookie(t);
		return 0;
#endif
#ifdef PT_PACMASK
	case PT_PACMASK:
		((register_t *)addr)[0] = process_get_pacmask(t);
		((register_t *)addr)[1] = process_get_pacmask(t);
		return 0;
#endif
#ifdef PT_GETXSTATE_INFO
	case PT_GETXSTATE_INFO:
		return process_read_xstate_info(t, addr);
#endif
#ifdef PT_GETXSTATE
	case PT_GETXSTATE:
		return process_read_xstate(t, addr);
#endif
#ifdef PT_SETXSTATE
	case PT_SETXSTATE:
		return process_write_xstate(t, addr);
#endif
	default:
		KASSERTMSG(0, "%s: unhandled request %d", __func__, req);
		break;
	}

	return 0;
}


/*
 * Helper for doing "it could be a PID or TID" lookup.  On failure
 * returns NULL; on success returns the selected process and sets *tp
 * to an appropriate thread in that process.
 */
static inline struct process *
process_tprfind(pid_t tpid, struct proc **tp)
{
	struct process *tr;
	struct proc *t;

	if (tpid > THREAD_PID_OFFSET) {
		t = tfind(tpid - THREAD_PID_OFFSET);
		if (t == NULL)
			return NULL;
		tr = t->p_p;
	} else {
		tr = prfind(tpid);
		if (tr == NULL)
			return NULL;
		mtx_enter(&tr->ps_mtx);
		if (tr->ps_trapped != NULL)
			t = tr->ps_trapped;
		else
			t = TAILQ_FIRST(&tr->ps_threads);
		mtx_leave(&tr->ps_mtx);
	}

	*tp = t;
	return tr;
}


/*
 * Check whether 'tr' is currently traced by 'curpr' and in a state
 * to be manipulated.  If 't' is supplied then it must be stopped and
 * waited for.
 */
static inline int
process_checktracestate(struct process *curpr, struct process *tr,
    struct proc *t)
{
	/*
	 * You can't do what you want to the process if:
	 *	(1) It's not being traced at all,
	 */
	if (!ISSET(tr->ps_flags, PS_TRACED))
		return EPERM;

	/*
	 *	(2) it's not being traced by _you_, or
	 */
	if (tr->ps_pptr != curpr)
		return EBUSY;

	/*
	 *	(3) it's in the middle of execve(2)
	 */
	if (ISSET(tr->ps_flags, PS_INEXEC))
		return EAGAIN;

	/*
	 *	(4) if a thread was specified and it's not currently stopped.
	 */
	if (t != NULL &&
	    (t->p_stat != SSTOP || !ISSET(tr->ps_flags, PS_WAITED)))
		return EBUSY;

	return 0;
}

#endif /* PTRACE */

/*
 * Check if a process is allowed to fiddle with the memory of another.
 *
 * p = tracer
 * tr = tracee
 *
 * 1.  You can't attach to a process not owned by you or one that has raised
 *     its privileges.
 * 1a. ...unless you are root.
 *
 * 2.  init is always off-limits because it can control the securelevel.
 * 2a. ...unless securelevel is permanently set to insecure.
 *
 * 3.  Processes that are in the process of doing an exec() are always
 *     off-limits because of the can of worms they are. Just wait a
 *     second.
 */
int
process_checkioperm(struct proc *p, struct process *tr)
{
	int error;

	if ((tr->ps_ucred->cr_ruid != p->p_ucred->cr_ruid ||
	    ISSET(tr->ps_flags, PS_SUGIDEXEC | PS_SUGID)) &&
	    (error = suser(p)) != 0)
		return (error);

	if ((tr->ps_pid == 1) && (securelevel > -1))
		return (EPERM);

	if (ISSET(tr->ps_flags, PS_INEXEC))
		return (EAGAIN);

	return (0);
}

int
process_domem(struct proc *curp, struct process *tr, struct uio *uio, int req)
{
	struct vmspace *vm;
	int error;
	vaddr_t addr;
	vsize_t len;

	len = uio->uio_resid;
	if (len == 0)
		return 0;

	if ((error = process_checkioperm(curp, tr)) != 0)
		return error;

	vm = tr->ps_vmspace;
	if ((tr->ps_flags & PS_EXITING) || (vm->vm_refcnt < 1))
		return EFAULT;
	addr = uio->uio_offset;

	uvmspace_addref(vm);

	error = uvm_io(&vm->vm_map, uio, UVM_IO_FIXPROT);

	uvmspace_free(vm);

	if (error == 0 && req == PT_WRITE_I)
		pmap_proc_iflush(tr, addr, len);

	return error;
}
