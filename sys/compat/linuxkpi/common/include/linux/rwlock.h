/*-
 * Copyright (c) 2010 Isilon Systems, Inc.
 * Copyright (c) 2010 iX Systems, Inc.
 * Copyright (c) 2010 Panasas, Inc.
 * Copyright (c) 2013, 2014 Mellanox Technologies, Ltd.
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
 */
#ifndef	_LINUX_RWLOCK_H_
#define	_LINUX_RWLOCK_H_

#include <sys/types.h>
#include <sys/lock.h>
#include <sys/rwlock.h>
#include <sys/libkern.h>

typedef struct {
	struct rwlock rw;
} rwlock_t;

#define	read_lock(_l)		rw_rlock(&(_l)->rw)
#define	write_lock(_l)		rw_wlock(&(_l)->rw)
#define	read_unlock(_l)		rw_runlock(&(_l)->rw)
#define	write_unlock(_l)	rw_wunlock(&(_l)->rw)
#define	read_lock_irq(lock)	read_lock((lock))
#define	read_unlock_irq(lock)	read_unlock((lock))
#define	write_lock_irq(lock)	write_lock((lock))
#define	write_unlock_irq(lock)	write_unlock((lock))
#define	read_lock_irqsave(lock, flags)					\
    do {(flags) = 0; read_lock(lock); } while (0)
#define	write_lock_irqsave(lock, flags)					\
    do {(flags) = 0; write_lock(lock); } while (0)
#define	read_unlock_irqrestore(lock, flags)				\
    do { read_unlock(lock); } while (0)
#define	write_unlock_irqrestore(lock, flags)				\
    do { write_unlock(lock); } while (0)

static inline void
rwlock_init(rwlock_t *lock)
{

	memset(&lock->rw, 0, sizeof(lock->rw));
	rw_init_flags(&lock->rw, "lnxrw", RW_NOWITNESS);
}

#endif	/* _LINUX_RWLOCK_H_ */
