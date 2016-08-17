/*
 * CDDL HEADER START
 *
 * This file and its contents are supplied under the terms of the
 * Common Development and Distribution License ("CDDL"), version 1.0.
 * You may only use this file in accordance with the terms of version
 * 1.0 of the CDDL.
 *
 * A full copy of the text of the CDDL should have accompanied this
 * source.  A copy of the CDDL is also available via the Internet at
 * http://www.illumos.org/license/CDDL.
 *
 * CDDL HEADER END
 */

/*
 * Copyright (c) 2013 by Delphix. All rights reserved.
 */

#ifndef _SYS_BLKPTR_H
#define	_SYS_BLKPTR_H

#include <sys/spa.h>
#include <sys/zio.h>

#ifdef	__cplusplus
extern "C" {
#endif

void encode_embedded_bp_compressed(blkptr_t *, void *,
    enum zio_compress, int, int);
void decode_embedded_bp_compressed(const blkptr_t *, void *);

#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_BLKPTR_H */
