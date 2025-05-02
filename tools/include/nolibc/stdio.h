/* SPDX-License-Identifier: LGPL-2.1 OR MIT */
/*
 * minimal stdio function definitions for NOLIBC
 * Copyright (C) 2017-2021 Willy Tarreau <w@1wt.eu>
 */

#ifndef _NOLIBC_STDIO_H
#define _NOLIBC_STDIO_H

#include "std.h"
#include "arch.h"
#include "errno.h"
#include "types.h"
#include "sys.h"
#include "stdarg.h"
#include "stdlib.h"
#include "string.h"
#include "compiler.h"

#ifndef EOF
#define EOF (-1)
#endif

/* Buffering mode used by setvbuf.  */
#define _IOFBF 0	/* Fully buffered. */
#define _IOLBF 1	/* Line buffered. */
#define _IONBF 2	/* No buffering. */

/* just define FILE as a non-empty type. The value of the pointer gives
 * the FD: FILE=~fd for fd>=0 or NULL for fd<0. This way positive FILE
 * are immediately identified as abnormal entries (i.e. possible copies
 * of valid pointers to something else).
 */
typedef struct FILE {
	char dummy[1];
} FILE;

static __attribute__((unused)) FILE* const stdin  = (FILE*)(intptr_t)~STDIN_FILENO;
static __attribute__((unused)) FILE* const stdout = (FILE*)(intptr_t)~STDOUT_FILENO;
static __attribute__((unused)) FILE* const stderr = (FILE*)(intptr_t)~STDERR_FILENO;

/* provides a FILE* equivalent of fd. The mode is ignored. */
static __attribute__((unused))
FILE *fdopen(int fd, const char *mode __attribute__((unused)))
{
	if (fd < 0) {
		SET_ERRNO(EBADF);
		return NULL;
	}
	return (FILE*)(intptr_t)~fd;
}

/* provides the fd of stream. */
static __attribute__((unused))
int fileno(FILE *stream)
{
	intptr_t i = (intptr_t)stream;

	if (i >= 0) {
		SET_ERRNO(EBADF);
		return -1;
	}
	return ~i;
}

/* flush a stream. */
static __attribute__((unused))
int fflush(FILE *stream)
{
	intptr_t i = (intptr_t)stream;

	/* NULL is valid here. */
	if (i > 0) {
		SET_ERRNO(EBADF);
		return -1;
	}

	/* Don't do anything, nolibc does not support buffering. */
	return 0;
}

/* flush a stream. */
static __attribute__((unused))
int fclose(FILE *stream)
{
	intptr_t i = (intptr_t)stream;

	if (i >= 0) {
		SET_ERRNO(EBADF);
		return -1;
	}

	if (close(~i))
		return EOF;

	return 0;
}

/* getc(), fgetc(), getchar() */

#define getc(stream) fgetc(stream)

static __attribute__((unused))
int fgetc(FILE* stream)
{
	unsigned char ch;

	if (read(fileno(stream), &ch, 1) <= 0)
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

	if (write(fileno(stream), &ch, 1) <= 0)
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
	int fd = fileno(stream);

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


/* minimal vfprintf(). It supports the following formats:
 *  - %[l*]{d,u,c,x,p}
 *  - %s
 *  - unknown modifiers are ignored.
 */
static __attribute__((unused, format(printf, 2, 0)))
int vfprintf(FILE *stream, const char *fmt, va_list args)
{
	char escape, lpref, c;
	unsigned long long v;
	unsigned int written;
	size_t len, ofs;
	char tmpbuf[21];
	const char *outstr;

	written = ofs = escape = lpref = 0;
	while (1) {
		c = fmt[ofs++];

		if (escape) {
			/* we're in an escape sequence, ofs == 1 */
			escape = 0;
			if (c == 'c' || c == 'd' || c == 'u' || c == 'x' || c == 'p') {
				char *out = tmpbuf;

				if (c == 'p')
					v = va_arg(args, unsigned long);
				else if (lpref) {
					if (lpref > 1)
						v = va_arg(args, unsigned long long);
					else
						v = va_arg(args, unsigned long);
				} else
					v = va_arg(args, unsigned int);

				if (c == 'd') {
					/* sign-extend the value */
					if (lpref == 0)
						v = (long long)(int)v;
					else if (lpref == 1)
						v = (long long)(long)v;
				}

				switch (c) {
				case 'c':
					out[0] = v;
					out[1] = 0;
					break;
				case 'd':
					i64toa_r(v, out);
					break;
				case 'u':
					u64toa_r(v, out);
					break;
				case 'p':
					*(out++) = '0';
					*(out++) = 'x';
					__nolibc_fallthrough;
				default: /* 'x' and 'p' above */
					u64toh_r(v, out);
					break;
				}
				outstr = tmpbuf;
			}
			else if (c == 's') {
				outstr = va_arg(args, char *);
				if (!outstr)
					outstr="(null)";
			}
			else if (c == '%') {
				/* queue it verbatim */
				continue;
			}
			else {
				/* modifiers or final 0 */
				if (c == 'l') {
					/* long format prefix, maintain the escape */
					lpref++;
				}
				escape = 1;
				goto do_escape;
			}
			len = strlen(outstr);
			goto flush_str;
		}

		/* not an escape sequence */
		if (c == 0 || c == '%') {
			/* flush pending data on escape or end */
			escape = 1;
			lpref = 0;
			outstr = fmt;
			len = ofs - 1;
		flush_str:
			if (_fwrite(outstr, len, stream) != 0)
				break;

			written += len;
		do_escape:
			if (c == 0)
				break;
			fmt += ofs;
			ofs = 0;
			continue;
		}

		/* literal char, just queue it */
	}
	return written;
}

static __attribute__((unused, format(printf, 1, 0)))
int vprintf(const char *fmt, va_list args)
{
	return vfprintf(stdout, fmt, args);
}

static __attribute__((unused, format(printf, 2, 3)))
int fprintf(FILE *stream, const char *fmt, ...)
{
	va_list args;
	int ret;

	va_start(args, fmt);
	ret = vfprintf(stream, fmt, args);
	va_end(args);
	return ret;
}

static __attribute__((unused, format(printf, 1, 2)))
int printf(const char *fmt, ...)
{
	va_list args;
	int ret;

	va_start(args, fmt);
	ret = vfprintf(stdout, fmt, args);
	va_end(args);
	return ret;
}

static __attribute__((unused))
int vsscanf(const char *str, const char *format, va_list args)
{
	uintmax_t uval;
	intmax_t ival;
	int base;
	char *endptr;
	int matches;
	int lpref;

	matches = 0;

	while (1) {
		if (*format == '%') {
			/* start of pattern */
			lpref = 0;
			format++;

			if (*format == 'l') {
				/* same as in printf() */
				lpref = 1;
				format++;
				if (*format == 'l') {
					lpref = 2;
					format++;
				}
			}

			if (*format == '%') {
				/* literal % */
				if ('%' != *str)
					goto done;
				str++;
				format++;
				continue;
			} else if (*format == 'd') {
				ival = strtoll(str, &endptr, 10);
				if (lpref == 0)
					*va_arg(args, int *) = ival;
				else if (lpref == 1)
					*va_arg(args, long *) = ival;
				else if (lpref == 2)
					*va_arg(args, long long *) = ival;
			} else if (*format == 'u' || *format == 'x' || *format == 'X') {
				base = *format == 'u' ? 10 : 16;
				uval = strtoull(str, &endptr, base);
				if (lpref == 0)
					*va_arg(args, unsigned int *) = uval;
				else if (lpref == 1)
					*va_arg(args, unsigned long *) = uval;
				else if (lpref == 2)
					*va_arg(args, unsigned long long *) = uval;
			} else if (*format == 'p') {
				*va_arg(args, void **) = (void *)strtoul(str, &endptr, 16);
			} else {
				SET_ERRNO(EILSEQ);
				goto done;
			}

			format++;
			str = endptr;
			matches++;

		} else if (*format == '\0') {
			goto done;
		} else if (isspace(*format)) {
			/* skip spaces in format and str */
			while (isspace(*format))
				format++;
			while (isspace(*str))
				str++;
		} else if (*format == *str) {
			/* literal match */
			format++;
			str++;
		} else {
			if (!matches)
				matches = EOF;
			goto done;
		}
	}

done:
	return matches;
}

static __attribute__((unused, format(scanf, 2, 3)))
int sscanf(const char *str, const char *format, ...)
{
	va_list args;
	int ret;

	va_start(args, format);
	ret = vsscanf(str, format, args);
	va_end(args);
	return ret;
}

static __attribute__((unused))
void perror(const char *msg)
{
	fprintf(stderr, "%s%serrno=%d\n", (msg && *msg) ? msg : "", (msg && *msg) ? ": " : "", errno);
}

static __attribute__((unused))
int setvbuf(FILE *stream __attribute__((unused)),
	    char *buf __attribute__((unused)),
	    int mode,
	    size_t size __attribute__((unused)))
{
	/*
	 * nolibc does not support buffering so this is a nop. Just check mode
	 * is valid as required by the spec.
	 */
	switch (mode) {
	case _IOFBF:
	case _IOLBF:
	case _IONBF:
		break;
	default:
		return EOF;
	}

	return 0;
}

static __attribute__((unused))
const char *strerror(int errno)
{
	static char buf[18] = "errno=";

	i64toa_r(errno, &buf[6]);

	return buf;
}

/* make sure to include all global symbols */
#include "nolibc.h"

#endif /* _NOLIBC_STDIO_H */
