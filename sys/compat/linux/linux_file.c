/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 1994-1995 Søren Schmidt
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

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include "opt_compat.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/capsicum.h>
#include <sys/conf.h>
#include <sys/dirent.h>
#include <sys/fcntl.h>
#include <sys/file.h>
#include <sys/filedesc.h>
#include <sys/lock.h>
#include <sys/malloc.h>
#include <sys/mount.h>
#include <sys/mutex.h>
#include <sys/namei.h>
#include <sys/proc.h>
#include <sys/stat.h>
#include <sys/sx.h>
#include <sys/syscallsubr.h>
#include <sys/sysproto.h>
#include <sys/tty.h>
#include <sys/unistd.h>
#include <sys/vnode.h>

#ifdef COMPAT_LINUX32
#include <machine/../linux32/linux.h>
#include <machine/../linux32/linux32_proto.h>
#else
#include <machine/../linux/linux.h>
#include <machine/../linux/linux_proto.h>
#endif
#include <compat/linux/linux_misc.h>
#include <compat/linux/linux_util.h>
#include <compat/linux/linux_file.h>

static int	linux_common_open(struct thread *, int, char *, int, int);
static int	linux_getdents_error(struct thread *, int, int);


#ifdef LINUX_LEGACY_SYSCALLS
int
linux_creat(struct thread *td, struct linux_creat_args *args)
{
	char *path;
	int error;

	LCONVPATHEXIST(td, args->path, &path);
#ifdef DEBUG
	if (ldebug(creat))
		printf(ARGS(creat, "%s, %d"), path, args->mode);
#endif
	error = kern_openat(td, AT_FDCWD, path, UIO_SYSSPACE,
	    O_WRONLY | O_CREAT | O_TRUNC, args->mode);
	LFREEPATH(path);
	return (error);
}
#endif

static int
linux_common_open(struct thread *td, int dirfd, char *path, int l_flags, int mode)
{
	struct proc *p = td->td_proc;
	struct file *fp;
	int fd;
	int bsd_flags, error;

	bsd_flags = 0;
	switch (l_flags & LINUX_O_ACCMODE) {
	case LINUX_O_WRONLY:
		bsd_flags |= O_WRONLY;
		break;
	case LINUX_O_RDWR:
		bsd_flags |= O_RDWR;
		break;
	default:
		bsd_flags |= O_RDONLY;
	}
	if (l_flags & LINUX_O_NDELAY)
		bsd_flags |= O_NONBLOCK;
	if (l_flags & LINUX_O_APPEND)
		bsd_flags |= O_APPEND;
	if (l_flags & LINUX_O_SYNC)
		bsd_flags |= O_FSYNC;
	if (l_flags & LINUX_O_NONBLOCK)
		bsd_flags |= O_NONBLOCK;
	if (l_flags & LINUX_FASYNC)
		bsd_flags |= O_ASYNC;
	if (l_flags & LINUX_O_CREAT)
		bsd_flags |= O_CREAT;
	if (l_flags & LINUX_O_TRUNC)
		bsd_flags |= O_TRUNC;
	if (l_flags & LINUX_O_EXCL)
		bsd_flags |= O_EXCL;
	if (l_flags & LINUX_O_NOCTTY)
		bsd_flags |= O_NOCTTY;
	if (l_flags & LINUX_O_DIRECT)
		bsd_flags |= O_DIRECT;
	if (l_flags & LINUX_O_NOFOLLOW)
		bsd_flags |= O_NOFOLLOW;
	if (l_flags & LINUX_O_DIRECTORY)
		bsd_flags |= O_DIRECTORY;
	/* XXX LINUX_O_NOATIME: unable to be easily implemented. */

	error = kern_openat(td, dirfd, path, UIO_SYSSPACE, bsd_flags, mode);
	if (error != 0)
		goto done;
	if (bsd_flags & O_NOCTTY)
		goto done;

	/*
	 * XXX In between kern_openat() and fget(), another process
	 * having the same filedesc could use that fd without
	 * checking below.
	*/
	fd = td->td_retval[0];
	if (fget(td, fd, &cap_ioctl_rights, &fp) == 0) {
		if (fp->f_type != DTYPE_VNODE) {
			fdrop(fp, td);
			goto done;
		}
		sx_slock(&proctree_lock);
		PROC_LOCK(p);
		if (SESS_LEADER(p) && !(p->p_flag & P_CONTROLT)) {
			PROC_UNLOCK(p);
			sx_sunlock(&proctree_lock);
			/* XXXPJD: Verify if TIOCSCTTY is allowed. */
			(void) fo_ioctl(fp, TIOCSCTTY, (caddr_t) 0,
			    td->td_ucred, td);
		} else {
			PROC_UNLOCK(p);
			sx_sunlock(&proctree_lock);
		}
		fdrop(fp, td);
	}

done:
#ifdef DEBUG
#ifdef LINUX_LEGACY_SYSCALLS
	if (ldebug(open))
#else
	if (ldebug(openat))
#endif
		printf(LMSG("open returns error %d"), error);
#endif
	LFREEPATH(path);
	return (error);
}

int
linux_openat(struct thread *td, struct linux_openat_args *args)
{
	char *path;
	int dfd;

	dfd = (args->dfd == LINUX_AT_FDCWD) ? AT_FDCWD : args->dfd;
	if (args->flags & LINUX_O_CREAT)
		LCONVPATH_AT(td, args->filename, &path, 1, dfd);
	else
		LCONVPATH_AT(td, args->filename, &path, 0, dfd);
#ifdef DEBUG
	if (ldebug(openat))
		printf(ARGS(openat, "%i, %s, 0x%x, 0x%x"), args->dfd,
		    path, args->flags, args->mode);
#endif
	return (linux_common_open(td, dfd, path, args->flags, args->mode));
}

#ifdef LINUX_LEGACY_SYSCALLS
int
linux_open(struct thread *td, struct linux_open_args *args)
{
	char *path;

	if (args->flags & LINUX_O_CREAT)
		LCONVPATHCREAT(td, args->path, &path);
	else
		LCONVPATHEXIST(td, args->path, &path);
#ifdef DEBUG
	if (ldebug(open))
		printf(ARGS(open, "%s, 0x%x, 0x%x"),
		    path, args->flags, args->mode);
#endif
	return (linux_common_open(td, AT_FDCWD, path, args->flags, args->mode));
}
#endif

int
linux_lseek(struct thread *td, struct linux_lseek_args *args)
{

#ifdef DEBUG
	if (ldebug(lseek))
		printf(ARGS(lseek, "%d, %ld, %d"),
		    args->fdes, (long)args->off, args->whence);
#endif
	return (kern_lseek(td, args->fdes, args->off, args->whence));
}

#if defined(__i386__) || (defined(__amd64__) && defined(COMPAT_LINUX32))
int
linux_llseek(struct thread *td, struct linux_llseek_args *args)
{
	int error;
	off_t off;

#ifdef DEBUG
	if (ldebug(llseek))
		printf(ARGS(llseek, "%d, %d:%d, %d"),
		    args->fd, args->ohigh, args->olow, args->whence);
#endif
	off = (args->olow) | (((off_t) args->ohigh) << 32);

	error = kern_lseek(td, args->fd, off, args->whence);
	if (error != 0)
		return (error);

	error = copyout(td->td_retval, args->res, sizeof(off_t));
	if (error != 0)
		return (error);

	td->td_retval[0] = 0;
	return (0);
}
#endif /* __i386__ || (__amd64__ && COMPAT_LINUX32) */

/*
 * Note that linux_getdents(2) and linux_getdents64(2) have the same
 * arguments. They only differ in the definition of struct dirent they
 * operate on.
 * Note that linux_readdir(2) is a special case of linux_getdents(2)
 * where count is always equals 1, meaning that the buffer is one
 * dirent-structure in size and that the code can't handle more anyway.
 * Note that linux_readdir(2) can't be implemented by means of linux_getdents(2)
 * as in case when the *dent buffer size is equal to 1 linux_getdents(2) will
 * trash user stack.
 */

static int
linux_getdents_error(struct thread *td, int fd, int err)
{
	struct vnode *vp;
	struct file *fp;
	int error;

	/* Linux return ENOTDIR in case when fd is not a directory. */
	error = getvnode(td, fd, &cap_read_rights, &fp);
	if (error != 0)
		return (error);
	vp = fp->f_vnode;
	if (vp->v_type != VDIR) {
		fdrop(fp, td);
		return (ENOTDIR);
	}
	fdrop(fp, td);
	return (err);
}

struct l_dirent {
	l_ulong		d_ino;
	l_off_t		d_off;
	l_ushort	d_reclen;
	char		d_name[LINUX_NAME_MAX + 1];
};

struct l_dirent64 {
	uint64_t	d_ino;
	int64_t		d_off;
	l_ushort	d_reclen;
	u_char		d_type;
	char		d_name[LINUX_NAME_MAX + 1];
};

/*
 * Linux uses the last byte in the dirent buffer to store d_type,
 * at least glibc-2.7 requires it. That is why l_dirent is padded with 2 bytes.
 */
#define LINUX_RECLEN(namlen)						\
    roundup(offsetof(struct l_dirent, d_name) + (namlen) + 2, sizeof(l_ulong))

#define LINUX_RECLEN64(namlen)						\
    roundup(offsetof(struct l_dirent64, d_name) + (namlen) + 1,		\
    sizeof(uint64_t))

#ifdef LINUX_LEGACY_SYSCALLS
int
linux_getdents(struct thread *td, struct linux_getdents_args *args)
{
	struct dirent *bdp;
	caddr_t inp, buf;		/* BSD-format */
	int len, reclen;		/* BSD-format */
	caddr_t outp;			/* Linux-format */
	int resid, linuxreclen;		/* Linux-format */
	caddr_t lbuf;			/* Linux-format */
	off_t base;
	struct l_dirent *linux_dirent;
	int buflen, error;
	size_t retval;

#ifdef DEBUG
	if (ldebug(getdents))
		printf(ARGS(getdents, "%d, *, %d"), args->fd, args->count);
#endif
	buflen = min(args->count, MAXBSIZE);
	buf = malloc(buflen, M_TEMP, M_WAITOK);

	error = kern_getdirentries(td, args->fd, buf, buflen,
	    &base, NULL, UIO_SYSSPACE);
	if (error != 0) {
		error = linux_getdents_error(td, args->fd, error);
		goto out1;
	}

	lbuf = malloc(LINUX_RECLEN(LINUX_NAME_MAX), M_TEMP, M_WAITOK | M_ZERO);

	len = td->td_retval[0];
	inp = buf;
	outp = (caddr_t)args->dent;
	resid = args->count;
	retval = 0;

	while (len > 0) {
		bdp = (struct dirent *) inp;
		reclen = bdp->d_reclen;
		linuxreclen = LINUX_RECLEN(bdp->d_namlen);
		/*
		 * No more space in the user supplied dirent buffer.
		 * Return EINVAL.
		 */
		if (resid < linuxreclen) {
			error = EINVAL;
			goto out;
		}

		linux_dirent = (struct l_dirent*)lbuf;
		linux_dirent->d_ino = bdp->d_fileno;
		linux_dirent->d_off = base + reclen;
		linux_dirent->d_reclen = linuxreclen;
		/*
		 * Copy d_type to last byte of l_dirent buffer
		 */
		lbuf[linuxreclen - 1] = bdp->d_type;
		strlcpy(linux_dirent->d_name, bdp->d_name,
		    linuxreclen - offsetof(struct l_dirent, d_name)-1);
		error = copyout(linux_dirent, outp, linuxreclen);
		if (error != 0)
			goto out;

		inp += reclen;
		base += reclen;
		len -= reclen;

		retval += linuxreclen;
		outp += linuxreclen;
		resid -= linuxreclen;
	}
	td->td_retval[0] = retval;

out:
	free(lbuf, M_TEMP);
out1:
	free(buf, M_TEMP);
	return (error);
}
#endif

int
linux_getdents64(struct thread *td, struct linux_getdents64_args *args)
{
	struct dirent *bdp;
	caddr_t inp, buf;		/* BSD-format */
	int len, reclen;		/* BSD-format */
	caddr_t outp;			/* Linux-format */
	int resid, linuxreclen;		/* Linux-format */
	caddr_t lbuf;			/* Linux-format */
	off_t base;
	struct l_dirent64 *linux_dirent64;
	int buflen, error;
	size_t retval;

#ifdef DEBUG
	if (ldebug(getdents64))
		uprintf(ARGS(getdents64, "%d, *, %d"), args->fd, args->count);
#endif
	buflen = min(args->count, MAXBSIZE);
	buf = malloc(buflen, M_TEMP, M_WAITOK);

	error = kern_getdirentries(td, args->fd, buf, buflen,
	    &base, NULL, UIO_SYSSPACE);
	if (error != 0) {
		error = linux_getdents_error(td, args->fd, error);
		goto out1;
	}

	lbuf = malloc(LINUX_RECLEN64(LINUX_NAME_MAX), M_TEMP, M_WAITOK | M_ZERO);

	len = td->td_retval[0];
	inp = buf;
	outp = (caddr_t)args->dirent;
	resid = args->count;
	retval = 0;

	while (len > 0) {
		bdp = (struct dirent *) inp;
		reclen = bdp->d_reclen;
		linuxreclen = LINUX_RECLEN64(bdp->d_namlen);
		/*
		 * No more space in the user supplied dirent buffer.
		 * Return EINVAL.
		 */
		if (resid < linuxreclen) {
			error = EINVAL;
			goto out;
		}

		linux_dirent64 = (struct l_dirent64*)lbuf;
		linux_dirent64->d_ino = bdp->d_fileno;
		linux_dirent64->d_off = base + reclen;
		linux_dirent64->d_reclen = linuxreclen;
		linux_dirent64->d_type = bdp->d_type;
		strlcpy(linux_dirent64->d_name, bdp->d_name,
		    linuxreclen - offsetof(struct l_dirent64, d_name));
		error = copyout(linux_dirent64, outp, linuxreclen);
		if (error != 0)
			goto out;

		inp += reclen;
		base += reclen;
		len -= reclen;

		retval += linuxreclen;
		outp += linuxreclen;
		resid -= linuxreclen;
	}
	td->td_retval[0] = retval;

out:
	free(lbuf, M_TEMP);
out1:
	free(buf, M_TEMP);
	return (error);
}

#if defined(__i386__) || (defined(__amd64__) && defined(COMPAT_LINUX32))
int
linux_readdir(struct thread *td, struct linux_readdir_args *args)
{
	struct dirent *bdp;
	caddr_t buf;			/* BSD-format */
	int linuxreclen;		/* Linux-format */
	caddr_t lbuf;			/* Linux-format */
	off_t base;
	struct l_dirent *linux_dirent;
	int buflen, error;

#ifdef DEBUG
	if (ldebug(readdir))
		printf(ARGS(readdir, "%d, *"), args->fd);
#endif
	buflen = LINUX_RECLEN(LINUX_NAME_MAX);
	buf = malloc(buflen, M_TEMP, M_WAITOK);

	error = kern_getdirentries(td, args->fd, buf, buflen,
	    &base, NULL, UIO_SYSSPACE);
	if (error != 0) {
		error = linux_getdents_error(td, args->fd, error);
		goto out;
	}
	if (td->td_retval[0] == 0)
		goto out;

	lbuf = malloc(LINUX_RECLEN(LINUX_NAME_MAX), M_TEMP, M_WAITOK | M_ZERO);

	bdp = (struct dirent *) buf;
	linuxreclen = LINUX_RECLEN(bdp->d_namlen);

	linux_dirent = (struct l_dirent*)lbuf;
	linux_dirent->d_ino = bdp->d_fileno;
	linux_dirent->d_off = linuxreclen;
	linux_dirent->d_reclen = bdp->d_namlen;
	strlcpy(linux_dirent->d_name, bdp->d_name,
	    linuxreclen - offsetof(struct l_dirent, d_name));
	error = copyout(linux_dirent, args->dent, linuxreclen);
	if (error == 0)
		td->td_retval[0] = linuxreclen;

	free(lbuf, M_TEMP);
out:
	free(buf, M_TEMP);
	return (error);
}
#endif /* __i386__ || (__amd64__ && COMPAT_LINUX32) */


/*
 * These exist mainly for hooks for doing /compat/linux translation.
 */

#ifdef LINUX_LEGACY_SYSCALLS
int
linux_access(struct thread *td, struct linux_access_args *args)
{
	char *path;
	int error;

	/* Linux convention. */
	if (args->amode & ~(F_OK | X_OK | W_OK | R_OK))
		return (EINVAL);

	LCONVPATHEXIST(td, args->path, &path);

#ifdef DEBUG
	if (ldebug(access))
		printf(ARGS(access, "%s, %d"), path, args->amode);
#endif
	error = kern_accessat(td, AT_FDCWD, path, UIO_SYSSPACE, 0,
	    args->amode);
	LFREEPATH(path);

	return (error);
}
#endif

int
linux_faccessat(struct thread *td, struct linux_faccessat_args *args)
{
	char *path;
	int error, dfd;

	/* Linux convention. */
	if (args->amode & ~(F_OK | X_OK | W_OK | R_OK))
		return (EINVAL);

	dfd = (args->dfd == LINUX_AT_FDCWD) ? AT_FDCWD : args->dfd;
	LCONVPATHEXIST_AT(td, args->filename, &path, dfd);

#ifdef DEBUG
	if (ldebug(faccessat))
		printf(ARGS(access, "%s, %d"), path, args->amode);
#endif

	error = kern_accessat(td, dfd, path, UIO_SYSSPACE, 0, args->amode);
	LFREEPATH(path);

	return (error);
}

#ifdef LINUX_LEGACY_SYSCALLS
int
linux_unlink(struct thread *td, struct linux_unlink_args *args)
{
	char *path;
	int error;
	struct stat st;

	LCONVPATHEXIST(td, args->path, &path);

#ifdef DEBUG
	if (ldebug(unlink))
		printf(ARGS(unlink, "%s"), path);
#endif

	error = kern_unlinkat(td, AT_FDCWD, path, UIO_SYSSPACE, 0, 0);
	if (error == EPERM) {
		/* Introduce POSIX noncompliant behaviour of Linux */
		if (kern_statat(td, 0, AT_FDCWD, path, UIO_SYSSPACE, &st,
		    NULL) == 0) {
			if (S_ISDIR(st.st_mode))
				error = EISDIR;
		}
	}
	LFREEPATH(path);
	return (error);
}
#endif

int
linux_unlinkat(struct thread *td, struct linux_unlinkat_args *args)
{
	char *path;
	int error, dfd;
	struct stat st;

	if (args->flag & ~LINUX_AT_REMOVEDIR)
		return (EINVAL);

	dfd = (args->dfd == LINUX_AT_FDCWD) ? AT_FDCWD : args->dfd;
	LCONVPATHEXIST_AT(td, args->pathname, &path, dfd);

#ifdef DEBUG
	if (ldebug(unlinkat))
		printf(ARGS(unlinkat, "%s"), path);
#endif

	if (args->flag & LINUX_AT_REMOVEDIR)
		error = kern_rmdirat(td, dfd, path, UIO_SYSSPACE, 0);
	else
		error = kern_unlinkat(td, dfd, path, UIO_SYSSPACE, 0, 0);
	if (error == EPERM && !(args->flag & LINUX_AT_REMOVEDIR)) {
		/* Introduce POSIX noncompliant behaviour of Linux */
		if (kern_statat(td, AT_SYMLINK_NOFOLLOW, dfd, path,
		    UIO_SYSSPACE, &st, NULL) == 0 && S_ISDIR(st.st_mode))
			error = EISDIR;
	}
	LFREEPATH(path);
	return (error);
}
int
linux_chdir(struct thread *td, struct linux_chdir_args *args)
{
	char *path;
	int error;

	LCONVPATHEXIST(td, args->path, &path);

#ifdef DEBUG
	if (ldebug(chdir))
		printf(ARGS(chdir, "%s"), path);
#endif
	error = kern_chdir(td, path, UIO_SYSSPACE);
	LFREEPATH(path);
	return (error);
}

#ifdef LINUX_LEGACY_SYSCALLS
int
linux_chmod(struct thread *td, struct linux_chmod_args *args)
{
	char *path;
	int error;

	LCONVPATHEXIST(td, args->path, &path);

#ifdef DEBUG
	if (ldebug(chmod))
		printf(ARGS(chmod, "%s, %d"), path, args->mode);
#endif
	error = kern_fchmodat(td, AT_FDCWD, path, UIO_SYSSPACE,
	    args->mode, 0);
	LFREEPATH(path);
	return (error);
}
#endif

int
linux_fchmodat(struct thread *td, struct linux_fchmodat_args *args)
{
	char *path;
	int error, dfd;

	dfd = (args->dfd == LINUX_AT_FDCWD) ? AT_FDCWD : args->dfd;
	LCONVPATHEXIST_AT(td, args->filename, &path, dfd);

#ifdef DEBUG
	if (ldebug(fchmodat))
		printf(ARGS(fchmodat, "%s, %d"), path, args->mode);
#endif

	error = kern_fchmodat(td, dfd, path, UIO_SYSSPACE, args->mode, 0);
	LFREEPATH(path);
	return (error);
}

#ifdef LINUX_LEGACY_SYSCALLS
int
linux_mkdir(struct thread *td, struct linux_mkdir_args *args)
{
	char *path;
	int error;

	LCONVPATHCREAT(td, args->path, &path);

#ifdef DEBUG
	if (ldebug(mkdir))
		printf(ARGS(mkdir, "%s, %d"), path, args->mode);
#endif
	error = kern_mkdirat(td, AT_FDCWD, path, UIO_SYSSPACE, args->mode);
	LFREEPATH(path);
	return (error);
}
#endif

int
linux_mkdirat(struct thread *td, struct linux_mkdirat_args *args)
{
	char *path;
	int error, dfd;

	dfd = (args->dfd == LINUX_AT_FDCWD) ? AT_FDCWD : args->dfd;
	LCONVPATHCREAT_AT(td, args->pathname, &path, dfd);

#ifdef DEBUG
	if (ldebug(mkdirat))
		printf(ARGS(mkdirat, "%s, %d"), path, args->mode);
#endif
	error = kern_mkdirat(td, dfd, path, UIO_SYSSPACE, args->mode);
	LFREEPATH(path);
	return (error);
}

#ifdef LINUX_LEGACY_SYSCALLS
int
linux_rmdir(struct thread *td, struct linux_rmdir_args *args)
{
	char *path;
	int error;

	LCONVPATHEXIST(td, args->path, &path);

#ifdef DEBUG
	if (ldebug(rmdir))
		printf(ARGS(rmdir, "%s"), path);
#endif
	error = kern_rmdirat(td, AT_FDCWD, path, UIO_SYSSPACE, 0);
	LFREEPATH(path);
	return (error);
}

int
linux_rename(struct thread *td, struct linux_rename_args *args)
{
	char *from, *to;
	int error;

	LCONVPATHEXIST(td, args->from, &from);
	/* Expand LCONVPATHCREATE so that `from' can be freed on errors */
	error = linux_emul_convpath(td, args->to, UIO_USERSPACE, &to, 1, AT_FDCWD);
	if (to == NULL) {
		LFREEPATH(from);
		return (error);
	}

#ifdef DEBUG
	if (ldebug(rename))
		printf(ARGS(rename, "%s, %s"), from, to);
#endif
	error = kern_renameat(td, AT_FDCWD, from, AT_FDCWD, to, UIO_SYSSPACE);
	LFREEPATH(from);
	LFREEPATH(to);
	return (error);
}
#endif

int
linux_renameat(struct thread *td, struct linux_renameat_args *args)
{
	char *from, *to;
	int error, olddfd, newdfd;

	olddfd = (args->olddfd == LINUX_AT_FDCWD) ? AT_FDCWD : args->olddfd;
	newdfd = (args->newdfd == LINUX_AT_FDCWD) ? AT_FDCWD : args->newdfd;
	LCONVPATHEXIST_AT(td, args->oldname, &from, olddfd);
	/* Expand LCONVPATHCREATE so that `from' can be freed on errors */
	error = linux_emul_convpath(td, args->newname, UIO_USERSPACE, &to, 1, newdfd);
	if (to == NULL) {
		LFREEPATH(from);
		return (error);
	}

#ifdef DEBUG
	if (ldebug(renameat))
		printf(ARGS(renameat, "%s, %s"), from, to);
#endif
	error = kern_renameat(td, olddfd, from, newdfd, to, UIO_SYSSPACE);
	LFREEPATH(from);
	LFREEPATH(to);
	return (error);
}

#ifdef LINUX_LEGACY_SYSCALLS
int
linux_symlink(struct thread *td, struct linux_symlink_args *args)
{
	char *path, *to;
	int error;

	LCONVPATHEXIST(td, args->path, &path);
	/* Expand LCONVPATHCREATE so that `path' can be freed on errors */
	error = linux_emul_convpath(td, args->to, UIO_USERSPACE, &to, 1, AT_FDCWD);
	if (to == NULL) {
		LFREEPATH(path);
		return (error);
	}

#ifdef DEBUG
	if (ldebug(symlink))
		printf(ARGS(symlink, "%s, %s"), path, to);
#endif
	error = kern_symlinkat(td, path, AT_FDCWD, to, UIO_SYSSPACE);
	LFREEPATH(path);
	LFREEPATH(to);
	return (error);
}
#endif

int
linux_symlinkat(struct thread *td, struct linux_symlinkat_args *args)
{
	char *path, *to;
	int error, dfd;

	dfd = (args->newdfd == LINUX_AT_FDCWD) ? AT_FDCWD : args->newdfd;
	LCONVPATHEXIST(td, args->oldname, &path);
	/* Expand LCONVPATHCREATE so that `path' can be freed on errors */
	error = linux_emul_convpath(td, args->newname, UIO_USERSPACE, &to, 1, dfd);
	if (to == NULL) {
		LFREEPATH(path);
		return (error);
	}

#ifdef DEBUG
	if (ldebug(symlinkat))
		printf(ARGS(symlinkat, "%s, %s"), path, to);
#endif

	error = kern_symlinkat(td, path, dfd, to, UIO_SYSSPACE);
	LFREEPATH(path);
	LFREEPATH(to);
	return (error);
}

#ifdef LINUX_LEGACY_SYSCALLS
int
linux_readlink(struct thread *td, struct linux_readlink_args *args)
{
	char *name;
	int error;

	LCONVPATHEXIST(td, args->name, &name);

#ifdef DEBUG
	if (ldebug(readlink))
		printf(ARGS(readlink, "%s, %p, %d"), name, (void *)args->buf,
		    args->count);
#endif
	error = kern_readlinkat(td, AT_FDCWD, name, UIO_SYSSPACE,
	    args->buf, UIO_USERSPACE, args->count);
	LFREEPATH(name);
	return (error);
}
#endif

int
linux_readlinkat(struct thread *td, struct linux_readlinkat_args *args)
{
	char *name;
	int error, dfd;

	dfd = (args->dfd == LINUX_AT_FDCWD) ? AT_FDCWD : args->dfd;
	LCONVPATHEXIST_AT(td, args->path, &name, dfd);

#ifdef DEBUG
	if (ldebug(readlinkat))
		printf(ARGS(readlinkat, "%s, %p, %d"), name, (void *)args->buf,
		    args->bufsiz);
#endif

	error = kern_readlinkat(td, dfd, name, UIO_SYSSPACE, args->buf,
	    UIO_USERSPACE, args->bufsiz);
	LFREEPATH(name);
	return (error);
}

int
linux_truncate(struct thread *td, struct linux_truncate_args *args)
{
	char *path;
	int error;

	LCONVPATHEXIST(td, args->path, &path);

#ifdef DEBUG
	if (ldebug(truncate))
		printf(ARGS(truncate, "%s, %ld"), path, (long)args->length);
#endif

	error = kern_truncate(td, path, UIO_SYSSPACE, args->length);
	LFREEPATH(path);
	return (error);
}

#if defined(__i386__) || (defined(__amd64__) && defined(COMPAT_LINUX32))
int
linux_truncate64(struct thread *td, struct linux_truncate64_args *args)
{
	char *path;
	int error;

	LCONVPATHEXIST(td, args->path, &path);

#ifdef DEBUG
	if (ldebug(truncate64))
		printf(ARGS(truncate64, "%s, %jd"), path, args->length);
#endif

	error = kern_truncate(td, path, UIO_SYSSPACE, args->length);
	LFREEPATH(path);
	return (error);
}
#endif /* __i386__ || (__amd64__ && COMPAT_LINUX32) */

int
linux_ftruncate(struct thread *td, struct linux_ftruncate_args *args)
{

	return (kern_ftruncate(td, args->fd, args->length));
}

#ifdef LINUX_LEGACY_SYSCALLS
int
linux_link(struct thread *td, struct linux_link_args *args)
{
	char *path, *to;
	int error;

	LCONVPATHEXIST(td, args->path, &path);
	/* Expand LCONVPATHCREATE so that `path' can be freed on errors */
	error = linux_emul_convpath(td, args->to, UIO_USERSPACE, &to, 1, AT_FDCWD);
	if (to == NULL) {
		LFREEPATH(path);
		return (error);
	}

#ifdef DEBUG
	if (ldebug(link))
		printf(ARGS(link, "%s, %s"), path, to);
#endif
	error = kern_linkat(td, AT_FDCWD, AT_FDCWD, path, to, UIO_SYSSPACE,
	    FOLLOW);
	LFREEPATH(path);
	LFREEPATH(to);
	return (error);
}
#endif

int
linux_linkat(struct thread *td, struct linux_linkat_args *args)
{
	char *path, *to;
	int error, olddfd, newdfd, follow;

	if (args->flag & ~LINUX_AT_SYMLINK_FOLLOW)
		return (EINVAL);

	olddfd = (args->olddfd == LINUX_AT_FDCWD) ? AT_FDCWD : args->olddfd;
	newdfd = (args->newdfd == LINUX_AT_FDCWD) ? AT_FDCWD : args->newdfd;
	LCONVPATHEXIST_AT(td, args->oldname, &path, olddfd);
	/* Expand LCONVPATHCREATE so that `path' can be freed on errors */
	error = linux_emul_convpath(td, args->newname, UIO_USERSPACE, &to, 1, newdfd);
	if (to == NULL) {
		LFREEPATH(path);
		return (error);
	}

#ifdef DEBUG
	if (ldebug(linkat))
		printf(ARGS(linkat, "%i, %s, %i, %s, %i"), args->olddfd, path,
			args->newdfd, to, args->flag);
#endif

	follow = (args->flag & LINUX_AT_SYMLINK_FOLLOW) == 0 ? NOFOLLOW :
	    FOLLOW;
	error = kern_linkat(td, olddfd, newdfd, path, to, UIO_SYSSPACE, follow);
	LFREEPATH(path);
	LFREEPATH(to);
	return (error);
}

int
linux_fdatasync(td, uap)
	struct thread *td;
	struct linux_fdatasync_args *uap;
{

	return (kern_fsync(td, uap->fd, false));
}

int
linux_pread(struct thread *td, struct linux_pread_args *uap)
{
	struct vnode *vp;
	int error;

	error = kern_pread(td, uap->fd, uap->buf, uap->nbyte, uap->offset);
	if (error == 0) {
		/* This seems to violate POSIX but Linux does it. */
		error = fgetvp(td, uap->fd, &cap_pread_rights, &vp);
		if (error != 0)
			return (error);
		if (vp->v_type == VDIR) {
			vrele(vp);
			return (EISDIR);
		}
		vrele(vp);
	}
	return (error);
}

int
linux_pwrite(struct thread *td, struct linux_pwrite_args *uap)
{

	return (kern_pwrite(td, uap->fd, uap->buf, uap->nbyte, uap->offset));
}

int
linux_preadv(struct thread *td, struct linux_preadv_args *uap)
{
	struct uio *auio;
	int error;
	off_t offset;

	/*
	 * According http://man7.org/linux/man-pages/man2/preadv.2.html#NOTES
	 * pos_l and pos_h, respectively, contain the
	 * low order and high order 32 bits of offset.
	 */
	offset = (((off_t)uap->pos_h << (sizeof(offset) * 4)) <<
	    (sizeof(offset) * 4)) | uap->pos_l;
	if (offset < 0)
		return (EINVAL);
#ifdef COMPAT_LINUX32
	error = linux32_copyinuio(PTRIN(uap->vec), uap->vlen, &auio);
#else
	error = copyinuio(uap->vec, uap->vlen, &auio);
#endif
	if (error != 0)
		return (error);
	error = kern_preadv(td, uap->fd, auio, offset);
	free(auio, M_IOV);
	return (error);
}

int
linux_pwritev(struct thread *td, struct linux_pwritev_args *uap)
{
	struct uio *auio;
	int error;
	off_t offset;

	/*
	 * According http://man7.org/linux/man-pages/man2/pwritev.2.html#NOTES
	 * pos_l and pos_h, respectively, contain the
	 * low order and high order 32 bits of offset.
	 */
	offset = (((off_t)uap->pos_h << (sizeof(offset) * 4)) <<
	    (sizeof(offset) * 4)) | uap->pos_l;
	if (offset < 0)
		return (EINVAL);
#ifdef COMPAT_LINUX32
	error = linux32_copyinuio(PTRIN(uap->vec), uap->vlen, &auio);
#else
	error = copyinuio(uap->vec, uap->vlen, &auio);
#endif
	if (error != 0)
		return (error);
	error = kern_pwritev(td, uap->fd, auio, offset);
	free(auio, M_IOV);
	return (error);
}

int
linux_mount(struct thread *td, struct linux_mount_args *args)
{
	char fstypename[MFSNAMELEN];
	char *mntonname, *mntfromname;
	int error, fsflags;

	mntonname = malloc(MNAMELEN, M_TEMP, M_WAITOK);
	mntfromname = malloc(MNAMELEN, M_TEMP, M_WAITOK);
	error = copyinstr(args->filesystemtype, fstypename, MFSNAMELEN - 1,
	    NULL);
	if (error != 0)
		goto out;
	error = copyinstr(args->specialfile, mntfromname, MNAMELEN - 1, NULL);
	if (error != 0)
		goto out;
	error = copyinstr(args->dir, mntonname, MNAMELEN - 1, NULL);
	if (error != 0)
		goto out;

#ifdef DEBUG
	if (ldebug(mount))
		printf(ARGS(mount, "%s, %s, %s"),
		    fstypename, mntfromname, mntonname);
#endif

	if (strcmp(fstypename, "ext2") == 0) {
		strcpy(fstypename, "ext2fs");
	} else if (strcmp(fstypename, "proc") == 0) {
		strcpy(fstypename, "linprocfs");
	} else if (strcmp(fstypename, "vfat") == 0) {
		strcpy(fstypename, "msdosfs");
	}

	fsflags = 0;

	if ((args->rwflag & 0xffff0000) == 0xc0ed0000) {
		/*
		 * Linux SYNC flag is not included; the closest equivalent
		 * FreeBSD has is !ASYNC, which is our default.
		 */
		if (args->rwflag & LINUX_MS_RDONLY)
			fsflags |= MNT_RDONLY;
		if (args->rwflag & LINUX_MS_NOSUID)
			fsflags |= MNT_NOSUID;
		if (args->rwflag & LINUX_MS_NOEXEC)
			fsflags |= MNT_NOEXEC;
		if (args->rwflag & LINUX_MS_REMOUNT)
			fsflags |= MNT_UPDATE;
	}

	error = kernel_vmount(fsflags,
	    "fstype", fstypename,
	    "fspath", mntonname,
	    "from", mntfromname,
	    NULL);
out:
	free(mntonname, M_TEMP);
	free(mntfromname, M_TEMP);
	return (error);
}

#if defined(__i386__) || (defined(__amd64__) && defined(COMPAT_LINUX32))
int
linux_oldumount(struct thread *td, struct linux_oldumount_args *args)
{
	struct linux_umount_args args2;

	args2.path = args->path;
	args2.flags = 0;
	return (linux_umount(td, &args2));
}
#endif /* __i386__ || (__amd64__ && COMPAT_LINUX32) */

#ifdef LINUX_LEGACY_SYSCALLS
int
linux_umount(struct thread *td, struct linux_umount_args *args)
{
	struct unmount_args bsd;

	bsd.path = args->path;
	bsd.flags = args->flags;	/* XXX correct? */
	return (sys_unmount(td, &bsd));
}
#endif

/*
 * fcntl family of syscalls
 */

struct l_flock {
	l_short		l_type;
	l_short		l_whence;
	l_off_t		l_start;
	l_off_t		l_len;
	l_pid_t		l_pid;
}
#if defined(__amd64__) && defined(COMPAT_LINUX32)
__packed
#endif
;

static void
linux_to_bsd_flock(struct l_flock *linux_flock, struct flock *bsd_flock)
{
	switch (linux_flock->l_type) {
	case LINUX_F_RDLCK:
		bsd_flock->l_type = F_RDLCK;
		break;
	case LINUX_F_WRLCK:
		bsd_flock->l_type = F_WRLCK;
		break;
	case LINUX_F_UNLCK:
		bsd_flock->l_type = F_UNLCK;
		break;
	default:
		bsd_flock->l_type = -1;
		break;
	}
	bsd_flock->l_whence = linux_flock->l_whence;
	bsd_flock->l_start = (off_t)linux_flock->l_start;
	bsd_flock->l_len = (off_t)linux_flock->l_len;
	bsd_flock->l_pid = (pid_t)linux_flock->l_pid;
	bsd_flock->l_sysid = 0;
}

static void
bsd_to_linux_flock(struct flock *bsd_flock, struct l_flock *linux_flock)
{
	switch (bsd_flock->l_type) {
	case F_RDLCK:
		linux_flock->l_type = LINUX_F_RDLCK;
		break;
	case F_WRLCK:
		linux_flock->l_type = LINUX_F_WRLCK;
		break;
	case F_UNLCK:
		linux_flock->l_type = LINUX_F_UNLCK;
		break;
	}
	linux_flock->l_whence = bsd_flock->l_whence;
	linux_flock->l_start = (l_off_t)bsd_flock->l_start;
	linux_flock->l_len = (l_off_t)bsd_flock->l_len;
	linux_flock->l_pid = (l_pid_t)bsd_flock->l_pid;
}

#if defined(__i386__) || (defined(__amd64__) && defined(COMPAT_LINUX32))
struct l_flock64 {
	l_short		l_type;
	l_short		l_whence;
	l_loff_t	l_start;
	l_loff_t	l_len;
	l_pid_t		l_pid;
}
#if defined(__amd64__) && defined(COMPAT_LINUX32)
__packed
#endif
;

static void
linux_to_bsd_flock64(struct l_flock64 *linux_flock, struct flock *bsd_flock)
{
	switch (linux_flock->l_type) {
	case LINUX_F_RDLCK:
		bsd_flock->l_type = F_RDLCK;
		break;
	case LINUX_F_WRLCK:
		bsd_flock->l_type = F_WRLCK;
		break;
	case LINUX_F_UNLCK:
		bsd_flock->l_type = F_UNLCK;
		break;
	default:
		bsd_flock->l_type = -1;
		break;
	}
	bsd_flock->l_whence = linux_flock->l_whence;
	bsd_flock->l_start = (off_t)linux_flock->l_start;
	bsd_flock->l_len = (off_t)linux_flock->l_len;
	bsd_flock->l_pid = (pid_t)linux_flock->l_pid;
	bsd_flock->l_sysid = 0;
}

static void
bsd_to_linux_flock64(struct flock *bsd_flock, struct l_flock64 *linux_flock)
{
	switch (bsd_flock->l_type) {
	case F_RDLCK:
		linux_flock->l_type = LINUX_F_RDLCK;
		break;
	case F_WRLCK:
		linux_flock->l_type = LINUX_F_WRLCK;
		break;
	case F_UNLCK:
		linux_flock->l_type = LINUX_F_UNLCK;
		break;
	}
	linux_flock->l_whence = bsd_flock->l_whence;
	linux_flock->l_start = (l_loff_t)bsd_flock->l_start;
	linux_flock->l_len = (l_loff_t)bsd_flock->l_len;
	linux_flock->l_pid = (l_pid_t)bsd_flock->l_pid;
}
#endif /* __i386__ || (__amd64__ && COMPAT_LINUX32) */

static int
fcntl_common(struct thread *td, struct linux_fcntl_args *args)
{
	struct l_flock linux_flock;
	struct flock bsd_flock;
	struct file *fp;
	long arg;
	int error, result;

	switch (args->cmd) {
	case LINUX_F_DUPFD:
		return (kern_fcntl(td, args->fd, F_DUPFD, args->arg));

	case LINUX_F_GETFD:
		return (kern_fcntl(td, args->fd, F_GETFD, 0));

	case LINUX_F_SETFD:
		return (kern_fcntl(td, args->fd, F_SETFD, args->arg));

	case LINUX_F_GETFL:
		error = kern_fcntl(td, args->fd, F_GETFL, 0);
		result = td->td_retval[0];
		td->td_retval[0] = 0;
		if (result & O_RDONLY)
			td->td_retval[0] |= LINUX_O_RDONLY;
		if (result & O_WRONLY)
			td->td_retval[0] |= LINUX_O_WRONLY;
		if (result & O_RDWR)
			td->td_retval[0] |= LINUX_O_RDWR;
		if (result & O_NDELAY)
			td->td_retval[0] |= LINUX_O_NONBLOCK;
		if (result & O_APPEND)
			td->td_retval[0] |= LINUX_O_APPEND;
		if (result & O_FSYNC)
			td->td_retval[0] |= LINUX_O_SYNC;
		if (result & O_ASYNC)
			td->td_retval[0] |= LINUX_FASYNC;
#ifdef LINUX_O_NOFOLLOW
		if (result & O_NOFOLLOW)
			td->td_retval[0] |= LINUX_O_NOFOLLOW;
#endif
#ifdef LINUX_O_DIRECT
		if (result & O_DIRECT)
			td->td_retval[0] |= LINUX_O_DIRECT;
#endif
		return (error);

	case LINUX_F_SETFL:
		arg = 0;
		if (args->arg & LINUX_O_NDELAY)
			arg |= O_NONBLOCK;
		if (args->arg & LINUX_O_APPEND)
			arg |= O_APPEND;
		if (args->arg & LINUX_O_SYNC)
			arg |= O_FSYNC;
		if (args->arg & LINUX_FASYNC)
			arg |= O_ASYNC;
#ifdef LINUX_O_NOFOLLOW
		if (args->arg & LINUX_O_NOFOLLOW)
			arg |= O_NOFOLLOW;
#endif
#ifdef LINUX_O_DIRECT
		if (args->arg & LINUX_O_DIRECT)
			arg |= O_DIRECT;
#endif
		return (kern_fcntl(td, args->fd, F_SETFL, arg));

	case LINUX_F_GETLK:
		error = copyin((void *)args->arg, &linux_flock,
		    sizeof(linux_flock));
		if (error)
			return (error);
		linux_to_bsd_flock(&linux_flock, &bsd_flock);
		error = kern_fcntl(td, args->fd, F_GETLK, (intptr_t)&bsd_flock);
		if (error)
			return (error);
		bsd_to_linux_flock(&bsd_flock, &linux_flock);
		return (copyout(&linux_flock, (void *)args->arg,
		    sizeof(linux_flock)));

	case LINUX_F_SETLK:
		error = copyin((void *)args->arg, &linux_flock,
		    sizeof(linux_flock));
		if (error)
			return (error);
		linux_to_bsd_flock(&linux_flock, &bsd_flock);
		return (kern_fcntl(td, args->fd, F_SETLK,
		    (intptr_t)&bsd_flock));

	case LINUX_F_SETLKW:
		error = copyin((void *)args->arg, &linux_flock,
		    sizeof(linux_flock));
		if (error)
			return (error);
		linux_to_bsd_flock(&linux_flock, &bsd_flock);
		return (kern_fcntl(td, args->fd, F_SETLKW,
		     (intptr_t)&bsd_flock));

	case LINUX_F_GETOWN:
		return (kern_fcntl(td, args->fd, F_GETOWN, 0));

	case LINUX_F_SETOWN:
		/*
		 * XXX some Linux applications depend on F_SETOWN having no
		 * significant effect for pipes (SIGIO is not delivered for
		 * pipes under Linux-2.2.35 at least).
		 */
		error = fget(td, args->fd,
		    &cap_fcntl_rights, &fp);
		if (error)
			return (error);
		if (fp->f_type == DTYPE_PIPE) {
			fdrop(fp, td);
			return (EINVAL);
		}
		fdrop(fp, td);

		return (kern_fcntl(td, args->fd, F_SETOWN, args->arg));

	case LINUX_F_DUPFD_CLOEXEC:
		return (kern_fcntl(td, args->fd, F_DUPFD_CLOEXEC, args->arg));
	}

	return (EINVAL);
}

int
linux_fcntl(struct thread *td, struct linux_fcntl_args *args)
{

#ifdef DEBUG
	if (ldebug(fcntl))
		printf(ARGS(fcntl, "%d, %08x, *"), args->fd, args->cmd);
#endif

	return (fcntl_common(td, args));
}

#if defined(__i386__) || (defined(__amd64__) && defined(COMPAT_LINUX32))
int
linux_fcntl64(struct thread *td, struct linux_fcntl64_args *args)
{
	struct l_flock64 linux_flock;
	struct flock bsd_flock;
	struct linux_fcntl_args fcntl_args;
	int error;

#ifdef DEBUG
	if (ldebug(fcntl64))
		printf(ARGS(fcntl64, "%d, %08x, *"), args->fd, args->cmd);
#endif

	switch (args->cmd) {
	case LINUX_F_GETLK64:
		error = copyin((void *)args->arg, &linux_flock,
		    sizeof(linux_flock));
		if (error)
			return (error);
		linux_to_bsd_flock64(&linux_flock, &bsd_flock);
		error = kern_fcntl(td, args->fd, F_GETLK, (intptr_t)&bsd_flock);
		if (error)
			return (error);
		bsd_to_linux_flock64(&bsd_flock, &linux_flock);
		return (copyout(&linux_flock, (void *)args->arg,
			    sizeof(linux_flock)));

	case LINUX_F_SETLK64:
		error = copyin((void *)args->arg, &linux_flock,
		    sizeof(linux_flock));
		if (error)
			return (error);
		linux_to_bsd_flock64(&linux_flock, &bsd_flock);
		return (kern_fcntl(td, args->fd, F_SETLK,
		    (intptr_t)&bsd_flock));

	case LINUX_F_SETLKW64:
		error = copyin((void *)args->arg, &linux_flock,
		    sizeof(linux_flock));
		if (error)
			return (error);
		linux_to_bsd_flock64(&linux_flock, &bsd_flock);
		return (kern_fcntl(td, args->fd, F_SETLKW,
		    (intptr_t)&bsd_flock));
	}

	fcntl_args.fd = args->fd;
	fcntl_args.cmd = args->cmd;
	fcntl_args.arg = args->arg;
	return (fcntl_common(td, &fcntl_args));
}
#endif /* __i386__ || (__amd64__ && COMPAT_LINUX32) */

#ifdef LINUX_LEGACY_SYSCALLS
int
linux_chown(struct thread *td, struct linux_chown_args *args)
{
	char *path;
	int error;

	LCONVPATHEXIST(td, args->path, &path);

#ifdef DEBUG
	if (ldebug(chown))
		printf(ARGS(chown, "%s, %d, %d"), path, args->uid, args->gid);
#endif
	error = kern_fchownat(td, AT_FDCWD, path, UIO_SYSSPACE, args->uid,
	    args->gid, 0);
	LFREEPATH(path);
	return (error);
}
#endif

int
linux_fchownat(struct thread *td, struct linux_fchownat_args *args)
{
	char *path;
	int error, dfd, flag;

	if (args->flag & ~LINUX_AT_SYMLINK_NOFOLLOW)
		return (EINVAL);

	dfd = (args->dfd == LINUX_AT_FDCWD) ? AT_FDCWD :  args->dfd;
	LCONVPATHEXIST_AT(td, args->filename, &path, dfd);

#ifdef DEBUG
	if (ldebug(fchownat))
		printf(ARGS(fchownat, "%s, %d, %d"), path, args->uid, args->gid);
#endif

	flag = (args->flag & LINUX_AT_SYMLINK_NOFOLLOW) == 0 ? 0 :
	    AT_SYMLINK_NOFOLLOW;
	error = kern_fchownat(td, dfd, path, UIO_SYSSPACE, args->uid, args->gid,
	    flag);
	LFREEPATH(path);
	return (error);
}

#ifdef LINUX_LEGACY_SYSCALLS
int
linux_lchown(struct thread *td, struct linux_lchown_args *args)
{
	char *path;
	int error;

	LCONVPATHEXIST(td, args->path, &path);

#ifdef DEBUG
	if (ldebug(lchown))
		printf(ARGS(lchown, "%s, %d, %d"), path, args->uid, args->gid);
#endif
	error = kern_fchownat(td, AT_FDCWD, path, UIO_SYSSPACE, args->uid,
	    args->gid, AT_SYMLINK_NOFOLLOW);
	LFREEPATH(path);
	return (error);
}
#endif

static int
convert_fadvice(int advice)
{
	switch (advice) {
	case LINUX_POSIX_FADV_NORMAL:
		return (POSIX_FADV_NORMAL);
	case LINUX_POSIX_FADV_RANDOM:
		return (POSIX_FADV_RANDOM);
	case LINUX_POSIX_FADV_SEQUENTIAL:
		return (POSIX_FADV_SEQUENTIAL);
	case LINUX_POSIX_FADV_WILLNEED:
		return (POSIX_FADV_WILLNEED);
	case LINUX_POSIX_FADV_DONTNEED:
		return (POSIX_FADV_DONTNEED);
	case LINUX_POSIX_FADV_NOREUSE:
		return (POSIX_FADV_NOREUSE);
	default:
		return (-1);
	}
}

int
linux_fadvise64(struct thread *td, struct linux_fadvise64_args *args)
{
	int advice;

	advice = convert_fadvice(args->advice);
	if (advice == -1)
		return (EINVAL);
	return (kern_posix_fadvise(td, args->fd, args->offset, args->len,
	    advice));
}

#if defined(__i386__) || (defined(__amd64__) && defined(COMPAT_LINUX32))
int
linux_fadvise64_64(struct thread *td, struct linux_fadvise64_64_args *args)
{
	int advice;

	advice = convert_fadvice(args->advice);
	if (advice == -1)
		return (EINVAL);
	return (kern_posix_fadvise(td, args->fd, args->offset, args->len,
	    advice));
}
#endif /* __i386__ || (__amd64__ && COMPAT_LINUX32) */

#ifdef LINUX_LEGACY_SYSCALLS
int
linux_pipe(struct thread *td, struct linux_pipe_args *args)
{
	int fildes[2];
	int error;

#ifdef DEBUG
	if (ldebug(pipe))
		printf(ARGS(pipe, "*"));
#endif

	error = kern_pipe(td, fildes, 0, NULL, NULL);
	if (error != 0)
		return (error);

	error = copyout(fildes, args->pipefds, sizeof(fildes));
	if (error != 0) {
		(void)kern_close(td, fildes[0]);
		(void)kern_close(td, fildes[1]);
	}

	return (error);
}
#endif

int
linux_pipe2(struct thread *td, struct linux_pipe2_args *args)
{
	int fildes[2];
	int error, flags;

#ifdef DEBUG
	if (ldebug(pipe2))
		printf(ARGS(pipe2, "*, %d"), args->flags);
#endif

	if ((args->flags & ~(LINUX_O_NONBLOCK | LINUX_O_CLOEXEC)) != 0)
		return (EINVAL);

	flags = 0;
	if ((args->flags & LINUX_O_NONBLOCK) != 0)
		flags |= O_NONBLOCK;
	if ((args->flags & LINUX_O_CLOEXEC) != 0)
		flags |= O_CLOEXEC;
	error = kern_pipe(td, fildes, flags, NULL, NULL);
	if (error != 0)
		return (error);

	error = copyout(fildes, args->pipefds, sizeof(fildes));
	if (error != 0) {
		(void)kern_close(td, fildes[0]);
		(void)kern_close(td, fildes[1]);
	}

	return (error);
}

int
linux_dup3(struct thread *td, struct linux_dup3_args *args)
{
	int cmd;
	intptr_t newfd;

	if (args->oldfd == args->newfd)
		return (EINVAL);
	if ((args->flags & ~LINUX_O_CLOEXEC) != 0)
		return (EINVAL);
	if (args->flags & LINUX_O_CLOEXEC)
		cmd = F_DUP2FD_CLOEXEC;
	else
		cmd = F_DUP2FD;

	newfd = args->newfd;
	return (kern_fcntl(td, args->oldfd, cmd, newfd));
}

int
linux_fallocate(struct thread *td, struct linux_fallocate_args *args)
{

	/*
	 * We emulate only posix_fallocate system call for which
	 * mode should be 0.
	 */
	if (args->mode != 0)
		return (ENOSYS);

	return (kern_posix_fallocate(td, args->fd, args->offset,
	    args->len));
}
