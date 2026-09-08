// SPDX-License-Identifier: GPL-2.0
#include <linux/module.h>

#include <kunit/test.h>

#include "trace.h"

struct parse_event_string_test_case {
	const char *input;

	const char *match, *sub, *event, *mod;
};

static const struct parse_event_string_test_case parse_event_string_test_cases[] = {
	{
		"",
		.match	= "",
	},
	{
		"*:event",
		.event	= "event",
	},
	{
		":event",
		.event	= "event",
	},
	{
		"sub:*",
		.sub	= "sub",
	},
	{
		"sub:",
		.sub	= "sub",
	},
	{
		"sub:event",
		.sub	= "sub",
		.event	= "event",
	},
	{
		"match",
		.match	= "match",
	},
	{
		":mod:module",
		.mod	= "module",
	},
	{
		"sub:event:mod:module",
		.sub	= "sub",
		.event	= "event",
		.mod	= "module",
	},
};

static void
parse_event_string_test_desc(const struct parse_event_string_test_case *params, char *desc)
{
	snprintf(desc, KUNIT_PARAM_DESC_SIZE, "'%s'", params->input);
}

KUNIT_ARRAY_PARAM(parse_event_string, parse_event_string_test_cases, parse_event_string_test_desc);

#define EXPECT_NULL_OR_STR_EQ(test, left, right)		\
({								\
	if ((left) != NULL)					\
		KUNIT_EXPECT_STREQ(test, (left), (right));	\
	else							\
		KUNIT_EXPECT_NULL(test, (right));		\
})

static void parse_event_string(struct kunit *test)
{
	const struct parse_event_string_test_case *params = test->param_value;
	char *input, *match, *sub, *event, *mod;

	input = kunit_kstrdup(test, params->input, GFP_KERNEL);
	if (!input)
		kunit_skip(test, "ENOMEM");

	ftrace_parse_event_string(input, &match, &sub, &event, &mod);

	EXPECT_NULL_OR_STR_EQ(test, match, params->match);
	EXPECT_NULL_OR_STR_EQ(test, sub, params->sub);
	EXPECT_NULL_OR_STR_EQ(test, event, params->event);
	EXPECT_NULL_OR_STR_EQ(test, mod, params->mod);
}

static struct kunit_case trace_events_test_cases[] = {
	KUNIT_CASE_PARAM(parse_event_string, parse_event_string_gen_params),
	{}
};

static struct kunit_suite trace_events_test_suite = {
	.name = "trace-events",
	.test_cases = trace_events_test_cases,
};
kunit_test_suite(trace_events_test_suite);

MODULE_DESCRIPTION("KUnit test for the event tracing implementation");
MODULE_LICENSE("GPL");
MODULE_IMPORT_NS("EXPORTED_FOR_KUNIT_TESTING");
