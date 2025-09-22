/* $OpenBSD: ts_req_print.c,v 1.5 2023/07/07 07:25:21 beck Exp $ */
/* Written by Zoltan Glozik (zglozik@stones.com) for the OpenSSL
 * project 2002.
 */
/* ====================================================================
 * Copyright (c) 2006 The OpenSSL Project.  All rights reserved.
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

#include <stdio.h>

#include <openssl/bn.h>
#include <openssl/objects.h>
#include <openssl/ts.h>
#include <openssl/x509v3.h>

/* Function definitions. */

int
TS_REQ_print_bio(BIO *bio, TS_REQ *a)
{
	int v;
	ASN1_OBJECT *policy_id;
	const ASN1_INTEGER *nonce;

	if (a == NULL)
		return 0;

	v = TS_REQ_get_version(a);
	BIO_printf(bio, "Version: %d\n", v);

	TS_MSG_IMPRINT_print_bio(bio, TS_REQ_get_msg_imprint(a));

	BIO_printf(bio, "Policy OID: ");
	policy_id = TS_REQ_get_policy_id(a);
	if (policy_id == NULL)
		BIO_printf(bio, "unspecified\n");
	else
		TS_OBJ_print_bio(bio, policy_id);

	BIO_printf(bio, "Nonce: ");
	nonce = TS_REQ_get_nonce(a);
	if (nonce == NULL)
		BIO_printf(bio, "unspecified");
	else
		TS_ASN1_INTEGER_print_bio(bio, nonce);
	BIO_write(bio, "\n", 1);

	BIO_printf(bio, "Certificate required: %s\n",
	    TS_REQ_get_cert_req(a) ? "yes" : "no");

	TS_ext_print_bio(bio, TS_REQ_get_exts(a));

	return 1;
}
LCRYPTO_ALIAS(TS_REQ_print_bio);
