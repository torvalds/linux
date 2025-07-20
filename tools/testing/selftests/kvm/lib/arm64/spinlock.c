// SPDX-License-Identifier: GPL-2.0
/*
 * ARM64 Spinlock support
 */
#include <stdint.h>

#include "spinlock.h"

void spin_lock(struct spinlock *lock)
{
	int val, res;

	asm volatile(
	"1:	ldaxr	%w0, [%2]\n"
	"	cbnz	%w0, 1b\n"
	"	mov	%w0, #1\n"
	"	stxr	%w1, %w0, [%2]\n"
	"	cbnz	%w1, 1b\n"
	: "=&r" (val), "=&r" (res)
	: "r" (&lock->v)
	: "memory");
}

void spin_unlock(struct spinlock *lock)
{
	asm volatile("stlr wzr, [%0]\n"	: : "r" (&lock->v) : "memory");
}
