/* $OpenBSD: tasn_fre.c,v 1.25 2025/08/14 19:02:17 tb Exp $ */
/* Written by Dr Stephen N Henson (steve@openssl.org) for the OpenSSL
 * project 2000.
 */
/* ====================================================================
 * Copyright (c) 2000 The OpenSSL Project.  All rights reserved.
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


#include <stddef.h>
#include <openssl/asn1.h>
#include <openssl/asn1t.h>
#include <openssl/objects.h>

#include "asn1_local.h"

static void asn1_item_free(ASN1_VALUE **pval, const ASN1_ITEM *it);

/* Free up an ASN1 structure */

void
ASN1_item_free(ASN1_VALUE *val, const ASN1_ITEM *it)
{
	asn1_item_free(&val, it);
}
LCRYPTO_ALIAS(ASN1_item_free);

void
ASN1_item_ex_free(ASN1_VALUE **pval, const ASN1_ITEM *it)
{
	asn1_item_free(pval, it);
}
LCRYPTO_ALIAS(ASN1_item_ex_free);

static void
asn1_item_free(ASN1_VALUE **pval, const ASN1_ITEM *it)
{
	const ASN1_TEMPLATE *tt = NULL, *seqtt;
	const ASN1_EXTERN_FUNCS *ef;
	const ASN1_AUX *aux = it->funcs;
	ASN1_aux_cb *asn1_cb = NULL;
	int i;

	if (pval == NULL)
		return;
	/* For primitive types *pval may be something other than C pointer. */
	if (it->itype != ASN1_ITYPE_PRIMITIVE && *pval == NULL)
		return;

	if (aux != NULL && aux->asn1_cb != NULL)
		asn1_cb = aux->asn1_cb;

	switch (it->itype) {
	case ASN1_ITYPE_PRIMITIVE:
		if (it->templates)
			ASN1_template_free(pval, it->templates);
		else
			ASN1_primitive_free(pval, it);
		break;

	case ASN1_ITYPE_MSTRING:
		ASN1_primitive_free(pval, it);
		break;

	case ASN1_ITYPE_CHOICE:
		if (asn1_cb) {
			i = asn1_cb(ASN1_OP_FREE_PRE, pval, it, NULL);
			if (i == 2)
				return;
		}
		i = asn1_get_choice_selector(pval, it);
		if ((i >= 0) && (i < it->tcount)) {
			ASN1_VALUE **pchval;
			tt = it->templates + i;
			pchval = asn1_get_field_ptr(pval, tt);
			ASN1_template_free(pchval, tt);
		}
		if (asn1_cb)
			asn1_cb(ASN1_OP_FREE_POST, pval, it, NULL);
		free(*pval);
		*pval = NULL;
		break;

	case ASN1_ITYPE_EXTERN:
		ef = it->funcs;
		if (ef && ef->asn1_ex_free)
			ef->asn1_ex_free(pval, it);
		break;

	case ASN1_ITYPE_NDEF_SEQUENCE:
	case ASN1_ITYPE_SEQUENCE:
		if (asn1_do_lock(pval, -1, it) > 0)
			return;
		if (asn1_cb) {
			i = asn1_cb(ASN1_OP_FREE_PRE, pval, it, NULL);
			if (i == 2)
				return;
		}
		asn1_enc_cleanup(pval, it);
		/*
		 * If we free up as normal, we will invalidate any
		 * ANY DEFINED BY field and we won't be able to
		 * determine the type of the field it defines. So
		 * free up in reverse order.
		 */
		for (i = it->tcount - 1; i >= 0; i--) {
			ASN1_VALUE **pseqval;
			seqtt = asn1_do_adb(pval, &it->templates[i], 0);
			if (!seqtt)
				continue;
			pseqval = asn1_get_field_ptr(pval, seqtt);
			ASN1_template_free(pseqval, seqtt);
		}
		if (asn1_cb)
			asn1_cb(ASN1_OP_FREE_POST, pval, it, NULL);
		free(*pval);
		*pval = NULL;
		break;
	}
}

void
ASN1_template_free(ASN1_VALUE **pval, const ASN1_TEMPLATE *tt)
{
	int i;
	if (tt->flags & ASN1_TFLG_SK_MASK) {
		STACK_OF(ASN1_VALUE) *sk = (STACK_OF(ASN1_VALUE) *)*pval;
		for (i = 0; i < sk_ASN1_VALUE_num(sk); i++) {
			ASN1_VALUE *vtmp;
			vtmp = sk_ASN1_VALUE_value(sk, i);
			asn1_item_free(&vtmp, tt->item);
		}
		sk_ASN1_VALUE_free(sk);
		*pval = NULL;
	} else
		asn1_item_free(pval, tt->item);
}

void
ASN1_primitive_free(ASN1_VALUE **pval, const ASN1_ITEM *it)
{
	int utype;

	if (it != NULL && it->funcs != NULL) {
		const ASN1_PRIMITIVE_FUNCS *pf = it->funcs;

		pf->prim_free(pval, it);
		return;
	}

	/* Special case: if 'it' is NULL free contents of ASN1_TYPE */
	if (!it) {
		ASN1_TYPE *typ = (ASN1_TYPE *)*pval;
		utype = typ->type;
		pval = &typ->value.asn1_value;
		if (!*pval)
			return;
	} else if (it->itype == ASN1_ITYPE_MSTRING) {
		utype = -1;
		if (!*pval)
			return;
	} else {
		utype = it->utype;
		if ((utype != V_ASN1_BOOLEAN) && !*pval)
			return;
	}

	switch (utype) {
	case V_ASN1_OBJECT:
		ASN1_OBJECT_free((ASN1_OBJECT *)*pval);
		break;

	case V_ASN1_BOOLEAN:
		if (it)
			*(ASN1_BOOLEAN *)pval = it->size;
		else
			*(ASN1_BOOLEAN *)pval = -1;
		return;

	case V_ASN1_NULL:
		break;

	case V_ASN1_ANY:
		ASN1_primitive_free(pval, NULL);
		free(*pval);
		break;

	default:
		ASN1_STRING_free((ASN1_STRING *)*pval);
		break;
	}
	*pval = NULL;
}
