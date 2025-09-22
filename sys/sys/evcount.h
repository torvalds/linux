/*	$OpenBSD: evcount.h,v 1.4 2022/11/10 07:05:41 jmatthew Exp $ */
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

#ifndef __SYS_EVCOUNT_H__
#define __SYS_EVCOUNT_H__

#ifdef _KERNEL

#include <sys/queue.h>

struct cpumem;

struct evcount {
	u_int64_t		ec_count;	/* main counter */
	int			ec_id;		/* counter ID */
	const char		*ec_name;	/* counter name */
	void			*ec_data;	/* user data */
	struct cpumem		*ec_percpu;	/* per-cpu counter */

	TAILQ_ENTRY(evcount)	next;
};

void evcount_attach(struct evcount *, const char *, void *);
void evcount_detach(struct evcount *);
void evcount_inc(struct evcount *);
void evcount_init_percpu(void);
void evcount_percpu(struct evcount *);
int evcount_sysctl(int *, u_int, void *, size_t *, void *, size_t);

#endif /* _KERNEL */

#endif
