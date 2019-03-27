/*-
 * Copyright (c) 2007 Pawel Jakub Dawidek <pjd@FreeBSD.org>
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHORS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHORS OR CONTRIBUTORS BE LIABLE
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

#ifndef _OPENSOLARIS_SYS_MUTEX_H_
#define	_OPENSOLARIS_SYS_MUTEX_H_

#ifdef _KERNEL

#include <sys/param.h>
#include <sys/lock.h>
#include_next <sys/mutex.h>
#include <sys/proc.h>
#include <sys/sx.h>

typedef enum {
	MUTEX_DEFAULT = 6	/* kernel default mutex */
} kmutex_type_t;

#define	MUTEX_HELD(x)		(mutex_owned(x))
#define	MUTEX_NOT_HELD(x)	(!mutex_owned(x) || panicstr)

typedef struct sx	kmutex_t;

#ifndef OPENSOLARIS_WITNESS
#define	MUTEX_FLAGS	(SX_DUPOK | SX_NEW | SX_NOWITNESS)
#else
#define	MUTEX_FLAGS	(SX_DUPOK | SX_NEW)
#endif

#define	mutex_init(lock, desc, type, arg)	do {			\
	const char *_name;						\
	ASSERT((type) == 0 || (type) == MUTEX_DEFAULT);			\
	KASSERT(((lock)->lock_object.lo_flags & LO_ALLMASK) !=		\
	    LO_EXPECTED, ("lock %s already initialized", #lock));	\
	for (_name = #lock; *_name != '\0'; _name++) {			\
		if (*_name >= 'a' && *_name <= 'z')			\
			break;						\
	}								\
	if (*_name == '\0')						\
		_name = #lock;						\
	sx_init_flags((lock), _name, MUTEX_FLAGS);			\
} while (0)
#define	mutex_destroy(lock)	sx_destroy(lock)
#define	mutex_enter(lock)	sx_xlock(lock)
#define	mutex_tryenter(lock)	sx_try_xlock(lock)
#define	mutex_exit(lock)	sx_xunlock(lock)
#define	mutex_owned(lock)	sx_xlocked(lock)
#define	mutex_owner(lock)	sx_xholder(lock)

#endif	/* _KERNEL */

#endif	/* _OPENSOLARIS_SYS_MUTEX_H_ */
