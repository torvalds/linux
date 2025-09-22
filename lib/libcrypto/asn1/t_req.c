/* $OpenBSD: t_req.c,v 1.29 2025/05/10 05:54:38 tb Exp $ */
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

#include <openssl/opensslconf.h>

#include <openssl/bn.h>
#include <openssl/buffer.h>
#include <openssl/objects.h>
#include <openssl/x509.h>
#include <openssl/x509v3.h>

#ifndef OPENSSL_NO_DSA
#include <openssl/dsa.h>
#endif
#ifndef OPENSSL_NO_RSA
#include <openssl/rsa.h>
#endif

#include "err_local.h"
#include "x509_local.h"

int
X509_REQ_print_fp(FILE *fp, X509_REQ *x)
{
	BIO *b;
	int ret;

	if ((b = BIO_new(BIO_s_file())) == NULL) {
		X509error(ERR_R_BUF_LIB);
		return (0);
	}
	BIO_set_fp(b, fp, BIO_NOCLOSE);
	ret = X509_REQ_print(b, x);
	BIO_free(b);
	return (ret);
}
LCRYPTO_ALIAS(X509_REQ_print_fp);

int
X509_REQ_print_ex(BIO *bp, X509_REQ *x, unsigned long nmflags,
    unsigned long cflag)
{
	unsigned long l;
	int i;
	X509_REQ_INFO *ri;
	EVP_PKEY *pkey;
	STACK_OF(X509_ATTRIBUTE) *sk;
	STACK_OF(X509_EXTENSION) *exts = NULL;
	char mlch = ' ';
	int nmindent = 0;

	if ((nmflags & XN_FLAG_SEP_MASK) == XN_FLAG_SEP_MULTILINE) {
		mlch = '\n';
		nmindent = 12;
	}

	if (nmflags == X509_FLAG_COMPAT)
		nmindent = 16;

	ri = x->req_info;
	if (!(cflag & X509_FLAG_NO_HEADER)) {
		if (BIO_write(bp, "Certificate Request:\n", 21) <= 0)
			goto err;
		if (BIO_write(bp, "    Data:\n", 10) <= 0)

			goto err;
	}
	if (!(cflag & X509_FLAG_NO_VERSION)) {
		if ((l = X509_REQ_get_version(x)) == 0) {
			if (BIO_printf(bp, "%8sVersion: 1 (0x0)\n", "") <= 0)
				goto err;
		} else {
			if (BIO_printf(bp, "%8sVersion: unknown (%ld)\n",
			    "", l) <= 0)
				goto err;
		}
	}
	if (!(cflag & X509_FLAG_NO_SUBJECT)) {
		if (BIO_printf(bp, "        Subject:%c", mlch) <= 0)
			goto err;
		if (X509_NAME_print_ex(bp, ri->subject, nmindent, nmflags) < 0)
			goto err;
		if (BIO_write(bp, "\n", 1) <= 0)
			goto err;
	}
	if (!(cflag & X509_FLAG_NO_PUBKEY)) {
		if (BIO_write(bp, "        Subject Public Key Info:\n",
		    33) <= 0)
			goto err;
		if (BIO_printf(bp, "%12sPublic Key Algorithm: ", "") <= 0)
			goto err;
		if (i2a_ASN1_OBJECT(bp, ri->pubkey->algor->algorithm) <= 0)
			goto err;
		if (BIO_puts(bp, "\n") <= 0)
			goto err;

		pkey = X509_REQ_get_pubkey(x);
		if (pkey == NULL) {
			BIO_printf(bp, "%12sUnable to load Public Key\n", "");
			ERR_print_errors(bp);
		} else {
			EVP_PKEY_print_public(bp, pkey, 16, NULL);
			EVP_PKEY_free(pkey);
		}
	}

	if (!(cflag & X509_FLAG_NO_ATTRIBUTES)) {
		/* may not be */
		if (BIO_printf(bp, "%8sAttributes:\n", "") <= 0)
			goto err;

		sk = x->req_info->attributes;
		if (sk_X509_ATTRIBUTE_num(sk) == 0) {
			if (BIO_printf(bp, "%12sa0:00\n", "") <= 0)
				goto err;
		} else {
			for (i = 0; i < sk_X509_ATTRIBUTE_num(sk); i++) {
				ASN1_TYPE *at;
				X509_ATTRIBUTE *a;
				ASN1_BIT_STRING *bs = NULL;
				int j, type = 0, count = 1, ii = 0;

				a = sk_X509_ATTRIBUTE_value(sk, i);
				if (X509_REQ_extension_nid(
				    OBJ_obj2nid(a->object)))
					continue;
				if (BIO_printf(bp, "%12s", "") <= 0)
					goto err;
				if ((j = i2a_ASN1_OBJECT(bp, a->object)) > 0) {
					ii = 0;
					count = sk_ASN1_TYPE_num(a->set);
 get_next:
					at = sk_ASN1_TYPE_value(a->set, ii);
					type = at->type;
					bs = at->value.asn1_string;
				}
				for (j = 25 - j; j > 0; j--)
					if (BIO_write(bp, " ", 1) != 1)
						goto err;
				if (BIO_puts(bp, ":") <= 0)
					goto err;
				if ((type == V_ASN1_PRINTABLESTRING) ||
				    (type == V_ASN1_T61STRING) ||
				    (type == V_ASN1_IA5STRING)) {
					if (BIO_write(bp, (char *)bs->data,
					    bs->length) != bs->length)
						goto err;
					BIO_puts(bp, "\n");
				} else {
					BIO_puts(bp,
					    "unable to print attribute\n");
				}
				if (++ii < count)
					goto get_next;
			}
		}
	}
	if (!(cflag & X509_FLAG_NO_EXTENSIONS)) {
		exts = X509_REQ_get_extensions(x);
		if (exts) {
			BIO_printf(bp, "%8sRequested Extensions:\n", "");
			for (i = 0; i < sk_X509_EXTENSION_num(exts); i++) {
				ASN1_OBJECT *obj;
				X509_EXTENSION *ex;
				int j;
				ex = sk_X509_EXTENSION_value(exts, i);
				if (BIO_printf(bp, "%12s", "") <= 0)
					goto err;
				obj = X509_EXTENSION_get_object(ex);
				i2a_ASN1_OBJECT(bp, obj);
				j = X509_EXTENSION_get_critical(ex);
				if (BIO_printf(bp, ": %s\n",
				    j ? "critical" : "") <= 0)
					goto err;
				if (!X509V3_EXT_print(bp, ex, cflag, 16)) {
					BIO_printf(bp, "%16s", "");
					ASN1_STRING_print(bp, ex->value);
				}
				if (BIO_write(bp, "\n", 1) <= 0)
					goto err;
			}
			sk_X509_EXTENSION_pop_free(exts, X509_EXTENSION_free);
			exts = NULL;
		}
	}

	if (!(cflag & X509_FLAG_NO_SIGDUMP)) {
		if (!X509_signature_print(bp, x->sig_alg, x->signature))
			goto err;
	}

	return (1);

 err:
	sk_X509_EXTENSION_pop_free(exts, X509_EXTENSION_free);
	X509error(ERR_R_BUF_LIB);
	return (0);
}
LCRYPTO_ALIAS(X509_REQ_print_ex);

int
X509_REQ_print(BIO *bp, X509_REQ *x)
{
	return X509_REQ_print_ex(bp, x, XN_FLAG_COMPAT, X509_FLAG_COMPAT);
}
LCRYPTO_ALIAS(X509_REQ_print);
