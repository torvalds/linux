/*	$NetBSD: file.c,v 1.5 2011/02/16 18:35:39 joerg Exp $	*/
/*	$FreeBSD$	*/
/*	$OpenBSD: file.c,v 1.11 2010/07/02 20:48:48 nicm Exp $	*/

/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 1999 James Howard and Dag-Erling Coïdan Smørgrav
 * Copyright (C) 2008-2010 Gabor Kovesdan <gabor@FreeBSD.org>
 * Copyright (C) 2010 Dimitry Andric <dimitry@andric.com>
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

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <wchar.h>
#include <wctype.h>

#include "grep.h"

#define	MAXBUFSIZ	(32 * 1024)
#define	LNBUFBUMP	80

static unsigned char *buffer;
static unsigned char *bufpos;
static size_t bufrem;
static size_t fsiz;

static unsigned char *lnbuf;
static size_t lnbuflen;

static inline int
grep_refill(struct file *f)
{
	ssize_t nr;

	if (filebehave == FILE_MMAP)
		return (0);

	bufpos = buffer;
	bufrem = 0;

	nr = read(f->fd, buffer, MAXBUFSIZ);
	if (nr < 0)
		return (-1);

	bufrem = nr;
	return (0);
}

static inline int
grep_lnbufgrow(size_t newlen)
{

	if (lnbuflen < newlen) {
		lnbuf = grep_realloc(lnbuf, newlen);
		lnbuflen = newlen;
	}

	return (0);
}

char *
grep_fgetln(struct file *f, struct parsec *pc)
{
	unsigned char *p;
	char *ret;
	size_t len;
	size_t off;
	ptrdiff_t diff;

	/* Fill the buffer, if necessary */
	if (bufrem == 0 && grep_refill(f) != 0)
		goto error;

	if (bufrem == 0) {
		/* Return zero length to indicate EOF */
		pc->ln.len= 0;
		return (bufpos);
	}

	/* Look for a newline in the remaining part of the buffer */
	if ((p = memchr(bufpos, fileeol, bufrem)) != NULL) {
		++p; /* advance over newline */
		ret = bufpos;
		len = p - bufpos;
		bufrem -= len;
		bufpos = p;
		pc->ln.len = len;
		return (ret);
	}

	/* We have to copy the current buffered data to the line buffer */
	for (len = bufrem, off = 0; ; len += bufrem) {
		/* Make sure there is room for more data */
		if (grep_lnbufgrow(len + LNBUFBUMP))
			goto error;
		memcpy(lnbuf + off, bufpos, len - off);
		/* With FILE_MMAP, this is EOF; there's no more to refill */
		if (filebehave == FILE_MMAP) {
			bufrem -= len;
			break;
		}
		off = len;
		/* Fetch more to try and find EOL/EOF */
		if (grep_refill(f) != 0)
			goto error;
		if (bufrem == 0)
			/* EOF: return partial line */
			break;
		if ((p = memchr(bufpos, fileeol, bufrem)) == NULL)
			continue;
		/* got it: finish up the line (like code above) */
		++p;
		diff = p - bufpos;
		len += diff;
		if (grep_lnbufgrow(len))
		    goto error;
		memcpy(lnbuf + off, bufpos, diff);
		bufrem -= diff;
		bufpos = p;
		break;
	}
	pc->ln.len = len;
	return (lnbuf);

error:
	pc->ln.len = 0;
	return (NULL);
}

/*
 * Opens a file for processing.
 */
struct file *
grep_open(const char *path)
{
	struct file *f;

	f = grep_malloc(sizeof *f);
	memset(f, 0, sizeof *f);
	if (path == NULL) {
		/* Processing stdin implies --line-buffered. */
		lbflag = true;
		f->fd = STDIN_FILENO;
	} else if ((f->fd = open(path, O_RDONLY)) == -1)
		goto error1;

	if (filebehave == FILE_MMAP) {
		struct stat st;

		if ((fstat(f->fd, &st) == -1) || (st.st_size > OFF_MAX) ||
		    (!S_ISREG(st.st_mode)))
			filebehave = FILE_STDIO;
		else {
			int flags = MAP_PRIVATE | MAP_NOCORE | MAP_NOSYNC;
#ifdef MAP_PREFAULT_READ
			flags |= MAP_PREFAULT_READ;
#endif
			fsiz = st.st_size;
			buffer = mmap(NULL, fsiz, PROT_READ, flags,
			     f->fd, (off_t)0);
			if (buffer == MAP_FAILED)
				filebehave = FILE_STDIO;
			else {
				bufrem = st.st_size;
				bufpos = buffer;
				madvise(buffer, st.st_size, MADV_SEQUENTIAL);
			}
		}
	}

	if ((buffer == NULL) || (buffer == MAP_FAILED))
		buffer = grep_malloc(MAXBUFSIZ);

	/* Fill read buffer, also catches errors early */
	if (bufrem == 0 && grep_refill(f) != 0)
		goto error2;

	/* Check for binary stuff, if necessary */
	if (binbehave != BINFILE_TEXT && fileeol != '\0' &&
	    memchr(bufpos, '\0', bufrem) != NULL)
		f->binary = true;

	return (f);

error2:
	close(f->fd);
error1:
	free(f);
	return (NULL);
}

/*
 * Closes a file.
 */
void
grep_close(struct file *f)
{

	close(f->fd);

	/* Reset read buffer and line buffer */
	if (filebehave == FILE_MMAP) {
		munmap(buffer, fsiz);
		buffer = NULL;
	}
	bufpos = buffer;
	bufrem = 0;

	free(lnbuf);
	lnbuf = NULL;
	lnbuflen = 0;
}
