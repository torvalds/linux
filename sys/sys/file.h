/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1982, 1986, 1989, 1993
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
 *	@(#)file.h	8.3 (Berkeley) 1/9/95
 * $FreeBSD$
 */

#ifndef _SYS_FILE_H_
#define	_SYS_FILE_H_

#ifndef _KERNEL
#include <sys/types.h> /* XXX */
#include <sys/fcntl.h>
#include <sys/unistd.h>
#else
#include <sys/queue.h>
#include <sys/refcount.h>
#include <sys/_lock.h>
#include <sys/_mutex.h>
#include <vm/vm.h>

struct filedesc;
struct stat;
struct thread;
struct uio;
struct knote;
struct vnode;

#endif /* _KERNEL */

#define	DTYPE_NONE	0	/* not yet initialized */
#define	DTYPE_VNODE	1	/* file */
#define	DTYPE_SOCKET	2	/* communications endpoint */
#define	DTYPE_PIPE	3	/* pipe */
#define	DTYPE_FIFO	4	/* fifo (named pipe) */
#define	DTYPE_KQUEUE	5	/* event queue */
#define	DTYPE_CRYPTO	6	/* crypto */
#define	DTYPE_MQUEUE	7	/* posix message queue */
#define	DTYPE_SHM	8	/* swap-backed shared memory */
#define	DTYPE_SEM	9	/* posix semaphore */
#define	DTYPE_PTS	10	/* pseudo teletype master device */
#define	DTYPE_DEV	11	/* Device specific fd type */
#define	DTYPE_PROCDESC	12	/* process descriptor */
#define	DTYPE_LINUXEFD	13	/* emulation eventfd type */
#define	DTYPE_LINUXTFD	14	/* emulation timerfd type */

#ifdef _KERNEL

struct file;
struct filecaps;
struct kaiocb;
struct kinfo_file;
struct ucred;

#define	FOF_OFFSET	0x01	/* Use the offset in uio argument */
#define	FOF_NOLOCK	0x02	/* Do not take FOFFSET_LOCK */
#define	FOF_NEXTOFF	0x04	/* Also update f_nextoff */
#define	FOF_NOUPDATE	0x10	/* Do not update f_offset */
off_t foffset_lock(struct file *fp, int flags);
void foffset_lock_uio(struct file *fp, struct uio *uio, int flags);
void foffset_unlock(struct file *fp, off_t val, int flags);
void foffset_unlock_uio(struct file *fp, struct uio *uio, int flags);

static inline off_t
foffset_get(struct file *fp)
{

	return (foffset_lock(fp, FOF_NOLOCK));
}

typedef int fo_rdwr_t(struct file *fp, struct uio *uio,
		    struct ucred *active_cred, int flags,
		    struct thread *td);
typedef	int fo_truncate_t(struct file *fp, off_t length,
		    struct ucred *active_cred, struct thread *td);
typedef	int fo_ioctl_t(struct file *fp, u_long com, void *data,
		    struct ucred *active_cred, struct thread *td);
typedef	int fo_poll_t(struct file *fp, int events,
		    struct ucred *active_cred, struct thread *td);
typedef	int fo_kqfilter_t(struct file *fp, struct knote *kn);
typedef	int fo_stat_t(struct file *fp, struct stat *sb,
		    struct ucred *active_cred, struct thread *td);
typedef	int fo_close_t(struct file *fp, struct thread *td);
typedef	int fo_chmod_t(struct file *fp, mode_t mode,
		    struct ucred *active_cred, struct thread *td);
typedef	int fo_chown_t(struct file *fp, uid_t uid, gid_t gid,
		    struct ucred *active_cred, struct thread *td);
typedef int fo_sendfile_t(struct file *fp, int sockfd, struct uio *hdr_uio,
		    struct uio *trl_uio, off_t offset, size_t nbytes,
		    off_t *sent, int flags, struct thread *td);
typedef int fo_seek_t(struct file *fp, off_t offset, int whence,
		    struct thread *td);
typedef int fo_fill_kinfo_t(struct file *fp, struct kinfo_file *kif,
		    struct filedesc *fdp);
typedef int fo_mmap_t(struct file *fp, vm_map_t map, vm_offset_t *addr,
		    vm_size_t size, vm_prot_t prot, vm_prot_t cap_maxprot,
		    int flags, vm_ooffset_t foff, struct thread *td);
typedef int fo_aio_queue_t(struct file *fp, struct kaiocb *job);
typedef	int fo_flags_t;

struct fileops {
	fo_rdwr_t	*fo_read;
	fo_rdwr_t	*fo_write;
	fo_truncate_t	*fo_truncate;
	fo_ioctl_t	*fo_ioctl;
	fo_poll_t	*fo_poll;
	fo_kqfilter_t	*fo_kqfilter;
	fo_stat_t	*fo_stat;
	fo_close_t	*fo_close;
	fo_chmod_t	*fo_chmod;
	fo_chown_t	*fo_chown;
	fo_sendfile_t	*fo_sendfile;
	fo_seek_t	*fo_seek;
	fo_fill_kinfo_t	*fo_fill_kinfo;
	fo_mmap_t	*fo_mmap;
	fo_aio_queue_t	*fo_aio_queue;
	fo_flags_t	fo_flags;	/* DFLAG_* below */
};

#define DFLAG_PASSABLE	0x01	/* may be passed via unix sockets. */
#define DFLAG_SEEKABLE	0x02	/* seekable / nonsequential */
#endif /* _KERNEL */

#if defined(_KERNEL) || defined(_WANT_FILE)
/*
 * Kernel descriptor table.
 * One entry for each open kernel vnode and socket.
 *
 * Below is the list of locks that protects members in struct file.
 *
 * (a) f_vnode lock required (shared allows both reads and writes)
 * (f) protected with mtx_lock(mtx_pool_find(fp))
 * (d) cdevpriv_mtx
 * none	not locked
 */

struct fadvise_info {
	int		fa_advice;	/* (f) FADV_* type. */
	off_t		fa_start;	/* (f) Region start. */
	off_t		fa_end;		/* (f) Region end. */
};

struct file {
	void		*f_data;	/* file descriptor specific data */
	struct fileops	*f_ops;		/* File operations */
	struct ucred	*f_cred;	/* associated credentials. */
	struct vnode 	*f_vnode;	/* NULL or applicable vnode */
	short		f_type;		/* descriptor type */
	short		f_vnread_flags; /* (f) Sleep lock for f_offset */
	volatile u_int	f_flag;		/* see fcntl.h */
	volatile u_int 	f_count;	/* reference count */
	/*
	 *  DTYPE_VNODE specific fields.
	 */
	int		f_seqcount;	/* (a) Count of sequential accesses. */
	off_t		f_nextoff;	/* next expected read/write offset. */
	union {
		struct cdev_privdata *fvn_cdevpriv;
					/* (d) Private data for the cdev. */
		struct fadvise_info *fvn_advice;
	} f_vnun;
	/*
	 *  DFLAG_SEEKABLE specific fields
	 */
	off_t		f_offset;
	/*
	 * Mandatory Access control information.
	 */
	void		*f_label;	/* Place-holder for MAC label. */
};

#define	f_cdevpriv	f_vnun.fvn_cdevpriv
#define	f_advice	f_vnun.fvn_advice

#define	FOFFSET_LOCKED       0x1
#define	FOFFSET_LOCK_WAITING 0x2
#define	FDEVFS_VNODE	     0x4

#endif /* _KERNEL || _WANT_FILE */

/*
 * Userland version of struct file, for sysctl
 */
struct xfile {
	ksize_t	xf_size;	/* size of struct xfile */
	pid_t	xf_pid;		/* owning process */
	uid_t	xf_uid;		/* effective uid of owning process */
	int	xf_fd;		/* descriptor number */
	int	_xf_int_pad1;
	kvaddr_t xf_file;	/* address of struct file */
	short	xf_type;	/* descriptor type */
	short	_xf_short_pad1;
	int	xf_count;	/* reference count */
	int	xf_msgcount;	/* references from message queue */
	int	_xf_int_pad2;
	off_t	xf_offset;	/* file offset */
	kvaddr_t xf_data;	/* file descriptor specific data */
	kvaddr_t xf_vnode;	/* vnode pointer */
	u_int	xf_flag;	/* flags (see fcntl.h) */
	int	_xf_int_pad3;
	int64_t	_xf_int64_pad[6];
};

#ifdef _KERNEL

extern struct fileops vnops;
extern struct fileops badfileops;
extern struct fileops socketops;
extern int maxfiles;		/* kernel limit on number of open files */
extern int maxfilesperproc;	/* per process limit on number of open files */
extern volatile int openfiles;	/* actual number of open files */

int fget(struct thread *td, int fd, cap_rights_t *rightsp, struct file **fpp);
int fget_mmap(struct thread *td, int fd, cap_rights_t *rightsp,
    u_char *maxprotp, struct file **fpp);
int fget_read(struct thread *td, int fd, cap_rights_t *rightsp,
    struct file **fpp);
int fget_write(struct thread *td, int fd, cap_rights_t *rightsp,
    struct file **fpp);
int fget_fcntl(struct thread *td, int fd, cap_rights_t *rightsp,
    int needfcntl, struct file **fpp);
int _fdrop(struct file *fp, struct thread *td);

fo_rdwr_t	invfo_rdwr;
fo_truncate_t	invfo_truncate;
fo_ioctl_t	invfo_ioctl;
fo_poll_t	invfo_poll;
fo_kqfilter_t	invfo_kqfilter;
fo_chmod_t	invfo_chmod;
fo_chown_t	invfo_chown;
fo_sendfile_t	invfo_sendfile;

fo_sendfile_t	vn_sendfile;
fo_seek_t	vn_seek;
fo_fill_kinfo_t	vn_fill_kinfo;
int vn_fill_kinfo_vnode(struct vnode *vp, struct kinfo_file *kif);

void finit(struct file *, u_int, short, void *, struct fileops *);
int fgetvp(struct thread *td, int fd, cap_rights_t *rightsp,
    struct vnode **vpp);
int fgetvp_exec(struct thread *td, int fd, cap_rights_t *rightsp,
    struct vnode **vpp);
int fgetvp_rights(struct thread *td, int fd, cap_rights_t *needrightsp,
    struct filecaps *havecaps, struct vnode **vpp);
int fgetvp_read(struct thread *td, int fd, cap_rights_t *rightsp,
    struct vnode **vpp);
int fgetvp_write(struct thread *td, int fd, cap_rights_t *rightsp,
    struct vnode **vpp);

static __inline int
_fnoop(void)
{

	return (0);
}

#define	fhold(fp)							\
	(refcount_acquire(&(fp)->f_count))
#define	fdrop(fp, td)							\
	(refcount_release(&(fp)->f_count) ? _fdrop((fp), (td)) : _fnoop())

static __inline fo_rdwr_t	fo_read;
static __inline fo_rdwr_t	fo_write;
static __inline fo_truncate_t	fo_truncate;
static __inline fo_ioctl_t	fo_ioctl;
static __inline fo_poll_t	fo_poll;
static __inline fo_kqfilter_t	fo_kqfilter;
static __inline fo_stat_t	fo_stat;
static __inline fo_close_t	fo_close;
static __inline fo_chmod_t	fo_chmod;
static __inline fo_chown_t	fo_chown;
static __inline fo_sendfile_t	fo_sendfile;

static __inline int
fo_read(struct file *fp, struct uio *uio, struct ucred *active_cred,
    int flags, struct thread *td)
{

	return ((*fp->f_ops->fo_read)(fp, uio, active_cred, flags, td));
}

static __inline int
fo_write(struct file *fp, struct uio *uio, struct ucred *active_cred,
    int flags, struct thread *td)
{

	return ((*fp->f_ops->fo_write)(fp, uio, active_cred, flags, td));
}

static __inline int
fo_truncate(struct file *fp, off_t length, struct ucred *active_cred,
    struct thread *td)
{

	return ((*fp->f_ops->fo_truncate)(fp, length, active_cred, td));
}

static __inline int
fo_ioctl(struct file *fp, u_long com, void *data, struct ucred *active_cred,
    struct thread *td)
{

	return ((*fp->f_ops->fo_ioctl)(fp, com, data, active_cred, td));
}

static __inline int
fo_poll(struct file *fp, int events, struct ucred *active_cred,
    struct thread *td)
{

	return ((*fp->f_ops->fo_poll)(fp, events, active_cred, td));
}

static __inline int
fo_stat(struct file *fp, struct stat *sb, struct ucred *active_cred,
    struct thread *td)
{

	return ((*fp->f_ops->fo_stat)(fp, sb, active_cred, td));
}

static __inline int
fo_close(struct file *fp, struct thread *td)
{

	return ((*fp->f_ops->fo_close)(fp, td));
}

static __inline int
fo_kqfilter(struct file *fp, struct knote *kn)
{

	return ((*fp->f_ops->fo_kqfilter)(fp, kn));
}

static __inline int
fo_chmod(struct file *fp, mode_t mode, struct ucred *active_cred,
    struct thread *td)
{

	return ((*fp->f_ops->fo_chmod)(fp, mode, active_cred, td));
}

static __inline int
fo_chown(struct file *fp, uid_t uid, gid_t gid, struct ucred *active_cred,
    struct thread *td)
{

	return ((*fp->f_ops->fo_chown)(fp, uid, gid, active_cred, td));
}

static __inline int
fo_sendfile(struct file *fp, int sockfd, struct uio *hdr_uio,
    struct uio *trl_uio, off_t offset, size_t nbytes, off_t *sent, int flags,
    struct thread *td)
{

	return ((*fp->f_ops->fo_sendfile)(fp, sockfd, hdr_uio, trl_uio, offset,
	    nbytes, sent, flags, td));
}

static __inline int
fo_seek(struct file *fp, off_t offset, int whence, struct thread *td)
{

	return ((*fp->f_ops->fo_seek)(fp, offset, whence, td));
}

static __inline int
fo_fill_kinfo(struct file *fp, struct kinfo_file *kif, struct filedesc *fdp)
{

	return ((*fp->f_ops->fo_fill_kinfo)(fp, kif, fdp));
}

static __inline int
fo_mmap(struct file *fp, vm_map_t map, vm_offset_t *addr, vm_size_t size,
    vm_prot_t prot, vm_prot_t cap_maxprot, int flags, vm_ooffset_t foff,
    struct thread *td)
{

	if (fp->f_ops->fo_mmap == NULL)
		return (ENODEV);
	return ((*fp->f_ops->fo_mmap)(fp, map, addr, size, prot, cap_maxprot,
	    flags, foff, td));
}

static __inline int
fo_aio_queue(struct file *fp, struct kaiocb *job)
{

	return ((*fp->f_ops->fo_aio_queue)(fp, job));
}

#endif /* _KERNEL */

#endif /* !SYS_FILE_H */
