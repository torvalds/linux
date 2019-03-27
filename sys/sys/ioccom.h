/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1982, 1986, 1990, 1993, 1994
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
 *	@(#)ioccom.h	8.2 (Berkeley) 3/28/94
 * $FreeBSD$
 */

#ifndef	_SYS_IOCCOM_H_
#define	_SYS_IOCCOM_H_

/*
 * Ioctl's have the command encoded in the lower word, and the size of
 * any in or out parameters in the upper word.  The high 3 bits of the
 * upper word are used to encode the in/out status of the parameter.
 */
#define	IOCPARM_SHIFT	13		/* number of bits for ioctl size */
#define	IOCPARM_MASK	((1 << IOCPARM_SHIFT) - 1) /* parameter length mask */
#define	IOCPARM_LEN(x)	(((x) >> 16) & IOCPARM_MASK)
#define	IOCBASECMD(x)	((x) & ~(IOCPARM_MASK << 16))
#define	IOCGROUP(x)	(((x) >> 8) & 0xff)

#define	IOCPARM_MAX	(1 << IOCPARM_SHIFT) /* max size of ioctl */
#define	IOC_VOID	0x20000000	/* no parameters */
#define	IOC_OUT		0x40000000	/* copy out parameters */
#define	IOC_IN		0x80000000	/* copy in parameters */
#define	IOC_INOUT	(IOC_IN|IOC_OUT)
#define	IOC_DIRMASK	(IOC_VOID|IOC_OUT|IOC_IN)

#define	_IOC(inout,group,num,len)	((unsigned long) \
	((inout) | (((len) & IOCPARM_MASK) << 16) | ((group) << 8) | (num)))
#define	_IO(g,n)	_IOC(IOC_VOID,	(g), (n), 0)
#define	_IOWINT(g,n)	_IOC(IOC_VOID,	(g), (n), sizeof(int))
#define	_IOR(g,n,t)	_IOC(IOC_OUT,	(g), (n), sizeof(t))
#define	_IOW(g,n,t)	_IOC(IOC_IN,	(g), (n), sizeof(t))
/* this should be _IORW, but stdio got there first */
#define	_IOWR(g,n,t)	_IOC(IOC_INOUT,	(g), (n), sizeof(t))
/* Replace length/type in an ioctl command. */
#define	_IOC_NEWLEN(ioc, len) \
    (((~(IOCPARM_MASK << 16)) & (ioc)) | (((len) & IOCPARM_MASK) << 16))
#define	_IOC_NEWTYPE(ioc, type)	_IOC_NEWLEN((ioc), sizeof(type))

#ifdef _KERNEL

#if defined(COMPAT_FREEBSD6) || defined(COMPAT_FREEBSD5) || \
    defined(COMPAT_FREEBSD4) || defined(COMPAT_43)
#define	IOCPARM_IVAL(x)	((int)(intptr_t)(void *)*(caddr_t *)(void *)(x))
#endif

#else

#include <sys/cdefs.h>

__BEGIN_DECLS
int	ioctl(int, unsigned long, ...);
__END_DECLS

#endif

#endif /* !_SYS_IOCCOM_H_ */
