/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2018 Solarflare Communications Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE,
 * EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * The views and conclusions contained in the software and documentation are
 * those of the authors and should not be interpreted as representing official
 * policies, either expressed or implied, of the FreeBSD Project.
 *
 * $FreeBSD$
 */

#ifndef	_SYS_EFX_ANNOTE_H
#define	_SYS_EFX_ANNOTE_H

#if defined(_WIN32) || defined(_WIN64)
#define	EFX_HAVE_WINDOWS_ANNOTATIONS 1
#else
#define	EFX_HAVE_WINDOWS_ANNOTATIONS 0
#endif	/* defined(_WIN32) || defined(_WIN64) */

#if defined(__sun)
#define	EFX_HAVE_SOLARIS_ANNOTATIONS 1
#else
#define	EFX_HAVE_SOLARIS_ANNOTATIONS 0
#endif	/* defined(__sun) */

#if !EFX_HAVE_WINDOWS_ANNOTATIONS

/* Ignore Windows SAL annotations on other platforms */
#define	__in
#define	__in_opt
#define	__in_ecount(_n)
#define	__in_ecount_opt(_n)
#define	__in_bcount(_n)
#define	__in_bcount_opt(_n)

#define	__out
#define	__out_opt
#define	__out_ecount(_n)
#define	__out_ecount_opt(_n)
#define	__out_ecount_part(_n, _l)
#define	__out_bcount(_n)
#define	__out_bcount_opt(_n)
#define	__out_bcount_part(_n, _l)
#define	__out_bcount_part_opt(_n, _l)

#define	__deref_out
#define	__deref_inout

#define	__inout
#define	__inout_opt
#define	__inout_ecount(_n)
#define	__inout_ecount_opt(_n)
#define	__inout_bcount(_n)
#define	__inout_bcount_opt(_n)
#define	__inout_bcount_full_opt(_n)

#define	__deref_out_bcount_opt(n)

#define	__checkReturn
#define	__success(_x)

#define	__drv_when(_p, _c)

#endif	/* !EFX_HAVE_WINDOWS_ANNOTATIONS */

#if !EFX_HAVE_SOLARIS_ANNOTATIONS

#if EFX_HAVE_WINDOWS_ANNOTATIONS

/*
 * Support some SunOS/Solaris style _NOTE() annotations
 *
 * At present with the facilities provided in the WDL and the SAL we can only
 * easily act upon _NOTE(ARGUNUSED(arglist)) annotations.
 *
 * Intermediate macros to expand individual _NOTE annotation types into
 * something the WDK or SAL can understand.  They shouldn't be used directly,
 * for example EFX_NOTE_ARGUNUSED() is only used as an intermediate step on the
 * transformation of _NOTE(ARGUNSED(arg1, arg2)) into
 * UNREFERENCED_PARAMETER((arg1, arg2));
 */
#define	EFX_NOTE_ALIGNMENT(_fname, _n)
#define	EFX_NOTE_ARGUNUSED(...)		UNREFERENCED_PARAMETER((__VA_ARGS__));
#define	EFX_NOTE_CONSTANTCONDITION
#define	EFX_NOTE_CONSTCOND
#define	EFX_NOTE_EMPTY
#define	EFX_NOTE_FALLTHROUGH
#define	EFX_NOTE_FALLTHRU
#define	EFX_NOTE_LINTED(_msg)
#define	EFX_NOTE_NOTREACHED
#define	EFX_NOTE_PRINTFLIKE(_n)
#define	EFX_NOTE_SCANFLIKE(_n)
#define	EFX_NOTE_VARARGS(_n)

#define	_NOTE(_annotation)		EFX_NOTE_ ## _annotation

#else

/* Ignore Solaris annotations on other platforms */

#define	_NOTE(_annotation)

#endif	/* EFX_HAVE_WINDOWS_ANNOTATIONS */

#endif	/* !EFX_HAVE_SOLARIS_ANNOTATIONS */

#endif	/* _SYS_EFX_ANNOTE_H */
