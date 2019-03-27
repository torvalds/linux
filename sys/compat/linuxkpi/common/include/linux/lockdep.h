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

#ifndef _LINUX_LOCKDEP_H_
#define	_LINUX_LOCKDEP_H_

struct lock_class_key {
};

#define	lockdep_set_class(lock, key)
#define	lockdep_set_class_and_name(lock, key, name)
#define	lockdep_set_current_reclaim_state(g) do { } while (0)
#define	lockdep_clear_current_reclaim_state() do { } while (0)

#define	lockdep_assert_held(m)				\
	sx_assert(&(m)->sx, SA_XLOCKED)

#define	lockdep_assert_held_once(m)			\
	sx_assert(&(m)->sx, SA_XLOCKED | SA_NOTRECURSED)

#define	lockdep_is_held(m)	(sx_xholder(&(m)->sx) == curthread)

#define	might_lock(m)	do { } while (0)
#define	might_lock_read(m) do { } while (0)

#define	lock_acquire(...) do { } while (0)
#define	lock_release(...) do { } while (0)
#define	lock_acquire_shared_recursive(...) do { } while (0)

#endif /* _LINUX_LOCKDEP_H_ */
