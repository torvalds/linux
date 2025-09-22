/*	$OpenBSD: errno.h,v 1.25 2017/09/05 03:06:26 jsg Exp $	*/
/*	$NetBSD: errno.h,v 1.10 1996/01/20 01:33:53 jtc Exp $	*/

/*
 * Copyright (c) 1982, 1986, 1989, 1993
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
 *	@(#)errno.h	8.5 (Berkeley) 1/21/94
 */

#include <sys/cdefs.h>

#define EPERM		1	/* Operation not permitted */
#define ENOENT		2	/* No such file or directory */
#define ESRCH		3	/* No such process */
#define EINTR		4	/* Interrupted system call */
#define EIO		5	/* Input/output error */
#define ENXIO		6	/* Device not configured */
#define E2BIG		7	/* Argument list too long */
#define ENOEXEC		8	/* Exec format error */
#define EBADF		9	/* Bad file descriptor */
#define ECHILD		10	/* No child processes */
#define EDEADLK		11	/* Resource deadlock avoided */
				/* 11 was EAGAIN */
#define ENOMEM		12	/* Cannot allocate memory */
#define EACCES		13	/* Permission denied */
#define EFAULT		14	/* Bad address */
#if __BSD_VISIBLE
#define ENOTBLK		15	/* Block device required */
#endif
#define EBUSY		16	/* Device busy */
#define EEXIST		17	/* File exists */
#define EXDEV		18	/* Cross-device link */
#define ENODEV		19	/* Operation not supported by device */
#define ENOTDIR		20	/* Not a directory */
#define EISDIR		21	/* Is a directory */
#define EINVAL		22	/* Invalid argument */
#define ENFILE		23	/* Too many open files in system */
#define EMFILE		24	/* Too many open files */
#define ENOTTY		25	/* Inappropriate ioctl for device */
#define ETXTBSY		26	/* Text file busy */
#define EFBIG		27	/* File too large */
#define ENOSPC		28	/* No space left on device */
#define ESPIPE		29	/* Illegal seek */
#define EROFS		30	/* Read-only file system */
#define EMLINK		31	/* Too many links */
#define EPIPE		32	/* Broken pipe */

/* math software */
#define EDOM		33	/* Numerical argument out of domain */
#define ERANGE		34	/* Result too large */

/* non-blocking and interrupt i/o */
#define EAGAIN		35	/* Resource temporarily unavailable */
#define EWOULDBLOCK	EAGAIN	/* Operation would block */
#define EINPROGRESS	36	/* Operation now in progress */
#define EALREADY	37	/* Operation already in progress */

/* ipc/network software -- argument errors */
#define ENOTSOCK	38	/* Socket operation on non-socket */
#define EDESTADDRREQ	39	/* Destination address required */
#define EMSGSIZE	40	/* Message too long */
#define EPROTOTYPE	41	/* Protocol wrong type for socket */
#define ENOPROTOOPT	42	/* Protocol not available */
#define EPROTONOSUPPORT	43	/* Protocol not supported */
#if __BSD_VISIBLE
#define ESOCKTNOSUPPORT	44	/* Socket type not supported */
#endif
#define EOPNOTSUPP	45	/* Operation not supported */
#if __BSD_VISIBLE
#define EPFNOSUPPORT	46	/* Protocol family not supported */
#endif
#define EAFNOSUPPORT	47	/* Address family not supported by protocol family */
#define EADDRINUSE	48	/* Address already in use */
#define EADDRNOTAVAIL	49	/* Can't assign requested address */

/* ipc/network software -- operational errors */
#define ENETDOWN	50	/* Network is down */
#define ENETUNREACH	51	/* Network is unreachable */
#define ENETRESET	52	/* Network dropped connection on reset */
#define ECONNABORTED	53	/* Software caused connection abort */
#define ECONNRESET	54	/* Connection reset by peer */
#define ENOBUFS		55	/* No buffer space available */
#define EISCONN		56	/* Socket is already connected */
#define ENOTCONN	57	/* Socket is not connected */
#if __BSD_VISIBLE
#define ESHUTDOWN	58	/* Can't send after socket shutdown */
#define ETOOMANYREFS	59	/* Too many references: can't splice */
#endif /* __BSD_VISIBLE */
#define ETIMEDOUT	60	/* Operation timed out */
#define ECONNREFUSED	61	/* Connection refused */

#define ELOOP		62	/* Too many levels of symbolic links */
#define ENAMETOOLONG	63	/* File name too long */

/* should be rearranged */
#if __BSD_VISIBLE
#define EHOSTDOWN	64	/* Host is down */
#endif /* __BSD_VISIBLE */
#define EHOSTUNREACH	65	/* No route to host */
#define ENOTEMPTY	66	/* Directory not empty */

/* quotas & mush */
#if __BSD_VISIBLE
#define EPROCLIM	67	/* Too many processes */
#define EUSERS		68	/* Too many users */
#endif /* __BSD_VISIBLE */
#define EDQUOT		69	/* Disk quota exceeded */

/* Network File System */
#define ESTALE		70	/* Stale NFS file handle */
#if __BSD_VISIBLE
#define EREMOTE		71	/* Too many levels of remote in path */
#define EBADRPC		72	/* RPC struct is bad */
#define ERPCMISMATCH	73	/* RPC version wrong */
#define EPROGUNAVAIL	74	/* RPC program not available */
#define EPROGMISMATCH	75	/* Program version wrong */
#define EPROCUNAVAIL	76	/* Bad procedure for program */
#endif /* __BSD_VISIBLE */

#define ENOLCK		77	/* No locks available */
#define ENOSYS		78	/* Function not implemented */

#if __BSD_VISIBLE
#define EFTYPE		79	/* Inappropriate file type or format */
#define EAUTH		80	/* Authentication error */
#define ENEEDAUTH	81	/* Need authenticator */
#define EIPSEC		82	/* IPsec processing failure */
#define ENOATTR		83	/* Attribute not found */
#endif /* __BSD_VISIBLE */
#define EILSEQ		84	/* Illegal byte sequence */
#if __BSD_VISIBLE
#define ENOMEDIUM	85	/* No medium found */
#define EMEDIUMTYPE	86	/* Wrong medium type */
#endif /* __BSD_VISIBLE */
#define EOVERFLOW	87	/* Value too large to be stored in data type */
#define ECANCELED	88	/* Operation canceled */
#define EIDRM		89	/* Identifier removed */
#define ENOMSG		90	/* No message of desired type */
#define ENOTSUP		91	/* Not supported */
#define EBADMSG		92	/* Bad message */
#define ENOTRECOVERABLE	93	/* State not recoverable */
#define EOWNERDEAD	94	/* Previous owner died */
#define EPROTO		95	/* Protocol error */
#if __BSD_VISIBLE
#define ELAST		95	/* Must be equal largest errno */
#endif /* __BSD_VISIBLE */

#ifdef _KERNEL
/* pseudo-errors returned inside kernel to modify return to process */
#define ERESTART	-1	/* restart syscall */
#define EJUSTRETURN	-2	/* don't modify regs, just return */
#endif
