/*	$OpenBSD: vfwscanf.c,v 1.8 2022/12/27 17:10:06 jmc Exp $ */
/*-
 * Copyright (c) 1990, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * Chris Torek.
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

#include <inttypes.h>
#include <limits.h>
#include <locale.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wctype.h>
#include "local.h"

#ifdef FLOATING_POINT
#include <float.h>
#include "floatio.h"
#endif

#define	BUF		513	/* Maximum length of numeric string. */

/*
 * Flags used during conversion.
 */
#define	LONG		0x00001	/* l: long or double */
#define	LONGDBL		0x00002	/* L: long double */
#define	SHORT		0x00004	/* h: short */
#define	SHORTSHORT	0x00008	/* hh: 8 bit integer */
#define LLONG		0x00010	/* ll: long long (+ deprecated q: quad) */
#define	POINTER		0x00020	/* p: void * (as hex) */
#define	SIZEINT		0x00040	/* z: (signed) size_t */
#define	MAXINT		0x00080	/* j: intmax_t */
#define	PTRINT		0x00100	/* t: ptrdiff_t */
#define	NOSKIP		0x00200	/* [ or c: do not skip blanks */
#define	SUPPRESS	0x00400	/* *: suppress assignment */
#define	UNSIGNED	0x00800	/* %[oupxX] conversions */

/*
 * The following are used in numeric conversions only:
 * SIGNOK, HAVESIGN, NDIGITS, DPTOK, and EXPOK are for floating point;
 * SIGNOK, HAVESIGN, NDIGITS, PFXOK, and NZDIGITS are for integral.
 */
#define	SIGNOK		0x01000	/* +/- is (still) legal */
#define	HAVESIGN	0x02000	/* sign detected */
#define	NDIGITS		0x04000	/* no digits detected */

#define	DPTOK		0x08000	/* (float) decimal point is still legal */
#define	EXPOK		0x10000	/* (float) exponent (e+3, etc) still legal */

#define	PFXOK		0x08000	/* 0x prefix is (still) legal */
#define	NZDIGITS	0x10000	/* no zero digits detected */

/*
 * Conversion types.
 */
#define	CT_CHAR		0	/* %c conversion */
#define	CT_CCL		1	/* %[...] conversion */
#define	CT_STRING	2	/* %s conversion */
#define	CT_INT		3	/* integer, i.e., strtoimax or strtoumax */
#define	CT_FLOAT	4	/* floating, i.e., strtod */

#define u_char unsigned char
#define u_long unsigned long

#define	INCCL(_c)	\
	(cclcompl ? (wmemchr(ccls, (_c), ccle - ccls) == NULL) : \
	(wmemchr(ccls, (_c), ccle - ccls) != NULL))

/*
 * vfwscanf
 */
int
__vfwscanf(FILE * __restrict fp, const wchar_t * __restrict fmt, __va_list ap)
{
	wint_t c;	/* character from format, or conversion */
	size_t width;	/* field width, or 0 */
	wchar_t *p;	/* points into all kinds of strings */
	int n;		/* handy integer */
	int flags;	/* flags as defined above */
	wchar_t *p0;	/* saves original value of p when necessary */
	int nassigned;		/* number of fields assigned */
	int nconversions;	/* number of conversions */
	int nread;		/* number of characters consumed from fp */
	int base;		/* base argument to strtoimax/strtouimax */
	wchar_t buf[BUF];	/* buffer for numeric conversions */
	const wchar_t *ccls;	/* character class start */
	const wchar_t *ccle;	/* character class end */
	int cclcompl;		/* ccl is complemented? */
	wint_t wi;		/* handy wint_t */
	char *mbp;		/* multibyte string pointer for %c %s %[ */
	size_t nconv;		/* number of bytes in mb. conversion */
	char mbbuf[MB_LEN_MAX];	/* temporary mb. character buffer */
 	mbstate_t mbs;
#ifdef FLOATING_POINT
	wchar_t decimal_point = 0;
#endif

	/* `basefix' is used to avoid `if' tests in the integer scanner */
	static short basefix[17] =
		{ 10, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16 };

	_SET_ORIENTATION(fp, 1);

	nassigned = 0;
	nconversions = 0;
	nread = 0;
	base = 0;		/* XXX just to keep gcc happy */
	ccls = ccle = NULL;
	for (;;) {
		c = *fmt++;
		if (c == 0) {
			return (nassigned);
		}
		if (iswspace(c)) {
			while ((c = __fgetwc_unlock(fp)) != WEOF &&
			    iswspace(c))
				;
			if (c != WEOF)
				__ungetwc(c, fp);
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
			if ((wi = __fgetwc_unlock(fp)) == WEOF)
				goto input_failure;
			if (wi != c) {
				__ungetwc(wi, fp);
				goto match_failure;
			}
			nread++;
			continue;

		case '*':
			flags |= SUPPRESS;
			goto again;
		case 'j':
			flags |= MAXINT;
			goto again;
		case 'L':
			flags |= LONGDBL;
			goto again;
		case 'h':
			if (*fmt == 'h') {
				fmt++;
				flags |= SHORTSHORT;
			} else {
				flags |= SHORT;
			}
			goto again;
		case 'l':
			if (*fmt == 'l') {
				fmt++;
				flags |= LLONG;
			} else {
				flags |= LONG;
			}
			goto again;
		case 'q':
			flags |= LLONG;		/* deprecated */
			goto again;
		case 't':
			flags |= PTRINT;
			goto again;
		case 'z':
			flags |= SIZEINT;
			goto again;

		case '0': case '1': case '2': case '3': case '4':
		case '5': case '6': case '7': case '8': case '9':
			width = width * 10 + c - '0';
			goto again;

		/*
		 * Conversions.
		 * Those marked `compat' are for 4.[123]BSD compatibility.
		 *
		 * (According to ANSI, E and X formats are supposed
		 * to the same as e and x.  Sorry about that.)
		 */
		case 'D':	/* compat */
			flags |= LONG;
			/* FALLTHROUGH */
		case 'd':
			c = CT_INT;
			base = 10;
			break;

		case 'i':
			c = CT_INT;
			base = 0;
			break;

		case 'O':	/* compat */
			flags |= LONG;
			/* FALLTHROUGH */
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

#ifdef FLOATING_POINT
		case 'e': case 'E':
		case 'f': case 'F':
		case 'g': case 'G':
		case 'a': case 'A':
			c = CT_FLOAT;
			break;
#endif

		case 's':
			c = CT_STRING;
			break;

		case '[':
			ccls = fmt;
			if (*fmt == '^') {
				cclcompl = 1;
				fmt++;
			} else
				cclcompl = 0;
			if (*fmt == ']')
				fmt++;
			while (*fmt != '\0' && *fmt != ']')
				fmt++;
			ccle = fmt;
			fmt++;
			flags |= NOSKIP;
			c = CT_CCL;
			break;

		case 'c':
			flags |= NOSKIP;
			c = CT_CHAR;
			break;

		case 'p':	/* pointer format is like hex */
			flags |= POINTER | PFXOK;
			c = CT_INT;
			flags |= UNSIGNED;
			base = 16;
			break;

		case 'n':
			nconversions++;
			if (flags & SUPPRESS)
				continue;
			if (flags & SHORTSHORT)
				*va_arg(ap, signed char *) = nread;
			else if (flags & SHORT)
				*va_arg(ap, short *) = nread;
			else if (flags & LONG)
				*va_arg(ap, long *) = nread;
			else if (flags & SIZEINT)
				*va_arg(ap, ssize_t *) = nread;
			else if (flags & PTRINT)
				*va_arg(ap, ptrdiff_t *) = nread;
			else if (flags & LLONG)
				*va_arg(ap, long long *) = nread;
			else if (flags & MAXINT)
				*va_arg(ap, intmax_t *) = nread;
			else
				*va_arg(ap, int *) = nread;
			continue;

		/*
		 * Disgusting backwards compatibility hacks.	XXX
		 */
		case '\0':	/* compat */
			return (EOF);

		default:	/* compat */
			if (iswupper(c))
				flags |= LONG;
			c = CT_INT;
			base = 10;
			break;
		}

		/*
		 * Consume leading white space, except for formats
		 * that suppress this.
		 */
		if ((flags & NOSKIP) == 0) {
			while ((wi = __fgetwc_unlock(fp)) != WEOF &&
			    iswspace(wi))
				nread++;
			if (wi == WEOF)
				goto input_failure;
			__ungetwc(wi, fp);
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
				if (!(flags & SUPPRESS))
					p = va_arg(ap, wchar_t *);
				n = 0;
				while (width-- != 0 &&
				    (wi = __fgetwc_unlock(fp)) != WEOF) {
					if (!(flags & SUPPRESS))
						*p++ = (wchar_t)wi;
					n++;
				}
				if (n == 0)
					goto input_failure;
				nread += n;
 				if (!(flags & SUPPRESS))
 					nassigned++;
			} else {
				if (!(flags & SUPPRESS))
					mbp = va_arg(ap, char *);
				n = 0;
				bzero(&mbs, sizeof(mbs));
				while (width != 0 &&
				    (wi = __fgetwc_unlock(fp)) != WEOF) {
					if (width >= MB_CUR_MAX &&
					    !(flags & SUPPRESS)) {
						nconv = wcrtomb(mbp, wi, &mbs);
						if (nconv == (size_t)-1)
							goto input_failure;
					} else {
						nconv = wcrtomb(mbbuf, wi,
						    &mbs);
						if (nconv == (size_t)-1)
							goto input_failure;
						if (nconv > width) {
							__ungetwc(wi, fp);
 							break;
 						}
						if (!(flags & SUPPRESS))
							memcpy(mbp, mbbuf,
							    nconv);
 					}
					if (!(flags & SUPPRESS))
						mbp += nconv;
					width -= nconv;
					n++;
 				}
				if (n == 0)
 					goto input_failure;
				nread += n;
				if (!(flags & SUPPRESS))
					nassigned++;
			}
			nconversions++;
			break;

		case CT_CCL:
			/* scan a (nonempty) character class (sets NOSKIP) */
			if (width == 0)
				width = (size_t)~0;	/* `infinity' */
			/* take only those things in the class */
			if ((flags & SUPPRESS) && (flags & LONG)) {
				n = 0;
				while ((wi = __fgetwc_unlock(fp)) != WEOF &&
				    width-- != 0 && INCCL(wi))
					n++;
				if (wi != WEOF)
					__ungetwc(wi, fp);
				if (n == 0)
					goto match_failure;
			} else if (flags & LONG) {
				p0 = p = va_arg(ap, wchar_t *);
				while ((wi = __fgetwc_unlock(fp)) != WEOF &&
				    width-- != 0 && INCCL(wi))
					*p++ = (wchar_t)wi;
				if (wi != WEOF)
					__ungetwc(wi, fp);
				n = p - p0;
				if (n == 0)
					goto match_failure;
				*p = 0;
				nassigned++;
			} else {
				if (!(flags & SUPPRESS))
					mbp = va_arg(ap, char *);
				n = 0;
				bzero(&mbs, sizeof(mbs));
				while ((wi = __fgetwc_unlock(fp)) != WEOF &&
				    width != 0 && INCCL(wi)) {
					if (width >= MB_CUR_MAX &&
					   !(flags & SUPPRESS)) {
						nconv = wcrtomb(mbp, wi, &mbs);
						if (nconv == (size_t)-1)
							goto input_failure;
					} else {
						nconv = wcrtomb(mbbuf, wi,
						    &mbs);
						if (nconv == (size_t)-1)
							goto input_failure;
						if (nconv > width)
							break;
						if (!(flags & SUPPRESS))
							memcpy(mbp, mbbuf,
							    nconv);
					}
					if (!(flags & SUPPRESS))
						mbp += nconv;
					width -= nconv;
					n++;
				}
				if (wi != WEOF)
					__ungetwc(wi, fp);
				if (!(flags & SUPPRESS)) {
					*mbp = 0;
					nassigned++;
				}
 			}
			nread += n;
			nconversions++;
			break;

		case CT_STRING:
			/* like CCL, but zero-length string OK, & no NOSKIP */
			if (width == 0)
				width = (size_t)~0;
			if ((flags & SUPPRESS) && (flags & LONG)) {
				while ((wi = __fgetwc_unlock(fp)) != WEOF &&
				    width-- != 0 &&
				    !iswspace(wi))
					nread++;
				if (wi != WEOF)
					__ungetwc(wi, fp);
			} else if (flags & LONG) {
				p0 = p = va_arg(ap, wchar_t *);
				while ((wi = __fgetwc_unlock(fp)) != WEOF &&
				    width-- != 0 &&
				    !iswspace(wi)) {
					*p++ = (wchar_t)wi;
					nread++;
				}
				if (wi != WEOF)
					__ungetwc(wi, fp);
				*p = 0;
				nassigned++;
			} else {
				if (!(flags & SUPPRESS))
					mbp = va_arg(ap, char *);
				bzero(&mbs, sizeof(mbs));
				while ((wi = __fgetwc_unlock(fp)) != WEOF &&
				    width != 0 &&
				    !iswspace(wi)) {
					if (width >= MB_CUR_MAX &&
					    !(flags & SUPPRESS)) {
						nconv = wcrtomb(mbp, wi, &mbs);
						if (nconv == (size_t)-1)
							goto input_failure;
					} else {
						nconv = wcrtomb(mbbuf, wi,
						    &mbs);
						if (nconv == (size_t)-1)
							goto input_failure;
						if (nconv > width)
							break;
						if (!(flags & SUPPRESS))
							memcpy(mbp, mbbuf,
							    nconv);
					}
					if (!(flags & SUPPRESS))
						mbp += nconv;
					width -= nconv;
					nread++;
				}
				if (wi != WEOF)
					__ungetwc(wi, fp);
				if (!(flags & SUPPRESS)) {
					*mbp = 0;
 					nassigned++;
 				}
			}
			nconversions++;
			continue;

		case CT_INT:
			/* scan an integer as if by strtoimax/strtoumax */
			if (width == 0 || width > sizeof(buf) /
			    sizeof(*buf) - 1)
				width = sizeof(buf) / sizeof(*buf) - 1;
			flags |= SIGNOK | NDIGITS | NZDIGITS;
			for (p = buf; width; width--) {
				c = __fgetwc_unlock(fp);
				/*
				 * Switch on the character; `goto ok'
				 * if we accept it as a part of number.
				 */
				switch (c) {

				/*
				 * The digit 0 is always legal, but is
				 * special.  For %i conversions, if no
				 * digits (zero or nonzero) have been
				 * scanned (only signs), we will have
				 * base==0.  In that case, we should set
				 * it to 8 and enable 0x prefixing.
				 * Also, if we have not scanned zero digits
				 * before this, do not turn off prefixing
				 * (someone else will turn it off if we
				 * have scanned any nonzero digits).
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
				 * x ok iff flag still set and 2nd char (or
				 * 3rd char if we have a sign).
				 */
				case 'x': case 'X':
					if ((flags & PFXOK) && p ==
					    buf + 1 + !!(flags & HAVESIGN)) {
						base = 16;	/* if %i */
						flags &= ~PFXOK;
						goto ok;
					}
					break;
				}

				/*
				 * If we got here, c is not a legal character
				 * for a number.  Stop accumulating digits.
				 */
				if (c != WEOF)
					__ungetwc(c, fp);
				break;
		ok:
				/*
				 * c is legal: store it and look at the next.
				 */
				*p++ = (wchar_t)c;
			}
			/*
			 * If we had only a sign, it is no good; push
			 * back the sign.  If the number ends in `x',
			 * it was [sign] '0' 'x', so push back the x
			 * and treat it as [sign] '0'.
			 */
			if (flags & NDIGITS) {
				if (p > buf)
					__ungetwc(*--p, fp);
				goto match_failure;
			}
			c = p[-1];
			if (c == 'x' || c == 'X') {
				--p;
				__ungetwc(c, fp);
			}
			if ((flags & SUPPRESS) == 0) {
				uintmax_t res;

				*p = '\0';
				if (flags & UNSIGNED)
					res = wcstoimax(buf, NULL, base);
				else
					res = wcstoumax(buf, NULL, base);
				if (flags & POINTER)
					*va_arg(ap, void **) =
					    (void *)(uintptr_t)res;
				else if (flags & MAXINT)
					*va_arg(ap, intmax_t *) = res;
				else if (flags & LLONG)
					*va_arg(ap, long long *) = res;
				else if (flags & SIZEINT)
					*va_arg(ap, ssize_t *) = res;
				else if (flags & PTRINT)
					*va_arg(ap, ptrdiff_t *) = res;
				else if (flags & LONG)
					*va_arg(ap, long *) = res;
				else if (flags & SHORT)
					*va_arg(ap, short *) = res;
				else if (flags & SHORTSHORT)
					*va_arg(ap, signed char *) = res;
				else
					*va_arg(ap, int *) = res;
				nassigned++;
			}
			nread += p - buf;
			nconversions++;
			break;

#ifdef FLOATING_POINT
		case CT_FLOAT:
			/* scan a floating point number as if by strtod */
			if (width == 0 || width > sizeof(buf) /
			    sizeof(*buf) - 1)
				width = sizeof(buf) / sizeof(*buf) - 1;
			flags |= SIGNOK | NDIGITS | DPTOK | EXPOK;
			for (p = buf; width; width--) {
				c = __fgetwc_unlock(fp);
				/*
				 * This code mimics the integer conversion
				 * code, but is much simpler.
				 */
				switch (c) {

				case '0': case '1': case '2': case '3':
				case '4': case '5': case '6': case '7':
				case '8': case '9':
					flags &= ~(SIGNOK | NDIGITS);
					goto fok;

				case '+': case '-':
					if (flags & SIGNOK) {
						flags &= ~SIGNOK;
						goto fok;
					}
					break;
				case 'e': case 'E':
					/* no exponent without some digits */
					if ((flags&(NDIGITS|EXPOK)) == EXPOK) {
						flags =
						    (flags & ~(EXPOK|DPTOK)) |
						    SIGNOK | NDIGITS;
						goto fok;
					}
					break;
				default:
					if (decimal_point == 0) {
						bzero(&mbs, sizeof(mbs));
						nconv = mbrtowc(&decimal_point,
						    localeconv()->decimal_point,
					    	    MB_CUR_MAX, &mbs);
						if (nconv == 0 ||
						    nconv == (size_t)-1 ||
						    nconv == (size_t)-2)
							decimal_point = '.';
					}
					if (c == decimal_point &&
					    (flags & DPTOK)) {
						flags &= ~(SIGNOK | DPTOK);
						goto fok;
					}
					break;
				}
				if (c != WEOF)
					__ungetwc(c, fp);
				break;
		fok:
				*p++ = c;
			}
			/*
			 * If no digits, might be missing exponent digits
			 * (just give back the exponent) or might be missing
			 * regular digits, but had sign and/or decimal point.
			 */
			if (flags & NDIGITS) {
				if (flags & EXPOK) {
					/* no digits at all */
					while (p > buf)
						__ungetwc(*--p, fp);
					goto match_failure;
				}
				/* just a bad exponent (e and maybe sign) */
				c = *--p;
				if (c != 'e' && c != 'E') {
					__ungetwc(c, fp);/* sign */
					c = *--p;
				}
				__ungetwc(c, fp);
			}
			if ((flags & SUPPRESS) == 0) {
				*p = 0;
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
				nassigned++;
			}
			nread += p - buf;
			nconversions++;
			break;
#endif /* FLOATING_POINT */
		}
	}
input_failure:
	return (nconversions != 0 ? nassigned : EOF);
match_failure:
	return (nassigned);
}

int
vfwscanf(FILE * __restrict fp, const wchar_t * __restrict fmt, __va_list ap)
{
	int r;

	FLOCKFILE(fp);
	r = __vfwscanf(fp, fmt, ap);
	FUNLOCKFILE(fp);
	return (r);
}
DEF_STRONG(vfwscanf);
