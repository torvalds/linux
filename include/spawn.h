/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2008 Ed Schouten <ed@FreeBSD.org>
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

#ifndef _SPAWN_H_
#define _SPAWN_H_

#include <sys/cdefs.h>
#include <sys/_types.h>
#include <sys/_sigset.h>

#ifndef _MODE_T_DECLARED
typedef	__mode_t	mode_t;
#define	_MODE_T_DECLARED
#endif

#ifndef _PID_T_DECLARED
typedef	__pid_t		pid_t;
#define	_PID_T_DECLARED
#endif

#ifndef _SIGSET_T_DECLARED
#define	_SIGSET_T_DECLARED
typedef	__sigset_t	sigset_t;
#endif

struct sched_param;

typedef struct __posix_spawnattr		*posix_spawnattr_t;
typedef struct __posix_spawn_file_actions	*posix_spawn_file_actions_t;

#define POSIX_SPAWN_RESETIDS		0x01
#define POSIX_SPAWN_SETPGROUP		0x02
#define POSIX_SPAWN_SETSCHEDPARAM	0x04
#define POSIX_SPAWN_SETSCHEDULER	0x08
#define POSIX_SPAWN_SETSIGDEF		0x10
#define POSIX_SPAWN_SETSIGMASK		0x20

__BEGIN_DECLS
/*
 * Spawn routines
 *
 * XXX both arrays should be __restrict, but this does not work when GCC
 * is invoked with -std=c99.
 */
int posix_spawn(pid_t * __restrict, const char * __restrict,
    const posix_spawn_file_actions_t *, const posix_spawnattr_t * __restrict,
    char * const [], char * const []);
int posix_spawnp(pid_t * __restrict, const char * __restrict,
    const posix_spawn_file_actions_t *, const posix_spawnattr_t * __restrict,
    char * const [], char * const []);

/*
 * File descriptor actions
 */
int posix_spawn_file_actions_init(posix_spawn_file_actions_t *);
int posix_spawn_file_actions_destroy(posix_spawn_file_actions_t *);

int posix_spawn_file_actions_addopen(posix_spawn_file_actions_t * __restrict,
    int, const char * __restrict, int, mode_t);
int posix_spawn_file_actions_adddup2(posix_spawn_file_actions_t *, int, int);
int posix_spawn_file_actions_addclose(posix_spawn_file_actions_t *, int);

/*
 * Spawn attributes
 */
int posix_spawnattr_init(posix_spawnattr_t *);
int posix_spawnattr_destroy(posix_spawnattr_t *);

int posix_spawnattr_getflags(const posix_spawnattr_t * __restrict,
    short * __restrict);
int posix_spawnattr_getpgroup(const posix_spawnattr_t * __restrict,
    pid_t * __restrict);
int posix_spawnattr_getschedparam(const posix_spawnattr_t * __restrict,
    struct sched_param * __restrict);
int posix_spawnattr_getschedpolicy(const posix_spawnattr_t * __restrict,
    int * __restrict);
int posix_spawnattr_getsigdefault(const posix_spawnattr_t * __restrict,
    sigset_t * __restrict);
int posix_spawnattr_getsigmask(const posix_spawnattr_t * __restrict,
    sigset_t * __restrict sigmask);

int posix_spawnattr_setflags(posix_spawnattr_t *, short);
int posix_spawnattr_setpgroup(posix_spawnattr_t *, pid_t);
int posix_spawnattr_setschedparam(posix_spawnattr_t * __restrict,
    const struct sched_param * __restrict);
int posix_spawnattr_setschedpolicy(posix_spawnattr_t *, int);
int posix_spawnattr_setsigdefault(posix_spawnattr_t * __restrict,
    const sigset_t * __restrict);
int posix_spawnattr_setsigmask(posix_spawnattr_t * __restrict,
    const sigset_t * __restrict);
__END_DECLS

#endif /* !_SPAWN_H_ */
