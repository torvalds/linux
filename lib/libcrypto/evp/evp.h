/* $OpenBSD: evp.h,v 1.138 2025/07/02 06:36:52 tb Exp $ */
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

#ifndef HEADER_ENVELOPE_H
#define HEADER_ENVELOPE_H

#include <openssl/opensslconf.h>

#include <openssl/ossl_typ.h>

#ifndef OPENSSL_NO_BIO
#include <openssl/bio.h>
#endif

/*
#define EVP_RC2_KEY_SIZE		16
#define EVP_RC4_KEY_SIZE		16
#define EVP_BLOWFISH_KEY_SIZE		16
#define EVP_CAST5_KEY_SIZE		16
#define EVP_RC5_32_12_16_KEY_SIZE	16
*/
#define EVP_MAX_MD_SIZE			64	/* longest known is SHA512 */
#define EVP_MAX_KEY_LENGTH		64
#define EVP_MAX_IV_LENGTH		16
#define EVP_MAX_BLOCK_LENGTH		32

#define PKCS5_SALT_LEN			8
/* Default PKCS#5 iteration count */
#define PKCS5_DEFAULT_ITER		2048

#include <openssl/objects.h>

#define EVP_PK_RSA	0x0001
#define EVP_PK_DSA	0x0002
#define EVP_PK_DH	0x0004
#define EVP_PK_EC	0x0008
#define EVP_PKT_SIGN	0x0010
#define EVP_PKT_ENC	0x0020
#define EVP_PKT_EXCH	0x0040
#define EVP_PKS_RSA	0x0100
#define EVP_PKS_DSA	0x0200
#define EVP_PKS_EC	0x0400
#define EVP_PKT_EXP	0x1000 /* <= 512 bit key */

#define EVP_PKEY_NONE		NID_undef
#define EVP_PKEY_RSA		NID_rsaEncryption
#define EVP_PKEY_RSA_PSS	NID_rsassaPss
#define EVP_PKEY_RSA2		NID_rsa
#define EVP_PKEY_DSA		NID_dsa
#define EVP_PKEY_DSA1		NID_dsa_2
#define EVP_PKEY_DSA2		NID_dsaWithSHA
#define EVP_PKEY_DSA3		NID_dsaWithSHA1
#define EVP_PKEY_DSA4		NID_dsaWithSHA1_2
#define EVP_PKEY_DH		NID_dhKeyAgreement
#define EVP_PKEY_EC		NID_X9_62_id_ecPublicKey
#define EVP_PKEY_GOSTR01	NID_id_GostR3410_2001
#define EVP_PKEY_GOSTIMIT	NID_id_Gost28147_89_MAC
#define EVP_PKEY_HMAC		NID_hmac
#define EVP_PKEY_CMAC		NID_cmac
#define EVP_PKEY_HKDF		NID_hkdf
#define EVP_PKEY_TLS1_PRF	NID_tls1_prf
#define EVP_PKEY_GOSTR12_256	NID_id_tc26_gost3410_2012_256
#define EVP_PKEY_GOSTR12_512	NID_id_tc26_gost3410_2012_512
#define EVP_PKEY_ED25519	NID_ED25519
#define EVP_PKEY_X25519		NID_X25519

#ifdef	__cplusplus
extern "C" {
#endif

#define EVP_PKEY_MO_SIGN	0x0001
#define EVP_PKEY_MO_VERIFY	0x0002
#define EVP_PKEY_MO_ENCRYPT	0x0004
#define EVP_PKEY_MO_DECRYPT	0x0008

#ifndef EVP_MD
#define EVP_MD_FLAG_ONESHOT	0x0001 /* digest can only handle a single
					* block */

/* DigestAlgorithmIdentifier flags... */

#define EVP_MD_FLAG_DIGALGID_MASK		0x0018

/* NULL or absent parameter accepted. Use NULL */

#define EVP_MD_FLAG_DIGALGID_NULL		0x0000

/* NULL or absent parameter accepted. Use NULL for PKCS#1 otherwise absent */

#define EVP_MD_FLAG_DIGALGID_ABSENT		0x0008

/* Custom handling via ctrl */

#define EVP_MD_FLAG_DIGALGID_CUSTOM		0x0018

#define EVP_MD_FLAG_FIPS	0x0400 /* Note if suitable for use in FIPS mode */

/* Digest ctrls */

#define	EVP_MD_CTRL_DIGALGID			0x1
#define	EVP_MD_CTRL_MICALG			0x2
#define	EVP_MD_CTRL_SET_KEY			0x3
#define	EVP_MD_CTRL_GOST_SET_SBOX		0x4

/* Minimum Algorithm specific ctrl value */

#define	EVP_MD_CTRL_ALG_CTRL			0x1000

#endif /* !EVP_MD */

/* values for EVP_MD_CTX flags */

#define EVP_MD_CTX_FLAG_ONESHOT		0x0001 /* digest update will be called
						* once only */
#define EVP_MD_CTX_FLAG_CLEANED		0x0002 /* context has already been
						* cleaned */
#define EVP_MD_CTX_FLAG_REUSE		0x0004 /* Don't free up ctx->md_data
						* in EVP_MD_CTX_cleanup */
/* FIPS and pad options are ignored in 1.0.0, definitions are here
 * so we don't accidentally reuse the values for other purposes.
 */

#define EVP_MD_CTX_FLAG_NON_FIPS_ALLOW	0x0008	/* Allow use of non FIPS digest
						 * in FIPS mode */

/* The following PAD options are also currently ignored in 1.0.0, digest
 * parameters are handled through EVP_DigestSign*() and EVP_DigestVerify*()
 * instead.
 */
#define EVP_MD_CTX_FLAG_PAD_MASK	0xF0	/* RSA mode to use */
#define EVP_MD_CTX_FLAG_PAD_PKCS1	0x00	/* PKCS#1 v1.5 mode */
#define EVP_MD_CTX_FLAG_PAD_PSS		0x20	/* PSS mode */

#define EVP_MD_CTX_FLAG_NO_INIT		0x0100 /* Don't initialize md_data */

/* Values for cipher flags */

/* Modes for ciphers */

#define		EVP_CIPH_STREAM_CIPHER		0x0
#define		EVP_CIPH_ECB_MODE		0x1
#define		EVP_CIPH_CBC_MODE		0x2
#define		EVP_CIPH_CFB_MODE		0x3
#define		EVP_CIPH_OFB_MODE		0x4
#define		EVP_CIPH_CTR_MODE		0x5
#define		EVP_CIPH_GCM_MODE		0x6
#define		EVP_CIPH_CCM_MODE		0x7
#define		EVP_CIPH_XTS_MODE		0x10001
#define		EVP_CIPH_WRAP_MODE		0x10002
#define		EVP_CIPH_MODE			0xF0007
/* Set if variable length cipher */
#define		EVP_CIPH_VARIABLE_LENGTH	0x8
/* Set if the iv handling should be done by the cipher itself */
#define		EVP_CIPH_CUSTOM_IV		0x10
/* Set if the cipher's init() function should be called if key is NULL */
#define		EVP_CIPH_ALWAYS_CALL_INIT	0x20
/* Call ctrl() to init cipher parameters */
#define		EVP_CIPH_CTRL_INIT		0x40
/* Don't use standard block padding */
#define		EVP_CIPH_NO_PADDING		0x100
/* cipher handles random key generation */
#define		EVP_CIPH_RAND_KEY		0x200
/* cipher has its own additional copying logic */
#define		EVP_CIPH_CUSTOM_COPY		0x400
/* Allow use default ASN1 get/set iv */
#define		EVP_CIPH_FLAG_DEFAULT_ASN1	0x1000
/* Buffer length in bits not bytes: CFB1 mode only */
#define		EVP_CIPH_FLAG_LENGTH_BITS	0x2000
/* Note if suitable for use in FIPS mode */
#define		EVP_CIPH_FLAG_FIPS		0x4000
/* Allow non FIPS cipher in FIPS mode */
#define		EVP_CIPH_FLAG_NON_FIPS_ALLOW	0x8000
/* Cipher handles any and all padding logic as well
 * as finalisation.
 */
#define		EVP_CIPH_FLAG_CUSTOM_CIPHER	0x100000
#define		EVP_CIPH_FLAG_AEAD_CIPHER	0x200000

/*
 * Cipher context flag to indicate that we can handle wrap mode: if allowed in
 * older applications, it could overflow buffers.
 */
#define		EVP_CIPHER_CTX_FLAG_WRAP_ALLOW	0x1

/* ctrl() values */

#define		EVP_CTRL_INIT			0x0
#define		EVP_CTRL_GET_RC2_KEY_BITS	0x2
#define		EVP_CTRL_SET_RC2_KEY_BITS	0x3
#define		EVP_CTRL_GET_RC5_ROUNDS		0x4
#define		EVP_CTRL_SET_RC5_ROUNDS		0x5
#define		EVP_CTRL_RAND_KEY		0x6
#define		EVP_CTRL_PBE_PRF_NID		0x7
#define		EVP_CTRL_COPY			0x8
#define		EVP_CTRL_AEAD_SET_IVLEN		0x9
#define		EVP_CTRL_AEAD_GET_TAG		0x10
#define		EVP_CTRL_AEAD_SET_TAG		0x11
#define		EVP_CTRL_AEAD_SET_IV_FIXED	0x12
#define		EVP_CTRL_GCM_SET_IVLEN		EVP_CTRL_AEAD_SET_IVLEN
#define		EVP_CTRL_GCM_GET_TAG		EVP_CTRL_AEAD_GET_TAG
#define		EVP_CTRL_GCM_SET_TAG		EVP_CTRL_AEAD_SET_TAG
#define		EVP_CTRL_GCM_SET_IV_FIXED	EVP_CTRL_AEAD_SET_IV_FIXED
#define		EVP_CTRL_GCM_IV_GEN		0x13
#define		EVP_CTRL_CCM_SET_IVLEN		EVP_CTRL_AEAD_SET_IVLEN
#define		EVP_CTRL_CCM_GET_TAG		EVP_CTRL_AEAD_GET_TAG
#define		EVP_CTRL_CCM_SET_TAG		EVP_CTRL_AEAD_SET_TAG
#define		EVP_CTRL_CCM_SET_L		0x14
#define		EVP_CTRL_CCM_SET_MSGLEN		0x15
/* AEAD cipher deduces payload length and returns number of bytes
 * required to store MAC and eventual padding. Subsequent call to
 * EVP_Cipher even appends/verifies MAC.
 */
#define		EVP_CTRL_AEAD_TLS1_AAD		0x16
/* Used by composite AEAD ciphers, no-op in GCM, CCM... */
#define		EVP_CTRL_AEAD_SET_MAC_KEY	0x17
/* Set the GCM invocation field, decrypt only */
#define		EVP_CTRL_GCM_SET_IV_INV		0x18
/* Set the S-BOX NID for GOST ciphers */
#define		EVP_CTRL_GOST_SET_SBOX		0x19

/* GCM TLS constants */
/* Length of fixed part of IV derived from PRF */
#define EVP_GCM_TLS_FIXED_IV_LEN			4
/* Length of explicit part of IV part of TLS records */
#define EVP_GCM_TLS_EXPLICIT_IV_LEN			8
/* Length of tag for TLS */
#define EVP_GCM_TLS_TAG_LEN				16

/* CCM TLS constants */
/* Length of fixed part of IV derived from PRF */
#define EVP_CCM_TLS_FIXED_IV_LEN			4
/* Length of explicit part of IV part of TLS records */
#define EVP_CCM_TLS_EXPLICIT_IV_LEN			8
/* Total length of CCM IV length for TLS */
#define EVP_CCM_TLS_IV_LEN				12
/* Length of tag for TLS */
#define EVP_CCM_TLS_TAG_LEN				16
/* Length of CCM8 tag for TLS */
#define EVP_CCM8_TLS_TAG_LEN				8

/* Length of tag for TLS */
#define EVP_CHACHAPOLY_TLS_TAG_LEN			16

/* XXX - do we want to expose these? */
#if defined(LIBRESSL_INTERNAL)
#define ED25519_KEYLEN					32
#define X25519_KEYLEN					32
#endif

typedef struct evp_cipher_info_st {
	const EVP_CIPHER *cipher;
	unsigned char iv[EVP_MAX_IV_LENGTH];
} EVP_CIPHER_INFO;

/* Password based encryption function */
typedef int EVP_PBE_KEYGEN(EVP_CIPHER_CTX *ctx, const char *pass, int passlen,
    ASN1_TYPE *param, const EVP_CIPHER *cipher, const EVP_MD *md, int en_de);

#ifndef OPENSSL_NO_RSA
#define EVP_PKEY_assign_RSA(pkey,rsa) EVP_PKEY_assign((pkey),EVP_PKEY_RSA,\
					(char *)(rsa))
#endif

#ifndef OPENSSL_NO_DSA
#define EVP_PKEY_assign_DSA(pkey,dsa) EVP_PKEY_assign((pkey),EVP_PKEY_DSA,\
					(char *)(dsa))
#endif

#ifndef OPENSSL_NO_DH
#define EVP_PKEY_assign_DH(pkey,dh) EVP_PKEY_assign((pkey),EVP_PKEY_DH,\
					(char *)(dh))
#endif

#ifndef OPENSSL_NO_EC
#define EVP_PKEY_assign_EC_KEY(pkey,eckey) EVP_PKEY_assign((pkey),EVP_PKEY_EC,\
                                        (char *)(eckey))
#endif

/* Add some extra combinations */
#define EVP_get_digestbynid(a) EVP_get_digestbyname(OBJ_nid2sn(a))
#define EVP_get_digestbyobj(a) EVP_get_digestbynid(OBJ_obj2nid(a))
#define EVP_get_cipherbynid(a) EVP_get_cipherbyname(OBJ_nid2sn(a))
#define EVP_get_cipherbyobj(a) EVP_get_cipherbynid(OBJ_obj2nid(a))

int EVP_MD_type(const EVP_MD *md);
#define EVP_MD_nid(e)			EVP_MD_type(e)
#define EVP_MD_name(e)			OBJ_nid2sn(EVP_MD_nid(e))
int EVP_MD_pkey_type(const EVP_MD *md);
int EVP_MD_size(const EVP_MD *md);
int EVP_MD_block_size(const EVP_MD *md);
unsigned long EVP_MD_flags(const EVP_MD *md);

const EVP_MD *EVP_MD_CTX_md(const EVP_MD_CTX *ctx);
void *EVP_MD_CTX_md_data(const EVP_MD_CTX *ctx);
EVP_PKEY_CTX *EVP_MD_CTX_pkey_ctx(const EVP_MD_CTX *ctx);
void EVP_MD_CTX_set_pkey_ctx(EVP_MD_CTX *ctx, EVP_PKEY_CTX *pctx);
#define EVP_MD_CTX_size(e)		EVP_MD_size(EVP_MD_CTX_md(e))
#define EVP_MD_CTX_block_size(e)	EVP_MD_block_size(EVP_MD_CTX_md(e))
#define EVP_MD_CTX_type(e)		EVP_MD_type(EVP_MD_CTX_md(e))

int EVP_CIPHER_nid(const EVP_CIPHER *cipher);
#define EVP_CIPHER_name(e)		OBJ_nid2sn(EVP_CIPHER_nid(e))
int EVP_CIPHER_block_size(const EVP_CIPHER *cipher);
int EVP_CIPHER_key_length(const EVP_CIPHER *cipher);
int EVP_CIPHER_iv_length(const EVP_CIPHER *cipher);
unsigned long EVP_CIPHER_flags(const EVP_CIPHER *cipher);
#define EVP_CIPHER_mode(e)		(EVP_CIPHER_flags(e) & EVP_CIPH_MODE)

const EVP_CIPHER * EVP_CIPHER_CTX_cipher(const EVP_CIPHER_CTX *ctx);
int EVP_CIPHER_CTX_encrypting(const EVP_CIPHER_CTX *ctx);
int EVP_CIPHER_CTX_nid(const EVP_CIPHER_CTX *ctx);
int EVP_CIPHER_CTX_block_size(const EVP_CIPHER_CTX *ctx);
int EVP_CIPHER_CTX_key_length(const EVP_CIPHER_CTX *ctx);
int EVP_CIPHER_CTX_iv_length(const EVP_CIPHER_CTX *ctx);
int EVP_CIPHER_CTX_get_iv(const EVP_CIPHER_CTX *ctx,
    unsigned char *iv, size_t len);
int EVP_CIPHER_CTX_set_iv(EVP_CIPHER_CTX *ctx,
    const unsigned char *iv, size_t len);
int EVP_CIPHER_CTX_copy(EVP_CIPHER_CTX *out, const EVP_CIPHER_CTX *in);
void *EVP_CIPHER_CTX_get_app_data(const EVP_CIPHER_CTX *ctx);
void EVP_CIPHER_CTX_set_app_data(EVP_CIPHER_CTX *ctx, void *data);
void *EVP_CIPHER_CTX_get_cipher_data(const EVP_CIPHER_CTX *ctx);
void *EVP_CIPHER_CTX_set_cipher_data(EVP_CIPHER_CTX *ctx, void *cipher_data);
unsigned char *EVP_CIPHER_CTX_buf_noconst(EVP_CIPHER_CTX *ctx);
#define EVP_CIPHER_CTX_type(c)         EVP_CIPHER_type(EVP_CIPHER_CTX_cipher(c))
unsigned long EVP_CIPHER_CTX_flags(const EVP_CIPHER_CTX *ctx);
#define EVP_CIPHER_CTX_mode(e)		(EVP_CIPHER_CTX_flags(e) & EVP_CIPH_MODE)

EVP_CIPHER *EVP_CIPHER_meth_new(int cipher_type, int block_size, int key_len);
EVP_CIPHER *EVP_CIPHER_meth_dup(const EVP_CIPHER *cipher);
void EVP_CIPHER_meth_free(EVP_CIPHER *cipher);

int EVP_CIPHER_meth_set_iv_length(EVP_CIPHER *cipher, int iv_len);
int EVP_CIPHER_meth_set_flags(EVP_CIPHER *cipher, unsigned long flags);
int EVP_CIPHER_meth_set_impl_ctx_size(EVP_CIPHER *cipher, int ctx_size);
int EVP_CIPHER_meth_set_init(EVP_CIPHER *cipher,
    int (*init)(EVP_CIPHER_CTX *ctx, const unsigned char *key,
    const unsigned char *iv, int enc));
int EVP_CIPHER_meth_set_do_cipher(EVP_CIPHER *cipher,
    int (*do_cipher)(EVP_CIPHER_CTX *ctx, unsigned char *out,
    const unsigned char *in, size_t inl));
int EVP_CIPHER_meth_set_cleanup(EVP_CIPHER *cipher,
    int (*cleanup)(EVP_CIPHER_CTX *));
int EVP_CIPHER_meth_set_set_asn1_params(EVP_CIPHER *cipher,
    int (*set_asn1_parameters)(EVP_CIPHER_CTX *, ASN1_TYPE *));
int EVP_CIPHER_meth_set_get_asn1_params(EVP_CIPHER *cipher,
    int (*get_asn1_parameters)(EVP_CIPHER_CTX *, ASN1_TYPE *));
int EVP_CIPHER_meth_set_ctrl(EVP_CIPHER *cipher,
    int (*ctrl)(EVP_CIPHER_CTX *, int type, int arg, void *ptr));

EVP_PKEY *EVP_PKEY_new_raw_private_key(int type, ENGINE *engine,
    const unsigned char *private_key, size_t len);
EVP_PKEY *EVP_PKEY_new_raw_public_key(int type, ENGINE *engine,
    const unsigned char *public_key, size_t len);
int EVP_PKEY_get_raw_private_key(const EVP_PKEY *pkey,
    unsigned char *out_private_key, size_t *out_len);
int EVP_PKEY_get_raw_public_key(const EVP_PKEY *pkey,
    unsigned char *out_public_key, size_t *out_len);

#define EVP_ENCODE_LENGTH(l)	(((l+2)/3*4)+(l/48+1)*2+80)
#define EVP_DECODE_LENGTH(l)	((l+3)/4*3+80)

#define EVP_SignInit_ex(a,b,c)		EVP_DigestInit_ex(a,b,c)
#define EVP_SignInit(a,b)		EVP_DigestInit(a,b)
#define EVP_SignUpdate(a,b,c)		EVP_DigestUpdate(a,b,c)
#define	EVP_VerifyInit_ex(a,b,c)	EVP_DigestInit_ex(a,b,c)
#define	EVP_VerifyInit(a,b)		EVP_DigestInit(a,b)
#define	EVP_VerifyUpdate(a,b,c)		EVP_DigestUpdate(a,b,c)
#define EVP_OpenUpdate(a,b,c,d,e)	EVP_DecryptUpdate(a,b,c,d,e)
#define EVP_SealUpdate(a,b,c,d,e)	EVP_EncryptUpdate(a,b,c,d,e)
#define EVP_DigestSignUpdate(a,b,c)	EVP_DigestUpdate(a,b,c)
#define EVP_DigestVerifyUpdate(a,b,c)	EVP_DigestUpdate(a,b,c)

#define BIO_set_md(b,md)		BIO_ctrl(b,BIO_C_SET_MD,0,(char *)md)
#define BIO_get_md(b,mdp)		BIO_ctrl(b,BIO_C_GET_MD,0,(char *)mdp)
#define BIO_get_md_ctx(b,mdcp)     BIO_ctrl(b,BIO_C_GET_MD_CTX,0,(char *)mdcp)
#define BIO_set_md_ctx(b,mdcp)     BIO_ctrl(b,BIO_C_SET_MD_CTX,0,(char *)mdcp)
#define BIO_get_cipher_status(b)	BIO_ctrl(b,BIO_C_GET_CIPHER_STATUS,0,NULL)
#define BIO_get_cipher_ctx(b,c_pp)	BIO_ctrl(b,BIO_C_GET_CIPHER_CTX,0,(char *)c_pp)

int EVP_Cipher(EVP_CIPHER_CTX *c, unsigned char *out, const unsigned char *in,
    unsigned int inl);

EVP_MD_CTX *EVP_MD_CTX_new(void);
void EVP_MD_CTX_free(EVP_MD_CTX *ctx);
int EVP_MD_CTX_init(EVP_MD_CTX *ctx);
int EVP_MD_CTX_reset(EVP_MD_CTX *ctx);
EVP_MD_CTX *EVP_MD_CTX_create(void);
void EVP_MD_CTX_destroy(EVP_MD_CTX *ctx);
int EVP_MD_CTX_cleanup(EVP_MD_CTX *ctx);
int EVP_MD_CTX_copy_ex(EVP_MD_CTX *out, const EVP_MD_CTX *in);
void EVP_MD_CTX_set_flags(EVP_MD_CTX *ctx, int flags);
void EVP_MD_CTX_clear_flags(EVP_MD_CTX *ctx, int flags);
int EVP_MD_CTX_ctrl(EVP_MD_CTX *ctx, int type, int arg, void *ptr);
int EVP_MD_CTX_test_flags(const EVP_MD_CTX *ctx, int flags);

int EVP_DigestInit_ex(EVP_MD_CTX *ctx, const EVP_MD *type, ENGINE *impl);
int EVP_DigestUpdate(EVP_MD_CTX *ctx, const void *d, size_t cnt);
int EVP_DigestFinal_ex(EVP_MD_CTX *ctx, unsigned char *md, unsigned int *s);
int EVP_Digest(const void *data, size_t count, unsigned char *md,
    unsigned int *size, const EVP_MD *type, ENGINE *impl);

int EVP_MD_CTX_copy(EVP_MD_CTX *out, const EVP_MD_CTX *in);
int EVP_DigestInit(EVP_MD_CTX *ctx, const EVP_MD *type);
int EVP_DigestFinal(EVP_MD_CTX *ctx, unsigned char *md, unsigned int *s);

int EVP_read_pw_string(char *buf, int length, const char *prompt, int verify);
int EVP_read_pw_string_min(char *buf, int minlen, int maxlen,
    const char *prompt, int verify);
void EVP_set_pw_prompt(const char *prompt);
char *EVP_get_pw_prompt(void);

int EVP_BytesToKey(const EVP_CIPHER *type, const EVP_MD *md,
    const unsigned char *salt, const unsigned char *data, int datal, int count,
    unsigned char *key, unsigned char *iv);

void EVP_CIPHER_CTX_set_flags(EVP_CIPHER_CTX *ctx, int flags);
void EVP_CIPHER_CTX_clear_flags(EVP_CIPHER_CTX *ctx, int flags);
int EVP_CIPHER_CTX_test_flags(const EVP_CIPHER_CTX *ctx, int flags);

int EVP_EncryptInit(EVP_CIPHER_CTX *ctx, const EVP_CIPHER *cipher,
    const unsigned char *key, const unsigned char *iv);
int EVP_EncryptInit_ex(EVP_CIPHER_CTX *ctx, const EVP_CIPHER *cipher,
    ENGINE *impl, const unsigned char *key, const unsigned char *iv);
int EVP_EncryptUpdate(EVP_CIPHER_CTX *ctx, unsigned char *out, int *outl,
    const unsigned char *in, int inl);
int EVP_EncryptFinal_ex(EVP_CIPHER_CTX *ctx, unsigned char *out, int *outl);
int EVP_EncryptFinal(EVP_CIPHER_CTX *ctx, unsigned char *out, int *outl);

int EVP_DecryptInit(EVP_CIPHER_CTX *ctx, const EVP_CIPHER *cipher,
    const unsigned char *key, const unsigned char *iv);
int EVP_DecryptInit_ex(EVP_CIPHER_CTX *ctx, const EVP_CIPHER *cipher,
    ENGINE *impl, const unsigned char *key, const unsigned char *iv);
int EVP_DecryptUpdate(EVP_CIPHER_CTX *ctx, unsigned char *out, int *outl,
    const unsigned char *in, int inl);
int EVP_DecryptFinal_ex(EVP_CIPHER_CTX *ctx, unsigned char *outm, int *outl);
int EVP_DecryptFinal(EVP_CIPHER_CTX *ctx, unsigned char *outm, int *outl);

int EVP_CipherInit(EVP_CIPHER_CTX *ctx, const EVP_CIPHER *cipher,
    const unsigned char *key, const unsigned char *iv, int enc);
int EVP_CipherInit_ex(EVP_CIPHER_CTX *ctx, const EVP_CIPHER *cipher,
    ENGINE *impl, const unsigned char *key, const unsigned char *iv, int enc);
int EVP_CipherUpdate(EVP_CIPHER_CTX *ctx, unsigned char *out, int *outl,
    const unsigned char *in, int inl);
int EVP_CipherFinal_ex(EVP_CIPHER_CTX *ctx, unsigned char *outm, int *outl);
int EVP_CipherFinal(EVP_CIPHER_CTX *ctx, unsigned char *outm, int *outl);

int EVP_SignFinal(EVP_MD_CTX *ctx, unsigned char *md, unsigned int *s,
    EVP_PKEY *pkey);

int EVP_VerifyFinal(EVP_MD_CTX *ctx, const unsigned char *sigbuf,
    unsigned int siglen, EVP_PKEY *pkey);

int EVP_DigestSignInit(EVP_MD_CTX *ctx, EVP_PKEY_CTX **pctx,
    const EVP_MD *type, ENGINE *e, EVP_PKEY *pkey);
int EVP_DigestSignFinal(EVP_MD_CTX *ctx, unsigned char *sigret, size_t *siglen);

int EVP_DigestSign(EVP_MD_CTX *ctx, unsigned char *sigret, size_t *siglen,
    const unsigned char *tbs, size_t tbslen);

int EVP_DigestVerifyInit(EVP_MD_CTX *ctx, EVP_PKEY_CTX **pctx,
    const EVP_MD *type, ENGINE *e, EVP_PKEY *pkey);
int EVP_DigestVerifyFinal(EVP_MD_CTX *ctx, const unsigned char *sig,
    size_t siglen);

int EVP_DigestVerify(EVP_MD_CTX *ctx, const unsigned char *sigret,
    size_t siglen, const unsigned char *tbs, size_t tbslen);

int EVP_OpenInit(EVP_CIPHER_CTX *ctx, const EVP_CIPHER *type,
    const unsigned char *ek, int ekl, const unsigned char *iv, EVP_PKEY *priv);
int EVP_OpenFinal(EVP_CIPHER_CTX *ctx, unsigned char *out, int *outl);

int EVP_SealInit(EVP_CIPHER_CTX *ctx, const EVP_CIPHER *type,
    unsigned char **ek, int *ekl, unsigned char *iv, EVP_PKEY **pubk,
    int npubk);
int EVP_SealFinal(EVP_CIPHER_CTX *ctx, unsigned char *out, int *outl);

EVP_ENCODE_CTX *EVP_ENCODE_CTX_new(void);
void EVP_ENCODE_CTX_free(EVP_ENCODE_CTX *ctx);
void EVP_EncodeInit(EVP_ENCODE_CTX *ctx);
int EVP_EncodeUpdate(EVP_ENCODE_CTX *ctx, unsigned char *out, int *outl,
    const unsigned char *in, int inl);
void EVP_EncodeFinal(EVP_ENCODE_CTX *ctx, unsigned char *out, int *outl);
int EVP_EncodeBlock(unsigned char *t, const unsigned char *f, int n);

void EVP_DecodeInit(EVP_ENCODE_CTX *ctx);
int EVP_DecodeUpdate(EVP_ENCODE_CTX *ctx, unsigned char *out, int *outl,
    const unsigned char *in, int inl);
int EVP_DecodeFinal(EVP_ENCODE_CTX *ctx, unsigned char *out, int *outl);
int EVP_DecodeBlock(unsigned char *t, const unsigned char *f, int n);

int EVP_CIPHER_CTX_init(EVP_CIPHER_CTX *a);
int EVP_CIPHER_CTX_cleanup(EVP_CIPHER_CTX *a);
EVP_CIPHER_CTX *EVP_CIPHER_CTX_new(void);
void EVP_CIPHER_CTX_free(EVP_CIPHER_CTX *a);
int EVP_CIPHER_CTX_reset(EVP_CIPHER_CTX *a);
int EVP_CIPHER_CTX_set_key_length(EVP_CIPHER_CTX *x, int keylen);
int EVP_CIPHER_CTX_set_padding(EVP_CIPHER_CTX *c, int pad);
int EVP_CIPHER_CTX_ctrl(EVP_CIPHER_CTX *ctx, int type, int arg, void *ptr);
int EVP_CIPHER_CTX_rand_key(EVP_CIPHER_CTX *ctx, unsigned char *key);

#ifndef OPENSSL_NO_BIO
const BIO_METHOD *BIO_f_md(void);
const BIO_METHOD *BIO_f_base64(void);
const BIO_METHOD *BIO_f_cipher(void);
int BIO_set_cipher(BIO *b, const EVP_CIPHER *c, const unsigned char *k,
    const unsigned char *i, int enc);
#endif

const EVP_MD *EVP_md_null(void);
#ifndef OPENSSL_NO_MD4
const EVP_MD *EVP_md4(void);
#endif
#ifndef OPENSSL_NO_MD5
const EVP_MD *EVP_md5(void);
const EVP_MD *EVP_md5_sha1(void);
#endif
#ifndef OPENSSL_NO_SHA
const EVP_MD *EVP_sha1(void);
#endif
#ifndef OPENSSL_NO_SHA256
const EVP_MD *EVP_sha224(void);
const EVP_MD *EVP_sha256(void);
#endif
#ifndef OPENSSL_NO_SHA512
const EVP_MD *EVP_sha384(void);
const EVP_MD *EVP_sha512(void);
const EVP_MD *EVP_sha512_224(void);
const EVP_MD *EVP_sha512_256(void);
#endif
#ifndef OPENSSL_NO_SHA3
const EVP_MD *EVP_sha3_224(void);
const EVP_MD *EVP_sha3_256(void);
const EVP_MD *EVP_sha3_384(void);
const EVP_MD *EVP_sha3_512(void);
#endif
#ifndef OPENSSL_NO_SM3
const EVP_MD *EVP_sm3(void);
#endif
#ifndef OPENSSL_NO_RIPEMD
const EVP_MD *EVP_ripemd160(void);
#endif
const EVP_CIPHER *EVP_enc_null(void);		/* does nothing :-) */
#ifndef OPENSSL_NO_DES
const EVP_CIPHER *EVP_des_ecb(void);
const EVP_CIPHER *EVP_des_ede(void);
const EVP_CIPHER *EVP_des_ede3(void);
const EVP_CIPHER *EVP_des_ede_ecb(void);
const EVP_CIPHER *EVP_des_ede3_ecb(void);
const EVP_CIPHER *EVP_des_cfb64(void);
# define EVP_des_cfb EVP_des_cfb64
const EVP_CIPHER *EVP_des_cfb1(void);
const EVP_CIPHER *EVP_des_cfb8(void);
const EVP_CIPHER *EVP_des_ede_cfb64(void);
# define EVP_des_ede_cfb EVP_des_ede_cfb64
const EVP_CIPHER *EVP_des_ede3_cfb64(void);
# define EVP_des_ede3_cfb EVP_des_ede3_cfb64
const EVP_CIPHER *EVP_des_ede3_cfb1(void);
const EVP_CIPHER *EVP_des_ede3_cfb8(void);
const EVP_CIPHER *EVP_des_ofb(void);
const EVP_CIPHER *EVP_des_ede_ofb(void);
const EVP_CIPHER *EVP_des_ede3_ofb(void);
const EVP_CIPHER *EVP_des_cbc(void);
const EVP_CIPHER *EVP_des_ede_cbc(void);
const EVP_CIPHER *EVP_des_ede3_cbc(void);
const EVP_CIPHER *EVP_desx_cbc(void);
#endif
#ifndef OPENSSL_NO_RC4
const EVP_CIPHER *EVP_rc4(void);
const EVP_CIPHER *EVP_rc4_40(void);
#endif
#ifndef OPENSSL_NO_IDEA
const EVP_CIPHER *EVP_idea_ecb(void);
const EVP_CIPHER *EVP_idea_cfb64(void);
# define EVP_idea_cfb EVP_idea_cfb64
const EVP_CIPHER *EVP_idea_ofb(void);
const EVP_CIPHER *EVP_idea_cbc(void);
#endif
#ifndef OPENSSL_NO_RC2
const EVP_CIPHER *EVP_rc2_ecb(void);
const EVP_CIPHER *EVP_rc2_cbc(void);
const EVP_CIPHER *EVP_rc2_40_cbc(void);
const EVP_CIPHER *EVP_rc2_64_cbc(void);
const EVP_CIPHER *EVP_rc2_cfb64(void);
# define EVP_rc2_cfb EVP_rc2_cfb64
const EVP_CIPHER *EVP_rc2_ofb(void);
#endif
#ifndef OPENSSL_NO_BF
const EVP_CIPHER *EVP_bf_ecb(void);
const EVP_CIPHER *EVP_bf_cbc(void);
const EVP_CIPHER *EVP_bf_cfb64(void);
# define EVP_bf_cfb EVP_bf_cfb64
const EVP_CIPHER *EVP_bf_ofb(void);
#endif
#ifndef OPENSSL_NO_CAST
const EVP_CIPHER *EVP_cast5_ecb(void);
const EVP_CIPHER *EVP_cast5_cbc(void);
const EVP_CIPHER *EVP_cast5_cfb64(void);
# define EVP_cast5_cfb EVP_cast5_cfb64
const EVP_CIPHER *EVP_cast5_ofb(void);
#endif
#ifndef OPENSSL_NO_AES
const EVP_CIPHER *EVP_aes_128_ecb(void);
const EVP_CIPHER *EVP_aes_128_cbc(void);
const EVP_CIPHER *EVP_aes_128_cfb1(void);
const EVP_CIPHER *EVP_aes_128_cfb8(void);
const EVP_CIPHER *EVP_aes_128_cfb128(void);
# define EVP_aes_128_cfb EVP_aes_128_cfb128
const EVP_CIPHER *EVP_aes_128_ofb(void);
const EVP_CIPHER *EVP_aes_128_ctr(void);
const EVP_CIPHER *EVP_aes_128_ccm(void);
const EVP_CIPHER *EVP_aes_128_gcm(void);
const EVP_CIPHER *EVP_aes_128_wrap(void);
const EVP_CIPHER *EVP_aes_128_xts(void);
const EVP_CIPHER *EVP_aes_192_ecb(void);
const EVP_CIPHER *EVP_aes_192_cbc(void);
const EVP_CIPHER *EVP_aes_192_cfb1(void);
const EVP_CIPHER *EVP_aes_192_cfb8(void);
const EVP_CIPHER *EVP_aes_192_cfb128(void);
# define EVP_aes_192_cfb EVP_aes_192_cfb128
const EVP_CIPHER *EVP_aes_192_ofb(void);
const EVP_CIPHER *EVP_aes_192_ctr(void);
const EVP_CIPHER *EVP_aes_192_ccm(void);
const EVP_CIPHER *EVP_aes_192_gcm(void);
const EVP_CIPHER *EVP_aes_192_wrap(void);
const EVP_CIPHER *EVP_aes_256_ecb(void);
const EVP_CIPHER *EVP_aes_256_cbc(void);
const EVP_CIPHER *EVP_aes_256_cfb1(void);
const EVP_CIPHER *EVP_aes_256_cfb8(void);
const EVP_CIPHER *EVP_aes_256_cfb128(void);
# define EVP_aes_256_cfb EVP_aes_256_cfb128
const EVP_CIPHER *EVP_aes_256_ofb(void);
const EVP_CIPHER *EVP_aes_256_ctr(void);
const EVP_CIPHER *EVP_aes_256_ccm(void);
const EVP_CIPHER *EVP_aes_256_gcm(void);
const EVP_CIPHER *EVP_aes_256_wrap(void);
const EVP_CIPHER *EVP_aes_256_xts(void);
#if !defined(OPENSSL_NO_CHACHA) && !defined(OPENSSL_NO_POLY1305)
const EVP_CIPHER *EVP_chacha20_poly1305(void);
#endif
#endif
#ifndef OPENSSL_NO_CAMELLIA
const EVP_CIPHER *EVP_camellia_128_ecb(void);
const EVP_CIPHER *EVP_camellia_128_cbc(void);
const EVP_CIPHER *EVP_camellia_128_cfb1(void);
const EVP_CIPHER *EVP_camellia_128_cfb8(void);
const EVP_CIPHER *EVP_camellia_128_cfb128(void);
# define EVP_camellia_128_cfb EVP_camellia_128_cfb128
const EVP_CIPHER *EVP_camellia_128_ofb(void);
const EVP_CIPHER *EVP_camellia_192_ecb(void);
const EVP_CIPHER *EVP_camellia_192_cbc(void);
const EVP_CIPHER *EVP_camellia_192_cfb1(void);
const EVP_CIPHER *EVP_camellia_192_cfb8(void);
const EVP_CIPHER *EVP_camellia_192_cfb128(void);
# define EVP_camellia_192_cfb EVP_camellia_192_cfb128
const EVP_CIPHER *EVP_camellia_192_ofb(void);
const EVP_CIPHER *EVP_camellia_256_ecb(void);
const EVP_CIPHER *EVP_camellia_256_cbc(void);
const EVP_CIPHER *EVP_camellia_256_cfb1(void);
const EVP_CIPHER *EVP_camellia_256_cfb8(void);
const EVP_CIPHER *EVP_camellia_256_cfb128(void);
# define EVP_camellia_256_cfb EVP_camellia_256_cfb128
const EVP_CIPHER *EVP_camellia_256_ofb(void);
#endif

#ifndef OPENSSL_NO_CHACHA
const EVP_CIPHER *EVP_chacha20(void);
#endif

#ifndef OPENSSL_NO_SM4
const EVP_CIPHER *EVP_sm4_ecb(void);
const EVP_CIPHER *EVP_sm4_cbc(void);
const EVP_CIPHER *EVP_sm4_cfb128(void);
#define EVP_sm4_cfb EVP_sm4_cfb128
const EVP_CIPHER *EVP_sm4_ofb(void);
const EVP_CIPHER *EVP_sm4_ctr(void);
#endif

void OPENSSL_add_all_algorithms_noconf(void);
void OPENSSL_add_all_algorithms_conf(void);

#ifdef OPENSSL_LOAD_CONF
#define OpenSSL_add_all_algorithms() OPENSSL_add_all_algorithms_conf()
#else
#define OpenSSL_add_all_algorithms() OPENSSL_add_all_algorithms_noconf()
#endif

void OpenSSL_add_all_ciphers(void);
void OpenSSL_add_all_digests(void);

#define SSLeay_add_all_algorithms() OpenSSL_add_all_algorithms()
#define SSLeay_add_all_ciphers() OpenSSL_add_all_ciphers()
#define SSLeay_add_all_digests() OpenSSL_add_all_digests()

const EVP_CIPHER *EVP_get_cipherbyname(const char *name);
const EVP_MD *EVP_get_digestbyname(const char *name);
void EVP_cleanup(void);

void EVP_CIPHER_do_all(void (*fn)(const EVP_CIPHER *ciph, const char *from,
    const char *to, void *x), void *arg);
void EVP_CIPHER_do_all_sorted(void (*fn)(const EVP_CIPHER *ciph,
    const char *from, const char *to, void *x), void *arg);

void EVP_MD_do_all(void (*fn)(const EVP_MD *ciph, const char *from,
    const char *to, void *x), void *arg);
void EVP_MD_do_all_sorted(void (*fn)(const EVP_MD *ciph, const char *from,
    const char *to, void *x), void *arg);

int EVP_PKEY_decrypt_old(unsigned char *dec_key, const unsigned char *enc_key,
    int enc_key_len, EVP_PKEY *private_key);
int EVP_PKEY_encrypt_old(unsigned char *enc_key, const unsigned char *key,
    int key_len, EVP_PKEY *pub_key);
int EVP_PKEY_type(int type);
int EVP_PKEY_id(const EVP_PKEY *pkey);
int EVP_PKEY_base_id(const EVP_PKEY *pkey);
int EVP_PKEY_bits(const EVP_PKEY *pkey);
int EVP_PKEY_security_bits(const EVP_PKEY *pkey);
int EVP_PKEY_size(const EVP_PKEY *pkey);
int EVP_PKEY_set_type(EVP_PKEY *pkey, int type);
int EVP_PKEY_set_type_str(EVP_PKEY *pkey, const char *str, int len);
int EVP_PKEY_assign(EVP_PKEY *pkey, int type, void *key);
void *EVP_PKEY_get0(const EVP_PKEY *pkey);
const unsigned char *EVP_PKEY_get0_hmac(const EVP_PKEY *pkey, size_t *len);

#ifndef OPENSSL_NO_RSA
RSA *EVP_PKEY_get0_RSA(const EVP_PKEY *pkey);
RSA *EVP_PKEY_get1_RSA(const EVP_PKEY *pkey);
int EVP_PKEY_set1_RSA(EVP_PKEY *pkey, RSA *key);
#endif
#ifndef OPENSSL_NO_DSA
DSA *EVP_PKEY_get0_DSA(const EVP_PKEY *pkey);
DSA *EVP_PKEY_get1_DSA(const EVP_PKEY *pkey);
int EVP_PKEY_set1_DSA(EVP_PKEY *pkey, DSA *key);
#endif
#ifndef OPENSSL_NO_DH
DH *EVP_PKEY_get0_DH(const EVP_PKEY *pkey);
DH *EVP_PKEY_get1_DH(const EVP_PKEY *pkey);
int EVP_PKEY_set1_DH(EVP_PKEY *pkey, DH *key);
#endif
#ifndef OPENSSL_NO_EC
EC_KEY *EVP_PKEY_get0_EC_KEY(const EVP_PKEY *pkey);
EC_KEY *EVP_PKEY_get1_EC_KEY(const EVP_PKEY *pkey);
int EVP_PKEY_set1_EC_KEY(EVP_PKEY *pkey, EC_KEY *key);
#endif

EVP_PKEY *EVP_PKEY_new(void);
void EVP_PKEY_free(EVP_PKEY *pkey);
int EVP_PKEY_up_ref(EVP_PKEY *pkey);

EVP_PKEY *d2i_PublicKey(int type, EVP_PKEY **a, const unsigned char **pp,
    long length);
int i2d_PublicKey(EVP_PKEY *a, unsigned char **pp);

EVP_PKEY *d2i_PrivateKey(int type, EVP_PKEY **a, const unsigned char **pp,
    long length);
EVP_PKEY *d2i_AutoPrivateKey(EVP_PKEY **a, const unsigned char **pp,
    long length);
int i2d_PrivateKey(EVP_PKEY *a, unsigned char **pp);

int EVP_PKEY_copy_parameters(EVP_PKEY *to, const EVP_PKEY *from);
int EVP_PKEY_missing_parameters(const EVP_PKEY *pkey);
int EVP_PKEY_save_parameters(EVP_PKEY *pkey, int mode);
int EVP_PKEY_cmp_parameters(const EVP_PKEY *a, const EVP_PKEY *b);

int EVP_PKEY_cmp(const EVP_PKEY *a, const EVP_PKEY *b);

int EVP_PKEY_print_public(BIO *out, const EVP_PKEY *pkey, int indent,
    ASN1_PCTX *pctx);
int EVP_PKEY_print_private(BIO *out, const EVP_PKEY *pkey, int indent,
    ASN1_PCTX *pctx);
int EVP_PKEY_print_params(BIO *out, const EVP_PKEY *pkey, int indent,
    ASN1_PCTX *pctx);

int EVP_PKEY_get_default_digest_nid(EVP_PKEY *pkey, int *pnid);

int EVP_CIPHER_type(const EVP_CIPHER *ctx);

/* PKCS5 password based encryption */
int PKCS5_PBKDF2_HMAC_SHA1(const char *pass, int passlen,
    const unsigned char *salt, int saltlen, int iter, int keylen,
    unsigned char *out);
int PKCS5_PBKDF2_HMAC(const char *pass, int passlen, const unsigned char *salt,
    int saltlen, int iter, const EVP_MD *digest, int keylen,
    unsigned char *out);

#define ASN1_PKEY_ALIAS		0x1
#define ASN1_PKEY_DYNAMIC	0x2
#define ASN1_PKEY_SIGPARAM_NULL	0x4

#define ASN1_PKEY_CTRL_PKCS7_SIGN	0x1
#define ASN1_PKEY_CTRL_PKCS7_ENCRYPT	0x2
#define ASN1_PKEY_CTRL_DEFAULT_MD_NID	0x3
#define ASN1_PKEY_CTRL_CMS_SIGN		0x5
#define ASN1_PKEY_CTRL_CMS_ENVELOPE	0x7
#define ASN1_PKEY_CTRL_CMS_RI_TYPE	0x8

int EVP_PKEY_asn1_get_count(void);
const EVP_PKEY_ASN1_METHOD *EVP_PKEY_asn1_get0(int idx);
const EVP_PKEY_ASN1_METHOD *EVP_PKEY_asn1_find(ENGINE **pe, int type);
const EVP_PKEY_ASN1_METHOD *EVP_PKEY_asn1_find_str(ENGINE **pe,
    const char *str, int len);
int EVP_PKEY_asn1_get0_info(int *ppkey_id, int *pkey_base_id, int *ppkey_flags,
    const char **pinfo, const char **ppem_str,
    const EVP_PKEY_ASN1_METHOD *ameth);

const EVP_PKEY_ASN1_METHOD *EVP_PKEY_get0_asn1(const EVP_PKEY *pkey);

#define EVP_PKEY_OP_UNDEFINED		0
#define EVP_PKEY_OP_PARAMGEN		(1<<1)
#define EVP_PKEY_OP_KEYGEN		(1<<2)
#define EVP_PKEY_OP_SIGN		(1<<3)
#define EVP_PKEY_OP_VERIFY		(1<<4)
#define EVP_PKEY_OP_VERIFYRECOVER	(1<<5)
#define EVP_PKEY_OP_SIGNCTX		(1<<6)
#define EVP_PKEY_OP_VERIFYCTX		(1<<7)
#define EVP_PKEY_OP_ENCRYPT		(1<<8)
#define EVP_PKEY_OP_DECRYPT		(1<<9)
#define EVP_PKEY_OP_DERIVE		(1<<10)

#define EVP_PKEY_OP_TYPE_SIG	\
	(EVP_PKEY_OP_SIGN | EVP_PKEY_OP_VERIFY | EVP_PKEY_OP_VERIFYRECOVER \
		| EVP_PKEY_OP_SIGNCTX | EVP_PKEY_OP_VERIFYCTX)

#define EVP_PKEY_OP_TYPE_CRYPT \
	(EVP_PKEY_OP_ENCRYPT | EVP_PKEY_OP_DECRYPT)

#define EVP_PKEY_OP_TYPE_NOGEN \
	(EVP_PKEY_OP_SIG | EVP_PKEY_OP_CRYPT | EVP_PKEY_OP_DERIVE)

#define EVP_PKEY_OP_TYPE_GEN \
		(EVP_PKEY_OP_PARAMGEN | EVP_PKEY_OP_KEYGEN)

#define EVP_PKEY_CTX_set_signature_md(ctx, md) \
		EVP_PKEY_CTX_ctrl(ctx, -1, EVP_PKEY_OP_TYPE_SIG, \
		    EVP_PKEY_CTRL_MD, 0, (void *)md)

#define EVP_PKEY_CTX_get_signature_md(ctx, pmd) \
		EVP_PKEY_CTX_ctrl(ctx, -1, EVP_PKEY_OP_TYPE_SIG, \
		    EVP_PKEY_CTRL_GET_MD, 0, (void *)(pmd))

#define EVP_PKEY_CTRL_MD		1
#define EVP_PKEY_CTRL_PEER_KEY		2

#define EVP_PKEY_CTRL_PKCS7_ENCRYPT	3
#define EVP_PKEY_CTRL_PKCS7_DECRYPT	4

#define EVP_PKEY_CTRL_PKCS7_SIGN	5

#define EVP_PKEY_CTRL_SET_MAC_KEY	6

#define EVP_PKEY_CTRL_DIGESTINIT	7

/* Used by GOST key encryption in TLS */
#define EVP_PKEY_CTRL_SET_IV		8

#define EVP_PKEY_CTRL_CMS_ENCRYPT	9
#define EVP_PKEY_CTRL_CMS_DECRYPT	10
#define EVP_PKEY_CTRL_CMS_SIGN		11

#define EVP_PKEY_CTRL_CIPHER		12

#define EVP_PKEY_CTRL_GET_MD		13

#define EVP_PKEY_ALG_CTRL		0x1000


#define EVP_PKEY_FLAG_AUTOARGLEN	2
/* Method handles all operations: don't assume any digest related
 * defaults.
 */
#define EVP_PKEY_FLAG_SIGCTX_CUSTOM	4

EVP_PKEY_CTX *EVP_PKEY_CTX_new(EVP_PKEY *pkey, ENGINE *e);
EVP_PKEY_CTX *EVP_PKEY_CTX_new_id(int id, ENGINE *e);
EVP_PKEY_CTX *EVP_PKEY_CTX_dup(EVP_PKEY_CTX *ctx);
void EVP_PKEY_CTX_free(EVP_PKEY_CTX *ctx);

int EVP_PKEY_CTX_ctrl(EVP_PKEY_CTX *ctx, int keytype, int optype, int cmd,
    int p1, void *p2);
int EVP_PKEY_CTX_ctrl_str(EVP_PKEY_CTX *ctx, const char *type,
    const char *value);

int EVP_PKEY_CTX_get_operation(EVP_PKEY_CTX *ctx);
void EVP_PKEY_CTX_set0_keygen_info(EVP_PKEY_CTX *ctx, int *dat, int datlen);

EVP_PKEY *EVP_PKEY_new_mac_key(int type, ENGINE *e, const unsigned char *key,
    int keylen);
EVP_PKEY *EVP_PKEY_new_CMAC_key(ENGINE *e, const unsigned char *priv,
    size_t len, const EVP_CIPHER *cipher);

void EVP_PKEY_CTX_set_data(EVP_PKEY_CTX *ctx, void *data);
void *EVP_PKEY_CTX_get_data(EVP_PKEY_CTX *ctx);
EVP_PKEY *EVP_PKEY_CTX_get0_pkey(EVP_PKEY_CTX *ctx);

EVP_PKEY *EVP_PKEY_CTX_get0_peerkey(EVP_PKEY_CTX *ctx);

void EVP_PKEY_CTX_set_app_data(EVP_PKEY_CTX *ctx, void *data);
void *EVP_PKEY_CTX_get_app_data(EVP_PKEY_CTX *ctx);

int EVP_PKEY_sign_init(EVP_PKEY_CTX *ctx);
int EVP_PKEY_sign(EVP_PKEY_CTX *ctx, unsigned char *sig, size_t *siglen,
    const unsigned char *tbs, size_t tbslen);
int EVP_PKEY_verify_init(EVP_PKEY_CTX *ctx);
int EVP_PKEY_verify(EVP_PKEY_CTX *ctx, const unsigned char *sig, size_t siglen,
    const unsigned char *tbs, size_t tbslen);
int EVP_PKEY_verify_recover_init(EVP_PKEY_CTX *ctx);
int EVP_PKEY_verify_recover(EVP_PKEY_CTX *ctx, unsigned char *rout,
    size_t *routlen, const unsigned char *sig, size_t siglen);
int EVP_PKEY_encrypt_init(EVP_PKEY_CTX *ctx);
int EVP_PKEY_encrypt(EVP_PKEY_CTX *ctx, unsigned char *out, size_t *outlen,
    const unsigned char *in, size_t inlen);
int EVP_PKEY_decrypt_init(EVP_PKEY_CTX *ctx);
int EVP_PKEY_decrypt(EVP_PKEY_CTX *ctx, unsigned char *out, size_t *outlen,
    const unsigned char *in, size_t inlen);

int EVP_PKEY_derive_init(EVP_PKEY_CTX *ctx);
int EVP_PKEY_derive_set_peer(EVP_PKEY_CTX *ctx, EVP_PKEY *peer);
int EVP_PKEY_derive(EVP_PKEY_CTX *ctx, unsigned char *key, size_t *keylen);

typedef int EVP_PKEY_gen_cb(EVP_PKEY_CTX *ctx);

int EVP_PKEY_paramgen_init(EVP_PKEY_CTX *ctx);
int EVP_PKEY_paramgen(EVP_PKEY_CTX *ctx, EVP_PKEY **ppkey);
int EVP_PKEY_keygen_init(EVP_PKEY_CTX *ctx);
int EVP_PKEY_keygen(EVP_PKEY_CTX *ctx, EVP_PKEY **ppkey);

void EVP_PKEY_CTX_set_cb(EVP_PKEY_CTX *ctx, EVP_PKEY_gen_cb *cb);
EVP_PKEY_gen_cb *EVP_PKEY_CTX_get_cb(EVP_PKEY_CTX *ctx);

int EVP_PKEY_CTX_get_keygen_info(EVP_PKEY_CTX *ctx, int idx);

/* Authenticated Encryption with Additional Data.
 *
 * AEAD couples confidentiality and integrity in a single primtive. AEAD
 * algorithms take a key and then can seal and open individual messages. Each
 * message has a unique, per-message nonce and, optionally, additional data
 * which is authenticated but not included in the output. */

typedef struct evp_aead_st EVP_AEAD;

#ifndef OPENSSL_NO_AES
/* EVP_aes_128_gcm is AES-128 in Galois Counter Mode. */
const EVP_AEAD *EVP_aead_aes_128_gcm(void);
/* EVP_aes_256_gcm is AES-256 in Galois Counter Mode. */
const EVP_AEAD *EVP_aead_aes_256_gcm(void);
#endif

#if !defined(OPENSSL_NO_CHACHA) && !defined(OPENSSL_NO_POLY1305)
/* EVP_aead_chacha20_poly1305 is ChaCha20 with a Poly1305 authenticator. */
const EVP_AEAD *EVP_aead_chacha20_poly1305(void);
/* EVP_aead_xchacha20_poly1305 is XChaCha20 with a Poly1305 authenticator. */
const EVP_AEAD *EVP_aead_xchacha20_poly1305(void);
#endif

/* EVP_AEAD_key_length returns the length of the keys used. */
size_t EVP_AEAD_key_length(const EVP_AEAD *aead);

/* EVP_AEAD_nonce_length returns the length of the per-message nonce. */
size_t EVP_AEAD_nonce_length(const EVP_AEAD *aead);

/* EVP_AEAD_max_overhead returns the maximum number of additional bytes added
 * by the act of sealing data with the AEAD. */
size_t EVP_AEAD_max_overhead(const EVP_AEAD *aead);

/* EVP_AEAD_max_tag_len returns the maximum tag length when using this AEAD.
 * This * is the largest value that can be passed as a tag length to
 * EVP_AEAD_CTX_init. */
size_t EVP_AEAD_max_tag_len(const EVP_AEAD *aead);

/* An EVP_AEAD_CTX represents an AEAD algorithm configured with a specific key
 * and message-independent IV. */
typedef struct evp_aead_ctx_st EVP_AEAD_CTX;

/* EVP_AEAD_MAX_TAG_LENGTH is the maximum tag length used by any AEAD
 * defined in this header. */
#define EVP_AEAD_MAX_TAG_LENGTH 16

/* EVP_AEAD_DEFAULT_TAG_LENGTH is a magic value that can be passed to
 * EVP_AEAD_CTX_init to indicate that the default tag length for an AEAD
 * should be used. */
#define EVP_AEAD_DEFAULT_TAG_LENGTH 0

/* EVP_AEAD_CTX_new allocates a new context for use with EVP_AEAD_CTX_init.
 * It can be cleaned up for reuse with EVP_AEAD_CTX_cleanup and must be freed
 * with EVP_AEAD_CTX_free. */
EVP_AEAD_CTX *EVP_AEAD_CTX_new(void);

/* EVP_AEAD_CTX_free releases all memory owned by the context. */
void EVP_AEAD_CTX_free(EVP_AEAD_CTX *ctx);

/* EVP_AEAD_CTX_init initializes the context for the given AEAD algorithm.
 * The implementation argument may be NULL to choose the default implementation.
 * Authentication tags may be truncated by passing a tag length. A tag length
 * of zero indicates the default tag length should be used. */
int EVP_AEAD_CTX_init(EVP_AEAD_CTX *ctx, const EVP_AEAD *aead,
    const unsigned char *key, size_t key_len, size_t tag_len, ENGINE *impl);

/* EVP_AEAD_CTX_cleanup frees any data allocated for this context. */
void EVP_AEAD_CTX_cleanup(EVP_AEAD_CTX *ctx);

/* EVP_AEAD_CTX_seal encrypts and authenticates the input and authenticates
 * any additional data (AD), the result being written as output. One is
 * returned on success, otherwise zero.
 *
 * This function may be called (with the same EVP_AEAD_CTX) concurrently with
 * itself or EVP_AEAD_CTX_open.
 *
 * At most max_out_len bytes are written as output and, in order to ensure
 * success, this value should be the length of the input plus the result of
 * EVP_AEAD_overhead. On successful return, out_len is set to the actual
 * number of bytes written.
 *
 * The length of the nonce is must be equal to the result of
 * EVP_AEAD_nonce_length for this AEAD.
 *
 * EVP_AEAD_CTX_seal never results in a partial output. If max_out_len is
 * insufficient, zero will be returned and out_len will be set to zero.
 *
 * If the input and output are aliased then out must be <= in. */
int EVP_AEAD_CTX_seal(const EVP_AEAD_CTX *ctx, unsigned char *out,
    size_t *out_len, size_t max_out_len, const unsigned char *nonce,
    size_t nonce_len, const unsigned char *in, size_t in_len,
    const unsigned char *ad, size_t ad_len);

/* EVP_AEAD_CTX_open authenticates the input and additional data, decrypting
 * the input and writing it as output. One is returned on success, otherwise
 * zero.
 *
 * This function may be called (with the same EVP_AEAD_CTX) concurrently with
 * itself or EVP_AEAD_CTX_seal.
 *
 * At most the number of input bytes are written as output. In order to ensure
 * success, max_out_len should be at least the same as the input length. On
 * successful return out_len is set to the actual number of bytes written.
 *
 * The length of nonce must be equal to the result of EVP_AEAD_nonce_length
 * for this AEAD.
 *
 * EVP_AEAD_CTX_open never results in a partial output. If max_out_len is
 * insufficient, zero will be returned and out_len will be set to zero.
 *
 * If the input and output are aliased then out must be <= in. */
int EVP_AEAD_CTX_open(const EVP_AEAD_CTX *ctx, unsigned char *out,
    size_t *out_len, size_t max_out_len, const unsigned char *nonce,
    size_t nonce_len, const unsigned char *in, size_t in_len,
    const unsigned char *ad, size_t ad_len);

void ERR_load_EVP_strings(void);

/* Error codes for the EVP functions. */

/* Function codes. */
#define EVP_F_AEAD_AES_GCM_INIT				 187
#define EVP_F_AEAD_AES_GCM_OPEN				 188
#define EVP_F_AEAD_AES_GCM_SEAL				 189
#define EVP_F_AEAD_CHACHA20_POLY1305_INIT		 192
#define EVP_F_AEAD_CHACHA20_POLY1305_OPEN		 193
#define EVP_F_AEAD_CHACHA20_POLY1305_SEAL		 194
#define EVP_F_AEAD_CTX_OPEN				 185
#define EVP_F_AEAD_CTX_SEAL				 186
#define EVP_F_AESNI_INIT_KEY				 165
#define EVP_F_AESNI_XTS_CIPHER				 176
#define EVP_F_AES_INIT_KEY				 133
#define EVP_F_AES_XTS					 172
#define EVP_F_AES_XTS_CIPHER				 175
#define EVP_F_ALG_MODULE_INIT				 177
#define EVP_F_CAMELLIA_INIT_KEY				 159
#define EVP_F_CMAC_INIT					 173
#define EVP_F_D2I_PKEY					 100
#define EVP_F_DO_SIGVER_INIT				 161
#define EVP_F_DSAPKEY2PKCS8				 134
#define EVP_F_DSA_PKEY2PKCS8				 135
#define EVP_F_ECDSA_PKEY2PKCS8				 129
#define EVP_F_ECKEY_PKEY2PKCS8				 132
#define EVP_F_EVP_AEAD_CTX_INIT				 180
#define EVP_F_EVP_AEAD_CTX_OPEN				 190
#define EVP_F_EVP_AEAD_CTX_SEAL				 191
#define EVP_F_EVP_BYTESTOKEY				 200
#define EVP_F_EVP_CIPHERINIT_EX				 123
#define EVP_F_EVP_CIPHER_CTX_COPY			 163
#define EVP_F_EVP_CIPHER_CTX_CTRL			 124
#define EVP_F_EVP_CIPHER_CTX_SET_KEY_LENGTH		 122
#define EVP_F_EVP_CIPHER_GET_ASN1_IV			 201
#define EVP_F_EVP_CIPHER_SET_ASN1_IV			 202
#define EVP_F_EVP_DECRYPTFINAL_EX			 101
#define EVP_F_EVP_DECRYPTUPDATE				 199
#define EVP_F_EVP_DIGESTFINAL_EX			 196
#define EVP_F_EVP_DIGESTINIT_EX				 128
#define EVP_F_EVP_ENCRYPTFINAL_EX			 127
#define EVP_F_EVP_ENCRYPTUPDATE				 198
#define EVP_F_EVP_MD_CTX_COPY_EX			 110
#define EVP_F_EVP_MD_CTX_CTRL				 195
#define EVP_F_EVP_MD_SIZE				 162
#define EVP_F_EVP_OPENINIT				 102
#define EVP_F_EVP_PBE_ALG_ADD				 115
#define EVP_F_EVP_PBE_ALG_ADD_TYPE			 160
#define EVP_F_EVP_PBE_CIPHERINIT			 116
#define EVP_F_EVP_PKCS82PKEY				 111
#define EVP_F_EVP_PKCS82PKEY_BROKEN			 136
#define EVP_F_EVP_PKEY2PKCS8_BROKEN			 113
#define EVP_F_EVP_PKEY_COPY_PARAMETERS			 103
#define EVP_F_EVP_PKEY_CTX_CTRL				 137
#define EVP_F_EVP_PKEY_CTX_CTRL_STR			 150
#define EVP_F_EVP_PKEY_CTX_DUP				 156
#define EVP_F_EVP_PKEY_DECRYPT				 104
#define EVP_F_EVP_PKEY_DECRYPT_INIT			 138
#define EVP_F_EVP_PKEY_DECRYPT_OLD			 151
#define EVP_F_EVP_PKEY_DERIVE				 153
#define EVP_F_EVP_PKEY_DERIVE_INIT			 154
#define EVP_F_EVP_PKEY_DERIVE_SET_PEER			 155
#define EVP_F_EVP_PKEY_ENCRYPT				 105
#define EVP_F_EVP_PKEY_ENCRYPT_INIT			 139
#define EVP_F_EVP_PKEY_ENCRYPT_OLD			 152
#define EVP_F_EVP_PKEY_GET1_DH				 119
#define EVP_F_EVP_PKEY_GET1_DSA				 120
#define EVP_F_EVP_PKEY_GET1_ECDSA			 130
#define EVP_F_EVP_PKEY_GET1_EC_KEY			 131
#define EVP_F_EVP_PKEY_GET1_RSA				 121
#define EVP_F_EVP_PKEY_KEYGEN				 146
#define EVP_F_EVP_PKEY_KEYGEN_INIT			 147
#define EVP_F_EVP_PKEY_NEW				 106
#define EVP_F_EVP_PKEY_PARAMGEN				 148
#define EVP_F_EVP_PKEY_PARAMGEN_INIT			 149
#define EVP_F_EVP_PKEY_SIGN				 140
#define EVP_F_EVP_PKEY_SIGN_INIT			 141
#define EVP_F_EVP_PKEY_VERIFY				 142
#define EVP_F_EVP_PKEY_VERIFY_INIT			 143
#define EVP_F_EVP_PKEY_VERIFY_RECOVER			 144
#define EVP_F_EVP_PKEY_VERIFY_RECOVER_INIT		 145
#define EVP_F_EVP_RIJNDAEL				 126
#define EVP_F_EVP_SIGNFINAL				 107
#define EVP_F_EVP_VERIFYFINAL				 108
#define EVP_F_FIPS_CIPHERINIT				 166
#define EVP_F_FIPS_CIPHER_CTX_COPY			 170
#define EVP_F_FIPS_CIPHER_CTX_CTRL			 167
#define EVP_F_FIPS_CIPHER_CTX_SET_KEY_LENGTH		 171
#define EVP_F_FIPS_DIGESTINIT				 168
#define EVP_F_FIPS_MD_CTX_COPY				 169
#define EVP_F_HMAC_INIT_EX				 174
#define EVP_F_INT_CTX_NEW				 157
#define EVP_F_PKCS5_PBE_KEYIVGEN			 117
#define EVP_F_PKCS5_V2_PBE_KEYIVGEN			 118
#define EVP_F_PKCS5_V2_PBKDF2_KEYIVGEN			 164
#define EVP_F_PKCS8_SET_BROKEN				 112
#define EVP_F_PKEY_SET_TYPE				 158
#define EVP_F_RC2_GET_ASN1_TYPE_AND_IV			 197
#define EVP_F_RC2_MAGIC_TO_METH				 109
#define EVP_F_RC5_CTRL					 125

/* Reason codes. */
#define EVP_R_AES_IV_SETUP_FAILED			 162
#define EVP_R_AES_KEY_SETUP_FAILED			 143
#define EVP_R_ASN1_LIB					 140
#define EVP_R_BAD_BLOCK_LENGTH				 136
#define EVP_R_BAD_DECRYPT				 100
#define EVP_R_BAD_KEY_LENGTH				 137
#define EVP_R_BN_DECODE_ERROR				 112
#define EVP_R_BN_PUBKEY_ERROR				 113
#define EVP_R_BUFFER_TOO_SMALL				 155
#define EVP_R_CAMELLIA_KEY_SETUP_FAILED			 157
#define EVP_R_CIPHER_PARAMETER_ERROR			 122
#define EVP_R_COMMAND_NOT_SUPPORTED			 147
#define EVP_R_CTRL_NOT_IMPLEMENTED			 132
#define EVP_R_CTRL_OPERATION_NOT_IMPLEMENTED		 133
#define EVP_R_DATA_NOT_MULTIPLE_OF_BLOCK_LENGTH		 138
#define EVP_R_DECODE_ERROR				 114
#define EVP_R_DIFFERENT_KEY_TYPES			 101
#define EVP_R_DIFFERENT_PARAMETERS			 153
#define EVP_R_DISABLED_FOR_FIPS				 163
#define EVP_R_ENCODE_ERROR				 115
#define EVP_R_ERROR_LOADING_SECTION			 165
#define EVP_R_ERROR_SETTING_FIPS_MODE			 166
#define EVP_R_EVP_PBE_CIPHERINIT_ERROR			 119
#define EVP_R_EXPECTING_AN_HMAC_KEY			 174
#define EVP_R_EXPECTING_AN_RSA_KEY			 127
#define EVP_R_EXPECTING_A_DH_KEY			 128
#define EVP_R_EXPECTING_A_DSA_KEY			 129
#define EVP_R_EXPECTING_A_ECDSA_KEY			 141
#define EVP_R_EXPECTING_A_EC_KEY			 142
#define EVP_R_FIPS_MODE_NOT_SUPPORTED			 167
#define EVP_R_GET_RAW_KEY_FAILED			 182
#define EVP_R_INITIALIZATION_ERROR			 134
#define EVP_R_INPUT_NOT_INITIALIZED			 111
#define EVP_R_INVALID_DIGEST				 152
#define EVP_R_INVALID_FIPS_MODE				 168
#define EVP_R_INVALID_IV_LENGTH				 194
#define EVP_R_INVALID_KEY_LENGTH			 130
#define EVP_R_INVALID_OPERATION				 148
#define EVP_R_IV_TOO_LARGE				 102
#define EVP_R_KEYGEN_FAILURE				 120
#define EVP_R_KEY_SETUP_FAILED				 180
#define EVP_R_MESSAGE_DIGEST_IS_NULL			 159
#define EVP_R_METHOD_NOT_SUPPORTED			 144
#define EVP_R_MISSING_PARAMETERS			 103
#define EVP_R_NO_CIPHER_SET				 131
#define EVP_R_NO_DEFAULT_DIGEST				 158
#define EVP_R_NO_DIGEST_SET				 139
#define EVP_R_NO_DSA_PARAMETERS				 116
#define EVP_R_NO_KEY_SET				 154
#define EVP_R_NO_OPERATION_SET				 149
#define EVP_R_NO_SIGN_FUNCTION_CONFIGURED		 104
#define EVP_R_NO_VERIFY_FUNCTION_CONFIGURED		 105
#define EVP_R_ONLY_ONESHOT_SUPPORTED			 177
#define EVP_R_OPERATION_NOT_SUPPORTED_FOR_THIS_KEYTYPE	 150
#define EVP_R_OPERATON_NOT_INITIALIZED			 151
#define EVP_R_OUTPUT_ALIASES_INPUT			 172
#define EVP_R_PKCS8_UNKNOWN_BROKEN_TYPE			 117
#define EVP_R_PRIVATE_KEY_DECODE_ERROR			 145
#define EVP_R_PRIVATE_KEY_ENCODE_ERROR			 146
#define EVP_R_PUBLIC_KEY_NOT_RSA			 106
#define EVP_R_TAG_TOO_LARGE				 171
#define EVP_R_TOO_LARGE					 164
#define EVP_R_UNKNOWN_CIPHER				 160
#define EVP_R_UNKNOWN_DIGEST				 161
#define EVP_R_UNKNOWN_OPTION				 169
#define EVP_R_UNKNOWN_PBE_ALGORITHM			 121
#define EVP_R_UNSUPORTED_NUMBER_OF_ROUNDS		 135
#define EVP_R_UNSUPPORTED_ALGORITHM			 156
#define EVP_R_UNSUPPORTED_CIPHER			 107
#define EVP_R_UNSUPPORTED_KEYLENGTH			 123
#define EVP_R_UNSUPPORTED_KEY_DERIVATION_FUNCTION	 124
#define EVP_R_UNSUPPORTED_KEY_SIZE			 108
#define EVP_R_UNSUPPORTED_PRF				 125
#define EVP_R_UNSUPPORTED_PRIVATE_KEY_ALGORITHM		 118
#define EVP_R_WRAP_MODE_NOT_ALLOWED			 170
#define EVP_R_UNSUPPORTED_SALT_TYPE			 126
#define EVP_R_WRONG_FINAL_BLOCK_LENGTH			 109
#define EVP_R_WRONG_PUBLIC_KEY_TYPE			 110

#ifdef  __cplusplus
}
#endif
#endif
