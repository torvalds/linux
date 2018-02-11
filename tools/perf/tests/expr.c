// SPDX-License-Identifier: GPL-2.0
#include "util/debug.h"
#include "util/expr.h"
#include "tests.h"
#include <stdlib.h>

static int test(struct parse_ctx *ctx, const char *e, double val2)
{
	double val;

	if (expr__parse(&val, ctx, &e))
		TEST_ASSERT_VAL("parse test failed", 0);
	TEST_ASSERT_VAL("unexpected value", val == val2);
	return 0;
}

int test__expr(struct test *t __maybe_unused, int subtest __maybe_unused)
{
	const char *p;
	const char **other;
	double val;
	int ret;
	struct parse_ctx ctx;
	int num_other;

	expr__ctx_init(&ctx);
	expr__add_id(&ctx, "FOO", 1);
	expr__add_id(&ctx, "BAR", 2);

	ret = test(&ctx, "1+1", 2);
	ret |= test(&ctx, "FOO+BAR", 3);
	ret |= test(&ctx, "(BAR/2)%2", 1);
	ret |= test(&ctx, "1 - -4",  5);
	ret |= test(&ctx, "(FOO-1)*2 + (BAR/2)%2 - -4",  5);
	ret |= test(&ctx, "1-1 | 1", 1);
	ret |= test(&ctx, "1-1 & 1", 0);
	ret |= test(&ctx, "min(1,2) + 1", 2);
	ret |= test(&ctx, "max(1,2) + 1", 3);
	ret |= test(&ctx, "1+1 if 3*4 else 0", 2);

	if (ret)
		return ret;

	p = "FOO/0";
	ret = expr__parse(&val, &ctx, &p);
	TEST_ASSERT_VAL("division by zero", ret == 1);

	p = "BAR/";
	ret = expr__parse(&val, &ctx, &p);
	TEST_ASSERT_VAL("missing operand", ret == 1);

	TEST_ASSERT_VAL("find other",
			expr__find_other("FOO + BAR + BAZ + BOZO", "FOO", &other, &num_other) == 0);
	TEST_ASSERT_VAL("find other", num_other == 3);
	TEST_ASSERT_VAL("find other", !strcmp(other[0], "BAR"));
	TEST_ASSERT_VAL("find other", !strcmp(other[1], "BAZ"));
	TEST_ASSERT_VAL("find other", !strcmp(other[2], "BOZO"));
	TEST_ASSERT_VAL("find other", other[3] == NULL);
	free((void *)other);

	return 0;
}
