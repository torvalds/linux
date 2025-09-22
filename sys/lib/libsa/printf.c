/*	$OpenBSD: printf.c,v 1.29 2019/05/11 16:56:47 deraadt Exp $	*/
/*	$NetBSD: printf.c,v 1.10 1996/11/30 04:19:21 gwr Exp $	*/

/*-
 * Copyright (c) 1993
 *	The Regents of the University of California.  All rights reserved.
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
 *
 *	@(#)printf.c	8.1 (Berkeley) 6/11/93
 */

/*
 * Scaled down version of printf(3).
 *
 * One additional format:
 *
 * The format %b is supported to decode error registers.
 * Its usage is:
 *
 *	printf("reg=%b\n", regval, "<base><arg>*");
 *
 * where <base> is the output base expressed as a control character, e.g.
 * \10 gives octal; \20 gives hex.  Each arg is a sequence of characters,
 * the first of which gives the bit number to be inspected (origin 1), and
 * the next characters (up to a control character, i.e. a character <= 32),
 * give the name of the register.  Thus:
 *
 *	printf("reg=%b\n", 3, "\10\2BITTWO\1BITONE\n");
 *
 * would produce output:
 *
 *	reg=3<BITTWO,BITONE>
 */

#include <sys/types.h>
#include <sys/stdarg.h>

#include "stand.h"

/*
 * macros for converting digits to letters and vice versa
 */
#define	to_digit(c)	((c) - '0')
#define	is_digit(c)	((unsigned)to_digit(c) <= 9)
#define	to_char(n)	((n) + '0')

void kprintn(void (*)(int), u_long, int, int, char);
#ifdef LIBSA_LONGLONG_PRINTF
void kprintn64(void (*)(int), u_int64_t, int, int, char);
#endif
void kdoprnt(void (*)(int), const char *, va_list);

const char hexdig[] = "0123456789abcdef";

void
printf(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	kdoprnt(putchar, fmt, ap);
	va_end(ap);
}

void
vprintf(const char *fmt, va_list ap)
{
	kdoprnt(putchar, fmt, ap);
}

void
kdoprnt(void (*put)(int), const char *fmt, va_list ap)
{
#ifdef LIBSA_LONGLONG_PRINTF
	u_int64_t ull;
#endif
	unsigned long ul;
	int ch, lflag, width, n;
	char *p, padchar;

	for (;;) {
		while ((ch = *fmt++) != '%') {
			if (ch == '\0')
				return;
			put(ch);
		}
		lflag = 0;
		padchar = ' ';
		width = 0;
rflag:		ch = *fmt++;
reswitch:	switch (ch) {
		case '0':
			/*
			 * ``Note that 0 is taken as a flag, not as the
			 * beginning of a field width.''
			 *	-- ANSI X3J11
			 */
			padchar = '0';
			goto rflag;
		case '1': case '2': case '3': case '4':
		case '5': case '6': case '7': case '8': case '9':
			n = 0;
			do {
				n = 10 * n + to_digit(ch);
				ch = *fmt++;
			} while (is_digit(ch));
			width = n;
			goto reswitch;
		case 'l':
			lflag++;
			goto rflag;
		case 'b':
		{
			int set, n;

			ul = va_arg(ap, int);
			p = va_arg(ap, char *);
			kprintn(put, ul, *p++, width, padchar);

			if (!ul)
				break;

			for (set = 0; (n = *p++);) {
				if (ul & (1 << (n - 1))) {
					put(set ? ',' : '<');
					for (; (n = *p) > ' '; ++p)
						put(n);
					set = 1;
				} else
					for (; *p > ' '; ++p)
						;
			}
			if (set)
				put('>');
		}
			break;
		case 'c':
			ch = va_arg(ap, int);
			put(ch & 0x7f);
			break;
		case 's':
			p = va_arg(ap, char *);
			while ((ch = *p++))
				put(ch);
			break;
		case 'd':
#ifdef LIBSA_LONGLONG_PRINTF
			if (lflag > 1) {
				ull = va_arg(ap, int64_t);
				if ((int64_t)ull < 0) {
					put('-');
					ull = -(int64_t)ull;
				}
				kprintn64(put, ull, 10, width, padchar);
				break;
			} 
#endif
			ul = lflag ?
			    va_arg(ap, long) : va_arg(ap, int);
			if ((long)ul < 0) {
				put('-');
				ul = -(long)ul;
			}
			kprintn(put, ul, 10, width, padchar);
			break;
		case 'o':
#ifdef LIBSA_LONGLONG_PRINTF
			if (lflag > 1) {
				ull = va_arg(ap, u_int64_t);
				kprintn64(put, ull, 8, width, padchar);
				break;
			} 
#endif
			ul = lflag ?
			    va_arg(ap, u_long) : va_arg(ap, u_int);
			kprintn(put, ul, 8, width, padchar);
			break;
		case 'u':
#ifdef LIBSA_LONGLONG_PRINTF
			if (lflag > 1) {
				ull = va_arg(ap, u_int64_t);
				kprintn64(put, ull, 10, width, padchar);
				break;
			} 
#endif
			ul = lflag ?
			    va_arg(ap, u_long) : va_arg(ap, u_int);
			kprintn(put, ul, 10, width, padchar);
			break;
		case 'p':
			put('0');
			put('x');
			lflag += sizeof(void *)==sizeof(u_long)? 1 : 0;
		case 'x':
#ifdef LIBSA_LONGLONG_PRINTF
			if (lflag > 1) {
				ull = va_arg(ap, u_int64_t);
				kprintn64(put, ull, 16, width, padchar);
				break;
			}
#else
 			if (lflag > 1) {
				/* hold an int64_t in base 16 */
				char *p, buf[(sizeof(u_int64_t) * NBBY / 4) + 1];
				u_int64_t ull;

 				ull = va_arg(ap, u_int64_t);
				p = buf;
				do {
					*p++ = hexdig[ull & 15];
				} while (ull >>= 4);
				while ((p - buf) < width &&
				    (p - buf) < sizeof(buf)) {
					*p++ = padchar;
				}
				do {
					put(*--p);
				} while (p > buf);
 				break;
 			}
#endif
			ul = lflag ?
			    va_arg(ap, u_long) : va_arg(ap, u_int);
			kprintn(put, ul, 16, width, padchar);
			break;
		default:
			put('%');
#ifdef LIBSA_LONGLONG_PRINTF
			while (--lflag)
#else
			if (lflag)
#endif
				put('l');
			put(ch);
		}
	}
}

void
kprintn(void (*put)(int), unsigned long ul, int base, int width, char padchar)
{
	/* hold a long in base 8 */
	char *p, buf[(sizeof(long) * NBBY / 3) + 1];

	p = buf;
	do {
		*p++ = hexdig[ul % base];
	} while (ul /= base);
	while ((p - buf) < width && (p - buf) < sizeof(buf)) {
		*p++ = padchar;
	}
	do {
		put(*--p);
	} while (p > buf);
}

#ifdef LIBSA_LONGLONG_PRINTF
void
kprintn64(void (*put)(int), u_int64_t ull, int base, int width, char padchar)
{
	/* hold an int64_t in base 8 */
	char *p, buf[(sizeof(u_int64_t) * NBBY / 3) + 1];

	p = buf;
	do {
		*p++ = hexdig[ull % base];
	} while (ull /= base);
	while ((p - buf) < width && (p - buf) < sizeof(buf)) {
		*p++ = padchar;
	}
	do {
		put(*--p);
	} while (p > buf);
}
#endif

int donottwiddle = 0;

void
twiddle(void)
{
	static int pos;

	if (!donottwiddle) {
		putchar("|/-\\"[pos++ & 3]);
		putchar('\b');
	}
}
