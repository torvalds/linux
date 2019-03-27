/*-
 * Copyright (c) 2013-2015 Mark R V Murray
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer
 *    in this position and unchanged.
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
 *
 * $FreeBSD$
 */

#ifndef SYS_DEV_RANDOM_FORTUNA_H_INCLUDED
#define	SYS_DEV_RANDOM_FORTUNA_H_INCLUDED

#ifdef _KERNEL
typedef struct mtx mtx_t;
#define	RANDOM_RESEED_INIT_LOCK(x)		mtx_init(&fortuna_state.fs_mtx, "reseed mutex", NULL, MTX_DEF)
#define	RANDOM_RESEED_DEINIT_LOCK(x)		mtx_destroy(&fortuna_state.fs_mtx)
#define	RANDOM_RESEED_LOCK(x)			mtx_lock(&fortuna_state.fs_mtx)
#define	RANDOM_RESEED_UNLOCK(x)			mtx_unlock(&fortuna_state.fs_mtx)
#define	RANDOM_RESEED_ASSERT_LOCK_OWNED(x)	mtx_assert(&fortuna_state.fs_mtx, MA_OWNED)
#else
#define	RANDOM_RESEED_INIT_LOCK(x)		mtx_init(&fortuna_state.fs_mtx, mtx_plain)
#define	RANDOM_RESEED_DEINIT_LOCK(x)		mtx_destroy(&fortuna_state.fs_mtx)
#define	RANDOM_RESEED_LOCK(x)			mtx_lock(&fortuna_state.fs_mtx)
#define	RANDOM_RESEED_UNLOCK(x)			mtx_unlock(&fortuna_state.fs_mtx)
#define	RANDOM_RESEED_ASSERT_LOCK_OWNED(x)
#endif

#endif /* SYS_DEV_RANDOM_FORTUNA_H_INCLUDED */
