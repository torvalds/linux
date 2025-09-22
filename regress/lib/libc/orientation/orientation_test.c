/*	$OpenBSD: orientation_test.c,v 1.5 2014/04/22 02:29:52 lteo Exp $ */

/*
 * Copyright (c) 2009 Philip Guenther
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
 */

/*
 * Test whether the various stdio functions set the stream orientation
 * ("width") as they should
 */

#include <sys/types.h>
#include <err.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <wchar.h>

char filename[] = "/tmp/fwide.XXXXXXXXXX";

FILE *dup_stdout = NULL;
int failures = 0;

void
fail(int line, int r, char const *expect, char const *test)
{
	failures++;
	fprintf(dup_stdout,
		"FAIL: %d: fwide returned %d, expected %s 0 after %s\n",
		line, r, expect, test);
}

FILE *
setup(int line)
{
	FILE	*f;
	int	r;

	if ((f = fopen(filename, "r+")) == NULL)
		err(2, "fopen");
	if ((r = fwide(f, 0)) != 0)
		fail(line, r, "==", "fopen");
	return (f);
}

FILE *
setup_std(FILE *std, int line)
{
	int	r;

	if (freopen(filename, "r+", std) == NULL)
		err(2, "freopen");
	if ((r = fwide(std, 0)) != 0)
		fail(line, r, "==", "freopen");
	return (std);
}

#define TEST_(x, op)						\
	do {							\
		f = setup(__LINE__);				\
		x;						\
		if (!((r = fwide(f, 0)) op 0))			\
			fail(__LINE__, r, #op, #x);		\
		fclose(f);					\
	} while (0)

#define TEST_STD_(std, x, op)					\
	do {							\
		f = setup_std(std, __LINE__);			\
		x;						\
		if (!((r = fwide(f, 0)) op 0))			\
			fail(__LINE__, r, #op, #x);		\
	} while (0)

#define TEST_UNCHANGED(x)		TEST_(x, ==)
#define TEST_NARROW(x)			TEST_(x, <)
#define TEST_WIDE(x)			TEST_(x, >)
#define TEST_UNCHANGED_STD(std, x)	TEST_STD_(std, x, ==)
#define TEST_NARROW_STD(std, x)		TEST_STD_(std, x, <)
#define TEST_WIDE_STD(std, x)		TEST_STD_(std, x, >)

int
main(int argc, char *argv[])
{
	char	buffer[BUFSIZ];
	wchar_t	wbuffer[BUFSIZ];
	char	*buf;
	wchar_t	*wbuf;
	FILE	*f;
	off_t	off;
	fpos_t	pos;
	size_t	size;
	int	fd, r;
	char	c;
	wchar_t	wc;

	if ((fd = dup(1)) == -1)
		err(2, "dup");
	if ((dup_stdout = fdopen(fd, "w")) == NULL)
		err(2, "fdopen");
	if ((fd = mkstemp(filename)) == -1)
		err(2, "mkstemp");
	if (write(fd, "0123456789\n\n", 12) != 12 || close(fd))
		err(2, "write + close");

	/* status */
	TEST_UNCHANGED(fwide(f, 0));
	TEST_NARROW(fwide(f, -1));
	TEST_WIDE(fwide(f, 1));
	TEST_UNCHANGED(feof(f));
	TEST_UNCHANGED(ferror(f));
	TEST_UNCHANGED(fileno(f));
	TEST_UNCHANGED(clearerr(f));

	/* flush and purge */
	TEST_UNCHANGED(fflush(f));
	TEST_UNCHANGED(fpurge(f));

	/* positioning */
	TEST_UNCHANGED(fgetpos(f, &pos));
	TEST_UNCHANGED(fgetpos(f, &pos); fsetpos(f, &pos));
	TEST_UNCHANGED(ftell(f));
	TEST_UNCHANGED(ftello(f));
	TEST_UNCHANGED(fseek(f, 1, SEEK_CUR));
	TEST_UNCHANGED(fseek(f, 1, SEEK_SET));
	TEST_UNCHANGED(fseek(f, 1, SEEK_END));
	TEST_UNCHANGED(fseeko(f, 1, SEEK_CUR));
	TEST_UNCHANGED(fseeko(f, 1, SEEK_SET));
	TEST_UNCHANGED(fseeko(f, 1, SEEK_END));
	TEST_UNCHANGED(rewind(f));

	/* buffering */
	TEST_UNCHANGED(setbuf(f, NULL));
	TEST_UNCHANGED(setbuf(f, buffer));
	TEST_UNCHANGED(setvbuf(f, buffer, _IONBF, BUFSIZ));
	TEST_UNCHANGED(setvbuf(f, buffer, _IOLBF, BUFSIZ));
	TEST_UNCHANGED(setvbuf(f, buffer, _IOFBF, BUFSIZ));
	TEST_UNCHANGED(setvbuf(f, NULL, _IONBF, 0));
	TEST_UNCHANGED(setvbuf(f, NULL, _IOLBF, 0));
	TEST_UNCHANGED(setvbuf(f, NULL, _IOFBF, 0));
	TEST_UNCHANGED(setbuffer(f, NULL, 0));
	TEST_UNCHANGED(setbuffer(f, buffer, BUFSIZ));
	TEST_UNCHANGED(setlinebuf(f));

	/* locking */
	TEST_UNCHANGED(flockfile(f);funlockfile(f));
	TEST_UNCHANGED(ftrylockfile(f);funlockfile(f));

	/* input */
	TEST_NARROW(getc(f));
	TEST_NARROW(getc_unlocked(f));
	TEST_NARROW(fgetc(f));
	TEST_NARROW(c = fgetc(f); ungetc(c, f));
	TEST_NARROW(fgets(buffer, BUFSIZ, f));
	TEST_NARROW(fscanf(f, "%s\n", buffer));
	TEST_NARROW(fgetln(f, &size));

	/* output */
	TEST_NARROW(putc('c', f));
	TEST_NARROW(putc_unlocked('c', f));
	TEST_NARROW(fputc('c', f));
	TEST_NARROW(fputs("foo", f));
	TEST_NARROW(fprintf(f, "%s\n", "foo"));

	/* input from stdin */
	TEST_NARROW_STD(stdin, getchar());
	TEST_NARROW_STD(stdin, getchar_unlocked());
	TEST_NARROW_STD(stdin, scanf("%s\n", buffer));

	/* output to stdout */
	TEST_NARROW_STD(stdout, putchar('c'));
	TEST_NARROW_STD(stdout, putchar_unlocked('c'));
	TEST_NARROW_STD(stdout, puts("foo"));
	TEST_NARROW_STD(stdout, printf("foo"));

	/* word-size ops */
	/*
	 * fread and fwrite are specified as being implemented in
	 * terms of fgetc() and fputc() and therefore must set the
	 * stream orientation to narrow.
	 */
	TEST_NARROW(fread(buffer, 4, BUFSIZ / 4, f));
	TEST_NARROW(fwrite(buffer, 4, BUFSIZ / 4, f));

	/*
	 * getw() and putw() aren't specified anywhere but logically
	 * should behave the same as fread/fwrite.  Not all OSes agree:
	 * Solaris 10 has them not changing the orientation.
	 */
	TEST_NARROW(getw(f));
	TEST_NARROW(putw(1234, f));


	/* WIDE CHAR TIME! */

	/* input */
	TEST_WIDE(getwc(f));
	TEST_WIDE(fgetwc(f));
	TEST_WIDE(wc = fgetwc(f); ungetwc(wc, f));
	TEST_WIDE(fgetws(wbuffer, BUFSIZ, f));
	TEST_WIDE(fwscanf(f, L"%s\n", wbuffer));

	/* output */
	TEST_WIDE(putwc(L'c', f));
	TEST_WIDE(fputwc(L'c', f));
	TEST_WIDE(fputws(L"foo", f));
	TEST_WIDE(fwprintf(f, L"%s\n", L"foo"));

	/* input from stdin */
	TEST_WIDE_STD(stdin, getwchar());
	TEST_WIDE_STD(stdin, wscanf(L"%s\n", wbuffer));

	/* output to stdout */
	TEST_WIDE_STD(stdout, putwchar(L'c'));
	TEST_WIDE_STD(stdout, wprintf(L"foo"));


	/* memory streams */
	f = open_memstream(&buf, &size);
	if (!((r = fwide(f, 0)) < 0))
		fail(__LINE__, r, "<", "open_memstream()");
	fclose(f);
	f = open_wmemstream(&wbuf, &size);
	if (!((r = fwide(f, 0)) > 0))
		fail(__LINE__, r, ">", "open_wmemstream()");
	fclose(f);


	/* random stuff? */
	TEST_UNCHANGED_STD(stderr, perror("foo"));

	remove(filename);
	if (failures)
		exit(1);
	exit(0);
}

