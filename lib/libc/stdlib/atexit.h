/*	$OpenBSD: atexit.h,v 1.12 2017/12/16 20:06:56 guenther Exp $ */

/*
 * Copyright (c) 2002 Daniel Hartmeier
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *    - Redistributions of source code must retain the above copyright
 *      notice, this list of conditions and the following disclaimer.
 *    - Redistributions in binary form must reproduce the above
 *      copyright notice, this list of conditions and the following
 *      disclaimer in the documentation and/or other materials provided
 *      with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 * COPYRIGHT HOLDERS OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 * ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 */

struct atexit {
	struct atexit *next;		/* next in list */
	int ind;			/* next index in this table */
	int max;			/* max entries >= ATEXIT_SIZE */
	struct atexit_fn {
		void (*fn_ptr)(void *);
		void *fn_arg;		/* argument for CXA callback */
		void *fn_dso;		/* shared module handle */
	} fns[1];			/* the table itself */
};

/* a chain of these are hung off each thread's TIB's tib_atexit member */
struct thread_atexit_fn {
	void (*func)(void *);
	void *arg;
	struct thread_atexit_fn *next;
};

__BEGIN_HIDDEN_DECLS
extern struct atexit *__atexit;		/* points to head of LIFO stack */
__END_HIDDEN_DECLS

int	__cxa_atexit(void (*)(void *), void *, void *);
int	__cxa_thread_atexit(void (*)(void *), void *, void *);
void	__cxa_finalize(void *);

PROTO_NORMAL(__cxa_atexit);
PROTO_STD_DEPRECATED(__cxa_thread_atexit);
PROTO_NORMAL(__cxa_finalize);
