/*	$OpenBSD: _wcstod.h,v 1.4 2015/10/01 02:32:07 guenther Exp $	*/
/* $NetBSD: wcstod.c,v 1.4 2001/10/28 12:08:43 yamt Exp $ */

/*-
 * Copyright (c)1999, 2000, 2001 Citrus Project,
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *	$Citrus: xpg4dl/FreeBSD/lib/libc/locale/wcstod.c,v 1.2 2001/09/27 16:23:57 yamt Exp $
 */

/*
 * function template for wcstof, wcstod and wcstold.
 *
 * parameters:
 *	FUNCNAME : function name
 *      float_type : return type
 *      STRTOD_FUNC : conversion function
 */

float_type
FUNCNAME(const wchar_t *nptr, wchar_t **endptr)
{
	const wchar_t *src;
	size_t size;
	const wchar_t *start;
	const wchar_t *aftersign;

	/*
	 * check length of string and call strtod
	 */
	src = nptr;

	/* skip space first */
	while (iswspace(*src)) {
		src++;
	}

	/* get length of string */
	start = src;
	if (*src && wcschr(L"+-", *src))
		src++;
	aftersign = src;
	if (wcsncasecmp(src, L"inf", 3) == 0) {
		src += 3;
		if (wcsncasecmp(src, L"inity", 5) == 0)
			src += 5;
		goto match;
	}
	if (wcsncasecmp(src, L"nan", 3) == 0) {
		src += 3;
		if (*src == L'(') {
			size = 1;
			while (src[size] != L'\0' && src[size] != L')')
				size++;
			if (src[size] == L')')
				src += size + 1;
		}
		goto match;
	}
	size = wcsspn(src, L"0123456789");
	src += size;
	if (*src == L'.') {/* XXX use localeconv */
		src++;
		size = wcsspn(src, L"0123456789");
		src += size;
	}
	if (*src && wcschr(L"Ee", *src)) {
		src++;
		if (*src && wcschr(L"+-", *src))
			src++;
		size = wcsspn(src, L"0123456789");
		src += size;
	}
match:
	size = src - start;

	/*
	 * convert to a char-string and pass it to strtod.
	 */
	if (src > aftersign) {
		mbstate_t st;
		char *buf;
		char *end;
		const wchar_t *s;
		size_t size_converted;
		float_type result;
		size_t bufsize;

		s = start;
		memset(&st, 0, sizeof(st));
		bufsize = wcsnrtombs(NULL, &s, size, 0, &st);

		buf = malloc(bufsize + 1);
		if (!buf) {
			errno = ENOMEM; /* XXX */
			goto fail;
		}

		s = start;
		memset(&st, 0, sizeof(st));
		size_converted = wcsnrtombs(buf, &s, size, bufsize, &st);
		if (size_converted != bufsize) {
			/* XXX should not happen */
			free(buf);
			errno = EILSEQ;
			goto fail;
		}

		buf[bufsize] = 0;
		result = STRTOD_FUNC(buf, &end);

		if (endptr) {
			const char *s = buf;
			memset(&st, 0, sizeof(st));
			size = mbsnrtowcs(NULL, &s, end - buf, 0, &st);
			*endptr = (wchar_t*)start + size;
		}

		free(buf);

		return result;
	}

fail:
	if (endptr)
		*endptr = (wchar_t*)nptr;

	return 0;
}
DEF_STRONG(FUNCNAME);
