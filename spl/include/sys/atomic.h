/*****************************************************************************\
 *  Copyright (C) 2007-2010 Lawrence Livermore National Security, LLC.
 *  Copyright (C) 2007 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Brian Behlendorf <behlendorf1@llnl.gov>.
 *  UCRL-CODE-235197
 *
 *  This file is part of the SPL, Solaris Porting Layer.
 *  For details, see <http://zfsonlinux.org/>.
 *
 *  The SPL is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License as published by the
 *  Free Software Foundation; either version 2 of the License, or (at your
 *  option) any later version.
 *
 *  The SPL is distributed in the hope that it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 *  FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 *  for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with the SPL.  If not, see <http://www.gnu.org/licenses/>.
\*****************************************************************************/

#ifndef _SPL_ATOMIC_H
#define _SPL_ATOMIC_H

#include <linux/module.h>
#include <linux/spinlock.h>
#include <sys/types.h>

/*
 * Two approaches to atomic operations are implemented each with its
 * own benefits are drawbacks imposed by the Solaris API.  Neither
 * approach handles the issue of word breaking when using a 64-bit
 * atomic variable on a 32-bit arch.  The Solaris API would need to
 * add an atomic read call to correctly support this.
 *
 * When ATOMIC_SPINLOCK is defined all atomic operations will be
 * serialized through global spin locks.  This is bad for performance
 * but it does allow a simple generic implementation.
 *
 * When ATOMIC_SPINLOCK is not defined the Linux atomic operations
 * are used.  This is safe as long as the core Linux implementation
 * doesn't change because we are relying on the fact that an atomic
 * type is really just a uint32 or uint64.  If this changes at some
 * point in the future we need to fall-back to the spin approach.
 */
#ifdef ATOMIC_SPINLOCK
extern spinlock_t atomic32_lock;
extern spinlock_t atomic64_lock;

static __inline__ void
atomic_inc_32(volatile uint32_t *target)
{
	spin_lock(&atomic32_lock);
	(*target)++;
	spin_unlock(&atomic32_lock);
}

static __inline__ void
atomic_dec_32(volatile uint32_t *target)
{
	spin_lock(&atomic32_lock);
	(*target)--;
	spin_unlock(&atomic32_lock);
}

static __inline__ void
atomic_add_32(volatile uint32_t *target, int32_t delta)
{
	spin_lock(&atomic32_lock);
	*target += delta;
	spin_unlock(&atomic32_lock);
}

static __inline__ void
atomic_sub_32(volatile uint32_t *target, int32_t delta)
{
	spin_lock(&atomic32_lock);
	*target -= delta;
	spin_unlock(&atomic32_lock);
}

static __inline__ uint32_t
atomic_inc_32_nv(volatile uint32_t *target)
{
	uint32_t nv;

	spin_lock(&atomic32_lock);
	nv = ++(*target);
	spin_unlock(&atomic32_lock);

	return nv;
}

static __inline__ uint32_t
atomic_dec_32_nv(volatile uint32_t *target)
{
	uint32_t nv;

	spin_lock(&atomic32_lock);
	nv = --(*target);
	spin_unlock(&atomic32_lock);

	return nv;
}

static __inline__ uint32_t
atomic_add_32_nv(volatile uint32_t *target, uint32_t delta)
{
	uint32_t nv;

	spin_lock(&atomic32_lock);
	*target += delta;
	nv = *target;
	spin_unlock(&atomic32_lock);

	return nv;
}

static __inline__ uint32_t
atomic_sub_32_nv(volatile uint32_t *target, uint32_t delta)
{
	uint32_t nv;

	spin_lock(&atomic32_lock);
	*target -= delta;
	nv = *target;
	spin_unlock(&atomic32_lock);

	return nv;
}

static __inline__ uint32_t
atomic_cas_32(volatile uint32_t *target,  uint32_t cmp,
              uint32_t newval)
{
	uint32_t rc;

	spin_lock(&atomic32_lock);
	rc = *target;
	if (*target == cmp)
		*target = newval;

	spin_unlock(&atomic32_lock);

	return rc;
}

static __inline__ uint32_t
atomic_swap_32(volatile uint32_t *target,  uint32_t newval)
{
	uint32_t rc;

	spin_lock(&atomic32_lock);
	rc = *target;
	*target = newval;
	spin_unlock(&atomic32_lock);

	return rc;
}

static __inline__ void
atomic_inc_64(volatile uint64_t *target)
{
	spin_lock(&atomic64_lock);
	(*target)++;
	spin_unlock(&atomic64_lock);
}

static __inline__ void
atomic_dec_64(volatile uint64_t *target)
{
	spin_lock(&atomic64_lock);
	(*target)--;
	spin_unlock(&atomic64_lock);
}

static __inline__ void
atomic_add_64(volatile uint64_t *target, uint64_t delta)
{
	spin_lock(&atomic64_lock);
	*target += delta;
	spin_unlock(&atomic64_lock);
}

static __inline__ void
atomic_sub_64(volatile uint64_t *target, uint64_t delta)
{
	spin_lock(&atomic64_lock);
	*target -= delta;
	spin_unlock(&atomic64_lock);
}

static __inline__ uint64_t
atomic_inc_64_nv(volatile uint64_t *target)
{
	uint64_t nv;

	spin_lock(&atomic64_lock);
	nv = ++(*target);
	spin_unlock(&atomic64_lock);

	return nv;
}

static __inline__ uint64_t
atomic_dec_64_nv(volatile uint64_t *target)
{
	uint64_t nv;

	spin_lock(&atomic64_lock);
	nv = --(*target);
	spin_unlock(&atomic64_lock);

	return nv;
}

static __inline__ uint64_t
atomic_add_64_nv(volatile uint64_t *target, uint64_t delta)
{
	uint64_t nv;

	spin_lock(&atomic64_lock);
	*target += delta;
	nv = *target;
	spin_unlock(&atomic64_lock);

	return nv;
}

static __inline__ uint64_t
atomic_sub_64_nv(volatile uint64_t *target, uint64_t delta)
{
	uint64_t nv;

	spin_lock(&atomic64_lock);
	*target -= delta;
	nv = *target;
	spin_unlock(&atomic64_lock);

	return nv;
}

static __inline__ uint64_t
atomic_cas_64(volatile uint64_t *target,  uint64_t cmp,
              uint64_t newval)
{
	uint64_t rc;

	spin_lock(&atomic64_lock);
	rc = *target;
	if (*target == cmp)
		*target = newval;
	spin_unlock(&atomic64_lock);

	return rc;
}

static __inline__ uint64_t
atomic_swap_64(volatile uint64_t *target,  uint64_t newval)
{
	uint64_t rc;

	spin_lock(&atomic64_lock);
	rc = *target;
	*target = newval;
	spin_unlock(&atomic64_lock);

	return rc;
}

#else /* ATOMIC_SPINLOCK */

#define atomic_inc_32(v)	atomic_inc((atomic_t *)(v))
#define atomic_dec_32(v)	atomic_dec((atomic_t *)(v))
#define atomic_add_32(v, i)	atomic_add((i), (atomic_t *)(v))
#define atomic_sub_32(v, i)	atomic_sub((i), (atomic_t *)(v))
#define atomic_inc_32_nv(v)	atomic_inc_return((atomic_t *)(v))
#define atomic_dec_32_nv(v)	atomic_dec_return((atomic_t *)(v))
#define atomic_add_32_nv(v, i)	atomic_add_return((i), (atomic_t *)(v))
#define atomic_sub_32_nv(v, i)	atomic_sub_return((i), (atomic_t *)(v))
#define atomic_cas_32(v, x, y)	atomic_cmpxchg((atomic_t *)(v), x, y)
#define atomic_swap_32(v, x)	atomic_xchg((atomic_t *)(v), x)
#define atomic_inc_64(v)	atomic64_inc((atomic64_t *)(v))
#define atomic_dec_64(v)	atomic64_dec((atomic64_t *)(v))
#define atomic_add_64(v, i)	atomic64_add((i), (atomic64_t *)(v))
#define atomic_sub_64(v, i)	atomic64_sub((i), (atomic64_t *)(v))
#define atomic_inc_64_nv(v)	atomic64_inc_return((atomic64_t *)(v))
#define atomic_dec_64_nv(v)	atomic64_dec_return((atomic64_t *)(v))
#define atomic_add_64_nv(v, i)	atomic64_add_return((i), (atomic64_t *)(v))
#define atomic_sub_64_nv(v, i)	atomic64_sub_return((i), (atomic64_t *)(v))
#define atomic_cas_64(v, x, y)	atomic64_cmpxchg((atomic64_t *)(v), x, y)
#define atomic_swap_64(v, x)	atomic64_xchg((atomic64_t *)(v), x)

#endif /* ATOMIC_SPINLOCK */

#ifdef _LP64
static __inline__ void *
atomic_cas_ptr(volatile void *target,  void *cmp, void *newval)
{
	return (void *)atomic_cas_64((volatile uint64_t *)target,
	                             (uint64_t)cmp, (uint64_t)newval);
}
#else /* _LP64 */
static __inline__ void *
atomic_cas_ptr(volatile void *target,  void *cmp, void *newval)
{
	return (void *)atomic_cas_32((volatile uint32_t *)target,
	                             (uint32_t)cmp, (uint32_t)newval);
}
#endif /* _LP64 */

#endif  /* _SPL_ATOMIC_H */
