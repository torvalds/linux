/*
 * Copyright 2010-2015 Samy Al Bahra.
 * All rights reserved.
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#ifndef CK_SPINLOCK_H
#define CK_SPINLOCK_H

#include "spinlock/anderson.h"
#include "spinlock/cas.h"
#include "spinlock/clh.h"
#include "spinlock/dec.h"
#include "spinlock/fas.h"
#include "spinlock/hclh.h"
#include "spinlock/mcs.h"
#include "spinlock/ticket.h"

/*
 * On tested x86, x86_64, PPC64 and SPARC64 targets,
 * ck_spinlock_fas proved to have lowest latency
 * in fast path testing or negligible degradation
 * from faster but less robust implementations.
 */
#define CK_SPINLOCK_INITIALIZER CK_SPINLOCK_FAS_INITIALIZER
#define ck_spinlock_t		ck_spinlock_fas_t
#define ck_spinlock_init(x)	ck_spinlock_fas_init(x)
#define ck_spinlock_lock(x)	ck_spinlock_fas_lock(x)
#define ck_spinlock_lock_eb(x)	ck_spinlock_fas_lock_eb(x)
#define ck_spinlock_unlock(x)	ck_spinlock_fas_unlock(x)
#define ck_spinlock_locked(x)	ck_spinlock_fas_locked(x)
#define ck_spinlock_trylock(x)	ck_spinlock_fas_trylock(x)

CK_ELIDE_PROTOTYPE(ck_spinlock, ck_spinlock_t,
    ck_spinlock_locked, ck_spinlock_lock,
    ck_spinlock_locked, ck_spinlock_unlock)

CK_ELIDE_TRYLOCK_PROTOTYPE(ck_spinlock, ck_spinlock_t,
    ck_spinlock_locked, ck_spinlock_trylock)

#endif /* CK_SPINLOCK_H */
