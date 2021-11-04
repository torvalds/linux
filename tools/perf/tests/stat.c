// SPDX-License-Identifier: GPL-2.0
#include <linux/compiler.h>
#include "event.h"
#include "tests.h"
#include "stat.h"
#include "counts.h"
#include "debug.h"
#include "util/synthetic-events.h"

static bool has_term(struct perf_record_stat_config *config,
		     u64 tag, u64 val)
{
	unsigned i;

	for (i = 0; i < config->nr; i++) {
		if ((config->data[i].tag == tag) &&
		    (config->data[i].val == val))
			return true;
	}

	return false;
}

static int process_stat_config_event(struct perf_tool *tool __maybe_unused,
				     union perf_event *event,
				     struct perf_sample *sample __maybe_unused,
				     struct machine *machine __maybe_unused)
{
	struct perf_record_stat_config *config = &event->stat_config;
	struct perf_stat_config stat_config;

#define HAS(term, val) \
	has_term(config, PERF_STAT_CONFIG_TERM__##term, val)

	TEST_ASSERT_VAL("wrong nr",        config->nr == PERF_STAT_CONFIG_TERM__MAX);
	TEST_ASSERT_VAL("wrong aggr_mode", HAS(AGGR_MODE, AGGR_CORE));
	TEST_ASSERT_VAL("wrong scale",     HAS(SCALE, 1));
	TEST_ASSERT_VAL("wrong interval",  HAS(INTERVAL, 1));

#undef HAS

	perf_event__read_stat_config(&stat_config, config);

	TEST_ASSERT_VAL("wrong aggr_mode", stat_config.aggr_mode == AGGR_CORE);
	TEST_ASSERT_VAL("wrong scale",     stat_config.scale == 1);
	TEST_ASSERT_VAL("wrong interval",  stat_config.interval == 1);
	return 0;
}

static int test__synthesize_stat_config(struct test_suite *test __maybe_unused,
					int subtest __maybe_unused)
{
	struct perf_stat_config stat_config = {
		.aggr_mode	= AGGR_CORE,
		.scale		= 1,
		.interval	= 1,
	};

	TEST_ASSERT_VAL("failed to synthesize stat_config",
		!perf_event__synthesize_stat_config(NULL, &stat_config, process_stat_config_event, NULL));

	return 0;
}

static int process_stat_event(struct perf_tool *tool __maybe_unused,
			      union perf_event *event,
			      struct perf_sample *sample __maybe_unused,
			      struct machine *machine __maybe_unused)
{
	struct perf_record_stat *st = &event->stat;

	TEST_ASSERT_VAL("wrong cpu",    st->cpu    == 1);
	TEST_ASSERT_VAL("wrong thread", st->thread == 2);
	TEST_ASSERT_VAL("wrong id",     st->id     == 3);
	TEST_ASSERT_VAL("wrong val",    st->val    == 100);
	TEST_ASSERT_VAL("wrong run",    st->ena    == 200);
	TEST_ASSERT_VAL("wrong ena",    st->run    == 300);
	return 0;
}

static int test__synthesize_stat(struct test_suite *test __maybe_unused, int subtest __maybe_unused)
{
	struct perf_counts_values count;

	count.val = 100;
	count.ena = 200;
	count.run = 300;

	TEST_ASSERT_VAL("failed to synthesize stat_config",
		!perf_event__synthesize_stat(NULL, 1, 2, 3, &count, process_stat_event, NULL));

	return 0;
}

static int process_stat_round_event(struct perf_tool *tool __maybe_unused,
				    union perf_event *event,
				    struct perf_sample *sample __maybe_unused,
				    struct machine *machine __maybe_unused)
{
	struct perf_record_stat_round *stat_round = &event->stat_round;

	TEST_ASSERT_VAL("wrong time", stat_round->time == 0xdeadbeef);
	TEST_ASSERT_VAL("wrong type", stat_round->type == PERF_STAT_ROUND_TYPE__INTERVAL);
	return 0;
}

static int test__synthesize_stat_round(struct test_suite *test __maybe_unused, int subtest __maybe_unused)
{
	TEST_ASSERT_VAL("failed to synthesize stat_config",
		!perf_event__synthesize_stat_round(NULL, 0xdeadbeef, PERF_STAT_ROUND_TYPE__INTERVAL,
						   process_stat_round_event, NULL));

	return 0;
}

DEFINE_SUITE("Synthesize stat config", synthesize_stat_config);
DEFINE_SUITE("Synthesize stat", synthesize_stat);
DEFINE_SUITE("Synthesize stat round", synthesize_stat_round);
