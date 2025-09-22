/*	$OpenBSD: test_fflush.c,v 1.3 2025/06/08 08:53:53 yasuoka Exp $	*/

/*
 * Copyright (c) 2025 YASUOKA Masahiko <yasuoka@yasuoka.net>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <assert.h>
#include <locale.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <wchar.h>

/* we use assert() */
#undef	NDEBUG

#define	TMPFILENAME	"test_fflush.tmp"

void setup(void);

void test_fflush_read0(void);
void test_fflush_read1(void);
void test_fflush_read2(void);
void test_fflush_read3(void);
void test_fflush_read4(void);
void setupw(void);
void test_fflush_read5(void);
void test_fflush_read6(void);

void
setup(void)
{
	FILE	*fp;

	/* common setup */
	unlink(TMPFILENAME);
	fp = fopen(TMPFILENAME, "w+");
	assert(fp != NULL);
	fputs("Hello world\n", fp);
	fclose(fp);
}

/* fflush work with reading file and seekable */
void
test_fflush_read0(void)
{
	int	 r;
	char	 buf[80];
	FILE	*fp;

	setup();

	/* In POSIX 2008, fflush() must work with the file object for reading */
	fp = fopen(TMPFILENAME, "r");
	assert(fp != NULL);
	assert(fgetc(fp) == 'H');
	r = fflush(fp);
	assert(r == 0);

	/* the position is moved to 1 */
	assert(ftell(fp) == 1);

	/* can read rest of that */
	fgets(buf, sizeof(buf), fp);
	assert(strcmp(buf, "ello world\n") == 0);
	r = fclose(fp);
	assert(r == 0);
}

/* fflush work with reading file and seekable + unget */
void
test_fflush_read1(void)
{
	int	 r;
	char	 buf[80];
	FILE	*fp;

	setup();

	fp = fopen(TMPFILENAME, "r");
	assert(fp != NULL);
	assert(fgetc(fp) == 'H');
	assert(fgetc(fp) == 'e');
	assert(fgetc(fp) == 'l');
	assert(fgetc(fp) == 'l');
	assert(fgetc(fp) == 'o');

	/* push the 'AAAA' back */
	ungetc('A', fp);
	ungetc('A', fp);
	ungetc('A', fp);
	ungetc('A', fp);

	/* can read rest of that */
	fgets(buf, sizeof(buf), fp);
	assert(strcmp(buf, "AAAA world\n") == 0);
	r = fclose(fp);
	assert(r == 0);

	/* do the same thing + fflush */

	fp = fopen(TMPFILENAME, "r");
	assert(fp != NULL);
	assert(fgetc(fp) == 'H');
	assert(fgetc(fp) == 'e');
	assert(fgetc(fp) == 'l');
	assert(fgetc(fp) == 'l');
	assert(fgetc(fp) == 'o');

	/* push 'AAAA' back */
	ungetc('A', fp);
	ungetc('A', fp);
	ungetc('A', fp);
	ungetc('A', fp);

	/* then fflush */
	r = fflush(fp);
	assert(r == 0);

	/* fllush() clears the all pushed back chars */

	/* can read rest of that */
	fgets(buf, sizeof(buf), fp);
	assert(strcmp(buf, " world\n") == 0);
	r = fclose(fp);
	assert(r == 0);
}

/* fflush() to reading and non-seekable stream */
void
test_fflush_read2(void)
{
	int	 r;
	FILE	*fp;
	char	 buf[80];

	/* In POSIX-2008, fflush() must work with the file object for reading */
	fp = popen("echo Hello world", "r");
	assert(fp != NULL);
	assert(fgetc(fp) == 'H');
	r = fflush(fp);
	assert(r == 0);

	/*
	 * FILE object for read and NOT seekable.  In that case, fflush does
	 * nothing, but must keep the buffer.
	 */

	/* can read rest of that */
	fgets(buf, sizeof(buf), fp);
	assert(strcmp(buf, "ello world\n") == 0);
	r = pclose(fp);
	assert(r == 0);
}

/* fflush() to the file which doesn't have any buffer */
void
test_fflush_read3(void)
{
	int	 r;
	FILE	*fp;

	setup();

	/* In POSIX-2008, fflush() must work with the file object for reading */
	fp = fopen(TMPFILENAME, "r");
	assert(fp != NULL);
	r = fflush(fp);
	assert(r == 0);
	r = fclose(fp);
	assert(r == 0);
}

/* freopen() should call fflush() internal */
void
test_fflush_read4(void)
{
	int	 r;
	FILE	*fp;
	off_t	 pos;
	char	 buf[80];

	setup();

	/* In POSIX-2008, fflush() must work with the file object for reading */
	fp = fopen(TMPFILENAME, "r");
	assert(fp != NULL);

	assert(fgetc(fp) == 'H');	/* read 1 */

	pos = lseek(fileno(fp), 0, SEEK_CUR);
	assert(pos >= 1);
	assert(pos > 1);	/* this test assume the buffer is used */

	/* freopen() should call fflush() internal */
	fp = freopen(TMPFILENAME, "r", fp);
	assert(fp != NULL);

	/* can read rest of that on fp */
	fgets(buf, sizeof(buf), fp);
	assert(strcmp(buf, "Hello world\n") == 0);

	r = fclose(fp);
	assert(r == 0);
}

void
setupw(void)
{
	FILE	*fp;

	/* common setup */
	unlink(TMPFILENAME);
	fp = fopen(TMPFILENAME, "w+");
	assert(fp != NULL);
	/* Konnitiwa Sekai(in Kanji) */
	fputws(L"\u3053\u3093\u306b\u3061\u308f \u4e16\u754c\n", fp);
	fclose(fp);
}

/* fflush work with reading file and seekable + ungetwc */
void
test_fflush_read5(void)
{
	int	 r;
	wchar_t	 buf[80];
	FILE	*fp;

	setupw();

	fp = fopen(TMPFILENAME, "r");

	assert(fp != NULL);
	assert(fgetwc(fp) == L'\u3053');	/* Ko */
	assert(fgetwc(fp) == L'\u3093');	/* N  */
	assert(fgetwc(fp) == L'\u306b');	/* Ni */
	assert(fgetwc(fp) == L'\u3061');	/* Ti */
	assert(fgetwc(fp) == L'\u308f');	/* Wa */

	/* push 263A(smile) back */
	assert(ungetwc(L'\u263a', fp));

	/* we support 1 push back wchar_t */
	assert(fgetwc(fp) == L'\u263a');

	/* can read reset of that */
	fgetws(buf, sizeof(buf), fp);
	assert(wcscmp(buf, L" \u4e16\u754c\n") == 0);

	r = fclose(fp);
	assert(r == 0);

	/* do the same thing + fflush */
	fp = fopen(TMPFILENAME, "r");

	assert(fp != NULL);
	assert(fgetwc(fp) == L'\u3053');	/* Ko */
	assert(fgetwc(fp) == L'\u3093');	/* N  */
	assert(fgetwc(fp) == L'\u306b');	/* Ni */
	assert(fgetwc(fp) == L'\u3061');	/* Ti */
	assert(fgetwc(fp) == L'\u308f');	/* Wa */

	/* push 263A(smile) back */
	assert(ungetwc(L'\u263a', fp));

	/* we support 1 push back wchar_t */
	assert(fgetwc(fp) == L'\u263a');

	/* then fflush */
	r = fflush(fp);
	assert(r == 0);

	/* fllush() clears the all pushed back chars */

	/* can read rest of that */
	fgetws(buf, sizeof(buf), fp);
	assert(wcscmp(buf, L" \u4e16\u754c\n") == 0);
	r = fclose(fp);
	assert(r == 0);
}

void
test_fflush_read6(void)
{
	int	 r, c;
	FILE	*fp;

	setup();
	fp = fopen(TMPFILENAME, "r");
	assert(fp != NULL);

	/*
	 * https://pubs.opengroup.org/onlinepubs/9699919799/functions/fflush.html
	 * .. any characters pushed back onto the stream by ungetc() or ungetwc()
	 * that have not subsequently been read from the stream shall be discarded
	 * (without further changing the file offset).
	 */

	assert(fgetc(fp) == 'H');
	c = getc(fp);
	ungetc(c, fp);	/* push back the character has been read */
	r = fflush(fp);
	assert(r == 0);
	assert(getc(fp) == c);

	fseek(fp, 0, SEEK_SET);
	assert(fgetc(fp) == 'H');
	c = getc(fp);
	ungetc('X', fp); /* push back the character has not been read */
	r = fflush(fp);
	assert(r == 0);
	assert(getc(fp) == 'l');

	r = fclose(fp);
	assert(r == 0);
}

int
main(int argc, char *argv[])
{
	setlocale(LC_ALL, "C.UTF-8");

	test_fflush_read0();
	test_fflush_read1();
	test_fflush_read2();
	test_fflush_read3();
	test_fflush_read4();
	test_fflush_read5();
	test_fflush_read6();

	exit(0);
}
