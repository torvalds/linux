/*	$OpenBSD: rmd160.h,v 1.5 2009/07/05 19:33:46 millert Exp $	*/
/*
 * Copyright (c) 2001 Markus Friedl.  All rights reserved.
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
#ifndef  _RMD160_H
#define  _RMD160_H

#define RMD160_BLOCK_LENGTH		64
#define RMD160_DIGEST_LENGTH		20

/* RMD160 context. */
typedef struct RMD160Context {
	u_int32_t state[5];			/* state */
	u_int64_t count;			/* number of bits, mod 2^64 */
	u_char buffer[RMD160_BLOCK_LENGTH];	/* input buffer */
} RMD160_CTX;

__BEGIN_DECLS
void	 RMD160Init(RMD160_CTX *);
void	 RMD160Transform(u_int32_t [5], const u_char [RMD160_BLOCK_LENGTH])
	     __attribute__((__bounded__(__minbytes__,1,5)))
	     __attribute__((__bounded__(__minbytes__,2,RMD160_BLOCK_LENGTH)));
void	 RMD160Update(RMD160_CTX *, const u_char *, u_int32_t)
	     __attribute__((__bounded__(__string__,2,3)));
void	 RMD160Final(u_char [RMD160_DIGEST_LENGTH], RMD160_CTX *)
	     __attribute__((__bounded__(__minbytes__,1,RMD160_DIGEST_LENGTH)));
__END_DECLS

#endif  /* _RMD160_H */
