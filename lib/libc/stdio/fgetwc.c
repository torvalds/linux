/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2002-2004 Tim J. Robbins.
 * All rights reserved.
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
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include "namespace.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <wchar.h>
#include "un-namespace.h"
#include "libc_private.h"
#include "local.h"
#include "mblocal.h"
#include "xlocale_private.h"

/*
 * MT-safe version.
 */
wint_t
fgetwc_l(FILE *fp, locale_t locale)
{
	wint_t r;
	FIX_LOCALE(locale);

	FLOCKFILE_CANCELSAFE(fp);
	ORIENT(fp, 1);
	r = __fgetwc(fp, locale);
	FUNLOCKFILE_CANCELSAFE();

	return (r);
}

wint_t
fgetwc(FILE *fp)
{
	return fgetwc_l(fp, __get_locale());
}

/*
 * Internal (non-MPSAFE) version of fgetwc().  This version takes an
 * mbstate_t argument specifying the initial conversion state.  For
 * wide streams, this should always be fp->_mbstate.  On return, *nread
 * is set to the number of bytes read.
 */
wint_t 
__fgetwc_mbs(FILE *fp, mbstate_t *mbs, int *nread, locale_t locale)
{
	wchar_t wc;
	size_t nconv;
	struct xlocale_ctype *l = XLOCALE_CTYPE(locale);

	*nread = 0;
	if (fp->_r <= 0 && __srefill(fp))
		return (WEOF);
	do {
		nconv = l->__mbrtowc(&wc, fp->_p, fp->_r, mbs);
		if (nconv == (size_t)-1) {
			fp->_flags |= __SERR;
			return (WEOF);
		} else if (nconv == (size_t)-2)
			continue;
		else if (nconv == 0) {
			fp->_p++;
			fp->_r--;
			(*nread)++;
			return (L'\0');
		} else {
			fp->_p += nconv;
			fp->_r -= nconv;
			*nread += nconv;
			return (wc);
		}
	} while (__srefill(fp) == 0);
	if (__sfeof(fp)) {
		fp->_flags |= __SERR;
		errno = EILSEQ;
	}
	return (WEOF);
}
