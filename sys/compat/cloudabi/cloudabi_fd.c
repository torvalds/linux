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
#include <sys/filedesc.h>
#include <sys/proc.h>
#include <sys/mman.h>
#include <sys/socketvar.h>
#include <sys/syscallsubr.h>
#include <sys/sysproto.h>
#include <sys/systm.h>
#include <sys/unistd.h>
#include <sys/vnode.h>

#include <contrib/cloudabi/cloudabi_types_common.h>

#include <compat/cloudabi/cloudabi_proto.h>
#include <compat/cloudabi/cloudabi_util.h>

/* Translation between CloudABI and Capsicum rights. */
#define RIGHTS_MAPPINGS \
	MAPPING(CLOUDABI_RIGHT_FD_DATASYNC, CAP_FSYNC)			\
	MAPPING(CLOUDABI_RIGHT_FD_READ, CAP_READ)			\
	MAPPING(CLOUDABI_RIGHT_FD_SEEK, CAP_SEEK)			\
	MAPPING(CLOUDABI_RIGHT_FD_STAT_PUT_FLAGS, CAP_FCNTL)		\
	MAPPING(CLOUDABI_RIGHT_FD_SYNC, CAP_FSYNC)			\
	MAPPING(CLOUDABI_RIGHT_FD_TELL, CAP_SEEK_TELL)			\
	MAPPING(CLOUDABI_RIGHT_FD_WRITE, CAP_WRITE)			\
	MAPPING(CLOUDABI_RIGHT_FILE_ADVISE)				\
	MAPPING(CLOUDABI_RIGHT_FILE_ALLOCATE, CAP_WRITE)		\
	MAPPING(CLOUDABI_RIGHT_FILE_CREATE_DIRECTORY, CAP_MKDIRAT)	\
	MAPPING(CLOUDABI_RIGHT_FILE_CREATE_FILE, CAP_CREATE)		\
	MAPPING(CLOUDABI_RIGHT_FILE_LINK_SOURCE, CAP_LINKAT_SOURCE)	\
	MAPPING(CLOUDABI_RIGHT_FILE_LINK_TARGET, CAP_LINKAT_TARGET)	\
	MAPPING(CLOUDABI_RIGHT_FILE_OPEN, CAP_LOOKUP)			\
	MAPPING(CLOUDABI_RIGHT_FILE_READDIR, CAP_READ)			\
	MAPPING(CLOUDABI_RIGHT_FILE_READLINK, CAP_LOOKUP)		\
	MAPPING(CLOUDABI_RIGHT_FILE_RENAME_SOURCE, CAP_RENAMEAT_SOURCE)	\
	MAPPING(CLOUDABI_RIGHT_FILE_RENAME_TARGET, CAP_RENAMEAT_TARGET)	\
	MAPPING(CLOUDABI_RIGHT_FILE_STAT_FGET, CAP_FSTAT)		\
	MAPPING(CLOUDABI_RIGHT_FILE_STAT_FPUT_SIZE, CAP_FTRUNCATE)	\
	MAPPING(CLOUDABI_RIGHT_FILE_STAT_FPUT_TIMES, CAP_FUTIMES)	\
	MAPPING(CLOUDABI_RIGHT_FILE_STAT_GET, CAP_FSTATAT)		\
	MAPPING(CLOUDABI_RIGHT_FILE_STAT_PUT_TIMES, CAP_FUTIMESAT)	\
	MAPPING(CLOUDABI_RIGHT_FILE_SYMLINK, CAP_SYMLINKAT)		\
	MAPPING(CLOUDABI_RIGHT_FILE_UNLINK, CAP_UNLINKAT)		\
	MAPPING(CLOUDABI_RIGHT_MEM_MAP, CAP_MMAP)			\
	MAPPING(CLOUDABI_RIGHT_MEM_MAP_EXEC, CAP_MMAP_X)		\
	MAPPING(CLOUDABI_RIGHT_POLL_FD_READWRITE, CAP_EVENT)		\
	MAPPING(CLOUDABI_RIGHT_POLL_PROC_TERMINATE, CAP_EVENT)		\
	MAPPING(CLOUDABI_RIGHT_PROC_EXEC, CAP_FEXECVE)			\
	MAPPING(CLOUDABI_RIGHT_SOCK_SHUTDOWN, CAP_SHUTDOWN)		\

int
cloudabi_sys_fd_close(struct thread *td, struct cloudabi_sys_fd_close_args *uap)
{

	return (kern_close(td, uap->fd));
}

int
cloudabi_sys_fd_create1(struct thread *td,
    struct cloudabi_sys_fd_create1_args *uap)
{
	struct filecaps fcaps = {};

	switch (uap->type) {
	case CLOUDABI_FILETYPE_SHARED_MEMORY:
		cap_rights_init(&fcaps.fc_rights, CAP_FSTAT, CAP_FTRUNCATE,
		    CAP_MMAP_RWX);
		return (kern_shm_open(td, SHM_ANON, O_RDWR, 0, &fcaps));
	default:
		return (EINVAL);
	}
}

int
cloudabi_sys_fd_create2(struct thread *td,
    struct cloudabi_sys_fd_create2_args *uap)
{
	int fds[2];
	int error, type;

	switch (uap->type) {
	case CLOUDABI_FILETYPE_SOCKET_DGRAM:
		type = SOCK_DGRAM;
		break;
	case CLOUDABI_FILETYPE_SOCKET_STREAM:
		type = SOCK_STREAM;
		break;
	default:
		return (EINVAL);
	}

	error = kern_socketpair(td, AF_UNIX, type, 0, fds);
	if (error == 0) {
		td->td_retval[0] = fds[0];
		td->td_retval[1] = fds[1];
	}
	return (0);
}

int
cloudabi_sys_fd_datasync(struct thread *td,
    struct cloudabi_sys_fd_datasync_args *uap)
{

	return (kern_fsync(td, uap->fd, false));
}

int
cloudabi_sys_fd_dup(struct thread *td, struct cloudabi_sys_fd_dup_args *uap)
{

	return (kern_dup(td, FDDUP_NORMAL, 0, uap->from, 0));
}

int
cloudabi_sys_fd_replace(struct thread *td,
    struct cloudabi_sys_fd_replace_args *uap)
{
	int error;

	/*
	 * CloudABI's equivalent to dup2(). CloudABI processes should
	 * not depend on hardcoded file descriptor layouts, but simply
	 * use the file descriptor numbers that are allocated by the
	 * kernel. Duplicating file descriptors to arbitrary numbers
	 * should not be done.
	 *
	 * Invoke kern_dup() with FDDUP_MUSTREPLACE, so that we return
	 * EBADF when duplicating to a nonexistent file descriptor. Also
	 * clear the return value, as this system call yields no return
	 * value.
	 */
	error = kern_dup(td, FDDUP_MUSTREPLACE, 0, uap->from, uap->to);
	td->td_retval[0] = 0;
	return (error);
}

int
cloudabi_sys_fd_seek(struct thread *td, struct cloudabi_sys_fd_seek_args *uap)
{
	int whence;

	switch (uap->whence) {
	case CLOUDABI_WHENCE_CUR:
		whence = SEEK_CUR;
		break;
	case CLOUDABI_WHENCE_END:
		whence = SEEK_END;
		break;
	case CLOUDABI_WHENCE_SET:
		whence = SEEK_SET;
		break;
	default:
		return (EINVAL);
	}

	return (kern_lseek(td, uap->fd, uap->offset, whence));
}

/* Converts a file descriptor to a CloudABI file descriptor type. */
cloudabi_filetype_t
cloudabi_convert_filetype(const struct file *fp)
{
	struct socket *so;
	struct vnode *vp;

	switch (fp->f_type) {
	case DTYPE_FIFO:
		return (CLOUDABI_FILETYPE_SOCKET_STREAM);
	case DTYPE_PIPE:
		return (CLOUDABI_FILETYPE_SOCKET_STREAM);
	case DTYPE_PROCDESC:
		return (CLOUDABI_FILETYPE_PROCESS);
	case DTYPE_SHM:
		return (CLOUDABI_FILETYPE_SHARED_MEMORY);
	case DTYPE_SOCKET:
		so = fp->f_data;
		switch (so->so_type) {
		case SOCK_DGRAM:
			return (CLOUDABI_FILETYPE_SOCKET_DGRAM);
		case SOCK_STREAM:
			return (CLOUDABI_FILETYPE_SOCKET_STREAM);
		default:
			return (CLOUDABI_FILETYPE_UNKNOWN);
		}
	case DTYPE_VNODE:
		vp = fp->f_vnode;
		switch (vp->v_type) {
		case VBLK:
			return (CLOUDABI_FILETYPE_BLOCK_DEVICE);
		case VCHR:
			return (CLOUDABI_FILETYPE_CHARACTER_DEVICE);
		case VDIR:
			return (CLOUDABI_FILETYPE_DIRECTORY);
		case VFIFO:
			return (CLOUDABI_FILETYPE_SOCKET_STREAM);
		case VLNK:
			return (CLOUDABI_FILETYPE_SYMBOLIC_LINK);
		case VREG:
			return (CLOUDABI_FILETYPE_REGULAR_FILE);
		case VSOCK:
			return (CLOUDABI_FILETYPE_SOCKET_STREAM);
		default:
			return (CLOUDABI_FILETYPE_UNKNOWN);
		}
	default:
		return (CLOUDABI_FILETYPE_UNKNOWN);
	}
}

/* Removes rights that conflict with the file descriptor type. */
void
cloudabi_remove_conflicting_rights(cloudabi_filetype_t filetype,
    cloudabi_rights_t *base, cloudabi_rights_t *inheriting)
{

	/*
	 * CloudABI has a small number of additional rights bits to
	 * disambiguate between multiple purposes. Remove the bits that
	 * don't apply to the type of the file descriptor.
	 *
	 * As file descriptor access modes (O_ACCMODE) has been fully
	 * replaced by rights bits, CloudABI distinguishes between
	 * rights that apply to the file descriptor itself (base) versus
	 * rights of new file descriptors derived from them
	 * (inheriting). The code below approximates the pair by
	 * decomposing depending on the file descriptor type.
	 *
	 * We need to be somewhat accurate about which actions can
	 * actually be performed on the file descriptor, as functions
	 * like fcntl(fd, F_GETFL) are emulated on top of this.
	 */
	switch (filetype) {
	case CLOUDABI_FILETYPE_DIRECTORY:
		*base &= CLOUDABI_RIGHT_FD_STAT_PUT_FLAGS |
		    CLOUDABI_RIGHT_FD_SYNC | CLOUDABI_RIGHT_FILE_ADVISE |
		    CLOUDABI_RIGHT_FILE_CREATE_DIRECTORY |
		    CLOUDABI_RIGHT_FILE_CREATE_FILE |
		    CLOUDABI_RIGHT_FILE_LINK_SOURCE |
		    CLOUDABI_RIGHT_FILE_LINK_TARGET |
		    CLOUDABI_RIGHT_FILE_OPEN |
		    CLOUDABI_RIGHT_FILE_READDIR |
		    CLOUDABI_RIGHT_FILE_READLINK |
		    CLOUDABI_RIGHT_FILE_RENAME_SOURCE |
		    CLOUDABI_RIGHT_FILE_RENAME_TARGET |
		    CLOUDABI_RIGHT_FILE_STAT_FGET |
		    CLOUDABI_RIGHT_FILE_STAT_FPUT_TIMES |
		    CLOUDABI_RIGHT_FILE_STAT_GET |
		    CLOUDABI_RIGHT_FILE_STAT_PUT_TIMES |
		    CLOUDABI_RIGHT_FILE_SYMLINK |
		    CLOUDABI_RIGHT_FILE_UNLINK |
		    CLOUDABI_RIGHT_POLL_FD_READWRITE;
		*inheriting &= CLOUDABI_RIGHT_FD_DATASYNC |
		    CLOUDABI_RIGHT_FD_READ |
		    CLOUDABI_RIGHT_FD_SEEK |
		    CLOUDABI_RIGHT_FD_STAT_PUT_FLAGS |
		    CLOUDABI_RIGHT_FD_SYNC |
		    CLOUDABI_RIGHT_FD_TELL |
		    CLOUDABI_RIGHT_FD_WRITE |
		    CLOUDABI_RIGHT_FILE_ADVISE |
		    CLOUDABI_RIGHT_FILE_ALLOCATE |
		    CLOUDABI_RIGHT_FILE_CREATE_DIRECTORY |
		    CLOUDABI_RIGHT_FILE_CREATE_FILE |
		    CLOUDABI_RIGHT_FILE_LINK_SOURCE |
		    CLOUDABI_RIGHT_FILE_LINK_TARGET |
		    CLOUDABI_RIGHT_FILE_OPEN |
		    CLOUDABI_RIGHT_FILE_READDIR |
		    CLOUDABI_RIGHT_FILE_READLINK |
		    CLOUDABI_RIGHT_FILE_RENAME_SOURCE |
		    CLOUDABI_RIGHT_FILE_RENAME_TARGET |
		    CLOUDABI_RIGHT_FILE_STAT_FGET |
		    CLOUDABI_RIGHT_FILE_STAT_FPUT_SIZE |
		    CLOUDABI_RIGHT_FILE_STAT_FPUT_TIMES |
		    CLOUDABI_RIGHT_FILE_STAT_GET |
		    CLOUDABI_RIGHT_FILE_STAT_PUT_TIMES |
		    CLOUDABI_RIGHT_FILE_SYMLINK |
		    CLOUDABI_RIGHT_FILE_UNLINK |
		    CLOUDABI_RIGHT_MEM_MAP |
		    CLOUDABI_RIGHT_MEM_MAP_EXEC |
		    CLOUDABI_RIGHT_POLL_FD_READWRITE |
		    CLOUDABI_RIGHT_PROC_EXEC;
		break;
	case CLOUDABI_FILETYPE_PROCESS:
		*base &= ~(CLOUDABI_RIGHT_FILE_ADVISE |
		    CLOUDABI_RIGHT_POLL_FD_READWRITE);
		*inheriting = 0;
		break;
	case CLOUDABI_FILETYPE_REGULAR_FILE:
		*base &= CLOUDABI_RIGHT_FD_DATASYNC |
		    CLOUDABI_RIGHT_FD_READ |
		    CLOUDABI_RIGHT_FD_SEEK |
		    CLOUDABI_RIGHT_FD_STAT_PUT_FLAGS |
		    CLOUDABI_RIGHT_FD_SYNC |
		    CLOUDABI_RIGHT_FD_TELL |
		    CLOUDABI_RIGHT_FD_WRITE |
		    CLOUDABI_RIGHT_FILE_ADVISE |
		    CLOUDABI_RIGHT_FILE_ALLOCATE |
		    CLOUDABI_RIGHT_FILE_STAT_FGET |
		    CLOUDABI_RIGHT_FILE_STAT_FPUT_SIZE |
		    CLOUDABI_RIGHT_FILE_STAT_FPUT_TIMES |
		    CLOUDABI_RIGHT_MEM_MAP |
		    CLOUDABI_RIGHT_MEM_MAP_EXEC |
		    CLOUDABI_RIGHT_POLL_FD_READWRITE |
		    CLOUDABI_RIGHT_PROC_EXEC;
		*inheriting = 0;
		break;
	case CLOUDABI_FILETYPE_SHARED_MEMORY:
		*base &= ~(CLOUDABI_RIGHT_FD_SEEK |
		    CLOUDABI_RIGHT_FD_TELL |
		    CLOUDABI_RIGHT_FILE_ADVISE |
		    CLOUDABI_RIGHT_FILE_ALLOCATE |
		    CLOUDABI_RIGHT_FILE_READDIR);
		*inheriting = 0;
		break;
	case CLOUDABI_FILETYPE_SOCKET_DGRAM:
	case CLOUDABI_FILETYPE_SOCKET_STREAM:
		*base &= CLOUDABI_RIGHT_FD_READ |
		    CLOUDABI_RIGHT_FD_STAT_PUT_FLAGS |
		    CLOUDABI_RIGHT_FD_WRITE |
		    CLOUDABI_RIGHT_FILE_STAT_FGET |
		    CLOUDABI_RIGHT_POLL_FD_READWRITE |
		    CLOUDABI_RIGHT_SOCK_SHUTDOWN;
		break;
	default:
		*inheriting = 0;
		break;
	}
}

/* Converts FreeBSD's Capsicum rights to CloudABI's set of rights. */
static void
convert_capabilities(const cap_rights_t *capabilities,
    cloudabi_filetype_t filetype, cloudabi_rights_t *base,
    cloudabi_rights_t *inheriting)
{
	cloudabi_rights_t rights;

	/* Convert FreeBSD bits to CloudABI bits. */
	rights = 0;
#define MAPPING(cloudabi, ...) do {				\
	if (cap_rights_is_set(capabilities, ##__VA_ARGS__))	\
		rights |= (cloudabi);				\
} while (0);
	RIGHTS_MAPPINGS
#undef MAPPING

	*base = rights;
	*inheriting = rights;
	cloudabi_remove_conflicting_rights(filetype, base, inheriting);
}

int
cloudabi_sys_fd_stat_get(struct thread *td,
    struct cloudabi_sys_fd_stat_get_args *uap)
{
	cloudabi_fdstat_t fsb = {0};
	struct file *fp;
	cap_rights_t rights;
	struct filecaps fcaps;
	int error, oflags;

	/* Obtain file descriptor properties. */
	error = fget_cap(td, uap->fd, cap_rights_init(&rights), &fp,
	    &fcaps);
	if (error != 0)
		return (error);
	oflags = OFLAGS(fp->f_flag);
	fsb.fs_filetype = cloudabi_convert_filetype(fp);
	fdrop(fp, td);

	/* Convert file descriptor flags. */
	if (oflags & O_APPEND)
		fsb.fs_flags |= CLOUDABI_FDFLAG_APPEND;
	if (oflags & O_NONBLOCK)
		fsb.fs_flags |= CLOUDABI_FDFLAG_NONBLOCK;
	if (oflags & O_SYNC)
		fsb.fs_flags |= CLOUDABI_FDFLAG_SYNC;

	/* Convert capabilities to CloudABI rights. */
	convert_capabilities(&fcaps.fc_rights, fsb.fs_filetype,
	    &fsb.fs_rights_base, &fsb.fs_rights_inheriting);
	filecaps_free(&fcaps);
	return (copyout(&fsb, (void *)uap->buf, sizeof(fsb)));
}

/* Converts CloudABI rights to a set of Capsicum capabilities. */
int
cloudabi_convert_rights(cloudabi_rights_t in, cap_rights_t *out)
{

	cap_rights_init(out);
#define MAPPING(cloudabi, ...) do {			\
	if (in & (cloudabi)) {				\
		cap_rights_set(out, ##__VA_ARGS__);	\
		in &= ~(cloudabi);			\
	}						\
} while (0);
	RIGHTS_MAPPINGS
#undef MAPPING
	if (in != 0)
		return (ENOTCAPABLE);
	return (0);
}

int
cloudabi_sys_fd_stat_put(struct thread *td,
    struct cloudabi_sys_fd_stat_put_args *uap)
{
	cloudabi_fdstat_t fsb;
	cap_rights_t rights;
	int error, oflags;

	error = copyin(uap->buf, &fsb, sizeof(fsb));
	if (error != 0)
		return (error);

	if (uap->flags == CLOUDABI_FDSTAT_FLAGS) {
		/* Convert flags. */
		oflags = 0;
		if (fsb.fs_flags & CLOUDABI_FDFLAG_APPEND)
			oflags |= O_APPEND;
		if (fsb.fs_flags & CLOUDABI_FDFLAG_NONBLOCK)
			oflags |= O_NONBLOCK;
		if (fsb.fs_flags & (CLOUDABI_FDFLAG_SYNC |
		    CLOUDABI_FDFLAG_DSYNC | CLOUDABI_FDFLAG_RSYNC))
			oflags |= O_SYNC;
		return (kern_fcntl(td, uap->fd, F_SETFL, oflags));
	} else if (uap->flags == CLOUDABI_FDSTAT_RIGHTS) {
		/* Convert rights. */
		error = cloudabi_convert_rights(
		    fsb.fs_rights_base | fsb.fs_rights_inheriting, &rights);
		if (error != 0)
			return (error);
		return (kern_cap_rights_limit(td, uap->fd, &rights));
	}
	return (EINVAL);
}

int
cloudabi_sys_fd_sync(struct thread *td, struct cloudabi_sys_fd_sync_args *uap)
{

	return (kern_fsync(td, uap->fd, true));
}
