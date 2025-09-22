/*	$OpenBSD: isr.h,v 1.4 2008/06/26 05:42:11 ray Exp $	*/
/*	$NetBSD: isr.h,v 1.1 2000/01/05 08:49:04 nisimura Exp $	*/

/*-
 * Copyright (c) 1996 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Jason R. Thorpe.
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
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/evcount.h>
#include <sys/queue.h>

/*
 * The max size of the autovectored interrupt.
 */
#define NISRAUTOVEC	8

/*
 * Autovectored interrupt handler cookie.
 */
struct isr_autovec {
	LIST_ENTRY(isr_autovec) isr_link;
	int		(*isr_func)(void *);
	void		*isr_arg;
	int		isr_ipl;
	int		isr_priority;
	struct evcount	isr_count;
};

typedef LIST_HEAD(, isr_autovec) isr_autovec_list_t;

/*
 * Autovectored ISR priorities.  These are not the same as interrupt levels.
 */
#define ISRPRI_BIO		0
#define ISRPRI_NET		1
#define ISRPRI_TTY		2
#define ISRPRI_TTYNOBUF		3

void	isrinit(void);
void	isrlink_autovec(int (*)(void *), void *, int, int, const char *);
void	isrdispatch_autovec(int);
