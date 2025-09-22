/*	$OpenBSD: kern_descrip.c,v 1.212 2025/08/04 04:59:31 guenther Exp $	*/
/*	$NetBSD: kern_descrip.c,v 1.42 1996/03/30 22:24:38 christos Exp $	*/

/*
 * Copyright (c) 1982, 1986, 1989, 1991, 1993
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
 *	@(#)kern_descrip.c	8.6 (Berkeley) 4/19/94
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/atomic.h>
#include <sys/filedesc.h>
#include <sys/vnode.h>
#include <sys/proc.h>
#include <sys/file.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <sys/fcntl.h>
#include <sys/lock.h>
#include <sys/malloc.h>
#include <sys/ucred.h>
#include <sys/unistd.h>
#include <sys/resourcevar.h>
#include <sys/mount.h>
#include <sys/syscallargs.h>
#include <sys/event.h>
#include <sys/pool.h>
#include <sys/ktrace.h>
#include <sys/pledge.h>

/*
 * Descriptor management.
 *
 * We need to block interrupts as long as `fhdlk' is being taken
 * with and without the KERNEL_LOCK().
 */
struct mutex fhdlk = MUTEX_INITIALIZER(IPL_MPFLOOR);
struct filelist filehead;	/* head of list of open files */
int numfiles;			/* actual number of open files */

static __inline void fd_used(struct filedesc *, int);
static __inline void fd_unused(struct filedesc *, int);
static __inline int find_next_zero(u_int *, int, u_int);
static __inline int fd_inuse(struct filedesc *, int);
int finishdup(struct proc *, struct file *, int, int, register_t *, int);
int find_last_set(struct filedesc *, int);
int dodup3(struct proc *, int, int, int, register_t *);

#define DUPF_CLOEXEC	0x01
#define DUPF_DUP2	0x02
#define DUPF_CLOFORK	0x04

struct pool file_pool;
struct pool fdesc_pool;

void
filedesc_init(void)
{
	pool_init(&file_pool, sizeof(struct file), 0, IPL_MPFLOOR,
	    PR_WAITOK, "filepl", NULL);
	pool_init(&fdesc_pool, sizeof(struct filedesc0), 0, IPL_NONE,
	    PR_WAITOK, "fdescpl", NULL);
	LIST_INIT(&filehead);
}

static __inline int
find_next_zero(u_int *bitmap, int want, u_int bits)
{
	int i, off, maxoff;
	u_int sub;

	if (want > bits)
		return -1;

	off = want >> NDENTRYSHIFT;
	i = want & NDENTRYMASK;
	if (i) {
		sub = bitmap[off] | ((u_int)~0 >> (NDENTRIES - i));
		if (sub != ~0)
			goto found;
		off++;
	}

	maxoff = NDLOSLOTS(bits);
	while (off < maxoff) {
		if ((sub = bitmap[off]) != ~0)
			goto found;
		off++;
	}

	return -1;

 found:
	return (off << NDENTRYSHIFT) + ffs(~sub) - 1;
}

int
find_last_set(struct filedesc *fd, int last)
{
	int off, i;
	u_int *bitmap = fd->fd_lomap;

	off = (last - 1) >> NDENTRYSHIFT;

	while (off >= 0 && !bitmap[off])
		off--;
	if (off < 0)
		return 0;

	i = ((off + 1) << NDENTRYSHIFT) - 1;
	if (i >= last)
		i = last - 1;

	while (i > 0 && !fd_inuse(fd, i))
		i--;
	return i;
}

static __inline int
fd_inuse(struct filedesc *fdp, int fd)
{
	u_int off = fd >> NDENTRYSHIFT;

	if (fdp->fd_lomap[off] & (1U << (fd & NDENTRYMASK)))
		return 1;

	return 0;
}

static __inline void
fd_used(struct filedesc *fdp, int fd)
{
	u_int off = fd >> NDENTRYSHIFT;

	fdp->fd_lomap[off] |= 1U << (fd & NDENTRYMASK);
	if (fdp->fd_lomap[off] == ~0)
		fdp->fd_himap[off >> NDENTRYSHIFT] |= 1U << (off & NDENTRYMASK);

	if (fd > fdp->fd_lastfile)
		fdp->fd_lastfile = fd;
	fdp->fd_openfd++;
}

static __inline void
fd_unused(struct filedesc *fdp, int fd)
{
	u_int off = fd >> NDENTRYSHIFT;

	if (fd < fdp->fd_freefile)
		fdp->fd_freefile = fd;

	if (fdp->fd_lomap[off] == ~0)
		fdp->fd_himap[off >> NDENTRYSHIFT] &= ~(1U << (off & NDENTRYMASK));
	fdp->fd_lomap[off] &= ~(1U << (fd & NDENTRYMASK));

#ifdef DIAGNOSTIC
	if (fd > fdp->fd_lastfile)
		panic("fd_unused: fd_lastfile inconsistent");
#endif
	if (fd == fdp->fd_lastfile)
		fdp->fd_lastfile = find_last_set(fdp, fd);
	fdp->fd_openfd--;
}

struct file *
fd_iterfile(struct file *fp, struct proc *p)
{
	struct file *nfp;
	unsigned int count;

	mtx_enter(&fhdlk);
	if (fp == NULL)
		nfp = LIST_FIRST(&filehead);
	else
		nfp = LIST_NEXT(fp, f_list);

	/* don't refcount when f_count == 0 to avoid race in fdrop() */
	while (nfp != NULL) {
		count = nfp->f_count;
		if (count == 0) {
			nfp = LIST_NEXT(nfp, f_list);
			continue;
		}
		if (atomic_cas_uint(&nfp->f_count, count, count + 1) == count)
			break;
	}
	mtx_leave(&fhdlk);

	if (fp != NULL)
		FRELE(fp, p);

	return nfp;
}

struct file *
fd_getfile(struct filedesc *fdp, int fd)
{
	struct file *fp;

	vfs_stall_barrier();

	if ((u_int)fd >= fdp->fd_nfiles)
		return (NULL);

	mtx_enter(&fdp->fd_fplock);
	fp = fdp->fd_ofiles[fd];
	if (fp != NULL)
		atomic_inc_int(&fp->f_count);
	mtx_leave(&fdp->fd_fplock);

	return (fp);
}

struct file *
fd_getfile_mode(struct filedesc *fdp, int fd, int mode)
{
	struct file *fp;

	KASSERT(mode != 0);

	fp = fd_getfile(fdp, fd);
	if (fp == NULL)
		return (NULL);

	if ((fp->f_flag & mode) == 0) {
		FRELE(fp, curproc);
		return (NULL);
	}

	return (fp);
}

int
fd_checkclosed(struct filedesc *fdp, int fd, struct file *fp)
{
	int closed;

	mtx_enter(&fdp->fd_fplock);
	KASSERT(fd < fdp->fd_nfiles);
	closed = (fdp->fd_ofiles[fd] != fp);
	mtx_leave(&fdp->fd_fplock);
	return (closed);
}

/*
 * System calls on descriptors.
 */

/*
 * Duplicate a file descriptor.
 */
int
sys_dup(struct proc *p, void *v, register_t *retval)
{
	struct sys_dup_args /* {
		syscallarg(int) fd;
	} */ *uap = v;
	struct filedesc *fdp = p->p_fd;
	int old = SCARG(uap, fd);
	struct file *fp;
	int new;
	int error;

restart:
	if ((fp = fd_getfile(fdp, old)) == NULL)
		return (EBADF);
	fdplock(fdp);
	if ((error = fdalloc(p, 0, &new)) != 0) {
		if (error == ENOSPC) {
			fdexpand(p);
			fdpunlock(fdp);
			FRELE(fp, p);
			goto restart;
		}
		fdpunlock(fdp);
		FRELE(fp, p);
		return (error);
	}
	/* No need for FRELE(), finishdup() uses current ref. */
	return (finishdup(p, fp, old, new, retval, 0));
}

/*
 * Duplicate a file descriptor to a particular value.
 */
int
sys_dup2(struct proc *p, void *v, register_t *retval)
{
	struct sys_dup2_args /* {
		syscallarg(int) from;
		syscallarg(int) to;
	} */ *uap = v;

	return (dodup3(p, SCARG(uap, from), SCARG(uap, to), 0, retval));
}

int
sys_dup3(struct proc *p, void *v, register_t *retval)
{
	struct sys_dup3_args /* {
		syscallarg(int) from;
		syscallarg(int) to;
		syscallarg(int) flags;
	} */ *uap = v;

	if (SCARG(uap, from) == SCARG(uap, to))
		return (EINVAL);
	if (SCARG(uap, flags) & ~(O_CLOEXEC | O_CLOFORK))
		return (EINVAL);
	return (dodup3(p, SCARG(uap, from), SCARG(uap, to),
	    SCARG(uap, flags), retval));
}

int
dodup3(struct proc *p, int old, int new, int flags, register_t *retval)
{
	struct filedesc *fdp = p->p_fd;
	struct file *fp;
	int dupflags, error, i;

restart:
	if ((fp = fd_getfile(fdp, old)) == NULL)
		return (EBADF);
	if (old == new) {
		/*
		 * NOTE! This doesn't clear the close-on-exec flag. This might
		 * or might not be the intended behavior from the start, but
		 * this is what everyone else does.
		 */
		*retval = new;
		FRELE(fp, p);
		return (0);
	}
	if ((u_int)new >= lim_cur(RLIMIT_NOFILE) ||
	    (u_int)new >= atomic_load_int(&maxfiles)) {
		FRELE(fp, p);
		return (EBADF);
	}
	fdplock(fdp);
	if (new >= fdp->fd_nfiles) {
		if ((error = fdalloc(p, new, &i)) != 0) {
			if (error == ENOSPC) {
				fdexpand(p);
				fdpunlock(fdp);
				FRELE(fp, p);
				goto restart;
			}
			fdpunlock(fdp);
			FRELE(fp, p);
			return (error);
		}
		if (new != i)
			panic("dup2: fdalloc");
		fd_unused(fdp, new);
	}

	dupflags = DUPF_DUP2;
	if (flags & O_CLOEXEC)
		dupflags |= DUPF_CLOEXEC;
	if (flags & O_CLOFORK)
		dupflags |= DUPF_CLOFORK;

	/* No need for FRELE(), finishdup() uses current ref. */
	return (finishdup(p, fp, old, new, retval, dupflags));
}

/*
 * The file control system call.
 */
int
sys_fcntl(struct proc *p, void *v, register_t *retval)
{
	struct sys_fcntl_args /* {
		syscallarg(int) fd;
		syscallarg(int) cmd;
		syscallarg(void *) arg;
	} */ *uap = v;
	int fd = SCARG(uap, fd);
	struct filedesc *fdp = p->p_fd;
	struct file *fp;
	struct vnode *vp;
	int i, prev, tmp, newmin, flg = F_POSIX;
	struct flock fl;
	int error = 0;

	error = pledge_fcntl(p, SCARG(uap, cmd));
	if (error)
		return (error);

restart:
	if ((fp = fd_getfile(fdp, fd)) == NULL)
		return (EBADF);
	switch (SCARG(uap, cmd)) {

	case F_DUPFD:
	case F_DUPFD_CLOEXEC:
	case F_DUPFD_CLOFORK:
		newmin = (long)SCARG(uap, arg);
		if ((u_int)newmin >= lim_cur(RLIMIT_NOFILE) ||
		    (u_int)newmin >= atomic_load_int(&maxfiles)) {
			error = EINVAL;
			break;
		}
		fdplock(fdp);
		if ((error = fdalloc(p, newmin, &i)) != 0) {
			if (error == ENOSPC) {
				fdexpand(p);
				fdpunlock(fdp);
				FRELE(fp, p);
				goto restart;
			}
			fdpunlock(fdp);
			FRELE(fp, p);
		} else {
			int dupflags = 0;

			if (SCARG(uap, cmd) == F_DUPFD_CLOEXEC)
				dupflags |= DUPF_CLOEXEC;
			if (SCARG(uap, cmd) == F_DUPFD_CLOFORK)
				dupflags |= DUPF_CLOFORK;

			/* No need for FRELE(), finishdup() uses current ref. */
			error = finishdup(p, fp, fd, i, retval, dupflags);
		}
		return (error);

	case F_GETFD:
		fdplock(fdp);
		*retval = (fdp->fd_ofileflags[fd] & UF_EXCLOSE   ? FD_CLOEXEC : 0)
			| (fdp->fd_ofileflags[fd] & UF_FORKCLOSE ? FD_CLOFORK : 0);
		fdpunlock(fdp);
		break;

	case F_SETFD:
		fdplock(fdp);
		i = ((long)SCARG(uap, arg) & FD_CLOEXEC ? UF_EXCLOSE   : 0) |
		    ((long)SCARG(uap, arg) & FD_CLOFORK ? UF_FORKCLOSE : 0);
		fdp->fd_ofileflags[fd] = (fdp->fd_ofileflags[fd] &
		    ~(UF_EXCLOSE | UF_FORKCLOSE)) | i;
		fdpunlock(fdp);
		break;

	case F_GETFL:
		*retval = OFLAGS(fp->f_flag);
		break;

	case F_ISATTY:
		vp = fp->f_data;
	        if (fp->f_type == DTYPE_VNODE && (vp->v_flag & VISTTY))
			*retval = 1;
		else {
			*retval = 0;
			error = ENOTTY;
		}
		break;

	case F_SETFL:
		do {
			tmp = prev = fp->f_flag;
			tmp &= ~FCNTLFLAGS;
			tmp |= FFLAGS((long)SCARG(uap, arg)) & FCNTLFLAGS;
		} while (atomic_cas_uint(&fp->f_flag, prev, tmp) != prev);
		tmp = fp->f_flag & FASYNC;
		error = (*fp->f_ops->fo_ioctl)(fp, FIOASYNC, (caddr_t)&tmp, p);
		break;

	case F_GETOWN:
		tmp = 0;
		error = (*fp->f_ops->fo_ioctl)
			(fp, FIOGETOWN, (caddr_t)&tmp, p);
		*retval = tmp;
		break;

	case F_SETOWN:
		tmp = (long)SCARG(uap, arg);
		error = ((*fp->f_ops->fo_ioctl)
			(fp, FIOSETOWN, (caddr_t)&tmp, p));
		break;

	case F_SETLKW:
		flg |= F_WAIT;
		/* FALLTHROUGH */

	case F_SETLK:
		error = pledge_flock(p);
		if (error != 0)
			break;

		if (fp->f_type != DTYPE_VNODE) {
			error = EINVAL;
			break;
		}
		vp = fp->f_data;
		/* Copy in the lock structure */
		error = copyin((caddr_t)SCARG(uap, arg), (caddr_t)&fl,
		    sizeof (fl));
		if (error)
			break;
#ifdef KTRACE
		if (KTRPOINT(p, KTR_STRUCT))
			ktrflock(p, &fl);
#endif
		if (fl.l_whence == SEEK_CUR) {
			off_t offset = foffset(fp);

			if (fl.l_start == 0 && fl.l_len < 0) {
				/* lockf(3) compliance hack */
				fl.l_len = -fl.l_len;
				fl.l_start = offset - fl.l_len;
			} else
				fl.l_start += offset;
		}
		switch (fl.l_type) {

		case F_RDLCK:
			if ((fp->f_flag & FREAD) == 0) {
				error = EBADF;
				goto out;
			}
			atomic_setbits_int(&fdp->fd_flags, FD_ADVLOCK);
			error = VOP_ADVLOCK(vp, fdp, F_SETLK, &fl, flg);
			break;

		case F_WRLCK:
			if ((fp->f_flag & FWRITE) == 0) {
				error = EBADF;
				goto out;
			}
			atomic_setbits_int(&fdp->fd_flags, FD_ADVLOCK);
			error = VOP_ADVLOCK(vp, fdp, F_SETLK, &fl, flg);
			break;

		case F_UNLCK:
			error = VOP_ADVLOCK(vp, fdp, F_UNLCK, &fl, F_POSIX);
			goto out;

		default:
			error = EINVAL;
			goto out;
		}

		if (fd_checkclosed(fdp, fd, fp)) {
			/*
			 * We have lost the race with close() or dup2();
			 * unlock, pretend that we've won the race and that
			 * lock had been removed by close()
			 */
			fl.l_whence = SEEK_SET;
			fl.l_start = 0;
			fl.l_len = 0;
			VOP_ADVLOCK(vp, fdp, F_UNLCK, &fl, F_POSIX);
			fl.l_type = F_UNLCK;
		}
		goto out;


	case F_GETLK:
		error = pledge_flock(p);
		if (error != 0)
			break;

		if (fp->f_type != DTYPE_VNODE) {
			error = EINVAL;
			break;
		}
		vp = fp->f_data;
		/* Copy in the lock structure */
		error = copyin((caddr_t)SCARG(uap, arg), (caddr_t)&fl,
		    sizeof (fl));
		if (error)
			break;
		if (fl.l_whence == SEEK_CUR) {
			off_t offset = foffset(fp);

			if (fl.l_start == 0 && fl.l_len < 0) {
				/* lockf(3) compliance hack */
				fl.l_len = -fl.l_len;
				fl.l_start = offset - fl.l_len;
			} else
				fl.l_start += offset;
		}
		if (fl.l_type != F_RDLCK &&
		    fl.l_type != F_WRLCK &&
		    fl.l_type != F_UNLCK &&
		    fl.l_type != 0) {
			error = EINVAL;
			break;
		}
		error = VOP_ADVLOCK(vp, fdp, F_GETLK, &fl, F_POSIX);
		if (error)
			break;
#ifdef KTRACE
		if (KTRPOINT(p, KTR_STRUCT))
			ktrflock(p, &fl);
#endif
		error = (copyout((caddr_t)&fl, (caddr_t)SCARG(uap, arg),
		    sizeof (fl)));
		break;

	default:
		error = EINVAL;
		break;
	}
out:
	FRELE(fp, p);
	return (error);
}

/*
 * Common code for dup, dup2, and fcntl(F_DUPFD).
 */
int
finishdup(struct proc *p, struct file *fp, int old, int new,
    register_t *retval, int dupflags)
{
	struct file *oldfp;
	struct filedesc *fdp = p->p_fd;
	int error;

	fdpassertlocked(fdp);
	KASSERT(fp->f_iflags & FIF_INSERTED);

	if (fp->f_count >= FDUP_MAX_COUNT) {
		error = EDEADLK;
		goto fail;
	}

	oldfp = fd_getfile(fdp, new);
	if ((dupflags & DUPF_DUP2) && oldfp == NULL) {
		if (fd_inuse(fdp, new)) {
			error = EBUSY;
			goto fail;
		}
		fd_used(fdp, new);
	}

	/*
	 * Use `fd_fplock' to synchronize with fd_getfile() so that
	 * the function no longer creates a new reference to the old file.
	 */
	mtx_enter(&fdp->fd_fplock);
	fdp->fd_ofiles[new] = fp;
	mtx_leave(&fdp->fd_fplock);

	fdp->fd_ofileflags[new] = fdp->fd_ofileflags[old] &
	    ~(UF_EXCLOSE | UF_FORKCLOSE);
	if (dupflags & DUPF_CLOEXEC)
		fdp->fd_ofileflags[new] |= UF_EXCLOSE;
	if (dupflags & DUPF_CLOFORK)
		fdp->fd_ofileflags[new] |= UF_FORKCLOSE;
	*retval = new;

	if (oldfp != NULL) {
		knote_fdclose(p, new);
		fdpunlock(fdp);
		closef(oldfp, p);
	} else {
		fdpunlock(fdp);
	}

	return (0);

fail:
	fdpunlock(fdp);
	FRELE(fp, p);
	return (error);
}

void
fdinsert(struct filedesc *fdp, int fd, int flags, struct file *fp)
{
	struct file *fq;

	fdpassertlocked(fdp);

	mtx_enter(&fhdlk);
	if ((fp->f_iflags & FIF_INSERTED) == 0) {
		atomic_setbits_int(&fp->f_iflags, FIF_INSERTED);
		if ((fq = fdp->fd_ofiles[0]) != NULL) {
			LIST_INSERT_AFTER(fq, fp, f_list);
		} else {
			LIST_INSERT_HEAD(&filehead, fp, f_list);
		}
	}
	mtx_leave(&fhdlk);

	mtx_enter(&fdp->fd_fplock);
	KASSERT(fdp->fd_ofiles[fd] == NULL);
	fdp->fd_ofiles[fd] = fp;
	mtx_leave(&fdp->fd_fplock);

	fdp->fd_ofileflags[fd] |= (flags & (UF_EXCLOSE | UF_FORKCLOSE));
}

void
fdremove(struct filedesc *fdp, int fd)
{
	fdpassertlocked(fdp);

	/*
	 * Use `fd_fplock' to synchronize with fd_getfile() so that
	 * the function no longer creates a new reference to the file.
	 */
	mtx_enter(&fdp->fd_fplock);
	fdp->fd_ofiles[fd] = NULL;
	mtx_leave(&fdp->fd_fplock);

	fdp->fd_ofileflags[fd] = 0;

	fd_unused(fdp, fd);
}

int
fdrelease(struct proc *p, int fd)
{
	struct filedesc *fdp = p->p_fd;
	struct file *fp;

	fdpassertlocked(fdp);

	fp = fd_getfile(fdp, fd);
	if (fp == NULL) {
		fdpunlock(fdp);
		return (EBADF);
	}
	fdremove(fdp, fd);
	knote_fdclose(p, fd);
	fdpunlock(fdp);
	return (closef(fp, p));
}

/*
 * Close a file descriptor.
 */
int
sys_close(struct proc *p, void *v, register_t *retval)
{
	struct sys_close_args /* {
		syscallarg(int) fd;
	} */ *uap = v;
	int fd = SCARG(uap, fd), error;
	struct filedesc *fdp = p->p_fd;

	fdplock(fdp);
	/* fdrelease unlocks fdp. */
	error = fdrelease(p, fd);

	return (error);
}

/*
 * Return status information about a file descriptor.
 */
int
sys_fstat(struct proc *p, void *v, register_t *retval)
{
	struct sys_fstat_args /* {
		syscallarg(int) fd;
		syscallarg(struct stat *) sb;
	} */ *uap = v;
	int fd = SCARG(uap, fd);
	struct filedesc *fdp = p->p_fd;
	struct file *fp;
	struct stat ub;
	int error;

	if ((fp = fd_getfile(fdp, fd)) == NULL)
		return (EBADF);
	error = (*fp->f_ops->fo_stat)(fp, &ub, p);
	FRELE(fp, p);
	if (error == 0) {
		/* 
		 * Don't let non-root see generation numbers
		 * (for NFS security)
		 */
		if (suser(p))
			ub.st_gen = 0;
		error = copyout((caddr_t)&ub, (caddr_t)SCARG(uap, sb),
		    sizeof (ub));
	}
#ifdef KTRACE
	if (error == 0 && KTRPOINT(p, KTR_STRUCT))
		ktrstat(p, &ub);
#endif
	return (error);
}

/*
 * Return pathconf information about a file descriptor.
 */
int
sys_fpathconf(struct proc *p, void *v, register_t *retval)
{
	struct sys_fpathconf_args /* {
		syscallarg(int) fd;
		syscallarg(int) name;
	} */ *uap = v;
	int fd = SCARG(uap, fd);
	struct filedesc *fdp = p->p_fd;
	struct file *fp;
	struct vnode *vp;
	int error;

	if ((fp = fd_getfile(fdp, fd)) == NULL)
		return (EBADF);
	switch (fp->f_type) {
	case DTYPE_PIPE:
	case DTYPE_SOCKET:
		if (SCARG(uap, name) != _PC_PIPE_BUF) {
			error = EINVAL;
			break;
		}
		*retval = PIPE_BUF;
		error = 0;
		break;

	case DTYPE_VNODE:
		vp = fp->f_data;
		vn_lock(vp, LK_EXCLUSIVE | LK_RETRY);
		error = VOP_PATHCONF(vp, SCARG(uap, name), retval);
		VOP_UNLOCK(vp);
		break;

	default:
		error = EOPNOTSUPP;
		break;
	}
	FRELE(fp, p);
	return (error);
}

/*
 * Allocate a file descriptor for the process.
 */
int
fdalloc(struct proc *p, int want, int *result)
{
	struct filedesc *fdp = p->p_fd;
	int lim, last, i;
	u_int new, off;

	fdpassertlocked(fdp);

	/*
	 * Search for a free descriptor starting at the higher
	 * of want or fd_freefile.  If that fails, consider
	 * expanding the ofile array.
	 */
restart:
	lim = min((int)lim_cur(RLIMIT_NOFILE), atomic_load_int(&maxfiles));
	last = min(fdp->fd_nfiles, lim);
	if ((i = want) < fdp->fd_freefile)
		i = fdp->fd_freefile;
	off = i >> NDENTRYSHIFT;
	new = find_next_zero(fdp->fd_himap, off,
	    (last + NDENTRIES - 1) >> NDENTRYSHIFT);
	if (new != -1) {
		i = find_next_zero(&fdp->fd_lomap[new], 
				   new > off ? 0 : i & NDENTRYMASK,
				   NDENTRIES);
		if (i == -1) {
			/*
			 * Free file descriptor in this block was
			 * below want, try again with higher want.
			 */
			want = (new + 1) << NDENTRYSHIFT;
			goto restart;
		}
		i += (new << NDENTRYSHIFT);
		if (i < last) {
			fd_used(fdp, i);
			if (want <= fdp->fd_freefile)
				fdp->fd_freefile = i;
			*result = i;
			fdp->fd_ofileflags[i] = 0;
			if (ISSET(p->p_p->ps_flags, PS_PLEDGE))
				fdp->fd_ofileflags[i] |= UF_PLEDGED;
			return (0);
		}
	}
	if (fdp->fd_nfiles >= lim)
		return (EMFILE);

	return (ENOSPC);
}

void
fdexpand(struct proc *p)
{
	struct filedesc *fdp = p->p_fd;
	int nfiles, oldnfiles;
	size_t copylen;
	struct file **newofile, **oldofile;
	char *newofileflags;
	u_int *newhimap, *newlomap;

	fdpassertlocked(fdp);

	oldnfiles = fdp->fd_nfiles;
	oldofile = fdp->fd_ofiles;

	/*
	 * No space in current array.
	 */
	if (fdp->fd_nfiles < NDEXTENT)
		nfiles = NDEXTENT;
	else
		nfiles = 2 * fdp->fd_nfiles;

	newofile = mallocarray(nfiles, OFILESIZE, M_FILEDESC, M_WAITOK);
	/*
	 * Allocate all required chunks before calling free(9) to make
	 * sure that ``fd_ofiles'' stays valid if we go to sleep.
	 */
	if (NDHISLOTS(nfiles) > NDHISLOTS(fdp->fd_nfiles)) {
		newhimap = mallocarray(NDHISLOTS(nfiles), sizeof(u_int),
		    M_FILEDESC, M_WAITOK);
		newlomap = mallocarray(NDLOSLOTS(nfiles), sizeof(u_int),
		    M_FILEDESC, M_WAITOK);
	}
	newofileflags = (char *) &newofile[nfiles];

	/*
	 * Copy the existing ofile and ofileflags arrays
	 * and zero the new portion of each array.
	 */
	copylen = sizeof(struct file *) * fdp->fd_nfiles;
	memcpy(newofile, fdp->fd_ofiles, copylen);
	memset((char *)newofile + copylen, 0,
	    nfiles * sizeof(struct file *) - copylen);
	copylen = sizeof(char) * fdp->fd_nfiles;
	memcpy(newofileflags, fdp->fd_ofileflags, copylen);
	memset(newofileflags + copylen, 0, nfiles * sizeof(char) - copylen);

	if (NDHISLOTS(nfiles) > NDHISLOTS(fdp->fd_nfiles)) {
		copylen = NDHISLOTS(fdp->fd_nfiles) * sizeof(u_int);
		memcpy(newhimap, fdp->fd_himap, copylen);
		memset((char *)newhimap + copylen, 0,
		    NDHISLOTS(nfiles) * sizeof(u_int) - copylen);

		copylen = NDLOSLOTS(fdp->fd_nfiles) * sizeof(u_int);
		memcpy(newlomap, fdp->fd_lomap, copylen);
		memset((char *)newlomap + copylen, 0,
		    NDLOSLOTS(nfiles) * sizeof(u_int) - copylen);

		if (NDHISLOTS(fdp->fd_nfiles) > NDHISLOTS(NDFILE)) {
			free(fdp->fd_himap, M_FILEDESC,
			    NDHISLOTS(fdp->fd_nfiles) * sizeof(u_int));
			free(fdp->fd_lomap, M_FILEDESC,
			    NDLOSLOTS(fdp->fd_nfiles) * sizeof(u_int));
		}
		fdp->fd_himap = newhimap;
		fdp->fd_lomap = newlomap;
	}

	mtx_enter(&fdp->fd_fplock);
	fdp->fd_ofiles = newofile;
	mtx_leave(&fdp->fd_fplock);

	fdp->fd_ofileflags = newofileflags;
	fdp->fd_nfiles = nfiles;

	if (oldnfiles > NDFILE)
		free(oldofile, M_FILEDESC, oldnfiles * OFILESIZE);
}

/*
 * Create a new open file structure and allocate
 * a file descriptor for the process that refers to it.
 */
int
falloc(struct proc *p, struct file **resultfp, int *resultfd)
{
	struct file *fp;
	int error, i;

	KASSERT(resultfp != NULL);
	KASSERT(resultfd != NULL);

	fdpassertlocked(p->p_fd);
restart:
	if ((error = fdalloc(p, 0, &i)) != 0) {
		if (error == ENOSPC) {
			fdexpand(p);
			goto restart;
		}
		return (error);
	}

	fp = fnew(p);
	if (fp == NULL) {
		fd_unused(p->p_fd, i);
		return (ENFILE);
	}

	FREF(fp);
	*resultfp = fp;
	*resultfd = i;

	return (0);
}

struct file *
fnew(struct proc *p)
{
	struct file *fp;
	int nfiles;

	nfiles = atomic_inc_int_nv(&numfiles);
	if (nfiles > atomic_load_int(&maxfiles)) {
		atomic_dec_int(&numfiles);
		tablefull("file");
		return (NULL);
	}

	fp = pool_get(&file_pool, PR_WAITOK|PR_ZERO);
	/*
	 * We need to block interrupts as long as `f_mtx' is being taken
	 * with and without the KERNEL_LOCK().
	 */
	mtx_init(&fp->f_mtx, IPL_MPFLOOR);
	fp->f_count = 1;
	fp->f_cred = p->p_ucred;
	crhold(fp->f_cred);

	return (fp);
}

/*
 * Build a new filedesc structure.
 */
struct filedesc *
fdinit(void)
{
	struct filedesc0 *newfdp;

	newfdp = pool_get(&fdesc_pool, PR_WAITOK|PR_ZERO);
	rw_init(&newfdp->fd_fd.fd_lock, "fdlock");
	mtx_init(&newfdp->fd_fd.fd_fplock, IPL_MPFLOOR);
	LIST_INIT(&newfdp->fd_fd.fd_kqlist);

	/* Create the file descriptor table. */
	newfdp->fd_fd.fd_refcnt = 1;
	newfdp->fd_fd.fd_cmask = S_IWGRP|S_IWOTH;
	newfdp->fd_fd.fd_ofiles = newfdp->fd_dfiles;
	newfdp->fd_fd.fd_ofileflags = newfdp->fd_dfileflags;
	newfdp->fd_fd.fd_nfiles = NDFILE;
	newfdp->fd_fd.fd_himap = newfdp->fd_dhimap;
	newfdp->fd_fd.fd_lomap = newfdp->fd_dlomap;

	newfdp->fd_fd.fd_freefile = 0;
	newfdp->fd_fd.fd_lastfile = 0;

	return (&newfdp->fd_fd);
}

/*
 * Share a filedesc structure.
 */
struct filedesc *
fdshare(struct process *pr)
{
	pr->ps_fd->fd_refcnt++;
	return (pr->ps_fd);
}

/*
 * Copy a filedesc structure.
 */
struct filedesc *
fdcopy(struct process *pr)
{
	struct filedesc *newfdp, *fdp = pr->ps_fd;
	int i;

	newfdp = fdinit();

	fdplock(fdp);
	if (fdp->fd_cdir) {
		vref(fdp->fd_cdir);
		newfdp->fd_cdir = fdp->fd_cdir;
	}
	if (fdp->fd_rdir) {
		vref(fdp->fd_rdir);
		newfdp->fd_rdir = fdp->fd_rdir;
	}

	/*
	 * If the number of open files fits in the internal arrays
	 * of the open file structure, use them, otherwise allocate
	 * additional memory for the number of descriptors currently
	 * in use.
	 */
	if (fdp->fd_lastfile >= NDFILE) {
		/*
		 * Compute the smallest multiple of NDEXTENT needed
		 * for the file descriptors currently in use,
		 * allowing the table to shrink.
		 */
		i = fdp->fd_nfiles;
		while (i >= 2 * NDEXTENT && i > fdp->fd_lastfile * 2)
			i /= 2;
		newfdp->fd_ofiles = mallocarray(i, OFILESIZE, M_FILEDESC,
		    M_WAITOK | M_ZERO);
		newfdp->fd_ofileflags = (char *) &newfdp->fd_ofiles[i];
		newfdp->fd_nfiles = i;
	}
	if (NDHISLOTS(newfdp->fd_nfiles) > NDHISLOTS(NDFILE)) {
		newfdp->fd_himap = mallocarray(NDHISLOTS(newfdp->fd_nfiles),
		    sizeof(u_int), M_FILEDESC, M_WAITOK | M_ZERO);
		newfdp->fd_lomap = mallocarray(NDLOSLOTS(newfdp->fd_nfiles),
		    sizeof(u_int), M_FILEDESC, M_WAITOK | M_ZERO);
	}
	newfdp->fd_freefile = fdp->fd_freefile;
	newfdp->fd_flags = fdp->fd_flags;
	newfdp->fd_cmask = fdp->fd_cmask;

	for (i = 0; i <= fdp->fd_lastfile; i++) {
		struct file *fp = fdp->fd_ofiles[i];

		if (fp != NULL) {
			int fileflags = fdp->fd_ofileflags[i];
			/*
			 * If the UF_FORKCLOSE flag is set, skip the fd.
			 * XXX Gruesome hack. If count gets too high, fail
			 * to copy an fd, since fdcopy()'s callers do not
			 * permit it to indicate failure yet.
			 * Meanwhile, kqueue files have to be
			 * tied to the process that opened them to enforce
			 * their internal consistency, so close them here.
			 */
			if (fp->f_count >= FDUP_MAX_COUNT ||
			    (fileflags & UF_FORKCLOSE) ||
			    fp->f_type == DTYPE_KQUEUE) {
				if (i < newfdp->fd_freefile)
					newfdp->fd_freefile = i;
				continue;
			}

			FREF(fp);
			newfdp->fd_ofiles[i] = fp;
			newfdp->fd_ofileflags[i] = fileflags;
			fd_used(newfdp, i);
		}
	}
	fdpunlock(fdp);

	return (newfdp);
}

/*
 * Release a filedesc structure.
 */
void
fdfree(struct proc *p)
{
	struct filedesc *fdp = p->p_fd;
	struct file *fp;
	int fd;

	if (--fdp->fd_refcnt > 0)
		return;
	for (fd = 0; fd <= fdp->fd_lastfile; fd++) {
		fp = fdp->fd_ofiles[fd];
		if (fp != NULL) {
			fdp->fd_ofiles[fd] = NULL;
			knote_fdclose(p, fd);
			 /* closef() expects a refcount of 2 */
			FREF(fp);
			(void) closef(fp, p);
		}
	}
	p->p_fd = NULL;
	if (fdp->fd_nfiles > NDFILE)
		free(fdp->fd_ofiles, M_FILEDESC, fdp->fd_nfiles * OFILESIZE);
	if (NDHISLOTS(fdp->fd_nfiles) > NDHISLOTS(NDFILE)) {
		free(fdp->fd_himap, M_FILEDESC,
		    NDHISLOTS(fdp->fd_nfiles) * sizeof(u_int));
		free(fdp->fd_lomap, M_FILEDESC,
		    NDLOSLOTS(fdp->fd_nfiles) * sizeof(u_int));
	}
	if (fdp->fd_cdir)
		vrele(fdp->fd_cdir);
	if (fdp->fd_rdir)
		vrele(fdp->fd_rdir);
	KASSERT(atomic_load_int(&fdp->fd_nuserevents) == 0);
	pool_put(&fdesc_pool, fdp);
}

/*
 * Internal form of close.
 * Decrement reference count on file structure.
 * Note: p may be NULL when closing a file
 * that was being passed in a message.
 *
 * The fp must have its usecount bumped and will be FRELEd here.
 */
int
closef(struct file *fp, struct proc *p)
{
	struct filedesc *fdp;

	if (fp == NULL)
		return (0);

	KASSERTMSG(fp->f_count >= 2, "count (%u) < 2", fp->f_count);

	atomic_dec_int(&fp->f_count);

	/*
	 * POSIX record locking dictates that any close releases ALL
	 * locks owned by this process.  This is handled by setting
	 * a flag in the unlock to free ONLY locks obeying POSIX
	 * semantics, and not to free BSD-style file locks.
	 * If the descriptor was in a message, POSIX-style locks
	 * aren't passed with the descriptor.
	 */

	if (p && ((fdp = p->p_fd) != NULL) &&
	    (fdp->fd_flags & FD_ADVLOCK) &&
	    fp->f_type == DTYPE_VNODE) {
		struct vnode *vp = fp->f_data;
		struct flock lf;

		lf.l_whence = SEEK_SET;
		lf.l_start = 0;
		lf.l_len = 0;
		lf.l_type = F_UNLCK;
		(void) VOP_ADVLOCK(vp, fdp, F_UNLCK, &lf, F_POSIX);
	}

	return (FRELE(fp, p));
}

int
fdrop(struct file *fp, struct proc *p)
{
	int error;

	KASSERTMSG(fp->f_count == 0, "count (%u) != 0", fp->f_count);

	mtx_enter(&fhdlk);
	if (fp->f_iflags & FIF_INSERTED)
		LIST_REMOVE(fp, f_list);
	mtx_leave(&fhdlk);

	if (fp->f_ops)
		error = (*fp->f_ops->fo_close)(fp, p);
	else
		error = 0;

	crfree(fp->f_cred);
	atomic_dec_int(&numfiles);
	pool_put(&file_pool, fp);

	return (error);
}

/*
 * Apply an advisory lock on a file descriptor.
 *
 * Just attempt to get a record lock of the requested type on
 * the entire file (l_whence = SEEK_SET, l_start = 0, l_len = 0).
 */
int
sys_flock(struct proc *p, void *v, register_t *retval)
{
	struct sys_flock_args /* {
		syscallarg(int) fd;
		syscallarg(int) how;
	} */ *uap = v;
	int fd = SCARG(uap, fd);
	int how = SCARG(uap, how);
	struct filedesc *fdp = p->p_fd;
	struct file *fp;
	struct vnode *vp;
	struct flock lf;
	int error;

	if ((fp = fd_getfile(fdp, fd)) == NULL)
		return (EBADF);
	if (fp->f_type != DTYPE_VNODE) {
		error = EOPNOTSUPP;
		goto out;
	}
	vp = fp->f_data;
	lf.l_whence = SEEK_SET;
	lf.l_start = 0;
	lf.l_len = 0;
	if (how & LOCK_UN) {
		lf.l_type = F_UNLCK;
		atomic_clearbits_int(&fp->f_iflags, FIF_HASLOCK);
		error = VOP_ADVLOCK(vp, (caddr_t)fp, F_UNLCK, &lf, F_FLOCK);
		goto out;
	}
	if (how & LOCK_EX)
		lf.l_type = F_WRLCK;
	else if (how & LOCK_SH)
		lf.l_type = F_RDLCK;
	else {
		error = EINVAL;
		goto out;
	}
	atomic_setbits_int(&fp->f_iflags, FIF_HASLOCK);
	if (how & LOCK_NB)
		error = VOP_ADVLOCK(vp, (caddr_t)fp, F_SETLK, &lf, F_FLOCK);
	else
		error = VOP_ADVLOCK(vp, (caddr_t)fp, F_SETLK, &lf, F_FLOCK|F_WAIT);
out:
	FRELE(fp, p);
	return (error);
}

/*
 * File Descriptor pseudo-device driver (/dev/fd/).
 *
 * Opening minor device N dup()s the file (if any) connected to file
 * descriptor N belonging to the calling process.  Note that this driver
 * consists of only the ``open()'' routine, because all subsequent
 * references to this file will be direct to the other driver.
 */
int
filedescopen(dev_t dev, int mode, int type, struct proc *p)
{

	/*
	 * XXX Kludge: set curproc->p_dupfd to contain the value of the
	 * the file descriptor being sought for duplication. The error
	 * return ensures that the vnode for this device will be released
	 * by vn_open. Open will detect this special error and take the
	 * actions in dupfdopen below. Other callers of vn_open or VOP_OPEN
	 * will simply report the error.
	 */
	p->p_dupfd = minor(dev);
	return (ENODEV);
}

/*
 * Duplicate the specified descriptor to a free descriptor.
 */
int
dupfdopen(struct proc *p, int indx, int mode)
{
	struct filedesc *fdp = p->p_fd;
	int dupfd = p->p_dupfd;
	struct file *wfp;

	fdpassertlocked(fdp);

	/*
	 * Assume that the filename was user-specified; applications do
	 * not tend to open /dev/fd/# when they can just call dup()
	 */
	if ((p->p_p->ps_flags & (PS_SUGIDEXEC | PS_SUGID))) {
		if (p->p_descfd == 255)
			return (EPERM);
		if (p->p_descfd != dupfd)
			return (EPERM);
	}

	/*
	 * If the to-be-dup'd fd number is greater than the allowed number
	 * of file descriptors, or the fd to be dup'd has already been
	 * closed, reject. Note, there is no need to check for new == old
	 * because fd_getfile will return NULL if the file at indx is
	 * newly created by falloc.
	 */
	if ((wfp = fd_getfile(fdp, dupfd)) == NULL)
		return (EBADF);

	/*
	 * Check that the mode the file is being opened for is a
	 * subset of the mode of the existing descriptor.
	 */
	if (((mode & (FREAD|FWRITE)) | wfp->f_flag) != wfp->f_flag) {
		FRELE(wfp, p);
		return (EACCES);
	}
	if (wfp->f_count >= FDUP_MAX_COUNT) {
		FRELE(wfp, p);
		return (EDEADLK);
	}

	KASSERT(wfp->f_iflags & FIF_INSERTED);

	mtx_enter(&fdp->fd_fplock);
	KASSERT(fdp->fd_ofiles[indx] == NULL);
	fdp->fd_ofiles[indx] = wfp;
	mtx_leave(&fdp->fd_fplock);

	fdp->fd_ofileflags[indx] =
	    (fdp->fd_ofileflags[indx] & (UF_EXCLOSE | UF_FORKCLOSE)) |
	    (fdp->fd_ofileflags[dupfd] & ~(UF_EXCLOSE | UF_FORKCLOSE));

	return (0);
}

/*
 * Doing an exec, so handle fd flags: do close-on-exec and clear
 * pledged and close-on-fork
 */
void
fdprepforexec(struct proc *p)
{
	struct filedesc *fdp = p->p_fd;
	int fd;

	fdplock(fdp);
	for (fd = 0; fd <= fdp->fd_lastfile; fd++) {
		fdp->fd_ofileflags[fd] &= ~(UF_PLEDGED | UF_FORKCLOSE);
		if (fdp->fd_ofileflags[fd] & UF_EXCLOSE) {
			/* fdrelease() unlocks fdp. */
			(void) fdrelease(p, fd);
			fdplock(fdp);
		}
	}
	fdpunlock(fdp);
}

int
sys_closefrom(struct proc *p, void *v, register_t *retval)
{
	struct sys_closefrom_args *uap = v;
	struct filedesc *fdp = p->p_fd;
	u_int startfd, i;

	startfd = SCARG(uap, fd);
	fdplock(fdp);

	if (startfd > fdp->fd_lastfile) {
		fdpunlock(fdp);
		return (EBADF);
	}

	for (i = startfd; i <= fdp->fd_lastfile; i++) {
		/* fdrelease() unlocks fdp. */
		fdrelease(p, i);
		fdplock(fdp);
	}

	fdpunlock(fdp);
	return (0);
}

int
sys_getdtablecount(struct proc *p, void *v, register_t *retval)
{
	*retval = p->p_fd->fd_openfd;
	return (0);
}
