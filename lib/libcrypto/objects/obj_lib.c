/* $OpenBSD: obj_lib.c,v 1.20 2025/05/10 05:54:38 tb Exp $ */
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
#include <string.h>

#include <openssl/buffer.h>
#include <openssl/lhash.h>
#include <openssl/objects.h>

#include "asn1_local.h"
#include "err_local.h"

ASN1_OBJECT *
OBJ_dup(const ASN1_OBJECT *o)
{
	ASN1_OBJECT *r;
	char *ln = NULL, *sn = NULL;
	unsigned char *data = NULL;

	if (o == NULL)
		return (NULL);
	if (!(o->flags & ASN1_OBJECT_FLAG_DYNAMIC))
		return((ASN1_OBJECT *)o); /* XXX: ugh! Why? What kind of
					     duplication is this??? */

	r = ASN1_OBJECT_new();
	if (r == NULL) {
		OBJerror(ERR_R_ASN1_LIB);
		return (NULL);
	}
	data = malloc(o->length);
	if (data == NULL)
		goto err;
	if (o->data != NULL)
		memcpy(data, o->data, o->length);
	/* once data attached to object it remains const */
	r->data = data;
	r->length = o->length;
	r->nid = o->nid;
	r->ln = r->sn = NULL;
	if (o->ln != NULL) {
		ln = strdup(o->ln);
		if (ln == NULL)
			goto err;
		r->ln = ln;
	}

	if (o->sn != NULL) {
		sn = strdup(o->sn);
		if (sn == NULL)
			goto err;
		r->sn = sn;
	}
	r->flags = o->flags | (ASN1_OBJECT_FLAG_DYNAMIC |
	    ASN1_OBJECT_FLAG_DYNAMIC_STRINGS | ASN1_OBJECT_FLAG_DYNAMIC_DATA);
	return (r);

 err:
	OBJerror(ERR_R_MALLOC_FAILURE);
	free(ln);
	free(sn);
	free(data);
	free(r);
	return (NULL);
}
LCRYPTO_ALIAS(OBJ_dup);

int
OBJ_cmp(const ASN1_OBJECT *a, const ASN1_OBJECT *b)
{
	int cmp;

	if ((cmp = a->length - b->length) != 0)
		return cmp;
	if (a->length == 0)
		return 0;
	return memcmp(a->data, b->data, a->length);
}
LCRYPTO_ALIAS(OBJ_cmp);
