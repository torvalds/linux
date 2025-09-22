/* $OpenBSD: ec_asn1.c,v 1.112 2025/05/10 05:54:38 tb Exp $ */
/*
 * Written by Nils Larsch for the OpenSSL project.
 */
/* ====================================================================
 * Copyright (c) 2000-2003 The OpenSSL Project.  All rights reserved.
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
#include <stdlib.h>
#include <string.h>

#include <openssl/opensslconf.h>

#include <openssl/asn1.h>
#include <openssl/bn.h>
#include <openssl/ec.h>
#include <openssl/asn1t.h>
#include <openssl/objects.h>

#include "asn1_local.h"
#include "ec_local.h"
#include "err_local.h"

int
EC_GROUP_get_basis_type(const EC_GROUP *group)
{
	return 0;
}
LCRYPTO_ALIAS(EC_GROUP_get_basis_type);

typedef struct x9_62_pentanomial_st {
	long k1;
	long k2;
	long k3;
} X9_62_PENTANOMIAL;

typedef struct x9_62_characteristic_two_st {
	long m;
	ASN1_OBJECT *type;
	union {
		char *ptr;
		/* NID_X9_62_onBasis */
		ASN1_NULL *onBasis;
		/* NID_X9_62_tpBasis */
		ASN1_INTEGER *tpBasis;
		/* NID_X9_62_ppBasis */
		X9_62_PENTANOMIAL *ppBasis;
		/* anything else */
		ASN1_TYPE *other;
	} p;
} X9_62_CHARACTERISTIC_TWO;

typedef struct x9_62_fieldid_st {
	ASN1_OBJECT *fieldType;
	union {
		char *ptr;
		/* NID_X9_62_prime_field */
		ASN1_INTEGER *prime;
		/* NID_X9_62_characteristic_two_field */
		X9_62_CHARACTERISTIC_TWO *char_two;
		/* anything else */
		ASN1_TYPE *other;
	} p;
} X9_62_FIELDID;

typedef struct x9_62_curve_st {
	ASN1_OCTET_STRING *a;
	ASN1_OCTET_STRING *b;
	ASN1_BIT_STRING *seed;
} X9_62_CURVE;

typedef struct ec_parameters_st {
	long version;
	X9_62_FIELDID *fieldID;
	X9_62_CURVE *curve;
	ASN1_OCTET_STRING *base;
	ASN1_INTEGER *order;
	ASN1_INTEGER *cofactor;
} ECPARAMETERS;

#define ECPK_PARAM_NAMED_CURVE		0
#define ECPK_PARAM_EXPLICIT		1
#define ECPK_PARAM_IMPLICITLY_CA	2

typedef struct ecpk_parameters_st {
	int type;
	union {
		ASN1_OBJECT *named_curve;
		ECPARAMETERS *parameters;
		ASN1_NULL *implicitlyCA;
	} value;
} ECPKPARAMETERS;

typedef struct ec_privatekey_st {
	long version;
	ASN1_OCTET_STRING *privateKey;
	ECPKPARAMETERS *parameters;
	ASN1_BIT_STRING *publicKey;
} EC_PRIVATEKEY;

static const ASN1_TEMPLATE X9_62_PENTANOMIAL_seq_tt[] = {
	{
		.flags = 0,
		.tag = 0,
		.offset = offsetof(X9_62_PENTANOMIAL, k1),
		.field_name = "k1",
		.item = &LONG_it,
	},
	{
		.flags = 0,
		.tag = 0,
		.offset = offsetof(X9_62_PENTANOMIAL, k2),
		.field_name = "k2",
		.item = &LONG_it,
	},
	{
		.flags = 0,
		.tag = 0,
		.offset = offsetof(X9_62_PENTANOMIAL, k3),
		.field_name = "k3",
		.item = &LONG_it,
	},
};

static const ASN1_ITEM X9_62_PENTANOMIAL_it = {
	.itype = ASN1_ITYPE_SEQUENCE,
	.utype = V_ASN1_SEQUENCE,
	.templates = X9_62_PENTANOMIAL_seq_tt,
	.tcount = sizeof(X9_62_PENTANOMIAL_seq_tt) / sizeof(ASN1_TEMPLATE),
	.funcs = NULL,
	.size = sizeof(X9_62_PENTANOMIAL),
	.sname = "X9_62_PENTANOMIAL",
};

static const ASN1_TEMPLATE char_two_def_tt = {
	.flags = 0,
	.tag = 0,
	.offset = offsetof(X9_62_CHARACTERISTIC_TWO, p.other),
	.field_name = "p.other",
	.item = &ASN1_ANY_it,
};

static const ASN1_ADB_TABLE X9_62_CHARACTERISTIC_TWO_adbtbl[] = {
	{
		.value = NID_X9_62_onBasis,
		.tt = {
			.flags = 0,
			.tag = 0,
			.offset = offsetof(X9_62_CHARACTERISTIC_TWO, p.onBasis),
			.field_name = "p.onBasis",
			.item = &ASN1_NULL_it,
		},
	},
	{
		.value = NID_X9_62_tpBasis,
		.tt = {
			.flags = 0,
			.tag = 0,
			.offset = offsetof(X9_62_CHARACTERISTIC_TWO, p.tpBasis),
			.field_name = "p.tpBasis",
			.item = &ASN1_INTEGER_it,
		},
	},
	{
		.value = NID_X9_62_ppBasis,
		.tt = {
			.flags = 0,
			.tag = 0,
			.offset = offsetof(X9_62_CHARACTERISTIC_TWO, p.ppBasis),
			.field_name = "p.ppBasis",
			.item = &X9_62_PENTANOMIAL_it,
		},

	},
};

static const ASN1_ADB X9_62_CHARACTERISTIC_TWO_adb = {
	.flags = 0,
	.offset = offsetof(X9_62_CHARACTERISTIC_TWO, type),
	.tbl = X9_62_CHARACTERISTIC_TWO_adbtbl,
	.tblcount = sizeof(X9_62_CHARACTERISTIC_TWO_adbtbl) / sizeof(ASN1_ADB_TABLE),
	.default_tt = &char_two_def_tt,
	.null_tt = NULL,
};

static const ASN1_TEMPLATE X9_62_CHARACTERISTIC_TWO_seq_tt[] = {
	{
		.flags = 0,
		.tag = 0,
		.offset = offsetof(X9_62_CHARACTERISTIC_TWO, m),
		.field_name = "m",
		.item = &LONG_it,
	},
	{
		.flags = 0,
		.tag = 0,
		.offset = offsetof(X9_62_CHARACTERISTIC_TWO, type),
		.field_name = "type",
		.item = &ASN1_OBJECT_it,
	},
	{
		.flags = ASN1_TFLG_ADB_OID,
		.tag = -1,
		.offset = 0,
		.field_name = "X9_62_CHARACTERISTIC_TWO",
		.item = (const ASN1_ITEM *)&X9_62_CHARACTERISTIC_TWO_adb,
	},
};

static const ASN1_ITEM X9_62_CHARACTERISTIC_TWO_it = {
	.itype = ASN1_ITYPE_SEQUENCE,
	.utype = V_ASN1_SEQUENCE,
	.templates = X9_62_CHARACTERISTIC_TWO_seq_tt,
	.tcount = sizeof(X9_62_CHARACTERISTIC_TWO_seq_tt) / sizeof(ASN1_TEMPLATE),
	.funcs = NULL,
	.size = sizeof(X9_62_CHARACTERISTIC_TWO),
	.sname = "X9_62_CHARACTERISTIC_TWO",
};

static const ASN1_TEMPLATE fieldID_def_tt = {
	.flags = 0,
	.tag = 0,
	.offset = offsetof(X9_62_FIELDID, p.other),
	.field_name = "p.other",
	.item = &ASN1_ANY_it,
};

static const ASN1_ADB_TABLE X9_62_FIELDID_adbtbl[] = {
	{
		.value = NID_X9_62_prime_field,
		.tt = {
			.flags = 0,
			.tag = 0,
			.offset = offsetof(X9_62_FIELDID, p.prime),
			.field_name = "p.prime",
			.item = &ASN1_INTEGER_it,
		},
	},
	{
		.value = NID_X9_62_characteristic_two_field,
		.tt = {
			.flags = 0,
			.tag = 0,
			.offset = offsetof(X9_62_FIELDID, p.char_two),
			.field_name = "p.char_two",
			.item = &X9_62_CHARACTERISTIC_TWO_it,
		},
	},
};

static const ASN1_ADB X9_62_FIELDID_adb = {
	.flags = 0,
	.offset = offsetof(X9_62_FIELDID, fieldType),
	.tbl = X9_62_FIELDID_adbtbl,
	.tblcount = sizeof(X9_62_FIELDID_adbtbl) / sizeof(ASN1_ADB_TABLE),
	.default_tt = &fieldID_def_tt,
	.null_tt = NULL,
};

static const ASN1_TEMPLATE X9_62_FIELDID_seq_tt[] = {
	{
		.flags = 0,
		.tag = 0,
		.offset = offsetof(X9_62_FIELDID, fieldType),
		.field_name = "fieldType",
		.item = &ASN1_OBJECT_it,
	},
	{
		.flags = ASN1_TFLG_ADB_OID,
		.tag = -1,
		.offset = 0,
		.field_name = "X9_62_FIELDID",
		.item = (const ASN1_ITEM *)&X9_62_FIELDID_adb,
	},
};

static const ASN1_ITEM X9_62_FIELDID_it = {
	.itype = ASN1_ITYPE_SEQUENCE,
	.utype = V_ASN1_SEQUENCE,
	.templates = X9_62_FIELDID_seq_tt,
	.tcount = sizeof(X9_62_FIELDID_seq_tt) / sizeof(ASN1_TEMPLATE),
	.funcs = NULL,
	.size = sizeof(X9_62_FIELDID),
	.sname = "X9_62_FIELDID",
};

static const ASN1_TEMPLATE X9_62_CURVE_seq_tt[] = {
	{
		.flags = 0,
		.tag = 0,
		.offset = offsetof(X9_62_CURVE, a),
		.field_name = "a",
		.item = &ASN1_OCTET_STRING_it,
	},
	{
		.flags = 0,
		.tag = 0,
		.offset = offsetof(X9_62_CURVE, b),
		.field_name = "b",
		.item = &ASN1_OCTET_STRING_it,
	},
	{
		.flags = ASN1_TFLG_OPTIONAL,
		.tag = 0,
		.offset = offsetof(X9_62_CURVE, seed),
		.field_name = "seed",
		.item = &ASN1_BIT_STRING_it,
	},
};

static const ASN1_ITEM X9_62_CURVE_it = {
	.itype = ASN1_ITYPE_SEQUENCE,
	.utype = V_ASN1_SEQUENCE,
	.templates = X9_62_CURVE_seq_tt,
	.tcount = sizeof(X9_62_CURVE_seq_tt) / sizeof(ASN1_TEMPLATE),
	.funcs = NULL,
	.size = sizeof(X9_62_CURVE),
	.sname = "X9_62_CURVE",
};

static const ASN1_TEMPLATE ECPARAMETERS_seq_tt[] = {
	{
		.flags = 0,
		.tag = 0,
		.offset = offsetof(ECPARAMETERS, version),
		.field_name = "version",
		.item = &LONG_it,
	},
	{
		.flags = 0,
		.tag = 0,
		.offset = offsetof(ECPARAMETERS, fieldID),
		.field_name = "fieldID",
		.item = &X9_62_FIELDID_it,
	},
	{
		.flags = 0,
		.tag = 0,
		.offset = offsetof(ECPARAMETERS, curve),
		.field_name = "curve",
		.item = &X9_62_CURVE_it,
	},
	{
		.flags = 0,
		.tag = 0,
		.offset = offsetof(ECPARAMETERS, base),
		.field_name = "base",
		.item = &ASN1_OCTET_STRING_it,
	},
	{
		.flags = 0,
		.tag = 0,
		.offset = offsetof(ECPARAMETERS, order),
		.field_name = "order",
		.item = &ASN1_INTEGER_it,
	},
	{
		.flags = ASN1_TFLG_OPTIONAL,
		.tag = 0,
		.offset = offsetof(ECPARAMETERS, cofactor),
		.field_name = "cofactor",
		.item = &ASN1_INTEGER_it,
	},
};

static const ASN1_ITEM ECPARAMETERS_it = {
	.itype = ASN1_ITYPE_SEQUENCE,
	.utype = V_ASN1_SEQUENCE,
	.templates = ECPARAMETERS_seq_tt,
	.tcount = sizeof(ECPARAMETERS_seq_tt) / sizeof(ASN1_TEMPLATE),
	.funcs = NULL,
	.size = sizeof(ECPARAMETERS),
	.sname = "ECPARAMETERS",
};

static ECPARAMETERS *
ECPARAMETERS_new(void)
{
	return (ECPARAMETERS*)ASN1_item_new(&ECPARAMETERS_it);
}

static void
ECPARAMETERS_free(ECPARAMETERS *a)
{
	ASN1_item_free((ASN1_VALUE *)a, &ECPARAMETERS_it);
}

static const ASN1_TEMPLATE ECPKPARAMETERS_ch_tt[] = {
	{
		.flags = 0,
		.tag = 0,
		.offset = offsetof(ECPKPARAMETERS, value.named_curve),
		.field_name = "value.named_curve",
		.item = &ASN1_OBJECT_it,
	},
	{
		.flags = 0,
		.tag = 0,
		.offset = offsetof(ECPKPARAMETERS, value.parameters),
		.field_name = "value.parameters",
		.item = &ECPARAMETERS_it,
	},
	{
		.flags = 0,
		.tag = 0,
		.offset = offsetof(ECPKPARAMETERS, value.implicitlyCA),
		.field_name = "value.implicitlyCA",
		.item = &ASN1_NULL_it,
	},
};

static const ASN1_ITEM ECPKPARAMETERS_it = {
	.itype = ASN1_ITYPE_CHOICE,
	.utype = offsetof(ECPKPARAMETERS, type),
	.templates = ECPKPARAMETERS_ch_tt,
	.tcount = sizeof(ECPKPARAMETERS_ch_tt) / sizeof(ASN1_TEMPLATE),
	.funcs = NULL,
	.size = sizeof(ECPKPARAMETERS),
	.sname = "ECPKPARAMETERS",
};

static ECPKPARAMETERS *
d2i_ECPKPARAMETERS(ECPKPARAMETERS **a, const unsigned char **in, long len)
{
	return (ECPKPARAMETERS *)ASN1_item_d2i((ASN1_VALUE **)a, in, len,
	    &ECPKPARAMETERS_it);
}

static int
i2d_ECPKPARAMETERS(const ECPKPARAMETERS *a, unsigned char **out)
{
	return ASN1_item_i2d((ASN1_VALUE *)a, out, &ECPKPARAMETERS_it);
}

static ECPKPARAMETERS *
ECPKPARAMETERS_new(void)
{
	return (ECPKPARAMETERS *)ASN1_item_new(&ECPKPARAMETERS_it);
}

static void
ECPKPARAMETERS_free(ECPKPARAMETERS *a)
{
	ASN1_item_free((ASN1_VALUE *)a, &ECPKPARAMETERS_it);
}

static const ASN1_TEMPLATE EC_PRIVATEKEY_seq_tt[] = {
	{
		.flags = 0,
		.tag = 0,
		.offset = offsetof(EC_PRIVATEKEY, version),
		.field_name = "version",
		.item = &LONG_it,
	},
	{
		.flags = 0,
		.tag = 0,
		.offset = offsetof(EC_PRIVATEKEY, privateKey),
		.field_name = "privateKey",
		.item = &ASN1_OCTET_STRING_it,
	},
	{
		.flags = ASN1_TFLG_EXPLICIT | ASN1_TFLG_OPTIONAL,
		.tag = 0,
		.offset = offsetof(EC_PRIVATEKEY, parameters),
		.field_name = "parameters",
		.item = &ECPKPARAMETERS_it,
	},
	{
		.flags = ASN1_TFLG_EXPLICIT | ASN1_TFLG_OPTIONAL,
		.tag = 1,
		.offset = offsetof(EC_PRIVATEKEY, publicKey),
		.field_name = "publicKey",
		.item = &ASN1_BIT_STRING_it,
	},
};

static const ASN1_ITEM EC_PRIVATEKEY_it = {
	.itype = ASN1_ITYPE_SEQUENCE,
	.utype = V_ASN1_SEQUENCE,
	.templates = EC_PRIVATEKEY_seq_tt,
	.tcount = sizeof(EC_PRIVATEKEY_seq_tt) / sizeof(ASN1_TEMPLATE),
	.funcs = NULL,
	.size = sizeof(EC_PRIVATEKEY),
	.sname = "EC_PRIVATEKEY",
};

static EC_PRIVATEKEY *
d2i_EC_PRIVATEKEY(EC_PRIVATEKEY **a, const unsigned char **in, long len)
{
	return (EC_PRIVATEKEY *)ASN1_item_d2i((ASN1_VALUE **)a, in, len,
	    &EC_PRIVATEKEY_it);
}

static int
i2d_EC_PRIVATEKEY(const EC_PRIVATEKEY *a, unsigned char **out)
{
	return ASN1_item_i2d((ASN1_VALUE *)a, out, &EC_PRIVATEKEY_it);
}

static EC_PRIVATEKEY *
EC_PRIVATEKEY_new(void)
{
	return (EC_PRIVATEKEY *)ASN1_item_new(&EC_PRIVATEKEY_it);
}

static void
EC_PRIVATEKEY_free(EC_PRIVATEKEY *a)
{
	ASN1_item_free((ASN1_VALUE *)a, &EC_PRIVATEKEY_it);
}

static int
ec_point_from_asn1_string(const EC_GROUP *group, const ASN1_STRING *astr,
    EC_POINT **out_point, uint8_t *out_form)
{
	return ec_point_from_octets(group, astr->data, astr->length,
	    out_point, out_form, NULL);
}

static int
ec_point_from_asn1_bit_string(const EC_GROUP *group, const ASN1_BIT_STRING *abs,
    EC_POINT **out_point, uint8_t *out_form)
{
	/*
	 * Per SEC 1, C.3, the bit string representing the public key comes from
	 * an octet string, therefore the unused bits octet must be 0x00.
	 * XXX - move this check to a helper in a_bitstr.c?
	 */
	if ((abs->flags & ASN1_STRING_FLAG_BITS_LEFT) != 0 &&
	    (abs->flags & 0x07) != 0)
		return 0;

	return ec_point_from_asn1_string(group, abs, out_point, out_form);
}

static int
ec_point_from_asn1_octet_string(const EC_GROUP *group, const ASN1_OCTET_STRING *aos,
    EC_POINT **out_point, uint8_t *out_form)
{
	return ec_point_from_asn1_string(group, aos, out_point, out_form);
}

static int
ec_point_to_asn1_string_type(const EC_GROUP *group, const EC_POINT *point,
    int form, int type, ASN1_STRING **out_astr)
{
	ASN1_STRING *astr = NULL;
	unsigned char *buf = NULL;
	size_t len = 0;
	int ret = 0;

	if (*out_astr != NULL && ASN1_STRING_type(*out_astr) != type)
		goto err;

	if (!ec_point_to_octets(group, point, form, &buf, &len, NULL))
		goto err;

	if ((astr = *out_astr) == NULL)
		astr = ASN1_STRING_type_new(type);
	if (astr == NULL)
		goto err;

	ASN1_STRING_set0(astr, buf, len);
	buf = NULL;
	len = 0;

	*out_astr = astr;
	astr = NULL;

	ret = 1;

 err:
	ASN1_STRING_free(astr);
	freezero(buf, len);

	return ret;
}

static int
ec_point_to_asn1_bit_string(const EC_GROUP *group, const EC_POINT *point,
    int form, ASN1_BIT_STRING **out_abs)
{
	if (!ec_point_to_asn1_string_type(group, point, form,
	    V_ASN1_BIT_STRING, out_abs))
		return 0;

	return asn1_abs_set_unused_bits(*out_abs, 0);
}

static int
ec_point_to_asn1_octet_string(const EC_GROUP *group, const EC_POINT *point,
    int form, ASN1_OCTET_STRING **out_aos)
{
	return ec_point_to_asn1_string_type(group, point, form,
	    V_ASN1_OCTET_STRING, out_aos);
}

static int
ec_asn1_group2fieldid(const EC_GROUP *group, X9_62_FIELDID *field)
{
	int ret = 0;

	if (group == NULL || field == NULL)
		goto err;

	if ((field->fieldType = OBJ_nid2obj(NID_X9_62_prime_field)) == NULL) {
		ECerror(ERR_R_OBJ_LIB);
		goto err;
	}
	if ((field->p.prime = BN_to_ASN1_INTEGER(group->p, NULL)) == NULL) {
		ECerror(ERR_R_ASN1_LIB);
		goto err;
	}

	ret = 1;

 err:
	return ret;
}

static int
ec_asn1_encode_bn(const EC_GROUP *group, const BIGNUM *bn, int len,
    ASN1_OCTET_STRING *os)
{
	unsigned char *buf;
	int ret = 0;

	/* One extra byte for historic NUL termination of ASN1_STRINGs. */
	if ((buf = calloc(1, len + 1)) == NULL)
		goto err;

	if (BN_bn2binpad(bn, buf, len) != len)
		goto err;

	ASN1_STRING_set0(os, buf, len);
	buf = NULL;
	len = 0;

	ret = 1;

 err:
	freezero(buf, len);

	return ret;
}

static int
ec_asn1_encode_field_element(const EC_GROUP *group, const BIGNUM *bn,
    ASN1_OCTET_STRING *os)
{
	/* Zero-pad field element to byte length of p per SEC 1, 2.3.5. */
	return ec_asn1_encode_bn(group, bn, BN_num_bytes(group->p), os);
}

static int
ec_asn1_encode_private_key(const EC_GROUP *group, const BIGNUM *bn,
    ASN1_OCTET_STRING *os)
{
	/* Zero-pad private key to byte length of order per SEC 1, C.4. */
	return ec_asn1_encode_bn(group, bn, BN_num_bytes(group->order), os);
}

static int
ec_asn1_group2curve(const EC_GROUP *group, X9_62_CURVE *curve)
{
	BIGNUM *a = NULL, *b = NULL;
	int ret = 0;

	if (group == NULL)
		goto err;
	if (curve == NULL || curve->a == NULL || curve->b == NULL)
		goto err;

	if ((a = BN_new()) == NULL || (b = BN_new()) == NULL) {
		ECerror(ERR_R_MALLOC_FAILURE);
		goto err;
	}

	if (!EC_GROUP_get_curve(group, NULL, a, b, NULL)) {
		ECerror(ERR_R_EC_LIB);
		goto err;
	}

	if (!ec_asn1_encode_field_element(group, a, curve->a)) {
		ECerror(ERR_R_EC_LIB);
		goto err;
	}
	if (!ec_asn1_encode_field_element(group, b, curve->b)) {
		ECerror(ERR_R_EC_LIB);
		goto err;
	}

	ASN1_BIT_STRING_free(curve->seed);
	curve->seed = NULL;

	if (group->seed != NULL) {
		if ((curve->seed = ASN1_BIT_STRING_new()) == NULL) {
			ECerror(ERR_R_MALLOC_FAILURE);
			goto err;
		}
		if (!ASN1_BIT_STRING_set(curve->seed,
		    group->seed, group->seed_len)) {
			ECerror(ERR_R_ASN1_LIB);
			goto err;
		}
		if (!asn1_abs_set_unused_bits(curve->seed, 0)) {
			ECerror(ERR_R_ASN1_LIB);
			goto err;
		}
	}

	ret = 1;

 err:
	BN_free(a);
	BN_free(b);

	return ret;
}

static ECPARAMETERS *
ec_asn1_group2parameters(const EC_GROUP *group)
{
	ECPARAMETERS *parameters = NULL;
	const EC_POINT *generator = NULL;
	const BIGNUM *order, *cofactor;
	uint8_t form;

	if ((parameters = ECPARAMETERS_new()) == NULL) {
		ECerror(ERR_R_MALLOC_FAILURE);
		goto err;
	}

	parameters->version = 0x1;

	if (!ec_asn1_group2fieldid(group, parameters->fieldID)) {
		ECerror(ERR_R_EC_LIB);
		goto err;
	}

	if (!ec_asn1_group2curve(group, parameters->curve)) {
		ECerror(ERR_R_EC_LIB);
		goto err;
	}

	if ((generator = EC_GROUP_get0_generator(group)) == NULL) {
		ECerror(EC_R_UNDEFINED_GENERATOR);
		goto err;
	}

	form = EC_GROUP_get_point_conversion_form(group);
	if (!ec_point_to_asn1_octet_string(group, generator, form, &parameters->base))
		goto err;

	if ((order = EC_GROUP_get0_order(group)) == NULL) {
		ECerror(ERR_R_EC_LIB);
		goto err;
	}
	if (BN_is_zero(order)) {
		ECerror(ERR_R_EC_LIB);
		goto err;
	}
	ASN1_INTEGER_free(parameters->order);
	if ((parameters->order = BN_to_ASN1_INTEGER(order, NULL)) == NULL) {
		ECerror(ERR_R_ASN1_LIB);
		goto err;
	}

	ASN1_INTEGER_free(parameters->cofactor);
	parameters->cofactor = NULL;
	if ((cofactor = EC_GROUP_get0_cofactor(group)) == NULL) {
		ECerror(ERR_R_EC_LIB);
		goto err;
	}
	if (!BN_is_zero(cofactor)) {
		if ((parameters->cofactor = BN_to_ASN1_INTEGER(cofactor,
		    NULL)) == NULL) {
			ECerror(ERR_R_ASN1_LIB);
			goto err;
		}
	}

	return parameters;

 err:
	ECPARAMETERS_free(parameters);

	return NULL;
}

static ECPKPARAMETERS *
ec_asn1_group2pkparameters(const EC_GROUP *group)
{
	ECPKPARAMETERS *pkparameters;
	ECPARAMETERS *parameters;
	ASN1_OBJECT *aobj;
	int nid;

	if ((pkparameters = ECPKPARAMETERS_new()) == NULL) {
		ECerror(ERR_R_MALLOC_FAILURE);
		goto err;
	}

	if ((EC_GROUP_get_asn1_flag(group) & OPENSSL_EC_NAMED_CURVE) != 0) {
		if ((nid = EC_GROUP_get_curve_name(group)) == NID_undef)
			goto err;
		if ((aobj = OBJ_nid2obj(nid)) == NULL)
			goto err;
		pkparameters->type = ECPK_PARAM_NAMED_CURVE;
		pkparameters->value.named_curve = aobj;
	} else {
		if ((parameters = ec_asn1_group2parameters(group)) == NULL)
			goto err;
		pkparameters->type = ECPK_PARAM_EXPLICIT;
		pkparameters->value.parameters = parameters;
		parameters = NULL;
	}

	return pkparameters;

 err:
	ECPKPARAMETERS_free(pkparameters);

	return NULL;
}

static int
ec_asn1_is_prime_field(const X9_62_FIELDID *fieldid)
{
	int nid;

	if (fieldid == NULL) {
		ECerror(EC_R_ASN1_ERROR);
		return 0;
	}
	if ((nid = OBJ_obj2nid(fieldid->fieldType)) == NID_undef) {
		ECerror(EC_R_INVALID_FIELD);
		return 0;
	}
	if (nid == NID_X9_62_characteristic_two_field) {
		ECerror(EC_R_GF2M_NOT_SUPPORTED);
		return 0;
	}
	if (nid != NID_X9_62_prime_field) {
		ECerror(EC_R_UNSUPPORTED_FIELD);
		return 0;
	}

	/* We can't check that this is actually a prime due to DoS risk. */
	if (fieldid->p.prime == NULL) {
		ECerror(EC_R_INVALID_FIELD);
		return 0;
	}

	return 1;
}

static int
ec_asn1_parameters_curve2group(const X9_62_CURVE *curve,
    const ASN1_INTEGER *prime, EC_GROUP **out_group)
{
	EC_GROUP *group = NULL;
	BIGNUM *p = NULL, *a = NULL, *b = NULL;
	int ret = 0;

	if (*out_group != NULL)
		goto err;

	if ((p = ASN1_INTEGER_to_BN(prime, NULL)) == NULL)
		goto err;
	if ((a = BN_bin2bn(curve->a->data, curve->a->length, NULL)) == NULL)
		goto err;
	if ((b = BN_bin2bn(curve->b->data, curve->b->length, NULL)) == NULL)
		goto err;

	/*
	 * XXX - move these checks to ec_GFp_simple_group_set_curve()?
	 * What about checking 0 <= a, b < p?
	 */
	if (BN_is_zero(p) || BN_is_negative(p)) {
		ECerror(EC_R_INVALID_FIELD);
		goto err;
	}
	if (BN_num_bits(p) > OPENSSL_ECC_MAX_FIELD_BITS) {
		ECerror(EC_R_FIELD_TOO_LARGE);
		goto err;
	}

	if ((group = EC_GROUP_new_curve_GFp(p, a, b, NULL)) == NULL)
		goto err;

	*out_group = group;
	group = NULL;

	ret = 1;

 err:
	BN_free(p);
	BN_free(a);
	BN_free(b);
	EC_GROUP_free(group);

	return ret;
}

static int
ec_asn1_set_group_parameters(const ECPARAMETERS *params, EC_GROUP *group)
{
	EC_POINT *generator = NULL;
	BIGNUM *order = NULL, *cofactor = NULL;
	const ASN1_BIT_STRING *seed;
	uint8_t form;
	int ret = 0;

	if (!ec_point_from_asn1_octet_string(group, params->base, &generator, &form))
		goto err;
	EC_GROUP_set_point_conversion_form(group, form);

	if ((order = ASN1_INTEGER_to_BN(params->order, NULL)) == NULL) {
		ECerror(ERR_R_ASN1_LIB);
		goto err;
	}
	if (params->cofactor != NULL) {
		if ((cofactor = ASN1_INTEGER_to_BN(params->cofactor,
		    NULL)) == NULL) {
			ECerror(ERR_R_ASN1_LIB);
			goto err;
		}
	}

	/* Checks the Hasse bound and sets the cofactor if possible or fails. */
	if (!EC_GROUP_set_generator(group, generator, order, cofactor)) {
		ECerror(ERR_R_EC_LIB);
		goto err;
	}

	if ((seed = params->curve->seed) != NULL) {
		if (EC_GROUP_set_seed(group, seed->data, seed->length) == 0) {
			ECerror(ERR_R_MALLOC_FAILURE);
			goto err;
		}
	}

	ret = 1;

 err:
	EC_POINT_free(generator);
	BN_free(order);
	BN_free(cofactor);

	return ret;
}

static int
ec_asn1_parameters_extract_prime_group(const ECPARAMETERS *params,
    EC_GROUP **out_group)
{
	EC_GROUP *group = NULL;
	int ret = 0;

	if (*out_group != NULL)
		goto err;

	if (!ec_asn1_is_prime_field(params->fieldID))
		goto err;
	if (!ec_asn1_parameters_curve2group(params->curve,
	    params->fieldID->p.prime, &group))
		goto err;
	if (!ec_asn1_set_group_parameters(params, group))
		goto err;

	*out_group = group;
	group = NULL;

	ret = 1;

 err:
	EC_GROUP_free(group);

	return ret;
}

static EC_GROUP *
ec_asn1_parameters2group(const ECPARAMETERS *params)
{
	EC_GROUP *group = NULL;
	int nid = NID_undef;

	if (params == NULL) {
		ECerror(EC_R_ASN1_ERROR);
		goto err;
	}

	if (!ec_asn1_parameters_extract_prime_group(params, &group))
		goto err;
	if (!ec_group_is_builtin_curve(group, &nid))
		goto err;
	EC_GROUP_set_curve_name(group, nid);

	return group;

 err:
	EC_GROUP_free(group);

	return NULL;
}

static EC_GROUP *
ec_asn1_pkparameters2group(const ECPKPARAMETERS *params)
{
	EC_GROUP *group;
	int nid;

	if (params->type == ECPK_PARAM_NAMED_CURVE) {
		if ((nid = OBJ_obj2nid(params->value.named_curve)) == NID_undef) {
			ECerror(EC_R_UNKNOWN_GROUP);
			return NULL;
		}
		if ((group = EC_GROUP_new_by_curve_name(nid)) == NULL) {
			ECerror(EC_R_EC_GROUP_NEW_BY_NAME_FAILURE);
			return NULL;
		}
		EC_GROUP_set_asn1_flag(group, OPENSSL_EC_NAMED_CURVE);
	} else if (params->type == ECPK_PARAM_EXPLICIT) {
		group = ec_asn1_parameters2group(params->value.parameters);
		if (group == NULL) {
			ECerror(ERR_R_EC_LIB);
			return NULL;
		}
		EC_GROUP_set_asn1_flag(group, OPENSSL_EC_EXPLICIT_CURVE);
	} else if (params->type == ECPK_PARAM_IMPLICITLY_CA) {
		return NULL;
	} else {
		ECerror(EC_R_ASN1_ERROR);
		return NULL;
	}

	return group;
}

EC_GROUP *
d2i_ECPKParameters(EC_GROUP **a, const unsigned char **in, long len)
{
	EC_GROUP *group = NULL;
	ECPKPARAMETERS *params;

	if ((params = d2i_ECPKPARAMETERS(NULL, in, len)) == NULL) {
		ECerror(EC_R_D2I_ECPKPARAMETERS_FAILURE);
		goto err;
	}
	if ((group = ec_asn1_pkparameters2group(params)) == NULL) {
		ECerror(EC_R_PKPARAMETERS2GROUP_FAILURE);
		goto err;
	}

	if (a != NULL) {
		EC_GROUP_free(*a);
		*a = group;
	}

 err:
	ECPKPARAMETERS_free(params);

	return group;
}
LCRYPTO_ALIAS(d2i_ECPKParameters);

int
i2d_ECPKParameters(const EC_GROUP *group, unsigned char **out_der)
{
	ECPKPARAMETERS *parameters;
	int ret = 0;

	if ((parameters = ec_asn1_group2pkparameters(group)) == NULL) {
		ECerror(EC_R_GROUP2PKPARAMETERS_FAILURE);
		goto err;
	}
	if ((ret = i2d_ECPKPARAMETERS(parameters, out_der)) <= 0) {
		ECerror(EC_R_I2D_ECPKPARAMETERS_FAILURE);
		goto err;
	}

 err:
	ECPKPARAMETERS_free(parameters);

	return ret;
}
LCRYPTO_ALIAS(i2d_ECPKParameters);

static int
ec_key_set_group_from_parameters(EC_KEY *ec_key, const ECPKPARAMETERS *params)
{
	EC_GROUP *group = NULL;
	int ret = 0;

	/* Use group in parameters, if any. Fall back to existing group. */
	if (params != NULL) {
		if ((group = ec_asn1_pkparameters2group(params)) == NULL)
			goto err;
		if (!EC_KEY_set_group(ec_key, group))
			goto err;
	}
	if (ec_key->group == NULL)
		goto err;

	ret = 1;

 err:
	EC_GROUP_free(group);

	return ret;
}

static int
ec_key_set_private_key(EC_KEY *ec_key, const ASN1_OCTET_STRING *aos)
{
	BIGNUM *priv_key = NULL;
	int ret = 0;

	if (aos == NULL) {
		ECerror(EC_R_MISSING_PRIVATE_KEY);
		goto err;
	}

	/*
	 * XXX - Sec 1, C.4 requires that this octet string be padded to the
	 * byte length of the group's order. This can't be enforced because
	 * i2d_ECPrivateKey() used to produce a semi-compatible ad hoc format.
	 */
	if ((priv_key = BN_bin2bn(aos->data, aos->length, NULL)) == NULL)
		goto err;
	if (!EC_KEY_set_private_key(ec_key, priv_key))
		goto err;

	ret = 1;

 err:
	BN_free(priv_key);

	return ret;
}

static int
ec_key_set_public_key(EC_KEY *ec_key, const ASN1_BIT_STRING *abs)
{
	EC_POINT *pub_key = NULL;
	uint8_t form;
	int ret = 0;

	if (abs == NULL) {
		ec_key->enc_flag |= EC_PKEY_NO_PUBKEY;
		return eckey_compute_pubkey(ec_key);
	}

	/* XXX - SEC 1, 2.3.4 does not allow hybrid encoding. */
	if (!ec_point_from_asn1_bit_string(ec_key->group, abs, &pub_key, &form))
		goto err;
	if (!EC_KEY_set_public_key(ec_key, pub_key))
		goto err;
	EC_KEY_set_conv_form(ec_key, form);

	ret = 1;

 err:
	EC_POINT_free(pub_key);

	return ret;
}

EC_KEY *
d2i_ECPrivateKey(EC_KEY **out_ec_key, const unsigned char **in, long len)
{
	EC_KEY *ec_key = NULL;
	EC_PRIVATEKEY *ec_privatekey = NULL;

	if (out_ec_key == NULL || (ec_key = *out_ec_key) == NULL)
		ec_key = EC_KEY_new();
	if (ec_key == NULL)
		goto err;

	if ((ec_privatekey = d2i_EC_PRIVATEKEY(NULL, in, len)) == NULL) {
		ECerror(ERR_R_EC_LIB);
		goto err;
	}

	ec_key->version = ec_privatekey->version;
	if (!ec_key_set_group_from_parameters(ec_key, ec_privatekey->parameters))
		goto err;
	if (!ec_key_set_private_key(ec_key, ec_privatekey->privateKey))
		goto err;
	if (!ec_key_set_public_key(ec_key, ec_privatekey->publicKey))
		goto err;

	EC_PRIVATEKEY_free(ec_privatekey);
	ec_privatekey = NULL;

	if (out_ec_key != NULL)
		*out_ec_key = ec_key;

	return ec_key;

 err:
	if (out_ec_key == NULL || *out_ec_key != ec_key)
		EC_KEY_free(ec_key);
	EC_PRIVATEKEY_free(ec_privatekey);

	return NULL;
}
LCRYPTO_ALIAS(d2i_ECPrivateKey);

int
i2d_ECPrivateKey(EC_KEY *ec_key, unsigned char **out)
{
	EC_PRIVATEKEY *ec_privatekey = NULL;
	const EC_GROUP *group;
	const BIGNUM *private_key;
	const EC_POINT *public_key = NULL;
	int ret = 0;

	if (ec_key == NULL) {
		ECerror(ERR_R_PASSED_NULL_PARAMETER);
		goto err;
	}
	if ((group = EC_KEY_get0_group(ec_key)) == NULL) {
		ECerror(EC_R_MISSING_PARAMETERS);
		goto err;
	}
	if ((private_key = EC_KEY_get0_private_key(ec_key)) == NULL) {
		ECerror(EC_R_KEYS_NOT_SET);
		goto err;
	}
	if ((ec_key->enc_flag & EC_PKEY_NO_PUBKEY) == 0) {
		if ((public_key = EC_KEY_get0_public_key(ec_key)) == NULL) {
			ECerror(EC_R_KEYS_NOT_SET);
			goto err;
		}
	}

	if ((ec_privatekey = EC_PRIVATEKEY_new()) == NULL) {
		ECerror(ERR_R_MALLOC_FAILURE);
		goto err;
	}
	ec_privatekey->version = ec_key->version;

	if (!ec_asn1_encode_private_key(group, private_key, ec_privatekey->privateKey))
		goto err;
	if ((ec_key->enc_flag & EC_PKEY_NO_PARAMETERS) == 0) {
		ECPKPARAMETERS *parameters;

		if ((parameters = ec_asn1_group2pkparameters(group)) == NULL) {
			ECerror(ERR_R_EC_LIB);
			goto err;
		}
		ec_privatekey->parameters = parameters;
	}
	if (public_key != NULL) {
		uint8_t form;

		form = EC_KEY_get_conv_form(ec_key);
		if (!ec_point_to_asn1_bit_string(group, public_key, form,
		    &ec_privatekey->publicKey))
			goto err;
	}

	if ((ret = i2d_EC_PRIVATEKEY(ec_privatekey, out)) <= 0) {
		ECerror(ERR_R_EC_LIB);
		goto err;
	}

 err:
	EC_PRIVATEKEY_free(ec_privatekey);

	return ret;
}
LCRYPTO_ALIAS(i2d_ECPrivateKey);

int
i2d_ECParameters(EC_KEY *ec_key, unsigned char **out)
{
	if (ec_key == NULL) {
		ECerror(ERR_R_PASSED_NULL_PARAMETER);
		return 0;
	}
	return i2d_ECPKParameters(ec_key->group, out);
}
LCRYPTO_ALIAS(i2d_ECParameters);

EC_KEY *
d2i_ECParameters(EC_KEY **out_ec_key, const unsigned char **in, long len)
{
	EC_KEY *ec_key = NULL;

	if (in == NULL || *in == NULL) {
		ECerror(ERR_R_PASSED_NULL_PARAMETER);
		goto err;
	}
	if (out_ec_key == NULL || (ec_key = *out_ec_key) == NULL)
		ec_key = EC_KEY_new();
	if (ec_key == NULL) {
		ECerror(ERR_R_MALLOC_FAILURE);
		goto err;
	}

	if (!d2i_ECPKParameters(&ec_key->group, in, len)) {
		ECerror(ERR_R_EC_LIB);
		goto err;
	}

	if (out_ec_key != NULL)
		*out_ec_key = ec_key;

	return ec_key;

 err:
	if (out_ec_key == NULL || *out_ec_key != ec_key)
		EC_KEY_free(ec_key);

	return NULL;
}
LCRYPTO_ALIAS(d2i_ECParameters);

EC_KEY *
ECParameters_dup(EC_KEY *key)
{
	const unsigned char *p;
	unsigned char *der = NULL;
	EC_KEY *dup = NULL;
	int len;

	if (key == NULL)
		return NULL;

	if ((len = i2d_ECParameters(key, &der)) <= 0)
		return NULL;

	p = der;
	dup = d2i_ECParameters(NULL, &p, len);
	freezero(der, len);

	return dup;
}
LCRYPTO_ALIAS(ECParameters_dup);

EC_KEY *
o2i_ECPublicKey(EC_KEY **in_ec_key, const unsigned char **in, long len)
{
	EC_KEY *ec_key = NULL;
	const EC_GROUP *group;
	uint8_t form;

	if (in_ec_key == NULL || (ec_key = *in_ec_key) == NULL) {
		ECerror(ERR_R_PASSED_NULL_PARAMETER);
		return NULL;
	}
	if ((group = ec_key->group) == NULL) {
		ECerror(ERR_R_PASSED_NULL_PARAMETER);
		return NULL;
	}
	if (len < 0) {
		ECerror(EC_R_INVALID_ARGUMENT);
		return NULL;
	}

	if (!ec_point_from_octets(group, *in, len, &ec_key->pub_key, &form, NULL))
		return NULL;
	EC_KEY_set_conv_form(ec_key, form);

	*in += len;

	return ec_key;
}
LCRYPTO_ALIAS(o2i_ECPublicKey);

int
i2o_ECPublicKey(const EC_KEY *ec_key, unsigned char **out)
{
	unsigned char *buf = NULL;
	size_t buf_len = 0;
	int ret = 0;

	if (ec_key == NULL) {
		ECerror(ERR_R_PASSED_NULL_PARAMETER);
		goto err;
	}

	if (!ec_point_to_octets(ec_key->group, ec_key->pub_key,
	    ec_key->conv_form, &buf, &buf_len, NULL))
		goto err;
	if (buf_len > INT_MAX)
		goto err;

	if (out != NULL && *out != NULL) {
		/* Muppet's answer to the Jackass show. */
		memcpy(*out, buf, buf_len);
		*out += buf_len;
	} else if (out != NULL) {
		*out = buf;
		buf = NULL;
	}

	ret = buf_len;

 err:
	freezero(buf, buf_len);

	return ret;
}
LCRYPTO_ALIAS(i2o_ECPublicKey);
