/* $OpenBSD: tasn_dec.c,v 1.89 2025/05/10 05:54:38 tb Exp $ */
/* Written by Dr Stephen N Henson (steve@openssl.org) for the OpenSSL
 * project 2000.
 */
/* ====================================================================
 * Copyright (c) 2000-2005 The OpenSSL Project.  All rights reserved.
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

#include <limits.h>
#include <stddef.h>
#include <string.h>

#include <openssl/asn1.h>
#include <openssl/asn1t.h>
#include <openssl/buffer.h>
#include <openssl/objects.h>

#include "asn1_local.h"
#include "bytestring.h"
#include "err_local.h"

/*
 * Constructed types with a recursive definition (such as can be found in PKCS7)
 * could eventually exceed the stack given malicious input with excessive
 * recursion. Therefore we limit the stack depth.
 */
#define ASN1_MAX_CONSTRUCTED_NEST 30

#ifndef ASN1_MAX_STRING_NEST
/*
 * This determines how many levels of recursion are permitted in ASN.1 string
 * types. If it is not limited stack overflows can occur. If set to zero no
 * recursion is allowed at all.
 */
#define ASN1_MAX_STRING_NEST 5
#endif

static int asn1_template_d2i(ASN1_VALUE **pval, CBS *cbs,
    const ASN1_TEMPLATE *at, int optional, int depth);

static int
asn1_check_eoc(CBS *cbs)
{
	uint16_t eoc;

	if (!CBS_peek_u16(cbs, &eoc))
		return 0;
	if (eoc != 0)
		return 0;

	return CBS_skip(cbs, 2);
}

static int
asn1_check_tag(CBS *cbs, size_t *out_len, int *out_tag, uint8_t *out_class,
    int *out_indefinite, int *out_constructed, int expected_tag,
    int expected_class, int optional)
{
	int constructed, indefinite;
	uint32_t tag_number;
	uint8_t tag_class;
	size_t length;

	if (out_len != NULL)
		*out_len = 0;
	if (out_tag != NULL)
		*out_tag = 0;
	if (out_class != NULL)
		*out_class = 0;
	if (out_indefinite != NULL)
		*out_indefinite = 0;
	if (out_constructed != NULL)
		*out_constructed = 0;

	if (!asn1_get_identifier_cbs(cbs, 0, &tag_class, &constructed,
	    &tag_number)) {
		ASN1error(ASN1_R_BAD_OBJECT_HEADER);
		return 0;
	}
	if (expected_tag >= 0) {
		if (expected_tag != tag_number ||
		    expected_class != tag_class << 6) {
			/* Indicate missing type if this is OPTIONAL. */
			if (optional)
				return -1;

			ASN1error(ASN1_R_WRONG_TAG);
			return 0;
		}
	}
	if (!asn1_get_length_cbs(cbs, 0, &indefinite, &length)) {
		ASN1error(ASN1_R_BAD_OBJECT_HEADER);
		return 0;
	}

	/* Indefinite length can only be used with constructed encoding. */
	if (indefinite && !constructed) {
		ASN1error(ASN1_R_BAD_OBJECT_HEADER);
		return 0;
	}

	if (!indefinite && CBS_len(cbs) < length) {
		ASN1error(ASN1_R_TOO_LONG);
		return 0;
	}

	if (tag_number > INT_MAX) {
		ASN1error(ASN1_R_TOO_LONG);
		return 0;
	}

	if (indefinite)
		length = CBS_len(cbs);

	if (out_len != NULL)
		*out_len = length;
	if (out_tag != NULL)
		*out_tag = tag_number;
	if (out_class != NULL)
		*out_class = tag_class << 6;
	if (out_indefinite != NULL)
		*out_indefinite = indefinite;
	if (out_constructed != NULL)
		*out_constructed = constructed;

	return 1;
}

/* Collect the contents from a constructed ASN.1 object. */
static int
asn1_collect(CBB *cbb, CBS *cbs, int indefinite, int expected_tag,
    int expected_class, int depth)
{
	int constructed;
	size_t length;
	CBS content;
	int need_eoc;

	if (depth > ASN1_MAX_STRING_NEST) {
		ASN1error(ASN1_R_NESTED_ASN1_STRING);
		return 0;
	}

	need_eoc = indefinite;

	while (CBS_len(cbs) > 0) {
		if (asn1_check_eoc(cbs)) {
			if (!need_eoc) {
				ASN1error(ASN1_R_UNEXPECTED_EOC);
				return 0;
			}
			return 1;
		}
		if (!asn1_check_tag(cbs, &length, NULL, NULL, &indefinite,
		    &constructed, expected_tag, expected_class, 0)) {
			ASN1error(ERR_R_NESTED_ASN1_ERROR);
			return 0;
		}

		if (constructed) {
			if (!asn1_collect(cbb, cbs, indefinite, expected_tag,
			    expected_class, depth + 1))
				return 0;
			continue;
		}

		if (!CBS_get_bytes(cbs, &content, length)) {
			ASN1error(ERR_R_NESTED_ASN1_ERROR);
			return 0;
		}
		if (!CBB_add_bytes(cbb, CBS_data(&content), CBS_len(&content)))
			return 0;
	}

	if (need_eoc) {
		ASN1error(ASN1_R_MISSING_EOC);
		return 0;
	}

	return 1;
}

/* Find the end of an ASN.1 object. */
static int
asn1_find_end(CBS *cbs, size_t length, int indefinite)
{
	size_t eoc_count;

	if (!indefinite) {
		if (!CBS_skip(cbs, length)) {
			ASN1error(ERR_R_NESTED_ASN1_ERROR);
			return 0;
		}
		return 1;
	}

	eoc_count = 1;

	while (CBS_len(cbs) > 0) {
		if (asn1_check_eoc(cbs)) {
			if (--eoc_count == 0)
				break;
			continue;
		}
		if (!asn1_check_tag(cbs, &length, NULL, NULL,
		    &indefinite, NULL, -1, 0, 0)) {
			ASN1error(ERR_R_NESTED_ASN1_ERROR);
			return 0;
		}
		if (indefinite) {
			eoc_count++;
			continue;
		}
		if (!CBS_skip(cbs, length))
			return 0;
	}

	if (eoc_count > 0) {
		ASN1error(ASN1_R_MISSING_EOC);
		return 0;
	}

	return 1;
}

static int
asn1_c2i_primitive(ASN1_VALUE **pval, CBS *content, int utype, const ASN1_ITEM *it)
{
	ASN1_BOOLEAN *abool;
	ASN1_STRING *astr;
	uint8_t val;
	int ret = 0;

	if (it->funcs != NULL)
		goto err;

	if (CBS_len(content) > INT_MAX)
		goto err;

	switch (utype) {
	case V_ASN1_OBJECT:
		if (!c2i_ASN1_OBJECT_cbs((ASN1_OBJECT **)pval, content))
			goto err;
		break;

	case V_ASN1_NULL:
		if (CBS_len(content) != 0) {
			ASN1error(ASN1_R_NULL_IS_WRONG_LENGTH);
			goto err;
		}
		*pval = (ASN1_VALUE *)1;
		break;

	case V_ASN1_BOOLEAN:
		abool = (ASN1_BOOLEAN *)pval;
		if (CBS_len(content) != 1) {
			ASN1error(ASN1_R_BOOLEAN_IS_WRONG_LENGTH);
			goto err;
		}
		if (!CBS_get_u8(content, &val))
			goto err;
		*abool = val;
		break;

	case V_ASN1_BIT_STRING:
		if (!c2i_ASN1_BIT_STRING_cbs((ASN1_BIT_STRING **)pval, content))
			goto err;
		break;

	case V_ASN1_ENUMERATED:
		if (!c2i_ASN1_ENUMERATED_cbs((ASN1_ENUMERATED **)pval, content))
			goto err;
		break;

	case V_ASN1_INTEGER:
		if (!c2i_ASN1_INTEGER_cbs((ASN1_INTEGER **)pval, content))
			goto err;
		break;

	case V_ASN1_OCTET_STRING:
	case V_ASN1_NUMERICSTRING:
	case V_ASN1_PRINTABLESTRING:
	case V_ASN1_T61STRING:
	case V_ASN1_VIDEOTEXSTRING:
	case V_ASN1_IA5STRING:
	case V_ASN1_UTCTIME:
	case V_ASN1_GENERALIZEDTIME:
	case V_ASN1_GRAPHICSTRING:
	case V_ASN1_VISIBLESTRING:
	case V_ASN1_GENERALSTRING:
	case V_ASN1_UNIVERSALSTRING:
	case V_ASN1_BMPSTRING:
	case V_ASN1_UTF8STRING:
	case V_ASN1_OTHER:
	case V_ASN1_SET:
	case V_ASN1_SEQUENCE:
	default:
		if (utype == V_ASN1_BMPSTRING && (CBS_len(content) & 1)) {
			ASN1error(ASN1_R_BMPSTRING_IS_WRONG_LENGTH);
			goto err;
		}
		if (utype == V_ASN1_UNIVERSALSTRING && (CBS_len(content) & 3)) {
			ASN1error(ASN1_R_UNIVERSALSTRING_IS_WRONG_LENGTH);
			goto err;
		}
		if (utype == V_ASN1_UTCTIME || utype == V_ASN1_GENERALIZEDTIME) {
			if (!asn1_time_parse_cbs(content,
			    utype == V_ASN1_GENERALIZEDTIME, NULL))  {
				ASN1error(ASN1_R_INVALID_TIME_FORMAT);
				goto err;
			}
		}
		/* All based on ASN1_STRING and handled the same way. */
		if (*pval != NULL) {
			ASN1_STRING_free((ASN1_STRING *)*pval);
			*pval = NULL;
		}
		if ((astr = ASN1_STRING_type_new(utype)) == NULL) {
			ASN1error(ERR_R_MALLOC_FAILURE);
			goto err;
		}
		if (!ASN1_STRING_set(astr, CBS_data(content), CBS_len(content))) {
			ASN1_STRING_free(astr);
			goto err;
		}
		*pval = (ASN1_VALUE *)astr;
		break;
	}

	ret = 1;

 err:
	return ret;
}

static int
asn1_c2i_any(ASN1_VALUE **pval, CBS *content, int utype, const ASN1_ITEM *it)
{
	ASN1_TYPE *atype;

	if (it->utype != V_ASN1_ANY || it->funcs != NULL)
		return 0;

	if (*pval != NULL) {
		ASN1_TYPE_free((ASN1_TYPE *)*pval);
		*pval = NULL;
	}

	if ((atype = ASN1_TYPE_new()) == NULL)
		return 0;

	if (!asn1_c2i_primitive(&atype->value.asn1_value, content, utype, it)) {
		ASN1_TYPE_free(atype);
		return 0;
	}
	atype->type = utype;

	/* Fix up value for ASN.1 NULL. */
	if (atype->type == V_ASN1_NULL)
		atype->value.ptr = NULL;

	*pval = (ASN1_VALUE *)atype;

	return 1;
}

static int
asn1_c2i(ASN1_VALUE **pval, CBS *content, int utype, const ASN1_ITEM *it)
{
	if (CBS_len(content) > INT_MAX)
		return 0;

	if (it->funcs != NULL) {
		const ASN1_PRIMITIVE_FUNCS *pf = it->funcs;
		char free_content = 0;

		if (pf->prim_c2i == NULL)
			return 0;

		return pf->prim_c2i(pval, CBS_data(content), CBS_len(content),
		    utype, &free_content, it);
	}

	if (it->utype == V_ASN1_ANY)
		return asn1_c2i_any(pval, content, utype, it);

	return asn1_c2i_primitive(pval, content, utype, it);
}

/*
 * Decode ASN.1 content into a primitive type. There are three possible forms -
 * a SEQUENCE/SET/OTHER that is stored verbatim (including the ASN.1 tag and
 * length octets), constructed objects and non-constructed objects. In the
 * first two cases indefinite length is permitted, which we may need to handle.
 * When this function is called the *cbs should reference the start of the
 * ASN.1 object (i.e. the tag/length header), while *cbs_object should
 * reference the start of the object contents (i.e. after the tag/length
 * header. Additionally, the *cbs_object offset should be relative to the
 * ASN.1 object being parsed. On success the *cbs will point at the octet
 * after the object.
 */
static int
asn1_d2i_primitive_content(ASN1_VALUE **pval, CBS *cbs, CBS *cbs_object,
    int utype, int constructed, int indefinite, size_t length,
    const ASN1_ITEM *it)
{
	CBS cbs_content, cbs_initial;
	uint8_t *data = NULL;
	size_t data_len = 0;
	CBB cbb;
	int ret = 0;

	memset(&cbb, 0, sizeof(cbb));

	CBS_dup(cbs, &cbs_initial);
	CBS_init(&cbs_content, NULL, 0);

	if (asn1_must_be_constructed(utype) && !constructed) {
		ASN1error(ASN1_R_TYPE_NOT_CONSTRUCTED);
		goto err;
	}
	if (asn1_must_be_primitive(utype) && constructed) {
		ASN1error(ASN1_R_TYPE_NOT_PRIMITIVE);
		goto err;
	}

	/* SEQUENCE, SET and "OTHER" are left in encoded form. */
	if (utype == V_ASN1_SEQUENCE || utype == V_ASN1_SET ||
	    utype == V_ASN1_OTHER) {
		if (!asn1_find_end(cbs_object, length, indefinite))
			goto err;
		if (!CBS_get_bytes(&cbs_initial, &cbs_content,
		    CBS_offset(cbs_object)))
			goto err;
	} else if (constructed) {
		/*
		 * Should really check the internal tags are correct but
		 * some things may get this wrong. The relevant specs
		 * say that constructed string types should be OCTET STRINGs
		 * internally irrespective of the type. So instead just check
		 * for UNIVERSAL class and ignore the tag.
		 */
		if (!CBB_init(&cbb, 0))
			goto err;
		if (!asn1_collect(&cbb, cbs_object, indefinite, -1,
		    V_ASN1_UNIVERSAL, 0))
			goto err;
		if (!CBB_finish(&cbb, &data, &data_len))
			goto err;

		CBS_init(&cbs_content, data, data_len);
	} else {
		if (!CBS_get_bytes(cbs_object, &cbs_content, length))
			goto err;
	}

	if (!asn1_c2i(pval, &cbs_content, utype, it))
		goto err;

	if (!CBS_skip(cbs, CBS_offset(cbs_object)))
		goto err;

	ret = 1;

 err:
	CBB_cleanup(&cbb);
	freezero(data, data_len);

	return ret;
}

static int
asn1_d2i_any(ASN1_VALUE **pval, CBS *cbs, const ASN1_ITEM *it,
    int tag_number, int tag_class, int optional)
{
	int constructed, indefinite;
	uint8_t object_class;
	int object_type;
	CBS cbs_object;
	size_t length;

	CBS_init(&cbs_object, CBS_data(cbs), CBS_len(cbs));

	if (it->utype != V_ASN1_ANY)
		return 0;

	if (tag_number >= 0) {
		ASN1error(ASN1_R_ILLEGAL_TAGGED_ANY);
		return 0;
	}
	if (optional) {
		ASN1error(ASN1_R_ILLEGAL_OPTIONAL_ANY);
		return 0;
	}

	/* Determine type from ASN.1 tag. */
	if (asn1_check_tag(&cbs_object, &length, &object_type, &object_class,
	    &indefinite, &constructed, -1, 0, 0) != 1) {
		ASN1error(ERR_R_NESTED_ASN1_ERROR);
		return 0;
	}
	if (object_class != V_ASN1_UNIVERSAL)
		object_type = V_ASN1_OTHER;

	return asn1_d2i_primitive_content(pval, cbs, &cbs_object, object_type,
	    constructed, indefinite, length, it);
}

static int
asn1_d2i_mstring(ASN1_VALUE **pval, CBS *cbs, const ASN1_ITEM *it,
    int tag_number, int tag_class, int optional)
{
	int constructed, indefinite;
	uint8_t object_class;
	int object_tag;
	CBS cbs_object;
	size_t length;

	CBS_init(&cbs_object, CBS_data(cbs), CBS_len(cbs));

	/*
	 * It never makes sense for multi-strings to have implicit tagging, so
	 * if tag_number != -1, then this looks like an error in the template.
	 */
	if (tag_number != -1) {
		ASN1error(ASN1_R_BAD_TEMPLATE);
		return 0;
	}

	if (asn1_check_tag(&cbs_object, &length, &object_tag, &object_class,
	    &indefinite, &constructed, -1, 0, 1) != 1) {
		ASN1error(ERR_R_NESTED_ASN1_ERROR);
		return 0;
	}

	/* Class must be UNIVERSAL. */
	if (object_class != V_ASN1_UNIVERSAL) {
		if (optional)
			return -1;
		ASN1error(ASN1_R_MSTRING_NOT_UNIVERSAL);
		return 0;
	}
	/* Check tag matches bit map. */
	if ((ASN1_tag2bit(object_tag) & it->utype) == 0) {
		if (optional)
			return -1;
		ASN1error(ASN1_R_MSTRING_WRONG_TAG);
		return 0;
	}

	return asn1_d2i_primitive_content(pval, cbs, &cbs_object,
	    object_tag, constructed, indefinite, length, it);
}

static int
asn1_d2i_primitive(ASN1_VALUE **pval, CBS *cbs, const ASN1_ITEM *it,
    int tag_number, int tag_class, int optional)
{
	CBS cbs_object;
	int constructed, indefinite;
	int utype = it->utype;
	size_t length;
	int ret;

	CBS_init(&cbs_object, CBS_data(cbs), CBS_len(cbs));

	if (it->itype == ASN1_ITYPE_MSTRING)
		return 0;

	if (it->utype == V_ASN1_ANY)
		return asn1_d2i_any(pval, cbs, it, tag_number, tag_class, optional);

	if (tag_number == -1) {
		tag_number = it->utype;
		tag_class = V_ASN1_UNIVERSAL;
	}

	ret = asn1_check_tag(&cbs_object, &length, NULL, NULL, &indefinite,
	    &constructed, tag_number, tag_class, optional);
	if (ret == -1)
		return -1;
	if (ret != 1) {
		ASN1error(ERR_R_NESTED_ASN1_ERROR);
		return 0;
	}

	return asn1_d2i_primitive_content(pval, cbs, &cbs_object, utype,
	    constructed, indefinite, length, it);
}

static int
asn1_item_d2i_choice(ASN1_VALUE **pval, CBS *cbs, const ASN1_ITEM *it,
    int tag_number, int tag_class, int optional, int depth)
{
	const ASN1_TEMPLATE *at, *errat = NULL;
	const ASN1_AUX *aux;
	ASN1_aux_cb *asn1_cb = NULL;
	ASN1_VALUE *achoice = NULL;
	ASN1_VALUE **pchptr;
	int i, ret;

	if ((aux = it->funcs) != NULL)
		asn1_cb = aux->asn1_cb;

	if (it->itype != ASN1_ITYPE_CHOICE)
		goto err;

	/*
	 * It never makes sense for CHOICE types to have implicit tagging, so
	 * if tag_number != -1, then this looks like an error in the template.
	 */
	if (tag_number != -1) {
		ASN1error(ASN1_R_BAD_TEMPLATE);
		goto err;
	}

	if (*pval != NULL) {
		ASN1_item_ex_free(pval, it);
		*pval = NULL;
	}

	if (!ASN1_item_ex_new(&achoice, it)) {
		ASN1error(ERR_R_NESTED_ASN1_ERROR);
		goto err;
	}

	if (asn1_cb != NULL && !asn1_cb(ASN1_OP_D2I_PRE, &achoice, it, NULL)) {
		ASN1error(ASN1_R_AUX_ERROR);
		goto err;
	}

	/* Try each possible CHOICE in turn. */
	for (i = 0; i < it->tcount; i++) {
		at = &it->templates[i];

		pchptr = asn1_get_field_ptr(&achoice, at);

		/* Mark field as OPTIONAL so its absence can be identified. */
		ret = asn1_template_d2i(pchptr, cbs, at, 1, depth);
		if (ret == -1)
			continue;
		if (ret != 1) {
			ASN1error(ERR_R_NESTED_ASN1_ERROR);
			errat = at;
			goto err;
		}

		/* We've successfully decoded an ASN.1 object. */
		asn1_set_choice_selector(&achoice, i, it);
		break;
	}

	/* Did we fall off the end without reading anything? */
	if (i == it->tcount) {
		if (optional) {
			ASN1_item_ex_free(&achoice, it);
			return -1;
		}
		ASN1error(ASN1_R_NO_MATCHING_CHOICE_TYPE);
		goto err;
	}

	if (asn1_cb != NULL && !asn1_cb(ASN1_OP_D2I_POST, &achoice, it, NULL)) {
		ASN1error(ASN1_R_AUX_ERROR);
		goto err;
	}

	*pval = achoice;
	achoice = NULL;

	return 1;

 err:
	ASN1_item_ex_free(&achoice, it);

	if (errat != NULL)
		ERR_asprintf_error_data("Field=%s, Type=%s", errat->field_name,
		    it->sname);
	else
		ERR_asprintf_error_data("Type=%s", it->sname);

	return 0;
}

static int
asn1_item_d2i_sequence(ASN1_VALUE **pval, CBS *cbs, const ASN1_ITEM *it,
    int tag_number, int tag_class, int optional, int depth)
{
	CBS cbs_seq, cbs_seq_content, cbs_object;
	int constructed, indefinite, optional_field;
	const ASN1_TEMPLATE *errat = NULL;
	const ASN1_TEMPLATE *seqat, *at;
	ASN1_aux_cb *asn1_cb = NULL;
	const ASN1_AUX *aux;
	ASN1_VALUE *aseq = NULL;
	ASN1_VALUE **pseqval;
	int eoc_needed, i;
	size_t length;
	int ret = 0;

	CBS_init(&cbs_seq, CBS_data(cbs), CBS_len(cbs));

	if ((aux = it->funcs) != NULL)
		asn1_cb = aux->asn1_cb;

	if (it->itype != ASN1_ITYPE_NDEF_SEQUENCE &&
	    it->itype != ASN1_ITYPE_SEQUENCE)
		goto err;

	if (*pval != NULL) {
		ASN1_item_ex_free(pval, it);
		*pval = NULL;
	}

	/* If no IMPLICIT tagging use UNIVERSAL/SEQUENCE. */
	if (tag_number == -1) {
		tag_class = V_ASN1_UNIVERSAL;
		tag_number = V_ASN1_SEQUENCE;
	}

	/* Read ASN.1 SEQUENCE header. */
	ret = asn1_check_tag(&cbs_seq, &length, NULL, NULL, &indefinite,
	    &constructed, tag_number, tag_class, optional);
	if (ret == -1)
		return -1;
	if (ret != 1) {
		ASN1error(ERR_R_NESTED_ASN1_ERROR);
		goto err;
	}

	if (!constructed) {
		ASN1error(ASN1_R_SEQUENCE_NOT_CONSTRUCTED);
		goto err;
	}

	if (indefinite) {
		eoc_needed = 1;
		CBS_init(&cbs_seq_content, CBS_data(&cbs_seq), CBS_len(&cbs_seq));
	} else {
		eoc_needed = 0;
		if (!CBS_get_bytes(&cbs_seq, &cbs_seq_content, length))
			goto err;
	}

	if (!ASN1_item_ex_new(&aseq, it)) {
		ASN1error(ERR_R_NESTED_ASN1_ERROR);
		goto err;
	}

	if (asn1_cb != NULL && !asn1_cb(ASN1_OP_D2I_PRE, &aseq, it, NULL)) {
		ASN1error(ASN1_R_AUX_ERROR);
		goto err;
	}

	for (i = 0; i < it->tcount; i++) {
		at = &it->templates[i];

		if (asn1_check_eoc(&cbs_seq_content)) {
			if (!indefinite) {
				ASN1error(ASN1_R_UNEXPECTED_EOC);
				goto err;
			}
			eoc_needed = 0;
			break;
		}
		if (CBS_len(&cbs_seq_content) == 0)
			break;

		if ((seqat = asn1_do_adb(&aseq, at, 1)) == NULL)
			goto err;

		pseqval = asn1_get_field_ptr(&aseq, seqat);

		/*
		 * This was originally implemented to "increase efficiency",
		 * however it currently needs to remain since it papers over
		 * the use of ASN.1 ANY with OPTIONAL in SEQUENCEs (which
		 * asn1_d2i_primitive() currently rejects).
		 */
		optional_field = (seqat->flags & ASN1_TFLG_OPTIONAL) != 0;
		if (i == it->tcount - 1)
			optional_field = 0;

		ret = asn1_template_d2i(pseqval, &cbs_seq_content,
		    seqat, optional_field, depth);
		if (ret == -1) {
			/* Absent OPTIONAL component. */
			ASN1_template_free(pseqval, seqat);
			continue;
		}
		if (ret != 1) {
			errat = seqat;
			goto err;
		}
	}

	if (eoc_needed && !asn1_check_eoc(&cbs_seq_content)) {
		ASN1error(ASN1_R_MISSING_EOC);
		goto err;
	}

	if (indefinite) {
		if (!CBS_skip(&cbs_seq, CBS_offset(&cbs_seq_content)))
			goto err;
	} else if (CBS_len(&cbs_seq_content) != 0) {
		ASN1error(ASN1_R_SEQUENCE_LENGTH_MISMATCH);
		goto err;
	}

	/*
	 * There is no more data in the ASN.1 SEQUENCE, however we may not have
	 * populated all fields - check that any remaining are OPTIONAL.
	 */
	for (; i < it->tcount; i++) {
		at = &it->templates[i];

		if ((seqat = asn1_do_adb(&aseq, at, 1)) == NULL)
			goto err;

		if ((seqat->flags & ASN1_TFLG_OPTIONAL) == 0) {
			ASN1error(ASN1_R_FIELD_MISSING);
			errat = seqat;
			goto err;
		}

		/* XXX - this is probably unnecessary with earlier free. */
		pseqval = asn1_get_field_ptr(&aseq, seqat);
		ASN1_template_free(pseqval, seqat);
	}

	if (!CBS_get_bytes(cbs, &cbs_object, CBS_offset(&cbs_seq)))
		goto err;

	if (!asn1_enc_save(&aseq, &cbs_object, it)) {
		ASN1error(ERR_R_MALLOC_FAILURE);
		goto err;
	}

	if (asn1_cb != NULL && !asn1_cb(ASN1_OP_D2I_POST, &aseq, it, NULL)) {
		ASN1error(ASN1_R_AUX_ERROR);
		goto err;
	}

	*pval = aseq;
	aseq = NULL;

	return 1;

 err:
	ASN1_item_ex_free(&aseq, it);

	if (errat != NULL)
		ERR_asprintf_error_data("Field=%s, Type=%s", errat->field_name,
		    it->sname);
	else
		ERR_asprintf_error_data("Type=%s", it->sname);

	return 0;
}

static int
asn1_item_d2i_extern(ASN1_VALUE **pval, CBS *cbs, const ASN1_ITEM *it,
    int tag_number, int tag_class, int optional)
{
	const ASN1_EXTERN_FUNCS *ef = it->funcs;
	const unsigned char *p = NULL;
	ASN1_TLC ctx = { 0 };
	int ret = 0;

	if (CBS_len(cbs) > LONG_MAX)
		return 0;

	p = CBS_data(cbs);

	if ((ret = ef->asn1_ex_d2i(pval, &p, (long)CBS_len(cbs), it,
	    tag_number, tag_class, optional, &ctx)) == 1) {
		if (!CBS_skip(cbs, p - CBS_data(cbs)))
			goto err;
	}
	return ret;

 err:
	ASN1_item_ex_free(pval, it);

	ERR_asprintf_error_data("Type=%s", it->sname);

	return 0;
}

static int
asn1_item_d2i(ASN1_VALUE **pval, CBS *cbs, const ASN1_ITEM *it,
    int tag_number, int tag_class, int optional, int depth)
{
	if (pval == NULL)
		return 0;

	if (++depth > ASN1_MAX_CONSTRUCTED_NEST) {
		ASN1error(ASN1_R_NESTED_TOO_DEEP);
		goto err;
	}

	switch (it->itype) {
	case ASN1_ITYPE_PRIMITIVE:
		if (it->templates != NULL) {
			/*
			 * Tagging or OPTIONAL is currently illegal on an item
			 * template because the flags can't get passed down.
			 * In practice this isn't a problem: we include the
			 * relevant flags from the item template in the
			 * template itself.
			 */
			if (tag_number != -1 || optional) {
				ASN1error(ASN1_R_ILLEGAL_OPTIONS_ON_ITEM_TEMPLATE);
				goto err;
			}
			return asn1_template_d2i(pval, cbs, it->templates,
			    optional, depth);
		}
		return asn1_d2i_primitive(pval, cbs, it, tag_number, tag_class,
		    optional);

	case ASN1_ITYPE_MSTRING:
		return asn1_d2i_mstring(pval, cbs, it, tag_number, tag_class,
		    optional);

	case ASN1_ITYPE_EXTERN:
		return asn1_item_d2i_extern(pval, cbs, it, tag_number,
		    tag_class, optional);

	case ASN1_ITYPE_CHOICE:
		return asn1_item_d2i_choice(pval, cbs, it, tag_number,
		    tag_class, optional, depth);

	case ASN1_ITYPE_NDEF_SEQUENCE:
	case ASN1_ITYPE_SEQUENCE:
		return asn1_item_d2i_sequence(pval, cbs, it, tag_number,
		    tag_class, optional, depth);

	default:
		return 0;
	}

 err:
	ASN1_item_ex_free(pval, it);

	ERR_asprintf_error_data("Type=%s", it->sname);

	return 0;
}

static void
asn1_template_stack_of_free(STACK_OF(ASN1_VALUE) *avals,
    const ASN1_TEMPLATE *at)
{
	ASN1_VALUE *aval;

	if (avals == NULL)
		return;

	while (sk_ASN1_VALUE_num(avals) > 0) {
		aval = sk_ASN1_VALUE_pop(avals);
		ASN1_item_ex_free(&aval, at->item);
	}
	sk_ASN1_VALUE_free(avals);
}

static int
asn1_template_stack_of_d2i(ASN1_VALUE **pval, CBS *cbs, const ASN1_TEMPLATE *at,
    int optional, int depth)
{
	CBS cbs_object, cbs_object_content;
	STACK_OF(ASN1_VALUE) *avals = NULL;
	ASN1_VALUE *aval = NULL;
	int tag_number, tag_class;
	int eoc_needed;
	int indefinite;
	size_t length;
	int ret;

	CBS_init(&cbs_object, CBS_data(cbs), CBS_len(cbs));

	if (pval == NULL)
		return 0;

	asn1_template_stack_of_free((STACK_OF(ASN1_VALUE) *)*pval, at);
	*pval = NULL;

	tag_number = at->tag;
	tag_class = at->flags & ASN1_TFLG_TAG_CLASS;

	/* Determine the inner tag value for SET OF or SEQUENCE OF. */
	if ((at->flags & ASN1_TFLG_IMPTAG) == 0) {
		tag_number = V_ASN1_SEQUENCE;
		tag_class = V_ASN1_UNIVERSAL;
		if ((at->flags & ASN1_TFLG_SET_OF) != 0)
			tag_number = V_ASN1_SET;
	}

	ret = asn1_check_tag(&cbs_object, &length, NULL, NULL, &indefinite,
	    NULL, tag_number, tag_class, optional);
	if (ret == -1)
		return -1;
	if (ret != 1) {
		ASN1error(ERR_R_NESTED_ASN1_ERROR);
		return 0;
	}

	if (indefinite) {
		eoc_needed = 1;
		CBS_init(&cbs_object_content, CBS_data(&cbs_object),
		    CBS_len(&cbs_object));
	} else {
		eoc_needed = 0;
		if (!CBS_get_bytes(&cbs_object, &cbs_object_content,
		    length))
			goto err;
	}

	if ((avals = sk_ASN1_VALUE_new_null()) == NULL) {
		ASN1error(ERR_R_MALLOC_FAILURE);
		goto err;
	}

	/* Read as many items as possible. */
	while (CBS_len(&cbs_object_content) > 0) {
		if (asn1_check_eoc(&cbs_object_content)) {
			if (!eoc_needed) {
				ASN1error(ASN1_R_UNEXPECTED_EOC);
				goto err;
			}
			eoc_needed = 0;
			break;
		}
		if (!asn1_item_d2i(&aval, &cbs_object_content, at->item, -1, 0,
		    0, depth)) {
			ASN1error(ERR_R_NESTED_ASN1_ERROR);
			goto err;
		}
		if (!sk_ASN1_VALUE_push(avals, aval)) {
			ASN1error(ERR_R_MALLOC_FAILURE);
			goto err;
		}
		aval = NULL;
	}
	if (eoc_needed) {
		ASN1error(ASN1_R_MISSING_EOC);
		goto err;
	}

	if (indefinite) {
		if (!CBS_skip(&cbs_object, CBS_offset(&cbs_object_content)))
			goto err;
	}

	if (!CBS_skip(cbs, CBS_offset(&cbs_object)))
		goto err;

	*pval = (ASN1_VALUE *)avals;
	avals = NULL;

	return 1;

 err:
	asn1_template_stack_of_free(avals, at);
	ASN1_item_ex_free(&aval, at->item);

	return 0;
}

static int
asn1_template_noexp_d2i(ASN1_VALUE **pval, CBS *cbs, const ASN1_TEMPLATE *at,
    int optional, int depth)
{
	int tag_number, tag_class;
	int ret;

	if (pval == NULL)
		return 0;

	if ((at->flags & ASN1_TFLG_SK_MASK) != 0)
		return asn1_template_stack_of_d2i(pval, cbs, at, optional, depth);

	tag_number = -1;
	tag_class = V_ASN1_UNIVERSAL;

	/* See if we need to use IMPLICIT tagging. */
	if ((at->flags & ASN1_TFLG_IMPTAG) != 0) {
		tag_number = at->tag;
		tag_class = at->flags & ASN1_TFLG_TAG_CLASS;
	}

	ret = asn1_item_d2i(pval, cbs, at->item, tag_number, tag_class,
	    optional, depth);
	if (ret == -1)
		return -1;
	if (ret != 1) {
		ASN1error(ERR_R_NESTED_ASN1_ERROR);
		goto err;
	}

	return 1;

 err:
	/* XXX - The called function should have freed already. */
	ASN1_template_free(pval, at);
	return 0;
}

static int
asn1_template_d2i(ASN1_VALUE **pval, CBS *cbs, const ASN1_TEMPLATE *at,
    int optional, int depth)
{
	CBS cbs_exp, cbs_exp_content;
	int constructed, indefinite;
	size_t length;
	int ret;

	if (pval == NULL)
		return 0;

	/* Check if EXPLICIT tag is expected. */
	if ((at->flags & ASN1_TFLG_EXPTAG) == 0)
		return asn1_template_noexp_d2i(pval, cbs, at, optional, depth);

	CBS_init(&cbs_exp, CBS_data(cbs), CBS_len(cbs));

	/* Read ASN.1 header for EXPLICIT tagged object. */
	ret = asn1_check_tag(&cbs_exp, &length, NULL, NULL, &indefinite,
	    &constructed, at->tag, at->flags & ASN1_TFLG_TAG_CLASS, optional);
	if (ret == -1)
		return -1;
	if (ret != 1) {
		ASN1error(ERR_R_NESTED_ASN1_ERROR);
		return 0;
	}

	if (!constructed) {
		ASN1error(ASN1_R_EXPLICIT_TAG_NOT_CONSTRUCTED);
		return 0;
	}

	if (indefinite) {
		CBS_init(&cbs_exp_content, CBS_data(&cbs_exp), CBS_len(&cbs_exp));
	} else {
		if (!CBS_get_bytes(&cbs_exp, &cbs_exp_content, length))
			goto err;
	}

	if ((ret = asn1_template_noexp_d2i(pval, &cbs_exp_content, at, 0,
	    depth)) != 1) {
		ASN1error(ERR_R_NESTED_ASN1_ERROR);
		return 0;
	}

	if (indefinite) {
		if (!asn1_check_eoc(&cbs_exp_content)) {
			ASN1error(ASN1_R_MISSING_EOC);
			goto err;
		}
		if (!CBS_skip(&cbs_exp, CBS_offset(&cbs_exp_content)))
			goto err;
	} else if (CBS_len(&cbs_exp_content) != 0) {
		ASN1error(ASN1_R_SEQUENCE_LENGTH_MISMATCH);
		goto err;
	}

	if (!CBS_skip(cbs, CBS_offset(&cbs_exp)))
		goto err;

	return 1;

 err:
	ASN1_template_free(pval, at);
	return 0;
}

ASN1_VALUE *
ASN1_item_d2i(ASN1_VALUE **pval, const unsigned char **in, long inlen,
    const ASN1_ITEM *it)
{
	ASN1_VALUE *ptmpval = NULL;

	if (pval == NULL)
		pval = &ptmpval;
	if (ASN1_item_ex_d2i(pval, in, inlen, it, -1, 0, 0, NULL) <= 0)
		return NULL;

	return *pval;
}
LCRYPTO_ALIAS(ASN1_item_d2i);

int
ASN1_item_ex_d2i(ASN1_VALUE **pval, const unsigned char **in, long inlen,
    const ASN1_ITEM *it, int tag_number, int tag_class, char optional,
    ASN1_TLC *ctx)
{
	CBS cbs;
	int ret;

	if (inlen < 0)
		return 0;

	CBS_init(&cbs, *in, inlen);
	if ((ret = asn1_item_d2i(pval, &cbs, it, tag_number, tag_class,
	    (int)optional, 0)) == 1)
		*in = CBS_data(&cbs);

	return ret;
}
LCRYPTO_ALIAS(ASN1_item_ex_d2i);
