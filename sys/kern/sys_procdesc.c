/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2009, 2016 Robert N. M. Watson
 * All rights reserved.
 *
 * This software was developed at the University of Cambridge Computer
 * Laboratory with support from a grant from Google, Inc.
 *
 * Portions of this software were developed by BAE Systems, the University of
 * Cambridge Computer Laboratory, and Memorial University under DARPA/AFRL
 * contract FA8650-15-C-7558 ("CADETS"), as part of the DARPA Transparent
 * Computing (TC) research program.
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
 */

/*-
 * FreeBSD process descriptor facility.
 *
 * Some processes are represented by a file descriptor, which will be used in
 * preference to signaling and pids for the purposes of process management,
 * and is, in effect, a form of capability.  When a process descriptor is
 * used with a process, it ceases to be visible to certain traditional UNIX
 * process facilities, such as waitpid(2).
 *
 * Some semantics:
 *
 * - At most one process descriptor will exist for any process, although
 *   references to that descriptor may be held from many processes (or even
 *   be in flight between processes over a local domain socket).
 * - Last close on the process descriptor will terminate the process using
 *   SIGKILL and reparent it to init so that there's a process to reap it
 *   when it's done exiting.
 * - If the process exits before the descriptor is closed, it will not
 *   generate SIGCHLD on termination, or be picked up by waitpid().
 * - The pdkill(2) system call may be used to deliver a signal to the process
 *   using its process descriptor.
 * - The pdwait4(2) system call may be used to block (or not) on a process
 *   descriptor to collect termination information.
 *
 * Open questions:
 *
 * - How to handle ptrace(2)?
 * - Will we want to add a pidtoprocdesc(2) system call to allow process
 *   descriptors to be created for processes without pdfork(2)?
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/capsicum.h>
#include <sys/fcntl.h>
#include <sys/file.h>
#include <sys/filedesc.h>
#include <sys/kernel.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/poll.h>
#include <sys/proc.h>
#include <sys/procdesc.h>
#include <sys/resourcevar.h>
#include <sys/stat.h>
#include <sys/sysproto.h>
#include <sys/sysctl.h>
#include <sys/systm.h>
#include <sys/ucred.h>
#include <sys/user.h>

#include <security/audit/audit.h>

#include <vm/uma.h>

FEATURE(process_descriptors, "Process Descriptors");

static uma_zone_t procdesc_zone;

static fo_poll_t	procdesc_poll;
static fo_kqfilter_t	procdesc_kqfilter;
static fo_stat_t	procdesc_stat;
static fo_close_t	procdesc_close;
static fo_fill_kinfo_t	procdesc_fill_kinfo;

static struct fileops procdesc_ops = {
	.fo_read = invfo_rdwr,
	.fo_write = invfo_rdwr,
	.fo_truncate = invfo_truncate,
	.fo_ioctl = invfo_ioctl,
	.fo_poll = procdesc_poll,
	.fo_kqfilter = procdesc_kqfilter,
	.fo_stat = procdesc_stat,
	.fo_close = procdesc_close,
	.fo_chmod = invfo_chmod,
	.fo_chown = invfo_chown,
	.fo_sendfile = invfo_sendfile,
	.fo_fill_kinfo = procdesc_fill_kinfo,
	.fo_flags = DFLAG_PASSABLE,
};

/*
 * Initialize with VFS so that process descriptors are available along with
 * other file descriptor types.  As long as it runs before init(8) starts,
 * there shouldn't be a problem.
 */
static void
procdesc_init(void *dummy __unused)
{

	procdesc_zone = uma_zcreate("procdesc", sizeof(struct procdesc),
	    NULL, NULL, NULL, NULL, UMA_ALIGN_PTR, 0);
	if (procdesc_zone == NULL)
		panic("procdesc_init: procdesc_zone not initialized");
}
SYSINIT(vfs, SI_SUB_VFS, SI_ORDER_ANY, procdesc_init, NULL);

/*
 * Return a locked process given a process descriptor, or ESRCH if it has
 * died.
 */
int
procdesc_find(struct thread *td, int fd, cap_rights_t *rightsp,
    struct proc **p)
{
	struct procdesc *pd;
	struct file *fp;
	int error;

	error = fget(td, fd, rightsp, &fp);
	if (error)
		return (error);
	if (fp->f_type != DTYPE_PROCDESC) {
		error = EBADF;
		goto out;
	}
	pd = fp->f_data;
	sx_slock(&proctree_lock);
	if (pd->pd_proc != NULL) {
		*p = pd->pd_proc;
		PROC_LOCK(*p);
	} else
		error = ESRCH;
	sx_sunlock(&proctree_lock);
out:
	fdrop(fp, td);
	return (error);
}

/*
 * Function to be used by procstat(1) sysctls when returning procdesc
 * information.
 */
pid_t
procdesc_pid(struct file *fp_procdesc)
{
	struct procdesc *pd;

	KASSERT(fp_procdesc->f_type == DTYPE_PROCDESC,
	   ("procdesc_pid: !procdesc"));

	pd = fp_procdesc->f_data;
	return (pd->pd_pid);
}

/*
 * Retrieve the PID associated with a process descriptor.
 */
int
kern_pdgetpid(struct thread *td, int fd, cap_rights_t *rightsp, pid_t *pidp)
{
	struct file *fp;
	int error;

	error = fget(td, fd, rightsp, &fp);
	if (error)
		return (error);
	if (fp->f_type != DTYPE_PROCDESC) {
		error = EBADF;
		goto out;
	}
	*pidp = procdesc_pid(fp);
out:
	fdrop(fp, td);
	return (error);
}

/*
 * System call to return the pid of a process given its process descriptor.
 */
int
sys_pdgetpid(struct thread *td, struct pdgetpid_args *uap)
{
	pid_t pid;
	int error;

	AUDIT_ARG_FD(uap->fd);
	error = kern_pdgetpid(td, uap->fd, &cap_pdgetpid_rights, &pid);
	if (error == 0)
		error = copyout(&pid, uap->pidp, sizeof(pid));
	return (error);
}

/*
 * When a new process is forked by pdfork(), a file descriptor is allocated
 * by the fork code first, then the process is forked, and then we get a
 * chance to set up the process descriptor.  Failure is not permitted at this
 * point, so procdesc_new() must succeed.
 */
void
procdesc_new(struct proc *p, int flags)
{
	struct procdesc *pd;

	pd = uma_zalloc(procdesc_zone, M_WAITOK | M_ZERO);
	pd->pd_proc = p;
	pd->pd_pid = p->p_pid;
	p->p_procdesc = pd;
	pd->pd_flags = 0;
	if (flags & PD_DAEMON)
		pd->pd_flags |= PDF_DAEMON;
	PROCDESC_LOCK_INIT(pd);
	knlist_init_mtx(&pd->pd_selinfo.si_note, &pd->pd_lock);

	/*
	 * Process descriptors start out with two references: one from their
	 * struct file, and the other from their struct proc.
	 */
	refcount_init(&pd->pd_refcount, 2);
}

/*
 * Create a new process decriptor for the process that refers to it.
 */
int
procdesc_falloc(struct thread *td, struct file **resultfp, int *resultfd,
    int flags, struct filecaps *fcaps)
{
	int fflags;

	fflags = 0;
	if (flags & PD_CLOEXEC)
		fflags = O_CLOEXEC;

	return (falloc_caps(td, resultfp, resultfd, fflags, fcaps));
}

/*
 * Initialize a file with a process descriptor.
 */
void
procdesc_finit(struct procdesc *pdp, struct file *fp)
{

	finit(fp, FREAD | FWRITE, DTYPE_PROCDESC, pdp, &procdesc_ops);
}

static void
procdesc_free(struct procdesc *pd)
{

	/*
	 * When the last reference is released, we assert that the descriptor
	 * has been closed, but not that the process has exited, as we will
	 * detach the descriptor before the process dies if the descript is
	 * closed, as we can't wait synchronously.
	 */
	if (refcount_release(&pd->pd_refcount)) {
		KASSERT(pd->pd_proc == NULL,
		    ("procdesc_free: pd_proc != NULL"));
		KASSERT((pd->pd_flags & PDF_CLOSED),
		    ("procdesc_free: !PDF_CLOSED"));

		knlist_destroy(&pd->pd_selinfo.si_note);
		PROCDESC_LOCK_DESTROY(pd);
		uma_zfree(procdesc_zone, pd);
	}
}

/*
 * procdesc_exit() - notify a process descriptor that its process is exiting.
 * We use the proctree_lock to ensure that process exit either happens
 * strictly before or strictly after a concurrent call to procdesc_close().
 */
int
procdesc_exit(struct proc *p)
{
	struct procdesc *pd;

	sx_assert(&proctree_lock, SA_XLOCKED);
	PROC_LOCK_ASSERT(p, MA_OWNED);
	KASSERT(p->p_procdesc != NULL, ("procdesc_exit: p_procdesc NULL"));

	pd = p->p_procdesc;

	PROCDESC_LOCK(pd);
	KASSERT((pd->pd_flags & PDF_CLOSED) == 0 || p->p_pptr == p->p_reaper,
	    ("procdesc_exit: closed && parent not reaper"));

	pd->pd_flags |= PDF_EXITED;
	pd->pd_xstat = KW_EXITCODE(p->p_xexit, p->p_xsig);

	/*
	 * If the process descriptor has been closed, then we have nothing
	 * to do; return 1 so that init will get SIGCHLD and do the reaping.
	 * Clean up the procdesc now rather than letting it happen during
	 * that reap.
	 */
	if (pd->pd_flags & PDF_CLOSED) {
		PROCDESC_UNLOCK(pd);
		pd->pd_proc = NULL;
		p->p_procdesc = NULL;
		procdesc_free(pd);
		return (1);
	}
	if (pd->pd_flags & PDF_SELECTED) {
		pd->pd_flags &= ~PDF_SELECTED;
		selwakeup(&pd->pd_selinfo);
	}
	KNOTE_LOCKED(&pd->pd_selinfo.si_note, NOTE_EXIT);
	PROCDESC_UNLOCK(pd);
	return (0);
}

/*
 * When a process descriptor is reaped, perhaps as a result of close() or
 * pdwait4(), release the process's reference on the process descriptor.
 */
void
procdesc_reap(struct proc *p)
{
	struct procdesc *pd;

	sx_assert(&proctree_lock, SA_XLOCKED);
	KASSERT(p->p_procdesc != NULL, ("procdesc_reap: p_procdesc == NULL"));

	pd = p->p_procdesc;
	pd->pd_proc = NULL;
	p->p_procdesc = NULL;
	procdesc_free(pd);
}

/*
 * procdesc_close() - last close on a process descriptor.  If the process is
 * still running, terminate with SIGKILL (unless PDF_DAEMON is set) and let
 * its reaper clean up the mess; if not, we have to clean up the zombie
 * ourselves.
 */
static int
procdesc_close(struct file *fp, struct thread *td)
{
	struct procdesc *pd;
	struct proc *p;

	KASSERT(fp->f_type == DTYPE_PROCDESC, ("procdesc_close: !procdesc"));

	pd = fp->f_data;
	fp->f_ops = &badfileops;
	fp->f_data = NULL;

	sx_xlock(&proctree_lock);
	PROCDESC_LOCK(pd);
	pd->pd_flags |= PDF_CLOSED;
	PROCDESC_UNLOCK(pd);
	p = pd->pd_proc;
	if (p == NULL) {
		/*
		 * This is the case where process' exit status was already
		 * collected and procdesc_reap() was already called.
		 */
		sx_xunlock(&proctree_lock);
	} else {
		PROC_LOCK(p);
		AUDIT_ARG_PROCESS(p);
		if (p->p_state == PRS_ZOMBIE) {
			/*
			 * If the process is already dead and just awaiting
			 * reaping, do that now.  This will release the
			 * process's reference to the process descriptor when it
			 * calls back into procdesc_reap().
			 */
			proc_reap(curthread, p, NULL, 0);
		} else {
			/*
			 * If the process is not yet dead, we need to kill it,
			 * but we can't wait around synchronously for it to go
			 * away, as that path leads to madness (and deadlocks).
			 * First, detach the process from its descriptor so that
			 * its exit status will be reported normally.
			 */
			pd->pd_proc = NULL;
			p->p_procdesc = NULL;
			procdesc_free(pd);

			/*
			 * Next, reparent it to its reaper (usually init(8)) so
			 * that there's someone to pick up the pieces; finally,
			 * terminate with prejudice.
			 */
			p->p_sigparent = SIGCHLD;
			proc_reparent(p, p->p_reaper, true);
			if ((pd->pd_flags & PDF_DAEMON) == 0)
				kern_psignal(p, SIGKILL);
			PROC_UNLOCK(p);
			sx_xunlock(&proctree_lock);
		}
	}

	/*
	 * Release the file descriptor's reference on the process descriptor.
	 */
	procdesc_free(pd);
	return (0);
}

static int
procdesc_poll(struct file *fp, int events, struct ucred *active_cred,
    struct thread *td)
{
	struct procdesc *pd;
	int revents;

	revents = 0;
	pd = fp->f_data;
	PROCDESC_LOCK(pd);
	if (pd->pd_flags & PDF_EXITED)
		revents |= POLLHUP;
	if (revents == 0) {
		selrecord(td, &pd->pd_selinfo);
		pd->pd_flags |= PDF_SELECTED;
	}
	PROCDESC_UNLOCK(pd);
	return (revents);
}

static void
procdesc_kqops_detach(struct knote *kn)
{
	struct procdesc *pd;

	pd = kn->kn_fp->f_data;
	knlist_remove(&pd->pd_selinfo.si_note, kn, 0);
}

static int
procdesc_kqops_event(struct knote *kn, long hint)
{
	struct procdesc *pd;
	u_int event;

	pd = kn->kn_fp->f_data;
	if (hint == 0) {
		/*
		 * Initial test after registration. Generate a NOTE_EXIT in
		 * case the process already terminated before registration.
		 */
		event = pd->pd_flags & PDF_EXITED ? NOTE_EXIT : 0;
	} else {
		/* Mask off extra data. */
		event = (u_int)hint & NOTE_PCTRLMASK;
	}

	/* If the user is interested in this event, record it. */
	if (kn->kn_sfflags & event)
		kn->kn_fflags |= event;

	/* Process is gone, so flag the event as finished. */
	if (event == NOTE_EXIT) {
		kn->kn_flags |= EV_EOF | EV_ONESHOT;
		if (kn->kn_fflags & NOTE_EXIT)
			kn->kn_data = pd->pd_xstat;
		if (kn->kn_fflags == 0)
			kn->kn_flags |= EV_DROP;
		return (1);
	}

	return (kn->kn_fflags != 0);
}

static struct filterops procdesc_kqops = {
	.f_isfd = 1,
	.f_detach = procdesc_kqops_detach,
	.f_event = procdesc_kqops_event,
};

static int
procdesc_kqfilter(struct file *fp, struct knote *kn)
{
	struct procdesc *pd;

	pd = fp->f_data;
	switch (kn->kn_filter) {
	case EVFILT_PROCDESC:
		kn->kn_fop = &procdesc_kqops;
		kn->kn_flags |= EV_CLEAR;
		knlist_add(&pd->pd_selinfo.si_note, kn, 0);
		return (0);
	default:
		return (EINVAL);
	}
}

static int
procdesc_stat(struct file *fp, struct stat *sb, struct ucred *active_cred,
    struct thread *td)
{
	struct procdesc *pd;
	struct timeval pstart, boottime;

	/*
	 * XXXRW: Perhaps we should cache some more information from the
	 * process so that we can return it reliably here even after it has
	 * died.  For example, caching its credential data.
	 */
	bzero(sb, sizeof(*sb));
	pd = fp->f_data;
	sx_slock(&proctree_lock);
	if (pd->pd_proc != NULL) {
		PROC_LOCK(pd->pd_proc);
		AUDIT_ARG_PROCESS(pd->pd_proc);

		/* Set birth and [acm] times to process start time. */
		pstart = pd->pd_proc->p_stats->p_start;
		getboottime(&boottime);
		timevaladd(&pstart, &boottime);
		TIMEVAL_TO_TIMESPEC(&pstart, &sb->st_birthtim);
		sb->st_atim = sb->st_birthtim;
		sb->st_ctim = sb->st_birthtim;
		sb->st_mtim = sb->st_birthtim;
		if (pd->pd_proc->p_state != PRS_ZOMBIE)
			sb->st_mode = S_IFREG | S_IRWXU;
		else
			sb->st_mode = S_IFREG;
		sb->st_uid = pd->pd_proc->p_ucred->cr_ruid;
		sb->st_gid = pd->pd_proc->p_ucred->cr_rgid;
		PROC_UNLOCK(pd->pd_proc);
	} else
		sb->st_mode = S_IFREG;
	sx_sunlock(&proctree_lock);
	return (0);
}

static int
procdesc_fill_kinfo(struct file *fp, struct kinfo_file *kif,
    struct filedesc *fdp)
{
	struct procdesc *pdp;

	kif->kf_type = KF_TYPE_PROCDESC;
	pdp = fp->f_data;
	kif->kf_un.kf_proc.kf_pid = pdp->pd_pid;
	return (0);
}
