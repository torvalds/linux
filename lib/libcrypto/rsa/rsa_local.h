/* $OpenBSD: rsa_local.h,v 1.10 2025/01/05 15:39:12 tb Exp $ */
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

__BEGIN_HIDDEN_DECLS

#define RSA_MIN_MODULUS_BITS	512

struct rsa_meth_st {
	char *name;
	int (*rsa_pub_enc)(int flen, const unsigned char *from,
	    unsigned char *to, RSA *rsa, int padding);
	int (*rsa_pub_dec)(int flen, const unsigned char *from,
	    unsigned char *to, RSA *rsa, int padding);
	int (*rsa_priv_enc)(int flen, const unsigned char *from,
	    unsigned char *to, RSA *rsa, int padding);
	int (*rsa_priv_dec)(int flen, const unsigned char *from,
	    unsigned char *to, RSA *rsa, int padding);
	int (*rsa_mod_exp)(BIGNUM *r0, const BIGNUM *I, RSA *rsa,
	    BN_CTX *ctx); /* Can be null */
	int (*bn_mod_exp)(BIGNUM *r, const BIGNUM *a, const BIGNUM *p,
	    const BIGNUM *m, BN_CTX *ctx, BN_MONT_CTX *m_ctx); /* Can be null */
	int (*init)(RSA *rsa);		/* called at new */
	int (*finish)(RSA *rsa);	/* called at free */
	int flags;			/* RSA_METHOD_FLAG_* things */
	char *app_data;			/* may be needed! */
/* New sign and verify functions: some libraries don't allow arbitrary data
 * to be signed/verified: this allows them to be used. Note: for this to work
 * the RSA_public_decrypt() and RSA_private_encrypt() should *NOT* be used
 * RSA_sign(), RSA_verify() should be used instead.
 */
	int (*rsa_sign)(int type, const unsigned char *m, unsigned int m_length,
	    unsigned char *sigret, unsigned int *siglen, const RSA *rsa);
	int (*rsa_verify)(int dtype, const unsigned char *m,
	    unsigned int m_length, const unsigned char *sigbuf,
	    unsigned int siglen, const RSA *rsa);
/* If this callback is NULL, the builtin software RSA key-gen will be used. This
 * is for behavioural compatibility whilst the code gets rewired, but one day
 * it would be nice to assume there are no such things as "builtin software"
 * implementations. */
	int (*rsa_keygen)(RSA *rsa, int bits, BIGNUM *e, BN_GENCB *cb);
};

struct rsa_st {
	long version;
	const RSA_METHOD *meth;

	BIGNUM *n;
	BIGNUM *e;
	BIGNUM *d;
	BIGNUM *p;
	BIGNUM *q;
	BIGNUM *dmp1;
	BIGNUM *dmq1;
	BIGNUM *iqmp;

	/* Parameter restrictions for PSS only keys. */
	RSA_PSS_PARAMS *pss;

	/* be careful using this if the RSA structure is shared */
	CRYPTO_EX_DATA ex_data;
	int references;
	int flags;

	/* Used to cache montgomery values */
	BN_MONT_CTX *_method_mod_n;
	BN_MONT_CTX *_method_mod_p;
	BN_MONT_CTX *_method_mod_q;

	/* all BIGNUM values are actually in the following data, if it is not
	 * NULL */
	BN_BLINDING *blinding;
	BN_BLINDING *mt_blinding;
};

RSA_PSS_PARAMS *rsa_pss_params_create(const EVP_MD *sigmd, const EVP_MD *mgf1md,
    int saltlen);
int rsa_pss_get_param(const RSA_PSS_PARAMS *pss, const EVP_MD **pmd,
    const EVP_MD **pmgf1md, int *psaltlen);

extern int int_rsa_verify(int dtype, const unsigned char *m,
    unsigned int m_len, unsigned char *rm, size_t *prm_len,
    const unsigned char *sigbuf, size_t siglen, RSA *rsa);

int RSA_padding_add_X931(unsigned char *to, int tlen,
    const unsigned char *f, int fl);
int RSA_padding_check_X931(unsigned char *to, int tlen,
    const unsigned char *f, int fl, int rsa_len);
int RSA_X931_hash_id(int nid);

BN_BLINDING *BN_BLINDING_new(const BIGNUM *e, const BIGNUM *mod, BN_CTX *ctx,
    int (*bn_mod_exp)(BIGNUM *r, const BIGNUM *a, const BIGNUM *p,
    const BIGNUM *m, BN_CTX *ctx, BN_MONT_CTX *m_ctx), BN_MONT_CTX *m_ctx);
void BN_BLINDING_free(BN_BLINDING *b);
int BN_BLINDING_convert(BIGNUM *n, BIGNUM *r, BN_BLINDING *b, BN_CTX *);
int BN_BLINDING_invert(BIGNUM *n, const BIGNUM *r, BN_BLINDING *b, BN_CTX *);
int BN_BLINDING_is_local(BN_BLINDING *b);
BN_BLINDING *RSA_setup_blinding(RSA *rsa, BN_CTX *ctx);

__END_HIDDEN_DECLS
