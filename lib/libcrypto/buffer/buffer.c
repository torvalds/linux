/* $OpenBSD: buffer.c,v 1.29 2025/05/10 05:54:38 tb Exp $ */
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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <openssl/buffer.h>

#include "err_local.h"

/*
 * LIMIT_BEFORE_EXPANSION is the maximum n such that (n + 3) / 3 * 4 < 2**31.
 * That function is applied in several functions in this file and this limit
 * ensures that the result fits in an int.
 */
#define LIMIT_BEFORE_EXPANSION 0x5ffffffc

BUF_MEM *
BUF_MEM_new(void)
{
	BUF_MEM *ret;

	if ((ret = calloc(1, sizeof(BUF_MEM))) == NULL) {
		BUFerror(ERR_R_MALLOC_FAILURE);
		return (NULL);
	}

	return (ret);
}
LCRYPTO_ALIAS(BUF_MEM_new);

void
BUF_MEM_free(BUF_MEM *a)
{
	if (a == NULL)
		return;

	freezero(a->data, a->max);
	free(a);
}
LCRYPTO_ALIAS(BUF_MEM_free);

int
BUF_MEM_grow(BUF_MEM *str, size_t len)
{
	return BUF_MEM_grow_clean(str, len);
}
LCRYPTO_ALIAS(BUF_MEM_grow);

int
BUF_MEM_grow_clean(BUF_MEM *str, size_t len)
{
	char *ret;
	size_t n;

	if (str->max >= len) {
		if (str->length >= len)
			memset(&str->data[len], 0, str->length - len);
		str->length = len;
		return (len);
	}

	if (len > LIMIT_BEFORE_EXPANSION) {
		BUFerror(ERR_R_MALLOC_FAILURE);
		return 0;
	}

	n = (len + 3) / 3 * 4;
	if ((ret = recallocarray(str->data, str->max, n, 1)) == NULL) {
		BUFerror(ERR_R_MALLOC_FAILURE);
		return (0);
	}
	str->data = ret;
	str->max = n;
	str->length = len;

	return (len);
}
LCRYPTO_ALIAS(BUF_MEM_grow_clean);

void
BUF_reverse(unsigned char *out, const unsigned char *in, size_t size)
{
	size_t i;

	if (in) {
		out += size - 1;
		for (i = 0; i < size; i++)
			*out-- = *in++;
	} else {
		unsigned char *q;
		char c;
		q = out + size - 1;
		for (i = 0; i < size / 2; i++) {
			c = *q;
			*q-- = *out;
			*out++ = c;
		}
	}
}
