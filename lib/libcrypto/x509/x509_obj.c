/* $OpenBSD: x509_obj.c,v 1.25 2025/01/27 04:24:46 tb Exp $ */
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

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <openssl/asn1.h>
#include <openssl/objects.h>
#include <openssl/x509.h>

#include "bytestring.h"
#include "x509_local.h"

static int
X509_NAME_ENTRY_add_object_cbb(CBB *cbb, const ASN1_OBJECT *aobj)
{
	const char *str;
	char buf[80];
	int nid;

	/* Prefer SN over LN, and fall back to textual representation of OID. */
	if ((nid = OBJ_obj2nid(aobj)) != NID_undef) {
		if ((str = OBJ_nid2sn(nid)) != NULL)
			return CBB_add_bytes(cbb, str, strlen(str));
		if ((str = OBJ_nid2ln(nid)) != NULL)
			return CBB_add_bytes(cbb, str, strlen(str));
	}
	if (OBJ_obj2txt(buf, sizeof(buf), aobj, 1) == 0)
		return 0;
	return CBB_add_bytes(cbb, buf, strlen(buf));
}

static int
X509_NAME_ENTRY_add_u8_cbb(CBB *cbb, uint8_t u8)
{
	static const char hex[] = "0123456789ABCDEF";

	if (' ' <= u8 && u8 <= '~')
		return CBB_add_u8(cbb, u8);

	if (!CBB_add_u8(cbb, '\\'))
		return 0;
	if (!CBB_add_u8(cbb, 'x'))
		return 0;
	if (!CBB_add_u8(cbb, hex[u8 >> 4]))
		return 0;
	if (!CBB_add_u8(cbb, hex[u8 & 0xf]))
		return 0;
	return 1;
}

static int
X509_NAME_ENTRY_add_value_cbb(CBB *cbb, const ASN1_STRING *astr)
{
	CBS cbs;
	uint8_t u8;
	size_t i;
	int mask[4] = { 1, 1, 1, 1 };

	if (astr->type == V_ASN1_GENERALSTRING && astr->length % 4 == 0) {
		int gs_mask[4] = { 0, 0, 0, 0 };

		i = 0;
		CBS_init(&cbs, astr->data, astr->length);
		while (CBS_len(&cbs) > 0) {
			if (!CBS_get_u8(&cbs, &u8))
				return 0;

			gs_mask[i++ & 0x3] |= u8;
		}

		if (gs_mask[0] == 0 && gs_mask[1] == 0 && gs_mask[2] == 0)
			mask[0] = mask[1] = mask[2] = 0;
	}

	i = 0;
	CBS_init(&cbs, astr->data, astr->length);
	while (CBS_len(&cbs) > 0) {
		if (!CBS_get_u8(&cbs, &u8))
			return 0;
		if (mask[i++ & 0x3] == 0)
			continue;
		if (!X509_NAME_ENTRY_add_u8_cbb(cbb, u8))
			return 0;
	}

	return 1;
}

int
X509_NAME_ENTRY_add_cbb(CBB *cbb, const X509_NAME_ENTRY *ne)
{
	if (!X509_NAME_ENTRY_add_object_cbb(cbb, ne->object))
		return 0;
	if (!CBB_add_u8(cbb, '='))
		return 0;
	if (!X509_NAME_ENTRY_add_value_cbb(cbb, ne->value))
		return 0;
	return 1;
}

char *
X509_NAME_oneline(const X509_NAME *a, char *buf, int len)
{
	CBB cbb;
	const X509_NAME_ENTRY *ne;
	uint8_t *line = NULL;
	size_t line_len = 0;
	int i;

	if (!CBB_init(&cbb, 0))
		goto err;

	for (i = 0; i < sk_X509_NAME_ENTRY_num(a->entries); i++) {
		ne = sk_X509_NAME_ENTRY_value(a->entries, i);
		if (!CBB_add_u8(&cbb, '/'))
			goto err;
		if (!X509_NAME_ENTRY_add_cbb(&cbb, ne))
			goto err;
	}

	if (!CBB_add_u8(&cbb, '\0'))
		goto err;

	if (!CBB_finish(&cbb, &line, &line_len))
		goto err;

	if (buf == NULL)
		return line;

	strlcpy(buf, line, len);
	free(line);

	return buf;

 err:
	CBB_cleanup(&cbb);

	return NULL;
}
LCRYPTO_ALIAS(X509_NAME_oneline);
