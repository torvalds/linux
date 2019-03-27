/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2011 Ed Schouten <ed@FreeBSD.org>
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
 *
 * $FreeBSD$
 */

#ifndef _THREADS_H_
#define	_THREADS_H_

#include <time.h>

/*
 * The C11 threads interface.
 *
 * This interface is implemented as a light-weight wrapper around
 * <pthread.h>.  To prevent namespace pollution, the once_flag object,
 * its corresponding ONCE_FLAG_INIT and TSS_DTOR_ITERATIONS have been
 * copied from this header file.  They must be kept in sync.
 */

typedef struct pthread_cond	*cnd_t;
typedef struct pthread_mutex	*mtx_t;
typedef struct pthread		*thrd_t;
typedef int			 tss_t;

typedef struct {
	int	__state;
	mtx_t	__mutex;
} once_flag;

typedef void (*tss_dtor_t)(void *);
typedef int (*thrd_start_t)(void *);

enum {
	mtx_plain = 0x1,
	mtx_recursive = 0x2,
	mtx_timed = 0x4
};

enum {
	thrd_busy = 1,
	thrd_error = 2,
	thrd_nomem = 3,
	thrd_success = 4,
	thrd_timedout = 5
};

#if !defined(__cplusplus) || __cplusplus < 201103L
#define	thread_local		_Thread_local
#endif
#define	ONCE_FLAG_INIT		{ 0, NULL }
#define	TSS_DTOR_ITERATIONS	4

__BEGIN_DECLS
void	call_once(once_flag *, void (*)(void));
int	cnd_broadcast(cnd_t *);
void	cnd_destroy(cnd_t *);
int	cnd_init(cnd_t *);
int	cnd_signal(cnd_t *);
int	cnd_timedwait(cnd_t *__restrict, mtx_t *__restrict __mtx,
    const struct timespec *__restrict)
    __requires_exclusive(*__mtx);
int	cnd_wait(cnd_t *, mtx_t *__mtx)
    __requires_exclusive(*__mtx);
void	mtx_destroy(mtx_t *__mtx)
    __requires_unlocked(*__mtx);
int	mtx_init(mtx_t *__mtx, int)
    __requires_unlocked(*__mtx);
int	mtx_lock(mtx_t *__mtx)
    __locks_exclusive(*__mtx);
int	mtx_timedlock(mtx_t *__restrict __mtx,
    const struct timespec *__restrict)
    __trylocks_exclusive(thrd_success, *__mtx);
int	mtx_trylock(mtx_t *__mtx)
    __trylocks_exclusive(thrd_success, *__mtx);
int	mtx_unlock(mtx_t *__mtx)
    __unlocks(*__mtx);
int	thrd_create(thrd_t *, thrd_start_t, void *);
thrd_t	thrd_current(void);
int	thrd_detach(thrd_t);
int	thrd_equal(thrd_t, thrd_t);
_Noreturn void
	thrd_exit(int);
int	thrd_join(thrd_t, int *);
int	thrd_sleep(const struct timespec *, struct timespec *);
void	thrd_yield(void);
int	tss_create(tss_t *, tss_dtor_t);
void	tss_delete(tss_t);
void *	tss_get(tss_t);
int	tss_set(tss_t, void *);
__END_DECLS

#endif /* !_THREADS_H_ */
