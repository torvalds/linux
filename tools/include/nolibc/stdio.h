/* SPDX-License-Identifier: LGPL-2.1 OR MIT */
/*
 * minimal stdio function definitions for NOLIBC
 * Copyright (C) 2017-2021 Willy Tarreau <w@1wt.eu>
 */

/* make sure to include all global symbols */
#include "nolibc.h"

#ifndef _NOLIBC_STDIO_H
#define _NOLIBC_STDIO_H

#include "std.h"
#include "arch.h"
#include "errno.h"
#include "fcntl.h"
#include "types.h"
#include "sys.h"
#include "stdarg.h"
#include "stdlib.h"
#include "string.h"
#include "compiler.h"

static const char *strerror(int errnum);

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

static __attribute__((unused))
FILE *fopen(const char *pathname, const char *mode)
{
	int flags, fd;

	switch (*mode) {
	case 'r':
		flags = O_RDONLY;
		break;
	case 'w':
		flags = O_WRONLY | O_CREAT | O_TRUNC;
		break;
	case 'a':
		flags = O_WRONLY | O_CREAT | O_APPEND;
		break;
	default:
		SET_ERRNO(EINVAL); return NULL;
	}

	if (mode[1] == '+')
		flags = (flags & ~(O_RDONLY | O_WRONLY)) | O_RDWR;

	fd = open(pathname, flags, 0666);
	return fdopen(fd, mode);
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


/* fwrite(), fread(), puts(), fputs(). Note that puts() emits '\n' but not fputs(). */

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

/* internal fread()-like function which only takes a size and returns 0 on
 * success or EOF on error. It automatically retries on short reads.
 */
static __attribute__((unused))
int _fread(void *buf, size_t size, FILE *stream)
{
	int fd = fileno(stream);
	ssize_t ret;

	while (size) {
		ret = read(fd, buf, size);
		if (ret <= 0)
			return EOF;
		size -= ret;
		buf += ret;
	}
	return 0;
}

static __attribute__((unused))
size_t fread(void *s, size_t size, size_t nmemb, FILE *stream)
{
	size_t nread;

	for (nread = 0; nread < nmemb; nread++) {
		if (_fread(s, size, stream) != 0)
			break;
		s += size;
	}
	return nread;
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


/* fseek */
static __attribute__((unused))
int fseek(FILE *stream, long offset, int whence)
{
	int fd = fileno(stream);
	off_t ret;

	ret = lseek(fd, offset, whence);

	/* lseek() and fseek() differ in that lseek returns the new
	 * position or -1, fseek() returns either 0 or -1.
	 */
	if (ret >= 0)
		return 0;

	return -1;
}


/* printf(). Supports the following integer and string formats.
 *  - %[#-+ 0][width][{l,ll,j}]{c,d,u,x,p,s,m,%}
 *  - %% generates a single %
 *  - %m outputs strerror(errno).
 *  - The modifiers [#-+ 0] are currently ignored.
 *  - No support for precision or variable widths.
 *  - No support for floating point or wide characters.
 *  - Invalid formats are copied to the output buffer.
 *
 * Called by vfprintf() and snprintf() to do the actual formatting.
 * The callers provide a callback function to save the formatted data.
 * The callback function is called multiple times:
 *  - for each group of literal characters in the format string.
 *  - for field padding.
 *  - for each conversion specifier.
 *  - with (NULL, 0) at the end of the __nolibc_printf.
 * If the callback returns non-zero __nolibc_printf() immediately returns -1.
 */

typedef int (*__nolibc_printf_cb)(void *state, const char *buf, size_t size);

/* This code uses 'flag' variables that are indexed by the low 6 bits
 * of characters to optimise checks for multiple characters.
 *
 * _NOLIBC_PF_FLAGS_CONTAIN(flags, 'a', 'b'. ...)
 * returns non-zero if the bit for any of the specified characters is set.
 *
 * _NOLIBC_PF_CHAR_IS_ONE_OF(ch, 'a', 'b'. ...)
 * returns the flag bit for ch if it is one of the specified characters.
 * All the characters must be in the same 32 character block (non-alphabetic,
 * upper case, or lower case) of the ASCII character set.
 */
#define _NOLIBC_PF_FLAG(ch) (1u << ((ch) & 0x1f))
#define _NOLIBC_PF_FLAG_NZ(ch) ((ch) ? _NOLIBC_PF_FLAG(ch) : 0)
#define _NOLIBC_PF_FLAG8(cmp_1, cmp_2, cmp_3, cmp_4, cmp_5, cmp_6, cmp_7, cmp_8, ...) \
	(_NOLIBC_PF_FLAG_NZ(cmp_1) | _NOLIBC_PF_FLAG_NZ(cmp_2) | \
	 _NOLIBC_PF_FLAG_NZ(cmp_3) | _NOLIBC_PF_FLAG_NZ(cmp_4) | \
	 _NOLIBC_PF_FLAG_NZ(cmp_5) | _NOLIBC_PF_FLAG_NZ(cmp_6) | \
	 _NOLIBC_PF_FLAG_NZ(cmp_7) | _NOLIBC_PF_FLAG_NZ(cmp_8))
#define _NOLIBC_PF_FLAGS_CONTAIN(flags, ...) \
	((flags) & _NOLIBC_PF_FLAG8(__VA_ARGS__, 0, 0, 0, 0, 0, 0, 0))
#define _NOLIBC_PF_CHAR_IS_ONE_OF(ch, cmp_1, ...) \
	((unsigned int)(ch) - (cmp_1 & 0xe0) > 0x1f ? 0 : \
		_NOLIBC_PF_FLAGS_CONTAIN(_NOLIBC_PF_FLAG(ch), cmp_1, __VA_ARGS__))

static __attribute__((unused, format(printf, 3, 0)))
int __nolibc_printf(__nolibc_printf_cb cb, void *state, const char *fmt, va_list args)
{
	char ch;
	unsigned long long v;
	long long signed_v;
	int written, width, len;
	unsigned int flags, ch_flag;
	char outbuf[21];
	char *out;
	const char *outstr;

	written = 0;
	while (1) {
		outstr = fmt;
		ch = *fmt++;
		if (!ch)
			break;

		width = 0;
		flags = 0;
		if (ch != '%') {
			while (*fmt && *fmt != '%')
				fmt++;
			/* Output characters from the format string. */
			len = fmt - outstr;
			goto do_output;
		}

		/* we're in a format sequence */

		/* Conversion flag characters */
		while (1) {
			ch = *fmt++;
			ch_flag = _NOLIBC_PF_CHAR_IS_ONE_OF(ch, ' ', '#', '+', '-', '0');
			if (!ch_flag)
				break;
			flags |= ch_flag;
		}

		/* width */
		while (ch >= '0' && ch <= '9') {
			width *= 10;
			width += ch - '0';

			ch = *fmt++;
		}

		/* Length modifier.
		 * They miss the conversion flags characters " #+-0" so can go into flags.
		 * Change ll to j (both always 64bits).
		 */
		ch_flag = _NOLIBC_PF_CHAR_IS_ONE_OF(ch, 'l', 'j');
		if (ch_flag != 0) {
			if (ch == 'l' && fmt[0] == 'l') {
				fmt++;
				ch_flag = _NOLIBC_PF_FLAG('j');
			}
			flags |= ch_flag;
			ch = *fmt++;
		}

		/* Conversion specifiers. */

		/* Numeric and pointer conversion specifiers.
		 *
		 * Use an explicit bound check (rather than _NOLIBC_PF_CHAR_IS_ONE_OF())
		 * so ch_flag can be used later.
		 */
		ch_flag = _NOLIBC_PF_FLAG(ch);
		if ((ch >= 'a' && ch <= 'z') &&
		    _NOLIBC_PF_FLAGS_CONTAIN(ch_flag, 'c', 'd', 'u', 'x', 'p')) {
			/* 'long' is needed for pointer conversions and ltz lengths.
			 * A single test can be used provided 'p' (the same bit as '0')
			 * is masked from flags.
			 */
			if (_NOLIBC_PF_FLAGS_CONTAIN(ch_flag | (flags & ~_NOLIBC_PF_FLAG('p')),
						     'p', 'l')) {
				v = va_arg(args, unsigned long);
				signed_v = (long)v;
			} else if (_NOLIBC_PF_FLAGS_CONTAIN(flags, 'j')) {
				v = va_arg(args, unsigned long long);
				signed_v = v;
			} else {
				v = va_arg(args, unsigned int);
				signed_v = (int)v;
			}

			if (ch == 'c') {
				/* "%c" - single character. */
				outbuf[0] = v;
				len = 1;
				outstr = outbuf;
				goto do_output;
			}

			out = outbuf;

			if (_NOLIBC_PF_FLAGS_CONTAIN(ch_flag, 'd')) {
				/* "%d" and "%i" - signed decimal numbers. */
				if (signed_v < 0) {
					*out++ = '-';
					v = -(signed_v + 1);
					v++;
				}
			}

			/* Convert the number to ascii in the required base. */
			if (_NOLIBC_PF_FLAGS_CONTAIN(ch_flag, 'd', 'u')) {
				/* Base 10 */
				u64toa_r(v, out);
			} else {
				/* Base 16 */
				if (_NOLIBC_PF_FLAGS_CONTAIN(ch_flag, 'p')) {
					*(out++) = '0';
					*(out++) = 'x';
				}
				u64toh_r(v, out);
			}

			outstr = outbuf;
			goto do_strlen_output;
		}

		if (ch == 's') {
			outstr = va_arg(args, char *);
			if (!outstr)
				outstr = "(null)";
			goto do_strlen_output;
		}

		if (ch == 'm') {
#ifdef NOLIBC_IGNORE_ERRNO
			outstr = "unknown error";
#else
			outstr = strerror(errno);
#endif /* NOLIBC_IGNORE_ERRNO */
			goto do_strlen_output;
		}

		if (ch != '%') {
			/* Invalid format: back up to output the format characters */
			fmt = outstr + 1;
			/* and output a '%' now. */
		}
		/* %% is documented as a 'conversion specifier'.
		 * Any flags, precision or length modifier are ignored.
		 */
		len = 1;
		width = 0;
		outstr = fmt - 1;
		goto do_output;

do_strlen_output:
		/* Open coded strlen() (slightly smaller). */
		for (len = 0;; len++)
			if (!outstr[len])
				break;

do_output:
		written += len;

		/* Stop gcc back-merging this code into one of the conditionals above. */
		_NOLIBC_OPTIMIZER_HIDE_VAR(len);

		width -= len;
		while (width > 0) {
			/* Output pad in 16 byte blocks with the small block first. */
			int pad_len = ((width - 1) & 15) + 1;
			width -= pad_len;
			written += pad_len;
			if (cb(state, "                ", pad_len) != 0)
				return -1;
		}
		if (cb(state, outstr, len) != 0)
			return -1;
	}

	/* Request a final '\0' be added to the snprintf() output.
	 * This may be the only call of the cb() function.
	 */
	if (cb(state, NULL, 0) != 0)
		return -1;

	return written;
}

static int __nolibc_fprintf_cb(void *stream, const char *buf, size_t size)
{
	return _fwrite(buf, size, stream);
}

static __attribute__((unused, format(printf, 2, 0)))
int vfprintf(FILE *stream, const char *fmt, va_list args)
{
	return __nolibc_printf(__nolibc_fprintf_cb, stream, fmt, args);
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

static __attribute__((unused, format(printf, 2, 0)))
int vdprintf(int fd, const char *fmt, va_list args)
{
	FILE *stream;

	stream = fdopen(fd, NULL);
	if (!stream)
		return -1;
	/* Technically 'stream' is leaked, but as it's only a wrapper around 'fd' that is fine */
	return vfprintf(stream, fmt, args);
}

static __attribute__((unused, format(printf, 2, 3)))
int dprintf(int fd, const char *fmt, ...)
{
	va_list args;
	int ret;

	va_start(args, fmt);
	ret = vdprintf(fd, fmt, args);
	va_end(args);

	return ret;
}

struct __nolibc_sprintf_cb_state {
	char *buf;
	size_t space;
};

static int __nolibc_sprintf_cb(void *v_state, const char *buf, size_t size)
{
	struct __nolibc_sprintf_cb_state *state = v_state;
	size_t space = state->space;
	char *tgt;

	/* Truncate the request to fit in the output buffer space.
	 * The last byte is reserved for the terminating '\0'.
	 * state->space can only be zero for snprintf(NULL, 0, fmt, args)
	 * so this normally lets through calls with 'size == 0'.
	 */
	if (size >= space) {
		if (space <= 1)
			return 0;
		size = space - 1;
	}
	tgt = state->buf;

	/* __nolibc_printf() ends with cb(state, NULL, 0) to request the output
	 * buffer be '\0' terminated.
	 * That will be the only cb() call for, eg, snprintf(buf, sz, "").
	 * Zero lengths can occur at other times (eg "%s" for an empty string).
	 * Unconditionally write the '\0' byte to reduce code size, it is
	 * normally overwritten by the data being output.
	 * There is no point adding a '\0' after copied data - there is always
	 * another call.
	 */
	*tgt = '\0';
	if (size) {
		state->space = space - size;
		state->buf = tgt + size;
		memcpy(tgt, buf, size);
	}

	return 0;
}

static __attribute__((unused, format(printf, 3, 0)))
int vsnprintf(char *buf, size_t size, const char *fmt, va_list args)
{
	struct __nolibc_sprintf_cb_state state = { .buf = buf, .space = size };

	return __nolibc_printf(__nolibc_sprintf_cb, &state, fmt, args);
}

static __attribute__((unused, format(printf, 3, 4)))
int snprintf(char *buf, size_t size, const char *fmt, ...)
{
	va_list args;
	int ret;

	va_start(args, fmt);
	ret = vsnprintf(buf, size, fmt, args);
	va_end(args);

	return ret;
}

static __attribute__((unused, format(printf, 2, 0)))
int vsprintf(char *buf, const char *fmt, va_list args)
{
	return vsnprintf(buf, SIZE_MAX, fmt, args);
}

static __attribute__((unused, format(printf, 2, 3)))
int sprintf(char *buf, const char *fmt, ...)
{
	va_list args;
	int ret;

	va_start(args, fmt);
	ret = vsprintf(buf, fmt, args);
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
#ifdef NOLIBC_IGNORE_ERRNO
	fprintf(stderr, "%s%sunknown error\n", (msg && *msg) ? msg : "", (msg && *msg) ? ": " : "");
#else
	fprintf(stderr, "%s%serrno=%d\n", (msg && *msg) ? msg : "", (msg && *msg) ? ": " : "", errno);
#endif
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
int strerror_r(int errnum, char *buf, size_t buflen)
{
	if (buflen < 18)
		return ERANGE;

	__builtin_memcpy(buf, "errno=", 6);
	i64toa_r(errnum, buf + 6);
	return 0;
}

static __attribute__((unused))
const char *strerror(int errnum)
{
	static char buf[18];
	char *b = buf;

	/* Force gcc to use 'register offset' to access buf[]. */
	_NOLIBC_OPTIMIZER_HIDE_VAR(b);

	/* Use strerror_r() to avoid having the only .data in small programs. */
	strerror_r(errnum, b, sizeof(buf));

	return b;
}

#endif /* _NOLIBC_STDIO_H */
