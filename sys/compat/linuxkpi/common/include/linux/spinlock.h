/*-
 * Copyright (c) 2010 Isilon Systems, Inc.
 * Copyright (c) 2010 iX Systems, Inc.
 * Copyright (c) 2010 Panasas, Inc.
 * Copyright (c) 2013-2017 Mellanox Technologies, Ltd.
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
#ifndef	_LINUX_SPINLOCK_H_
#define	_LINUX_SPINLOCK_H_

#include <sys/param.h>
#include <sys/kernel.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/kdb.h>

#include <linux/compiler.h>
#include <linux/rwlock.h>
#include <linux/bottom_half.h>

typedef struct {
	struct mtx m;
} spinlock_t;

/*
 * By defining CONFIG_SPIN_SKIP LinuxKPI spinlocks and asserts will be
 * skipped during panic(). By default it is disabled due to
 * performance reasons.
 */
#ifdef CONFIG_SPIN_SKIP
#define	SPIN_SKIP(void)	unlikely(SCHEDULER_STOPPED() || kdb_active)
#else
#define	SPIN_SKIP(void) 0
#endif

#define	spin_lock(_l) do {			\
	if (SPIN_SKIP())			\
		break;				\
	mtx_lock(&(_l)->m);			\
	local_bh_disable();			\
} while (0)

#define	spin_lock_bh(_l) do {			\
	spin_lock(_l);				\
} while (0)

#define	spin_lock_irq(_l) do {			\
	spin_lock(_l);				\
} while (0)

#define	spin_unlock(_l)	do {			\
	if (SPIN_SKIP())			\
		break;				\
	local_bh_enable();			\
	mtx_unlock(&(_l)->m);			\
} while (0)

#define	spin_unlock_bh(_l) do {			\
	spin_unlock(_l);			\
} while (0)

#define	spin_unlock_irq(_l) do {		\
	spin_unlock(_l);			\
} while (0)

#define	spin_trylock(_l) ({			\
	int __ret;				\
	if (SPIN_SKIP()) {			\
		__ret = 1;			\
	} else {				\
		__ret = mtx_trylock(&(_l)->m);	\
		if (likely(__ret != 0))		\
			local_bh_disable();	\
	}					\
	__ret;					\
})

#define	spin_trylock_irq(_l)			\
	spin_trylock(_l)

#define	spin_lock_nested(_l, _n) do {		\
	if (SPIN_SKIP())			\
		break;				\
	mtx_lock_flags(&(_l)->m, MTX_DUPOK);	\
	local_bh_disable();			\
} while (0)

#define	spin_lock_irqsave(_l, flags) do {	\
	(flags) = 0;				\
	spin_lock(_l);				\
} while (0)

#define	spin_lock_irqsave_nested(_l, flags, _n) do {	\
	(flags) = 0;					\
	spin_lock_nested(_l, _n);			\
} while (0)

#define	spin_unlock_irqrestore(_l, flags) do {		\
	spin_unlock(_l);				\
} while (0)

#ifdef WITNESS_ALL
/* NOTE: the maximum WITNESS name is 64 chars */
#define	__spin_lock_name(name, file, line)		\
	(((const char *){file ":" #line "-" name}) +	\
	(sizeof(file) > 16 ? sizeof(file) - 16 : 0))
#else
#define	__spin_lock_name(name, file, line)	name
#endif
#define	_spin_lock_name(...)		__spin_lock_name(__VA_ARGS__)
#define	spin_lock_name(name)		_spin_lock_name(name, __FILE__, __LINE__)

#define	spin_lock_init(lock)	linux_spin_lock_init(lock, spin_lock_name("lnxspin"))

static inline void
linux_spin_lock_init(spinlock_t *lock, const char *name)
{

	memset(lock, 0, sizeof(*lock));
	mtx_init(&lock->m, name, NULL, MTX_DEF | MTX_NOWITNESS);
}

static inline void
spin_lock_destroy(spinlock_t *lock)
{

       mtx_destroy(&lock->m);
}

#define	DEFINE_SPINLOCK(lock)					\
	spinlock_t lock;					\
	MTX_SYSINIT(lock, &(lock).m, spin_lock_name("lnxspin"), MTX_DEF)

#define	assert_spin_locked(_l) do {		\
	if (SPIN_SKIP())			\
		break;				\
	mtx_assert(&(_l)->m, MA_OWNED);		\
} while (0)

#endif					/* _LINUX_SPINLOCK_H_ */
