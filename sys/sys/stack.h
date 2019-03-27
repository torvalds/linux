/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2005 Antoine Brodin
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

#ifndef _SYS_STACK_H_
#define	_SYS_STACK_H_

#include <sys/_stack.h>

struct sbuf;

/* MI Routines. */
struct stack	*stack_create(int);
void		 stack_destroy(struct stack *);
int		 stack_put(struct stack *, vm_offset_t);
void		 stack_copy(const struct stack *, struct stack *);
void		 stack_zero(struct stack *);
void		 stack_print(const struct stack *);
void		 stack_print_ddb(const struct stack *);
void		 stack_print_short(const struct stack *);
void		 stack_print_short_ddb(const struct stack *);
void		 stack_sbuf_print(struct sbuf *, const struct stack *);
void		 stack_sbuf_print_ddb(struct sbuf *, const struct stack *);
int		 stack_sbuf_print_flags(struct sbuf *, const struct stack *,
		 int);
#ifdef KTR
void		 stack_ktr(u_int, const char *, int, const struct stack *,
		    u_int);
#define	CTRSTACK(m, st, depth) do {					\
	if (KTR_COMPILE & (m))						\
		stack_ktr((m), __FILE__, __LINE__, st, depth);		\
	} while(0)
#else
#define	CTRSTACK(m, st, depth)
#endif

/* MD Routines. */
struct thread;
void		 stack_save(struct stack *);
void		 stack_save_td(struct stack *, struct thread *);
int		 stack_save_td_running(struct stack *, struct thread *);

#endif
