/* $OpenBSD: tasn_prn.c,v 1.29 2025/06/07 09:28:00 tb Exp $ */
/* Written by Dr Stephen N Henson (steve@openssl.org) for the OpenSSL
 * project 2000.
 */
/* ====================================================================
 * Copyright (c) 2000,2005 The OpenSSL Project.  All rights reserved.
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
#include <openssl/buffer.h>
#include <openssl/objects.h>
#include <openssl/x509v3.h>

#include "asn1_local.h"

/* Print routines.
 */

/* ASN1_PCTX routines */

static const ASN1_PCTX default_pctx = {
	.flags = ASN1_PCTX_FLAGS_SHOW_ABSENT,
};

static int asn1_item_print_ctx(BIO *out, ASN1_VALUE **fld, int indent,
    const ASN1_ITEM *it, const char *fname, const char *sname, int nohdr,
    const ASN1_PCTX *pctx);

int asn1_template_print_ctx(BIO *out, ASN1_VALUE **fld, int indent,
    const ASN1_TEMPLATE *tt, const ASN1_PCTX *pctx);

static int asn1_primitive_print(BIO *out, ASN1_VALUE **fld,
    const ASN1_ITEM *it, int indent, const char *fname, const char *sname,
    const ASN1_PCTX *pctx);

static int asn1_print_fsname(BIO *out, int indent, const char *fname,
    const char *sname, const ASN1_PCTX *pctx);

int
ASN1_item_print(BIO *out, ASN1_VALUE *ifld, int indent, const ASN1_ITEM *it,
    const ASN1_PCTX *pctx)
{
	const char *sname;

	if (pctx == NULL)
		pctx = &default_pctx;
	if (pctx->flags & ASN1_PCTX_FLAGS_NO_STRUCT_NAME)
		sname = NULL;
	else
		sname = it->sname;
	return asn1_item_print_ctx(out, &ifld, indent, it, NULL, sname,
	    0, pctx);
}
LCRYPTO_ALIAS(ASN1_item_print);

static int
asn1_item_print_ctx(BIO *out, ASN1_VALUE **fld, int indent, const ASN1_ITEM *it,
    const char *fname, const char *sname, int nohdr, const ASN1_PCTX *pctx)
{
	const ASN1_TEMPLATE *tt;
	const ASN1_EXTERN_FUNCS *ef;
	ASN1_VALUE **tmpfld;
	const ASN1_AUX *aux = it->funcs;
	ASN1_aux_cb *asn1_cb;
	ASN1_PRINT_ARG parg;
	int i;

	if (aux && aux->asn1_cb) {
		parg.out = out;
		parg.indent = indent;
		parg.pctx = pctx;
		asn1_cb = aux->asn1_cb;
	} else
		asn1_cb = NULL;

	if ((it->itype != ASN1_ITYPE_PRIMITIVE ||
	    it->utype != V_ASN1_BOOLEAN) && *fld == NULL) {
		if (pctx->flags & ASN1_PCTX_FLAGS_SHOW_ABSENT) {
			if (!nohdr &&
			    !asn1_print_fsname(out, indent, fname, sname, pctx))
				return 0;
			if (BIO_puts(out, "<ABSENT>\n") <= 0)
				return 0;
		}
		return 1;
	}

	switch (it->itype) {
	case ASN1_ITYPE_PRIMITIVE:
		if (it->templates) {
			if (!asn1_template_print_ctx(out, fld, indent,
			    it->templates, pctx))
				return 0;
		}
		/* fall thru */
	case ASN1_ITYPE_MSTRING:
		if (!asn1_primitive_print(out, fld, it,
		    indent, fname, sname, pctx))
			return 0;
		break;

	case ASN1_ITYPE_EXTERN:
		if (!nohdr &&
		    !asn1_print_fsname(out, indent, fname, sname, pctx))
			return 0;
		/* Use new style print routine if possible */
		ef = it->funcs;
		if (ef && ef->asn1_ex_print) {
			i = ef->asn1_ex_print(out, fld, indent, "", pctx);
			if (!i)
				return 0;
			if ((i == 2) && (BIO_puts(out, "\n") <= 0))
				return 0;
			return 1;
		} else if (sname &&
		    BIO_printf(out, ":EXTERNAL TYPE %s\n", sname) <= 0)
			return 0;
		break;

	case ASN1_ITYPE_CHOICE:
		/* CHOICE type, get selector */
		i = asn1_get_choice_selector(fld, it);
		/* This should never happen... */
		if ((i < 0) || (i >= it->tcount)) {
			if (BIO_printf(out,
			    "ERROR: selector [%d] invalid\n", i) <= 0)
				return 0;
			return 1;
		}
		tt = it->templates + i;
		tmpfld = asn1_get_field_ptr(fld, tt);
		if (!asn1_template_print_ctx(out, tmpfld, indent, tt, pctx))
			return 0;
		break;

	case ASN1_ITYPE_SEQUENCE:
	case ASN1_ITYPE_NDEF_SEQUENCE:
		if (!nohdr &&
		    !asn1_print_fsname(out, indent, fname, sname, pctx))
			return 0;
		if (fname || sname) {
			if (pctx->flags & ASN1_PCTX_FLAGS_SHOW_SEQUENCE) {
				if (BIO_puts(out, " {\n") <= 0)
					return 0;
			} else {
				if (BIO_puts(out, "\n") <= 0)
					return 0;
			}
		}

		if (asn1_cb) {
			i = asn1_cb(ASN1_OP_PRINT_PRE, fld, it, &parg);
			if (i == 0)
				return 0;
			if (i == 2)
				return 1;
		}

		/* Print each field entry */
		for (i = 0, tt = it->templates; i < it->tcount; i++, tt++) {
			const ASN1_TEMPLATE *seqtt;

			seqtt = asn1_do_adb(fld, tt, 1);
			if (seqtt == NULL)
				return 0;
			tmpfld = asn1_get_field_ptr(fld, seqtt);
			if (!asn1_template_print_ctx(out, tmpfld, indent + 2,
			    seqtt, pctx))
				return 0;
		}
		if (pctx->flags & ASN1_PCTX_FLAGS_SHOW_SEQUENCE) {
			if (BIO_printf(out, "%*s}\n", indent, "") < 0)
				return 0;
		}

		if (asn1_cb) {
			i = asn1_cb(ASN1_OP_PRINT_POST, fld, it, &parg);
			if (i == 0)
				return 0;
		}
		break;

	default:
		BIO_printf(out, "Unprocessed type %d\n", it->itype);
		return 0;
	}

	return 1;
}

int
asn1_template_print_ctx(BIO *out, ASN1_VALUE **fld, int indent,
    const ASN1_TEMPLATE *tt, const ASN1_PCTX *pctx)
{
	int i, flags;
	const char *sname, *fname;

	flags = tt->flags;
	if (pctx->flags & ASN1_PCTX_FLAGS_SHOW_FIELD_STRUCT_NAME)
		sname = tt->item->sname;
	else
		sname = NULL;
	if (pctx->flags & ASN1_PCTX_FLAGS_NO_FIELD_NAME)
		fname = NULL;
	else
		fname = tt->field_name;
	if (flags & ASN1_TFLG_SK_MASK) {
		char *tname;
		ASN1_VALUE *skitem;
		STACK_OF(ASN1_VALUE) *stack;

		/* SET OF, SEQUENCE OF */
		if (fname) {
			if (pctx->flags & ASN1_PCTX_FLAGS_SHOW_SSOF) {
				if (flags & ASN1_TFLG_SET_OF)
					tname = "SET";
				else
					tname = "SEQUENCE";
				if (BIO_printf(out, "%*s%s OF %s {\n",
				    indent, "", tname, tt->field_name) <= 0)
					return 0;
			} else if (BIO_printf(out, "%*s%s:\n", indent, "",
			    fname) <= 0)
				return 0;
		}
		stack = (STACK_OF(ASN1_VALUE) *)*fld;
		for (i = 0; i < sk_ASN1_VALUE_num(stack); i++) {
			if ((i > 0) && (BIO_puts(out, "\n") <= 0))
				return 0;
			skitem = sk_ASN1_VALUE_value(stack, i);
			if (!asn1_item_print_ctx(out, &skitem, indent + 2,
			    tt->item, NULL, NULL, 1, pctx))
				return 0;
		}
		if (!i && BIO_printf(out, "%*s<EMPTY>\n", indent + 2, "") <= 0)
			return 0;
		if (pctx->flags & ASN1_PCTX_FLAGS_SHOW_SEQUENCE) {
			if (BIO_printf(out, "%*s}\n", indent, "") <= 0)
				return 0;
		}
		return 1;
	}
	return asn1_item_print_ctx(out, fld, indent, tt->item,
	    fname, sname, 0, pctx);
}

static int
asn1_print_fsname(BIO *out, int indent, const char *fname, const char *sname,
    const ASN1_PCTX *pctx)
{
	if (indent < 0)
		return 0;
	if (!BIO_indent(out, indent, indent))
		return 0;
	if (pctx->flags & ASN1_PCTX_FLAGS_NO_STRUCT_NAME)
		sname = NULL;
	if (pctx->flags & ASN1_PCTX_FLAGS_NO_FIELD_NAME)
		fname = NULL;
	if (!sname && !fname)
		return 1;
	if (fname) {
		if (BIO_puts(out, fname) <= 0)
			return 0;
	}
	if (sname) {
		if (fname) {
			if (BIO_printf(out, " (%s)", sname) <= 0)
				return 0;
		} else {
			if (BIO_puts(out, sname) <= 0)
				return 0;
		}
	}
	if (BIO_write(out, ": ", 2) != 2)
		return 0;
	return 1;
}

static int
asn1_print_boolean_ctx(BIO *out, int boolval, const ASN1_PCTX *pctx)
{
	const char *str;
	switch (boolval) {
	case -1:
		str = "BOOL ABSENT";
		break;

	case 0:
		str = "FALSE";
		break;

	default:
		str = "TRUE";
		break;

	}

	if (BIO_puts(out, str) <= 0)
		return 0;
	return 1;

}

static int
asn1_print_integer_ctx(BIO *out, ASN1_INTEGER *str, const ASN1_PCTX *pctx)
{
	char *s;
	int ret = 1;
	if ((s = i2s_ASN1_INTEGER(NULL, str)) == NULL)
		return 0;
	if (BIO_puts(out, s) <= 0)
		ret = 0;
	free(s);
	return ret;
}

static int
asn1_print_oid_ctx(BIO *out, const ASN1_OBJECT *oid, const ASN1_PCTX *pctx)
{
	char objbuf[80];
	const char *ln;
	ln = OBJ_nid2ln(OBJ_obj2nid(oid));
	if (!ln)
		ln = "";
	OBJ_obj2txt(objbuf, sizeof objbuf, oid, 1);
	if (BIO_printf(out, "%s (%s)", ln, objbuf) <= 0)
		return 0;
	return 1;
}

static int
asn1_print_obstring_ctx(BIO *out, ASN1_STRING *str, int indent,
    const ASN1_PCTX *pctx)
{
	if (str->type == V_ASN1_BIT_STRING) {
		if (BIO_printf(out, " (%ld unused bits)\n",
		    str->flags & 0x7) <= 0)
			return 0;
	} else if (BIO_puts(out, "\n") <= 0)
		return 0;
	if ((str->length > 0) &&
	    BIO_dump_indent(out, (char *)str->data, str->length,
	    indent + 2) <= 0)
		return 0;
	return 1;
}

static int
asn1_primitive_print(BIO *out, ASN1_VALUE **fld, const ASN1_ITEM *it,
    int indent, const char *fname, const char *sname, const ASN1_PCTX *pctx)
{
	long utype;
	ASN1_STRING *str;
	int ret = 1, needlf = 1;
	const char *pname;

	if (!asn1_print_fsname(out, indent, fname, sname, pctx))
		return 0;

	if (it->funcs != NULL) {
		const ASN1_PRIMITIVE_FUNCS *pf = it->funcs;

		if (pf->prim_print == NULL)
			return 0;

		return pf->prim_print(out, fld, it, indent, pctx);
	}
	if (it->itype == ASN1_ITYPE_MSTRING) {
		str = (ASN1_STRING *)*fld;
		utype = str->type & ~V_ASN1_NEG;
	} else {
		utype = it->utype;
		if (utype == V_ASN1_BOOLEAN)
			str = NULL;
		else
			str = (ASN1_STRING *)*fld;
	}
	if (utype == V_ASN1_ANY) {
		ASN1_TYPE *atype = (ASN1_TYPE *)*fld;
		utype = atype->type;
		fld = &atype->value.asn1_value;
		str = (ASN1_STRING *)*fld;
		if (pctx->flags & ASN1_PCTX_FLAGS_NO_ANY_TYPE)
			pname = NULL;
		else
			pname = ASN1_tag2str(utype);
	} else {
		if (pctx->flags & ASN1_PCTX_FLAGS_SHOW_TYPE)
			pname = ASN1_tag2str(utype);
		else
			pname = NULL;
	}

	if (utype == V_ASN1_NULL) {
		if (BIO_puts(out, "NULL\n") <= 0)
			return 0;
		return 1;
	}

	if (pname) {
		if (BIO_puts(out, pname) <= 0)
			return 0;
		if (BIO_puts(out, ":") <= 0)
			return 0;
	}

	switch (utype) {
	case V_ASN1_BOOLEAN:
		{
			int boolval = *(int *)fld;
			if (boolval == -1)
				boolval = it->size;
			ret = asn1_print_boolean_ctx(out, boolval, pctx);
		}
		break;

	case V_ASN1_INTEGER:
	case V_ASN1_ENUMERATED:
		ret = asn1_print_integer_ctx(out, str, pctx);
		break;

	case V_ASN1_UTCTIME:
		ret = ASN1_UTCTIME_print(out, str);
		break;

	case V_ASN1_GENERALIZEDTIME:
		ret = ASN1_GENERALIZEDTIME_print(out, str);
		break;

	case V_ASN1_OBJECT:
		ret = asn1_print_oid_ctx(out, (const ASN1_OBJECT *)*fld, pctx);
		break;

	case V_ASN1_OCTET_STRING:
	case V_ASN1_BIT_STRING:
		ret = asn1_print_obstring_ctx(out, str, indent, pctx);
		needlf = 0;
		break;

	case V_ASN1_SEQUENCE:
	case V_ASN1_SET:
	case V_ASN1_OTHER:
		if (BIO_puts(out, "\n") <= 0)
			return 0;
		if (ASN1_parse_dump(out, str->data, str->length,
		    indent, 0) <= 0)
			ret = 0;
		needlf = 0;
		break;

	default:
		ret = ASN1_STRING_print_ex(out, str, pctx->str_flags);
	}
	if (!ret)
		return 0;
	if (needlf && BIO_puts(out, "\n") <= 0)
		return 0;
	return 1;
}
