/*	$OpenBSD: bytebuf.h,v 1.3 2012/05/08 13:15:11 yasuoka Exp $ */
/*-
 * Copyright (c) 2009 Internet Initiative Japan Inc.
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
#ifndef	BYTEBUF_H
#define	BYTEBUF_H 1

/* $Id: bytebuf.h,v 1.3 2012/05/08 13:15:11 yasuoka Exp $ */

typedef struct _bytebuffer bytebuffer;

extern const void * BYTEBUFFER_PUT_DIRECT;
extern void * BYTEBUFFER_GET_DIRECT;

#ifdef __cplusplus
extern "C" {
#endif

bytebuffer   *bytebuffer_create (size_t);
bytebuffer   *bytebuffer_wrap (void *, size_t);
void         *bytebuffer_unwrap (bytebuffer *);
int          bytebuffer_realloc (bytebuffer *, size_t);
void         bytebuffer_compact (bytebuffer *);
void         *bytebuffer_put (bytebuffer *, const void *, size_t);
void         *bytebuffer_get (bytebuffer *, void *, size_t);
int          bytebuffer_position (bytebuffer *);
int          bytebuffer_limit (bytebuffer *);
int          bytebuffer_capacity (bytebuffer *);
void         *bytebuffer_pointer (bytebuffer *);
size_t        bytebuffer_remaining (bytebuffer *);
int          bytebuffer_has_remaining (bytebuffer *);
void         bytebuffer_flip (bytebuffer *);
void         bytebuffer_rewind (bytebuffer *);
void         bytebuffer_clear (bytebuffer *);
void         bytebuffer_destroy (bytebuffer *);
void         bytebuffer_mark (bytebuffer *);
void         bytebuffer_reset (bytebuffer *);

#ifdef __cplusplus
}
#endif

#endif	/* !BYTEBUF_H */
