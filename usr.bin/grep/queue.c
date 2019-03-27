/*	$NetBSD: queue.c,v 1.5 2011/08/31 16:24:57 plunky Exp $	*/
/*	$FreeBSD$	*/

/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 1999 James Howard and Dag-Erling Coïdan Smørgrav
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
 */

/*
 * A really poor man's queue.  It does only what it has to and gets out of
 * Dodge.  It is used in place of <sys/queue.h> to get a better performance.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/queue.h>

#include <stdlib.h>
#include <string.h>

#include "grep.h"

struct qentry {
	STAILQ_ENTRY(qentry)	list;
	struct str	 	data;
};

static STAILQ_HEAD(, qentry)	queue = STAILQ_HEAD_INITIALIZER(queue);
static long long		count;

static struct qentry	*dequeue(void);

/*
 * Enqueue another line; return true if we've dequeued a line as a result
 */
bool
enqueue(struct str *x)
{
	struct qentry *item;

	item = grep_malloc(sizeof(struct qentry));
	item->data.dat = grep_malloc(sizeof(char) * x->len);
	item->data.len = x->len;
	item->data.line_no = x->line_no;
	item->data.boff = x->boff;
	item->data.off = x->off;
	memcpy(item->data.dat, x->dat, x->len);
	item->data.file = x->file;

	STAILQ_INSERT_TAIL(&queue, item, list);

	if (++count > Bflag) {
		item = dequeue();
		free(item->data.dat);
		free(item);
		return (true);
	}
	return (false);
}

static struct qentry *
dequeue(void)
{
	struct qentry *item;

	item = STAILQ_FIRST(&queue);
	if (item == NULL)
		return (NULL);

	STAILQ_REMOVE_HEAD(&queue, list);
	--count;
	return (item);
}

void
printqueue(void)
{
	struct qentry *item;

	while ((item = dequeue()) != NULL) {
		grep_printline(&item->data, '-');
		free(item->data.dat);
		free(item);
	}
}

void
clearqueue(void)
{
	struct qentry *item;

	while ((item = dequeue()) != NULL) {
		free(item->data.dat);
		free(item);
	}
}
