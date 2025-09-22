/*	$OpenBSD: uio.h,v 1.20 2024/10/26 05:39:03 jsg Exp $	*/
/*	$NetBSD: uio.h,v 1.12 1996/02/09 18:25:45 christos Exp $	*/

/*
 * Copyright (c) 1982, 1986, 1993, 1994
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
 *	@(#)uio.h	8.5 (Berkeley) 2/22/94
 */

#ifndef _SYS_UIO_H_
#define	_SYS_UIO_H_

#include <sys/cdefs.h>
#include <sys/_types.h>

#ifndef	_SIZE_T_DEFINED_
#define	_SIZE_T_DEFINED_
typedef	__size_t	size_t;
#endif

#ifndef	_SSIZE_T_DEFINED_
#define	_SSIZE_T_DEFINED_
typedef	__ssize_t	ssize_t;
#endif

struct iovec {
	void	*iov_base;	/* Base address. */
	size_t	 iov_len;	/* Length. */
};

#if __BSD_VISIBLE	/* needed by kdump */
enum	uio_rw { UIO_READ, UIO_WRITE };

/* Segment flag values. */
enum uio_seg {
	UIO_USERSPACE,		/* from user data space */
	UIO_SYSSPACE		/* from system space */
};
#endif /* __BSD_VISIBLE */

#ifdef _KERNEL
struct uio {
	struct	iovec *uio_iov;	/* pointer to array of iovecs */
	int	uio_iovcnt;	/* number of iovecs in array */
	off_t	uio_offset;	/* offset into file this uio corresponds to */
	size_t	uio_resid;	/* residual i/o count */
	enum	uio_seg uio_segflg; /* see above */
	enum	uio_rw uio_rw;	/* see above */
	struct	proc *uio_procp;/* associated thread or NULL */
};

/*
 * Limits
 */
#define UIO_SMALLIOV	8		/* 8 on stack, else malloc */
#endif /* _KERNEL */

#if __BSD_VISIBLE
#define UIO_MAXIOV	1024		/* Deprecated, use IOV_MAX instead */
#endif

#ifndef	_KERNEL
__BEGIN_DECLS
#if __BSD_VISIBLE
ssize_t preadv(int, const struct iovec *, int, __off_t);
ssize_t pwritev(int, const struct iovec *, int, __off_t);
#endif /* __BSD_VISIBLE */
ssize_t	readv(int, const struct iovec *, int);
ssize_t	writev(int, const struct iovec *, int);
__END_DECLS
#else
int	ureadc(int c, struct uio *);

int	iovec_copyin(const struct iovec *, struct iovec **, struct iovec *,
	    unsigned int, size_t *);
void	iovec_free(struct iovec *, unsigned int );
int	dofilereadv(struct proc *, int, struct uio *, int, register_t *);
int	dofilewritev(struct proc *, int, struct uio *, int, register_t *);

#endif /* !_KERNEL */

#endif /* !_SYS_UIO_H_ */
