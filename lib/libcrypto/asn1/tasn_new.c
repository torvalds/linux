/* $OpenBSD: tasn_new.c,v 1.26 2025/05/10 05:54:38 tb Exp $ */
/* Written by Dr Stephen N Henson (steve@openssl.org) for the OpenSSL
 * project 2000.
 */
/* ====================================================================
 * Copyright (c) 2000-2004 The OpenSSL Project.  All rights reserved.
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
#include <openssl/objects.h>
#include <openssl/asn1t.h>
#include <string.h>

#include "asn1_local.h"
#include "err_local.h"

static int asn1_item_ex_new(ASN1_VALUE **pval, const ASN1_ITEM *it);
static void asn1_item_clear(ASN1_VALUE **pval, const ASN1_ITEM *it);
static void asn1_template_clear(ASN1_VALUE **pval, const ASN1_TEMPLATE *tt);
static void asn1_primitive_clear(ASN1_VALUE **pval, const ASN1_ITEM *it);

ASN1_VALUE *
ASN1_item_new(const ASN1_ITEM *it)
{
	ASN1_VALUE *ret = NULL;
	if (ASN1_item_ex_new(&ret, it) > 0)
		return ret;
	return NULL;
}
LCRYPTO_ALIAS(ASN1_item_new);

/* Allocate an ASN1 structure */

int
ASN1_item_ex_new(ASN1_VALUE **pval, const ASN1_ITEM *it)
{
	return asn1_item_ex_new(pval, it);
}
LCRYPTO_ALIAS(ASN1_item_ex_new);

static int
asn1_item_ex_new(ASN1_VALUE **pval, const ASN1_ITEM *it)
{
	const ASN1_TEMPLATE *tt = NULL;
	const ASN1_EXTERN_FUNCS *ef;
	const ASN1_AUX *aux = it->funcs;
	ASN1_aux_cb *asn1_cb = NULL;
	ASN1_VALUE **pseqval;
	int i;

	if (aux != NULL && aux->asn1_cb != NULL)
		asn1_cb = aux->asn1_cb;

	*pval = NULL;

	switch (it->itype) {
	case ASN1_ITYPE_EXTERN:
		ef = it->funcs;
		if (ef && ef->asn1_ex_new) {
			if (!ef->asn1_ex_new(pval, it))
				goto memerr;
		}
		break;

	case ASN1_ITYPE_PRIMITIVE:
		if (it->templates) {
			if (!ASN1_template_new(pval, it->templates))
				goto memerr;
		} else if (!ASN1_primitive_new(pval, it))
			goto memerr;
		break;

	case ASN1_ITYPE_MSTRING:
		if (!ASN1_primitive_new(pval, it))
			goto memerr;
		break;

	case ASN1_ITYPE_CHOICE:
		if (asn1_cb) {
			i = asn1_cb(ASN1_OP_NEW_PRE, pval, it, NULL);
			if (!i)
				goto auxerr;
			if (i == 2) {
				return 1;
			}
		}
		*pval = calloc(1, it->size);
		if (!*pval)
			goto memerr;
		asn1_set_choice_selector(pval, -1, it);
		if (asn1_cb && !asn1_cb(ASN1_OP_NEW_POST, pval, it, NULL))
			goto auxerr;
		break;

	case ASN1_ITYPE_NDEF_SEQUENCE:
	case ASN1_ITYPE_SEQUENCE:
		if (asn1_cb) {
			i = asn1_cb(ASN1_OP_NEW_PRE, pval, it, NULL);
			if (!i)
				goto auxerr;
			if (i == 2) {
				return 1;
			}
		}
		*pval = calloc(1, it->size);
		if (!*pval)
			goto memerr;
		asn1_do_lock(pval, 0, it);
		asn1_enc_init(pval, it);
		for (i = 0, tt = it->templates; i < it->tcount; tt++, i++) {
			pseqval = asn1_get_field_ptr(pval, tt);
			if (!ASN1_template_new(pseqval, tt))
				goto memerr;
		}
		if (asn1_cb && !asn1_cb(ASN1_OP_NEW_POST, pval, it, NULL))
			goto auxerr;
		break;
	}
	return 1;

 memerr:
	ASN1error(ERR_R_MALLOC_FAILURE);
	return 0;

 auxerr:
	ASN1error(ASN1_R_AUX_ERROR);
	ASN1_item_ex_free(pval, it);
	return 0;

}

static void
asn1_item_clear(ASN1_VALUE **pval, const ASN1_ITEM *it)
{
	const ASN1_EXTERN_FUNCS *ef;

	switch (it->itype) {
	case ASN1_ITYPE_EXTERN:
		ef = it->funcs;
		if (ef && ef->asn1_ex_clear)
			ef->asn1_ex_clear(pval, it);
		else
			*pval = NULL;
		break;

	case ASN1_ITYPE_PRIMITIVE:
		if (it->templates)
			asn1_template_clear(pval, it->templates);
		else
			asn1_primitive_clear(pval, it);
		break;

	case ASN1_ITYPE_MSTRING:
		asn1_primitive_clear(pval, it);
		break;

	case ASN1_ITYPE_CHOICE:
	case ASN1_ITYPE_SEQUENCE:
	case ASN1_ITYPE_NDEF_SEQUENCE:
		*pval = NULL;
		break;
	}
}

int
ASN1_template_new(ASN1_VALUE **pval, const ASN1_TEMPLATE *tt)
{
	const ASN1_ITEM *it = tt->item;
	int ret;

	if (tt->flags & ASN1_TFLG_OPTIONAL) {
		asn1_template_clear(pval, tt);
		return 1;
	}
	/* If ANY DEFINED BY nothing to do */

	if (tt->flags & ASN1_TFLG_ADB_MASK) {
		*pval = NULL;
		return 1;
	}
	/* If SET OF or SEQUENCE OF, its a STACK */
	if (tt->flags & ASN1_TFLG_SK_MASK) {
		STACK_OF(ASN1_VALUE) *skval;
		skval = sk_ASN1_VALUE_new_null();
		if (!skval) {
			ASN1error(ERR_R_MALLOC_FAILURE);
			ret = 0;
			goto done;
		}
		*pval = (ASN1_VALUE *)skval;
		ret = 1;
		goto done;
	}
	/* Otherwise pass it back to the item routine */
	ret = asn1_item_ex_new(pval, it);
 done:
	return ret;
}

static void
asn1_template_clear(ASN1_VALUE **pval, const ASN1_TEMPLATE *tt)
{
	/* If ADB or STACK just NULL the field */
	if (tt->flags & (ASN1_TFLG_ADB_MASK|ASN1_TFLG_SK_MASK))
		*pval = NULL;
	else
		asn1_item_clear(pval, tt->item);
}


/* NB: could probably combine most of the real XXX_new() behaviour and junk
 * all the old functions.
 */

int
ASN1_primitive_new(ASN1_VALUE **pval, const ASN1_ITEM *it)
{
	ASN1_TYPE *typ;
	ASN1_STRING *str;
	int utype;

	if (it != NULL && it->funcs != NULL) {
		const ASN1_PRIMITIVE_FUNCS *pf = it->funcs;

		if (pf->prim_new == NULL)
			return 0;
		return pf->prim_new(pval, it);
	}

	if (!it || (it->itype == ASN1_ITYPE_MSTRING))
		utype = V_ASN1_UNDEF;
	else
		utype = it->utype;
	switch (utype) {
	case V_ASN1_OBJECT:
		*pval = (ASN1_VALUE *)OBJ_nid2obj(NID_undef);
		return 1;

	case V_ASN1_BOOLEAN:
		*(ASN1_BOOLEAN *)pval = it->size;
		return 1;

	case V_ASN1_NULL:
		*pval = (ASN1_VALUE *)1;
		return 1;

	case V_ASN1_ANY:
		typ = malloc(sizeof(ASN1_TYPE));
		if (typ != NULL) {
			typ->value.ptr = NULL;
			typ->type = V_ASN1_UNDEF;
		}
		*pval = (ASN1_VALUE *)typ;
		break;

	default:
		str = ASN1_STRING_type_new(utype);
		if (it != NULL && it->itype == ASN1_ITYPE_MSTRING &&
		    str != NULL)
			str->flags |= ASN1_STRING_FLAG_MSTRING;
		*pval = (ASN1_VALUE *)str;
		break;
	}
	if (*pval)
		return 1;
	return 0;
}

static void
asn1_primitive_clear(ASN1_VALUE **pval, const ASN1_ITEM *it)
{
	int utype;

	if (it != NULL && it->funcs != NULL) {
		const ASN1_PRIMITIVE_FUNCS *pf = it->funcs;

		if (pf->prim_clear)
			pf->prim_clear(pval, it);
		else
			*pval = NULL;
		return;
	}

	if (!it || (it->itype == ASN1_ITYPE_MSTRING))
		utype = V_ASN1_UNDEF;
	else
		utype = it->utype;
	if (utype == V_ASN1_BOOLEAN)
		*(ASN1_BOOLEAN *)pval = it->size;
	else
		*pval = NULL;
}
