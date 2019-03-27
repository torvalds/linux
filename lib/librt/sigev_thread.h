/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2005 David Xu <davidxu@freebsd.org>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice unmodified, this list of conditions, and the following
 *    disclaimer.
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
 *
 */

#ifndef _SIGEV_THREAD_H_
#define _SIGEV_THREAD_H_

#include <sys/types.h>
#include <sys/queue.h>

struct sigev_thread;
struct sigev_node;

typedef uintptr_t	sigev_id_t;
typedef void		(*sigev_dispatch_t)(struct sigev_node *);

struct sigev_node {
	LIST_ENTRY(sigev_node)		sn_link;
	int				sn_type;
	sigev_id_t			sn_id;
	sigev_dispatch_t		sn_dispatch;
	union sigval			sn_value;
	void 				*sn_func;
	int				sn_flags;
	int				sn_gen;
	siginfo_t			sn_info;
	pthread_attr_t			sn_attr;
	struct sigev_thread		*sn_tn;
};


struct sigev_thread {
	LIST_ENTRY(sigev_thread)	tn_link;
	pthread_t			tn_thread;
	struct sigev_node		*tn_cur;
	int				tn_refcount;
	long				tn_lwpid;
	pthread_cond_t			tn_cv;
};

#define	SNF_WORKING		0x01
#define	SNF_REMOVED		0x02
#define	SNF_SYNC		0x04

int	__sigev_check_init();
struct sigev_node *__sigev_alloc(int, const struct sigevent *,
	struct sigev_node *, int);
struct sigev_node *__sigev_find(int, sigev_id_t);
void	__sigev_get_sigevent(struct sigev_node *, struct sigevent *,
		sigev_id_t);
int	__sigev_register(struct sigev_node *);
int	__sigev_delete(int, sigev_id_t);
int	__sigev_delete_node(struct sigev_node *);
void	__sigev_list_lock(void);
void	__sigev_list_unlock(void);
void	__sigev_free(struct sigev_node *);

#endif
