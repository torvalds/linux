/*	$OpenBSD: spinlock.h,v 1.4 2017/09/05 02:40:54 guenther Exp $	*/

#ifndef _POWERPC_SPINLOCK_H_
#define _POWERPC_SPINLOCK_H_

#define _ATOMIC_LOCK_UNLOCKED	(0)
#define _ATOMIC_LOCK_LOCKED	(1)
typedef int _atomic_lock_t;

#endif
