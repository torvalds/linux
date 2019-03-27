/*	$FreeBSD$	*/
/* $OpenBSD: deflate.h,v 1.3 2002/03/14 01:26:51 millert Exp $ */

/*-
 * Copyright (c) 2001 Jean-Jacques Bernard-Gundol (jj@wabbitt.org)
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *   notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *   notice, this list of conditions and the following disclaimer in the
 *   documentation and/or other materials provided with the distribution.
 * 3. The name of the author may not be used to endorse or promote products
 *   derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * Definition for the wrapper around the deflate compression 
 * algorithm used in /sys/crypto
 */

#ifndef _CRYPTO_DEFLATE_H_
#define _CRYPTO_DEFLATE_H_

#include <sys/zlib.h>

#define Z_METHOD	8
#define Z_MEMLEVEL	8
#define MINCOMP		2	/* won't be used, but must be defined */
#define ZBUF		10

u_int32_t deflate_global(u_int8_t *, u_int32_t, int, u_int8_t **);
void *z_alloc(void *, u_int, u_int);
void z_free(void *, void *);

/*
 * We are going to use a combined allocation to hold the metadata
 * from the struct immediately followed by the real application data.
 */
struct deflate_buf {
	struct deflate_buf *next;
	uint32_t size;
	uint8_t data[];
};

#endif /* _CRYPTO_DEFLATE_H_ */
