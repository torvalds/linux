/* $FreeBSD$ */
/* $NetBSD: citrus_fix_grouping.h,v 1.2 2009/01/11 02:46:24 christos Exp $ */

/*-
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c)2008 Citrus Project,
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
 */

#ifndef _CITRUS_FIX_GROUPING_H_
#define _CITRUS_FIX_GROUPING_H_

#define _CITRUS_LC_GROUPING_VALUE_MIN		0
#define _CITRUS_LC_GROUPING_VALUE_MAX		126
#define _CITRUS_LC_GROUPING_VALUE_NO_FUTHER	127

#if CHAR_MAX != _CITRUS_LC_GROUPING_VALUE_NO_FUTHER
static __inline void
_citrus_fixup_char_max_md(char *grouping)
{
	char *p;

	for (p = grouping; *p != '\0'; ++p)
		if (*p == _CITRUS_LC_GROUPING_VALUE_NO_FUTHER)
			*p = (char)CHAR_MAX;
}
#define _CITRUS_FIXUP_CHAR_MAX_MD(grouping) \
    _citrus_fixup_char_max_md(__DECONST(void *, grouping))
#else
#define _CITRUS_FIXUP_CHAR_MAX_MD(grouping)	/* nothing to do */
#endif

#endif /*_CITRUS_FIX_GROUPING_H_*/
