/*	$OpenBSD: mutex.h,v 1.10 2024/05/16 09:30:03 kettenis Exp $	*/

/*
 * Copyright (c) 2004 Artur Grabowski <art@openbsd.org>
 * All rights reserved. 
 *
 * Redistribution and use in source and binary forms, with or without 
 * modification, are permitted provided that the following conditions 
 * are met: 
 *
 * 1. Redistributions of source code must retain the above copyright 
 *    notice, this list of conditions and the following disclaimer. 
 * 2. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission. 
 *
 * THIS SOFTWARE IS PROVIDED ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES,
 * INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY
 * AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL
 * THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL  DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE. 
 */

#ifndef _MACHINE_MUTEX_H_
#define _MACHINE_MUTEX_H_

#include <sys/_lock.h>

#define	MUTEX_UNLOCKED	{ 1, 1, 1, 1 }

/* Note: mtx_lock must be 16-byte aligned. */
struct mutex {
#ifdef MULTIPROCESSOR
	volatile int mtx_lock[4];
#endif
	int mtx_wantipl;
	int mtx_oldipl;
	volatile void *mtx_owner;
#ifdef WITNESS
	struct lock_object mtx_lock_obj;
#endif
};

#ifdef MULTIPROCESSOR
#ifdef WITNESS
#define MUTEX_INITIALIZER_FLAGS(ipl, name, flags) \
	{ MUTEX_UNLOCKED, __MUTEX_IPL((ipl)), 0, NULL, \
	  MTX_LO_INITIALIZER(name, flags) }
#else /* WITNESS */
#define MUTEX_INITIALIZER_FLAGS(ipl, name, flags) \
	{ MUTEX_UNLOCKED, __MUTEX_IPL((ipl)), 0, NULL }
#endif /* WITNESS */
#else /* MULTIPROCESSOR */
#define MUTEX_INITIALIZER_FLAGS(ipl, name, flags) \
	{ __MUTEX_IPL((ipl)), 0, NULL }
#endif /* MULTIPROCESSOR */

void __mtx_init(struct mutex *, int);
#define _mtx_init(mtx, ipl) __mtx_init((mtx), __MUTEX_IPL((ipl)))

#ifdef DIAGNOSTIC
#define MUTEX_ASSERT_LOCKED(mtx) do {					\
	if ((mtx)->mtx_owner != curcpu())				\
		panic("mutex %p not held in %s", (mtx), __func__);	\
} while (0)

#define MUTEX_ASSERT_UNLOCKED(mtx) do {					\
	if ((mtx)->mtx_owner == curcpu())				\
		panic("mutex %p held in %s", (mtx), __func__);		\
} while (0)
#else
#define MUTEX_ASSERT_LOCKED(mtx) do { } while (0)
#define MUTEX_ASSERT_UNLOCKED(mtx) do { } while (0)
#endif

#define MUTEX_LOCK_OBJECT(mtx)	(&(mtx)->mtx_lock_obj)
#define MUTEX_OLDIPL(mtx)	(mtx)->mtx_oldipl

#endif
