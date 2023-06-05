// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2022 Oracle and/or its affiliates.
 *
 * KUnit test of SunRPC's GSS Kerberos mechanism. Subsystem
 * name is "rpcsec_gss_krb5".
 */

#include <kunit/test.h>
#include <kunit/visibility.h>

#include <linux/kernel.h>
#include <crypto/hash.h>

#include <linux/sunrpc/xdr.h>
#include <linux/sunrpc/gss_krb5.h>

#include "gss_krb5_internal.h"

MODULE_IMPORT_NS(EXPORTED_FOR_KUNIT_TESTING);

struct gss_krb5_test_param {
	const char			*desc;
	u32				enctype;
	u32				nfold;
	u32				constant;
	const struct xdr_netobj		*base_key;
	const struct xdr_netobj		*Ke;
	const struct xdr_netobj		*usage;
	const struct xdr_netobj		*plaintext;
	const struct xdr_netobj		*confounder;
	const struct xdr_netobj		*expected_result;
	const struct xdr_netobj		*expected_hmac;
	const struct xdr_netobj		*next_iv;
};

static inline void gss_krb5_get_desc(const struct gss_krb5_test_param *param,
				     char *desc)
{
	strscpy(desc, param->desc, KUNIT_PARAM_DESC_SIZE);
}

static void kdf_case(struct kunit *test)
{
	const struct gss_krb5_test_param *param = test->param_value;
	const struct gss_krb5_enctype *gk5e;
	struct xdr_netobj derivedkey;
	int err;

	/* Arrange */
	gk5e = gss_krb5_lookup_enctype(param->enctype);
	if (!gk5e)
		kunit_skip(test, "Encryption type is not available");

	derivedkey.data = kunit_kzalloc(test, param->expected_result->len,
					GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, derivedkey.data);
	derivedkey.len = param->expected_result->len;

	/* Act */
	err = gk5e->derive_key(gk5e, param->base_key, &derivedkey,
			       param->usage, GFP_KERNEL);
	KUNIT_ASSERT_EQ(test, err, 0);

	/* Assert */
	KUNIT_EXPECT_EQ_MSG(test,
			    memcmp(param->expected_result->data,
				   derivedkey.data, derivedkey.len), 0,
			    "key mismatch");
}

static void checksum_case(struct kunit *test)
{
	const struct gss_krb5_test_param *param = test->param_value;
	struct xdr_buf buf = {
		.head[0].iov_len	= param->plaintext->len,
		.len			= param->plaintext->len,
	};
	const struct gss_krb5_enctype *gk5e;
	struct xdr_netobj Kc, checksum;
	struct crypto_ahash *tfm;
	int err;

	/* Arrange */
	gk5e = gss_krb5_lookup_enctype(param->enctype);
	if (!gk5e)
		kunit_skip(test, "Encryption type is not available");

	Kc.len = gk5e->Kc_length;
	Kc.data = kunit_kzalloc(test, Kc.len, GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, Kc.data);
	err = gk5e->derive_key(gk5e, param->base_key, &Kc,
			       param->usage, GFP_KERNEL);
	KUNIT_ASSERT_EQ(test, err, 0);

	tfm = crypto_alloc_ahash(gk5e->cksum_name, 0, CRYPTO_ALG_ASYNC);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, tfm);
	err = crypto_ahash_setkey(tfm, Kc.data, Kc.len);
	KUNIT_ASSERT_EQ(test, err, 0);

	buf.head[0].iov_base = kunit_kzalloc(test, buf.head[0].iov_len, GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, buf.head[0].iov_base);
	memcpy(buf.head[0].iov_base, param->plaintext->data, buf.head[0].iov_len);

	checksum.len = gk5e->cksumlength;
	checksum.data = kunit_kzalloc(test, checksum.len, GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, checksum.data);

	/* Act */
	err = gss_krb5_checksum(tfm, NULL, 0, &buf, 0, &checksum);
	KUNIT_ASSERT_EQ(test, err, 0);

	/* Assert */
	KUNIT_EXPECT_EQ_MSG(test,
			    memcmp(param->expected_result->data,
				   checksum.data, checksum.len), 0,
			    "checksum mismatch");

	crypto_free_ahash(tfm);
}

#define DEFINE_HEX_XDR_NETOBJ(name, hex_array...)		\
	static const u8 name ## _data[] = { hex_array };	\
	static const struct xdr_netobj name = {			\
		.data	= (u8 *)name##_data,			\
		.len	= sizeof(name##_data),			\
	}

#define DEFINE_STR_XDR_NETOBJ(name, string)			\
	static const u8 name ## _str[] = string;		\
	static const struct xdr_netobj name = {			\
		.data	= (u8 *)name##_str,			\
		.len	= sizeof(name##_str) - 1,		\
	}

/*
 * RFC 3961 Appendix A.1.  n-fold
 *
 * The n-fold function is defined in section 5.1 of RFC 3961.
 *
 * This test material is copyright (C) The Internet Society (2005).
 */

DEFINE_HEX_XDR_NETOBJ(nfold_test1_plaintext,
		      0x30, 0x31, 0x32, 0x33, 0x34, 0x35
);
DEFINE_HEX_XDR_NETOBJ(nfold_test1_expected_result,
		      0xbe, 0x07, 0x26, 0x31, 0x27, 0x6b, 0x19, 0x55
);

DEFINE_HEX_XDR_NETOBJ(nfold_test2_plaintext,
		      0x70, 0x61, 0x73, 0x73, 0x77, 0x6f, 0x72, 0x64
);
DEFINE_HEX_XDR_NETOBJ(nfold_test2_expected_result,
		      0x78, 0xa0, 0x7b, 0x6c, 0xaf, 0x85, 0xfa
);

DEFINE_HEX_XDR_NETOBJ(nfold_test3_plaintext,
		      0x52, 0x6f, 0x75, 0x67, 0x68, 0x20, 0x43, 0x6f,
		      0x6e, 0x73, 0x65, 0x6e, 0x73, 0x75, 0x73, 0x2c,
		      0x20, 0x61, 0x6e, 0x64, 0x20, 0x52, 0x75, 0x6e,
		      0x6e, 0x69, 0x6e, 0x67, 0x20, 0x43, 0x6f, 0x64,
		      0x65
);
DEFINE_HEX_XDR_NETOBJ(nfold_test3_expected_result,
		      0xbb, 0x6e, 0xd3, 0x08, 0x70, 0xb7, 0xf0, 0xe0
);

DEFINE_HEX_XDR_NETOBJ(nfold_test4_plaintext,
		      0x70, 0x61, 0x73, 0x73, 0x77, 0x6f, 0x72, 0x64
);
DEFINE_HEX_XDR_NETOBJ(nfold_test4_expected_result,
		      0x59, 0xe4, 0xa8, 0xca, 0x7c, 0x03, 0x85, 0xc3,
		      0xc3, 0x7b, 0x3f, 0x6d, 0x20, 0x00, 0x24, 0x7c,
		      0xb6, 0xe6, 0xbd, 0x5b, 0x3e
);

DEFINE_HEX_XDR_NETOBJ(nfold_test5_plaintext,
		      0x4d, 0x41, 0x53, 0x53, 0x41, 0x43, 0x48, 0x56,
		      0x53, 0x45, 0x54, 0x54, 0x53, 0x20, 0x49, 0x4e,
		      0x53, 0x54, 0x49, 0x54, 0x56, 0x54, 0x45, 0x20,
		      0x4f, 0x46, 0x20, 0x54, 0x45, 0x43, 0x48, 0x4e,
		      0x4f, 0x4c, 0x4f, 0x47, 0x59
);
DEFINE_HEX_XDR_NETOBJ(nfold_test5_expected_result,
		      0xdb, 0x3b, 0x0d, 0x8f, 0x0b, 0x06, 0x1e, 0x60,
		      0x32, 0x82, 0xb3, 0x08, 0xa5, 0x08, 0x41, 0x22,
		      0x9a, 0xd7, 0x98, 0xfa, 0xb9, 0x54, 0x0c, 0x1b
);

DEFINE_HEX_XDR_NETOBJ(nfold_test6_plaintext,
		      0x51
);
DEFINE_HEX_XDR_NETOBJ(nfold_test6_expected_result,
		      0x51, 0x8a, 0x54, 0xa2, 0x15, 0xa8, 0x45, 0x2a,
		      0x51, 0x8a, 0x54, 0xa2, 0x15, 0xa8, 0x45, 0x2a,
		      0x51, 0x8a, 0x54, 0xa2, 0x15
);

DEFINE_HEX_XDR_NETOBJ(nfold_test7_plaintext,
		      0x62, 0x61
);
DEFINE_HEX_XDR_NETOBJ(nfold_test7_expected_result,
		      0xfb, 0x25, 0xd5, 0x31, 0xae, 0x89, 0x74, 0x49,
		      0x9f, 0x52, 0xfd, 0x92, 0xea, 0x98, 0x57, 0xc4,
		      0xba, 0x24, 0xcf, 0x29, 0x7e
);

DEFINE_HEX_XDR_NETOBJ(nfold_test_kerberos,
		      0x6b, 0x65, 0x72, 0x62, 0x65, 0x72, 0x6f, 0x73
);
DEFINE_HEX_XDR_NETOBJ(nfold_test8_expected_result,
		      0x6b, 0x65, 0x72, 0x62, 0x65, 0x72, 0x6f, 0x73
);
DEFINE_HEX_XDR_NETOBJ(nfold_test9_expected_result,
		      0x6b, 0x65, 0x72, 0x62, 0x65, 0x72, 0x6f, 0x73,
		      0x7b, 0x9b, 0x5b, 0x2b, 0x93, 0x13, 0x2b, 0x93
);
DEFINE_HEX_XDR_NETOBJ(nfold_test10_expected_result,
		      0x83, 0x72, 0xc2, 0x36, 0x34, 0x4e, 0x5f, 0x15,
		      0x50, 0xcd, 0x07, 0x47, 0xe1, 0x5d, 0x62, 0xca,
		      0x7a, 0x5a, 0x3b, 0xce, 0xa4
);
DEFINE_HEX_XDR_NETOBJ(nfold_test11_expected_result,
		      0x6b, 0x65, 0x72, 0x62, 0x65, 0x72, 0x6f, 0x73,
		      0x7b, 0x9b, 0x5b, 0x2b, 0x93, 0x13, 0x2b, 0x93,
		      0x5c, 0x9b, 0xdc, 0xda, 0xd9, 0x5c, 0x98, 0x99,
		      0xc4, 0xca, 0xe4, 0xde, 0xe6, 0xd6, 0xca, 0xe4
);

static const struct gss_krb5_test_param rfc3961_nfold_test_params[] = {
	{
		.desc			= "64-fold(\"012345\")",
		.nfold			= 64,
		.plaintext		= &nfold_test1_plaintext,
		.expected_result	= &nfold_test1_expected_result,
	},
	{
		.desc			= "56-fold(\"password\")",
		.nfold			= 56,
		.plaintext		= &nfold_test2_plaintext,
		.expected_result	= &nfold_test2_expected_result,
	},
	{
		.desc			= "64-fold(\"Rough Consensus, and Running Code\")",
		.nfold			= 64,
		.plaintext		= &nfold_test3_plaintext,
		.expected_result	= &nfold_test3_expected_result,
	},
	{
		.desc			= "168-fold(\"password\")",
		.nfold			= 168,
		.plaintext		= &nfold_test4_plaintext,
		.expected_result	= &nfold_test4_expected_result,
	},
	{
		.desc			= "192-fold(\"MASSACHVSETTS INSTITVTE OF TECHNOLOGY\")",
		.nfold			= 192,
		.plaintext		= &nfold_test5_plaintext,
		.expected_result	= &nfold_test5_expected_result,
	},
	{
		.desc			= "168-fold(\"Q\")",
		.nfold			= 168,
		.plaintext		= &nfold_test6_plaintext,
		.expected_result	= &nfold_test6_expected_result,
	},
	{
		.desc			= "168-fold(\"ba\")",
		.nfold			= 168,
		.plaintext		= &nfold_test7_plaintext,
		.expected_result	= &nfold_test7_expected_result,
	},
	{
		.desc			= "64-fold(\"kerberos\")",
		.nfold			= 64,
		.plaintext		= &nfold_test_kerberos,
		.expected_result	= &nfold_test8_expected_result,
	},
	{
		.desc			= "128-fold(\"kerberos\")",
		.nfold			= 128,
		.plaintext		= &nfold_test_kerberos,
		.expected_result	= &nfold_test9_expected_result,
	},
	{
		.desc			= "168-fold(\"kerberos\")",
		.nfold			= 168,
		.plaintext		= &nfold_test_kerberos,
		.expected_result	= &nfold_test10_expected_result,
	},
	{
		.desc			= "256-fold(\"kerberos\")",
		.nfold			= 256,
		.plaintext		= &nfold_test_kerberos,
		.expected_result	= &nfold_test11_expected_result,
	},
};

/* Creates the function rfc3961_nfold_gen_params */
KUNIT_ARRAY_PARAM(rfc3961_nfold, rfc3961_nfold_test_params, gss_krb5_get_desc);

static void rfc3961_nfold_case(struct kunit *test)
{
	const struct gss_krb5_test_param *param = test->param_value;
	u8 *result;

	/* Arrange */
	result = kunit_kzalloc(test, 4096, GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, result);

	/* Act */
	krb5_nfold(param->plaintext->len * 8, param->plaintext->data,
		   param->expected_result->len * 8, result);

	/* Assert */
	KUNIT_EXPECT_EQ_MSG(test,
			    memcmp(param->expected_result->data,
				   result, param->expected_result->len), 0,
			    "result mismatch");
}

/*
 * RFC 3961 Appendix A.3.  DES3 DR and DK
 *
 * These tests show the derived-random and derived-key values for the
 * des3-hmac-sha1-kd encryption scheme, using the DR and DK functions
 * defined in section 6.3.1.  The input keys were randomly generated;
 * the usage values are from this specification.
 *
 * This test material is copyright (C) The Internet Society (2005).
 */

DEFINE_HEX_XDR_NETOBJ(des3_dk_usage_155,
		      0x00, 0x00, 0x00, 0x01, 0x55
);

DEFINE_HEX_XDR_NETOBJ(des3_dk_usage_1aa,
		      0x00, 0x00, 0x00, 0x01, 0xaa
);

DEFINE_HEX_XDR_NETOBJ(des3_dk_usage_kerberos,
		      0x6b, 0x65, 0x72, 0x62, 0x65, 0x72, 0x6f, 0x73
);

DEFINE_HEX_XDR_NETOBJ(des3_dk_test1_base_key,
		      0xdc, 0xe0, 0x6b, 0x1f, 0x64, 0xc8, 0x57, 0xa1,
		      0x1c, 0x3d, 0xb5, 0x7c, 0x51, 0x89, 0x9b, 0x2c,
		      0xc1, 0x79, 0x10, 0x08, 0xce, 0x97, 0x3b, 0x92
);
DEFINE_HEX_XDR_NETOBJ(des3_dk_test1_derived_key,
		      0x92, 0x51, 0x79, 0xd0, 0x45, 0x91, 0xa7, 0x9b,
		      0x5d, 0x31, 0x92, 0xc4, 0xa7, 0xe9, 0xc2, 0x89,
		      0xb0, 0x49, 0xc7, 0x1f, 0x6e, 0xe6, 0x04, 0xcd
);

DEFINE_HEX_XDR_NETOBJ(des3_dk_test2_base_key,
		      0x5e, 0x13, 0xd3, 0x1c, 0x70, 0xef, 0x76, 0x57,
		      0x46, 0x57, 0x85, 0x31, 0xcb, 0x51, 0xc1, 0x5b,
		      0xf1, 0x1c, 0xa8, 0x2c, 0x97, 0xce, 0xe9, 0xf2
);
DEFINE_HEX_XDR_NETOBJ(des3_dk_test2_derived_key,
		      0x9e, 0x58, 0xe5, 0xa1, 0x46, 0xd9, 0x94, 0x2a,
		      0x10, 0x1c, 0x46, 0x98, 0x45, 0xd6, 0x7a, 0x20,
		      0xe3, 0xc4, 0x25, 0x9e, 0xd9, 0x13, 0xf2, 0x07
);

DEFINE_HEX_XDR_NETOBJ(des3_dk_test3_base_key,
		      0x98, 0xe6, 0xfd, 0x8a, 0x04, 0xa4, 0xb6, 0x85,
		      0x9b, 0x75, 0xa1, 0x76, 0x54, 0x0b, 0x97, 0x52,
		      0xba, 0xd3, 0xec, 0xd6, 0x10, 0xa2, 0x52, 0xbc
);
DEFINE_HEX_XDR_NETOBJ(des3_dk_test3_derived_key,
		      0x13, 0xfe, 0xf8, 0x0d, 0x76, 0x3e, 0x94, 0xec,
		      0x6d, 0x13, 0xfd, 0x2c, 0xa1, 0xd0, 0x85, 0x07,
		      0x02, 0x49, 0xda, 0xd3, 0x98, 0x08, 0xea, 0xbf
);

DEFINE_HEX_XDR_NETOBJ(des3_dk_test4_base_key,
		      0x62, 0x2a, 0xec, 0x25, 0xa2, 0xfe, 0x2c, 0xad,
		      0x70, 0x94, 0x68, 0x0b, 0x7c, 0x64, 0x94, 0x02,
		      0x80, 0x08, 0x4c, 0x1a, 0x7c, 0xec, 0x92, 0xb5
);
DEFINE_HEX_XDR_NETOBJ(des3_dk_test4_derived_key,
		      0xf8, 0xdf, 0xbf, 0x04, 0xb0, 0x97, 0xe6, 0xd9,
		      0xdc, 0x07, 0x02, 0x68, 0x6b, 0xcb, 0x34, 0x89,
		      0xd9, 0x1f, 0xd9, 0xa4, 0x51, 0x6b, 0x70, 0x3e
);

DEFINE_HEX_XDR_NETOBJ(des3_dk_test5_base_key,
		      0xd3, 0xf8, 0x29, 0x8c, 0xcb, 0x16, 0x64, 0x38,
		      0xdc, 0xb9, 0xb9, 0x3e, 0xe5, 0xa7, 0x62, 0x92,
		      0x86, 0xa4, 0x91, 0xf8, 0x38, 0xf8, 0x02, 0xfb
);
DEFINE_HEX_XDR_NETOBJ(des3_dk_test5_derived_key,
		      0x23, 0x70, 0xda, 0x57, 0x5d, 0x2a, 0x3d, 0xa8,
		      0x64, 0xce, 0xbf, 0xdc, 0x52, 0x04, 0xd5, 0x6d,
		      0xf7, 0x79, 0xa7, 0xdf, 0x43, 0xd9, 0xda, 0x43
);

DEFINE_HEX_XDR_NETOBJ(des3_dk_test6_base_key,
		      0xc1, 0x08, 0x16, 0x49, 0xad, 0xa7, 0x43, 0x62,
		      0xe6, 0xa1, 0x45, 0x9d, 0x01, 0xdf, 0xd3, 0x0d,
		      0x67, 0xc2, 0x23, 0x4c, 0x94, 0x07, 0x04, 0xda
);
DEFINE_HEX_XDR_NETOBJ(des3_dk_test6_derived_key,
		      0x34, 0x80, 0x57, 0xec, 0x98, 0xfd, 0xc4, 0x80,
		      0x16, 0x16, 0x1c, 0x2a, 0x4c, 0x7a, 0x94, 0x3e,
		      0x92, 0xae, 0x49, 0x2c, 0x98, 0x91, 0x75, 0xf7
);

DEFINE_HEX_XDR_NETOBJ(des3_dk_test7_base_key,
		      0x5d, 0x15, 0x4a, 0xf2, 0x38, 0xf4, 0x67, 0x13,
		      0x15, 0x57, 0x19, 0xd5, 0x5e, 0x2f, 0x1f, 0x79,
		      0x0d, 0xd6, 0x61, 0xf2, 0x79, 0xa7, 0x91, 0x7c
);
DEFINE_HEX_XDR_NETOBJ(des3_dk_test7_derived_key,
		      0xa8, 0x80, 0x8a, 0xc2, 0x67, 0xda, 0xda, 0x3d,
		      0xcb, 0xe9, 0xa7, 0xc8, 0x46, 0x26, 0xfb, 0xc7,
		      0x61, 0xc2, 0x94, 0xb0, 0x13, 0x15, 0xe5, 0xc1
);

DEFINE_HEX_XDR_NETOBJ(des3_dk_test8_base_key,
		      0x79, 0x85, 0x62, 0xe0, 0x49, 0x85, 0x2f, 0x57,
		      0xdc, 0x8c, 0x34, 0x3b, 0xa1, 0x7f, 0x2c, 0xa1,
		      0xd9, 0x73, 0x94, 0xef, 0xc8, 0xad, 0xc4, 0x43
);
DEFINE_HEX_XDR_NETOBJ(des3_dk_test8_derived_key,
		      0xc8, 0x13, 0xf8, 0x8a, 0x3b, 0xe3, 0xb3, 0x34,
		      0xf7, 0x54, 0x25, 0xce, 0x91, 0x75, 0xfb, 0xe3,
		      0xc8, 0x49, 0x3b, 0x89, 0xc8, 0x70, 0x3b, 0x49
);

DEFINE_HEX_XDR_NETOBJ(des3_dk_test9_base_key,
		      0x26, 0xdc, 0xe3, 0x34, 0xb5, 0x45, 0x29, 0x2f,
		      0x2f, 0xea, 0xb9, 0xa8, 0x70, 0x1a, 0x89, 0xa4,
		      0xb9, 0x9e, 0xb9, 0x94, 0x2c, 0xec, 0xd0, 0x16
);
DEFINE_HEX_XDR_NETOBJ(des3_dk_test9_derived_key,
		      0xf4, 0x8f, 0xfd, 0x6e, 0x83, 0xf8, 0x3e, 0x73,
		      0x54, 0xe6, 0x94, 0xfd, 0x25, 0x2c, 0xf8, 0x3b,
		      0xfe, 0x58, 0xf7, 0xd5, 0xba, 0x37, 0xec, 0x5d
);

static const struct gss_krb5_test_param rfc3961_kdf_test_params[] = {
	{
		.desc			= "des3-hmac-sha1 key derivation case 1",
		.enctype		= ENCTYPE_DES3_CBC_RAW,
		.base_key		= &des3_dk_test1_base_key,
		.usage			= &des3_dk_usage_155,
		.expected_result	= &des3_dk_test1_derived_key,
	},
	{
		.desc			= "des3-hmac-sha1 key derivation case 2",
		.enctype		= ENCTYPE_DES3_CBC_RAW,
		.base_key		= &des3_dk_test2_base_key,
		.usage			= &des3_dk_usage_1aa,
		.expected_result	= &des3_dk_test2_derived_key,
	},
	{
		.desc			= "des3-hmac-sha1 key derivation case 3",
		.enctype		= ENCTYPE_DES3_CBC_RAW,
		.base_key		= &des3_dk_test3_base_key,
		.usage			= &des3_dk_usage_155,
		.expected_result	= &des3_dk_test3_derived_key,
	},
	{
		.desc			= "des3-hmac-sha1 key derivation case 4",
		.enctype		= ENCTYPE_DES3_CBC_RAW,
		.base_key		= &des3_dk_test4_base_key,
		.usage			= &des3_dk_usage_1aa,
		.expected_result	= &des3_dk_test4_derived_key,
	},
	{
		.desc			= "des3-hmac-sha1 key derivation case 5",
		.enctype		= ENCTYPE_DES3_CBC_RAW,
		.base_key		= &des3_dk_test5_base_key,
		.usage			= &des3_dk_usage_kerberos,
		.expected_result	= &des3_dk_test5_derived_key,
	},
	{
		.desc			= "des3-hmac-sha1 key derivation case 6",
		.enctype		= ENCTYPE_DES3_CBC_RAW,
		.base_key		= &des3_dk_test6_base_key,
		.usage			= &des3_dk_usage_155,
		.expected_result	= &des3_dk_test6_derived_key,
	},
	{
		.desc			= "des3-hmac-sha1 key derivation case 7",
		.enctype		= ENCTYPE_DES3_CBC_RAW,
		.base_key		= &des3_dk_test7_base_key,
		.usage			= &des3_dk_usage_1aa,
		.expected_result	= &des3_dk_test7_derived_key,
	},
	{
		.desc			= "des3-hmac-sha1 key derivation case 8",
		.enctype		= ENCTYPE_DES3_CBC_RAW,
		.base_key		= &des3_dk_test8_base_key,
		.usage			= &des3_dk_usage_155,
		.expected_result	= &des3_dk_test8_derived_key,
	},
	{
		.desc			= "des3-hmac-sha1 key derivation case 9",
		.enctype		= ENCTYPE_DES3_CBC_RAW,
		.base_key		= &des3_dk_test9_base_key,
		.usage			= &des3_dk_usage_1aa,
		.expected_result	= &des3_dk_test9_derived_key,
	},
};

/* Creates the function rfc3961_kdf_gen_params */
KUNIT_ARRAY_PARAM(rfc3961_kdf, rfc3961_kdf_test_params, gss_krb5_get_desc);

static struct kunit_case rfc3961_test_cases[] = {
	{
		.name			= "RFC 3961 n-fold",
		.run_case		= rfc3961_nfold_case,
		.generate_params	= rfc3961_nfold_gen_params,
	},
	{
		.name			= "RFC 3961 key derivation",
		.run_case		= kdf_case,
		.generate_params	= rfc3961_kdf_gen_params,
	},
	{}
};

static struct kunit_suite rfc3961_suite = {
	.name			= "RFC 3961 tests",
	.test_cases		= rfc3961_test_cases,
};

/*
 * From RFC 3962 Appendix B:   Sample Test Vectors
 *
 * Some test vectors for CBC with ciphertext stealing, using an
 * initial vector of all-zero.
 *
 * This test material is copyright (C) The Internet Society (2005).
 */

DEFINE_HEX_XDR_NETOBJ(rfc3962_encryption_key,
		      0x63, 0x68, 0x69, 0x63, 0x6b, 0x65, 0x6e, 0x20,
		      0x74, 0x65, 0x72, 0x69, 0x79, 0x61, 0x6b, 0x69
);

DEFINE_HEX_XDR_NETOBJ(rfc3962_enc_test1_plaintext,
		      0x49, 0x20, 0x77, 0x6f, 0x75, 0x6c, 0x64, 0x20,
		      0x6c, 0x69, 0x6b, 0x65, 0x20, 0x74, 0x68, 0x65,
		      0x20
);
DEFINE_HEX_XDR_NETOBJ(rfc3962_enc_test1_expected_result,
		      0xc6, 0x35, 0x35, 0x68, 0xf2, 0xbf, 0x8c, 0xb4,
		      0xd8, 0xa5, 0x80, 0x36, 0x2d, 0xa7, 0xff, 0x7f,
		      0x97
);
DEFINE_HEX_XDR_NETOBJ(rfc3962_enc_test1_next_iv,
		      0xc6, 0x35, 0x35, 0x68, 0xf2, 0xbf, 0x8c, 0xb4,
		      0xd8, 0xa5, 0x80, 0x36, 0x2d, 0xa7, 0xff, 0x7f
);

DEFINE_HEX_XDR_NETOBJ(rfc3962_enc_test2_plaintext,
		      0x49, 0x20, 0x77, 0x6f, 0x75, 0x6c, 0x64, 0x20,
		      0x6c, 0x69, 0x6b, 0x65, 0x20, 0x74, 0x68, 0x65,
		      0x20, 0x47, 0x65, 0x6e, 0x65, 0x72, 0x61, 0x6c,
		      0x20, 0x47, 0x61, 0x75, 0x27, 0x73, 0x20
);
DEFINE_HEX_XDR_NETOBJ(rfc3962_enc_test2_expected_result,
		      0xfc, 0x00, 0x78, 0x3e, 0x0e, 0xfd, 0xb2, 0xc1,
		      0xd4, 0x45, 0xd4, 0xc8, 0xef, 0xf7, 0xed, 0x22,
		      0x97, 0x68, 0x72, 0x68, 0xd6, 0xec, 0xcc, 0xc0,
		      0xc0, 0x7b, 0x25, 0xe2, 0x5e, 0xcf, 0xe5
);
DEFINE_HEX_XDR_NETOBJ(rfc3962_enc_test2_next_iv,
		      0xfc, 0x00, 0x78, 0x3e, 0x0e, 0xfd, 0xb2, 0xc1,
		      0xd4, 0x45, 0xd4, 0xc8, 0xef, 0xf7, 0xed, 0x22
);

DEFINE_HEX_XDR_NETOBJ(rfc3962_enc_test3_plaintext,
		      0x49, 0x20, 0x77, 0x6f, 0x75, 0x6c, 0x64, 0x20,
		      0x6c, 0x69, 0x6b, 0x65, 0x20, 0x74, 0x68, 0x65,
		      0x20, 0x47, 0x65, 0x6e, 0x65, 0x72, 0x61, 0x6c,
		      0x20, 0x47, 0x61, 0x75, 0x27, 0x73, 0x20, 0x43
);
DEFINE_HEX_XDR_NETOBJ(rfc3962_enc_test3_expected_result,
		      0x39, 0x31, 0x25, 0x23, 0xa7, 0x86, 0x62, 0xd5,
		      0xbe, 0x7f, 0xcb, 0xcc, 0x98, 0xeb, 0xf5, 0xa8,
		      0x97, 0x68, 0x72, 0x68, 0xd6, 0xec, 0xcc, 0xc0,
		      0xc0, 0x7b, 0x25, 0xe2, 0x5e, 0xcf, 0xe5, 0x84
);
DEFINE_HEX_XDR_NETOBJ(rfc3962_enc_test3_next_iv,
		      0x39, 0x31, 0x25, 0x23, 0xa7, 0x86, 0x62, 0xd5,
		      0xbe, 0x7f, 0xcb, 0xcc, 0x98, 0xeb, 0xf5, 0xa8
);

DEFINE_HEX_XDR_NETOBJ(rfc3962_enc_test4_plaintext,
		      0x49, 0x20, 0x77, 0x6f, 0x75, 0x6c, 0x64, 0x20,
		      0x6c, 0x69, 0x6b, 0x65, 0x20, 0x74, 0x68, 0x65,
		      0x20, 0x47, 0x65, 0x6e, 0x65, 0x72, 0x61, 0x6c,
		      0x20, 0x47, 0x61, 0x75, 0x27, 0x73, 0x20, 0x43,
		      0x68, 0x69, 0x63, 0x6b, 0x65, 0x6e, 0x2c, 0x20,
		      0x70, 0x6c, 0x65, 0x61, 0x73, 0x65, 0x2c
);
DEFINE_HEX_XDR_NETOBJ(rfc3962_enc_test4_expected_result,
		      0x97, 0x68, 0x72, 0x68, 0xd6, 0xec, 0xcc, 0xc0,
		      0xc0, 0x7b, 0x25, 0xe2, 0x5e, 0xcf, 0xe5, 0x84,
		      0xb3, 0xff, 0xfd, 0x94, 0x0c, 0x16, 0xa1, 0x8c,
		      0x1b, 0x55, 0x49, 0xd2, 0xf8, 0x38, 0x02, 0x9e,
		      0x39, 0x31, 0x25, 0x23, 0xa7, 0x86, 0x62, 0xd5,
		      0xbe, 0x7f, 0xcb, 0xcc, 0x98, 0xeb, 0xf5
);
DEFINE_HEX_XDR_NETOBJ(rfc3962_enc_test4_next_iv,
		      0xb3, 0xff, 0xfd, 0x94, 0x0c, 0x16, 0xa1, 0x8c,
		      0x1b, 0x55, 0x49, 0xd2, 0xf8, 0x38, 0x02, 0x9e
);

DEFINE_HEX_XDR_NETOBJ(rfc3962_enc_test5_plaintext,
		      0x49, 0x20, 0x77, 0x6f, 0x75, 0x6c, 0x64, 0x20,
		      0x6c, 0x69, 0x6b, 0x65, 0x20, 0x74, 0x68, 0x65,
		      0x20, 0x47, 0x65, 0x6e, 0x65, 0x72, 0x61, 0x6c,
		      0x20, 0x47, 0x61, 0x75, 0x27, 0x73, 0x20, 0x43,
		      0x68, 0x69, 0x63, 0x6b, 0x65, 0x6e, 0x2c, 0x20,
		      0x70, 0x6c, 0x65, 0x61, 0x73, 0x65, 0x2c, 0x20
);
DEFINE_HEX_XDR_NETOBJ(rfc3962_enc_test5_expected_result,
		      0x97, 0x68, 0x72, 0x68, 0xd6, 0xec, 0xcc, 0xc0,
		      0xc0, 0x7b, 0x25, 0xe2, 0x5e, 0xcf, 0xe5, 0x84,
		      0x9d, 0xad, 0x8b, 0xbb, 0x96, 0xc4, 0xcd, 0xc0,
		      0x3b, 0xc1, 0x03, 0xe1, 0xa1, 0x94, 0xbb, 0xd8,
		      0x39, 0x31, 0x25, 0x23, 0xa7, 0x86, 0x62, 0xd5,
		      0xbe, 0x7f, 0xcb, 0xcc, 0x98, 0xeb, 0xf5, 0xa8
);
DEFINE_HEX_XDR_NETOBJ(rfc3962_enc_test5_next_iv,
		      0x9d, 0xad, 0x8b, 0xbb, 0x96, 0xc4, 0xcd, 0xc0,
		      0x3b, 0xc1, 0x03, 0xe1, 0xa1, 0x94, 0xbb, 0xd8
);

DEFINE_HEX_XDR_NETOBJ(rfc3962_enc_test6_plaintext,
		      0x49, 0x20, 0x77, 0x6f, 0x75, 0x6c, 0x64, 0x20,
		      0x6c, 0x69, 0x6b, 0x65, 0x20, 0x74, 0x68, 0x65,
		      0x20, 0x47, 0x65, 0x6e, 0x65, 0x72, 0x61, 0x6c,
		      0x20, 0x47, 0x61, 0x75, 0x27, 0x73, 0x20, 0x43,
		      0x68, 0x69, 0x63, 0x6b, 0x65, 0x6e, 0x2c, 0x20,
		      0x70, 0x6c, 0x65, 0x61, 0x73, 0x65, 0x2c, 0x20,
		      0x61, 0x6e, 0x64, 0x20, 0x77, 0x6f, 0x6e, 0x74,
		      0x6f, 0x6e, 0x20, 0x73, 0x6f, 0x75, 0x70, 0x2e
);
DEFINE_HEX_XDR_NETOBJ(rfc3962_enc_test6_expected_result,
		      0x97, 0x68, 0x72, 0x68, 0xd6, 0xec, 0xcc, 0xc0,
		      0xc0, 0x7b, 0x25, 0xe2, 0x5e, 0xcf, 0xe5, 0x84,
		      0x39, 0x31, 0x25, 0x23, 0xa7, 0x86, 0x62, 0xd5,
		      0xbe, 0x7f, 0xcb, 0xcc, 0x98, 0xeb, 0xf5, 0xa8,
		      0x48, 0x07, 0xef, 0xe8, 0x36, 0xee, 0x89, 0xa5,
		      0x26, 0x73, 0x0d, 0xbc, 0x2f, 0x7b, 0xc8, 0x40,
		      0x9d, 0xad, 0x8b, 0xbb, 0x96, 0xc4, 0xcd, 0xc0,
		      0x3b, 0xc1, 0x03, 0xe1, 0xa1, 0x94, 0xbb, 0xd8
);
DEFINE_HEX_XDR_NETOBJ(rfc3962_enc_test6_next_iv,
		      0x48, 0x07, 0xef, 0xe8, 0x36, 0xee, 0x89, 0xa5,
		      0x26, 0x73, 0x0d, 0xbc, 0x2f, 0x7b, 0xc8, 0x40
);

static const struct gss_krb5_test_param rfc3962_encrypt_test_params[] = {
	{
		.desc			= "Encrypt with aes128-cts-hmac-sha1-96 case 1",
		.enctype		= ENCTYPE_AES128_CTS_HMAC_SHA1_96,
		.Ke			= &rfc3962_encryption_key,
		.plaintext		= &rfc3962_enc_test1_plaintext,
		.expected_result	= &rfc3962_enc_test1_expected_result,
		.next_iv		= &rfc3962_enc_test1_next_iv,
	},
	{
		.desc			= "Encrypt with aes128-cts-hmac-sha1-96 case 2",
		.enctype		= ENCTYPE_AES128_CTS_HMAC_SHA1_96,
		.Ke			= &rfc3962_encryption_key,
		.plaintext		= &rfc3962_enc_test2_plaintext,
		.expected_result	= &rfc3962_enc_test2_expected_result,
		.next_iv		= &rfc3962_enc_test2_next_iv,
	},
	{
		.desc			= "Encrypt with aes128-cts-hmac-sha1-96 case 3",
		.enctype		= ENCTYPE_AES128_CTS_HMAC_SHA1_96,
		.Ke			= &rfc3962_encryption_key,
		.plaintext		= &rfc3962_enc_test3_plaintext,
		.expected_result	= &rfc3962_enc_test3_expected_result,
		.next_iv		= &rfc3962_enc_test3_next_iv,
	},
	{
		.desc			= "Encrypt with aes128-cts-hmac-sha1-96 case 4",
		.enctype		= ENCTYPE_AES128_CTS_HMAC_SHA1_96,
		.Ke			= &rfc3962_encryption_key,
		.plaintext		= &rfc3962_enc_test4_plaintext,
		.expected_result	= &rfc3962_enc_test4_expected_result,
		.next_iv		= &rfc3962_enc_test4_next_iv,
	},
	{
		.desc			= "Encrypt with aes128-cts-hmac-sha1-96 case 5",
		.enctype		= ENCTYPE_AES128_CTS_HMAC_SHA1_96,
		.Ke			= &rfc3962_encryption_key,
		.plaintext		= &rfc3962_enc_test5_plaintext,
		.expected_result	= &rfc3962_enc_test5_expected_result,
		.next_iv		= &rfc3962_enc_test5_next_iv,
	},
	{
		.desc			= "Encrypt with aes128-cts-hmac-sha1-96 case 6",
		.enctype		= ENCTYPE_AES128_CTS_HMAC_SHA1_96,
		.Ke			= &rfc3962_encryption_key,
		.plaintext		= &rfc3962_enc_test6_plaintext,
		.expected_result	= &rfc3962_enc_test6_expected_result,
		.next_iv		= &rfc3962_enc_test6_next_iv,
	},
};

/* Creates the function rfc3962_encrypt_gen_params */
KUNIT_ARRAY_PARAM(rfc3962_encrypt, rfc3962_encrypt_test_params,
		  gss_krb5_get_desc);

/*
 * This tests the implementation of the encryption part of the mechanism.
 * It does not apply a confounder or test the result of HMAC over the
 * plaintext.
 */
static void rfc3962_encrypt_case(struct kunit *test)
{
	const struct gss_krb5_test_param *param = test->param_value;
	struct crypto_sync_skcipher *cts_tfm, *cbc_tfm;
	const struct gss_krb5_enctype *gk5e;
	struct xdr_buf buf;
	void *iv, *text;
	u32 err;

	/* Arrange */
	gk5e = gss_krb5_lookup_enctype(param->enctype);
	if (!gk5e)
		kunit_skip(test, "Encryption type is not available");

	cbc_tfm = crypto_alloc_sync_skcipher(gk5e->aux_cipher, 0, 0);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, cbc_tfm);
	err = crypto_sync_skcipher_setkey(cbc_tfm, param->Ke->data, param->Ke->len);
	KUNIT_ASSERT_EQ(test, err, 0);

	cts_tfm = crypto_alloc_sync_skcipher(gk5e->encrypt_name, 0, 0);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, cts_tfm);
	err = crypto_sync_skcipher_setkey(cts_tfm, param->Ke->data, param->Ke->len);
	KUNIT_ASSERT_EQ(test, err, 0);

	iv = kunit_kzalloc(test, crypto_sync_skcipher_ivsize(cts_tfm), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, iv);

	text = kunit_kzalloc(test, param->plaintext->len, GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, text);

	memcpy(text, param->plaintext->data, param->plaintext->len);
	memset(&buf, 0, sizeof(buf));
	buf.head[0].iov_base = text;
	buf.head[0].iov_len = param->plaintext->len;
	buf.len = buf.head[0].iov_len;

	/* Act */
	err = krb5_cbc_cts_encrypt(cts_tfm, cbc_tfm, 0, &buf, NULL,
				   iv, crypto_sync_skcipher_ivsize(cts_tfm));
	KUNIT_ASSERT_EQ(test, err, 0);

	/* Assert */
	KUNIT_EXPECT_EQ_MSG(test,
			    param->expected_result->len, buf.len,
			    "ciphertext length mismatch");
	KUNIT_EXPECT_EQ_MSG(test,
			    memcmp(param->expected_result->data,
				   text, param->expected_result->len), 0,
			    "ciphertext mismatch");
	KUNIT_EXPECT_EQ_MSG(test,
			    memcmp(param->next_iv->data, iv,
				   param->next_iv->len), 0,
			    "IV mismatch");

	crypto_free_sync_skcipher(cts_tfm);
	crypto_free_sync_skcipher(cbc_tfm);
}

static struct kunit_case rfc3962_test_cases[] = {
	{
		.name			= "RFC 3962 encryption",
		.run_case		= rfc3962_encrypt_case,
		.generate_params	= rfc3962_encrypt_gen_params,
	},
	{}
};

static struct kunit_suite rfc3962_suite = {
	.name			= "RFC 3962 suite",
	.test_cases		= rfc3962_test_cases,
};

/*
 * From RFC 6803 Section 10.  Test vectors
 *
 * Sample results for key derivation
 *
 * Copyright (c) 2012 IETF Trust and the persons identified as the
 * document authors.  All rights reserved.
 */

DEFINE_HEX_XDR_NETOBJ(camellia128_cts_cmac_basekey,
		      0x57, 0xd0, 0x29, 0x72, 0x98, 0xff, 0xd9, 0xd3,
		      0x5d, 0xe5, 0xa4, 0x7f, 0xb4, 0xbd, 0xe2, 0x4b
);
DEFINE_HEX_XDR_NETOBJ(camellia128_cts_cmac_Kc,
		      0xd1, 0x55, 0x77, 0x5a, 0x20, 0x9d, 0x05, 0xf0,
		      0x2b, 0x38, 0xd4, 0x2a, 0x38, 0x9e, 0x5a, 0x56
);
DEFINE_HEX_XDR_NETOBJ(camellia128_cts_cmac_Ke,
		      0x64, 0xdf, 0x83, 0xf8, 0x5a, 0x53, 0x2f, 0x17,
		      0x57, 0x7d, 0x8c, 0x37, 0x03, 0x57, 0x96, 0xab
);
DEFINE_HEX_XDR_NETOBJ(camellia128_cts_cmac_Ki,
		      0x3e, 0x4f, 0xbd, 0xf3, 0x0f, 0xb8, 0x25, 0x9c,
		      0x42, 0x5c, 0xb6, 0xc9, 0x6f, 0x1f, 0x46, 0x35
);

DEFINE_HEX_XDR_NETOBJ(camellia256_cts_cmac_basekey,
		      0xb9, 0xd6, 0x82, 0x8b, 0x20, 0x56, 0xb7, 0xbe,
		      0x65, 0x6d, 0x88, 0xa1, 0x23, 0xb1, 0xfa, 0xc6,
		      0x82, 0x14, 0xac, 0x2b, 0x72, 0x7e, 0xcf, 0x5f,
		      0x69, 0xaf, 0xe0, 0xc4, 0xdf, 0x2a, 0x6d, 0x2c
);
DEFINE_HEX_XDR_NETOBJ(camellia256_cts_cmac_Kc,
		      0xe4, 0x67, 0xf9, 0xa9, 0x55, 0x2b, 0xc7, 0xd3,
		      0x15, 0x5a, 0x62, 0x20, 0xaf, 0x9c, 0x19, 0x22,
		      0x0e, 0xee, 0xd4, 0xff, 0x78, 0xb0, 0xd1, 0xe6,
		      0xa1, 0x54, 0x49, 0x91, 0x46, 0x1a, 0x9e, 0x50
);
DEFINE_HEX_XDR_NETOBJ(camellia256_cts_cmac_Ke,
		      0x41, 0x2a, 0xef, 0xc3, 0x62, 0xa7, 0x28, 0x5f,
		      0xc3, 0x96, 0x6c, 0x6a, 0x51, 0x81, 0xe7, 0x60,
		      0x5a, 0xe6, 0x75, 0x23, 0x5b, 0x6d, 0x54, 0x9f,
		      0xbf, 0xc9, 0xab, 0x66, 0x30, 0xa4, 0xc6, 0x04
);
DEFINE_HEX_XDR_NETOBJ(camellia256_cts_cmac_Ki,
		      0xfa, 0x62, 0x4f, 0xa0, 0xe5, 0x23, 0x99, 0x3f,
		      0xa3, 0x88, 0xae, 0xfd, 0xc6, 0x7e, 0x67, 0xeb,
		      0xcd, 0x8c, 0x08, 0xe8, 0xa0, 0x24, 0x6b, 0x1d,
		      0x73, 0xb0, 0xd1, 0xdd, 0x9f, 0xc5, 0x82, 0xb0
);

DEFINE_HEX_XDR_NETOBJ(usage_checksum,
		      0x00, 0x00, 0x00, 0x02, KEY_USAGE_SEED_CHECKSUM
);
DEFINE_HEX_XDR_NETOBJ(usage_encryption,
		      0x00, 0x00, 0x00, 0x02, KEY_USAGE_SEED_ENCRYPTION
);
DEFINE_HEX_XDR_NETOBJ(usage_integrity,
		      0x00, 0x00, 0x00, 0x02, KEY_USAGE_SEED_INTEGRITY
);

static const struct gss_krb5_test_param rfc6803_kdf_test_params[] = {
	{
		.desc			= "Derive Kc subkey for camellia128-cts-cmac",
		.enctype		= ENCTYPE_CAMELLIA128_CTS_CMAC,
		.base_key		= &camellia128_cts_cmac_basekey,
		.usage			= &usage_checksum,
		.expected_result	= &camellia128_cts_cmac_Kc,
	},
	{
		.desc			= "Derive Ke subkey for camellia128-cts-cmac",
		.enctype		= ENCTYPE_CAMELLIA128_CTS_CMAC,
		.base_key		= &camellia128_cts_cmac_basekey,
		.usage			= &usage_encryption,
		.expected_result	= &camellia128_cts_cmac_Ke,
	},
	{
		.desc			= "Derive Ki subkey for camellia128-cts-cmac",
		.enctype		= ENCTYPE_CAMELLIA128_CTS_CMAC,
		.base_key		= &camellia128_cts_cmac_basekey,
		.usage			= &usage_integrity,
		.expected_result	= &camellia128_cts_cmac_Ki,
	},
	{
		.desc			= "Derive Kc subkey for camellia256-cts-cmac",
		.enctype		= ENCTYPE_CAMELLIA256_CTS_CMAC,
		.base_key		= &camellia256_cts_cmac_basekey,
		.usage			= &usage_checksum,
		.expected_result	= &camellia256_cts_cmac_Kc,
	},
	{
		.desc			= "Derive Ke subkey for camellia256-cts-cmac",
		.enctype		= ENCTYPE_CAMELLIA256_CTS_CMAC,
		.base_key		= &camellia256_cts_cmac_basekey,
		.usage			= &usage_encryption,
		.expected_result	= &camellia256_cts_cmac_Ke,
	},
	{
		.desc			= "Derive Ki subkey for camellia256-cts-cmac",
		.enctype		= ENCTYPE_CAMELLIA256_CTS_CMAC,
		.base_key		= &camellia256_cts_cmac_basekey,
		.usage			= &usage_integrity,
		.expected_result	= &camellia256_cts_cmac_Ki,
	},
};

/* Creates the function rfc6803_kdf_gen_params */
KUNIT_ARRAY_PARAM(rfc6803_kdf, rfc6803_kdf_test_params, gss_krb5_get_desc);

/*
 * From RFC 6803 Section 10.  Test vectors
 *
 * Sample checksums.
 *
 * Copyright (c) 2012 IETF Trust and the persons identified as the
 * document authors.  All rights reserved.
 *
 * XXX: These tests are likely to fail on EBCDIC or Unicode platforms.
 */
DEFINE_STR_XDR_NETOBJ(rfc6803_checksum_test1_plaintext,
		      "abcdefghijk");
DEFINE_HEX_XDR_NETOBJ(rfc6803_checksum_test1_basekey,
		      0x1d, 0xc4, 0x6a, 0x8d, 0x76, 0x3f, 0x4f, 0x93,
		      0x74, 0x2b, 0xcb, 0xa3, 0x38, 0x75, 0x76, 0xc3
);
DEFINE_HEX_XDR_NETOBJ(rfc6803_checksum_test1_usage,
		      0x00, 0x00, 0x00, 0x07, KEY_USAGE_SEED_CHECKSUM
);
DEFINE_HEX_XDR_NETOBJ(rfc6803_checksum_test1_expected_result,
		      0x11, 0x78, 0xe6, 0xc5, 0xc4, 0x7a, 0x8c, 0x1a,
		      0xe0, 0xc4, 0xb9, 0xc7, 0xd4, 0xeb, 0x7b, 0x6b
);

DEFINE_STR_XDR_NETOBJ(rfc6803_checksum_test2_plaintext,
		      "ABCDEFGHIJKLMNOPQRSTUVWXYZ");
DEFINE_HEX_XDR_NETOBJ(rfc6803_checksum_test2_basekey,
		      0x50, 0x27, 0xbc, 0x23, 0x1d, 0x0f, 0x3a, 0x9d,
		      0x23, 0x33, 0x3f, 0x1c, 0xa6, 0xfd, 0xbe, 0x7c
);
DEFINE_HEX_XDR_NETOBJ(rfc6803_checksum_test2_usage,
		      0x00, 0x00, 0x00, 0x08, KEY_USAGE_SEED_CHECKSUM
);
DEFINE_HEX_XDR_NETOBJ(rfc6803_checksum_test2_expected_result,
		      0xd1, 0xb3, 0x4f, 0x70, 0x04, 0xa7, 0x31, 0xf2,
		      0x3a, 0x0c, 0x00, 0xbf, 0x6c, 0x3f, 0x75, 0x3a
);

DEFINE_STR_XDR_NETOBJ(rfc6803_checksum_test3_plaintext,
		      "123456789");
DEFINE_HEX_XDR_NETOBJ(rfc6803_checksum_test3_basekey,
		      0xb6, 0x1c, 0x86, 0xcc, 0x4e, 0x5d, 0x27, 0x57,
		      0x54, 0x5a, 0xd4, 0x23, 0x39, 0x9f, 0xb7, 0x03,
		      0x1e, 0xca, 0xb9, 0x13, 0xcb, 0xb9, 0x00, 0xbd,
		      0x7a, 0x3c, 0x6d, 0xd8, 0xbf, 0x92, 0x01, 0x5b
);
DEFINE_HEX_XDR_NETOBJ(rfc6803_checksum_test3_usage,
		      0x00, 0x00, 0x00, 0x09, KEY_USAGE_SEED_CHECKSUM
);
DEFINE_HEX_XDR_NETOBJ(rfc6803_checksum_test3_expected_result,
		      0x87, 0xa1, 0x2c, 0xfd, 0x2b, 0x96, 0x21, 0x48,
		      0x10, 0xf0, 0x1c, 0x82, 0x6e, 0x77, 0x44, 0xb1
);

DEFINE_STR_XDR_NETOBJ(rfc6803_checksum_test4_plaintext,
		      "!@#$%^&*()!@#$%^&*()!@#$%^&*()");
DEFINE_HEX_XDR_NETOBJ(rfc6803_checksum_test4_basekey,
		      0x32, 0x16, 0x4c, 0x5b, 0x43, 0x4d, 0x1d, 0x15,
		      0x38, 0xe4, 0xcf, 0xd9, 0xbe, 0x80, 0x40, 0xfe,
		      0x8c, 0x4a, 0xc7, 0xac, 0xc4, 0xb9, 0x3d, 0x33,
		      0x14, 0xd2, 0x13, 0x36, 0x68, 0x14, 0x7a, 0x05
);
DEFINE_HEX_XDR_NETOBJ(rfc6803_checksum_test4_usage,
		      0x00, 0x00, 0x00, 0x0a, KEY_USAGE_SEED_CHECKSUM
);
DEFINE_HEX_XDR_NETOBJ(rfc6803_checksum_test4_expected_result,
		      0x3f, 0xa0, 0xb4, 0x23, 0x55, 0xe5, 0x2b, 0x18,
		      0x91, 0x87, 0x29, 0x4a, 0xa2, 0x52, 0xab, 0x64
);

static const struct gss_krb5_test_param rfc6803_checksum_test_params[] = {
	{
		.desc			= "camellia128-cts-cmac checksum test 1",
		.enctype		= ENCTYPE_CAMELLIA128_CTS_CMAC,
		.base_key		= &rfc6803_checksum_test1_basekey,
		.usage			= &rfc6803_checksum_test1_usage,
		.plaintext		= &rfc6803_checksum_test1_plaintext,
		.expected_result	= &rfc6803_checksum_test1_expected_result,
	},
	{
		.desc			= "camellia128-cts-cmac checksum test 2",
		.enctype		= ENCTYPE_CAMELLIA128_CTS_CMAC,
		.base_key		= &rfc6803_checksum_test2_basekey,
		.usage			= &rfc6803_checksum_test2_usage,
		.plaintext		= &rfc6803_checksum_test2_plaintext,
		.expected_result	= &rfc6803_checksum_test2_expected_result,
	},
	{
		.desc			= "camellia256-cts-cmac checksum test 3",
		.enctype		= ENCTYPE_CAMELLIA256_CTS_CMAC,
		.base_key		= &rfc6803_checksum_test3_basekey,
		.usage			= &rfc6803_checksum_test3_usage,
		.plaintext		= &rfc6803_checksum_test3_plaintext,
		.expected_result	= &rfc6803_checksum_test3_expected_result,
	},
	{
		.desc			= "camellia256-cts-cmac checksum test 4",
		.enctype		= ENCTYPE_CAMELLIA256_CTS_CMAC,
		.base_key		= &rfc6803_checksum_test4_basekey,
		.usage			= &rfc6803_checksum_test4_usage,
		.plaintext		= &rfc6803_checksum_test4_plaintext,
		.expected_result	= &rfc6803_checksum_test4_expected_result,
	},
};

/* Creates the function rfc6803_checksum_gen_params */
KUNIT_ARRAY_PARAM(rfc6803_checksum, rfc6803_checksum_test_params,
		  gss_krb5_get_desc);

/*
 * From RFC 6803 Section 10.  Test vectors
 *
 * Sample encryptions (all using the default cipher state)
 *
 * Copyright (c) 2012 IETF Trust and the persons identified as the
 * document authors.  All rights reserved.
 *
 * Key usage values are from errata 4326 against RFC 6803.
 */

static const struct xdr_netobj rfc6803_enc_empty_plaintext = {
	.len	= 0,
};

DEFINE_STR_XDR_NETOBJ(rfc6803_enc_1byte_plaintext, "1");
DEFINE_STR_XDR_NETOBJ(rfc6803_enc_9byte_plaintext, "9 bytesss");
DEFINE_STR_XDR_NETOBJ(rfc6803_enc_13byte_plaintext, "13 bytes byte");
DEFINE_STR_XDR_NETOBJ(rfc6803_enc_30byte_plaintext,
		      "30 bytes bytes bytes bytes byt"
);

DEFINE_HEX_XDR_NETOBJ(rfc6803_enc_test1_confounder,
		      0xb6, 0x98, 0x22, 0xa1, 0x9a, 0x6b, 0x09, 0xc0,
		      0xeb, 0xc8, 0x55, 0x7d, 0x1f, 0x1b, 0x6c, 0x0a
);
DEFINE_HEX_XDR_NETOBJ(rfc6803_enc_test1_basekey,
		      0x1d, 0xc4, 0x6a, 0x8d, 0x76, 0x3f, 0x4f, 0x93,
		      0x74, 0x2b, 0xcb, 0xa3, 0x38, 0x75, 0x76, 0xc3
);
DEFINE_HEX_XDR_NETOBJ(rfc6803_enc_test1_expected_result,
		      0xc4, 0x66, 0xf1, 0x87, 0x10, 0x69, 0x92, 0x1e,
		      0xdb, 0x7c, 0x6f, 0xde, 0x24, 0x4a, 0x52, 0xdb,
		      0x0b, 0xa1, 0x0e, 0xdc, 0x19, 0x7b, 0xdb, 0x80,
		      0x06, 0x65, 0x8c, 0xa3, 0xcc, 0xce, 0x6e, 0xb8
);

DEFINE_HEX_XDR_NETOBJ(rfc6803_enc_test2_confounder,
		      0x6f, 0x2f, 0xc3, 0xc2, 0xa1, 0x66, 0xfd, 0x88,
		      0x98, 0x96, 0x7a, 0x83, 0xde, 0x95, 0x96, 0xd9
);
DEFINE_HEX_XDR_NETOBJ(rfc6803_enc_test2_basekey,
		      0x50, 0x27, 0xbc, 0x23, 0x1d, 0x0f, 0x3a, 0x9d,
		      0x23, 0x33, 0x3f, 0x1c, 0xa6, 0xfd, 0xbe, 0x7c
);
DEFINE_HEX_XDR_NETOBJ(rfc6803_enc_test2_expected_result,
		      0x84, 0x2d, 0x21, 0xfd, 0x95, 0x03, 0x11, 0xc0,
		      0xdd, 0x46, 0x4a, 0x3f, 0x4b, 0xe8, 0xd6, 0xda,
		      0x88, 0xa5, 0x6d, 0x55, 0x9c, 0x9b, 0x47, 0xd3,
		      0xf9, 0xa8, 0x50, 0x67, 0xaf, 0x66, 0x15, 0x59,
		      0xb8
);

DEFINE_HEX_XDR_NETOBJ(rfc6803_enc_test3_confounder,
		      0xa5, 0xb4, 0xa7, 0x1e, 0x07, 0x7a, 0xee, 0xf9,
		      0x3c, 0x87, 0x63, 0xc1, 0x8f, 0xdb, 0x1f, 0x10
);
DEFINE_HEX_XDR_NETOBJ(rfc6803_enc_test3_basekey,
		      0xa1, 0xbb, 0x61, 0xe8, 0x05, 0xf9, 0xba, 0x6d,
		      0xde, 0x8f, 0xdb, 0xdd, 0xc0, 0x5c, 0xde, 0xa0
);
DEFINE_HEX_XDR_NETOBJ(rfc6803_enc_test3_expected_result,
		      0x61, 0x9f, 0xf0, 0x72, 0xe3, 0x62, 0x86, 0xff,
		      0x0a, 0x28, 0xde, 0xb3, 0xa3, 0x52, 0xec, 0x0d,
		      0x0e, 0xdf, 0x5c, 0x51, 0x60, 0xd6, 0x63, 0xc9,
		      0x01, 0x75, 0x8c, 0xcf, 0x9d, 0x1e, 0xd3, 0x3d,
		      0x71, 0xdb, 0x8f, 0x23, 0xaa, 0xbf, 0x83, 0x48,
		      0xa0
);

DEFINE_HEX_XDR_NETOBJ(rfc6803_enc_test4_confounder,
		      0x19, 0xfe, 0xe4, 0x0d, 0x81, 0x0c, 0x52, 0x4b,
		      0x5b, 0x22, 0xf0, 0x18, 0x74, 0xc6, 0x93, 0xda
);
DEFINE_HEX_XDR_NETOBJ(rfc6803_enc_test4_basekey,
		      0x2c, 0xa2, 0x7a, 0x5f, 0xaf, 0x55, 0x32, 0x24,
		      0x45, 0x06, 0x43, 0x4e, 0x1c, 0xef, 0x66, 0x76
);
DEFINE_HEX_XDR_NETOBJ(rfc6803_enc_test4_expected_result,
		      0xb8, 0xec, 0xa3, 0x16, 0x7a, 0xe6, 0x31, 0x55,
		      0x12, 0xe5, 0x9f, 0x98, 0xa7, 0xc5, 0x00, 0x20,
		      0x5e, 0x5f, 0x63, 0xff, 0x3b, 0xb3, 0x89, 0xaf,
		      0x1c, 0x41, 0xa2, 0x1d, 0x64, 0x0d, 0x86, 0x15,
		      0xc9, 0xed, 0x3f, 0xbe, 0xb0, 0x5a, 0xb6, 0xac,
		      0xb6, 0x76, 0x89, 0xb5, 0xea
);

DEFINE_HEX_XDR_NETOBJ(rfc6803_enc_test5_confounder,
		      0xca, 0x7a, 0x7a, 0xb4, 0xbe, 0x19, 0x2d, 0xab,
		      0xd6, 0x03, 0x50, 0x6d, 0xb1, 0x9c, 0x39, 0xe2
);
DEFINE_HEX_XDR_NETOBJ(rfc6803_enc_test5_basekey,
		      0x78, 0x24, 0xf8, 0xc1, 0x6f, 0x83, 0xff, 0x35,
		      0x4c, 0x6b, 0xf7, 0x51, 0x5b, 0x97, 0x3f, 0x43
);
DEFINE_HEX_XDR_NETOBJ(rfc6803_enc_test5_expected_result,
		      0xa2, 0x6a, 0x39, 0x05, 0xa4, 0xff, 0xd5, 0x81,
		      0x6b, 0x7b, 0x1e, 0x27, 0x38, 0x0d, 0x08, 0x09,
		      0x0c, 0x8e, 0xc1, 0xf3, 0x04, 0x49, 0x6e, 0x1a,
		      0xbd, 0xcd, 0x2b, 0xdc, 0xd1, 0xdf, 0xfc, 0x66,
		      0x09, 0x89, 0xe1, 0x17, 0xa7, 0x13, 0xdd, 0xbb,
		      0x57, 0xa4, 0x14, 0x6c, 0x15, 0x87, 0xcb, 0xa4,
		      0x35, 0x66, 0x65, 0x59, 0x1d, 0x22, 0x40, 0x28,
		      0x2f, 0x58, 0x42, 0xb1, 0x05, 0xa5
);

DEFINE_HEX_XDR_NETOBJ(rfc6803_enc_test6_confounder,
		      0x3c, 0xbb, 0xd2, 0xb4, 0x59, 0x17, 0x94, 0x10,
		      0x67, 0xf9, 0x65, 0x99, 0xbb, 0x98, 0x92, 0x6c
);
DEFINE_HEX_XDR_NETOBJ(rfc6803_enc_test6_basekey,
		      0xb6, 0x1c, 0x86, 0xcc, 0x4e, 0x5d, 0x27, 0x57,
		      0x54, 0x5a, 0xd4, 0x23, 0x39, 0x9f, 0xb7, 0x03,
		      0x1e, 0xca, 0xb9, 0x13, 0xcb, 0xb9, 0x00, 0xbd,
		      0x7a, 0x3c, 0x6d, 0xd8, 0xbf, 0x92, 0x01, 0x5b
);
DEFINE_HEX_XDR_NETOBJ(rfc6803_enc_test6_expected_result,
		      0x03, 0x88, 0x6d, 0x03, 0x31, 0x0b, 0x47, 0xa6,
		      0xd8, 0xf0, 0x6d, 0x7b, 0x94, 0xd1, 0xdd, 0x83,
		      0x7e, 0xcc, 0xe3, 0x15, 0xef, 0x65, 0x2a, 0xff,
		      0x62, 0x08, 0x59, 0xd9, 0x4a, 0x25, 0x92, 0x66
);

DEFINE_HEX_XDR_NETOBJ(rfc6803_enc_test7_confounder,
		      0xde, 0xf4, 0x87, 0xfc, 0xeb, 0xe6, 0xde, 0x63,
		      0x46, 0xd4, 0xda, 0x45, 0x21, 0xbb, 0xa2, 0xd2
);
DEFINE_HEX_XDR_NETOBJ(rfc6803_enc_test7_basekey,
		      0x1b, 0x97, 0xfe, 0x0a, 0x19, 0x0e, 0x20, 0x21,
		      0xeb, 0x30, 0x75, 0x3e, 0x1b, 0x6e, 0x1e, 0x77,
		      0xb0, 0x75, 0x4b, 0x1d, 0x68, 0x46, 0x10, 0x35,
		      0x58, 0x64, 0x10, 0x49, 0x63, 0x46, 0x38, 0x33
);
DEFINE_HEX_XDR_NETOBJ(rfc6803_enc_test7_expected_result,
		      0x2c, 0x9c, 0x15, 0x70, 0x13, 0x3c, 0x99, 0xbf,
		      0x6a, 0x34, 0xbc, 0x1b, 0x02, 0x12, 0x00, 0x2f,
		      0xd1, 0x94, 0x33, 0x87, 0x49, 0xdb, 0x41, 0x35,
		      0x49, 0x7a, 0x34, 0x7c, 0xfc, 0xd9, 0xd1, 0x8a,
		      0x12
);

DEFINE_HEX_XDR_NETOBJ(rfc6803_enc_test8_confounder,
		      0xad, 0x4f, 0xf9, 0x04, 0xd3, 0x4e, 0x55, 0x53,
		      0x84, 0xb1, 0x41, 0x00, 0xfc, 0x46, 0x5f, 0x88
);
DEFINE_HEX_XDR_NETOBJ(rfc6803_enc_test8_basekey,
		      0x32, 0x16, 0x4c, 0x5b, 0x43, 0x4d, 0x1d, 0x15,
		      0x38, 0xe4, 0xcf, 0xd9, 0xbe, 0x80, 0x40, 0xfe,
		      0x8c, 0x4a, 0xc7, 0xac, 0xc4, 0xb9, 0x3d, 0x33,
		      0x14, 0xd2, 0x13, 0x36, 0x68, 0x14, 0x7a, 0x05
);
DEFINE_HEX_XDR_NETOBJ(rfc6803_enc_test8_expected_result,
		      0x9c, 0x6d, 0xe7, 0x5f, 0x81, 0x2d, 0xe7, 0xed,
		      0x0d, 0x28, 0xb2, 0x96, 0x35, 0x57, 0xa1, 0x15,
		      0x64, 0x09, 0x98, 0x27, 0x5b, 0x0a, 0xf5, 0x15,
		      0x27, 0x09, 0x91, 0x3f, 0xf5, 0x2a, 0x2a, 0x9c,
		      0x8e, 0x63, 0xb8, 0x72, 0xf9, 0x2e, 0x64, 0xc8,
		      0x39
);

DEFINE_HEX_XDR_NETOBJ(rfc6803_enc_test9_confounder,
		      0xcf, 0x9b, 0xca, 0x6d, 0xf1, 0x14, 0x4e, 0x0c,
		      0x0a, 0xf9, 0xb8, 0xf3, 0x4c, 0x90, 0xd5, 0x14
);
DEFINE_HEX_XDR_NETOBJ(rfc6803_enc_test9_basekey,
		      0xb0, 0x38, 0xb1, 0x32, 0xcd, 0x8e, 0x06, 0x61,
		      0x22, 0x67, 0xfa, 0xb7, 0x17, 0x00, 0x66, 0xd8,
		      0x8a, 0xec, 0xcb, 0xa0, 0xb7, 0x44, 0xbf, 0xc6,
		      0x0d, 0xc8, 0x9b, 0xca, 0x18, 0x2d, 0x07, 0x15
);
DEFINE_HEX_XDR_NETOBJ(rfc6803_enc_test9_expected_result,
		      0xee, 0xec, 0x85, 0xa9, 0x81, 0x3c, 0xdc, 0x53,
		      0x67, 0x72, 0xab, 0x9b, 0x42, 0xde, 0xfc, 0x57,
		      0x06, 0xf7, 0x26, 0xe9, 0x75, 0xdd, 0xe0, 0x5a,
		      0x87, 0xeb, 0x54, 0x06, 0xea, 0x32, 0x4c, 0xa1,
		      0x85, 0xc9, 0x98, 0x6b, 0x42, 0xaa, 0xbe, 0x79,
		      0x4b, 0x84, 0x82, 0x1b, 0xee
);

DEFINE_HEX_XDR_NETOBJ(rfc6803_enc_test10_confounder,
		      0x64, 0x4d, 0xef, 0x38, 0xda, 0x35, 0x00, 0x72,
		      0x75, 0x87, 0x8d, 0x21, 0x68, 0x55, 0xe2, 0x28
);
DEFINE_HEX_XDR_NETOBJ(rfc6803_enc_test10_basekey,
		      0xcc, 0xfc, 0xd3, 0x49, 0xbf, 0x4c, 0x66, 0x77,
		      0xe8, 0x6e, 0x4b, 0x02, 0xb8, 0xea, 0xb9, 0x24,
		      0xa5, 0x46, 0xac, 0x73, 0x1c, 0xf9, 0xbf, 0x69,
		      0x89, 0xb9, 0x96, 0xe7, 0xd6, 0xbf, 0xbb, 0xa7
);
DEFINE_HEX_XDR_NETOBJ(rfc6803_enc_test10_expected_result,
		      0x0e, 0x44, 0x68, 0x09, 0x85, 0x85, 0x5f, 0x2d,
		      0x1f, 0x18, 0x12, 0x52, 0x9c, 0xa8, 0x3b, 0xfd,
		      0x8e, 0x34, 0x9d, 0xe6, 0xfd, 0x9a, 0xda, 0x0b,
		      0xaa, 0xa0, 0x48, 0xd6, 0x8e, 0x26, 0x5f, 0xeb,
		      0xf3, 0x4a, 0xd1, 0x25, 0x5a, 0x34, 0x49, 0x99,
		      0xad, 0x37, 0x14, 0x68, 0x87, 0xa6, 0xc6, 0x84,
		      0x57, 0x31, 0xac, 0x7f, 0x46, 0x37, 0x6a, 0x05,
		      0x04, 0xcd, 0x06, 0x57, 0x14, 0x74
);

static const struct gss_krb5_test_param rfc6803_encrypt_test_params[] = {
	{
		.desc			= "Encrypt empty plaintext with camellia128-cts-cmac",
		.enctype		= ENCTYPE_CAMELLIA128_CTS_CMAC,
		.constant		= 0,
		.base_key		= &rfc6803_enc_test1_basekey,
		.plaintext		= &rfc6803_enc_empty_plaintext,
		.confounder		= &rfc6803_enc_test1_confounder,
		.expected_result	= &rfc6803_enc_test1_expected_result,
	},
	{
		.desc			= "Encrypt 1 byte with camellia128-cts-cmac",
		.enctype		= ENCTYPE_CAMELLIA128_CTS_CMAC,
		.constant		= 1,
		.base_key		= &rfc6803_enc_test2_basekey,
		.plaintext		= &rfc6803_enc_1byte_plaintext,
		.confounder		= &rfc6803_enc_test2_confounder,
		.expected_result	= &rfc6803_enc_test2_expected_result,
	},
	{
		.desc			= "Encrypt 9 bytes with camellia128-cts-cmac",
		.enctype		= ENCTYPE_CAMELLIA128_CTS_CMAC,
		.constant		= 2,
		.base_key		= &rfc6803_enc_test3_basekey,
		.plaintext		= &rfc6803_enc_9byte_plaintext,
		.confounder		= &rfc6803_enc_test3_confounder,
		.expected_result	= &rfc6803_enc_test3_expected_result,
	},
	{
		.desc			= "Encrypt 13 bytes with camellia128-cts-cmac",
		.enctype		= ENCTYPE_CAMELLIA128_CTS_CMAC,
		.constant		= 3,
		.base_key		= &rfc6803_enc_test4_basekey,
		.plaintext		= &rfc6803_enc_13byte_plaintext,
		.confounder		= &rfc6803_enc_test4_confounder,
		.expected_result	= &rfc6803_enc_test4_expected_result,
	},
	{
		.desc			= "Encrypt 30 bytes with camellia128-cts-cmac",
		.enctype		= ENCTYPE_CAMELLIA128_CTS_CMAC,
		.constant		= 4,
		.base_key		= &rfc6803_enc_test5_basekey,
		.plaintext		= &rfc6803_enc_30byte_plaintext,
		.confounder		= &rfc6803_enc_test5_confounder,
		.expected_result	= &rfc6803_enc_test5_expected_result,
	},
	{
		.desc			= "Encrypt empty plaintext with camellia256-cts-cmac",
		.enctype		= ENCTYPE_CAMELLIA256_CTS_CMAC,
		.constant		= 0,
		.base_key		= &rfc6803_enc_test6_basekey,
		.plaintext		= &rfc6803_enc_empty_plaintext,
		.confounder		= &rfc6803_enc_test6_confounder,
		.expected_result	= &rfc6803_enc_test6_expected_result,
	},
	{
		.desc			= "Encrypt 1 byte with camellia256-cts-cmac",
		.enctype		= ENCTYPE_CAMELLIA256_CTS_CMAC,
		.constant		= 1,
		.base_key		= &rfc6803_enc_test7_basekey,
		.plaintext		= &rfc6803_enc_1byte_plaintext,
		.confounder		= &rfc6803_enc_test7_confounder,
		.expected_result	= &rfc6803_enc_test7_expected_result,
	},
	{
		.desc			= "Encrypt 9 bytes with camellia256-cts-cmac",
		.enctype		= ENCTYPE_CAMELLIA256_CTS_CMAC,
		.constant		= 2,
		.base_key		= &rfc6803_enc_test8_basekey,
		.plaintext		= &rfc6803_enc_9byte_plaintext,
		.confounder		= &rfc6803_enc_test8_confounder,
		.expected_result	= &rfc6803_enc_test8_expected_result,
	},
	{
		.desc			= "Encrypt 13 bytes with camellia256-cts-cmac",
		.enctype		= ENCTYPE_CAMELLIA256_CTS_CMAC,
		.constant		= 3,
		.base_key		= &rfc6803_enc_test9_basekey,
		.plaintext		= &rfc6803_enc_13byte_plaintext,
		.confounder		= &rfc6803_enc_test9_confounder,
		.expected_result	= &rfc6803_enc_test9_expected_result,
	},
	{
		.desc			= "Encrypt 30 bytes with camellia256-cts-cmac",
		.enctype		= ENCTYPE_CAMELLIA256_CTS_CMAC,
		.constant		= 4,
		.base_key		= &rfc6803_enc_test10_basekey,
		.plaintext		= &rfc6803_enc_30byte_plaintext,
		.confounder		= &rfc6803_enc_test10_confounder,
		.expected_result	= &rfc6803_enc_test10_expected_result,
	},
};

/* Creates the function rfc6803_encrypt_gen_params */
KUNIT_ARRAY_PARAM(rfc6803_encrypt, rfc6803_encrypt_test_params,
		  gss_krb5_get_desc);

static void rfc6803_encrypt_case(struct kunit *test)
{
	const struct gss_krb5_test_param *param = test->param_value;
	struct crypto_sync_skcipher *cts_tfm, *cbc_tfm;
	const struct gss_krb5_enctype *gk5e;
	struct xdr_netobj Ke, Ki, checksum;
	u8 usage_data[GSS_KRB5_K5CLENGTH];
	struct xdr_netobj usage = {
		.data = usage_data,
		.len = sizeof(usage_data),
	};
	struct crypto_ahash *ahash_tfm;
	unsigned int blocksize;
	struct xdr_buf buf;
	void *text;
	size_t len;
	u32 err;

	/* Arrange */
	gk5e = gss_krb5_lookup_enctype(param->enctype);
	if (!gk5e)
		kunit_skip(test, "Encryption type is not available");

	memset(usage_data, 0, sizeof(usage_data));
	usage.data[3] = param->constant;

	Ke.len = gk5e->Ke_length;
	Ke.data = kunit_kzalloc(test, Ke.len, GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, Ke.data);
	usage.data[4] = KEY_USAGE_SEED_ENCRYPTION;
	err = gk5e->derive_key(gk5e, param->base_key, &Ke, &usage, GFP_KERNEL);
	KUNIT_ASSERT_EQ(test, err, 0);

	cbc_tfm = crypto_alloc_sync_skcipher(gk5e->aux_cipher, 0, 0);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, cbc_tfm);
	err = crypto_sync_skcipher_setkey(cbc_tfm, Ke.data, Ke.len);
	KUNIT_ASSERT_EQ(test, err, 0);

	cts_tfm = crypto_alloc_sync_skcipher(gk5e->encrypt_name, 0, 0);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, cts_tfm);
	err = crypto_sync_skcipher_setkey(cts_tfm, Ke.data, Ke.len);
	KUNIT_ASSERT_EQ(test, err, 0);
	blocksize = crypto_sync_skcipher_blocksize(cts_tfm);

	len = param->confounder->len + param->plaintext->len + blocksize;
	text = kunit_kzalloc(test, len, GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, text);
	memcpy(text, param->confounder->data, param->confounder->len);
	memcpy(text + param->confounder->len, param->plaintext->data,
	       param->plaintext->len);

	memset(&buf, 0, sizeof(buf));
	buf.head[0].iov_base = text;
	buf.head[0].iov_len = param->confounder->len + param->plaintext->len;
	buf.len = buf.head[0].iov_len;

	checksum.len = gk5e->cksumlength;
	checksum.data = kunit_kzalloc(test, checksum.len, GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, checksum.data);

	Ki.len = gk5e->Ki_length;
	Ki.data = kunit_kzalloc(test, Ki.len, GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, Ki.data);
	usage.data[4] = KEY_USAGE_SEED_INTEGRITY;
	err = gk5e->derive_key(gk5e, param->base_key, &Ki,
			       &usage, GFP_KERNEL);
	KUNIT_ASSERT_EQ(test, err, 0);
	ahash_tfm = crypto_alloc_ahash(gk5e->cksum_name, 0, CRYPTO_ALG_ASYNC);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, ahash_tfm);
	err = crypto_ahash_setkey(ahash_tfm, Ki.data, Ki.len);
	KUNIT_ASSERT_EQ(test, err, 0);

	/* Act */
	err = gss_krb5_checksum(ahash_tfm, NULL, 0, &buf, 0, &checksum);
	KUNIT_ASSERT_EQ(test, err, 0);

	err = krb5_cbc_cts_encrypt(cts_tfm, cbc_tfm, 0, &buf, NULL, NULL, 0);
	KUNIT_ASSERT_EQ(test, err, 0);

	/* Assert */
	KUNIT_EXPECT_EQ_MSG(test, param->expected_result->len,
			    buf.len + checksum.len,
			    "ciphertext length mismatch");
	KUNIT_EXPECT_EQ_MSG(test,
			    memcmp(param->expected_result->data,
				   buf.head[0].iov_base, buf.len), 0,
			    "encrypted result mismatch");
	KUNIT_EXPECT_EQ_MSG(test,
			    memcmp(param->expected_result->data +
				   (param->expected_result->len - checksum.len),
				   checksum.data, checksum.len), 0,
			    "HMAC mismatch");

	crypto_free_ahash(ahash_tfm);
	crypto_free_sync_skcipher(cts_tfm);
	crypto_free_sync_skcipher(cbc_tfm);
}

static struct kunit_case rfc6803_test_cases[] = {
	{
		.name			= "RFC 6803 key derivation",
		.run_case		= kdf_case,
		.generate_params	= rfc6803_kdf_gen_params,
	},
	{
		.name			= "RFC 6803 checksum",
		.run_case		= checksum_case,
		.generate_params	= rfc6803_checksum_gen_params,
	},
	{
		.name			= "RFC 6803 encryption",
		.run_case		= rfc6803_encrypt_case,
		.generate_params	= rfc6803_encrypt_gen_params,
	},
	{}
};

static struct kunit_suite rfc6803_suite = {
	.name			= "RFC 6803 suite",
	.test_cases		= rfc6803_test_cases,
};

/*
 * From RFC 8009 Appendix A.  Test Vectors
 *
 * Sample results for SHA-2 enctype key derivation
 *
 * This test material is copyright (c) 2016 IETF Trust and the
 * persons identified as the document authors.  All rights reserved.
 */

DEFINE_HEX_XDR_NETOBJ(aes128_cts_hmac_sha256_128_basekey,
		      0x37, 0x05, 0xd9, 0x60, 0x80, 0xc1, 0x77, 0x28,
		      0xa0, 0xe8, 0x00, 0xea, 0xb6, 0xe0, 0xd2, 0x3c
);
DEFINE_HEX_XDR_NETOBJ(aes128_cts_hmac_sha256_128_Kc,
		      0xb3, 0x1a, 0x01, 0x8a, 0x48, 0xf5, 0x47, 0x76,
		      0xf4, 0x03, 0xe9, 0xa3, 0x96, 0x32, 0x5d, 0xc3
);
DEFINE_HEX_XDR_NETOBJ(aes128_cts_hmac_sha256_128_Ke,
		      0x9b, 0x19, 0x7d, 0xd1, 0xe8, 0xc5, 0x60, 0x9d,
		      0x6e, 0x67, 0xc3, 0xe3, 0x7c, 0x62, 0xc7, 0x2e
);
DEFINE_HEX_XDR_NETOBJ(aes128_cts_hmac_sha256_128_Ki,
		      0x9f, 0xda, 0x0e, 0x56, 0xab, 0x2d, 0x85, 0xe1,
		      0x56, 0x9a, 0x68, 0x86, 0x96, 0xc2, 0x6a, 0x6c
);

DEFINE_HEX_XDR_NETOBJ(aes256_cts_hmac_sha384_192_basekey,
		      0x6d, 0x40, 0x4d, 0x37, 0xfa, 0xf7, 0x9f, 0x9d,
		      0xf0, 0xd3, 0x35, 0x68, 0xd3, 0x20, 0x66, 0x98,
		      0x00, 0xeb, 0x48, 0x36, 0x47, 0x2e, 0xa8, 0xa0,
		      0x26, 0xd1, 0x6b, 0x71, 0x82, 0x46, 0x0c, 0x52
);
DEFINE_HEX_XDR_NETOBJ(aes256_cts_hmac_sha384_192_Kc,
		      0xef, 0x57, 0x18, 0xbe, 0x86, 0xcc, 0x84, 0x96,
		      0x3d, 0x8b, 0xbb, 0x50, 0x31, 0xe9, 0xf5, 0xc4,
		      0xba, 0x41, 0xf2, 0x8f, 0xaf, 0x69, 0xe7, 0x3d
);
DEFINE_HEX_XDR_NETOBJ(aes256_cts_hmac_sha384_192_Ke,
		      0x56, 0xab, 0x22, 0xbe, 0xe6, 0x3d, 0x82, 0xd7,
		      0xbc, 0x52, 0x27, 0xf6, 0x77, 0x3f, 0x8e, 0xa7,
		      0xa5, 0xeb, 0x1c, 0x82, 0x51, 0x60, 0xc3, 0x83,
		      0x12, 0x98, 0x0c, 0x44, 0x2e, 0x5c, 0x7e, 0x49
);
DEFINE_HEX_XDR_NETOBJ(aes256_cts_hmac_sha384_192_Ki,
		      0x69, 0xb1, 0x65, 0x14, 0xe3, 0xcd, 0x8e, 0x56,
		      0xb8, 0x20, 0x10, 0xd5, 0xc7, 0x30, 0x12, 0xb6,
		      0x22, 0xc4, 0xd0, 0x0f, 0xfc, 0x23, 0xed, 0x1f
);

static const struct gss_krb5_test_param rfc8009_kdf_test_params[] = {
	{
		.desc			= "Derive Kc subkey for aes128-cts-hmac-sha256-128",
		.enctype		= ENCTYPE_AES128_CTS_HMAC_SHA256_128,
		.base_key		= &aes128_cts_hmac_sha256_128_basekey,
		.usage			= &usage_checksum,
		.expected_result	= &aes128_cts_hmac_sha256_128_Kc,
	},
	{
		.desc			= "Derive Ke subkey for aes128-cts-hmac-sha256-128",
		.enctype		= ENCTYPE_AES128_CTS_HMAC_SHA256_128,
		.base_key		= &aes128_cts_hmac_sha256_128_basekey,
		.usage			= &usage_encryption,
		.expected_result	= &aes128_cts_hmac_sha256_128_Ke,
	},
	{
		.desc			= "Derive Ki subkey for aes128-cts-hmac-sha256-128",
		.enctype		= ENCTYPE_AES128_CTS_HMAC_SHA256_128,
		.base_key		= &aes128_cts_hmac_sha256_128_basekey,
		.usage			= &usage_integrity,
		.expected_result	= &aes128_cts_hmac_sha256_128_Ki,
	},
	{
		.desc			= "Derive Kc subkey for aes256-cts-hmac-sha384-192",
		.enctype		= ENCTYPE_AES256_CTS_HMAC_SHA384_192,
		.base_key		= &aes256_cts_hmac_sha384_192_basekey,
		.usage			= &usage_checksum,
		.expected_result	= &aes256_cts_hmac_sha384_192_Kc,
	},
	{
		.desc			= "Derive Ke subkey for aes256-cts-hmac-sha384-192",
		.enctype		= ENCTYPE_AES256_CTS_HMAC_SHA384_192,
		.base_key		= &aes256_cts_hmac_sha384_192_basekey,
		.usage			= &usage_encryption,
		.expected_result	= &aes256_cts_hmac_sha384_192_Ke,
	},
	{
		.desc			= "Derive Ki subkey for aes256-cts-hmac-sha384-192",
		.enctype		= ENCTYPE_AES256_CTS_HMAC_SHA384_192,
		.base_key		= &aes256_cts_hmac_sha384_192_basekey,
		.usage			= &usage_integrity,
		.expected_result	= &aes256_cts_hmac_sha384_192_Ki,
	},
};

/* Creates the function rfc8009_kdf_gen_params */
KUNIT_ARRAY_PARAM(rfc8009_kdf, rfc8009_kdf_test_params, gss_krb5_get_desc);

/*
 * From RFC 8009 Appendix A.  Test Vectors
 *
 * These sample checksums use the above sample key derivation results,
 * including use of the same base-key and key usage values.
 *
 * This test material is copyright (c) 2016 IETF Trust and the
 * persons identified as the document authors.  All rights reserved.
 */

DEFINE_HEX_XDR_NETOBJ(rfc8009_checksum_plaintext,
		      0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
		      0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f,
		      0x10, 0x11, 0x12, 0x13, 0x14
);
DEFINE_HEX_XDR_NETOBJ(rfc8009_checksum_test1_expected_result,
		      0xd7, 0x83, 0x67, 0x18, 0x66, 0x43, 0xd6, 0x7b,
		      0x41, 0x1c, 0xba, 0x91, 0x39, 0xfc, 0x1d, 0xee
);
DEFINE_HEX_XDR_NETOBJ(rfc8009_checksum_test2_expected_result,
		      0x45, 0xee, 0x79, 0x15, 0x67, 0xee, 0xfc, 0xa3,
		      0x7f, 0x4a, 0xc1, 0xe0, 0x22, 0x2d, 0xe8, 0x0d,
		      0x43, 0xc3, 0xbf, 0xa0, 0x66, 0x99, 0x67, 0x2a
);

static const struct gss_krb5_test_param rfc8009_checksum_test_params[] = {
	{
		.desc			= "Checksum with aes128-cts-hmac-sha256-128",
		.enctype		= ENCTYPE_AES128_CTS_HMAC_SHA256_128,
		.base_key		= &aes128_cts_hmac_sha256_128_basekey,
		.usage			= &usage_checksum,
		.plaintext		= &rfc8009_checksum_plaintext,
		.expected_result	= &rfc8009_checksum_test1_expected_result,
	},
	{
		.desc			= "Checksum with aes256-cts-hmac-sha384-192",
		.enctype		= ENCTYPE_AES256_CTS_HMAC_SHA384_192,
		.base_key		= &aes256_cts_hmac_sha384_192_basekey,
		.usage			= &usage_checksum,
		.plaintext		= &rfc8009_checksum_plaintext,
		.expected_result	= &rfc8009_checksum_test2_expected_result,
	},
};

/* Creates the function rfc8009_checksum_gen_params */
KUNIT_ARRAY_PARAM(rfc8009_checksum, rfc8009_checksum_test_params,
		  gss_krb5_get_desc);

/*
 * From RFC 8009 Appendix A.  Test Vectors
 *
 * Sample encryptions (all using the default cipher state):
 * --------------------------------------------------------
 *
 * These sample encryptions use the above sample key derivation results,
 * including use of the same base-key and key usage values.
 *
 * This test material is copyright (c) 2016 IETF Trust and the
 * persons identified as the document authors.  All rights reserved.
 */

static const struct xdr_netobj rfc8009_enc_empty_plaintext = {
	.len	= 0,
};
DEFINE_HEX_XDR_NETOBJ(rfc8009_enc_short_plaintext,
		      0x00, 0x01, 0x02, 0x03, 0x04, 0x05
);
DEFINE_HEX_XDR_NETOBJ(rfc8009_enc_block_plaintext,
		      0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
		      0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f
);
DEFINE_HEX_XDR_NETOBJ(rfc8009_enc_long_plaintext,
		      0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
		      0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f,
		      0x10, 0x11, 0x12, 0x13, 0x14
);

DEFINE_HEX_XDR_NETOBJ(rfc8009_enc_test1_confounder,
		      0x7e, 0x58, 0x95, 0xea, 0xf2, 0x67, 0x24, 0x35,
		      0xba, 0xd8, 0x17, 0xf5, 0x45, 0xa3, 0x71, 0x48
);
DEFINE_HEX_XDR_NETOBJ(rfc8009_enc_test1_expected_result,
		      0xef, 0x85, 0xfb, 0x89, 0x0b, 0xb8, 0x47, 0x2f,
		      0x4d, 0xab, 0x20, 0x39, 0x4d, 0xca, 0x78, 0x1d
);
DEFINE_HEX_XDR_NETOBJ(rfc8009_enc_test1_expected_hmac,
		      0xad, 0x87, 0x7e, 0xda, 0x39, 0xd5, 0x0c, 0x87,
		      0x0c, 0x0d, 0x5a, 0x0a, 0x8e, 0x48, 0xc7, 0x18
);

DEFINE_HEX_XDR_NETOBJ(rfc8009_enc_test2_confounder,
		      0x7b, 0xca, 0x28, 0x5e, 0x2f, 0xd4, 0x13, 0x0f,
		      0xb5, 0x5b, 0x1a, 0x5c, 0x83, 0xbc, 0x5b, 0x24
);
DEFINE_HEX_XDR_NETOBJ(rfc8009_enc_test2_expected_result,
		      0x84, 0xd7, 0xf3, 0x07, 0x54, 0xed, 0x98, 0x7b,
		      0xab, 0x0b, 0xf3, 0x50, 0x6b, 0xeb, 0x09, 0xcf,
		      0xb5, 0x54, 0x02, 0xce, 0xf7, 0xe6
);
DEFINE_HEX_XDR_NETOBJ(rfc8009_enc_test2_expected_hmac,
		      0x87, 0x7c, 0xe9, 0x9e, 0x24, 0x7e, 0x52, 0xd1,
		      0x6e, 0xd4, 0x42, 0x1d, 0xfd, 0xf8, 0x97, 0x6c
);

DEFINE_HEX_XDR_NETOBJ(rfc8009_enc_test3_confounder,
		      0x56, 0xab, 0x21, 0x71, 0x3f, 0xf6, 0x2c, 0x0a,
		      0x14, 0x57, 0x20, 0x0f, 0x6f, 0xa9, 0x94, 0x8f
);
DEFINE_HEX_XDR_NETOBJ(rfc8009_enc_test3_expected_result,
		      0x35, 0x17, 0xd6, 0x40, 0xf5, 0x0d, 0xdc, 0x8a,
		      0xd3, 0x62, 0x87, 0x22, 0xb3, 0x56, 0x9d, 0x2a,
		      0xe0, 0x74, 0x93, 0xfa, 0x82, 0x63, 0x25, 0x40,
		      0x80, 0xea, 0x65, 0xc1, 0x00, 0x8e, 0x8f, 0xc2
);
DEFINE_HEX_XDR_NETOBJ(rfc8009_enc_test3_expected_hmac,
		      0x95, 0xfb, 0x48, 0x52, 0xe7, 0xd8, 0x3e, 0x1e,
		      0x7c, 0x48, 0xc3, 0x7e, 0xeb, 0xe6, 0xb0, 0xd3
);

DEFINE_HEX_XDR_NETOBJ(rfc8009_enc_test4_confounder,
		      0xa7, 0xa4, 0xe2, 0x9a, 0x47, 0x28, 0xce, 0x10,
		      0x66, 0x4f, 0xb6, 0x4e, 0x49, 0xad, 0x3f, 0xac
);
DEFINE_HEX_XDR_NETOBJ(rfc8009_enc_test4_expected_result,
		      0x72, 0x0f, 0x73, 0xb1, 0x8d, 0x98, 0x59, 0xcd,
		      0x6c, 0xcb, 0x43, 0x46, 0x11, 0x5c, 0xd3, 0x36,
		      0xc7, 0x0f, 0x58, 0xed, 0xc0, 0xc4, 0x43, 0x7c,
		      0x55, 0x73, 0x54, 0x4c, 0x31, 0xc8, 0x13, 0xbc,
		      0xe1, 0xe6, 0xd0, 0x72, 0xc1
);
DEFINE_HEX_XDR_NETOBJ(rfc8009_enc_test4_expected_hmac,
		      0x86, 0xb3, 0x9a, 0x41, 0x3c, 0x2f, 0x92, 0xca,
		      0x9b, 0x83, 0x34, 0xa2, 0x87, 0xff, 0xcb, 0xfc
);

DEFINE_HEX_XDR_NETOBJ(rfc8009_enc_test5_confounder,
		      0xf7, 0x64, 0xe9, 0xfa, 0x15, 0xc2, 0x76, 0x47,
		      0x8b, 0x2c, 0x7d, 0x0c, 0x4e, 0x5f, 0x58, 0xe4
);
DEFINE_HEX_XDR_NETOBJ(rfc8009_enc_test5_expected_result,
		      0x41, 0xf5, 0x3f, 0xa5, 0xbf, 0xe7, 0x02, 0x6d,
		      0x91, 0xfa, 0xf9, 0xbe, 0x95, 0x91, 0x95, 0xa0
);
DEFINE_HEX_XDR_NETOBJ(rfc8009_enc_test5_expected_hmac,
		      0x58, 0x70, 0x72, 0x73, 0xa9, 0x6a, 0x40, 0xf0,
		      0xa0, 0x19, 0x60, 0x62, 0x1a, 0xc6, 0x12, 0x74,
		      0x8b, 0x9b, 0xbf, 0xbe, 0x7e, 0xb4, 0xce, 0x3c
);

DEFINE_HEX_XDR_NETOBJ(rfc8009_enc_test6_confounder,
		      0xb8, 0x0d, 0x32, 0x51, 0xc1, 0xf6, 0x47, 0x14,
		      0x94, 0x25, 0x6f, 0xfe, 0x71, 0x2d, 0x0b, 0x9a
);
DEFINE_HEX_XDR_NETOBJ(rfc8009_enc_test6_expected_result,
		      0x4e, 0xd7, 0xb3, 0x7c, 0x2b, 0xca, 0xc8, 0xf7,
		      0x4f, 0x23, 0xc1, 0xcf, 0x07, 0xe6, 0x2b, 0xc7,
		      0xb7, 0x5f, 0xb3, 0xf6, 0x37, 0xb9
);
DEFINE_HEX_XDR_NETOBJ(rfc8009_enc_test6_expected_hmac,
		      0xf5, 0x59, 0xc7, 0xf6, 0x64, 0xf6, 0x9e, 0xab,
		      0x7b, 0x60, 0x92, 0x23, 0x75, 0x26, 0xea, 0x0d,
		      0x1f, 0x61, 0xcb, 0x20, 0xd6, 0x9d, 0x10, 0xf2
);

DEFINE_HEX_XDR_NETOBJ(rfc8009_enc_test7_confounder,
		      0x53, 0xbf, 0x8a, 0x0d, 0x10, 0x52, 0x65, 0xd4,
		      0xe2, 0x76, 0x42, 0x86, 0x24, 0xce, 0x5e, 0x63
);
DEFINE_HEX_XDR_NETOBJ(rfc8009_enc_test7_expected_result,
		      0xbc, 0x47, 0xff, 0xec, 0x79, 0x98, 0xeb, 0x91,
		      0xe8, 0x11, 0x5c, 0xf8, 0xd1, 0x9d, 0xac, 0x4b,
		      0xbb, 0xe2, 0xe1, 0x63, 0xe8, 0x7d, 0xd3, 0x7f,
		      0x49, 0xbe, 0xca, 0x92, 0x02, 0x77, 0x64, 0xf6
);
DEFINE_HEX_XDR_NETOBJ(rfc8009_enc_test7_expected_hmac,
		      0x8c, 0xf5, 0x1f, 0x14, 0xd7, 0x98, 0xc2, 0x27,
		      0x3f, 0x35, 0xdf, 0x57, 0x4d, 0x1f, 0x93, 0x2e,
		      0x40, 0xc4, 0xff, 0x25, 0x5b, 0x36, 0xa2, 0x66
);

DEFINE_HEX_XDR_NETOBJ(rfc8009_enc_test8_confounder,
		      0x76, 0x3e, 0x65, 0x36, 0x7e, 0x86, 0x4f, 0x02,
		      0xf5, 0x51, 0x53, 0xc7, 0xe3, 0xb5, 0x8a, 0xf1
);
DEFINE_HEX_XDR_NETOBJ(rfc8009_enc_test8_expected_result,
		      0x40, 0x01, 0x3e, 0x2d, 0xf5, 0x8e, 0x87, 0x51,
		      0x95, 0x7d, 0x28, 0x78, 0xbc, 0xd2, 0xd6, 0xfe,
		      0x10, 0x1c, 0xcf, 0xd5, 0x56, 0xcb, 0x1e, 0xae,
		      0x79, 0xdb, 0x3c, 0x3e, 0xe8, 0x64, 0x29, 0xf2,
		      0xb2, 0xa6, 0x02, 0xac, 0x86
);
DEFINE_HEX_XDR_NETOBJ(rfc8009_enc_test8_expected_hmac,
		      0xfe, 0xf6, 0xec, 0xb6, 0x47, 0xd6, 0x29, 0x5f,
		      0xae, 0x07, 0x7a, 0x1f, 0xeb, 0x51, 0x75, 0x08,
		      0xd2, 0xc1, 0x6b, 0x41, 0x92, 0xe0, 0x1f, 0x62
);

static const struct gss_krb5_test_param rfc8009_encrypt_test_params[] = {
	{
		.desc			= "Encrypt empty plaintext with aes128-cts-hmac-sha256-128",
		.enctype		= ENCTYPE_AES128_CTS_HMAC_SHA256_128,
		.plaintext		= &rfc8009_enc_empty_plaintext,
		.confounder		= &rfc8009_enc_test1_confounder,
		.base_key		= &aes128_cts_hmac_sha256_128_basekey,
		.expected_result	= &rfc8009_enc_test1_expected_result,
		.expected_hmac		= &rfc8009_enc_test1_expected_hmac,
	},
	{
		.desc			= "Encrypt short plaintext with aes128-cts-hmac-sha256-128",
		.enctype		= ENCTYPE_AES128_CTS_HMAC_SHA256_128,
		.plaintext		= &rfc8009_enc_short_plaintext,
		.confounder		= &rfc8009_enc_test2_confounder,
		.base_key		= &aes128_cts_hmac_sha256_128_basekey,
		.expected_result	= &rfc8009_enc_test2_expected_result,
		.expected_hmac		= &rfc8009_enc_test2_expected_hmac,
	},
	{
		.desc			= "Encrypt block plaintext with aes128-cts-hmac-sha256-128",
		.enctype		= ENCTYPE_AES128_CTS_HMAC_SHA256_128,
		.plaintext		= &rfc8009_enc_block_plaintext,
		.confounder		= &rfc8009_enc_test3_confounder,
		.base_key		= &aes128_cts_hmac_sha256_128_basekey,
		.expected_result	= &rfc8009_enc_test3_expected_result,
		.expected_hmac		= &rfc8009_enc_test3_expected_hmac,
	},
	{
		.desc			= "Encrypt long plaintext with aes128-cts-hmac-sha256-128",
		.enctype		= ENCTYPE_AES128_CTS_HMAC_SHA256_128,
		.plaintext		= &rfc8009_enc_long_plaintext,
		.confounder		= &rfc8009_enc_test4_confounder,
		.base_key		= &aes128_cts_hmac_sha256_128_basekey,
		.expected_result	= &rfc8009_enc_test4_expected_result,
		.expected_hmac		= &rfc8009_enc_test4_expected_hmac,
	},
	{
		.desc			= "Encrypt empty plaintext with aes256-cts-hmac-sha384-192",
		.enctype		= ENCTYPE_AES256_CTS_HMAC_SHA384_192,
		.plaintext		= &rfc8009_enc_empty_plaintext,
		.confounder		= &rfc8009_enc_test5_confounder,
		.base_key		= &aes256_cts_hmac_sha384_192_basekey,
		.expected_result	= &rfc8009_enc_test5_expected_result,
		.expected_hmac		= &rfc8009_enc_test5_expected_hmac,
	},
	{
		.desc			= "Encrypt short plaintext with aes256-cts-hmac-sha384-192",
		.enctype		= ENCTYPE_AES256_CTS_HMAC_SHA384_192,
		.plaintext		= &rfc8009_enc_short_plaintext,
		.confounder		= &rfc8009_enc_test6_confounder,
		.base_key		= &aes256_cts_hmac_sha384_192_basekey,
		.expected_result	= &rfc8009_enc_test6_expected_result,
		.expected_hmac		= &rfc8009_enc_test6_expected_hmac,
	},
	{
		.desc			= "Encrypt block plaintext with aes256-cts-hmac-sha384-192",
		.enctype		= ENCTYPE_AES256_CTS_HMAC_SHA384_192,
		.plaintext		= &rfc8009_enc_block_plaintext,
		.confounder		= &rfc8009_enc_test7_confounder,
		.base_key		= &aes256_cts_hmac_sha384_192_basekey,
		.expected_result	= &rfc8009_enc_test7_expected_result,
		.expected_hmac		= &rfc8009_enc_test7_expected_hmac,
	},
	{
		.desc			= "Encrypt long plaintext with aes256-cts-hmac-sha384-192",
		.enctype		= ENCTYPE_AES256_CTS_HMAC_SHA384_192,
		.plaintext		= &rfc8009_enc_long_plaintext,
		.confounder		= &rfc8009_enc_test8_confounder,
		.base_key		= &aes256_cts_hmac_sha384_192_basekey,
		.expected_result	= &rfc8009_enc_test8_expected_result,
		.expected_hmac		= &rfc8009_enc_test8_expected_hmac,
	},
};

/* Creates the function rfc8009_encrypt_gen_params */
KUNIT_ARRAY_PARAM(rfc8009_encrypt, rfc8009_encrypt_test_params,
		  gss_krb5_get_desc);

static void rfc8009_encrypt_case(struct kunit *test)
{
	const struct gss_krb5_test_param *param = test->param_value;
	struct crypto_sync_skcipher *cts_tfm, *cbc_tfm;
	const struct gss_krb5_enctype *gk5e;
	struct xdr_netobj Ke, Ki, checksum;
	u8 usage_data[GSS_KRB5_K5CLENGTH];
	struct xdr_netobj usage = {
		.data = usage_data,
		.len = sizeof(usage_data),
	};
	struct crypto_ahash *ahash_tfm;
	struct xdr_buf buf;
	void *text;
	size_t len;
	u32 err;

	/* Arrange */
	gk5e = gss_krb5_lookup_enctype(param->enctype);
	if (!gk5e)
		kunit_skip(test, "Encryption type is not available");

	*(__be32 *)usage.data = cpu_to_be32(2);

	Ke.len = gk5e->Ke_length;
	Ke.data = kunit_kzalloc(test, Ke.len, GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, Ke.data);
	usage.data[4] = KEY_USAGE_SEED_ENCRYPTION;
	err = gk5e->derive_key(gk5e, param->base_key, &Ke,
			       &usage, GFP_KERNEL);
	KUNIT_ASSERT_EQ(test, err, 0);

	cbc_tfm = crypto_alloc_sync_skcipher(gk5e->aux_cipher, 0, 0);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, cbc_tfm);
	err = crypto_sync_skcipher_setkey(cbc_tfm, Ke.data, Ke.len);
	KUNIT_ASSERT_EQ(test, err, 0);

	cts_tfm = crypto_alloc_sync_skcipher(gk5e->encrypt_name, 0, 0);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, cts_tfm);
	err = crypto_sync_skcipher_setkey(cts_tfm, Ke.data, Ke.len);
	KUNIT_ASSERT_EQ(test, err, 0);

	len = param->confounder->len + param->plaintext->len;
	text = kunit_kzalloc(test, len, GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, text);
	memcpy(text, param->confounder->data, param->confounder->len);
	memcpy(text + param->confounder->len, param->plaintext->data,
	       param->plaintext->len);

	memset(&buf, 0, sizeof(buf));
	buf.head[0].iov_base = text;
	buf.head[0].iov_len = param->confounder->len + param->plaintext->len;
	buf.len = buf.head[0].iov_len;

	checksum.len = gk5e->cksumlength;
	checksum.data = kunit_kzalloc(test, checksum.len, GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, checksum.data);

	Ki.len = gk5e->Ki_length;
	Ki.data = kunit_kzalloc(test, Ki.len, GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, Ki.data);
	usage.data[4] = KEY_USAGE_SEED_INTEGRITY;
	err = gk5e->derive_key(gk5e, param->base_key, &Ki,
			       &usage, GFP_KERNEL);
	KUNIT_ASSERT_EQ(test, err, 0);

	ahash_tfm = crypto_alloc_ahash(gk5e->cksum_name, 0, CRYPTO_ALG_ASYNC);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, ahash_tfm);
	err = crypto_ahash_setkey(ahash_tfm, Ki.data, Ki.len);
	KUNIT_ASSERT_EQ(test, err, 0);

	/* Act */
	err = krb5_cbc_cts_encrypt(cts_tfm, cbc_tfm, 0, &buf, NULL, NULL, 0);
	KUNIT_ASSERT_EQ(test, err, 0);
	err = krb5_etm_checksum(cts_tfm, ahash_tfm, &buf, 0, &checksum);
	KUNIT_ASSERT_EQ(test, err, 0);

	/* Assert */
	KUNIT_EXPECT_EQ_MSG(test,
			    param->expected_result->len, buf.len,
			    "ciphertext length mismatch");
	KUNIT_EXPECT_EQ_MSG(test,
			    memcmp(param->expected_result->data,
				   buf.head[0].iov_base,
				   param->expected_result->len), 0,
			    "ciphertext mismatch");
	KUNIT_EXPECT_EQ_MSG(test, memcmp(param->expected_hmac->data,
					 checksum.data,
					 checksum.len), 0,
			    "HMAC mismatch");

	crypto_free_ahash(ahash_tfm);
	crypto_free_sync_skcipher(cts_tfm);
	crypto_free_sync_skcipher(cbc_tfm);
}

static struct kunit_case rfc8009_test_cases[] = {
	{
		.name			= "RFC 8009 key derivation",
		.run_case		= kdf_case,
		.generate_params	= rfc8009_kdf_gen_params,
	},
	{
		.name			= "RFC 8009 checksum",
		.run_case		= checksum_case,
		.generate_params	= rfc8009_checksum_gen_params,
	},
	{
		.name			= "RFC 8009 encryption",
		.run_case		= rfc8009_encrypt_case,
		.generate_params	= rfc8009_encrypt_gen_params,
	},
	{}
};

static struct kunit_suite rfc8009_suite = {
	.name			= "RFC 8009 suite",
	.test_cases		= rfc8009_test_cases,
};

/*
 * Encryption self-tests
 */

DEFINE_STR_XDR_NETOBJ(encrypt_selftest_plaintext,
		      "This is the plaintext for the encryption self-test.");

static const struct gss_krb5_test_param encrypt_selftest_params[] = {
	{
		.desc			= "aes128-cts-hmac-sha1-96 encryption self-test",
		.enctype		= ENCTYPE_AES128_CTS_HMAC_SHA1_96,
		.Ke			= &rfc3962_encryption_key,
		.plaintext		= &encrypt_selftest_plaintext,
	},
	{
		.desc			= "aes256-cts-hmac-sha1-96 encryption self-test",
		.enctype		= ENCTYPE_AES256_CTS_HMAC_SHA1_96,
		.Ke			= &rfc3962_encryption_key,
		.plaintext		= &encrypt_selftest_plaintext,
	},
	{
		.desc			= "camellia128-cts-cmac encryption self-test",
		.enctype		= ENCTYPE_CAMELLIA128_CTS_CMAC,
		.Ke			= &camellia128_cts_cmac_Ke,
		.plaintext		= &encrypt_selftest_plaintext,
	},
	{
		.desc			= "camellia256-cts-cmac encryption self-test",
		.enctype		= ENCTYPE_CAMELLIA256_CTS_CMAC,
		.Ke			= &camellia256_cts_cmac_Ke,
		.plaintext		= &encrypt_selftest_plaintext,
	},
	{
		.desc			= "aes128-cts-hmac-sha256-128 encryption self-test",
		.enctype		= ENCTYPE_AES128_CTS_HMAC_SHA256_128,
		.Ke			= &aes128_cts_hmac_sha256_128_Ke,
		.plaintext		= &encrypt_selftest_plaintext,
	},
	{
		.desc			= "aes256-cts-hmac-sha384-192 encryption self-test",
		.enctype		= ENCTYPE_AES256_CTS_HMAC_SHA384_192,
		.Ke			= &aes256_cts_hmac_sha384_192_Ke,
		.plaintext		= &encrypt_selftest_plaintext,
	},
};

/* Creates the function encrypt_selftest_gen_params */
KUNIT_ARRAY_PARAM(encrypt_selftest, encrypt_selftest_params,
		  gss_krb5_get_desc);

/*
 * Encrypt and decrypt plaintext, and ensure the input plaintext
 * matches the output plaintext. A confounder is not added in this
 * case.
 */
static void encrypt_selftest_case(struct kunit *test)
{
	const struct gss_krb5_test_param *param = test->param_value;
	struct crypto_sync_skcipher *cts_tfm, *cbc_tfm;
	const struct gss_krb5_enctype *gk5e;
	struct xdr_buf buf;
	void *text;
	int err;

	/* Arrange */
	gk5e = gss_krb5_lookup_enctype(param->enctype);
	if (!gk5e)
		kunit_skip(test, "Encryption type is not available");

	cbc_tfm = crypto_alloc_sync_skcipher(gk5e->aux_cipher, 0, 0);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, cbc_tfm);
	err = crypto_sync_skcipher_setkey(cbc_tfm, param->Ke->data, param->Ke->len);
	KUNIT_ASSERT_EQ(test, err, 0);

	cts_tfm = crypto_alloc_sync_skcipher(gk5e->encrypt_name, 0, 0);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, cts_tfm);
	err = crypto_sync_skcipher_setkey(cts_tfm, param->Ke->data, param->Ke->len);
	KUNIT_ASSERT_EQ(test, err, 0);

	text = kunit_kzalloc(test, roundup(param->plaintext->len,
					   crypto_sync_skcipher_blocksize(cbc_tfm)),
			     GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, text);

	memcpy(text, param->plaintext->data, param->plaintext->len);
	memset(&buf, 0, sizeof(buf));
	buf.head[0].iov_base = text;
	buf.head[0].iov_len = param->plaintext->len;
	buf.len = buf.head[0].iov_len;

	/* Act */
	err = krb5_cbc_cts_encrypt(cts_tfm, cbc_tfm, 0, &buf, NULL, NULL, 0);
	KUNIT_ASSERT_EQ(test, err, 0);
	err = krb5_cbc_cts_decrypt(cts_tfm, cbc_tfm, 0, &buf);
	KUNIT_ASSERT_EQ(test, err, 0);

	/* Assert */
	KUNIT_EXPECT_EQ_MSG(test,
			    param->plaintext->len, buf.len,
			    "length mismatch");
	KUNIT_EXPECT_EQ_MSG(test,
			    memcmp(param->plaintext->data,
				   buf.head[0].iov_base, buf.len), 0,
			    "plaintext mismatch");

	crypto_free_sync_skcipher(cts_tfm);
	crypto_free_sync_skcipher(cbc_tfm);
}

static struct kunit_case encryption_test_cases[] = {
	{
		.name			= "Encryption self-tests",
		.run_case		= encrypt_selftest_case,
		.generate_params	= encrypt_selftest_gen_params,
	},
	{}
};

static struct kunit_suite encryption_test_suite = {
	.name			= "Encryption test suite",
	.test_cases		= encryption_test_cases,
};

kunit_test_suites(&rfc3961_suite,
		  &rfc3962_suite,
		  &rfc6803_suite,
		  &rfc8009_suite,
		  &encryption_test_suite);

MODULE_DESCRIPTION("Test RPCSEC GSS Kerberos 5 functions");
MODULE_LICENSE("GPL");
