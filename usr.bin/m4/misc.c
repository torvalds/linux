/*	$OpenBSD: misc.c,v 1.46 2015/12/07 14:12:46 espie Exp $	*/
/*	$NetBSD: misc.c,v 1.6 1995/09/28 05:37:41 tls Exp $	*/

/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1989, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * Ozan Yigit at York University.
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

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/types.h>
#include <errno.h>
#include <unistd.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <stddef.h>
#include <string.h>
#include <err.h>
#include "mdef.h"
#include "stdd.h"
#include "extern.h"
#include "pathnames.h"


char *ep;		/* first free char in strspace */
static char *strspace;	/* string space for evaluation */
char *endest;		/* end of string space	       */
static size_t strsize = STRSPMAX;
static size_t bufsize = BUFSIZE;

unsigned char *buf;			/* push-back buffer	       */
unsigned char *bufbase;			/* the base for current ilevel */
unsigned char *bbase[MAXINP];		/* the base for each ilevel    */
unsigned char *bp;			/* first available character   */
unsigned char *endpbb;			/* end of push-back buffer     */


/*
 * find the index of second str in the first str.
 */
ptrdiff_t
indx(const char *s1, const char *s2)
{
	char *t;

	t = strstr(s1, s2);
	if (t == NULL)
		return (-1);
	else
		return (t - s1);
}
/*
 *  pushback - push character back onto input
 */
void
pushback(int c)
{
	if (c == EOF)
		return;
	if (bp >= endpbb)
		enlarge_bufspace();
	*bp++ = c;
}

/*
 *  pbstr - push string back onto input
 *          pushback is replicated to improve
 *          performance.
 */
void
pbstr(const char *s)
{
	size_t n;

	n = strlen(s);
	while (endpbb - bp <= (long)n)
		enlarge_bufspace();
	while (n > 0)
		*bp++ = s[--n];
}

/*
 *  pbnum - convert number to string, push back on input.
 */
void
pbnum(int n)
{
	pbnumbase(n, 10, 0);
}

void
pbnumbase(int n, int base, int d)
{
	static char digits[36] = "0123456789abcdefghijklmnopqrstuvwxyz";
	int num;
	int printed = 0;

	if (base > 36)
		m4errx(1, "base %d > 36: not supported.", base);

	if (base < 2)
		m4errx(1, "bad base %d for conversion.", base);

	num = (n < 0) ? -n : n;
	do {
		pushback(digits[num % base]);
		printed++;
	}
	while ((num /= base) > 0);

	if (n < 0)
		printed++;
	while (printed++ < d)
		pushback('0');

	if (n < 0)
		pushback('-');
}

/*
 *  pbunsigned - convert unsigned long to string, push back on input.
 */
void
pbunsigned(unsigned long n)
{
	do {
		pushback(n % 10 + '0');
	}
	while ((n /= 10) > 0);
}

void
initspaces(void)
{
	int i;

	strspace = xalloc(strsize+1, NULL);
	ep = strspace;
	endest = strspace+strsize;
	buf = xalloc(bufsize, NULL);
	bufbase = buf;
	bp = buf;
	endpbb = buf + bufsize;
	for (i = 0; i < MAXINP; i++)
		bbase[i] = buf;
}

void
enlarge_strspace(void)
{
	char *newstrspace;
	int i;

	strsize *= 2;
	newstrspace = malloc(strsize + 1);
	if (!newstrspace)
		errx(1, "string space overflow");
	memcpy(newstrspace, strspace, strsize/2);
	for (i = 0; i <= sp; i++)
		if (sstack[i] == STORAGE_STRSPACE)
			mstack[i].sstr = (mstack[i].sstr - strspace)
			    + newstrspace;
	ep = (ep-strspace) + newstrspace;
	free(strspace);
	strspace = newstrspace;
	endest = strspace + strsize;
}

void
enlarge_bufspace(void)
{
	unsigned char *newbuf;
	int i;

	bufsize += bufsize/2;
	newbuf = xrealloc(buf, bufsize, "too many characters pushed back");
	for (i = 0; i < MAXINP; i++)
		bbase[i] = (bbase[i]-buf)+newbuf;
	bp = (bp-buf)+newbuf;
	bufbase = (bufbase-buf)+newbuf;
	buf = newbuf;
	endpbb = buf+bufsize;
}

/*
 *  chrsave - put single char on string space
 */
void
chrsave(int c)
{
	if (ep >= endest)
		enlarge_strspace();
	*ep++ = c;
}

/*
 * read in a diversion file, and dispose it.
 */
void
getdiv(int n)
{
	int c;

	if (active == outfile[n])
		m4errx(1, "undivert: diversion still active.");
	rewind(outfile[n]);
	while ((c = getc(outfile[n])) != EOF)
		putc(c, active);
	(void) fclose(outfile[n]);
	outfile[n] = NULL;
}

void
onintr(int signo __unused)
{
#define intrmessage	"m4: interrupted.\n"
	write(STDERR_FILENO, intrmessage, sizeof(intrmessage)-1);
	_exit(1);
}

/*
 * killdiv - get rid of the diversion files
 */
void
killdiv(void)
{
	int n;

	for (n = 0; n < maxout; n++)
		if (outfile[n] != NULL) {
			(void) fclose(outfile[n]);
		}
}

extern char *__progname;

void
m4errx(int eval, const char *fmt, ...)
{
	fprintf(stderr, "%s: ", __progname);
	fprintf(stderr, "%s at line %lu: ", CURRENT_NAME, CURRENT_LINE);
	if (fmt != NULL) {
		va_list ap;

		va_start(ap, fmt);
		vfprintf(stderr, fmt, ap);
		va_end(ap);
	}
	fprintf(stderr, "\n");
	exit(eval);
}

/*
 * resizedivs: allocate more diversion files */
void
resizedivs(int n)
{
	int i;

	outfile = xreallocarray(outfile, n, sizeof(FILE *),
	    "too many diverts %d", n);
	for (i = maxout; i < n; i++)
		outfile[i] = NULL;
	maxout = n;
}

void *
xalloc(size_t n, const char *fmt, ...)
{
	void *p = malloc(n);

	if (p == NULL) {
		if (fmt == NULL)
			err(1, "malloc");
		else {
			va_list va;

			va_start(va, fmt);
			verr(1, fmt, va);
			va_end(va);
		}
	}
	return p;
}

void *
xcalloc(size_t n, size_t s, const char *fmt, ...)
{
	void *p = calloc(n, s);

	if (p == NULL) {
		if (fmt == NULL)
			err(1, "calloc");
		else {
			va_list va;

			va_start(va, fmt);
			verr(1, fmt, va);
			va_end(va);
		}
	}
	return p;
}

void *
xrealloc(void *old, size_t n, const char *fmt, ...)
{
	char *p = realloc(old, n);

	if (p == NULL) {
		free(old);
		if (fmt == NULL)
			err(1, "realloc");
		else {
			va_list va;

			va_start(va, fmt);
			verr(1, fmt, va);
			va_end(va);
		}
	}
	return p;
}

void *
xreallocarray(void *old, size_t s1, size_t s2, const char *fmt, ...)
{
	void *p = reallocarray(old, s1, s2);

	if (p == NULL) {
		free(old);
		if (fmt == NULL)
			err(1, "reallocarray");
		else {
			va_list va;

			va_start(va, fmt);
			verr(1, fmt, va);
			va_end(va);
		}
	}
	return p;
}

char *
xstrdup(const char *s)
{
	char *p = strdup(s);
	if (p == NULL)
		err(1, "strdup");
	return p;
}

void
usage(void)
{
	fprintf(stderr, "usage: m4 [-gPs] [-Dname[=value]] [-d flags] "
			"[-I dirname] [-o filename]\n"
			"\t[-t macro] [-Uname] [file ...]\n");
	exit(1);
}

int
obtain_char(struct input_file *f)
{
	if (f->c == EOF)
		return EOF;

	f->c = fgetc(f->file);
	if (f->c == '\n')
		f->lineno++;

	return f->c;
}

void
set_input(struct input_file *f, FILE *real, const char *name)
{
	f->file = real;
	f->lineno = 1;
	f->c = 0;
	f->name = xstrdup(name);
	emit_synchline();
}

void
do_emit_synchline(void)
{
	fprintf(active, "#line %lu \"%s\"\n",
	    infile[ilevel].lineno, infile[ilevel].name);
	infile[ilevel].synch_lineno = infile[ilevel].lineno;
}

void
release_input(struct input_file *f)
{
	if (ferror(f->file))
		errx(1, "Fatal error reading from %s\n", f->name);
	if (f->file != stdin)
	    fclose(f->file);
	f->c = EOF;
	/*
	 * XXX can't free filename, as there might still be
	 * error information pointing to it.
	 */
}

void
doprintlineno(struct input_file *f)
{
	pbunsigned(f->lineno);
}

void
doprintfilename(struct input_file *f)
{
	pbstr(rquote);
	pbstr(f->name);
	pbstr(lquote);
}

/*
 * buffer_mark/dump_buffer: allows one to save a mark in a buffer,
 * and later dump everything that was added since then to a file.
 */
size_t
buffer_mark(void)
{
	return bp - buf;
}


void
dump_buffer(FILE *f, size_t m)
{
	unsigned char *s;

	for (s = bp; s-buf > (long)m;)
		fputc(*--s, f);
}
