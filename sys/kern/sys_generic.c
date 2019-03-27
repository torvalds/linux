/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
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

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include "opt_capsicum.h"
#include "opt_ktrace.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/sysproto.h>
#include <sys/capsicum.h>
#include <sys/filedesc.h>
#include <sys/filio.h>
#include <sys/fcntl.h>
#include <sys/file.h>
#include <sys/lock.h>
#include <sys/proc.h>
#include <sys/signalvar.h>
#include <sys/socketvar.h>
#include <sys/uio.h>
#include <sys/kernel.h>
#include <sys/ktr.h>
#include <sys/limits.h>
#include <sys/malloc.h>
#include <sys/poll.h>
#include <sys/resourcevar.h>
#include <sys/selinfo.h>
#include <sys/sleepqueue.h>
#include <sys/syscallsubr.h>
#include <sys/sysctl.h>
#include <sys/sysent.h>
#include <sys/vnode.h>
#include <sys/bio.h>
#include <sys/buf.h>
#include <sys/condvar.h>
#ifdef KTRACE
#include <sys/ktrace.h>
#endif

#include <security/audit/audit.h>

/*
 * The following macro defines how many bytes will be allocated from
 * the stack instead of memory allocated when passing the IOCTL data
 * structures from userspace and to the kernel. Some IOCTLs having
 * small data structures are used very frequently and this small
 * buffer on the stack gives a significant speedup improvement for
 * those requests. The value of this define should be greater or equal
 * to 64 bytes and should also be power of two. The data structure is
 * currently hard-aligned to a 8-byte boundary on the stack. This
 * should currently be sufficient for all supported platforms.
 */
#define	SYS_IOCTL_SMALL_SIZE	128	/* bytes */
#define	SYS_IOCTL_SMALL_ALIGN	8	/* bytes */

#ifdef __LP64__
static int iosize_max_clamp = 0;
SYSCTL_INT(_debug, OID_AUTO, iosize_max_clamp, CTLFLAG_RW,
    &iosize_max_clamp, 0, "Clamp max i/o size to INT_MAX");
static int devfs_iosize_max_clamp = 1;
SYSCTL_INT(_debug, OID_AUTO, devfs_iosize_max_clamp, CTLFLAG_RW,
    &devfs_iosize_max_clamp, 0, "Clamp max i/o size to INT_MAX for devices");
#endif

/*
 * Assert that the return value of read(2) and write(2) syscalls fits
 * into a register.  If not, an architecture will need to provide the
 * usermode wrappers to reconstruct the result.
 */
CTASSERT(sizeof(register_t) >= sizeof(size_t));

static MALLOC_DEFINE(M_IOCTLOPS, "ioctlops", "ioctl data buffer");
static MALLOC_DEFINE(M_SELECT, "select", "select() buffer");
MALLOC_DEFINE(M_IOV, "iov", "large iov's");

static int	pollout(struct thread *, struct pollfd *, struct pollfd *,
		    u_int);
static int	pollscan(struct thread *, struct pollfd *, u_int);
static int	pollrescan(struct thread *);
static int	selscan(struct thread *, fd_mask **, fd_mask **, int);
static int	selrescan(struct thread *, fd_mask **, fd_mask **);
static void	selfdalloc(struct thread *, void *);
static void	selfdfree(struct seltd *, struct selfd *);
static int	dofileread(struct thread *, int, struct file *, struct uio *,
		    off_t, int);
static int	dofilewrite(struct thread *, int, struct file *, struct uio *,
		    off_t, int);
static void	doselwakeup(struct selinfo *, int);
static void	seltdinit(struct thread *);
static int	seltdwait(struct thread *, sbintime_t, sbintime_t);
static void	seltdclear(struct thread *);

/*
 * One seltd per-thread allocated on demand as needed.
 *
 *	t - protected by st_mtx
 * 	k - Only accessed by curthread or read-only
 */
struct seltd {
	STAILQ_HEAD(, selfd)	st_selq;	/* (k) List of selfds. */
	struct selfd		*st_free1;	/* (k) free fd for read set. */
	struct selfd		*st_free2;	/* (k) free fd for write set. */
	struct mtx		st_mtx;		/* Protects struct seltd */
	struct cv		st_wait;	/* (t) Wait channel. */
	int			st_flags;	/* (t) SELTD_ flags. */
};

#define	SELTD_PENDING	0x0001			/* We have pending events. */
#define	SELTD_RESCAN	0x0002			/* Doing a rescan. */

/*
 * One selfd allocated per-thread per-file-descriptor.
 *	f - protected by sf_mtx
 */
struct selfd {
	STAILQ_ENTRY(selfd)	sf_link;	/* (k) fds owned by this td. */
	TAILQ_ENTRY(selfd)	sf_threads;	/* (f) fds on this selinfo. */
	struct selinfo		*sf_si;		/* (f) selinfo when linked. */
	struct mtx		*sf_mtx;	/* Pointer to selinfo mtx. */
	struct seltd		*sf_td;		/* (k) owning seltd. */
	void			*sf_cookie;	/* (k) fd or pollfd. */
	u_int			sf_refs;
};

static uma_zone_t selfd_zone;
static struct mtx_pool *mtxpool_select;

#ifdef __LP64__
size_t
devfs_iosize_max(void)
{

	return (devfs_iosize_max_clamp || SV_CURPROC_FLAG(SV_ILP32) ?
	    INT_MAX : SSIZE_MAX);
}

size_t
iosize_max(void)
{

	return (iosize_max_clamp || SV_CURPROC_FLAG(SV_ILP32) ?
	    INT_MAX : SSIZE_MAX);
}
#endif

#ifndef _SYS_SYSPROTO_H_
struct read_args {
	int	fd;
	void	*buf;
	size_t	nbyte;
};
#endif
int
sys_read(struct thread *td, struct read_args *uap)
{
	struct uio auio;
	struct iovec aiov;
	int error;

	if (uap->nbyte > IOSIZE_MAX)
		return (EINVAL);
	aiov.iov_base = uap->buf;
	aiov.iov_len = uap->nbyte;
	auio.uio_iov = &aiov;
	auio.uio_iovcnt = 1;
	auio.uio_resid = uap->nbyte;
	auio.uio_segflg = UIO_USERSPACE;
	error = kern_readv(td, uap->fd, &auio);
	return (error);
}

/*
 * Positioned read system call
 */
#ifndef _SYS_SYSPROTO_H_
struct pread_args {
	int	fd;
	void	*buf;
	size_t	nbyte;
	int	pad;
	off_t	offset;
};
#endif
int
sys_pread(struct thread *td, struct pread_args *uap)
{

	return (kern_pread(td, uap->fd, uap->buf, uap->nbyte, uap->offset));
}

int
kern_pread(struct thread *td, int fd, void *buf, size_t nbyte, off_t offset)
{
	struct uio auio;
	struct iovec aiov;
	int error;

	if (nbyte > IOSIZE_MAX)
		return (EINVAL);
	aiov.iov_base = buf;
	aiov.iov_len = nbyte;
	auio.uio_iov = &aiov;
	auio.uio_iovcnt = 1;
	auio.uio_resid = nbyte;
	auio.uio_segflg = UIO_USERSPACE;
	error = kern_preadv(td, fd, &auio, offset);
	return (error);
}

#if defined(COMPAT_FREEBSD6)
int
freebsd6_pread(struct thread *td, struct freebsd6_pread_args *uap)
{

	return (kern_pread(td, uap->fd, uap->buf, uap->nbyte, uap->offset));
}
#endif

/*
 * Scatter read system call.
 */
#ifndef _SYS_SYSPROTO_H_
struct readv_args {
	int	fd;
	struct	iovec *iovp;
	u_int	iovcnt;
};
#endif
int
sys_readv(struct thread *td, struct readv_args *uap)
{
	struct uio *auio;
	int error;

	error = copyinuio(uap->iovp, uap->iovcnt, &auio);
	if (error)
		return (error);
	error = kern_readv(td, uap->fd, auio);
	free(auio, M_IOV);
	return (error);
}

int
kern_readv(struct thread *td, int fd, struct uio *auio)
{
	struct file *fp;
	int error;

	error = fget_read(td, fd, &cap_read_rights, &fp);
	if (error)
		return (error);
	error = dofileread(td, fd, fp, auio, (off_t)-1, 0);
	fdrop(fp, td);
	return (error);
}

/*
 * Scatter positioned read system call.
 */
#ifndef _SYS_SYSPROTO_H_
struct preadv_args {
	int	fd;
	struct	iovec *iovp;
	u_int	iovcnt;
	off_t	offset;
};
#endif
int
sys_preadv(struct thread *td, struct preadv_args *uap)
{
	struct uio *auio;
	int error;

	error = copyinuio(uap->iovp, uap->iovcnt, &auio);
	if (error)
		return (error);
	error = kern_preadv(td, uap->fd, auio, uap->offset);
	free(auio, M_IOV);
	return (error);
}

int
kern_preadv(struct thread *td, int fd, struct uio *auio, off_t offset)
{
	struct file *fp;
	int error;

	error = fget_read(td, fd, &cap_pread_rights, &fp);
	if (error)
		return (error);
	if (!(fp->f_ops->fo_flags & DFLAG_SEEKABLE))
		error = ESPIPE;
	else if (offset < 0 &&
	    (fp->f_vnode == NULL || fp->f_vnode->v_type != VCHR))
		error = EINVAL;
	else
		error = dofileread(td, fd, fp, auio, offset, FOF_OFFSET);
	fdrop(fp, td);
	return (error);
}

/*
 * Common code for readv and preadv that reads data in
 * from a file using the passed in uio, offset, and flags.
 */
static int
dofileread(struct thread *td, int fd, struct file *fp, struct uio *auio,
    off_t offset, int flags)
{
	ssize_t cnt;
	int error;
#ifdef KTRACE
	struct uio *ktruio = NULL;
#endif

	AUDIT_ARG_FD(fd);

	/* Finish zero length reads right here */
	if (auio->uio_resid == 0) {
		td->td_retval[0] = 0;
		return (0);
	}
	auio->uio_rw = UIO_READ;
	auio->uio_offset = offset;
	auio->uio_td = td;
#ifdef KTRACE
	if (KTRPOINT(td, KTR_GENIO)) 
		ktruio = cloneuio(auio);
#endif
	cnt = auio->uio_resid;
	if ((error = fo_read(fp, auio, td->td_ucred, flags, td))) {
		if (auio->uio_resid != cnt && (error == ERESTART ||
		    error == EINTR || error == EWOULDBLOCK))
			error = 0;
	}
	cnt -= auio->uio_resid;
#ifdef KTRACE
	if (ktruio != NULL) {
		ktruio->uio_resid = cnt;
		ktrgenio(fd, UIO_READ, ktruio, error);
	}
#endif
	td->td_retval[0] = cnt;
	return (error);
}

#ifndef _SYS_SYSPROTO_H_
struct write_args {
	int	fd;
	const void *buf;
	size_t	nbyte;
};
#endif
int
sys_write(struct thread *td, struct write_args *uap)
{
	struct uio auio;
	struct iovec aiov;
	int error;

	if (uap->nbyte > IOSIZE_MAX)
		return (EINVAL);
	aiov.iov_base = (void *)(uintptr_t)uap->buf;
	aiov.iov_len = uap->nbyte;
	auio.uio_iov = &aiov;
	auio.uio_iovcnt = 1;
	auio.uio_resid = uap->nbyte;
	auio.uio_segflg = UIO_USERSPACE;
	error = kern_writev(td, uap->fd, &auio);
	return (error);
}

/*
 * Positioned write system call.
 */
#ifndef _SYS_SYSPROTO_H_
struct pwrite_args {
	int	fd;
	const void *buf;
	size_t	nbyte;
	int	pad;
	off_t	offset;
};
#endif
int
sys_pwrite(struct thread *td, struct pwrite_args *uap)
{

	return (kern_pwrite(td, uap->fd, uap->buf, uap->nbyte, uap->offset));
}

int
kern_pwrite(struct thread *td, int fd, const void *buf, size_t nbyte,
    off_t offset)
{
	struct uio auio;
	struct iovec aiov;
	int error;

	if (nbyte > IOSIZE_MAX)
		return (EINVAL);
	aiov.iov_base = (void *)(uintptr_t)buf;
	aiov.iov_len = nbyte;
	auio.uio_iov = &aiov;
	auio.uio_iovcnt = 1;
	auio.uio_resid = nbyte;
	auio.uio_segflg = UIO_USERSPACE;
	error = kern_pwritev(td, fd, &auio, offset);
	return (error);
}

#if defined(COMPAT_FREEBSD6)
int
freebsd6_pwrite(struct thread *td, struct freebsd6_pwrite_args *uap)
{

	return (kern_pwrite(td, uap->fd, uap->buf, uap->nbyte, uap->offset));
}
#endif

/*
 * Gather write system call.
 */
#ifndef _SYS_SYSPROTO_H_
struct writev_args {
	int	fd;
	struct	iovec *iovp;
	u_int	iovcnt;
};
#endif
int
sys_writev(struct thread *td, struct writev_args *uap)
{
	struct uio *auio;
	int error;

	error = copyinuio(uap->iovp, uap->iovcnt, &auio);
	if (error)
		return (error);
	error = kern_writev(td, uap->fd, auio);
	free(auio, M_IOV);
	return (error);
}

int
kern_writev(struct thread *td, int fd, struct uio *auio)
{
	struct file *fp;
	int error;

	error = fget_write(td, fd, &cap_write_rights, &fp);
	if (error)
		return (error);
	error = dofilewrite(td, fd, fp, auio, (off_t)-1, 0);
	fdrop(fp, td);
	return (error);
}

/*
 * Gather positioned write system call.
 */
#ifndef _SYS_SYSPROTO_H_
struct pwritev_args {
	int	fd;
	struct	iovec *iovp;
	u_int	iovcnt;
	off_t	offset;
};
#endif
int
sys_pwritev(struct thread *td, struct pwritev_args *uap)
{
	struct uio *auio;
	int error;

	error = copyinuio(uap->iovp, uap->iovcnt, &auio);
	if (error)
		return (error);
	error = kern_pwritev(td, uap->fd, auio, uap->offset);
	free(auio, M_IOV);
	return (error);
}

int
kern_pwritev(struct thread *td, int fd, struct uio *auio, off_t offset)
{
	struct file *fp;
	int error;

	error = fget_write(td, fd, &cap_pwrite_rights, &fp);
	if (error)
		return (error);
	if (!(fp->f_ops->fo_flags & DFLAG_SEEKABLE))
		error = ESPIPE;
	else if (offset < 0 &&
	    (fp->f_vnode == NULL || fp->f_vnode->v_type != VCHR))
		error = EINVAL;
	else
		error = dofilewrite(td, fd, fp, auio, offset, FOF_OFFSET);
	fdrop(fp, td);
	return (error);
}

/*
 * Common code for writev and pwritev that writes data to
 * a file using the passed in uio, offset, and flags.
 */
static int
dofilewrite(struct thread *td, int fd, struct file *fp, struct uio *auio,
    off_t offset, int flags)
{
	ssize_t cnt;
	int error;
#ifdef KTRACE
	struct uio *ktruio = NULL;
#endif

	AUDIT_ARG_FD(fd);
	auio->uio_rw = UIO_WRITE;
	auio->uio_td = td;
	auio->uio_offset = offset;
#ifdef KTRACE
	if (KTRPOINT(td, KTR_GENIO))
		ktruio = cloneuio(auio);
#endif
	cnt = auio->uio_resid;
	if (fp->f_type == DTYPE_VNODE &&
	    (fp->f_vnread_flags & FDEVFS_VNODE) == 0)
		bwillwrite();
	if ((error = fo_write(fp, auio, td->td_ucred, flags, td))) {
		if (auio->uio_resid != cnt && (error == ERESTART ||
		    error == EINTR || error == EWOULDBLOCK))
			error = 0;
		/* Socket layer is responsible for issuing SIGPIPE. */
		if (fp->f_type != DTYPE_SOCKET && error == EPIPE) {
			PROC_LOCK(td->td_proc);
			tdsignal(td, SIGPIPE);
			PROC_UNLOCK(td->td_proc);
		}
	}
	cnt -= auio->uio_resid;
#ifdef KTRACE
	if (ktruio != NULL) {
		ktruio->uio_resid = cnt;
		ktrgenio(fd, UIO_WRITE, ktruio, error);
	}
#endif
	td->td_retval[0] = cnt;
	return (error);
}

/*
 * Truncate a file given a file descriptor.
 *
 * Can't use fget_write() here, since must return EINVAL and not EBADF if the
 * descriptor isn't writable.
 */
int
kern_ftruncate(struct thread *td, int fd, off_t length)
{
	struct file *fp;
	int error;

	AUDIT_ARG_FD(fd);
	if (length < 0)
		return (EINVAL);
	error = fget(td, fd, &cap_ftruncate_rights, &fp);
	if (error)
		return (error);
	AUDIT_ARG_FILE(td->td_proc, fp);
	if (!(fp->f_flag & FWRITE)) {
		fdrop(fp, td);
		return (EINVAL);
	}
	error = fo_truncate(fp, length, td->td_ucred, td);
	fdrop(fp, td);
	return (error);
}

#ifndef _SYS_SYSPROTO_H_
struct ftruncate_args {
	int	fd;
	int	pad;
	off_t	length;
};
#endif
int
sys_ftruncate(struct thread *td, struct ftruncate_args *uap)
{

	return (kern_ftruncate(td, uap->fd, uap->length));
}

#if defined(COMPAT_43)
#ifndef _SYS_SYSPROTO_H_
struct oftruncate_args {
	int	fd;
	long	length;
};
#endif
int
oftruncate(struct thread *td, struct oftruncate_args *uap)
{

	return (kern_ftruncate(td, uap->fd, uap->length));
}
#endif /* COMPAT_43 */

#ifndef _SYS_SYSPROTO_H_
struct ioctl_args {
	int	fd;
	u_long	com;
	caddr_t	data;
};
#endif
/* ARGSUSED */
int
sys_ioctl(struct thread *td, struct ioctl_args *uap)
{
	u_char smalldata[SYS_IOCTL_SMALL_SIZE] __aligned(SYS_IOCTL_SMALL_ALIGN);
	u_long com;
	int arg, error;
	u_int size;
	caddr_t data;

	if (uap->com > 0xffffffff) {
		printf(
		    "WARNING pid %d (%s): ioctl sign-extension ioctl %lx\n",
		    td->td_proc->p_pid, td->td_name, uap->com);
		uap->com &= 0xffffffff;
	}
	com = uap->com;

	/*
	 * Interpret high order word to find amount of data to be
	 * copied to/from the user's address space.
	 */
	size = IOCPARM_LEN(com);
	if ((size > IOCPARM_MAX) ||
	    ((com & (IOC_VOID  | IOC_IN | IOC_OUT)) == 0) ||
#if defined(COMPAT_FREEBSD5) || defined(COMPAT_FREEBSD4) || defined(COMPAT_43)
	    ((com & IOC_OUT) && size == 0) ||
#else
	    ((com & (IOC_IN | IOC_OUT)) && size == 0) ||
#endif
	    ((com & IOC_VOID) && size > 0 && size != sizeof(int)))
		return (ENOTTY);

	if (size > 0) {
		if (com & IOC_VOID) {
			/* Integer argument. */
			arg = (intptr_t)uap->data;
			data = (void *)&arg;
			size = 0;
		} else {
			if (size > SYS_IOCTL_SMALL_SIZE)
				data = malloc((u_long)size, M_IOCTLOPS, M_WAITOK);
			else
				data = smalldata;
		}
	} else
		data = (void *)&uap->data;
	if (com & IOC_IN) {
		error = copyin(uap->data, data, (u_int)size);
		if (error != 0)
			goto out;
	} else if (com & IOC_OUT) {
		/*
		 * Zero the buffer so the user always
		 * gets back something deterministic.
		 */
		bzero(data, size);
	}

	error = kern_ioctl(td, uap->fd, com, data);

	if (error == 0 && (com & IOC_OUT))
		error = copyout(data, uap->data, (u_int)size);

out:
	if (size > SYS_IOCTL_SMALL_SIZE)
		free(data, M_IOCTLOPS);
	return (error);
}

int
kern_ioctl(struct thread *td, int fd, u_long com, caddr_t data)
{
	struct file *fp;
	struct filedesc *fdp;
	int error, tmp, locked;

	AUDIT_ARG_FD(fd);
	AUDIT_ARG_CMD(com);

	fdp = td->td_proc->p_fd;

	switch (com) {
	case FIONCLEX:
	case FIOCLEX:
		FILEDESC_XLOCK(fdp);
		locked = LA_XLOCKED;
		break;
	default:
#ifdef CAPABILITIES
		FILEDESC_SLOCK(fdp);
		locked = LA_SLOCKED;
#else
		locked = LA_UNLOCKED;
#endif
		break;
	}

#ifdef CAPABILITIES
	if ((fp = fget_locked(fdp, fd)) == NULL) {
		error = EBADF;
		goto out;
	}
	if ((error = cap_ioctl_check(fdp, fd, com)) != 0) {
		fp = NULL;	/* fhold() was not called yet */
		goto out;
	}
	fhold(fp);
	if (locked == LA_SLOCKED) {
		FILEDESC_SUNLOCK(fdp);
		locked = LA_UNLOCKED;
	}
#else
	error = fget(td, fd, &cap_ioctl_rights, &fp);
	if (error != 0) {
		fp = NULL;
		goto out;
	}
#endif
	if ((fp->f_flag & (FREAD | FWRITE)) == 0) {
		error = EBADF;
		goto out;
	}

	switch (com) {
	case FIONCLEX:
		fdp->fd_ofiles[fd].fde_flags &= ~UF_EXCLOSE;
		goto out;
	case FIOCLEX:
		fdp->fd_ofiles[fd].fde_flags |= UF_EXCLOSE;
		goto out;
	case FIONBIO:
		if ((tmp = *(int *)data))
			atomic_set_int(&fp->f_flag, FNONBLOCK);
		else
			atomic_clear_int(&fp->f_flag, FNONBLOCK);
		data = (void *)&tmp;
		break;
	case FIOASYNC:
		if ((tmp = *(int *)data))
			atomic_set_int(&fp->f_flag, FASYNC);
		else
			atomic_clear_int(&fp->f_flag, FASYNC);
		data = (void *)&tmp;
		break;
	}

	error = fo_ioctl(fp, com, data, td->td_ucred, td);
out:
	switch (locked) {
	case LA_XLOCKED:
		FILEDESC_XUNLOCK(fdp);
		break;
#ifdef CAPABILITIES
	case LA_SLOCKED:
		FILEDESC_SUNLOCK(fdp);
		break;
#endif
	default:
		FILEDESC_UNLOCK_ASSERT(fdp);
		break;
	}
	if (fp != NULL)
		fdrop(fp, td);
	return (error);
}

int
poll_no_poll(int events)
{
	/*
	 * Return true for read/write.  If the user asked for something
	 * special, return POLLNVAL, so that clients have a way of
	 * determining reliably whether or not the extended
	 * functionality is present without hard-coding knowledge
	 * of specific filesystem implementations.
	 */
	if (events & ~POLLSTANDARD)
		return (POLLNVAL);

	return (events & (POLLIN | POLLOUT | POLLRDNORM | POLLWRNORM));
}

int
sys_pselect(struct thread *td, struct pselect_args *uap)
{
	struct timespec ts;
	struct timeval tv, *tvp;
	sigset_t set, *uset;
	int error;

	if (uap->ts != NULL) {
		error = copyin(uap->ts, &ts, sizeof(ts));
		if (error != 0)
		    return (error);
		TIMESPEC_TO_TIMEVAL(&tv, &ts);
		tvp = &tv;
	} else
		tvp = NULL;
	if (uap->sm != NULL) {
		error = copyin(uap->sm, &set, sizeof(set));
		if (error != 0)
			return (error);
		uset = &set;
	} else
		uset = NULL;
	return (kern_pselect(td, uap->nd, uap->in, uap->ou, uap->ex, tvp,
	    uset, NFDBITS));
}

int
kern_pselect(struct thread *td, int nd, fd_set *in, fd_set *ou, fd_set *ex,
    struct timeval *tvp, sigset_t *uset, int abi_nfdbits)
{
	int error;

	if (uset != NULL) {
		error = kern_sigprocmask(td, SIG_SETMASK, uset,
		    &td->td_oldsigmask, 0);
		if (error != 0)
			return (error);
		td->td_pflags |= TDP_OLDMASK;
		/*
		 * Make sure that ast() is called on return to
		 * usermode and TDP_OLDMASK is cleared, restoring old
		 * sigmask.
		 */
		thread_lock(td);
		td->td_flags |= TDF_ASTPENDING;
		thread_unlock(td);
	}
	error = kern_select(td, nd, in, ou, ex, tvp, abi_nfdbits);
	return (error);
}

#ifndef _SYS_SYSPROTO_H_
struct select_args {
	int	nd;
	fd_set	*in, *ou, *ex;
	struct	timeval *tv;
};
#endif
int
sys_select(struct thread *td, struct select_args *uap)
{
	struct timeval tv, *tvp;
	int error;

	if (uap->tv != NULL) {
		error = copyin(uap->tv, &tv, sizeof(tv));
		if (error)
			return (error);
		tvp = &tv;
	} else
		tvp = NULL;

	return (kern_select(td, uap->nd, uap->in, uap->ou, uap->ex, tvp,
	    NFDBITS));
}

/*
 * In the unlikely case when user specified n greater then the last
 * open file descriptor, check that no bits are set after the last
 * valid fd.  We must return EBADF if any is set.
 *
 * There are applications that rely on the behaviour.
 *
 * nd is fd_lastfile + 1.
 */
static int
select_check_badfd(fd_set *fd_in, int nd, int ndu, int abi_nfdbits)
{
	char *addr, *oaddr;
	int b, i, res;
	uint8_t bits;

	if (nd >= ndu || fd_in == NULL)
		return (0);

	oaddr = NULL;
	bits = 0; /* silence gcc */
	for (i = nd; i < ndu; i++) {
		b = i / NBBY;
#if BYTE_ORDER == LITTLE_ENDIAN
		addr = (char *)fd_in + b;
#else
		addr = (char *)fd_in;
		if (abi_nfdbits == NFDBITS) {
			addr += rounddown(b, sizeof(fd_mask)) +
			    sizeof(fd_mask) - 1 - b % sizeof(fd_mask);
		} else {
			addr += rounddown(b, sizeof(uint32_t)) +
			    sizeof(uint32_t) - 1 - b % sizeof(uint32_t);
		}
#endif
		if (addr != oaddr) {
			res = fubyte(addr);
			if (res == -1)
				return (EFAULT);
			oaddr = addr;
			bits = res;
		}
		if ((bits & (1 << (i % NBBY))) != 0)
			return (EBADF);
	}
	return (0);
}

int
kern_select(struct thread *td, int nd, fd_set *fd_in, fd_set *fd_ou,
    fd_set *fd_ex, struct timeval *tvp, int abi_nfdbits)
{
	struct filedesc *fdp;
	/*
	 * The magic 2048 here is chosen to be just enough for FD_SETSIZE
	 * infds with the new FD_SETSIZE of 1024, and more than enough for
	 * FD_SETSIZE infds, outfds and exceptfds with the old FD_SETSIZE
	 * of 256.
	 */
	fd_mask s_selbits[howmany(2048, NFDBITS)];
	fd_mask *ibits[3], *obits[3], *selbits, *sbp;
	struct timeval rtv;
	sbintime_t asbt, precision, rsbt;
	u_int nbufbytes, ncpbytes, ncpubytes, nfdbits;
	int error, lf, ndu;

	if (nd < 0)
		return (EINVAL);
	fdp = td->td_proc->p_fd;
	ndu = nd;
	lf = fdp->fd_lastfile;
	if (nd > lf + 1)
		nd = lf + 1;

	error = select_check_badfd(fd_in, nd, ndu, abi_nfdbits);
	if (error != 0)
		return (error);
	error = select_check_badfd(fd_ou, nd, ndu, abi_nfdbits);
	if (error != 0)
		return (error);
	error = select_check_badfd(fd_ex, nd, ndu, abi_nfdbits);
	if (error != 0)
		return (error);

	/*
	 * Allocate just enough bits for the non-null fd_sets.  Use the
	 * preallocated auto buffer if possible.
	 */
	nfdbits = roundup(nd, NFDBITS);
	ncpbytes = nfdbits / NBBY;
	ncpubytes = roundup(nd, abi_nfdbits) / NBBY;
	nbufbytes = 0;
	if (fd_in != NULL)
		nbufbytes += 2 * ncpbytes;
	if (fd_ou != NULL)
		nbufbytes += 2 * ncpbytes;
	if (fd_ex != NULL)
		nbufbytes += 2 * ncpbytes;
	if (nbufbytes <= sizeof s_selbits)
		selbits = &s_selbits[0];
	else
		selbits = malloc(nbufbytes, M_SELECT, M_WAITOK);

	/*
	 * Assign pointers into the bit buffers and fetch the input bits.
	 * Put the output buffers together so that they can be bzeroed
	 * together.
	 */
	sbp = selbits;
#define	getbits(name, x) \
	do {								\
		if (name == NULL) {					\
			ibits[x] = NULL;				\
			obits[x] = NULL;				\
		} else {						\
			ibits[x] = sbp + nbufbytes / 2 / sizeof *sbp;	\
			obits[x] = sbp;					\
			sbp += ncpbytes / sizeof *sbp;			\
			error = copyin(name, ibits[x], ncpubytes);	\
			if (error != 0)					\
				goto done;				\
			if (ncpbytes != ncpubytes)			\
				bzero((char *)ibits[x] + ncpubytes,	\
				    ncpbytes - ncpubytes);		\
		}							\
	} while (0)
	getbits(fd_in, 0);
	getbits(fd_ou, 1);
	getbits(fd_ex, 2);
#undef	getbits

#if BYTE_ORDER == BIG_ENDIAN && defined(__LP64__)
	/*
	 * XXX: swizzle_fdset assumes that if abi_nfdbits != NFDBITS,
	 * we are running under 32-bit emulation. This should be more
	 * generic.
	 */
#define swizzle_fdset(bits)						\
	if (abi_nfdbits != NFDBITS && bits != NULL) {			\
		int i;							\
		for (i = 0; i < ncpbytes / sizeof *sbp; i++)		\
			bits[i] = (bits[i] >> 32) | (bits[i] << 32);	\
	}
#else
#define swizzle_fdset(bits)
#endif

	/* Make sure the bit order makes it through an ABI transition */
	swizzle_fdset(ibits[0]);
	swizzle_fdset(ibits[1]);
	swizzle_fdset(ibits[2]);
	
	if (nbufbytes != 0)
		bzero(selbits, nbufbytes / 2);

	precision = 0;
	if (tvp != NULL) {
		rtv = *tvp;
		if (rtv.tv_sec < 0 || rtv.tv_usec < 0 ||
		    rtv.tv_usec >= 1000000) {
			error = EINVAL;
			goto done;
		}
		if (!timevalisset(&rtv))
			asbt = 0;
		else if (rtv.tv_sec <= INT32_MAX) {
			rsbt = tvtosbt(rtv);
			precision = rsbt;
			precision >>= tc_precexp;
			if (TIMESEL(&asbt, rsbt))
				asbt += tc_tick_sbt;
			if (asbt <= SBT_MAX - rsbt)
				asbt += rsbt;
			else
				asbt = -1;
		} else
			asbt = -1;
	} else
		asbt = -1;
	seltdinit(td);
	/* Iterate until the timeout expires or descriptors become ready. */
	for (;;) {
		error = selscan(td, ibits, obits, nd);
		if (error || td->td_retval[0] != 0)
			break;
		error = seltdwait(td, asbt, precision);
		if (error)
			break;
		error = selrescan(td, ibits, obits);
		if (error || td->td_retval[0] != 0)
			break;
	}
	seltdclear(td);

done:
	/* select is not restarted after signals... */
	if (error == ERESTART)
		error = EINTR;
	if (error == EWOULDBLOCK)
		error = 0;

	/* swizzle bit order back, if necessary */
	swizzle_fdset(obits[0]);
	swizzle_fdset(obits[1]);
	swizzle_fdset(obits[2]);
#undef swizzle_fdset

#define	putbits(name, x) \
	if (name && (error2 = copyout(obits[x], name, ncpubytes))) \
		error = error2;
	if (error == 0) {
		int error2;

		putbits(fd_in, 0);
		putbits(fd_ou, 1);
		putbits(fd_ex, 2);
#undef putbits
	}
	if (selbits != &s_selbits[0])
		free(selbits, M_SELECT);

	return (error);
}
/* 
 * Convert a select bit set to poll flags.
 *
 * The backend always returns POLLHUP/POLLERR if appropriate and we
 * return this as a set bit in any set.
 */
static int select_flags[3] = {
    POLLRDNORM | POLLHUP | POLLERR,
    POLLWRNORM | POLLHUP | POLLERR,
    POLLRDBAND | POLLERR
};

/*
 * Compute the fo_poll flags required for a fd given by the index and
 * bit position in the fd_mask array.
 */
static __inline int
selflags(fd_mask **ibits, int idx, fd_mask bit)
{
	int flags;
	int msk;

	flags = 0;
	for (msk = 0; msk < 3; msk++) {
		if (ibits[msk] == NULL)
			continue;
		if ((ibits[msk][idx] & bit) == 0)
			continue;
		flags |= select_flags[msk];
	}
	return (flags);
}

/*
 * Set the appropriate output bits given a mask of fired events and the
 * input bits originally requested.
 */
static __inline int
selsetbits(fd_mask **ibits, fd_mask **obits, int idx, fd_mask bit, int events)
{
	int msk;
	int n;

	n = 0;
	for (msk = 0; msk < 3; msk++) {
		if ((events & select_flags[msk]) == 0)
			continue;
		if (ibits[msk] == NULL)
			continue;
		if ((ibits[msk][idx] & bit) == 0)
			continue;
		/*
		 * XXX Check for a duplicate set.  This can occur because a
		 * socket calls selrecord() twice for each poll() call
		 * resulting in two selfds per real fd.  selrescan() will
		 * call selsetbits twice as a result.
		 */
		if ((obits[msk][idx] & bit) != 0)
			continue;
		obits[msk][idx] |= bit;
		n++;
	}

	return (n);
}

static __inline int
getselfd_cap(struct filedesc *fdp, int fd, struct file **fpp)
{

	return (fget_unlocked(fdp, fd, &cap_event_rights, fpp, NULL));
}

/*
 * Traverse the list of fds attached to this thread's seltd and check for
 * completion.
 */
static int
selrescan(struct thread *td, fd_mask **ibits, fd_mask **obits)
{
	struct filedesc *fdp;
	struct selinfo *si;
	struct seltd *stp;
	struct selfd *sfp;
	struct selfd *sfn;
	struct file *fp;
	fd_mask bit;
	int fd, ev, n, idx;
	int error;

	fdp = td->td_proc->p_fd;
	stp = td->td_sel;
	n = 0;
	STAILQ_FOREACH_SAFE(sfp, &stp->st_selq, sf_link, sfn) {
		fd = (int)(uintptr_t)sfp->sf_cookie;
		si = sfp->sf_si;
		selfdfree(stp, sfp);
		/* If the selinfo wasn't cleared the event didn't fire. */
		if (si != NULL)
			continue;
		error = getselfd_cap(fdp, fd, &fp);
		if (error)
			return (error);
		idx = fd / NFDBITS;
		bit = (fd_mask)1 << (fd % NFDBITS);
		ev = fo_poll(fp, selflags(ibits, idx, bit), td->td_ucred, td);
		fdrop(fp, td);
		if (ev != 0)
			n += selsetbits(ibits, obits, idx, bit, ev);
	}
	stp->st_flags = 0;
	td->td_retval[0] = n;
	return (0);
}

/*
 * Perform the initial filedescriptor scan and register ourselves with
 * each selinfo.
 */
static int
selscan(struct thread *td, fd_mask **ibits, fd_mask **obits, int nfd)
{
	struct filedesc *fdp;
	struct file *fp;
	fd_mask bit;
	int ev, flags, end, fd;
	int n, idx;
	int error;

	fdp = td->td_proc->p_fd;
	n = 0;
	for (idx = 0, fd = 0; fd < nfd; idx++) {
		end = imin(fd + NFDBITS, nfd);
		for (bit = 1; fd < end; bit <<= 1, fd++) {
			/* Compute the list of events we're interested in. */
			flags = selflags(ibits, idx, bit);
			if (flags == 0)
				continue;
			error = getselfd_cap(fdp, fd, &fp);
			if (error)
				return (error);
			selfdalloc(td, (void *)(uintptr_t)fd);
			ev = fo_poll(fp, flags, td->td_ucred, td);
			fdrop(fp, td);
			if (ev != 0)
				n += selsetbits(ibits, obits, idx, bit, ev);
		}
	}

	td->td_retval[0] = n;
	return (0);
}

int
sys_poll(struct thread *td, struct poll_args *uap)
{
	struct timespec ts, *tsp;

	if (uap->timeout != INFTIM) {
		if (uap->timeout < 0)
			return (EINVAL);
		ts.tv_sec = uap->timeout / 1000;
		ts.tv_nsec = (uap->timeout % 1000) * 1000000;
		tsp = &ts;
	} else
		tsp = NULL;

	return (kern_poll(td, uap->fds, uap->nfds, tsp, NULL));
}

int
kern_poll(struct thread *td, struct pollfd *ufds, u_int nfds,
    struct timespec *tsp, sigset_t *uset)
{
	struct pollfd *kfds;
	struct pollfd stackfds[32];
	sbintime_t sbt, precision, tmp;
	time_t over;
	struct timespec ts;
	int error;

	precision = 0;
	if (tsp != NULL) {
		if (tsp->tv_sec < 0)
			return (EINVAL);
		if (tsp->tv_nsec < 0 || tsp->tv_nsec >= 1000000000)
			return (EINVAL);
		if (tsp->tv_sec == 0 && tsp->tv_nsec == 0)
			sbt = 0;
		else {
			ts = *tsp;
			if (ts.tv_sec > INT32_MAX / 2) {
				over = ts.tv_sec - INT32_MAX / 2;
				ts.tv_sec -= over;
			} else
				over = 0;
			tmp = tstosbt(ts);
			precision = tmp;
			precision >>= tc_precexp;
			if (TIMESEL(&sbt, tmp))
				sbt += tc_tick_sbt;
			sbt += tmp;
		}
	} else
		sbt = -1;

	/*
	 * This is kinda bogus.  We have fd limits, but that is not
	 * really related to the size of the pollfd array.  Make sure
	 * we let the process use at least FD_SETSIZE entries and at
	 * least enough for the system-wide limits.  We want to be reasonably
	 * safe, but not overly restrictive.
	 */
	if (nfds > maxfilesperproc && nfds > FD_SETSIZE) 
		return (EINVAL);
	if (nfds > nitems(stackfds))
		kfds = mallocarray(nfds, sizeof(*kfds), M_TEMP, M_WAITOK);
	else
		kfds = stackfds;
	error = copyin(ufds, kfds, nfds * sizeof(*kfds));
	if (error)
		goto done;

	if (uset != NULL) {
		error = kern_sigprocmask(td, SIG_SETMASK, uset,
		    &td->td_oldsigmask, 0);
		if (error)
			goto done;
		td->td_pflags |= TDP_OLDMASK;
		/*
		 * Make sure that ast() is called on return to
		 * usermode and TDP_OLDMASK is cleared, restoring old
		 * sigmask.
		 */
		thread_lock(td);
		td->td_flags |= TDF_ASTPENDING;
		thread_unlock(td);
	}

	seltdinit(td);
	/* Iterate until the timeout expires or descriptors become ready. */
	for (;;) {
		error = pollscan(td, kfds, nfds);
		if (error || td->td_retval[0] != 0)
			break;
		error = seltdwait(td, sbt, precision);
		if (error)
			break;
		error = pollrescan(td);
		if (error || td->td_retval[0] != 0)
			break;
	}
	seltdclear(td);

done:
	/* poll is not restarted after signals... */
	if (error == ERESTART)
		error = EINTR;
	if (error == EWOULDBLOCK)
		error = 0;
	if (error == 0) {
		error = pollout(td, kfds, ufds, nfds);
		if (error)
			goto out;
	}
out:
	if (nfds > nitems(stackfds))
		free(kfds, M_TEMP);
	return (error);
}

int
sys_ppoll(struct thread *td, struct ppoll_args *uap)
{
	struct timespec ts, *tsp;
	sigset_t set, *ssp;
	int error;

	if (uap->ts != NULL) {
		error = copyin(uap->ts, &ts, sizeof(ts));
		if (error)
			return (error);
		tsp = &ts;
	} else
		tsp = NULL;
	if (uap->set != NULL) {
		error = copyin(uap->set, &set, sizeof(set));
		if (error)
			return (error);
		ssp = &set;
	} else
		ssp = NULL;
	/*
	 * fds is still a pointer to user space. kern_poll() will
	 * take care of copyin that array to the kernel space.
	 */

	return (kern_poll(td, uap->fds, uap->nfds, tsp, ssp));
}

static int
pollrescan(struct thread *td)
{
	struct seltd *stp;
	struct selfd *sfp;
	struct selfd *sfn;
	struct selinfo *si;
	struct filedesc *fdp;
	struct file *fp;
	struct pollfd *fd;
	int n;

	n = 0;
	fdp = td->td_proc->p_fd;
	stp = td->td_sel;
	FILEDESC_SLOCK(fdp);
	STAILQ_FOREACH_SAFE(sfp, &stp->st_selq, sf_link, sfn) {
		fd = (struct pollfd *)sfp->sf_cookie;
		si = sfp->sf_si;
		selfdfree(stp, sfp);
		/* If the selinfo wasn't cleared the event didn't fire. */
		if (si != NULL)
			continue;
		fp = fdp->fd_ofiles[fd->fd].fde_file;
#ifdef CAPABILITIES
		if (fp == NULL ||
		    cap_check(cap_rights(fdp, fd->fd), &cap_event_rights) != 0)
#else
		if (fp == NULL)
#endif
		{
			fd->revents = POLLNVAL;
			n++;
			continue;
		}

		/*
		 * Note: backend also returns POLLHUP and
		 * POLLERR if appropriate.
		 */
		fd->revents = fo_poll(fp, fd->events, td->td_ucred, td);
		if (fd->revents != 0)
			n++;
	}
	FILEDESC_SUNLOCK(fdp);
	stp->st_flags = 0;
	td->td_retval[0] = n;
	return (0);
}


static int
pollout(struct thread *td, struct pollfd *fds, struct pollfd *ufds, u_int nfd)
{
	int error = 0;
	u_int i = 0;
	u_int n = 0;

	for (i = 0; i < nfd; i++) {
		error = copyout(&fds->revents, &ufds->revents,
		    sizeof(ufds->revents));
		if (error)
			return (error);
		if (fds->revents != 0)
			n++;
		fds++;
		ufds++;
	}
	td->td_retval[0] = n;
	return (0);
}

static int
pollscan(struct thread *td, struct pollfd *fds, u_int nfd)
{
	struct filedesc *fdp = td->td_proc->p_fd;
	struct file *fp;
	int i, n = 0;

	FILEDESC_SLOCK(fdp);
	for (i = 0; i < nfd; i++, fds++) {
		if (fds->fd > fdp->fd_lastfile) {
			fds->revents = POLLNVAL;
			n++;
		} else if (fds->fd < 0) {
			fds->revents = 0;
		} else {
			fp = fdp->fd_ofiles[fds->fd].fde_file;
#ifdef CAPABILITIES
			if (fp == NULL ||
			    cap_check(cap_rights(fdp, fds->fd), &cap_event_rights) != 0)
#else
			if (fp == NULL)
#endif
			{
				fds->revents = POLLNVAL;
				n++;
			} else {
				/*
				 * Note: backend also returns POLLHUP and
				 * POLLERR if appropriate.
				 */
				selfdalloc(td, fds);
				fds->revents = fo_poll(fp, fds->events,
				    td->td_ucred, td);
				/*
				 * POSIX requires POLLOUT to be never
				 * set simultaneously with POLLHUP.
				 */
				if ((fds->revents & POLLHUP) != 0)
					fds->revents &= ~POLLOUT;

				if (fds->revents != 0)
					n++;
			}
		}
	}
	FILEDESC_SUNLOCK(fdp);
	td->td_retval[0] = n;
	return (0);
}

/*
 * XXX This was created specifically to support netncp and netsmb.  This
 * allows the caller to specify a socket to wait for events on.  It returns
 * 0 if any events matched and an error otherwise.  There is no way to
 * determine which events fired.
 */
int
selsocket(struct socket *so, int events, struct timeval *tvp, struct thread *td)
{
	struct timeval rtv;
	sbintime_t asbt, precision, rsbt;
	int error;

	precision = 0;	/* stupid gcc! */
	if (tvp != NULL) {
		rtv = *tvp;
		if (rtv.tv_sec < 0 || rtv.tv_usec < 0 || 
		    rtv.tv_usec >= 1000000)
			return (EINVAL);
		if (!timevalisset(&rtv))
			asbt = 0;
		else if (rtv.tv_sec <= INT32_MAX) {
			rsbt = tvtosbt(rtv);
			precision = rsbt;
			precision >>= tc_precexp;
			if (TIMESEL(&asbt, rsbt))
				asbt += tc_tick_sbt;
			if (asbt <= SBT_MAX - rsbt)
				asbt += rsbt;
			else
				asbt = -1;
		} else
			asbt = -1;
	} else
		asbt = -1;
	seltdinit(td);
	/*
	 * Iterate until the timeout expires or the socket becomes ready.
	 */
	for (;;) {
		selfdalloc(td, NULL);
		error = sopoll(so, events, NULL, td);
		/* error here is actually the ready events. */
		if (error)
			return (0);
		error = seltdwait(td, asbt, precision);
		if (error)
			break;
	}
	seltdclear(td);
	/* XXX Duplicates ncp/smb behavior. */
	if (error == ERESTART)
		error = 0;
	return (error);
}

/*
 * Preallocate two selfds associated with 'cookie'.  Some fo_poll routines
 * have two select sets, one for read and another for write.
 */
static void
selfdalloc(struct thread *td, void *cookie)
{
	struct seltd *stp;

	stp = td->td_sel;
	if (stp->st_free1 == NULL)
		stp->st_free1 = uma_zalloc(selfd_zone, M_WAITOK|M_ZERO);
	stp->st_free1->sf_td = stp;
	stp->st_free1->sf_cookie = cookie;
	if (stp->st_free2 == NULL)
		stp->st_free2 = uma_zalloc(selfd_zone, M_WAITOK|M_ZERO);
	stp->st_free2->sf_td = stp;
	stp->st_free2->sf_cookie = cookie;
}

static void
selfdfree(struct seltd *stp, struct selfd *sfp)
{
	STAILQ_REMOVE(&stp->st_selq, sfp, selfd, sf_link);
	if (sfp->sf_si != NULL) {
		mtx_lock(sfp->sf_mtx);
		if (sfp->sf_si != NULL) {
			TAILQ_REMOVE(&sfp->sf_si->si_tdlist, sfp, sf_threads);
			refcount_release(&sfp->sf_refs);
		}
		mtx_unlock(sfp->sf_mtx);
	}
	if (refcount_release(&sfp->sf_refs))
		uma_zfree(selfd_zone, sfp);
}

/* Drain the waiters tied to all the selfd belonging the specified selinfo. */
void
seldrain(struct selinfo *sip)
{

	/*
	 * This feature is already provided by doselwakeup(), thus it is
	 * enough to go for it.
	 * Eventually, the context, should take care to avoid races
	 * between thread calling select()/poll() and file descriptor
	 * detaching, but, again, the races are just the same as
	 * selwakeup().
	 */
        doselwakeup(sip, -1);
}

/*
 * Record a select request.
 */
void
selrecord(struct thread *selector, struct selinfo *sip)
{
	struct selfd *sfp;
	struct seltd *stp;
	struct mtx *mtxp;

	stp = selector->td_sel;
	/*
	 * Don't record when doing a rescan.
	 */
	if (stp->st_flags & SELTD_RESCAN)
		return;
	/*
	 * Grab one of the preallocated descriptors.
	 */
	sfp = NULL;
	if ((sfp = stp->st_free1) != NULL)
		stp->st_free1 = NULL;
	else if ((sfp = stp->st_free2) != NULL)
		stp->st_free2 = NULL;
	else
		panic("selrecord: No free selfd on selq");
	mtxp = sip->si_mtx;
	if (mtxp == NULL)
		mtxp = mtx_pool_find(mtxpool_select, sip);
	/*
	 * Initialize the sfp and queue it in the thread.
	 */
	sfp->sf_si = sip;
	sfp->sf_mtx = mtxp;
	refcount_init(&sfp->sf_refs, 2);
	STAILQ_INSERT_TAIL(&stp->st_selq, sfp, sf_link);
	/*
	 * Now that we've locked the sip, check for initialization.
	 */
	mtx_lock(mtxp);
	if (sip->si_mtx == NULL) {
		sip->si_mtx = mtxp;
		TAILQ_INIT(&sip->si_tdlist);
	}
	/*
	 * Add this thread to the list of selfds listening on this selinfo.
	 */
	TAILQ_INSERT_TAIL(&sip->si_tdlist, sfp, sf_threads);
	mtx_unlock(sip->si_mtx);
}

/* Wake up a selecting thread. */
void
selwakeup(struct selinfo *sip)
{
	doselwakeup(sip, -1);
}

/* Wake up a selecting thread, and set its priority. */
void
selwakeuppri(struct selinfo *sip, int pri)
{
	doselwakeup(sip, pri);
}

/*
 * Do a wakeup when a selectable event occurs.
 */
static void
doselwakeup(struct selinfo *sip, int pri)
{
	struct selfd *sfp;
	struct selfd *sfn;
	struct seltd *stp;

	/* If it's not initialized there can't be any waiters. */
	if (sip->si_mtx == NULL)
		return;
	/*
	 * Locking the selinfo locks all selfds associated with it.
	 */
	mtx_lock(sip->si_mtx);
	TAILQ_FOREACH_SAFE(sfp, &sip->si_tdlist, sf_threads, sfn) {
		/*
		 * Once we remove this sfp from the list and clear the
		 * sf_si seltdclear will know to ignore this si.
		 */
		TAILQ_REMOVE(&sip->si_tdlist, sfp, sf_threads);
		sfp->sf_si = NULL;
		stp = sfp->sf_td;
		mtx_lock(&stp->st_mtx);
		stp->st_flags |= SELTD_PENDING;
		cv_broadcastpri(&stp->st_wait, pri);
		mtx_unlock(&stp->st_mtx);
		if (refcount_release(&sfp->sf_refs))
			uma_zfree(selfd_zone, sfp);
	}
	mtx_unlock(sip->si_mtx);
}

static void
seltdinit(struct thread *td)
{
	struct seltd *stp;

	if ((stp = td->td_sel) != NULL)
		goto out;
	td->td_sel = stp = malloc(sizeof(*stp), M_SELECT, M_WAITOK|M_ZERO);
	mtx_init(&stp->st_mtx, "sellck", NULL, MTX_DEF);
	cv_init(&stp->st_wait, "select");
out:
	stp->st_flags = 0;
	STAILQ_INIT(&stp->st_selq);
}

static int
seltdwait(struct thread *td, sbintime_t sbt, sbintime_t precision)
{
	struct seltd *stp;
	int error;

	stp = td->td_sel;
	/*
	 * An event of interest may occur while we do not hold the seltd
	 * locked so check the pending flag before we sleep.
	 */
	mtx_lock(&stp->st_mtx);
	/*
	 * Any further calls to selrecord will be a rescan.
	 */
	stp->st_flags |= SELTD_RESCAN;
	if (stp->st_flags & SELTD_PENDING) {
		mtx_unlock(&stp->st_mtx);
		return (0);
	}
	if (sbt == 0)
		error = EWOULDBLOCK;
	else if (sbt != -1)
		error = cv_timedwait_sig_sbt(&stp->st_wait, &stp->st_mtx,
		    sbt, precision, C_ABSOLUTE);
	else
		error = cv_wait_sig(&stp->st_wait, &stp->st_mtx);
	mtx_unlock(&stp->st_mtx);

	return (error);
}

void
seltdfini(struct thread *td)
{
	struct seltd *stp;

	stp = td->td_sel;
	if (stp == NULL)
		return;
	if (stp->st_free1)
		uma_zfree(selfd_zone, stp->st_free1);
	if (stp->st_free2)
		uma_zfree(selfd_zone, stp->st_free2);
	td->td_sel = NULL;
	cv_destroy(&stp->st_wait);
	mtx_destroy(&stp->st_mtx);
	free(stp, M_SELECT);
}

/*
 * Remove the references to the thread from all of the objects we were
 * polling.
 */
static void
seltdclear(struct thread *td)
{
	struct seltd *stp;
	struct selfd *sfp;
	struct selfd *sfn;

	stp = td->td_sel;
	STAILQ_FOREACH_SAFE(sfp, &stp->st_selq, sf_link, sfn)
		selfdfree(stp, sfp);
	stp->st_flags = 0;
}

static void selectinit(void *);
SYSINIT(select, SI_SUB_SYSCALLS, SI_ORDER_ANY, selectinit, NULL);
static void
selectinit(void *dummy __unused)
{

	selfd_zone = uma_zcreate("selfd", sizeof(struct selfd), NULL, NULL,
	    NULL, NULL, UMA_ALIGN_PTR, 0);
	mtxpool_select = mtx_pool_create("select mtxpool", 128, MTX_DEF);
}

/*
 * Set up a syscall return value that follows the convention specified for
 * posix_* functions.
 */
int
kern_posix_error(struct thread *td, int error)
{

	if (error <= 0)
		return (error);
	td->td_errno = error;
	td->td_pflags |= TDP_NERRNO;
	td->td_retval[0] = error;
	return (0);
}
