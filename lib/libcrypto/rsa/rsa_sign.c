/* $OpenBSD: rsa_sign.c,v 1.38 2025/05/10 05:54:38 tb Exp $ */
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
#include <string.h>

#include <openssl/bn.h>
#include <openssl/objects.h>
#include <openssl/rsa.h>
#include <openssl/x509.h>

#include "asn1_local.h"
#include "err_local.h"
#include "rsa_local.h"
#include "x509_local.h"

/* Size of an SSL signature: MD5+SHA1 */
#define SSL_SIG_LENGTH	36

static int encode_pkcs1(unsigned char **, int *, int , const unsigned char *,
    unsigned int);

/*
 * encode_pkcs1 encodes a DigestInfo prefix of hash `type' and digest `m', as
 * described in EMSA-PKCS-v1_5-ENCODE, RFC 8017 section 9. step 2. This
 * encodes the DigestInfo (T and tLen) but does not add the padding.
 *
 * On success, it returns one and sets `*out' to a newly allocated buffer
 * containing the result and `*out_len' to its length.  Freeing `*out' is
 * the caller's responsibility. Failure is indicated by zero.
 */
static int
encode_pkcs1(unsigned char **out, int *out_len, int type,
    const unsigned char *m, unsigned int m_len)
{
	X509_SIG sig;
	X509_ALGOR algor;
	ASN1_TYPE parameter;
	ASN1_OCTET_STRING digest;
	uint8_t *der = NULL;
	int len;

	sig.algor = &algor;
	if ((sig.algor->algorithm = OBJ_nid2obj(type)) == NULL) {
		RSAerror(RSA_R_UNKNOWN_ALGORITHM_TYPE);
		return 0;
	}
	if (sig.algor->algorithm->length == 0) {
		RSAerror(
		    RSA_R_THE_ASN1_OBJECT_IDENTIFIER_IS_NOT_KNOWN_FOR_THIS_MD);
		return 0;
	}
	parameter.type = V_ASN1_NULL;
	parameter.value.ptr = NULL;
	sig.algor->parameter = &parameter;

	sig.digest = &digest;
	sig.digest->data = (unsigned char *)m; /* TMP UGLY CAST */
	sig.digest->length = m_len;

	if ((len = i2d_X509_SIG(&sig, &der)) < 0)
		return 0;

	*out = der;
	*out_len = len;

	return 1;
}

int
RSA_sign(int type, const unsigned char *m, unsigned int m_len,
    unsigned char *sigret, unsigned int *siglen, RSA *rsa)
{
	const unsigned char *encoded = NULL;
	unsigned char *tmps = NULL;
	int encrypt_len, encoded_len = 0, ret = 0;

	if (rsa->meth->rsa_sign != NULL)
		return rsa->meth->rsa_sign(type, m, m_len, sigret, siglen, rsa);

	/* Compute the encoded digest. */
	if (type == NID_md5_sha1) {
		/*
		 * NID_md5_sha1 corresponds to the MD5/SHA1 combination in
		 * TLS 1.1 and earlier. It has no DigestInfo wrapper but
		 * otherwise is RSASSA-PKCS-v1.5.
		 */
		if (m_len != SSL_SIG_LENGTH) {
			RSAerror(RSA_R_INVALID_DIGEST_LENGTH);
			return 0;
		}
		encoded_len = SSL_SIG_LENGTH;
		encoded = m;
	} else {
		if (!encode_pkcs1(&tmps, &encoded_len, type, m, m_len))
			goto err;
		encoded = tmps;
	}
	if (encoded_len > RSA_size(rsa) - RSA_PKCS1_PADDING_SIZE) {
		RSAerror(RSA_R_DIGEST_TOO_BIG_FOR_RSA_KEY);
		goto err;
	}
	if ((encrypt_len = RSA_private_encrypt(encoded_len, encoded, sigret,
	    rsa, RSA_PKCS1_PADDING)) <= 0)
		goto err;

	*siglen = encrypt_len;
	ret = 1;

 err:
	freezero(tmps, (size_t)encoded_len);
	return (ret);
}
LCRYPTO_ALIAS(RSA_sign);

/*
 * int_rsa_verify verifies an RSA signature in `sigbuf' using `rsa'. It may be
 * called in two modes. If `rm' is NULL, it verifies the signature for the
 * digest `m'. Otherwise, it recovers the digest from the signature, writing the
 * digest to `rm' and the length to `*prm_len'. `type' is the NID of the digest
 * algorithm to use. It returns one on successful verification and zero
 * otherwise.
 */
int
int_rsa_verify(int type, const unsigned char *m, unsigned int m_len,
    unsigned char *rm, size_t *prm_len, const unsigned char *sigbuf,
    size_t siglen, RSA *rsa)
{
	unsigned char *decrypt_buf, *encoded = NULL;
	int decrypt_len, encoded_len = 0, ret = 0;

	if (siglen != (size_t)RSA_size(rsa)) {
		RSAerror(RSA_R_WRONG_SIGNATURE_LENGTH);
		return 0;
	}

	/* Recover the encoded digest. */
	if ((decrypt_buf = malloc(siglen)) == NULL) {
		RSAerror(ERR_R_MALLOC_FAILURE);
		goto err;
	}
	if ((decrypt_len = RSA_public_decrypt((int)siglen, sigbuf, decrypt_buf,
	    rsa, RSA_PKCS1_PADDING)) <= 0)
		goto err;

	if (type == NID_md5_sha1) {
		/*
		 * NID_md5_sha1 corresponds to the MD5/SHA1 combination in
		 * TLS 1.1 and earlier. It has no DigestInfo wrapper but
		 * otherwise is RSASSA-PKCS1-v1_5.
		 */
		if (decrypt_len != SSL_SIG_LENGTH) {
			RSAerror(RSA_R_INVALID_DIGEST_LENGTH);
			goto err;
		}

		if (rm != NULL) {
			memcpy(rm, decrypt_buf, SSL_SIG_LENGTH);
			*prm_len = SSL_SIG_LENGTH;
		} else {
			if (m_len != SSL_SIG_LENGTH) {
				RSAerror(RSA_R_INVALID_MESSAGE_LENGTH);
				goto err;
			}
			if (timingsafe_bcmp(decrypt_buf,
			    m, SSL_SIG_LENGTH) != 0) {
				RSAerror(RSA_R_BAD_SIGNATURE);
				goto err;
			}
		}
	} else {
		/*
		 * If recovering the digest, extract a digest-sized output from
		 * the end of `decrypt_buf' for `encode_pkcs1', then compare the
		 * decryption output as in a standard verification.
		 */
		if (rm != NULL) {
			const EVP_MD *md;

			if ((md = EVP_get_digestbynid(type)) == NULL) {
				RSAerror(RSA_R_UNKNOWN_ALGORITHM_TYPE);
				goto err;
			}
			if ((m_len = EVP_MD_size(md)) > (size_t)decrypt_len) {
				RSAerror(RSA_R_INVALID_DIGEST_LENGTH);
				goto err;
			}
			m = decrypt_buf + decrypt_len - m_len;
		}

		/* Construct the encoded digest and ensure it matches */
		if (!encode_pkcs1(&encoded, &encoded_len, type, m, m_len))
			goto err;

		if (encoded_len != decrypt_len ||
		    timingsafe_bcmp(encoded, decrypt_buf, encoded_len) != 0) {
			RSAerror(RSA_R_BAD_SIGNATURE);
			goto err;
		}

		/* Output the recovered digest. */
		if (rm != NULL) {
			memcpy(rm, m, m_len);
			*prm_len = m_len;
		}
	}

	ret = 1;
 err:
	freezero(encoded, (size_t)encoded_len);
	freezero(decrypt_buf, siglen);
	return ret;
}

int
RSA_verify(int dtype, const unsigned char *m, unsigned int m_len,
    const unsigned char *sigbuf, unsigned int siglen, RSA *rsa)
{
	if (rsa->meth->rsa_verify != NULL)
		return rsa->meth->rsa_verify(dtype, m, m_len, sigbuf, siglen,
		    rsa);

	return int_rsa_verify(dtype, m, m_len, NULL, NULL, sigbuf, siglen, rsa);
}
LCRYPTO_ALIAS(RSA_verify);
