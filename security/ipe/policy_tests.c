// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2020-2024 Microsoft Corporation. All rights reserved.
 */

#include <linux/slab.h>
#include <linux/types.h>
#include <linux/list.h>
#include <kunit/test.h>
#include "policy.h"
struct policy_case {
	const char *const policy;
	int errno;
	const char *const desc;
};

static const struct policy_case policy_cases[] = {
	{
		"policy_name=allowall policy_version=0.0.0\n"
		"DEFAULT action=ALLOW",
		0,
		"basic",
	},
	{
		"policy_name=trailing_comment policy_version=152.0.0 #This is comment\n"
		"DEFAULT action=ALLOW",
		0,
		"trailing comment",
	},
	{
		"policy_name=allowallnewline policy_version=0.2.0\n"
		"DEFAULT action=ALLOW\n"
		"\n",
		0,
		"trailing newline",
	},
	{
		"policy_name=carriagereturnlinefeed policy_version=0.0.1\n"
		"DEFAULT action=ALLOW\n"
		"\r\n",
		0,
		"clrf newline",
	},
	{
		"policy_name=whitespace policy_version=0.0.0\n"
		"DEFAULT\taction=ALLOW\n"
		"     \t     DEFAULT \t    op=EXECUTE      action=DENY\n"
		"op=EXECUTE boot_verified=TRUE action=ALLOW\n"
		"# this is a\tcomment\t\t\t\t\n"
		"DEFAULT \t op=KMODULE\t\t\t  action=DENY\r\n"
		"op=KMODULE boot_verified=TRUE action=ALLOW\n",
		0,
		"various whitespaces and nested default",
	},
	{
		"policy_name=boot_verified policy_version=-1236.0.0\n"
		"DEFAULT\taction=ALLOW\n",
		-EINVAL,
		"negative version",
	},
	{
		"policy_name=$@!*&^%%\\:;{}() policy_version=0.0.0\n"
		"DEFAULT action=ALLOW",
		0,
		"special characters",
	},
	{
		"policy_name=test policy_version=999999.0.0\n"
		"DEFAULT action=ALLOW",
		-ERANGE,
		"overflow version",
	},
	{
		"policy_name=test policy_version=255.0\n"
		"DEFAULT action=ALLOW",
		-EBADMSG,
		"incomplete version",
	},
	{
		"policy_name=test policy_version=111.0.0.0\n"
		"DEFAULT action=ALLOW",
		-EBADMSG,
		"extra version",
	},
	{
		"",
		-EBADMSG,
		"0-length policy",
	},
	{
		"policy_name=test\0policy_version=0.0.0\n"
		"DEFAULT action=ALLOW",
		-EBADMSG,
		"random null in header",
	},
	{
		"policy_name=test policy_version=0.0.0\n"
		"\0DEFAULT action=ALLOW",
		-EBADMSG,
		"incomplete policy from NULL",
	},
	{
		"policy_name=test policy_version=0.0.0\n"
		"DEFAULT action=DENY\n\0"
		"op=EXECUTE dmverity_signature=TRUE action=ALLOW\n",
		0,
		"NULL truncates policy",
	},
	{
		"policy_name=test policy_version=0.0.0\n"
		"DEFAULT action=ALLOW\n"
		"op=EXECUTE dmverity_signature=abc action=ALLOW",
		-EBADMSG,
		"invalid property type",
	},
	{
		"DEFAULT action=ALLOW",
		-EBADMSG,
		"missing policy header",
	},
	{
		"policy_name=test policy_version=0.0.0\n",
		-EBADMSG,
		"missing default definition",
	},
	{
		"policy_name=test policy_version=0.0.0\n"
		"DEFAULT action=ALLOW\n"
		"dmverity_signature=TRUE op=EXECUTE action=ALLOW",
		-EBADMSG,
		"invalid rule ordering"
	},
	{
		"policy_name=test policy_version=0.0.0\n"
		"DEFAULT action=ALLOW\n"
		"action=ALLOW op=EXECUTE dmverity_signature=TRUE",
		-EBADMSG,
		"invalid rule ordering (2)",
	},
	{
		"policy_name=test policy_version=0.0\n"
		"DEFAULT action=ALLOW\n"
		"op=EXECUTE dmverity_signature=TRUE action=ALLOW",
		-EBADMSG,
		"invalid version",
	},
	{
		"policy_name=test policy_version=0.0.0\n"
		"DEFAULT action=ALLOW\n"
		"op=UNKNOWN dmverity_signature=TRUE action=ALLOW",
		-EBADMSG,
		"unknown operation",
	},
	{
		"policy_name=asdvpolicy_version=0.0.0\n"
		"DEFAULT action=ALLOW\n",
		-EBADMSG,
		"missing space after policy name",
	},
	{
		"policy_name=test\xFF\xEF policy_version=0.0.0\n"
		"DEFAULT action=ALLOW\n"
		"op=EXECUTE dmverity_signature=TRUE action=ALLOW",
		0,
		"expanded ascii",
	},
	{
		"policy_name=test\xFF\xEF policy_version=0.0.0\n"
		"DEFAULT action=ALLOW\n"
		"op=EXECUTE dmverity_roothash=GOOD_DOG action=ALLOW",
		-EBADMSG,
		"invalid property value (2)",
	},
	{
		"policy_name=test policy_version=0.0.0\n"
		"policy_name=test policy_version=0.1.0\n"
		"DEFAULT action=ALLOW",
		-EBADMSG,
		"double header"
	},
	{
		"policy_name=test policy_version=0.0.0\n"
		"DEFAULT action=ALLOW\n"
		"DEFAULT action=ALLOW\n",
		-EBADMSG,
		"double default"
	},
	{
		"policy_name=test policy_version=0.0.0\n"
		"DEFAULT action=ALLOW\n"
		"DEFAULT op=EXECUTE action=DENY\n"
		"DEFAULT op=EXECUTE action=ALLOW\n",
		-EBADMSG,
		"double operation default"
	},
	{
		"policy_name=test policy_version=0.0.0\n"
		"DEFAULT action=ALLOW\n"
		"DEFAULT op=EXECUTE action=DEN\n",
		-EBADMSG,
		"invalid action value"
	},
	{
		"policy_name=test policy_version=0.0.0\n"
		"DEFAULT action=ALLOW\n"
		"DEFAULT op=EXECUTE action\n",
		-EBADMSG,
		"invalid action value (2)"
	},
	{
		"policy_name=test policy_version=0.0.0\n"
		"DEFAULT action=ALLOW\n"
		"UNKNOWN value=true\n",
		-EBADMSG,
		"unrecognized statement"
	},
	{
		"policy_name=test policy_version=0.0.0\n"
		"DEFAULT action=ALLOW\n"
		"op=EXECUTE dmverity_roothash=1c0d7ee1f8343b7fbe418378e8eb22c061d7dec7 action=DENY\n",
		-EBADMSG,
		"old-style digest"
	},
	{
		"policy_name=test policy_version=0.0.0\n"
		"DEFAULT action=ALLOW\n"
		"op=EXECUTE fsverity_digest=1c0d7ee1f8343b7fbe418378e8eb22c061d7dec7 action=DENY\n",
		-EBADMSG,
		"old-style digest"
	}
};

static void pol_to_desc(const struct policy_case *c, char *desc)
{
	strscpy(desc, c->desc, KUNIT_PARAM_DESC_SIZE);
}

KUNIT_ARRAY_PARAM(ipe_policies, policy_cases, pol_to_desc);

/**
 * ipe_parser_unsigned_test - Test the parser by passing unsigned policies.
 * @test: Supplies a pointer to a kunit structure.
 *
 * This is called by the kunit harness. This test does not check the correctness
 * of the policy, but ensures that errors are handled correctly.
 */
static void ipe_parser_unsigned_test(struct kunit *test)
{
	const struct policy_case *p = test->param_value;
	struct ipe_policy *pol;

	pol = ipe_new_policy(p->policy, strlen(p->policy), NULL, 0);

	if (p->errno) {
		KUNIT_EXPECT_EQ(test, PTR_ERR(pol), p->errno);
		return;
	}

	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, pol);
	KUNIT_EXPECT_NOT_ERR_OR_NULL(test, pol->parsed);
	KUNIT_EXPECT_STREQ(test, pol->text, p->policy);
	KUNIT_EXPECT_PTR_EQ(test, NULL, pol->pkcs7);
	KUNIT_EXPECT_EQ(test, 0, pol->pkcs7len);

	ipe_free_policy(pol);
}

/**
 * ipe_parser_widestring_test - Ensure parser fail on a wide string policy.
 * @test: Supplies a pointer to a kunit structure.
 *
 * This is called by the kunit harness.
 */
static void ipe_parser_widestring_test(struct kunit *test)
{
	const unsigned short policy[] = L"policy_name=Test policy_version=0.0.0\n"
					L"DEFAULT action=ALLOW";
	struct ipe_policy *pol = NULL;

	pol = ipe_new_policy((const char *)policy, (ARRAY_SIZE(policy) - 1) * 2, NULL, 0);
	KUNIT_EXPECT_TRUE(test, IS_ERR_OR_NULL(pol));

	ipe_free_policy(pol);
}

static struct kunit_case ipe_parser_test_cases[] = {
	KUNIT_CASE_PARAM(ipe_parser_unsigned_test, ipe_policies_gen_params),
	KUNIT_CASE(ipe_parser_widestring_test),
	{ }
};

static struct kunit_suite ipe_parser_test_suite = {
	.name = "ipe-parser",
	.test_cases = ipe_parser_test_cases,
};

kunit_test_suite(ipe_parser_test_suite);
