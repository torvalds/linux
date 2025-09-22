/* $OpenBSD: ripemd.h,v 1.20 2025/01/25 17:59:44 tb Exp $ */
/* Copyright (C) 1995-1998 Eric Young (eay@cryptsoft.com)
 * All rights reserved.
 *
 * This package is an SSL implementation written
 * by Eric Young (eay@cryptsoft.com).
 * The implementation was written so as to conform with Netscapes SSL.
 *
 * This library is free for commercial and non-commercial use as long as
 * the following conditions are aheared to.  The following conditions
 * apply to all code found in this distribution, be it the RC4, RSA,
 * lhash, DES, etc., code; not just the SSL code.  The SSL documentation
 * included with this distribution is covered by the same copyright terms
 * except that the holder is Tim Hudson (tjh@cryptsoft.com).
 *
 * Copyright remains Eric Young's, and as such any Copyright notices in
 * the code are not to be removed.
 * If this package is used in a product, Eric Young should be given attribution
 * as the author of the parts of the library used.
 * This can be in the form of a textual message at program startup or
 * in documentation (online or textual) provided with the package.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *    "This product includes cryptographic software written by
 *     Eric Young (eay@cryptsoft.com)"
 *    The word 'cryptographic' can be left out if the rouines from the library
 *    being used are not cryptographic related :-).
 * 4. If you include any Windows specific code (or a derivative thereof) from
 *    the apps directory (application code) you must include an acknowledgement:
 *    "This product includes software written by Tim Hudson (tjh@cryptsoft.com)"
 *
 * THIS SOFTWARE IS PROVIDED BY ERIC YOUNG ``AS IS'' AND
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
 * The licence and distribution terms for any publically available version or
 * derivative of this code cannot be changed.  i.e. this code cannot simply be
 * copied and put under another distribution licence
 * [including the GNU Public Licence.]
 */

#include <stddef.h>

#ifndef HEADER_RIPEMD_H
#define HEADER_RIPEMD_H

#if !defined(HAVE_ATTRIBUTE__BOUNDED__) && !defined(__OpenBSD__)
#define __bounded__(x, y, z)
#endif

#include <openssl/opensslconf.h>

#ifdef  __cplusplus
extern "C" {
#endif

#if defined(__LP32__)
#define RIPEMD160_LONG unsigned long
#elif defined(__ILP64__)
#define RIPEMD160_LONG unsigned long
#define RIPEMD160_LONG_LOG2 3
#else
#define RIPEMD160_LONG unsigned int
#endif

#define RIPEMD160_CBLOCK	64
#define RIPEMD160_LBLOCK	(RIPEMD160_CBLOCK/4)
#define RIPEMD160_DIGEST_LENGTH	20

typedef struct RIPEMD160state_st {
	RIPEMD160_LONG A, B,C, D, E;
	RIPEMD160_LONG Nl, Nh;
	RIPEMD160_LONG data[RIPEMD160_LBLOCK];
	unsigned int   num;
} RIPEMD160_CTX;

int RIPEMD160_Init(RIPEMD160_CTX *c);
int RIPEMD160_Update(RIPEMD160_CTX *c, const void *data, size_t len)
    __attribute__ ((__bounded__(__buffer__, 2, 3)));
int RIPEMD160_Final(unsigned char *md, RIPEMD160_CTX *c);
unsigned char *RIPEMD160(const unsigned char *d, size_t n,
    unsigned char *md)
    __attribute__ ((__bounded__(__buffer__, 1, 2)))
    __attribute__ ((__nonnull__(3)));
void RIPEMD160_Transform(RIPEMD160_CTX *c, const unsigned char *b);
#ifdef  __cplusplus
}
#endif

#endif
