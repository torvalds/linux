// SPDX-License-Identifier: GPL-2.0
#include "util/debug.h"
#include "util/expr.h"
#include "tests.h"
#include <stdlib.h>
#include <string.h>
#include <linux/zalloc.h>

static int test(struct expr_parse_ctx *ctx, const char *e, double val2)
{
	double val;

	if (expr__parse(&val, ctx, e, 1))
		TEST_ASSERT_VAL("parse test failed", 0);
	TEST_ASSERT_VAL("unexpected value", val == val2);
	return 0;
}

int test__expr(struct test *t __maybe_unused, int subtest __maybe_unused)
{
	const char *p;
	double val, *val_ptr;
	int ret;
	struct expr_parse_ctx ctx;

	expr__ctx_init(&ctx);
	expr__add_id(&ctx, strdup("FOO"), 1);
	expr__add_id(&ctx, strdup("BAR"), 2);

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
	ret |= test(&ctx, "1.1 + 2.1", 3.2);
	ret |= test(&ctx, ".1 + 2.", 2.1);

	if (ret)
		return ret;

	p = "FOO/0";
	ret = expr__parse(&val, &ctx, p, 1);
	TEST_ASSERT_VAL("division by zero", ret == -1);

	p = "BAR/";
	ret = expr__parse(&val, &ctx, p, 1);
	TEST_ASSERT_VAL("missing operand", ret == -1);

	expr__ctx_clear(&ctx);
	TEST_ASSERT_VAL("find other",
			expr__find_other("FOO + BAR + BAZ + BOZO", "FOO",
					 &ctx, 1) == 0);
	TEST_ASSERT_VAL("find other", hashmap__size(&ctx.ids) == 3);
	TEST_ASSERT_VAL("find other", hashmap__find(&ctx.ids, "BAR",
						    (void **)&val_ptr));
	TEST_ASSERT_VAL("find other", hashmap__find(&ctx.ids, "BAZ",
						    (void **)&val_ptr));
	TEST_ASSERT_VAL("find other", hashmap__find(&ctx.ids, "BOZO",
						    (void **)&val_ptr));

	expr__ctx_clear(&ctx);
	TEST_ASSERT_VAL("find other",
			expr__find_other("EVENT1\\,param\\=?@ + EVENT2\\,param\\=?@",
					 NULL, &ctx, 3) == 0);
	TEST_ASSERT_VAL("find other", hashmap__size(&ctx.ids) == 2);
	TEST_ASSERT_VAL("find other", hashmap__find(&ctx.ids, "EVENT1,param=3/",
						    (void **)&val_ptr));
	TEST_ASSERT_VAL("find other", hashmap__find(&ctx.ids, "EVENT2,param=3/",
						    (void **)&val_ptr));

	expr__ctx_clear(&ctx);

	return 0;
}
