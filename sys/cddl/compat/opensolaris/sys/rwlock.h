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

#ifndef _OPENSOLARIS_SYS_RWLOCK_H_
#define	_OPENSOLARIS_SYS_RWLOCK_H_

#include <sys/param.h>
#include <sys/proc.h>
#include <sys/lock.h>
#include <sys/sx.h>

#ifdef _KERNEL

typedef enum {
	RW_DEFAULT = 4		/* kernel default rwlock */
} krw_type_t;

typedef enum {
	RW_WRITER,
	RW_READER
} krw_t;

typedef	struct sx	krwlock_t;

#ifndef OPENSOLARIS_WITNESS
#define	RW_FLAGS	(SX_DUPOK | SX_NOWITNESS)
#else
#define	RW_FLAGS	(SX_DUPOK)
#endif

#define	RW_READ_HELD(x)		(rw_read_held((x)))
#define	RW_WRITE_HELD(x)	(rw_write_held((x)))
#define	RW_LOCK_HELD(x)		(rw_lock_held((x)))
#define	RW_ISWRITER(x)		(rw_iswriter(x))

#define	rw_init(lock, desc, type, arg)	do {				\
	const char *_name;						\
	ASSERT((type) == 0 || (type) == RW_DEFAULT);			\
	KASSERT(((lock)->lock_object.lo_flags & LO_ALLMASK) !=		\
	    LO_EXPECTED, ("lock %s already initialized", #lock));	\
	bzero((lock), sizeof(struct sx));				\
	for (_name = #lock; *_name != '\0'; _name++) {			\
		if (*_name >= 'a' && *_name <= 'z')			\
			break;						\
	}								\
	if (*_name == '\0')						\
		_name = #lock;						\
	sx_init_flags((lock), _name, RW_FLAGS);				\
} while (0)
#define	rw_destroy(lock)	sx_destroy(lock)
#define	rw_enter(lock, how)	do {					\
	if ((how) == RW_READER)						\
		sx_slock(lock);						\
	else /* if ((how) == RW_WRITER) */				\
		sx_xlock(lock);						\
} while (0)
#define	rw_tryenter(lock, how)	((how) == RW_READER ? sx_try_slock(lock) : sx_try_xlock(lock))
#define	rw_exit(lock)		sx_unlock(lock)
#define	rw_downgrade(lock)	sx_downgrade(lock)
#define	rw_tryupgrade(lock)	sx_try_upgrade(lock)
#define	rw_read_held(lock)	((lock)->sx_lock != SX_LOCK_UNLOCKED && ((lock)->sx_lock & SX_LOCK_SHARED))
#define	rw_write_held(lock)	sx_xlocked(lock)
#define	rw_lock_held(lock)	(rw_read_held(lock) || rw_write_held(lock))
#define	rw_iswriter(lock)	sx_xlocked(lock)
/* TODO: Change to sx_xholder() once it is moved from kern_sx.c to sx.h. */
#define	rw_owner(lock)		((lock)->sx_lock & SX_LOCK_SHARED ? NULL : (struct thread *)SX_OWNER((lock)->sx_lock))

#endif	/* defined(_KERNEL) */

#endif	/* _OPENSOLARIS_SYS_RWLOCK_H_ */
