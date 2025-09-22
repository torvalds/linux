#ifndef	_M88K_LOCK_H_
#define	_M88K_LOCK_H_
/*	$OpenBSD: lock.h,v 1.11 2015/02/11 00:14:11 dlg Exp $	*/

/*
 * Copyright (c) 2005, Miodrag Vallat.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 * ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include <m88k/asm.h>

typedef volatile u_int	__cpu_simple_lock_t;

/* do not change these - code below assumes r0 == __SIMPLELOCK_UNLOCKED */
#define	__SIMPLELOCK_LOCKED	1
#define	__SIMPLELOCK_UNLOCKED	0

static __inline__ void
__cpu_simple_lock_init(__cpu_simple_lock_t *l)
{
	*l = __SIMPLELOCK_UNLOCKED;
}

static __inline__ int
__cpu_simple_lock_try(__cpu_simple_lock_t *l)
{
	/*
	 * The local __cpu_simple_lock_t is not declared volatile, so that
	 * there are not pipeline synchronization around stores to it.
	 * xmem will do the right thing regardless of the volatile qualifier.
	 */
	u_int old = __SIMPLELOCK_LOCKED;

	__asm__ volatile
	    ("xmem %0, %2, %%r0" : "+r"(old), "+m"(*l) : "r"(l));

	return (old == __SIMPLELOCK_UNLOCKED);
}

static __inline__ void
__cpu_simple_lock(__cpu_simple_lock_t *l)
{
	for (;;) {
		if (__cpu_simple_lock_try(l) != 0)
			break;
		while (*l != __SIMPLELOCK_UNLOCKED)
			;	/* spin without exclusive bus access */
	}
}

static __inline__ void
__cpu_simple_unlock(__cpu_simple_lock_t *l)
{
	*l = __SIMPLELOCK_UNLOCKED;
}

#endif	/* _M88K_LOCK_H_ */
