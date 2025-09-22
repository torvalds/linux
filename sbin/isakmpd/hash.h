/* $OpenBSD: hash.h,v 1.10 2025/07/18 03:16:28 tb Exp $	 */
/* $EOM: hash.h,v 1.6 1998/07/25 22:04:36 niklas Exp $	 */

/*
 * Copyright (c) 1998 Niels Provos.  All rights reserved.
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

/*
 * This code was written under funding by Ericsson Radio Systems.
 */

#ifndef _HASH_H_
#define _HASH_H_

/* Normal mode hash encapsulation */

#define MD5_SIZE	16
#define SHA1_SIZE	20
#define SHA2_256_SIZE	32
#define SHA2_384_SIZE	48
#define SHA2_512_SIZE	64
#define HASH_MAX	SHA2_512_SIZE

enum hashes {
	HASH_MD5 = 0,
	HASH_SHA1,
	HASH_SHA2_256,
	HASH_SHA2_384,
	HASH_SHA2_512
};

union ANY_CTX;

struct hash {
	enum hashes     type;
	int             id;	/* ISAKMP/Oakley ID */
	u_int8_t        hashsize;	/* Size of the hash */
	unsigned	blocklen;	/* The hash's block length */
	void           *ctx;	/* Pointer to a context, for HMAC ictx */
	unsigned char  *digest;	/* Pointer to a digest */
	int             ctxsize;
	void           *ctx2;	/* Pointer to a 2nd context, for HMAC octx */
	void            (*Init) (union ANY_CTX *);
	void            (*Update) (union ANY_CTX *, const unsigned char *, size_t);
	void            (*Final) (unsigned char *, union ANY_CTX *);
	void            (*HMACInit) (struct hash *, unsigned char *, unsigned int);
	void            (*HMACFinal) (unsigned char *, struct hash *);
};

/* HMAC Hash Encapsulation */

#define HMAC_IPAD_VAL	0x36
#define HMAC_OPAD_VAL	0x5C

extern struct hash *hash_get(enum hashes);
extern void     hmac_init(struct hash *, unsigned char *, unsigned int);

#endif				/* _HASH_H_ */
