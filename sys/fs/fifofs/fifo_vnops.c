/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1990, 1993, 1995
 *	The Regents of the University of California.
 * Copyright (c) 2005 Robert N. M. Watson
 * Copyright (c) 2012 Giovanni Trematerra
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
 *	@(#)fifo_vnops.c	8.10 (Berkeley) 5/27/95
 * $FreeBSD$
 */

#include <sys/param.h>
#include <sys/event.h>
#include <sys/file.h>
#include <sys/filedesc.h>
#include <sys/filio.h>
#include <sys/fcntl.h>
#include <sys/kernel.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/malloc.h>
#include <sys/selinfo.h>
#include <sys/pipe.h>
#include <sys/proc.h>
#include <sys/signalvar.h>
#include <sys/sx.h>
#include <sys/systm.h>
#include <sys/un.h>
#include <sys/unistd.h>
#include <sys/vnode.h>

/*
 * This structure is associated with the FIFO vnode and stores
 * the state associated with the FIFO.
 * Notes about locking:
 *   - fi_pipe is invariant since init time.
 *   - fi_readers and fi_writers are protected by the vnode lock.
 */
struct fifoinfo {
	struct pipe *fi_pipe;
	long	fi_readers;
	long	fi_writers;
	u_int	fi_rgen;
	u_int	fi_wgen;
};

static vop_print_t	fifo_print;
static vop_open_t	fifo_open;
static vop_close_t	fifo_close;
static vop_advlock_t	fifo_advlock;

struct vop_vector fifo_specops = {
	.vop_default =		&default_vnodeops,

	.vop_advlock =		fifo_advlock,
	.vop_close =		fifo_close,
	.vop_create =		VOP_PANIC,
	.vop_getattr =		VOP_EBADF,
	.vop_ioctl =		VOP_PANIC,
	.vop_kqfilter =		VOP_PANIC,
	.vop_link =		VOP_PANIC,
	.vop_mkdir =		VOP_PANIC,
	.vop_mknod =		VOP_PANIC,
	.vop_open =		fifo_open,
	.vop_pathconf =		VOP_PANIC,
	.vop_print =		fifo_print,
	.vop_read =		VOP_PANIC,
	.vop_readdir =		VOP_PANIC,
	.vop_readlink =		VOP_PANIC,
	.vop_reallocblks =	VOP_PANIC,
	.vop_reclaim =		VOP_NULL,
	.vop_remove =		VOP_PANIC,
	.vop_rename =		VOP_PANIC,
	.vop_rmdir =		VOP_PANIC,
	.vop_setattr =		VOP_EBADF,
	.vop_symlink =		VOP_PANIC,
	.vop_write =		VOP_PANIC,
};

/*
 * Dispose of fifo resources.
 */
static void
fifo_cleanup(struct vnode *vp)
{
	struct fifoinfo *fip;

	ASSERT_VOP_ELOCKED(vp, "fifo_cleanup");
	fip = vp->v_fifoinfo;
	if (fip->fi_readers == 0 && fip->fi_writers == 0) {
		vp->v_fifoinfo = NULL;
		pipe_dtor(fip->fi_pipe);
		free(fip, M_VNODE);
	}
}

/*
 * Open called to set up a new instance of a fifo or
 * to find an active instance of a fifo.
 */
/* ARGSUSED */
static int
fifo_open(ap)
	struct vop_open_args /* {
		struct vnode *a_vp;
		int  a_mode;
		struct ucred *a_cred;
		struct thread *a_td;
		struct file *a_fp;
	} */ *ap;
{
	struct vnode *vp;
	struct file *fp;
	struct thread *td;
	struct fifoinfo *fip;
	struct pipe *fpipe;
	u_int gen;
	int error, stops_deferred;

	vp = ap->a_vp;
	fp = ap->a_fp;
	td = ap->a_td;
	ASSERT_VOP_ELOCKED(vp, "fifo_open");
	if (fp == NULL || (ap->a_mode & FEXEC) != 0)
		return (EINVAL);
	if ((fip = vp->v_fifoinfo) == NULL) {
		pipe_named_ctor(&fpipe, td);
		fip = malloc(sizeof(*fip), M_VNODE, M_WAITOK);
		fip->fi_pipe = fpipe;
		fpipe->pipe_wgen = fip->fi_readers = fip->fi_writers = 0;
 		KASSERT(vp->v_fifoinfo == NULL, ("fifo_open: v_fifoinfo race"));
		vp->v_fifoinfo = fip;
	}
	fpipe = fip->fi_pipe;
 	KASSERT(fpipe != NULL, ("fifo_open: pipe is NULL"));

	/*
	 * Use the pipe mutex here, in addition to the vnode lock,
	 * in order to allow vnode lock dropping before msleep() calls
	 * and still avoiding missed wakeups.
	 */
	PIPE_LOCK(fpipe);
	if (ap->a_mode & FREAD) {
		fip->fi_readers++;
		fip->fi_rgen++;
		if (fip->fi_readers == 1) {
			fpipe->pipe_state &= ~PIPE_EOF;
			if (fip->fi_writers > 0)
				wakeup(&fip->fi_writers);
		}
		fp->f_seqcount = fpipe->pipe_wgen - fip->fi_writers;
	}
	if (ap->a_mode & FWRITE) {
		if ((ap->a_mode & O_NONBLOCK) && fip->fi_readers == 0) {
			PIPE_UNLOCK(fpipe);
			if (fip->fi_writers == 0)
				fifo_cleanup(vp);
			return (ENXIO);
		}
		fip->fi_writers++;
		fip->fi_wgen++;
		if (fip->fi_writers == 1) {
			fpipe->pipe_state &= ~PIPE_EOF;
			if (fip->fi_readers > 0)
				wakeup(&fip->fi_readers);
		}
	}
	if ((ap->a_mode & O_NONBLOCK) == 0) {
		if ((ap->a_mode & FREAD) && fip->fi_writers == 0) {
			gen = fip->fi_wgen;
			VOP_UNLOCK(vp, 0);
			stops_deferred = sigdeferstop(SIGDEFERSTOP_OFF);
			error = msleep(&fip->fi_readers, PIPE_MTX(fpipe),
			    PDROP | PCATCH | PSOCK, "fifoor", 0);
			sigallowstop(stops_deferred);
			vn_lock(vp, LK_EXCLUSIVE | LK_RETRY);
			if (error != 0 && gen == fip->fi_wgen) {
				fip->fi_readers--;
				if (fip->fi_readers == 0) {
					PIPE_LOCK(fpipe);
					fpipe->pipe_state |= PIPE_EOF;
					if (fpipe->pipe_state & PIPE_WANTW)
						wakeup(fpipe);
					PIPE_UNLOCK(fpipe);
					fifo_cleanup(vp);
				}
				return (error);
			}
			PIPE_LOCK(fpipe);
			/*
			 * We must have got woken up because we had a writer.
			 * That (and not still having one) is the condition
			 * that we must wait for.
			 */
		}
		if ((ap->a_mode & FWRITE) && fip->fi_readers == 0) {
			gen = fip->fi_rgen;
			VOP_UNLOCK(vp, 0);
			stops_deferred = sigdeferstop(SIGDEFERSTOP_OFF);
			error = msleep(&fip->fi_writers, PIPE_MTX(fpipe),
			    PDROP | PCATCH | PSOCK, "fifoow", 0);
			sigallowstop(stops_deferred);
			vn_lock(vp, LK_EXCLUSIVE | LK_RETRY);
			if (error != 0 && gen == fip->fi_rgen) {
				fip->fi_writers--;
				if (fip->fi_writers == 0) {
					PIPE_LOCK(fpipe);
					fpipe->pipe_state |= PIPE_EOF;
					if (fpipe->pipe_state & PIPE_WANTR)
						wakeup(fpipe);
					fpipe->pipe_wgen++;
					PIPE_UNLOCK(fpipe);
					fifo_cleanup(vp);
				}
				return (error);
			}
			/*
			 * We must have got woken up because we had
			 * a reader.  That (and not still having one)
			 * is the condition that we must wait for.
			 */
			PIPE_LOCK(fpipe);
		}
	}
	PIPE_UNLOCK(fpipe);
	KASSERT(fp != NULL, ("can't fifo/vnode bypass"));
	KASSERT(fp->f_ops == &badfileops, ("not badfileops in fifo_open"));
	finit(fp, fp->f_flag, DTYPE_FIFO, fpipe, &pipeops);
	return (0);
}

/*
 * Device close routine
 */
/* ARGSUSED */
static int
fifo_close(ap)
	struct vop_close_args /* {
		struct vnode *a_vp;
		int  a_fflag;
		struct ucred *a_cred;
		struct thread *a_td;
	} */ *ap;
{
	struct vnode *vp;
	struct fifoinfo *fip;
	struct pipe *cpipe;

	vp = ap->a_vp;
	fip = vp->v_fifoinfo;
	cpipe = fip->fi_pipe;
	ASSERT_VOP_ELOCKED(vp, "fifo_close");
	if (ap->a_fflag & FREAD) {
		fip->fi_readers--;
		if (fip->fi_readers == 0) {
			PIPE_LOCK(cpipe);
			cpipe->pipe_state |= PIPE_EOF;
			if ((cpipe->pipe_state & PIPE_WANTW)) {
				cpipe->pipe_state &= ~PIPE_WANTW;
				wakeup(cpipe);
			}
			pipeselwakeup(cpipe);
			PIPE_UNLOCK(cpipe);
		}
	}
	if (ap->a_fflag & FWRITE) {
		fip->fi_writers--;
		if (fip->fi_writers == 0) {
			PIPE_LOCK(cpipe);
			cpipe->pipe_state |= PIPE_EOF;
			if ((cpipe->pipe_state & PIPE_WANTR)) {
				cpipe->pipe_state &= ~PIPE_WANTR;
				wakeup(cpipe);
			}
			cpipe->pipe_wgen++;
			pipeselwakeup(cpipe);
			PIPE_UNLOCK(cpipe);
		}
	}
	fifo_cleanup(vp);
	return (0);
}

/*
 * Print out internal contents of a fifo vnode.
 */
int
fifo_printinfo(vp)
	struct vnode *vp;
{
	struct fifoinfo *fip = vp->v_fifoinfo;

	if (fip == NULL){
		printf(", NULL v_fifoinfo");
		return (0);
	}
	printf(", fifo with %ld readers and %ld writers",
		fip->fi_readers, fip->fi_writers);
	return (0);
}

/*
 * Print out the contents of a fifo vnode.
 */
static int
fifo_print(ap)
	struct vop_print_args /* {
		struct vnode *a_vp;
	} */ *ap;
{
	printf("    ");
	fifo_printinfo(ap->a_vp);
	printf("\n");
	return (0);
}

/*
 * Fifo advisory byte-level locks.
 */
/* ARGSUSED */
static int
fifo_advlock(ap)
	struct vop_advlock_args /* {
		struct vnode *a_vp;
		caddr_t  a_id;
		int  a_op;
		struct flock *a_fl;
		int  a_flags;
	} */ *ap;
{

	return (ap->a_flags & F_FLOCK ? EOPNOTSUPP : EINVAL);
}

