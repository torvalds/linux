/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2010, David Xu <davidxu@freebsd.org>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice unmodified, this list of conditions, and the following
 *    disclaimer.
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
 *
 * $FreeBSD$
 *
 */

#ifndef _SYS__UMTX_H_
#define	_SYS__UMTX_H_

#include <sys/_types.h>
#include <sys/_timespec.h>

struct umutex {
	volatile __lwpid_t	m_owner;	/* Owner of the mutex */
	__uint32_t		m_flags;	/* Flags of the mutex */
	__uint32_t		m_ceilings[2];	/* Priority protect ceiling */
	__uintptr_t		m_rb_lnk;	/* Robust linkage */
#ifndef __LP64__
	__uint32_t		m_pad;
#endif
	__uint32_t		m_spare[2];
};

struct ucond {
	volatile __uint32_t	c_has_waiters;	/* Has waiters in kernel */
	__uint32_t		c_flags;	/* Flags of the condition variable */
	__uint32_t              c_clockid;	/* Clock id */
	__uint32_t              c_spare[1];	/* Spare space */
};

struct urwlock {
	volatile __int32_t	rw_state;
	__uint32_t		rw_flags;
	__uint32_t		rw_blocked_readers;
	__uint32_t		rw_blocked_writers;
	__uint32_t		rw_spare[4];
};

struct _usem {
	volatile __uint32_t	_has_waiters;
	volatile __uint32_t	_count;
	__uint32_t		_flags;
};

struct _usem2 {
	volatile __uint32_t	_count;		/* Waiters flag in high bit. */
	__uint32_t		_flags;
};

struct _umtx_time {
	struct timespec		_timeout;
	__uint32_t		_flags;
	__uint32_t		_clockid;
};

#endif /* !_SYS__UMTX_H_ */
