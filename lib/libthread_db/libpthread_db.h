/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2004 David Xu <davidxu@freebsd.org>
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

#ifndef _LIBPTHREAD_DB_H_
#define	_LIBPTHREAD_DB_H_

#include <sys/ucontext.h>
#include <machine/reg.h>

#include "thread_db_int.h"

enum pt_type {
	PT_NONE,
	PT_USER,
	PT_LWP
};

struct pt_map {
	enum pt_type	type;
	union {
		lwpid_t		lwp;
		psaddr_t	thr;
	};
};

struct td_thragent {
	TD_THRAGENT_FIELDS;
	psaddr_t	libkse_debug_addr;
	psaddr_t	thread_list_addr;
	psaddr_t	thread_listgen_addr;
	psaddr_t	thread_activated_addr;
	psaddr_t	thread_active_threads_addr;
	psaddr_t	thread_keytable_addr;
	int		thread_activated;
	int		thread_off_dtv;
	int		thread_off_kse_locklevel;
	int		thread_off_kse;
	int		thread_off_tlsindex;
	int		thread_off_attr_flags;
	int		thread_size_key;
	int		thread_off_tcb;
	int		thread_off_linkmap;
	int		thread_off_tmbx;
	int		thread_off_thr_locklevel;
	int		thread_off_next;
	int		thread_off_state;
	int		thread_max_keys;
	int		thread_off_key_allocated;
	int		thread_off_key_destructor;
	int		thread_state_zoombie;
	int		thread_state_running;
	int		thread_off_sigmask;
	int		thread_off_sigpend;
	struct pt_map	*map;
	unsigned int	map_len;
};

void pt_md_init(void);
void pt_reg_to_ucontext(const struct reg *, ucontext_t *);
void pt_ucontext_to_reg(const ucontext_t *, struct reg *);
void pt_fpreg_to_ucontext(const struct fpreg *, ucontext_t *);
void pt_ucontext_to_fpreg(const ucontext_t *, struct fpreg *);
#ifdef __i386__
void pt_fxsave_to_ucontext(const char *, ucontext_t *);
void pt_ucontext_to_fxsave(const ucontext_t *, char *);
#endif
int  pt_reg_sstep(struct reg *reg, int step);

#endif /* _LIBPTHREAD_DB_H_ */
