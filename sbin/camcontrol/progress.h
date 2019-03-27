/*	$NetBSD: progressbar.c,v 1.21 2009/04/12 10:18:52 lukem Exp $	*/

/*-
 * SPDX-License-Identifier: BSD-2-Clause-NetBSD
 *
 * Copyright (c) 1997-2012 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Luke Mewburn.
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
 *
 * $FreeBSD$
 */

#ifndef PROGRESS_H_
#define PROGRESS_H_	20100228

#include <sys/types.h>

#include <inttypes.h>

/* structure used to display a progress meter */
typedef struct progress_t {
	char		*prefix;	/* any prefix explanation */
	uint64_t	 size;		/* total of bytes/units to be counted */
	uint64_t	 done;		/* number of units counted to date */
	uint64_t	 percent;	/* cache the percentage complete */
	time_t		 start;		/* time we started this */
	time_t		 now;		/* time now */
	time_t		 eta;		/* estimated # of secs until completion */
	int64_t		 elapsed;	/* cached # of elapsed seconds */
	int32_t		 ttywidth;	/* width of tty in columns */
} progress_t;

int progress_init(progress_t */*meter*/, const char */*prefix*/, uint64_t /*size*/);
int progress_update(progress_t */*meter*/, uint64_t /*done*/);
int progress_draw(progress_t */*meter*/);
int progress_reset_size(progress_t */*meter*/, uint64_t /*size*/);
int progress_complete(progress_t */*meter*/, uint64_t /*done*/);

#endif
