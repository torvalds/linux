/*	$OpenBSD: kern_ktrace.c,v 1.115 2024/12/27 11:57:16 mpi Exp $	*/
/*	$NetBSD: kern_ktrace.c,v 1.23 1996/02/09 18:59:36 christos Exp $	*/

/*
 * Copyright (c) 1989, 1993
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
 *	@(#)kern_ktrace.c	8.2 (Berkeley) 9/23/93
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/proc.h>
#include <sys/sched.h>
#include <sys/fcntl.h>
#include <sys/namei.h>
#include <sys/vnode.h>
#include <sys/lock.h>
#include <sys/ktrace.h>
#include <sys/malloc.h>
#include <sys/syslog.h>
#include <sys/sysctl.h>
#include <sys/pledge.h>

#include <sys/mount.h>
#include <sys/syscall.h>
#include <sys/syscallargs.h>

void	ktrinitheaderraw(struct ktr_header *, uint, pid_t, pid_t);
void	ktrinitheader(struct ktr_header *, struct proc *, int);
int	ktrstart(struct proc *, struct vnode *, struct ucred *);
int	ktrops(struct proc *, struct process *, int, int, struct vnode *,
	    struct ucred *);
int	ktrsetchildren(struct proc *, struct process *, int, int,
	    struct vnode *, struct ucred *);
int	ktrwrite(struct proc *, struct ktr_header *, const void *, size_t);
int	ktrwrite2(struct proc *, struct ktr_header *, const void *, size_t,
	    const void *, size_t);
int	ktrwriteraw(struct proc *, struct vnode *, struct ucred *,
	    struct ktr_header *, struct iovec *);
int	ktrcanset(struct proc *, struct process *);

/*
 * Clear the trace settings in a correct way (to avoid races).
 */
void
ktrcleartrace(struct process *pr)
{
	struct vnode *vp;
	struct ucred *cred;

	if (pr->ps_tracevp != NULL) {
		vp = pr->ps_tracevp;
		cred = pr->ps_tracecred;

		pr->ps_traceflag = 0;
		pr->ps_tracevp = NULL;
		pr->ps_tracecred = NULL;

		vp->v_writecount--;
		vrele(vp);
		crfree(cred);
	}
}

/*
 * Change the trace setting in a correct way (to avoid races).
 */
void
ktrsettrace(struct process *pr, int facs, struct vnode *newvp,
    struct ucred *newcred)
{
	struct vnode *oldvp;
	struct ucred *oldcred;

	KASSERT(newvp != NULL);
	KASSERT(newcred != NULL);

	pr->ps_traceflag |= facs;

	/* nothing to change about where the trace goes? */
	if (pr->ps_tracevp == newvp && pr->ps_tracecred == newcred)
		return;

	vref(newvp);
	crhold(newcred);
	newvp->v_writecount++;

	oldvp = pr->ps_tracevp;
	oldcred = pr->ps_tracecred;

	pr->ps_tracevp = newvp;
	pr->ps_tracecred = newcred;

	if (oldvp != NULL) {
		oldvp->v_writecount--;
		vrele(oldvp);
		crfree(oldcred);
	}
}

void
ktrinitheaderraw(struct ktr_header *kth, uint type, pid_t pid, pid_t tid)
{
	memset(kth, 0, sizeof(struct ktr_header));
	kth->ktr_type = type;
	kth->ktr_pid = pid;
	kth->ktr_tid = tid;
}

void
ktrinitheader(struct ktr_header *kth, struct proc *p, int type)
{
	struct process *pr = p->p_p;

	ktrinitheaderraw(kth, type, pr->ps_pid, p->p_tid + THREAD_PID_OFFSET);
	memcpy(kth->ktr_comm, pr->ps_comm, sizeof(kth->ktr_comm));
}

int
ktrstart(struct proc *p, struct vnode *vp, struct ucred *cred)
{
	struct ktr_header kth;

	ktrinitheaderraw(&kth, htobe32(KTR_START), -1, -1);
	return (ktrwriteraw(p, vp, cred, &kth, NULL));
}

void
ktrsyscall(struct proc *p, register_t code, size_t argsize, register_t args[])
{
	struct	ktr_header kth;
	struct	ktr_syscall *ktp;
	size_t len = sizeof(struct ktr_syscall) + argsize;
	register_t *argp;
	u_int nargs = 0;
	int i;

	if (code == SYS_sysctl) {
		/*
		 * The sysctl encoding stores the mib[]
		 * array because it is interesting.
		 */
		if (args[1] > 0)
			nargs = lmin(args[1], CTL_MAXNAME);
		len += nargs * sizeof(int);
	}
	atomic_setbits_int(&p->p_flag, P_INKTR);
	ktrinitheader(&kth, p, KTR_SYSCALL);
	ktp = malloc(len, M_TEMP, M_WAITOK);
	ktp->ktr_code = code;
	ktp->ktr_argsize = argsize;
	argp = (register_t *)((char *)ktp + sizeof(struct ktr_syscall));
	for (i = 0; i < (argsize / sizeof *argp); i++)
		*argp++ = args[i];
	if (nargs && copyin((void *)args[0], argp, nargs * sizeof(int)))
		memset(argp, 0, nargs * sizeof(int));
	KERNEL_LOCK();
	ktrwrite(p, &kth, ktp, len);
	KERNEL_UNLOCK();
	free(ktp, M_TEMP, len);
	atomic_clearbits_int(&p->p_flag, P_INKTR);
}

void
ktrsysret(struct proc *p, register_t code, int error,
    const register_t retval[2])
{
	struct ktr_header kth;
	struct ktr_sysret ktp;
	int len;

	atomic_setbits_int(&p->p_flag, P_INKTR);
	ktrinitheader(&kth, p, KTR_SYSRET);
	ktp.ktr_code = code;
	ktp.ktr_error = error;
	if (error)
		len = 0;
	else if (code == SYS_lseek)
		/* the one exception: lseek on ILP32 needs more */
		len = sizeof(long long);
	else
		len = sizeof(register_t);
	KERNEL_LOCK();
	ktrwrite2(p, &kth, &ktp, sizeof(ktp), retval, len);
	KERNEL_UNLOCK();
	atomic_clearbits_int(&p->p_flag, P_INKTR);
}

void
ktrnamei(struct proc *p, char *path)
{
	struct ktr_header kth;

	atomic_setbits_int(&p->p_flag, P_INKTR);
	ktrinitheader(&kth, p, KTR_NAMEI);
	KERNEL_LOCK();
	ktrwrite(p, &kth, path, strlen(path));
	KERNEL_UNLOCK();
	atomic_clearbits_int(&p->p_flag, P_INKTR);
}

void
ktrgenio(struct proc *p, int fd, enum uio_rw rw, struct iovec *iov,
    ssize_t len)
{
	struct ktr_header kth;
	struct ktr_genio ktp;
	caddr_t cp;
	int count, error;
	int buflen;

	atomic_setbits_int(&p->p_flag, P_INKTR);

	/* beware overflow */
	if (len > PAGE_SIZE)
		buflen = PAGE_SIZE;
	else
		buflen = len + sizeof(struct ktr_genio);

	ktrinitheader(&kth, p, KTR_GENIO);
	ktp.ktr_fd = fd;
	ktp.ktr_rw = rw;

	cp = malloc(buflen, M_TEMP, M_WAITOK);
	while (len > 0) {
		/*
		 * Don't allow this process to hog the cpu when doing
		 * huge I/O.
		 */
		sched_pause(preempt);

		count = lmin(iov->iov_len, buflen);
		if (count > len)
			count = len;
		if (copyin(iov->iov_base, cp, count))
			break;

		KERNEL_LOCK();
		error = ktrwrite2(p, &kth, &ktp, sizeof(ktp), cp, count);
		KERNEL_UNLOCK();
		if (error != 0)
			break;

		iov->iov_len -= count;
		iov->iov_base = (caddr_t)iov->iov_base + count;

		if (iov->iov_len == 0)
			iov++;

		len -= count;
	}

	free(cp, M_TEMP, buflen);
	atomic_clearbits_int(&p->p_flag, P_INKTR);
}

void
ktrpsig(struct proc *p, int sig, sig_t action, int mask, int code,
    siginfo_t *si)
{
	struct ktr_header kth;
	struct ktr_psig kp;

	atomic_setbits_int(&p->p_flag, P_INKTR);
	ktrinitheader(&kth, p, KTR_PSIG);
	kp.signo = (char)sig;
	kp.action = action;
	kp.mask = mask;
	kp.code = code;
	kp.si = *si;

	KERNEL_LOCK();
	ktrwrite(p, &kth, &kp, sizeof(kp));
	KERNEL_UNLOCK();
	atomic_clearbits_int(&p->p_flag, P_INKTR);
}

void
ktrstruct(struct proc *p, const char *name, const void *data, size_t datalen)
{
	struct ktr_header kth;

	atomic_setbits_int(&p->p_flag, P_INKTR);
	ktrinitheader(&kth, p, KTR_STRUCT);

	if (data == NULL)
		datalen = 0;
	KERNEL_LOCK();
	ktrwrite2(p, &kth, name, strlen(name) + 1, data, datalen);
	KERNEL_UNLOCK();
	atomic_clearbits_int(&p->p_flag, P_INKTR);
}

int
ktruser(struct proc *p, const char *id, const void *addr, size_t len)
{
	struct ktr_header kth;
	struct ktr_user ktp;
	int error;
	void *memp;
#define	STK_PARAMS	128
	long long stkbuf[STK_PARAMS / sizeof(long long)];

	if (!KTRPOINT(p, KTR_USER))
		return (0);
	if (len > KTR_USER_MAXLEN)
		return (EINVAL);

	atomic_setbits_int(&p->p_flag, P_INKTR);
	ktrinitheader(&kth, p, KTR_USER);
	memset(ktp.ktr_id, 0, KTR_USER_MAXIDLEN);
	error = copyinstr(id, ktp.ktr_id, KTR_USER_MAXIDLEN, NULL);
	if (error == 0) {
		if (len > sizeof(stkbuf))
			memp = malloc(len, M_TEMP, M_WAITOK);
		else
			memp = stkbuf;
		error = copyin(addr, memp, len);
		if (error == 0) {
			KERNEL_LOCK();
			ktrwrite2(p, &kth, &ktp, sizeof(ktp), memp, len);
			KERNEL_UNLOCK();
		}
		if (memp != stkbuf)
			free(memp, M_TEMP, len);
	}
	atomic_clearbits_int(&p->p_flag, P_INKTR);
	return (error);
}

void
ktrexec(struct proc *p, int type, const char *data, ssize_t len)
{
	struct ktr_header kth;
	int count, error;
	int buflen;

	assert(type == KTR_EXECARGS || type == KTR_EXECENV);
	atomic_setbits_int(&p->p_flag, P_INKTR);

	/* beware overflow */
	if (len > PAGE_SIZE)
		buflen = PAGE_SIZE;
	else
		buflen = len;

	ktrinitheader(&kth, p, type);

	while (len > 0) {
		/*
		 * Don't allow this process to hog the cpu when doing
		 * huge I/O.
		 */
		sched_pause(preempt);

		count = lmin(len, buflen);
		KERNEL_LOCK();
		error = ktrwrite(p, &kth, data, count);
		KERNEL_UNLOCK();
		if (error != 0)
			break;

		len -= count;
		data += count;
	}

	atomic_clearbits_int(&p->p_flag, P_INKTR);
}

void
ktrpledge(struct proc *p, int error, uint64_t code, int syscall)
{
	struct ktr_header kth;
	struct ktr_pledge kp;

	atomic_setbits_int(&p->p_flag, P_INKTR);
	ktrinitheader(&kth, p, KTR_PLEDGE);
	kp.error = error;
	kp.code = code;
	kp.syscall = syscall;

	KERNEL_LOCK();
	ktrwrite(p, &kth, &kp, sizeof(kp));
	KERNEL_UNLOCK();
	atomic_clearbits_int(&p->p_flag, P_INKTR);
}

void
ktrpinsyscall(struct proc *p, int error, int syscall, vaddr_t addr)
{
	struct ktr_header kth;
	struct ktr_pinsyscall kp;

	atomic_setbits_int(&p->p_flag, P_INKTR);
	ktrinitheader(&kth, p, KTR_PINSYSCALL);
	kp.error = error;
	kp.syscall = syscall;
	kp.addr = addr;

	KERNEL_LOCK();
	ktrwrite(p, &kth, &kp, sizeof(kp));
	KERNEL_UNLOCK();
	atomic_clearbits_int(&p->p_flag, P_INKTR);
}

/* Interface and common routines */

int
doktrace(struct vnode *vp, int ops, int facs, pid_t pid, struct proc *p)
{
	struct process *pr = NULL;
	struct ucred *cred = NULL;
	struct pgrp *pg;
	int descend = ops & KTRFLAG_DESCEND;
	int ret = 0;
	int error = 0;

	facs = facs & ~((unsigned)KTRFAC_ROOT);
	ops = KTROP(ops);

	if (ops != KTROP_CLEAR) {
		/*
		 * an operation which requires a file argument.
		 */
		cred = p->p_ucred;
		if (!vp) {
			error = EINVAL;
			goto done;
		}
		if (vp->v_type != VREG) {
			error = EACCES;
			goto done;
		}
	}
	/*
	 * Clear all uses of the tracefile
	 */
	if (ops == KTROP_CLEARFILE) {
		LIST_FOREACH(pr, &allprocess, ps_list) {
			if (pr->ps_tracevp == vp) {
				if (ktrcanset(p, pr))
					ktrcleartrace(pr);
				else
					error = EPERM;
			}
		}
		goto done;
	}
	/*
	 * need something to (un)trace (XXX - why is this here?)
	 */
	if (!facs) {
		error = EINVAL;
		goto done;
	}
	if (ops == KTROP_SET) {
		if (suser(p) == 0)
			facs |= KTRFAC_ROOT;
		error = ktrstart(p, vp, cred);
		if (error != 0)
			goto done;
	}
	/*
	 * do it
	 */
	if (pid < 0) {
		/*
		 * by process group
		 */
		pg = pgfind(-pid);
		if (pg == NULL) {
			error = ESRCH;
			goto done;
		}
		LIST_FOREACH(pr, &pg->pg_members, ps_pglist) {
			if (descend)
				ret |= ktrsetchildren(p, pr, ops, facs, vp,
				    cred);
			else
				ret |= ktrops(p, pr, ops, facs, vp, cred);
		}
	} else {
		/*
		 * by pid
		 */
		pr = prfind(pid);
		if (pr == NULL) {
			error = ESRCH;
			goto done;
		}
		if (descend)
			ret |= ktrsetchildren(p, pr, ops, facs, vp, cred);
		else
			ret |= ktrops(p, pr, ops, facs, vp, cred);
	}
	if (!ret)
		error = EPERM;
done:
	return (error);
}

/*
 * ktrace system call
 */
int
sys_ktrace(struct proc *p, void *v, register_t *retval)
{
	struct sys_ktrace_args /* {
		syscallarg(const char *) fname;
		syscallarg(int) ops;
		syscallarg(int) facs;
		syscallarg(pid_t) pid;
	} */ *uap = v;
	struct vnode *vp = NULL;
	const char *fname = SCARG(uap, fname);
	struct ucred *cred = NULL;
	int error;

	if (fname) {
		struct nameidata nd;

		cred = p->p_ucred;
		NDINIT(&nd, 0, 0, UIO_USERSPACE, fname, p);
		nd.ni_pledge = PLEDGE_CPATH | PLEDGE_WPATH;
		nd.ni_unveil = UNVEIL_CREATE | UNVEIL_WRITE;
		if ((error = vn_open(&nd, FWRITE|O_NOFOLLOW, 0)) != 0)
			return error;
		vp = nd.ni_vp;

		VOP_UNLOCK(vp);
	}

	error = doktrace(vp, SCARG(uap, ops), SCARG(uap, facs),
	    SCARG(uap, pid), p);
	if (vp != NULL)
		(void)vn_close(vp, FWRITE, cred, p);

	return error;
}

int
ktrops(struct proc *curp, struct process *pr, int ops, int facs,
    struct vnode *vp, struct ucred *cred)
{
	if (!ktrcanset(curp, pr))
		return (0);
	if (ops == KTROP_SET)
		ktrsettrace(pr, facs, vp, cred);
	else {
		/* KTROP_CLEAR */
		pr->ps_traceflag &= ~facs;
		if ((pr->ps_traceflag & KTRFAC_MASK) == 0) {
			/* cleared all the facility bits, so stop completely */
			ktrcleartrace(pr);
		}
	}

	return (1);
}

int
ktrsetchildren(struct proc *curp, struct process *top, int ops, int facs,
    struct vnode *vp, struct ucred *cred)
{
	struct process *pr;
	int ret = 0;

	pr = top;
	for (;;) {
		ret |= ktrops(curp, pr, ops, facs, vp, cred);
		/*
		 * If this process has children, descend to them next,
		 * otherwise do any siblings, and if done with this level,
		 * follow back up the tree (but not past top).
		 */
		if (!LIST_EMPTY(&pr->ps_children))
			pr = LIST_FIRST(&pr->ps_children);
		else for (;;) {
			if (pr == top)
				return (ret);
			if (LIST_NEXT(pr, ps_sibling) != NULL) {
				pr = LIST_NEXT(pr, ps_sibling);
				break;
			}
			pr = pr->ps_pptr;
		}
	}
	/*NOTREACHED*/
}

int
ktrwrite(struct proc *p, struct ktr_header *kth, const void *aux, size_t len)
{
	struct vnode *vp = p->p_p->ps_tracevp;
	struct ucred *cred = p->p_p->ps_tracecred;
	struct iovec data[2];
	int error;

	if (vp == NULL)
		return 0;
	crhold(cred);
	data[0].iov_base = (void *)aux;
	data[0].iov_len = len;
	data[1].iov_len = 0;
	kth->ktr_len = len;
	error = ktrwriteraw(p, vp, cred, kth, data);
	crfree(cred);
	return (error);
}

int
ktrwrite2(struct proc *p, struct ktr_header *kth, const void *aux1,
    size_t len1, const void *aux2, size_t len2)
{
	struct vnode *vp = p->p_p->ps_tracevp;
	struct ucred *cred = p->p_p->ps_tracecred;
	struct iovec data[2];
	int error;

	if (vp == NULL)
		return 0;
	crhold(cred);
	data[0].iov_base = (void *)aux1;
	data[0].iov_len = len1;
	data[1].iov_base = (void *)aux2;
	data[1].iov_len = len2;
	kth->ktr_len = len1 + len2;
	error = ktrwriteraw(p, vp, cred, kth, data);
	crfree(cred);
	return (error);
}

int
ktrwriteraw(struct proc *curp, struct vnode *vp, struct ucred *cred,
    struct ktr_header *kth, struct iovec *data)
{
	struct uio auio;
	struct iovec aiov[3];
	struct process *pr;
	int error;

	nanotime(&kth->ktr_time);

	KERNEL_ASSERT_LOCKED();

	auio.uio_iov = &aiov[0];
	auio.uio_offset = 0;
	auio.uio_segflg = UIO_SYSSPACE;
	auio.uio_rw = UIO_WRITE;
	aiov[0].iov_base = (caddr_t)kth;
	aiov[0].iov_len = sizeof(struct ktr_header);
	auio.uio_resid = sizeof(struct ktr_header);
	auio.uio_iovcnt = 1;
	auio.uio_procp = curp;
	if (kth->ktr_len > 0) {
		aiov[1] = data[0];
		aiov[2] = data[1];
		auio.uio_iovcnt++;
		if (aiov[2].iov_len > 0)
			auio.uio_iovcnt++;
		auio.uio_resid += kth->ktr_len;
	}
	error = vget(vp, LK_EXCLUSIVE | LK_RETRY);
	if (error)
		goto bad;
	error = VOP_WRITE(vp, &auio, IO_UNIT|IO_APPEND, cred);
	vput(vp);
	if (error)
		goto bad;

	return (0);

bad:
	/*
	 * If error encountered, give up tracing on this vnode.
	 */
	log(LOG_NOTICE, "ktrace write failed, errno %d, tracing stopped\n",
	    error);
	LIST_FOREACH(pr, &allprocess, ps_list) {
		if (pr == curp->p_p)
			continue;
		if (pr->ps_tracevp == vp && pr->ps_tracecred == cred)
			ktrcleartrace(pr);
	}
	ktrcleartrace(curp->p_p);
	return (error);
}

/*
 * Return true if caller has permission to set the ktracing state
 * of target.  Essentially, the target can't possess any
 * more permissions than the caller.  KTRFAC_ROOT signifies that
 * root previously set the tracing status on the target process, and 
 * so, only root may further change it.
 *
 * TODO: check groups.  use caller effective gid.
 */
int
ktrcanset(struct proc *callp, struct process *targetpr)
{
	struct ucred *caller = callp->p_ucred;
	struct ucred *target = targetpr->ps_ucred;

	if ((caller->cr_uid == target->cr_ruid &&
	    target->cr_ruid == target->cr_svuid &&
	    caller->cr_rgid == target->cr_rgid &&	/* XXX */
	    target->cr_rgid == target->cr_svgid &&
	    (targetpr->ps_traceflag & KTRFAC_ROOT) == 0 &&
	    !ISSET(targetpr->ps_flags, PS_SUGID)) ||
	    caller->cr_uid == 0)
		return (1);

	return (0);
}
