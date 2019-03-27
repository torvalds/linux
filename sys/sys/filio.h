/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1982, 1986, 1990, 1993, 1994
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
 *	@(#)filio.h	8.1 (Berkeley) 3/28/94
 * $FreeBSD$
 */

#ifndef	_SYS_FILIO_H_
#define	_SYS_FILIO_H_

#include <sys/ioccom.h>

/* Generic file-descriptor ioctl's. */
#define	FIOCLEX		 _IO('f', 1)		/* set close on exec on fd */
#define	FIONCLEX	 _IO('f', 2)		/* remove close on exec */
#define	FIONREAD	_IOR('f', 127, int)	/* get # bytes to read */
#define	FIONBIO		_IOW('f', 126, int)	/* set/clear non-blocking i/o */
#define	FIOASYNC	_IOW('f', 125, int)	/* set/clear async i/o */
#define	FIOSETOWN	_IOW('f', 124, int)	/* set owner */
#define	FIOGETOWN	_IOR('f', 123, int)	/* get owner */
#define	FIODTYPE	_IOR('f', 122, int)	/* get d_flags type part */
#define	FIOGETLBA	_IOR('f', 121, int)	/* get start blk # */
struct fiodgname_arg {
	int	len;
	void	*buf;
};
#define	FIODGNAME	_IOW('f', 120, struct fiodgname_arg) /* get dev. name */
#define	FIONWRITE	_IOR('f', 119, int)	/* get # bytes (yet) to write */
#define	FIONSPACE	_IOR('f', 118, int)	/* get space in send queue */
/* Handle lseek SEEK_DATA and SEEK_HOLE for holey file knowledge. */
#define	FIOSEEKDATA	_IOWR('f', 97, off_t)	/* SEEK_DATA */
#define	FIOSEEKHOLE	_IOWR('f', 98, off_t)	/* SEEK_HOLE */

#ifdef _KERNEL
#ifdef COMPAT_FREEBSD32
struct fiodgname_arg32 {
	int		len;
	uint32_t	buf;	/* (void *) */
};
#define	FIODGNAME_32	_IOC_NEWTYPE(FIODGNAME, struct fiodgname_arg32)
#endif

void	*fiodgname_buf_get_ptr(void *fgnp, u_long com);
#endif

#endif /* !_SYS_FILIO_H_ */
