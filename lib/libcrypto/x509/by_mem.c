/* $OpenBSD: by_mem.c,v 1.11 2025/05/10 05:54:39 tb Exp $ */
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

#include <sys/uio.h>
#include <errno.h>
#include <stdio.h>
#include <time.h>
#include <unistd.h>

#include <openssl/buffer.h>
#include <openssl/pem.h>
#include <openssl/lhash.h>
#include <openssl/x509.h>

#include "err_local.h"
#include "x509_local.h"

static int by_mem_ctrl(X509_LOOKUP *, int, const char *, long, char **);

static const X509_LOOKUP_METHOD x509_mem_lookup = {
	.name = "Load cert from memory",
	.new_item = NULL,
	.free = NULL,
	.ctrl = by_mem_ctrl,
	.get_by_subject = NULL,
};

const X509_LOOKUP_METHOD *
X509_LOOKUP_mem(void)
{
	return (&x509_mem_lookup);
}
LCRYPTO_ALIAS(X509_LOOKUP_mem);

static int
by_mem_ctrl(X509_LOOKUP *lu, int cmd, const char *buf,
    long type, char **ret)
{
	STACK_OF(X509_INFO)	*inf = NULL;
	const struct iovec	*iov;
	X509_INFO		*itmp;
	BIO			*in = NULL;
	int			 i, count = 0, ok = 0;

	iov = (const struct iovec *)buf;

	if (!(cmd == X509_L_MEM && type == X509_FILETYPE_PEM))
		goto done;

	if ((in = BIO_new_mem_buf(iov->iov_base, iov->iov_len)) == NULL)
		goto done;

	if ((inf = PEM_X509_INFO_read_bio(in, NULL, NULL, NULL)) == NULL)
		goto done;

	for (i = 0; i < sk_X509_INFO_num(inf); i++) {
		itmp = sk_X509_INFO_value(inf, i);
		if (itmp->x509) {
			ok = X509_STORE_add_cert(lu->store_ctx, itmp->x509);
			if (!ok)
				goto done;
			count++;
		}
		if (itmp->crl) {
			ok = X509_STORE_add_crl(lu->store_ctx, itmp->crl);
			if (!ok)
				goto done;
			count++;
		}
	}

	ok = count != 0;
 done:
	if (count == 0)
		X509error(ERR_R_PEM_LIB);
	if (inf != NULL)
		sk_X509_INFO_pop_free(inf, X509_INFO_free);
	if (in != NULL)
		BIO_free(in);
	return (ok);
}
