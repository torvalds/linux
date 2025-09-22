/*	$OpenBSD: spinlock.h,v 1.5 2017/09/05 02:40:54 guenther Exp $	*/
 /* Public domain */

#ifndef _MIPS64_SPINLOCK_H_
#define _MIPS64_SPINLOCK_H_

#define _ATOMIC_LOCK_UNLOCKED	(0)
#define _ATOMIC_LOCK_LOCKED	(1)
typedef int _atomic_lock_t;

#endif /* !_MIPS64_SPINLOCK_H_ */
