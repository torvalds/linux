/* $OpenBSD: asn1_item.c,v 1.22 2025/05/10 05:54:38 tb Exp $ */
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
/* ====================================================================
 * Copyright (c) 1998-2003 The OpenSSL Project.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 *
 * 3. All advertising materials mentioning features or use of this
 *    software must display the following acknowledgment:
 *    "This product includes software developed by the OpenSSL Project
 *    for use in the OpenSSL Toolkit. (http://www.openssl.org/)"
 *
 * 4. The names "OpenSSL Toolkit" and "OpenSSL Project" must not be used to
 *    endorse or promote products derived from this software without
 *    prior written permission. For written permission, please contact
 *    openssl-core@openssl.org.
 *
 * 5. Products derived from this software may not be called "OpenSSL"
 *    nor may "OpenSSL" appear in their names without prior written
 *    permission of the OpenSSL Project.
 *
 * 6. Redistributions of any form whatsoever must retain the following
 *    acknowledgment:
 *    "This product includes software developed by the OpenSSL Project
 *    for use in the OpenSSL Toolkit (http://www.openssl.org/)"
 *
 * THIS SOFTWARE IS PROVIDED BY THE OpenSSL PROJECT ``AS IS'' AND ANY
 * EXPRESSED OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE OpenSSL PROJECT OR
 * ITS CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED
 * OF THE POSSIBILITY OF SUCH DAMAGE.
 * ====================================================================
 *
 * This product includes cryptographic software written by Eric Young
 * (eay@cryptsoft.com).  This product includes software written by Tim
 * Hudson (tjh@cryptsoft.com).
 *
 */

#include <limits.h>

#include <openssl/buffer.h>
#include <openssl/evp.h>
#include <openssl/x509.h>

#include "asn1_local.h"
#include "err_local.h"
#include "evp_local.h"
#include "x509_local.h"

int
ASN1_item_digest(const ASN1_ITEM *it, const EVP_MD *type, void *asn,
    unsigned char *md, unsigned int *len)
{
	int i;
	unsigned char *str = NULL;

	i = ASN1_item_i2d(asn, &str, it);
	if (!str)
		return (0);

	if (!EVP_Digest(str, i, md, len, type, NULL)) {
		free(str);
		return (0);
	}

	free(str);
	return (1);
}
LCRYPTO_ALIAS(ASN1_item_digest);

/*
 * ASN1_ITEM version of ASN1_dup(): follows the same model except there's no
 * need to allocate the buffer. At some point this could be rewritten to dup
 * the underlying structure directly instead of doing an encode and decode.
 */

void *
ASN1_item_dup(const ASN1_ITEM *it, void *x)
{
	unsigned char *b = NULL;
	const unsigned char *p;
	long i;
	void *ret;

	if (x == NULL)
		return (NULL);

	i = ASN1_item_i2d(x, &b, it);
	if (b == NULL) {
		ASN1error(ERR_R_MALLOC_FAILURE);
		return (NULL);
	}
	p = b;
	ret = ASN1_item_d2i(NULL, &p, i, it);
	free(b);
	return (ret);
}
LCRYPTO_ALIAS(ASN1_item_dup);

/* Pack an ASN1 object into an ASN1_STRING. */
ASN1_STRING *
ASN1_item_pack(void *obj, const ASN1_ITEM *it, ASN1_STRING **oct)
{
	ASN1_STRING *octmp;

	if (!oct || !*oct) {
		if (!(octmp = ASN1_STRING_new ())) {
			ASN1error(ERR_R_MALLOC_FAILURE);
			return NULL;
		}
	} else
		octmp = *oct;

	free(octmp->data);
	octmp->data = NULL;

	if (!(octmp->length = ASN1_item_i2d(obj, &octmp->data, it))) {
		ASN1error(ASN1_R_ENCODE_ERROR);
		goto err;
	}
	if (!octmp->data) {
		ASN1error(ERR_R_MALLOC_FAILURE);
		goto err;
	}
	if (oct)
		*oct = octmp;
	return octmp;
 err:
	if (!oct || octmp != *oct)
		ASN1_STRING_free(octmp);
	return NULL;
}
LCRYPTO_ALIAS(ASN1_item_pack);

/* Extract an ASN1 object from an ASN1_STRING. */
void *
ASN1_item_unpack(const ASN1_STRING *oct, const ASN1_ITEM *it)
{
	const unsigned char *p;
	void *ret;

	p = oct->data;
	if (!(ret = ASN1_item_d2i(NULL, &p, oct->length, it)))
		ASN1error(ASN1_R_DECODE_ERROR);
	return ret;
}
LCRYPTO_ALIAS(ASN1_item_unpack);

int
ASN1_item_sign(const ASN1_ITEM *it, X509_ALGOR *algor1, X509_ALGOR *algor2,
    ASN1_BIT_STRING *signature, void *asn, EVP_PKEY *pkey, const EVP_MD *type)
{
	EVP_MD_CTX *md_ctx = NULL;
	int ret = 0;

	if ((md_ctx = EVP_MD_CTX_new()) == NULL)
		goto err;
	if (!EVP_DigestSignInit(md_ctx, NULL, type, NULL, pkey))
		goto err;

	ret = ASN1_item_sign_ctx(it, algor1, algor2, signature, asn, md_ctx);

 err:
	EVP_MD_CTX_free(md_ctx);

	return ret;
}
LCRYPTO_ALIAS(ASN1_item_sign);

static int
asn1_item_set_algorithm_identifiers(EVP_MD_CTX *ctx, X509_ALGOR *algor1,
    X509_ALGOR *algor2)
{
	EVP_PKEY *pkey;
	const EVP_MD *md;
	int sign_id, sign_param;

	if ((pkey = EVP_PKEY_CTX_get0_pkey(ctx->pctx)) == NULL) {
		ASN1error(ASN1_R_CONTEXT_NOT_INITIALISED);
		return 0;
	}

	if ((md = EVP_MD_CTX_md(ctx)) == NULL) {
		ASN1error(ASN1_R_CONTEXT_NOT_INITIALISED);
		return 0;
	}

	if (!OBJ_find_sigid_by_algs(&sign_id, EVP_MD_nid(md),
	    pkey->ameth->pkey_id)) {
		ASN1error(ASN1_R_DIGEST_AND_KEY_TYPE_NOT_SUPPORTED);
		return 0;
	}

	sign_param = V_ASN1_UNDEF;
	if (pkey->ameth->pkey_flags & ASN1_PKEY_SIGPARAM_NULL)
		sign_param = V_ASN1_NULL;

	if (algor1 != NULL) {
		if (!X509_ALGOR_set0_by_nid(algor1, sign_id, sign_param, NULL))
			return 0;
	}
	if (algor2 != NULL) {
		if (!X509_ALGOR_set0_by_nid(algor2, sign_id, sign_param, NULL))
			return 0;
	}

	return 1;
}

static int
asn1_item_sign(EVP_MD_CTX *ctx, const ASN1_ITEM *it, void *asn,
    ASN1_BIT_STRING *signature)
{
	unsigned char *in = NULL, *out = NULL;
	size_t out_len = 0;
	int in_len = 0;
	int ret = 0;

	if ((in_len = ASN1_item_i2d(asn, &in, it)) <= 0) {
		in_len = 0;
		goto err;
	}

	if (!EVP_DigestSign(ctx, NULL, &out_len, in, in_len)) {
		ASN1error(ERR_R_EVP_LIB);
		goto err;
	}
	if ((out = calloc(1, out_len)) == NULL) {
		ASN1error(ERR_R_MALLOC_FAILURE);
		goto err;
	}
	if (!EVP_DigestSign(ctx, out, &out_len, in, in_len)) {
		ASN1error(ERR_R_EVP_LIB);
		goto err;
	}

	if (out_len > INT_MAX) {
		ASN1error(ASN1_R_TOO_LONG);
		goto err;
	}

	ASN1_STRING_set0(signature, out, out_len);
	out = NULL;

	if (!asn1_abs_set_unused_bits(signature, 0)) {
		ASN1_STRING_set0(signature, NULL, 0);
		ASN1error(ERR_R_ASN1_LIB);
		goto err;
	}

	ret = 1;

 err:
	freezero(in, in_len);
	freezero(out, out_len);

	return ret;
}

int
ASN1_item_sign_ctx(const ASN1_ITEM *it, X509_ALGOR *algor1, X509_ALGOR *algor2,
    ASN1_BIT_STRING *signature, void *asn, EVP_MD_CTX *ctx)
{
	EVP_PKEY *pkey;
	int rv;
	int ret = 0;

	if ((pkey = EVP_PKEY_CTX_get0_pkey(ctx->pctx)) == NULL) {
		ASN1error(ASN1_R_CONTEXT_NOT_INITIALISED);
		goto err;
	}
	if (pkey->ameth == NULL) {
		ASN1error(ASN1_R_DIGEST_AND_KEY_TYPE_NOT_SUPPORTED);
		goto err;
	}

	/*
	 * API insanity ahead. If the item_sign() method is absent or if it
	 * returns 2, this means: do all the work here. If it returns 3, only
	 * sign. If it returns 1, then there's nothing to do but to return
	 * the signature's length. Everything else is an error.
	 */

	rv = 2;
	if (pkey->ameth->item_sign != NULL)
		rv = pkey->ameth->item_sign(ctx, it, asn, algor1, algor2,
		    signature);
	if (rv <= 0 || rv > 3)
		goto err;
	if (rv == 1)
		goto done;
	if (rv == 2) {
		if (!asn1_item_set_algorithm_identifiers(ctx, algor1, algor2))
			goto err;
	}

	if (!asn1_item_sign(ctx, it, asn, signature))
		goto err;

 done:
	ret = signature->length;

 err:
	EVP_MD_CTX_cleanup(ctx);

	return ret;
}
LCRYPTO_ALIAS(ASN1_item_sign_ctx);

int
ASN1_item_verify(const ASN1_ITEM *it, X509_ALGOR *a,
    ASN1_BIT_STRING *signature, void *asn, EVP_PKEY *pkey)
{
	EVP_MD_CTX *md_ctx = NULL;
	unsigned char *in = NULL;
	int mdnid, pknid;
	int in_len = 0;
	int ret = -1;

	if (pkey == NULL) {
		ASN1error(ERR_R_PASSED_NULL_PARAMETER);
		goto err;
	}

	if (signature->type == V_ASN1_BIT_STRING && signature->flags & 0x7) {
		ASN1error(ASN1_R_INVALID_BIT_STRING_BITS_LEFT);
		goto err;
	}

	if ((md_ctx = EVP_MD_CTX_new()) == NULL)
		goto err;

	/* Convert signature OID into digest and public key OIDs */
	if (!OBJ_find_sigid_algs(OBJ_obj2nid(a->algorithm), &mdnid, &pknid)) {
		ASN1error(ASN1_R_UNKNOWN_SIGNATURE_ALGORITHM);
		goto err;
	}
	if (mdnid == NID_undef) {
		if (!pkey->ameth || !pkey->ameth->item_verify) {
			ASN1error(ASN1_R_UNKNOWN_SIGNATURE_ALGORITHM);
			goto err;
		}
		ret = pkey->ameth->item_verify(md_ctx, it, asn, a,
		    signature, pkey);
		/* Return value of 2 means carry on, anything else means we
		 * exit straight away: either a fatal error of the underlying
		 * verification routine handles all verification.
		 */
		if (ret != 2)
			goto err;
		ret = -1;
	} else {
		const EVP_MD *type;
		type = EVP_get_digestbynid(mdnid);
		if (type == NULL) {
			ASN1error(ASN1_R_UNKNOWN_MESSAGE_DIGEST_ALGORITHM);
			goto err;
		}

		/* Check public key OID matches public key type */
		if (EVP_PKEY_type(pknid) != pkey->ameth->pkey_id) {
			ASN1error(ASN1_R_WRONG_PUBLIC_KEY_TYPE);
			goto err;
		}

		if (!EVP_DigestVerifyInit(md_ctx, NULL, type, NULL, pkey)) {
			ASN1error(ERR_R_EVP_LIB);
			ret = 0;
			goto err;
		}

	}

	if ((in_len = ASN1_item_i2d(asn, &in, it)) <= 0) {
		ASN1error(ERR_R_MALLOC_FAILURE);
		in_len = 0;
		goto err;
	}

	if (EVP_DigestVerify(md_ctx, signature->data, signature->length,
	    in, in_len) <= 0) {
		ASN1error(ERR_R_EVP_LIB);
		ret = 0;
		goto err;
	}

	ret = 1;

 err:
	EVP_MD_CTX_free(md_ctx);
	freezero(in, in_len);

	return ret;
}
LCRYPTO_ALIAS(ASN1_item_verify);

#define HEADER_SIZE   8
#define ASN1_CHUNK_INITIAL_SIZE (16 * 1024)
int
asn1_d2i_read_bio(BIO *in, BUF_MEM **pb)
{
	BUF_MEM *b;
	unsigned char *p;
	const unsigned char *q;
	long slen;
	int i, inf, tag, xclass;
	size_t want = HEADER_SIZE;
	int eos = 0;
	size_t off = 0;
	size_t len = 0;

	b = BUF_MEM_new();
	if (b == NULL) {
		ASN1error(ERR_R_MALLOC_FAILURE);
		return -1;
	}

	ERR_clear_error();
	for (;;) {
		if (want >= (len - off)) {
			want -= (len - off);

			if (len + want < len ||
			    !BUF_MEM_grow_clean(b, len + want)) {
				ASN1error(ERR_R_MALLOC_FAILURE);
				goto err;
			}
			i = BIO_read(in, &(b->data[len]), want);
			if ((i < 0) && ((len - off) == 0)) {
				ASN1error(ASN1_R_NOT_ENOUGH_DATA);
				goto err;
			}
			if (i > 0) {
				if (len + i < len) {
					ASN1error(ASN1_R_TOO_LONG);
					goto err;
				}
				len += i;
			}
		}
		/* else data already loaded */

		p = (unsigned char *) & (b->data[off]);
		q = p;
		inf = ASN1_get_object(&q, &slen, &tag, &xclass, len - off);
		if (inf & 0x80) {
			unsigned long e;

			e = ERR_GET_REASON(ERR_peek_error());
			if (e != ASN1_R_TOO_LONG)
				goto err;
			else
				ERR_clear_error(); /* clear error */
		}
		i = q - p;	/* header length */
		off += i;	/* end of data */

		if (inf & 1) {
			/* no data body so go round again */
			eos++;
			if (eos < 0) {
				ASN1error(ASN1_R_HEADER_TOO_LONG);
				goto err;
			}
			want = HEADER_SIZE;
		} else if (eos && slen == 0 && tag == V_ASN1_EOC) {
			/* eos value, so go back and read another header */
			eos--;
			if (eos <= 0)
				break;
			else
				want = HEADER_SIZE;
		} else {
			/* suck in slen bytes of data */
			want = slen;
			if (want > (len - off)) {
				size_t chunk_max = ASN1_CHUNK_INITIAL_SIZE;

				want -= (len - off);
				if (want > INT_MAX /* BIO_read takes an int length */ ||
				    len+want < len) {
					ASN1error(ASN1_R_TOO_LONG);
					goto err;
				}
				while (want > 0) {
					/*
					 * Read content in chunks of increasing size
					 * so we can return an error for EOF without
					 * having to allocate the entire content length
					 * in one go.
					 */
					size_t chunk = want > chunk_max ? chunk_max : want;

					if (!BUF_MEM_grow_clean(b, len + chunk)) {
						ASN1error(ERR_R_MALLOC_FAILURE);
						goto err;
					}
					want -= chunk;
					while (chunk > 0) {
						i = BIO_read(in, &(b->data[len]), chunk);
						if (i <= 0) {
							ASN1error(ASN1_R_NOT_ENOUGH_DATA);
							goto err;
						}
						/*
						 * This can't overflow because |len+want|
						 * didn't overflow.
						 */
						len += i;
						chunk -= i;
					}
					if (chunk_max < INT_MAX/2)
						chunk_max *= 2;
				}
			}
			if (off + slen < off) {
				ASN1error(ASN1_R_TOO_LONG);
				goto err;
			}
			off += slen;
			if (eos <= 0) {
				break;
			} else
				want = HEADER_SIZE;
		}
	}

	if (off > INT_MAX) {
		ASN1error(ASN1_R_TOO_LONG);
		goto err;
	}

	*pb = b;
	return off;

 err:
	if (b != NULL)
		BUF_MEM_free(b);
	return -1;
}

void *
ASN1_item_d2i_bio(const ASN1_ITEM *it, BIO *in, void *x)
{
	BUF_MEM *b = NULL;
	const unsigned char *p;
	void *ret = NULL;
	int len;

	len = asn1_d2i_read_bio(in, &b);
	if (len < 0)
		goto err;

	p = (const unsigned char *)b->data;
	ret = ASN1_item_d2i(x, &p, len, it);

 err:
	if (b != NULL)
		BUF_MEM_free(b);
	return (ret);
}
LCRYPTO_ALIAS(ASN1_item_d2i_bio);

void *
ASN1_item_d2i_fp(const ASN1_ITEM *it, FILE *in, void *x)
{
	BIO *b;
	char *ret;

	if ((b = BIO_new(BIO_s_file())) == NULL) {
		ASN1error(ERR_R_BUF_LIB);
		return (NULL);
	}
	BIO_set_fp(b, in, BIO_NOCLOSE);
	ret = ASN1_item_d2i_bio(it, b, x);
	BIO_free(b);
	return (ret);
}
LCRYPTO_ALIAS(ASN1_item_d2i_fp);

int
ASN1_item_i2d_bio(const ASN1_ITEM *it, BIO *out, void *x)
{
	unsigned char *b = NULL;
	int i, j = 0, n, ret = 1;

	n = ASN1_item_i2d(x, &b, it);
	if (b == NULL) {
		ASN1error(ERR_R_MALLOC_FAILURE);
		return (0);
	}

	for (;;) {
		i = BIO_write(out, &(b[j]), n);
		if (i == n)
			break;
		if (i <= 0) {
			ret = 0;
			break;
		}
		j += i;
		n -= i;
	}
	free(b);
	return (ret);
}
LCRYPTO_ALIAS(ASN1_item_i2d_bio);

int
ASN1_item_i2d_fp(const ASN1_ITEM *it, FILE *out, void *x)
{
	BIO *b;
	int ret;

	if ((b = BIO_new(BIO_s_file())) == NULL) {
		ASN1error(ERR_R_BUF_LIB);
		return (0);
	}
	BIO_set_fp(b, out, BIO_NOCLOSE);
	ret = ASN1_item_i2d_bio(it, b, x);
	BIO_free(b);
	return (ret);
}
LCRYPTO_ALIAS(ASN1_item_i2d_fp);
