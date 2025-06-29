/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Kerberos 5 crypto
 *
 * Copyright (C) 2025 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 */

#ifndef _CRYPTO_KRB5_H
#define _CRYPTO_KRB5_H

#include <linux/crypto.h>
#include <crypto/aead.h>
#include <crypto/hash.h>

struct crypto_shash;
struct scatterlist;

/*
 * Per Kerberos v5 protocol spec crypto types from the wire.  These get mapped
 * to linux kernel crypto routines.
 */
#define KRB5_ENCTYPE_NULL			0x0000
#define KRB5_ENCTYPE_DES_CBC_CRC		0x0001	/* DES cbc mode with CRC-32 */
#define KRB5_ENCTYPE_DES_CBC_MD4		0x0002	/* DES cbc mode with RSA-MD4 */
#define KRB5_ENCTYPE_DES_CBC_MD5		0x0003	/* DES cbc mode with RSA-MD5 */
#define KRB5_ENCTYPE_DES_CBC_RAW		0x0004	/* DES cbc mode raw */
/* XXX deprecated? */
#define KRB5_ENCTYPE_DES3_CBC_SHA		0x0005	/* DES-3 cbc mode with NIST-SHA */
#define KRB5_ENCTYPE_DES3_CBC_RAW		0x0006	/* DES-3 cbc mode raw */
#define KRB5_ENCTYPE_DES_HMAC_SHA1		0x0008
#define KRB5_ENCTYPE_DES3_CBC_SHA1		0x0010
#define KRB5_ENCTYPE_AES128_CTS_HMAC_SHA1_96	0x0011
#define KRB5_ENCTYPE_AES256_CTS_HMAC_SHA1_96	0x0012
#define KRB5_ENCTYPE_AES128_CTS_HMAC_SHA256_128	0x0013
#define KRB5_ENCTYPE_AES256_CTS_HMAC_SHA384_192	0x0014
#define KRB5_ENCTYPE_ARCFOUR_HMAC		0x0017
#define KRB5_ENCTYPE_ARCFOUR_HMAC_EXP		0x0018
#define KRB5_ENCTYPE_CAMELLIA128_CTS_CMAC	0x0019
#define KRB5_ENCTYPE_CAMELLIA256_CTS_CMAC	0x001a
#define KRB5_ENCTYPE_UNKNOWN			0x01ff

#define KRB5_CKSUMTYPE_CRC32			0x0001
#define KRB5_CKSUMTYPE_RSA_MD4			0x0002
#define KRB5_CKSUMTYPE_RSA_MD4_DES		0x0003
#define KRB5_CKSUMTYPE_DESCBC			0x0004
#define KRB5_CKSUMTYPE_RSA_MD5			0x0007
#define KRB5_CKSUMTYPE_RSA_MD5_DES		0x0008
#define KRB5_CKSUMTYPE_NIST_SHA			0x0009
#define KRB5_CKSUMTYPE_HMAC_SHA1_DES3		0x000c
#define KRB5_CKSUMTYPE_HMAC_SHA1_96_AES128	0x000f
#define KRB5_CKSUMTYPE_HMAC_SHA1_96_AES256	0x0010
#define KRB5_CKSUMTYPE_CMAC_CAMELLIA128		0x0011
#define KRB5_CKSUMTYPE_CMAC_CAMELLIA256		0x0012
#define KRB5_CKSUMTYPE_HMAC_SHA256_128_AES128	0x0013
#define KRB5_CKSUMTYPE_HMAC_SHA384_192_AES256	0x0014
#define KRB5_CKSUMTYPE_HMAC_MD5_ARCFOUR		-138 /* Microsoft md5 hmac cksumtype */

/*
 * Constants used for key derivation
 */
/* from rfc3961 */
#define KEY_USAGE_SEED_CHECKSUM         (0x99)
#define KEY_USAGE_SEED_ENCRYPTION       (0xAA)
#define KEY_USAGE_SEED_INTEGRITY        (0x55)

/*
 * Standard Kerberos error codes.
 */
#define KRB5_PROG_KEYTYPE_NOSUPP		-1765328233

/*
 * Mode of operation.
 */
enum krb5_crypto_mode {
	KRB5_CHECKSUM_MODE,	/* Checksum only */
	KRB5_ENCRYPT_MODE,	/* Fully encrypted, possibly with integrity checksum */
};

struct krb5_buffer {
	unsigned int	len;
	void		*data;
};

/*
 * Kerberos encoding type definition.
 */
struct krb5_enctype {
	int		etype;		/* Encryption (key) type */
	int		ctype;		/* Checksum type */
	const char	*name;		/* "Friendly" name */
	const char	*encrypt_name;	/* Crypto encrypt+checksum name */
	const char	*cksum_name;	/* Crypto checksum name */
	const char	*hash_name;	/* Crypto hash name */
	const char	*derivation_enc; /* Cipher used in key derivation */
	u16		block_len;	/* Length of encryption block */
	u16		conf_len;	/* Length of confounder (normally == block_len) */
	u16		cksum_len;	/* Length of checksum */
	u16		key_bytes;	/* Length of raw key, in bytes */
	u16		key_len;	/* Length of final key, in bytes */
	u16		hash_len;	/* Length of hash in bytes */
	u16		prf_len;	/* Length of PRF() result in bytes */
	u16		Kc_len;		/* Length of Kc in bytes */
	u16		Ke_len;		/* Length of Ke in bytes */
	u16		Ki_len;		/* Length of Ki in bytes */
	bool		keyed_cksum;	/* T if a keyed cksum */

	const struct krb5_crypto_profile *profile;

	int (*random_to_key)(const struct krb5_enctype *krb5,
			     const struct krb5_buffer *in,
			     struct krb5_buffer *out);	/* complete key generation */
};

/*
 * krb5_api.c
 */
const struct krb5_enctype *crypto_krb5_find_enctype(u32 enctype);
size_t crypto_krb5_how_much_buffer(const struct krb5_enctype *krb5,
				   enum krb5_crypto_mode mode,
				   size_t data_size, size_t *_offset);
size_t crypto_krb5_how_much_data(const struct krb5_enctype *krb5,
				 enum krb5_crypto_mode mode,
				 size_t *_buffer_size, size_t *_offset);
void crypto_krb5_where_is_the_data(const struct krb5_enctype *krb5,
				   enum krb5_crypto_mode mode,
				   size_t *_offset, size_t *_len);
struct crypto_aead *crypto_krb5_prepare_encryption(const struct krb5_enctype *krb5,
						   const struct krb5_buffer *TK,
						   u32 usage, gfp_t gfp);
struct crypto_shash *crypto_krb5_prepare_checksum(const struct krb5_enctype *krb5,
						  const struct krb5_buffer *TK,
						  u32 usage, gfp_t gfp);
ssize_t crypto_krb5_encrypt(const struct krb5_enctype *krb5,
			    struct crypto_aead *aead,
			    struct scatterlist *sg, unsigned int nr_sg,
			    size_t sg_len,
			    size_t data_offset, size_t data_len,
			    bool preconfounded);
int crypto_krb5_decrypt(const struct krb5_enctype *krb5,
			struct crypto_aead *aead,
			struct scatterlist *sg, unsigned int nr_sg,
			size_t *_offset, size_t *_len);
ssize_t crypto_krb5_get_mic(const struct krb5_enctype *krb5,
			    struct crypto_shash *shash,
			    const struct krb5_buffer *metadata,
			    struct scatterlist *sg, unsigned int nr_sg,
			    size_t sg_len,
			    size_t data_offset, size_t data_len);
int crypto_krb5_verify_mic(const struct krb5_enctype *krb5,
			   struct crypto_shash *shash,
			   const struct krb5_buffer *metadata,
			   struct scatterlist *sg, unsigned int nr_sg,
			   size_t *_offset, size_t *_len);

/*
 * krb5_kdf.c
 */
int crypto_krb5_calc_PRFplus(const struct krb5_enctype *krb5,
			     const struct krb5_buffer *K,
			     unsigned int L,
			     const struct krb5_buffer *S,
			     struct krb5_buffer *result,
			     gfp_t gfp);

#endif /* _CRYPTO_KRB5_H */
