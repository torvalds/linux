// SPDX-License-Identifier: GPL-2.0
#include "util/cputopo.h"
#include "util/debug.h"
#include "util/expr.h"
#include "util/hashmap.h"
#include "util/header.h"
#include "util/smt.h"
#include "tests.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <string2.h>
#include <linux/zalloc.h>

static int test_ids_union(void)
{
	struct hashmap *ids1, *ids2;

	/* Empty union. */
	ids1 = ids__new();
	TEST_ASSERT_VAL("ids__new", ids1);
	ids2 = ids__new();
	TEST_ASSERT_VAL("ids__new", ids2);

	ids1 = ids__union(ids1, ids2);
	TEST_ASSERT_EQUAL("union", (int)hashmap__size(ids1), 0);

	/* Union {foo, bar} against {}. */
	ids2 = ids__new();
	TEST_ASSERT_VAL("ids__new", ids2);

	TEST_ASSERT_EQUAL("ids__insert", ids__insert(ids1, strdup("foo")), 0);
	TEST_ASSERT_EQUAL("ids__insert", ids__insert(ids1, strdup("bar")), 0);

	ids1 = ids__union(ids1, ids2);
	TEST_ASSERT_EQUAL("union", (int)hashmap__size(ids1), 2);

	/* Union {foo, bar} against {foo}. */
	ids2 = ids__new();
	TEST_ASSERT_VAL("ids__new", ids2);
	TEST_ASSERT_EQUAL("ids__insert", ids__insert(ids2, strdup("foo")), 0);

	ids1 = ids__union(ids1, ids2);
	TEST_ASSERT_EQUAL("union", (int)hashmap__size(ids1), 2);

	/* Union {foo, bar} against {bar,baz}. */
	ids2 = ids__new();
	TEST_ASSERT_VAL("ids__new", ids2);
	TEST_ASSERT_EQUAL("ids__insert", ids__insert(ids2, strdup("bar")), 0);
	TEST_ASSERT_EQUAL("ids__insert", ids__insert(ids2, strdup("baz")), 0);

	ids1 = ids__union(ids1, ids2);
	TEST_ASSERT_EQUAL("union", (int)hashmap__size(ids1), 3);

	ids__free(ids1);

	return 0;
}

static int test(struct expr_parse_ctx *ctx, const char *e, double val2)
{
	double val;

	if (expr__parse(&val, ctx, e))
		TEST_ASSERT_VAL("parse test failed", 0);
	TEST_ASSERT_VAL("unexpected value", val == val2);
	return 0;
}

static int test__expr(struct test_suite *t __maybe_unused, int subtest __maybe_unused)
{
	struct expr_id_data *val_ptr;
	const char *p;
	double val, num_cpus_online, num_cpus, num_cores, num_dies, num_packages;
	int ret;
	struct expr_parse_ctx *ctx;
	bool is_intel = false;
	char strcmp_cpuid_buf[256];
	struct perf_pmu *pmu = perf_pmus__find_core_pmu();
	char *cpuid = perf_pmu__getcpuid(pmu);
	char *escaped_cpuid1, *escaped_cpuid2;

	TEST_ASSERT_VAL("get_cpuid", cpuid);
	is_intel = strstr(cpuid, "Intel") != NULL;

	TEST_ASSERT_EQUAL("ids_union", test_ids_union(), 0);

	ctx = expr__ctx_new();
	TEST_ASSERT_VAL("expr__ctx_new", ctx);
	expr__add_id_val(ctx, strdup("FOO"), 1);
	expr__add_id_val(ctx, strdup("BAR"), 2);

	ret = test(ctx, "1+1", 2);
	ret |= test(ctx, "FOO+BAR", 3);
	ret |= test(ctx, "(BAR/2)%2", 1);
	ret |= test(ctx, "1 - -4",  5);
	ret |= test(ctx, "(FOO-1)*2 + (BAR/2)%2 - -4",  5);
	ret |= test(ctx, "1-1 | 1", 1);
	ret |= test(ctx, "1-1 & 1", 0);
	ret |= test(ctx, "min(1,2) + 1", 2);
	ret |= test(ctx, "max(1,2) + 1", 3);
	ret |= test(ctx, "1+1 if 3*4 else 0", 2);
	ret |= test(ctx, "100 if 1 else 200 if 1 else 300", 100);
	ret |= test(ctx, "100 if 0 else 200 if 1 else 300", 200);
	ret |= test(ctx, "100 if 1 else 200 if 0 else 300", 100);
	ret |= test(ctx, "100 if 0 else 200 if 0 else 300", 300);
	ret |= test(ctx, "1.1 + 2.1", 3.2);
	ret |= test(ctx, ".1 + 2.", 2.1);
	ret |= test(ctx, "d_ratio(1, 2)", 0.5);
	ret |= test(ctx, "d_ratio(2.5, 0)", 0);
	ret |= test(ctx, "1.1 < 2.2", 1);
	ret |= test(ctx, "2.2 > 1.1", 1);
	ret |= test(ctx, "1.1 < 1.1", 0);
	ret |= test(ctx, "2.2 > 2.2", 0);
	ret |= test(ctx, "2.2 < 1.1", 0);
	ret |= test(ctx, "1.1 > 2.2", 0);
	ret |= test(ctx, "1.1e10 < 1.1e100", 1);
	ret |= test(ctx, "1.1e2 > 1.1e-2", 1);

	if (ret) {
		expr__ctx_free(ctx);
		return ret;
	}

	p = "FOO/0";
	ret = expr__parse(&val, ctx, p);
	TEST_ASSERT_VAL("division by zero", ret == 0);
	TEST_ASSERT_VAL("division by zero", isnan(val));

	p = "BAR/";
	ret = expr__parse(&val, ctx, p);
	TEST_ASSERT_VAL("missing operand", ret == -1);

	expr__ctx_clear(ctx);
	TEST_ASSERT_VAL("find ids",
			expr__find_ids("FOO + BAR + BAZ + BOZO", "FOO",
					ctx) == 0);
	TEST_ASSERT_VAL("find ids", hashmap__size(ctx->ids) == 3);
	TEST_ASSERT_VAL("find ids", hashmap__find(ctx->ids, "BAR", &val_ptr));
	TEST_ASSERT_VAL("find ids", hashmap__find(ctx->ids, "BAZ", &val_ptr));
	TEST_ASSERT_VAL("find ids", hashmap__find(ctx->ids, "BOZO", &val_ptr));

	expr__ctx_clear(ctx);
	ctx->sctx.runtime = 3;
	TEST_ASSERT_VAL("find ids",
			expr__find_ids("EVENT1\\,param\\=?@ + EVENT2\\,param\\=?@",
					NULL, ctx) == 0);
	TEST_ASSERT_VAL("find ids", hashmap__size(ctx->ids) == 2);
	TEST_ASSERT_VAL("find ids", hashmap__find(ctx->ids, "EVENT1,param=3@", &val_ptr));
	TEST_ASSERT_VAL("find ids", hashmap__find(ctx->ids, "EVENT2,param=3@", &val_ptr));

	expr__ctx_clear(ctx);
	TEST_ASSERT_VAL("find ids",
			expr__find_ids("dash\\-event1 - dash\\-event2",
				       NULL, ctx) == 0);
	TEST_ASSERT_VAL("find ids", hashmap__size(ctx->ids) == 2);
	TEST_ASSERT_VAL("find ids", hashmap__find(ctx->ids, "dash-event1", &val_ptr));
	TEST_ASSERT_VAL("find ids", hashmap__find(ctx->ids, "dash-event2", &val_ptr));

	/* Only EVENT1 or EVENT2 need be measured depending on the value of smt_on. */
	{
		bool smton = smt_on();
		bool corewide = core_wide(/*system_wide=*/false,
					  /*user_requested_cpus=*/false);

		expr__ctx_clear(ctx);
		TEST_ASSERT_VAL("find ids",
				expr__find_ids("EVENT1 if #smt_on else EVENT2",
					NULL, ctx) == 0);
		TEST_ASSERT_VAL("find ids", hashmap__size(ctx->ids) == 1);
		TEST_ASSERT_VAL("find ids", hashmap__find(ctx->ids,
							  smton ? "EVENT1" : "EVENT2",
							  &val_ptr));

		expr__ctx_clear(ctx);
		TEST_ASSERT_VAL("find ids",
				expr__find_ids("EVENT1 if #core_wide else EVENT2",
					NULL, ctx) == 0);
		TEST_ASSERT_VAL("find ids", hashmap__size(ctx->ids) == 1);
		TEST_ASSERT_VAL("find ids", hashmap__find(ctx->ids,
							  corewide ? "EVENT1" : "EVENT2",
							  &val_ptr));

	}
	/* The expression is a constant 1.0 without needing to evaluate EVENT1. */
	expr__ctx_clear(ctx);
	TEST_ASSERT_VAL("find ids",
			expr__find_ids("1.0 if EVENT1 > 100.0 else 1.0",
			NULL, ctx) == 0);
	TEST_ASSERT_VAL("find ids", hashmap__size(ctx->ids) == 0);

	/* The expression is a constant 0.0 without needing to evaluate EVENT1. */
	expr__ctx_clear(ctx);
	TEST_ASSERT_VAL("find ids",
			expr__find_ids("0 & EVENT1 > 0", NULL, ctx) == 0);
	TEST_ASSERT_VAL("find ids", hashmap__size(ctx->ids) == 0);
	expr__ctx_clear(ctx);
	TEST_ASSERT_VAL("find ids",
			expr__find_ids("EVENT1 > 0 & 0", NULL, ctx) == 0);
	TEST_ASSERT_VAL("find ids", hashmap__size(ctx->ids) == 0);
	expr__ctx_clear(ctx);
	TEST_ASSERT_VAL("find ids",
			expr__find_ids("1 & EVENT1 > 0", NULL, ctx) == 0);
	TEST_ASSERT_VAL("find ids", hashmap__size(ctx->ids) == 1);
	TEST_ASSERT_VAL("find ids", hashmap__find(ctx->ids, "EVENT1", &val_ptr));
	expr__ctx_clear(ctx);
	TEST_ASSERT_VAL("find ids",
			expr__find_ids("EVENT1 > 0 & 1", NULL, ctx) == 0);
	TEST_ASSERT_VAL("find ids", hashmap__size(ctx->ids) == 1);
	TEST_ASSERT_VAL("find ids", hashmap__find(ctx->ids, "EVENT1", &val_ptr));

	/* The expression is a constant 1.0 without needing to evaluate EVENT1. */
	expr__ctx_clear(ctx);
	TEST_ASSERT_VAL("find ids",
			expr__find_ids("1 | EVENT1 > 0", NULL, ctx) == 0);
	TEST_ASSERT_VAL("find ids", hashmap__size(ctx->ids) == 0);
	expr__ctx_clear(ctx);
	TEST_ASSERT_VAL("find ids",
			expr__find_ids("EVENT1 > 0 | 1", NULL, ctx) == 0);
	TEST_ASSERT_VAL("find ids", hashmap__size(ctx->ids) == 0);
	expr__ctx_clear(ctx);
	TEST_ASSERT_VAL("find ids",
			expr__find_ids("0 | EVENT1 > 0", NULL, ctx) == 0);
	TEST_ASSERT_VAL("find ids", hashmap__size(ctx->ids) == 1);
	TEST_ASSERT_VAL("find ids", hashmap__find(ctx->ids, "EVENT1", &val_ptr));
	expr__ctx_clear(ctx);
	TEST_ASSERT_VAL("find ids",
			expr__find_ids("EVENT1 > 0 | 0", NULL, ctx) == 0);
	TEST_ASSERT_VAL("find ids", hashmap__size(ctx->ids) == 1);
	TEST_ASSERT_VAL("find ids", hashmap__find(ctx->ids, "EVENT1", &val_ptr));

	/* Test toplogy constants appear well ordered. */
	expr__ctx_clear(ctx);
	TEST_ASSERT_VAL("#num_cpus_online",
			expr__parse(&num_cpus_online, ctx, "#num_cpus_online") == 0);
	TEST_ASSERT_VAL("#num_cpus", expr__parse(&num_cpus, ctx, "#num_cpus") == 0);
	TEST_ASSERT_VAL("#num_cpus >= #num_cpus_online", num_cpus >= num_cpus_online);
	TEST_ASSERT_VAL("#num_cores", expr__parse(&num_cores, ctx, "#num_cores") == 0);
	TEST_ASSERT_VAL("#num_cpus >= #num_cores", num_cpus >= num_cores);
	TEST_ASSERT_VAL("#num_dies", expr__parse(&num_dies, ctx, "#num_dies") == 0);
	TEST_ASSERT_VAL("#num_cores >= #num_dies", num_cores >= num_dies);
	TEST_ASSERT_VAL("#num_packages", expr__parse(&num_packages, ctx, "#num_packages") == 0);

	if (num_dies) // Some platforms do not have CPU die support, for example s390
		TEST_ASSERT_VAL("#num_dies >= #num_packages", num_dies >= num_packages);

	TEST_ASSERT_VAL("#system_tsc_freq", expr__parse(&val, ctx, "#system_tsc_freq") == 0);
	if (is_intel)
		TEST_ASSERT_VAL("#system_tsc_freq > 0", val > 0);
	else
		TEST_ASSERT_VAL("#system_tsc_freq == 0", fpclassify(val) == FP_ZERO);

	/*
	 * Source count returns the number of events aggregating in a leader
	 * event including the leader. Check parsing yields an id.
	 */
	expr__ctx_clear(ctx);
	TEST_ASSERT_VAL("source count",
			expr__find_ids("source_count(EVENT1)",
			NULL, ctx) == 0);
	TEST_ASSERT_VAL("source count", hashmap__size(ctx->ids) == 1);
	TEST_ASSERT_VAL("source count", hashmap__find(ctx->ids, "EVENT1", &val_ptr));


	/* Test no cpuid match */
	ret = test(ctx, "strcmp_cpuid_str(0x0)", 0);

	/*
	 * Test cpuid match with current cpuid. Special chars have to be
	 * escaped.
	 */
	escaped_cpuid1 = strreplace_chars('-', cpuid, "\\-");
	free(cpuid);
	escaped_cpuid2 = strreplace_chars(',', escaped_cpuid1, "\\,");
	free(escaped_cpuid1);
	escaped_cpuid1 = strreplace_chars('=', escaped_cpuid2, "\\=");
	free(escaped_cpuid2);
	scnprintf(strcmp_cpuid_buf, sizeof(strcmp_cpuid_buf),
		  "strcmp_cpuid_str(%s)", escaped_cpuid1);
	free(escaped_cpuid1);
	ret |= test(ctx, strcmp_cpuid_buf, 1);

	/* has_event returns 1 when an event exists. */
	expr__add_id_val(ctx, strdup("cycles"), 2);
	ret |= test(ctx, "has_event(cycles)", 1);

	expr__ctx_free(ctx);

	return ret;
}

DEFINE_SUITE("Simple expression parser", expr);
