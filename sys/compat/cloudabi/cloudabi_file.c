/*-
 * Copyright (c) 2015 Nuxi, https://nuxi.nl/
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

#include <sys/param.h>
#include <sys/capsicum.h>
#include <sys/dirent.h>
#include <sys/fcntl.h>
#include <sys/kernel.h>
#include <sys/malloc.h>
#include <sys/namei.h>
#include <sys/proc.h>
#include <sys/stat.h>
#include <sys/syscallsubr.h>
#include <sys/uio.h>
#include <sys/vnode.h>

#include <contrib/cloudabi/cloudabi_types_common.h>

#include <compat/cloudabi/cloudabi_proto.h>
#include <compat/cloudabi/cloudabi_util.h>

#include <security/mac/mac_framework.h>

static MALLOC_DEFINE(M_CLOUDABI_PATH, "cloudabipath", "CloudABI pathnames");

/*
 * Copying pathnames from userspace to kernelspace.
 *
 * Unlike most operating systems, CloudABI doesn't use null-terminated
 * pathname strings. Processes always pass pathnames to the kernel by
 * providing a base pointer and a length. This has a couple of reasons:
 *
 * - It makes it easier to use CloudABI in combination with programming
 *   languages other than C, that may use non-null terminated strings.
 * - It allows for calling system calls on individual components of the
 *   pathname without modifying the input string.
 *
 * The function below copies in pathname strings and null-terminates it.
 * It also ensure that the string itself does not contain any null
 * bytes.
 *
 * TODO(ed): Add an abstraction to vfs_lookup.c that allows us to pass
 *           in unterminated pathname strings, so we can do away with
 *           the copying.
 */

static int
copyin_path(const char *uaddr, size_t len, char **result)
{
	char *buf;
	int error;

	if (len >= PATH_MAX)
		return (ENAMETOOLONG);
	buf = malloc(len + 1, M_CLOUDABI_PATH, M_WAITOK);
	error = copyin(uaddr, buf, len);
	if (error != 0) {
		free(buf, M_CLOUDABI_PATH);
		return (error);
	}
	if (memchr(buf, '\0', len) != NULL) {
		free(buf, M_CLOUDABI_PATH);
		return (EINVAL);
	}
	buf[len] = '\0';
	*result = buf;
	return (0);
}

static void
cloudabi_freestr(char *buf)
{

	free(buf, M_CLOUDABI_PATH);
}

int
cloudabi_sys_file_advise(struct thread *td,
    struct cloudabi_sys_file_advise_args *uap)
{
	int advice;

	switch (uap->advice) {
	case CLOUDABI_ADVICE_DONTNEED:
		advice = POSIX_FADV_DONTNEED;
		break;
	case CLOUDABI_ADVICE_NOREUSE:
		advice = POSIX_FADV_NOREUSE;
		break;
	case CLOUDABI_ADVICE_NORMAL:
		advice = POSIX_FADV_NORMAL;
		break;
	case CLOUDABI_ADVICE_RANDOM:
		advice = POSIX_FADV_RANDOM;
		break;
	case CLOUDABI_ADVICE_SEQUENTIAL:
		advice = POSIX_FADV_SEQUENTIAL;
		break;
	case CLOUDABI_ADVICE_WILLNEED:
		advice = POSIX_FADV_WILLNEED;
		break;
	default:
		return (EINVAL);
	}

	return (kern_posix_fadvise(td, uap->fd, uap->offset, uap->len, advice));
}

int
cloudabi_sys_file_allocate(struct thread *td,
    struct cloudabi_sys_file_allocate_args *uap)
{

	return (kern_posix_fallocate(td, uap->fd, uap->offset, uap->len));
}

int
cloudabi_sys_file_create(struct thread *td,
    struct cloudabi_sys_file_create_args *uap)
{
	char *path;
	int error;

	error = copyin_path(uap->path, uap->path_len, &path);
	if (error != 0)
		return (error);

	/*
	 * CloudABI processes cannot interact with UNIX credentials and
	 * permissions. Depend on the umask that is set prior to
	 * execution to restrict the file permissions.
	 */
	switch (uap->type) {
	case CLOUDABI_FILETYPE_DIRECTORY:
		error = kern_mkdirat(td, uap->fd, path, UIO_SYSSPACE, 0777);
		break;
	default:
		error = EINVAL;
		break;
	}
	cloudabi_freestr(path);
	return (error);
}

int
cloudabi_sys_file_link(struct thread *td,
    struct cloudabi_sys_file_link_args *uap)
{
	char *path1, *path2;
	int error;

	error = copyin_path(uap->path1, uap->path1_len, &path1);
	if (error != 0)
		return (error);
	error = copyin_path(uap->path2, uap->path2_len, &path2);
	if (error != 0) {
		cloudabi_freestr(path1);
		return (error);
	}

	error = kern_linkat(td, uap->fd1.fd, uap->fd2, path1, path2,
	    UIO_SYSSPACE, (uap->fd1.flags & CLOUDABI_LOOKUP_SYMLINK_FOLLOW) ?
	    FOLLOW : NOFOLLOW);
	cloudabi_freestr(path1);
	cloudabi_freestr(path2);
	return (error);
}

int
cloudabi_sys_file_open(struct thread *td,
    struct cloudabi_sys_file_open_args *uap)
{
	cloudabi_fdstat_t fds;
	cap_rights_t rights;
	struct filecaps fcaps = {};
	struct nameidata nd;
	struct file *fp;
	struct vnode *vp;
	char *path;
	int error, fd, fflags;
	bool read, write;

	error = copyin(uap->fds, &fds, sizeof(fds));
	if (error != 0)
		return (error);

	/* All the requested rights should be set on the descriptor. */
	error = cloudabi_convert_rights(
	    fds.fs_rights_base | fds.fs_rights_inheriting, &rights);
	if (error != 0)
		return (error);
	cap_rights_set(&rights, CAP_LOOKUP);

	/* Convert rights to corresponding access mode. */
	read = (fds.fs_rights_base & (CLOUDABI_RIGHT_FD_READ |
	    CLOUDABI_RIGHT_FILE_READDIR | CLOUDABI_RIGHT_MEM_MAP_EXEC)) != 0;
	write = (fds.fs_rights_base & (CLOUDABI_RIGHT_FD_DATASYNC |
	    CLOUDABI_RIGHT_FD_WRITE | CLOUDABI_RIGHT_FILE_ALLOCATE |
	    CLOUDABI_RIGHT_FILE_STAT_FPUT_SIZE)) != 0;
	fflags = write ? read ? FREAD | FWRITE : FWRITE : FREAD;

	/* Convert open flags. */
	if ((uap->oflags & CLOUDABI_O_CREAT) != 0) {
		fflags |= O_CREAT;
		cap_rights_set(&rights, CAP_CREATE);
	}
	if ((uap->oflags & CLOUDABI_O_DIRECTORY) != 0)
		fflags |= O_DIRECTORY;
	if ((uap->oflags & CLOUDABI_O_EXCL) != 0)
		fflags |= O_EXCL;
	if ((uap->oflags & CLOUDABI_O_TRUNC) != 0) {
		fflags |= O_TRUNC;
		cap_rights_set(&rights, CAP_FTRUNCATE);
	}
	if ((fds.fs_flags & CLOUDABI_FDFLAG_APPEND) != 0)
		fflags |= O_APPEND;
	if ((fds.fs_flags & CLOUDABI_FDFLAG_NONBLOCK) != 0)
		fflags |= O_NONBLOCK;
	if ((fds.fs_flags & (CLOUDABI_FDFLAG_SYNC | CLOUDABI_FDFLAG_DSYNC |
	    CLOUDABI_FDFLAG_RSYNC)) != 0) {
		fflags |= O_SYNC;
		cap_rights_set(&rights, CAP_FSYNC);
	}
	if ((uap->dirfd.flags & CLOUDABI_LOOKUP_SYMLINK_FOLLOW) == 0)
		fflags |= O_NOFOLLOW;
	if (write && (fflags & (O_APPEND | O_TRUNC)) == 0)
		cap_rights_set(&rights, CAP_SEEK);

	/* Allocate new file descriptor. */
	error = falloc_noinstall(td, &fp);
	if (error != 0)
		return (error);
	fp->f_flag = fflags & FMASK;

	/* Open path. */
	error = copyin_path(uap->path, uap->path_len, &path);
	if (error != 0) {
		fdrop(fp, td);
		return (error);
	}
	NDINIT_ATRIGHTS(&nd, LOOKUP, FOLLOW, UIO_SYSSPACE, path, uap->dirfd.fd,
	    &rights, td);
	error = vn_open(&nd, &fflags, 0777 & ~td->td_proc->p_fd->fd_cmask, fp);
	cloudabi_freestr(path);
	if (error != 0) {
		/* Custom operations provided. */
		if (error == ENXIO && fp->f_ops != &badfileops)
			goto success;

		/*
		 * POSIX compliance: return ELOOP in case openat() is
		 * called on a symbolic link and O_NOFOLLOW is set.
		 */
		if (error == EMLINK)
			error = ELOOP;
		fdrop(fp, td);
		return (error);
	}
	NDFREE(&nd, NDF_ONLY_PNBUF);
	filecaps_free(&nd.ni_filecaps);
	fp->f_vnode = vp = nd.ni_vp;

	/* Install vnode operations if no custom operations are provided. */
	if (fp->f_ops == &badfileops) {
		fp->f_seqcount = 1;
		finit(fp, (fflags & FMASK) | (fp->f_flag & FHASLOCK),
		    DTYPE_VNODE, vp, &vnops);
	}
	VOP_UNLOCK(vp, 0);

	/* Truncate file. */
	if (fflags & O_TRUNC) {
		error = fo_truncate(fp, 0, td->td_ucred, td);
		if (error != 0) {
			fdrop(fp, td);
			return (error);
		}
	}

success:
	/* Determine which Capsicum rights to set on the file descriptor. */
	cloudabi_remove_conflicting_rights(cloudabi_convert_filetype(fp),
	    &fds.fs_rights_base, &fds.fs_rights_inheriting);
	cloudabi_convert_rights(fds.fs_rights_base | fds.fs_rights_inheriting,
	    &fcaps.fc_rights);
	if (cap_rights_is_set(&fcaps.fc_rights))
		fcaps.fc_fcntls = CAP_FCNTL_SETFL;

	error = finstall(td, fp, &fd, fflags, &fcaps);
	fdrop(fp, td);
	if (error != 0)
		return (error);
	td->td_retval[0] = fd;
	return (0);
}

/* Converts a FreeBSD directory entry structure and writes it to userspace. */
static int
write_dirent(struct dirent *bde, cloudabi_dircookie_t cookie, struct uio *uio)
{
	cloudabi_dirent_t cde = {
		.d_next = cookie,
		.d_ino = bde->d_fileno,
		.d_namlen = bde->d_namlen,
	};
	size_t len;
	int error;

	/* Convert file type. */
	switch (bde->d_type) {
	case DT_BLK:
		cde.d_type = CLOUDABI_FILETYPE_BLOCK_DEVICE;
		break;
	case DT_CHR:
		cde.d_type = CLOUDABI_FILETYPE_CHARACTER_DEVICE;
		break;
	case DT_DIR:
		cde.d_type = CLOUDABI_FILETYPE_DIRECTORY;
		break;
	case DT_FIFO:
		cde.d_type = CLOUDABI_FILETYPE_SOCKET_STREAM;
		break;
	case DT_LNK:
		cde.d_type = CLOUDABI_FILETYPE_SYMBOLIC_LINK;
		break;
	case DT_REG:
		cde.d_type = CLOUDABI_FILETYPE_REGULAR_FILE;
		break;
	case DT_SOCK:
		/* The exact socket type cannot be derived. */
		cde.d_type = CLOUDABI_FILETYPE_SOCKET_STREAM;
		break;
	default:
		cde.d_type = CLOUDABI_FILETYPE_UNKNOWN;
		break;
	}

	/* Write directory entry structure. */
	len = sizeof(cde) < uio->uio_resid ? sizeof(cde) : uio->uio_resid;
	error = uiomove(&cde, len, uio);
	if (error != 0)
		return (error);

	/* Write filename. */
	len = bde->d_namlen < uio->uio_resid ? bde->d_namlen : uio->uio_resid;
	return (uiomove(bde->d_name, len, uio));
}

int
cloudabi_sys_file_readdir(struct thread *td,
    struct cloudabi_sys_file_readdir_args *uap)
{
	struct iovec iov = {
		.iov_base = uap->buf,
		.iov_len = uap->buf_len
	};
	struct uio uio = {
		.uio_iov = &iov,
		.uio_iovcnt = 1,
		.uio_resid = iov.iov_len,
		.uio_segflg = UIO_USERSPACE,
		.uio_rw = UIO_READ,
		.uio_td = td
	};
	struct file *fp;
	struct vnode *vp;
	void *readbuf;
	cloudabi_dircookie_t offset;
	int error;

	/* Obtain directory vnode. */
	error = getvnode(td, uap->fd, &cap_read_rights, &fp);
	if (error != 0) {
		if (error == EINVAL)
			return (ENOTDIR);
		return (error);
	}
	if ((fp->f_flag & FREAD) == 0) {
		fdrop(fp, td);
		return (EBADF);
	}

	/*
	 * Call VOP_READDIR() and convert resulting data until the user
	 * provided buffer is filled.
	 */
	readbuf = malloc(MAXBSIZE, M_TEMP, M_WAITOK);
	offset = uap->cookie;
	vp = fp->f_vnode;
	while (uio.uio_resid > 0) {
		struct iovec readiov = {
			.iov_base = readbuf,
			.iov_len = MAXBSIZE
		};
		struct uio readuio = {
			.uio_iov = &readiov,
			.uio_iovcnt = 1,
			.uio_rw = UIO_READ,
			.uio_segflg = UIO_SYSSPACE,
			.uio_td = td,
			.uio_resid = MAXBSIZE,
			.uio_offset = offset
		};
		struct dirent *bde;
		unsigned long *cookies, *cookie;
		size_t readbuflen;
		int eof, ncookies;

		/* Validate file type. */
		vn_lock(vp, LK_SHARED | LK_RETRY);
		if (vp->v_type != VDIR) {
			VOP_UNLOCK(vp, 0);
			error = ENOTDIR;
			goto done;
		}
#ifdef MAC
		error = mac_vnode_check_readdir(td->td_ucred, vp);
		if (error != 0) {
			VOP_UNLOCK(vp, 0);
			goto done;
		}
#endif /* MAC */

		/* Read new directory entries. */
		cookies = NULL;
		ncookies = 0;
		error = VOP_READDIR(vp, &readuio, fp->f_cred, &eof,
		    &ncookies, &cookies);
		VOP_UNLOCK(vp, 0);
		if (error != 0)
			goto done;

		/* Convert entries to CloudABI's format. */
		readbuflen = MAXBSIZE - readuio.uio_resid;
		bde = readbuf;
		cookie = cookies;
		while (readbuflen >= offsetof(struct dirent, d_name) &&
		    uio.uio_resid > 0 && ncookies > 0) {
			/* Ensure that the returned offset always increases. */
			if (readbuflen >= bde->d_reclen && bde->d_fileno != 0 &&
			    *cookie > offset) {
				error = write_dirent(bde, *cookie, &uio);
				if (error != 0) {
					free(cookies, M_TEMP);
					goto done;
				}
			}

			if (offset < *cookie)
				offset = *cookie;
			++cookie;
			--ncookies;
			readbuflen -= bde->d_reclen;
			bde = (struct dirent *)((char *)bde + bde->d_reclen);
		}
		free(cookies, M_TEMP);
		if (eof)
			break;
	}

done:
	fdrop(fp, td);
	free(readbuf, M_TEMP);
	if (error != 0)
		return (error);

	/* Return number of bytes copied to userspace. */
	td->td_retval[0] = uap->buf_len - uio.uio_resid;
	return (0);
}

int
cloudabi_sys_file_readlink(struct thread *td,
    struct cloudabi_sys_file_readlink_args *uap)
{
	char *path;
	int error;

	error = copyin_path(uap->path, uap->path_len, &path);
	if (error != 0)
		return (error);

	error = kern_readlinkat(td, uap->fd, path, UIO_SYSSPACE,
	    uap->buf, UIO_USERSPACE, uap->buf_len);
	cloudabi_freestr(path);
	return (error);
}

int
cloudabi_sys_file_rename(struct thread *td,
    struct cloudabi_sys_file_rename_args *uap)
{
	char *old, *new;
	int error;

	error = copyin_path(uap->path1, uap->path1_len, &old);
	if (error != 0)
		return (error);
	error = copyin_path(uap->path2, uap->path2_len, &new);
	if (error != 0) {
		cloudabi_freestr(old);
		return (error);
	}

	error = kern_renameat(td, uap->fd1, old, uap->fd2, new,
	    UIO_SYSSPACE);
	cloudabi_freestr(old);
	cloudabi_freestr(new);
	return (error);
}

/* Converts a FreeBSD stat structure to a CloudABI stat structure. */
static void
convert_stat(const struct stat *sb, cloudabi_filestat_t *csb)
{
	cloudabi_filestat_t res = {
		.st_dev		= sb->st_dev,
		.st_ino		= sb->st_ino,
		.st_nlink	= sb->st_nlink,
		.st_size	= sb->st_size,
	};

	cloudabi_convert_timespec(&sb->st_atim, &res.st_atim);
	cloudabi_convert_timespec(&sb->st_mtim, &res.st_mtim);
	cloudabi_convert_timespec(&sb->st_ctim, &res.st_ctim);
	*csb = res;
}

int
cloudabi_sys_file_stat_fget(struct thread *td,
    struct cloudabi_sys_file_stat_fget_args *uap)
{
	struct stat sb;
	cloudabi_filestat_t csb;
	struct file *fp;
	cloudabi_filetype_t filetype;
	int error;

	memset(&csb, 0, sizeof(csb));

	/* Fetch file descriptor attributes. */
	error = fget(td, uap->fd, &cap_fstat_rights, &fp);
	if (error != 0)
		return (error);
	error = fo_stat(fp, &sb, td->td_ucred, td);
	if (error != 0) {
		fdrop(fp, td);
		return (error);
	}
	filetype = cloudabi_convert_filetype(fp);
	fdrop(fp, td);

	/* Convert attributes to CloudABI's format. */
	convert_stat(&sb, &csb);
	csb.st_filetype = filetype;
	return (copyout(&csb, uap->buf, sizeof(csb)));
}

/* Converts timestamps to arguments to futimens() and utimensat(). */
static void
convert_utimens_arguments(const cloudabi_filestat_t *fs,
    cloudabi_fsflags_t flags, struct timespec *ts)
{

	if ((flags & CLOUDABI_FILESTAT_ATIM_NOW) != 0) {
		ts[0].tv_nsec = UTIME_NOW;
	} else if ((flags & CLOUDABI_FILESTAT_ATIM) != 0) {
		ts[0].tv_sec = fs->st_atim / 1000000000;
		ts[0].tv_nsec = fs->st_atim % 1000000000;
	} else {
		ts[0].tv_nsec = UTIME_OMIT;
	}

	if ((flags & CLOUDABI_FILESTAT_MTIM_NOW) != 0) {
		ts[1].tv_nsec = UTIME_NOW;
	} else if ((flags & CLOUDABI_FILESTAT_MTIM) != 0) {
		ts[1].tv_sec = fs->st_mtim / 1000000000;
		ts[1].tv_nsec = fs->st_mtim % 1000000000;
	} else {
		ts[1].tv_nsec = UTIME_OMIT;
	}
}

int
cloudabi_sys_file_stat_fput(struct thread *td,
    struct cloudabi_sys_file_stat_fput_args *uap)
{
	cloudabi_filestat_t fs;
	struct timespec ts[2];
	int error;

	error = copyin(uap->buf, &fs, sizeof(fs));
	if (error != 0)
		return (error);

	/*
	 * Only support truncation and timestamp modification separately
	 * for now, to prevent unnecessary code duplication.
	 */
	if ((uap->flags & CLOUDABI_FILESTAT_SIZE) != 0) {
		/* Call into kern_ftruncate() for file truncation. */
		if ((uap->flags & ~CLOUDABI_FILESTAT_SIZE) != 0)
			return (EINVAL);
		return (kern_ftruncate(td, uap->fd, fs.st_size));
	} else if ((uap->flags & (CLOUDABI_FILESTAT_ATIM |
	    CLOUDABI_FILESTAT_ATIM_NOW | CLOUDABI_FILESTAT_MTIM |
	    CLOUDABI_FILESTAT_MTIM_NOW)) != 0) {
		/* Call into kern_futimens() for timestamp modification. */
		if ((uap->flags & ~(CLOUDABI_FILESTAT_ATIM |
		    CLOUDABI_FILESTAT_ATIM_NOW | CLOUDABI_FILESTAT_MTIM |
		    CLOUDABI_FILESTAT_MTIM_NOW)) != 0)
			return (EINVAL);
		convert_utimens_arguments(&fs, uap->flags, ts);
		return (kern_futimens(td, uap->fd, ts, UIO_SYSSPACE));
	}
	return (EINVAL);
}

int
cloudabi_sys_file_stat_get(struct thread *td,
    struct cloudabi_sys_file_stat_get_args *uap)
{
	struct stat sb;
	cloudabi_filestat_t csb;
	char *path;
	int error;

	memset(&csb, 0, sizeof(csb));

	error = copyin_path(uap->path, uap->path_len, &path);
	if (error != 0)
		return (error);

	error = kern_statat(td,
	    (uap->fd.flags & CLOUDABI_LOOKUP_SYMLINK_FOLLOW) != 0 ? 0 :
	    AT_SYMLINK_NOFOLLOW, uap->fd.fd, path, UIO_SYSSPACE, &sb, NULL);
	cloudabi_freestr(path);
	if (error != 0)
		return (error);

	/* Convert results and return them. */
	convert_stat(&sb, &csb);
	if (S_ISBLK(sb.st_mode))
		csb.st_filetype = CLOUDABI_FILETYPE_BLOCK_DEVICE;
	else if (S_ISCHR(sb.st_mode))
		csb.st_filetype = CLOUDABI_FILETYPE_CHARACTER_DEVICE;
	else if (S_ISDIR(sb.st_mode))
		csb.st_filetype = CLOUDABI_FILETYPE_DIRECTORY;
	else if (S_ISFIFO(sb.st_mode))
		csb.st_filetype = CLOUDABI_FILETYPE_SOCKET_STREAM;
	else if (S_ISREG(sb.st_mode))
		csb.st_filetype = CLOUDABI_FILETYPE_REGULAR_FILE;
	else if (S_ISSOCK(sb.st_mode)) {
		/* Inaccurate, but the best that we can do. */
		csb.st_filetype = CLOUDABI_FILETYPE_SOCKET_STREAM;
	} else if (S_ISLNK(sb.st_mode))
		csb.st_filetype = CLOUDABI_FILETYPE_SYMBOLIC_LINK;
	else
		csb.st_filetype = CLOUDABI_FILETYPE_UNKNOWN;
	return (copyout(&csb, uap->buf, sizeof(csb)));
}

int
cloudabi_sys_file_stat_put(struct thread *td,
    struct cloudabi_sys_file_stat_put_args *uap)
{
	cloudabi_filestat_t fs;
	struct timespec ts[2];
	char *path;
	int error;

	/*
	 * Only support timestamp modification for now, as there is no
	 * truncateat().
	 */
	if ((uap->flags & ~(CLOUDABI_FILESTAT_ATIM |
	    CLOUDABI_FILESTAT_ATIM_NOW | CLOUDABI_FILESTAT_MTIM |
	    CLOUDABI_FILESTAT_MTIM_NOW)) != 0)
		return (EINVAL);

	error = copyin(uap->buf, &fs, sizeof(fs));
	if (error != 0)
		return (error);
	error = copyin_path(uap->path, uap->path_len, &path);
	if (error != 0)
		return (error);

	convert_utimens_arguments(&fs, uap->flags, ts);
	error = kern_utimensat(td, uap->fd.fd, path, UIO_SYSSPACE, ts,
	    UIO_SYSSPACE, (uap->fd.flags & CLOUDABI_LOOKUP_SYMLINK_FOLLOW) ?
	    0 : AT_SYMLINK_NOFOLLOW);
	cloudabi_freestr(path);
	return (error);
}

int
cloudabi_sys_file_symlink(struct thread *td,
    struct cloudabi_sys_file_symlink_args *uap)
{
	char *path1, *path2;
	int error;

	error = copyin_path(uap->path1, uap->path1_len, &path1);
	if (error != 0)
		return (error);
	error = copyin_path(uap->path2, uap->path2_len, &path2);
	if (error != 0) {
		cloudabi_freestr(path1);
		return (error);
	}

	error = kern_symlinkat(td, path1, uap->fd, path2, UIO_SYSSPACE);
	cloudabi_freestr(path1);
	cloudabi_freestr(path2);
	return (error);
}

int
cloudabi_sys_file_unlink(struct thread *td,
    struct cloudabi_sys_file_unlink_args *uap)
{
	char *path;
	int error;

	error = copyin_path(uap->path, uap->path_len, &path);
	if (error != 0)
		return (error);

	if (uap->flags & CLOUDABI_UNLINK_REMOVEDIR)
		error = kern_rmdirat(td, uap->fd, path, UIO_SYSSPACE, 0);
	else
		error = kern_unlinkat(td, uap->fd, path, UIO_SYSSPACE, 0, 0);
	cloudabi_freestr(path);
	return (error);
}
