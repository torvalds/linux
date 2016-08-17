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
 * Copyright 2009 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#ifndef _SYS_ZIO_COMPRESS_H
#define	_SYS_ZIO_COMPRESS_H

#include <sys/zio.h>

#ifdef	__cplusplus
extern "C" {
#endif

/* Common signature for all zio compress functions. */
typedef size_t zio_compress_func_t(void *src, void *dst,
    size_t s_len, size_t d_len, int);
/* Common signature for all zio decompress functions. */
typedef int zio_decompress_func_t(void *src, void *dst,
    size_t s_len, size_t d_len, int);

/*
 * Information about each compression function.
 */
typedef const struct zio_compress_info {
	zio_compress_func_t	*ci_compress;	/* compression function */
	zio_decompress_func_t	*ci_decompress;	/* decompression function */
	int			ci_level;	/* level parameter */
	char			*ci_name;	/* algorithm name */
} zio_compress_info_t;

extern zio_compress_info_t zio_compress_table[ZIO_COMPRESS_FUNCTIONS];

/*
 * lz4 compression init & free
 */
extern void lz4_init(void);
extern void lz4_fini(void);

/*
 * Compression routines.
 */
extern size_t lzjb_compress(void *src, void *dst, size_t s_len, size_t d_len,
    int level);
extern int lzjb_decompress(void *src, void *dst, size_t s_len, size_t d_len,
    int level);
extern size_t gzip_compress(void *src, void *dst, size_t s_len, size_t d_len,
    int level);
extern int gzip_decompress(void *src, void *dst, size_t s_len, size_t d_len,
    int level);
extern size_t zle_compress(void *src, void *dst, size_t s_len, size_t d_len,
    int level);
extern int zle_decompress(void *src, void *dst, size_t s_len, size_t d_len,
    int level);
extern size_t lz4_compress_zfs(void *src, void *dst, size_t s_len, size_t d_len,
    int level);
extern int lz4_decompress_zfs(void *src, void *dst, size_t s_len, size_t d_len,
    int level);

/*
 * Compress and decompress data if necessary.
 */
extern size_t zio_compress_data(enum zio_compress c, void *src, void *dst,
    size_t s_len);
extern int zio_decompress_data(enum zio_compress c, void *src, void *dst,
    size_t s_len, size_t d_len);

#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_ZIO_COMPRESS_H */
