/* $OpenBSD: dh.h,v 1.38 2025/01/25 17:59:44 tb Exp $ */
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

#ifndef HEADER_DH_H
#define HEADER_DH_H

#include <openssl/opensslconf.h>

#ifndef OPENSSL_NO_BIO
#include <openssl/bio.h>
#endif
#include <openssl/ossl_typ.h>
#include <openssl/bn.h>

#ifndef OPENSSL_DH_MAX_MODULUS_BITS
# define OPENSSL_DH_MAX_MODULUS_BITS	10000
#endif

#define DH_FLAG_CACHE_MONT_P     0x01

/* If this flag is set the DH method is FIPS compliant and can be used
 * in FIPS mode. This is set in the validated module method. If an
 * application sets this flag in its own methods it is its reposibility
 * to ensure the result is compliant.
 */

#define DH_FLAG_FIPS_METHOD			0x0400

/* If this flag is set the operations normally disabled in FIPS mode are
 * permitted it is then the applications responsibility to ensure that the
 * usage is compliant.
 */

#define DH_FLAG_NON_FIPS_ALLOW			0x0400

#ifdef  __cplusplus
extern "C" {
#endif

#define DH_GENERATOR_2		2
/* #define DH_GENERATOR_3	3 */
#define DH_GENERATOR_5		5

/* DH_check error codes */
#define DH_CHECK_P_NOT_PRIME		0x01
#define DH_CHECK_P_NOT_SAFE_PRIME	0x02
#define DH_UNABLE_TO_CHECK_GENERATOR	0x04
#define DH_NOT_SUITABLE_GENERATOR	0x08
#define DH_CHECK_Q_NOT_PRIME		0x10
#define DH_CHECK_INVALID_Q_VALUE	0x20
#define DH_CHECK_INVALID_J_VALUE	0x40

/* DH_check_pub_key error codes */
#define DH_CHECK_PUBKEY_TOO_SMALL	0x01
#define DH_CHECK_PUBKEY_TOO_LARGE	0x02
#define DH_CHECK_PUBKEY_INVALID		0x04

/* primes p where (p-1)/2 is prime too are called "safe"; we define
   this for backward compatibility: */
#define DH_CHECK_P_NOT_STRONG_PRIME	DH_CHECK_P_NOT_SAFE_PRIME

DH *d2i_DHparams_bio(BIO *bp, DH **a);
int i2d_DHparams_bio(BIO *bp, DH *a);
DH *d2i_DHparams_fp(FILE *fp, DH **a);
int i2d_DHparams_fp(FILE *fp, DH *a);

DH *DHparams_dup(DH *);

const DH_METHOD *DH_OpenSSL(void);

void DH_set_default_method(const DH_METHOD *meth);
const DH_METHOD *DH_get_default_method(void);
int DH_set_method(DH *dh, const DH_METHOD *meth);
DH *DH_new_method(ENGINE *engine);

DH *	DH_new(void);
void	DH_free(DH *dh);
int	DH_up_ref(DH *dh);
int	DH_size(const DH *dh);
int	DH_bits(const DH *dh);
int DH_get_ex_new_index(long argl, void *argp, CRYPTO_EX_new *new_func,
	     CRYPTO_EX_dup *dup_func, CRYPTO_EX_free *free_func);
int DH_set_ex_data(DH *d, int idx, void *arg);
void *DH_get_ex_data(DH *d, int idx);
int DH_security_bits(const DH *dh);

ENGINE *DH_get0_engine(DH *d);
void DH_get0_pqg(const DH *dh, const BIGNUM **p, const BIGNUM **q,
    const BIGNUM **g);
int DH_set0_pqg(DH *dh, BIGNUM *p, BIGNUM *q, BIGNUM *g);
void DH_get0_key(const DH *dh, const BIGNUM **pub_key, const BIGNUM **priv_key);
int DH_set0_key(DH *dh, BIGNUM *pub_key, BIGNUM *priv_key);
const BIGNUM *DH_get0_p(const DH *dh);
const BIGNUM *DH_get0_q(const DH *dh);
const BIGNUM *DH_get0_g(const DH *dh);
const BIGNUM *DH_get0_priv_key(const DH *dh);
const BIGNUM *DH_get0_pub_key(const DH *dh);
void DH_clear_flags(DH *dh, int flags);
int DH_test_flags(const DH *dh, int flags);
void DH_set_flags(DH *dh, int flags);
long DH_get_length(const DH *dh);
int DH_set_length(DH *dh, long length);

/*
 * Wrapped in OPENSSL_NO_DEPRECATED in 0.9.8, added to rust-openssl in 2020,
 * for "advanced DH support".
 */
DH *	DH_generate_parameters(int prime_len,int generator,
		void (*callback)(int,int,void *),void *cb_arg);

/* New version */
int	DH_generate_parameters_ex(DH *dh, int prime_len,int generator, BN_GENCB *cb);

int	DH_check(const DH *dh,int *codes);
int	DH_check_pub_key(const DH *dh,const BIGNUM *pub_key, int *codes);
int	DH_generate_key(DH *dh);
int	DH_compute_key(unsigned char *key,const BIGNUM *pub_key,DH *dh);
DH *	d2i_DHparams(DH **a,const unsigned char **pp, long length);
int	i2d_DHparams(const DH *a,unsigned char **pp);
int	DHparams_print_fp(FILE *fp, const DH *x);
#ifndef OPENSSL_NO_BIO
int	DHparams_print(BIO *bp, const DH *x);
#else
int	DHparams_print(char *bp, const DH *x);
#endif

#define EVP_PKEY_CTX_set_dh_paramgen_prime_len(ctx, len) \
	EVP_PKEY_CTX_ctrl(ctx, EVP_PKEY_DH, EVP_PKEY_OP_PARAMGEN, \
			EVP_PKEY_CTRL_DH_PARAMGEN_PRIME_LEN, len, NULL)

#define EVP_PKEY_CTX_set_dh_paramgen_generator(ctx, gen) \
	EVP_PKEY_CTX_ctrl(ctx, EVP_PKEY_DH, EVP_PKEY_OP_PARAMGEN, \
			EVP_PKEY_CTRL_DH_PARAMGEN_GENERATOR, gen, NULL)

#define	EVP_PKEY_CTRL_DH_PARAMGEN_PRIME_LEN	(EVP_PKEY_ALG_CTRL + 1)
#define	EVP_PKEY_CTRL_DH_PARAMGEN_GENERATOR	(EVP_PKEY_ALG_CTRL + 2)


void ERR_load_DH_strings(void);

/* Error codes for the DH functions. */

/* Function codes. */
#define DH_F_COMPUTE_KEY				 102
#define DH_F_DHPARAMS_PRINT_FP				 101
#define DH_F_DH_BUILTIN_GENPARAMS			 106
#define DH_F_DH_COMPUTE_KEY				 114
#define DH_F_DH_GENERATE_KEY				 115
#define DH_F_DH_GENERATE_PARAMETERS_EX			 116
#define DH_F_DH_NEW_METHOD				 105
#define DH_F_DH_PARAM_DECODE				 107
#define DH_F_DH_PRIV_DECODE				 110
#define DH_F_DH_PRIV_ENCODE				 111
#define DH_F_DH_PUB_DECODE				 108
#define DH_F_DH_PUB_ENCODE				 109
#define DH_F_DO_DH_PRINT				 100
#define DH_F_GENERATE_KEY				 103
#define DH_F_GENERATE_PARAMETERS			 104
#define DH_F_PKEY_DH_DERIVE				 112
#define DH_F_PKEY_DH_KEYGEN				 113

/* Reason codes. */
#define DH_R_BAD_GENERATOR				 101
#define DH_R_BN_DECODE_ERROR				 109
#define DH_R_BN_ERROR					 106
#define DH_R_DECODE_ERROR				 104
#define DH_R_INVALID_PUBKEY				 102
#define DH_R_KEYS_NOT_SET				 108
#define DH_R_KEY_SIZE_TOO_SMALL				 110
#define DH_R_MODULUS_TOO_LARGE				 103
#define DH_R_NON_FIPS_METHOD				 111
#define DH_R_NO_PARAMETERS_SET				 107
#define DH_R_NO_PRIVATE_VALUE				 100
#define DH_R_PARAMETER_ENCODING_ERROR			 105
#define DH_R_CHECK_INVALID_J_VALUE			 115
#define DH_R_CHECK_INVALID_Q_VALUE			 116
#define DH_R_CHECK_PUBKEY_INVALID			 122
#define DH_R_CHECK_PUBKEY_TOO_LARGE			 123
#define DH_R_CHECK_PUBKEY_TOO_SMALL			 124
#define DH_R_CHECK_P_NOT_PRIME				 117
#define DH_R_CHECK_P_NOT_SAFE_PRIME			 118
#define DH_R_CHECK_Q_NOT_PRIME				 119
#define DH_R_MISSING_PUBKEY				 125
#define DH_R_NOT_SUITABLE_GENERATOR			 120
#define DH_R_UNABLE_TO_CHECK_GENERATOR			 121

#ifdef  __cplusplus
}
#endif
#endif
