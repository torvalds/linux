/*	$OpenBSD: _atomic_lock.c,v 1.2 2022/12/27 17:10:06 jmc Exp $	*/
/*
 * Copyright (c) 2020	Mars Li <mengshi.li.mars@gmail.com>
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
 * Atomic lock for riscv64
 */

#include <sys/types.h>
#include <machine/spinlock.h>

/*
 * Spinlock does not have Acquire/Release semantics, so amoswap.w.aq
 * should not used here.
 */
int
_atomic_lock(volatile _atomic_lock_t *lock)
{
	_atomic_lock_t old;

	/*
	 * Use the amoswap instruction to swap the lock value with
	 * a local variable containing the locked state.
	 */
	__asm__("amoswap.w %0, %1, (%2)"
		: "=r" (old)
		: "r" (_ATOMIC_LOCK_LOCKED), "r"  (lock) : "memory");

	return (old != _ATOMIC_LOCK_UNLOCKED);
}
