/*	$OpenBSD: _atomic_lock.c,v 1.1 2017/08/15 06:13:24 guenther Exp $	*/
/*
 * Copyright (c) 1998 Dale Rahn <drahn@openbsd.org>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

/*
 * Atomic lock for powerpc
 */

#include <machine/spinlock.h>

int
_atomic_lock(volatile _atomic_lock_t *lock)
{
	_atomic_lock_t old;

	__asm__("1: lwarx 0,0,%1   \n"
		"   stwcx. %2,0,%1 \n"
		"   bne- 1b        \n"
		"   mr %0, 0	   \n"
		: "=r" (old), "=r" (lock)
		: "r" (_ATOMIC_LOCK_LOCKED), "1" (lock) : "0"
	);

	return (old != _ATOMIC_LOCK_UNLOCKED);

	/*
	 * Dale <drahn@openbsd.org> says:
	 *   Side note. to prevent two processes from accessing
	 *   the same address with the lwarx in one instruction
	 *   and the stwcx in another process, the current powerpc
	 *   kernel uses a stwcx instruction without the corresponding
	 *   lwarx which causes any reservation of a process
	 *   to be removed.  if a context switch occurs
	 *   between the two accesses the store will not occur
	 *   and the condition code will cause it to loop. If on
	 *   a dual processor machine, the reserve will cause
	 *   appropriate bus cycle accesses to notify other
	 *   processors.
	 */
}
