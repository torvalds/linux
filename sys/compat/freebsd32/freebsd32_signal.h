/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2006 David Xu <davidxu@freebsd.org>
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
 *
 * $FreeBSD$
 */

#ifndef _COMPAT_FREEBSD32_SIGNAL_H_
#define _COMPAT_FREEBSD32_SIGNAL_H_

struct sigaltstack32 {
	u_int32_t	ss_sp;		/* signal stack base */
	u_int32_t	ss_size;	/* signal stack length */
	int		ss_flags;	/* SS_DISABLE and/or SS_ONSTACK */
};

struct osigevent32 {
	int	sigev_notify;		/* Notification type */
	union {
		int	__sigev_signo;	/* Signal number */
		int	__sigev_notify_kqueue;
	} __sigev_u;
	union sigval32 sigev_value;	/* Signal value */
};

struct sigevent32 {
	int	sigev_notify;		/* Notification type */
	int	sigev_signo;		/* Signal number */
	union sigval32 sigev_value;	/* Signal value */
	union {
		__lwpid_t	_threadid;
		struct {
			uint32_t _function;
			uint32_t _attribute;
		} _sigev_thread;
		unsigned short	_kevent_flags;
		uint32_t __spare__[8];
	} _sigev_un;
};

struct sigevent;
int convert_sigevent32(struct sigevent32 *sig32, struct sigevent *sig);
void siginfo_to_siginfo32(const siginfo_t *src, struct siginfo32 *dst);

#endif /* !_COMPAT_FREEBSD32_SIGNAL_H_ */
