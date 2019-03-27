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
#ifndef _LINUX_SEMAPHORE_H_
#define _LINUX_SEMAPHORE_H_

#include <sys/param.h>
#include <sys/lock.h>
#include <sys/sema.h>
#include <sys/libkern.h>

/*
 * XXX BSD semaphores are disused and slow.  They also do not provide a
 * sema_wait_sig method.  This must be resolved eventually.
 */
struct semaphore {
	struct sema	sema;
};

#define	down(_sem)			sema_wait(&(_sem)->sema)
#define	down_interruptible(_sem)	sema_wait(&(_sem)->sema), 0
#define	down_trylock(_sem)		!sema_trywait(&(_sem)->sema)
#define	up(_sem)			sema_post(&(_sem)->sema)

static inline void
linux_sema_init(struct semaphore *sem, int val)
{

	memset(&sem->sema, 0, sizeof(sem->sema));
	sema_init(&sem->sema, val, "lnxsema");
}

static inline void
init_MUTEX(struct semaphore *sem)
{

	memset(&sem->sema, 0, sizeof(sem->sema));
	sema_init(&sem->sema, 1, "lnxsema");
}

#define	sema_init	linux_sema_init

#endif /* _LINUX_SEMAPHORE_H_ */
