/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1990, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * Chris Torek.
 *
 * Copyright (c) 2011 The FreeBSD Foundation
 * All rights reserved.
 * Portions of this software were developed by David Chisnall
 * under sponsorship from the FreeBSD Foundation.
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

#if 0
#if defined(LIBC_SCCS) && !defined(lint)
static char sccsid[] = "@(#)vfscanf.c	8.1 (Berkeley) 6/4/93";
#endif /* LIBC_SCCS and not lint */
#endif
#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include "namespace.h"
#include <ctype.h>
#include <inttypes.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <stdarg.h>
#include <string.h>
#include <wchar.h>
#include <wctype.h>
#include "un-namespace.h"

#include "libc_private.h"
#include "local.h"
#include "xlocale_private.h"

#define	BUF		513	/* Maximum length of numeric string. */

/*
 * Flags used during conversion.
 */
#define	LONG		0x01	/* l: long or double */
#define	LONGDBL		0x02	/* L: long double */
#define	SHORT		0x04	/* h: short */
#define	SUPPRESS	0x08	/* *: suppress assignment */
#define	POINTER		0x10	/* p: void * (as hex) */
#define	NOSKIP		0x20	/* [ or c: do not skip blanks */
#define	LONGLONG	0x400	/* ll: long long (+ deprecated q: quad) */
#define	INTMAXT		0x800	/* j: intmax_t */
#define	PTRDIFFT	0x1000	/* t: ptrdiff_t */
#define	SIZET		0x2000	/* z: size_t */
#define	SHORTSHORT	0x4000	/* hh: char */
#define	UNSIGNED	0x8000	/* %[oupxX] conversions */

/*
 * The following are used in integral conversions only:
 * SIGNOK, NDIGITS, PFXOK, and NZDIGITS
 */
#define	SIGNOK		0x40	/* +/- is (still) legal */
#define	NDIGITS		0x80	/* no digits detected */
#define	PFXOK		0x100	/* 0x prefix is (still) legal */
#define	NZDIGITS	0x200	/* no zero digits detected */
#define	HAVESIGN	0x10000	/* sign detected */

/*
 * Conversion types.
 */
#define	CT_CHAR		0	/* %c conversion */
#define	CT_CCL		1	/* %[...] conversion */
#define	CT_STRING	2	/* %s conversion */
#define	CT_INT		3	/* %[dioupxX] conversion */
#define	CT_FLOAT	4	/* %[efgEFG] conversion */

#ifndef NO_FLOATING_POINT
static int parsefloat(FILE *, wchar_t *, wchar_t *, locale_t);
#endif

struct ccl {
	const wchar_t *start;	/* character class start */
	const wchar_t *end;	/* character class end */
	int compl;		/* ccl is complemented? */
};

static __inline int
inccl(const struct ccl *ccl, wint_t wi)
{

	if (ccl->compl) {
		return (wmemchr(ccl->start, wi, ccl->end - ccl->start)
		    == NULL);
	} else {
		return (wmemchr(ccl->start, wi, ccl->end - ccl->start) != NULL);
	}
}

/*
 * Conversion functions are passed a pointer to this object instead of
 * a real parameter to indicate that the assignment-suppression (*)
 * flag was specified.  We could use a NULL pointer to indicate this,
 * but that would mask bugs in applications that call scanf() with a
 * NULL pointer.
 */
static const int suppress;
#define	SUPPRESS_PTR	((void *)&suppress)

static const mbstate_t initial_mbs;

/*
 * The following conversion functions return the number of characters consumed,
 * or -1 on input failure.  Character class conversion returns 0 on match
 * failure.
 */

static __inline int
convert_char(FILE *fp, char * mbp, int width, locale_t locale)
{
	mbstate_t mbs;
	size_t nconv;
	wint_t wi;
	int n;

	n = 0;
	mbs = initial_mbs;
	while (width-- != 0 && (wi = __fgetwc(fp, locale)) != WEOF) {
		if (mbp != SUPPRESS_PTR) {
			nconv = wcrtomb(mbp, wi, &mbs);
			if (nconv == (size_t)-1)
				return (-1);
			mbp += nconv;
		}
		n++;
	}
	if (n == 0)
		return (-1);
	return (n);
}

static __inline int
convert_wchar(FILE *fp, wchar_t *wcp, int width, locale_t locale)
{
	wint_t wi;
	int n;

	n = 0;
	while (width-- != 0 && (wi = __fgetwc(fp, locale)) != WEOF) {
		if (wcp != SUPPRESS_PTR)
			*wcp++ = (wchar_t)wi;
		n++;
	}
	if (n == 0)
		return (-1);
	return (n);
}

static __inline int
convert_ccl(FILE *fp, char * mbp, int width, const struct ccl *ccl,
    locale_t locale)
{
	mbstate_t mbs;
	size_t nconv;
	wint_t wi;
	int n;

	n = 0;
	mbs = initial_mbs;
	while ((wi = __fgetwc(fp, locale)) != WEOF &&
	    width-- != 0 && inccl(ccl, wi)) {
		if (mbp != SUPPRESS_PTR) {
			nconv = wcrtomb(mbp, wi, &mbs);
			if (nconv == (size_t)-1)
				return (-1);
			mbp += nconv;
		}
		n++;
	}
	if (wi != WEOF)
		__ungetwc(wi, fp, locale);
	if (mbp != SUPPRESS_PTR)
		*mbp = 0;
	return (n);
}

static __inline int
convert_wccl(FILE *fp, wchar_t *wcp, int width, const struct ccl *ccl,
    locale_t locale)
{
	wchar_t *wcp0;
	wint_t wi;
	int n;

	if (wcp == SUPPRESS_PTR) {
		n = 0;
		while ((wi = __fgetwc(fp, locale)) != WEOF &&
		    width-- != 0 && inccl(ccl, wi))
			n++;
		if (wi != WEOF)
			__ungetwc(wi, fp, locale);
	} else {
		wcp0 = wcp;
		while ((wi = __fgetwc(fp, locale)) != WEOF &&
		    width-- != 0 && inccl(ccl, wi))
			*wcp++ = (wchar_t)wi;
		if (wi != WEOF)
			__ungetwc(wi, fp, locale);
		n = wcp - wcp0;
		if (n == 0)
			return (0);
		*wcp = 0;
	}
	return (n);
}

static __inline int
convert_string(FILE *fp, char * mbp, int width, locale_t locale)
{
	mbstate_t mbs;
	size_t nconv;
	wint_t wi;
	int nread;

	mbs = initial_mbs;
	nread = 0;
	while ((wi = __fgetwc(fp, locale)) != WEOF && width-- != 0 &&
	    !iswspace(wi)) {
		if (mbp != SUPPRESS_PTR) {
			nconv = wcrtomb(mbp, wi, &mbs);
			if (nconv == (size_t)-1)
				return (-1);
			mbp += nconv;
		}
		nread++;
	}
	if (wi != WEOF)
		__ungetwc(wi, fp, locale);
	if (mbp != SUPPRESS_PTR)
		*mbp = 0;
	return (nread);
}

static __inline int
convert_wstring(FILE *fp, wchar_t *wcp, int width, locale_t locale)
{
	wchar_t *wcp0;
	wint_t wi;
	int nread;

	nread = 0;
	if (wcp == SUPPRESS_PTR) {
		while ((wi = __fgetwc(fp, locale)) != WEOF &&
		    width-- != 0 && !iswspace(wi))
			nread++;
		if (wi != WEOF)
			__ungetwc(wi, fp, locale);
	} else {
		wcp0 = wcp;
		while ((wi = __fgetwc(fp, locale)) != WEOF &&
		    width-- != 0 && !iswspace(wi)) {
			*wcp++ = (wchar_t)wi;
			nread++;
		}
		if (wi != WEOF)
			__ungetwc(wi, fp, locale);
		*wcp = '\0';
	}
	return (nread);
}

/*
 * Read an integer, storing it in buf.  The only relevant bit in the
 * flags argument is PFXOK.
 *
 * Return 0 on a match failure, and the number of characters read
 * otherwise.
 */
static __inline int
parseint(FILE *fp, wchar_t *buf, int width, int base, int flags,
    locale_t locale)
{
	/* `basefix' is used to avoid `if' tests */
	static const short basefix[17] =
		{ 10, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16 };
	wchar_t *wcp;
	int c;

	flags |= SIGNOK | NDIGITS | NZDIGITS;
	for (wcp = buf; width; width--) {
		c = __fgetwc(fp, locale);
		/*
		 * Switch on the character; `goto ok' if we accept it
		 * as a part of number.
		 */
		switch (c) {

		/*
		 * The digit 0 is always legal, but is special.  For
		 * %i conversions, if no digits (zero or nonzero) have
		 * been scanned (only signs), we will have base==0.
		 * In that case, we should set it to 8 and enable 0x
		 * prefixing.  Also, if we have not scanned zero
		 * digits before this, do not turn off prefixing
		 * (someone else will turn it off if we have scanned
		 * any nonzero digits).
		 */
		case '0':
			if (base == 0) {
				base = 8;
				flags |= PFXOK;
			}
			if (flags & NZDIGITS)
				flags &= ~(SIGNOK|NZDIGITS|NDIGITS);
			else
				flags &= ~(SIGNOK|PFXOK|NDIGITS);
			goto ok;

		/* 1 through 7 always legal */
		case '1': case '2': case '3':
		case '4': case '5': case '6': case '7':
			base = basefix[base];
			flags &= ~(SIGNOK | PFXOK | NDIGITS);
			goto ok;

		/* digits 8 and 9 ok iff decimal or hex */
		case '8': case '9':
			base = basefix[base];
			if (base <= 8)
				break;	/* not legal here */
			flags &= ~(SIGNOK | PFXOK | NDIGITS);
			goto ok;

		/* letters ok iff hex */
		case 'A': case 'B': case 'C':
		case 'D': case 'E': case 'F':
		case 'a': case 'b': case 'c':
		case 'd': case 'e': case 'f':
			/* no need to fix base here */
			if (base <= 10)
				break;	/* not legal here */
			flags &= ~(SIGNOK | PFXOK | NDIGITS);
			goto ok;

		/* sign ok only as first character */
		case '+': case '-':
			if (flags & SIGNOK) {
				flags &= ~SIGNOK;
				flags |= HAVESIGN;
				goto ok;
			}
			break;

		/*
		 * x ok iff flag still set & 2nd char (or 3rd char if
		 * we have a sign).
		 */
		case 'x': case 'X':
			if (flags & PFXOK && wcp ==
			    buf + 1 + !!(flags & HAVESIGN)) {
				base = 16;	/* if %i */
				flags &= ~PFXOK;
				goto ok;
			}
			break;
		}

		/*
		 * If we got here, c is not a legal character for a
		 * number.  Stop accumulating digits.
		 */
		if (c != WEOF)
			__ungetwc(c, fp, locale);
		break;
	ok:
		/*
		 * c is legal: store it and look at the next.
		 */
		*wcp++ = (wchar_t)c;
	}
	/*
	 * If we had only a sign, it is no good; push back the sign.
	 * If the number ends in `x', it was [sign] '0' 'x', so push
	 * back the x and treat it as [sign] '0'.
	 */
	if (flags & NDIGITS) {
		if (wcp > buf)
			__ungetwc(*--wcp, fp, locale);
		return (0);
	}
	c = wcp[-1];
	if (c == 'x' || c == 'X') {
		--wcp;
		__ungetwc(c, fp, locale);
	}
	return (wcp - buf);
}

/*
 * MT-safe version.
 */
int
vfwscanf_l(FILE * __restrict fp, locale_t locale,
		const wchar_t * __restrict fmt, va_list ap)
{
	int ret;
	FIX_LOCALE(locale);

	FLOCKFILE_CANCELSAFE(fp);
	ORIENT(fp, 1);
	ret = __vfwscanf(fp, locale, fmt, ap);
	FUNLOCKFILE_CANCELSAFE();
	return (ret);
}
int
vfwscanf(FILE * __restrict fp, const wchar_t * __restrict fmt, va_list ap)
{
	return vfwscanf_l(fp, __get_locale(), fmt, ap);
}

/*
 * Non-MT-safe version.
 */
int
__vfwscanf(FILE * __restrict fp, locale_t locale,
		const wchar_t * __restrict fmt, va_list ap)
{
#define	GETARG(type)	((flags & SUPPRESS) ? SUPPRESS_PTR : va_arg(ap, type))
	wint_t c;		/* character from format, or conversion */
	size_t width;		/* field width, or 0 */
	int flags;		/* flags as defined above */
	int nassigned;		/* number of fields assigned */
	int nconversions;	/* number of conversions */
	int nr;			/* characters read by the current conversion */
	int nread;		/* number of characters consumed from fp */
	int base;		/* base argument to conversion function */
	struct ccl ccl;		/* character class info */
	wchar_t buf[BUF];	/* buffer for numeric conversions */
	wint_t wi;		/* handy wint_t */

	nassigned = 0;
	nconversions = 0;
	nread = 0;
	ccl.start = ccl.end = NULL;
	for (;;) {
		c = *fmt++;
		if (c == 0)
			return (nassigned);
		if (iswspace(c)) {
			while ((c = __fgetwc(fp, locale)) != WEOF &&
			    iswspace_l(c, locale))
				nread++;
			if (c != WEOF)
				__ungetwc(c, fp, locale);
			continue;
		}
		if (c != '%')
			goto literal;
		width = 0;
		flags = 0;
		/*
		 * switch on the format.  continue if done;
		 * break once format type is derived.
		 */
again:		c = *fmt++;
		switch (c) {
		case '%':
literal:
			if ((wi = __fgetwc(fp, locale)) == WEOF)
				goto input_failure;
			if (wi != c) {
				__ungetwc(wi, fp, locale);
				goto match_failure;
			}
			nread++;
			continue;

		case '*':
			flags |= SUPPRESS;
			goto again;
		case 'j':
			flags |= INTMAXT;
			goto again;
		case 'l':
			if (flags & LONG) {
				flags &= ~LONG;
				flags |= LONGLONG;
			} else
				flags |= LONG;
			goto again;
		case 'q':
			flags |= LONGLONG;	/* not quite */
			goto again;
		case 't':
			flags |= PTRDIFFT;
			goto again;
		case 'z':
			flags |= SIZET;
			goto again;
		case 'L':
			flags |= LONGDBL;
			goto again;
		case 'h':
			if (flags & SHORT) {
				flags &= ~SHORT;
				flags |= SHORTSHORT;
			} else
				flags |= SHORT;
			goto again;

		case '0': case '1': case '2': case '3': case '4':
		case '5': case '6': case '7': case '8': case '9':
			width = width * 10 + c - '0';
			goto again;

		/*
		 * Conversions.
		 */
		case 'd':
			c = CT_INT;
			base = 10;
			break;

		case 'i':
			c = CT_INT;
			base = 0;
			break;

		case 'o':
			c = CT_INT;
			flags |= UNSIGNED;
			base = 8;
			break;

		case 'u':
			c = CT_INT;
			flags |= UNSIGNED;
			base = 10;
			break;

		case 'X':
		case 'x':
			flags |= PFXOK;	/* enable 0x prefixing */
			c = CT_INT;
			flags |= UNSIGNED;
			base = 16;
			break;

#ifndef NO_FLOATING_POINT
		case 'A': case 'E': case 'F': case 'G':
		case 'a': case 'e': case 'f': case 'g':
			c = CT_FLOAT;
			break;
#endif

		case 'S':
			flags |= LONG;
			/* FALLTHROUGH */
		case 's':
			c = CT_STRING;
			break;

		case '[':
			ccl.start = fmt;
			if (*fmt == '^') {
				ccl.compl = 1;
				fmt++;
			} else
				ccl.compl = 0;
			if (*fmt == ']')
				fmt++;
			while (*fmt != '\0' && *fmt != ']')
				fmt++;
			ccl.end = fmt;
			fmt++;
			flags |= NOSKIP;
			c = CT_CCL;
			break;

		case 'C':
			flags |= LONG;
			/* FALLTHROUGH */
		case 'c':
			flags |= NOSKIP;
			c = CT_CHAR;
			break;

		case 'p':	/* pointer format is like hex */
			flags |= POINTER | PFXOK;
			c = CT_INT;		/* assumes sizeof(uintmax_t) */
			flags |= UNSIGNED;	/*      >= sizeof(uintptr_t) */
			base = 16;
			break;

		case 'n':
			if (flags & SUPPRESS)	/* ??? */
				continue;
			if (flags & SHORTSHORT)
				*va_arg(ap, char *) = nread;
			else if (flags & SHORT)
				*va_arg(ap, short *) = nread;
			else if (flags & LONG)
				*va_arg(ap, long *) = nread;
			else if (flags & LONGLONG)
				*va_arg(ap, long long *) = nread;
			else if (flags & INTMAXT)
				*va_arg(ap, intmax_t *) = nread;
			else if (flags & SIZET)
				*va_arg(ap, size_t *) = nread;
			else if (flags & PTRDIFFT)
				*va_arg(ap, ptrdiff_t *) = nread;
			else
				*va_arg(ap, int *) = nread;
			continue;

		default:
			goto match_failure;

		/*
		 * Disgusting backwards compatibility hack.	XXX
		 */
		case '\0':	/* compat */
			return (EOF);
		}

		/*
		 * Consume leading white space, except for formats
		 * that suppress this.
		 */
		if ((flags & NOSKIP) == 0) {
			while ((wi = __fgetwc(fp, locale)) != WEOF && iswspace(wi))
				nread++;
			if (wi == WEOF)
				goto input_failure;
			__ungetwc(wi, fp, locale);
		}

		/*
		 * Do the conversion.
		 */
		switch (c) {

		case CT_CHAR:
			/* scan arbitrary characters (sets NOSKIP) */
			if (width == 0)
				width = 1;
			if (flags & LONG) {
				nr = convert_wchar(fp, GETARG(wchar_t *), width,
				    locale);
			} else {
				nr = convert_char(fp, GETARG(char *), width,
				    locale);
			}
			if (nr < 0)
				goto input_failure;
			break;

		case CT_CCL:
			/* scan a (nonempty) character class (sets NOSKIP) */
			if (width == 0)
				width = (size_t)~0;	/* `infinity' */
			/* take only those things in the class */
			if (flags & LONG) {
				nr = convert_wccl(fp, GETARG(wchar_t *), width,
				    &ccl, locale);
			} else {
				nr = convert_ccl(fp, GETARG(char *), width,
				    &ccl, locale);
			}
			if (nr <= 0) {
				if (nr < 0)
					goto input_failure;
				else /* nr == 0 */
					goto match_failure;
			}
			break;

		case CT_STRING:
			/* like CCL, but zero-length string OK, & no NOSKIP */
			if (width == 0)
				width = (size_t)~0;
			if (flags & LONG) {
				nr = convert_wstring(fp, GETARG(wchar_t *),
				    width, locale);
			} else {
				nr = convert_string(fp, GETARG(char *), width,
				    locale);
			}
			if (nr < 0)
				goto input_failure;
			break;

		case CT_INT:
			/* scan an integer as if by the conversion function */
			if (width == 0 || width > sizeof(buf) /
			    sizeof(*buf) - 1)
				width = sizeof(buf) / sizeof(*buf) - 1;

			nr = parseint(fp, buf, width, base, flags, locale);
			if (nr == 0)
				goto match_failure;
			if ((flags & SUPPRESS) == 0) {
				uintmax_t res;

				buf[nr] = L'\0';
				if ((flags & UNSIGNED) == 0)
				    res = wcstoimax(buf, NULL, base);
				else
				    res = wcstoumax(buf, NULL, base);
				if (flags & POINTER)
					*va_arg(ap, void **) =
							(void *)(uintptr_t)res;
				else if (flags & SHORTSHORT)
					*va_arg(ap, char *) = res;
				else if (flags & SHORT)
					*va_arg(ap, short *) = res;
				else if (flags & LONG)
					*va_arg(ap, long *) = res;
				else if (flags & LONGLONG)
					*va_arg(ap, long long *) = res;
				else if (flags & INTMAXT)
					*va_arg(ap, intmax_t *) = res;
				else if (flags & PTRDIFFT)
					*va_arg(ap, ptrdiff_t *) = res;
				else if (flags & SIZET)
					*va_arg(ap, size_t *) = res;
				else
					*va_arg(ap, int *) = res;
			}
			break;

#ifndef NO_FLOATING_POINT
		case CT_FLOAT:
			/* scan a floating point number as if by strtod */
			if (width == 0 || width > sizeof(buf) /
			    sizeof(*buf) - 1)
				width = sizeof(buf) / sizeof(*buf) - 1;
			nr = parsefloat(fp, buf, buf + width, locale);
			if (nr == 0)
				goto match_failure;
			if ((flags & SUPPRESS) == 0) {
				if (flags & LONGDBL) {
					long double res = wcstold(buf, NULL);
					*va_arg(ap, long double *) = res;
				} else if (flags & LONG) {
					double res = wcstod(buf, NULL);
					*va_arg(ap, double *) = res;
				} else {
					float res = wcstof(buf, NULL);
					*va_arg(ap, float *) = res;
				}
			}
			break;
#endif /* !NO_FLOATING_POINT */
		}
		if (!(flags & SUPPRESS))
			nassigned++;
		nread += nr;
		nconversions++;
	}
input_failure:
	return (nconversions != 0 ? nassigned : EOF);
match_failure:
	return (nassigned);
}

#ifndef NO_FLOATING_POINT
static int
parsefloat(FILE *fp, wchar_t *buf, wchar_t *end, locale_t locale)
{
	mbstate_t mbs;
	size_t nconv;
	wchar_t *commit, *p;
	int infnanpos = 0;
	enum {
		S_START, S_GOTSIGN, S_INF, S_NAN, S_DONE, S_MAYBEHEX,
		S_DIGITS, S_FRAC, S_EXP, S_EXPDIGITS
	} state = S_START;
	wchar_t c;
	wchar_t decpt;
	_Bool gotmantdig = 0, ishex = 0;

	mbs = initial_mbs;
	nconv = mbrtowc(&decpt, localeconv()->decimal_point, MB_CUR_MAX, &mbs);
	if (nconv == (size_t)-1 || nconv == (size_t)-2)
		decpt = '.';	/* failsafe */

	/*
	 * We set commit = p whenever the string we have read so far
	 * constitutes a valid representation of a floating point
	 * number by itself.  At some point, the parse will complete
	 * or fail, and we will ungetc() back to the last commit point.
	 * To ensure that the file offset gets updated properly, it is
	 * always necessary to read at least one character that doesn't
	 * match; thus, we can't short-circuit "infinity" or "nan(...)".
	 */
	commit = buf - 1;
	c = WEOF;
	for (p = buf; p < end; ) {
		if ((c = __fgetwc(fp, locale)) == WEOF)
			break;
reswitch:
		switch (state) {
		case S_START:
			state = S_GOTSIGN;
			if (c == '-' || c == '+')
				break;
			else
				goto reswitch;
		case S_GOTSIGN:
			switch (c) {
			case '0':
				state = S_MAYBEHEX;
				commit = p;
				break;
			case 'I':
			case 'i':
				state = S_INF;
				break;
			case 'N':
			case 'n':
				state = S_NAN;
				break;
			default:
				state = S_DIGITS;
				goto reswitch;
			}
			break;
		case S_INF:
			if (infnanpos > 6 ||
			    (c != "nfinity"[infnanpos] &&
			     c != "NFINITY"[infnanpos]))
				goto parsedone;
			if (infnanpos == 1 || infnanpos == 6)
				commit = p;	/* inf or infinity */
			infnanpos++;
			break;
		case S_NAN:
			switch (infnanpos) {
			case 0:
				if (c != 'A' && c != 'a')
					goto parsedone;
				break;
			case 1:
				if (c != 'N' && c != 'n')
					goto parsedone;
				else
					commit = p;
				break;
			case 2:
				if (c != '(')
					goto parsedone;
				break;
			default:
				if (c == ')') {
					commit = p;
					state = S_DONE;
				} else if (!iswalnum(c) && c != '_')
					goto parsedone;
				break;
			}
			infnanpos++;
			break;
		case S_DONE:
			goto parsedone;
		case S_MAYBEHEX:
			state = S_DIGITS;
			if (c == 'X' || c == 'x') {
				ishex = 1;
				break;
			} else {	/* we saw a '0', but no 'x' */
				gotmantdig = 1;
				goto reswitch;
			}
		case S_DIGITS:
			if ((ishex && iswxdigit(c)) || iswdigit(c))
				gotmantdig = 1;
			else {
				state = S_FRAC;
				if (c != decpt)
					goto reswitch;
			}
			if (gotmantdig)
				commit = p;
			break;
		case S_FRAC:
			if (((c == 'E' || c == 'e') && !ishex) ||
			    ((c == 'P' || c == 'p') && ishex)) {
				if (!gotmantdig)
					goto parsedone;
				else
					state = S_EXP;
			} else if ((ishex && iswxdigit(c)) || iswdigit(c)) {
				commit = p;
				gotmantdig = 1;
			} else
				goto parsedone;
			break;
		case S_EXP:
			state = S_EXPDIGITS;
			if (c == '-' || c == '+')
				break;
			else
				goto reswitch;
		case S_EXPDIGITS:
			if (iswdigit(c))
				commit = p;
			else
				goto parsedone;
			break;
		default:
			abort();
		}
		*p++ = c;
		c = WEOF;
	}

parsedone:
	if (c != WEOF)
		__ungetwc(c, fp, locale);
	while (commit < --p)
		__ungetwc(*p, fp, locale);
	*++commit = '\0';
	return (commit - buf);
}
#endif
