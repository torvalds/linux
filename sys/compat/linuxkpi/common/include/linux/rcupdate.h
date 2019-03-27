/*-
 * Copyright (c) 2016-2017 Mellanox Technologies, Ltd.
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
#ifndef	_LINUX_RCUPDATE_H_
#define	_LINUX_RCUPDATE_H_

#include <linux/compiler.h>
#include <linux/types.h>

#include <machine/atomic.h>

#define	LINUX_KFREE_RCU_OFFSET_MAX	4096	/* exclusive */

#define	RCU_INITIALIZER(v)			\
	((__typeof(*(v)) *)(v))

#define	RCU_INIT_POINTER(p, v) do {		\
	(p) = (v);				\
} while (0)

#define	call_rcu(ptr, func) do {		\
	linux_call_rcu(ptr, func);		\
} while (0)

#define	rcu_barrier(void) do {			\
	linux_rcu_barrier();			\
} while (0)

#define	rcu_read_lock(void) do {		\
	linux_rcu_read_lock();			\
} while (0)

#define	rcu_read_unlock(void) do {		\
	linux_rcu_read_unlock();		\
} while (0)

#define	synchronize_rcu(void) do {	\
	linux_synchronize_rcu();	\
} while (0)

#define	synchronize_rcu_expedited(void) do {	\
	linux_synchronize_rcu();		\
} while (0)

#define	kfree_rcu(ptr, rcu_head) do {				\
	CTASSERT(offsetof(__typeof(*(ptr)), rcu_head) <		\
	    LINUX_KFREE_RCU_OFFSET_MAX);			\
	call_rcu(&(ptr)->rcu_head, (rcu_callback_t)(uintptr_t)	\
	    offsetof(__typeof(*(ptr)), rcu_head));		\
} while (0)

#define	rcu_access_pointer(p)			\
	((__typeof(*p) *)READ_ONCE(p))

#define	rcu_dereference_protected(p, c)		\
	((__typeof(*p) *)READ_ONCE(p))

#define	rcu_dereference(p)			\
	rcu_dereference_protected(p, 0)

#define	rcu_dereference_raw(p)			\
	((__typeof(*p) *)READ_ONCE(p))

#define	rcu_pointer_handoff(p) (p)

#define	rcu_assign_pointer(p, v) do {				\
	atomic_store_rel_ptr((volatile uintptr_t *)&(p),	\
	    (uintptr_t)(v));					\
} while (0)

/* prototypes */

extern void linux_call_rcu(struct rcu_head *ptr, rcu_callback_t func);
extern void linux_rcu_barrier(void);
extern void linux_rcu_read_lock(void);
extern void linux_rcu_read_unlock(void);
extern void linux_synchronize_rcu(void);

/* Empty implementation for !DEBUG */
#define	init_rcu_head(...)
#define	destroy_rcu_head(...)
#define	init_rcu_head_on_stack(...)
#define	destroy_rcu_head_on_stack(...)

#endif					/* _LINUX_RCUPDATE_H_ */
