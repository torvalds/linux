/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2010 The FreeBSD Foundation
 * All rights reserved.
 *
 * This software was developed by Pawel Jakub Dawidek under sponsorship from
 * the FreeBSD Foundation.
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHORS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHORS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/queue.h>

#include <stdbool.h>
#include <stdlib.h>
#include <unistd.h>

#include <pjdlog.h>

#include "rangelock.h"

#ifndef	PJDLOG_ASSERT
#include <assert.h>
#define	PJDLOG_ASSERT(...)	assert(__VA_ARGS__)
#endif

#define	RANGELOCKS_MAGIC	0x94310c
struct rangelocks {
	int	 rls_magic;		/* Magic value. */
	TAILQ_HEAD(, rlock) rls_locks;	/* List of locked ranges. */
};

struct rlock {
	off_t	rl_start;
	off_t	rl_end;
	TAILQ_ENTRY(rlock) rl_next;
};

int
rangelock_init(struct rangelocks **rlsp)
{
	struct rangelocks *rls;

	PJDLOG_ASSERT(rlsp != NULL);

	rls = malloc(sizeof(*rls));
	if (rls == NULL)
		return (-1);

	TAILQ_INIT(&rls->rls_locks);

	rls->rls_magic = RANGELOCKS_MAGIC;
	*rlsp = rls;

	return (0);
}

void
rangelock_free(struct rangelocks *rls)
{
	struct rlock *rl;

	PJDLOG_ASSERT(rls->rls_magic == RANGELOCKS_MAGIC);

	rls->rls_magic = 0;

	while ((rl = TAILQ_FIRST(&rls->rls_locks)) != NULL) {
		TAILQ_REMOVE(&rls->rls_locks, rl, rl_next);
		free(rl);
	}
	free(rls);
}

int
rangelock_add(struct rangelocks *rls, off_t offset, off_t length)
{
	struct rlock *rl;

	PJDLOG_ASSERT(rls->rls_magic == RANGELOCKS_MAGIC);

	rl = malloc(sizeof(*rl));
	if (rl == NULL)
		return (-1);
	rl->rl_start = offset;
	rl->rl_end = offset + length;
	TAILQ_INSERT_TAIL(&rls->rls_locks, rl, rl_next);
	return (0);
}

void
rangelock_del(struct rangelocks *rls, off_t offset, off_t length)
{
	struct rlock *rl;

	PJDLOG_ASSERT(rls->rls_magic == RANGELOCKS_MAGIC);

	TAILQ_FOREACH(rl, &rls->rls_locks, rl_next) {
		if (rl->rl_start == offset && rl->rl_end == offset + length)
			break;
	}
	PJDLOG_ASSERT(rl != NULL);
	TAILQ_REMOVE(&rls->rls_locks, rl, rl_next);
	free(rl);
}

bool
rangelock_islocked(struct rangelocks *rls, off_t offset, off_t length)
{
	struct rlock *rl;
	off_t end;

	PJDLOG_ASSERT(rls->rls_magic == RANGELOCKS_MAGIC);

	end = offset + length;
	TAILQ_FOREACH(rl, &rls->rls_locks, rl_next) {
		if (rl->rl_start < end && rl->rl_end > offset)
			break;
	}
	return (rl != NULL);
}
