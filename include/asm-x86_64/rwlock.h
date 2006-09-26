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

#define RW_LOCK_BIAS		 0x01000000
#define RW_LOCK_BIAS_STR	 "0x01000000"

#define __build_read_lock(rw)   \
	asm volatile(LOCK_PREFIX "subl $1,(%0)\n\t" \
		     "jns 1f\n" \
		     "call __read_lock_failed\n" \
		     "1:\n" \
		     ::"D" (rw), "i" (RW_LOCK_BIAS) : "memory")

#define __build_write_lock(rw) \
	asm volatile(LOCK_PREFIX "subl %1,(%0)\n\t" \
		     "jz 1f\n" \
		     "\tcall __write_lock_failed\n\t" \
		     "1:\n" \
		     ::"D" (rw), "i" (RW_LOCK_BIAS) : "memory")

#endif
