/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or http://www.opensolaris.org/os/licensing.
 * See the License for the specific language governing permissions
 * and limitations under the License.
 *
 * When distributing Covered Code, include this CDDL HEADER in each
 * file and include the License file at usr/src/OPENSOLARIS.LICENSE.
 * If applicable, add the following below this CDDL HEADER, with the
 * fields enclosed by brackets "[]" replaced with your own identifying
 * information: Portions Copyright [yyyy] [name of copyright owner]
 *
 * CDDL HEADER END
 */
/*
 * Copyright 2007 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#ifndef	_SYS_U8_TEXTPREP_H
#define	_SYS_U8_TEXTPREP_H

#pragma ident	"%Z%%M%	%I%	%E% SMI"

#include <sys/isa_defs.h>
#include <sys/types.h>
#include <sys/errno.h>

#ifdef	__cplusplus
extern "C" {
#endif

#ifdef illumos
/*
 * Unicode encoding conversion functions and their macros.
 */
#define	UCONV_IN_BIG_ENDIAN		0x0001
#define	UCONV_OUT_BIG_ENDIAN		0x0002
#define	UCONV_IN_SYSTEM_ENDIAN		0x0004
#define	UCONV_OUT_SYSTEM_ENDIAN		0x0008
#define	UCONV_IN_LITTLE_ENDIAN		0x0010
#define	UCONV_OUT_LITTLE_ENDIAN		0x0020
#define	UCONV_IGNORE_NULL		0x0040
#define	UCONV_IN_ACCEPT_BOM		0x0080
#define	UCONV_OUT_EMIT_BOM		0x0100

extern int uconv_u16tou32(const uint16_t *, size_t *, uint32_t *, size_t *,
	int);
extern int uconv_u16tou8(const uint16_t *, size_t *, uchar_t *, size_t *, int);
extern int uconv_u32tou16(const uint32_t *, size_t *, uint16_t *, size_t *,
	int);
extern int uconv_u32tou8(const uint32_t *, size_t *, uchar_t *, size_t *, int);
extern int uconv_u8tou16(const uchar_t *, size_t *, uint16_t *, size_t *, int);
extern int uconv_u8tou32(const uchar_t *, size_t *, uint32_t *, size_t *, int);
#endif	/* illumos */

/*
 * UTF-8 text preparation functions and their macros.
 *
 * Among the macros defined, U8_CANON_DECOMP, U8_COMPAT_DECOMP, and
 * U8_CANON_COMP are not public interfaces and must not be used directly
 * at the flag input argument.
 */
#define	U8_STRCMP_CS			(0x00000001)
#define	U8_STRCMP_CI_UPPER		(0x00000002)
#define	U8_STRCMP_CI_LOWER		(0x00000004)

#define	U8_CANON_DECOMP			(0x00000010)
#define	U8_COMPAT_DECOMP		(0x00000020)
#define	U8_CANON_COMP			(0x00000040)

#define	U8_STRCMP_NFD			(U8_CANON_DECOMP)
#define	U8_STRCMP_NFC			(U8_CANON_DECOMP | U8_CANON_COMP)
#define	U8_STRCMP_NFKD			(U8_COMPAT_DECOMP)
#define	U8_STRCMP_NFKC			(U8_COMPAT_DECOMP | U8_CANON_COMP)

#define	U8_TEXTPREP_TOUPPER		(U8_STRCMP_CI_UPPER)
#define	U8_TEXTPREP_TOLOWER		(U8_STRCMP_CI_LOWER)

#define	U8_TEXTPREP_NFD			(U8_STRCMP_NFD)
#define	U8_TEXTPREP_NFC			(U8_STRCMP_NFC)
#define	U8_TEXTPREP_NFKD		(U8_STRCMP_NFKD)
#define	U8_TEXTPREP_NFKC		(U8_STRCMP_NFKC)

#define	U8_TEXTPREP_IGNORE_NULL		(0x00010000)
#define	U8_TEXTPREP_IGNORE_INVALID	(0x00020000)
#define	U8_TEXTPREP_NOWAIT		(0x00040000)

#define	U8_UNICODE_320			(0)
#define	U8_UNICODE_500			(1)
#define	U8_UNICODE_LATEST		(U8_UNICODE_500)

#define	U8_VALIDATE_ENTIRE		(0x00100000)
#define	U8_VALIDATE_CHECK_ADDITIONAL	(0x00200000)
#define	U8_VALIDATE_UCS2_RANGE		(0x00400000)

#define	U8_ILLEGAL_CHAR			(-1)
#define	U8_OUT_OF_RANGE_CHAR		(-2)

extern int u8_validate(char *, size_t, char **, int, int *);
extern int u8_strcmp(const char *, const char *, size_t, int, size_t, int *);
extern size_t u8_textprep_str(char *, size_t *, char *, size_t *, int, size_t,
	int *);

#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_U8_TEXTPREP_H */
