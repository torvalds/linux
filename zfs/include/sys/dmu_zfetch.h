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

#ifndef	_DFETCH_H
#define	_DFETCH_H

#include <sys/zfs_context.h>

#ifdef	__cplusplus
extern "C" {
#endif

extern unsigned long	zfetch_array_rd_sz;

struct dnode;				/* so we can reference dnode */

typedef enum zfetch_dirn {
	ZFETCH_FORWARD = 1,		/* prefetch increasing block numbers */
	ZFETCH_BACKWARD	= -1		/* prefetch decreasing block numbers */
} zfetch_dirn_t;

typedef struct zstream {
	uint64_t	zst_offset;	/* offset of starting block in range */
	uint64_t	zst_len;	/* length of range, in blocks */
	zfetch_dirn_t	zst_direction;	/* direction of prefetch */
	uint64_t	zst_stride;	/* length of stride, in blocks */
	uint64_t	zst_ph_offset;	/* prefetch offset, in blocks */
	uint64_t	zst_cap;	/* prefetch limit (cap), in blocks */
	kmutex_t	zst_lock;	/* protects stream */
	clock_t		zst_last;	/* lbolt of last prefetch */
	list_node_t	zst_node;	/* next zstream here */
} zstream_t;

typedef struct zfetch {
	krwlock_t	zf_rwlock;	/* protects zfetch structure */
	list_t		zf_stream;	/* AVL tree of zstream_t's */
	struct dnode	*zf_dnode;	/* dnode that owns this zfetch */
	uint32_t	zf_stream_cnt;	/* # of active streams */
	uint64_t	zf_alloc_fail;	/* # of failed attempts to alloc strm */
} zfetch_t;

void		zfetch_init(void);
void		zfetch_fini(void);

void		dmu_zfetch_init(zfetch_t *, struct dnode *);
void		dmu_zfetch_rele(zfetch_t *);
void		dmu_zfetch(zfetch_t *, uint64_t, uint64_t, int);


#ifdef	__cplusplus
}
#endif

#endif	/* _DFETCH_H */
