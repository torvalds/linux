/* SPDX-License-Identifier: LGPL-2.1 OR MIT */
/*
 * minimal stdio function definitions for NOLIBC
 * Copyright (C) 2017-2021 Willy Tarreau <w@1wt.eu>
 */

#ifndef _NOLIBC_STDIO_H
#define _NOLIBC_STDIO_H

#include "std.h"
#include "arch.h"
#include "types.h"
#include "sys.h"
#include "stdlib.h"
#include "string.h"

#ifndef EOF
#define EOF (-1)
#endif

/* just define FILE as a non-empty type */
typedef struct FILE {
	char dummy[1];
} FILE;

/* We define the 3 common stdio files as constant invalid pointers that
 * are easily recognized.
 */
static __attribute__((unused)) FILE* const stdin  = (FILE*)-3;
static __attribute__((unused)) FILE* const stdout = (FILE*)-2;
static __attribute__((unused)) FILE* const stderr = (FILE*)-1;

/* getc(), fgetc(), getchar() */

#define getc(stream) fgetc(stream)

static __attribute__((unused))
int fgetc(FILE* stream)
{
	unsigned char ch;
	int fd;

	if (stream < stdin || stream > stderr)
		return EOF;

	fd = 3 + (long)stream;

	if (read(fd, &ch, 1) <= 0)
		return EOF;
	return ch;
}

static __attribute__((unused))
int getchar(void)
{
	return fgetc(stdin);
}


/* putc(), fputc(), putchar() */

#define putc(c, stream) fputc(c, stream)

static __attribute__((unused))
int fputc(int c, FILE* stream)
{
	unsigned char ch = c;
	int fd;

	if (stream < stdin || stream > stderr)
		return EOF;

	fd = 3 + (long)stream;

	if (write(fd, &ch, 1) <= 0)
		return EOF;
	return ch;
}

static __attribute__((unused))
int putchar(int c)
{
	return fputc(c, stdout);
}


/* puts(), fputs(). Note that puts() emits '\n' but not fputs(). */

static __attribute__((unused))
int fputs(const char *s, FILE *stream)
{
	size_t len = strlen(s);
	ssize_t ret;
	int fd;

	if (stream < stdin || stream > stderr)
		return EOF;

	fd = 3 + (long)stream;

	while (len > 0) {
		ret = write(fd, s, len);
		if (ret <= 0)
			return EOF;
		s += ret;
		len -= ret;
	}
	return 0;
}

static __attribute__((unused))
int puts(const char *s)
{
	if (fputs(s, stdout) == EOF)
		return EOF;
	return putchar('\n');
}


/* fgets() */
static __attribute__((unused))
char *fgets(char *s, int size, FILE *stream)
{
	int ofs;
	int c;

	for (ofs = 0; ofs + 1 < size;) {
		c = fgetc(stream);
		if (c == EOF)
			break;
		s[ofs++] = c;
		if (c == '\n')
			break;
	}
	if (ofs < size)
		s[ofs] = 0;
	return ofs ? s : NULL;
}

#endif /* _NOLIBC_STDIO_H */
