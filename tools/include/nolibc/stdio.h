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


/* fwrite(), puts(), fputs(). Note that puts() emits '\n' but not fputs(). */

/* internal fwrite()-like function which only takes a size and returns 0 on
 * success or EOF on error. It automatically retries on short writes.
 */
static __attribute__((unused))
int _fwrite(const void *buf, size_t size, FILE *stream)
{
	ssize_t ret;
	int fd;

	if (stream < stdin || stream > stderr)
		return EOF;

	fd = 3 + (long)stream;

	while (size) {
		ret = write(fd, buf, size);
		if (ret <= 0)
			return EOF;
		size -= ret;
		buf += ret;
	}
	return 0;
}

static __attribute__((unused))
size_t fwrite(const void *s, size_t size, size_t nmemb, FILE *stream)
{
	size_t written;

	for (written = 0; written < nmemb; written++) {
		if (_fwrite(s, size, stream) != 0)
			break;
		s += size;
	}
	return written;
}

static __attribute__((unused))
int fputs(const char *s, FILE *stream)
{
	return _fwrite(s, strlen(s), stream);
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
