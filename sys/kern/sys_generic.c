/*	$OpenBSD: sys_generic.c,v 1.160 2024/12/30 02:46:00 guenther Exp $	*/
/*	$NetBSD: sys_generic.c,v 1.24 1996/03/29 00:25:32 cgd Exp $	*/

/*
 * Copyright (c) 1996 Theo de Raadt
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
 *	@(#)sys_generic.c	8.5 (Berkeley) 1/21/94
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/filedesc.h>
#include <sys/ioctl.h>
#include <sys/fcntl.h>
#include <sys/vnode.h>
#include <sys/file.h>
#include <sys/proc.h>
#include <sys/resourcevar.h>
#include <sys/socketvar.h>
#include <sys/signalvar.h>
#include <sys/uio.h>
#include <sys/time.h>
#include <sys/malloc.h>
#include <sys/poll.h>
#include <sys/eventvar.h>
#ifdef KTRACE
#include <sys/ktrace.h>
#endif
#include <sys/pledge.h>

#include <sys/mount.h>
#include <sys/syscallargs.h>

/*
 * Debug values:
 *  1 - print implementation errors, things that should not happen.
 *  2 - print ppoll(2) information, somewhat verbose
 *  3 - print pselect(2) and ppoll(2) information, very verbose
 */
/* #define KQPOLL_DEBUG */
#ifdef KQPOLL_DEBUG
int kqpoll_debug = 1;
#define DPRINTFN(v, x...) if (kqpoll_debug > v) {			\
	printf("%s(%d): ", curproc->p_p->ps_comm, curproc->p_tid);	\
	printf(x);							\
}
#else
#define DPRINTFN(v, x...) do {} while (0);
#endif

int pselregister(struct proc *, fd_set **, fd_set **, int, int *, int *);
int pselcollect(struct proc *, struct kevent *, fd_set **, int *);
void ppollregister(struct proc *, struct pollfd *, int, int *, int *);
int ppollcollect(struct proc *, struct kevent *, struct pollfd *, u_int);

int pollout(struct pollfd *, struct pollfd *, u_int);
int dopselect(struct proc *, int, fd_set *, fd_set *, fd_set *,
    struct timespec *, const sigset_t *, register_t *);
int doppoll(struct proc *, struct pollfd *, u_int, struct timespec *,
    const sigset_t *, register_t *);

int
iovec_copyin(const struct iovec *uiov, struct iovec **iovp, struct iovec *aiov,
    unsigned int iovcnt, size_t *residp)
{
#ifdef KTRACE
	struct proc *p = curproc;
#endif
	struct iovec *iov;
	int error, i;
	size_t resid = 0;

	if (iovcnt > UIO_SMALLIOV) {
		if (iovcnt > IOV_MAX)
			return (EINVAL);
		iov = mallocarray(iovcnt, sizeof(*iov), M_IOV, M_WAITOK);
	} else if (iovcnt > 0) {
		iov = aiov;
	} else {
		return (EINVAL);
	}
	*iovp = iov;

	if ((error = copyin(uiov, iov, iovcnt * sizeof(*iov))))
		return (error);

#ifdef KTRACE
	if (KTRPOINT(p, KTR_STRUCT))
		ktriovec(p, iov, iovcnt);
#endif

	for (i = 0; i < iovcnt; i++) {
		resid += iov->iov_len;
		/*
		 * Writes return ssize_t because -1 is returned on error.
		 * Therefore we must restrict the length to SSIZE_MAX to
		 * avoid garbage return values.  Note that the addition is
		 * guaranteed to not wrap because SSIZE_MAX * 2 < SIZE_MAX.
		 */
		if (iov->iov_len > SSIZE_MAX || resid > SSIZE_MAX)
			return (EINVAL);
		iov++;
	}

	if (residp != NULL)
		*residp = resid;

	return (0);
}

void
iovec_free(struct iovec *iov, unsigned int iovcnt)
{
	if (iovcnt > UIO_SMALLIOV)
		free(iov, M_IOV, iovcnt * sizeof(*iov));
}

/*
 * Read system call.
 */
int
sys_read(struct proc *p, void *v, register_t *retval)
{
	struct sys_read_args /* {
		syscallarg(int) fd;
		syscallarg(void *) buf;
		syscallarg(size_t) nbyte;
	} */ *uap = v;
	struct iovec iov;
	struct uio auio;

	iov.iov_base = SCARG(uap, buf);
	iov.iov_len = SCARG(uap, nbyte);
	if (iov.iov_len > SSIZE_MAX)
		return (EINVAL);

	auio.uio_iov = &iov;
	auio.uio_iovcnt = 1;
	auio.uio_resid = iov.iov_len;

	return (dofilereadv(p, SCARG(uap, fd), &auio, 0, retval));
}

/*
 * Scatter read system call.
 */
int
sys_readv(struct proc *p, void *v, register_t *retval)
{
	struct sys_readv_args /* {
		syscallarg(int) fd;
		syscallarg(const struct iovec *) iovp;
		syscallarg(int) iovcnt;
	} */ *uap = v;
	struct iovec aiov[UIO_SMALLIOV], *iov = NULL;
	int error, iovcnt = SCARG(uap, iovcnt);
	struct uio auio;
	size_t resid;

	error = iovec_copyin(SCARG(uap, iovp), &iov, aiov, iovcnt, &resid);
	if (error)
		goto done;

	auio.uio_iov = iov;
	auio.uio_iovcnt = iovcnt;
	auio.uio_resid = resid;

	error = dofilereadv(p, SCARG(uap, fd), &auio, 0, retval);
 done:
	iovec_free(iov, iovcnt);
	return (error);
}

int
dofilereadv(struct proc *p, int fd, struct uio *uio, int flags,
    register_t *retval)
{
	struct filedesc *fdp = p->p_fd;
	struct file *fp;
	long cnt, error = 0;
	u_int iovlen;
#ifdef KTRACE
	struct iovec *ktriov = NULL;
#endif

	KASSERT(uio->uio_iov != NULL && uio->uio_iovcnt > 0);
	iovlen = uio->uio_iovcnt * sizeof(struct iovec);

	if ((fp = fd_getfile_mode(fdp, fd, FREAD)) == NULL)
		return (EBADF);

	/* Checks for positioned read. */
	if (flags & FO_POSITION) {
		struct vnode *vp = fp->f_data;

		if (fp->f_type != DTYPE_VNODE || vp->v_type == VFIFO ||
		    (vp->v_flag & VISTTY)) {
			error = ESPIPE;
			goto done;
		}

		if (uio->uio_offset < 0 && vp->v_type != VCHR) {
			error = EINVAL;
			goto done;
		}
	}

	uio->uio_rw = UIO_READ;
	uio->uio_segflg = UIO_USERSPACE;
	uio->uio_procp = p;
#ifdef KTRACE
	/*
	 * if tracing, save a copy of iovec
	 */
	if (KTRPOINT(p, KTR_GENIO)) {
		ktriov = malloc(iovlen, M_TEMP, M_WAITOK);
		memcpy(ktriov, uio->uio_iov, iovlen);
	}
#endif
	cnt = uio->uio_resid;
	error = (*fp->f_ops->fo_read)(fp, uio, flags);
	if (error) {
		if (uio->uio_resid != cnt && (error == ERESTART ||
		    error == EINTR || error == EWOULDBLOCK))
			error = 0;
	}
	cnt -= uio->uio_resid;

	mtx_enter(&fp->f_mtx);
	fp->f_rxfer++;
	fp->f_rbytes += cnt;
	mtx_leave(&fp->f_mtx);
#ifdef KTRACE
	if (ktriov != NULL) {
		if (error == 0)
			ktrgenio(p, fd, UIO_READ, ktriov, cnt);
		free(ktriov, M_TEMP, iovlen);
	}
#endif
	*retval = cnt;
 done:
	FRELE(fp, p);
	return (error);
}

/*
 * Write system call
 */
int
sys_write(struct proc *p, void *v, register_t *retval)
{
	struct sys_write_args /* {
		syscallarg(int) fd;
		syscallarg(const void *) buf;
		syscallarg(size_t) nbyte;
	} */ *uap = v;
	struct iovec iov;
	struct uio auio;

	iov.iov_base = (void *)SCARG(uap, buf);
	iov.iov_len = SCARG(uap, nbyte);
	if (iov.iov_len > SSIZE_MAX)
		return (EINVAL);

	auio.uio_iov = &iov;
	auio.uio_iovcnt = 1;
	auio.uio_resid = iov.iov_len;

	return (dofilewritev(p, SCARG(uap, fd), &auio, 0, retval));
}

/*
 * Gather write system call
 */
int
sys_writev(struct proc *p, void *v, register_t *retval)
{
	struct sys_writev_args /* {
		syscallarg(int) fd;
		syscallarg(const struct iovec *) iovp;
		syscallarg(int) iovcnt;
	} */ *uap = v;
	struct iovec aiov[UIO_SMALLIOV], *iov = NULL;
	int error, iovcnt = SCARG(uap, iovcnt);
	struct uio auio;
	size_t resid;

	error = iovec_copyin(SCARG(uap, iovp), &iov, aiov, iovcnt, &resid);
	if (error)
		goto done;

	auio.uio_iov = iov;
	auio.uio_iovcnt = iovcnt;
	auio.uio_resid = resid;

	error = dofilewritev(p, SCARG(uap, fd), &auio, 0, retval);
 done:
	iovec_free(iov, iovcnt);
 	return (error);
}

int
dofilewritev(struct proc *p, int fd, struct uio *uio, int flags,
    register_t *retval)
{
	struct filedesc *fdp = p->p_fd;
	struct file *fp;
	long cnt, error = 0;
	u_int iovlen;
#ifdef KTRACE
	struct iovec *ktriov = NULL;
#endif

	KASSERT(uio->uio_iov != NULL && uio->uio_iovcnt > 0);
	iovlen = uio->uio_iovcnt * sizeof(struct iovec);

	if ((fp = fd_getfile_mode(fdp, fd, FWRITE)) == NULL)
		return (EBADF);

	/* Checks for positioned write. */
	if (flags & FO_POSITION) {
		struct vnode *vp = fp->f_data;

		if (fp->f_type != DTYPE_VNODE || vp->v_type == VFIFO ||
		    (vp->v_flag & VISTTY)) {
			error = ESPIPE;
			goto done;
		}

		if (uio->uio_offset < 0 && vp->v_type != VCHR) {
			error = EINVAL;
			goto done;
		}
	}

	uio->uio_rw = UIO_WRITE;
	uio->uio_segflg = UIO_USERSPACE;
	uio->uio_procp = p;
#ifdef KTRACE
	/*
	 * if tracing, save a copy of iovec
	 */
	if (KTRPOINT(p, KTR_GENIO)) {
		ktriov = malloc(iovlen, M_TEMP, M_WAITOK);
		memcpy(ktriov, uio->uio_iov, iovlen);
	}
#endif
	cnt = uio->uio_resid;
	error = (*fp->f_ops->fo_write)(fp, uio, flags);
	if (error) {
		if (uio->uio_resid != cnt && (error == ERESTART ||
		    error == EINTR || error == EWOULDBLOCK))
			error = 0;
		if (error == EPIPE)
			ptsignal(p, SIGPIPE, STHREAD);
	}
	cnt -= uio->uio_resid;

	mtx_enter(&fp->f_mtx);
	fp->f_wxfer++;
	fp->f_wbytes += cnt;
	mtx_leave(&fp->f_mtx);
#ifdef KTRACE
	if (ktriov != NULL) {
		if (error == 0)
			ktrgenio(p, fd, UIO_WRITE, ktriov, cnt);
		free(ktriov, M_TEMP, iovlen);
	}
#endif
	*retval = cnt;
 done:
	FRELE(fp, p);
	return (error);
}

/*
 * Ioctl system call
 */
int
sys_ioctl(struct proc *p, void *v, register_t *retval)
{
	struct sys_ioctl_args /* {
		syscallarg(int) fd;
		syscallarg(u_long) com;
		syscallarg(void *) data;
	} */ *uap = v;
	struct file *fp;
	struct filedesc *fdp = p->p_fd;
	u_long com = SCARG(uap, com);
	int error = 0;
	u_int size = 0;
	caddr_t data, memp = NULL;
	int tmp;
#define STK_PARAMS	128
	long long stkbuf[STK_PARAMS / sizeof(long long)];

	if ((fp = fd_getfile_mode(fdp, SCARG(uap, fd), FREAD|FWRITE)) == NULL)
		return (EBADF);

	if (fp->f_type == DTYPE_SOCKET) {
		struct socket *so = fp->f_data;

		if (so->so_state & SS_DNS) {
			error = EINVAL;
			goto out;
		}
	}

	error = pledge_ioctl(p, com, fp);
	if (error)
		goto out;

	switch (com) {
	case FIONCLEX:
	case FIOCLEX:
		fdplock(fdp);
		if (com == FIONCLEX)
			fdp->fd_ofileflags[SCARG(uap, fd)] &= ~UF_EXCLOSE;
		else
			fdp->fd_ofileflags[SCARG(uap, fd)] |= UF_EXCLOSE;
		fdpunlock(fdp);
		goto out;
	}

	/*
	 * Interpret high order word to find amount of data to be
	 * copied to/from the user's address space.
	 */
	size = IOCPARM_LEN(com);
	if (size > IOCPARM_MAX) {
		error = ENOTTY;
		goto out;
	}
	if (size > sizeof (stkbuf)) {
		memp = malloc(size, M_IOCTLOPS, M_WAITOK);
		data = memp;
	} else
		data = (caddr_t)stkbuf;
	if (com&IOC_IN) {
		if (size) {
			error = copyin(SCARG(uap, data), data, size);
			if (error) {
				goto out;
			}
		} else
			*(caddr_t *)data = SCARG(uap, data);
	} else if ((com&IOC_OUT) && size)
		/*
		 * Zero the buffer so the user always
		 * gets back something deterministic.
		 */
		memset(data, 0, size);
	else if (com&IOC_VOID)
		*(caddr_t *)data = SCARG(uap, data);

	switch (com) {

	case FIONBIO:
		if ((tmp = *(int *)data) != 0)
			atomic_setbits_int(&fp->f_flag, FNONBLOCK);
		else
			atomic_clearbits_int(&fp->f_flag, FNONBLOCK);
		error = 0;
		break;

	case FIOASYNC:
		if ((tmp = *(int *)data) != 0)
			atomic_setbits_int(&fp->f_flag, FASYNC);
		else
			atomic_clearbits_int(&fp->f_flag, FASYNC);
		error = (*fp->f_ops->fo_ioctl)(fp, FIOASYNC, (caddr_t)&tmp, p);
		break;

	default:
		error = (*fp->f_ops->fo_ioctl)(fp, com, data, p);
		break;
	}
	/*
	 * Copy any data to user, size was
	 * already set and checked above.
	 */
	if (error == 0 && (com&IOC_OUT) && size)
		error = copyout(data, SCARG(uap, data), size);
out:
	FRELE(fp, p);
	free(memp, M_IOCTLOPS, size);
	return (error);
}

/*
 * Select system call.
 */
int
sys_select(struct proc *p, void *v, register_t *retval)
{
	struct sys_select_args /* {
		syscallarg(int) nd;
		syscallarg(fd_set *) in;
		syscallarg(fd_set *) ou;
		syscallarg(fd_set *) ex;
		syscallarg(struct timeval *) tv;
	} */ *uap = v;

	struct timespec ts, *tsp = NULL;
	int error;

	if (SCARG(uap, tv) != NULL) {
		struct timeval tv;
		if ((error = copyin(SCARG(uap, tv), &tv, sizeof tv)) != 0)
			return (error);
#ifdef KTRACE
		if (KTRPOINT(p, KTR_STRUCT))
			ktrreltimeval(p, &tv);
#endif
		if (tv.tv_sec < 0 || !timerisvalid(&tv))
			return (EINVAL);
		TIMEVAL_TO_TIMESPEC(&tv, &ts);
		tsp = &ts;
	}

	return (dopselect(p, SCARG(uap, nd), SCARG(uap, in), SCARG(uap, ou),
	    SCARG(uap, ex), tsp, NULL, retval));
}

int
sys_pselect(struct proc *p, void *v, register_t *retval)
{
	struct sys_pselect_args /* {
		syscallarg(int) nd;
		syscallarg(fd_set *) in;
		syscallarg(fd_set *) ou;
		syscallarg(fd_set *) ex;
		syscallarg(const struct timespec *) ts;
		syscallarg(const sigset_t *) mask;
	} */ *uap = v;

	struct timespec ts, *tsp = NULL;
	sigset_t ss, *ssp = NULL;
	int error;

	if (SCARG(uap, ts) != NULL) {
		if ((error = copyin(SCARG(uap, ts), &ts, sizeof ts)) != 0)
			return (error);
#ifdef KTRACE
		if (KTRPOINT(p, KTR_STRUCT))
			ktrreltimespec(p, &ts);
#endif
		if (ts.tv_sec < 0 || !timespecisvalid(&ts))
			return (EINVAL);
		tsp = &ts;
	}
	if (SCARG(uap, mask) != NULL) {
		if ((error = copyin(SCARG(uap, mask), &ss, sizeof ss)) != 0)
			return (error);
		ssp = &ss;
	}

	return (dopselect(p, SCARG(uap, nd), SCARG(uap, in), SCARG(uap, ou),
	    SCARG(uap, ex), tsp, ssp, retval));
}

int
dopselect(struct proc *p, int nd, fd_set *in, fd_set *ou, fd_set *ex,
    struct timespec *timeout, const sigset_t *sigmask, register_t *retval)
{
	struct kqueue_scan_state scan;
	struct timespec zerots = {};
	fd_mask bits[6];
	fd_set *pibits[3], *pobits[3];
	int error, nfiles, ncollected = 0, nevents = 0;
	u_int ni;

	if (nd < 0)
		return (EINVAL);

	nfiles = READ_ONCE(p->p_fd->fd_nfiles);
	if (nd > nfiles)
		nd = nfiles;

	ni = howmany(nd, NFDBITS) * sizeof(fd_mask);
	if (ni > sizeof(bits[0])) {
		caddr_t mbits;

		mbits = mallocarray(6, ni, M_TEMP, M_WAITOK|M_ZERO);
		pibits[0] = (fd_set *)&mbits[ni * 0];
		pibits[1] = (fd_set *)&mbits[ni * 1];
		pibits[2] = (fd_set *)&mbits[ni * 2];
		pobits[0] = (fd_set *)&mbits[ni * 3];
		pobits[1] = (fd_set *)&mbits[ni * 4];
		pobits[2] = (fd_set *)&mbits[ni * 5];
	} else {
		memset(bits, 0, sizeof(bits));
		pibits[0] = (fd_set *)&bits[0];
		pibits[1] = (fd_set *)&bits[1];
		pibits[2] = (fd_set *)&bits[2];
		pobits[0] = (fd_set *)&bits[3];
		pobits[1] = (fd_set *)&bits[4];
		pobits[2] = (fd_set *)&bits[5];
	}

	kqpoll_init(nd);

#define	getbits(name, x) \
	if (name && (error = copyin(name, pibits[x], ni))) \
		goto done;
	getbits(in, 0);
	getbits(ou, 1);
	getbits(ex, 2);
#undef	getbits
#ifdef KTRACE
	if (ni > 0 && KTRPOINT(p, KTR_STRUCT)) {
		if (in) ktrfdset(p, pibits[0], ni);
		if (ou) ktrfdset(p, pibits[1], ni);
		if (ex) ktrfdset(p, pibits[2], ni);
	}
#endif

	if (sigmask)
		dosigsuspend(p, *sigmask &~ sigcantmask);

	/* Register kqueue events */
	error = pselregister(p, pibits, pobits, nd, &nevents, &ncollected);
	if (error != 0)
		goto done;

	/*
	 * The poll/select family of syscalls has been designed to
	 * block when file descriptors are not available, even if
	 * there's nothing to wait for.
	 */
	if (nevents == 0 && ncollected == 0) {
		uint64_t nsecs = INFSLP;

		if (timeout != NULL) {
			if (!timespecisset(timeout))
				goto done;
			nsecs = MAX(1, MIN(TIMESPEC_TO_NSEC(timeout), MAXTSLP));
		}
		error = tsleep_nsec(&nowake, PSOCK | PCATCH, "kqsel", nsecs);
		/* select is not restarted after signals... */
		if (error == ERESTART)
			error = EINTR;
		if (error == EWOULDBLOCK)
			error = 0;
		goto done;
	}

	/* Do not block if registering found pending events. */
	if (ncollected > 0)
		timeout = &zerots;

	/* Collect at most `nevents' possibly waiting in kqueue_scan() */
	kqueue_scan_setup(&scan, p->p_kq);
	while (nevents > 0) {
		struct kevent kev[KQ_NEVENTS];
		int i, ready, count;

		/* Maximum number of events per iteration */
		count = MIN(nitems(kev), nevents);
		ready = kqueue_scan(&scan, count, kev, timeout, p, &error);

		/* Convert back events that are ready. */
		for (i = 0; i < ready && error == 0; i++)
			error = pselcollect(p, &kev[i], pobits, &ncollected);
		/*
		 * Stop if there was an error or if we had enough
		 * space to collect all events that were ready.
		 */
		if (error || ready < count)
			break;

		nevents -= ready;
	}
	kqueue_scan_finish(&scan);
	*retval = ncollected;
done:
#define	putbits(name, x) \
	if (name && (error2 = copyout(pobits[x], name, ni))) \
		error = error2;
	if (error == 0) {
		int error2;

		putbits(in, 0);
		putbits(ou, 1);
		putbits(ex, 2);
#undef putbits
#ifdef KTRACE
		if (ni > 0 && KTRPOINT(p, KTR_STRUCT)) {
			if (in) ktrfdset(p, pobits[0], ni);
			if (ou) ktrfdset(p, pobits[1], ni);
			if (ex) ktrfdset(p, pobits[2], ni);
		}
#endif
	}

	if (pibits[0] != (fd_set *)&bits[0])
		free(pibits[0], M_TEMP, 6 * ni);

	kqpoll_done(nd);

	return (error);
}

/*
 * Convert fd_set into kqueue events and register them on the
 * per-thread queue.
 */
int
pselregister(struct proc *p, fd_set *pibits[3], fd_set *pobits[3], int nfd,
    int *nregistered, int *ncollected)
{
	static const int evf[] = { EVFILT_READ, EVFILT_WRITE, EVFILT_EXCEPT };
	static const int evff[] = { 0, 0, NOTE_OOB };
	int msk, i, j, fd, nevents = 0, error = 0;
	struct kevent kev;
	fd_mask bits;

	for (msk = 0; msk < 3; msk++) {
		for (i = 0; i < nfd; i += NFDBITS) {
			bits = pibits[msk]->fds_bits[i / NFDBITS];
			while ((j = ffs(bits)) && (fd = i + --j) < nfd) {
				bits &= ~(1 << j);

				DPRINTFN(2, "select fd %d mask %d serial %lu\n",
				    fd, msk, p->p_kq_serial);
				EV_SET(&kev, fd, evf[msk],
				    EV_ADD|EV_ENABLE|__EV_SELECT,
				    evff[msk], 0, (void *)(p->p_kq_serial));
				error = kqueue_register(p->p_kq, &kev, 0, p);
				switch (error) {
				case 0:
					nevents++;
				/* FALLTHROUGH */
				case EOPNOTSUPP:/* No underlying kqfilter */
				case EINVAL:	/* Unimplemented filter */
				case EPERM:	/* Specific to FIFO and
						 * __EV_SELECT */
					error = 0;
					break;
				case ENXIO:	/* Device has been detached */
				default:
					goto bad;
				}
			}
		}
	}

	*nregistered = nevents;
	return (0);
bad:
	DPRINTFN(0, "select fd %u filt %d error %d\n", (int)kev.ident,
	    kev.filter, error);
	return (error);
}

/*
 * Convert given kqueue event into corresponding select(2) bit.
 */
int
pselcollect(struct proc *p, struct kevent *kevp, fd_set *pobits[3],
    int *ncollected)
{
	if ((unsigned long)kevp->udata != p->p_kq_serial) {
		panic("%s: spurious kevp %p fd %d udata 0x%lx serial 0x%lx",
		    __func__, kevp, (int)kevp->ident,
		    (unsigned long)kevp->udata, p->p_kq_serial);
	}

	if (kevp->flags & EV_ERROR) {
		DPRINTFN(2, "select fd %d filt %d error %d\n",
		    (int)kevp->ident, kevp->filter, (int)kevp->data);
		return (kevp->data);
	}

	switch (kevp->filter) {
	case EVFILT_READ:
		FD_SET(kevp->ident, pobits[0]);
		break;
	case EVFILT_WRITE:
		FD_SET(kevp->ident, pobits[1]);
		break;
	case EVFILT_EXCEPT:
		FD_SET(kevp->ident, pobits[2]);
		break;
	default:
		KASSERT(0);
	}
	(*ncollected)++;

	DPRINTFN(2, "select fd %d filt %d\n", (int)kevp->ident, kevp->filter);
	return (0);
}

/*
 * Do a wakeup when a selectable event occurs.
 */
void
selwakeup(struct selinfo *sip)
{
	KERNEL_LOCK();
	knote_locked(&sip->si_note, NOTE_SUBMIT);
	KERNEL_UNLOCK();
}

/*
 * Only copyout the revents field.
 */
int
pollout(struct pollfd *pl, struct pollfd *upl, u_int nfds)
{
	int error = 0;
	u_int i = 0;

	while (!error && i++ < nfds) {
		error = copyout(&pl->revents, &upl->revents,
		    sizeof(upl->revents));
		pl++;
		upl++;
	}

	return (error);
}

/*
 * We are using the same mechanism as select only we encode/decode args
 * differently.
 */
int
sys_poll(struct proc *p, void *v, register_t *retval)
{
	struct sys_poll_args /* {
		syscallarg(struct pollfd *) fds;
		syscallarg(u_int) nfds;
		syscallarg(int) timeout;
	} */ *uap = v;

	struct timespec ts, *tsp = NULL;
	int msec = SCARG(uap, timeout);

	if (msec != INFTIM) {
		if (msec < 0)
			return (EINVAL);
		ts.tv_sec = msec / 1000;
		ts.tv_nsec = (msec - (ts.tv_sec * 1000)) * 1000000;
		tsp = &ts;
	}

	return (doppoll(p, SCARG(uap, fds), SCARG(uap, nfds), tsp, NULL,
	    retval));
}

int
sys_ppoll(struct proc *p, void *v, register_t *retval)
{
	struct sys_ppoll_args /* {
		syscallarg(struct pollfd *) fds;
		syscallarg(u_int) nfds;
		syscallarg(const struct timespec *) ts;
		syscallarg(const sigset_t *) mask;
	} */ *uap = v;

	int error;
	struct timespec ts, *tsp = NULL;
	sigset_t ss, *ssp = NULL;

	if (SCARG(uap, ts) != NULL) {
		if ((error = copyin(SCARG(uap, ts), &ts, sizeof ts)) != 0)
			return (error);
#ifdef KTRACE
		if (KTRPOINT(p, KTR_STRUCT))
			ktrreltimespec(p, &ts);
#endif
		if (ts.tv_sec < 0 || !timespecisvalid(&ts))
			return (EINVAL);
		tsp = &ts;
	}

	if (SCARG(uap, mask) != NULL) {
		if ((error = copyin(SCARG(uap, mask), &ss, sizeof ss)) != 0)
			return (error);
		ssp = &ss;
	}

	return (doppoll(p, SCARG(uap, fds), SCARG(uap, nfds), tsp, ssp,
	    retval));
}

int
doppoll(struct proc *p, struct pollfd *fds, u_int nfds,
    struct timespec *timeout, const sigset_t *sigmask, register_t *retval)
{
	struct kqueue_scan_state scan;
	struct timespec zerots = {};
	struct pollfd pfds[4], *pl = pfds;
	int error, ncollected = 0, nevents = 0;
	size_t sz;

	/* Standards say no more than MAX_OPEN; this is possibly better. */
	if (nfds > min((int)lim_cur(RLIMIT_NOFILE), maxfiles))
		return (EINVAL);

	/* optimize for the default case, of a small nfds value */
	if (nfds > nitems(pfds)) {
		pl = mallocarray(nfds, sizeof(*pl), M_TEMP,
		    M_WAITOK | M_CANFAIL);
		if (pl == NULL)
			return (EINVAL);
	}

	kqpoll_init(nfds);

	sz = nfds * sizeof(*pl);

	if ((error = copyin(fds, pl, sz)) != 0)
		goto bad;

	if (sigmask)
		dosigsuspend(p, *sigmask &~ sigcantmask);

	/* Register kqueue events */
	ppollregister(p, pl, nfds, &nevents, &ncollected);

	/*
	 * The poll/select family of syscalls has been designed to
	 * block when file descriptors are not available, even if
	 * there's nothing to wait for.
	 */
	if (nevents == 0 && ncollected == 0) {
		uint64_t nsecs = INFSLP;

		if (timeout != NULL) {
			if (!timespecisset(timeout))
				goto done;
			nsecs = MAX(1, MIN(TIMESPEC_TO_NSEC(timeout), MAXTSLP));
		}

		error = tsleep_nsec(&nowake, PSOCK | PCATCH, "kqpoll", nsecs);
		if (error == ERESTART)
			error = EINTR;
		if (error == EWOULDBLOCK)
			error = 0;
		goto done;
	}

	/* Do not block if registering found pending events. */
	if (ncollected > 0)
		timeout = &zerots;

	/* Collect at most `nevents' possibly waiting in kqueue_scan() */
	kqueue_scan_setup(&scan, p->p_kq);
	while (nevents > 0) {
		struct kevent kev[KQ_NEVENTS];
		int i, ready, count;

		/* Maximum number of events per iteration */
		count = MIN(nitems(kev), nevents);
		ready = kqueue_scan(&scan, count, kev, timeout, p, &error);

		/* Convert back events that are ready. */
		for (i = 0; i < ready; i++)
			ncollected += ppollcollect(p, &kev[i], pl, nfds);

		/*
		 * Stop if there was an error or if we had enough
		 * place to collect all events that were ready.
		 */
		if (error || ready < count)
			break;

		nevents -= ready;
	}
	kqueue_scan_finish(&scan);
	*retval = ncollected;
done:
	/*
	 * NOTE: poll(2) is not restarted after a signal and EWOULDBLOCK is
	 *       ignored (since the whole point is to see what would block).
	 */
	switch (error) {
	case EINTR:
		error = pollout(pl, fds, nfds);
		if (error == 0)
			error = EINTR;
		break;
	case EWOULDBLOCK:
	case 0:
		error = pollout(pl, fds, nfds);
		break;
	}
#ifdef KTRACE
	if (KTRPOINT(p, KTR_STRUCT))
		ktrpollfd(p, pl, nfds);
#endif /* KTRACE */
bad:
	if (pl != pfds)
		free(pl, M_TEMP, sz);

	kqpoll_done(nfds);

	return (error);
}

int
ppollregister_evts(struct proc *p, struct kevent *kevp, int nkev,
    struct pollfd *pl, unsigned int pollid)
{
	int i, error, nevents = 0;

	KASSERT(pl->revents == 0);

	for (i = 0; i < nkev; i++, kevp++) {
again:
		error = kqueue_register(p->p_kq, kevp, pollid, p);
		switch (error) {
		case 0:
			nevents++;
			break;
		case EOPNOTSUPP:/* No underlying kqfilter */
		case EINVAL:	/* Unimplemented filter */
			break;
		case EBADF:	/* Bad file descriptor */
			pl->revents |= POLLNVAL;
			break;
		case EPERM:	/* Specific to FIFO */
			KASSERT(kevp->filter == EVFILT_WRITE);
			if (nkev == 1) {
				/*
				 * If this is the only filter make sure
				 * POLLHUP is passed to userland.
				 */
				kevp->filter = EVFILT_EXCEPT;
				goto again;
			}
			break;
		default:
			DPRINTFN(0, "poll err %lu fd %d revents %02x serial"
			    " %lu filt %d ERROR=%d\n",
			    ((unsigned long)kevp->udata - p->p_kq_serial),
			    pl->fd, pl->revents, p->p_kq_serial, kevp->filter,
			    error);
			/* FALLTHROUGH */
		case ENXIO:	/* Device has been detached */
			pl->revents |= POLLERR;
			break;
		}
	}

	return (nevents);
}

/*
 * Convert pollfd into kqueue events and register them on the
 * per-thread queue.
 *
 * At most 3 events can correspond to a single pollfd.
 */
void
ppollregister(struct proc *p, struct pollfd *pl, int nfds, int *nregistered,
    int *ncollected)
{
	int i, nkev, nevt, forcehup;
	struct kevent kev[3], *kevp;

	for (i = 0; i < nfds; i++) {
		pl[i].events &= ~POLL_NOHUP;
		pl[i].revents = 0;

		if (pl[i].fd < 0)
			continue;

		/*
		 * POLLHUP checking is implicit in the event filters.
		 * However, the checking must be even if no events are
		 * requested.
		 */
		forcehup = ((pl[i].events & ~POLLHUP) == 0);

		DPRINTFN(1, "poll set %d/%d fd %d events %02x serial %lu\n",
		    i+1, nfds, pl[i].fd, pl[i].events, p->p_kq_serial);

		nevt = 0;
		nkev = 0;
		kevp = kev;
		if (pl[i].events & (POLLIN | POLLRDNORM)) {
			EV_SET(kevp, pl[i].fd, EVFILT_READ,
			    EV_ADD|EV_ENABLE|__EV_POLL, 0, 0,
			    (void *)(p->p_kq_serial + i));
			nkev++;
			kevp++;
		}
		if (pl[i].events & (POLLOUT | POLLWRNORM)) {
			EV_SET(kevp, pl[i].fd, EVFILT_WRITE,
			    EV_ADD|EV_ENABLE|__EV_POLL, 0, 0,
			    (void *)(p->p_kq_serial + i));
			nkev++;
			kevp++;
		}
		if ((pl[i].events & (POLLPRI | POLLRDBAND)) || forcehup) {
			int evff = forcehup ? 0 : NOTE_OOB;

			EV_SET(kevp, pl[i].fd, EVFILT_EXCEPT,
			    EV_ADD|EV_ENABLE|__EV_POLL, evff, 0,
			    (void *)(p->p_kq_serial + i));
			nkev++;
			kevp++;
		}

		if (nkev == 0)
			continue;

		*nregistered += ppollregister_evts(p, kev, nkev, &pl[i], i);

		if (pl[i].revents != 0)
			(*ncollected)++;
	}

	DPRINTFN(1, "poll registered = %d, collected = %d\n", *nregistered,
	    *ncollected);
}

/*
 * Convert given kqueue event into corresponding poll(2) revents bit.
 */
int
ppollcollect(struct proc *p, struct kevent *kevp, struct pollfd *pl, u_int nfds)
{
	static struct timeval poll_errintvl = { 5, 0 };
	static struct timeval poll_lasterr;
	int already_seen;
	unsigned long i;

	/*  Extract poll array index */
	i = (unsigned long)kevp->udata - p->p_kq_serial;

	if (i >= nfds) {
		panic("%s: spurious kevp %p nfds %u udata 0x%lx serial 0x%lx",
		    __func__, kevp, nfds,
		    (unsigned long)kevp->udata, p->p_kq_serial);
	}
	if ((int)kevp->ident != pl[i].fd) {
		panic("%s: kevp %p %lu/%d mismatch fd %d!=%d serial 0x%lx",
		    __func__, kevp, i + 1, nfds, (int)kevp->ident, pl[i].fd,
		    p->p_kq_serial);
	}

	/*
	 * A given descriptor may already have generated an error
	 * against another filter during kqueue_register().
	 *
	 * Make sure to set the appropriate flags but do not
	 * increment `*retval' more than once.
	 */
	already_seen = (pl[i].revents != 0);

	/* POLLNVAL preempts other events. */
	if ((kevp->flags & EV_ERROR) && kevp->data == EBADF) {
		pl[i].revents = POLLNVAL;
		goto done;
	} else if (pl[i].revents & POLLNVAL) {
		goto done;
	}

	switch (kevp->filter) {
	case EVFILT_READ:
		if (kevp->flags & __EV_HUP)
			pl[i].revents |= POLLHUP;
		if (pl[i].events & (POLLIN | POLLRDNORM))
			pl[i].revents |= pl[i].events & (POLLIN | POLLRDNORM);
		break;
	case EVFILT_WRITE:
		/* POLLHUP and POLLOUT/POLLWRNORM are mutually exclusive */
		if (kevp->flags & __EV_HUP) {
			pl[i].revents |= POLLHUP;
		} else if (pl[i].events & (POLLOUT | POLLWRNORM)) {
			pl[i].revents |= pl[i].events & (POLLOUT | POLLWRNORM);
		}
		break;
	case EVFILT_EXCEPT:
		if (kevp->flags & __EV_HUP) {
			if (pl[i].events != 0 && pl[i].events != POLLOUT)
				DPRINTFN(0, "weird events %x\n", pl[i].events);
			pl[i].revents |= POLLHUP;
			break;
		}
		if (pl[i].events & (POLLPRI | POLLRDBAND))
			pl[i].revents |= pl[i].events & (POLLPRI | POLLRDBAND);
		break;
	default:
		KASSERT(0);
	}

done:
	DPRINTFN(1, "poll get %lu/%d fd %d revents %02x serial %lu filt %d\n",
	    i+1, nfds, pl[i].fd, pl[i].revents, (unsigned long)kevp->udata,
	    kevp->filter);

	/*
	 * Make noise about unclaimed events as they might indicate a bug
	 * and can result in spurious-looking wakeups of poll(2).
	 *
	 * Live-locking within the system call should not happen because
	 * the scan loop in doppoll() has an upper limit for the number
	 * of events to process.
	 */
	if (pl[i].revents == 0 && ratecheck(&poll_lasterr, &poll_errintvl)) {
		printf("%s[%d]: poll index %lu fd %d events 0x%x "
		    "filter %d/0x%x unclaimed\n",
		    p->p_p->ps_comm, p->p_tid, i, pl[i].fd,
		    pl[i].events, kevp->filter, kevp->flags);
	}

	if (!already_seen && (pl[i].revents != 0))
		return (1);

	return (0);
}

/*
 * utrace system call
 */
int
sys_utrace(struct proc *curp, void *v, register_t *retval)
{
#ifdef KTRACE
	struct sys_utrace_args /* {
		syscallarg(const char *) label;
		syscallarg(const void *) addr;
		syscallarg(size_t) len;
	} */ *uap = v;

	return (ktruser(curp, SCARG(uap, label), SCARG(uap, addr),
	    SCARG(uap, len)));
#else
	return (0);
#endif
}
