/* $OpenBSD: pvkfmt.c,v 1.30 2025/06/07 09:32:35 tb Exp $ */
/* Written by Dr Stephen N Henson (steve@openssl.org) for the OpenSSL
 * project 2005.
 */
/* ====================================================================
 * Copyright (c) 2005 The OpenSSL Project.  All rights reserved.
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
 *    for use in the OpenSSL Toolkit. (http://www.OpenSSL.org/)"
 *
 * 4. The names "OpenSSL Toolkit" and "OpenSSL Project" must not be used to
 *    endorse or promote products derived from this software without
 *    prior written permission. For written permission, please contact
 *    licensing@OpenSSL.org.
 *
 * 5. Products derived from this software may not be called "OpenSSL"
 *    nor may "OpenSSL" appear in their names without prior written
 *    permission of the OpenSSL Project.
 *
 * 6. Redistributions of any form whatsoever must retain the following
 *    acknowledgment:
 *    "This product includes software developed by the OpenSSL Project
 *    for use in the OpenSSL Toolkit (http://www.OpenSSL.org/)"
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

/* Support for PVK format keys and related structures (such a PUBLICKEYBLOB
 * and PRIVATEKEYBLOB).
 */

#include <stdlib.h>
#include <string.h>

#include <openssl/opensslconf.h>

#include <openssl/bn.h>
#include <openssl/pem.h>

#if !defined(OPENSSL_NO_RSA) && !defined(OPENSSL_NO_DSA)
#include <openssl/dsa.h>
#include <openssl/rsa.h>

#include "bn_local.h"
#include "dsa_local.h"
#include "err_local.h"
#include "evp_local.h"
#include "rsa_local.h"

/* Utility function: read a DWORD (4 byte unsigned integer) in little endian
 * format
 */

static unsigned int
read_ledword(const unsigned char **in)
{
	const unsigned char *p = *in;
	unsigned int ret;

	ret = *p++;
	ret |= (*p++ << 8);
	ret |= (*p++ << 16);
	ret |= (*p++ << 24);
	*in = p;
	return ret;
}

/* Read a BIGNUM in little endian format. The docs say that this should take up
 * bitlen/8 bytes.
 */

static int
read_lebn(const unsigned char **in, unsigned int nbyte, BIGNUM **r)
{
	const unsigned char *p;
	unsigned char *tmpbuf, *q;
	unsigned int i;

	p = *in + nbyte - 1;
	tmpbuf = malloc(nbyte);
	if (!tmpbuf)
		return 0;
	q = tmpbuf;
	for (i = 0; i < nbyte; i++)
		*q++ = *p--;
	*r = BN_bin2bn(tmpbuf, nbyte, NULL);
	free(tmpbuf);
	if (*r) {
		*in += nbyte;
		return 1;
	} else
		return 0;
}


/* Convert private key blob to EVP_PKEY: RSA and DSA keys supported */

#define MS_PUBLICKEYBLOB	0x6
#define MS_PRIVATEKEYBLOB	0x7
#define MS_RSA1MAGIC		0x31415352L
#define MS_RSA2MAGIC		0x32415352L
#define MS_DSS1MAGIC		0x31535344L
#define MS_DSS2MAGIC		0x32535344L

#define MS_KEYALG_RSA_KEYX	0xa400
#define MS_KEYALG_DSS_SIGN	0x2200

#define MS_KEYTYPE_KEYX		0x1
#define MS_KEYTYPE_SIGN		0x2

/* The PVK file magic number: seems to spell out "bobsfile", who is Bob? */
#define MS_PVKMAGIC		0xb0b5f11eL
/* Salt length for PVK files */
#define PVK_SALTLEN		0x10

static EVP_PKEY *b2i_rsa(const unsigned char **in, unsigned int length,
    unsigned int bitlen, int ispub);
static EVP_PKEY *b2i_dss(const unsigned char **in, unsigned int length,
    unsigned int bitlen, int ispub);

static int
do_blob_header(const unsigned char **in, unsigned int length,
    unsigned int *pmagic, unsigned int *pbitlen, int *pisdss, int *pispub)
{
	const unsigned char *p = *in;

	if (length < 16)
		return 0;
	/* bType */
	if (*p == MS_PUBLICKEYBLOB) {
		if (*pispub == 0) {
			PEMerror(PEM_R_EXPECTING_PRIVATE_KEY_BLOB);
			return 0;
		}
		*pispub = 1;
	} else if (*p == MS_PRIVATEKEYBLOB) {
		if (*pispub == 1) {
			PEMerror(PEM_R_EXPECTING_PUBLIC_KEY_BLOB);
			return 0;
		}
		*pispub = 0;
	} else
		return 0;
	p++;
	/* Version */
	if (*p++ != 0x2) {
		PEMerror(PEM_R_BAD_VERSION_NUMBER);
		return 0;
	}
	/* Ignore reserved, aiKeyAlg */
	p += 6;
	*pmagic = read_ledword(&p);
	*pbitlen = read_ledword(&p);
	if (*pbitlen > 65536) {
		PEMerror(PEM_R_INCONSISTENT_HEADER);
		return 0;
	}
	*pisdss = 0;
	switch (*pmagic) {

	case MS_DSS1MAGIC:
		*pisdss = 1;
	case MS_RSA1MAGIC:
		if (*pispub == 0) {
			PEMerror(PEM_R_EXPECTING_PRIVATE_KEY_BLOB);
			return 0;
		}
		break;

	case MS_DSS2MAGIC:
		*pisdss = 1;
	case MS_RSA2MAGIC:
		if (*pispub == 1) {
			PEMerror(PEM_R_EXPECTING_PUBLIC_KEY_BLOB);
			return 0;
		}
		break;

	default:
		PEMerror(PEM_R_BAD_MAGIC_NUMBER);
		return -1;
	}
	*in = p;
	return 1;
}

static unsigned int
blob_length(unsigned bitlen, int isdss, int ispub)
{
	unsigned int nbyte, hnbyte;

	nbyte = (bitlen + 7) >> 3;
	hnbyte = (bitlen + 15) >> 4;
	if (isdss) {

		/* Expected length: 20 for q + 3 components bitlen each + 24
		 * for seed structure.
		 */
		if (ispub)
			return 44 + 3 * nbyte;
		/* Expected length: 20 for q, priv, 2 bitlen components + 24
		 * for seed structure.
		 */
		else
			return 64 + 2 * nbyte;
	} else {
		/* Expected length: 4 for 'e' + 'n' */
		if (ispub)
			return 4 + nbyte;
		else
		/* Expected length: 4 for 'e' and 7 other components.
		 * 2 components are bitlen size, 5 are bitlen/2
		 */
				return 4 + 2*nbyte + 5*hnbyte;
	}

}

static EVP_PKEY *
do_b2i(const unsigned char **in, unsigned int length, int ispub)
{
	const unsigned char *p = *in;
	unsigned int bitlen, magic;
	int isdss;

	if (do_blob_header(&p, length, &magic, &bitlen, &isdss, &ispub) <= 0) {
		PEMerror(PEM_R_KEYBLOB_HEADER_PARSE_ERROR);
		return NULL;
	}
	length -= 16;
	if (length < blob_length(bitlen, isdss, ispub)) {
		PEMerror(PEM_R_KEYBLOB_TOO_SHORT);
		return NULL;
	}
	if (isdss)
		return b2i_dss(&p, length, bitlen, ispub);
	else
		return b2i_rsa(&p, length, bitlen, ispub);
}

static EVP_PKEY *
do_b2i_bio(BIO *in, int ispub)
{
	const unsigned char *p;
	unsigned char hdr_buf[16], *buf = NULL;
	unsigned int bitlen, magic, length;
	int isdss;
	EVP_PKEY *ret = NULL;

	if (BIO_read(in, hdr_buf, 16) != 16) {
		PEMerror(PEM_R_KEYBLOB_TOO_SHORT);
		return NULL;
	}
	p = hdr_buf;
	if (do_blob_header(&p, 16, &magic, &bitlen, &isdss, &ispub) <= 0)
		return NULL;

	length = blob_length(bitlen, isdss, ispub);
	buf = malloc(length);
	if (!buf) {
		PEMerror(ERR_R_MALLOC_FAILURE);
		goto err;
	}
	p = buf;
	if (BIO_read(in, buf, length) != (int)length) {
		PEMerror(PEM_R_KEYBLOB_TOO_SHORT);
		goto err;
	}

	if (isdss)
		ret = b2i_dss(&p, length, bitlen, ispub);
	else
		ret = b2i_rsa(&p, length, bitlen, ispub);

 err:
	free(buf);
	return ret;
}

static EVP_PKEY *
b2i_dss(const unsigned char **in, unsigned int length, unsigned int bitlen,
    int ispub)
{
	const unsigned char *p = *in;
	EVP_PKEY *ret = NULL;
	DSA *dsa = NULL;
	BN_CTX *ctx = NULL;
	unsigned int nbyte;

	nbyte = (bitlen + 7) >> 3;

	dsa = DSA_new();
	ret = EVP_PKEY_new();
	if (!dsa || !ret)
		goto err;
	if (!read_lebn(&p, nbyte, &dsa->p))
		goto err;
	if (!read_lebn(&p, 20, &dsa->q))
		goto err;
	if (!read_lebn(&p, nbyte, &dsa->g))
		goto err;
	if (ispub) {
		if (!read_lebn(&p, nbyte, &dsa->pub_key))
			goto err;
	} else {
		if (!read_lebn(&p, 20, &dsa->priv_key))
			goto err;
		/* Calculate public key */
		if (!(dsa->pub_key = BN_new()))
			goto err;
		if (!(ctx = BN_CTX_new()))
			goto err;
		if (!BN_mod_exp_ct(dsa->pub_key, dsa->g,
		    dsa->priv_key, dsa->p, ctx))
			goto err;
		BN_CTX_free(ctx);
	}

	EVP_PKEY_set1_DSA(ret, dsa);
	DSA_free(dsa);
	*in = p;
	return ret;

 err:
	PEMerror(ERR_R_MALLOC_FAILURE);
	DSA_free(dsa);
	EVP_PKEY_free(ret);
	BN_CTX_free(ctx);
	return NULL;
}

static EVP_PKEY *
b2i_rsa(const unsigned char **in, unsigned int length, unsigned int bitlen,
    int ispub)
{
	const unsigned char *p = *in;
	EVP_PKEY *ret = NULL;
	RSA *rsa = NULL;
	unsigned int nbyte, hnbyte;

	nbyte = (bitlen + 7) >> 3;
	hnbyte = (bitlen + 15) >> 4;
	rsa = RSA_new();
	ret = EVP_PKEY_new();
	if (!rsa || !ret)
		goto err;
	rsa->e = BN_new();
	if (!rsa->e)
		goto err;
	if (!BN_set_word(rsa->e, read_ledword(&p)))
		goto err;
	if (!read_lebn(&p, nbyte, &rsa->n))
		goto err;
	if (!ispub) {
		if (!read_lebn(&p, hnbyte, &rsa->p))
			goto err;
		if (!read_lebn(&p, hnbyte, &rsa->q))
			goto err;
		if (!read_lebn(&p, hnbyte, &rsa->dmp1))
			goto err;
		if (!read_lebn(&p, hnbyte, &rsa->dmq1))
			goto err;
		if (!read_lebn(&p, hnbyte, &rsa->iqmp))
			goto err;
		if (!read_lebn(&p, nbyte, &rsa->d))
			goto err;
	}

	EVP_PKEY_set1_RSA(ret, rsa);
	RSA_free(rsa);
	*in = p;
	return ret;

 err:
	PEMerror(ERR_R_MALLOC_FAILURE);
	RSA_free(rsa);
	EVP_PKEY_free(ret);
	return NULL;
}

EVP_PKEY *
b2i_PrivateKey(const unsigned char **in, long length)
{
	return do_b2i(in, length, 0);
}
LCRYPTO_ALIAS(b2i_PrivateKey);

EVP_PKEY *
b2i_PublicKey(const unsigned char **in, long length)
{
	return do_b2i(in, length, 1);
}
LCRYPTO_ALIAS(b2i_PublicKey);

EVP_PKEY *
b2i_PrivateKey_bio(BIO *in)
{
	return do_b2i_bio(in, 0);
}
LCRYPTO_ALIAS(b2i_PrivateKey_bio);

EVP_PKEY *
b2i_PublicKey_bio(BIO *in)
{
	return do_b2i_bio(in, 1);
}
LCRYPTO_ALIAS(b2i_PublicKey_bio);

static void
write_ledword(unsigned char **out, unsigned int dw)
{
	unsigned char *p = *out;

	*p++ = dw & 0xff;
	*p++ = (dw >> 8) & 0xff;
	*p++ = (dw >> 16) & 0xff;
	*p++ = (dw >> 24) & 0xff;
	*out = p;
}

static void
write_lebn(unsigned char **out, const BIGNUM *bn, int len)
{
	int nb, i;
	unsigned char *p = *out, *q, c;

	nb = BN_num_bytes(bn);
	BN_bn2bin(bn, p);
	q = p + nb - 1;
	/* In place byte order reversal */
	for (i = 0; i < nb / 2; i++) {
		c = *p;
		*p++ = *q;
		*q-- = c;
	}
	*out += nb;
	/* Pad with zeroes if we have to */
	if (len > 0) {
		len -= nb;
		if (len > 0) {
			memset(*out, 0, len);
			*out += len;
		}
	}
}


static int check_bitlen_rsa(RSA *rsa, int ispub, unsigned int *magic);
static int check_bitlen_dsa(DSA *dsa, int ispub, unsigned int *magic);

static void write_rsa(unsigned char **out, RSA *rsa, int ispub);
static void write_dsa(unsigned char **out, DSA *dsa, int ispub);

static int
do_i2b(unsigned char **out, EVP_PKEY *pk, int ispub)
{
	unsigned char *p;
	unsigned int bitlen, magic = 0, keyalg;
	int outlen, noinc = 0;

	if (pk->type == EVP_PKEY_DSA) {
		bitlen = check_bitlen_dsa(pk->pkey.dsa, ispub, &magic);
		keyalg = MS_KEYALG_DSS_SIGN;
	} else if (pk->type == EVP_PKEY_RSA) {
		bitlen = check_bitlen_rsa(pk->pkey.rsa, ispub, &magic);
		keyalg = MS_KEYALG_RSA_KEYX;
	} else
		return -1;
	if (bitlen == 0)
		return -1;
	outlen = 16 + blob_length(bitlen,
	    keyalg == MS_KEYALG_DSS_SIGN ? 1 : 0, ispub);
	if (out == NULL)
		return outlen;
	if (*out)
		p = *out;
	else {
		p = malloc(outlen);
		if (!p)
			return -1;
		*out = p;
		noinc = 1;
	}
	if (ispub)
		*p++ = MS_PUBLICKEYBLOB;
	else
		*p++ = MS_PRIVATEKEYBLOB;
	*p++ = 0x2;
	*p++ = 0;
	*p++ = 0;
	write_ledword(&p, keyalg);
	write_ledword(&p, magic);
	write_ledword(&p, bitlen);
	if (keyalg == MS_KEYALG_DSS_SIGN)
		write_dsa(&p, pk->pkey.dsa, ispub);
	else
		write_rsa(&p, pk->pkey.rsa, ispub);
	if (!noinc)
		*out += outlen;
	return outlen;
}

static int
do_i2b_bio(BIO *out, EVP_PKEY *pk, int ispub)
{
	unsigned char *tmp = NULL;
	int outlen, wrlen;

	outlen = do_i2b(&tmp, pk, ispub);
	if (outlen < 0)
		return -1;
	wrlen = BIO_write(out, tmp, outlen);
	free(tmp);
	if (wrlen == outlen)
		return outlen;
	return -1;
}

static int
check_bitlen_dsa(DSA *dsa, int ispub, unsigned int *pmagic)
{
	int bitlen;

	bitlen = BN_num_bits(dsa->p);
	if ((bitlen & 7) || (BN_num_bits(dsa->q) != 160) ||
	    (BN_num_bits(dsa->g) > bitlen))
		goto err;
	if (ispub) {
		if (BN_num_bits(dsa->pub_key) > bitlen)
			goto err;
		*pmagic = MS_DSS1MAGIC;
	} else {
		if (BN_num_bits(dsa->priv_key) > 160)
			goto err;
		*pmagic = MS_DSS2MAGIC;
	}

	return bitlen;

 err:
	PEMerror(PEM_R_UNSUPPORTED_KEY_COMPONENTS);
	return 0;
}

static int
check_bitlen_rsa(RSA *rsa, int ispub, unsigned int *pmagic)
{
	int nbyte, hnbyte, bitlen;

	if (BN_num_bits(rsa->e) > 32)
		goto err;
	bitlen = BN_num_bits(rsa->n);
	nbyte = BN_num_bytes(rsa->n);
	hnbyte = (BN_num_bits(rsa->n) + 15) >> 4;
	if (ispub) {
		*pmagic = MS_RSA1MAGIC;
		return bitlen;
	} else {
		*pmagic = MS_RSA2MAGIC;
		/* For private key each component must fit within nbyte or
		 * hnbyte.
		 */
		if (BN_num_bytes(rsa->d) > nbyte)
			goto err;
		if ((BN_num_bytes(rsa->iqmp) > hnbyte) ||
		    (BN_num_bytes(rsa->p) > hnbyte) ||
		    (BN_num_bytes(rsa->q) > hnbyte) ||
		    (BN_num_bytes(rsa->dmp1) > hnbyte) ||
		    (BN_num_bytes(rsa->dmq1) > hnbyte))
			goto err;
	}
	return bitlen;

 err:
	PEMerror(PEM_R_UNSUPPORTED_KEY_COMPONENTS);
	return 0;
}

static void
write_rsa(unsigned char **out, RSA *rsa, int ispub)
{
	int nbyte, hnbyte;

	nbyte = BN_num_bytes(rsa->n);
	hnbyte = (BN_num_bits(rsa->n) + 15) >> 4;
	write_lebn(out, rsa->e, 4);
	write_lebn(out, rsa->n, -1);
	if (ispub)
		return;
	write_lebn(out, rsa->p, hnbyte);
	write_lebn(out, rsa->q, hnbyte);
	write_lebn(out, rsa->dmp1, hnbyte);
	write_lebn(out, rsa->dmq1, hnbyte);
	write_lebn(out, rsa->iqmp, hnbyte);
	write_lebn(out, rsa->d, nbyte);
}

static void
write_dsa(unsigned char **out, DSA *dsa, int ispub)
{
	int nbyte;

	nbyte = BN_num_bytes(dsa->p);
	write_lebn(out, dsa->p, nbyte);
	write_lebn(out, dsa->q, 20);
	write_lebn(out, dsa->g, nbyte);
	if (ispub)
		write_lebn(out, dsa->pub_key, nbyte);
	else
		write_lebn(out, dsa->priv_key, 20);
	/* Set "invalid" for seed structure values */
	memset(*out, 0xff, 24);
	*out += 24;
	return;
}

int
i2b_PrivateKey_bio(BIO *out, EVP_PKEY *pk)
{
	return do_i2b_bio(out, pk, 0);
}
LCRYPTO_ALIAS(i2b_PrivateKey_bio);

int
i2b_PublicKey_bio(BIO *out, EVP_PKEY *pk)
{
	return do_i2b_bio(out, pk, 1);
}
LCRYPTO_ALIAS(i2b_PublicKey_bio);

#ifndef OPENSSL_NO_RC4

static int
do_PVK_header(const unsigned char **in, unsigned int length, int skip_magic,
    unsigned int *psaltlen, unsigned int *pkeylen)
{
	const unsigned char *p = *in;
	unsigned int pvk_magic, is_encrypted;

	if (skip_magic) {
		if (length < 20) {
			PEMerror(PEM_R_PVK_TOO_SHORT);
			return 0;
		}
		length -= 20;
	} else {
		if (length < 24) {
			PEMerror(PEM_R_PVK_TOO_SHORT);
			return 0;
		}
		length -= 24;
		pvk_magic = read_ledword(&p);
		if (pvk_magic != MS_PVKMAGIC) {
			PEMerror(PEM_R_BAD_MAGIC_NUMBER);
			return 0;
		}
	}
	/* Skip reserved */
	p += 4;
	/*keytype = */read_ledword(&p);
	is_encrypted = read_ledword(&p);
	*psaltlen = read_ledword(&p);
	*pkeylen = read_ledword(&p);
	if (*psaltlen > 65536 || *pkeylen > 65536) {
		PEMerror(PEM_R_ERROR_CONVERTING_PRIVATE_KEY);
		return 0;
	}

	if (is_encrypted && !*psaltlen) {
		PEMerror(PEM_R_INCONSISTENT_HEADER);
		return 0;
	}

	*in = p;
	return 1;
}

static int
derive_pvk_key(unsigned char *key, const unsigned char *salt,
    unsigned int saltlen, const unsigned char *pass, int passlen)
{
	EVP_MD_CTX mctx;
	int rv = 1;

	EVP_MD_CTX_legacy_clear(&mctx);
	if (!EVP_DigestInit_ex(&mctx, EVP_sha1(), NULL) ||
	    !EVP_DigestUpdate(&mctx, salt, saltlen) ||
	    !EVP_DigestUpdate(&mctx, pass, passlen) ||
	    !EVP_DigestFinal_ex(&mctx, key, NULL))
		rv = 0;

	EVP_MD_CTX_cleanup(&mctx);
	return rv;
}

static EVP_PKEY *
do_PVK_body(const unsigned char **in, unsigned int saltlen,
    unsigned int keylen, pem_password_cb *cb, void *u)
{
	EVP_PKEY *ret = NULL;
	const unsigned char *p = *in;
	unsigned int magic;
	unsigned char *enctmp = NULL, *q;
	EVP_CIPHER_CTX *cctx = NULL;

	if ((cctx = EVP_CIPHER_CTX_new()) == NULL) {
		PEMerror(ERR_R_MALLOC_FAILURE);
		goto err;
	}
	if (saltlen) {
		char psbuf[PEM_BUFSIZE];
		unsigned char keybuf[20];
		int enctmplen, inlen;

		if (cb)
			inlen = cb(psbuf, PEM_BUFSIZE, 0, u);
		else
			inlen = PEM_def_callback(psbuf, PEM_BUFSIZE, 0, u);
		if (inlen <= 0) {
			PEMerror(PEM_R_BAD_PASSWORD_READ);
			goto err;
		}
		enctmp = malloc(keylen + 8);
		if (!enctmp) {
			PEMerror(ERR_R_MALLOC_FAILURE);
			goto err;
		}
		if (!derive_pvk_key(keybuf, p, saltlen, (unsigned char *)psbuf,
		    inlen)) {
			goto err;
		}
		p += saltlen;
		/* Copy BLOBHEADER across, decrypt rest */
		memcpy(enctmp, p, 8);
		p += 8;
		if (keylen < 8) {
			PEMerror(PEM_R_PVK_TOO_SHORT);
			goto err;
		}
		inlen = keylen - 8;
		q = enctmp + 8;
		if (!EVP_DecryptInit_ex(cctx, EVP_rc4(), NULL, keybuf, NULL))
			goto err;
		if (!EVP_DecryptUpdate(cctx, q, &enctmplen, p, inlen))
			goto err;
		if (!EVP_DecryptFinal_ex(cctx, q + enctmplen, &enctmplen))
			goto err;
		magic = read_ledword((const unsigned char **)&q);
		if (magic != MS_RSA2MAGIC && magic != MS_DSS2MAGIC) {
			q = enctmp + 8;
			memset(keybuf + 5, 0, 11);
			if (!EVP_DecryptInit_ex(cctx, EVP_rc4(), NULL, keybuf,
			    NULL))
				goto err;
			explicit_bzero(keybuf, 20);
			if (!EVP_DecryptUpdate(cctx, q, &enctmplen, p, inlen))
				goto err;
			if (!EVP_DecryptFinal_ex(cctx, q + enctmplen,
			    &enctmplen))
				goto err;
			magic = read_ledword((const unsigned char **)&q);
			if (magic != MS_RSA2MAGIC && magic != MS_DSS2MAGIC) {
				PEMerror(PEM_R_BAD_DECRYPT);
				goto err;
			}
		} else
			explicit_bzero(keybuf, 20);
		p = enctmp;
	}

	ret = b2i_PrivateKey(&p, keylen);

 err:
	EVP_CIPHER_CTX_free(cctx);
	free(enctmp);

	return ret;
}


EVP_PKEY *
b2i_PVK_bio(BIO *in, pem_password_cb *cb, void *u)
{
	unsigned char pvk_hdr[24], *buf = NULL;
	const unsigned char *p;
	size_t buflen;
	EVP_PKEY *ret = NULL;
	unsigned int saltlen, keylen;

	if (BIO_read(in, pvk_hdr, 24) != 24) {
		PEMerror(PEM_R_PVK_DATA_TOO_SHORT);
		return NULL;
	}
	p = pvk_hdr;

	if (!do_PVK_header(&p, 24, 0, &saltlen, &keylen))
		return 0;
	buflen = keylen + saltlen;
	buf = malloc(buflen);
	if (!buf) {
		PEMerror(ERR_R_MALLOC_FAILURE);
		return 0;
	}
	p = buf;
	if (BIO_read(in, buf, buflen) != buflen) {
		PEMerror(PEM_R_PVK_DATA_TOO_SHORT);
		goto err;
	}
	ret = do_PVK_body(&p, saltlen, keylen, cb, u);

 err:
	freezero(buf, buflen);
	return ret;
}
LCRYPTO_ALIAS(b2i_PVK_bio);

static int
i2b_PVK(unsigned char **out, EVP_PKEY*pk, int enclevel, pem_password_cb *cb,
    void *u)
{
	int outlen = 24, pklen;
	unsigned char *p = NULL, *start = NULL, *salt = NULL;
	EVP_CIPHER_CTX *cctx = NULL;

	if ((cctx = EVP_CIPHER_CTX_new()) == NULL) {
		PEMerror(ERR_R_MALLOC_FAILURE);
		goto err;
	}
	if (enclevel != 0)
		outlen += PVK_SALTLEN;
	pklen = do_i2b(NULL, pk, 0);
	if (pklen < 0)
		goto err;
	outlen += pklen;
	start = p = malloc(outlen);
	if (!p) {
		PEMerror(ERR_R_MALLOC_FAILURE);
		goto err;
	}

	write_ledword(&p, MS_PVKMAGIC);
	write_ledword(&p, 0);
	if (pk->type == EVP_PKEY_DSA)
		write_ledword(&p, MS_KEYTYPE_SIGN);
	else
		write_ledword(&p, MS_KEYTYPE_KEYX);
	write_ledword(&p, enclevel ? 1 : 0);
	write_ledword(&p, enclevel ? PVK_SALTLEN : 0);
	write_ledword(&p, pklen);
	if (enclevel != 0) {
		arc4random_buf(p, PVK_SALTLEN);
		salt = p;
		p += PVK_SALTLEN;
	}
	do_i2b(&p, pk, 0);
	if (enclevel != 0) {
		char psbuf[PEM_BUFSIZE];
		unsigned char keybuf[20];
		int enctmplen, inlen;
		if (cb)
			inlen = cb(psbuf, PEM_BUFSIZE, 1, u);
		else
			inlen = PEM_def_callback(psbuf, PEM_BUFSIZE, 1, u);
		if (inlen <= 0) {
			PEMerror(PEM_R_BAD_PASSWORD_READ);
			goto err;
		}
		if (!derive_pvk_key(keybuf, salt, PVK_SALTLEN,
		    (unsigned char *)psbuf, inlen))
			goto err;
		if (enclevel == 1)
			memset(keybuf + 5, 0, 11);
		p = salt + PVK_SALTLEN + 8;
		if (!EVP_EncryptInit_ex(cctx, EVP_rc4(), NULL, keybuf, NULL))
			goto err;
		explicit_bzero(keybuf, 20);
		if (!EVP_EncryptUpdate(cctx, p, &enctmplen, p, pklen - 8))
			goto err;
		if (!EVP_EncryptFinal_ex(cctx, p + enctmplen, &enctmplen))
			goto err;
	}
	EVP_CIPHER_CTX_free(cctx);
	*out = start;
	return outlen;

 err:
	EVP_CIPHER_CTX_free(cctx);
	free(start);
	return -1;
}

int
i2b_PVK_bio(BIO *out, EVP_PKEY *pk, int enclevel, pem_password_cb *cb, void *u)
{
	unsigned char *tmp = NULL;
	int outlen, wrlen;

	outlen = i2b_PVK(&tmp, pk, enclevel, cb, u);
	if (outlen < 0)
		return -1;
	wrlen = BIO_write(out, tmp, outlen);
	free(tmp);
	if (wrlen != outlen) {
		PEMerror(PEM_R_BIO_WRITE_FAILURE);
		return -1;
	}
	return outlen;
}
LCRYPTO_ALIAS(i2b_PVK_bio);

#endif

#endif
