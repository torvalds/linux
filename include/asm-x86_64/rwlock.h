/* include/asm-x86_64/rwlock.h
 *
 *	Helpers used by both rw spinlocks and rw semaphores.
 *
 *	Based in part on code from semaphore.h and
 *	spinlock.h Copyright 1996 Linus Torvalds.
 *
 *	Copyright 1999 Red Hat, Inc.
 *	Copyright 2001,2002 SuSE labs 
 *
 *	Written by Benjamin LaHaise.
 *
 *	This program is free software; you can redistribute it and/or
 *	modify it under the terms of the GNU General Public License
 *	as published by the Free Software Foundation; either version
 *	2 of the License, or (at your option) any later version.
 */
#ifndef _ASM_X86_64_RWLOCK_H
#define _ASM_X86_64_RWLOCK_H

#include <linux/stringify.h>

#define RW_LOCK_BIAS		 0x01000000
#define RW_LOCK_BIAS_STR	"0x01000000"

#define __build_read_lock_ptr(rw, helper)   \
	asm volatile(LOCK "subl $1,(%0)\n\t" \
		     "js 2f\n" \
		     "1:\n" \
		    LOCK_SECTION_START("") \
		     "2:\tcall " helper "\n\t" \
		     "jmp 1b\n" \
		    LOCK_SECTION_END \
		     ::"a" (rw) : "memory")

#define __build_read_lock_const(rw, helper)   \
	asm volatile(LOCK "subl $1,%0\n\t" \
		     "js 2f\n" \
		     "1:\n" \
		    LOCK_SECTION_START("") \
		     "2:\tpushq %%rax\n\t" \
		     "leaq %0,%%rax\n\t" \
		     "call " helper "\n\t" \
		     "popq %%rax\n\t" \
		     "jmp 1b\n" \
		    LOCK_SECTION_END \
		     :"=m" (*((volatile int *)rw))::"memory")

#define __build_read_lock(rw, helper)	do { \
						if (__builtin_constant_p(rw)) \
							__build_read_lock_const(rw, helper); \
						else \
							__build_read_lock_ptr(rw, helper); \
					} while (0)

#define __build_write_lock_ptr(rw, helper) \
	asm volatile(LOCK "subl $" RW_LOCK_BIAS_STR ",(%0)\n\t" \
		     "jnz 2f\n" \
		     "1:\n" \
		     LOCK_SECTION_START("") \
		     "2:\tcall " helper "\n\t" \
		     "jmp 1b\n" \
		     LOCK_SECTION_END \
		     ::"a" (rw) : "memory")

#define __build_write_lock_const(rw, helper) \
	asm volatile(LOCK "subl $" RW_LOCK_BIAS_STR ",(%0)\n\t" \
		     "jnz 2f\n" \
		     "1:\n" \
		    LOCK_SECTION_START("") \
		     "2:\tpushq %%rax\n\t" \
		     "leaq %0,%%rax\n\t" \
		     "call " helper "\n\t" \
		     "popq %%rax\n\t" \
		     "jmp 1b\n" \
		    LOCK_SECTION_END \
		     :"=m" (*((volatile long *)rw))::"memory")

#define __build_write_lock(rw, helper)	do { \
						if (__builtin_constant_p(rw)) \
							__build_write_lock_const(rw, helper); \
						else \
							__build_write_lock_ptr(rw, helper); \
					} while (0)

#endif
