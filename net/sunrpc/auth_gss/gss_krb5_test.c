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

#include <linux/sunrpc/xdr.h>
#include <linux/sunrpc/gss_krb5.h>

#include "gss_krb5_internal.h"

MODULE_IMPORT_NS(EXPORTED_FOR_KUNIT_TESTING);

struct gss_krb5_test_param {
	const char			*desc;
	u32				nfold;
	const struct xdr_netobj         *plaintext;
	const struct xdr_netobj		*expected_result;
};

static inline void gss_krb5_get_desc(const struct gss_krb5_test_param *param,
				     char *desc)
{
	strscpy(desc, param->desc, KUNIT_PARAM_DESC_SIZE);
}

#define DEFINE_HEX_XDR_NETOBJ(name, hex_array...)		\
	static const u8 name ## _data[] = { hex_array };	\
	static const struct xdr_netobj name = {			\
		.data	= (u8 *)name##_data,			\
		.len	= sizeof(name##_data),			\
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

static struct kunit_case rfc3961_test_cases[] = {
	{
		.name			= "RFC 3961 n-fold",
		.run_case		= rfc3961_nfold_case,
		.generate_params	= rfc3961_nfold_gen_params,
	},
};

static struct kunit_suite rfc3961_suite = {
	.name			= "RFC 3961 tests",
	.test_cases		= rfc3961_test_cases,
};

kunit_test_suites(&rfc3961_suite);

MODULE_DESCRIPTION("Test RPCSEC GSS Kerberos 5 functions");
MODULE_LICENSE("GPL");
