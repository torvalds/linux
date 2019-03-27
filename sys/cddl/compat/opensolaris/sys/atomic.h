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

#ifndef _OPENSOLARIS_SYS_ATOMIC_H_
#define	_OPENSOLARIS_SYS_ATOMIC_H_

#include <sys/types.h>
#include <machine/atomic.h>

#define	casptr(_a, _b, _c)	\
	atomic_cmpset_ptr((volatile uintptr_t *)(_a), (uintptr_t)(_b), (uintptr_t) (_c))
#define cas32	atomic_cmpset_32

#if defined(__i386__) && (defined(_KERNEL) || defined(KLD_MODULE))
#define	I386_HAVE_ATOMIC64
#endif

#if !defined(__LP64__) && !defined(__mips_n32) && \
    !defined(ARM_HAVE_ATOMIC64) && !defined(I386_HAVE_ATOMIC64)
extern void atomic_add_64(volatile uint64_t *target, int64_t delta);
extern void atomic_dec_64(volatile uint64_t *target);
#endif
#ifndef __sparc64__
extern uint32_t atomic_cas_32(volatile uint32_t *target, uint32_t cmp,
    uint32_t newval);
extern uint64_t atomic_cas_64(volatile uint64_t *target, uint64_t cmp,
    uint64_t newval);
#endif
extern uint64_t atomic_add_64_nv(volatile uint64_t *target, int64_t delta);
extern uint8_t atomic_or_8_nv(volatile uint8_t *target, uint8_t value);
extern void membar_producer(void);

#if defined(__sparc64__) || defined(__powerpc__) || defined(__arm__) || \
    defined(__mips__) || defined(__aarch64__) || defined(__riscv)
extern void atomic_or_8(volatile uint8_t *target, uint8_t value);
#else
static __inline void
atomic_or_8(volatile uint8_t *target, uint8_t value)
{
	atomic_set_8(target, value);
}
#endif

static __inline uint32_t
atomic_add_32_nv(volatile uint32_t *target, int32_t delta)
{
	return (atomic_fetchadd_32(target, delta) + delta);
}

static __inline u_int
atomic_add_int_nv(volatile u_int *target, int delta)
{
	return (atomic_add_32_nv(target, delta));
}

static __inline void
atomic_dec_32(volatile uint32_t *target)
{
	atomic_subtract_32(target, 1);
}

static __inline uint32_t
atomic_dec_32_nv(volatile uint32_t *target)
{
	return (atomic_fetchadd_32(target, -1) - 1);
}

#if defined(__LP64__) || defined(__mips_n32) || \
    defined(ARM_HAVE_ATOMIC64) || defined(I386_HAVE_ATOMIC64)
static __inline void
atomic_dec_64(volatile uint64_t *target)
{
	atomic_subtract_64(target, 1);
}
#endif

static __inline void
atomic_inc_32(volatile uint32_t *target)
{
	atomic_add_32(target, 1);
}

static __inline uint32_t
atomic_inc_32_nv(volatile uint32_t *target)
{
	return (atomic_add_32_nv(target, 1));
}

static __inline void
atomic_inc_64(volatile uint64_t *target)
{
	atomic_add_64(target, 1);
}

static __inline uint64_t
atomic_inc_64_nv(volatile uint64_t *target)
{
	return (atomic_add_64_nv(target, 1));
}

static __inline uint64_t
atomic_dec_64_nv(volatile uint64_t *target)
{
	return (atomic_add_64_nv(target, -1));
}

#if !defined(COMPAT_32BIT) && defined(__LP64__)
static __inline void *
atomic_cas_ptr(volatile void *target, void *cmp,  void *newval)
{
	return ((void *)atomic_cas_64((volatile uint64_t *)target,
	    (uint64_t)cmp, (uint64_t)newval));
}
#else
static __inline void *
atomic_cas_ptr(volatile void *target, void *cmp,  void *newval)
{
	return ((void *)atomic_cas_32((volatile uint32_t *)target,
	    (uint32_t)cmp, (uint32_t)newval));
}
#endif	/* !defined(COMPAT_32BIT) && defined(__LP64__) */

#endif	/* !_OPENSOLARIS_SYS_ATOMIC_H_ */
