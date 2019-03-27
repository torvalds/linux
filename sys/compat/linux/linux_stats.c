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
#include <sys/capsicum.h>
#include <sys/dirent.h>
#include <sys/file.h>
#include <sys/filedesc.h>
#include <sys/proc.h>
#include <sys/malloc.h>
#include <sys/mount.h>
#include <sys/namei.h>
#include <sys/stat.h>
#include <sys/syscallsubr.h>
#include <sys/systm.h>
#include <sys/tty.h>
#include <sys/vnode.h>
#include <sys/conf.h>
#include <sys/fcntl.h>

#ifdef COMPAT_LINUX32
#include <machine/../linux32/linux.h>
#include <machine/../linux32/linux32_proto.h>
#else
#include <machine/../linux/linux.h>
#include <machine/../linux/linux_proto.h>
#endif

#include <compat/linux/linux_util.h>
#include <compat/linux/linux_file.h>


static void
translate_vnhook_major_minor(struct vnode *vp, struct stat *sb)
{
	int major, minor;

	if (vp->v_type == VCHR && vp->v_rdev != NULL &&
	    linux_driver_get_major_minor(devtoname(vp->v_rdev),
	    &major, &minor) == 0) {
		sb->st_rdev = (major << 8 | minor);
	}
}

static int
linux_kern_statat(struct thread *td, int flag, int fd, char *path,
    enum uio_seg pathseg, struct stat *sbp)
{

	return (kern_statat(td, flag, fd, path, pathseg, sbp,
	    translate_vnhook_major_minor));
}

#ifdef LINUX_LEGACY_SYSCALLS
static int
linux_kern_stat(struct thread *td, char *path, enum uio_seg pathseg,
    struct stat *sbp)
{

	return (linux_kern_statat(td, 0, AT_FDCWD, path, pathseg, sbp));
}

static int
linux_kern_lstat(struct thread *td, char *path, enum uio_seg pathseg,
    struct stat *sbp)
{

	return (linux_kern_statat(td, AT_SYMLINK_NOFOLLOW, AT_FDCWD, path,
	    pathseg, sbp));
}
#endif

static void
translate_fd_major_minor(struct thread *td, int fd, struct stat *buf)
{
	struct file *fp;
	struct vnode *vp;
	int major, minor;

	/*
	 * No capability rights required here.
	 */
	if ((!S_ISCHR(buf->st_mode) && !S_ISBLK(buf->st_mode)) ||
	    fget(td, fd, &cap_no_rights, &fp) != 0)
		return;
	vp = fp->f_vnode;
	if (vp != NULL && vp->v_rdev != NULL &&
	    linux_driver_get_major_minor(devtoname(vp->v_rdev),
					 &major, &minor) == 0) {
		buf->st_rdev = (major << 8 | minor);
	} else if (fp->f_type == DTYPE_PTS) {
		struct tty *tp = fp->f_data;

		/* Convert the numbers for the slave device. */
		if (linux_driver_get_major_minor(devtoname(tp->t_dev),
					 &major, &minor) == 0) {
			buf->st_rdev = (major << 8 | minor);
		}
	}
	fdrop(fp, td);
}

/*
 * l_dev_t has the same encoding as dev_t in the latter's low 16 bits, so
 * truncation of a dev_t to 16 bits gives the same result as unpacking
 * using major() and minor() and repacking in the l_dev_t format.  This
 * detail is hidden in dev_to_ldev().  Overflow in conversions of dev_t's
 * are not checked for, as for other fields.
 *
 * dev_to_ldev() is only used for translating st_dev.  When we convert
 * st_rdev for copying it out, it isn't really a dev_t, but has already
 * been translated to an l_dev_t in a nontrivial way.  Translating it
 * again would be illogical but would have no effect since the low 16
 * bits have the same encoding.
 *
 * The nontrivial translation for st_rdev renumbers some devices, but not
 * ones that can be mounted on, so it is consistent with the translation
 * for st_dev except when the renumbering or truncation causes conflicts.
 */
#define	dev_to_ldev(d)	((uint16_t)(d))

static int
newstat_copyout(struct stat *buf, void *ubuf)
{
	struct l_newstat tbuf;

	bzero(&tbuf, sizeof(tbuf));
	tbuf.st_dev = dev_to_ldev(buf->st_dev);
	tbuf.st_ino = buf->st_ino;
	tbuf.st_mode = buf->st_mode;
	tbuf.st_nlink = buf->st_nlink;
	tbuf.st_uid = buf->st_uid;
	tbuf.st_gid = buf->st_gid;
	tbuf.st_rdev = buf->st_rdev;
	tbuf.st_size = buf->st_size;
	tbuf.st_atim.tv_sec = buf->st_atim.tv_sec;
	tbuf.st_atim.tv_nsec = buf->st_atim.tv_nsec;
	tbuf.st_mtim.tv_sec = buf->st_mtim.tv_sec;
	tbuf.st_mtim.tv_nsec = buf->st_mtim.tv_nsec;
	tbuf.st_ctim.tv_sec = buf->st_ctim.tv_sec;
	tbuf.st_ctim.tv_nsec = buf->st_ctim.tv_nsec;
	tbuf.st_blksize = buf->st_blksize;
	tbuf.st_blocks = buf->st_blocks;

	return (copyout(&tbuf, ubuf, sizeof(tbuf)));
}

#ifdef LINUX_LEGACY_SYSCALLS
int
linux_newstat(struct thread *td, struct linux_newstat_args *args)
{
	struct stat buf;
	char *path;
	int error;

	LCONVPATHEXIST(td, args->path, &path);

#ifdef DEBUG
	if (ldebug(newstat))
		printf(ARGS(newstat, "%s, *"), path);
#endif

	error = linux_kern_stat(td, path, UIO_SYSSPACE, &buf);
	LFREEPATH(path);
	if (error)
		return (error);
	return (newstat_copyout(&buf, args->buf));
}

int
linux_newlstat(struct thread *td, struct linux_newlstat_args *args)
{
	struct stat sb;
	char *path;
	int error;

	LCONVPATHEXIST(td, args->path, &path);

#ifdef DEBUG
	if (ldebug(newlstat))
		printf(ARGS(newlstat, "%s, *"), path);
#endif

	error = linux_kern_lstat(td, path, UIO_SYSSPACE, &sb);
	LFREEPATH(path);
	if (error)
		return (error);
	return (newstat_copyout(&sb, args->buf));
}
#endif

int
linux_newfstat(struct thread *td, struct linux_newfstat_args *args)
{
	struct stat buf;
	int error;

#ifdef DEBUG
	if (ldebug(newfstat))
		printf(ARGS(newfstat, "%d, *"), args->fd);
#endif

	error = kern_fstat(td, args->fd, &buf);
	translate_fd_major_minor(td, args->fd, &buf);
	if (!error)
		error = newstat_copyout(&buf, args->buf);

	return (error);
}

#if defined(__i386__) || (defined(__amd64__) && defined(COMPAT_LINUX32))
static int
stat_copyout(struct stat *buf, void *ubuf)
{
	struct l_stat lbuf;

	bzero(&lbuf, sizeof(lbuf));
	lbuf.st_dev = dev_to_ldev(buf->st_dev);
	lbuf.st_ino = buf->st_ino;
	lbuf.st_mode = buf->st_mode;
	lbuf.st_nlink = buf->st_nlink;
	lbuf.st_uid = buf->st_uid;
	lbuf.st_gid = buf->st_gid;
	lbuf.st_rdev = buf->st_rdev;
	lbuf.st_size = MIN(buf->st_size, INT32_MAX);
	lbuf.st_atim.tv_sec = buf->st_atim.tv_sec;
	lbuf.st_atim.tv_nsec = buf->st_atim.tv_nsec;
	lbuf.st_mtim.tv_sec = buf->st_mtim.tv_sec;
	lbuf.st_mtim.tv_nsec = buf->st_mtim.tv_nsec;
	lbuf.st_ctim.tv_sec = buf->st_ctim.tv_sec;
	lbuf.st_ctim.tv_nsec = buf->st_ctim.tv_nsec;
	lbuf.st_blksize = buf->st_blksize;
	lbuf.st_blocks = buf->st_blocks;
	lbuf.st_flags = buf->st_flags;
	lbuf.st_gen = buf->st_gen;

	return (copyout(&lbuf, ubuf, sizeof(lbuf)));
}

int
linux_stat(struct thread *td, struct linux_stat_args *args)
{
	struct stat buf;
	char *path;
	int error;

	LCONVPATHEXIST(td, args->path, &path);

#ifdef DEBUG
	if (ldebug(stat))
		printf(ARGS(stat, "%s, *"), path);
#endif
	error = linux_kern_stat(td, path, UIO_SYSSPACE, &buf);
	if (error) {
		LFREEPATH(path);
		return (error);
	}
	LFREEPATH(path);
	return (stat_copyout(&buf, args->up));
}

int
linux_lstat(struct thread *td, struct linux_lstat_args *args)
{
	struct stat buf;
	char *path;
	int error;

	LCONVPATHEXIST(td, args->path, &path);

#ifdef DEBUG
	if (ldebug(lstat))
		printf(ARGS(lstat, "%s, *"), path);
#endif
	error = linux_kern_lstat(td, path, UIO_SYSSPACE, &buf);
	if (error) {
		LFREEPATH(path);
		return (error);
	}
	LFREEPATH(path);
	return (stat_copyout(&buf, args->up));
}
#endif /* __i386__ || (__amd64__ && COMPAT_LINUX32) */

struct l_statfs {
	l_long		f_type;
	l_long		f_bsize;
	l_long		f_blocks;
	l_long		f_bfree;
	l_long		f_bavail;
	l_long		f_files;
	l_long		f_ffree;
	l_fsid_t	f_fsid;
	l_long		f_namelen;
	l_long		f_frsize;
	l_long		f_flags;
	l_long		f_spare[4];
};

#define	LINUX_CODA_SUPER_MAGIC	0x73757245L
#define	LINUX_EXT2_SUPER_MAGIC	0xEF53L
#define	LINUX_HPFS_SUPER_MAGIC	0xf995e849L
#define	LINUX_ISOFS_SUPER_MAGIC	0x9660L
#define	LINUX_MSDOS_SUPER_MAGIC	0x4d44L
#define	LINUX_NCP_SUPER_MAGIC	0x564cL
#define	LINUX_NFS_SUPER_MAGIC	0x6969L
#define	LINUX_NTFS_SUPER_MAGIC	0x5346544EL
#define	LINUX_PROC_SUPER_MAGIC	0x9fa0L
#define	LINUX_UFS_SUPER_MAGIC	0x00011954L	/* XXX - UFS_MAGIC in Linux */
#define	LINUX_ZFS_SUPER_MAGIC	0x2FC12FC1
#define	LINUX_DEVFS_SUPER_MAGIC	0x1373L
#define	LINUX_SHMFS_MAGIC	0x01021994

static long
bsd_to_linux_ftype(const char *fstypename)
{
	int i;
	static struct {const char *bsd_name; long linux_type;} b2l_tbl[] = {
		{"ufs",     LINUX_UFS_SUPER_MAGIC},
		{"zfs",     LINUX_ZFS_SUPER_MAGIC},
		{"cd9660",  LINUX_ISOFS_SUPER_MAGIC},
		{"nfs",     LINUX_NFS_SUPER_MAGIC},
		{"ext2fs",  LINUX_EXT2_SUPER_MAGIC},
		{"procfs",  LINUX_PROC_SUPER_MAGIC},
		{"msdosfs", LINUX_MSDOS_SUPER_MAGIC},
		{"ntfs",    LINUX_NTFS_SUPER_MAGIC},
		{"nwfs",    LINUX_NCP_SUPER_MAGIC},
		{"hpfs",    LINUX_HPFS_SUPER_MAGIC},
		{"coda",    LINUX_CODA_SUPER_MAGIC},
		{"devfs",   LINUX_DEVFS_SUPER_MAGIC},
		{"tmpfs",   LINUX_SHMFS_MAGIC},
		{NULL,      0L}};

	for (i = 0; b2l_tbl[i].bsd_name != NULL; i++)
		if (strcmp(b2l_tbl[i].bsd_name, fstypename) == 0)
			return (b2l_tbl[i].linux_type);

	return (0L);
}

static int
bsd_to_linux_statfs(struct statfs *bsd_statfs, struct l_statfs *linux_statfs)
{
#if defined(__i386__) || (defined(__amd64__) && defined(COMPAT_LINUX32))
	uint64_t tmp;

#define	LINUX_HIBITS	0xffffffff00000000ULL

	tmp = bsd_statfs->f_blocks | bsd_statfs->f_bfree | bsd_statfs->f_files |
	    bsd_statfs->f_bsize;
	if ((bsd_statfs->f_bavail != -1 && (bsd_statfs->f_bavail & LINUX_HIBITS)) ||
	    (bsd_statfs->f_ffree != -1 && (bsd_statfs->f_ffree & LINUX_HIBITS)) ||
	    (tmp & LINUX_HIBITS))
		return (EOVERFLOW);
#undef	LINUX_HIBITS
#endif
	linux_statfs->f_type = bsd_to_linux_ftype(bsd_statfs->f_fstypename);
	linux_statfs->f_bsize = bsd_statfs->f_bsize;
	linux_statfs->f_blocks = bsd_statfs->f_blocks;
	linux_statfs->f_bfree = bsd_statfs->f_bfree;
	linux_statfs->f_bavail = bsd_statfs->f_bavail;
	linux_statfs->f_ffree = bsd_statfs->f_ffree;
	linux_statfs->f_files = bsd_statfs->f_files;
	linux_statfs->f_fsid.val[0] = bsd_statfs->f_fsid.val[0];
	linux_statfs->f_fsid.val[1] = bsd_statfs->f_fsid.val[1];
	linux_statfs->f_namelen = MAXNAMLEN;
	linux_statfs->f_frsize = bsd_statfs->f_bsize;
	linux_statfs->f_flags = 0;
	memset(linux_statfs->f_spare, 0, sizeof(linux_statfs->f_spare));

	return (0);
}

int
linux_statfs(struct thread *td, struct linux_statfs_args *args)
{
	struct l_statfs linux_statfs;
	struct statfs *bsd_statfs;
	char *path;
	int error;

	LCONVPATHEXIST(td, args->path, &path);

#ifdef DEBUG
	if (ldebug(statfs))
		printf(ARGS(statfs, "%s, *"), path);
#endif
	bsd_statfs = malloc(sizeof(struct statfs), M_STATFS, M_WAITOK);
	error = kern_statfs(td, path, UIO_SYSSPACE, bsd_statfs);
	LFREEPATH(path);
	if (error == 0)
		error = bsd_to_linux_statfs(bsd_statfs, &linux_statfs);
	free(bsd_statfs, M_STATFS);
	if (error != 0)
		return (error);
	return (copyout(&linux_statfs, args->buf, sizeof(linux_statfs)));
}

#if defined(__i386__) || (defined(__amd64__) && defined(COMPAT_LINUX32))
static void
bsd_to_linux_statfs64(struct statfs *bsd_statfs, struct l_statfs64 *linux_statfs)
{

	linux_statfs->f_type = bsd_to_linux_ftype(bsd_statfs->f_fstypename);
	linux_statfs->f_bsize = bsd_statfs->f_bsize;
	linux_statfs->f_blocks = bsd_statfs->f_blocks;
	linux_statfs->f_bfree = bsd_statfs->f_bfree;
	linux_statfs->f_bavail = bsd_statfs->f_bavail;
	linux_statfs->f_ffree = bsd_statfs->f_ffree;
	linux_statfs->f_files = bsd_statfs->f_files;
	linux_statfs->f_fsid.val[0] = bsd_statfs->f_fsid.val[0];
	linux_statfs->f_fsid.val[1] = bsd_statfs->f_fsid.val[1];
	linux_statfs->f_namelen = MAXNAMLEN;
	linux_statfs->f_frsize = bsd_statfs->f_bsize;
	linux_statfs->f_flags = 0;
	memset(linux_statfs->f_spare, 0, sizeof(linux_statfs->f_spare));
}

int
linux_statfs64(struct thread *td, struct linux_statfs64_args *args)
{
	struct l_statfs64 linux_statfs;
	struct statfs *bsd_statfs;
	char *path;
	int error;

	if (args->bufsize != sizeof(struct l_statfs64))
		return (EINVAL);

	LCONVPATHEXIST(td, args->path, &path);

#ifdef DEBUG
	if (ldebug(statfs64))
		printf(ARGS(statfs64, "%s, *"), path);
#endif
	bsd_statfs = malloc(sizeof(struct statfs), M_STATFS, M_WAITOK);
	error = kern_statfs(td, path, UIO_SYSSPACE, bsd_statfs);
	LFREEPATH(path);
	if (error == 0)
		bsd_to_linux_statfs64(bsd_statfs, &linux_statfs);
	free(bsd_statfs, M_STATFS);
	if (error != 0)
		return (error);
	return (copyout(&linux_statfs, args->buf, sizeof(linux_statfs)));
}

int
linux_fstatfs64(struct thread *td, struct linux_fstatfs64_args *args)
{
	struct l_statfs64 linux_statfs;
	struct statfs *bsd_statfs;
	int error;

#ifdef DEBUG
	if (ldebug(fstatfs64))
		printf(ARGS(fstatfs64, "%d, *"), args->fd);
#endif
	if (args->bufsize != sizeof(struct l_statfs64))
		return (EINVAL);

	bsd_statfs = malloc(sizeof(struct statfs), M_STATFS, M_WAITOK);
	error = kern_fstatfs(td, args->fd, bsd_statfs);
	if (error == 0)
		bsd_to_linux_statfs64(bsd_statfs, &linux_statfs);
	free(bsd_statfs, M_STATFS);
	if (error != 0)
		return (error);
	return (copyout(&linux_statfs, args->buf, sizeof(linux_statfs)));
}
#endif /* __i386__ || (__amd64__ && COMPAT_LINUX32) */

int
linux_fstatfs(struct thread *td, struct linux_fstatfs_args *args)
{
	struct l_statfs linux_statfs;
	struct statfs *bsd_statfs;
	int error;

#ifdef DEBUG
	if (ldebug(fstatfs))
		printf(ARGS(fstatfs, "%d, *"), args->fd);
#endif
	bsd_statfs = malloc(sizeof(struct statfs), M_STATFS, M_WAITOK);
	error = kern_fstatfs(td, args->fd, bsd_statfs);
	if (error == 0)
		error = bsd_to_linux_statfs(bsd_statfs, &linux_statfs);
	free(bsd_statfs, M_STATFS);
	if (error != 0)
		return (error);
	return (copyout(&linux_statfs, args->buf, sizeof(linux_statfs)));
}

struct l_ustat
{
	l_daddr_t	f_tfree;
	l_ino_t		f_tinode;
	char		f_fname[6];
	char		f_fpack[6];
};

#ifdef LINUX_LEGACY_SYSCALLS
int
linux_ustat(struct thread *td, struct linux_ustat_args *args)
{
#ifdef DEBUG
	if (ldebug(ustat))
		printf(ARGS(ustat, "%ju, *"), (uintmax_t)args->dev);
#endif

	return (EOPNOTSUPP);
}
#endif

#if defined(__i386__) || (defined(__amd64__) && defined(COMPAT_LINUX32))

static int
stat64_copyout(struct stat *buf, void *ubuf)
{
	struct l_stat64 lbuf;

	bzero(&lbuf, sizeof(lbuf));
	lbuf.st_dev = dev_to_ldev(buf->st_dev);
	lbuf.st_ino = buf->st_ino;
	lbuf.st_mode = buf->st_mode;
	lbuf.st_nlink = buf->st_nlink;
	lbuf.st_uid = buf->st_uid;
	lbuf.st_gid = buf->st_gid;
	lbuf.st_rdev = buf->st_rdev;
	lbuf.st_size = buf->st_size;
	lbuf.st_atim.tv_sec = buf->st_atim.tv_sec;
	lbuf.st_atim.tv_nsec = buf->st_atim.tv_nsec;
	lbuf.st_mtim.tv_sec = buf->st_mtim.tv_sec;
	lbuf.st_mtim.tv_nsec = buf->st_mtim.tv_nsec;
	lbuf.st_ctim.tv_sec = buf->st_ctim.tv_sec;
	lbuf.st_ctim.tv_nsec = buf->st_ctim.tv_nsec;
	lbuf.st_blksize = buf->st_blksize;
	lbuf.st_blocks = buf->st_blocks;

	/*
	 * The __st_ino field makes all the difference. In the Linux kernel
	 * it is conditionally compiled based on STAT64_HAS_BROKEN_ST_INO,
	 * but without the assignment to __st_ino the runtime linker refuses
	 * to mmap(2) any shared libraries. I guess it's broken alright :-)
	 */
	lbuf.__st_ino = buf->st_ino;

	return (copyout(&lbuf, ubuf, sizeof(lbuf)));
}

int
linux_stat64(struct thread *td, struct linux_stat64_args *args)
{
	struct stat buf;
	char *filename;
	int error;

	LCONVPATHEXIST(td, args->filename, &filename);

#ifdef DEBUG
	if (ldebug(stat64))
		printf(ARGS(stat64, "%s, *"), filename);
#endif

	error = linux_kern_stat(td, filename, UIO_SYSSPACE, &buf);
	LFREEPATH(filename);
	if (error)
		return (error);
	return (stat64_copyout(&buf, args->statbuf));
}

int
linux_lstat64(struct thread *td, struct linux_lstat64_args *args)
{
	struct stat sb;
	char *filename;
	int error;

	LCONVPATHEXIST(td, args->filename, &filename);

#ifdef DEBUG
	if (ldebug(lstat64))
		printf(ARGS(lstat64, "%s, *"), args->filename);
#endif

	error = linux_kern_lstat(td, filename, UIO_SYSSPACE, &sb);
	LFREEPATH(filename);
	if (error)
		return (error);
	return (stat64_copyout(&sb, args->statbuf));
}

int
linux_fstat64(struct thread *td, struct linux_fstat64_args *args)
{
	struct stat buf;
	int error;

#ifdef DEBUG
	if (ldebug(fstat64))
		printf(ARGS(fstat64, "%d, *"), args->fd);
#endif

	error = kern_fstat(td, args->fd, &buf);
	translate_fd_major_minor(td, args->fd, &buf);
	if (!error)
		error = stat64_copyout(&buf, args->statbuf);

	return (error);
}

int
linux_fstatat64(struct thread *td, struct linux_fstatat64_args *args)
{
	char *path;
	int error, dfd, flag;
	struct stat buf;

	if (args->flag & ~LINUX_AT_SYMLINK_NOFOLLOW)
		return (EINVAL);
	flag = (args->flag & LINUX_AT_SYMLINK_NOFOLLOW) ?
	    AT_SYMLINK_NOFOLLOW : 0;

	dfd = (args->dfd == LINUX_AT_FDCWD) ? AT_FDCWD : args->dfd;
	LCONVPATHEXIST_AT(td, args->pathname, &path, dfd);

#ifdef DEBUG
	if (ldebug(fstatat64))
		printf(ARGS(fstatat64, "%i, %s, %i"), args->dfd, path, args->flag);
#endif

	error = linux_kern_statat(td, flag, dfd, path, UIO_SYSSPACE, &buf);
	if (!error)
		error = stat64_copyout(&buf, args->statbuf);
	LFREEPATH(path);

	return (error);
}

#else /* __amd64__ && !COMPAT_LINUX32 */

int
linux_newfstatat(struct thread *td, struct linux_newfstatat_args *args)
{
	char *path;
	int error, dfd, flag;
	struct stat buf;

	if (args->flag & ~LINUX_AT_SYMLINK_NOFOLLOW)
		return (EINVAL);
	flag = (args->flag & LINUX_AT_SYMLINK_NOFOLLOW) ?
	    AT_SYMLINK_NOFOLLOW : 0;

	dfd = (args->dfd == LINUX_AT_FDCWD) ? AT_FDCWD : args->dfd;
	LCONVPATHEXIST_AT(td, args->pathname, &path, dfd);

#ifdef DEBUG
	if (ldebug(newfstatat))
		printf(ARGS(newfstatat, "%i, %s, %i"), args->dfd, path, args->flag);
#endif

	error = linux_kern_statat(td, flag, dfd, path, UIO_SYSSPACE, &buf);
	if (error == 0)
		error = newstat_copyout(&buf, args->statbuf);
	LFREEPATH(path);

	return (error);
}

#endif /* __i386__ || (__amd64__ && COMPAT_LINUX32) */

int
linux_syncfs(struct thread *td, struct linux_syncfs_args *args)
{
	struct mount *mp;
	struct vnode *vp;
	int error, save;

	error = fgetvp(td, args->fd, &cap_fsync_rights, &vp);
	if (error != 0)
		/*
		 * Linux syncfs() returns only EBADF, however fgetvp()
		 * can return EINVAL in case of file descriptor does
		 * not represent a vnode. XXX.
		 */
		return (error);

	mp = vp->v_mount;
	mtx_lock(&mountlist_mtx);
	error = vfs_busy(mp, MBF_MNTLSTLOCK);
	if (error != 0) {
		/* See comment above. */
		mtx_unlock(&mountlist_mtx);
		goto out;
	}
	if ((mp->mnt_flag & MNT_RDONLY) == 0 &&
	    vn_start_write(NULL, &mp, V_NOWAIT) == 0) {
		save = curthread_pflags_set(TDP_SYNCIO);
		vfs_msync(mp, MNT_NOWAIT);
		VFS_SYNC(mp, MNT_NOWAIT);
		curthread_pflags_restore(save);
		vn_finished_write(mp);
	}
	vfs_unbusy(mp);

 out:
	vrele(vp);
	return (error);
}
