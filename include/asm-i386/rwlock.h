/* include/asm-i386/rwlock.h
 *
 *	Helpers used by both rw spinlocks and rw semaphores.
 *
 *	Based in part on code from semaphore.h and
 *	spinlock.h Copyright 1996 Linus Torvalds.
 *
 *	Copyright 1999 Red Hat, Inc.
 *
 *	Written by Benjamin LaHaise.
 *
 *	This program is free software; you can redistribute it and/or
 *	modify it under the terms of the GNU General Public License
 *	as published by the Free Software Foundation; either version
 *	2 of the License, or (at your option) any later version.
 */
#ifndef _ASM_I386_RWLOCK_H
#define _ASM_I386_RWLOCK_H

#define RW_LOCK_BIAS		 0x01000000
#define RW_LOCK_BIAS_STR	"0x01000000"

#define __build_read_lock_ptr(rw, helper)   \
	asm volatile(LOCK_PREFIX " subl $1,(%0)\n\t" \
			"jns 1f\n" \
			"call " helper "\n\t" \
			"1:\n" \
			::"a" (rw) : "memory")

#define __build_read_lock_const(rw, helper)   \
	asm volatile(LOCK_PREFIX " subl $1,%0\n\t" \
			"jns 1f\n" \
			"pushl %%eax\n\t" \
			"leal %0,%%eax\n\t" \
			"call " helper "\n\t" \
			"popl %%eax\n\t" \
			"1:\n" \
			:"+m" (*(volatile int *)rw) : : "memory")

#define __build_read_lock(rw, helper)	do { \
						if (__builtin_constant_p(rw)) \
							__build_read_lock_const(rw, helper); \
						else \
							__build_read_lock_ptr(rw, helper); \
					} while (0)

#define __build_write_lock_ptr(rw, helper) \
	asm volatile(LOCK_PREFIX " subl $" RW_LOCK_BIAS_STR ",(%0)\n\t" \
			"jz 1f\n" \
			"call " helper "\n\t" \
			"1:\n" \
			::"a" (rw) : "memory")

#define __build_write_lock_const(rw, helper) \
	asm volatile(LOCK_PREFIX " subl $" RW_LOCK_BIAS_STR ",%0\n\t" \
			"jz 1f\n" \
			"pushl %%eax\n\t" \
			"leal %0,%%eax\n\t" \
			"call " helper "\n\t" \
			"popl %%eax\n\t" \
			"1:\n" \
			:"+m" (*(volatile int *)rw) : : "memory")

#define __build_write_lock(rw, helper)	do { \
						if (__builtin_constant_p(rw)) \
							__build_write_lock_const(rw, helper); \
						else \
							__build_write_lock_ptr(rw, helper); \
					} while (0)

#endif
