/*	$OpenBSD: un.h,v 1.14 2015/07/18 15:00:01 guenther Exp $	*/
/*	$NetBSD: un.h,v 1.11 1996/02/04 02:12:47 christos Exp $	*/

/*
 * Copyright (c) 1982, 1986, 1993
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
 *	@(#)un.h	8.1 (Berkeley) 6/2/93
 */

#ifndef _SYS_UN_H_
#define	_SYS_UN_H_

#include <sys/cdefs.h>
#include <sys/_types.h>

#ifndef	_SA_FAMILY_T_DEFINED_
#define	_SA_FAMILY_T_DEFINED_
typedef	__sa_family_t	sa_family_t;	/* sockaddr address family type */
#endif

/*
 * Definitions for UNIX IPC domain.
 */
struct	sockaddr_un {
	unsigned char	sun_len;	/* sockaddr len excluding NUL */
	sa_family_t	sun_family;	/* AF_UNIX */
	char	sun_path[104];		/* path name (gag) */
};

#ifndef _KERNEL
#if __BSD_VISIBLE

/* actual length of an initialized sockaddr_un */
#define SUN_LEN(su) \
	(sizeof(*(su)) - sizeof((su)->sun_path) + strlen((su)->sun_path))

#endif /* __BSD_VISIBLE */

#endif /* _KERNEL */
#endif /* !_SYS_UN_H_ */
