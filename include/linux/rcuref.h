/*
 * rcuref.h
 *
 * Reference counting for elements of lists/arrays protected by
 * RCU.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 *
 * Copyright (C) IBM Corporation, 2005
 *
 * Author: Dipankar Sarma <dipankar@in.ibm.com>
 *	   Ravikiran Thirumalai <kiran_th@gmail.com>
 *
 * See Documentation/RCU/rcuref.txt for detailed user guide.
 *
 */

#ifndef _RCUREF_H_
#define _RCUREF_H_

#ifdef __KERNEL__

#include <linux/types.h>
#include <linux/interrupt.h>
#include <linux/spinlock.h>
#include <asm/atomic.h>

/*
 * These APIs work on traditional atomic_t counters used in the
 * kernel for reference counting. Under special circumstances
 * where a lock-free get() operation races with a put() operation
 * these APIs can be used. See Documentation/RCU/rcuref.txt.
 */

#ifdef __HAVE_ARCH_CMPXCHG

/**
 * rcuref_inc - increment refcount for object.
 * @rcuref: reference counter in the object in question.
 *
 * This should be used only for objects where we use RCU and
 * use the rcuref_inc_lf() api to acquire a reference
 * in a lock-free reader-side critical section.
 */
static inline void rcuref_inc(atomic_t *rcuref)
{
	atomic_inc(rcuref);
}

/**
 * rcuref_dec - decrement refcount for object.
 * @rcuref: reference counter in the object in question.
 *
 * This should be used only for objects where we use RCU and
 * use the rcuref_inc_lf() api to acquire a reference
 * in a lock-free reader-side critical section.
 */
static inline void rcuref_dec(atomic_t *rcuref)
{
	atomic_dec(rcuref);
}

/**
 * rcuref_dec_and_test - decrement refcount for object and test
 * @rcuref: reference counter in the object.
 * @release: pointer to the function that will clean up the object
 *	     when the last reference to the object is released.
 *	     This pointer is required.
 *
 * Decrement the refcount, and if 0, return 1. Else return 0.
 *
 * This should be used only for objects where we use RCU and
 * use the rcuref_inc_lf() api to acquire a reference
 * in a lock-free reader-side critical section.
 */
static inline int rcuref_dec_and_test(atomic_t *rcuref)
{
	return atomic_dec_and_test(rcuref);
}

/*
 * cmpxchg is needed on UP too, if deletions to the list/array can happen
 * in interrupt context.
 */

/**
 * rcuref_inc_lf - Take reference to an object in a read-side
 * critical section protected by RCU.
 * @rcuref: reference counter in the object in question.
 *
 * Try and increment the refcount by 1.  The increment might fail if
 * the reference counter has been through a 1 to 0 transition and
 * is no longer part of the lock-free list.
 * Returns non-zero on successful increment and zero otherwise.
 */
static inline int rcuref_inc_lf(atomic_t *rcuref)
{
	int c, old;
	c = atomic_read(rcuref);
	while (c && (old = cmpxchg(&rcuref->counter, c, c + 1)) != c)
		c = old;
	return c;
}

#else				/* !__HAVE_ARCH_CMPXCHG */

extern spinlock_t __rcuref_hash[];

/*
 * Use a hash table of locks to protect the reference count
 * since cmpxchg is not available in this arch.
 */
#ifdef	CONFIG_SMP
#define RCUREF_HASH_SIZE	4
#define RCUREF_HASH(k) \
	(&__rcuref_hash[(((unsigned long)k)>>8) & (RCUREF_HASH_SIZE-1)])
#else
#define	RCUREF_HASH_SIZE	1
#define RCUREF_HASH(k) 	&__rcuref_hash[0]
#endif				/* CONFIG_SMP */

/**
 * rcuref_inc - increment refcount for object.
 * @rcuref: reference counter in the object in question.
 *
 * This should be used only for objects where we use RCU and
 * use the rcuref_inc_lf() api to acquire a reference in a lock-free
 * reader-side critical section.
 */
static inline void rcuref_inc(atomic_t *rcuref)
{
	unsigned long flags;
	spin_lock_irqsave(RCUREF_HASH(rcuref), flags);
	rcuref->counter += 1;
	spin_unlock_irqrestore(RCUREF_HASH(rcuref), flags);
}

/**
 * rcuref_dec - decrement refcount for object.
 * @rcuref: reference counter in the object in question.
 *
 * This should be used only for objects where we use RCU and
 * use the rcuref_inc_lf() api to acquire a reference in a lock-free
 * reader-side critical section.
 */
static inline void rcuref_dec(atomic_t *rcuref)
{
	unsigned long flags;
	spin_lock_irqsave(RCUREF_HASH(rcuref), flags);
	rcuref->counter -= 1;
	spin_unlock_irqrestore(RCUREF_HASH(rcuref), flags);
}

/**
 * rcuref_dec_and_test - decrement refcount for object and test
 * @rcuref: reference counter in the object.
 * @release: pointer to the function that will clean up the object
 *	     when the last reference to the object is released.
 *	     This pointer is required.
 *
 * Decrement the refcount, and if 0, return 1. Else return 0.
 *
 * This should be used only for objects where we use RCU and
 * use the rcuref_inc_lf() api to acquire a reference in a lock-free
 * reader-side critical section.
 */
static inline int rcuref_dec_and_test(atomic_t *rcuref)
{
	unsigned long flags;
	spin_lock_irqsave(RCUREF_HASH(rcuref), flags);
	rcuref->counter--;
	if (!rcuref->counter) {
		spin_unlock_irqrestore(RCUREF_HASH(rcuref), flags);
		return 1;
	} else {
		spin_unlock_irqrestore(RCUREF_HASH(rcuref), flags);
		return 0;
	}
}

/**
 * rcuref_inc_lf - Take reference to an object of a lock-free collection
 * by traversing a lock-free list/array.
 * @rcuref: reference counter in the object in question.
 *
 * Try and increment the refcount by 1.  The increment might fail if
 * the reference counter has been through a 1 to 0 transition and
 * object is no longer part of the lock-free list.
 * Returns non-zero on successful increment and zero otherwise.
 */
static inline int rcuref_inc_lf(atomic_t *rcuref)
{
	int ret;
	unsigned long flags;
	spin_lock_irqsave(RCUREF_HASH(rcuref), flags);
	if (rcuref->counter)
		ret = rcuref->counter++;
	else
		ret = 0;
	spin_unlock_irqrestore(RCUREF_HASH(rcuref), flags);
	return ret;
}


#endif /* !__HAVE_ARCH_CMPXCHG */

#endif /* __KERNEL__ */
#endif /* _RCUREF_H_ */
