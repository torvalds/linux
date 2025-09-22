/*	$OpenBSD: test___freading.c,v 1.2 2025/06/12 07:39:26 yasuoka Exp $	*/

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
#include <stdio.h>
#include <stdio_ext.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* we use assert() */
#undef	NDEBUG

#define	TMPFILENAME	"test___freading.tmp"

void setup(void);

void test___freading0(void);
void test___freading1(void);
void test___freading2(void);

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

void
test___freading0(void)
{
	FILE	*fp;
	int	 r;
	char	 buf[80];

	fp = popen("echo Hello world", "r");
	assert(fp != NULL);
	assert(__freading(fp) != 0);
	assert(fgets(buf, sizeof(buf), fp) != NULL);
	assert(strcmp(buf, "Hello world\n") == 0);
	r = pclose(fp);
	assert(r == 0);
}

void
test___freading1(void)
{
	FILE	*fp;
	int	 r;

	/* when the last operaiton is read, __freading() returns true */
	fp = fopen(TMPFILENAME, "w+");
	assert(fp != NULL);
	assert(__freading(fp) == 0);
	r = fputs("Hello world\n", fp);
	assert(r >= 0);
	assert(__freading(fp) == 0);
	rewind(fp);
	assert(fgetc(fp) == 'H');
	assert(__freading(fp) != 0);
	/* write */
	fseek(fp, 0, SEEK_END);
	r = fputs("\n", fp);
	assert(__freading(fp) == 0);
	/* ungetc */
	rewind(fp);
	assert(ungetc('X', fp) != 0);
	assert(__freading(fp) != 0);	/* reading */

	r = fclose(fp);
	assert(r == 0);
}

void
test___freading2(void)
{
	int	 r;
	FILE	*fp;

	/*
	 * until v1.10 of fpurge.c mistakenly enables the writing buffer
	 * without _SRD flag set.
	 */
	fp = fopen(TMPFILENAME, "r+");
	assert(fp != NULL);
	assert(fgetc(fp) == 'H');
	fpurge(fp);
	fseek(fp, 0, SEEK_CUR);
	assert(fputc('X', fp) == 'X');
	assert(__freading(fp) == 0);

	r = fclose(fp);
	assert(r == 0);
}

int
main(int argc, char *argv[])
{
	test___freading0();
	test___freading1();
	test___freading2();

	exit(0);
}
