/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2006 John Baldwin <jhb@FreeBSD.org>
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
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

#ifndef _SYS__RWLOCK_H_
#define	_SYS__RWLOCK_H_

#include <machine/param.h>

/*
 * Reader/writer lock.
 *
 * All reader/writer lock implementations must always have a member
 * called rw_lock.  Other locking primitive structures are not allowed to
 * use this name for their members.
 * If this rule needs to change, the bits in the reader/writer lock
 * implementation must be modified appropriately.
 */
struct rwlock {
	struct lock_object	lock_object;
	volatile uintptr_t	rw_lock;
};

/*
 * Members of struct rwlock_padalign must mirror members of struct rwlock.
 * rwlock_padalign rwlocks can use the rwlock(9) API transparently without
 * modification.
 * Pad-aligned rwlocks used within structures should generally be the
 * first member of the struct.  Otherwise, the compiler can generate
 * additional padding for the struct to keep a correct alignment for
 * the rwlock.
 */
struct rwlock_padalign {
	struct lock_object	lock_object;
	volatile uintptr_t	rw_lock;
} __aligned(CACHE_LINE_SIZE);

#endif /* !_SYS__RWLOCK_H_ */
