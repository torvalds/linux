/*-
 * Copyright (c) 2013 Andre Oppermann <andre@FreeBSD.org>
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
 * 3. The name of the author may not be used to endorse or promote
 *    products derived from this software without specific prior written
 *    permission.
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
 *
 * $FreeBSD$
 */

/*
 * SipHash is a family of pseudorandom functions (a.k.a. keyed hash functions)
 * optimized for speed on short messages returning a 64bit hash/digest value.
 *
 * The number of rounds is defined during the initialization:
 *  SipHash24_Init() for the fast and resonable strong version
 *  SipHash48_Init() for the strong version (half as fast)
 *
 * struct SIPHASH_CTX ctx;
 * SipHash24_Init(&ctx);
 * SipHash_SetKey(&ctx, "16bytes long key");
 * SipHash_Update(&ctx, pointer_to_string, length_of_string);
 * SipHash_Final(output, &ctx);
 */

#ifndef _SIPHASH_H_
#define _SIPHASH_H_

#define SIPHASH_BLOCK_LENGTH	 8
#define SIPHASH_KEY_LENGTH	16
#define SIPHASH_DIGEST_LENGTH	 8

typedef struct _SIPHASH_CTX {
	uint64_t	v[4];
	union {
		uint64_t	b64;
		uint8_t		b8[8];
	} buf;
	uint64_t	bytes;
	uint8_t		buflen;
	uint8_t		rounds_compr;
	uint8_t		rounds_final;
	uint8_t		initialized;
} SIPHASH_CTX;


#define SipHash24_Init(x)	SipHash_InitX((x), 2, 4)
#define SipHash48_Init(x)	SipHash_InitX((x), 4, 8)
void SipHash_InitX(SIPHASH_CTX *, int, int);
void SipHash_SetKey(SIPHASH_CTX *,
    const uint8_t[__min_size(SIPHASH_KEY_LENGTH)]);
void SipHash_Update(SIPHASH_CTX *, const void *, size_t);
void SipHash_Final(uint8_t[__min_size(SIPHASH_DIGEST_LENGTH)], SIPHASH_CTX *);
uint64_t SipHash_End(SIPHASH_CTX *);

#define SipHash24(x, y, z, i)	SipHashX((x), 2, 4, (y), (z), (i));
#define SipHash48(x, y, z, i)	SipHashX((x), 4, 8, (y), (z), (i));
uint64_t SipHashX(SIPHASH_CTX *, int, int,
    const uint8_t[__min_size(SIPHASH_KEY_LENGTH)], const void *, size_t);

int SipHash24_TestVectors(void);

#endif /* _SIPHASH_H_ */
