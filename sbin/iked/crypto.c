/*	$OpenBSD: crypto.c,v 1.47 2024/11/21 13:26:49 claudio Exp $	*/

/*
 * Copyright (c) 2010-2013 Reyk Floeter <reyk@openbsd.org>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <sys/types.h>
#include <sys/queue.h>
#include <sys/socket.h>
#include <sys/uio.h>

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <event.h>

#include <openssl/ecdsa.h>
#include <openssl/hmac.h>
#include <openssl/evp.h>
#include <openssl/sha.h>
#include <openssl/md5.h>
#include <openssl/x509.h>
#include <openssl/rsa.h>

#include "iked.h"
#include "ikev2.h"

/* RFC 7427, A.1 RSA */
static const uint8_t sha256WithRSA[] = {
	0x30, 0x0d, 0x06, 0x09, 0x2a, 0x86, 0x48, 0x86,
	0xf7, 0x0d, 0x01, 0x01, 0x0b, 0x05, 0x00
};
static const uint8_t sha384WithRSA[] = {
	0x30, 0x0d, 0x06, 0x09, 0x2a, 0x86, 0x48, 0x86,
	0xf7, 0x0d, 0x01, 0x01, 0x0c, 0x05, 0x00
};
static const uint8_t sha512WithRSA[] = {
	0x30, 0x0d, 0x06, 0x09, 0x2a, 0x86, 0x48, 0x86,
	0xf7, 0x0d, 0x01, 0x01, 0x0d, 0x05, 0x00
};
/* RFC 7427, A.3 ECDSA */
static const uint8_t ecdsa_sha256[] = {
	0x30, 0x0a, 0x06, 0x08, 0x2a, 0x86, 0x48, 0xce,
	0x3d, 0x04, 0x03, 0x02
};
static const uint8_t ecdsa_sha384[] = {
	0x30, 0x0a, 0x06, 0x08, 0x2a, 0x86, 0x48, 0xce,
	0x3d, 0x04, 0x03, 0x03
};
static const uint8_t ecdsa_sha512[] = {
	0x30, 0x0a, 0x06, 0x08, 0x2a, 0x86, 0x48, 0xce,
	0x3d, 0x04, 0x03, 0x04
};
/* RFC 7427, A.4.3 RSASSA-PSS with SHA-256 */
static const uint8_t rsapss_sha256[] = {
	0x30, 0x46, 0x06, 0x09, 0x2a, 0x86, 0x48, 0x86,
	0xf7, 0x0d, 0x01, 0x01, 0x0a, 0x30, 0x39, 0xa0,
	0x0f, 0x30, 0x0d, 0x06, 0x09, 0x60, 0x86, 0x48,
	0x01, 0x65, 0x03, 0x04, 0x02, 0x01, 0x05, 0x00,
	0xa1, 0x1c, 0x30, 0x1a, 0x06, 0x09, 0x2a, 0x86,
	0x48, 0x86, 0xf7, 0x0d, 0x01, 0x01, 0x08, 0x30,
	0x0d, 0x06, 0x09, 0x60, 0x86, 0x48, 0x01, 0x65,
	0x03, 0x04, 0x02, 0x01, 0x05, 0x00, 0xa2, 0x03,
	0x02, 0x01, 0x20, 0xa3, 0x03, 0x02, 0x01, 0x01
};
/* RSASSA-PSS SHA-384 */
static const uint8_t rsapss_sha384[] = {
	0x30, 0x46, 0x06, 0x09, 0x2a, 0x86, 0x48, 0x86,
	0xf7, 0x0d, 0x01, 0x01, 0x0a, 0x30, 0x34, 0xa0,
	0x0f, 0x30, 0x0d, 0x06, 0x09, 0x60, 0x86, 0x48,
	0x01, 0x65, 0x03, 0x04, 0x02, 0x02, 0x05, 0x00,
	0xa1, 0x1c, 0x30, 0x1a, 0x06, 0x09, 0x2a, 0x86,
	0x48, 0x86, 0xf7, 0x0d, 0x01, 0x01, 0x08, 0x30,
	0x0d, 0x06, 0x09, 0x60, 0x86, 0x48, 0x01, 0x65,
	0x03, 0x04, 0x02, 0x02, 0x05, 0x00, 0xa2, 0x03,
	0x02, 0x01, 0x30, 0xa3, 0x03, 0x02, 0x01, 0x01
};
/* RSASSA-PSS SHA-512 */
static const uint8_t rsapss_sha512[] = {
	0x30, 0x46, 0x06, 0x09, 0x2a, 0x86, 0x48, 0x86,
	0xf7, 0x0d, 0x01, 0x01, 0x0a, 0x30, 0x34, 0xa0,
	0x0f, 0x30, 0x0d, 0x06, 0x09, 0x60, 0x86, 0x48,
	0x01, 0x65, 0x03, 0x04, 0x02, 0x03, 0x05, 0x00,
	0xa1, 0x1c, 0x30, 0x1a, 0x06, 0x09, 0x2a, 0x86,
	0x48, 0x86, 0xf7, 0x0d, 0x01, 0x01, 0x08, 0x30,
	0x0d, 0x06, 0x09, 0x60, 0x86, 0x48, 0x01, 0x65,
	0x03, 0x04, 0x02, 0x03, 0x05, 0x00, 0xa2, 0x03,
	0x02, 0x01, 0x40, 0xa3, 0x03, 0x02, 0x01, 0x01
};
/* RSASSA-PSS SHA-256, no trailer */
static const uint8_t rsapss_sha256nt[] = {
	0x30, 0x41, 0x06, 0x09, 0x2a, 0x86, 0x48, 0x86,
	0xf7, 0x0d, 0x01, 0x01, 0x0a, 0x30, 0x34, 0xa0,
	0x0f, 0x30, 0x0d, 0x06, 0x09, 0x60, 0x86, 0x48,
	0x01, 0x65, 0x03, 0x04, 0x02, 0x01, 0x05, 0x00,
	0xa1, 0x1c, 0x30, 0x1a, 0x06, 0x09, 0x2a, 0x86,
	0x48, 0x86, 0xf7, 0x0d, 0x01, 0x01, 0x08, 0x30,
	0x0d, 0x06, 0x09, 0x60, 0x86, 0x48, 0x01, 0x65,
	0x03, 0x04, 0x02, 0x01, 0x05, 0x00, 0xa2, 0x03,
	0x02, 0x01, 0x20
};
/* RSASSA-PSS SHA-384, no trailer */
static const uint8_t rsapss_sha384nt[] = {
	0x30, 0x41, 0x06, 0x09, 0x2a, 0x86, 0x48, 0x86,
	0xf7, 0x0d, 0x01, 0x01, 0x0a, 0x30, 0x34, 0xa0,
	0x0f, 0x30, 0x0d, 0x06, 0x09, 0x60, 0x86, 0x48,
	0x01, 0x65, 0x03, 0x04, 0x02, 0x02, 0x05, 0x00,
	0xa1, 0x1c, 0x30, 0x1a, 0x06, 0x09, 0x2a, 0x86,
	0x48, 0x86, 0xf7, 0x0d, 0x01, 0x01, 0x08, 0x30,
	0x0d, 0x06, 0x09, 0x60, 0x86, 0x48, 0x01, 0x65,
	0x03, 0x04, 0x02, 0x02, 0x05, 0x00, 0xa2, 0x03,
	0x02, 0x01, 0x30
};
/* RSASSA-PSS SHA-512, no trailer */
static const uint8_t rsapss_sha512nt[] = {
	0x30, 0x41, 0x06, 0x09, 0x2a, 0x86, 0x48, 0x86,
	0xf7, 0x0d, 0x01, 0x01, 0x0a, 0x30, 0x34, 0xa0,
	0x0f, 0x30, 0x0d, 0x06, 0x09, 0x60, 0x86, 0x48,
	0x01, 0x65, 0x03, 0x04, 0x02, 0x03, 0x05, 0x00,
	0xa1, 0x1c, 0x30, 0x1a, 0x06, 0x09, 0x2a, 0x86,
	0x48, 0x86, 0xf7, 0x0d, 0x01, 0x01, 0x08, 0x30,
	0x0d, 0x06, 0x09, 0x60, 0x86, 0x48, 0x01, 0x65,
	0x03, 0x04, 0x02, 0x03, 0x05, 0x00, 0xa2, 0x03,
	0x02, 0x01, 0x40
};

#define FLAG_RSA_PSS	0x00001
int force_rsa_pss = 0;	/* XXX move to API */

static const struct {
	int		 sc_keytype;
	const EVP_MD	*(*sc_md)(void);
	uint8_t		 sc_len;
	const uint8_t	*sc_oid;
	uint32_t	 sc_flags;
} schemes[] = {
	{ EVP_PKEY_RSA, EVP_sha256, sizeof(sha256WithRSA), sha256WithRSA, 0 },
	{ EVP_PKEY_RSA, EVP_sha384, sizeof(sha384WithRSA), sha384WithRSA, 0 },
	{ EVP_PKEY_RSA, EVP_sha512, sizeof(sha512WithRSA), sha512WithRSA, 0 },
	{ EVP_PKEY_EC,  EVP_sha256, sizeof(ecdsa_sha256),  ecdsa_sha256, 0 },
	{ EVP_PKEY_EC,  EVP_sha384, sizeof(ecdsa_sha384),  ecdsa_sha384, 0 },
	{ EVP_PKEY_EC,  EVP_sha512, sizeof(ecdsa_sha512),  ecdsa_sha512, 0 },
	{ EVP_PKEY_RSA, EVP_sha256, sizeof(rsapss_sha256), rsapss_sha256,
	    FLAG_RSA_PSS },
	{ EVP_PKEY_RSA, EVP_sha384, sizeof(rsapss_sha384), rsapss_sha384,
	    FLAG_RSA_PSS },
	{ EVP_PKEY_RSA, EVP_sha512, sizeof(rsapss_sha512), rsapss_sha512,
	    FLAG_RSA_PSS },
	{ EVP_PKEY_RSA, EVP_sha256, sizeof(rsapss_sha256nt), rsapss_sha256nt,
	    FLAG_RSA_PSS },
	{ EVP_PKEY_RSA, EVP_sha384, sizeof(rsapss_sha384nt), rsapss_sha384nt,
	    FLAG_RSA_PSS },
	{ EVP_PKEY_RSA, EVP_sha512, sizeof(rsapss_sha512nt), rsapss_sha512nt,
	    FLAG_RSA_PSS },
};

int	_dsa_verify_init(struct iked_dsa *, const uint8_t *, size_t);
int	_dsa_verify_prepare(struct iked_dsa *, uint8_t **, size_t *,
	    uint8_t **);
int	_dsa_sign_encode(struct iked_dsa *, uint8_t *, size_t, size_t *);
int	_dsa_sign_ecdsa(struct iked_dsa *, uint8_t *, size_t);

struct iked_hash *
hash_new(uint8_t type, uint16_t id)
{
	struct iked_hash	*hash;
	const EVP_MD		*md = NULL;
	int			 length = 0, fixedkey = 0, trunc = 0, isaead = 0;

	switch (type) {
	case IKEV2_XFORMTYPE_PRF:
		switch (id) {
		case IKEV2_XFORMPRF_HMAC_MD5:
			md = EVP_md5();
			length = MD5_DIGEST_LENGTH;
			break;
		case IKEV2_XFORMPRF_HMAC_SHA1:
			md = EVP_sha1();
			length = SHA_DIGEST_LENGTH;
			break;
		case IKEV2_XFORMPRF_HMAC_SHA2_256:
			md = EVP_sha256();
			length = SHA256_DIGEST_LENGTH;
			break;
		case IKEV2_XFORMPRF_HMAC_SHA2_384:
			md = EVP_sha384();
			length = SHA384_DIGEST_LENGTH;
			break;
		case IKEV2_XFORMPRF_HMAC_SHA2_512:
			md = EVP_sha512();
			length = SHA512_DIGEST_LENGTH;
			break;
		case IKEV2_XFORMPRF_AES128_XCBC:
			fixedkey = 128 / 8;
			length = fixedkey;
			/* FALLTHROUGH */
		case IKEV2_XFORMPRF_HMAC_TIGER:
		case IKEV2_XFORMPRF_AES128_CMAC:
		default:
			log_debug("%s: prf %s not supported", __func__,
			    print_map(id, ikev2_xformprf_map));
			break;
		}
		break;
	case IKEV2_XFORMTYPE_INTEGR:
		switch (id) {
		case IKEV2_XFORMAUTH_HMAC_MD5_96:
			md = EVP_md5();
			length = MD5_DIGEST_LENGTH;
			trunc = 12;
			break;
		case IKEV2_XFORMAUTH_HMAC_SHA1_96:
			md = EVP_sha1();
			length = SHA_DIGEST_LENGTH;
			trunc = 12;
			break;
		case IKEV2_XFORMAUTH_HMAC_SHA2_256_128:
			md = EVP_sha256();
			length = SHA256_DIGEST_LENGTH;
			trunc = 16;
			break;
		case IKEV2_XFORMAUTH_HMAC_SHA2_384_192:
			md = EVP_sha384();
			length = SHA384_DIGEST_LENGTH;
			trunc = 24;
			break;
		case IKEV2_XFORMAUTH_HMAC_SHA2_512_256:
			md = EVP_sha512();
			length = SHA512_DIGEST_LENGTH;
			trunc = 32;
			break;
		case IKEV2_XFORMAUTH_AES_GCM_12:
			length = 12;
			isaead = 1;
			break;
		case IKEV2_XFORMAUTH_AES_GCM_16:
			length = 16;
			isaead = 1;
			break;
		case IKEV2_XFORMAUTH_NONE:
		case IKEV2_XFORMAUTH_DES_MAC:
		case IKEV2_XFORMAUTH_KPDK_MD5:
		case IKEV2_XFORMAUTH_AES_XCBC_96:
		case IKEV2_XFORMAUTH_HMAC_MD5_128:
		case IKEV2_XFORMAUTH_HMAC_SHA1_160:
		case IKEV2_XFORMAUTH_AES_CMAC_96:
		case IKEV2_XFORMAUTH_AES_128_GMAC:
		case IKEV2_XFORMAUTH_AES_192_GMAC:
		case IKEV2_XFORMAUTH_AES_256_GMAC:
		default:
			log_debug("%s: auth %s not supported", __func__,
			    print_map(id, ikev2_xformauth_map));
			break;
		}
		break;
	default:
		log_debug("%s: hash type %s not supported", __func__,
		    print_map(id, ikev2_xformtype_map));
		break;
	}
	if (!isaead && md == NULL)
		return (NULL);

	if ((hash = calloc(1, sizeof(*hash))) == NULL) {
		log_debug("%s: alloc hash", __func__);
		return (NULL);
	}

	hash->hash_type = type;
	hash->hash_id = id;
	hash->hash_priv = md;
	hash->hash_ctx = NULL;
	hash->hash_trunc = trunc;
	hash->hash_length = length;
	hash->hash_fixedkey = fixedkey;
	hash->hash_isaead = isaead;

	if (isaead)
		return (hash);

	hash->hash_ctx = HMAC_CTX_new();
	if (hash->hash_ctx == NULL) {
		log_debug("%s: alloc hash ctx", __func__);
		hash_free(hash);
		return (NULL);
	}

	return (hash);
}

struct ibuf *
hash_setkey(struct iked_hash *hash, void *key, size_t keylen)
{
	ibuf_free(hash->hash_key);
	if ((hash->hash_key = ibuf_new(key, keylen)) == NULL) {
		log_debug("%s: alloc hash key", __func__);
		return (NULL);
	}
	return (hash->hash_key);
}

void
hash_free(struct iked_hash *hash)
{
	if (hash == NULL)
		return;
	HMAC_CTX_free(hash->hash_ctx);
	ibuf_free(hash->hash_key);
	free(hash);
}

void
hash_init(struct iked_hash *hash)
{
	HMAC_Init_ex(hash->hash_ctx, ibuf_data(hash->hash_key),
	    ibuf_size(hash->hash_key), hash->hash_priv, NULL);
}

void
hash_update(struct iked_hash *hash, void *buf, size_t len)
{
	HMAC_Update(hash->hash_ctx, buf, len);
}

void
hash_final(struct iked_hash *hash, void *buf, size_t *len)
{
	unsigned int	 length = 0;

	HMAC_Final(hash->hash_ctx, buf, &length);
	*len = (size_t)length;

	/* Truncate the result if required by the alg */
	if (hash->hash_trunc && *len > hash->hash_trunc)
		*len = hash->hash_trunc;
}

size_t
hash_length(struct iked_hash *hash)
{
	if (hash->hash_trunc)
		return (hash->hash_trunc);
	return (hash->hash_length);
}

size_t
hash_keylength(struct iked_hash *hash)
{
	return (hash->hash_length);
}

struct iked_cipher *
cipher_new(uint8_t type, uint16_t id, uint16_t id_length)
{
	struct iked_cipher	*encr;
	const EVP_CIPHER	*cipher = NULL;
	int			 length = 0, fixedkey = 0, ivlength = 0;
	int			 saltlength = 0, authid = 0;

	switch (type) {
	case IKEV2_XFORMTYPE_ENCR:
		switch (id) {
		case IKEV2_XFORMENCR_3DES:
			cipher = EVP_des_ede3_cbc();
			length = EVP_CIPHER_block_size(cipher);
			fixedkey = EVP_CIPHER_key_length(cipher);
			ivlength = EVP_CIPHER_iv_length(cipher);
			break;
		case IKEV2_XFORMENCR_AES_CBC:
			switch (id_length) {
			case 128:
				cipher = EVP_aes_128_cbc();
				break;
			case 192:
				cipher = EVP_aes_192_cbc();
				break;
			case 256:
				cipher = EVP_aes_256_cbc();
				break;
			default:
				log_debug("%s: invalid key length %d"
				    " for cipher %s", __func__, id_length,
				    print_map(id, ikev2_xformencr_map));
				break;
			}
			if (cipher == NULL)
				break;
			length = EVP_CIPHER_block_size(cipher);
			ivlength = EVP_CIPHER_iv_length(cipher);
			fixedkey = EVP_CIPHER_key_length(cipher);
			break;
		case IKEV2_XFORMENCR_AES_GCM_16:
		case IKEV2_XFORMENCR_AES_GCM_12:
			switch (id_length) {
			case 128:
				cipher = EVP_aes_128_gcm();
				break;
			case 256:
				cipher = EVP_aes_256_gcm();
				break;
			default:
				log_debug("%s: invalid key length %d"
				    " for cipher %s", __func__, id_length,
				    print_map(id, ikev2_xformencr_map));
				break;
			}
			if (cipher == NULL)
				break;
			switch(id) {
			case IKEV2_XFORMENCR_AES_GCM_16:
				authid = IKEV2_XFORMAUTH_AES_GCM_16;
				break;
			case IKEV2_XFORMENCR_AES_GCM_12:
				authid = IKEV2_XFORMAUTH_AES_GCM_12;
				break;
			}
			length = EVP_CIPHER_block_size(cipher);
			ivlength = 8;
			saltlength = 4;
			fixedkey = EVP_CIPHER_key_length(cipher) + saltlength;
			break;
		case IKEV2_XFORMENCR_DES_IV64:
		case IKEV2_XFORMENCR_DES:
		case IKEV2_XFORMENCR_RC5:
		case IKEV2_XFORMENCR_IDEA:
		case IKEV2_XFORMENCR_CAST:
		case IKEV2_XFORMENCR_BLOWFISH:
		case IKEV2_XFORMENCR_3IDEA:
		case IKEV2_XFORMENCR_DES_IV32:
		case IKEV2_XFORMENCR_NULL:
		case IKEV2_XFORMENCR_AES_CTR:
			/* FALLTHROUGH */
		default:
			log_debug("%s: cipher %s not supported", __func__,
			    print_map(id, ikev2_xformencr_map));
			cipher = NULL;
			break;
		}
		break;
	default:
		log_debug("%s: cipher type %s not supported", __func__,
		    print_map(id, ikev2_xformtype_map));
		break;
	}
	if (cipher == NULL)
		return (NULL);

	if ((encr = calloc(1, sizeof(*encr))) == NULL) {
		log_debug("%s: alloc cipher", __func__);
		return (NULL);
	}

	encr->encr_id = id;
	encr->encr_priv = cipher;
	encr->encr_ctx = NULL;
	encr->encr_length = length;
	encr->encr_fixedkey = fixedkey;
	encr->encr_ivlength = ivlength ? ivlength : length;
	encr->encr_saltlength = saltlength;
	encr->encr_authid = authid;

	encr->encr_ctx = EVP_CIPHER_CTX_new();
	if (encr->encr_ctx == NULL) {
		log_debug("%s: alloc cipher ctx", __func__);
		cipher_free(encr);
		return (NULL);
	}

	return (encr);
}

struct ibuf *
cipher_setkey(struct iked_cipher *encr, const void *key, size_t keylen)
{
	ibuf_free(encr->encr_key);
	if ((encr->encr_key = ibuf_new(key, keylen)) == NULL) {
		log_debug("%s: alloc cipher key", __func__);
		return (NULL);
	}
	return (encr->encr_key);
}

struct ibuf *
cipher_setiv(struct iked_cipher *encr, const void *iv, size_t len)
{
	ibuf_free(encr->encr_iv);
	encr->encr_iv = NULL;
	if (iv != NULL) {
		if (len < encr->encr_ivlength) {
			log_debug("%s: invalid IV length %zu", __func__, len);
			return (NULL);
		}
		encr->encr_iv = ibuf_new(iv, encr->encr_ivlength);
	} else {
		switch (encr->encr_id) {
		case IKEV2_XFORMENCR_AES_GCM_16:
		case IKEV2_XFORMENCR_AES_GCM_12:
			if (encr->encr_ivlength != sizeof(encr->encr_civ)) {
				log_info("%s: ivlen does not match %zu != %zu",
				    __func__, encr->encr_ivlength,
				    sizeof(encr->encr_civ));
				return (NULL);
			}
			encr->encr_iv = ibuf_new(&encr->encr_civ, sizeof(encr->encr_civ));
			encr->encr_civ++;
			break;
		default:
			/* Get new random IV */
			encr->encr_iv = ibuf_random(encr->encr_ivlength);
		}
	}
	if (encr->encr_iv == NULL) {
		log_debug("%s: failed to set IV", __func__);
		return (NULL);
	}
	return (encr->encr_iv);
}

int
cipher_settag(struct iked_cipher *encr, uint8_t *data, size_t len)
{
	return (EVP_CIPHER_CTX_ctrl(encr->encr_ctx,
	    EVP_CTRL_GCM_SET_TAG, len, data) != 1);
}

int
cipher_gettag(struct iked_cipher *encr, uint8_t *data, size_t len)
{
	return (EVP_CIPHER_CTX_ctrl(encr->encr_ctx,
	    EVP_CTRL_GCM_GET_TAG, len, data) != 1);
}

void
cipher_free(struct iked_cipher *encr)
{
	if (encr == NULL)
		return;
	EVP_CIPHER_CTX_free(encr->encr_ctx);
	ibuf_free(encr->encr_iv);
	ibuf_free(encr->encr_key);
	free(encr);
}

int
cipher_init(struct iked_cipher *encr, int enc)
{
	struct ibuf	*nonce = NULL;
	int		 ret = -1;

	if (EVP_CipherInit_ex(encr->encr_ctx, encr->encr_priv, NULL,
	    NULL, NULL, enc) != 1)
		return (-1);
	if (encr->encr_saltlength > 0) {
		/* For AEADs the nonce is salt + IV  (see RFC5282) */
		nonce = ibuf_new(ibuf_seek(encr->encr_key,
		    ibuf_size(encr->encr_key) - encr->encr_saltlength,
		    encr->encr_saltlength), encr->encr_saltlength);
		if (nonce == NULL)
			return (-1);
		if (ibuf_add_ibuf(nonce, encr->encr_iv) != 0)
			goto done;
		if (EVP_CipherInit_ex(encr->encr_ctx, NULL, NULL,
		    ibuf_data(encr->encr_key), ibuf_data(nonce), enc) != 1)
			goto done;
	} else
		if (EVP_CipherInit_ex(encr->encr_ctx, NULL, NULL,
		    ibuf_data(encr->encr_key), ibuf_data(encr->encr_iv), enc) != 1)
			return (-1);
	EVP_CIPHER_CTX_set_padding(encr->encr_ctx, 0);
	ret = 0;
 done:
	ibuf_free(nonce);
	return (ret);
}

int
cipher_init_encrypt(struct iked_cipher *encr)
{
	return (cipher_init(encr, 1));
}

int
cipher_init_decrypt(struct iked_cipher *encr)
{
	return (cipher_init(encr, 0));
}

void
cipher_aad(struct iked_cipher *encr, const void *in, size_t inlen,
    size_t *outlen)
{
	int	 olen = 0;

	if (EVP_CipherUpdate(encr->encr_ctx, NULL, &olen, in, inlen) != 1) {
		ca_sslerror(__func__);
		*outlen = 0;
		return;
	}
	*outlen = (size_t)olen;
}

int
cipher_update(struct iked_cipher *encr, const void *in, size_t inlen,
    void *out, size_t *outlen)
{
	int	 olen;

	olen = 0;
	if (EVP_CipherUpdate(encr->encr_ctx, out, &olen, in, inlen) != 1) {
		ca_sslerror(__func__);
		*outlen = 0;
		return (-1);
	}
	*outlen = (size_t)olen;
	return (0);
}

int
cipher_final(struct iked_cipher *encr)
{
	int	 olen;

	/*
	 * We always have EVP_CIPH_NO_PADDING set.  This means arg
	 * out is not used and olen should always be 0.
	 */
	if (EVP_CipherFinal_ex(encr->encr_ctx, NULL, &olen) != 1) {
		ca_sslerror(__func__);
		return (-1);
	}
	return (0);
}

size_t
cipher_length(struct iked_cipher *encr)
{
	return (encr->encr_length);
}

size_t
cipher_keylength(struct iked_cipher *encr)
{
	if (encr->encr_fixedkey)
		return (encr->encr_fixedkey);

	/* Might return zero */
	return (ibuf_length(encr->encr_key));
}

size_t
cipher_ivlength(struct iked_cipher *encr)
{
	return (encr->encr_ivlength);
}

size_t
cipher_outlength(struct iked_cipher *encr, size_t inlen)
{
	return (roundup(inlen, encr->encr_length));
}

struct iked_dsa *
dsa_new(uint8_t id, struct iked_hash *prf, int sign)
{
	struct iked_dsa		*dsap = NULL, dsa;

	bzero(&dsa, sizeof(dsa));

	switch (id) {
	case IKEV2_AUTH_SIG:
		if (sign)
			dsa.dsa_priv = EVP_sha256(); /* XXX should be passed */
		else
			dsa.dsa_priv = NULL; /* set later by dsa_init() */
		break;
	case IKEV2_AUTH_RSA_SIG:
		/* RFC5996 says we SHOULD use SHA1 here */
		dsa.dsa_priv = EVP_sha1();
		break;
	case IKEV2_AUTH_SHARED_KEY_MIC:
		if (prf == NULL || prf->hash_priv == NULL)
			fatalx("dsa_new: invalid PRF");
		dsa.dsa_priv = prf->hash_priv;
		dsa.dsa_hmac = 1;
		break;
	case IKEV2_AUTH_DSS_SIG:
		dsa.dsa_priv = EVP_sha1();
		break;
	case IKEV2_AUTH_ECDSA_256:
		dsa.dsa_priv = EVP_sha256();
		break;
	case IKEV2_AUTH_ECDSA_384:
		dsa.dsa_priv = EVP_sha384();
		break;
	case IKEV2_AUTH_ECDSA_521:
		dsa.dsa_priv = EVP_sha512();
		break;
	default:
		log_debug("%s: auth method %s not supported", __func__,
		    print_map(id, ikev2_auth_map));
		break;
	}

	if ((dsap = calloc(1, sizeof(*dsap))) == NULL) {
		log_debug("%s: alloc dsa ctx", __func__);

		return (NULL);
	}
	memcpy(dsap, &dsa, sizeof(*dsap));

	dsap->dsa_method = id;
	dsap->dsa_sign = sign;

	if (dsap->dsa_hmac) {
		if ((dsap->dsa_ctx = HMAC_CTX_new()) == NULL) {
			log_debug("%s: alloc hash ctx", __func__);
			dsa_free(dsap);
			return (NULL);
		}
	} else {
		if ((dsap->dsa_ctx = EVP_MD_CTX_create()) == NULL) {
			log_debug("%s: alloc digest ctx", __func__);
			dsa_free(dsap);
			return (NULL);
		}
	}

	return (dsap);
}

struct iked_dsa *
dsa_sign_new(uint8_t id, struct iked_hash *prf)
{
	return (dsa_new(id, prf, 1));
}

struct iked_dsa *
dsa_verify_new(uint8_t id, struct iked_hash *prf)
{
	return (dsa_new(id, prf, 0));
}

void
dsa_free(struct iked_dsa *dsa)
{
	if (dsa == NULL)
		return;
	if (dsa->dsa_hmac) {
		HMAC_CTX_free((HMAC_CTX *)dsa->dsa_ctx);
	} else {
		EVP_MD_CTX_free((EVP_MD_CTX *)dsa->dsa_ctx);
		EVP_PKEY_free(dsa->dsa_key);
	}

	ibuf_free(dsa->dsa_keydata);
	free(dsa);
}

struct ibuf *
dsa_setkey(struct iked_dsa *dsa, void *key, size_t keylen, uint8_t type)
{
	BIO		*rawcert = NULL;
	X509		*cert = NULL;
	RSA		*rsa = NULL;
	EC_KEY		*ec = NULL;
	EVP_PKEY	*pkey = NULL;

	ibuf_free(dsa->dsa_keydata);
	if ((dsa->dsa_keydata = ibuf_new(key, keylen)) == NULL) {
		log_debug("%s: alloc signature key", __func__);
		return (NULL);
	}

	if ((rawcert = BIO_new_mem_buf(key, keylen)) == NULL)
		goto err;

	switch (type) {
	case IKEV2_CERT_X509_CERT:
		if ((cert = d2i_X509_bio(rawcert, NULL)) == NULL)
			goto sslerr;
		if ((pkey = X509_get_pubkey(cert)) == NULL)
			goto sslerr;
		dsa->dsa_key = pkey;
		break;
	case IKEV2_CERT_RSA_KEY:
		if (dsa->dsa_sign) {
			if ((rsa = d2i_RSAPrivateKey_bio(rawcert,
			    NULL)) == NULL)
				goto sslerr;
		} else {
			if ((rsa = d2i_RSAPublicKey_bio(rawcert,
			    NULL)) == NULL)
				goto sslerr;
		}

		if ((pkey = EVP_PKEY_new()) == NULL)
			goto sslerr;
		if (!EVP_PKEY_set1_RSA(pkey, rsa))
			goto sslerr;

		RSA_free(rsa);		/* pkey now has the reference */
		dsa->dsa_key = pkey;
		break;
	case IKEV2_CERT_ECDSA:
		if (dsa->dsa_sign) {
			if ((ec = d2i_ECPrivateKey_bio(rawcert, NULL)) == NULL)
				goto sslerr;
		} else {
			if ((ec = d2i_EC_PUBKEY_bio(rawcert, NULL)) == NULL)
				goto sslerr;
		}

		if ((pkey = EVP_PKEY_new()) == NULL)
			goto sslerr;
		if (!EVP_PKEY_set1_EC_KEY(pkey, ec))
			goto sslerr;

		EC_KEY_free(ec);	/* pkey now has the reference */
		dsa->dsa_key = pkey;
		break;
	default:
		if (dsa->dsa_hmac)
			break;
		log_debug("%s: unsupported key type", __func__);
		goto err;
	}

	X509_free(cert);
	BIO_free(rawcert);	/* temporary for parsing */

	return (dsa->dsa_keydata);

 sslerr:
	ca_sslerror(__func__);
 err:
	log_debug("%s: error", __func__);

	RSA_free(rsa);
	EC_KEY_free(ec);
	EVP_PKEY_free(pkey);
	X509_free(cert);
	BIO_free(rawcert);
	ibuf_free(dsa->dsa_keydata);
	dsa->dsa_keydata = NULL;
	return (NULL);
}

int
_dsa_verify_init(struct iked_dsa *dsa, const uint8_t *sig, size_t len)
{
	uint8_t			 oidlen;
	size_t			 i;
	int			 keytype;

	if (dsa->dsa_priv != NULL)
		return (0);
	/*
	 * For IKEV2_AUTH_SIG the oid of the authentication signature
	 * is encoded in the first bytes of the auth message.
	 */
	if (dsa->dsa_method != IKEV2_AUTH_SIG)  {
		log_debug("%s: dsa_priv not set for %s", __func__,
		    print_map(dsa->dsa_method, ikev2_auth_map));
		return (-1);
	}
	if (dsa->dsa_key == NULL) {
		log_debug("%s: dsa_key not set for %s", __func__,
		    print_map(dsa->dsa_method, ikev2_auth_map));
		return (-1);
	}
	keytype = EVP_PKEY_type(EVP_PKEY_id(((EVP_PKEY *)dsa->dsa_key)));
	if (sig == NULL) {
		log_debug("%s: signature missing", __func__);
		return (-1);
	}
	if (len < sizeof(oidlen)) {
		log_debug("%s: signature (%zu) too small for oid length",
		    __func__, len);
		return (-1);
	}
	memcpy(&oidlen, sig, sizeof(oidlen));
	if (len < (size_t)oidlen + sizeof(oidlen)) {
		log_debug("%s: signature (%zu) too small for oid (%u)",
		    __func__, len, oidlen);
		return (-1);
	}
	for (i = 0; i < nitems(schemes); i++) {
		if (keytype == schemes[i].sc_keytype &&
		    oidlen == schemes[i].sc_len &&
		    memcmp(sig + 1, schemes[i].sc_oid,
		    schemes[i].sc_len) == 0) {
			dsa->dsa_priv = (*schemes[i].sc_md)();
			dsa->dsa_flags = schemes[i].sc_flags;
			log_debug("%s: signature scheme %zd selected",
			    __func__, i);
			return (0);
		}
	}
	log_debug("%s: unsupported signature (%d)", __func__, oidlen);
	return (-1);
}

int
dsa_init(struct iked_dsa *dsa, const void *buf, size_t len)
{
	int		 ret;
	EVP_PKEY_CTX	*pctx = NULL;

	if (dsa->dsa_hmac) {
		if (!HMAC_Init_ex(dsa->dsa_ctx, ibuf_data(dsa->dsa_keydata),
		    ibuf_size(dsa->dsa_keydata), dsa->dsa_priv, NULL))
			return (-1);
		return (0);
	}

	if (dsa->dsa_sign) {
		if (force_rsa_pss &&
		    EVP_PKEY_base_id(dsa->dsa_key) == EVP_PKEY_RSA)
			dsa->dsa_flags = FLAG_RSA_PSS;
		ret = EVP_DigestSignInit(dsa->dsa_ctx, &pctx, dsa->dsa_priv,
		    NULL, dsa->dsa_key);
	} else {
		/* sets dsa_priv, dsa_flags */
		if ((ret = _dsa_verify_init(dsa, buf, len)) != 0)
			return (ret);
		ret = EVP_DigestVerifyInit(dsa->dsa_ctx, &pctx, dsa->dsa_priv,
		    NULL, dsa->dsa_key);
	}
	if (ret == 1 && dsa->dsa_flags == FLAG_RSA_PSS) {
		if (EVP_PKEY_CTX_set_rsa_padding(pctx,
		    RSA_PKCS1_PSS_PADDING) <= 0 ||
		    EVP_PKEY_CTX_set_rsa_pss_saltlen(pctx, -1) <= 0)
			return (-1);
	}
	if (_dsa_sign_encode(dsa, NULL, 0, NULL) < 0)
		return (-1);

	return (ret == 1 ? 0 : -1);
}

int
dsa_update(struct iked_dsa *dsa, const void *buf, size_t len)
{
	int	ret;

	if (dsa->dsa_hmac)
		ret = HMAC_Update(dsa->dsa_ctx, buf, len);
	else if (dsa->dsa_sign)
		ret = EVP_DigestSignUpdate(dsa->dsa_ctx, buf, len);
	else
		ret = EVP_DigestVerifyUpdate(dsa->dsa_ctx, buf, len);

	return (ret == 1 ? 0 : -1);
}

/* Prefix signature hash with encoded type */
int
_dsa_sign_encode(struct iked_dsa *dsa, uint8_t *ptr, size_t len, size_t *offp)
{
	int		 keytype;
	size_t		 i, need;

	if (offp)
		*offp = 0;
	if (dsa->dsa_method != IKEV2_AUTH_SIG)
		return (0);
	if (dsa->dsa_key == NULL)
		return (-1);
	keytype = EVP_PKEY_type(EVP_PKEY_id(((EVP_PKEY *)dsa->dsa_key)));
	for (i = 0; i < nitems(schemes); i++) {
		/* XXX should avoid calling sc_md() each time... */
		if (keytype == schemes[i].sc_keytype &&
		    dsa->dsa_flags == schemes[i].sc_flags &&
		    (dsa->dsa_priv == (*schemes[i].sc_md)()))
			break;
	}
	if (i >= nitems(schemes))
		return (-1);
	log_debug("%s: signature scheme %zd selected", __func__, i);
	need = sizeof(ptr[0]) + schemes[i].sc_len;
	if (ptr) {
		if (len < need)
			return (-1);
		ptr[0] = schemes[i].sc_len;
		memcpy(ptr + sizeof(ptr[0]), schemes[i].sc_oid,
		    schemes[i].sc_len);
	}
	if (offp)
		*offp = need;
	return (0);
}

/* Export size of encoded signature hash type */
size_t
dsa_prefix(struct iked_dsa *dsa)
{
	size_t		off = 0;

	if (_dsa_sign_encode(dsa, NULL, 0, &off) < 0)
		fatal("dsa_prefix: internal error");
	return off;
}

size_t
dsa_length(struct iked_dsa *dsa)
{
	if (dsa->dsa_hmac)
		return (EVP_MD_size(dsa->dsa_priv));
	switch (dsa->dsa_method) {
	case IKEV2_AUTH_ECDSA_256:
	case IKEV2_AUTH_ECDSA_384:
	case IKEV2_AUTH_ECDSA_521:
		/* size of concat(r|s) */
		return (2 * ((EVP_PKEY_bits(dsa->dsa_key) + 7) / 8));
	}
	return (dsa_prefix(dsa) + EVP_PKEY_size(dsa->dsa_key));
}

int
_dsa_sign_ecdsa(struct iked_dsa *dsa, uint8_t *ptr, size_t len)
{
	ECDSA_SIG	*obj = NULL;
	uint8_t		*tmp = NULL;
	const uint8_t	*p;
	size_t		 tmplen;
	int		 ret = -1;
	int		 bnlen, off;
	const BIGNUM	*r, *s;

	if (len % 2)
		goto done;	/* must be even */
	bnlen = len/2;
	/*
	 * (a) create DER signature into 'tmp' buffer
	 * (b) convert buffer to ECDSA_SIG object
	 * (c) concatenate the padded r|s BIGNUMS into 'ptr'
	 */
	if (EVP_DigestSignFinal(dsa->dsa_ctx, NULL, &tmplen) != 1)
		goto done;
	if ((tmp = calloc(1, tmplen)) == NULL)
		goto done;
	if (EVP_DigestSignFinal(dsa->dsa_ctx, tmp, &tmplen) != 1)
		goto done;
	p = tmp;
	if ((obj = d2i_ECDSA_SIG(NULL, &p, tmplen)) == NULL)
		goto done;
	ECDSA_SIG_get0(obj, &r, &s);
	if (BN_num_bytes(r) > bnlen || BN_num_bytes(s) > bnlen)
		goto done;
	memset(ptr, 0, len);
	off = bnlen - BN_num_bytes(r);
	BN_bn2bin(r, ptr + off);
	off = 2 * bnlen - BN_num_bytes(s);
	BN_bn2bin(s, ptr + off);
	ret = 0;
 done:
	free(tmp);
	ECDSA_SIG_free(obj);

	return (ret);
}

ssize_t
dsa_sign_final(struct iked_dsa *dsa, void *buf, size_t len)
{
	unsigned int	 hmaclen;
	size_t		 off = 0;
	uint8_t		*ptr = buf;

	if (len < dsa_length(dsa))
		return (-1);

	if (dsa->dsa_hmac) {
		if (!HMAC_Final(dsa->dsa_ctx, buf, &hmaclen))
			return (-1);
		if (hmaclen > INT_MAX)
			return (-1);
		return (ssize_t)hmaclen;
	} else {
		switch (dsa->dsa_method) {
		case IKEV2_AUTH_ECDSA_256:
		case IKEV2_AUTH_ECDSA_384:
		case IKEV2_AUTH_ECDSA_521:
			if (_dsa_sign_ecdsa(dsa, buf, len) < 0)
				return (-1);
			return (len);
		default:
			if (_dsa_sign_encode(dsa, ptr, len, &off) < 0)
				return (-1);
			if (off > len)
				return (-1);
			len -= off;
			ptr += off;
			if (EVP_DigestSignFinal(dsa->dsa_ctx, ptr, &len) != 1)
				return (-1);
			return (len + off);
		}
	}
	return (-1);
}

int
_dsa_verify_prepare(struct iked_dsa *dsa, uint8_t **sigp, size_t *lenp,
    uint8_t **freemep)
{
	ECDSA_SIG	*obj = NULL;
	uint8_t		*ptr = NULL;
	size_t		 bnlen, off;
	ssize_t		 len;
	int		 ret = -1;
	BIGNUM		*r = NULL, *s = NULL;

	*freemep = NULL;	/* don't return garbage in case of an error */

	switch (dsa->dsa_method) {
	case IKEV2_AUTH_SIG:
		/*
		 * The first byte of the signature encodes the OID
		 * prefix length which we need to skip.
		 */
		off = (*sigp)[0] + 1;
		*sigp = *sigp + off;
		*lenp = *lenp - off;
		*freemep = NULL;
		ret = 0;
		break;
	case IKEV2_AUTH_ECDSA_256:
	case IKEV2_AUTH_ECDSA_384:
	case IKEV2_AUTH_ECDSA_521:
		/*
		 * sigp points to concatenation r|s, while EVP_VerifyFinal()
		 * expects the signature as a DER-encoded blob (of the two
		 * values), so we need to convert the signature in a new
		 * buffer (we cannot override the given buffer) and the caller
		 * has to free this buffer ('freeme').
		 */
		if (*lenp < 64 || *lenp > 132 || *lenp % 2)
			goto done;
		bnlen = (*lenp)/2;
		/* sigp points to concatenation: r|s */
		if ((obj = ECDSA_SIG_new()) == NULL ||
		    (r = BN_bin2bn(*sigp, bnlen, NULL)) == NULL ||
		    (s = BN_bin2bn(*sigp+bnlen, bnlen, NULL)) == NULL ||
		    ECDSA_SIG_set0(obj, r, s) == 0 ||
		    (len = i2d_ECDSA_SIG(obj, &ptr)) <= 0)
			goto done;
		r = s = NULL;
		*lenp = len;
		*sigp = ptr;
		*freemep = ptr;
		ptr = NULL;
		ret = 0;
		break;
	default:
		return (0);
	}
 done:
	BN_clear_free(r);
	BN_clear_free(s);
	free(ptr);
	ECDSA_SIG_free(obj);

	return (ret);
}

ssize_t
dsa_verify_final(struct iked_dsa *dsa, void *buf, size_t len)
{
	uint8_t		 sig[EVP_MAX_MD_SIZE];
	uint8_t		*ptr = buf, *freeme = NULL;
	unsigned int	 siglen = sizeof(sig);

	if (dsa->dsa_hmac) {
		if (!HMAC_Final(dsa->dsa_ctx, sig, &siglen))
			return (-1);
		if (siglen != len || memcmp(buf, sig, siglen) != 0)
			return (-1);
	} else {
		if (_dsa_verify_prepare(dsa, &ptr, &len, &freeme) < 0)
			return (-1);
		if (EVP_DigestVerifyFinal(dsa->dsa_ctx, ptr, len) != 1) {
			OPENSSL_free(freeme);
			ca_sslerror(__func__);
			return (-1);
		}
		OPENSSL_free(freeme);
	}

	return (0);
}
