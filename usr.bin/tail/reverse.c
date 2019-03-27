/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1991, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * Edward Sze-Tyan Wang.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#if 0
#ifndef lint
static char sccsid[] = "@(#)reverse.c	8.1 (Berkeley) 6/6/93";
#endif /* not lint */
#endif

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/queue.h>
#include <sys/stat.h>
#include <sys/mman.h>

#include <err.h>
#include <errno.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "extern.h"

static void r_buf(FILE *, const char *);
static void r_reg(FILE *, const char *, enum STYLE, off_t, struct stat *);

/*
 * reverse -- display input in reverse order by line.
 *
 * There are six separate cases for this -- regular and non-regular
 * files by bytes, lines or the whole file.
 *
 * BYTES	display N bytes
 *	REG	mmap the file and display the lines
 *	NOREG	cyclically read characters into a wrap-around buffer
 *
 * LINES	display N lines
 *	REG	mmap the file and display the lines
 *	NOREG	cyclically read lines into a wrap-around array of buffers
 *
 * FILE		display the entire file
 *	REG	mmap the file and display the lines
 *	NOREG	cyclically read input into a linked list of buffers
 */
void
reverse(FILE *fp, const char *fn, enum STYLE style, off_t off, struct stat *sbp)
{
	if (style != REVERSE && off == 0)
		return;

	if (S_ISREG(sbp->st_mode))
		r_reg(fp, fn, style, off, sbp);
	else
		switch(style) {
		case FBYTES:
		case RBYTES:
			bytes(fp, fn, off);
			break;
		case FLINES:
		case RLINES:
			lines(fp, fn, off);
			break;
		case REVERSE:
			r_buf(fp, fn);
			break;
		default:
			break;
		}
}

/*
 * r_reg -- display a regular file in reverse order by line.
 */
static void
r_reg(FILE *fp, const char *fn, enum STYLE style, off_t off, struct stat *sbp)
{
	struct mapinfo map;
	off_t curoff, size, lineend;
	int i;

	if (!(size = sbp->st_size))
		return;

	map.start = NULL;
	map.mapoff = map.maxoff = size;
	map.fd = fileno(fp);
	map.maplen = 0;

	/*
	 * Last char is special, ignore whether newline or not. Note that
	 * size == 0 is dealt with above, and size == 1 sets curoff to -1.
	 */
	curoff = size - 2;
	lineend = size;
	while (curoff >= 0) {
		if (curoff < map.mapoff ||
		    curoff >= map.mapoff + (off_t)map.maplen) {
			if (maparound(&map, curoff) != 0) {
				ierr(fn);
				return;
			}
		}
		for (i = curoff - map.mapoff; i >= 0; i--) {
			if (style == RBYTES && --off == 0)
				break;
			if (map.start[i] == '\n')
				break;
		}
		/* `i' is either the map offset of a '\n', or -1. */
		curoff = map.mapoff + i;
		if (i < 0)
			continue;

		/* Print the line and update offsets. */
		if (mapprint(&map, curoff + 1, lineend - curoff - 1) != 0) {
			ierr(fn);
			return;
		}
		lineend = curoff + 1;
		curoff--;

		if (style == RLINES)
			off--;

		if (off == 0 && style != REVERSE) {
			/* Avoid printing anything below. */
			curoff = 0;
			break;
		}
	}
	if (curoff < 0 && mapprint(&map, 0, lineend) != 0) {
		ierr(fn);
		return;
	}
	if (map.start != NULL && munmap(map.start, map.maplen))
		ierr(fn);
}

#define BSZ	(128 * 1024)
typedef struct bfelem {
	TAILQ_ENTRY(bfelem) entries;
	size_t len;
	char l[BSZ];
} bfelem_t;

/*
 * r_buf -- display a non-regular file in reverse order by line.
 *
 * This is the function that saves the entire input, storing the data in a
 * doubly linked list of buffers and then displays them in reverse order.
 * It has the usual nastiness of trying to find the newlines, as there's no
 * guarantee that a newline occurs anywhere in the file, let alone in any
 * particular buffer.  If we run out of memory, input is discarded (and the
 * user warned).
 */
static void
r_buf(FILE *fp, const char *fn)
{
	struct bfelem *tl, *first = NULL;
	size_t llen;
	char *p;
	off_t enomem = 0;
	TAILQ_HEAD(bfhead, bfelem) head;

	TAILQ_INIT(&head);

	while (!feof(fp)) {
		size_t len;

		/*
		 * Allocate a new block and link it into place in a doubly
		 * linked list.  If out of memory, toss the LRU block and
		 * keep going.
		 */
		while ((tl = malloc(sizeof(bfelem_t))) == NULL) {
			first = TAILQ_FIRST(&head);
			if (TAILQ_EMPTY(&head))
				err(1, "malloc");
			enomem += first->len;
			TAILQ_REMOVE(&head, first, entries);
			free(first);
		}
		TAILQ_INSERT_TAIL(&head, tl, entries);

		/* Fill the block with input data. */
		len = 0;
		while ((!feof(fp)) && len < BSZ) {
			p = tl->l + len;
			len += fread(p, 1, BSZ - len, fp);
			if (ferror(fp)) {
				ierr(fn);
				return;
			}
		}

		tl->len = len;
	}

	if (enomem) {
		warnx("warning: %jd bytes discarded", (intmax_t)enomem);
		rval = 1;
	}

	/*
	 * Now print the lines in reverse order
	 * Outline:
	 *    Scan backward for "\n",
	 *    print forward to the end of the buffers
	 *    free any buffers that start after the "\n" just found
	 *    Loop
	 */
	tl = TAILQ_LAST(&head, bfhead);
	first = TAILQ_FIRST(&head);
	while (tl != NULL) {
		struct bfelem *temp;

		for (p = tl->l + tl->len - 1, llen = 0; p >= tl->l;
		    --p, ++llen) {
			int start = (tl == first && p == tl->l);

			if ((*p == '\n') || start) {
				struct bfelem *tr;

				if (llen && start && *p != '\n')
					WR(p, llen + 1);
				else if (llen) {
					WR(p + 1, llen);
					if (start && *p == '\n')
						WR(p, 1);
				}
				tr = TAILQ_NEXT(tl, entries);
				llen = 0;
				if (tr != NULL) {
					TAILQ_FOREACH_FROM_SAFE(tr, &head,
					    entries, temp) {
						if (tr->len)
							WR(&tr->l, tr->len);
						TAILQ_REMOVE(&head, tr,
						    entries);
						free(tr);
					}
				}
			}
		}
		tl->len = llen;
		tl = TAILQ_PREV(tl, bfhead, entries);
	}
	TAILQ_REMOVE(&head, first, entries);
	free(first);
}
