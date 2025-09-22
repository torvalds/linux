/*	$OpenBSD: poll.h,v 1.16 2024/08/04 22:28:08 guenther Exp $ */

/*
 * Copyright (c) 1996 Theo de Raadt
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef	_SYS_POLL_H_
#define	_SYS_POLL_H_

typedef struct pollfd {
	int 	fd;
	short	events;
	short	revents;
} pollfd_t;

typedef unsigned int	nfds_t;

#define	POLLIN		0x0001
#define	POLLPRI		0x0002
#define	POLLOUT		0x0004
#define	POLLERR		0x0008
#define	POLLHUP		0x0010
#define	POLLNVAL	0x0020
#define	POLLRDNORM	0x0040
#define POLLNORM	POLLRDNORM
#define POLLWRNORM      POLLOUT
#define	POLLRDBAND	0x0080
#define	POLLWRBAND	0x0100
#ifdef _KERNEL
#define	POLL_NOHUP	0x1000		/* internal use only */
#endif

#define INFTIM		(-1)

#ifndef _KERNEL
#include <sys/cdefs.h>

#if __POSIX_VISIBLE >= 202405 || __BSD_VISIBLE
#include <sys/_types.h>

#ifndef _SIGSET_T_DEFINED_
#define _SIGSET_T_DEFINED_
typedef unsigned int	sigset_t;
#endif

#ifndef _TIME_T_DEFINED_
#define _TIME_T_DEFINED_
typedef __time_t	time_t;
#endif

#ifndef _TIMESPEC_DECLARED
#define _TIMESPEC_DECLARED
struct timespec {
	time_t	tv_sec;		/* seconds */
	long	tv_nsec;	/* and nanoseconds */
};
#endif
#endif /* __BSD_VISIBLE */

__BEGIN_DECLS
int   poll(struct pollfd[], nfds_t, int);
#if __POSIX_VISIBLE >= 202405 || __BSD_VISIBLE
int   ppoll(struct pollfd[], nfds_t, const struct timespec * __restrict, const sigset_t * __restrict);
#endif /* __BSD_VISIBLE */
__END_DECLS
#endif /* _KERNEL */

#endif /* !_SYS_POLL_H_ */
