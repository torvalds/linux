/* $OpenBSD: rsa_chk.c,v 1.19 2025/05/10 05:54:38 tb Exp $ */
/* ====================================================================
 * Copyright (c) 1999 The OpenSSL Project.  All rights reserved.
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
 *    openssl-core@OpenSSL.org.
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
 */

#include <openssl/bn.h>
#include <openssl/rsa.h>

#include "bn_local.h"
#include "err_local.h"
#include "rsa_local.h"

int
RSA_check_key(const RSA *key)
{
	BIGNUM *i, *j, *k, *l, *m;
	BN_CTX *ctx;
	int r;
	int ret = 1;

	if (!key->p || !key->q || !key->n || !key->e || !key->d) {
		RSAerror(RSA_R_VALUE_MISSING);
		return 0;
	}

	i = BN_new();
	j = BN_new();
	k = BN_new();
	l = BN_new();
	m = BN_new();
	ctx = BN_CTX_new();
	if (i == NULL || j == NULL || k == NULL || l == NULL || m == NULL ||
	    ctx == NULL) {
		ret = -1;
		RSAerror(ERR_R_MALLOC_FAILURE);
		goto err;
	}

	if (BN_is_one(key->e)) {
		ret = 0;
		RSAerror(RSA_R_BAD_E_VALUE);
	}
	if (!BN_is_odd(key->e)) {
		ret = 0;
		RSAerror(RSA_R_BAD_E_VALUE);
	}

	/* p prime? */
	r = BN_is_prime_ex(key->p, BN_prime_checks, NULL, NULL);
	if (r != 1) {
		ret = r;
		if (r != 0)
			goto err;
		RSAerror(RSA_R_P_NOT_PRIME);
	}

	/* q prime? */
	r = BN_is_prime_ex(key->q, BN_prime_checks, NULL, NULL);
	if (r != 1) {
		ret = r;
		if (r != 0)
			goto err;
		RSAerror(RSA_R_Q_NOT_PRIME);
	}

	/* n = p*q? */
	r = BN_mul(i, key->p, key->q, ctx);
	if (!r) {
		ret = -1;
		goto err;
	}

	if (BN_cmp(i, key->n) != 0) {
		ret = 0;
		RSAerror(RSA_R_N_DOES_NOT_EQUAL_P_Q);
	}

	/* d*e = 1  mod lcm(p-1,q-1)? */

	r = BN_sub(i, key->p, BN_value_one());
	if (!r) {
		ret = -1;
		goto err;
	}
	r = BN_sub(j, key->q, BN_value_one());
	if (!r) {
		ret = -1;
		goto err;
	}

	/* now compute k = lcm(i,j) */
	r = BN_mul(l, i, j, ctx);
	if (!r) {
		ret = -1;
		goto err;
	}
	r = BN_gcd_ct(m, i, j, ctx);
	if (!r) {
		ret = -1;
		goto err;
	}
	r = BN_div_ct(k, NULL, l, m, ctx); /* remainder is 0 */
	if (!r) {
		ret = -1;
		goto err;
	}

	r = BN_mod_mul(i, key->d, key->e, k, ctx);
	if (!r) {
		ret = -1;
		goto err;
	}

	if (!BN_is_one(i)) {
		ret = 0;
		RSAerror(RSA_R_D_E_NOT_CONGRUENT_TO_1);
	}

	if (key->dmp1 != NULL && key->dmq1 != NULL && key->iqmp != NULL) {
		/* dmp1 = d mod (p-1)? */
		r = BN_sub(i, key->p, BN_value_one());
		if (!r) {
			ret = -1;
			goto err;
		}

		r = BN_mod_ct(j, key->d, i, ctx);
		if (!r) {
			ret = -1;
			goto err;
		}

		if (BN_cmp(j, key->dmp1) != 0) {
			ret = 0;
			RSAerror(RSA_R_DMP1_NOT_CONGRUENT_TO_D);
		}

		/* dmq1 = d mod (q-1)? */
		r = BN_sub(i, key->q, BN_value_one());
		if (!r) {
			ret = -1;
			goto err;
		}

		r = BN_mod_ct(j, key->d, i, ctx);
		if (!r) {
			ret = -1;
			goto err;
		}

		if (BN_cmp(j, key->dmq1) != 0) {
			ret = 0;
			RSAerror(RSA_R_DMQ1_NOT_CONGRUENT_TO_D);
		}

		/* iqmp = q^-1 mod p? */
		if (BN_mod_inverse_ct(i, key->q, key->p, ctx) == NULL) {
			ret = -1;
			goto err;
		}

		if (BN_cmp(i, key->iqmp) != 0) {
			ret = 0;
			RSAerror(RSA_R_IQMP_NOT_INVERSE_OF_Q);
		}
	}

err:
	BN_free(i);
	BN_free(j);
	BN_free(k);
	BN_free(l);
	BN_free(m);
	BN_CTX_free(ctx);

	return (ret);
}
LCRYPTO_ALIAS(RSA_check_key);
