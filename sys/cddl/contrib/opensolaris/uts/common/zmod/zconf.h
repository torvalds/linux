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

#ifndef	_ZCONF_H
#define	_ZCONF_H

#pragma ident	"%Z%%M%	%I%	%E% SMI"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/types.h>

#ifdef	__cplusplus
extern "C" {
#endif

/*
 * We don't want to turn on zlib's debugging.
 */
#undef DEBUG

/*
 * We define our own memory allocation and deallocation routines that use kmem.
 */
#define	MY_ZCALLOC

/*
 * We don't define HAVE_MEMCPY here, but do in zutil.c, and implement our
 * our versions of zmemcpy(), zmemzero(), and zmemcmp().
 */

/*
 * We have a sufficiently capable compiler as to not need zlib's compiler hack.
 */
#define	NO_DUMMY_DECL

#define	compressBound(len)	(len + (len >> 12) + (len >> 14) + 11)

#define	z_off_t	off_t
#define	OF(p)	p
#define	ZEXTERN	extern
#define	ZEXPORT
#define	ZEXPORTVA
#define	FAR

#define	deflateInit_		z_deflateInit_
#define	deflate			z_deflate
#define	deflateEnd		z_deflateEnd
#define	inflateInit_		z_inflateInit_
#define	inflate			z_inflate
#define	inflateEnd		z_inflateEnd
#define	deflateInit2_		z_deflateInit2_
#define	deflateSetDictionary	z_deflateSetDictionary
#define	deflateCopy		z_deflateCopy
#define	deflateReset		z_deflateReset
#define	deflateParams		z_deflateParams
#define	deflateBound		z_deflateBound
#define	deflatePrime		z_deflatePrime
#define	inflateInit2_		z_inflateInit2_
#define	inflateSetDictionary	z_inflateSetDictionary
#define	inflateSync		z_inflateSync
#define	inflateSyncPoint	z_inflateSyncPoint
#define	inflateCopy		z_inflateCopy
#define	inflateReset		z_inflateReset
#define	inflateBack		z_inflateBack
#define	inflateBackEnd		z_inflateBackEnd
#define	compress		zz_compress
#define	compress2		zz_compress2
#define	uncompress		zz_uncompress
#define	adler32			z_adler32
#define	crc32			z_crc32
#define	get_crc_table		z_get_crc_table
#define	zError			z_zError

#define	MAX_MEM_LEVEL	9
#define	MAX_WBITS	15

typedef unsigned char Byte;
typedef unsigned int uInt;
typedef unsigned long uLong;
typedef Byte Bytef;
typedef char charf;
typedef int intf;
typedef uInt uIntf;
typedef uLong uLongf;
typedef void *voidpc;
typedef void *voidpf;
typedef void *voidp;

#ifdef	__cplusplus
}
#endif

#endif	/* _ZCONF_H */
