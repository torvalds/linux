/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (C) 2001 Jason Evans <jasone@freebsd.org>.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice(s), this list of conditions and the following disclaimer as
 *    the first lines of this file unmodified other than the possible
 *    addition of one or more copyright notices.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice(s), this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDER(S) ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT HOLDER(S) BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH
 * DAMAGE.
 *
 * $FreeBSD$
 */

#ifndef	_SYS_SEMA_H_
#define	_SYS_SEMA_H_

#include <sys/queue.h>
#include <sys/_lock.h>
#include <sys/_mutex.h>
#include <sys/condvar.h>

struct sema {
	struct mtx	sema_mtx;	/* General protection lock. */
	struct cv	sema_cv;	/* Waiters. */
	int		sema_waiters;	/* Number of waiters. */
	int		sema_value;	/* Semaphore value. */
};

#ifdef _KERNEL
void	sema_init(struct sema *sema, int value, const char *description);
void	sema_destroy(struct sema *sema);
void	_sema_post(struct sema *sema, const char *file, int line);
void	_sema_wait(struct sema *sema, const char *file, int line);
int	_sema_timedwait(struct sema *sema, int timo, const char *file, int
    line);
int	_sema_trywait(struct sema *sema, const char *file, int line);
int	sema_value(struct sema *sema);

#define	sema_post(sema)		_sema_post((sema), LOCK_FILE, LOCK_LINE)
#define	sema_wait(sema)		_sema_wait((sema), LOCK_FILE, LOCK_LINE)
#define	sema_timedwait(sema, timo)					\
	_sema_timedwait((sema), (timo), LOCK_FILE, LOCK_LINE)
#define	sema_trywait(sema)	_sema_trywait((sema), LOCK_FILE, LOCK_LINE)

#endif	/* _KERNEL */
#endif	/* _SYS_SEMA_H_ */
