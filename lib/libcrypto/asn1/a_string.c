/* $OpenBSD: a_string.c,v 1.18 2025/05/10 05:54:38 tb Exp $ */
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

#include <limits.h>
#include <stdlib.h>
#include <string.h>

#include <openssl/asn1.h>

#include "asn1_local.h"
#include "err_local.h"

ASN1_STRING *
ASN1_STRING_new(void)
{
	return ASN1_STRING_type_new(V_ASN1_OCTET_STRING);
}
LCRYPTO_ALIAS(ASN1_STRING_new);

ASN1_STRING *
ASN1_STRING_type_new(int type)
{
	ASN1_STRING *astr;

	if ((astr = calloc(1, sizeof(ASN1_STRING))) == NULL) {
		ASN1error(ERR_R_MALLOC_FAILURE);
		return NULL;
	}
	astr->type = type;

	return astr;
}
LCRYPTO_ALIAS(ASN1_STRING_type_new);

static void
ASN1_STRING_clear(ASN1_STRING *astr)
{
	if (!(astr->flags & ASN1_STRING_FLAG_NDEF))
		freezero(astr->data, astr->length);

	astr->flags &= ~ASN1_STRING_FLAG_NDEF;
	astr->data = NULL;
	astr->length = 0;
}

void
ASN1_STRING_free(ASN1_STRING *astr)
{
	if (astr == NULL)
		return;

	ASN1_STRING_clear(astr);

	free(astr);
}
LCRYPTO_ALIAS(ASN1_STRING_free);

int
ASN1_STRING_cmp(const ASN1_STRING *a, const ASN1_STRING *b)
{
	int cmp;

	if (a == NULL || b == NULL)
		return -1;
	if ((cmp = (a->length - b->length)) != 0)
		return cmp;
	if (a->length != 0) {
		if ((cmp = memcmp(a->data, b->data, a->length)) != 0)
			return cmp;
	}

	return a->type - b->type;
}
LCRYPTO_ALIAS(ASN1_STRING_cmp);

int
ASN1_STRING_copy(ASN1_STRING *dst, const ASN1_STRING *src)
{
	if (src == NULL)
		return 0;

	if (!ASN1_STRING_set(dst, src->data, src->length))
		return 0;

	dst->type = src->type;
	dst->flags = src->flags & ~ASN1_STRING_FLAG_NDEF;

	return 1;
}
LCRYPTO_ALIAS(ASN1_STRING_copy);

ASN1_STRING *
ASN1_STRING_dup(const ASN1_STRING *src)
{
	ASN1_STRING *astr;

	if (src == NULL)
		return NULL;

	if ((astr = ASN1_STRING_new()) == NULL)
		return NULL;
	if (!ASN1_STRING_copy(astr, src)) {
		ASN1_STRING_free(astr);
		return NULL;
	}
	return astr;
}
LCRYPTO_ALIAS(ASN1_STRING_dup);

int
ASN1_STRING_set(ASN1_STRING *astr, const void *_data, int len)
{
	const char *data = _data;

	if (len == -1) {
		size_t slen;

		if (data == NULL)
			return 0;

		if ((slen = strlen(data)) > INT_MAX)
			return 0;

		len = (int)slen;
	}

	ASN1_STRING_clear(astr);

	if (len < 0 || len >= INT_MAX)
		return 0;

	if ((astr->data = calloc(1, len + 1)) == NULL) {
		ASN1error(ERR_R_MALLOC_FAILURE);
		return 0;
	}
	astr->length = len;

	if (data != NULL) {
		memcpy(astr->data, data, len);
		astr->data[len] = '\0';
	}

	return 1;
}
LCRYPTO_ALIAS(ASN1_STRING_set);

void
ASN1_STRING_set0(ASN1_STRING *astr, void *data, int len)
{
	ASN1_STRING_clear(astr);

	astr->data = data;
	astr->length = len;
}
LCRYPTO_ALIAS(ASN1_STRING_set0);

int
ASN1_STRING_length(const ASN1_STRING *astr)
{
	return astr->length;
}
LCRYPTO_ALIAS(ASN1_STRING_length);

void
ASN1_STRING_length_set(ASN1_STRING *astr, int len)
{
	/* This is dangerous and unfixable. */
	astr->length = len;
}
LCRYPTO_ALIAS(ASN1_STRING_length_set);

int
ASN1_STRING_type(const ASN1_STRING *astr)
{
	return astr->type;
}
LCRYPTO_ALIAS(ASN1_STRING_type);

unsigned char *
ASN1_STRING_data(ASN1_STRING *astr)
{
	return astr->data;
}
LCRYPTO_ALIAS(ASN1_STRING_data);

const unsigned char *
ASN1_STRING_get0_data(const ASN1_STRING *astr)
{
	return astr->data;
}
LCRYPTO_ALIAS(ASN1_STRING_get0_data);

int
ASN1_STRING_print(BIO *bp, const ASN1_STRING *astr)
{
	int i, n;
	char buf[80];
	const char *p;

	if (astr == NULL)
		return 0;

	n = 0;
	p = (const char *)astr->data;
	for (i = 0; i < astr->length; i++) {
		if ((p[i] > '~') || ((p[i] < ' ') &&
		    (p[i] != '\n') && (p[i] != '\r')))
			buf[n] = '.';
		else
			buf[n] = p[i];
		n++;
		if (n >= 80) {
			if (BIO_write(bp, buf, n) <= 0)
				return 0;
			n = 0;
		}
	}
	if (n > 0) {
		if (BIO_write(bp, buf, n) <= 0)
			return 0;
	}

	return 1;
}
LCRYPTO_ALIAS(ASN1_STRING_print);

/*
 * Utility function: convert any string type to UTF8, returns number of bytes
 * in output string or a negative error code
 */
int
ASN1_STRING_to_UTF8(unsigned char **out, const ASN1_STRING *in)
{
	ASN1_STRING *astr = NULL;
	int mbflag;
	int ret = -1;

	/*
	 * XXX We can't fail on *out != NULL here since things like haproxy and
	 * grpc pass in a pointer to an uninitialized pointer on the stack.
	 */
	if (out == NULL)
		goto err;

	if (in == NULL)
		goto err;

	if ((mbflag = asn1_tag2charwidth(in->type)) == -1)
		goto err;

	mbflag |= MBSTRING_FLAG;

	if ((ret = ASN1_mbstring_copy(&astr, in->data, in->length, mbflag,
	    B_ASN1_UTF8STRING)) < 0)
		goto err;

	*out = astr->data;
	ret = astr->length;

	astr->data = NULL;
	astr->length = 0;

 err:
	ASN1_STRING_free(astr);

	return ret;
}
LCRYPTO_ALIAS(ASN1_STRING_to_UTF8);

int
i2a_ASN1_STRING(BIO *bp, const ASN1_STRING *astr, int type)
{
	int i, n = 0;
	static const char h[] = "0123456789ABCDEF";
	char buf[2];

	if (astr == NULL)
		return 0;

	if (astr->length == 0) {
		if (BIO_write(bp, "0", 1) != 1)
			goto err;
		n = 1;
	} else {
		for (i = 0; i < astr->length; i++) {
			if ((i != 0) && (i % 35 == 0)) {
				if (BIO_write(bp, "\\\n", 2) != 2)
					goto err;
				n += 2;
			}
			buf[0] = h[((unsigned char)astr->data[i] >> 4) & 0x0f];
			buf[1] = h[((unsigned char)astr->data[i]) & 0x0f];
			if (BIO_write(bp, buf, 2) != 2)
				goto err;
			n += 2;
		}
	}
	return n;

 err:
	return -1;
}
LCRYPTO_ALIAS(i2a_ASN1_STRING);

int
a2i_ASN1_STRING(BIO *bp, ASN1_STRING *astr, char *buf, int size)
{
	int ret = 0;
	int i, j, k, m, n, again, bufsize;
	unsigned char *s = NULL, *sp;
	unsigned char *bufp;
	int first = 1;
	size_t num = 0, slen = 0;

	bufsize = BIO_gets(bp, buf, size);
	for (;;) {
		if (bufsize < 1) {
			if (first)
				break;
			else
				goto err_sl;
		}
		first = 0;

		i = bufsize;
		if (buf[i-1] == '\n')
			buf[--i] = '\0';
		if (i == 0)
			goto err_sl;
		if (buf[i-1] == '\r')
			buf[--i] = '\0';
		if (i == 0)
			goto err_sl;
		if (buf[i - 1] == '\\') {
			i--;
			again = 1;
		} else
			again = 0;
		buf[i] = '\0';
		if (i < 2)
			goto err_sl;

		bufp = (unsigned char *)buf;

		k = 0;
		if (i % 2 != 0) {
			ASN1error(ASN1_R_ODD_NUMBER_OF_CHARS);
			goto err;
		}
		i /= 2;
		if (num + i > slen) {
			sp = realloc(s, num + i);
			if (sp == NULL) {
				ASN1error(ERR_R_MALLOC_FAILURE);
				goto err;
			}
			s = sp;
			slen = num + i;
		}
		for (j = 0; j < i; j++, k += 2) {
			for (n = 0; n < 2; n++) {
				m = bufp[k + n];
				if ((m >= '0') && (m <= '9'))
					m -= '0';
				else if ((m >= 'a') && (m <= 'f'))
					m = m - 'a' + 10;
				else if ((m >= 'A') && (m <= 'F'))
					m = m - 'A' + 10;
				else {
					ASN1error(ASN1_R_NON_HEX_CHARACTERS);
					goto err;
				}
				s[num + j] <<= 4;
				s[num + j] |= m;
			}
		}
		num += i;
		if (again)
			bufsize = BIO_gets(bp, buf, size);
		else
			break;
	}
	astr->length = num;
	astr->data = s;

	return 1;

 err_sl:
	ASN1error(ASN1_R_SHORT_LINE);
 err:
	free(s);

	return ret;
}
LCRYPTO_ALIAS(a2i_ASN1_STRING);
