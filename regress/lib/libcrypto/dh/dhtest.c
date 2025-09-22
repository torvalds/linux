/*	$OpenBSD: dhtest.c,v 1.14 2023/08/20 22:21:00 tb Exp $	*/
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

#include <err.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <openssl/crypto.h>
#include <openssl/bio.h>
#include <openssl/bn.h>
#include <openssl/err.h>

#include <openssl/dh.h>

static int
cb(int p, int n, BN_GENCB *arg)
{
	char c = '*';

	if (p == 0)
		c = '.';
	if (p == 1)
		c = '+';
	if (p == 2)
		c = '*';
	if (p == 3)
		c = '\n';
	printf("%c", c);
	fflush(stdout);
	return 1;
}

int
main(int argc, char *argv[])
{
	BN_GENCB *_cb;
	DH *dh = NULL;
	unsigned char *buf = NULL;
	int flags, buf_len, secret_len;
	int i;
	int ret = 1;

	if ((_cb = BN_GENCB_new()) == NULL)
		err(1, "BN_GENCB_new");

	BN_GENCB_set(_cb, &cb, NULL);
	if ((dh = DH_new()) == NULL)
		goto err;

#ifdef OPENSSL_NO_ENGINE
	if (DH_get0_engine(dh) != NULL) {
		fprintf(stderr, "ENGINE was not NULL\n");
		goto err;
	}
#endif

	if (!DH_generate_parameters_ex(dh, 64, DH_GENERATOR_5, _cb))
		goto err;

	if (!DH_check(dh, &flags))
		goto err;
	if (flags & DH_CHECK_P_NOT_PRIME)
		printf("p value is not prime\n");
	if (flags & DH_CHECK_P_NOT_SAFE_PRIME)
		printf("p value is not a safe prime\n");
	if (flags & DH_UNABLE_TO_CHECK_GENERATOR)
		printf("unable to check the generator value\n");
	if (flags & DH_NOT_SUITABLE_GENERATOR)
		printf("the g value is not a generator\n");

	printf("\np    = ");
	if (!BN_print_fp(stdout, DH_get0_p(dh)))
		goto err;
	printf("\ng    = ");
	if (!BN_print_fp(stdout, DH_get0_g(dh)))
		goto err;
	printf("\n");

	if (!DH_generate_key(dh))
		goto err;
	printf("pri1 = ");
	if (!BN_print_fp(stdout, DH_get0_priv_key(dh)))
		goto err;
	printf("\npub1 = ");
	if (!BN_print_fp(stdout, DH_get0_pub_key(dh)))
		goto err;
	printf("\n");

	buf_len = DH_size(dh);
	if ((buf = malloc(buf_len)) == NULL)
		err(1, "malloc");
	secret_len = DH_compute_key(buf, DH_get0_pub_key(dh), dh);

	printf("key1 = ");
	for (i = 0; i < secret_len; i++) {
		printf("%02X", buf[i]);
	}
	printf("\n");

	if (secret_len < 4) {
		fprintf(stderr, "Error in DH routines\n");
		goto err;
	}

	ret = 0;
err:
	ERR_print_errors_fp(stderr);

	free(buf);
	DH_free(dh);
	BN_GENCB_free(_cb);

	return (ret);
}
