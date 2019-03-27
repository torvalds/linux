/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2001, 2002 Mike Barcroft <mike@FreeBSD.org>
 * Copyright (c) 2001 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Klaus Klein.
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
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#ifndef	_MACHINE__STDINT_H_
#define	_MACHINE__STDINT_H_

#if !defined(__cplusplus) || defined(__STDC_CONSTANT_MACROS)

#define	INT8_C(c)		(c)
#define	INT16_C(c)		(c)
#define	INT32_C(c)		(c)
#define	INT64_C(c)		(c ## L)

#define	UINT8_C(c)		(c)
#define	UINT16_C(c)		(c)
#define	UINT32_C(c)		(c ## U)
#define	UINT64_C(c)		(c ## UL)

#define	INTMAX_C(c)		INT64_C(c)
#define	UINTMAX_C(c)		UINT64_C(c)

#endif /* !defined(__cplusplus) || defined(__STDC_CONSTANT_MACROS) */

#if !defined(__cplusplus) || defined(__STDC_LIMIT_MACROS)

/*
 * ISO/IEC 9899:1999
 * 7.18.2.1 Limits of exact-width integer types
 */
/* Minimum values of exact-width signed integer types. */
#define	INT8_MIN	(-0x7f-1)
#define	INT16_MIN	(-0x7fff-1)
#define	INT32_MIN	(-0x7fffffff-1)
#define	INT64_MIN	(-0x7fffffffffffffffL-1)

/* Maximum values of exact-width signed integer types. */
#define	INT8_MAX	0x7f
#define	INT16_MAX	0x7fff
#define	INT32_MAX	0x7fffffff
#define	INT64_MAX	0x7fffffffffffffffL

/* Maximum values of exact-width unsigned integer types. */
#define	UINT8_MAX	0xff
#define	UINT16_MAX	0xffff
#define	UINT32_MAX	0xffffffffU
#define	UINT64_MAX	0xffffffffffffffffUL

/*
 * ISO/IEC 9899:1999
 * 7.18.2.2  Limits of minimum-width integer types
 */
/* Minimum values of minimum-width signed integer types. */
#define	INT_LEAST8_MIN	INT8_MIN
#define	INT_LEAST16_MIN	INT16_MIN
#define	INT_LEAST32_MIN	INT32_MIN
#define	INT_LEAST64_MIN	INT64_MIN

/* Maximum values of minimum-width signed integer types. */
#define	INT_LEAST8_MAX	INT8_MAX
#define	INT_LEAST16_MAX	INT16_MAX
#define	INT_LEAST32_MAX	INT32_MAX
#define	INT_LEAST64_MAX	INT64_MAX

/* Maximum values of minimum-width unsigned integer types. */
#define	UINT_LEAST8_MAX	 UINT8_MAX
#define	UINT_LEAST16_MAX UINT16_MAX
#define	UINT_LEAST32_MAX UINT32_MAX
#define	UINT_LEAST64_MAX UINT64_MAX

/*
 * ISO/IEC 9899:1999
 * 7.18.2.3  Limits of fastest minimum-width integer types
 */
/* Minimum values of fastest minimum-width signed integer types. */
#define	INT_FAST8_MIN	INT32_MIN
#define	INT_FAST16_MIN	INT32_MIN
#define	INT_FAST32_MIN	INT32_MIN
#define	INT_FAST64_MIN	INT64_MIN

/* Maximum values of fastest minimum-width signed integer types. */
#define	INT_FAST8_MAX	INT32_MAX
#define	INT_FAST16_MAX	INT32_MAX
#define	INT_FAST32_MAX	INT32_MAX
#define	INT_FAST64_MAX	INT64_MAX

/* Maximum values of fastest minimum-width unsigned integer types. */
#define	UINT_FAST8_MAX	UINT32_MAX
#define	UINT_FAST16_MAX	UINT32_MAX
#define	UINT_FAST32_MAX	UINT32_MAX
#define	UINT_FAST64_MAX	UINT64_MAX

/*
 * ISO/IEC 9899:1999
 * 7.18.2.4  Limits of integer types capable of holding object pointers
 */
#define	INTPTR_MIN	INT64_MIN
#define	INTPTR_MAX	INT64_MAX
#define	UINTPTR_MAX	UINT64_MAX

/*
 * ISO/IEC 9899:1999
 * 7.18.2.5  Limits of greatest-width integer types
 */
#define	INTMAX_MIN	INT64_MIN
#define	INTMAX_MAX	INT64_MAX
#define	UINTMAX_MAX	UINT64_MAX

/*
 * ISO/IEC 9899:1999
 * 7.18.3  Limits of other integer types
 */
/* Limits of ptrdiff_t. */
#define	PTRDIFF_MIN	INT64_MIN	
#define	PTRDIFF_MAX	INT64_MAX

/* Limits of sig_atomic_t. */
#define	SIG_ATOMIC_MIN	INT32_MIN
#define	SIG_ATOMIC_MAX	INT32_MAX

/* Limit of size_t. */
#define	SIZE_MAX	UINT64_MAX

/* Limits of wint_t. */
#define	WINT_MIN	INT32_MIN
#define	WINT_MAX	INT32_MAX

#endif /* !defined(__cplusplus) || defined(__STDC_LIMIT_MACROS) */

#endif /* !_MACHINE__STDINT_H_ */
