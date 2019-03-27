/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1997 Berkeley Software Design, Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Berkeley Software Design Inc's name may not be used to endorse or
 *    promote products derived from this software without specific prior
 *    written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY BERKELEY SOFTWARE DESIGN INC ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL BERKELEY SOFTWARE DESIGN INC BE LIABLE
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

#ifndef _SYS__MUTEX_H_
#define	_SYS__MUTEX_H_

#include <machine/param.h>

/*
 * Sleep/spin mutex.
 *
 * All mutex implementations must always have a member called mtx_lock.
 * Other locking primitive structures are not allowed to use this name
 * for their members.
 * If this rule needs to change, the bits in the mutex implementation must
 * be modified appropriately.
 */
struct mtx {
	struct lock_object	lock_object;	/* Common lock properties. */
	volatile uintptr_t	mtx_lock;	/* Owner and flags. */
};

/*
 * Members of struct mtx_padalign must mirror members of struct mtx.
 * mtx_padalign mutexes can use the mtx(9) API transparently without
 * modification.
 * Pad-aligned mutexes used within structures should generally be the
 * first member of the struct.  Otherwise, the compiler can generate
 * additional padding for the struct to keep a correct alignment for
 * the mutex.
 */
struct mtx_padalign {
	struct lock_object	lock_object;	/* Common lock properties. */
	volatile uintptr_t	mtx_lock;	/* Owner and flags. */
} __aligned(CACHE_LINE_SIZE);

#endif /* !_SYS__MUTEX_H_ */
