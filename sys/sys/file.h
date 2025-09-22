/*	$OpenBSD: file.h,v 1.66 2022/06/20 01:39:44 visa Exp $	*/
/*	$NetBSD: file.h,v 1.11 1995/03/26 20:24:13 jtc Exp $	*/

/*
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
 *	@(#)file.h	8.2 (Berkeley) 8/20/94
 */

#ifndef _KERNEL
#include <sys/fcntl.h>

#else /* _KERNEL */
#include <sys/queue.h>
#include <sys/mutex.h>
#endif /* _KERNEL */

#define	DTYPE_VNODE	1	/* file */
#define	DTYPE_SOCKET	2	/* communications endpoint */
#define	DTYPE_PIPE	3	/* pipe */
#define	DTYPE_KQUEUE	4	/* event queue */
#define	DTYPE_DMABUF	5	/* DMA buffer (for DRM) */
#define	DTYPE_SYNC	6	/* sync file (for DRM) */

#ifdef _KERNEL
struct proc;
struct uio;
struct knote;
struct stat;
struct file;
struct ucred;

/**
 * File operations.
 * The following entries could be called without KERNEL_LOCK hold:
 * - fo_read
 * - fo_write
 * - fo_close
 */
struct	fileops {
	int	(*fo_read)(struct file *, struct uio *, int);
	int	(*fo_write)(struct file *, struct uio *, int);
	int	(*fo_ioctl)(struct file *, u_long, caddr_t, struct proc *);
	int	(*fo_kqfilter)(struct file *, struct knote *);
	int	(*fo_stat)(struct file *, struct stat *, struct proc *);
	int	(*fo_close)(struct file *, struct proc *);
	int	(*fo_seek)(struct file *, off_t *, int, struct proc *);
};
#define FO_POSITION	0x00000001	/* positioned read/write */

/*
 * Kernel descriptor table.
 * One entry for each open kernel vnode and socket.
 *
 *  Locks used to protect struct members in this file:
 *	I	immutable after creation
 *	F	global `fhdlk' mutex
 *	a	atomic operations
 *	f	per file `f_mtx'
 *	v	vnode lock
 */
struct file {
	LIST_ENTRY(file) f_list;/* [F] list of active files */
	struct mutex f_mtx;
	u_int	f_flag;		/* [a] see fcntl.h */
	u_int	f_iflags;	/* [a] internal flags */
	int	f_type;		/* [I] descriptor type */
	u_int	f_count;	/* [a] reference count */
	struct	ucred *f_cred;	/* [I] credentials associated with descriptor */
	const struct fileops *f_ops; /* [I] file operation pointers */
	off_t	f_offset;	/* [f,v] offset */
	void 	*f_data;	/* [I] private data */
	uint64_t f_rxfer;	/* [f] total number of read transfers */
	uint64_t f_wxfer;	/* [f] total number of write transfers */
	uint64_t f_seek;	/* [f] total independent seek operations */
	uint64_t f_rbytes;	/* [f] total bytes read */
	uint64_t f_wbytes;	/* [f] total bytes written */
};

#define FIF_HASLOCK		0x01	/* descriptor holds advisory lock */
#define FIF_INSERTED		0x80	/* present in `filehead' */

#define FREF(fp) \
	do { \
		extern void vfs_stall_barrier(void); \
		vfs_stall_barrier(); \
		atomic_inc_int(&(fp)->f_count); \
	} while (0)

#define FRELE(fp,p) \
	(atomic_dec_int_nv(&fp->f_count) == 0 ? fdrop(fp, p) : 0)

#define FDUP_MAX_COUNT		(UINT_MAX - 2 * MAXCPUS)

int	fdrop(struct file *, struct proc *);

static inline off_t
foffset(struct file *fp)
{
	off_t offset;

	mtx_enter(&fp->f_mtx);
	offset = fp->f_offset;
	mtx_leave(&fp->f_mtx);
	return (offset);
}

LIST_HEAD(filelist, file);
extern int maxfiles;			/* kernel limit on number of open files */
extern int numfiles;			/* actual number of open files */
extern const struct fileops socketops;	/* socket operations for files */
extern const struct fileops vnops;	/* vnode operations for files */

#endif /* _KERNEL */
