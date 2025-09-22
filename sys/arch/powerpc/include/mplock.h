/*	$OpenBSD: mplock.h,v 1.7 2024/09/04 07:54:51 mglocker Exp $	*/

/*
 * Copyright (c) 2004 Niklas Hallqvist.  All rights reserved.
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
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef _POWERPC_MPLOCK_H_
#define _POWERPC_MPLOCK_H_

#define __USE_MI_MPLOCK

/*
 * __ppc_lock exists because pte_spill_r() can't use __mp_lock.
 * Really simple spinlock implementation with recursive capabilities.
 * Correctness is paramount, no fanciness allowed.
 */

struct __ppc_lock {
	struct cpu_info *volatile	mpl_cpu;
	long				mpl_count;
};

#ifndef _LOCORE

#define PPC_LOCK_INITIALIZER	{ NULL, 0 }

void __ppc_lock(struct __ppc_lock *);
void __ppc_unlock(struct __ppc_lock *);

#endif

#endif /* !_POWERPC_MPLOCK_H */
