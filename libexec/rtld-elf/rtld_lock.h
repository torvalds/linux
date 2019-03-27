/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright 2003 Alexander Kabaev.
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
 *
 * $FreeBSD$
 */

#ifndef _RTLD_LOCK_H_
#define	_RTLD_LOCK_H_

#define	RTLI_VERSION	0x01
#define	MAX_RTLD_LOCKS	8

struct RtldLockInfo
{
	unsigned int rtli_version;
	void *(*lock_create)(void);
	void  (*lock_destroy)(void *);
	void  (*rlock_acquire)(void *);
	void  (*wlock_acquire)(void *);
	void  (*lock_release)(void *);
	int   (*thread_set_flag)(int);
	int   (*thread_clr_flag)(int);
	void  (*at_fork)(void);
};

extern void _rtld_thread_init(struct RtldLockInfo *) __exported;
extern void _rtld_atfork_pre(int *) __exported;
extern void _rtld_atfork_post(int *) __exported;

#ifdef IN_RTLD

struct rtld_lock;
typedef struct rtld_lock *rtld_lock_t;

extern rtld_lock_t	rtld_bind_lock;
extern rtld_lock_t	rtld_libc_lock;
extern rtld_lock_t	rtld_phdr_lock;

#define	RTLD_LOCK_UNLOCKED	0
#define	RTLD_LOCK_RLOCKED	1
#define	RTLD_LOCK_WLOCKED	2

struct Struct_RtldLockState;
typedef struct Struct_RtldLockState RtldLockState;

void	rlock_acquire(rtld_lock_t, RtldLockState *);
void 	wlock_acquire(rtld_lock_t, RtldLockState *);
void	lock_release(rtld_lock_t, RtldLockState *);
void	lock_upgrade(rtld_lock_t, RtldLockState *);
void	lock_restart_for_upgrade(RtldLockState *);

#endif	/* IN_RTLD */

#endif
