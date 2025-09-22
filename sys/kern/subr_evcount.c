/*	$OpenBSD: subr_evcount.c,v 1.16 2023/09/16 09:33:27 mpi Exp $ */
/*
 * Copyright (c) 2004 Artur Grabowski <art@openbsd.org>
 * Copyright (c) 2004 Aaron Campbell <aaron@openbsd.org>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES,
 * INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY
 * AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL
 * THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL  DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/param.h>
#include <sys/evcount.h>
#include <sys/systm.h>
#include <sys/sysctl.h>
#include <sys/percpu.h>

static TAILQ_HEAD(,evcount) evcount_list = TAILQ_HEAD_INITIALIZER(evcount_list);
static TAILQ_HEAD(,evcount) evcount_percpu_init_list =
    TAILQ_HEAD_INITIALIZER(evcount_percpu_init_list);
static int evcount_percpu_done;

void
evcount_attach(struct evcount *ec, const char *name, void *data)
{
	static int nextid = 0;

	memset(ec, 0, sizeof(*ec));
	ec->ec_name = name;
	ec->ec_id = ++nextid;
	ec->ec_data = data;
	TAILQ_INSERT_TAIL(&evcount_list, ec, next);
}

void
evcount_percpu(struct evcount *ec)
{
	if (evcount_percpu_done == 0) {
		TAILQ_REMOVE(&evcount_list, ec, next);
		TAILQ_INSERT_TAIL(&evcount_percpu_init_list, ec, next);
	} else {
		ec->ec_percpu = counters_alloc(1);
	}
}

void
evcount_init_percpu(void)
{
	struct evcount *ec;

	KASSERT(evcount_percpu_done == 0);
	TAILQ_FOREACH(ec, &evcount_percpu_init_list, next) {
		ec->ec_percpu = counters_alloc(1);
		counters_add(ec->ec_percpu, 0, ec->ec_count);
		ec->ec_count = 0;
	}
	TAILQ_CONCAT(&evcount_list, &evcount_percpu_init_list, next);
	evcount_percpu_done = 1;
}

void
evcount_detach(struct evcount *ec)
{
	TAILQ_REMOVE(&evcount_list, ec, next);
	if (ec->ec_percpu != NULL) {
		counters_free(ec->ec_percpu, 1);
		ec->ec_percpu = NULL;
	}
}

void
evcount_inc(struct evcount *ec)
{
	if (ec->ec_percpu != NULL)
		counters_inc(ec->ec_percpu, 0);
	else
		ec->ec_count++;
}

#ifndef	SMALL_KERNEL

int
evcount_sysctl(int *name, u_int namelen, void *oldp, size_t *oldlenp,
    void *newp, size_t newlen)
{
	int error = 0, s, nintr, i;
	struct evcount *ec;
	uint64_t count, scratch;

	if (newp != NULL)
		return (EPERM);

	if (name[0] != KERN_INTRCNT_NUM) {
		if (namelen != 2)
			return (ENOTDIR);
		if (name[1] < 0)
			return (EINVAL);
		i = name[1];
	} else
		i = -1;

	nintr = 0;
	TAILQ_FOREACH(ec, &evcount_list, next) {
		if (nintr++ == i)
			break;
	}

	switch (name[0]) {
	case KERN_INTRCNT_NUM:
		error = sysctl_rdint(oldp, oldlenp, NULL, nintr);
		break;
	case KERN_INTRCNT_CNT:
		if (ec == NULL)
			return (ENOENT);
		if (ec->ec_percpu != NULL) {
			counters_read(ec->ec_percpu, &count, 1, &scratch);
		} else {
			s = splhigh();
			count = ec->ec_count;
			splx(s);
		}
		error = sysctl_rdquad(oldp, oldlenp, NULL, count);
		break;
	case KERN_INTRCNT_NAME:
		if (ec == NULL)
			return (ENOENT);
		error = sysctl_rdstring(oldp, oldlenp, NULL, ec->ec_name);
		break;
	case KERN_INTRCNT_VECTOR:
		if (ec == NULL || ec->ec_data == NULL)
			return (ENOENT);
		error = sysctl_rdint(oldp, oldlenp, NULL,
		    *((int *)ec->ec_data));
		break;
	default:
		error = EOPNOTSUPP;
		break;
	}

	return (error);
}
#endif	/* SMALL_KERNEL */
